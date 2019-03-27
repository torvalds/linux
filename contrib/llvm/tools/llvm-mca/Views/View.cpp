//===----------------------- View.cpp ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the virtual anchor method in View.h to pin the vtable.
///
//===----------------------------------------------------------------------===//

#include "Views/View.h"

namespace llvm {
namespace mca {

void View::anchor() {}
} // namespace mca
} // namespace llvm
