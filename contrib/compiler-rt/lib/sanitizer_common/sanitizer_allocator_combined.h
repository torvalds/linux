//===-- sanitizer_allocator_combined.h --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Part of the Sanitizer Allocator.
//
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_ALLOCATOR_H
#error This file must be included inside sanitizer_allocator.h
#endif

// This class implements a complete memory allocator by using two
// internal allocators:
// PrimaryAllocator is efficient, but may not allocate some sizes (alignments).
//  When allocating 2^x bytes it should return 2^x aligned chunk.
// PrimaryAllocator is used via a local AllocatorCache.
// SecondaryAllocator can allocate anything, but is not efficient.
template <class PrimaryAllocator, class AllocatorCache,
          class SecondaryAllocator,
          typename AddressSpaceViewTy = LocalAddressSpaceView>  // NOLINT
class CombinedAllocator {
 public:
  using AddressSpaceView = AddressSpaceViewTy;
  static_assert(is_same<AddressSpaceView,
                        typename PrimaryAllocator::AddressSpaceView>::value,
                "PrimaryAllocator is using wrong AddressSpaceView");
  static_assert(is_same<AddressSpaceView,
                        typename SecondaryAllocator::AddressSpaceView>::value,
                "SecondaryAllocator is using wrong AddressSpaceView");

  void InitLinkerInitialized(s32 release_to_os_interval_ms) {
    stats_.InitLinkerInitialized();
    primary_.Init(release_to_os_interval_ms);
    secondary_.InitLinkerInitialized();
  }

  void Init(s32 release_to_os_interval_ms) {
    stats_.Init();
    primary_.Init(release_to_os_interval_ms);
    secondary_.Init();
  }

  void *Allocate(AllocatorCache *cache, uptr size, uptr alignment) {
    // Returning 0 on malloc(0) may break a lot of code.
    if (size == 0)
      size = 1;
    if (size + alignment < size) {
      Report("WARNING: %s: CombinedAllocator allocation overflow: "
             "0x%zx bytes with 0x%zx alignment requested\n",
             SanitizerToolName, size, alignment);
      return nullptr;
    }
    uptr original_size = size;
    // If alignment requirements are to be fulfilled by the frontend allocator
    // rather than by the primary or secondary, passing an alignment lower than
    // or equal to 8 will prevent any further rounding up, as well as the later
    // alignment check.
    if (alignment > 8)
      size = RoundUpTo(size, alignment);
    // The primary allocator should return a 2^x aligned allocation when
    // requested 2^x bytes, hence using the rounded up 'size' when being
    // serviced by the primary (this is no longer true when the primary is
    // using a non-fixed base address). The secondary takes care of the
    // alignment without such requirement, and allocating 'size' would use
    // extraneous memory, so we employ 'original_size'.
    void *res;
    if (primary_.CanAllocate(size, alignment))
      res = cache->Allocate(&primary_, primary_.ClassID(size));
    else
      res = secondary_.Allocate(&stats_, original_size, alignment);
    if (alignment > 8)
      CHECK_EQ(reinterpret_cast<uptr>(res) & (alignment - 1), 0);
    return res;
  }

  s32 ReleaseToOSIntervalMs() const {
    return primary_.ReleaseToOSIntervalMs();
  }

  void SetReleaseToOSIntervalMs(s32 release_to_os_interval_ms) {
    primary_.SetReleaseToOSIntervalMs(release_to_os_interval_ms);
  }

  void ForceReleaseToOS() {
    primary_.ForceReleaseToOS();
  }

  void Deallocate(AllocatorCache *cache, void *p) {
    if (!p) return;
    if (primary_.PointerIsMine(p))
      cache->Deallocate(&primary_, primary_.GetSizeClass(p), p);
    else
      secondary_.Deallocate(&stats_, p);
  }

  void *Reallocate(AllocatorCache *cache, void *p, uptr new_size,
                   uptr alignment) {
    if (!p)
      return Allocate(cache, new_size, alignment);
    if (!new_size) {
      Deallocate(cache, p);
      return nullptr;
    }
    CHECK(PointerIsMine(p));
    uptr old_size = GetActuallyAllocatedSize(p);
    uptr memcpy_size = Min(new_size, old_size);
    void *new_p = Allocate(cache, new_size, alignment);
    if (new_p)
      internal_memcpy(new_p, p, memcpy_size);
    Deallocate(cache, p);
    return new_p;
  }

  bool PointerIsMine(void *p) {
    if (primary_.PointerIsMine(p))
      return true;
    return secondary_.PointerIsMine(p);
  }

  bool FromPrimary(void *p) {
    return primary_.PointerIsMine(p);
  }

  void *GetMetaData(const void *p) {
    if (primary_.PointerIsMine(p))
      return primary_.GetMetaData(p);
    return secondary_.GetMetaData(p);
  }

  void *GetBlockBegin(const void *p) {
    if (primary_.PointerIsMine(p))
      return primary_.GetBlockBegin(p);
    return secondary_.GetBlockBegin(p);
  }

  // This function does the same as GetBlockBegin, but is much faster.
  // Must be called with the allocator locked.
  void *GetBlockBeginFastLocked(void *p) {
    if (primary_.PointerIsMine(p))
      return primary_.GetBlockBegin(p);
    return secondary_.GetBlockBeginFastLocked(p);
  }

  uptr GetActuallyAllocatedSize(void *p) {
    if (primary_.PointerIsMine(p))
      return primary_.GetActuallyAllocatedSize(p);
    return secondary_.GetActuallyAllocatedSize(p);
  }

  uptr TotalMemoryUsed() {
    return primary_.TotalMemoryUsed() + secondary_.TotalMemoryUsed();
  }

  void TestOnlyUnmap() { primary_.TestOnlyUnmap(); }

  void InitCache(AllocatorCache *cache) {
    cache->Init(&stats_);
  }

  void DestroyCache(AllocatorCache *cache) {
    cache->Destroy(&primary_, &stats_);
  }

  void SwallowCache(AllocatorCache *cache) {
    cache->Drain(&primary_);
  }

  void GetStats(AllocatorStatCounters s) const {
    stats_.Get(s);
  }

  void PrintStats() {
    primary_.PrintStats();
    secondary_.PrintStats();
  }

  // ForceLock() and ForceUnlock() are needed to implement Darwin malloc zone
  // introspection API.
  void ForceLock() {
    primary_.ForceLock();
    secondary_.ForceLock();
  }

  void ForceUnlock() {
    secondary_.ForceUnlock();
    primary_.ForceUnlock();
  }

  // Iterate over all existing chunks.
  // The allocator must be locked when calling this function.
  void ForEachChunk(ForEachChunkCallback callback, void *arg) {
    primary_.ForEachChunk(callback, arg);
    secondary_.ForEachChunk(callback, arg);
  }

 private:
  PrimaryAllocator primary_;
  SecondaryAllocator secondary_;
  AllocatorGlobalStats stats_;
};
