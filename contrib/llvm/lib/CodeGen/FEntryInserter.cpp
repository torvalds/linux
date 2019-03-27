//===-- FEntryInsertion.cpp - Patchable prologues for LLVM -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file edits function bodies to insert fentry calls.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

using namespace llvm;

namespace {
struct FEntryInserter : public MachineFunctionPass {
  static char ID; // Pass identification, replacement for typeid
  FEntryInserter() : MachineFunctionPass(ID) {
    initializeFEntryInserterPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &F) override;
};
}

bool FEntryInserter::runOnMachineFunction(MachineFunction &MF) {
  const std::string FEntryName =
      MF.getFunction().getFnAttribute("fentry-call").getValueAsString();
  if (FEntryName != "true")
    return false;

  auto &FirstMBB = *MF.begin();
  auto *TII = MF.getSubtarget().getInstrInfo();
  BuildMI(FirstMBB, FirstMBB.begin(), DebugLoc(),
          TII->get(TargetOpcode::FENTRY_CALL));
  return true;
}

char FEntryInserter::ID = 0;
char &llvm::FEntryInserterID = FEntryInserter::ID;
INITIALIZE_PASS(FEntryInserter, "fentry-insert", "Insert fentry calls", false,
                false)
