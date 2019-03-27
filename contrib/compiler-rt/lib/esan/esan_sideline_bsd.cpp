//===-- esan_sideline_bsd.cpp -----------------------------------*- C++ -*-===//
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
// Support for a separate or "sideline" tool thread on FreeBSD.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_FREEBSD

#include "esan_sideline.h"

namespace __esan {

static SidelineThread *TheThread;

bool SidelineThread::launchThread(SidelineFunc takeSample, void *Arg,
                                  u32 FreqMilliSec) {
  return true;
}

bool SidelineThread::joinThread() {
  return true;
}

} // namespace __esan

#endif // SANITIZER_FREEBSD
