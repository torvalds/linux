//===-- llvm/Support/DataTypes.h - Define fixed size types ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Due to layering constraints (Support depends on llvm-c) this is a thin
// wrapper around the implementation that lives in llvm-c, though most clients
// can/should think of this as being provided by Support for simplicity (not
// many clients are aware of their dependency on llvm-c).
//
//===----------------------------------------------------------------------===//

#include "llvm-c/DataTypes.h"
