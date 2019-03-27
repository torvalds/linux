//===-- scudo_tsd_exclusive.inc ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// Scudo exclusive TSD fastpath functions implementation.
///
//===----------------------------------------------------------------------===//

#ifndef SCUDO_TSD_H_
# error "This file must be included inside scudo_tsd.h."
#endif  // SCUDO_TSD_H_

#if SCUDO_TSD_EXCLUSIVE

enum ThreadState : u8 {
  ThreadNotInitialized = 0,
  ThreadInitialized,
  ThreadTornDown,
};
__attribute__((tls_model("initial-exec")))
extern THREADLOCAL ThreadState ScudoThreadState;
__attribute__((tls_model("initial-exec")))
extern THREADLOCAL ScudoTSD TSD;

extern ScudoTSD FallbackTSD;

ALWAYS_INLINE void initThreadMaybe(bool MinimalInit = false) {
  if (LIKELY(ScudoThreadState != ThreadNotInitialized))
    return;
  initThread(MinimalInit);
}

ALWAYS_INLINE ScudoTSD *getTSDAndLock(bool *UnlockRequired) {
  if (UNLIKELY(ScudoThreadState != ThreadInitialized)) {
    FallbackTSD.lock();
    *UnlockRequired = true;
    return &FallbackTSD;
  }
  *UnlockRequired = false;
  return &TSD;
}

#endif  // SCUDO_TSD_EXCLUSIVE
