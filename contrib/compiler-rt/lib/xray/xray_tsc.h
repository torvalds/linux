//===-- xray_tsc.h ----------------------------------------------*- C++ -*-===//
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
#ifndef XRAY_EMULATE_TSC_H
#define XRAY_EMULATE_TSC_H

#include "sanitizer_common/sanitizer_common.h"

namespace __xray {
static constexpr uint64_t NanosecondsPerSecond = 1000ULL * 1000 * 1000;
}

#if SANITIZER_FUCHSIA
#include <zircon/syscalls.h>

namespace __xray {

inline bool probeRequiredCPUFeatures() XRAY_NEVER_INSTRUMENT { return true; }

ALWAYS_INLINE uint64_t readTSC(uint8_t &CPU) XRAY_NEVER_INSTRUMENT {
  CPU = 0;
  return _zx_ticks_get();
}

inline uint64_t getTSCFrequency() XRAY_NEVER_INSTRUMENT {
  return _zx_ticks_per_second();
}

} // namespace __xray

#else // SANITIZER_FUCHSIA

#if defined(__x86_64__)
#include "xray_x86_64.inc"
#elif defined(__powerpc64__)
#include "xray_powerpc64.inc"
#elif defined(__arm__) || defined(__aarch64__) || defined(__mips__)
// Emulated TSC.
// There is no instruction like RDTSCP in user mode on ARM. ARM's CP15 does
//   not have a constant frequency like TSC on x86(_64), it may go faster
//   or slower depending on CPU turbo or power saving mode. Furthermore,
//   to read from CP15 on ARM a kernel modification or a driver is needed.
//   We can not require this from users of compiler-rt.
// So on ARM we use clock_gettime() which gives the result in nanoseconds.
//   To get the measurements per second, we scale this by the number of
//   nanoseconds per second, pretending that the TSC frequency is 1GHz and
//   one TSC tick is 1 nanosecond.
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "xray_defs.h"
#include <cerrno>
#include <cstdint>
#include <time.h>

namespace __xray {

inline bool probeRequiredCPUFeatures() XRAY_NEVER_INSTRUMENT { return true; }

ALWAYS_INLINE uint64_t readTSC(uint8_t &CPU) XRAY_NEVER_INSTRUMENT {
  timespec TS;
  int result = clock_gettime(CLOCK_REALTIME, &TS);
  if (result != 0) {
    Report("clock_gettime(2) returned %d, errno=%d.", result, int(errno));
    TS.tv_sec = 0;
    TS.tv_nsec = 0;
  }
  CPU = 0;
  return TS.tv_sec * NanosecondsPerSecond + TS.tv_nsec;
}

inline uint64_t getTSCFrequency() XRAY_NEVER_INSTRUMENT {
  return NanosecondsPerSecond;
}

} // namespace __xray

#else
#error Target architecture is not supported.
#endif // CPU architecture
#endif // SANITIZER_FUCHSIA

#endif // XRAY_EMULATE_TSC_H
