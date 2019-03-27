//===-- scudo_allocator.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// Scudo Hardened Allocator implementation.
/// It uses the sanitizer_common allocator as a base and aims at mitigating
/// heap corruption vulnerabilities. It provides a checksum-guarded chunk
/// header, a delayed free list, and additional sanity checks.
///
//===----------------------------------------------------------------------===//

#include "scudo_allocator.h"
#include "scudo_crc32.h"
#include "scudo_errors.h"
#include "scudo_flags.h"
#include "scudo_interface_internal.h"
#include "scudo_tsd.h"
#include "scudo_utils.h"

#include "sanitizer_common/sanitizer_allocator_checks.h"
#include "sanitizer_common/sanitizer_allocator_interface.h"
#include "sanitizer_common/sanitizer_quarantine.h"

#include <errno.h>
#include <string.h>

namespace __scudo {

// Global static cookie, initialized at start-up.
static u32 Cookie;

// We default to software CRC32 if the alternatives are not supported, either
// at compilation or at runtime.
static atomic_uint8_t HashAlgorithm = { CRC32Software };

INLINE u32 computeCRC32(u32 Crc, uptr Value, uptr *Array, uptr ArraySize) {
  // If the hardware CRC32 feature is defined here, it was enabled everywhere,
  // as opposed to only for scudo_crc32.cpp. This means that other hardware
  // specific instructions were likely emitted at other places, and as a
  // result there is no reason to not use it here.
#if defined(__SSE4_2__) || defined(__ARM_FEATURE_CRC32)
  Crc = CRC32_INTRINSIC(Crc, Value);
  for (uptr i = 0; i < ArraySize; i++)
    Crc = CRC32_INTRINSIC(Crc, Array[i]);
  return Crc;
#else
  if (atomic_load_relaxed(&HashAlgorithm) == CRC32Hardware) {
    Crc = computeHardwareCRC32(Crc, Value);
    for (uptr i = 0; i < ArraySize; i++)
      Crc = computeHardwareCRC32(Crc, Array[i]);
    return Crc;
  }
  Crc = computeSoftwareCRC32(Crc, Value);
  for (uptr i = 0; i < ArraySize; i++)
    Crc = computeSoftwareCRC32(Crc, Array[i]);
  return Crc;
#endif  // defined(__SSE4_2__) || defined(__ARM_FEATURE_CRC32)
}

static BackendT &getBackend();

namespace Chunk {
  static INLINE AtomicPackedHeader *getAtomicHeader(void *Ptr) {
    return reinterpret_cast<AtomicPackedHeader *>(reinterpret_cast<uptr>(Ptr) -
        getHeaderSize());
  }
  static INLINE
  const AtomicPackedHeader *getConstAtomicHeader(const void *Ptr) {
    return reinterpret_cast<const AtomicPackedHeader *>(
        reinterpret_cast<uptr>(Ptr) - getHeaderSize());
  }

  static INLINE bool isAligned(const void *Ptr) {
    return IsAligned(reinterpret_cast<uptr>(Ptr), MinAlignment);
  }

  // We can't use the offset member of the chunk itself, as we would double
  // fetch it without any warranty that it wouldn't have been tampered. To
  // prevent this, we work with a local copy of the header.
  static INLINE void *getBackendPtr(const void *Ptr, UnpackedHeader *Header) {
    return reinterpret_cast<void *>(reinterpret_cast<uptr>(Ptr) -
        getHeaderSize() - (Header->Offset << MinAlignmentLog));
  }

  // Returns the usable size for a chunk, meaning the amount of bytes from the
  // beginning of the user data to the end of the backend allocated chunk.
  static INLINE uptr getUsableSize(const void *Ptr, UnpackedHeader *Header) {
    const uptr ClassId = Header->ClassId;
    if (ClassId)
      return PrimaryT::ClassIdToSize(ClassId) - getHeaderSize() -
          (Header->Offset << MinAlignmentLog);
    return SecondaryT::GetActuallyAllocatedSize(
        getBackendPtr(Ptr, Header)) - getHeaderSize();
  }

  // Returns the size the user requested when allocating the chunk.
  static INLINE uptr getSize(const void *Ptr, UnpackedHeader *Header) {
    const uptr SizeOrUnusedBytes = Header->SizeOrUnusedBytes;
    if (Header->ClassId)
      return SizeOrUnusedBytes;
    return SecondaryT::GetActuallyAllocatedSize(
        getBackendPtr(Ptr, Header)) - getHeaderSize() - SizeOrUnusedBytes;
  }

  // Compute the checksum of the chunk pointer and its header.
  static INLINE u16 computeChecksum(const void *Ptr, UnpackedHeader *Header) {
    UnpackedHeader ZeroChecksumHeader = *Header;
    ZeroChecksumHeader.Checksum = 0;
    uptr HeaderHolder[sizeof(UnpackedHeader) / sizeof(uptr)];
    memcpy(&HeaderHolder, &ZeroChecksumHeader, sizeof(HeaderHolder));
    const u32 Crc = computeCRC32(Cookie, reinterpret_cast<uptr>(Ptr),
                                 HeaderHolder, ARRAY_SIZE(HeaderHolder));
    return static_cast<u16>(Crc);
  }

  // Checks the validity of a chunk by verifying its checksum. It doesn't
  // incur termination in the event of an invalid chunk.
  static INLINE bool isValid(const void *Ptr) {
    PackedHeader NewPackedHeader =
        atomic_load_relaxed(getConstAtomicHeader(Ptr));
    UnpackedHeader NewUnpackedHeader =
        bit_cast<UnpackedHeader>(NewPackedHeader);
    return (NewUnpackedHeader.Checksum ==
            computeChecksum(Ptr, &NewUnpackedHeader));
  }

  // Ensure that ChunkAvailable is 0, so that if a 0 checksum is ever valid
  // for a fully nulled out header, its state will be available anyway.
  COMPILER_CHECK(ChunkAvailable == 0);

  // Loads and unpacks the header, verifying the checksum in the process.
  static INLINE
  void loadHeader(const void *Ptr, UnpackedHeader *NewUnpackedHeader) {
    PackedHeader NewPackedHeader =
        atomic_load_relaxed(getConstAtomicHeader(Ptr));
    *NewUnpackedHeader = bit_cast<UnpackedHeader>(NewPackedHeader);
    if (UNLIKELY(NewUnpackedHeader->Checksum !=
        computeChecksum(Ptr, NewUnpackedHeader)))
      dieWithMessage("corrupted chunk header at address %p\n", Ptr);
  }

  // Packs and stores the header, computing the checksum in the process.
  static INLINE void storeHeader(void *Ptr, UnpackedHeader *NewUnpackedHeader) {
    NewUnpackedHeader->Checksum = computeChecksum(Ptr, NewUnpackedHeader);
    PackedHeader NewPackedHeader = bit_cast<PackedHeader>(*NewUnpackedHeader);
    atomic_store_relaxed(getAtomicHeader(Ptr), NewPackedHeader);
  }

  // Packs and stores the header, computing the checksum in the process. We
  // compare the current header with the expected provided one to ensure that
  // we are not being raced by a corruption occurring in another thread.
  static INLINE void compareExchangeHeader(void *Ptr,
                                           UnpackedHeader *NewUnpackedHeader,
                                           UnpackedHeader *OldUnpackedHeader) {
    NewUnpackedHeader->Checksum = computeChecksum(Ptr, NewUnpackedHeader);
    PackedHeader NewPackedHeader = bit_cast<PackedHeader>(*NewUnpackedHeader);
    PackedHeader OldPackedHeader = bit_cast<PackedHeader>(*OldUnpackedHeader);
    if (UNLIKELY(!atomic_compare_exchange_strong(
            getAtomicHeader(Ptr), &OldPackedHeader, NewPackedHeader,
            memory_order_relaxed)))
      dieWithMessage("race on chunk header at address %p\n", Ptr);
  }
}  // namespace Chunk

struct QuarantineCallback {
  explicit QuarantineCallback(AllocatorCacheT *Cache)
    : Cache_(Cache) {}

  // Chunk recycling function, returns a quarantined chunk to the backend,
  // first making sure it hasn't been tampered with.
  void Recycle(void *Ptr) {
    UnpackedHeader Header;
    Chunk::loadHeader(Ptr, &Header);
    if (UNLIKELY(Header.State != ChunkQuarantine))
      dieWithMessage("invalid chunk state when recycling address %p\n", Ptr);
    UnpackedHeader NewHeader = Header;
    NewHeader.State = ChunkAvailable;
    Chunk::compareExchangeHeader(Ptr, &NewHeader, &Header);
    void *BackendPtr = Chunk::getBackendPtr(Ptr, &Header);
    if (Header.ClassId)
      getBackend().deallocatePrimary(Cache_, BackendPtr, Header.ClassId);
    else
      getBackend().deallocateSecondary(BackendPtr);
  }

  // Internal quarantine allocation and deallocation functions. We first check
  // that the batches are indeed serviced by the Primary.
  // TODO(kostyak): figure out the best way to protect the batches.
  void *Allocate(uptr Size) {
    const uptr BatchClassId = SizeClassMap::ClassID(sizeof(QuarantineBatch));
    return getBackend().allocatePrimary(Cache_, BatchClassId);
  }

  void Deallocate(void *Ptr) {
    const uptr BatchClassId = SizeClassMap::ClassID(sizeof(QuarantineBatch));
    getBackend().deallocatePrimary(Cache_, Ptr, BatchClassId);
  }

  AllocatorCacheT *Cache_;
  COMPILER_CHECK(sizeof(QuarantineBatch) < SizeClassMap::kMaxSize);
};

typedef Quarantine<QuarantineCallback, void> QuarantineT;
typedef QuarantineT::Cache QuarantineCacheT;
COMPILER_CHECK(sizeof(QuarantineCacheT) <=
               sizeof(ScudoTSD::QuarantineCachePlaceHolder));

QuarantineCacheT *getQuarantineCache(ScudoTSD *TSD) {
  return reinterpret_cast<QuarantineCacheT *>(TSD->QuarantineCachePlaceHolder);
}

struct Allocator {
  static const uptr MaxAllowedMallocSize =
      FIRST_32_SECOND_64(2UL << 30, 1ULL << 40);

  BackendT Backend;
  QuarantineT Quarantine;

  u32 QuarantineChunksUpToSize;

  bool DeallocationTypeMismatch;
  bool ZeroContents;
  bool DeleteSizeMismatch;

  bool CheckRssLimit;
  uptr HardRssLimitMb;
  uptr SoftRssLimitMb;
  atomic_uint8_t RssLimitExceeded;
  atomic_uint64_t RssLastCheckedAtNS;

  explicit Allocator(LinkerInitialized)
    : Quarantine(LINKER_INITIALIZED) {}

  NOINLINE void performSanityChecks();

  void init() {
    SanitizerToolName = "Scudo";
    PrimaryAllocatorName = "ScudoPrimary";
    SecondaryAllocatorName = "ScudoSecondary";

    initFlags();

    performSanityChecks();

    // Check if hardware CRC32 is supported in the binary and by the platform,
    // if so, opt for the CRC32 hardware version of the checksum.
    if (&computeHardwareCRC32 && hasHardwareCRC32())
      atomic_store_relaxed(&HashAlgorithm, CRC32Hardware);

    SetAllocatorMayReturnNull(common_flags()->allocator_may_return_null);
    Backend.init(common_flags()->allocator_release_to_os_interval_ms);
    HardRssLimitMb = common_flags()->hard_rss_limit_mb;
    SoftRssLimitMb = common_flags()->soft_rss_limit_mb;
    Quarantine.Init(
        static_cast<uptr>(getFlags()->QuarantineSizeKb) << 10,
        static_cast<uptr>(getFlags()->ThreadLocalQuarantineSizeKb) << 10);
    QuarantineChunksUpToSize = (Quarantine.GetCacheSize() == 0) ? 0 :
        getFlags()->QuarantineChunksUpToSize;
    DeallocationTypeMismatch = getFlags()->DeallocationTypeMismatch;
    DeleteSizeMismatch = getFlags()->DeleteSizeMismatch;
    ZeroContents = getFlags()->ZeroContents;

    if (UNLIKELY(!GetRandom(reinterpret_cast<void *>(&Cookie), sizeof(Cookie),
                            /*blocking=*/false))) {
      Cookie = static_cast<u32>((NanoTime() >> 12) ^
                                (reinterpret_cast<uptr>(this) >> 4));
    }

    CheckRssLimit = HardRssLimitMb || SoftRssLimitMb;
    if (CheckRssLimit)
      atomic_store_relaxed(&RssLastCheckedAtNS, MonotonicNanoTime());
  }

  // Helper function that checks for a valid Scudo chunk. nullptr isn't.
  bool isValidPointer(const void *Ptr) {
    initThreadMaybe();
    if (UNLIKELY(!Ptr))
      return false;
    if (!Chunk::isAligned(Ptr))
      return false;
    return Chunk::isValid(Ptr);
  }

  NOINLINE bool isRssLimitExceeded();

  // Allocates a chunk.
  void *allocate(uptr Size, uptr Alignment, AllocType Type,
                 bool ForceZeroContents = false) {
    initThreadMaybe();
    if (UNLIKELY(Alignment > MaxAlignment)) {
      if (AllocatorMayReturnNull())
        return nullptr;
      reportAllocationAlignmentTooBig(Alignment, MaxAlignment);
    }
    if (UNLIKELY(Alignment < MinAlignment))
      Alignment = MinAlignment;

    const uptr NeededSize = RoundUpTo(Size ? Size : 1, MinAlignment) +
        Chunk::getHeaderSize();
    const uptr AlignedSize = (Alignment > MinAlignment) ?
        NeededSize + (Alignment - Chunk::getHeaderSize()) : NeededSize;
    if (UNLIKELY(Size >= MaxAllowedMallocSize) ||
        UNLIKELY(AlignedSize >= MaxAllowedMallocSize)) {
      if (AllocatorMayReturnNull())
        return nullptr;
      reportAllocationSizeTooBig(Size, AlignedSize, MaxAllowedMallocSize);
    }

    if (CheckRssLimit && UNLIKELY(isRssLimitExceeded())) {
      if (AllocatorMayReturnNull())
        return nullptr;
      reportRssLimitExceeded();
    }

    // Primary and Secondary backed allocations have a different treatment. We
    // deal with alignment requirements of Primary serviced allocations here,
    // but the Secondary will take care of its own alignment needs.
    void *BackendPtr;
    uptr BackendSize;
    u8 ClassId;
    if (PrimaryT::CanAllocate(AlignedSize, MinAlignment)) {
      BackendSize = AlignedSize;
      ClassId = SizeClassMap::ClassID(BackendSize);
      bool UnlockRequired;
      ScudoTSD *TSD = getTSDAndLock(&UnlockRequired);
      BackendPtr = Backend.allocatePrimary(&TSD->Cache, ClassId);
      if (UnlockRequired)
        TSD->unlock();
    } else {
      BackendSize = NeededSize;
      ClassId = 0;
      BackendPtr = Backend.allocateSecondary(BackendSize, Alignment);
    }
    if (UNLIKELY(!BackendPtr)) {
      SetAllocatorOutOfMemory();
      if (AllocatorMayReturnNull())
        return nullptr;
      reportOutOfMemory(Size);
    }

    // If requested, we will zero out the entire contents of the returned chunk.
    if ((ForceZeroContents || ZeroContents) && ClassId)
      memset(BackendPtr, 0, PrimaryT::ClassIdToSize(ClassId));

    UnpackedHeader Header = {};
    uptr UserPtr = reinterpret_cast<uptr>(BackendPtr) + Chunk::getHeaderSize();
    if (UNLIKELY(!IsAligned(UserPtr, Alignment))) {
      // Since the Secondary takes care of alignment, a non-aligned pointer
      // means it is from the Primary. It is also the only case where the offset
      // field of the header would be non-zero.
      DCHECK(ClassId);
      const uptr AlignedUserPtr = RoundUpTo(UserPtr, Alignment);
      Header.Offset = (AlignedUserPtr - UserPtr) >> MinAlignmentLog;
      UserPtr = AlignedUserPtr;
    }
    DCHECK_LE(UserPtr + Size, reinterpret_cast<uptr>(BackendPtr) + BackendSize);
    Header.State = ChunkAllocated;
    Header.AllocType = Type;
    if (ClassId) {
      Header.ClassId = ClassId;
      Header.SizeOrUnusedBytes = Size;
    } else {
      // The secondary fits the allocations to a page, so the amount of unused
      // bytes is the difference between the end of the user allocation and the
      // next page boundary.
      const uptr PageSize = GetPageSizeCached();
      const uptr TrailingBytes = (UserPtr + Size) & (PageSize - 1);
      if (TrailingBytes)
        Header.SizeOrUnusedBytes = PageSize - TrailingBytes;
    }
    void *Ptr = reinterpret_cast<void *>(UserPtr);
    Chunk::storeHeader(Ptr, &Header);
    if (SCUDO_CAN_USE_HOOKS && &__sanitizer_malloc_hook)
      __sanitizer_malloc_hook(Ptr, Size);
    return Ptr;
  }

  // Place a chunk in the quarantine or directly deallocate it in the event of
  // a zero-sized quarantine, or if the size of the chunk is greater than the
  // quarantine chunk size threshold.
  void quarantineOrDeallocateChunk(void *Ptr, UnpackedHeader *Header,
                                   uptr Size) {
    const bool BypassQuarantine = !Size || (Size > QuarantineChunksUpToSize);
    if (BypassQuarantine) {
      UnpackedHeader NewHeader = *Header;
      NewHeader.State = ChunkAvailable;
      Chunk::compareExchangeHeader(Ptr, &NewHeader, Header);
      void *BackendPtr = Chunk::getBackendPtr(Ptr, Header);
      if (Header->ClassId) {
        bool UnlockRequired;
        ScudoTSD *TSD = getTSDAndLock(&UnlockRequired);
        getBackend().deallocatePrimary(&TSD->Cache, BackendPtr,
                                       Header->ClassId);
        if (UnlockRequired)
          TSD->unlock();
      } else {
        getBackend().deallocateSecondary(BackendPtr);
      }
    } else {
      // If a small memory amount was allocated with a larger alignment, we want
      // to take that into account. Otherwise the Quarantine would be filled
      // with tiny chunks, taking a lot of VA memory. This is an approximation
      // of the usable size, that allows us to not call
      // GetActuallyAllocatedSize.
      const uptr EstimatedSize = Size + (Header->Offset << MinAlignmentLog);
      UnpackedHeader NewHeader = *Header;
      NewHeader.State = ChunkQuarantine;
      Chunk::compareExchangeHeader(Ptr, &NewHeader, Header);
      bool UnlockRequired;
      ScudoTSD *TSD = getTSDAndLock(&UnlockRequired);
      Quarantine.Put(getQuarantineCache(TSD), QuarantineCallback(&TSD->Cache),
                     Ptr, EstimatedSize);
      if (UnlockRequired)
        TSD->unlock();
    }
  }

  // Deallocates a Chunk, which means either adding it to the quarantine or
  // directly returning it to the backend if criteria are met.
  void deallocate(void *Ptr, uptr DeleteSize, uptr DeleteAlignment,
                  AllocType Type) {
    // For a deallocation, we only ensure minimal initialization, meaning thread
    // local data will be left uninitialized for now (when using ELF TLS). The
    // fallback cache will be used instead. This is a workaround for a situation
    // where the only heap operation performed in a thread would be a free past
    // the TLS destructors, ending up in initialized thread specific data never
    // being destroyed properly. Any other heap operation will do a full init.
    initThreadMaybe(/*MinimalInit=*/true);
    if (SCUDO_CAN_USE_HOOKS && &__sanitizer_free_hook)
      __sanitizer_free_hook(Ptr);
    if (UNLIKELY(!Ptr))
      return;
    if (UNLIKELY(!Chunk::isAligned(Ptr)))
      dieWithMessage("misaligned pointer when deallocating address %p\n", Ptr);
    UnpackedHeader Header;
    Chunk::loadHeader(Ptr, &Header);
    if (UNLIKELY(Header.State != ChunkAllocated))
      dieWithMessage("invalid chunk state when deallocating address %p\n", Ptr);
    if (DeallocationTypeMismatch) {
      // The deallocation type has to match the allocation one.
      if (Header.AllocType != Type) {
        // With the exception of memalign'd Chunks, that can be still be free'd.
        if (Header.AllocType != FromMemalign || Type != FromMalloc)
          dieWithMessage("allocation type mismatch when deallocating address "
                         "%p\n", Ptr);
      }
    }
    const uptr Size = Chunk::getSize(Ptr, &Header);
    if (DeleteSizeMismatch) {
      if (DeleteSize && DeleteSize != Size)
        dieWithMessage("invalid sized delete when deallocating address %p\n",
                       Ptr);
    }
    (void)DeleteAlignment;  // TODO(kostyak): verify that the alignment matches.
    quarantineOrDeallocateChunk(Ptr, &Header, Size);
  }

  // Reallocates a chunk. We can save on a new allocation if the new requested
  // size still fits in the chunk.
  void *reallocate(void *OldPtr, uptr NewSize) {
    initThreadMaybe();
    if (UNLIKELY(!Chunk::isAligned(OldPtr)))
      dieWithMessage("misaligned address when reallocating address %p\n",
                     OldPtr);
    UnpackedHeader OldHeader;
    Chunk::loadHeader(OldPtr, &OldHeader);
    if (UNLIKELY(OldHeader.State != ChunkAllocated))
      dieWithMessage("invalid chunk state when reallocating address %p\n",
                     OldPtr);
    if (DeallocationTypeMismatch) {
      if (UNLIKELY(OldHeader.AllocType != FromMalloc))
        dieWithMessage("allocation type mismatch when reallocating address "
                       "%p\n", OldPtr);
    }
    const uptr UsableSize = Chunk::getUsableSize(OldPtr, &OldHeader);
    // The new size still fits in the current chunk, and the size difference
    // is reasonable.
    if (NewSize <= UsableSize &&
        (UsableSize - NewSize) < (SizeClassMap::kMaxSize / 2)) {
      UnpackedHeader NewHeader = OldHeader;
      NewHeader.SizeOrUnusedBytes =
          OldHeader.ClassId ? NewSize : UsableSize - NewSize;
      Chunk::compareExchangeHeader(OldPtr, &NewHeader, &OldHeader);
      return OldPtr;
    }
    // Otherwise, we have to allocate a new chunk and copy the contents of the
    // old one.
    void *NewPtr = allocate(NewSize, MinAlignment, FromMalloc);
    if (NewPtr) {
      const uptr OldSize = OldHeader.ClassId ? OldHeader.SizeOrUnusedBytes :
          UsableSize - OldHeader.SizeOrUnusedBytes;
      memcpy(NewPtr, OldPtr, Min(NewSize, UsableSize));
      quarantineOrDeallocateChunk(OldPtr, &OldHeader, OldSize);
    }
    return NewPtr;
  }

  // Helper function that returns the actual usable size of a chunk.
  uptr getUsableSize(const void *Ptr) {
    initThreadMaybe();
    if (UNLIKELY(!Ptr))
      return 0;
    UnpackedHeader Header;
    Chunk::loadHeader(Ptr, &Header);
    // Getting the usable size of a chunk only makes sense if it's allocated.
    if (UNLIKELY(Header.State != ChunkAllocated))
      dieWithMessage("invalid chunk state when sizing address %p\n", Ptr);
    return Chunk::getUsableSize(Ptr, &Header);
  }

  void *calloc(uptr NMemB, uptr Size) {
    initThreadMaybe();
    if (UNLIKELY(CheckForCallocOverflow(NMemB, Size))) {
      if (AllocatorMayReturnNull())
        return nullptr;
      reportCallocOverflow(NMemB, Size);
    }
    return allocate(NMemB * Size, MinAlignment, FromMalloc, true);
  }

  void commitBack(ScudoTSD *TSD) {
    Quarantine.Drain(getQuarantineCache(TSD), QuarantineCallback(&TSD->Cache));
    Backend.destroyCache(&TSD->Cache);
  }

  uptr getStats(AllocatorStat StatType) {
    initThreadMaybe();
    uptr stats[AllocatorStatCount];
    Backend.getStats(stats);
    return stats[StatType];
  }

  bool canReturnNull() {
    initThreadMaybe();
    return AllocatorMayReturnNull();
  }

  void setRssLimit(uptr LimitMb, bool HardLimit) {
    if (HardLimit)
      HardRssLimitMb = LimitMb;
    else
      SoftRssLimitMb = LimitMb;
    CheckRssLimit = HardRssLimitMb || SoftRssLimitMb;
  }

  void printStats() {
    initThreadMaybe();
    Backend.printStats();
  }
};

NOINLINE void Allocator::performSanityChecks() {
  // Verify that the header offset field can hold the maximum offset. In the
  // case of the Secondary allocator, it takes care of alignment and the
  // offset will always be 0. In the case of the Primary, the worst case
  // scenario happens in the last size class, when the backend allocation
  // would already be aligned on the requested alignment, which would happen
  // to be the maximum alignment that would fit in that size class. As a
  // result, the maximum offset will be at most the maximum alignment for the
  // last size class minus the header size, in multiples of MinAlignment.
  UnpackedHeader Header = {};
  const uptr MaxPrimaryAlignment =
      1 << MostSignificantSetBitIndex(SizeClassMap::kMaxSize - MinAlignment);
  const uptr MaxOffset =
      (MaxPrimaryAlignment - Chunk::getHeaderSize()) >> MinAlignmentLog;
  Header.Offset = MaxOffset;
  if (Header.Offset != MaxOffset)
    dieWithMessage("maximum possible offset doesn't fit in header\n");
  // Verify that we can fit the maximum size or amount of unused bytes in the
  // header. Given that the Secondary fits the allocation to a page, the worst
  // case scenario happens in the Primary. It will depend on the second to
  // last and last class sizes, as well as the dynamic base for the Primary.
  // The following is an over-approximation that works for our needs.
  const uptr MaxSizeOrUnusedBytes = SizeClassMap::kMaxSize - 1;
  Header.SizeOrUnusedBytes = MaxSizeOrUnusedBytes;
  if (Header.SizeOrUnusedBytes != MaxSizeOrUnusedBytes)
    dieWithMessage("maximum possible unused bytes doesn't fit in header\n");

  const uptr LargestClassId = SizeClassMap::kLargestClassID;
  Header.ClassId = LargestClassId;
  if (Header.ClassId != LargestClassId)
    dieWithMessage("largest class ID doesn't fit in header\n");
}

// Opportunistic RSS limit check. This will update the RSS limit status, if
// it can, every 100ms, otherwise it will just return the current one.
NOINLINE bool Allocator::isRssLimitExceeded() {
  u64 LastCheck = atomic_load_relaxed(&RssLastCheckedAtNS);
  const u64 CurrentCheck = MonotonicNanoTime();
  if (LIKELY(CurrentCheck < LastCheck + (100ULL * 1000000ULL)))
    return atomic_load_relaxed(&RssLimitExceeded);
  if (!atomic_compare_exchange_weak(&RssLastCheckedAtNS, &LastCheck,
                                    CurrentCheck, memory_order_relaxed))
    return atomic_load_relaxed(&RssLimitExceeded);
  // TODO(kostyak): We currently use sanitizer_common's GetRSS which reads the
  //                RSS from /proc/self/statm by default. We might want to
  //                call getrusage directly, even if it's less accurate.
  const uptr CurrentRssMb = GetRSS() >> 20;
  if (HardRssLimitMb && UNLIKELY(HardRssLimitMb < CurrentRssMb))
    dieWithMessage("hard RSS limit exhausted (%zdMb vs %zdMb)\n",
                   HardRssLimitMb, CurrentRssMb);
  if (SoftRssLimitMb) {
    if (atomic_load_relaxed(&RssLimitExceeded)) {
      if (CurrentRssMb <= SoftRssLimitMb)
        atomic_store_relaxed(&RssLimitExceeded, false);
    } else {
      if (CurrentRssMb > SoftRssLimitMb) {
        atomic_store_relaxed(&RssLimitExceeded, true);
        Printf("Scudo INFO: soft RSS limit exhausted (%zdMb vs %zdMb)\n",
               SoftRssLimitMb, CurrentRssMb);
      }
    }
  }
  return atomic_load_relaxed(&RssLimitExceeded);
}

static Allocator Instance(LINKER_INITIALIZED);

static BackendT &getBackend() {
  return Instance.Backend;
}

void initScudo() {
  Instance.init();
}

void ScudoTSD::init() {
  getBackend().initCache(&Cache);
  memset(QuarantineCachePlaceHolder, 0, sizeof(QuarantineCachePlaceHolder));
}

void ScudoTSD::commitBack() {
  Instance.commitBack(this);
}

void *scudoAllocate(uptr Size, uptr Alignment, AllocType Type) {
  if (Alignment && UNLIKELY(!IsPowerOfTwo(Alignment))) {
    errno = EINVAL;
    if (Instance.canReturnNull())
      return nullptr;
    reportAllocationAlignmentNotPowerOfTwo(Alignment);
  }
  return SetErrnoOnNull(Instance.allocate(Size, Alignment, Type));
}

void scudoDeallocate(void *Ptr, uptr Size, uptr Alignment, AllocType Type) {
  Instance.deallocate(Ptr, Size, Alignment, Type);
}

void *scudoRealloc(void *Ptr, uptr Size) {
  if (!Ptr)
    return SetErrnoOnNull(Instance.allocate(Size, MinAlignment, FromMalloc));
  if (Size == 0) {
    Instance.deallocate(Ptr, 0, 0, FromMalloc);
    return nullptr;
  }
  return SetErrnoOnNull(Instance.reallocate(Ptr, Size));
}

void *scudoCalloc(uptr NMemB, uptr Size) {
  return SetErrnoOnNull(Instance.calloc(NMemB, Size));
}

void *scudoValloc(uptr Size) {
  return SetErrnoOnNull(
      Instance.allocate(Size, GetPageSizeCached(), FromMemalign));
}

void *scudoPvalloc(uptr Size) {
  const uptr PageSize = GetPageSizeCached();
  if (UNLIKELY(CheckForPvallocOverflow(Size, PageSize))) {
    errno = ENOMEM;
    if (Instance.canReturnNull())
      return nullptr;
    reportPvallocOverflow(Size);
  }
  // pvalloc(0) should allocate one page.
  Size = Size ? RoundUpTo(Size, PageSize) : PageSize;
  return SetErrnoOnNull(Instance.allocate(Size, PageSize, FromMemalign));
}

int scudoPosixMemalign(void **MemPtr, uptr Alignment, uptr Size) {
  if (UNLIKELY(!CheckPosixMemalignAlignment(Alignment))) {
    if (!Instance.canReturnNull())
      reportInvalidPosixMemalignAlignment(Alignment);
    return EINVAL;
  }
  void *Ptr = Instance.allocate(Size, Alignment, FromMemalign);
  if (UNLIKELY(!Ptr))
    return ENOMEM;
  *MemPtr = Ptr;
  return 0;
}

void *scudoAlignedAlloc(uptr Alignment, uptr Size) {
  if (UNLIKELY(!CheckAlignedAllocAlignmentAndSize(Alignment, Size))) {
    errno = EINVAL;
    if (Instance.canReturnNull())
      return nullptr;
    reportInvalidAlignedAllocAlignment(Size, Alignment);
  }
  return SetErrnoOnNull(Instance.allocate(Size, Alignment, FromMalloc));
}

uptr scudoMallocUsableSize(void *Ptr) {
  return Instance.getUsableSize(Ptr);
}

}  // namespace __scudo

using namespace __scudo;

// MallocExtension helper functions

uptr __sanitizer_get_current_allocated_bytes() {
  return Instance.getStats(AllocatorStatAllocated);
}

uptr __sanitizer_get_heap_size() {
  return Instance.getStats(AllocatorStatMapped);
}

uptr __sanitizer_get_free_bytes() {
  return 1;
}

uptr __sanitizer_get_unmapped_bytes() {
  return 1;
}

uptr __sanitizer_get_estimated_allocated_size(uptr Size) {
  return Size;
}

int __sanitizer_get_ownership(const void *Ptr) {
  return Instance.isValidPointer(Ptr);
}

uptr __sanitizer_get_allocated_size(const void *Ptr) {
  return Instance.getUsableSize(Ptr);
}

#if !SANITIZER_SUPPORTS_WEAK_HOOKS
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_malloc_hook,
                             void *Ptr, uptr Size) {
  (void)Ptr;
  (void)Size;
}

SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_free_hook, void *Ptr) {
  (void)Ptr;
}
#endif

// Interface functions

void __scudo_set_rss_limit(uptr LimitMb, s32 HardLimit) {
  if (!SCUDO_CAN_USE_PUBLIC_INTERFACE)
    return;
  Instance.setRssLimit(LimitMb, !!HardLimit);
}

void __scudo_print_stats() {
  Instance.printStats();
}
