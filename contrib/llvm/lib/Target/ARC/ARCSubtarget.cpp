//===- ARCSubtarget.cpp - ARC Subtarget Information -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the ARC specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "ARCSubtarget.h"
#include "ARC.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "arc-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "ARCGenSubtargetInfo.inc"

void ARCSubtarget::anchor() {}

ARCSubtarget::ARCSubtarget(const Triple &TT, const std::string &CPU,
                           const std::string &FS, const TargetMachine &TM)
    : ARCGenSubtargetInfo(TT, CPU, FS), FrameLowering(*this),
      TLInfo(TM, *this) {}
