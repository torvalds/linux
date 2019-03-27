//===-- asan_stats.h --------------------------------------------*- C++ -*-===//
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
// ASan-private header for statistics.
//===----------------------------------------------------------------------===//
#ifndef ASAN_STATS_H
#define ASAN_STATS_H

#include "asan_allocator.h"
#include "asan_internal.h"

namespace __asan {

// AsanStats struct is NOT thread-safe.
// Each AsanThread has its own AsanStats, which are sometimes flushed
// to the accumulated AsanStats.
struct AsanStats {
  // AsanStats must be a struct consisting of uptr fields only.
  // When merging two AsanStats structs, we treat them as arrays of uptr.
  uptr mallocs;
  uptr malloced;
  uptr malloced_redzones;
  uptr frees;
  uptr freed;
  uptr real_frees;
  uptr really_freed;
  uptr reallocs;
  uptr realloced;
  uptr mmaps;
  uptr mmaped;
  uptr munmaps;
  uptr munmaped;
  uptr malloc_large;
  uptr malloced_by_size[kNumberOfSizeClasses];

  // Ctor for global AsanStats (accumulated stats for dead threads).
  explicit AsanStats(LinkerInitialized) { }
  // Creates empty stats.
  AsanStats();

  void Print();  // Prints formatted stats to stderr.
  void Clear();
  void MergeFrom(const AsanStats *stats);
};

// Returns stats for GetCurrentThread(), or stats for fake "unknown thread"
// if GetCurrentThread() returns 0.
AsanStats &GetCurrentThreadStats();
// Flushes a given stats into accumulated stats of dead threads.
void FlushToDeadThreadStats(AsanStats *stats);

// A cross-platform equivalent of malloc_statistics_t on Mac OS.
struct AsanMallocStats {
  uptr blocks_in_use;
  uptr size_in_use;
  uptr max_size_in_use;
  uptr size_allocated;
};

void FillMallocStatistics(AsanMallocStats *malloc_stats);

}  // namespace __asan

#endif  // ASAN_STATS_H
