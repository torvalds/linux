//===-- NVPTXMCTargetDesc.h - NVPTX Target Descriptions ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides NVPTX specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_MCTARGETDESC_NVPTXMCTARGETDESC_H
#define LLVM_LIB_TARGET_NVPTX_MCTARGETDESC_NVPTXMCTARGETDESC_H

#include <stdint.h>

namespace llvm {
class Target;

Target &getTheNVPTXTarget32();
Target &getTheNVPTXTarget64();

} // End llvm namespace

// Defines symbolic names for PTX registers.
#define GET_REGINFO_ENUM
#include "NVPTXGenRegisterInfo.inc"

// Defines symbolic names for the PTX instructions.
#define GET_INSTRINFO_ENUM
#include "NVPTXGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "NVPTXGenSubtargetInfo.inc"

#endif
