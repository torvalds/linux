//==-- AArch64DeadRegisterDefinitions.cpp - Replace dead defs w/ zero reg --==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file When allowed by the instruction, replace a dead definition of a GPR
/// with the zero register. This makes the code a bit friendlier towards the
/// hardware's register renamer.
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "AArch64RegisterInfo.h"
#include "AArch64Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "aarch64-dead-defs"

STATISTIC(NumDeadDefsReplaced, "Number of dead definitions replaced");

#define AARCH64_DEAD_REG_DEF_NAME "AArch64 Dead register definitions"

namespace {
class AArch64DeadRegisterDefinitions : public MachineFunctionPass {
private:
  const TargetRegisterInfo *TRI;
  const MachineRegisterInfo *MRI;
  const TargetInstrInfo *TII;
  bool Changed;
  void processMachineBasicBlock(MachineBasicBlock &MBB);
public:
  static char ID; // Pass identification, replacement for typeid.
  AArch64DeadRegisterDefinitions() : MachineFunctionPass(ID) {
    initializeAArch64DeadRegisterDefinitionsPass(
        *PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &F) override;

  StringRef getPassName() const override { return AARCH64_DEAD_REG_DEF_NAME; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool shouldSkip(const MachineInstr &MI, const MachineFunction &MF) const;
};
char AArch64DeadRegisterDefinitions::ID = 0;
} // end anonymous namespace

INITIALIZE_PASS(AArch64DeadRegisterDefinitions, "aarch64-dead-defs",
                AARCH64_DEAD_REG_DEF_NAME, false, false)

static bool usesFrameIndex(const MachineInstr &MI) {
  for (const MachineOperand &MO : MI.uses())
    if (MO.isFI())
      return true;
  return false;
}

bool
AArch64DeadRegisterDefinitions::shouldSkip(const MachineInstr &MI,
                                           const MachineFunction &MF) const {
  if (!MF.getSubtarget<AArch64Subtarget>().hasLSE())
    return false;

#define CASE_AARCH64_ATOMIC_(PREFIX) \
  case AArch64::PREFIX##X: \
  case AArch64::PREFIX##W: \
  case AArch64::PREFIX##H: \
  case AArch64::PREFIX##B

  for (const MachineMemOperand *MMO : MI.memoperands()) {
    if (MMO->isAtomic()) {
      unsigned Opcode = MI.getOpcode();
      switch (Opcode) {
      default:
        return false;
        break;

      CASE_AARCH64_ATOMIC_(LDADDA):
      CASE_AARCH64_ATOMIC_(LDADDAL):

      CASE_AARCH64_ATOMIC_(LDCLRA):
      CASE_AARCH64_ATOMIC_(LDCLRAL):

      CASE_AARCH64_ATOMIC_(LDEORA):
      CASE_AARCH64_ATOMIC_(LDEORAL):

      CASE_AARCH64_ATOMIC_(LDSETA):
      CASE_AARCH64_ATOMIC_(LDSETAL):

      CASE_AARCH64_ATOMIC_(LDSMAXA):
      CASE_AARCH64_ATOMIC_(LDSMAXAL):

      CASE_AARCH64_ATOMIC_(LDSMINA):
      CASE_AARCH64_ATOMIC_(LDSMINAL):

      CASE_AARCH64_ATOMIC_(LDUMAXA):
      CASE_AARCH64_ATOMIC_(LDUMAXAL):

      CASE_AARCH64_ATOMIC_(LDUMINA):
      CASE_AARCH64_ATOMIC_(LDUMINAL):

      CASE_AARCH64_ATOMIC_(SWPA):
      CASE_AARCH64_ATOMIC_(SWPAL):
        return true;
        break;
                                                                    }
    }
  }

#undef CASE_AARCH64_ATOMIC_

  return false;
}

void AArch64DeadRegisterDefinitions::processMachineBasicBlock(
    MachineBasicBlock &MBB) {
  const MachineFunction &MF = *MBB.getParent();
  for (MachineInstr &MI : MBB) {
    if (usesFrameIndex(MI)) {
      // We need to skip this instruction because while it appears to have a
      // dead def it uses a frame index which might expand into a multi
      // instruction sequence during EPI.
      LLVM_DEBUG(dbgs() << "    Ignoring, operand is frame index\n");
      continue;
    }
    if (MI.definesRegister(AArch64::XZR) || MI.definesRegister(AArch64::WZR)) {
      // It is not allowed to write to the same register (not even the zero
      // register) twice in a single instruction.
      LLVM_DEBUG(
          dbgs()
          << "    Ignoring, XZR or WZR already used by the instruction\n");
      continue;
    }

    if (shouldSkip(MI, MF)) {
      LLVM_DEBUG(dbgs() << "    Ignoring, Atomic instruction with acquire "
                           "semantics using WZR/XZR\n");
      continue;
    }

    const MCInstrDesc &Desc = MI.getDesc();
    for (int I = 0, E = Desc.getNumDefs(); I != E; ++I) {
      MachineOperand &MO = MI.getOperand(I);
      if (!MO.isReg() || !MO.isDef())
        continue;
      // We should not have any relevant physreg defs that are replacable by
      // zero before register allocation. So we just check for dead vreg defs.
      unsigned Reg = MO.getReg();
      if (!TargetRegisterInfo::isVirtualRegister(Reg) ||
          (!MO.isDead() && !MRI->use_nodbg_empty(Reg)))
        continue;
      assert(!MO.isImplicit() && "Unexpected implicit def!");
      LLVM_DEBUG(dbgs() << "  Dead def operand #" << I << " in:\n    ";
                 MI.print(dbgs()));
      // Be careful not to change the register if it's a tied operand.
      if (MI.isRegTiedToUseOperand(I)) {
        LLVM_DEBUG(dbgs() << "    Ignoring, def is tied operand.\n");
        continue;
      }
      const TargetRegisterClass *RC = TII->getRegClass(Desc, I, TRI, MF);
      unsigned NewReg;
      if (RC == nullptr) {
        LLVM_DEBUG(dbgs() << "    Ignoring, register is not a GPR.\n");
        continue;
      } else if (RC->contains(AArch64::WZR))
        NewReg = AArch64::WZR;
      else if (RC->contains(AArch64::XZR))
        NewReg = AArch64::XZR;
      else {
        LLVM_DEBUG(dbgs() << "    Ignoring, register is not a GPR.\n");
        continue;
      }
      LLVM_DEBUG(dbgs() << "    Replacing with zero register. New:\n      ");
      MO.setReg(NewReg);
      MO.setIsDead();
      LLVM_DEBUG(MI.print(dbgs()));
      ++NumDeadDefsReplaced;
      Changed = true;
      // Only replace one dead register, see check for zero register above.
      break;
    }
  }
}

// Scan the function for instructions that have a dead definition of a
// register. Replace that register with the zero register when possible.
bool AArch64DeadRegisterDefinitions::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  TRI = MF.getSubtarget().getRegisterInfo();
  TII = MF.getSubtarget().getInstrInfo();
  MRI = &MF.getRegInfo();
  LLVM_DEBUG(dbgs() << "***** AArch64DeadRegisterDefinitions *****\n");
  Changed = false;
  for (auto &MBB : MF)
    processMachineBasicBlock(MBB);
  return Changed;
}

FunctionPass *llvm::createAArch64DeadRegisterDefinitions() {
  return new AArch64DeadRegisterDefinitions();
}
