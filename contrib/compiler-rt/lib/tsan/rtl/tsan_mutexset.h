//===-- tsan_mutexset.h -----------------------------------------*- C++ -*-===//
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
// MutexSet holds the set of mutexes currently held by a thread.
//===----------------------------------------------------------------------===//
#ifndef TSAN_MUTEXSET_H
#define TSAN_MUTEXSET_H

#include "tsan_defs.h"

namespace __tsan {

class MutexSet {
 public:
  // Holds limited number of mutexes.
  // The oldest mutexes are discarded on overflow.
  static const uptr kMaxSize = 16;
  struct Desc {
    u64 id;
    u64 epoch;
    int count;
    bool write;
  };

  MutexSet();
  // The 'id' is obtained from SyncVar::GetId().
  void Add(u64 id, bool write, u64 epoch);
  void Del(u64 id, bool write);
  void Remove(u64 id);  // Removes the mutex completely (if it's destroyed).
  uptr Size() const;
  Desc Get(uptr i) const;

  void operator=(const MutexSet &other) {
    internal_memcpy(this, &other, sizeof(*this));
  }

 private:
#if !SANITIZER_GO
  uptr size_;
  Desc descs_[kMaxSize];
#endif

  void RemovePos(uptr i);
  MutexSet(const MutexSet&);
};

// Go does not have mutexes, so do not spend memory and time.
// (Go sync.Mutex is actually a semaphore -- can be unlocked
// in different goroutine).
#if SANITIZER_GO
MutexSet::MutexSet() {}
void MutexSet::Add(u64 id, bool write, u64 epoch) {}
void MutexSet::Del(u64 id, bool write) {}
void MutexSet::Remove(u64 id) {}
void MutexSet::RemovePos(uptr i) {}
uptr MutexSet::Size() const { return 0; }
MutexSet::Desc MutexSet::Get(uptr i) const { return Desc(); }
#endif

}  // namespace __tsan

#endif  // TSAN_MUTEXSET_H
