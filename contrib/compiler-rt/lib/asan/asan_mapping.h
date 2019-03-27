//===-- asan_mapping.h ------------------------------------------*- C++ -*-===//
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
// Defines ASan memory mapping.
//===----------------------------------------------------------------------===//
#ifndef ASAN_MAPPING_H
#define ASAN_MAPPING_H

#include "asan_internal.h"

// The full explanation of the memory mapping could be found here:
// https://github.com/google/sanitizers/wiki/AddressSanitizerAlgorithm
//
// Typical shadow mapping on Linux/x86_64 with SHADOW_OFFSET == 0x00007fff8000:
// || `[0x10007fff8000, 0x7fffffffffff]` || HighMem    ||
// || `[0x02008fff7000, 0x10007fff7fff]` || HighShadow ||
// || `[0x00008fff7000, 0x02008fff6fff]` || ShadowGap  ||
// || `[0x00007fff8000, 0x00008fff6fff]` || LowShadow  ||
// || `[0x000000000000, 0x00007fff7fff]` || LowMem     ||
//
// When SHADOW_OFFSET is zero (-pie):
// || `[0x100000000000, 0x7fffffffffff]` || HighMem    ||
// || `[0x020000000000, 0x0fffffffffff]` || HighShadow ||
// || `[0x000000040000, 0x01ffffffffff]` || ShadowGap  ||
//
// Special case when something is already mapped between
// 0x003000000000 and 0x005000000000 (e.g. when prelink is installed):
// || `[0x10007fff8000, 0x7fffffffffff]` || HighMem    ||
// || `[0x02008fff7000, 0x10007fff7fff]` || HighShadow ||
// || `[0x005000000000, 0x02008fff6fff]` || ShadowGap3 ||
// || `[0x003000000000, 0x004fffffffff]` || MidMem     ||
// || `[0x000a7fff8000, 0x002fffffffff]` || ShadowGap2 ||
// || `[0x00067fff8000, 0x000a7fff7fff]` || MidShadow  ||
// || `[0x00008fff7000, 0x00067fff7fff]` || ShadowGap  ||
// || `[0x00007fff8000, 0x00008fff6fff]` || LowShadow  ||
// || `[0x000000000000, 0x00007fff7fff]` || LowMem     ||
//
// Default Linux/i386 mapping on x86_64 machine:
// || `[0x40000000, 0xffffffff]` || HighMem    ||
// || `[0x28000000, 0x3fffffff]` || HighShadow ||
// || `[0x24000000, 0x27ffffff]` || ShadowGap  ||
// || `[0x20000000, 0x23ffffff]` || LowShadow  ||
// || `[0x00000000, 0x1fffffff]` || LowMem     ||
//
// Default Linux/i386 mapping on i386 machine
// (addresses starting with 0xc0000000 are reserved
// for kernel and thus not sanitized):
// || `[0x38000000, 0xbfffffff]` || HighMem    ||
// || `[0x27000000, 0x37ffffff]` || HighShadow ||
// || `[0x24000000, 0x26ffffff]` || ShadowGap  ||
// || `[0x20000000, 0x23ffffff]` || LowShadow  ||
// || `[0x00000000, 0x1fffffff]` || LowMem     ||
//
// Default Linux/MIPS32 mapping:
// || `[0x2aaa0000, 0xffffffff]` || HighMem    ||
// || `[0x0fff4000, 0x2aa9ffff]` || HighShadow ||
// || `[0x0bff4000, 0x0fff3fff]` || ShadowGap  ||
// || `[0x0aaa0000, 0x0bff3fff]` || LowShadow  ||
// || `[0x00000000, 0x0aa9ffff]` || LowMem     ||
//
// Default Linux/MIPS64 mapping:
// || `[0x4000000000, 0xffffffffff]` || HighMem    ||
// || `[0x2800000000, 0x3fffffffff]` || HighShadow ||
// || `[0x2400000000, 0x27ffffffff]` || ShadowGap  ||
// || `[0x2000000000, 0x23ffffffff]` || LowShadow  ||
// || `[0x0000000000, 0x1fffffffff]` || LowMem     ||
//
// Default Linux/AArch64 (39-bit VMA) mapping:
// || `[0x2000000000, 0x7fffffffff]` || highmem    ||
// || `[0x1400000000, 0x1fffffffff]` || highshadow ||
// || `[0x1200000000, 0x13ffffffff]` || shadowgap  ||
// || `[0x1000000000, 0x11ffffffff]` || lowshadow  ||
// || `[0x0000000000, 0x0fffffffff]` || lowmem     ||
//
// Default Linux/AArch64 (42-bit VMA) mapping:
// || `[0x10000000000, 0x3ffffffffff]` || highmem    ||
// || `[0x0a000000000, 0x0ffffffffff]` || highshadow ||
// || `[0x09000000000, 0x09fffffffff]` || shadowgap  ||
// || `[0x08000000000, 0x08fffffffff]` || lowshadow  ||
// || `[0x00000000000, 0x07fffffffff]` || lowmem     ||
//
// Default Linux/S390 mapping:
// || `[0x30000000, 0x7fffffff]` || HighMem    ||
// || `[0x26000000, 0x2fffffff]` || HighShadow ||
// || `[0x24000000, 0x25ffffff]` || ShadowGap  ||
// || `[0x20000000, 0x23ffffff]` || LowShadow  ||
// || `[0x00000000, 0x1fffffff]` || LowMem     ||
//
// Default Linux/SystemZ mapping:
// || `[0x14000000000000, 0x1fffffffffffff]` || HighMem    ||
// || `[0x12800000000000, 0x13ffffffffffff]` || HighShadow ||
// || `[0x12000000000000, 0x127fffffffffff]` || ShadowGap  ||
// || `[0x10000000000000, 0x11ffffffffffff]` || LowShadow  ||
// || `[0x00000000000000, 0x0fffffffffffff]` || LowMem     ||
//
// Shadow mapping on FreeBSD/x86-64 with SHADOW_OFFSET == 0x400000000000:
// || `[0x500000000000, 0x7fffffffffff]` || HighMem    ||
// || `[0x4a0000000000, 0x4fffffffffff]` || HighShadow ||
// || `[0x480000000000, 0x49ffffffffff]` || ShadowGap  ||
// || `[0x400000000000, 0x47ffffffffff]` || LowShadow  ||
// || `[0x000000000000, 0x3fffffffffff]` || LowMem     ||
//
// Shadow mapping on FreeBSD/i386 with SHADOW_OFFSET == 0x40000000:
// || `[0x60000000, 0xffffffff]` || HighMem    ||
// || `[0x4c000000, 0x5fffffff]` || HighShadow ||
// || `[0x48000000, 0x4bffffff]` || ShadowGap  ||
// || `[0x40000000, 0x47ffffff]` || LowShadow  ||
// || `[0x00000000, 0x3fffffff]` || LowMem     ||
//
// Shadow mapping on NetBSD/x86-64 with SHADOW_OFFSET == 0x400000000000:
// || `[0x4feffffffe01, 0x7f7ffffff000]` || HighMem    ||
// || `[0x49fdffffffc0, 0x4feffffffe00]` || HighShadow ||
// || `[0x480000000000, 0x49fdffffffbf]` || ShadowGap  ||
// || `[0x400000000000, 0x47ffffffffff]` || LowShadow  ||
// || `[0x000000000000, 0x3fffffffffff]` || LowMem     ||
//
// Shadow mapping on NetBSD/i386 with SHADOW_OFFSET == 0x40000000:
// || `[0x60000000, 0xfffff000]` || HighMem    ||
// || `[0x4c000000, 0x5fffffff]` || HighShadow ||
// || `[0x48000000, 0x4bffffff]` || ShadowGap  ||
// || `[0x40000000, 0x47ffffff]` || LowShadow  ||
// || `[0x00000000, 0x3fffffff]` || LowMem     ||
//
// Default Windows/i386 mapping:
// (the exact location of HighShadow/HighMem may vary depending
//  on WoW64, /LARGEADDRESSAWARE, etc).
// || `[0x50000000, 0xffffffff]` || HighMem    ||
// || `[0x3a000000, 0x4fffffff]` || HighShadow ||
// || `[0x36000000, 0x39ffffff]` || ShadowGap  ||
// || `[0x30000000, 0x35ffffff]` || LowShadow  ||
// || `[0x00000000, 0x2fffffff]` || LowMem     ||
//
// Shadow mapping on Myriad2 (for shadow scale 5):
// || `[0x9ff80000, 0x9fffffff]` || ShadowGap  ||
// || `[0x9f000000, 0x9ff7ffff]` || LowShadow  ||
// || `[0x80000000, 0x9effffff]` || LowMem     ||
// || `[0x00000000, 0x7fffffff]` || Ignored    ||

#if defined(ASAN_SHADOW_SCALE)
static const u64 kDefaultShadowScale = ASAN_SHADOW_SCALE;
#else
static const u64 kDefaultShadowScale = SANITIZER_MYRIAD2 ? 5 : 3;
#endif
static const u64 kDefaultShadowSentinel = ~(uptr)0;
static const u64 kDefaultShadowOffset32 = 1ULL << 29;  // 0x20000000
static const u64 kDefaultShadowOffset64 = 1ULL << 44;
static const u64 kDefaultShort64bitShadowOffset =
    0x7FFFFFFF & (~0xFFFULL << kDefaultShadowScale);  // < 2G.
static const u64 kIosShadowOffset32 = 1ULL << 30;  // 0x40000000
static const u64 kIosShadowOffset64 = 0x120200000;
static const u64 kIosSimShadowOffset32 = 1ULL << 30;
static const u64 kIosSimShadowOffset64 = kDefaultShadowOffset64;
static const u64 kAArch64_ShadowOffset64 = 1ULL << 36;
static const u64 kMIPS32_ShadowOffset32 = 0x0aaa0000;
static const u64 kMIPS64_ShadowOffset64 = 1ULL << 37;
static const u64 kPPC64_ShadowOffset64 = 1ULL << 44;
static const u64 kSystemZ_ShadowOffset64 = 1ULL << 52;
static const u64 kFreeBSD_ShadowOffset32 = 1ULL << 30;  // 0x40000000
static const u64 kFreeBSD_ShadowOffset64 = 1ULL << 46;  // 0x400000000000
static const u64 kNetBSD_ShadowOffset32 = 1ULL << 30;  // 0x40000000
static const u64 kNetBSD_ShadowOffset64 = 1ULL << 46;  // 0x400000000000
static const u64 kWindowsShadowOffset32 = 3ULL << 28;  // 0x30000000

static const u64 kMyriadMemoryOffset32 = 0x80000000ULL;
static const u64 kMyriadMemorySize32 = 0x20000000ULL;
static const u64 kMyriadMemoryEnd32 =
    kMyriadMemoryOffset32 + kMyriadMemorySize32 - 1;
static const u64 kMyriadShadowOffset32 =
    (kMyriadMemoryOffset32 + kMyriadMemorySize32 -
     (kMyriadMemorySize32 >> kDefaultShadowScale));
static const u64 kMyriadCacheBitMask32 = 0x40000000ULL;

#define SHADOW_SCALE kDefaultShadowScale

#if SANITIZER_FUCHSIA
#  define SHADOW_OFFSET (0)
#elif SANITIZER_WORDSIZE == 32
#  if SANITIZER_ANDROID
#    define SHADOW_OFFSET __asan_shadow_memory_dynamic_address
#  elif defined(__mips__)
#    define SHADOW_OFFSET kMIPS32_ShadowOffset32
#  elif SANITIZER_FREEBSD
#    define SHADOW_OFFSET kFreeBSD_ShadowOffset32
#  elif SANITIZER_NETBSD
#    define SHADOW_OFFSET kNetBSD_ShadowOffset32
#  elif SANITIZER_WINDOWS
#    define SHADOW_OFFSET kWindowsShadowOffset32
#  elif SANITIZER_IOS
#    if SANITIZER_IOSSIM
#      define SHADOW_OFFSET kIosSimShadowOffset32
#    else
#      define SHADOW_OFFSET kIosShadowOffset32
#    endif
#  elif SANITIZER_MYRIAD2
#    define SHADOW_OFFSET kMyriadShadowOffset32
#  else
#    define SHADOW_OFFSET kDefaultShadowOffset32
#  endif
#else
#  if SANITIZER_IOS
#    if SANITIZER_IOSSIM
#      define SHADOW_OFFSET kIosSimShadowOffset64
#    else
#      define SHADOW_OFFSET __asan_shadow_memory_dynamic_address
#    endif
#  elif defined(__aarch64__)
#    define SHADOW_OFFSET kAArch64_ShadowOffset64
#  elif defined(__powerpc64__)
#    define SHADOW_OFFSET kPPC64_ShadowOffset64
#  elif defined(__s390x__)
#    define SHADOW_OFFSET kSystemZ_ShadowOffset64
#  elif SANITIZER_FREEBSD
#    define SHADOW_OFFSET kFreeBSD_ShadowOffset64
#  elif SANITIZER_NETBSD
#    define SHADOW_OFFSET kNetBSD_ShadowOffset64
#  elif SANITIZER_MAC
#   define SHADOW_OFFSET kDefaultShadowOffset64
#  elif defined(__mips64)
#   define SHADOW_OFFSET kMIPS64_ShadowOffset64
#  elif SANITIZER_WINDOWS64
#   define SHADOW_OFFSET __asan_shadow_memory_dynamic_address
#  else
#   define SHADOW_OFFSET kDefaultShort64bitShadowOffset
#  endif
#endif

#if SANITIZER_ANDROID && defined(__arm__)
# define ASAN_PREMAP_SHADOW 1
#else
# define ASAN_PREMAP_SHADOW 0
#endif

#define SHADOW_GRANULARITY (1ULL << SHADOW_SCALE)

#define DO_ASAN_MAPPING_PROFILE 0  // Set to 1 to profile the functions below.

#if DO_ASAN_MAPPING_PROFILE
# define PROFILE_ASAN_MAPPING() AsanMappingProfile[__LINE__]++;
#else
# define PROFILE_ASAN_MAPPING()
#endif

// If 1, all shadow boundaries are constants.
// Don't set to 1 other than for testing.
#define ASAN_FIXED_MAPPING 0

namespace __asan {

extern uptr AsanMappingProfile[];

#if ASAN_FIXED_MAPPING
// Fixed mapping for 64-bit Linux. Mostly used for performance comparison
// with non-fixed mapping. As of r175253 (Feb 2013) the performance
// difference between fixed and non-fixed mapping is below the noise level.
static uptr kHighMemEnd = 0x7fffffffffffULL;
static uptr kMidMemBeg =    0x3000000000ULL;
static uptr kMidMemEnd =    0x4fffffffffULL;
#else
extern uptr kHighMemEnd, kMidMemBeg, kMidMemEnd;  // Initialized in __asan_init.
#endif

}  // namespace __asan

#if SANITIZER_MYRIAD2
#include "asan_mapping_myriad.h"
#else
#define MEM_TO_SHADOW(mem) (((mem) >> SHADOW_SCALE) + (SHADOW_OFFSET))

#define kLowMemBeg      0
#define kLowMemEnd      (SHADOW_OFFSET ? SHADOW_OFFSET - 1 : 0)

#define kLowShadowBeg   SHADOW_OFFSET
#define kLowShadowEnd   MEM_TO_SHADOW(kLowMemEnd)

#define kHighMemBeg     (MEM_TO_SHADOW(kHighMemEnd) + 1)

#define kHighShadowBeg  MEM_TO_SHADOW(kHighMemBeg)
#define kHighShadowEnd  MEM_TO_SHADOW(kHighMemEnd)

# define kMidShadowBeg MEM_TO_SHADOW(kMidMemBeg)
# define kMidShadowEnd MEM_TO_SHADOW(kMidMemEnd)

// With the zero shadow base we can not actually map pages starting from 0.
// This constant is somewhat arbitrary.
#define kZeroBaseShadowStart 0
#define kZeroBaseMaxShadowStart (1 << 18)

#define kShadowGapBeg   (kLowShadowEnd ? kLowShadowEnd + 1 \
                                       : kZeroBaseShadowStart)
#define kShadowGapEnd   ((kMidMemBeg ? kMidShadowBeg : kHighShadowBeg) - 1)

#define kShadowGap2Beg (kMidMemBeg ? kMidShadowEnd + 1 : 0)
#define kShadowGap2End (kMidMemBeg ? kMidMemBeg - 1 : 0)

#define kShadowGap3Beg (kMidMemBeg ? kMidMemEnd + 1 : 0)
#define kShadowGap3End (kMidMemBeg ? kHighShadowBeg - 1 : 0)

namespace __asan {

static inline bool AddrIsInLowMem(uptr a) {
  PROFILE_ASAN_MAPPING();
  return a <= kLowMemEnd;
}

static inline bool AddrIsInLowShadow(uptr a) {
  PROFILE_ASAN_MAPPING();
  return a >= kLowShadowBeg && a <= kLowShadowEnd;
}

static inline bool AddrIsInMidMem(uptr a) {
  PROFILE_ASAN_MAPPING();
  return kMidMemBeg && a >= kMidMemBeg && a <= kMidMemEnd;
}

static inline bool AddrIsInMidShadow(uptr a) {
  PROFILE_ASAN_MAPPING();
  return kMidMemBeg && a >= kMidShadowBeg && a <= kMidShadowEnd;
}

static inline bool AddrIsInHighMem(uptr a) {
  PROFILE_ASAN_MAPPING();
  return kHighMemBeg && a >= kHighMemBeg && a <= kHighMemEnd;
}

static inline bool AddrIsInHighShadow(uptr a) {
  PROFILE_ASAN_MAPPING();
  return kHighMemBeg && a >= kHighShadowBeg && a <= kHighShadowEnd;
}

static inline bool AddrIsInShadowGap(uptr a) {
  PROFILE_ASAN_MAPPING();
  if (kMidMemBeg) {
    if (a <= kShadowGapEnd)
      return SHADOW_OFFSET == 0 || a >= kShadowGapBeg;
    return (a >= kShadowGap2Beg && a <= kShadowGap2End) ||
           (a >= kShadowGap3Beg && a <= kShadowGap3End);
  }
  // In zero-based shadow mode we treat addresses near zero as addresses
  // in shadow gap as well.
  if (SHADOW_OFFSET == 0)
    return a <= kShadowGapEnd;
  return a >= kShadowGapBeg && a <= kShadowGapEnd;
}

}  // namespace __asan

#endif  // SANITIZER_MYRIAD2

namespace __asan {

static inline bool AddrIsInMem(uptr a) {
  PROFILE_ASAN_MAPPING();
  return AddrIsInLowMem(a) || AddrIsInMidMem(a) || AddrIsInHighMem(a) ||
      (flags()->protect_shadow_gap == 0 && AddrIsInShadowGap(a));
}

static inline uptr MemToShadow(uptr p) {
  PROFILE_ASAN_MAPPING();
  CHECK(AddrIsInMem(p));
  return MEM_TO_SHADOW(p);
}

static inline bool AddrIsInShadow(uptr a) {
  PROFILE_ASAN_MAPPING();
  return AddrIsInLowShadow(a) || AddrIsInMidShadow(a) || AddrIsInHighShadow(a);
}

static inline bool AddrIsAlignedByGranularity(uptr a) {
  PROFILE_ASAN_MAPPING();
  return (a & (SHADOW_GRANULARITY - 1)) == 0;
}

static inline bool AddressIsPoisoned(uptr a) {
  PROFILE_ASAN_MAPPING();
  if (SANITIZER_MYRIAD2 && !AddrIsInMem(a) && !AddrIsInShadow(a))
    return false;
  const uptr kAccessSize = 1;
  u8 *shadow_address = (u8*)MEM_TO_SHADOW(a);
  s8 shadow_value = *shadow_address;
  if (shadow_value) {
    u8 last_accessed_byte = (a & (SHADOW_GRANULARITY - 1))
                                 + kAccessSize - 1;
    return (last_accessed_byte >= shadow_value);
  }
  return false;
}

// Must be after all calls to PROFILE_ASAN_MAPPING().
static const uptr kAsanMappingProfileSize = __LINE__;

}  // namespace __asan

#endif  // ASAN_MAPPING_H
