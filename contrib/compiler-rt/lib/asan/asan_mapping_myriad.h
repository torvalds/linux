//===-- asan_mapping_myriad.h -----------------------------------*- C++ -*-===//
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
// Myriad-specific definitions for ASan memory mapping.
//===----------------------------------------------------------------------===//
#ifndef ASAN_MAPPING_MYRIAD_H
#define ASAN_MAPPING_MYRIAD_H

#define RAW_ADDR(mem) ((mem) & ~kMyriadCacheBitMask32)
#define MEM_TO_SHADOW(mem) \
  (((RAW_ADDR(mem) - kLowMemBeg) >> SHADOW_SCALE) + (SHADOW_OFFSET))

#define kLowMemBeg     kMyriadMemoryOffset32
#define kLowMemEnd     (SHADOW_OFFSET - 1)

#define kLowShadowBeg  SHADOW_OFFSET
#define kLowShadowEnd  MEM_TO_SHADOW(kLowMemEnd)

#define kHighMemBeg    0

#define kHighShadowBeg 0
#define kHighShadowEnd 0

#define kMidShadowBeg  0
#define kMidShadowEnd  0

#define kShadowGapBeg  (kLowShadowEnd + 1)
#define kShadowGapEnd  kMyriadMemoryEnd32

#define kShadowGap2Beg 0
#define kShadowGap2End 0

#define kShadowGap3Beg 0
#define kShadowGap3End 0

namespace __asan {

static inline bool AddrIsInLowMem(uptr a) {
  PROFILE_ASAN_MAPPING();
  a = RAW_ADDR(a);
  return a >= kLowMemBeg && a <= kLowMemEnd;
}

static inline bool AddrIsInLowShadow(uptr a) {
  PROFILE_ASAN_MAPPING();
  a = RAW_ADDR(a);
  return a >= kLowShadowBeg && a <= kLowShadowEnd;
}

static inline bool AddrIsInMidMem(uptr a) {
  PROFILE_ASAN_MAPPING();
  return false;
}

static inline bool AddrIsInMidShadow(uptr a) {
  PROFILE_ASAN_MAPPING();
  return false;
}

static inline bool AddrIsInHighMem(uptr a) {
  PROFILE_ASAN_MAPPING();
  return false;
}

static inline bool AddrIsInHighShadow(uptr a) {
  PROFILE_ASAN_MAPPING();
  return false;
}

static inline bool AddrIsInShadowGap(uptr a) {
  PROFILE_ASAN_MAPPING();
  a = RAW_ADDR(a);
  return a >= kShadowGapBeg && a <= kShadowGapEnd;
}

}  // namespace __asan

#endif  // ASAN_MAPPING_MYRIAD_H
