//===- LiveRegUnits.cpp - Register Unit Set -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file This file imlements the LiveRegUnits set.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/LiveRegUnits.h"

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/MCRegisterInfo.h"

using namespace llvm;

void LiveRegUnits::removeRegsNotPreserved(const uint32_t *RegMask) {
  for (unsigned U = 0, E = TRI->getNumRegUnits(); U != E; ++U) {
    for (MCRegUnitRootIterator RootReg(U, TRI); RootReg.isValid(); ++RootReg) {
      if (MachineOperand::clobbersPhysReg(RegMask, *RootReg))
        Units.reset(U);
    }
  }
}

void LiveRegUnits::addRegsInMask(const uint32_t *RegMask) {
  for (unsigned U = 0, E = TRI->getNumRegUnits(); U != E; ++U) {
    for (MCRegUnitRootIterator RootReg(U, TRI); RootReg.isValid(); ++RootReg) {
      if (MachineOperand::clobbersPhysReg(RegMask, *RootReg))
        Units.set(U);
    }
  }
}

void LiveRegUnits::stepBackward(const MachineInstr &MI) {
  // Remove defined registers and regmask kills from the set.
  for (ConstMIBundleOperands O(MI); O.isValid(); ++O) {
    if (O->isReg()) {
      if (!O->isDef() || O->isDebug())
        continue;
      unsigned Reg = O->getReg();
      if (!TargetRegisterInfo::isPhysicalRegister(Reg))
        continue;
      removeReg(Reg);
    } else if (O->isRegMask())
      removeRegsNotPreserved(O->getRegMask());
  }

  // Add uses to the set.
  for (ConstMIBundleOperands O(MI); O.isValid(); ++O) {
    if (!O->isReg() || !O->readsReg() || O->isDebug())
      continue;
    unsigned Reg = O->getReg();
    if (!TargetRegisterInfo::isPhysicalRegister(Reg))
      continue;
    addReg(Reg);
  }
}

void LiveRegUnits::accumulate(const MachineInstr &MI) {
  // Add defs, uses and regmask clobbers to the set.
  for (ConstMIBundleOperands O(MI); O.isValid(); ++O) {
    if (O->isReg()) {
      unsigned Reg = O->getReg();
      if (!TargetRegisterInfo::isPhysicalRegister(Reg))
        continue;
      if (!O->isDef() && !O->readsReg())
        continue;
      addReg(Reg);
    } else if (O->isRegMask())
      addRegsInMask(O->getRegMask());
  }
}

/// Add live-in registers of basic block \p MBB to \p LiveUnits.
static void addBlockLiveIns(LiveRegUnits &LiveUnits,
                            const MachineBasicBlock &MBB) {
  for (const auto &LI : MBB.liveins())
    LiveUnits.addRegMasked(LI.PhysReg, LI.LaneMask);
}

/// Adds all callee saved registers to \p LiveUnits.
static void addCalleeSavedRegs(LiveRegUnits &LiveUnits,
                               const MachineFunction &MF) {
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  for (const MCPhysReg *CSR = MRI.getCalleeSavedRegs(); CSR && *CSR; ++CSR)
    LiveUnits.addReg(*CSR);
}

void LiveRegUnits::addPristines(const MachineFunction &MF) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  if (!MFI.isCalleeSavedInfoValid())
    return;
  /// This function will usually be called on an empty object, handle this
  /// as a special case.
  if (empty()) {
    /// Add all callee saved regs, then remove the ones that are saved and
    /// restored.
    addCalleeSavedRegs(*this, MF);
    /// Remove the ones that are not saved/restored; they are pristine.
    for (const CalleeSavedInfo &Info : MFI.getCalleeSavedInfo())
      removeReg(Info.getReg());
    return;
  }
  /// If a callee-saved register that is not pristine is already present
  /// in the set, we should make sure that it stays in it. Precompute the
  /// set of pristine registers in a separate object.
  /// Add all callee saved regs, then remove the ones that are saved+restored.
  LiveRegUnits Pristine(*TRI);
  addCalleeSavedRegs(Pristine, MF);
  /// Remove the ones that are not saved/restored; they are pristine.
  for (const CalleeSavedInfo &Info : MFI.getCalleeSavedInfo())
    Pristine.removeReg(Info.getReg());
  addUnits(Pristine.getBitVector());
}

void LiveRegUnits::addLiveOuts(const MachineBasicBlock &MBB) {
  const MachineFunction &MF = *MBB.getParent();
  if (!MBB.succ_empty()) {
    addPristines(MF);
    // To get the live-outs we simply merge the live-ins of all successors.
    for (const MachineBasicBlock *Succ : MBB.successors())
      addBlockLiveIns(*this, *Succ);
  } else if (MBB.isReturnBlock()) {
    // For the return block: Add all callee saved registers.
    const MachineFrameInfo &MFI = MF.getFrameInfo();
    if (MFI.isCalleeSavedInfoValid())
      addCalleeSavedRegs(*this, MF);
  }
}

void LiveRegUnits::addLiveIns(const MachineBasicBlock &MBB) {
  const MachineFunction &MF = *MBB.getParent();
  addPristines(MF);
  addBlockLiveIns(*this, MBB);
}
