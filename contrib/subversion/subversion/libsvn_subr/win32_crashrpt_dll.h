/*
 * win32_crashrpt_dll.h : private header file.
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#ifndef SVN_LIBSVN_SUBR_WIN32_CRASHRPT_DLL_H
#define SVN_LIBSVN_SUBR_WIN32_CRASHRPT_DLL_H

#ifdef WIN32
#ifdef SVN_USE_WIN32_CRASHHANDLER

/* public functions in dbghelp.dll */
typedef BOOL  (WINAPI * MINIDUMPWRITEDUMP)(HANDLE hProcess, DWORD ProcessId,
               HANDLE hFile, MINIDUMP_TYPE DumpType,
               CONST PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
               CONST PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
               CONST PMINIDUMP_CALLBACK_INFORMATION CallbackParam);
typedef BOOL  (WINAPI * SYMINITIALIZE)(HANDLE hProcess, PSTR UserSearchPath,
                                    BOOL fInvadeProcess);
typedef DWORD (WINAPI * SYMSETOPTIONS)(DWORD SymOptions);

typedef DWORD (WINAPI * SYMGETOPTIONS)(VOID);

typedef BOOL  (WINAPI * SYMCLEANUP)(HANDLE hProcess);

typedef BOOL  (WINAPI * SYMGETTYPEINFO)(HANDLE hProcess, DWORD64 ModBase,
                                     ULONG TypeId, IMAGEHLP_SYMBOL_TYPE_INFO GetType,
                                     PVOID pInfo);

typedef BOOL  (WINAPI * SYMGETLINEFROMADDR64)(HANDLE hProcess, DWORD64 dwAddr,
                                 PDWORD pdwDisplacement, PIMAGEHLP_LINE64 Line);

typedef BOOL  (WINAPI * SYMENUMSYMBOLS)(HANDLE hProcess, ULONG64 BaseOfDll, PCSTR Mask,
                             PSYM_ENUMERATESYMBOLS_CALLBACK EnumSymbolsCallback,
                             PVOID UserContext);

typedef BOOL  (WINAPI * SYMSETCONTEXT)(HANDLE hProcess, PIMAGEHLP_STACK_FRAME StackFrame,
                            PIMAGEHLP_CONTEXT Context);

typedef BOOL  (WINAPI * SYMFROMADDR)(HANDLE hProcess, DWORD64 Address,
                          PDWORD64 Displacement, PSYMBOL_INFO Symbol);

typedef BOOL (WINAPI * STACKWALK64)(DWORD MachineType, HANDLE hProcess, HANDLE hThread,
                                LPSTACKFRAME64 StackFrame, PVOID ContextRecord,
                                PREAD_PROCESS_MEMORY_ROUTINE64 ReadMemoryRoutine,
                                PFUNCTION_TABLE_ACCESS_ROUTINE64 FunctionTableAccessRoutine,
                                PGET_MODULE_BASE_ROUTINE64 GetModuleBaseRoutine,
                                PTRANSLATE_ADDRESS_ROUTINE64 TranslateAddress);

typedef PVOID (WINAPI * SYMFUNCTIONTABLEACCESS64)(HANDLE hProcess, DWORD64 AddrBase);

typedef DWORD64 (WINAPI * SYMGETMODULEBASE64)(HANDLE hProcess, DWORD64 dwAddr);

/* function pointers */
MINIDUMPWRITEDUMP        MiniDumpWriteDump_;
SYMINITIALIZE            SymInitialize_;
SYMSETOPTIONS            SymSetOptions_;
SYMGETOPTIONS            SymGetOptions_;
SYMCLEANUP               SymCleanup_;
SYMGETTYPEINFO           SymGetTypeInfo_;
SYMGETLINEFROMADDR64     SymGetLineFromAddr64_;
SYMENUMSYMBOLS           SymEnumSymbols_;
SYMSETCONTEXT            SymSetContext_;
SYMFROMADDR              SymFromAddr_;
STACKWALK64              StackWalk64_;
SYMFUNCTIONTABLEACCESS64 SymFunctionTableAccess64_;
SYMGETMODULEBASE64       SymGetModuleBase64_;

#endif /* SVN_USE_WIN32_CRASHHANDLER */
#endif /* WIN32 */

#endif /* SVN_LIBSVN_SUBR_WIN32_CRASHRPT_DLL_H */