//=-- lsan_allocator.h ----------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of LeakSanitizer.
// Allocator for standalone LSan.
//
//===----------------------------------------------------------------------===//

#ifndef LSAN_ALLOCATOR_H
#define LSAN_ALLOCATOR_H

#include "sanitizer_common/sanitizer_allocator.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "lsan_common.h"

namespace __lsan {

void *Allocate(const StackTrace &stack, uptr size, uptr alignment,
               bool cleared);
void Deallocate(void *p);
void *Reallocate(const StackTrace &stack, void *p, uptr new_size,
                 uptr alignment);
uptr GetMallocUsableSize(const void *p);

template<typename Callable>
void ForEachChunk(const Callable &callback);

void GetAllocatorCacheRange(uptr *begin, uptr *end);
void AllocatorThreadFinish();
void InitializeAllocator();

const bool kAlwaysClearMemory = true;

struct ChunkMetadata {
  u8 allocated : 8;  // Must be first.
  ChunkTag tag : 2;
#if SANITIZER_WORDSIZE == 64
  uptr requested_size : 54;
#else
  uptr requested_size : 32;
  uptr padding : 22;
#endif
  u32 stack_trace_id;
};

#if defined(__mips64) || defined(__aarch64__) || defined(__i386__) || \
    defined(__arm__)
static const uptr kRegionSizeLog = 20;
static const uptr kNumRegions = SANITIZER_MMAP_RANGE_SIZE >> kRegionSizeLog;
template <typename AddressSpaceView>
using ByteMapASVT =
    TwoLevelByteMap<(kNumRegions >> 12), 1 << 12, AddressSpaceView>;

template <typename AddressSpaceViewTy>
struct AP32 {
  static const uptr kSpaceBeg = 0;
  static const u64 kSpaceSize = SANITIZER_MMAP_RANGE_SIZE;
  static const uptr kMetadataSize = sizeof(ChunkMetadata);
  typedef __sanitizer::CompactSizeClassMap SizeClassMap;
  static const uptr kRegionSizeLog = __lsan::kRegionSizeLog;
  using AddressSpaceView = AddressSpaceViewTy;
  using ByteMap = __lsan::ByteMapASVT<AddressSpaceView>;
  typedef NoOpMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
};
template <typename AddressSpaceView>
using PrimaryAllocatorASVT = SizeClassAllocator32<AP32<AddressSpaceView>>;
using PrimaryAllocator = PrimaryAllocatorASVT<LocalAddressSpaceView>;
#elif defined(__x86_64__) || defined(__powerpc64__)
# if defined(__powerpc64__)
const uptr kAllocatorSpace = 0xa0000000000ULL;
const uptr kAllocatorSize  = 0x20000000000ULL;  // 2T.
# else
const uptr kAllocatorSpace = 0x600000000000ULL;
const uptr kAllocatorSize  = 0x40000000000ULL;  // 4T.
# endif
template <typename AddressSpaceViewTy>
struct AP64 {  // Allocator64 parameters. Deliberately using a short name.
  static const uptr kSpaceBeg = kAllocatorSpace;
  static const uptr kSpaceSize = kAllocatorSize;
  static const uptr kMetadataSize = sizeof(ChunkMetadata);
  typedef DefaultSizeClassMap SizeClassMap;
  typedef NoOpMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
  using AddressSpaceView = AddressSpaceViewTy;
};

template <typename AddressSpaceView>
using PrimaryAllocatorASVT = SizeClassAllocator64<AP64<AddressSpaceView>>;
using PrimaryAllocator = PrimaryAllocatorASVT<LocalAddressSpaceView>;
#endif

template <typename AddressSpaceView>
using AllocatorCacheASVT =
    SizeClassAllocatorLocalCache<PrimaryAllocatorASVT<AddressSpaceView>>;
using AllocatorCache = AllocatorCacheASVT<LocalAddressSpaceView>;

template <typename AddressSpaceView>
using SecondaryAllocatorASVT =
    LargeMmapAllocator<NoOpMapUnmapCallback, DefaultLargeMmapAllocatorPtrArray,
                       AddressSpaceView>;

template <typename AddressSpaceView>
using AllocatorASVT =
    CombinedAllocator<PrimaryAllocatorASVT<AddressSpaceView>,
                      AllocatorCacheASVT<AddressSpaceView>,
                      SecondaryAllocatorASVT<AddressSpaceView>>;
using Allocator = AllocatorASVT<LocalAddressSpaceView>;

AllocatorCache *GetAllocatorCache();

int lsan_posix_memalign(void **memptr, uptr alignment, uptr size,
                        const StackTrace &stack);
void *lsan_aligned_alloc(uptr alignment, uptr size, const StackTrace &stack);
void *lsan_memalign(uptr alignment, uptr size, const StackTrace &stack);
void *lsan_malloc(uptr size, const StackTrace &stack);
void lsan_free(void *p);
void *lsan_realloc(void *p, uptr size, const StackTrace &stack);
void *lsan_calloc(uptr nmemb, uptr size, const StackTrace &stack);
void *lsan_valloc(uptr size, const StackTrace &stack);
void *lsan_pvalloc(uptr size, const StackTrace &stack);
uptr lsan_mz_size(const void *p);

}  // namespace __lsan

#endif  // LSAN_ALLOCATOR_H
