//===-- scudo_tsd.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// Scudo thread specific data definition.
/// Implementation will differ based on the thread local storage primitives
/// offered by the underlying platform.
///
//===----------------------------------------------------------------------===//

#ifndef SCUDO_TSD_H_
#define SCUDO_TSD_H_

#include "scudo_allocator.h"
#include "scudo_utils.h"

#include <pthread.h>

namespace __scudo {

struct ALIGNED(SANITIZER_CACHE_LINE_SIZE) ScudoTSD {
  AllocatorCacheT Cache;
  uptr QuarantineCachePlaceHolder[4];

  void init();
  void commitBack();

  INLINE bool tryLock() {
    if (Mutex.TryLock()) {
      atomic_store_relaxed(&Precedence, 0);
      return true;
    }
    if (atomic_load_relaxed(&Precedence) == 0)
      atomic_store_relaxed(&Precedence, static_cast<uptr>(
          MonotonicNanoTime() >> FIRST_32_SECOND_64(16, 0)));
    return false;
  }

  INLINE void lock() {
    atomic_store_relaxed(&Precedence, 0);
    Mutex.Lock();
  }

  INLINE void unlock() { Mutex.Unlock(); }

  INLINE uptr getPrecedence() { return atomic_load_relaxed(&Precedence); }

 private:
  StaticSpinMutex Mutex;
  atomic_uintptr_t Precedence;
};

void initThread(bool MinimalInit);

// TSD model specific fastpath functions definitions.
#include "scudo_tsd_exclusive.inc"
#include "scudo_tsd_shared.inc"

}  // namespace __scudo

#endif  // SCUDO_TSD_H_
