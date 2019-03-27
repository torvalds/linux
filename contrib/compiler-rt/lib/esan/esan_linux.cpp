//===-- esan.cpp ----------------------------------------------------------===//
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
// Linux-specific code for the Esan run-time.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_FREEBSD || SANITIZER_LINUX

#include "esan.h"
#include "esan_shadow.h"
#include "interception/interception.h"
#include "sanitizer_common/sanitizer_common.h"
#include <sys/mman.h>
#include <errno.h>

namespace __esan {

void verifyAddressSpace() {
#if SANITIZER_LINUX && (defined(__x86_64__) || SANITIZER_MIPS64)
  // The kernel determines its mmap base from the stack size limit.
  // Our Linux 64-bit shadow mapping assumes the stack limit is less than a
  // terabyte, which keeps the mmap region above 0x7e00'.
  uptr StackLimit = GetStackSizeLimitInBytes();
  if (StackSizeIsUnlimited() || StackLimit > MaxStackSize) {
    VReport(1, "The stack size limit is beyond the maximum supported.\n"
            "Re-execing with a stack size below 1TB.\n");
    SetStackSizeLimitInBytes(MaxStackSize);
    ReExec();
  }
#endif
}

static bool liesWithinSingleAppRegion(uptr Start, SIZE_T Size) {
  uptr AppStart, AppEnd;
  for (int i = 0; getAppRegion(i, &AppStart, &AppEnd); ++i) {
    if (Start >= AppStart && Start + Size - 1 <= AppEnd) {
      return true;
    }
  }
  return false;
}

bool fixMmapAddr(void **Addr, SIZE_T Size, int Flags) {
  if (*Addr) {
    if (!liesWithinSingleAppRegion((uptr)*Addr, Size)) {
      VPrintf(1, "mmap conflict: [%p-%p) is not in an app region\n",
              *Addr, (uptr)*Addr + Size);
      if (Flags & MAP_FIXED) {
        errno = EINVAL;
        return false;
      } else {
        *Addr = 0;
      }
    }
  }
  return true;
}

uptr checkMmapResult(uptr Addr, SIZE_T Size) {
  if ((void *)Addr == MAP_FAILED)
    return Addr;
  if (!liesWithinSingleAppRegion(Addr, Size)) {
    // FIXME: attempt to dynamically add this as an app region if it
    // fits our shadow criteria.
    // We could also try to remap somewhere else.
    Printf("ERROR: unsupported mapping at [%p-%p)\n", Addr, Addr+Size);
    Die();
  }
  return Addr;
}

} // namespace __esan

#endif // SANITIZER_FREEBSD || SANITIZER_LINUX
