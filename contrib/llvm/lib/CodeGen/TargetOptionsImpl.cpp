//===-- TargetOptionsImpl.cpp - Options that apply to all targets ----------==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the methods in the TargetOptions.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Target/TargetOptions.h"
using namespace llvm;

/// DisableFramePointerElim - This returns true if frame pointer elimination
/// optimization should be disabled for the given machine function.
bool TargetOptions::DisableFramePointerElim(const MachineFunction &MF) const {
  // Check to see if the target want to forcably keep frame pointer.
  if (MF.getSubtarget().getFrameLowering()->keepFramePointer(MF))
    return true;

  const Function &F = MF.getFunction();

  // TODO: Remove support for old `fp elim` function attributes after fully
  //       migrate to use "frame-pointer"
  if (!F.hasFnAttribute("frame-pointer")) {
    // Check to see if we should eliminate all frame pointers.
    if (F.getFnAttribute("no-frame-pointer-elim").getValueAsString() == "true")
      return true;

    // Check to see if we should eliminate non-leaf frame pointers.
    if (F.hasFnAttribute("no-frame-pointer-elim-non-leaf"))
      return MF.getFrameInfo().hasCalls();

    return false;
  }

  StringRef FP = F.getFnAttribute("frame-pointer").getValueAsString();
  if (FP == "all")
    return true;
  if (FP == "non-leaf")
    return MF.getFrameInfo().hasCalls();
  if (FP == "none")
    return false;
  llvm_unreachable("unknown frame pointer flag");
}

/// HonorSignDependentRoundingFPMath - Return true if the codegen must assume
/// that the rounding mode of the FPU can change from its default.
bool TargetOptions::HonorSignDependentRoundingFPMath() const {
  return !UnsafeFPMath && HonorSignDependentRoundingFPMathOption;
}
