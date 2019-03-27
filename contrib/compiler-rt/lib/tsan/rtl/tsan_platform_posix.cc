//===-- tsan_platform_posix.cc --------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// POSIX-specific code.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_POSIX

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_errno.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_procmaps.h"
#include "tsan_platform.h"
#include "tsan_rtl.h"

namespace __tsan {

static const char kShadowMemoryMappingWarning[] =
    "FATAL: %s can not madvise shadow region [%zx, %zx] with %s (errno: %d)\n";
static const char kShadowMemoryMappingHint[] =
    "HINT: if %s is not supported in your environment, you may set "
    "TSAN_OPTIONS=%s=0\n";

static void NoHugePagesInShadow(uptr addr, uptr size) {
  if (common_flags()->no_huge_pages_for_shadow)
    if (!NoHugePagesInRegion(addr, size)) {
      Printf(kShadowMemoryMappingWarning, SanitizerToolName, addr, addr + size,
             "MADV_NOHUGEPAGE", errno);
      Printf(kShadowMemoryMappingHint, "MADV_NOHUGEPAGE",
             "no_huge_pages_for_shadow");
      Die();
    }
}

static void DontDumpShadow(uptr addr, uptr size) {
  if (common_flags()->use_madv_dontdump)
    if (!DontDumpShadowMemory(addr, size)) {
      Printf(kShadowMemoryMappingWarning, SanitizerToolName, addr, addr + size,
             "MADV_DONTDUMP", errno);
      Printf(kShadowMemoryMappingHint, "MADV_DONTDUMP", "use_madv_dontdump");
      Die();
    }
}

#if !SANITIZER_GO
void InitializeShadowMemory() {
  // Map memory shadow.
  if (!MmapFixedNoReserve(ShadowBeg(), ShadowEnd() - ShadowBeg(), "shadow")) {
    Printf("FATAL: ThreadSanitizer can not mmap the shadow memory\n");
    Printf("FATAL: Make sure to compile with -fPIE and to link with -pie.\n");
    Die();
  }
  // This memory range is used for thread stacks and large user mmaps.
  // Frequently a thread uses only a small part of stack and similarly
  // a program uses a small part of large mmap. On some programs
  // we see 20% memory usage reduction without huge pages for this range.
  // FIXME: don't use constants here.
#if defined(__x86_64__)
  const uptr kMadviseRangeBeg  = 0x7f0000000000ull;
  const uptr kMadviseRangeSize = 0x010000000000ull;
#elif defined(__mips64)
  const uptr kMadviseRangeBeg  = 0xff00000000ull;
  const uptr kMadviseRangeSize = 0x0100000000ull;
#elif defined(__aarch64__) && defined(__APPLE__)
  uptr kMadviseRangeBeg = LoAppMemBeg();
  uptr kMadviseRangeSize = LoAppMemEnd() - LoAppMemBeg();
#elif defined(__aarch64__)
  uptr kMadviseRangeBeg = 0;
  uptr kMadviseRangeSize = 0;
  if (vmaSize == 39) {
    kMadviseRangeBeg  = 0x7d00000000ull;
    kMadviseRangeSize = 0x0300000000ull;
  } else if (vmaSize == 42) {
    kMadviseRangeBeg  = 0x3f000000000ull;
    kMadviseRangeSize = 0x01000000000ull;
  } else {
    DCHECK(0);
  }
#elif defined(__powerpc64__)
  uptr kMadviseRangeBeg = 0;
  uptr kMadviseRangeSize = 0;
  if (vmaSize == 44) {
    kMadviseRangeBeg  = 0x0f60000000ull;
    kMadviseRangeSize = 0x0010000000ull;
  } else if (vmaSize == 46) {
    kMadviseRangeBeg  = 0x3f0000000000ull;
    kMadviseRangeSize = 0x010000000000ull;
  } else {
    DCHECK(0);
  }
#endif
  NoHugePagesInShadow(MemToShadow(kMadviseRangeBeg),
                      kMadviseRangeSize * kShadowMultiplier);
  DontDumpShadow(ShadowBeg(), ShadowEnd() - ShadowBeg());
  DPrintf("memory shadow: %zx-%zx (%zuGB)\n",
      ShadowBeg(), ShadowEnd(),
      (ShadowEnd() - ShadowBeg()) >> 30);

  // Map meta shadow.
  const uptr meta = MetaShadowBeg();
  const uptr meta_size = MetaShadowEnd() - meta;
  if (!MmapFixedNoReserve(meta, meta_size, "meta shadow")) {
    Printf("FATAL: ThreadSanitizer can not mmap the shadow memory\n");
    Printf("FATAL: Make sure to compile with -fPIE and to link with -pie.\n");
    Die();
  }
  NoHugePagesInShadow(meta, meta_size);
  DontDumpShadow(meta, meta_size);
  DPrintf("meta shadow: %zx-%zx (%zuGB)\n",
      meta, meta + meta_size, meta_size >> 30);

  InitializeShadowMemoryPlatform();
}

static void ProtectRange(uptr beg, uptr end) {
  CHECK_LE(beg, end);
  if (beg == end)
    return;
  if (beg != (uptr)MmapFixedNoAccess(beg, end - beg)) {
    Printf("FATAL: ThreadSanitizer can not protect [%zx,%zx]\n", beg, end);
    Printf("FATAL: Make sure you are not using unlimited stack\n");
    Die();
  }
}

void CheckAndProtect() {
  // Ensure that the binary is indeed compiled with -pie.
  MemoryMappingLayout proc_maps(true);
  MemoryMappedSegment segment;
  while (proc_maps.Next(&segment)) {
    if (IsAppMem(segment.start)) continue;
    if (segment.start >= HeapMemEnd() && segment.start < HeapEnd()) continue;
    if (segment.protection == 0)  // Zero page or mprotected.
      continue;
    if (segment.start >= VdsoBeg())  // vdso
      break;
    Printf("FATAL: ThreadSanitizer: unexpected memory mapping %p-%p\n",
           segment.start, segment.end);
    Die();
  }

#if defined(__aarch64__) && defined(__APPLE__)
  ProtectRange(HeapMemEnd(), ShadowBeg());
  ProtectRange(ShadowEnd(), MetaShadowBeg());
  ProtectRange(MetaShadowEnd(), TraceMemBeg());
#else
  ProtectRange(LoAppMemEnd(), ShadowBeg());
  ProtectRange(ShadowEnd(), MetaShadowBeg());
#ifdef TSAN_MID_APP_RANGE
  ProtectRange(MetaShadowEnd(), MidAppMemBeg());
  ProtectRange(MidAppMemEnd(), TraceMemBeg());
#else
  ProtectRange(MetaShadowEnd(), TraceMemBeg());
#endif
  // Memory for traces is mapped lazily in MapThreadTrace.
  // Protect the whole range for now, so that user does not map something here.
  ProtectRange(TraceMemBeg(), TraceMemEnd());
  ProtectRange(TraceMemEnd(), HeapMemBeg());
  ProtectRange(HeapEnd(), HiAppMemBeg());
#endif
}
#endif

}  // namespace __tsan

#endif  // SANITIZER_POSIX
