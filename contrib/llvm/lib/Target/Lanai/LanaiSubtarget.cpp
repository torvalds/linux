//===- LanaiSubtarget.cpp - Lanai Subtarget Information -----------*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Lanai specific subclass of TargetSubtarget.
//
//===----------------------------------------------------------------------===//

#include "LanaiSubtarget.h"

#include "Lanai.h"

#define DEBUG_TYPE "lanai-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "LanaiGenSubtargetInfo.inc"

using namespace llvm;

void LanaiSubtarget::initSubtargetFeatures(StringRef CPU, StringRef FS) {
  std::string CPUName = CPU;
  if (CPUName.empty())
    CPUName = "generic";

  ParseSubtargetFeatures(CPUName, FS);
}

LanaiSubtarget &LanaiSubtarget::initializeSubtargetDependencies(StringRef CPU,
                                                                StringRef FS) {
  initSubtargetFeatures(CPU, FS);
  return *this;
}

LanaiSubtarget::LanaiSubtarget(const Triple &TargetTriple, StringRef Cpu,
                               StringRef FeatureString, const TargetMachine &TM,
                               const TargetOptions & /*Options*/,
                               CodeModel::Model /*CodeModel*/,
                               CodeGenOpt::Level /*OptLevel*/)
    : LanaiGenSubtargetInfo(TargetTriple, Cpu, FeatureString),
      FrameLowering(initializeSubtargetDependencies(Cpu, FeatureString)),
      InstrInfo(), TLInfo(TM, *this), TSInfo() {}
