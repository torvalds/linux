//===- Optional.h - Simple variant for passing optional values --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file provides Optional, a template class modeled in the spirit of
//  OCaml's 'opt' variant.  The idea is to strongly type whether or not
//  a value can be optional.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_OPTIONAL_H
#define LLVM_ADT_OPTIONAL_H

#include "llvm/ADT/None.h"
#include "llvm/Support/AlignOf.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/type_traits.h"
#include <algorithm>
#include <cassert>
#include <new>
#include <utility>

namespace llvm {

namespace optional_detail {
/// Storage for any type.
template <typename T, bool = isPodLike<T>::value> struct OptionalStorage {
  AlignedCharArrayUnion<T> storage;
  bool hasVal = false;

  OptionalStorage() = default;

  OptionalStorage(const T &y) : hasVal(true) { new (storage.buffer) T(y); }
  OptionalStorage(const OptionalStorage &O) : hasVal(O.hasVal) {
    if (hasVal)
      new (storage.buffer) T(*O.getPointer());
  }
  OptionalStorage(T &&y) : hasVal(true) {
    new (storage.buffer) T(std::forward<T>(y));
  }
  OptionalStorage(OptionalStorage &&O) : hasVal(O.hasVal) {
    if (O.hasVal) {
      new (storage.buffer) T(std::move(*O.getPointer()));
    }
  }

  OptionalStorage &operator=(T &&y) {
    if (hasVal)
      *getPointer() = std::move(y);
    else {
      new (storage.buffer) T(std::move(y));
      hasVal = true;
    }
    return *this;
  }
  OptionalStorage &operator=(OptionalStorage &&O) {
    if (!O.hasVal)
      reset();
    else {
      *this = std::move(*O.getPointer());
    }
    return *this;
  }

  // FIXME: these assignments (& the equivalent const T&/const Optional& ctors)
  // could be made more efficient by passing by value, possibly unifying them
  // with the rvalue versions above - but this could place a different set of
  // requirements (notably: the existence of a default ctor) when implemented
  // in that way. Careful SFINAE to avoid such pitfalls would be required.
  OptionalStorage &operator=(const T &y) {
    if (hasVal)
      *getPointer() = y;
    else {
      new (storage.buffer) T(y);
      hasVal = true;
    }
    return *this;
  }
  OptionalStorage &operator=(const OptionalStorage &O) {
    if (!O.hasVal)
      reset();
    else
      *this = *O.getPointer();
    return *this;
  }

  ~OptionalStorage() { reset(); }

  void reset() {
    if (hasVal) {
      (*getPointer()).~T();
      hasVal = false;
    }
  }

  T *getPointer() {
    assert(hasVal);
    return reinterpret_cast<T *>(storage.buffer);
  }
  const T *getPointer() const {
    assert(hasVal);
    return reinterpret_cast<const T *>(storage.buffer);
  }
};

} // namespace optional_detail

template <typename T> class Optional {
  optional_detail::OptionalStorage<T> Storage;

public:
  using value_type = T;

  constexpr Optional() {}
  constexpr Optional(NoneType) {}

  Optional(const T &y) : Storage(y) {}
  Optional(const Optional &O) = default;

  Optional(T &&y) : Storage(std::forward<T>(y)) {}
  Optional(Optional &&O) = default;

  Optional &operator=(T &&y) {
    Storage = std::move(y);
    return *this;
  }
  Optional &operator=(Optional &&O) = default;

  /// Create a new object by constructing it in place with the given arguments.
  template <typename... ArgTypes> void emplace(ArgTypes &&... Args) {
    reset();
    Storage.hasVal = true;
    new (getPointer()) T(std::forward<ArgTypes>(Args)...);
  }

  static inline Optional create(const T *y) {
    return y ? Optional(*y) : Optional();
  }

  Optional &operator=(const T &y) {
    Storage = y;
    return *this;
  }
  Optional &operator=(const Optional &O) = default;

  void reset() { Storage.reset(); }

  const T *getPointer() const {
    assert(Storage.hasVal);
    return reinterpret_cast<const T *>(Storage.storage.buffer);
  }
  T *getPointer() {
    assert(Storage.hasVal);
    return reinterpret_cast<T *>(Storage.storage.buffer);
  }
  const T &getValue() const LLVM_LVALUE_FUNCTION { return *getPointer(); }
  T &getValue() LLVM_LVALUE_FUNCTION { return *getPointer(); }

  explicit operator bool() const { return Storage.hasVal; }
  bool hasValue() const { return Storage.hasVal; }
  const T *operator->() const { return getPointer(); }
  T *operator->() { return getPointer(); }
  const T &operator*() const LLVM_LVALUE_FUNCTION { return *getPointer(); }
  T &operator*() LLVM_LVALUE_FUNCTION { return *getPointer(); }

  template <typename U>
  constexpr T getValueOr(U &&value) const LLVM_LVALUE_FUNCTION {
    return hasValue() ? getValue() : std::forward<U>(value);
  }

#if LLVM_HAS_RVALUE_REFERENCE_THIS
  T &&getValue() && { return std::move(*getPointer()); }
  T &&operator*() && { return std::move(*getPointer()); }

  template <typename U>
  T getValueOr(U &&value) && {
    return hasValue() ? std::move(getValue()) : std::forward<U>(value);
  }
#endif
};

template <typename T> struct isPodLike<Optional<T>> {
  // An Optional<T> is pod-like if T is.
  static const bool value = isPodLike<T>::value;
};

template <typename T, typename U>
bool operator==(const Optional<T> &X, const Optional<U> &Y) {
  if (X && Y)
    return *X == *Y;
  return X.hasValue() == Y.hasValue();
}

template <typename T, typename U>
bool operator!=(const Optional<T> &X, const Optional<U> &Y) {
  return !(X == Y);
}

template <typename T, typename U>
bool operator<(const Optional<T> &X, const Optional<U> &Y) {
  if (X && Y)
    return *X < *Y;
  return X.hasValue() < Y.hasValue();
}

template <typename T, typename U>
bool operator<=(const Optional<T> &X, const Optional<U> &Y) {
  return !(Y < X);
}

template <typename T, typename U>
bool operator>(const Optional<T> &X, const Optional<U> &Y) {
  return Y < X;
}

template <typename T, typename U>
bool operator>=(const Optional<T> &X, const Optional<U> &Y) {
  return !(X < Y);
}

template<typename T>
bool operator==(const Optional<T> &X, NoneType) {
  return !X;
}

template<typename T>
bool operator==(NoneType, const Optional<T> &X) {
  return X == None;
}

template<typename T>
bool operator!=(const Optional<T> &X, NoneType) {
  return !(X == None);
}

template<typename T>
bool operator!=(NoneType, const Optional<T> &X) {
  return X != None;
}

template <typename T> bool operator<(const Optional<T> &X, NoneType) {
  return false;
}

template <typename T> bool operator<(NoneType, const Optional<T> &X) {
  return X.hasValue();
}

template <typename T> bool operator<=(const Optional<T> &X, NoneType) {
  return !(None < X);
}

template <typename T> bool operator<=(NoneType, const Optional<T> &X) {
  return !(X < None);
}

template <typename T> bool operator>(const Optional<T> &X, NoneType) {
  return None < X;
}

template <typename T> bool operator>(NoneType, const Optional<T> &X) {
  return X < None;
}

template <typename T> bool operator>=(const Optional<T> &X, NoneType) {
  return None <= X;
}

template <typename T> bool operator>=(NoneType, const Optional<T> &X) {
  return X <= None;
}

template <typename T> bool operator==(const Optional<T> &X, const T &Y) {
  return X && *X == Y;
}

template <typename T> bool operator==(const T &X, const Optional<T> &Y) {
  return Y && X == *Y;
}

template <typename T> bool operator!=(const Optional<T> &X, const T &Y) {
  return !(X == Y);
}

template <typename T> bool operator!=(const T &X, const Optional<T> &Y) {
  return !(X == Y);
}

template <typename T> bool operator<(const Optional<T> &X, const T &Y) {
  return !X || *X < Y;
}

template <typename T> bool operator<(const T &X, const Optional<T> &Y) {
  return Y && X < *Y;
}

template <typename T> bool operator<=(const Optional<T> &X, const T &Y) {
  return !(Y < X);
}

template <typename T> bool operator<=(const T &X, const Optional<T> &Y) {
  return !(Y < X);
}

template <typename T> bool operator>(const Optional<T> &X, const T &Y) {
  return Y < X;
}

template <typename T> bool operator>(const T &X, const Optional<T> &Y) {
  return Y < X;
}

template <typename T> bool operator>=(const Optional<T> &X, const T &Y) {
  return !(X < Y);
}

template <typename T> bool operator>=(const T &X, const Optional<T> &Y) {
  return !(X < Y);
}

} // end namespace llvm

#endif // LLVM_ADT_OPTIONAL_H
