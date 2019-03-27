/*
 * win32_crashrpt.c : provides information after a crash
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

/* prevent "empty compilation unit" warning on e.g. UNIX */
typedef int win32_crashrpt__dummy;

#ifdef WIN32
#ifdef SVN_USE_WIN32_CRASHHANDLER

/*** Includes. ***/
#include <apr.h>
#include <dbghelp.h>
#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "svn_version.h"

#include "sysinfo.h"

#include "win32_crashrpt.h"
#include "win32_crashrpt_dll.h"

/*** Global variables ***/
static HANDLE dbghelp_dll = INVALID_HANDLE_VALUE;

/* Email address where the crash reports should be sent too. */
#define CRASHREPORT_EMAIL "users@subversion.apache.org"

#define DBGHELP_DLL "dbghelp.dll"

#define LOGFILE_PREFIX "svn-crash-log"

#if defined(_M_IX86)
#define FORMAT_PTR "0x%08Ix"
#elif defined(_M_X64)
#define FORMAT_PTR "0x%016Ix"
#endif

/*** Code. ***/

/* Convert a wide-character string to the current windows locale, suitable
 * for directly using stdio. This function will create a buffer large
 * enough to hold the result string, the caller should free this buffer.
 * If the string can't be converted, NULL is returned.
 */
static char *
convert_wbcs_to_ansi(const wchar_t *str)
{
  size_t len = wcslen(str);
  char *utf8_str = malloc(sizeof(wchar_t) * len + 1);
  len = wcstombs(utf8_str, str, len);

  if (len == -1)
    return NULL;

  utf8_str[len] = '\0';

  return utf8_str;
}

/* Convert the exception code to a string */
static const char *
exception_string(int exception)
{
#define EXCEPTION(x) case x: return (#x);

  switch (exception)
    {
      EXCEPTION(EXCEPTION_ACCESS_VIOLATION)
      EXCEPTION(EXCEPTION_DATATYPE_MISALIGNMENT)
      EXCEPTION(EXCEPTION_BREAKPOINT)
      EXCEPTION(EXCEPTION_SINGLE_STEP)
      EXCEPTION(EXCEPTION_ARRAY_BOUNDS_EXCEEDED)
      EXCEPTION(EXCEPTION_FLT_DENORMAL_OPERAND)
      EXCEPTION(EXCEPTION_FLT_DIVIDE_BY_ZERO)
      EXCEPTION(EXCEPTION_FLT_INEXACT_RESULT)
      EXCEPTION(EXCEPTION_FLT_INVALID_OPERATION)
      EXCEPTION(EXCEPTION_FLT_OVERFLOW)
      EXCEPTION(EXCEPTION_FLT_STACK_CHECK)
      EXCEPTION(EXCEPTION_FLT_UNDERFLOW)
      EXCEPTION(EXCEPTION_INT_DIVIDE_BY_ZERO)
      EXCEPTION(EXCEPTION_INT_OVERFLOW)
      EXCEPTION(EXCEPTION_PRIV_INSTRUCTION)
      EXCEPTION(EXCEPTION_IN_PAGE_ERROR)
      EXCEPTION(EXCEPTION_ILLEGAL_INSTRUCTION)
      EXCEPTION(EXCEPTION_NONCONTINUABLE_EXCEPTION)
      EXCEPTION(EXCEPTION_STACK_OVERFLOW)
      EXCEPTION(EXCEPTION_INVALID_DISPOSITION)
      EXCEPTION(EXCEPTION_GUARD_PAGE)
      EXCEPTION(EXCEPTION_INVALID_HANDLE)
      EXCEPTION(STATUS_NO_MEMORY)

      default:
        return "UNKNOWN_ERROR";
    }
#undef EXCEPTION
}

/* Write the minidump to file. The callback function will at the same time
   write the list of modules to the log file. */
static BOOL
write_minidump_file(const char *file, PEXCEPTION_POINTERS ptrs,
                    MINIDUMP_CALLBACK_ROUTINE module_callback,
                    void *data)
{
  /* open minidump file */
  HANDLE minidump_file = CreateFile(file, GENERIC_WRITE, 0, NULL,
                                    CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL,
                                    NULL);

  if (minidump_file != INVALID_HANDLE_VALUE)
    {
      MINIDUMP_EXCEPTION_INFORMATION expt_info;
      MINIDUMP_CALLBACK_INFORMATION dump_cb_info;

      expt_info.ThreadId = GetCurrentThreadId();
      expt_info.ExceptionPointers = ptrs;
      expt_info.ClientPointers = FALSE;

      dump_cb_info.CallbackRoutine = module_callback;
      dump_cb_info.CallbackParam = data;

      MiniDumpWriteDump_(GetCurrentProcess(),
                         GetCurrentProcessId(),
                         minidump_file,
                         MiniDumpNormal,
                         ptrs ? &expt_info : NULL,
                         NULL,
                         &dump_cb_info);

      CloseHandle(minidump_file);
      return TRUE;
    }

  return FALSE;
}

/* Write module information to the log file */
static BOOL CALLBACK
write_module_info_callback(void *data,
                 CONST PMINIDUMP_CALLBACK_INPUT callback_input,
                 PMINIDUMP_CALLBACK_OUTPUT callback_output)
{
  if (data != NULL &&
      callback_input != NULL &&
      callback_input->CallbackType == ModuleCallback)
    {
      FILE *log_file = (FILE *)data;
      MINIDUMP_MODULE_CALLBACK module = callback_input->Module;

      char *buf = convert_wbcs_to_ansi(module.FullPath);
      fprintf(log_file, FORMAT_PTR, (UINT_PTR)module.BaseOfImage);
      fprintf(log_file, "  %s", buf);
      free(buf);

      fprintf(log_file, " (%d.%d.%d.%d, %d bytes)\n",
                              HIWORD(module.VersionInfo.dwFileVersionMS),
                              LOWORD(module.VersionInfo.dwFileVersionMS),
                              HIWORD(module.VersionInfo.dwFileVersionLS),
                              LOWORD(module.VersionInfo.dwFileVersionLS),
                              module.SizeOfImage);
    }

  return TRUE;
}

/* Write details about the current process, platform and the exception */
static void
write_process_info(EXCEPTION_RECORD *exception, CONTEXT *context,
                   FILE *log_file)
{
  OSVERSIONINFOEXW oi;
  const char *cmd_line;
  char workingdir[8192];

  /* write the command line */
  cmd_line = GetCommandLine();
  fprintf(log_file,
                "Cmd line: %s\n", cmd_line);

  _getcwd(workingdir, sizeof(workingdir));
  fprintf(log_file,
                "Working Dir: %s\n", workingdir);

  /* write the svn version number info. */
  fprintf(log_file,
                "Version:  %s, compiled %s, %s\n",
                SVN_VERSION, __DATE__, __TIME__);

  /* write information about the OS */
  if (svn_sysinfo___fill_windows_version(&oi))
    fprintf(log_file,
                  "Platform: Windows OS version %d.%d build %d %S\n\n",
                  oi.dwMajorVersion, oi.dwMinorVersion, oi.dwBuildNumber,
                  oi.szCSDVersion);

  /* write the exception code */
  fprintf(log_file,
               "Exception: %s\n\n",
               exception_string(exception->ExceptionCode));

  /* write the register info. */
  fprintf(log_file,
                "Registers:\n");
#if defined(_M_IX86)
  fprintf(log_file,
                "eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x\n",
                context->Eax, context->Ebx, context->Ecx,
                context->Edx, context->Esi, context->Edi);
  fprintf(log_file,
                "eip=%08x esp=%08x ebp=%08x efl=%08x\n",
                context->Eip, context->Esp,
                context->Ebp, context->EFlags);
  fprintf(log_file,
                "cs=%04x  ss=%04x  ds=%04x  es=%04x  fs=%04x  gs=%04x\n",
                context->SegCs, context->SegSs, context->SegDs,
                context->SegEs, context->SegFs, context->SegGs);
#elif defined(_M_X64)
  fprintf(log_file,
                "Rax=%016I64x Rcx=%016I64x Rdx=%016I64x Rbx=%016I64x\n",
                context->Rax, context->Rcx, context->Rdx, context->Rbx);
  fprintf(log_file,
                "Rsp=%016I64x Rbp=%016I64x Rsi=%016I64x Rdi=%016I64x\n",
                context->Rsp, context->Rbp, context->Rsi, context->Rdi);
  fprintf(log_file,
                "R8= %016I64x R9= %016I64x R10=%016I64x R11=%016I64x\n",
                context->R8, context->R9, context->R10, context->R11);
  fprintf(log_file,
                "R12=%016I64x R13=%016I64x R14=%016I64x R15=%016I64x\n",
                context->R12, context->R13, context->R14, context->R15);

  fprintf(log_file,
                "cs=%04x  ss=%04x  ds=%04x  es=%04x  fs=%04x  gs=%04x\n",
                context->SegCs, context->SegSs, context->SegDs,
                context->SegEs, context->SegFs, context->SegGs);
#else
#error Unknown processortype, please disable SVN_USE_WIN32_CRASHHANDLER
#endif
}

/* Writes the value at address based on the specified basic type
 * (char, int, long ...) to LOG_FILE. */
static void
write_basic_type(FILE *log_file, DWORD basic_type, DWORD64 length,
                 void *address)
{
  switch(length)
    {
      case 1:
        fprintf(log_file, "0x%02x", (int)*(unsigned char *)address);
        break;
      case 2:
        fprintf(log_file, "0x%04x", (int)*(unsigned short *)address);
        break;
      case 4:
        switch(basic_type)
          {
            case 2:  /* btChar */
              {
                if (!IsBadStringPtr(*(PSTR*)address, 32))
                  fprintf(log_file, "\"%.31s\"", *(const char **)address);
                else
                  fprintf(log_file, FORMAT_PTR, *(DWORD_PTR *)address);
              }
            case 6:  /* btInt */
              fprintf(log_file, "%d", *(int *)address);
              break;
            case 8:  /* btFloat */
              fprintf(log_file, "%f", *(float *)address);
              break;
            default:
              fprintf(log_file, FORMAT_PTR, *(DWORD_PTR *)address);
              break;
          }
        break;
      case 8:
        if (basic_type == 8) /* btFloat */
          fprintf(log_file, "%lf", *(double *)address);
        else
          fprintf(log_file, "0x%016I64X", *(unsigned __int64 *)address);
        break;
      default:
        fprintf(log_file, "[unhandled type 0x%08x of length " FORMAT_PTR "]",
                basic_type, (UINT_PTR)length);
        break;
    }
}

/* Writes the value at address based on the type (pointer, user defined,
 * basic type) to LOG_FILE. */
static void
write_value(FILE *log_file, DWORD64 mod_base, DWORD type, void *value_addr)
{
  DWORD tag = 0;
  int ptr = 0;
  HANDLE proc = GetCurrentProcess();

  while (SymGetTypeInfo_(proc, mod_base, type, TI_GET_SYMTAG, &tag))
    {
      /* SymTagPointerType */
      if (tag == 14)
        {
          ptr++;
          SymGetTypeInfo_(proc, mod_base, type, TI_GET_TYPE, &type);
          continue;
        }
      break;
    }

  switch(tag)
    {
      case 11: /* SymTagUDT */
        {
          WCHAR *type_name_wbcs;
          if (SymGetTypeInfo_(proc, mod_base, type, TI_GET_SYMNAME,
                              &type_name_wbcs))
            {
              char *type_name = convert_wbcs_to_ansi(type_name_wbcs);
              LocalFree(type_name_wbcs);

              if (ptr == 0)
                fprintf(log_file, "(%s) " FORMAT_PTR,
                        type_name, (UINT_PTR)(DWORD_PTR *)value_addr);
              else if (ptr == 1)
                fprintf(log_file, "(%s *) " FORMAT_PTR,
                        type_name, *(DWORD_PTR *)value_addr);
              else
                fprintf(log_file, "(%s **) " FORMAT_PTR,
                        type_name, *(DWORD_PTR *)value_addr);

              free(type_name);
            }
          else
            fprintf(log_file, "[no symbol tag]");
        }
        break;
      case 16: /* SymTagBaseType */
        {
          DWORD bt;
          ULONG64 length;
          SymGetTypeInfo_(proc, mod_base, type, TI_GET_LENGTH, &length);

          /* print a char * as a string */
          if (ptr == 1 && length == 1)
            {
              fprintf(log_file, FORMAT_PTR " \"%s\"",
                      *(DWORD_PTR *)value_addr, *(const char **)value_addr);
            }
          else if (ptr >= 1)
            {
              fprintf(log_file, FORMAT_PTR, *(DWORD_PTR *)value_addr);
            }
          else if (SymGetTypeInfo_(proc, mod_base, type, TI_GET_BASETYPE, &bt))
            {
              write_basic_type(log_file, bt, length, value_addr);
            }
        }
        break;
      case 12: /* SymTagEnum */
          fprintf(log_file, "%Id", *(DWORD_PTR *)value_addr);
          break;
      case 13: /* SymTagFunctionType */
          fprintf(log_file, FORMAT_PTR, *(DWORD_PTR *)value_addr);
          break;
      default:
          fprintf(log_file, "[unhandled tag: %d]", tag);
          break;
    }
}

/* Internal structure used to pass some data to the enumerate symbols
 * callback */
typedef struct symbols_baton_t {
  STACKFRAME64 *stack_frame;
  FILE *log_file;
  int nr_of_frame;
  BOOL log_params;
} symbols_baton_t;

/* Write the details of one parameter or local variable to the log file */
static BOOL WINAPI
write_var_values(PSYMBOL_INFO sym_info, ULONG sym_size, void *baton)
{
  static int last_nr_of_frame = 0;
  DWORD_PTR var_data = 0;    /* Will point to the variable's data in memory */
  STACKFRAME64 *stack_frame = ((symbols_baton_t*)baton)->stack_frame;
  FILE *log_file   = ((symbols_baton_t*)baton)->log_file;
  int nr_of_frame = ((symbols_baton_t*)baton)->nr_of_frame;
  BOOL log_params = ((symbols_baton_t*)baton)->log_params;

  /* get the variable's data */
  if (sym_info->Flags & SYMFLAG_REGREL)
    {
      var_data = (DWORD_PTR)stack_frame->AddrFrame.Offset;
      var_data += (DWORD_PTR)sym_info->Address;
    }
  else
    return FALSE;

  if (log_params && sym_info->Flags & SYMFLAG_PARAMETER)
    {
      if (last_nr_of_frame == nr_of_frame)
        fprintf(log_file, ", ");
      else
        last_nr_of_frame = nr_of_frame;

      fprintf(log_file, "%.*s=", (int)sym_info->NameLen, sym_info->Name);
      write_value(log_file, sym_info->ModBase, sym_info->TypeIndex,
                  (void *)var_data);
    }
  if (!log_params && sym_info->Flags & SYMFLAG_LOCAL)
    {
      fprintf(log_file, "        %.*s = ", (int)sym_info->NameLen,
              sym_info->Name);
      write_value(log_file, sym_info->ModBase, sym_info->TypeIndex,
                  (void *)var_data);
      fprintf(log_file, "\n");
    }

  return TRUE;
}

/* Write the details of one function to the log file */
static void
write_function_detail(STACKFRAME64 stack_frame, int nr_of_frame, FILE *log_file)
{
  ULONG64 symbolBuffer[(sizeof(SYMBOL_INFO) +
    MAX_SYM_NAME +
    sizeof(ULONG64) - 1) /
    sizeof(ULONG64)];
  PSYMBOL_INFO pIHS = (PSYMBOL_INFO)symbolBuffer;
  DWORD64 func_disp=0;

  IMAGEHLP_STACK_FRAME ih_stack_frame;
  IMAGEHLP_LINE64 ih_line;
  DWORD line_disp=0;

  HANDLE proc = GetCurrentProcess();

  symbols_baton_t ensym;

  nr_of_frame++; /* We need a 1 based index here */

  /* log the function name */
  pIHS->SizeOfStruct = sizeof(SYMBOL_INFO);
  pIHS->MaxNameLen = MAX_SYM_NAME;
  if (SymFromAddr_(proc, stack_frame.AddrPC.Offset, &func_disp, pIHS))
    {
      fprintf(log_file,
                    "#%d  0x%08I64x in %.*s(",
                    nr_of_frame, stack_frame.AddrPC.Offset,
                    pIHS->NameLen > 200 ? 200 : (int)pIHS->NameLen,
                    pIHS->Name);

      /* restrict symbol enumeration to this frame only */
      ih_stack_frame.InstructionOffset = stack_frame.AddrPC.Offset;
      SymSetContext_(proc, &ih_stack_frame, 0);

      ensym.log_file = log_file;
      ensym.stack_frame = &stack_frame;
      ensym.nr_of_frame = nr_of_frame;

      /* log all function parameters */
      ensym.log_params = TRUE;
      SymEnumSymbols_(proc, 0, 0, write_var_values, &ensym);

      fprintf(log_file, ")");
    }
  else
    {
      fprintf(log_file,
                    "#%d  0x%08I64x in (unknown function)",
                    nr_of_frame, stack_frame.AddrPC.Offset);
    }

  /* find the source line for this function. */
  ih_line.SizeOfStruct = sizeof(IMAGEHLP_LINE);
  if (SymGetLineFromAddr64_(proc, stack_frame.AddrPC.Offset,
                          &line_disp, &ih_line) != 0)
    {
      fprintf(log_file,
                    " at %s:%d\n", ih_line.FileName, ih_line.LineNumber);
    }
  else
    {
      fprintf(log_file, "\n");
    }

  /* log all function local variables */
  ensym.log_params = FALSE;
  SymEnumSymbols_(proc, 0, 0, write_var_values, &ensym);
}

/* Walk over the stack and log all relevant information to the log file */
static void
write_stacktrace(CONTEXT *context, FILE *log_file)
{
#if defined (_M_IX86) || defined(_M_X64) || defined(_M_IA64)
  HANDLE proc = GetCurrentProcess();
  STACKFRAME64 stack_frame;
  DWORD machine;
  CONTEXT ctx;
  int skip = 0, i = 0;

  /* The thread information - if not supplied. */
  if (context == NULL)
    {
      /* If no context is supplied, skip 1 frame */
      skip = 1;

      ctx.ContextFlags = CONTEXT_FULL;
      if (!GetThreadContext(GetCurrentThread(), &ctx))
        return;
    }
  else
    {
      ctx = *context;
    }

  if (context == NULL)
    return;

  /* Write the stack trace */
  ZeroMemory(&stack_frame, sizeof(STACKFRAME64));
  stack_frame.AddrPC.Mode = AddrModeFlat;
  stack_frame.AddrStack.Mode   = AddrModeFlat;
  stack_frame.AddrFrame.Mode   = AddrModeFlat;

#if defined(_M_IX86)
  machine = IMAGE_FILE_MACHINE_I386;
  stack_frame.AddrPC.Offset    = context->Eip;
  stack_frame.AddrStack.Offset = context->Esp;
  stack_frame.AddrFrame.Offset = context->Ebp;
#elif defined(_M_X64)
  machine = IMAGE_FILE_MACHINE_AMD64;
  stack_frame.AddrPC.Offset     = context->Rip;
  stack_frame.AddrStack.Offset  = context->Rsp;
  stack_frame.AddrFrame.Offset  = context->Rbp;
#elif defined(_M_IA64)
  machine = IMAGE_FILE_MACHINE_IA64;
  stack_frame.AddrPC.Offset     = context->StIIP;
  stack_frame.AddrStack.Offset  = context->SP;
  stack_frame.AddrBStore.Mode   = AddrModeFlat;
  stack_frame.AddrBStore.Offset = context->RsBSP;
#else
#error Unknown processortype, please disable SVN_USE_WIN32_CRASHHANDLER
#endif

  while (1)
    {
      if (! StackWalk64_(machine, proc, GetCurrentThread(),
                         &stack_frame, &ctx, NULL,
                         SymFunctionTableAccess64_, SymGetModuleBase64_, NULL))
        {
          break;
        }

      if (i >= skip)
        {
          /* Try to include symbolic information.
             Also check that the address is not zero. Sometimes StackWalk
             returns TRUE with a frame of zero. */
          if (stack_frame.AddrPC.Offset != 0)
            {
              write_function_detail(stack_frame, i, log_file);
            }
        }
      i++;
    }
#else
#error Unknown processortype, please disable SVN_USE_WIN32_CRASHHANDLER
#endif
}

/* Check if a debugger is attached to this process */
static BOOL
is_debugger_present()
{
  return IsDebuggerPresent();
}

/* Load the dbghelp.dll file, try to find a version that matches our
   requirements. */
static BOOL
load_dbghelp_dll()
{
  dbghelp_dll = LoadLibrary(DBGHELP_DLL);
  if (dbghelp_dll != NULL)
    {
      DWORD opts;

      /* load the functions */
      MiniDumpWriteDump_ =
           (MINIDUMPWRITEDUMP)GetProcAddress(dbghelp_dll, "MiniDumpWriteDump");
      SymInitialize_ =
           (SYMINITIALIZE)GetProcAddress(dbghelp_dll, "SymInitialize");
      SymSetOptions_ =
           (SYMSETOPTIONS)GetProcAddress(dbghelp_dll, "SymSetOptions");
      SymGetOptions_ =
           (SYMGETOPTIONS)GetProcAddress(dbghelp_dll, "SymGetOptions");
      SymCleanup_ =
           (SYMCLEANUP)GetProcAddress(dbghelp_dll, "SymCleanup");
      SymGetTypeInfo_ =
           (SYMGETTYPEINFO)GetProcAddress(dbghelp_dll, "SymGetTypeInfo");
      SymGetLineFromAddr64_ =
           (SYMGETLINEFROMADDR64)GetProcAddress(dbghelp_dll,
                                              "SymGetLineFromAddr64");
      SymEnumSymbols_ =
           (SYMENUMSYMBOLS)GetProcAddress(dbghelp_dll, "SymEnumSymbols");
      SymSetContext_ =
           (SYMSETCONTEXT)GetProcAddress(dbghelp_dll, "SymSetContext");
      SymFromAddr_ = (SYMFROMADDR)GetProcAddress(dbghelp_dll, "SymFromAddr");
      StackWalk64_ = (STACKWALK64)GetProcAddress(dbghelp_dll, "StackWalk64");
      SymFunctionTableAccess64_ =
           (SYMFUNCTIONTABLEACCESS64)GetProcAddress(dbghelp_dll,
                                                  "SymFunctionTableAccess64");
      SymGetModuleBase64_ =
           (SYMGETMODULEBASE64)GetProcAddress(dbghelp_dll, "SymGetModuleBase64");

      if (! (MiniDumpWriteDump_ &&
             SymInitialize_ && SymSetOptions_  && SymGetOptions_ &&
             SymCleanup_    && SymGetTypeInfo_ && SymGetLineFromAddr64_ &&
             SymEnumSymbols_ && SymSetContext_ && SymFromAddr_ &&
             SymGetModuleBase64_ && StackWalk64_ &&
             SymFunctionTableAccess64_))
        goto cleanup;

      /* initialize the symbol loading code */
      opts = SymGetOptions_();

      /* Set the 'load lines' option to retrieve line number information;
         set the Deferred Loads option to map the debug info in memory only
         when needed. */
      SymSetOptions_(opts | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);

      /* Initialize the debughlp DLL with the default path and automatic
         module enumeration (and loading of symbol tables) for this process.
       */
      SymInitialize_(GetCurrentProcess(), NULL, TRUE);

      return TRUE;
    }

cleanup:
  if (dbghelp_dll)
    FreeLibrary(dbghelp_dll);

  return FALSE;
}

/* Cleanup the dbghelp.dll library */
static void
cleanup_debughlp()
{
  SymCleanup_(GetCurrentProcess());

  FreeLibrary(dbghelp_dll);
}

/* Create a filename based on a prefix, the timestamp and an extension.
   check if the filename was already taken, retry 3 times. */
BOOL
get_temp_filename(char *filename, const char *prefix, const char *ext)
{
  char temp_dir[MAX_PATH - 64];
  int i;

  if (! GetTempPath(MAX_PATH - 64, temp_dir))
    return FALSE;

  for (i = 0;i < 3;i++)
    {
      HANDLE file;
      time_t now;
      char time_str[64];

      time(&now);
      strftime(time_str, 64, "%Y%m%d%H%M%S", localtime(&now));
      sprintf(filename, "%s%s%s.%s", temp_dir, prefix, time_str, ext);

      file = CreateFile(filename, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                        FILE_ATTRIBUTE_NORMAL, NULL);
      if (file != INVALID_HANDLE_VALUE)
        {
          CloseHandle(file);
          return TRUE;
        }
    }

   filename[0] = '\0';
   return FALSE;
}

/* Unhandled exception callback set with SetUnhandledExceptionFilter() */
LONG WINAPI
svn__unhandled_exception_filter(PEXCEPTION_POINTERS ptrs)
{
  char dmp_filename[MAX_PATH];
  char log_filename[MAX_PATH];
  FILE *log_file;

  /* Check if the crash handler was already loaded (crash while handling the
     crash) */
  if (dbghelp_dll != INVALID_HANDLE_VALUE)
    return EXCEPTION_CONTINUE_SEARCH;

  /* don't log anything if we're running inside a debugger ... */
  if (is_debugger_present())
    return EXCEPTION_CONTINUE_SEARCH;

  /* ... or if we can't create the log files ... */
  if (!get_temp_filename(dmp_filename, LOGFILE_PREFIX, "dmp") ||
      !get_temp_filename(log_filename, LOGFILE_PREFIX, "log"))
    return EXCEPTION_CONTINUE_SEARCH;

  /* If we can't load a recent version of the dbghelp.dll, pass on this
     exception */
  if (!load_dbghelp_dll())
    return EXCEPTION_CONTINUE_SEARCH;

  /* open log file */
  log_file = fopen(log_filename, "w+");

  /* write information about the process */
  fprintf(log_file, "\nProcess info:\n");
  write_process_info(ptrs ? ptrs->ExceptionRecord : NULL,
                     ptrs ? ptrs->ContextRecord : NULL,
                     log_file);

  /* write the stacktrace, if available */
  fprintf(log_file, "\nStacktrace:\n");
  write_stacktrace(ptrs ? ptrs->ContextRecord : NULL, log_file);

  /* write the minidump file and use the callback to write the list of modules
     to the log file */
  fprintf(log_file, "\n\nLoaded modules:\n");
  write_minidump_file(dmp_filename, ptrs,
                      write_module_info_callback, (void *)log_file);

  fclose(log_file);

  /* inform the user */
  fprintf(stderr, "This application has halted due to an unexpected error.\n"
                  "A crash report and minidump file were saved to disk, you"
                  " can find them here:\n"
                  "%s\n%s\n"
                  "Please send the log file to %s to help us analyze\nand "
                  "solve this problem.\n\n"
                  "NOTE: The crash report and minidump files can contain some"
                  " sensitive information\n(filenames, partial file content, "
                  "usernames and passwords etc.)\n",
                  log_filename,
                  dmp_filename,
                  CRASHREPORT_EMAIL);

  if (getenv("SVN_DBG_STACKTRACES_TO_STDERR") != NULL)
    {
      fprintf(stderr, "\nProcess info:\n");
      write_process_info(ptrs ? ptrs->ExceptionRecord : NULL,
                         ptrs ? ptrs->ContextRecord : NULL,
                         stderr);
      fprintf(stderr, "\nStacktrace:\n");
      write_stacktrace(ptrs ? ptrs->ContextRecord : NULL, stderr);
    }

  fflush(stderr);
  fflush(stdout);

  cleanup_debughlp();

  /* terminate the application */
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif /* SVN_USE_WIN32_CRASHHANDLER */
#else  /* !WIN32 */

/* Silence OSX ranlib warnings about object files with no symbols. */
#include <apr.h>
extern const apr_uint32_t svn__fake__win32_crashrpt;
const apr_uint32_t svn__fake__win32_crashrpt = 0xdeadbeef;

#endif /* WIN32 */
