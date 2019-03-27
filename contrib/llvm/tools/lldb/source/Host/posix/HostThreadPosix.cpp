//===-- HostThreadPosix.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/posix/HostThreadPosix.h"
#include "lldb/Utility/Status.h"

#include <errno.h>
#include <pthread.h>

using namespace lldb;
using namespace lldb_private;

HostThreadPosix::HostThreadPosix() {}

HostThreadPosix::HostThreadPosix(lldb::thread_t thread)
    : HostNativeThreadBase(thread) {}

HostThreadPosix::~HostThreadPosix() {}

Status HostThreadPosix::Join(lldb::thread_result_t *result) {
  Status error;
  if (IsJoinable()) {
    int err = ::pthread_join(m_thread, result);
    error.SetError(err, lldb::eErrorTypePOSIX);
  } else {
    if (result)
      *result = NULL;
    error.SetError(EINVAL, eErrorTypePOSIX);
  }

  Reset();
  return error;
}

Status HostThreadPosix::Cancel() {
  Status error;
  if (IsJoinable()) {
#ifndef __FreeBSD__
    llvm_unreachable("someone is calling HostThread::Cancel()");
#else
    int err = ::pthread_cancel(m_thread);
    error.SetError(err, eErrorTypePOSIX);
#endif
  }
  return error;
}

Status HostThreadPosix::Detach() {
  Status error;
  if (IsJoinable()) {
    int err = ::pthread_detach(m_thread);
    error.SetError(err, eErrorTypePOSIX);
  }
  Reset();
  return error;
}
