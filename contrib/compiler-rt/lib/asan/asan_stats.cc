//===-- asan_stats.cc -----------------------------------------------------===//
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
// Code related to statistics collected by AddressSanitizer.
//===----------------------------------------------------------------------===//
#include "asan_interceptors.h"
#include "asan_internal.h"
#include "asan_stats.h"
#include "asan_thread.h"
#include "sanitizer_common/sanitizer_allocator_interface.h"
#include "sanitizer_common/sanitizer_mutex.h"
#include "sanitizer_common/sanitizer_stackdepot.h"

namespace __asan {

AsanStats::AsanStats() {
  Clear();
}

void AsanStats::Clear() {
  CHECK(REAL(memset));
  REAL(memset)(this, 0, sizeof(AsanStats));
}

static void PrintMallocStatsArray(const char *prefix,
                                  uptr (&array)[kNumberOfSizeClasses]) {
  Printf("%s", prefix);
  for (uptr i = 0; i < kNumberOfSizeClasses; i++) {
    if (!array[i]) continue;
    Printf("%zu:%zu; ", i, array[i]);
  }
  Printf("\n");
}

void AsanStats::Print() {
  Printf("Stats: %zuM malloced (%zuM for red zones) by %zu calls\n",
             malloced>>20, malloced_redzones>>20, mallocs);
  Printf("Stats: %zuM realloced by %zu calls\n", realloced>>20, reallocs);
  Printf("Stats: %zuM freed by %zu calls\n", freed>>20, frees);
  Printf("Stats: %zuM really freed by %zu calls\n",
             really_freed>>20, real_frees);
  Printf("Stats: %zuM (%zuM-%zuM) mmaped; %zu maps, %zu unmaps\n",
             (mmaped-munmaped)>>20, mmaped>>20, munmaped>>20,
             mmaps, munmaps);

  PrintMallocStatsArray("  mallocs by size class: ", malloced_by_size);
  Printf("Stats: malloc large: %zu\n", malloc_large);
}

void AsanStats::MergeFrom(const AsanStats *stats) {
  uptr *dst_ptr = reinterpret_cast<uptr*>(this);
  const uptr *src_ptr = reinterpret_cast<const uptr*>(stats);
  uptr num_fields = sizeof(*this) / sizeof(uptr);
  for (uptr i = 0; i < num_fields; i++)
    dst_ptr[i] += src_ptr[i];
}

static BlockingMutex print_lock(LINKER_INITIALIZED);

static AsanStats unknown_thread_stats(LINKER_INITIALIZED);
static AsanStats dead_threads_stats(LINKER_INITIALIZED);
static BlockingMutex dead_threads_stats_lock(LINKER_INITIALIZED);
// Required for malloc_zone_statistics() on OS X. This can't be stored in
// per-thread AsanStats.
static uptr max_malloced_memory;

static void MergeThreadStats(ThreadContextBase *tctx_base, void *arg) {
  AsanStats *accumulated_stats = reinterpret_cast<AsanStats*>(arg);
  AsanThreadContext *tctx = static_cast<AsanThreadContext*>(tctx_base);
  if (AsanThread *t = tctx->thread)
    accumulated_stats->MergeFrom(&t->stats());
}

static void GetAccumulatedStats(AsanStats *stats) {
  stats->Clear();
  {
    ThreadRegistryLock l(&asanThreadRegistry());
    asanThreadRegistry()
        .RunCallbackForEachThreadLocked(MergeThreadStats, stats);
  }
  stats->MergeFrom(&unknown_thread_stats);
  {
    BlockingMutexLock lock(&dead_threads_stats_lock);
    stats->MergeFrom(&dead_threads_stats);
  }
  // This is not very accurate: we may miss allocation peaks that happen
  // between two updates of accumulated_stats_. For more accurate bookkeeping
  // the maximum should be updated on every malloc(), which is unacceptable.
  if (max_malloced_memory < stats->malloced) {
    max_malloced_memory = stats->malloced;
  }
}

void FlushToDeadThreadStats(AsanStats *stats) {
  BlockingMutexLock lock(&dead_threads_stats_lock);
  dead_threads_stats.MergeFrom(stats);
  stats->Clear();
}

void FillMallocStatistics(AsanMallocStats *malloc_stats) {
  AsanStats stats;
  GetAccumulatedStats(&stats);
  malloc_stats->blocks_in_use = stats.mallocs;
  malloc_stats->size_in_use = stats.malloced;
  malloc_stats->max_size_in_use = max_malloced_memory;
  malloc_stats->size_allocated = stats.mmaped;
}

AsanStats &GetCurrentThreadStats() {
  AsanThread *t = GetCurrentThread();
  return (t) ? t->stats() : unknown_thread_stats;
}

static void PrintAccumulatedStats() {
  AsanStats stats;
  GetAccumulatedStats(&stats);
  // Use lock to keep reports from mixing up.
  BlockingMutexLock lock(&print_lock);
  stats.Print();
  StackDepotStats *stack_depot_stats = StackDepotGetStats();
  Printf("Stats: StackDepot: %zd ids; %zdM allocated\n",
         stack_depot_stats->n_uniq_ids, stack_depot_stats->allocated >> 20);
  PrintInternalAllocatorStats();
}

}  // namespace __asan

// ---------------------- Interface ---------------- {{{1
using namespace __asan;  // NOLINT

uptr __sanitizer_get_current_allocated_bytes() {
  AsanStats stats;
  GetAccumulatedStats(&stats);
  uptr malloced = stats.malloced;
  uptr freed = stats.freed;
  // Return sane value if malloced < freed due to racy
  // way we update accumulated stats.
  return (malloced > freed) ? malloced - freed : 1;
}

uptr __sanitizer_get_heap_size() {
  AsanStats stats;
  GetAccumulatedStats(&stats);
  return stats.mmaped - stats.munmaped;
}

uptr __sanitizer_get_free_bytes() {
  AsanStats stats;
  GetAccumulatedStats(&stats);
  uptr total_free = stats.mmaped
                  - stats.munmaped
                  + stats.really_freed;
  uptr total_used = stats.malloced
                  + stats.malloced_redzones;
  // Return sane value if total_free < total_used due to racy
  // way we update accumulated stats.
  return (total_free > total_used) ? total_free - total_used : 1;
}

uptr __sanitizer_get_unmapped_bytes() {
  return 0;
}

void __asan_print_accumulated_stats() {
  PrintAccumulatedStats();
}
