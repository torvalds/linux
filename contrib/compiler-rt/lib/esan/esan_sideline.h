//===-- esan_sideline.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of EfficiencySanitizer, a family of performance tuners.
//
// Esan sideline thread support.
//===----------------------------------------------------------------------===//

#ifndef ESAN_SIDELINE_H
#define ESAN_SIDELINE_H

#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_platform_limits_freebsd.h"
#include "sanitizer_common/sanitizer_platform_limits_posix.h"

namespace __esan {

typedef void (*SidelineFunc)(void *Arg);

// Currently only one sideline thread is supported.
// It calls the SidelineFunc passed to launchThread once on each sample at the
// given frequency in real time (i.e., wall clock time).
class SidelineThread {
public:
  // We cannot initialize any fields in the constructor as it will be called
  // *after* launchThread for a static instance, as esan.module_ctor is called
  // before static initializers.
  SidelineThread() {}
  ~SidelineThread() {}

  // To simplify declaration in sanitizer code where we want to avoid
  // heap allocations, the constructor and destructor do nothing and
  // launchThread and joinThread do the real work.
  // They should each be called just once.
  bool launchThread(SidelineFunc takeSample, void *Arg, u32 FreqMilliSec);
  bool joinThread();

  // Must be called from the sideline thread itself.
  bool adjustTimer(u32 FreqMilliSec);

private:
  static int runSideline(void *Arg);
  static void registerSignal(int SigNum);
  static void handleSidelineSignal(int SigNum, __sanitizer_siginfo *SigInfo,
                                   void *Ctx);

  char *Stack;
  SidelineFunc sampleFunc;
  void *FuncArg;
  u32 Freq;
  uptr SidelineId;
  atomic_uintptr_t SidelineExit;
};

} // namespace __esan

#endif  // ESAN_SIDELINE_H
