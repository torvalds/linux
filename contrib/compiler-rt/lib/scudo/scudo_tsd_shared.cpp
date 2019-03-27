//===-- scudo_tsd_shared.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// Scudo shared TSD implementation.
///
//===----------------------------------------------------------------------===//

#include "scudo_tsd.h"

#if !SCUDO_TSD_EXCLUSIVE

namespace __scudo {

static pthread_once_t GlobalInitialized = PTHREAD_ONCE_INIT;
pthread_key_t PThreadKey;

static atomic_uint32_t CurrentIndex;
static ScudoTSD *TSDs;
static u32 NumberOfTSDs;
static u32 CoPrimes[SCUDO_SHARED_TSD_POOL_SIZE];
static u32 NumberOfCoPrimes = 0;

#if SANITIZER_LINUX && !SANITIZER_ANDROID
__attribute__((tls_model("initial-exec")))
THREADLOCAL ScudoTSD *CurrentTSD;
#endif

static void initOnce() {
  CHECK_EQ(pthread_key_create(&PThreadKey, NULL), 0);
  initScudo();
  NumberOfTSDs = Min(Max(1U, GetNumberOfCPUsCached()),
                     static_cast<u32>(SCUDO_SHARED_TSD_POOL_SIZE));
  TSDs = reinterpret_cast<ScudoTSD *>(
      MmapOrDie(sizeof(ScudoTSD) * NumberOfTSDs, "ScudoTSDs"));
  for (u32 I = 0; I < NumberOfTSDs; I++) {
    TSDs[I].init();
    u32 A = I + 1;
    u32 B = NumberOfTSDs;
    while (B != 0) { const u32 T = A; A = B; B = T % B; }
    if (A == 1)
      CoPrimes[NumberOfCoPrimes++] = I + 1;
  }
}

ALWAYS_INLINE void setCurrentTSD(ScudoTSD *TSD) {
#if SANITIZER_ANDROID
  *get_android_tls_ptr() = reinterpret_cast<uptr>(TSD);
#elif SANITIZER_LINUX
  CurrentTSD = TSD;
#else
  CHECK_EQ(pthread_setspecific(PThreadKey, reinterpret_cast<void *>(TSD)), 0);
#endif  // SANITIZER_ANDROID
}

void initThread(bool MinimalInit) {
  pthread_once(&GlobalInitialized, initOnce);
  // Initial context assignment is done in a plain round-robin fashion.
  u32 Index = atomic_fetch_add(&CurrentIndex, 1, memory_order_relaxed);
  setCurrentTSD(&TSDs[Index % NumberOfTSDs]);
}

ScudoTSD *getTSDAndLockSlow(ScudoTSD *TSD) {
  if (NumberOfTSDs > 1) {
    // Use the Precedence of the current TSD as our random seed. Since we are in
    // the slow path, it means that tryLock failed, and as a result it's very
    // likely that said Precedence is non-zero.
    u32 RandState = static_cast<u32>(TSD->getPrecedence());
    const u32 R = Rand(&RandState);
    const u32 Inc = CoPrimes[R % NumberOfCoPrimes];
    u32 Index = R % NumberOfTSDs;
    uptr LowestPrecedence = UINTPTR_MAX;
    ScudoTSD *CandidateTSD = nullptr;
    // Go randomly through at most 4 contexts and find a candidate.
    for (u32 I = 0; I < Min(4U, NumberOfTSDs); I++) {
      if (TSDs[Index].tryLock()) {
        setCurrentTSD(&TSDs[Index]);
        return &TSDs[Index];
      }
      const uptr Precedence = TSDs[Index].getPrecedence();
      // A 0 precedence here means another thread just locked this TSD.
      if (UNLIKELY(Precedence == 0))
        continue;
      if (Precedence < LowestPrecedence) {
        CandidateTSD = &TSDs[Index];
        LowestPrecedence = Precedence;
      }
      Index += Inc;
      if (Index >= NumberOfTSDs)
        Index -= NumberOfTSDs;
    }
    if (CandidateTSD) {
      CandidateTSD->lock();
      setCurrentTSD(CandidateTSD);
      return CandidateTSD;
    }
  }
  // Last resort, stick with the current one.
  TSD->lock();
  return TSD;
}

}  // namespace __scudo

#endif  // !SCUDO_TSD_EXCLUSIVE
