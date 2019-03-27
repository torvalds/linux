//===-- ThreadLauncher.cpp ---------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// lldb Includes
#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Host/HostNativeThread.h"
#include "lldb/Host/HostThread.h"
#include "lldb/Utility/Log.h"

#if defined(_WIN32)
#include "lldb/Host/windows/windows.h"
#endif

using namespace lldb;
using namespace lldb_private;

HostThread ThreadLauncher::LaunchThread(llvm::StringRef name,
                                        lldb::thread_func_t thread_function,
                                        lldb::thread_arg_t thread_arg,
                                        Status *error_ptr,
                                        size_t min_stack_byte_size) {
  Status error;
  if (error_ptr)
    error_ptr->Clear();

  // Host::ThreadCreateTrampoline will delete this pointer for us.
  HostThreadCreateInfo *info_ptr =
      new HostThreadCreateInfo(name.data(), thread_function, thread_arg);
  lldb::thread_t thread;
#ifdef _WIN32
  thread = (lldb::thread_t)::_beginthreadex(
      0, (unsigned)min_stack_byte_size,
      HostNativeThread::ThreadCreateTrampoline, info_ptr, 0, NULL);
  if (thread == (lldb::thread_t)(-1L))
    error.SetError(::GetLastError(), eErrorTypeWin32);
#else

// ASAN instrumentation adds a lot of bookkeeping overhead on stack frames.
#if __has_feature(address_sanitizer)
  const size_t eight_megabytes = 8 * 1024 * 1024;
  if (min_stack_byte_size < eight_megabytes) {
    min_stack_byte_size += eight_megabytes;
  }
#endif

  pthread_attr_t *thread_attr_ptr = NULL;
  pthread_attr_t thread_attr;
  bool destroy_attr = false;
  if (min_stack_byte_size > 0) {
    if (::pthread_attr_init(&thread_attr) == 0) {
      destroy_attr = true;
      size_t default_min_stack_byte_size = 0;
      if (::pthread_attr_getstacksize(&thread_attr,
                                      &default_min_stack_byte_size) == 0) {
        if (default_min_stack_byte_size < min_stack_byte_size) {
          if (::pthread_attr_setstacksize(&thread_attr, min_stack_byte_size) ==
              0)
            thread_attr_ptr = &thread_attr;
        }
      }
    }
  }
  int err =
      ::pthread_create(&thread, thread_attr_ptr,
                       HostNativeThread::ThreadCreateTrampoline, info_ptr);

  if (destroy_attr)
    ::pthread_attr_destroy(&thread_attr);

  error.SetError(err, eErrorTypePOSIX);
#endif
  if (error_ptr)
    *error_ptr = error;
  if (!error.Success())
    thread = LLDB_INVALID_HOST_THREAD;

  return HostThread(thread);
}
