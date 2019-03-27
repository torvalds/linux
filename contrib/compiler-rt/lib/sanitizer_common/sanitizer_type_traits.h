//===-- sanitizer_type_traits.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implements a subset of C++ type traits. This is so we can avoid depending
// on system C++ headers.
//
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_TYPE_TRAITS_H
#define SANITIZER_TYPE_TRAITS_H

namespace __sanitizer {

struct true_type {
  static const bool value = true;
};

struct false_type {
  static const bool value = false;
};

// is_same<T, U>
//
// Type trait to compare if types are the same.
// E.g.
//
// ```
// is_same<int,int>::value - True
// is_same<int,char>::value - False
// ```
template <typename T, typename U>
struct is_same : public false_type {};

template <typename T>
struct is_same<T, T> : public true_type {};

}  // namespace __sanitizer

#endif
