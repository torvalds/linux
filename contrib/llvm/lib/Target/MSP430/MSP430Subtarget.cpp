//===-- MSP430Subtarget.cpp - MSP430 Subtarget Information ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the MSP430 specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "MSP430Subtarget.h"
#include "MSP430.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "msp430-subtarget"

static cl::opt<MSP430Subtarget::HWMultEnum>
HWMultModeOption("mhwmult", cl::Hidden,
           cl::desc("Hardware multiplier use mode for MSP430"),
           cl::init(MSP430Subtarget::NoHWMult),
           cl::values(
             clEnumValN(MSP430Subtarget::NoHWMult, "none",
                "Do not use hardware multiplier"),
             clEnumValN(MSP430Subtarget::HWMult16, "16bit",
                "Use 16-bit hardware multiplier"),
             clEnumValN(MSP430Subtarget::HWMult32, "32bit",
                "Use 32-bit hardware multiplier"),
             clEnumValN(MSP430Subtarget::HWMultF5, "f5series",
                "Use F5 series hardware multiplier")));

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "MSP430GenSubtargetInfo.inc"

void MSP430Subtarget::anchor() { }

MSP430Subtarget &
MSP430Subtarget::initializeSubtargetDependencies(StringRef CPU, StringRef FS) {
  ExtendedInsts = false;
  HWMultMode = NoHWMult;

  std::string CPUName = CPU;
  if (CPUName.empty())
    CPUName = "msp430";

  ParseSubtargetFeatures(CPUName, FS);

  if (HWMultModeOption != NoHWMult)
    HWMultMode = HWMultModeOption;

  return *this;
}

MSP430Subtarget::MSP430Subtarget(const Triple &TT, const std::string &CPU,
                                 const std::string &FS, const TargetMachine &TM)
    : MSP430GenSubtargetInfo(TT, CPU, FS), FrameLowering(),
      InstrInfo(initializeSubtargetDependencies(CPU, FS)), TLInfo(TM, *this) {}
