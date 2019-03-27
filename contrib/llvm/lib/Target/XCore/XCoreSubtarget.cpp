//===-- XCoreSubtarget.cpp - XCore Subtarget Information ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the XCore specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "XCoreSubtarget.h"
#include "XCore.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "xcore-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "XCoreGenSubtargetInfo.inc"

void XCoreSubtarget::anchor() { }

XCoreSubtarget::XCoreSubtarget(const Triple &TT, const std::string &CPU,
                               const std::string &FS, const TargetMachine &TM)
    : XCoreGenSubtargetInfo(TT, CPU, FS), InstrInfo(), FrameLowering(*this),
      TLInfo(TM, *this), TSInfo() {}
