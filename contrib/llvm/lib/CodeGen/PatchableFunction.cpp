//===-- PatchableFunction.cpp - Patchable prologues for LLVM -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements edits function bodies in place to support the
// "patchable-function" attribute.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"

using namespace llvm;

namespace {
struct PatchableFunction : public MachineFunctionPass {
  static char ID; // Pass identification, replacement for typeid
  PatchableFunction() : MachineFunctionPass(ID) {
    initializePatchableFunctionPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &F) override;
   MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }
};
}

/// Returns true if instruction \p MI will not result in actual machine code
/// instructions.
static bool doesNotGeneratecode(const MachineInstr &MI) {
  // TODO: Introduce an MCInstrDesc flag for this
  switch (MI.getOpcode()) {
  default: return false;
  case TargetOpcode::IMPLICIT_DEF:
  case TargetOpcode::KILL:
  case TargetOpcode::CFI_INSTRUCTION:
  case TargetOpcode::EH_LABEL:
  case TargetOpcode::GC_LABEL:
  case TargetOpcode::DBG_VALUE:
  case TargetOpcode::DBG_LABEL:
    return true;
  }
}

bool PatchableFunction::runOnMachineFunction(MachineFunction &MF) {
  if (!MF.getFunction().hasFnAttribute("patchable-function"))
    return false;

#ifndef NDEBUG
  Attribute PatchAttr = MF.getFunction().getFnAttribute("patchable-function");
  StringRef PatchType = PatchAttr.getValueAsString();
  assert(PatchType == "prologue-short-redirect" && "Only possibility today!");
#endif

  auto &FirstMBB = *MF.begin();
  MachineBasicBlock::iterator FirstActualI = FirstMBB.begin();
  for (; doesNotGeneratecode(*FirstActualI); ++FirstActualI)
    assert(FirstActualI != FirstMBB.end());

  auto *TII = MF.getSubtarget().getInstrInfo();
  auto MIB = BuildMI(FirstMBB, FirstActualI, FirstActualI->getDebugLoc(),
                     TII->get(TargetOpcode::PATCHABLE_OP))
                 .addImm(2)
                 .addImm(FirstActualI->getOpcode());

  for (auto &MO : FirstActualI->operands())
    MIB.add(MO);

  FirstActualI->eraseFromParent();
  MF.ensureAlignment(4);
  return true;
}

char PatchableFunction::ID = 0;
char &llvm::PatchableFunctionID = PatchableFunction::ID;
INITIALIZE_PASS(PatchableFunction, "patchable-function",
                "Implement the 'patchable-function' attribute", false, false)
