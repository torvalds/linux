//===- ARCMCTargetDesc.h - ARC Target Descriptions --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides ARC specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARC_MCTARGETDESC_ARCMCTARGETDESC_H
#define LLVM_LIB_TARGET_ARC_MCTARGETDESC_ARCMCTARGETDESC_H

#include "llvm/Support/DataTypes.h"

namespace llvm {

class Target;

Target &getTheARCTarget();

} // end namespace llvm

// Defines symbolic names for ARC registers.  This defines a mapping from
// register name to register number.
#define GET_REGINFO_ENUM
#include "ARCGenRegisterInfo.inc"

// Defines symbolic names for the ARC instructions.
#define GET_INSTRINFO_ENUM
#include "ARCGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "ARCGenSubtargetInfo.inc"

#endif // LLVM_LIB_TARGET_ARC_MCTARGETDESC_ARCMCTARGETDESC_H
