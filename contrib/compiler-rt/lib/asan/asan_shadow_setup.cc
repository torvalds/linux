//===-- asan_shadow_setup.cc ----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Set up the shadow memory.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"

// asan_fuchsia.cc and asan_rtems.cc have their own
// InitializeShadowMemory implementation.
#if !SANITIZER_FUCHSIA && !SANITIZER_RTEMS

#include "asan_internal.h"
#include "asan_mapping.h"

namespace __asan {

// ---------------------- mmap -------------------- {{{1
// Reserve memory range [beg, end].
// We need to use inclusive range because end+1 may not be representable.
void ReserveShadowMemoryRange(uptr beg, uptr end, const char *name) {
  CHECK_EQ((beg % GetMmapGranularity()), 0);
  CHECK_EQ(((end + 1) % GetMmapGranularity()), 0);
  uptr size = end - beg + 1;
  DecreaseTotalMmap(size);  // Don't count the shadow against mmap_limit_mb.
  if (!MmapFixedNoReserve(beg, size, name)) {
    Report(
        "ReserveShadowMemoryRange failed while trying to map 0x%zx bytes. "
        "Perhaps you're using ulimit -v\n",
        size);
    Abort();
  }
  if (common_flags()->no_huge_pages_for_shadow) NoHugePagesInRegion(beg, size);
  if (common_flags()->use_madv_dontdump) DontDumpShadowMemory(beg, size);
}

static void ProtectGap(uptr addr, uptr size) {
  if (!flags()->protect_shadow_gap) {
    // The shadow gap is unprotected, so there is a chance that someone
    // is actually using this memory. Which means it needs a shadow...
    uptr GapShadowBeg = RoundDownTo(MEM_TO_SHADOW(addr), GetPageSizeCached());
    uptr GapShadowEnd =
        RoundUpTo(MEM_TO_SHADOW(addr + size), GetPageSizeCached()) - 1;
    if (Verbosity())
      Printf(
          "protect_shadow_gap=0:"
          " not protecting shadow gap, allocating gap's shadow\n"
          "|| `[%p, %p]` || ShadowGap's shadow ||\n",
          GapShadowBeg, GapShadowEnd);
    ReserveShadowMemoryRange(GapShadowBeg, GapShadowEnd,
                             "unprotected gap shadow");
    return;
  }
  void *res = MmapFixedNoAccess(addr, size, "shadow gap");
  if (addr == (uptr)res) return;
  // A few pages at the start of the address space can not be protected.
  // But we really want to protect as much as possible, to prevent this memory
  // being returned as a result of a non-FIXED mmap().
  if (addr == kZeroBaseShadowStart) {
    uptr step = GetMmapGranularity();
    while (size > step && addr < kZeroBaseMaxShadowStart) {
      addr += step;
      size -= step;
      void *res = MmapFixedNoAccess(addr, size, "shadow gap");
      if (addr == (uptr)res) return;
    }
  }

  Report(
      "ERROR: Failed to protect the shadow gap. "
      "ASan cannot proceed correctly. ABORTING.\n");
  DumpProcessMap();
  Die();
}

static void MaybeReportLinuxPIEBug() {
#if SANITIZER_LINUX && (defined(__x86_64__) || defined(__aarch64__))
  Report("This might be related to ELF_ET_DYN_BASE change in Linux 4.12.\n");
  Report(
      "See https://github.com/google/sanitizers/issues/856 for possible "
      "workarounds.\n");
#endif
}

void InitializeShadowMemory() {
  // Set the shadow memory address to uninitialized.
  __asan_shadow_memory_dynamic_address = kDefaultShadowSentinel;

  uptr shadow_start = kLowShadowBeg;
  // Detect if a dynamic shadow address must used and find a available location
  // when necessary. When dynamic address is used, the macro |kLowShadowBeg|
  // expands to |__asan_shadow_memory_dynamic_address| which is
  // |kDefaultShadowSentinel|.
  bool full_shadow_is_available = false;
  if (shadow_start == kDefaultShadowSentinel) {
    __asan_shadow_memory_dynamic_address = 0;
    CHECK_EQ(0, kLowShadowBeg);
    shadow_start = FindDynamicShadowStart();
    if (SANITIZER_LINUX) full_shadow_is_available = true;
  }
  // Update the shadow memory address (potentially) used by instrumentation.
  __asan_shadow_memory_dynamic_address = shadow_start;

  if (kLowShadowBeg) shadow_start -= GetMmapGranularity();

  if (!full_shadow_is_available)
    full_shadow_is_available =
        MemoryRangeIsAvailable(shadow_start, kHighShadowEnd);

#if SANITIZER_LINUX && defined(__x86_64__) && defined(_LP64) && \
    !ASAN_FIXED_MAPPING
  if (!full_shadow_is_available) {
    kMidMemBeg = kLowMemEnd < 0x3000000000ULL ? 0x3000000000ULL : 0;
    kMidMemEnd = kLowMemEnd < 0x3000000000ULL ? 0x4fffffffffULL : 0;
  }
#endif

  if (Verbosity()) PrintAddressSpaceLayout();

  if (full_shadow_is_available) {
    // mmap the low shadow plus at least one page at the left.
    if (kLowShadowBeg)
      ReserveShadowMemoryRange(shadow_start, kLowShadowEnd, "low shadow");
    // mmap the high shadow.
    ReserveShadowMemoryRange(kHighShadowBeg, kHighShadowEnd, "high shadow");
    // protect the gap.
    ProtectGap(kShadowGapBeg, kShadowGapEnd - kShadowGapBeg + 1);
    CHECK_EQ(kShadowGapEnd, kHighShadowBeg - 1);
  } else if (kMidMemBeg &&
             MemoryRangeIsAvailable(shadow_start, kMidMemBeg - 1) &&
             MemoryRangeIsAvailable(kMidMemEnd + 1, kHighShadowEnd)) {
    CHECK(kLowShadowBeg != kLowShadowEnd);
    // mmap the low shadow plus at least one page at the left.
    ReserveShadowMemoryRange(shadow_start, kLowShadowEnd, "low shadow");
    // mmap the mid shadow.
    ReserveShadowMemoryRange(kMidShadowBeg, kMidShadowEnd, "mid shadow");
    // mmap the high shadow.
    ReserveShadowMemoryRange(kHighShadowBeg, kHighShadowEnd, "high shadow");
    // protect the gaps.
    ProtectGap(kShadowGapBeg, kShadowGapEnd - kShadowGapBeg + 1);
    ProtectGap(kShadowGap2Beg, kShadowGap2End - kShadowGap2Beg + 1);
    ProtectGap(kShadowGap3Beg, kShadowGap3End - kShadowGap3Beg + 1);
  } else {
    Report(
        "Shadow memory range interleaves with an existing memory mapping. "
        "ASan cannot proceed correctly. ABORTING.\n");
    Report("ASan shadow was supposed to be located in the [%p-%p] range.\n",
           shadow_start, kHighShadowEnd);
    MaybeReportLinuxPIEBug();
    DumpProcessMap();
    Die();
  }
}

}  // namespace __asan

#endif  // !SANITIZER_FUCHSIA && !SANITIZER_RTEMS
