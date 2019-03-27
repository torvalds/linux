//===-- AArch64CondBrTuning.cpp --- Conditional branch tuning for AArch64 -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file contains a pass that transforms CBZ/CBNZ/TBZ/TBNZ instructions
/// into a conditional branch (B.cond), when the NZCV flags can be set for
/// "free".  This is preferred on targets that have more flexibility when
/// scheduling B.cond instructions as compared to CBZ/CBNZ/TBZ/TBNZ (assuming
/// all other variables are equal).  This can also reduce register pressure.
///
/// A few examples:
///
/// 1) add w8, w0, w1  -> cmn w0, w1             ; CMN is an alias of ADDS.
///    cbz w8, .LBB_2  -> b.eq .LBB0_2
///
/// 2) add w8, w0, w1  -> adds w8, w0, w1        ; w8 has multiple uses.
///    cbz w8, .LBB1_2 -> b.eq .LBB1_2
///
/// 3) sub w8, w0, w1       -> subs w8, w0, w1   ; w8 has multiple uses.
///    tbz w8, #31, .LBB6_2 -> b.pl .LBB6_2
///
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "AArch64Subtarget.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "aarch64-cond-br-tuning"
#define AARCH64_CONDBR_TUNING_NAME "AArch64 Conditional Branch Tuning"

namespace {
class AArch64CondBrTuning : public MachineFunctionPass {
  const AArch64InstrInfo *TII;
  const TargetRegisterInfo *TRI;

  MachineRegisterInfo *MRI;

public:
  static char ID;
  AArch64CondBrTuning() : MachineFunctionPass(ID) {
    initializeAArch64CondBrTuningPass(*PassRegistry::getPassRegistry());
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return AARCH64_CONDBR_TUNING_NAME; }

private:
  MachineInstr *getOperandDef(const MachineOperand &MO);
  MachineInstr *convertToFlagSetting(MachineInstr &MI, bool IsFlagSetting);
  MachineInstr *convertToCondBr(MachineInstr &MI);
  bool tryToTuneBranch(MachineInstr &MI, MachineInstr &DefMI);
};
} // end anonymous namespace

char AArch64CondBrTuning::ID = 0;

INITIALIZE_PASS(AArch64CondBrTuning, "aarch64-cond-br-tuning",
                AARCH64_CONDBR_TUNING_NAME, false, false)

void AArch64CondBrTuning::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  MachineFunctionPass::getAnalysisUsage(AU);
}

MachineInstr *AArch64CondBrTuning::getOperandDef(const MachineOperand &MO) {
  if (!TargetRegisterInfo::isVirtualRegister(MO.getReg()))
    return nullptr;
  return MRI->getUniqueVRegDef(MO.getReg());
}

MachineInstr *AArch64CondBrTuning::convertToFlagSetting(MachineInstr &MI,
                                                        bool IsFlagSetting) {
  // If this is already the flag setting version of the instruction (e.g., SUBS)
  // just make sure the implicit-def of NZCV isn't marked dead.
  if (IsFlagSetting) {
    for (unsigned I = MI.getNumExplicitOperands(), E = MI.getNumOperands();
         I != E; ++I) {
      MachineOperand &MO = MI.getOperand(I);
      if (MO.isReg() && MO.isDead() && MO.getReg() == AArch64::NZCV)
        MO.setIsDead(false);
    }
    return &MI;
  }
  bool Is64Bit;
  unsigned NewOpc = TII->convertToFlagSettingOpc(MI.getOpcode(), Is64Bit);
  unsigned NewDestReg = MI.getOperand(0).getReg();
  if (MRI->hasOneNonDBGUse(MI.getOperand(0).getReg()))
    NewDestReg = Is64Bit ? AArch64::XZR : AArch64::WZR;

  MachineInstrBuilder MIB = BuildMI(*MI.getParent(), MI, MI.getDebugLoc(),
                                    TII->get(NewOpc), NewDestReg);
  for (unsigned I = 1, E = MI.getNumOperands(); I != E; ++I)
    MIB.add(MI.getOperand(I));

  return MIB;
}

MachineInstr *AArch64CondBrTuning::convertToCondBr(MachineInstr &MI) {
  AArch64CC::CondCode CC;
  MachineBasicBlock *TargetMBB = TII->getBranchDestBlock(MI);
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("Unexpected opcode!");

  case AArch64::CBZW:
  case AArch64::CBZX:
    CC = AArch64CC::EQ;
    break;
  case AArch64::CBNZW:
  case AArch64::CBNZX:
    CC = AArch64CC::NE;
    break;
  case AArch64::TBZW:
  case AArch64::TBZX:
    CC = AArch64CC::PL;
    break;
  case AArch64::TBNZW:
  case AArch64::TBNZX:
    CC = AArch64CC::MI;
    break;
  }
  return BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), TII->get(AArch64::Bcc))
      .addImm(CC)
      .addMBB(TargetMBB);
}

bool AArch64CondBrTuning::tryToTuneBranch(MachineInstr &MI,
                                          MachineInstr &DefMI) {
  // We don't want NZCV bits live across blocks.
  if (MI.getParent() != DefMI.getParent())
    return false;

  bool IsFlagSetting = true;
  unsigned MIOpc = MI.getOpcode();
  MachineInstr *NewCmp = nullptr, *NewBr = nullptr;
  switch (DefMI.getOpcode()) {
  default:
    return false;
  case AArch64::ADDWri:
  case AArch64::ADDWrr:
  case AArch64::ADDWrs:
  case AArch64::ADDWrx:
  case AArch64::ANDWri:
  case AArch64::ANDWrr:
  case AArch64::ANDWrs:
  case AArch64::BICWrr:
  case AArch64::BICWrs:
  case AArch64::SUBWri:
  case AArch64::SUBWrr:
  case AArch64::SUBWrs:
  case AArch64::SUBWrx:
    IsFlagSetting = false;
    LLVM_FALLTHROUGH;
  case AArch64::ADDSWri:
  case AArch64::ADDSWrr:
  case AArch64::ADDSWrs:
  case AArch64::ADDSWrx:
  case AArch64::ANDSWri:
  case AArch64::ANDSWrr:
  case AArch64::ANDSWrs:
  case AArch64::BICSWrr:
  case AArch64::BICSWrs:
  case AArch64::SUBSWri:
  case AArch64::SUBSWrr:
  case AArch64::SUBSWrs:
  case AArch64::SUBSWrx:
    switch (MIOpc) {
    default:
      llvm_unreachable("Unexpected opcode!");

    case AArch64::CBZW:
    case AArch64::CBNZW:
    case AArch64::TBZW:
    case AArch64::TBNZW:
      // Check to see if the TBZ/TBNZ is checking the sign bit.
      if ((MIOpc == AArch64::TBZW || MIOpc == AArch64::TBNZW) &&
          MI.getOperand(1).getImm() != 31)
        return false;

      // There must not be any instruction between DefMI and MI that clobbers or
      // reads NZCV.
      MachineBasicBlock::iterator I(DefMI), E(MI);
      for (I = std::next(I); I != E; ++I) {
        if (I->modifiesRegister(AArch64::NZCV, TRI) ||
            I->readsRegister(AArch64::NZCV, TRI))
          return false;
      }
      LLVM_DEBUG(dbgs() << "  Replacing instructions:\n    ");
      LLVM_DEBUG(DefMI.print(dbgs()));
      LLVM_DEBUG(dbgs() << "    ");
      LLVM_DEBUG(MI.print(dbgs()));

      NewCmp = convertToFlagSetting(DefMI, IsFlagSetting);
      NewBr = convertToCondBr(MI);
      break;
    }
    break;

  case AArch64::ADDXri:
  case AArch64::ADDXrr:
  case AArch64::ADDXrs:
  case AArch64::ADDXrx:
  case AArch64::ANDXri:
  case AArch64::ANDXrr:
  case AArch64::ANDXrs:
  case AArch64::BICXrr:
  case AArch64::BICXrs:
  case AArch64::SUBXri:
  case AArch64::SUBXrr:
  case AArch64::SUBXrs:
  case AArch64::SUBXrx:
    IsFlagSetting = false;
    LLVM_FALLTHROUGH;
  case AArch64::ADDSXri:
  case AArch64::ADDSXrr:
  case AArch64::ADDSXrs:
  case AArch64::ADDSXrx:
  case AArch64::ANDSXri:
  case AArch64::ANDSXrr:
  case AArch64::ANDSXrs:
  case AArch64::BICSXrr:
  case AArch64::BICSXrs:
  case AArch64::SUBSXri:
  case AArch64::SUBSXrr:
  case AArch64::SUBSXrs:
  case AArch64::SUBSXrx:
    switch (MIOpc) {
    default:
      llvm_unreachable("Unexpected opcode!");

    case AArch64::CBZX:
    case AArch64::CBNZX:
    case AArch64::TBZX:
    case AArch64::TBNZX: {
      // Check to see if the TBZ/TBNZ is checking the sign bit.
      if ((MIOpc == AArch64::TBZX || MIOpc == AArch64::TBNZX) &&
          MI.getOperand(1).getImm() != 63)
        return false;
      // There must not be any instruction between DefMI and MI that clobbers or
      // reads NZCV.
      MachineBasicBlock::iterator I(DefMI), E(MI);
      for (I = std::next(I); I != E; ++I) {
        if (I->modifiesRegister(AArch64::NZCV, TRI) ||
            I->readsRegister(AArch64::NZCV, TRI))
          return false;
      }
      LLVM_DEBUG(dbgs() << "  Replacing instructions:\n    ");
      LLVM_DEBUG(DefMI.print(dbgs()));
      LLVM_DEBUG(dbgs() << "    ");
      LLVM_DEBUG(MI.print(dbgs()));

      NewCmp = convertToFlagSetting(DefMI, IsFlagSetting);
      NewBr = convertToCondBr(MI);
      break;
    }
    }
    break;
  }
  (void)NewCmp; (void)NewBr;
  assert(NewCmp && NewBr && "Expected new instructions.");

  LLVM_DEBUG(dbgs() << "  with instruction:\n    ");
  LLVM_DEBUG(NewCmp->print(dbgs()));
  LLVM_DEBUG(dbgs() << "    ");
  LLVM_DEBUG(NewBr->print(dbgs()));

  // If this was a flag setting version of the instruction, we use the original
  // instruction by just clearing the dead marked on the implicit-def of NCZV.
  // Therefore, we should not erase this instruction.
  if (!IsFlagSetting)
    DefMI.eraseFromParent();
  MI.eraseFromParent();
  return true;
}

bool AArch64CondBrTuning::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  LLVM_DEBUG(
      dbgs() << "********** AArch64 Conditional Branch Tuning  **********\n"
             << "********** Function: " << MF.getName() << '\n');

  TII = static_cast<const AArch64InstrInfo *>(MF.getSubtarget().getInstrInfo());
  TRI = MF.getSubtarget().getRegisterInfo();
  MRI = &MF.getRegInfo();

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    bool LocalChange = false;
    for (MachineBasicBlock::iterator I = MBB.getFirstTerminator(),
                                     E = MBB.end();
         I != E; ++I) {
      MachineInstr &MI = *I;
      switch (MI.getOpcode()) {
      default:
        break;
      case AArch64::CBZW:
      case AArch64::CBZX:
      case AArch64::CBNZW:
      case AArch64::CBNZX:
      case AArch64::TBZW:
      case AArch64::TBZX:
      case AArch64::TBNZW:
      case AArch64::TBNZX:
        MachineInstr *DefMI = getOperandDef(MI.getOperand(0));
        LocalChange = (DefMI && tryToTuneBranch(MI, *DefMI));
        break;
      }
      // If the optimization was successful, we can't optimize any other
      // branches because doing so would clobber the NZCV flags.
      if (LocalChange) {
        Changed = true;
        break;
      }
    }
  }
  return Changed;
}

FunctionPass *llvm::createAArch64CondBrTuning() {
  return new AArch64CondBrTuning();
}
