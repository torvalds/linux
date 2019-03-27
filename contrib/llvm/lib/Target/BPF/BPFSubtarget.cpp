//===-- BPFSubtarget.cpp - BPF Subtarget Information ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the BPF specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "BPFSubtarget.h"
#include "BPF.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "bpf-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "BPFGenSubtargetInfo.inc"

void BPFSubtarget::anchor() {}

BPFSubtarget &BPFSubtarget::initializeSubtargetDependencies(StringRef CPU,
                                                            StringRef FS) {
  initializeEnvironment();
  initSubtargetFeatures(CPU, FS);
  ParseSubtargetFeatures(CPU, FS);
  return *this;
}

void BPFSubtarget::initializeEnvironment() {
  HasJmpExt = false;
  HasAlu32 = false;
  UseDwarfRIS = false;
}

void BPFSubtarget::initSubtargetFeatures(StringRef CPU, StringRef FS) {
  if (CPU == "probe")
    CPU = sys::detail::getHostCPUNameForBPF();
  if (CPU == "generic" || CPU == "v1")
    return;
  if (CPU == "v2") {
    HasJmpExt = true;
    return;
  }
}

BPFSubtarget::BPFSubtarget(const Triple &TT, const std::string &CPU,
                           const std::string &FS, const TargetMachine &TM)
    : BPFGenSubtargetInfo(TT, CPU, FS), InstrInfo(),
      FrameLowering(initializeSubtargetDependencies(CPU, FS)),
      TLInfo(TM, *this) {}
