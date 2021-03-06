#include "PICForRemoteProcess.h"

#include <stdio.h>

/* 
   Everything in this file is position independant code that can be injected 
   and executed without relocation to any application 
*/

#pragma optimize("ts", on )  
#pragma strict_gs_check(push, off)   
#pragma auto_inline(off)
#pragma check_stack(off)
#pragma code_seg(push, ".p")

HMODULE GetProcAddressWithHash(_In_ DWORD dwModuleFunctionHash)
{
	PPEB PebAddress;
	PMY_PEB_LDR_DATA pLdr;
	PMY_LDR_DATA_TABLE_ENTRY pDataTableEntry;
	PVOID pModuleBase;
	PIMAGE_NT_HEADERS pNTHeader;
	DWORD dwExportDirRVA;
	PIMAGE_EXPORT_DIRECTORY pExportDir;
	PLIST_ENTRY pNextModule;
	DWORD dwNumFunctions;
	USHORT usOrdinalTableIndex;
	PDWORD pdwFunctionNameBase;
	PCSTR pFunctionName;
	UNICODE_STRING BaseDllName;
	DWORD dwModuleHash;
	DWORD dwFunctionHash;
	PCSTR pTempChar;
	DWORD i;

	PebAddress = READPEBADDR

	pLdr = (PMY_PEB_LDR_DATA)PebAddress->Ldr;
	pNextModule = pLdr->InLoadOrderModuleList.Flink;
	pDataTableEntry = (PMY_LDR_DATA_TABLE_ENTRY)pNextModule;

	while (pDataTableEntry->DllBase != NULL)	{
		dwModuleHash = 0;
		pModuleBase = pDataTableEntry->DllBase;
		BaseDllName = pDataTableEntry->BaseDllName;
		pNTHeader = (PIMAGE_NT_HEADERS)((ULONG_PTR)pModuleBase + ((PIMAGE_DOS_HEADER)pModuleBase)->e_lfanew);
		dwExportDirRVA = pNTHeader->OptionalHeader.DataDirectory[0].VirtualAddress;
		pDataTableEntry = (PMY_LDR_DATA_TABLE_ENTRY)pDataTableEntry->InLoadOrderLinks.Flink;

		if (dwExportDirRVA == 0)
			continue;

		for (i = 0; i < BaseDllName.MaximumLength; i++) {
			pTempChar = ((PCSTR)BaseDllName.Buffer + i);
			dwModuleHash = ROTR32(dwModuleHash, 13);
			if (*pTempChar >= 0x61)
				dwModuleHash += *pTempChar - 0x20;
			else
				dwModuleHash += *pTempChar;
		}

		pExportDir = (PIMAGE_EXPORT_DIRECTORY)((ULONG_PTR)pModuleBase + dwExportDirRVA);
		dwNumFunctions = pExportDir->NumberOfNames;
		pdwFunctionNameBase = (PDWORD)((PCHAR)pModuleBase + pExportDir->AddressOfNames);

		for (i = 0; i < dwNumFunctions; i++) {
			dwFunctionHash = 0;
			pFunctionName = (PCSTR)(*pdwFunctionNameBase + (ULONG_PTR)pModuleBase);
			pdwFunctionNameBase++;
			pTempChar = pFunctionName;

			do {
				dwFunctionHash = ROTR32(dwFunctionHash, 13);
				dwFunctionHash += *pTempChar;
				pTempChar++;
			} while (*(pTempChar - 1) != 0);

			dwFunctionHash += dwModuleHash;

			if (dwFunctionHash == dwModuleFunctionHash) {
				usOrdinalTableIndex = *(PUSHORT)(((ULONG_PTR)pModuleBase + pExportDir->AddressOfNameOrdinals) + (2 * i));
				return (HMODULE)((ULONG_PTR)pModuleBase + *(PDWORD)(((ULONG_PTR)pModuleBase + pExportDir->AddressOfFunctions) + (4 * usOrdinalTableIndex)));
			}
		}
	}
	return NULL;
}


template <typename T>
FORCEINLINE uintptr_t installHook(char* targetModule, char*fctName, T b)
{
	INITPIC(-1);
	sText(szKernel32Dll, "kernel32.dll");
	sText(szMsvrtDll, "msvcrt.dll");
	PIC(szKernel32Dll, VirtualProtect);
	PIC(szKernel32Dll, GetModuleHandleA);
	_PIC(szMsvrtDll, strcmp);
	_PIC(szMsvrtDll, printf);
	
	HMODULE module;
	if (!targetModule)
		module = fGetModuleHandleA(0);
	else
		module = fGetModuleHandleA(targetModule);

	_printf(_("Module %p\n"), module);
	void** IATAddr = 0;
	PIMAGE_DOS_HEADER img_dos_headers = (PIMAGE_DOS_HEADER)module;
	PIMAGE_NT_HEADERS img_nt_headers = (PIMAGE_NT_HEADERS)((byte*)img_dos_headers + img_dos_headers->e_lfanew);
	PIMAGE_IMPORT_DESCRIPTOR img_import_desc = (PIMAGE_IMPORT_DESCRIPTOR)((byte*)img_dos_headers + img_nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

	for (IMAGE_IMPORT_DESCRIPTOR *iid = img_import_desc; iid->Name != 0; iid++) {
		for (int func_idx = 0; *(func_idx + (void**)(iid->FirstThunk + (uintptr_t)module)) != nullptr; func_idx++) {
			char* mod_func_name = (char*)(*(func_idx + (uintptr_t*)(iid->OriginalFirstThunk + (uintptr_t)module)) + (uintptr_t)module + 2);
			intptr_t nmod_func_name = (intptr_t)mod_func_name;
			if (nmod_func_name >= 0) {
				
				if (!_strcmp(fctName, mod_func_name)) {
					IATAddr = func_idx + (void**)(iid->FirstThunk + (uintptr_t)module);
					//_printf(_("Imported module for %s : %s\n"), targetModule, mod_func_name);
				}
			}
		}
	}
	
	if (IATAddr) {

		DWORD oldProtect = 0;
		fVirtualProtect(IATAddr, sizeof(uintptr_t), PAGE_READWRITE, &oldProtect);
		*IATAddr = b;
		fVirtualProtect(IATAddr, sizeof(uintptr_t), oldProtect, &oldProtect);
		return 1;
	}
	else
		return 0;
	
}


FORCEINLINE uintptr_t restoreHook(char* targetModule, char*fctName, void* origPointer)
{
	INITPIC(-1);
	sText(szKernel32Dll, "kernel32.dll");
	sText(szMsvrtDll, "msvcrt.dll");
	PIC(szKernel32Dll, VirtualProtect);
	PIC(szKernel32Dll, GetModuleHandleA);
	_PIC(szMsvrtDll, strcmp);
	_PIC(szMsvrtDll, printf);

	HMODULE module;
	if (!targetModule)
		module = fGetModuleHandleA(0);
	else
		module = fGetModuleHandleA(targetModule);

	_printf(_("Module %p\n"), module);
	void** IATAddr = 0;
	PIMAGE_DOS_HEADER img_dos_headers = (PIMAGE_DOS_HEADER)module;
	PIMAGE_NT_HEADERS img_nt_headers = (PIMAGE_NT_HEADERS)((byte*)img_dos_headers + img_dos_headers->e_lfanew);
	PIMAGE_IMPORT_DESCRIPTOR img_import_desc = (PIMAGE_IMPORT_DESCRIPTOR)((byte*)img_dos_headers + img_nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

	for (IMAGE_IMPORT_DESCRIPTOR *iid = img_import_desc; iid->Name != 0; iid++) {
		for (int func_idx = 0; *(func_idx + (void**)(iid->FirstThunk + (uintptr_t)module)) != nullptr; func_idx++) {
			char* mod_func_name = (char*)(*(func_idx + (uintptr_t*)(iid->OriginalFirstThunk + (uintptr_t)module)) + (uintptr_t)module + 2);
			intptr_t nmod_func_name = (intptr_t)mod_func_name;
			if (nmod_func_name >= 0) {

				if (!_strcmp(fctName, mod_func_name)) {
					IATAddr = func_idx + (void**)(iid->FirstThunk + (uintptr_t)module);
				}
			}
		}
	}

	if (IATAddr) {
		DWORD oldProtect = 0;
		fVirtualProtect(IATAddr, sizeof(uintptr_t), PAGE_READWRITE, &oldProtect);
		*IATAddr = origPointer;
		fVirtualProtect(IATAddr, sizeof(uintptr_t), oldProtect, &oldProtect);
		return 1;
	}
	else
		return 0;

}

typedef struct _CLIENT_ID { HANDLE UniqueProcess; HANDLE UniqueThread; } CLIENT_ID, *PCLIENT_ID;
NTSTATUS NTAPI NtOpenProcess(PHANDLE ProcessHandle, ACCESS_MASK AccessMask, POBJECT_ATTRIBUTES ObjectAttributes, PCLIENT_ID ClientID);



 void __stdcall RemoteFunction() {

	INITPIC();
	sText(szNtdllDll, "ntdll.dll");
	sText(szLsasrvDll, "lsasrv.dll");
	//sText(szLsasrvDll, "api-ms-win-core-synch-l1-1-0.dll");

	HOOKTARGET(szLsasrvDll, 
		NtOpenProcess, [](PHANDLE a, ACCESS_MASK b, POBJECT_ATTRIBUTES c, PCLIENT_ID d) -> NTSTATUS 
		{
			INITPIC(false);
			sText(szNtdllDll, "ntdll.dll");
			PIC(szNtdllDll, NtOpenProcess);
			//restoreHook(_("lsasrv.dll"), szNtOpenProcess, fNtOpenProcess);
			//restoreHook(_("api-ms-win-core-synch-l1-1-0.dll"), szNtOpenProcess, fNtOpenProcess);
			auto result = fNtOpenProcess(a, b, c, d);

			sText(szMsvrtDll, "msvcrt.dll");
			_PIC(szMsvrtDll, fopen);
			_PIC(szMsvrtDll, fprintf);
			_PIC(szMsvrtDll, fclose);

			FILE*f = _fopen(_("C:\\nc\\debug.log"), _("ab+"));
			_fprintf(f, _("0x%08x access for %d\r\n"), b, d->UniqueProcess);
			_fclose(f);

			return result;
		}
	);

}

#pragma optimize("", off)
__declspec(noinline) void __stdcall end_marker(void) {
	end_marker();
	return;
}
#pragma optimize("", on)
#pragma strict_gs_check(pop)   
#pragma code_seg(pop)