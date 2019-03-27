//===-- tsan_mutexset.cc --------------------------------------------------===//
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
#include "tsan_mutexset.h"
#include "tsan_rtl.h"

namespace __tsan {

const uptr MutexSet::kMaxSize;

MutexSet::MutexSet() {
  size_ = 0;
  internal_memset(&descs_, 0, sizeof(descs_));
}

void MutexSet::Add(u64 id, bool write, u64 epoch) {
  // Look up existing mutex with the same id.
  for (uptr i = 0; i < size_; i++) {
    if (descs_[i].id == id) {
      descs_[i].count++;
      descs_[i].epoch = epoch;
      return;
    }
  }
  // On overflow, find the oldest mutex and drop it.
  if (size_ == kMaxSize) {
    u64 minepoch = (u64)-1;
    u64 mini = (u64)-1;
    for (uptr i = 0; i < size_; i++) {
      if (descs_[i].epoch < minepoch) {
        minepoch = descs_[i].epoch;
        mini = i;
      }
    }
    RemovePos(mini);
    CHECK_EQ(size_, kMaxSize - 1);
  }
  // Add new mutex descriptor.
  descs_[size_].id = id;
  descs_[size_].write = write;
  descs_[size_].epoch = epoch;
  descs_[size_].count = 1;
  size_++;
}

void MutexSet::Del(u64 id, bool write) {
  for (uptr i = 0; i < size_; i++) {
    if (descs_[i].id == id) {
      if (--descs_[i].count == 0)
        RemovePos(i);
      return;
    }
  }
}

void MutexSet::Remove(u64 id) {
  for (uptr i = 0; i < size_; i++) {
    if (descs_[i].id == id) {
      RemovePos(i);
      return;
    }
  }
}

void MutexSet::RemovePos(uptr i) {
  CHECK_LT(i, size_);
  descs_[i] = descs_[size_ - 1];
  size_--;
}

uptr MutexSet::Size() const {
  return size_;
}

MutexSet::Desc MutexSet::Get(uptr i) const {
  CHECK_LT(i, size_);
  return descs_[i];
}

}  // namespace __tsan
