//===-- sanitizer_allocator_stats.h -----------------------------*- C++ -*-===//
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

// Memory allocator statistics
enum AllocatorStat {
  AllocatorStatAllocated,
  AllocatorStatMapped,
  AllocatorStatCount
};

typedef uptr AllocatorStatCounters[AllocatorStatCount];

// Per-thread stats, live in per-thread cache.
class AllocatorStats {
 public:
  void Init() {
    internal_memset(this, 0, sizeof(*this));
  }
  void InitLinkerInitialized() {}

  void Add(AllocatorStat i, uptr v) {
    v += atomic_load(&stats_[i], memory_order_relaxed);
    atomic_store(&stats_[i], v, memory_order_relaxed);
  }

  void Sub(AllocatorStat i, uptr v) {
    v = atomic_load(&stats_[i], memory_order_relaxed) - v;
    atomic_store(&stats_[i], v, memory_order_relaxed);
  }

  void Set(AllocatorStat i, uptr v) {
    atomic_store(&stats_[i], v, memory_order_relaxed);
  }

  uptr Get(AllocatorStat i) const {
    return atomic_load(&stats_[i], memory_order_relaxed);
  }

 private:
  friend class AllocatorGlobalStats;
  AllocatorStats *next_;
  AllocatorStats *prev_;
  atomic_uintptr_t stats_[AllocatorStatCount];
};

// Global stats, used for aggregation and querying.
class AllocatorGlobalStats : public AllocatorStats {
 public:
  void InitLinkerInitialized() {
    next_ = this;
    prev_ = this;
  }
  void Init() {
    internal_memset(this, 0, sizeof(*this));
    InitLinkerInitialized();
  }

  void Register(AllocatorStats *s) {
    SpinMutexLock l(&mu_);
    s->next_ = next_;
    s->prev_ = this;
    next_->prev_ = s;
    next_ = s;
  }

  void Unregister(AllocatorStats *s) {
    SpinMutexLock l(&mu_);
    s->prev_->next_ = s->next_;
    s->next_->prev_ = s->prev_;
    for (int i = 0; i < AllocatorStatCount; i++)
      Add(AllocatorStat(i), s->Get(AllocatorStat(i)));
  }

  void Get(AllocatorStatCounters s) const {
    internal_memset(s, 0, AllocatorStatCount * sizeof(uptr));
    SpinMutexLock l(&mu_);
    const AllocatorStats *stats = this;
    for (;;) {
      for (int i = 0; i < AllocatorStatCount; i++)
        s[i] += stats->Get(AllocatorStat(i));
      stats = stats->next_;
      if (stats == this)
        break;
    }
    // All stats must be non-negative.
    for (int i = 0; i < AllocatorStatCount; i++)
      s[i] = ((sptr)s[i]) >= 0 ? s[i] : 0;
  }

 private:
  mutable StaticSpinMutex mu_;
};


