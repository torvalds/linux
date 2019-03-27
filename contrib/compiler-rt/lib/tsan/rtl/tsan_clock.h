//===-- tsan_clock.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//
#ifndef TSAN_CLOCK_H
#define TSAN_CLOCK_H

#include "tsan_defs.h"
#include "tsan_dense_alloc.h"

namespace __tsan {

typedef DenseSlabAlloc<ClockBlock, 1<<16, 1<<10> ClockAlloc;
typedef DenseSlabAllocCache ClockCache;

// The clock that lives in sync variables (mutexes, atomics, etc).
class SyncClock {
 public:
  SyncClock();
  ~SyncClock();

  uptr size() const;

  // These are used only in tests.
  u64 get(unsigned tid) const;
  u64 get_clean(unsigned tid) const;

  void Resize(ClockCache *c, uptr nclk);
  void Reset(ClockCache *c);

  void DebugDump(int(*printf)(const char *s, ...));

  // Clock element iterator.
  // Note: it iterates only over the table without regard to dirty entries.
  class Iter {
   public:
    explicit Iter(SyncClock* parent);
    Iter& operator++();
    bool operator!=(const Iter& other);
    ClockElem &operator*();

   private:
    SyncClock *parent_;
    // [pos_, end_) is the current continuous range of clock elements.
    ClockElem *pos_;
    ClockElem *end_;
    int block_;  // Current number of second level block.

    NOINLINE void Next();
  };

  Iter begin();
  Iter end();

 private:
  friend class ThreadClock;
  friend class Iter;
  static const uptr kDirtyTids = 2;

  struct Dirty {
    u64 epoch  : kClkBits;
    u64 tid : 64 - kClkBits;  // kInvalidId if not active
  };

  unsigned release_store_tid_;
  unsigned release_store_reused_;
  Dirty dirty_[kDirtyTids];
  // If size_ is 0, tab_ is nullptr.
  // If size <= 64 (kClockCount), tab_ contains pointer to an array with
  // 64 ClockElem's (ClockBlock::clock).
  // Otherwise, tab_ points to an array with up to 127 u32 elements,
  // each pointing to the second-level 512b block with 64 ClockElem's.
  // Unused space in the first level ClockBlock is used to store additional
  // clock elements.
  // The last u32 element in the first level ClockBlock is always used as
  // reference counter.
  //
  // See the following scheme for details.
  // All memory blocks are 512 bytes (allocated from ClockAlloc).
  // Clock (clk) elements are 64 bits.
  // Idx and ref are 32 bits.
  //
  // tab_
  //    |
  //    \/
  //    +----------------------------------------------------+
  //    | clk128 | clk129 | ...unused... | idx1 | idx0 | ref |
  //    +----------------------------------------------------+
  //                                        |      |
  //                                        |      \/
  //                                        |      +----------------+
  //                                        |      | clk0 ... clk63 |
  //                                        |      +----------------+
  //                                        \/
  //                                        +------------------+
  //                                        | clk64 ... clk127 |
  //                                        +------------------+
  //
  // Note: dirty entries, if active, always override what's stored in the clock.
  ClockBlock *tab_;
  u32 tab_idx_;
  u16 size_;
  u16 blocks_;  // Number of second level blocks.

  void Unshare(ClockCache *c);
  bool IsShared() const;
  bool Cachable() const;
  void ResetImpl();
  void FlushDirty();
  uptr capacity() const;
  u32 get_block(uptr bi) const;
  void append_block(u32 idx);
  ClockElem &elem(unsigned tid) const;
};

// The clock that lives in threads.
class ThreadClock {
 public:
  typedef DenseSlabAllocCache Cache;

  explicit ThreadClock(unsigned tid, unsigned reused = 0);

  u64 get(unsigned tid) const;
  void set(ClockCache *c, unsigned tid, u64 v);
  void set(u64 v);
  void tick();
  uptr size() const;

  void acquire(ClockCache *c, SyncClock *src);
  void release(ClockCache *c, SyncClock *dst);
  void acq_rel(ClockCache *c, SyncClock *dst);
  void ReleaseStore(ClockCache *c, SyncClock *dst);
  void ResetCached(ClockCache *c);

  void DebugReset();
  void DebugDump(int(*printf)(const char *s, ...));

 private:
  static const uptr kDirtyTids = SyncClock::kDirtyTids;
  // Index of the thread associated with he clock ("current thread").
  const unsigned tid_;
  const unsigned reused_;  // tid_ reuse count.
  // Current thread time when it acquired something from other threads.
  u64 last_acquire_;

  // Cached SyncClock (without dirty entries and release_store_tid_).
  // We reuse it for subsequent store-release operations without intervening
  // acquire operations. Since it is shared (and thus constant), clock value
  // for the current thread is then stored in dirty entries in the SyncClock.
  // We host a refernece to the table while it is cached here.
  u32 cached_idx_;
  u16 cached_size_;
  u16 cached_blocks_;

  // Number of active elements in the clk_ table (the rest is zeros).
  uptr nclk_;
  u64 clk_[kMaxTidInClock];  // Fixed size vector clock.

  bool IsAlreadyAcquired(const SyncClock *src) const;
  void UpdateCurrentThread(ClockCache *c, SyncClock *dst) const;
};

ALWAYS_INLINE u64 ThreadClock::get(unsigned tid) const {
  DCHECK_LT(tid, kMaxTidInClock);
  return clk_[tid];
}

ALWAYS_INLINE void ThreadClock::set(u64 v) {
  DCHECK_GE(v, clk_[tid_]);
  clk_[tid_] = v;
}

ALWAYS_INLINE void ThreadClock::tick() {
  clk_[tid_]++;
}

ALWAYS_INLINE uptr ThreadClock::size() const {
  return nclk_;
}

ALWAYS_INLINE SyncClock::Iter SyncClock::begin() {
  return Iter(this);
}

ALWAYS_INLINE SyncClock::Iter SyncClock::end() {
  return Iter(nullptr);
}

ALWAYS_INLINE uptr SyncClock::size() const {
  return size_;
}

ALWAYS_INLINE SyncClock::Iter::Iter(SyncClock* parent)
    : parent_(parent)
    , pos_(nullptr)
    , end_(nullptr)
    , block_(-1) {
  if (parent)
    Next();
}

ALWAYS_INLINE SyncClock::Iter& SyncClock::Iter::operator++() {
  pos_++;
  if (UNLIKELY(pos_ >= end_))
    Next();
  return *this;
}

ALWAYS_INLINE bool SyncClock::Iter::operator!=(const SyncClock::Iter& other) {
  return parent_ != other.parent_;
}

ALWAYS_INLINE ClockElem &SyncClock::Iter::operator*() {
  return *pos_;
}
}  // namespace __tsan

#endif  // TSAN_CLOCK_H
