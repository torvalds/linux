//=- CachePruning.h - Helper to manage the pruning of a cache dir -*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements pruning of a directory intended for cache storage, using
// various policies.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CACHE_PRUNING_H
#define LLVM_SUPPORT_CACHE_PRUNING_H

#include "llvm/ADT/StringRef.h"
#include <chrono>

namespace llvm {

template <typename T> class Expected;

/// Policy for the pruneCache() function. A default constructed
/// CachePruningPolicy provides a reasonable default policy.
struct CachePruningPolicy {
  /// The pruning interval. This is intended to be used to avoid scanning the
  /// directory too often. It does not impact the decision of which file to
  /// prune. A value of 0 forces the scan to occur. A value of None disables
  /// pruning.
  llvm::Optional<std::chrono::seconds> Interval = std::chrono::seconds(1200);

  /// The expiration for a file. When a file hasn't been accessed for Expiration
  /// seconds, it is removed from the cache. A value of 0 disables the
  /// expiration-based pruning.
  std::chrono::seconds Expiration = std::chrono::hours(7 * 24); // 1w

  /// The maximum size for the cache directory, in terms of percentage of the
  /// available space on the disk. Set to 100 to indicate no limit, 50 to
  /// indicate that the cache size will not be left over half the available disk
  /// space. A value over 100 will be reduced to 100. A value of 0 disables the
  /// percentage size-based pruning.
  unsigned MaxSizePercentageOfAvailableSpace = 75;

  /// The maximum size for the cache directory in bytes. A value over the amount
  /// of available space on the disk will be reduced to the amount of available
  /// space. A value of 0 disables the absolute size-based pruning.
  uint64_t MaxSizeBytes = 0;

  /// The maximum number of files in the cache directory. A value of 0 disables
  /// the number of files based pruning.
  ///
  /// This defaults to 1000000 because with that many files there are
  /// diminishing returns on the effectiveness of the cache. Some systems have a
  /// limit on total number of files, and some also limit the number of files
  /// per directory, such as Linux ext4, with the default setting (block size is
  /// 4096 and large_dir disabled), there is a per-directory entry limit of
  /// 508*510*floor(4096/(40+8))~=20M for average filename length of 40.
  uint64_t MaxSizeFiles = 1000000;
};

/// Parse the given string as a cache pruning policy. Defaults are taken from a
/// default constructed CachePruningPolicy object.
/// For example: "prune_interval=30s:prune_after=24h:cache_size=50%"
/// which means a pruning interval of 30 seconds, expiration time of 24 hours
/// and maximum cache size of 50% of available disk space.
Expected<CachePruningPolicy> parseCachePruningPolicy(StringRef PolicyStr);

/// Peform pruning using the supplied policy, returns true if pruning
/// occurred, i.e. if Policy.Interval was expired.
///
/// As a safeguard against data loss if the user specifies the wrong directory
/// as their cache directory, this function will ignore files not matching the
/// pattern "llvmcache-*".
bool pruneCache(StringRef Path, CachePruningPolicy Policy);

} // namespace llvm

#endif
