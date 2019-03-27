//===-- sanitizer_ring_buffer.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Simple ring buffer.
//
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_RING_BUFFER_H
#define SANITIZER_RING_BUFFER_H

#include "sanitizer_common.h"

namespace __sanitizer {
// RingBuffer<T>: fixed-size ring buffer optimized for speed of push().
// T should be a POD type and sizeof(T) should be divisible by sizeof(void*).
// At creation, all elements are zero.
template<class T>
class RingBuffer {
 public:
  COMPILER_CHECK(sizeof(T) % sizeof(void *) == 0);
  static RingBuffer *New(uptr Size) {
    void *Ptr = MmapOrDie(SizeInBytes(Size), "RingBuffer");
    RingBuffer *RB = reinterpret_cast<RingBuffer*>(Ptr);
    uptr End = reinterpret_cast<uptr>(Ptr) + SizeInBytes(Size);
    RB->last_ = RB->next_ = reinterpret_cast<T*>(End - sizeof(T));
    return RB;
  }
  void Delete() {
    UnmapOrDie(this, SizeInBytes(size()));
  }
  uptr size() const {
    return last_ + 1 -
           reinterpret_cast<T *>(reinterpret_cast<uptr>(this) +
                                 2 * sizeof(T *));
  }

  static uptr SizeInBytes(uptr Size) {
    return Size * sizeof(T) + 2 * sizeof(T*);
  }

  uptr SizeInBytes() { return SizeInBytes(size()); }

  void push(T t) {
    *next_ = t;
    next_--;
    // The condition below works only if sizeof(T) is divisible by sizeof(T*).
    if (next_ <= reinterpret_cast<T*>(&next_))
      next_ = last_;
  }

  T operator[](uptr Idx) const {
    CHECK_LT(Idx, size());
    sptr IdxNext = Idx + 1;
    if (IdxNext > last_ - next_)
      IdxNext -= size();
    return next_[IdxNext];
  }

 private:
  RingBuffer() {}
  ~RingBuffer() {}
  RingBuffer(const RingBuffer&) = delete;

  // Data layout:
  // LNDDDDDDDD
  // D: data elements.
  // L: last_, always points to the last data element.
  // N: next_, initially equals to last_, is decremented on every push,
  //    wraps around if it's less or equal than its own address.
  T *last_;
  T *next_;
  T data_[1];  // flexible array.
};

// A ring buffer with externally provided storage that encodes its state in 8
// bytes. Has significant constraints on size and alignment of storage.
// See a comment in hwasan/hwasan_thread_list.h for the motivation behind this.
#if SANITIZER_WORDSIZE == 64
template <class T>
class CompactRingBuffer {
  // Top byte of long_ stores the buffer size in pages.
  // Lower bytes store the address of the next buffer element.
  static constexpr int kPageSizeBits = 12;
  static constexpr int kSizeShift = 56;
  static constexpr uptr kNextMask = (1ULL << kSizeShift) - 1;

  uptr GetStorageSize() const { return (long_ >> kSizeShift) << kPageSizeBits; }

  void Init(void *storage, uptr size) {
    CHECK_EQ(sizeof(CompactRingBuffer<T>), sizeof(void *));
    CHECK(IsPowerOfTwo(size));
    CHECK_GE(size, 1 << kPageSizeBits);
    CHECK_LE(size, 128 << kPageSizeBits);
    CHECK_EQ(size % 4096, 0);
    CHECK_EQ(size % sizeof(T), 0);
    CHECK_EQ((uptr)storage % (size * 2), 0);
    long_ = (uptr)storage | ((size >> kPageSizeBits) << kSizeShift);
  }

  void SetNext(const T *next) {
    long_ = (long_ & ~kNextMask) | (uptr)next;
  }

 public:
  CompactRingBuffer(void *storage, uptr size) {
    Init(storage, size);
  }

  // A copy constructor of sorts.
  CompactRingBuffer(const CompactRingBuffer &other, void *storage) {
    uptr size = other.GetStorageSize();
    internal_memcpy(storage, other.StartOfStorage(), size);
    Init(storage, size);
    uptr Idx = other.Next() - (const T *)other.StartOfStorage();
    SetNext((const T *)storage + Idx);
  }

  T *Next() const { return (T *)(long_ & kNextMask); }

  void *StartOfStorage() const {
    return (void *)((uptr)Next() & ~(GetStorageSize() - 1));
  }

  void *EndOfStorage() const {
    return (void *)((uptr)StartOfStorage() + GetStorageSize());
  }

  uptr size() const { return GetStorageSize() / sizeof(T); }

  void push(T t) {
    T *next = Next();
    *next = t;
    next++;
    next = (T *)((uptr)next & ~GetStorageSize());
    SetNext(next);
  }

  T operator[](uptr Idx) const {
    CHECK_LT(Idx, size());
    const T *Begin = (const T *)StartOfStorage();
    sptr StorageIdx = Next() - Begin;
    StorageIdx -= (sptr)(Idx + 1);
    if (StorageIdx < 0)
      StorageIdx += size();
    return Begin[StorageIdx];
  }

 public:
  ~CompactRingBuffer() {}
  CompactRingBuffer(const CompactRingBuffer &) = delete;

  uptr long_;
};
#endif
}  // namespace __sanitizer

#endif  // SANITIZER_RING_BUFFER_H
