//===-- XCoreFrameToArgsOffsetElim.cpp ----------------------------*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Replace Pseudo FRAME_TO_ARGS_OFFSET with the appropriate real offset.
//
//===----------------------------------------------------------------------===//

#include "XCore.h"
#include "XCoreInstrInfo.h"
#include "XCoreSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

namespace {
  struct XCoreFTAOElim : public MachineFunctionPass {
    static char ID;
    XCoreFTAOElim() : MachineFunctionPass(ID) {}

    bool runOnMachineFunction(MachineFunction &Fn) override;
    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }

    StringRef getPassName() const override {
      return "XCore FRAME_TO_ARGS_OFFSET Elimination";
    }
  };
  char XCoreFTAOElim::ID = 0;
}

/// createXCoreFrameToArgsOffsetEliminationPass - returns an instance of the
/// Frame to args offset elimination pass
FunctionPass *llvm::createXCoreFrameToArgsOffsetEliminationPass() {
  return new XCoreFTAOElim();
}

bool XCoreFTAOElim::runOnMachineFunction(MachineFunction &MF) {
  const XCoreInstrInfo &TII =
      *static_cast<const XCoreInstrInfo *>(MF.getSubtarget().getInstrInfo());
  unsigned StackSize = MF.getFrameInfo().getStackSize();
  for (MachineFunction::iterator MFI = MF.begin(), E = MF.end(); MFI != E;
       ++MFI) {
    MachineBasicBlock &MBB = *MFI;
    for (MachineBasicBlock::iterator MBBI = MBB.begin(), EE = MBB.end();
         MBBI != EE; ++MBBI) {
      if (MBBI->getOpcode() == XCore::FRAME_TO_ARGS_OFFSET) {
        MachineInstr &OldInst = *MBBI;
        unsigned Reg = OldInst.getOperand(0).getReg();
        MBBI = TII.loadImmediate(MBB, MBBI, Reg, StackSize);
        OldInst.eraseFromParent();
      }
    }
  }
  return true;
}
