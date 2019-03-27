//===-- MipsExpandPseudoInsts.cpp - Expand pseudo instructions ------------===//
//
// The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that expands pseudo instructions into target
// instructions to allow proper scheduling, if-conversion, and other late
// optimizations. This pass should be run after register allocation but before
// the post-regalloc scheduling pass.
//
// This is currently only used for expanding atomic pseudos after register
// allocation. We do this to avoid the fast register allocator introducing
// spills between ll and sc. These stores cause some MIPS implementations to
// abort the atomic RMW sequence.
//
//===----------------------------------------------------------------------===//

#include "Mips.h"
#include "MipsInstrInfo.h"
#include "MipsSubtarget.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

using namespace llvm;

#define DEBUG_TYPE "mips-pseudo"

namespace {
  class MipsExpandPseudo : public MachineFunctionPass {
  public:
    static char ID;
    MipsExpandPseudo() : MachineFunctionPass(ID) {}

    const MipsInstrInfo *TII;
    const MipsSubtarget *STI;

    bool runOnMachineFunction(MachineFunction &Fn) override;

    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }

    StringRef getPassName() const override {
      return "Mips pseudo instruction expansion pass";
    }

  private:
    bool expandAtomicCmpSwap(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator MBBI,
                             MachineBasicBlock::iterator &NextMBBI);
    bool expandAtomicCmpSwapSubword(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator MBBI,
                                    MachineBasicBlock::iterator &NextMBBI);

    bool expandAtomicBinOp(MachineBasicBlock &BB,
                           MachineBasicBlock::iterator I,
                           MachineBasicBlock::iterator &NMBBI, unsigned Size);
    bool expandAtomicBinOpSubword(MachineBasicBlock &BB,
                                  MachineBasicBlock::iterator I,
                                  MachineBasicBlock::iterator &NMBBI);

    bool expandMI(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                  MachineBasicBlock::iterator &NMBB);
    bool expandMBB(MachineBasicBlock &MBB);
   };
  char MipsExpandPseudo::ID = 0;
}

bool MipsExpandPseudo::expandAtomicCmpSwapSubword(
    MachineBasicBlock &BB, MachineBasicBlock::iterator I,
    MachineBasicBlock::iterator &NMBBI) {

  MachineFunction *MF = BB.getParent();

  const bool ArePtrs64bit = STI->getABI().ArePtrs64bit();
  DebugLoc DL = I->getDebugLoc();
  unsigned LL, SC;

  unsigned ZERO = Mips::ZERO;
  unsigned BNE = Mips::BNE;
  unsigned BEQ = Mips::BEQ;
  unsigned SEOp =
      I->getOpcode() == Mips::ATOMIC_CMP_SWAP_I8_POSTRA ? Mips::SEB : Mips::SEH;

  if (STI->inMicroMipsMode()) {
      LL = STI->hasMips32r6() ? Mips::LL_MMR6 : Mips::LL_MM;
      SC = STI->hasMips32r6() ? Mips::SC_MMR6 : Mips::SC_MM;
      BNE = STI->hasMips32r6() ? Mips::BNEC_MMR6 : Mips::BNE_MM;
      BEQ = STI->hasMips32r6() ? Mips::BEQC_MMR6 : Mips::BEQ_MM;
  } else {
    LL = STI->hasMips32r6() ? (ArePtrs64bit ? Mips::LL64_R6 : Mips::LL_R6)
                            : (ArePtrs64bit ? Mips::LL64 : Mips::LL);
    SC = STI->hasMips32r6() ? (ArePtrs64bit ? Mips::SC64_R6 : Mips::SC_R6)
                            : (ArePtrs64bit ? Mips::SC64 : Mips::SC);
  }

  unsigned Dest = I->getOperand(0).getReg();
  unsigned Ptr = I->getOperand(1).getReg();
  unsigned Mask = I->getOperand(2).getReg();
  unsigned ShiftCmpVal = I->getOperand(3).getReg();
  unsigned Mask2 = I->getOperand(4).getReg();
  unsigned ShiftNewVal = I->getOperand(5).getReg();
  unsigned ShiftAmnt = I->getOperand(6).getReg();
  unsigned Scratch = I->getOperand(7).getReg();
  unsigned Scratch2 = I->getOperand(8).getReg();

  // insert new blocks after the current block
  const BasicBlock *LLVM_BB = BB.getBasicBlock();
  MachineBasicBlock *loop1MBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *loop2MBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *sinkMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *exitMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineFunction::iterator It = ++BB.getIterator();
  MF->insert(It, loop1MBB);
  MF->insert(It, loop2MBB);
  MF->insert(It, sinkMBB);
  MF->insert(It, exitMBB);

  // Transfer the remainder of BB and its successor edges to exitMBB.
  exitMBB->splice(exitMBB->begin(), &BB,
                  std::next(MachineBasicBlock::iterator(I)), BB.end());
  exitMBB->transferSuccessorsAndUpdatePHIs(&BB);

  //  thisMBB:
  //    ...
  //    fallthrough --> loop1MBB
  BB.addSuccessor(loop1MBB, BranchProbability::getOne());
  loop1MBB->addSuccessor(sinkMBB);
  loop1MBB->addSuccessor(loop2MBB);
  loop1MBB->normalizeSuccProbs();
  loop2MBB->addSuccessor(loop1MBB);
  loop2MBB->addSuccessor(sinkMBB);
  loop2MBB->normalizeSuccProbs();
  sinkMBB->addSuccessor(exitMBB, BranchProbability::getOne());

  // loop1MBB:
  //   ll dest, 0(ptr)
  //   and Mask', dest, Mask
  //   bne Mask', ShiftCmpVal, exitMBB
  BuildMI(loop1MBB, DL, TII->get(LL), Scratch).addReg(Ptr).addImm(0);
  BuildMI(loop1MBB, DL, TII->get(Mips::AND), Scratch2)
      .addReg(Scratch)
      .addReg(Mask);
  BuildMI(loop1MBB, DL, TII->get(BNE))
    .addReg(Scratch2).addReg(ShiftCmpVal).addMBB(sinkMBB);

  // loop2MBB:
  //   and dest, dest, mask2
  //   or dest, dest, ShiftNewVal
  //   sc dest, dest, 0(ptr)
  //   beq dest, $0, loop1MBB
  BuildMI(loop2MBB, DL, TII->get(Mips::AND), Scratch)
      .addReg(Scratch, RegState::Kill)
      .addReg(Mask2);
  BuildMI(loop2MBB, DL, TII->get(Mips::OR), Scratch)
      .addReg(Scratch, RegState::Kill)
      .addReg(ShiftNewVal);
  BuildMI(loop2MBB, DL, TII->get(SC), Scratch)
      .addReg(Scratch, RegState::Kill)
      .addReg(Ptr)
      .addImm(0);
  BuildMI(loop2MBB, DL, TII->get(BEQ))
      .addReg(Scratch, RegState::Kill)
      .addReg(ZERO)
      .addMBB(loop1MBB);

  //  sinkMBB:
  //    srl     srlres, Mask', shiftamt
  //    sign_extend dest,srlres
  BuildMI(sinkMBB, DL, TII->get(Mips::SRLV), Dest)
      .addReg(Scratch2)
      .addReg(ShiftAmnt);
  if (STI->hasMips32r2()) {
    BuildMI(sinkMBB, DL, TII->get(SEOp), Dest).addReg(Dest);
  } else {
    const unsigned ShiftImm =
        I->getOpcode() == Mips::ATOMIC_CMP_SWAP_I16_POSTRA ? 16 : 24;
    BuildMI(sinkMBB, DL, TII->get(Mips::SLL), Dest)
        .addReg(Dest, RegState::Kill)
        .addImm(ShiftImm);
    BuildMI(sinkMBB, DL, TII->get(Mips::SRA), Dest)
        .addReg(Dest, RegState::Kill)
        .addImm(ShiftImm);
  }

  LivePhysRegs LiveRegs;
  computeAndAddLiveIns(LiveRegs, *loop1MBB);
  computeAndAddLiveIns(LiveRegs, *loop2MBB);
  computeAndAddLiveIns(LiveRegs, *sinkMBB);
  computeAndAddLiveIns(LiveRegs, *exitMBB);

  NMBBI = BB.end();
  I->eraseFromParent();
  return true;
}

bool MipsExpandPseudo::expandAtomicCmpSwap(MachineBasicBlock &BB,
                                           MachineBasicBlock::iterator I,
                                           MachineBasicBlock::iterator &NMBBI) {

  const unsigned Size =
      I->getOpcode() == Mips::ATOMIC_CMP_SWAP_I32_POSTRA ? 4 : 8;
  MachineFunction *MF = BB.getParent();

  const bool ArePtrs64bit = STI->getABI().ArePtrs64bit();
  DebugLoc DL = I->getDebugLoc();

  unsigned LL, SC, ZERO, BNE, BEQ, MOVE;

  if (Size == 4) {
    if (STI->inMicroMipsMode()) {
      LL = STI->hasMips32r6() ? Mips::LL_MMR6 : Mips::LL_MM;
      SC = STI->hasMips32r6() ? Mips::SC_MMR6 : Mips::SC_MM;
      BNE = STI->hasMips32r6() ? Mips::BNEC_MMR6 : Mips::BNE_MM;
      BEQ = STI->hasMips32r6() ? Mips::BEQC_MMR6 : Mips::BEQ_MM;
    } else {
      LL = STI->hasMips32r6()
               ? (ArePtrs64bit ? Mips::LL64_R6 : Mips::LL_R6)
               : (ArePtrs64bit ? Mips::LL64 : Mips::LL);
      SC = STI->hasMips32r6()
               ? (ArePtrs64bit ? Mips::SC64_R6 : Mips::SC_R6)
               : (ArePtrs64bit ? Mips::SC64 : Mips::SC);
      BNE = Mips::BNE;
      BEQ = Mips::BEQ;
    }

    ZERO = Mips::ZERO;
    MOVE = Mips::OR;
  } else {
    LL = STI->hasMips64r6() ? Mips::LLD_R6 : Mips::LLD;
    SC = STI->hasMips64r6() ? Mips::SCD_R6 : Mips::SCD;
    ZERO = Mips::ZERO_64;
    BNE = Mips::BNE64;
    BEQ = Mips::BEQ64;
    MOVE = Mips::OR64;
  }

  unsigned Dest = I->getOperand(0).getReg();
  unsigned Ptr = I->getOperand(1).getReg();
  unsigned OldVal = I->getOperand(2).getReg();
  unsigned NewVal = I->getOperand(3).getReg();
  unsigned Scratch = I->getOperand(4).getReg();

  // insert new blocks after the current block
  const BasicBlock *LLVM_BB = BB.getBasicBlock();
  MachineBasicBlock *loop1MBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *loop2MBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *exitMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineFunction::iterator It = ++BB.getIterator();
  MF->insert(It, loop1MBB);
  MF->insert(It, loop2MBB);
  MF->insert(It, exitMBB);

  // Transfer the remainder of BB and its successor edges to exitMBB.
  exitMBB->splice(exitMBB->begin(), &BB,
                  std::next(MachineBasicBlock::iterator(I)), BB.end());
  exitMBB->transferSuccessorsAndUpdatePHIs(&BB);

  //  thisMBB:
  //    ...
  //    fallthrough --> loop1MBB
  BB.addSuccessor(loop1MBB, BranchProbability::getOne());
  loop1MBB->addSuccessor(exitMBB);
  loop1MBB->addSuccessor(loop2MBB);
  loop1MBB->normalizeSuccProbs();
  loop2MBB->addSuccessor(loop1MBB);
  loop2MBB->addSuccessor(exitMBB);
  loop2MBB->normalizeSuccProbs();

  // loop1MBB:
  //   ll dest, 0(ptr)
  //   bne dest, oldval, exitMBB
  BuildMI(loop1MBB, DL, TII->get(LL), Dest).addReg(Ptr).addImm(0);
  BuildMI(loop1MBB, DL, TII->get(BNE))
    .addReg(Dest, RegState::Kill).addReg(OldVal).addMBB(exitMBB);

  // loop2MBB:
  //   move scratch, NewVal
  //   sc Scratch, Scratch, 0(ptr)
  //   beq Scratch, $0, loop1MBB
  BuildMI(loop2MBB, DL, TII->get(MOVE), Scratch).addReg(NewVal).addReg(ZERO);
  BuildMI(loop2MBB, DL, TII->get(SC), Scratch)
    .addReg(Scratch).addReg(Ptr).addImm(0);
  BuildMI(loop2MBB, DL, TII->get(BEQ))
    .addReg(Scratch, RegState::Kill).addReg(ZERO).addMBB(loop1MBB);

  LivePhysRegs LiveRegs;
  computeAndAddLiveIns(LiveRegs, *loop1MBB);
  computeAndAddLiveIns(LiveRegs, *loop2MBB);
  computeAndAddLiveIns(LiveRegs, *exitMBB);

  NMBBI = BB.end();
  I->eraseFromParent();
  return true;
}

bool MipsExpandPseudo::expandAtomicBinOpSubword(
    MachineBasicBlock &BB, MachineBasicBlock::iterator I,
    MachineBasicBlock::iterator &NMBBI) {

  MachineFunction *MF = BB.getParent();

  const bool ArePtrs64bit = STI->getABI().ArePtrs64bit();
  DebugLoc DL = I->getDebugLoc();

  unsigned LL, SC;
  unsigned BEQ = Mips::BEQ;
  unsigned SEOp = Mips::SEH;

  if (STI->inMicroMipsMode()) {
      LL = STI->hasMips32r6() ? Mips::LL_MMR6 : Mips::LL_MM;
      SC = STI->hasMips32r6() ? Mips::SC_MMR6 : Mips::SC_MM;
      BEQ = STI->hasMips32r6() ? Mips::BEQC_MMR6 : Mips::BEQ_MM;
  } else {
    LL = STI->hasMips32r6() ? (ArePtrs64bit ? Mips::LL64_R6 : Mips::LL_R6)
                            : (ArePtrs64bit ? Mips::LL64 : Mips::LL);
    SC = STI->hasMips32r6() ? (ArePtrs64bit ? Mips::SC64_R6 : Mips::SC_R6)
                            : (ArePtrs64bit ? Mips::SC64 : Mips::SC);
  }

  bool IsSwap = false;
  bool IsNand = false;

  unsigned Opcode = 0;
  switch (I->getOpcode()) {
  case Mips::ATOMIC_LOAD_NAND_I8_POSTRA:
    SEOp = Mips::SEB;
    LLVM_FALLTHROUGH;
  case Mips::ATOMIC_LOAD_NAND_I16_POSTRA:
    IsNand = true;
    break;
  case Mips::ATOMIC_SWAP_I8_POSTRA:
    SEOp = Mips::SEB;
    LLVM_FALLTHROUGH;
  case Mips::ATOMIC_SWAP_I16_POSTRA:
    IsSwap = true;
    break;
  case Mips::ATOMIC_LOAD_ADD_I8_POSTRA:
    SEOp = Mips::SEB;
    LLVM_FALLTHROUGH;
  case Mips::ATOMIC_LOAD_ADD_I16_POSTRA:
    Opcode = Mips::ADDu;
    break;
  case Mips::ATOMIC_LOAD_SUB_I8_POSTRA:
    SEOp = Mips::SEB;
    LLVM_FALLTHROUGH;
  case Mips::ATOMIC_LOAD_SUB_I16_POSTRA:
    Opcode = Mips::SUBu;
    break;
  case Mips::ATOMIC_LOAD_AND_I8_POSTRA:
    SEOp = Mips::SEB;
    LLVM_FALLTHROUGH;
  case Mips::ATOMIC_LOAD_AND_I16_POSTRA:
    Opcode = Mips::AND;
    break;
  case Mips::ATOMIC_LOAD_OR_I8_POSTRA:
    SEOp = Mips::SEB;
    LLVM_FALLTHROUGH;
  case Mips::ATOMIC_LOAD_OR_I16_POSTRA:
    Opcode = Mips::OR;
    break;
  case Mips::ATOMIC_LOAD_XOR_I8_POSTRA:
    SEOp = Mips::SEB;
    LLVM_FALLTHROUGH;
  case Mips::ATOMIC_LOAD_XOR_I16_POSTRA:
    Opcode = Mips::XOR;
    break;
  default:
    llvm_unreachable("Unknown subword atomic pseudo for expansion!");
  }

  unsigned Dest = I->getOperand(0).getReg();
  unsigned Ptr = I->getOperand(1).getReg();
  unsigned Incr = I->getOperand(2).getReg();
  unsigned Mask = I->getOperand(3).getReg();
  unsigned Mask2 = I->getOperand(4).getReg();
  unsigned ShiftAmnt = I->getOperand(5).getReg();
  unsigned OldVal = I->getOperand(6).getReg();
  unsigned BinOpRes = I->getOperand(7).getReg();
  unsigned StoreVal = I->getOperand(8).getReg();

  const BasicBlock *LLVM_BB = BB.getBasicBlock();
  MachineBasicBlock *loopMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *sinkMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *exitMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineFunction::iterator It = ++BB.getIterator();
  MF->insert(It, loopMBB);
  MF->insert(It, sinkMBB);
  MF->insert(It, exitMBB);

  exitMBB->splice(exitMBB->begin(), &BB, std::next(I), BB.end());
  exitMBB->transferSuccessorsAndUpdatePHIs(&BB);

  BB.addSuccessor(loopMBB, BranchProbability::getOne());
  loopMBB->addSuccessor(sinkMBB);
  loopMBB->addSuccessor(loopMBB);
  loopMBB->normalizeSuccProbs();

  BuildMI(loopMBB, DL, TII->get(LL), OldVal).addReg(Ptr).addImm(0);
  if (IsNand) {
    //  and andres, oldval, incr2
    //  nor binopres, $0, andres
    //  and newval, binopres, mask
    BuildMI(loopMBB, DL, TII->get(Mips::AND), BinOpRes)
        .addReg(OldVal)
        .addReg(Incr);
    BuildMI(loopMBB, DL, TII->get(Mips::NOR), BinOpRes)
        .addReg(Mips::ZERO)
        .addReg(BinOpRes);
    BuildMI(loopMBB, DL, TII->get(Mips::AND), BinOpRes)
        .addReg(BinOpRes)
        .addReg(Mask);
  } else if (!IsSwap) {
    //  <binop> binopres, oldval, incr2
    //  and newval, binopres, mask
    BuildMI(loopMBB, DL, TII->get(Opcode), BinOpRes)
        .addReg(OldVal)
        .addReg(Incr);
    BuildMI(loopMBB, DL, TII->get(Mips::AND), BinOpRes)
        .addReg(BinOpRes)
        .addReg(Mask);
  } else { // atomic.swap
    //  and newval, incr2, mask
    BuildMI(loopMBB, DL, TII->get(Mips::AND), BinOpRes)
        .addReg(Incr)
        .addReg(Mask);
  }

  // and StoreVal, OlddVal, Mask2
  // or StoreVal, StoreVal, BinOpRes
  // StoreVal<tied1> = sc StoreVal, 0(Ptr)
  // beq StoreVal, zero, loopMBB
  BuildMI(loopMBB, DL, TII->get(Mips::AND), StoreVal)
    .addReg(OldVal).addReg(Mask2);
  BuildMI(loopMBB, DL, TII->get(Mips::OR), StoreVal)
    .addReg(StoreVal).addReg(BinOpRes);
  BuildMI(loopMBB, DL, TII->get(SC), StoreVal)
    .addReg(StoreVal).addReg(Ptr).addImm(0);
  BuildMI(loopMBB, DL, TII->get(BEQ))
    .addReg(StoreVal).addReg(Mips::ZERO).addMBB(loopMBB);

  //  sinkMBB:
  //    and     maskedoldval1,oldval,mask
  //    srl     srlres,maskedoldval1,shiftamt
  //    sign_extend dest,srlres

  sinkMBB->addSuccessor(exitMBB, BranchProbability::getOne());

  BuildMI(sinkMBB, DL, TII->get(Mips::AND), Dest)
    .addReg(OldVal).addReg(Mask);
  BuildMI(sinkMBB, DL, TII->get(Mips::SRLV), Dest)
      .addReg(Dest).addReg(ShiftAmnt);

  if (STI->hasMips32r2()) {
    BuildMI(sinkMBB, DL, TII->get(SEOp), Dest).addReg(Dest);
  } else {
    const unsigned ShiftImm = SEOp == Mips::SEH ? 16 : 24;
    BuildMI(sinkMBB, DL, TII->get(Mips::SLL), Dest)
        .addReg(Dest, RegState::Kill)
        .addImm(ShiftImm);
    BuildMI(sinkMBB, DL, TII->get(Mips::SRA), Dest)
        .addReg(Dest, RegState::Kill)
        .addImm(ShiftImm);
  }

  LivePhysRegs LiveRegs;
  computeAndAddLiveIns(LiveRegs, *loopMBB);
  computeAndAddLiveIns(LiveRegs, *sinkMBB);
  computeAndAddLiveIns(LiveRegs, *exitMBB);

  NMBBI = BB.end();
  I->eraseFromParent();

  return true;
}

bool MipsExpandPseudo::expandAtomicBinOp(MachineBasicBlock &BB,
                                         MachineBasicBlock::iterator I,
                                         MachineBasicBlock::iterator &NMBBI,
                                         unsigned Size) {
  MachineFunction *MF = BB.getParent();

  const bool ArePtrs64bit = STI->getABI().ArePtrs64bit();
  DebugLoc DL = I->getDebugLoc();

  unsigned LL, SC, ZERO, BEQ;

  if (Size == 4) {
    if (STI->inMicroMipsMode()) {
      LL = STI->hasMips32r6() ? Mips::LL_MMR6 : Mips::LL_MM;
      SC = STI->hasMips32r6() ? Mips::SC_MMR6 : Mips::SC_MM;
      BEQ = STI->hasMips32r6() ? Mips::BEQC_MMR6 : Mips::BEQ_MM;
    } else {
      LL = STI->hasMips32r6()
               ? (ArePtrs64bit ? Mips::LL64_R6 : Mips::LL_R6)
               : (ArePtrs64bit ? Mips::LL64 : Mips::LL);
      SC = STI->hasMips32r6()
               ? (ArePtrs64bit ? Mips::SC64_R6 : Mips::SC_R6)
               : (ArePtrs64bit ? Mips::SC64 : Mips::SC);
      BEQ = Mips::BEQ;
    }

    ZERO = Mips::ZERO;
  } else {
    LL = STI->hasMips64r6() ? Mips::LLD_R6 : Mips::LLD;
    SC = STI->hasMips64r6() ? Mips::SCD_R6 : Mips::SCD;
    ZERO = Mips::ZERO_64;
    BEQ = Mips::BEQ64;
  }

  unsigned OldVal = I->getOperand(0).getReg();
  unsigned Ptr = I->getOperand(1).getReg();
  unsigned Incr = I->getOperand(2).getReg();
  unsigned Scratch = I->getOperand(3).getReg();

  unsigned Opcode = 0;
  unsigned OR = 0;
  unsigned AND = 0;
  unsigned NOR = 0;
  bool IsNand = false;
  switch (I->getOpcode()) {
  case Mips::ATOMIC_LOAD_ADD_I32_POSTRA:
    Opcode = Mips::ADDu;
    break;
  case Mips::ATOMIC_LOAD_SUB_I32_POSTRA:
    Opcode = Mips::SUBu;
    break;
  case Mips::ATOMIC_LOAD_AND_I32_POSTRA:
    Opcode = Mips::AND;
    break;
  case Mips::ATOMIC_LOAD_OR_I32_POSTRA:
    Opcode = Mips::OR;
    break;
  case Mips::ATOMIC_LOAD_XOR_I32_POSTRA:
    Opcode = Mips::XOR;
    break;
  case Mips::ATOMIC_LOAD_NAND_I32_POSTRA:
    IsNand = true;
    AND = Mips::AND;
    NOR = Mips::NOR;
    break;
  case Mips::ATOMIC_SWAP_I32_POSTRA:
    OR = Mips::OR;
    break;
  case Mips::ATOMIC_LOAD_ADD_I64_POSTRA:
    Opcode = Mips::DADDu;
    break;
  case Mips::ATOMIC_LOAD_SUB_I64_POSTRA:
    Opcode = Mips::DSUBu;
    break;
  case Mips::ATOMIC_LOAD_AND_I64_POSTRA:
    Opcode = Mips::AND64;
    break;
  case Mips::ATOMIC_LOAD_OR_I64_POSTRA:
    Opcode = Mips::OR64;
    break;
  case Mips::ATOMIC_LOAD_XOR_I64_POSTRA:
    Opcode = Mips::XOR64;
    break;
  case Mips::ATOMIC_LOAD_NAND_I64_POSTRA:
    IsNand = true;
    AND = Mips::AND64;
    NOR = Mips::NOR64;
    break;
  case Mips::ATOMIC_SWAP_I64_POSTRA:
    OR = Mips::OR64;
    break;
  default:
    llvm_unreachable("Unknown pseudo atomic!");
  }

  const BasicBlock *LLVM_BB = BB.getBasicBlock();
  MachineBasicBlock *loopMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *exitMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineFunction::iterator It = ++BB.getIterator();
  MF->insert(It, loopMBB);
  MF->insert(It, exitMBB);

  exitMBB->splice(exitMBB->begin(), &BB, std::next(I), BB.end());
  exitMBB->transferSuccessorsAndUpdatePHIs(&BB);

  BB.addSuccessor(loopMBB, BranchProbability::getOne());
  loopMBB->addSuccessor(exitMBB);
  loopMBB->addSuccessor(loopMBB);
  loopMBB->normalizeSuccProbs();

  BuildMI(loopMBB, DL, TII->get(LL), OldVal).addReg(Ptr).addImm(0);
  assert((OldVal != Ptr) && "Clobbered the wrong ptr reg!");
  assert((OldVal != Incr) && "Clobbered the wrong reg!");
  if (Opcode) {
    BuildMI(loopMBB, DL, TII->get(Opcode), Scratch).addReg(OldVal).addReg(Incr);
  } else if (IsNand) {
    assert(AND && NOR &&
           "Unknown nand instruction for atomic pseudo expansion");
    BuildMI(loopMBB, DL, TII->get(AND), Scratch).addReg(OldVal).addReg(Incr);
    BuildMI(loopMBB, DL, TII->get(NOR), Scratch).addReg(ZERO).addReg(Scratch);
  } else {
    assert(OR && "Unknown instruction for atomic pseudo expansion!");
    BuildMI(loopMBB, DL, TII->get(OR), Scratch).addReg(Incr).addReg(ZERO);
  }

  BuildMI(loopMBB, DL, TII->get(SC), Scratch).addReg(Scratch).addReg(Ptr).addImm(0);
  BuildMI(loopMBB, DL, TII->get(BEQ)).addReg(Scratch).addReg(ZERO).addMBB(loopMBB);

  NMBBI = BB.end();
  I->eraseFromParent();

  LivePhysRegs LiveRegs;
  computeAndAddLiveIns(LiveRegs, *loopMBB);
  computeAndAddLiveIns(LiveRegs, *exitMBB);

  return true;
}

bool MipsExpandPseudo::expandMI(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MBBI,
                                MachineBasicBlock::iterator &NMBB) {

  bool Modified = false;

  switch (MBBI->getOpcode()) {
  case Mips::ATOMIC_CMP_SWAP_I32_POSTRA:
  case Mips::ATOMIC_CMP_SWAP_I64_POSTRA:
    return expandAtomicCmpSwap(MBB, MBBI, NMBB);
  case Mips::ATOMIC_CMP_SWAP_I8_POSTRA:
  case Mips::ATOMIC_CMP_SWAP_I16_POSTRA:
    return expandAtomicCmpSwapSubword(MBB, MBBI, NMBB);
  case Mips::ATOMIC_SWAP_I8_POSTRA:
  case Mips::ATOMIC_SWAP_I16_POSTRA:
  case Mips::ATOMIC_LOAD_NAND_I8_POSTRA:
  case Mips::ATOMIC_LOAD_NAND_I16_POSTRA:
  case Mips::ATOMIC_LOAD_ADD_I8_POSTRA:
  case Mips::ATOMIC_LOAD_ADD_I16_POSTRA:
  case Mips::ATOMIC_LOAD_SUB_I8_POSTRA:
  case Mips::ATOMIC_LOAD_SUB_I16_POSTRA:
  case Mips::ATOMIC_LOAD_AND_I8_POSTRA:
  case Mips::ATOMIC_LOAD_AND_I16_POSTRA:
  case Mips::ATOMIC_LOAD_OR_I8_POSTRA:
  case Mips::ATOMIC_LOAD_OR_I16_POSTRA:
  case Mips::ATOMIC_LOAD_XOR_I8_POSTRA:
  case Mips::ATOMIC_LOAD_XOR_I16_POSTRA:
    return expandAtomicBinOpSubword(MBB, MBBI, NMBB);
  case Mips::ATOMIC_LOAD_ADD_I32_POSTRA:
  case Mips::ATOMIC_LOAD_SUB_I32_POSTRA:
  case Mips::ATOMIC_LOAD_AND_I32_POSTRA:
  case Mips::ATOMIC_LOAD_OR_I32_POSTRA:
  case Mips::ATOMIC_LOAD_XOR_I32_POSTRA:
  case Mips::ATOMIC_LOAD_NAND_I32_POSTRA:
  case Mips::ATOMIC_SWAP_I32_POSTRA:
    return expandAtomicBinOp(MBB, MBBI, NMBB, 4);
  case Mips::ATOMIC_LOAD_ADD_I64_POSTRA:
  case Mips::ATOMIC_LOAD_SUB_I64_POSTRA:
  case Mips::ATOMIC_LOAD_AND_I64_POSTRA:
  case Mips::ATOMIC_LOAD_OR_I64_POSTRA:
  case Mips::ATOMIC_LOAD_XOR_I64_POSTRA:
  case Mips::ATOMIC_LOAD_NAND_I64_POSTRA:
  case Mips::ATOMIC_SWAP_I64_POSTRA:
    return expandAtomicBinOp(MBB, MBBI, NMBB, 8);
  default:
    return Modified;
  }
}

bool MipsExpandPseudo::expandMBB(MachineBasicBlock &MBB) {
  bool Modified = false;

  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  while (MBBI != E) {
    MachineBasicBlock::iterator NMBBI = std::next(MBBI);
    Modified |= expandMI(MBB, MBBI, NMBBI);
    MBBI = NMBBI;
  }

  return Modified;
}

bool MipsExpandPseudo::runOnMachineFunction(MachineFunction &MF) {
  STI = &static_cast<const MipsSubtarget &>(MF.getSubtarget());
  TII = STI->getInstrInfo();

  bool Modified = false;
  for (MachineFunction::iterator MFI = MF.begin(), E = MF.end(); MFI != E;
       ++MFI)
    Modified |= expandMBB(*MFI);

  if (Modified)
    MF.RenumberBlocks();

  return Modified;
}

/// createMipsExpandPseudoPass - returns an instance of the pseudo instruction
/// expansion pass.
FunctionPass *llvm::createMipsExpandPseudoPass() {
  return new MipsExpandPseudo();
}
