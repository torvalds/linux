//===-- lib/CodeGen/GlobalISel/GISelChangeObserver.cpp --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file constains common code to combine machine functions at generic
// level.
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/GISelChangeObserver.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

void GISelChangeObserver::changingAllUsesOfReg(
    const MachineRegisterInfo &MRI, unsigned Reg) {
  for (auto &ChangingMI : MRI.use_instructions(Reg)) {
    changingInstr(ChangingMI);
    ChangingAllUsesOfReg.insert(&ChangingMI);
  }
}

void GISelChangeObserver::finishedChangingAllUsesOfReg() {
  for (auto *ChangedMI : ChangingAllUsesOfReg)
    changedInstr(*ChangedMI);
}

RAIIDelegateInstaller::RAIIDelegateInstaller(MachineFunction &MF,
                                             MachineFunction::Delegate *Del)
    : MF(MF), Delegate(Del) {
  // Register this as the delegate for handling insertions and deletions of
  // instructions.
  MF.setDelegate(Del);
}

RAIIDelegateInstaller::~RAIIDelegateInstaller() { MF.resetDelegate(Delegate); }
