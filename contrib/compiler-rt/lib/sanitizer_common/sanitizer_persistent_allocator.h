//===-- sanitizer_persistent_allocator.h ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// A fast memory allocator that does not support free() nor realloc().
// All allocations are forever.
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_PERSISTENT_ALLOCATOR_H
#define SANITIZER_PERSISTENT_ALLOCATOR_H

#include "sanitizer_internal_defs.h"
#include "sanitizer_mutex.h"
#include "sanitizer_atomic.h"
#include "sanitizer_common.h"

namespace __sanitizer {

class PersistentAllocator {
 public:
  void *alloc(uptr size);

 private:
  void *tryAlloc(uptr size);
  StaticSpinMutex mtx;  // Protects alloc of new blocks for region allocator.
  atomic_uintptr_t region_pos;  // Region allocator for Node's.
  atomic_uintptr_t region_end;
};

inline void *PersistentAllocator::tryAlloc(uptr size) {
  // Optimisic lock-free allocation, essentially try to bump the region ptr.
  for (;;) {
    uptr cmp = atomic_load(&region_pos, memory_order_acquire);
    uptr end = atomic_load(&region_end, memory_order_acquire);
    if (cmp == 0 || cmp + size > end) return nullptr;
    if (atomic_compare_exchange_weak(&region_pos, &cmp, cmp + size,
                                     memory_order_acquire))
      return (void *)cmp;
  }
}

inline void *PersistentAllocator::alloc(uptr size) {
  // First, try to allocate optimisitically.
  void *s = tryAlloc(size);
  if (s) return s;
  // If failed, lock, retry and alloc new superblock.
  SpinMutexLock l(&mtx);
  for (;;) {
    s = tryAlloc(size);
    if (s) return s;
    atomic_store(&region_pos, 0, memory_order_relaxed);
    uptr allocsz = 64 * 1024;
    if (allocsz < size) allocsz = size;
    uptr mem = (uptr)MmapOrDie(allocsz, "stack depot");
    atomic_store(&region_end, mem + allocsz, memory_order_release);
    atomic_store(&region_pos, mem, memory_order_release);
  }
}

extern PersistentAllocator thePersistentAllocator;
inline void *PersistentAlloc(uptr sz) {
  return thePersistentAllocator.alloc(sz);
}

} // namespace __sanitizer

#endif // SANITIZER_PERSISTENT_ALLOCATOR_H
