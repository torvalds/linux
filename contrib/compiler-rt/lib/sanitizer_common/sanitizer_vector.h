//===-- sanitizer_vector.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is shared between sanitizers run-time libraries.
//
//===----------------------------------------------------------------------===//

// Low-fat STL-like vector container.

#ifndef SANITIZER_VECTOR_H
#define SANITIZER_VECTOR_H

#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_libc.h"

namespace __sanitizer {

template<typename T>
class Vector {
 public:
  explicit Vector()
      : begin_()
      , end_()
      , last_() {
  }

  ~Vector() {
    if (begin_)
      InternalFree(begin_);
  }

  void Reset() {
    if (begin_)
      InternalFree(begin_);
    begin_ = 0;
    end_ = 0;
    last_ = 0;
  }

  uptr Size() const {
    return end_ - begin_;
  }

  T &operator[](uptr i) {
    DCHECK_LT(i, end_ - begin_);
    return begin_[i];
  }

  const T &operator[](uptr i) const {
    DCHECK_LT(i, end_ - begin_);
    return begin_[i];
  }

  T *PushBack() {
    EnsureSize(Size() + 1);
    T *p = &end_[-1];
    internal_memset(p, 0, sizeof(*p));
    return p;
  }

  T *PushBack(const T& v) {
    EnsureSize(Size() + 1);
    T *p = &end_[-1];
    internal_memcpy(p, &v, sizeof(*p));
    return p;
  }

  void PopBack() {
    DCHECK_GT(end_, begin_);
    end_--;
  }

  void Resize(uptr size) {
    if (size == 0) {
      end_ = begin_;
      return;
    }
    uptr old_size = Size();
    if (size <= old_size) {
      end_ = begin_ + size;
      return;
    }
    EnsureSize(size);
    if (old_size < size) {
      for (uptr i = old_size; i < size; i++)
        internal_memset(&begin_[i], 0, sizeof(begin_[i]));
    }
  }

 private:
  T *begin_;
  T *end_;
  T *last_;

  void EnsureSize(uptr size) {
    if (size <= Size())
      return;
    if (size <= (uptr)(last_ - begin_)) {
      end_ = begin_ + size;
      return;
    }
    uptr cap0 = last_ - begin_;
    uptr cap = cap0 * 5 / 4;  // 25% growth
    if (cap == 0)
      cap = 16;
    if (cap < size)
      cap = size;
    T *p = (T*)InternalAlloc(cap * sizeof(T));
    if (cap0) {
      internal_memcpy(p, begin_, cap0 * sizeof(T));
      InternalFree(begin_);
    }
    begin_ = p;
    end_ = begin_ + size;
    last_ = begin_ + cap;
  }

  Vector(const Vector&);
  void operator=(const Vector&);
};
}  // namespace __sanitizer

#endif  // #ifndef SANITIZER_VECTOR_H
