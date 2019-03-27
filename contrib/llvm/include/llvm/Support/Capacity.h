//===--- Capacity.h - Generic computation of ADT memory use -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the capacity function that computes the amount of
// memory used by an ADT.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CAPACITY_H
#define LLVM_SUPPORT_CAPACITY_H

#include <cstddef>

namespace llvm {

template <typename T>
static inline size_t capacity_in_bytes(const T &x) {
  // This default definition of capacity should work for things like std::vector
  // and friends.  More specialized versions will work for others.
  return x.capacity() * sizeof(typename T::value_type);
}

} // end namespace llvm

#endif

