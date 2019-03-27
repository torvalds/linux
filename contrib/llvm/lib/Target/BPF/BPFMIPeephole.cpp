//===-------------- BPFMIPeephole.cpp - MI Peephole Cleanups  -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass performs peephole optimizations to cleanup ugly code sequences at
// MachineInstruction layer.
//
// Currently, there are two optimizations implemented:
//  - One pre-RA MachineSSA pass to eliminate type promotion sequences, those
//    zero extend 32-bit subregisters to 64-bit registers, if the compiler
//    could prove the subregisters is defined by 32-bit operations in which
//    case the upper half of the underlying 64-bit registers were zeroed
//    implicitly.
//
//  - One post-RA PreEmit pass to do final cleanup on some redundant
//    instructions generated due to bad RA on subregister.
//===----------------------------------------------------------------------===//

#include "BPF.h"
#include "BPFInstrInfo.h"
#include "BPFTargetMachine.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

#define DEBUG_TYPE "bpf-mi-zext-elim"

STATISTIC(ZExtElemNum, "Number of zero extension shifts eliminated");

namespace {

struct BPFMIPeephole : public MachineFunctionPass {

  static char ID;
  const BPFInstrInfo *TII;
  MachineFunction *MF;
  MachineRegisterInfo *MRI;

  BPFMIPeephole() : MachineFunctionPass(ID) {
    initializeBPFMIPeepholePass(*PassRegistry::getPassRegistry());
  }

private:
  // Initialize class variables.
  void initialize(MachineFunction &MFParm);

  bool isMovFrom32Def(MachineInstr *MovMI);
  bool eliminateZExtSeq(void);

public:

  // Main entry point for this pass.
  bool runOnMachineFunction(MachineFunction &MF) override {
    if (skipFunction(MF.getFunction()))
      return false;

    initialize(MF);

    return eliminateZExtSeq();
  }
};

// Initialize class variables.
void BPFMIPeephole::initialize(MachineFunction &MFParm) {
  MF = &MFParm;
  MRI = &MF->getRegInfo();
  TII = MF->getSubtarget<BPFSubtarget>().getInstrInfo();
  LLVM_DEBUG(dbgs() << "*** BPF MachineSSA peephole pass ***\n\n");
}

bool BPFMIPeephole::isMovFrom32Def(MachineInstr *MovMI)
{
  MachineInstr *DefInsn = MRI->getVRegDef(MovMI->getOperand(1).getReg());

  LLVM_DEBUG(dbgs() << "  Def of Mov Src:");
  LLVM_DEBUG(DefInsn->dump());

  if (!DefInsn)
    return false;

  if (DefInsn->isPHI()) {
    for (unsigned i = 1, e = DefInsn->getNumOperands(); i < e; i += 2) {
      MachineOperand &opnd = DefInsn->getOperand(i);

      if (!opnd.isReg())
        return false;

      MachineInstr *PhiDef = MRI->getVRegDef(opnd.getReg());
      // quick check on PHI incoming definitions.
      if (!PhiDef || PhiDef->isPHI() || PhiDef->getOpcode() == BPF::COPY)
        return false;
    }
  }

  if (DefInsn->getOpcode() == BPF::COPY) {
    MachineOperand &opnd = DefInsn->getOperand(1);

    if (!opnd.isReg())
      return false;

    unsigned Reg = opnd.getReg();
    if ((TargetRegisterInfo::isVirtualRegister(Reg) &&
         MRI->getRegClass(Reg) == &BPF::GPRRegClass))
       return false;
  }

  LLVM_DEBUG(dbgs() << "  One ZExt elim sequence identified.\n");

  return true;
}

bool BPFMIPeephole::eliminateZExtSeq(void) {
  MachineInstr* ToErase = nullptr;
  bool Eliminated = false;

  for (MachineBasicBlock &MBB : *MF) {
    for (MachineInstr &MI : MBB) {
      // If the previous instruction was marked for elimination, remove it now.
      if (ToErase) {
        ToErase->eraseFromParent();
        ToErase = nullptr;
      }

      // Eliminate the 32-bit to 64-bit zero extension sequence when possible.
      //
      //   MOV_32_64 rB, wA
      //   SLL_ri    rB, rB, 32
      //   SRL_ri    rB, rB, 32
      if (MI.getOpcode() == BPF::SRL_ri &&
          MI.getOperand(2).getImm() == 32) {
        unsigned DstReg = MI.getOperand(0).getReg();
        unsigned ShfReg = MI.getOperand(1).getReg();
        MachineInstr *SllMI = MRI->getVRegDef(ShfReg);

        LLVM_DEBUG(dbgs() << "Starting SRL found:");
        LLVM_DEBUG(MI.dump());

        if (!SllMI ||
            SllMI->isPHI() ||
            SllMI->getOpcode() != BPF::SLL_ri ||
            SllMI->getOperand(2).getImm() != 32)
          continue;

        LLVM_DEBUG(dbgs() << "  SLL found:");
        LLVM_DEBUG(SllMI->dump());

        MachineInstr *MovMI = MRI->getVRegDef(SllMI->getOperand(1).getReg());
        if (!MovMI ||
            MovMI->isPHI() ||
            MovMI->getOpcode() != BPF::MOV_32_64)
          continue;

        LLVM_DEBUG(dbgs() << "  Type cast Mov found:");
        LLVM_DEBUG(MovMI->dump());

        unsigned SubReg = MovMI->getOperand(1).getReg();
        if (!isMovFrom32Def(MovMI)) {
          LLVM_DEBUG(dbgs()
                     << "  One ZExt elim sequence failed qualifying elim.\n");
          continue;
        }

        BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(BPF::SUBREG_TO_REG), DstReg)
          .addImm(0).addReg(SubReg).addImm(BPF::sub_32);

        SllMI->eraseFromParent();
        MovMI->eraseFromParent();
        // MI is the right shift, we can't erase it in it's own iteration.
        // Mark it to ToErase, and erase in the next iteration.
        ToErase = &MI;
        ZExtElemNum++;
        Eliminated = true;
      }
    }
  }

  return Eliminated;
}

} // end default namespace

INITIALIZE_PASS(BPFMIPeephole, DEBUG_TYPE,
                "BPF MachineSSA Peephole Optimization", false, false)

char BPFMIPeephole::ID = 0;
FunctionPass* llvm::createBPFMIPeepholePass() { return new BPFMIPeephole(); }

STATISTIC(RedundantMovElemNum, "Number of redundant moves eliminated");

namespace {

struct BPFMIPreEmitPeephole : public MachineFunctionPass {

  static char ID;
  MachineFunction *MF;
  const TargetRegisterInfo *TRI;

  BPFMIPreEmitPeephole() : MachineFunctionPass(ID) {
    initializeBPFMIPreEmitPeepholePass(*PassRegistry::getPassRegistry());
  }

private:
  // Initialize class variables.
  void initialize(MachineFunction &MFParm);

  bool eliminateRedundantMov(void);

public:

  // Main entry point for this pass.
  bool runOnMachineFunction(MachineFunction &MF) override {
    if (skipFunction(MF.getFunction()))
      return false;

    initialize(MF);

    return eliminateRedundantMov();
  }
};

// Initialize class variables.
void BPFMIPreEmitPeephole::initialize(MachineFunction &MFParm) {
  MF = &MFParm;
  TRI = MF->getSubtarget<BPFSubtarget>().getRegisterInfo();
  LLVM_DEBUG(dbgs() << "*** BPF PreEmit peephole pass ***\n\n");
}

bool BPFMIPreEmitPeephole::eliminateRedundantMov(void) {
  MachineInstr* ToErase = nullptr;
  bool Eliminated = false;

  for (MachineBasicBlock &MBB : *MF) {
    for (MachineInstr &MI : MBB) {
      // If the previous instruction was marked for elimination, remove it now.
      if (ToErase) {
        LLVM_DEBUG(dbgs() << "  Redundant Mov Eliminated:");
        LLVM_DEBUG(ToErase->dump());
        ToErase->eraseFromParent();
        ToErase = nullptr;
      }

      // Eliminate identical move:
      //
      //   MOV rA, rA
      //
      // This is particularly possible to happen when sub-register support
      // enabled. The special type cast insn MOV_32_64 involves different
      // register class on src (i32) and dst (i64), RA could generate useless
      // instruction due to this.
      if (MI.getOpcode() == BPF::MOV_32_64) {
        unsigned dst = MI.getOperand(0).getReg();
        unsigned dst_sub = TRI->getSubReg(dst, BPF::sub_32);
        unsigned src = MI.getOperand(1).getReg();

        if (dst_sub != src)
          continue;

        ToErase = &MI;
        RedundantMovElemNum++;
        Eliminated = true;
      }
    }
  }

  return Eliminated;
}

} // end default namespace

INITIALIZE_PASS(BPFMIPreEmitPeephole, "bpf-mi-pemit-peephole",
                "BPF PreEmit Peephole Optimization", false, false)

char BPFMIPreEmitPeephole::ID = 0;
FunctionPass* llvm::createBPFMIPreEmitPeepholePass()
{
  return new BPFMIPreEmitPeephole();
}
