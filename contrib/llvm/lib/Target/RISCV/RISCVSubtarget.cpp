//===-- RISCVSubtarget.cpp - RISCV Subtarget Information ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the RISCV specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "RISCVSubtarget.h"
#include "RISCV.h"
#include "RISCVFrameLowering.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "riscv-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "RISCVGenSubtargetInfo.inc"

void RISCVSubtarget::anchor() {}

RISCVSubtarget &RISCVSubtarget::initializeSubtargetDependencies(StringRef CPU,
                                                                StringRef FS,
                                                                bool Is64Bit) {
  // Determine default and user-specified characteristics
  std::string CPUName = CPU;
  if (CPUName.empty())
    CPUName = Is64Bit ? "generic-rv64" : "generic-rv32";
  ParseSubtargetFeatures(CPUName, FS);
  if (Is64Bit) {
    XLenVT = MVT::i64;
    XLen = 64;
  }
  return *this;
}

RISCVSubtarget::RISCVSubtarget(const Triple &TT, const std::string &CPU,
                               const std::string &FS, const TargetMachine &TM)
    : RISCVGenSubtargetInfo(TT, CPU, FS),
      FrameLowering(initializeSubtargetDependencies(CPU, FS, TT.isArch64Bit())),
      InstrInfo(), RegInfo(getHwMode()), TLInfo(TM, *this) {}
