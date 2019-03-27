//===-- sanitizer_type_traits.cc --------------------------------*- C++ -*-===//
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
#include "sanitizer_type_traits.h"

namespace __sanitizer {

const bool true_type::value;
const bool false_type::value;

}  // namespace __sanitizer
