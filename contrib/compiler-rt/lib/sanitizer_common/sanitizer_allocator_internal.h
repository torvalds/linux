//===-- sanitizer_allocator_internal.h --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This allocator is used inside run-times.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_ALLOCATOR_INTERNAL_H
#define SANITIZER_ALLOCATOR_INTERNAL_H

#include "sanitizer_allocator.h"
#include "sanitizer_internal_defs.h"

namespace __sanitizer {

// FIXME: Check if we may use even more compact size class map for internal
// purposes.
typedef CompactSizeClassMap InternalSizeClassMap;

static const uptr kInternalAllocatorRegionSizeLog = 20;
static const uptr kInternalAllocatorNumRegions =
    SANITIZER_MMAP_RANGE_SIZE >> kInternalAllocatorRegionSizeLog;
#if SANITIZER_WORDSIZE == 32
typedef FlatByteMap<kInternalAllocatorNumRegions> ByteMap;
#else
typedef TwoLevelByteMap<(kInternalAllocatorNumRegions >> 12), 1 << 12> ByteMap;
#endif
struct AP32 {
  static const uptr kSpaceBeg = 0;
  static const u64 kSpaceSize = SANITIZER_MMAP_RANGE_SIZE;
  static const uptr kMetadataSize = 0;
  typedef InternalSizeClassMap SizeClassMap;
  static const uptr kRegionSizeLog = kInternalAllocatorRegionSizeLog;
  using AddressSpaceView = LocalAddressSpaceView;
  using ByteMap = __sanitizer::ByteMap;
  typedef NoOpMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
};
typedef SizeClassAllocator32<AP32> PrimaryInternalAllocator;

typedef SizeClassAllocatorLocalCache<PrimaryInternalAllocator>
    InternalAllocatorCache;

typedef LargeMmapAllocator<NoOpMapUnmapCallback,
                           LargeMmapAllocatorPtrArrayStatic>
    SecondaryInternalAllocator;

typedef CombinedAllocator<PrimaryInternalAllocator, InternalAllocatorCache,
                          SecondaryInternalAllocator> InternalAllocator;

void *InternalAlloc(uptr size, InternalAllocatorCache *cache = nullptr,
                    uptr alignment = 0);
void *InternalRealloc(void *p, uptr size,
                      InternalAllocatorCache *cache = nullptr);
void *InternalCalloc(uptr countr, uptr size,
                     InternalAllocatorCache *cache = nullptr);
void InternalFree(void *p, InternalAllocatorCache *cache = nullptr);
InternalAllocator *internal_allocator();

} // namespace __sanitizer

#endif // SANITIZER_ALLOCATOR_INTERNAL_H
