//===-- sanitizer_mutex.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_MUTEX_H
#define SANITIZER_MUTEX_H

#include "sanitizer_atomic.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_libc.h"

namespace __sanitizer {

class StaticSpinMutex {
 public:
  void Init() {
    atomic_store(&state_, 0, memory_order_relaxed);
  }

  void Lock() {
    if (TryLock())
      return;
    LockSlow();
  }

  bool TryLock() {
    return atomic_exchange(&state_, 1, memory_order_acquire) == 0;
  }

  void Unlock() {
    atomic_store(&state_, 0, memory_order_release);
  }

  void CheckLocked() {
    CHECK_EQ(atomic_load(&state_, memory_order_relaxed), 1);
  }

 private:
  atomic_uint8_t state_;

  void NOINLINE LockSlow() {
    for (int i = 0;; i++) {
      if (i < 10)
        proc_yield(10);
      else
        internal_sched_yield();
      if (atomic_load(&state_, memory_order_relaxed) == 0
          && atomic_exchange(&state_, 1, memory_order_acquire) == 0)
        return;
    }
  }
};

class SpinMutex : public StaticSpinMutex {
 public:
  SpinMutex() {
    Init();
  }

 private:
  SpinMutex(const SpinMutex&);
  void operator=(const SpinMutex&);
};

class BlockingMutex {
 public:
  explicit constexpr BlockingMutex(LinkerInitialized)
      : opaque_storage_ {0, }, owner_ {0} {}
  BlockingMutex();
  void Lock();
  void Unlock();

  // This function does not guarantee an explicit check that the calling thread
  // is the thread which owns the mutex. This behavior, while more strictly
  // correct, causes problems in cases like StopTheWorld, where a parent thread
  // owns the mutex but a child checks that it is locked. Rather than
  // maintaining complex state to work around those situations, the check only
  // checks that the mutex is owned, and assumes callers to be generally
  // well-behaved.
  void CheckLocked();

 private:
  // Solaris mutex_t has a member that requires 64-bit alignment.
  ALIGNED(8) uptr opaque_storage_[10];
  uptr owner_;  // for debugging
};

// Reader-writer spin mutex.
class RWMutex {
 public:
  RWMutex() {
    atomic_store(&state_, kUnlocked, memory_order_relaxed);
  }

  ~RWMutex() {
    CHECK_EQ(atomic_load(&state_, memory_order_relaxed), kUnlocked);
  }

  void Lock() {
    u32 cmp = kUnlocked;
    if (atomic_compare_exchange_strong(&state_, &cmp, kWriteLock,
                                       memory_order_acquire))
      return;
    LockSlow();
  }

  void Unlock() {
    u32 prev = atomic_fetch_sub(&state_, kWriteLock, memory_order_release);
    DCHECK_NE(prev & kWriteLock, 0);
    (void)prev;
  }

  void ReadLock() {
    u32 prev = atomic_fetch_add(&state_, kReadLock, memory_order_acquire);
    if ((prev & kWriteLock) == 0)
      return;
    ReadLockSlow();
  }

  void ReadUnlock() {
    u32 prev = atomic_fetch_sub(&state_, kReadLock, memory_order_release);
    DCHECK_EQ(prev & kWriteLock, 0);
    DCHECK_GT(prev & ~kWriteLock, 0);
    (void)prev;
  }

  void CheckLocked() {
    CHECK_NE(atomic_load(&state_, memory_order_relaxed), kUnlocked);
  }

 private:
  atomic_uint32_t state_;

  enum {
    kUnlocked = 0,
    kWriteLock = 1,
    kReadLock = 2
  };

  void NOINLINE LockSlow() {
    for (int i = 0;; i++) {
      if (i < 10)
        proc_yield(10);
      else
        internal_sched_yield();
      u32 cmp = atomic_load(&state_, memory_order_relaxed);
      if (cmp == kUnlocked &&
          atomic_compare_exchange_weak(&state_, &cmp, kWriteLock,
                                       memory_order_acquire))
          return;
    }
  }

  void NOINLINE ReadLockSlow() {
    for (int i = 0;; i++) {
      if (i < 10)
        proc_yield(10);
      else
        internal_sched_yield();
      u32 prev = atomic_load(&state_, memory_order_acquire);
      if ((prev & kWriteLock) == 0)
        return;
    }
  }

  RWMutex(const RWMutex&);
  void operator = (const RWMutex&);
};

template<typename MutexType>
class GenericScopedLock {
 public:
  explicit GenericScopedLock(MutexType *mu)
      : mu_(mu) {
    mu_->Lock();
  }

  ~GenericScopedLock() {
    mu_->Unlock();
  }

 private:
  MutexType *mu_;

  GenericScopedLock(const GenericScopedLock&);
  void operator=(const GenericScopedLock&);
};

template<typename MutexType>
class GenericScopedReadLock {
 public:
  explicit GenericScopedReadLock(MutexType *mu)
      : mu_(mu) {
    mu_->ReadLock();
  }

  ~GenericScopedReadLock() {
    mu_->ReadUnlock();
  }

 private:
  MutexType *mu_;

  GenericScopedReadLock(const GenericScopedReadLock&);
  void operator=(const GenericScopedReadLock&);
};

typedef GenericScopedLock<StaticSpinMutex> SpinMutexLock;
typedef GenericScopedLock<BlockingMutex> BlockingMutexLock;
typedef GenericScopedLock<RWMutex> RWMutexLock;
typedef GenericScopedReadLock<RWMutex> RWMutexReadLock;

}  // namespace __sanitizer

#endif  // SANITIZER_MUTEX_H
