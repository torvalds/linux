//===-------------------------- HardwareUnit.h ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines a base class for describing a simulated hardware
/// unit.  These units are used to construct a simulated backend.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_HARDWAREUNIT_H
#define LLVM_MCA_HARDWAREUNIT_H

namespace llvm {
namespace mca {

class HardwareUnit {
  HardwareUnit(const HardwareUnit &H) = delete;
  HardwareUnit &operator=(const HardwareUnit &H) = delete;

public:
  HardwareUnit() = default;
  virtual ~HardwareUnit();
};

} // namespace mca
} // namespace llvm
#endif // LLVM_MCA_HARDWAREUNIT_H
