//===-- SystemZShortenInst.cpp - Instruction-shortening pass --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass tries to replace instructions with shorter forms.  For example,
// IILF can be replaced with LLILL or LLILH if the constant fits and if the
// other 32 bits of the GR64 destination are not live.
//
//===----------------------------------------------------------------------===//

#include "SystemZTargetMachine.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

using namespace llvm;

#define DEBUG_TYPE "systemz-shorten-inst"

namespace {
class SystemZShortenInst : public MachineFunctionPass {
public:
  static char ID;
  SystemZShortenInst(const SystemZTargetMachine &tm);

  StringRef getPassName() const override {
    return "SystemZ Instruction Shortening";
  }

  bool processBlock(MachineBasicBlock &MBB);
  bool runOnMachineFunction(MachineFunction &F) override;
  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

private:
  bool shortenIIF(MachineInstr &MI, unsigned LLIxL, unsigned LLIxH);
  bool shortenOn0(MachineInstr &MI, unsigned Opcode);
  bool shortenOn01(MachineInstr &MI, unsigned Opcode);
  bool shortenOn001(MachineInstr &MI, unsigned Opcode);
  bool shortenOn001AddCC(MachineInstr &MI, unsigned Opcode);
  bool shortenFPConv(MachineInstr &MI, unsigned Opcode);

  const SystemZInstrInfo *TII;
  const TargetRegisterInfo *TRI;
  LivePhysRegs LiveRegs;
};

char SystemZShortenInst::ID = 0;
} // end anonymous namespace

FunctionPass *llvm::createSystemZShortenInstPass(SystemZTargetMachine &TM) {
  return new SystemZShortenInst(TM);
}

SystemZShortenInst::SystemZShortenInst(const SystemZTargetMachine &tm)
  : MachineFunctionPass(ID), TII(nullptr) {}

// Tie operands if MI has become a two-address instruction.
static void tieOpsIfNeeded(MachineInstr &MI) {
  if (MI.getDesc().getOperandConstraint(0, MCOI::TIED_TO) &&
      !MI.getOperand(0).isTied())
    MI.tieOperands(0, 1);
}

// MI loads one word of a GPR using an IIxF instruction and LLIxL and LLIxH
// are the halfword immediate loads for the same word.  Try to use one of them
// instead of IIxF.
bool SystemZShortenInst::shortenIIF(MachineInstr &MI, unsigned LLIxL,
                                    unsigned LLIxH) {
  unsigned Reg = MI.getOperand(0).getReg();
  // The new opcode will clear the other half of the GR64 reg, so
  // cancel if that is live.
  unsigned thisSubRegIdx =
      (SystemZ::GRH32BitRegClass.contains(Reg) ? SystemZ::subreg_h32
                                               : SystemZ::subreg_l32);
  unsigned otherSubRegIdx =
      (thisSubRegIdx == SystemZ::subreg_l32 ? SystemZ::subreg_h32
                                            : SystemZ::subreg_l32);
  unsigned GR64BitReg =
      TRI->getMatchingSuperReg(Reg, thisSubRegIdx, &SystemZ::GR64BitRegClass);
  unsigned OtherReg = TRI->getSubReg(GR64BitReg, otherSubRegIdx);
  if (LiveRegs.contains(OtherReg))
    return false;

  uint64_t Imm = MI.getOperand(1).getImm();
  if (SystemZ::isImmLL(Imm)) {
    MI.setDesc(TII->get(LLIxL));
    MI.getOperand(0).setReg(SystemZMC::getRegAsGR64(Reg));
    return true;
  }
  if (SystemZ::isImmLH(Imm)) {
    MI.setDesc(TII->get(LLIxH));
    MI.getOperand(0).setReg(SystemZMC::getRegAsGR64(Reg));
    MI.getOperand(1).setImm(Imm >> 16);
    return true;
  }
  return false;
}

// Change MI's opcode to Opcode if register operand 0 has a 4-bit encoding.
bool SystemZShortenInst::shortenOn0(MachineInstr &MI, unsigned Opcode) {
  if (SystemZMC::getFirstReg(MI.getOperand(0).getReg()) < 16) {
    MI.setDesc(TII->get(Opcode));
    return true;
  }
  return false;
}

// Change MI's opcode to Opcode if register operands 0 and 1 have a
// 4-bit encoding.
bool SystemZShortenInst::shortenOn01(MachineInstr &MI, unsigned Opcode) {
  if (SystemZMC::getFirstReg(MI.getOperand(0).getReg()) < 16 &&
      SystemZMC::getFirstReg(MI.getOperand(1).getReg()) < 16) {
    MI.setDesc(TII->get(Opcode));
    return true;
  }
  return false;
}

// Change MI's opcode to Opcode if register operands 0, 1 and 2 have a
// 4-bit encoding and if operands 0 and 1 are tied. Also ties op 0
// with op 1, if MI becomes 2-address.
bool SystemZShortenInst::shortenOn001(MachineInstr &MI, unsigned Opcode) {
  if (SystemZMC::getFirstReg(MI.getOperand(0).getReg()) < 16 &&
      MI.getOperand(1).getReg() == MI.getOperand(0).getReg() &&
      SystemZMC::getFirstReg(MI.getOperand(2).getReg()) < 16) {
    MI.setDesc(TII->get(Opcode));
    tieOpsIfNeeded(MI);
    return true;
  }
  return false;
}

// Calls shortenOn001 if CCLive is false. CC def operand is added in
// case of success.
bool SystemZShortenInst::shortenOn001AddCC(MachineInstr &MI, unsigned Opcode) {
  if (!LiveRegs.contains(SystemZ::CC) && shortenOn001(MI, Opcode)) {
    MachineInstrBuilder(*MI.getParent()->getParent(), &MI)
      .addReg(SystemZ::CC, RegState::ImplicitDefine | RegState::Dead);
    return true;
  }
  return false;
}

// MI is a vector-style conversion instruction with the operand order:
// destination, source, exact-suppress, rounding-mode.  If both registers
// have a 4-bit encoding then change it to Opcode, which has operand order:
// destination, rouding-mode, source, exact-suppress.
bool SystemZShortenInst::shortenFPConv(MachineInstr &MI, unsigned Opcode) {
  if (SystemZMC::getFirstReg(MI.getOperand(0).getReg()) < 16 &&
      SystemZMC::getFirstReg(MI.getOperand(1).getReg()) < 16) {
    MachineOperand Dest(MI.getOperand(0));
    MachineOperand Src(MI.getOperand(1));
    MachineOperand Suppress(MI.getOperand(2));
    MachineOperand Mode(MI.getOperand(3));
    MI.RemoveOperand(3);
    MI.RemoveOperand(2);
    MI.RemoveOperand(1);
    MI.RemoveOperand(0);
    MI.setDesc(TII->get(Opcode));
    MachineInstrBuilder(*MI.getParent()->getParent(), &MI)
        .add(Dest)
        .add(Mode)
        .add(Src)
        .add(Suppress);
    return true;
  }
  return false;
}

// Process all instructions in MBB.  Return true if something changed.
bool SystemZShortenInst::processBlock(MachineBasicBlock &MBB) {
  bool Changed = false;

  // Set up the set of live registers at the end of MBB (live out)
  LiveRegs.clear();
  LiveRegs.addLiveOuts(MBB);

  // Iterate backwards through the block looking for instructions to change.
  for (auto MBBI = MBB.rbegin(), MBBE = MBB.rend(); MBBI != MBBE; ++MBBI) {
    MachineInstr &MI = *MBBI;
    switch (MI.getOpcode()) {
    case SystemZ::IILF:
      Changed |= shortenIIF(MI, SystemZ::LLILL, SystemZ::LLILH);
      break;

    case SystemZ::IIHF:
      Changed |= shortenIIF(MI, SystemZ::LLIHL, SystemZ::LLIHH);
      break;

    case SystemZ::WFADB:
      Changed |= shortenOn001AddCC(MI, SystemZ::ADBR);
      break;

    case SystemZ::WFASB:
      Changed |= shortenOn001AddCC(MI, SystemZ::AEBR);
      break;

    case SystemZ::WFDDB:
      Changed |= shortenOn001(MI, SystemZ::DDBR);
      break;

    case SystemZ::WFDSB:
      Changed |= shortenOn001(MI, SystemZ::DEBR);
      break;

    case SystemZ::WFIDB:
      Changed |= shortenFPConv(MI, SystemZ::FIDBRA);
      break;

    case SystemZ::WFISB:
      Changed |= shortenFPConv(MI, SystemZ::FIEBRA);
      break;

    case SystemZ::WLDEB:
      Changed |= shortenOn01(MI, SystemZ::LDEBR);
      break;

    case SystemZ::WLEDB:
      Changed |= shortenFPConv(MI, SystemZ::LEDBRA);
      break;

    case SystemZ::WFMDB:
      Changed |= shortenOn001(MI, SystemZ::MDBR);
      break;

    case SystemZ::WFMSB:
      Changed |= shortenOn001(MI, SystemZ::MEEBR);
      break;

    case SystemZ::WFLCDB:
      Changed |= shortenOn01(MI, SystemZ::LCDFR);
      break;

    case SystemZ::WFLCSB:
      Changed |= shortenOn01(MI, SystemZ::LCDFR_32);
      break;

    case SystemZ::WFLNDB:
      Changed |= shortenOn01(MI, SystemZ::LNDFR);
      break;

    case SystemZ::WFLNSB:
      Changed |= shortenOn01(MI, SystemZ::LNDFR_32);
      break;

    case SystemZ::WFLPDB:
      Changed |= shortenOn01(MI, SystemZ::LPDFR);
      break;

    case SystemZ::WFLPSB:
      Changed |= shortenOn01(MI, SystemZ::LPDFR_32);
      break;

    case SystemZ::WFSQDB:
      Changed |= shortenOn01(MI, SystemZ::SQDBR);
      break;

    case SystemZ::WFSQSB:
      Changed |= shortenOn01(MI, SystemZ::SQEBR);
      break;

    case SystemZ::WFSDB:
      Changed |= shortenOn001AddCC(MI, SystemZ::SDBR);
      break;

    case SystemZ::WFSSB:
      Changed |= shortenOn001AddCC(MI, SystemZ::SEBR);
      break;

    case SystemZ::WFCDB:
      Changed |= shortenOn01(MI, SystemZ::CDBR);
      break;

    case SystemZ::WFCSB:
      Changed |= shortenOn01(MI, SystemZ::CEBR);
      break;

    case SystemZ::VL32:
      // For z13 we prefer LDE over LE to avoid partial register dependencies.
      Changed |= shortenOn0(MI, SystemZ::LDE32);
      break;

    case SystemZ::VST32:
      Changed |= shortenOn0(MI, SystemZ::STE);
      break;

    case SystemZ::VL64:
      Changed |= shortenOn0(MI, SystemZ::LD);
      break;

    case SystemZ::VST64:
      Changed |= shortenOn0(MI, SystemZ::STD);
      break;
    }

    LiveRegs.stepBackward(MI);
  }

  return Changed;
}

bool SystemZShortenInst::runOnMachineFunction(MachineFunction &F) {
  if (skipFunction(F.getFunction()))
    return false;

  const SystemZSubtarget &ST = F.getSubtarget<SystemZSubtarget>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
  LiveRegs.init(*TRI);

  bool Changed = false;
  for (auto &MBB : F)
    Changed |= processBlock(MBB);

  return Changed;
}
