//===- ARCInfo.h - Additional ARC Info --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains small standalone helper functions and enum definitions for
// the ARC target useful for the compiler back-end and the MC libraries.
// As such, it deliberately does not include references to LLVM core
// code gen types, passes, etc..
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARC_MCTARGETDESC_ARCINFO_H
#define LLVM_LIB_TARGET_ARC_MCTARGETDESC_ARCINFO_H

namespace llvm {

// Enums corresponding to ARC condition codes
namespace ARCCC {

enum CondCode {
  AL = 0x0,
  EQ = 0x1,
  NE = 0x2,
  P = 0x3,
  N = 0x4,
  LO = 0x5,
  HS = 0x6,
  VS = 0x7,
  VC = 0x8,
  GT = 0x9,
  GE = 0xa,
  LT = 0xb,
  LE = 0xc,
  HI = 0xd,
  LS = 0xe,
  PNZ = 0xf,
  Z = 0x11, // Low 4-bits = EQ
  NZ = 0x12 // Low 4-bits = NE
};

enum BRCondCode {
  BREQ = 0x0,
  BRNE = 0x1,
  BRLT = 0x2,
  BRGE = 0x3,
  BRLO = 0x4,
  BRHS = 0x5
};

} // end namespace ARCCC

} // end namespace llvm

#endif
