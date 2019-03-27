//===-- tsan_clock.cc -----------------------------------------------------===//
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
#include "tsan_clock.h"
#include "tsan_rtl.h"
#include "sanitizer_common/sanitizer_placement_new.h"

// SyncClock and ThreadClock implement vector clocks for sync variables
// (mutexes, atomic variables, file descriptors, etc) and threads, respectively.
// ThreadClock contains fixed-size vector clock for maximum number of threads.
// SyncClock contains growable vector clock for currently necessary number of
// threads.
// Together they implement very simple model of operations, namely:
//
//   void ThreadClock::acquire(const SyncClock *src) {
//     for (int i = 0; i < kMaxThreads; i++)
//       clock[i] = max(clock[i], src->clock[i]);
//   }
//
//   void ThreadClock::release(SyncClock *dst) const {
//     for (int i = 0; i < kMaxThreads; i++)
//       dst->clock[i] = max(dst->clock[i], clock[i]);
//   }
//
//   void ThreadClock::ReleaseStore(SyncClock *dst) const {
//     for (int i = 0; i < kMaxThreads; i++)
//       dst->clock[i] = clock[i];
//   }
//
//   void ThreadClock::acq_rel(SyncClock *dst) {
//     acquire(dst);
//     release(dst);
//   }
//
// Conformance to this model is extensively verified in tsan_clock_test.cc.
// However, the implementation is significantly more complex. The complexity
// allows to implement important classes of use cases in O(1) instead of O(N).
//
// The use cases are:
// 1. Singleton/once atomic that has a single release-store operation followed
//    by zillions of acquire-loads (the acquire-load is O(1)).
// 2. Thread-local mutex (both lock and unlock can be O(1)).
// 3. Leaf mutex (unlock is O(1)).
// 4. A mutex shared by 2 threads (both lock and unlock can be O(1)).
// 5. An atomic with a single writer (writes can be O(1)).
// The implementation dynamically adopts to workload. So if an atomic is in
// read-only phase, these reads will be O(1); if it later switches to read/write
// phase, the implementation will correctly handle that by switching to O(N).
//
// Thread-safety note: all const operations on SyncClock's are conducted under
// a shared lock; all non-const operations on SyncClock's are conducted under
// an exclusive lock; ThreadClock's are private to respective threads and so
// do not need any protection.
//
// Description of SyncClock state:
// clk_ - variable size vector clock, low kClkBits hold timestamp,
//   the remaining bits hold "acquired" flag (the actual value is thread's
//   reused counter);
//   if acquried == thr->reused_, then the respective thread has already
//   acquired this clock (except possibly for dirty elements).
// dirty_ - holds up to two indeces in the vector clock that other threads
//   need to acquire regardless of "acquired" flag value;
// release_store_tid_ - denotes that the clock state is a result of
//   release-store operation by the thread with release_store_tid_ index.
// release_store_reused_ - reuse count of release_store_tid_.

// We don't have ThreadState in these methods, so this is an ugly hack that
// works only in C++.
#if !SANITIZER_GO
# define CPP_STAT_INC(typ) StatInc(cur_thread(), typ)
#else
# define CPP_STAT_INC(typ) (void)0
#endif

namespace __tsan {

static atomic_uint32_t *ref_ptr(ClockBlock *cb) {
  return reinterpret_cast<atomic_uint32_t *>(&cb->table[ClockBlock::kRefIdx]);
}

// Drop reference to the first level block idx.
static void UnrefClockBlock(ClockCache *c, u32 idx, uptr blocks) {
  ClockBlock *cb = ctx->clock_alloc.Map(idx);
  atomic_uint32_t *ref = ref_ptr(cb);
  u32 v = atomic_load(ref, memory_order_acquire);
  for (;;) {
    CHECK_GT(v, 0);
    if (v == 1)
      break;
    if (atomic_compare_exchange_strong(ref, &v, v - 1, memory_order_acq_rel))
      return;
  }
  // First level block owns second level blocks, so them as well.
  for (uptr i = 0; i < blocks; i++)
    ctx->clock_alloc.Free(c, cb->table[ClockBlock::kBlockIdx - i]);
  ctx->clock_alloc.Free(c, idx);
}

ThreadClock::ThreadClock(unsigned tid, unsigned reused)
    : tid_(tid)
    , reused_(reused + 1)  // 0 has special meaning
    , cached_idx_()
    , cached_size_()
    , cached_blocks_() {
  CHECK_LT(tid, kMaxTidInClock);
  CHECK_EQ(reused_, ((u64)reused_ << kClkBits) >> kClkBits);
  nclk_ = tid_ + 1;
  last_acquire_ = 0;
  internal_memset(clk_, 0, sizeof(clk_));
}

void ThreadClock::ResetCached(ClockCache *c) {
  if (cached_idx_) {
    UnrefClockBlock(c, cached_idx_, cached_blocks_);
    cached_idx_ = 0;
    cached_size_ = 0;
    cached_blocks_ = 0;
  }
}

void ThreadClock::acquire(ClockCache *c, SyncClock *src) {
  DCHECK_LE(nclk_, kMaxTid);
  DCHECK_LE(src->size_, kMaxTid);
  CPP_STAT_INC(StatClockAcquire);

  // Check if it's empty -> no need to do anything.
  const uptr nclk = src->size_;
  if (nclk == 0) {
    CPP_STAT_INC(StatClockAcquireEmpty);
    return;
  }

  bool acquired = false;
  for (unsigned i = 0; i < kDirtyTids; i++) {
    SyncClock::Dirty dirty = src->dirty_[i];
    unsigned tid = dirty.tid;
    if (tid != kInvalidTid) {
      if (clk_[tid] < dirty.epoch) {
        clk_[tid] = dirty.epoch;
        acquired = true;
      }
    }
  }

  // Check if we've already acquired src after the last release operation on src
  if (tid_ >= nclk || src->elem(tid_).reused != reused_) {
    // O(N) acquire.
    CPP_STAT_INC(StatClockAcquireFull);
    nclk_ = max(nclk_, nclk);
    u64 *dst_pos = &clk_[0];
    for (ClockElem &src_elem : *src) {
      u64 epoch = src_elem.epoch;
      if (*dst_pos < epoch) {
        *dst_pos = epoch;
        acquired = true;
      }
      dst_pos++;
    }

    // Remember that this thread has acquired this clock.
    if (nclk > tid_)
      src->elem(tid_).reused = reused_;
  }

  if (acquired) {
    CPP_STAT_INC(StatClockAcquiredSomething);
    last_acquire_ = clk_[tid_];
    ResetCached(c);
  }
}

void ThreadClock::release(ClockCache *c, SyncClock *dst) {
  DCHECK_LE(nclk_, kMaxTid);
  DCHECK_LE(dst->size_, kMaxTid);

  if (dst->size_ == 0) {
    // ReleaseStore will correctly set release_store_tid_,
    // which can be important for future operations.
    ReleaseStore(c, dst);
    return;
  }

  CPP_STAT_INC(StatClockRelease);
  // Check if we need to resize dst.
  if (dst->size_ < nclk_)
    dst->Resize(c, nclk_);

  // Check if we had not acquired anything from other threads
  // since the last release on dst. If so, we need to update
  // only dst->elem(tid_).
  if (dst->elem(tid_).epoch > last_acquire_) {
    UpdateCurrentThread(c, dst);
    if (dst->release_store_tid_ != tid_ ||
        dst->release_store_reused_ != reused_)
      dst->release_store_tid_ = kInvalidTid;
    return;
  }

  // O(N) release.
  CPP_STAT_INC(StatClockReleaseFull);
  dst->Unshare(c);
  // First, remember whether we've acquired dst.
  bool acquired = IsAlreadyAcquired(dst);
  if (acquired)
    CPP_STAT_INC(StatClockReleaseAcquired);
  // Update dst->clk_.
  dst->FlushDirty();
  uptr i = 0;
  for (ClockElem &ce : *dst) {
    ce.epoch = max(ce.epoch, clk_[i]);
    ce.reused = 0;
    i++;
  }
  // Clear 'acquired' flag in the remaining elements.
  if (nclk_ < dst->size_)
    CPP_STAT_INC(StatClockReleaseClearTail);
  for (uptr i = nclk_; i < dst->size_; i++)
    dst->elem(i).reused = 0;
  dst->release_store_tid_ = kInvalidTid;
  dst->release_store_reused_ = 0;
  // If we've acquired dst, remember this fact,
  // so that we don't need to acquire it on next acquire.
  if (acquired)
    dst->elem(tid_).reused = reused_;
}

void ThreadClock::ReleaseStore(ClockCache *c, SyncClock *dst) {
  DCHECK_LE(nclk_, kMaxTid);
  DCHECK_LE(dst->size_, kMaxTid);
  CPP_STAT_INC(StatClockStore);

  if (dst->size_ == 0 && cached_idx_ != 0) {
    // Reuse the cached clock.
    // Note: we could reuse/cache the cached clock in more cases:
    // we could update the existing clock and cache it, or replace it with the
    // currently cached clock and release the old one. And for a shared
    // existing clock, we could replace it with the currently cached;
    // or unshare, update and cache. But, for simplicity, we currnetly reuse
    // cached clock only when the target clock is empty.
    dst->tab_ = ctx->clock_alloc.Map(cached_idx_);
    dst->tab_idx_ = cached_idx_;
    dst->size_ = cached_size_;
    dst->blocks_ = cached_blocks_;
    CHECK_EQ(dst->dirty_[0].tid, kInvalidTid);
    // The cached clock is shared (immutable),
    // so this is where we store the current clock.
    dst->dirty_[0].tid = tid_;
    dst->dirty_[0].epoch = clk_[tid_];
    dst->release_store_tid_ = tid_;
    dst->release_store_reused_ = reused_;
    // Rememeber that we don't need to acquire it in future.
    dst->elem(tid_).reused = reused_;
    // Grab a reference.
    atomic_fetch_add(ref_ptr(dst->tab_), 1, memory_order_relaxed);
    return;
  }

  // Check if we need to resize dst.
  if (dst->size_ < nclk_)
    dst->Resize(c, nclk_);

  if (dst->release_store_tid_ == tid_ &&
      dst->release_store_reused_ == reused_ &&
      dst->elem(tid_).epoch > last_acquire_) {
    CPP_STAT_INC(StatClockStoreFast);
    UpdateCurrentThread(c, dst);
    return;
  }

  // O(N) release-store.
  CPP_STAT_INC(StatClockStoreFull);
  dst->Unshare(c);
  // Note: dst can be larger than this ThreadClock.
  // This is fine since clk_ beyond size is all zeros.
  uptr i = 0;
  for (ClockElem &ce : *dst) {
    ce.epoch = clk_[i];
    ce.reused = 0;
    i++;
  }
  for (uptr i = 0; i < kDirtyTids; i++)
    dst->dirty_[i].tid = kInvalidTid;
  dst->release_store_tid_ = tid_;
  dst->release_store_reused_ = reused_;
  // Rememeber that we don't need to acquire it in future.
  dst->elem(tid_).reused = reused_;

  // If the resulting clock is cachable, cache it for future release operations.
  // The clock is always cachable if we released to an empty sync object.
  if (cached_idx_ == 0 && dst->Cachable()) {
    // Grab a reference to the ClockBlock.
    atomic_uint32_t *ref = ref_ptr(dst->tab_);
    if (atomic_load(ref, memory_order_acquire) == 1)
      atomic_store_relaxed(ref, 2);
    else
      atomic_fetch_add(ref_ptr(dst->tab_), 1, memory_order_relaxed);
    cached_idx_ = dst->tab_idx_;
    cached_size_ = dst->size_;
    cached_blocks_ = dst->blocks_;
  }
}

void ThreadClock::acq_rel(ClockCache *c, SyncClock *dst) {
  CPP_STAT_INC(StatClockAcquireRelease);
  acquire(c, dst);
  ReleaseStore(c, dst);
}

// Updates only single element related to the current thread in dst->clk_.
void ThreadClock::UpdateCurrentThread(ClockCache *c, SyncClock *dst) const {
  // Update the threads time, but preserve 'acquired' flag.
  for (unsigned i = 0; i < kDirtyTids; i++) {
    SyncClock::Dirty *dirty = &dst->dirty_[i];
    const unsigned tid = dirty->tid;
    if (tid == tid_ || tid == kInvalidTid) {
      CPP_STAT_INC(StatClockReleaseFast);
      dirty->tid = tid_;
      dirty->epoch = clk_[tid_];
      return;
    }
  }
  // Reset all 'acquired' flags, O(N).
  // We are going to touch dst elements, so we need to unshare it.
  dst->Unshare(c);
  CPP_STAT_INC(StatClockReleaseSlow);
  dst->elem(tid_).epoch = clk_[tid_];
  for (uptr i = 0; i < dst->size_; i++)
    dst->elem(i).reused = 0;
  dst->FlushDirty();
}

// Checks whether the current thread has already acquired src.
bool ThreadClock::IsAlreadyAcquired(const SyncClock *src) const {
  if (src->elem(tid_).reused != reused_)
    return false;
  for (unsigned i = 0; i < kDirtyTids; i++) {
    SyncClock::Dirty dirty = src->dirty_[i];
    if (dirty.tid != kInvalidTid) {
      if (clk_[dirty.tid] < dirty.epoch)
        return false;
    }
  }
  return true;
}

// Sets a single element in the vector clock.
// This function is called only from weird places like AcquireGlobal.
void ThreadClock::set(ClockCache *c, unsigned tid, u64 v) {
  DCHECK_LT(tid, kMaxTid);
  DCHECK_GE(v, clk_[tid]);
  clk_[tid] = v;
  if (nclk_ <= tid)
    nclk_ = tid + 1;
  last_acquire_ = clk_[tid_];
  ResetCached(c);
}

void ThreadClock::DebugDump(int(*printf)(const char *s, ...)) {
  printf("clock=[");
  for (uptr i = 0; i < nclk_; i++)
    printf("%s%llu", i == 0 ? "" : ",", clk_[i]);
  printf("] tid=%u/%u last_acq=%llu", tid_, reused_, last_acquire_);
}

SyncClock::SyncClock() {
  ResetImpl();
}

SyncClock::~SyncClock() {
  // Reset must be called before dtor.
  CHECK_EQ(size_, 0);
  CHECK_EQ(blocks_, 0);
  CHECK_EQ(tab_, 0);
  CHECK_EQ(tab_idx_, 0);
}

void SyncClock::Reset(ClockCache *c) {
  if (size_)
    UnrefClockBlock(c, tab_idx_, blocks_);
  ResetImpl();
}

void SyncClock::ResetImpl() {
  tab_ = 0;
  tab_idx_ = 0;
  size_ = 0;
  blocks_ = 0;
  release_store_tid_ = kInvalidTid;
  release_store_reused_ = 0;
  for (uptr i = 0; i < kDirtyTids; i++)
    dirty_[i].tid = kInvalidTid;
}

void SyncClock::Resize(ClockCache *c, uptr nclk) {
  CPP_STAT_INC(StatClockReleaseResize);
  Unshare(c);
  if (nclk <= capacity()) {
    // Memory is already allocated, just increase the size.
    size_ = nclk;
    return;
  }
  if (size_ == 0) {
    // Grow from 0 to one-level table.
    CHECK_EQ(size_, 0);
    CHECK_EQ(blocks_, 0);
    CHECK_EQ(tab_, 0);
    CHECK_EQ(tab_idx_, 0);
    tab_idx_ = ctx->clock_alloc.Alloc(c);
    tab_ = ctx->clock_alloc.Map(tab_idx_);
    internal_memset(tab_, 0, sizeof(*tab_));
    atomic_store_relaxed(ref_ptr(tab_), 1);
    size_ = 1;
  } else if (size_ > blocks_ * ClockBlock::kClockCount) {
    u32 idx = ctx->clock_alloc.Alloc(c);
    ClockBlock *new_cb = ctx->clock_alloc.Map(idx);
    uptr top = size_ - blocks_ * ClockBlock::kClockCount;
    CHECK_LT(top, ClockBlock::kClockCount);
    const uptr move = top * sizeof(tab_->clock[0]);
    internal_memcpy(&new_cb->clock[0], tab_->clock, move);
    internal_memset(&new_cb->clock[top], 0, sizeof(*new_cb) - move);
    internal_memset(tab_->clock, 0, move);
    append_block(idx);
  }
  // At this point we have first level table allocated and all clock elements
  // are evacuated from it to a second level block.
  // Add second level tables as necessary.
  while (nclk > capacity()) {
    u32 idx = ctx->clock_alloc.Alloc(c);
    ClockBlock *cb = ctx->clock_alloc.Map(idx);
    internal_memset(cb, 0, sizeof(*cb));
    append_block(idx);
  }
  size_ = nclk;
}

// Flushes all dirty elements into the main clock array.
void SyncClock::FlushDirty() {
  for (unsigned i = 0; i < kDirtyTids; i++) {
    Dirty *dirty = &dirty_[i];
    if (dirty->tid != kInvalidTid) {
      CHECK_LT(dirty->tid, size_);
      elem(dirty->tid).epoch = dirty->epoch;
      dirty->tid = kInvalidTid;
    }
  }
}

bool SyncClock::IsShared() const {
  if (size_ == 0)
    return false;
  atomic_uint32_t *ref = ref_ptr(tab_);
  u32 v = atomic_load(ref, memory_order_acquire);
  CHECK_GT(v, 0);
  return v > 1;
}

// Unshares the current clock if it's shared.
// Shared clocks are immutable, so they need to be unshared before any updates.
// Note: this does not apply to dirty entries as they are not shared.
void SyncClock::Unshare(ClockCache *c) {
  if (!IsShared())
    return;
  // First, copy current state into old.
  SyncClock old;
  old.tab_ = tab_;
  old.tab_idx_ = tab_idx_;
  old.size_ = size_;
  old.blocks_ = blocks_;
  old.release_store_tid_ = release_store_tid_;
  old.release_store_reused_ = release_store_reused_;
  for (unsigned i = 0; i < kDirtyTids; i++)
    old.dirty_[i] = dirty_[i];
  // Then, clear current object.
  ResetImpl();
  // Allocate brand new clock in the current object.
  Resize(c, old.size_);
  // Now copy state back into this object.
  Iter old_iter(&old);
  for (ClockElem &ce : *this) {
    ce = *old_iter;
    ++old_iter;
  }
  release_store_tid_ = old.release_store_tid_;
  release_store_reused_ = old.release_store_reused_;
  for (unsigned i = 0; i < kDirtyTids; i++)
    dirty_[i] = old.dirty_[i];
  // Drop reference to old and delete if necessary.
  old.Reset(c);
}

// Can we cache this clock for future release operations?
ALWAYS_INLINE bool SyncClock::Cachable() const {
  if (size_ == 0)
    return false;
  for (unsigned i = 0; i < kDirtyTids; i++) {
    if (dirty_[i].tid != kInvalidTid)
      return false;
  }
  return atomic_load_relaxed(ref_ptr(tab_)) == 1;
}

// elem linearizes the two-level structure into linear array.
// Note: this is used only for one time accesses, vector operations use
// the iterator as it is much faster.
ALWAYS_INLINE ClockElem &SyncClock::elem(unsigned tid) const {
  DCHECK_LT(tid, size_);
  const uptr block = tid / ClockBlock::kClockCount;
  DCHECK_LE(block, blocks_);
  tid %= ClockBlock::kClockCount;
  if (block == blocks_)
    return tab_->clock[tid];
  u32 idx = get_block(block);
  ClockBlock *cb = ctx->clock_alloc.Map(idx);
  return cb->clock[tid];
}

ALWAYS_INLINE uptr SyncClock::capacity() const {
  if (size_ == 0)
    return 0;
  uptr ratio = sizeof(ClockBlock::clock[0]) / sizeof(ClockBlock::table[0]);
  // How many clock elements we can fit into the first level block.
  // +1 for ref counter.
  uptr top = ClockBlock::kClockCount - RoundUpTo(blocks_ + 1, ratio) / ratio;
  return blocks_ * ClockBlock::kClockCount + top;
}

ALWAYS_INLINE u32 SyncClock::get_block(uptr bi) const {
  DCHECK(size_);
  DCHECK_LT(bi, blocks_);
  return tab_->table[ClockBlock::kBlockIdx - bi];
}

ALWAYS_INLINE void SyncClock::append_block(u32 idx) {
  uptr bi = blocks_++;
  CHECK_EQ(get_block(bi), 0);
  tab_->table[ClockBlock::kBlockIdx - bi] = idx;
}

// Used only by tests.
u64 SyncClock::get(unsigned tid) const {
  for (unsigned i = 0; i < kDirtyTids; i++) {
    Dirty dirty = dirty_[i];
    if (dirty.tid == tid)
      return dirty.epoch;
  }
  return elem(tid).epoch;
}

// Used only by Iter test.
u64 SyncClock::get_clean(unsigned tid) const {
  return elem(tid).epoch;
}

void SyncClock::DebugDump(int(*printf)(const char *s, ...)) {
  printf("clock=[");
  for (uptr i = 0; i < size_; i++)
    printf("%s%llu", i == 0 ? "" : ",", elem(i).epoch);
  printf("] reused=[");
  for (uptr i = 0; i < size_; i++)
    printf("%s%llu", i == 0 ? "" : ",", elem(i).reused);
  printf("] release_store_tid=%d/%d dirty_tids=%d[%llu]/%d[%llu]",
      release_store_tid_, release_store_reused_,
      dirty_[0].tid, dirty_[0].epoch,
      dirty_[1].tid, dirty_[1].epoch);
}

void SyncClock::Iter::Next() {
  // Finished with the current block, move on to the next one.
  block_++;
  if (block_ < parent_->blocks_) {
    // Iterate over the next second level block.
    u32 idx = parent_->get_block(block_);
    ClockBlock *cb = ctx->clock_alloc.Map(idx);
    pos_ = &cb->clock[0];
    end_ = pos_ + min(parent_->size_ - block_ * ClockBlock::kClockCount,
        ClockBlock::kClockCount);
    return;
  }
  if (block_ == parent_->blocks_ &&
      parent_->size_ > parent_->blocks_ * ClockBlock::kClockCount) {
    // Iterate over elements in the first level block.
    pos_ = &parent_->tab_->clock[0];
    end_ = pos_ + min(parent_->size_ - block_ * ClockBlock::kClockCount,
        ClockBlock::kClockCount);
    return;
  }
  parent_ = nullptr;  // denotes end
}
}  // namespace __tsan
