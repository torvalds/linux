//===-- asan_allocator.h ----------------------------------------*- C++ -*-===//
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
// ASan-private header for asan_allocator.cc.
//===----------------------------------------------------------------------===//

#ifndef ASAN_ALLOCATOR_H
#define ASAN_ALLOCATOR_H

#include "asan_flags.h"
#include "asan_internal.h"
#include "asan_interceptors.h"
#include "sanitizer_common/sanitizer_allocator.h"
#include "sanitizer_common/sanitizer_list.h"

namespace __asan {

enum AllocType {
  FROM_MALLOC = 1,  // Memory block came from malloc, calloc, realloc, etc.
  FROM_NEW = 2,     // Memory block came from operator new.
  FROM_NEW_BR = 3   // Memory block came from operator new [ ]
};

struct AsanChunk;

struct AllocatorOptions {
  u32 quarantine_size_mb;
  u32 thread_local_quarantine_size_kb;
  u16 min_redzone;
  u16 max_redzone;
  u8 may_return_null;
  u8 alloc_dealloc_mismatch;
  s32 release_to_os_interval_ms;

  void SetFrom(const Flags *f, const CommonFlags *cf);
  void CopyTo(Flags *f, CommonFlags *cf);
};

void InitializeAllocator(const AllocatorOptions &options);
void ReInitializeAllocator(const AllocatorOptions &options);
void GetAllocatorOptions(AllocatorOptions *options);

class AsanChunkView {
 public:
  explicit AsanChunkView(AsanChunk *chunk) : chunk_(chunk) {}
  bool IsValid() const;        // Checks if AsanChunkView points to a valid
                               // allocated or quarantined chunk.
  bool IsAllocated() const;    // Checks if the memory is currently allocated.
  bool IsQuarantined() const;  // Checks if the memory is currently quarantined.
  uptr Beg() const;            // First byte of user memory.
  uptr End() const;            // Last byte of user memory.
  uptr UsedSize() const;       // Size requested by the user.
  u32 UserRequestedAlignment() const;  // Originally requested alignment.
  uptr AllocTid() const;
  uptr FreeTid() const;
  bool Eq(const AsanChunkView &c) const { return chunk_ == c.chunk_; }
  u32 GetAllocStackId() const;
  u32 GetFreeStackId() const;
  StackTrace GetAllocStack() const;
  StackTrace GetFreeStack() const;
  AllocType GetAllocType() const;
  bool AddrIsInside(uptr addr, uptr access_size, sptr *offset) const {
    if (addr >= Beg() && (addr + access_size) <= End()) {
      *offset = addr - Beg();
      return true;
    }
    return false;
  }
  bool AddrIsAtLeft(uptr addr, uptr access_size, sptr *offset) const {
    (void)access_size;
    if (addr < Beg()) {
      *offset = Beg() - addr;
      return true;
    }
    return false;
  }
  bool AddrIsAtRight(uptr addr, uptr access_size, sptr *offset) const {
    if (addr + access_size > End()) {
      *offset = addr - End();
      return true;
    }
    return false;
  }

 private:
  AsanChunk *const chunk_;
};

AsanChunkView FindHeapChunkByAddress(uptr address);
AsanChunkView FindHeapChunkByAllocBeg(uptr address);

// List of AsanChunks with total size.
class AsanChunkFifoList: public IntrusiveList<AsanChunk> {
 public:
  explicit AsanChunkFifoList(LinkerInitialized) { }
  AsanChunkFifoList() { clear(); }
  void Push(AsanChunk *n);
  void PushList(AsanChunkFifoList *q);
  AsanChunk *Pop();
  uptr size() { return size_; }
  void clear() {
    IntrusiveList<AsanChunk>::clear();
    size_ = 0;
  }
 private:
  uptr size_;
};

struct AsanMapUnmapCallback {
  void OnMap(uptr p, uptr size) const;
  void OnUnmap(uptr p, uptr size) const;
};

#if SANITIZER_CAN_USE_ALLOCATOR64
# if SANITIZER_FUCHSIA
const uptr kAllocatorSpace = ~(uptr)0;
const uptr kAllocatorSize  =  0x40000000000ULL;  // 4T.
typedef DefaultSizeClassMap SizeClassMap;
# elif defined(__powerpc64__)
const uptr kAllocatorSpace = ~(uptr)0;
const uptr kAllocatorSize  =  0x20000000000ULL;  // 2T.
typedef DefaultSizeClassMap SizeClassMap;
# elif defined(__aarch64__) && SANITIZER_ANDROID
// Android needs to support 39, 42 and 48 bit VMA.
const uptr kAllocatorSpace =  ~(uptr)0;
const uptr kAllocatorSize  =  0x2000000000ULL;  // 128G.
typedef VeryCompactSizeClassMap SizeClassMap;
# elif defined(__aarch64__)
// AArch64/SANITIZER_CAN_USER_ALLOCATOR64 is only for 42-bit VMA
// so no need to different values for different VMA.
const uptr kAllocatorSpace =  0x10000000000ULL;
const uptr kAllocatorSize  =  0x10000000000ULL;  // 3T.
typedef DefaultSizeClassMap SizeClassMap;
# elif SANITIZER_WINDOWS
const uptr kAllocatorSpace = ~(uptr)0;
const uptr kAllocatorSize  =  0x8000000000ULL;  // 500G
typedef DefaultSizeClassMap SizeClassMap;
# else
const uptr kAllocatorSpace = 0x600000000000ULL;
const uptr kAllocatorSize  =  0x40000000000ULL;  // 4T.
typedef DefaultSizeClassMap SizeClassMap;
# endif
template <typename AddressSpaceViewTy>
struct AP64 {  // Allocator64 parameters. Deliberately using a short name.
  static const uptr kSpaceBeg = kAllocatorSpace;
  static const uptr kSpaceSize = kAllocatorSize;
  static const uptr kMetadataSize = 0;
  typedef __asan::SizeClassMap SizeClassMap;
  typedef AsanMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
  using AddressSpaceView = AddressSpaceViewTy;
};

template <typename AddressSpaceView>
using PrimaryAllocatorASVT = SizeClassAllocator64<AP64<AddressSpaceView>>;
using PrimaryAllocator = PrimaryAllocatorASVT<LocalAddressSpaceView>;
#else  // Fallback to SizeClassAllocator32.
static const uptr kRegionSizeLog = 20;
static const uptr kNumRegions = SANITIZER_MMAP_RANGE_SIZE >> kRegionSizeLog;
# if SANITIZER_WORDSIZE == 32
template <typename AddressSpaceView>
using ByteMapASVT = FlatByteMap<kNumRegions, AddressSpaceView>;
# elif SANITIZER_WORDSIZE == 64
template <typename AddressSpaceView>
using ByteMapASVT =
    TwoLevelByteMap<(kNumRegions >> 12), 1 << 12, AddressSpaceView>;
# endif
typedef CompactSizeClassMap SizeClassMap;
template <typename AddressSpaceViewTy>
struct AP32 {
  static const uptr kSpaceBeg = 0;
  static const u64 kSpaceSize = SANITIZER_MMAP_RANGE_SIZE;
  static const uptr kMetadataSize = 16;
  typedef __asan::SizeClassMap SizeClassMap;
  static const uptr kRegionSizeLog = __asan::kRegionSizeLog;
  using AddressSpaceView = AddressSpaceViewTy;
  using ByteMap = __asan::ByteMapASVT<AddressSpaceView>;
  typedef AsanMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
};
template <typename AddressSpaceView>
using PrimaryAllocatorASVT = SizeClassAllocator32<AP32<AddressSpaceView> >;
using PrimaryAllocator = PrimaryAllocatorASVT<LocalAddressSpaceView>;
#endif  // SANITIZER_CAN_USE_ALLOCATOR64

static const uptr kNumberOfSizeClasses = SizeClassMap::kNumClasses;
template <typename AddressSpaceView>
using AllocatorCacheASVT =
    SizeClassAllocatorLocalCache<PrimaryAllocatorASVT<AddressSpaceView>>;
using AllocatorCache = AllocatorCacheASVT<LocalAddressSpaceView>;

template <typename AddressSpaceView>
using SecondaryAllocatorASVT =
    LargeMmapAllocator<AsanMapUnmapCallback, DefaultLargeMmapAllocatorPtrArray,
                       AddressSpaceView>;
template <typename AddressSpaceView>
using AsanAllocatorASVT =
    CombinedAllocator<PrimaryAllocatorASVT<AddressSpaceView>,
                      AllocatorCacheASVT<AddressSpaceView>,
                      SecondaryAllocatorASVT<AddressSpaceView>>;
using AsanAllocator = AsanAllocatorASVT<LocalAddressSpaceView>;

struct AsanThreadLocalMallocStorage {
  uptr quarantine_cache[16];
  AllocatorCache allocator_cache;
  void CommitBack();
 private:
  // These objects are allocated via mmap() and are zero-initialized.
  AsanThreadLocalMallocStorage() {}
};

void *asan_memalign(uptr alignment, uptr size, BufferedStackTrace *stack,
                    AllocType alloc_type);
void asan_free(void *ptr, BufferedStackTrace *stack, AllocType alloc_type);
void asan_delete(void *ptr, uptr size, uptr alignment,
                 BufferedStackTrace *stack, AllocType alloc_type);

void *asan_malloc(uptr size, BufferedStackTrace *stack);
void *asan_calloc(uptr nmemb, uptr size, BufferedStackTrace *stack);
void *asan_realloc(void *p, uptr size, BufferedStackTrace *stack);
void *asan_valloc(uptr size, BufferedStackTrace *stack);
void *asan_pvalloc(uptr size, BufferedStackTrace *stack);

void *asan_aligned_alloc(uptr alignment, uptr size, BufferedStackTrace *stack);
int asan_posix_memalign(void **memptr, uptr alignment, uptr size,
                        BufferedStackTrace *stack);
uptr asan_malloc_usable_size(const void *ptr, uptr pc, uptr bp);

uptr asan_mz_size(const void *ptr);
void asan_mz_force_lock();
void asan_mz_force_unlock();

void PrintInternalAllocatorStats();
void AsanSoftRssLimitExceededCallback(bool exceeded);

}  // namespace __asan
#endif  // ASAN_ALLOCATOR_H
