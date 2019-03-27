//===----------------------------- Registers.hpp --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//
// Abstract interface to shared reader/writer log, hiding platform and
// configuration differences.
//
//===----------------------------------------------------------------------===//

#ifndef __RWMUTEX_HPP__
#define __RWMUTEX_HPP__

#if defined(_WIN32)
#include <windows.h>
#elif !defined(_LIBUNWIND_HAS_NO_THREADS)
#include <pthread.h>
#endif

namespace libunwind {

#if defined(_LIBUNWIND_HAS_NO_THREADS)

class _LIBUNWIND_HIDDEN RWMutex {
public:
  bool lock_shared() { return true; }
  bool unlock_shared() { return true; }
  bool lock() { return true; }
  bool unlock() { return true; }
};

#elif defined(_WIN32)

class _LIBUNWIND_HIDDEN RWMutex {
public:
  bool lock_shared() {
    AcquireSRWLockShared(&_lock);
    return true;
  }
  bool unlock_shared() {
    ReleaseSRWLockShared(&_lock);
    return true;
  }
  bool lock() {
    AcquireSRWLockExclusive(&_lock);
    return true;
  }
  bool unlock() {
    ReleaseSRWLockExclusive(&_lock);
    return true;
  }

private:
  SRWLOCK _lock = SRWLOCK_INIT;
};

#else

class _LIBUNWIND_HIDDEN RWMutex {
public:
  bool lock_shared() { return pthread_rwlock_rdlock(&_lock) == 0; }
  bool unlock_shared() { return pthread_rwlock_unlock(&_lock) == 0; }
  bool lock() { return pthread_rwlock_wrlock(&_lock) == 0; }
  bool unlock() { return pthread_rwlock_unlock(&_lock) == 0; }

private:
  pthread_rwlock_t _lock = PTHREAD_RWLOCK_INITIALIZER;
};

#endif

} // namespace libunwind

#endif // __RWMUTEX_HPP__
