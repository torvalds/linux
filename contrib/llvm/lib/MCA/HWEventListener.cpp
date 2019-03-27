//===----------------------- HWEventListener.cpp ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines a vtable anchor for class HWEventListener.
///
//===----------------------------------------------------------------------===//

#include "llvm/MCA/HWEventListener.h"

namespace llvm {
namespace mca {

// Anchor the vtable here.
void HWEventListener::anchor() {}
} // namespace mca
} // namespace llvm
