//===-- AVRRelaxMemOperations.cpp - Relax out of range loads/stores -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass which relaxes out of range memory operations into
// equivalent operations which handle bigger addresses.
//
//===----------------------------------------------------------------------===//

#include "AVR.h"
#include "AVRInstrInfo.h"
#include "AVRTargetMachine.h"
#include "MCTargetDesc/AVRMCTargetDesc.h"

#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

using namespace llvm;

#define AVR_RELAX_MEM_OPS_NAME "AVR memory operation relaxation pass"

namespace {

class AVRRelaxMem : public MachineFunctionPass {
public:
  static char ID;

  AVRRelaxMem() : MachineFunctionPass(ID) {
    initializeAVRRelaxMemPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return AVR_RELAX_MEM_OPS_NAME; }

private:
  typedef MachineBasicBlock Block;
  typedef Block::iterator BlockIt;

  const TargetInstrInfo *TII;

  template <unsigned OP> bool relax(Block &MBB, BlockIt MBBI);

  bool runOnBasicBlock(Block &MBB);
  bool runOnInstruction(Block &MBB, BlockIt MBBI);

  MachineInstrBuilder buildMI(Block &MBB, BlockIt MBBI, unsigned Opcode) {
    return BuildMI(MBB, MBBI, MBBI->getDebugLoc(), TII->get(Opcode));
  }
};

char AVRRelaxMem::ID = 0;

bool AVRRelaxMem::runOnMachineFunction(MachineFunction &MF) {
  bool Modified = false;

  const AVRSubtarget &STI = MF.getSubtarget<AVRSubtarget>();
  TII = STI.getInstrInfo();

  for (Block &MBB : MF) {
    bool BlockModified = runOnBasicBlock(MBB);
    Modified |= BlockModified;
  }

  return Modified;
}

bool AVRRelaxMem::runOnBasicBlock(Block &MBB) {
  bool Modified = false;

  BlockIt MBBI = MBB.begin(), E = MBB.end();
  while (MBBI != E) {
    BlockIt NMBBI = std::next(MBBI);
    Modified |= runOnInstruction(MBB, MBBI);
    MBBI = NMBBI;
  }

  return Modified;
}

template <>
bool AVRRelaxMem::relax<AVR::STDWPtrQRr>(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;

  MachineOperand &Ptr = MI.getOperand(0);
  MachineOperand &Src = MI.getOperand(2);
  int64_t Imm = MI.getOperand(1).getImm();

  // We can definitely optimise this better.
  if (Imm > 63) {
    // Push the previous state of the pointer register.
    // This instruction must preserve the value.
    buildMI(MBB, MBBI, AVR::PUSHWRr)
      .addReg(Ptr.getReg());

    // Add the immediate to the pointer register.
    buildMI(MBB, MBBI, AVR::SBCIWRdK)
      .addReg(Ptr.getReg(), RegState::Define)
      .addReg(Ptr.getReg())
      .addImm(-Imm);

    // Store the value in the source register to the address
    // pointed to by the pointer register.
    buildMI(MBB, MBBI, AVR::STWPtrRr)
      .addReg(Ptr.getReg())
      .addReg(Src.getReg(), getKillRegState(Src.isKill()));

    // Pop the original state of the pointer register.
    buildMI(MBB, MBBI, AVR::POPWRd)
      .addReg(Ptr.getReg(), getKillRegState(Ptr.isKill()));

    MI.removeFromParent();
  }

  return false;
}

bool AVRRelaxMem::runOnInstruction(Block &MBB, BlockIt MBBI) {
  MachineInstr &MI = *MBBI;
  int Opcode = MBBI->getOpcode();

#define RELAX(Op)                \
  case Op:                       \
    return relax<Op>(MBB, MI)

  switch (Opcode) {
    RELAX(AVR::STDWPtrQRr);
  }
#undef RELAX
  return false;
}

} // end of anonymous namespace

INITIALIZE_PASS(AVRRelaxMem, "avr-relax-mem",
                AVR_RELAX_MEM_OPS_NAME, false, false)

namespace llvm {

FunctionPass *createAVRRelaxMemPass() { return new AVRRelaxMem(); }

} // end of namespace llvm
