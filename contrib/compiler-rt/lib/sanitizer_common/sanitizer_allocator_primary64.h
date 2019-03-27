//===-- sanitizer_allocator_primary64.h -------------------------*- C++ -*-===//
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

template<class SizeClassAllocator> struct SizeClassAllocator64LocalCache;

// SizeClassAllocator64 -- allocator for 64-bit address space.
// The template parameter Params is a class containing the actual parameters.
//
// Space: a portion of address space of kSpaceSize bytes starting at SpaceBeg.
// If kSpaceBeg is ~0 then SpaceBeg is chosen dynamically my mmap.
// Otherwise SpaceBeg=kSpaceBeg (fixed address).
// kSpaceSize is a power of two.
// At the beginning the entire space is mprotect-ed, then small parts of it
// are mapped on demand.
//
// Region: a part of Space dedicated to a single size class.
// There are kNumClasses Regions of equal size.
//
// UserChunk: a piece of memory returned to user.
// MetaChunk: kMetadataSize bytes of metadata associated with a UserChunk.

// FreeArray is an array free-d chunks (stored as 4-byte offsets)
//
// A Region looks like this:
// UserChunk1 ... UserChunkN <gap> MetaChunkN ... MetaChunk1 FreeArray

struct SizeClassAllocator64FlagMasks {  //  Bit masks.
  enum {
    kRandomShuffleChunks = 1,
  };
};

template <class Params>
class SizeClassAllocator64 {
 public:
  using AddressSpaceView = typename Params::AddressSpaceView;
  static const uptr kSpaceBeg = Params::kSpaceBeg;
  static const uptr kSpaceSize = Params::kSpaceSize;
  static const uptr kMetadataSize = Params::kMetadataSize;
  typedef typename Params::SizeClassMap SizeClassMap;
  typedef typename Params::MapUnmapCallback MapUnmapCallback;

  static const bool kRandomShuffleChunks =
      Params::kFlags & SizeClassAllocator64FlagMasks::kRandomShuffleChunks;

  typedef SizeClassAllocator64<Params> ThisT;
  typedef SizeClassAllocator64LocalCache<ThisT> AllocatorCache;

  // When we know the size class (the region base) we can represent a pointer
  // as a 4-byte integer (offset from the region start shifted right by 4).
  typedef u32 CompactPtrT;
  static const uptr kCompactPtrScale = 4;
  CompactPtrT PointerToCompactPtr(uptr base, uptr ptr) const {
    return static_cast<CompactPtrT>((ptr - base) >> kCompactPtrScale);
  }
  uptr CompactPtrToPointer(uptr base, CompactPtrT ptr32) const {
    return base + (static_cast<uptr>(ptr32) << kCompactPtrScale);
  }

  void Init(s32 release_to_os_interval_ms) {
    uptr TotalSpaceSize = kSpaceSize + AdditionalSize();
    if (kUsingConstantSpaceBeg) {
      CHECK_EQ(kSpaceBeg, address_range.Init(TotalSpaceSize,
                                             PrimaryAllocatorName, kSpaceBeg));
    } else {
      NonConstSpaceBeg = address_range.Init(TotalSpaceSize,
                                            PrimaryAllocatorName);
      CHECK_NE(NonConstSpaceBeg, ~(uptr)0);
    }
    SetReleaseToOSIntervalMs(release_to_os_interval_ms);
    MapWithCallbackOrDie(SpaceEnd(), AdditionalSize());
    // Check that the RegionInfo array is aligned on the CacheLine size.
    DCHECK_EQ(SpaceEnd() % kCacheLineSize, 0);
  }

  s32 ReleaseToOSIntervalMs() const {
    return atomic_load(&release_to_os_interval_ms_, memory_order_relaxed);
  }

  void SetReleaseToOSIntervalMs(s32 release_to_os_interval_ms) {
    atomic_store(&release_to_os_interval_ms_, release_to_os_interval_ms,
                 memory_order_relaxed);
  }

  void ForceReleaseToOS() {
    for (uptr class_id = 1; class_id < kNumClasses; class_id++) {
      BlockingMutexLock l(&GetRegionInfo(class_id)->mutex);
      MaybeReleaseToOS(class_id, true /*force*/);
    }
  }

  static bool CanAllocate(uptr size, uptr alignment) {
    return size <= SizeClassMap::kMaxSize &&
      alignment <= SizeClassMap::kMaxSize;
  }

  NOINLINE void ReturnToAllocator(AllocatorStats *stat, uptr class_id,
                                  const CompactPtrT *chunks, uptr n_chunks) {
    RegionInfo *region = GetRegionInfo(class_id);
    uptr region_beg = GetRegionBeginBySizeClass(class_id);
    CompactPtrT *free_array = GetFreeArray(region_beg);

    BlockingMutexLock l(&region->mutex);
    uptr old_num_chunks = region->num_freed_chunks;
    uptr new_num_freed_chunks = old_num_chunks + n_chunks;
    // Failure to allocate free array space while releasing memory is non
    // recoverable.
    if (UNLIKELY(!EnsureFreeArraySpace(region, region_beg,
                                       new_num_freed_chunks))) {
      Report("FATAL: Internal error: %s's allocator exhausted the free list "
             "space for size class %zd (%zd bytes).\n", SanitizerToolName,
             class_id, ClassIdToSize(class_id));
      Die();
    }
    for (uptr i = 0; i < n_chunks; i++)
      free_array[old_num_chunks + i] = chunks[i];
    region->num_freed_chunks = new_num_freed_chunks;
    region->stats.n_freed += n_chunks;

    MaybeReleaseToOS(class_id, false /*force*/);
  }

  NOINLINE bool GetFromAllocator(AllocatorStats *stat, uptr class_id,
                                 CompactPtrT *chunks, uptr n_chunks) {
    RegionInfo *region = GetRegionInfo(class_id);
    uptr region_beg = GetRegionBeginBySizeClass(class_id);
    CompactPtrT *free_array = GetFreeArray(region_beg);

    BlockingMutexLock l(&region->mutex);
    if (UNLIKELY(region->num_freed_chunks < n_chunks)) {
      if (UNLIKELY(!PopulateFreeArray(stat, class_id, region,
                                      n_chunks - region->num_freed_chunks)))
        return false;
      CHECK_GE(region->num_freed_chunks, n_chunks);
    }
    region->num_freed_chunks -= n_chunks;
    uptr base_idx = region->num_freed_chunks;
    for (uptr i = 0; i < n_chunks; i++)
      chunks[i] = free_array[base_idx + i];
    region->stats.n_allocated += n_chunks;
    return true;
  }

  bool PointerIsMine(const void *p) {
    uptr P = reinterpret_cast<uptr>(p);
    if (kUsingConstantSpaceBeg && (kSpaceBeg % kSpaceSize) == 0)
      return P / kSpaceSize == kSpaceBeg / kSpaceSize;
    return P >= SpaceBeg() && P < SpaceEnd();
  }

  uptr GetRegionBegin(const void *p) {
    if (kUsingConstantSpaceBeg)
      return reinterpret_cast<uptr>(p) & ~(kRegionSize - 1);
    uptr space_beg = SpaceBeg();
    return ((reinterpret_cast<uptr>(p)  - space_beg) & ~(kRegionSize - 1)) +
        space_beg;
  }

  uptr GetRegionBeginBySizeClass(uptr class_id) const {
    return SpaceBeg() + kRegionSize * class_id;
  }

  uptr GetSizeClass(const void *p) {
    if (kUsingConstantSpaceBeg && (kSpaceBeg % kSpaceSize) == 0)
      return ((reinterpret_cast<uptr>(p)) / kRegionSize) % kNumClassesRounded;
    return ((reinterpret_cast<uptr>(p) - SpaceBeg()) / kRegionSize) %
           kNumClassesRounded;
  }

  void *GetBlockBegin(const void *p) {
    uptr class_id = GetSizeClass(p);
    uptr size = ClassIdToSize(class_id);
    if (!size) return nullptr;
    uptr chunk_idx = GetChunkIdx((uptr)p, size);
    uptr reg_beg = GetRegionBegin(p);
    uptr beg = chunk_idx * size;
    uptr next_beg = beg + size;
    if (class_id >= kNumClasses) return nullptr;
    RegionInfo *region = GetRegionInfo(class_id);
    if (region->mapped_user >= next_beg)
      return reinterpret_cast<void*>(reg_beg + beg);
    return nullptr;
  }

  uptr GetActuallyAllocatedSize(void *p) {
    CHECK(PointerIsMine(p));
    return ClassIdToSize(GetSizeClass(p));
  }

  uptr ClassID(uptr size) { return SizeClassMap::ClassID(size); }

  void *GetMetaData(const void *p) {
    uptr class_id = GetSizeClass(p);
    uptr size = ClassIdToSize(class_id);
    uptr chunk_idx = GetChunkIdx(reinterpret_cast<uptr>(p), size);
    uptr region_beg = GetRegionBeginBySizeClass(class_id);
    return reinterpret_cast<void *>(GetMetadataEnd(region_beg) -
                                    (1 + chunk_idx) * kMetadataSize);
  }

  uptr TotalMemoryUsed() {
    uptr res = 0;
    for (uptr i = 0; i < kNumClasses; i++)
      res += GetRegionInfo(i)->allocated_user;
    return res;
  }

  // Test-only.
  void TestOnlyUnmap() {
    UnmapWithCallbackOrDie(SpaceBeg(), kSpaceSize + AdditionalSize());
  }

  static void FillMemoryProfile(uptr start, uptr rss, bool file, uptr *stats,
                           uptr stats_size) {
    for (uptr class_id = 0; class_id < stats_size; class_id++)
      if (stats[class_id] == start)
        stats[class_id] = rss;
  }

  void PrintStats(uptr class_id, uptr rss) {
    RegionInfo *region = GetRegionInfo(class_id);
    if (region->mapped_user == 0) return;
    uptr in_use = region->stats.n_allocated - region->stats.n_freed;
    uptr avail_chunks = region->allocated_user / ClassIdToSize(class_id);
    Printf(
        "%s %02zd (%6zd): mapped: %6zdK allocs: %7zd frees: %7zd inuse: %6zd "
        "num_freed_chunks %7zd avail: %6zd rss: %6zdK releases: %6zd "
        "last released: %6zdK region: 0x%zx\n",
        region->exhausted ? "F" : " ", class_id, ClassIdToSize(class_id),
        region->mapped_user >> 10, region->stats.n_allocated,
        region->stats.n_freed, in_use, region->num_freed_chunks, avail_chunks,
        rss >> 10, region->rtoi.num_releases,
        region->rtoi.last_released_bytes >> 10,
        SpaceBeg() + kRegionSize * class_id);
  }

  void PrintStats() {
    uptr rss_stats[kNumClasses];
    for (uptr class_id = 0; class_id < kNumClasses; class_id++)
      rss_stats[class_id] = SpaceBeg() + kRegionSize * class_id;
    GetMemoryProfile(FillMemoryProfile, rss_stats, kNumClasses);

    uptr total_mapped = 0;
    uptr total_rss = 0;
    uptr n_allocated = 0;
    uptr n_freed = 0;
    for (uptr class_id = 1; class_id < kNumClasses; class_id++) {
      RegionInfo *region = GetRegionInfo(class_id);
      if (region->mapped_user != 0) {
        total_mapped += region->mapped_user;
        total_rss += rss_stats[class_id];
      }
      n_allocated += region->stats.n_allocated;
      n_freed += region->stats.n_freed;
    }

    Printf("Stats: SizeClassAllocator64: %zdM mapped (%zdM rss) in "
           "%zd allocations; remains %zd\n", total_mapped >> 20,
           total_rss >> 20, n_allocated, n_allocated - n_freed);
    for (uptr class_id = 1; class_id < kNumClasses; class_id++)
      PrintStats(class_id, rss_stats[class_id]);
  }

  // ForceLock() and ForceUnlock() are needed to implement Darwin malloc zone
  // introspection API.
  void ForceLock() {
    for (uptr i = 0; i < kNumClasses; i++) {
      GetRegionInfo(i)->mutex.Lock();
    }
  }

  void ForceUnlock() {
    for (int i = (int)kNumClasses - 1; i >= 0; i--) {
      GetRegionInfo(i)->mutex.Unlock();
    }
  }

  // Iterate over all existing chunks.
  // The allocator must be locked when calling this function.
  void ForEachChunk(ForEachChunkCallback callback, void *arg) {
    for (uptr class_id = 1; class_id < kNumClasses; class_id++) {
      RegionInfo *region = GetRegionInfo(class_id);
      uptr chunk_size = ClassIdToSize(class_id);
      uptr region_beg = SpaceBeg() + class_id * kRegionSize;
      uptr region_allocated_user_size =
          AddressSpaceView::Load(region)->allocated_user;
      for (uptr chunk = region_beg;
           chunk < region_beg + region_allocated_user_size;
           chunk += chunk_size) {
        // Too slow: CHECK_EQ((void *)chunk, GetBlockBegin((void *)chunk));
        callback(chunk, arg);
      }
    }
  }

  static uptr ClassIdToSize(uptr class_id) {
    return SizeClassMap::Size(class_id);
  }

  static uptr AdditionalSize() {
    return RoundUpTo(sizeof(RegionInfo) * kNumClassesRounded,
                     GetPageSizeCached());
  }

  typedef SizeClassMap SizeClassMapT;
  static const uptr kNumClasses = SizeClassMap::kNumClasses;
  static const uptr kNumClassesRounded = SizeClassMap::kNumClassesRounded;

  // A packed array of counters. Each counter occupies 2^n bits, enough to store
  // counter's max_value. Ctor will try to allocate the required buffer via
  // mapper->MapPackedCounterArrayBuffer and the caller is expected to check
  // whether the initialization was successful by checking IsAllocated() result.
  // For the performance sake, none of the accessors check the validity of the
  // arguments, it is assumed that index is always in [0, n) range and the value
  // is not incremented past max_value.
  template<class MemoryMapperT>
  class PackedCounterArray {
   public:
    PackedCounterArray(u64 num_counters, u64 max_value, MemoryMapperT *mapper)
        : n(num_counters), memory_mapper(mapper) {
      CHECK_GT(num_counters, 0);
      CHECK_GT(max_value, 0);
      constexpr u64 kMaxCounterBits = sizeof(*buffer) * 8ULL;
      // Rounding counter storage size up to the power of two allows for using
      // bit shifts calculating particular counter's index and offset.
      uptr counter_size_bits =
          RoundUpToPowerOfTwo(MostSignificantSetBitIndex(max_value) + 1);
      CHECK_LE(counter_size_bits, kMaxCounterBits);
      counter_size_bits_log = Log2(counter_size_bits);
      counter_mask = ~0ULL >> (kMaxCounterBits - counter_size_bits);

      uptr packing_ratio = kMaxCounterBits >> counter_size_bits_log;
      CHECK_GT(packing_ratio, 0);
      packing_ratio_log = Log2(packing_ratio);
      bit_offset_mask = packing_ratio - 1;

      buffer_size =
          (RoundUpTo(n, 1ULL << packing_ratio_log) >> packing_ratio_log) *
          sizeof(*buffer);
      buffer = reinterpret_cast<u64*>(
          memory_mapper->MapPackedCounterArrayBuffer(buffer_size));
    }
    ~PackedCounterArray() {
      if (buffer) {
        memory_mapper->UnmapPackedCounterArrayBuffer(
            reinterpret_cast<uptr>(buffer), buffer_size);
      }
    }

    bool IsAllocated() const {
      return !!buffer;
    }

    u64 GetCount() const {
      return n;
    }

    uptr Get(uptr i) const {
      DCHECK_LT(i, n);
      uptr index = i >> packing_ratio_log;
      uptr bit_offset = (i & bit_offset_mask) << counter_size_bits_log;
      return (buffer[index] >> bit_offset) & counter_mask;
    }

    void Inc(uptr i) const {
      DCHECK_LT(Get(i), counter_mask);
      uptr index = i >> packing_ratio_log;
      uptr bit_offset = (i & bit_offset_mask) << counter_size_bits_log;
      buffer[index] += 1ULL << bit_offset;
    }

    void IncRange(uptr from, uptr to) const {
      DCHECK_LE(from, to);
      for (uptr i = from; i <= to; i++)
        Inc(i);
    }

   private:
    const u64 n;
    u64 counter_size_bits_log;
    u64 counter_mask;
    u64 packing_ratio_log;
    u64 bit_offset_mask;

    MemoryMapperT* const memory_mapper;
    u64 buffer_size;
    u64* buffer;
  };

  template<class MemoryMapperT>
  class FreePagesRangeTracker {
   public:
    explicit FreePagesRangeTracker(MemoryMapperT* mapper)
        : memory_mapper(mapper),
          page_size_scaled_log(Log2(GetPageSizeCached() >> kCompactPtrScale)),
          in_the_range(false), current_page(0), current_range_start_page(0) {}

    void NextPage(bool freed) {
      if (freed) {
        if (!in_the_range) {
          current_range_start_page = current_page;
          in_the_range = true;
        }
      } else {
        CloseOpenedRange();
      }
      current_page++;
    }

    void Done() {
      CloseOpenedRange();
    }

   private:
    void CloseOpenedRange() {
      if (in_the_range) {
        memory_mapper->ReleasePageRangeToOS(
            current_range_start_page << page_size_scaled_log,
            current_page << page_size_scaled_log);
        in_the_range = false;
      }
    }

    MemoryMapperT* const memory_mapper;
    const uptr page_size_scaled_log;
    bool in_the_range;
    uptr current_page;
    uptr current_range_start_page;
  };

  // Iterates over the free_array to identify memory pages containing freed
  // chunks only and returns these pages back to OS.
  // allocated_pages_count is the total number of pages allocated for the
  // current bucket.
  template<class MemoryMapperT>
  static void ReleaseFreeMemoryToOS(CompactPtrT *free_array,
                                    uptr free_array_count, uptr chunk_size,
                                    uptr allocated_pages_count,
                                    MemoryMapperT *memory_mapper) {
    const uptr page_size = GetPageSizeCached();

    // Figure out the number of chunks per page and whether we can take a fast
    // path (the number of chunks per page is the same for all pages).
    uptr full_pages_chunk_count_max;
    bool same_chunk_count_per_page;
    if (chunk_size <= page_size && page_size % chunk_size == 0) {
      // Same number of chunks per page, no cross overs.
      full_pages_chunk_count_max = page_size / chunk_size;
      same_chunk_count_per_page = true;
    } else if (chunk_size <= page_size && page_size % chunk_size != 0 &&
        chunk_size % (page_size % chunk_size) == 0) {
      // Some chunks are crossing page boundaries, which means that the page
      // contains one or two partial chunks, but all pages contain the same
      // number of chunks.
      full_pages_chunk_count_max = page_size / chunk_size + 1;
      same_chunk_count_per_page = true;
    } else if (chunk_size <= page_size) {
      // Some chunks are crossing page boundaries, which means that the page
      // contains one or two partial chunks.
      full_pages_chunk_count_max = page_size / chunk_size + 2;
      same_chunk_count_per_page = false;
    } else if (chunk_size > page_size && chunk_size % page_size == 0) {
      // One chunk covers multiple pages, no cross overs.
      full_pages_chunk_count_max = 1;
      same_chunk_count_per_page = true;
    } else if (chunk_size > page_size) {
      // One chunk covers multiple pages, Some chunks are crossing page
      // boundaries. Some pages contain one chunk, some contain two.
      full_pages_chunk_count_max = 2;
      same_chunk_count_per_page = false;
    } else {
      UNREACHABLE("All chunk_size/page_size ratios must be handled.");
    }

    PackedCounterArray<MemoryMapperT> counters(allocated_pages_count,
                                               full_pages_chunk_count_max,
                                               memory_mapper);
    if (!counters.IsAllocated())
      return;

    const uptr chunk_size_scaled = chunk_size >> kCompactPtrScale;
    const uptr page_size_scaled = page_size >> kCompactPtrScale;
    const uptr page_size_scaled_log = Log2(page_size_scaled);

    // Iterate over free chunks and count how many free chunks affect each
    // allocated page.
    if (chunk_size <= page_size && page_size % chunk_size == 0) {
      // Each chunk affects one page only.
      for (uptr i = 0; i < free_array_count; i++)
        counters.Inc(free_array[i] >> page_size_scaled_log);
    } else {
      // In all other cases chunks might affect more than one page.
      for (uptr i = 0; i < free_array_count; i++) {
        counters.IncRange(
            free_array[i] >> page_size_scaled_log,
            (free_array[i] + chunk_size_scaled - 1) >> page_size_scaled_log);
      }
    }

    // Iterate over pages detecting ranges of pages with chunk counters equal
    // to the expected number of chunks for the particular page.
    FreePagesRangeTracker<MemoryMapperT> range_tracker(memory_mapper);
    if (same_chunk_count_per_page) {
      // Fast path, every page has the same number of chunks affecting it.
      for (uptr i = 0; i < counters.GetCount(); i++)
        range_tracker.NextPage(counters.Get(i) == full_pages_chunk_count_max);
    } else {
      // Show path, go through the pages keeping count how many chunks affect
      // each page.
      const uptr pn =
          chunk_size < page_size ? page_size_scaled / chunk_size_scaled : 1;
      const uptr pnc = pn * chunk_size_scaled;
      // The idea is to increment the current page pointer by the first chunk
      // size, middle portion size (the portion of the page covered by chunks
      // except the first and the last one) and then the last chunk size, adding
      // up the number of chunks on the current page and checking on every step
      // whether the page boundary was crossed.
      uptr prev_page_boundary = 0;
      uptr current_boundary = 0;
      for (uptr i = 0; i < counters.GetCount(); i++) {
        uptr page_boundary = prev_page_boundary + page_size_scaled;
        uptr chunks_per_page = pn;
        if (current_boundary < page_boundary) {
          if (current_boundary > prev_page_boundary)
            chunks_per_page++;
          current_boundary += pnc;
          if (current_boundary < page_boundary) {
            chunks_per_page++;
            current_boundary += chunk_size_scaled;
          }
        }
        prev_page_boundary = page_boundary;

        range_tracker.NextPage(counters.Get(i) == chunks_per_page);
      }
    }
    range_tracker.Done();
  }

 private:
  friend class MemoryMapper;

  ReservedAddressRange address_range;

  static const uptr kRegionSize = kSpaceSize / kNumClassesRounded;
  // FreeArray is the array of free-d chunks (stored as 4-byte offsets).
  // In the worst case it may reguire kRegionSize/SizeClassMap::kMinSize
  // elements, but in reality this will not happen. For simplicity we
  // dedicate 1/8 of the region's virtual space to FreeArray.
  static const uptr kFreeArraySize = kRegionSize / 8;

  static const bool kUsingConstantSpaceBeg = kSpaceBeg != ~(uptr)0;
  uptr NonConstSpaceBeg;
  uptr SpaceBeg() const {
    return kUsingConstantSpaceBeg ? kSpaceBeg : NonConstSpaceBeg;
  }
  uptr SpaceEnd() const { return  SpaceBeg() + kSpaceSize; }
  // kRegionSize must be >= 2^32.
  COMPILER_CHECK((kRegionSize) >= (1ULL << (SANITIZER_WORDSIZE / 2)));
  // kRegionSize must be <= 2^36, see CompactPtrT.
  COMPILER_CHECK((kRegionSize) <= (1ULL << (SANITIZER_WORDSIZE / 2 + 4)));
  // Call mmap for user memory with at least this size.
  static const uptr kUserMapSize = 1 << 16;
  // Call mmap for metadata memory with at least this size.
  static const uptr kMetaMapSize = 1 << 16;
  // Call mmap for free array memory with at least this size.
  static const uptr kFreeArrayMapSize = 1 << 16;

  atomic_sint32_t release_to_os_interval_ms_;

  struct Stats {
    uptr n_allocated;
    uptr n_freed;
  };

  struct ReleaseToOsInfo {
    uptr n_freed_at_last_release;
    uptr num_releases;
    u64 last_release_at_ns;
    u64 last_released_bytes;
  };

  struct ALIGNED(SANITIZER_CACHE_LINE_SIZE) RegionInfo {
    BlockingMutex mutex;
    uptr num_freed_chunks;  // Number of elements in the freearray.
    uptr mapped_free_array;  // Bytes mapped for freearray.
    uptr allocated_user;  // Bytes allocated for user memory.
    uptr allocated_meta;  // Bytes allocated for metadata.
    uptr mapped_user;  // Bytes mapped for user memory.
    uptr mapped_meta;  // Bytes mapped for metadata.
    u32 rand_state;  // Seed for random shuffle, used if kRandomShuffleChunks.
    bool exhausted;  // Whether region is out of space for new chunks.
    Stats stats;
    ReleaseToOsInfo rtoi;
  };
  COMPILER_CHECK(sizeof(RegionInfo) % kCacheLineSize == 0);

  RegionInfo *GetRegionInfo(uptr class_id) const {
    DCHECK_LT(class_id, kNumClasses);
    RegionInfo *regions = reinterpret_cast<RegionInfo *>(SpaceEnd());
    return &regions[class_id];
  }

  uptr GetMetadataEnd(uptr region_beg) const {
    return region_beg + kRegionSize - kFreeArraySize;
  }

  uptr GetChunkIdx(uptr chunk, uptr size) const {
    if (!kUsingConstantSpaceBeg)
      chunk -= SpaceBeg();

    uptr offset = chunk % kRegionSize;
    // Here we divide by a non-constant. This is costly.
    // size always fits into 32-bits. If the offset fits too, use 32-bit div.
    if (offset >> (SANITIZER_WORDSIZE / 2))
      return offset / size;
    return (u32)offset / (u32)size;
  }

  CompactPtrT *GetFreeArray(uptr region_beg) const {
    return reinterpret_cast<CompactPtrT *>(GetMetadataEnd(region_beg));
  }

  bool MapWithCallback(uptr beg, uptr size) {
    uptr mapped = address_range.Map(beg, size);
    if (UNLIKELY(!mapped))
      return false;
    CHECK_EQ(beg, mapped);
    MapUnmapCallback().OnMap(beg, size);
    return true;
  }

  void MapWithCallbackOrDie(uptr beg, uptr size) {
    CHECK_EQ(beg, address_range.MapOrDie(beg, size));
    MapUnmapCallback().OnMap(beg, size);
  }

  void UnmapWithCallbackOrDie(uptr beg, uptr size) {
    MapUnmapCallback().OnUnmap(beg, size);
    address_range.Unmap(beg, size);
  }

  bool EnsureFreeArraySpace(RegionInfo *region, uptr region_beg,
                            uptr num_freed_chunks) {
    uptr needed_space = num_freed_chunks * sizeof(CompactPtrT);
    if (region->mapped_free_array < needed_space) {
      uptr new_mapped_free_array = RoundUpTo(needed_space, kFreeArrayMapSize);
      CHECK_LE(new_mapped_free_array, kFreeArraySize);
      uptr current_map_end = reinterpret_cast<uptr>(GetFreeArray(region_beg)) +
                             region->mapped_free_array;
      uptr new_map_size = new_mapped_free_array - region->mapped_free_array;
      if (UNLIKELY(!MapWithCallback(current_map_end, new_map_size)))
        return false;
      region->mapped_free_array = new_mapped_free_array;
    }
    return true;
  }

  // Check whether this size class is exhausted.
  bool IsRegionExhausted(RegionInfo *region, uptr class_id,
                         uptr additional_map_size) {
    if (LIKELY(region->mapped_user + region->mapped_meta +
               additional_map_size <= kRegionSize - kFreeArraySize))
      return false;
    if (!region->exhausted) {
      region->exhausted = true;
      Printf("%s: Out of memory. ", SanitizerToolName);
      Printf("The process has exhausted %zuMB for size class %zu.\n",
             kRegionSize >> 20, ClassIdToSize(class_id));
    }
    return true;
  }

  NOINLINE bool PopulateFreeArray(AllocatorStats *stat, uptr class_id,
                                  RegionInfo *region, uptr requested_count) {
    // region->mutex is held.
    const uptr region_beg = GetRegionBeginBySizeClass(class_id);
    const uptr size = ClassIdToSize(class_id);

    const uptr total_user_bytes =
        region->allocated_user + requested_count * size;
    // Map more space for chunks, if necessary.
    if (LIKELY(total_user_bytes > region->mapped_user)) {
      if (UNLIKELY(region->mapped_user == 0)) {
        if (!kUsingConstantSpaceBeg && kRandomShuffleChunks)
          // The random state is initialized from ASLR.
          region->rand_state = static_cast<u32>(region_beg >> 12);
        // Postpone the first release to OS attempt for ReleaseToOSIntervalMs,
        // preventing just allocated memory from being released sooner than
        // necessary and also preventing extraneous ReleaseMemoryPagesToOS calls
        // for short lived processes.
        // Do it only when the feature is turned on, to avoid a potentially
        // extraneous syscall.
        if (ReleaseToOSIntervalMs() >= 0)
          region->rtoi.last_release_at_ns = MonotonicNanoTime();
      }
      // Do the mmap for the user memory.
      const uptr user_map_size =
          RoundUpTo(total_user_bytes - region->mapped_user, kUserMapSize);
      if (UNLIKELY(IsRegionExhausted(region, class_id, user_map_size)))
        return false;
      if (UNLIKELY(!MapWithCallback(region_beg + region->mapped_user,
                                    user_map_size)))
        return false;
      stat->Add(AllocatorStatMapped, user_map_size);
      region->mapped_user += user_map_size;
    }
    const uptr new_chunks_count =
        (region->mapped_user - region->allocated_user) / size;

    if (kMetadataSize) {
      // Calculate the required space for metadata.
      const uptr total_meta_bytes =
          region->allocated_meta + new_chunks_count * kMetadataSize;
      const uptr meta_map_size = (total_meta_bytes > region->mapped_meta) ?
          RoundUpTo(total_meta_bytes - region->mapped_meta, kMetaMapSize) : 0;
      // Map more space for metadata, if necessary.
      if (meta_map_size) {
        if (UNLIKELY(IsRegionExhausted(region, class_id, meta_map_size)))
          return false;
        if (UNLIKELY(!MapWithCallback(
            GetMetadataEnd(region_beg) - region->mapped_meta - meta_map_size,
            meta_map_size)))
          return false;
        region->mapped_meta += meta_map_size;
      }
    }

    // If necessary, allocate more space for the free array and populate it with
    // newly allocated chunks.
    const uptr total_freed_chunks = region->num_freed_chunks + new_chunks_count;
    if (UNLIKELY(!EnsureFreeArraySpace(region, region_beg, total_freed_chunks)))
      return false;
    CompactPtrT *free_array = GetFreeArray(region_beg);
    for (uptr i = 0, chunk = region->allocated_user; i < new_chunks_count;
         i++, chunk += size)
      free_array[total_freed_chunks - 1 - i] = PointerToCompactPtr(0, chunk);
    if (kRandomShuffleChunks)
      RandomShuffle(&free_array[region->num_freed_chunks], new_chunks_count,
                    &region->rand_state);

    // All necessary memory is mapped and now it is safe to advance all
    // 'allocated_*' counters.
    region->num_freed_chunks += new_chunks_count;
    region->allocated_user += new_chunks_count * size;
    CHECK_LE(region->allocated_user, region->mapped_user);
    region->allocated_meta += new_chunks_count * kMetadataSize;
    CHECK_LE(region->allocated_meta, region->mapped_meta);
    region->exhausted = false;

    // TODO(alekseyshl): Consider bumping last_release_at_ns here to prevent
    // MaybeReleaseToOS from releasing just allocated pages or protect these
    // not yet used chunks some other way.

    return true;
  }

  class MemoryMapper {
   public:
    MemoryMapper(const ThisT& base_allocator, uptr class_id)
        : allocator(base_allocator),
          region_base(base_allocator.GetRegionBeginBySizeClass(class_id)),
          released_ranges_count(0),
          released_bytes(0) {
    }

    uptr GetReleasedRangesCount() const {
      return released_ranges_count;
    }

    uptr GetReleasedBytes() const {
      return released_bytes;
    }

    uptr MapPackedCounterArrayBuffer(uptr buffer_size) {
      // TODO(alekseyshl): The idea to explore is to check if we have enough
      // space between num_freed_chunks*sizeof(CompactPtrT) and
      // mapped_free_array to fit buffer_size bytes and use that space instead
      // of mapping a temporary one.
      return reinterpret_cast<uptr>(
          MmapOrDieOnFatalError(buffer_size, "ReleaseToOSPageCounters"));
    }

    void UnmapPackedCounterArrayBuffer(uptr buffer, uptr buffer_size) {
      UnmapOrDie(reinterpret_cast<void *>(buffer), buffer_size);
    }

    // Releases [from, to) range of pages back to OS.
    void ReleasePageRangeToOS(CompactPtrT from, CompactPtrT to) {
      const uptr from_page = allocator.CompactPtrToPointer(region_base, from);
      const uptr to_page = allocator.CompactPtrToPointer(region_base, to);
      ReleaseMemoryPagesToOS(from_page, to_page);
      released_ranges_count++;
      released_bytes += to_page - from_page;
    }

   private:
    const ThisT& allocator;
    const uptr region_base;
    uptr released_ranges_count;
    uptr released_bytes;
  };

  // Attempts to release RAM occupied by freed chunks back to OS. The region is
  // expected to be locked.
  void MaybeReleaseToOS(uptr class_id, bool force) {
    RegionInfo *region = GetRegionInfo(class_id);
    const uptr chunk_size = ClassIdToSize(class_id);
    const uptr page_size = GetPageSizeCached();

    uptr n = region->num_freed_chunks;
    if (n * chunk_size < page_size)
      return;  // No chance to release anything.
    if ((region->stats.n_freed -
         region->rtoi.n_freed_at_last_release) * chunk_size < page_size) {
      return;  // Nothing new to release.
    }

    if (!force) {
      s32 interval_ms = ReleaseToOSIntervalMs();
      if (interval_ms < 0)
        return;

      if (region->rtoi.last_release_at_ns + interval_ms * 1000000ULL >
          MonotonicNanoTime()) {
        return;  // Memory was returned recently.
      }
    }

    MemoryMapper memory_mapper(*this, class_id);

    ReleaseFreeMemoryToOS<MemoryMapper>(
        GetFreeArray(GetRegionBeginBySizeClass(class_id)), n, chunk_size,
        RoundUpTo(region->allocated_user, page_size) / page_size,
        &memory_mapper);

    if (memory_mapper.GetReleasedRangesCount() > 0) {
      region->rtoi.n_freed_at_last_release = region->stats.n_freed;
      region->rtoi.num_releases += memory_mapper.GetReleasedRangesCount();
      region->rtoi.last_released_bytes = memory_mapper.GetReleasedBytes();
    }
    region->rtoi.last_release_at_ns = MonotonicNanoTime();
  }
};
