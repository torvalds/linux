//==- llvm/Support/Recycler.h - Recycling Allocator --------------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the Recycler class template.  See the doxygen comment for
// Recycler for more details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RECYCLER_H
#define LLVM_SUPPORT_RECYCLER_H

#include "llvm/ADT/ilist.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>

namespace llvm {

/// PrintRecyclingAllocatorStats - Helper for RecyclingAllocator for
/// printing statistics.
///
void PrintRecyclerStats(size_t Size, size_t Align, size_t FreeListSize);

/// Recycler - This class manages a linked-list of deallocated nodes
/// and facilitates reusing deallocated memory in place of allocating
/// new memory.
///
template <class T, size_t Size = sizeof(T), size_t Align = alignof(T)>
class Recycler {
  struct FreeNode {
    FreeNode *Next;
  };

  /// List of nodes that have deleted contents and are not in active use.
  FreeNode *FreeList = nullptr;

  FreeNode *pop_val() {
    auto *Val = FreeList;
    __asan_unpoison_memory_region(Val, Size);
    FreeList = FreeList->Next;
    __msan_allocated_memory(Val, Size);
    return Val;
  }

  void push(FreeNode *N) {
    N->Next = FreeList;
    FreeList = N;
    __asan_poison_memory_region(N, Size);
  }

public:
  ~Recycler() {
    // If this fails, either the callee has lost track of some allocation,
    // or the callee isn't tracking allocations and should just call
    // clear() before deleting the Recycler.
    assert(!FreeList && "Non-empty recycler deleted!");
  }

  /// clear - Release all the tracked allocations to the allocator. The
  /// recycler must be free of any tracked allocations before being
  /// deleted; calling clear is one way to ensure this.
  template<class AllocatorType>
  void clear(AllocatorType &Allocator) {
    while (FreeList) {
      T *t = reinterpret_cast<T *>(pop_val());
      Allocator.Deallocate(t);
    }
  }

  /// Special case for BumpPtrAllocator which has an empty Deallocate()
  /// function.
  ///
  /// There is no need to traverse the free list, pulling all the objects into
  /// cache.
  void clear(BumpPtrAllocator &) { FreeList = nullptr; }

  template<class SubClass, class AllocatorType>
  SubClass *Allocate(AllocatorType &Allocator) {
    static_assert(alignof(SubClass) <= Align,
                  "Recycler allocation alignment is less than object align!");
    static_assert(sizeof(SubClass) <= Size,
                  "Recycler allocation size is less than object size!");
    return FreeList ? reinterpret_cast<SubClass *>(pop_val())
                    : static_cast<SubClass *>(Allocator.Allocate(Size, Align));
  }

  template<class AllocatorType>
  T *Allocate(AllocatorType &Allocator) {
    return Allocate<T>(Allocator);
  }

  template<class SubClass, class AllocatorType>
  void Deallocate(AllocatorType & /*Allocator*/, SubClass* Element) {
    push(reinterpret_cast<FreeNode *>(Element));
  }

  void PrintStats();
};

template <class T, size_t Size, size_t Align>
void Recycler<T, Size, Align>::PrintStats() {
  size_t S = 0;
  for (auto *I = FreeList; I; I = I->Next)
    ++S;
  PrintRecyclerStats(Size, Align, S);
}

}

#endif
