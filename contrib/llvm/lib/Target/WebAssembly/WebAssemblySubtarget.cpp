//===-- WebAssemblySubtarget.cpp - WebAssembly Subtarget Information ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the WebAssembly-specific subclass of
/// TargetSubtarget.
///
//===----------------------------------------------------------------------===//

#include "WebAssemblySubtarget.h"
#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssemblyInstrInfo.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-subtarget"

#define GET_SUBTARGETINFO_CTOR
#define GET_SUBTARGETINFO_TARGET_DESC
#include "WebAssemblyGenSubtargetInfo.inc"

WebAssemblySubtarget &
WebAssemblySubtarget::initializeSubtargetDependencies(StringRef FS) {
  // Determine default and user-specified characteristics

  if (CPUString.empty())
    CPUString = "generic";

  ParseSubtargetFeatures(CPUString, FS);
  return *this;
}

WebAssemblySubtarget::WebAssemblySubtarget(const Triple &TT,
                                           const std::string &CPU,
                                           const std::string &FS,
                                           const TargetMachine &TM)
    : WebAssemblyGenSubtargetInfo(TT, CPU, FS), CPUString(CPU),
      TargetTriple(TT), FrameLowering(),
      InstrInfo(initializeSubtargetDependencies(FS)), TSInfo(),
      TLInfo(TM, *this) {}

bool WebAssemblySubtarget::enableMachineScheduler() const {
  // Disable the MachineScheduler for now. Even with ShouldTrackPressure set and
  // enableMachineSchedDefaultSched overridden, it appears to have an overall
  // negative effect for the kinds of register optimizations we're doing.
  return false;
}

bool WebAssemblySubtarget::useAA() const { return true; }
