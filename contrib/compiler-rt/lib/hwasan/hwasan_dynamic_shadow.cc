//===-- hwasan_dynamic_shadow.cc --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file is a part of HWAddressSanitizer. It reserves dynamic shadow memory
/// region and handles ifunc resolver case, when necessary.
///
//===----------------------------------------------------------------------===//

#include "hwasan.h"
#include "hwasan_dynamic_shadow.h"
#include "hwasan_mapping.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_posix.h"

// The code in this file needs to run in an unrelocated binary. It should not
// access any external symbol, including its own non-hidden globals.

namespace __hwasan {

static void UnmapFromTo(uptr from, uptr to) {
  if (to == from)
    return;
  CHECK(to >= from);
  uptr res = internal_munmap(reinterpret_cast<void *>(from), to - from);
  if (UNLIKELY(internal_iserror(res))) {
    Report("ERROR: %s failed to unmap 0x%zx (%zd) bytes at address %p\n",
           SanitizerToolName, to - from, to - from, from);
    CHECK("unable to unmap" && 0);
  }
}

// Returns an address aligned to kShadowBaseAlignment, such that
// 2**kShadowBaseAlingment on the left and shadow_size_bytes bytes on the right
// of it are mapped no access.
static uptr MapDynamicShadow(uptr shadow_size_bytes) {
  const uptr granularity = GetMmapGranularity();
  const uptr min_alignment = granularity << kShadowScale;
  const uptr alignment = 1ULL << kShadowBaseAlignment;
  CHECK_GE(alignment, min_alignment);

  const uptr left_padding = 1ULL << kShadowBaseAlignment;
  const uptr shadow_size =
      RoundUpTo(shadow_size_bytes, granularity);
  const uptr map_size = shadow_size + left_padding + alignment;

  const uptr map_start = (uptr)MmapNoAccess(map_size);
  CHECK_NE(map_start, ~(uptr)0);

  const uptr shadow_start = RoundUpTo(map_start + left_padding, alignment);

  UnmapFromTo(map_start, shadow_start - left_padding);
  UnmapFromTo(shadow_start + shadow_size, map_start + map_size);

  return shadow_start;
}

}  // namespace __hwasan

#if SANITIZER_ANDROID
extern "C" {

INTERFACE_ATTRIBUTE void __hwasan_shadow();
decltype(__hwasan_shadow)* __hwasan_premap_shadow();

}  // extern "C"

namespace __hwasan {

// Conservative upper limit.
static uptr PremapShadowSize() {
  return RoundUpTo(GetMaxVirtualAddress() >> kShadowScale,
                   GetMmapGranularity());
}

static uptr PremapShadow() {
  return MapDynamicShadow(PremapShadowSize());
}

static bool IsPremapShadowAvailable() {
  const uptr shadow = reinterpret_cast<uptr>(&__hwasan_shadow);
  const uptr resolver = reinterpret_cast<uptr>(&__hwasan_premap_shadow);
  // shadow == resolver is how Android KitKat and older handles ifunc.
  // shadow == 0 just in case.
  return shadow != 0 && shadow != resolver;
}

static uptr FindPremappedShadowStart(uptr shadow_size_bytes) {
  const uptr granularity = GetMmapGranularity();
  const uptr shadow_start = reinterpret_cast<uptr>(&__hwasan_shadow);
  const uptr premap_shadow_size = PremapShadowSize();
  const uptr shadow_size = RoundUpTo(shadow_size_bytes, granularity);

  // We may have mapped too much. Release extra memory.
  UnmapFromTo(shadow_start + shadow_size, shadow_start + premap_shadow_size);
  return shadow_start;
}

}  // namespace __hwasan

extern "C" {

decltype(__hwasan_shadow)* __hwasan_premap_shadow() {
  // The resolver might be called multiple times. Map the shadow just once.
  static __sanitizer::uptr shadow = 0;
  if (!shadow)
    shadow = __hwasan::PremapShadow();
  return reinterpret_cast<decltype(__hwasan_shadow)*>(shadow);
}

// __hwasan_shadow is a "function" that has the same address as the first byte
// of the shadow mapping.
INTERFACE_ATTRIBUTE __attribute__((ifunc("__hwasan_premap_shadow")))
void __hwasan_shadow();

}  // extern "C"

namespace __hwasan {

uptr FindDynamicShadowStart(uptr shadow_size_bytes) {
  if (IsPremapShadowAvailable())
    return FindPremappedShadowStart(shadow_size_bytes);
  return MapDynamicShadow(shadow_size_bytes);
}

}  // namespace __hwasan
#else
namespace __hwasan {

uptr FindDynamicShadowStart(uptr shadow_size_bytes) {
  return MapDynamicShadow(shadow_size_bytes);
}

}  // namespace __hwasan
#
#endif  // SANITIZER_ANDROID
