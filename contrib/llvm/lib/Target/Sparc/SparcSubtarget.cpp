//===-- SparcSubtarget.cpp - SPARC Subtarget Information ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the SPARC specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "SparcSubtarget.h"
#include "Sparc.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "sparc-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "SparcGenSubtargetInfo.inc"

void SparcSubtarget::anchor() { }

SparcSubtarget &SparcSubtarget::initializeSubtargetDependencies(StringRef CPU,
                                                                StringRef FS) {
  UseSoftMulDiv = false;
  IsV9 = false;
  IsLeon = false;
  V8DeprecatedInsts = false;
  IsVIS = false;
  IsVIS2 = false;
  IsVIS3 = false;
  HasHardQuad = false;
  UsePopc = false;
  UseSoftFloat = false;
  HasNoFSMULD = false;
  HasNoFMULS = false;

  // Leon features
  HasLeonCasa = false;
  HasUmacSmac = false;
  HasPWRPSR = false;
  InsertNOPLoad = false;
  FixAllFDIVSQRT = false;
  DetectRoundChange = false;
  HasLeonCycleCounter = false;

  // Determine default and user specified characteristics
  std::string CPUName = CPU;
  if (CPUName.empty())
    CPUName = (Is64Bit) ? "v9" : "v8";

  // Parse features string.
  ParseSubtargetFeatures(CPUName, FS);

  // Popc is a v9-only instruction.
  if (!IsV9)
    UsePopc = false;

  return *this;
}

SparcSubtarget::SparcSubtarget(const Triple &TT, const std::string &CPU,
                               const std::string &FS, const TargetMachine &TM,
                               bool is64Bit)
    : SparcGenSubtargetInfo(TT, CPU, FS), TargetTriple(TT), Is64Bit(is64Bit),
      InstrInfo(initializeSubtargetDependencies(CPU, FS)), TLInfo(TM, *this),
      FrameLowering(*this) {}

int SparcSubtarget::getAdjustedFrameSize(int frameSize) const {

  if (is64Bit()) {
    // All 64-bit stack frames must be 16-byte aligned, and must reserve space
    // for spilling the 16 window registers at %sp+BIAS..%sp+BIAS+128.
    frameSize += 128;
    // Frames with calls must also reserve space for 6 outgoing arguments
    // whether they are used or not. LowerCall_64 takes care of that.
    frameSize = alignTo(frameSize, 16);
  } else {
    // Emit the correct save instruction based on the number of bytes in
    // the frame. Minimum stack frame size according to V8 ABI is:
    //   16 words for register window spill
    //    1 word for address of returned aggregate-value
    // +  6 words for passing parameters on the stack
    // ----------
    //   23 words * 4 bytes per word = 92 bytes
    frameSize += 92;

    // Round up to next doubleword boundary -- a double-word boundary
    // is required by the ABI.
    frameSize = alignTo(frameSize, 8);
  }
  return frameSize;
}

bool SparcSubtarget::enableMachineScheduler() const {
  return true;
}
