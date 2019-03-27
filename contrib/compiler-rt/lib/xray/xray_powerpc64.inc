//===-- xray_powerpc64.inc --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <mutex>
#include <sys/platform/ppc.h>

#include "xray_defs.h"

namespace __xray {

ALWAYS_INLINE uint64_t readTSC(uint8_t &CPU) XRAY_NEVER_INSTRUMENT {
  CPU = 0;
  return __ppc_get_timebase();
}

inline uint64_t getTSCFrequency() XRAY_NEVER_INSTRUMENT {
  static std::mutex M;
  std::lock_guard<std::mutex> Guard(M);
  return __ppc_get_timebase_freq();
}

inline bool probeRequiredCPUFeatures() XRAY_NEVER_INSTRUMENT {
  return true;
}

} // namespace __xray
