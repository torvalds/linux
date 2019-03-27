//===-- ProcessRunLock.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef _WIN32
#include "lldb/Host/ProcessRunLock.h"

namespace lldb_private {

ProcessRunLock::ProcessRunLock() : m_running(false) {
  int err = ::pthread_rwlock_init(&m_rwlock, NULL);
  (void)err;
  //#if LLDB_CONFIGURATION_DEBUG
  //        assert(err == 0);
  //#endif
}

ProcessRunLock::~ProcessRunLock() {
  int err = ::pthread_rwlock_destroy(&m_rwlock);
  (void)err;
  //#if LLDB_CONFIGURATION_DEBUG
  //        assert(err == 0);
  //#endif
}

bool ProcessRunLock::ReadTryLock() {
  ::pthread_rwlock_rdlock(&m_rwlock);
  if (!m_running) {
    return true;
  }
  ::pthread_rwlock_unlock(&m_rwlock);
  return false;
}

bool ProcessRunLock::ReadUnlock() {
  return ::pthread_rwlock_unlock(&m_rwlock) == 0;
}

bool ProcessRunLock::SetRunning() {
  ::pthread_rwlock_wrlock(&m_rwlock);
  m_running = true;
  ::pthread_rwlock_unlock(&m_rwlock);
  return true;
}

bool ProcessRunLock::TrySetRunning() {
  bool r;

  if (::pthread_rwlock_trywrlock(&m_rwlock) == 0) {
    r = !m_running;
    m_running = true;
    ::pthread_rwlock_unlock(&m_rwlock);
    return r;
  }
  return false;
}

bool ProcessRunLock::SetStopped() {
  ::pthread_rwlock_wrlock(&m_rwlock);
  m_running = false;
  ::pthread_rwlock_unlock(&m_rwlock);
  return true;
}
}

#endif
