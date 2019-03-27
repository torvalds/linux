//===-- hwasan_allocator.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer.
//
//===----------------------------------------------------------------------===//

#ifndef HWASAN_ALLOCATOR_H
#define HWASAN_ALLOCATOR_H

#include "interception/interception.h"
#include "sanitizer_common/sanitizer_allocator.h"
#include "sanitizer_common/sanitizer_allocator_checks.h"
#include "sanitizer_common/sanitizer_allocator_interface.h"
#include "sanitizer_common/sanitizer_allocator_report.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_ring_buffer.h"
#include "hwasan_poisoning.h"

#if !defined(__aarch64__) && !defined(__x86_64__)
#error Unsupported platform
#endif

#if HWASAN_WITH_INTERCEPTORS
DECLARE_REAL(void *, realloc, void *ptr, uptr size)
DECLARE_REAL(void, free, void *ptr)
#endif

namespace __hwasan {

struct Metadata {
  u32 requested_size : 31;  // sizes are < 2G.
  u32 right_aligned  : 1;
  u32 alloc_context_id;
};

struct HwasanMapUnmapCallback {
  void OnMap(uptr p, uptr size) const { UpdateMemoryUsage(); }
  void OnUnmap(uptr p, uptr size) const {
    // We are about to unmap a chunk of user memory.
    // It can return as user-requested mmap() or another thread stack.
    // Make it accessible with zero-tagged pointer.
    TagMemory(p, size, 0);
  }
};

static const uptr kMaxAllowedMallocSize = 2UL << 30;  // 2G

struct AP64 {
  static const uptr kSpaceBeg = ~0ULL;
  static const uptr kSpaceSize = 0x2000000000ULL;
  static const uptr kMetadataSize = sizeof(Metadata);
  typedef __sanitizer::VeryDenseSizeClassMap SizeClassMap;
  using AddressSpaceView = LocalAddressSpaceView;
  typedef HwasanMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
};
typedef SizeClassAllocator64<AP64> PrimaryAllocator;
typedef SizeClassAllocatorLocalCache<PrimaryAllocator> AllocatorCache;
typedef LargeMmapAllocator<HwasanMapUnmapCallback> SecondaryAllocator;
typedef CombinedAllocator<PrimaryAllocator, AllocatorCache,
                          SecondaryAllocator> Allocator;

void AllocatorSwallowThreadLocalCache(AllocatorCache *cache);

class HwasanChunkView {
 public:
  HwasanChunkView() : block_(0), metadata_(nullptr) {}
  HwasanChunkView(uptr block, Metadata *metadata)
      : block_(block), metadata_(metadata) {}
  bool IsAllocated() const;    // Checks if the memory is currently allocated
  uptr Beg() const;            // First byte of user memory
  uptr End() const;            // Last byte of user memory
  uptr UsedSize() const;       // Size requested by the user
  uptr ActualSize() const;     // Size allocated by the allocator.
  u32 GetAllocStackId() const;
  bool FromSmallHeap() const;
 private:
  uptr block_;
  Metadata *const metadata_;
};

HwasanChunkView FindHeapChunkByAddress(uptr address);

// Information about one (de)allocation that happened in the past.
// These are recorded in a thread-local ring buffer.
// TODO: this is currently 24 bytes (20 bytes + alignment).
// Compress it to 16 bytes or extend it to be more useful.
struct HeapAllocationRecord {
  uptr tagged_addr;
  u32  alloc_context_id;
  u32  free_context_id;
  u32  requested_size;
};

typedef RingBuffer<HeapAllocationRecord> HeapAllocationsRingBuffer;

void GetAllocatorStats(AllocatorStatCounters s);

} // namespace __hwasan

#endif // HWASAN_ALLOCATOR_H
