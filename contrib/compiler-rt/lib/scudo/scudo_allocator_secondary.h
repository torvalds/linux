//===-- scudo_allocator_secondary.h -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// Scudo Secondary Allocator.
/// This services allocation that are too large to be serviced by the Primary
/// Allocator. It is directly backed by the memory mapping functions of the
/// operating system.
///
//===----------------------------------------------------------------------===//

#ifndef SCUDO_ALLOCATOR_SECONDARY_H_
#define SCUDO_ALLOCATOR_SECONDARY_H_

#ifndef SCUDO_ALLOCATOR_H_
# error "This file must be included inside scudo_allocator.h."
#endif

// Secondary backed allocations are standalone chunks that contain extra
// information stored in a LargeChunk::Header prior to the frontend's header.
//
// The secondary takes care of alignment requirements (so that it can release
// unnecessary pages in the rare event of larger alignments), and as such must
// know about the frontend's header size.
//
// Since Windows doesn't support partial releasing of a reserved memory region,
// we have to keep track of both the reserved and the committed memory.
//
// The resulting chunk resembles the following:
//
//   +--------------------+
//   | Guard page(s)      |
//   +--------------------+
//   | Unused space*      |
//   +--------------------+
//   | LargeChunk::Header |
//   +--------------------+
//   | {Unp,P}ackedHeader |
//   +--------------------+
//   | Data (aligned)     |
//   +--------------------+
//   | Unused space**     |
//   +--------------------+
//   | Guard page(s)      |
//   +--------------------+

namespace LargeChunk {
  struct Header {
    ReservedAddressRange StoredRange;
    uptr CommittedSize;
    uptr Size;
  };
  constexpr uptr getHeaderSize() {
    return RoundUpTo(sizeof(Header), MinAlignment);
  }
  static Header *getHeader(uptr Ptr) {
    return reinterpret_cast<Header *>(Ptr - getHeaderSize());
  }
  static Header *getHeader(const void *Ptr) {
    return getHeader(reinterpret_cast<uptr>(Ptr));
  }
}  // namespace LargeChunk

class LargeMmapAllocator {
 public:
  void Init() {
    internal_memset(this, 0, sizeof(*this));
  }

  void *Allocate(AllocatorStats *Stats, uptr Size, uptr Alignment) {
    const uptr UserSize = Size - Chunk::getHeaderSize();
    // The Scudo frontend prevents us from allocating more than
    // MaxAllowedMallocSize, so integer overflow checks would be superfluous.
    uptr ReservedSize = Size + LargeChunk::getHeaderSize();
    if (UNLIKELY(Alignment > MinAlignment))
      ReservedSize += Alignment;
    const uptr PageSize = GetPageSizeCached();
    ReservedSize = RoundUpTo(ReservedSize, PageSize);
    // Account for 2 guard pages, one before and one after the chunk.
    ReservedSize += 2 * PageSize;

    ReservedAddressRange AddressRange;
    uptr ReservedBeg = AddressRange.Init(ReservedSize, SecondaryAllocatorName);
    if (UNLIKELY(ReservedBeg == ~static_cast<uptr>(0)))
      return nullptr;
    // A page-aligned pointer is assumed after that, so check it now.
    DCHECK(IsAligned(ReservedBeg, PageSize));
    uptr ReservedEnd = ReservedBeg + ReservedSize;
    // The beginning of the user area for that allocation comes after the
    // initial guard page, and both headers. This is the pointer that has to
    // abide by alignment requirements.
    uptr CommittedBeg = ReservedBeg + PageSize;
    uptr UserBeg = CommittedBeg + HeadersSize;
    uptr UserEnd = UserBeg + UserSize;
    uptr CommittedEnd = RoundUpTo(UserEnd, PageSize);

    // In the rare event of larger alignments, we will attempt to fit the mmap
    // area better and unmap extraneous memory. This will also ensure that the
    // offset and unused bytes field of the header stay small.
    if (UNLIKELY(Alignment > MinAlignment)) {
      if (!IsAligned(UserBeg, Alignment)) {
        UserBeg = RoundUpTo(UserBeg, Alignment);
        CommittedBeg = RoundDownTo(UserBeg - HeadersSize, PageSize);
        const uptr NewReservedBeg = CommittedBeg - PageSize;
        DCHECK_GE(NewReservedBeg, ReservedBeg);
        if (!SANITIZER_WINDOWS && NewReservedBeg != ReservedBeg) {
          AddressRange.Unmap(ReservedBeg, NewReservedBeg - ReservedBeg);
          ReservedBeg = NewReservedBeg;
        }
        UserEnd = UserBeg + UserSize;
        CommittedEnd = RoundUpTo(UserEnd, PageSize);
      }
      const uptr NewReservedEnd = CommittedEnd + PageSize;
      DCHECK_LE(NewReservedEnd, ReservedEnd);
      if (!SANITIZER_WINDOWS && NewReservedEnd != ReservedEnd) {
        AddressRange.Unmap(NewReservedEnd, ReservedEnd - NewReservedEnd);
        ReservedEnd = NewReservedEnd;
      }
    }

    DCHECK_LE(UserEnd, CommittedEnd);
    const uptr CommittedSize = CommittedEnd - CommittedBeg;
    // Actually mmap the memory, preserving the guard pages on either sides.
    CHECK_EQ(CommittedBeg, AddressRange.Map(CommittedBeg, CommittedSize));
    const uptr Ptr = UserBeg - Chunk::getHeaderSize();
    LargeChunk::Header *H = LargeChunk::getHeader(Ptr);
    H->StoredRange = AddressRange;
    H->Size = CommittedEnd - Ptr;
    H->CommittedSize = CommittedSize;

    // The primary adds the whole class size to the stats when allocating a
    // chunk, so we will do something similar here. But we will not account for
    // the guard pages.
    {
      SpinMutexLock l(&StatsMutex);
      Stats->Add(AllocatorStatAllocated, CommittedSize);
      Stats->Add(AllocatorStatMapped, CommittedSize);
      AllocatedBytes += CommittedSize;
      if (LargestSize < CommittedSize)
        LargestSize = CommittedSize;
      NumberOfAllocs++;
    }

    return reinterpret_cast<void *>(Ptr);
  }

  void Deallocate(AllocatorStats *Stats, void *Ptr) {
    LargeChunk::Header *H = LargeChunk::getHeader(Ptr);
    // Since we're unmapping the entirety of where the ReservedAddressRange
    // actually is, copy onto the stack.
    ReservedAddressRange AddressRange = H->StoredRange;
    const uptr Size = H->CommittedSize;
    {
      SpinMutexLock l(&StatsMutex);
      Stats->Sub(AllocatorStatAllocated, Size);
      Stats->Sub(AllocatorStatMapped, Size);
      FreedBytes += Size;
      NumberOfFrees++;
    }
    AddressRange.Unmap(reinterpret_cast<uptr>(AddressRange.base()),
                       AddressRange.size());
  }

  static uptr GetActuallyAllocatedSize(void *Ptr) {
    return LargeChunk::getHeader(Ptr)->Size;
  }

  void PrintStats() {
    Printf("Stats: LargeMmapAllocator: allocated %zd times (%zd K), "
           "freed %zd times (%zd K), remains %zd (%zd K) max %zd M\n",
           NumberOfAllocs, AllocatedBytes >> 10, NumberOfFrees,
           FreedBytes >> 10, NumberOfAllocs - NumberOfFrees,
           (AllocatedBytes - FreedBytes) >> 10, LargestSize >> 20);
  }

 private:
  static constexpr uptr HeadersSize =
      LargeChunk::getHeaderSize() + Chunk::getHeaderSize();

  StaticSpinMutex StatsMutex;
  u32 NumberOfAllocs;
  u32 NumberOfFrees;
  uptr AllocatedBytes;
  uptr FreedBytes;
  uptr LargestSize;
};

#endif  // SCUDO_ALLOCATOR_SECONDARY_H_
