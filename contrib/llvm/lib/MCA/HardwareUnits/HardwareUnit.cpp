//===------------------------- HardwareUnit.cpp -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the anchor for the base class that describes
/// simulated hardware units.
///
//===----------------------------------------------------------------------===//

#include "llvm/MCA/HardwareUnits/HardwareUnit.h"

namespace llvm {
namespace mca {

// Pin the vtable with this method.
HardwareUnit::~HardwareUnit() = default;

} // namespace mca
} // namespace llvm
