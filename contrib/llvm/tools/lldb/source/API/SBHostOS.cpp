//===-- SBHostOS.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_DISABLE_PYTHON
#include "Plugins/ScriptInterpreter/Python/lldb-python.h"
#endif

#include "lldb/API/SBError.h"
#include "lldb/API/SBHostOS.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/HostNativeThread.h"
#include "lldb/Host/HostThread.h"
#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Log.h"

#include "Plugins/ExpressionParser/Clang/ClangHost.h"
#ifndef LLDB_DISABLE_PYTHON
#include "Plugins/ScriptInterpreter/Python/ScriptInterpreterPython.h"
#endif

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Path.h"

using namespace lldb;
using namespace lldb_private;

SBFileSpec SBHostOS::GetProgramFileSpec() {
  SBFileSpec sb_filespec;
  sb_filespec.SetFileSpec(HostInfo::GetProgramFileSpec());
  return sb_filespec;
}

SBFileSpec SBHostOS::GetLLDBPythonPath() {
  return GetLLDBPath(ePathTypePythonDir);
}

SBFileSpec SBHostOS::GetLLDBPath(lldb::PathType path_type) {
  FileSpec fspec;
  switch (path_type) {
  case ePathTypeLLDBShlibDir:
    fspec = HostInfo::GetShlibDir();
    break;
  case ePathTypeSupportExecutableDir:
    fspec = HostInfo::GetSupportExeDir();
    break;
  case ePathTypeHeaderDir:
    fspec = HostInfo::GetHeaderDir();
    break;
  case ePathTypePythonDir:
#ifndef LLDB_DISABLE_PYTHON
    fspec = ScriptInterpreterPython::GetPythonDir();
#endif
    break;
  case ePathTypeLLDBSystemPlugins:
    fspec = HostInfo::GetSystemPluginDir();
    break;
  case ePathTypeLLDBUserPlugins:
    fspec = HostInfo::GetUserPluginDir();
    break;
  case ePathTypeLLDBTempSystemDir:
    fspec = HostInfo::GetProcessTempDir();
    break;
  case ePathTypeGlobalLLDBTempSystemDir:
    fspec = HostInfo::GetGlobalTempDir();
    break;
  case ePathTypeClangDir:
    fspec = GetClangResourceDir();
    break;
  }

  SBFileSpec sb_fspec;
  sb_fspec.SetFileSpec(fspec);
  return sb_fspec;
}

SBFileSpec SBHostOS::GetUserHomeDirectory() {
  SBFileSpec sb_fspec;

  llvm::SmallString<64> home_dir_path;
  llvm::sys::path::home_directory(home_dir_path);
  FileSpec homedir(home_dir_path.c_str());
  FileSystem::Instance().Resolve(homedir);

  sb_fspec.SetFileSpec(homedir);
  return sb_fspec;
}

lldb::thread_t SBHostOS::ThreadCreate(const char *name,
                                      lldb::thread_func_t thread_function,
                                      void *thread_arg, SBError *error_ptr) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (log)
    log->Printf(
        "SBHostOS::ThreadCreate (name=\"%s\", thread_function=%p, "
        "thread_arg=%p, error_ptr=%p)",
        name,
        reinterpret_cast<void *>(reinterpret_cast<intptr_t>(thread_function)),
        static_cast<void *>(thread_arg), static_cast<void *>(error_ptr));

  // FIXME: You should log the return value?

  HostThread thread(ThreadLauncher::LaunchThread(
      name, thread_function, thread_arg, error_ptr ? error_ptr->get() : NULL));
  return thread.Release();
}

void SBHostOS::ThreadCreated(const char *name) {}

bool SBHostOS::ThreadCancel(lldb::thread_t thread, SBError *error_ptr) {
  Status error;
  HostThread host_thread(thread);
  error = host_thread.Cancel();
  if (error_ptr)
    error_ptr->SetError(error);
  host_thread.Release();
  return error.Success();
}

bool SBHostOS::ThreadDetach(lldb::thread_t thread, SBError *error_ptr) {
  Status error;
#if defined(_WIN32)
  if (error_ptr)
    error_ptr->SetErrorString("ThreadDetach is not supported on this platform");
#else
  HostThread host_thread(thread);
  error = host_thread.GetNativeThread().Detach();
  if (error_ptr)
    error_ptr->SetError(error);
  host_thread.Release();
#endif
  return error.Success();
}

bool SBHostOS::ThreadJoin(lldb::thread_t thread, lldb::thread_result_t *result,
                          SBError *error_ptr) {
  Status error;
  HostThread host_thread(thread);
  error = host_thread.Join(result);
  if (error_ptr)
    error_ptr->SetError(error);
  host_thread.Release();
  return error.Success();
}
