//===-- esan_circular_buffer.h ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of EfficiencySanitizer, a family of performance tuners.
//
// Circular buffer data structure.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_common.h"

namespace __esan {

// A circular buffer for POD data whose memory is allocated using mmap.
// There are two usage models: one is to use initialize/free (for global
// instances) and the other is to use placement new with the
// constructor and to call the destructor or free (they are equivalent).
template<typename T>
class CircularBuffer {
 public:
  // To support global instances we cannot initialize any field in the
  // default constructor.
  explicit CircularBuffer() {}
  CircularBuffer(uptr BufferCapacity) {
    initialize(BufferCapacity);
    WasConstructed = true;
  }
  ~CircularBuffer() {
    if (WasConstructed) // Else caller will call free() explicitly.
      free();
  }
  void initialize(uptr BufferCapacity) {
    Capacity = BufferCapacity;
    // MmapOrDie rounds up to the page size for us.
    Data = (T *)MmapOrDie(Capacity * sizeof(T), "CircularBuffer");
    StartIdx = 0;
    Count = 0;
    WasConstructed = false;
  }
  void free() {
    UnmapOrDie(Data, Capacity * sizeof(T));
  }
  T &operator[](uptr Idx) {
    CHECK_LT(Idx, Count);
    uptr ArrayIdx = (StartIdx + Idx) % Capacity;
    return Data[ArrayIdx];
  }
  const T &operator[](uptr Idx) const {
    CHECK_LT(Idx, Count);
    uptr ArrayIdx = (StartIdx + Idx) % Capacity;
    return Data[ArrayIdx];
  }
  void push_back(const T &Item) {
    CHECK_GT(Capacity, 0);
    uptr ArrayIdx = (StartIdx + Count) % Capacity;
    Data[ArrayIdx] = Item;
    if (Count < Capacity)
      ++Count;
    else
      StartIdx = (StartIdx + 1) % Capacity;
  }
  T &back() {
    CHECK_GT(Count, 0);
    uptr ArrayIdx = (StartIdx + Count - 1) % Capacity;
    return Data[ArrayIdx];
  }
  void pop_back() {
    CHECK_GT(Count, 0);
    --Count;
  }
  uptr size() const {
    return Count;
  }
  void clear() {
    StartIdx = 0;
    Count = 0;
  }
  bool empty() const { return size() == 0; }

 private:
  CircularBuffer(const CircularBuffer&);
  void operator=(const CircularBuffer&);

  bool WasConstructed;
  T *Data;
  uptr Capacity;
  uptr StartIdx;
  uptr Count;
};

} // namespace __esan
