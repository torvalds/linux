//===-- SIOptimizeExecMasking.cpp -----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUSubtarget.h"
#include "SIInstrInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "si-optimize-exec-masking"

namespace {

class SIOptimizeExecMasking : public MachineFunctionPass {
public:
  static char ID;

public:
  SIOptimizeExecMasking() : MachineFunctionPass(ID) {
    initializeSIOptimizeExecMaskingPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "SI optimize exec mask operations";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // End anonymous namespace.

INITIALIZE_PASS_BEGIN(SIOptimizeExecMasking, DEBUG_TYPE,
                      "SI optimize exec mask operations", false, false)
INITIALIZE_PASS_DEPENDENCY(LiveIntervals)
INITIALIZE_PASS_END(SIOptimizeExecMasking, DEBUG_TYPE,
                    "SI optimize exec mask operations", false, false)

char SIOptimizeExecMasking::ID = 0;

char &llvm::SIOptimizeExecMaskingID = SIOptimizeExecMasking::ID;

/// If \p MI is a copy from exec, return the register copied to.
static unsigned isCopyFromExec(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case AMDGPU::COPY:
  case AMDGPU::S_MOV_B64:
  case AMDGPU::S_MOV_B64_term: {
    const MachineOperand &Src = MI.getOperand(1);
    if (Src.isReg() && Src.getReg() == AMDGPU::EXEC)
      return MI.getOperand(0).getReg();
  }
  }

  return AMDGPU::NoRegister;
}

/// If \p MI is a copy to exec, return the register copied from.
static unsigned isCopyToExec(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case AMDGPU::COPY:
  case AMDGPU::S_MOV_B64: {
    const MachineOperand &Dst = MI.getOperand(0);
    if (Dst.isReg() && Dst.getReg() == AMDGPU::EXEC && MI.getOperand(1).isReg())
      return MI.getOperand(1).getReg();
    break;
  }
  case AMDGPU::S_MOV_B64_term:
    llvm_unreachable("should have been replaced");
  }

  return AMDGPU::NoRegister;
}

/// If \p MI is a logical operation on an exec value,
/// return the register copied to.
static unsigned isLogicalOpOnExec(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case AMDGPU::S_AND_B64:
  case AMDGPU::S_OR_B64:
  case AMDGPU::S_XOR_B64:
  case AMDGPU::S_ANDN2_B64:
  case AMDGPU::S_ORN2_B64:
  case AMDGPU::S_NAND_B64:
  case AMDGPU::S_NOR_B64:
  case AMDGPU::S_XNOR_B64: {
    const MachineOperand &Src1 = MI.getOperand(1);
    if (Src1.isReg() && Src1.getReg() == AMDGPU::EXEC)
      return MI.getOperand(0).getReg();
    const MachineOperand &Src2 = MI.getOperand(2);
    if (Src2.isReg() && Src2.getReg() == AMDGPU::EXEC)
      return MI.getOperand(0).getReg();
  }
  }

  return AMDGPU::NoRegister;
}

static unsigned getSaveExecOp(unsigned Opc) {
  switch (Opc) {
  case AMDGPU::S_AND_B64:
    return AMDGPU::S_AND_SAVEEXEC_B64;
  case AMDGPU::S_OR_B64:
    return AMDGPU::S_OR_SAVEEXEC_B64;
  case AMDGPU::S_XOR_B64:
    return AMDGPU::S_XOR_SAVEEXEC_B64;
  case AMDGPU::S_ANDN2_B64:
    return AMDGPU::S_ANDN2_SAVEEXEC_B64;
  case AMDGPU::S_ORN2_B64:
    return AMDGPU::S_ORN2_SAVEEXEC_B64;
  case AMDGPU::S_NAND_B64:
    return AMDGPU::S_NAND_SAVEEXEC_B64;
  case AMDGPU::S_NOR_B64:
    return AMDGPU::S_NOR_SAVEEXEC_B64;
  case AMDGPU::S_XNOR_B64:
    return AMDGPU::S_XNOR_SAVEEXEC_B64;
  default:
    return AMDGPU::INSTRUCTION_LIST_END;
  }
}

// These are only terminators to get correct spill code placement during
// register allocation, so turn them back into normal instructions. Only one of
// these is expected per block.
static bool removeTerminatorBit(const SIInstrInfo &TII, MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case AMDGPU::S_MOV_B64_term: {
    MI.setDesc(TII.get(AMDGPU::COPY));
    return true;
  }
  case AMDGPU::S_XOR_B64_term: {
    // This is only a terminator to get the correct spill code placement during
    // register allocation.
    MI.setDesc(TII.get(AMDGPU::S_XOR_B64));
    return true;
  }
  case AMDGPU::S_ANDN2_B64_term: {
    // This is only a terminator to get the correct spill code placement during
    // register allocation.
    MI.setDesc(TII.get(AMDGPU::S_ANDN2_B64));
    return true;
  }
  default:
    return false;
  }
}

static MachineBasicBlock::reverse_iterator fixTerminators(
  const SIInstrInfo &TII,
  MachineBasicBlock &MBB) {
  MachineBasicBlock::reverse_iterator I = MBB.rbegin(), E = MBB.rend();
  for (; I != E; ++I) {
    if (!I->isTerminator())
      return I;

    if (removeTerminatorBit(TII, *I))
      return I;
  }

  return E;
}

static MachineBasicBlock::reverse_iterator findExecCopy(
  const SIInstrInfo &TII,
  MachineBasicBlock &MBB,
  MachineBasicBlock::reverse_iterator I,
  unsigned CopyToExec) {
  const unsigned InstLimit = 25;

  auto E = MBB.rend();
  for (unsigned N = 0; N <= InstLimit && I != E; ++I, ++N) {
    unsigned CopyFromExec = isCopyFromExec(*I);
    if (CopyFromExec != AMDGPU::NoRegister)
      return I;
  }

  return E;
}

// XXX - Seems LivePhysRegs doesn't work correctly since it will incorrectly
// repor tthe register as unavailable because a super-register with a lane mask
// as unavailable.
static bool isLiveOut(const MachineBasicBlock &MBB, unsigned Reg) {
  for (MachineBasicBlock *Succ : MBB.successors()) {
    if (Succ->isLiveIn(Reg))
      return true;
  }

  return false;
}

bool SIOptimizeExecMasking::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  const SIInstrInfo *TII = ST.getInstrInfo();

  // Optimize sequences emitted for control flow lowering. They are originally
  // emitted as the separate operations because spill code may need to be
  // inserted for the saved copy of exec.
  //
  //     x = copy exec
  //     z = s_<op>_b64 x, y
  //     exec = copy z
  // =>
  //     x = s_<op>_saveexec_b64 y
  //

  for (MachineBasicBlock &MBB : MF) {
    MachineBasicBlock::reverse_iterator I = fixTerminators(*TII, MBB);
    MachineBasicBlock::reverse_iterator E = MBB.rend();
    if (I == E)
      continue;

    unsigned CopyToExec = isCopyToExec(*I);
    if (CopyToExec == AMDGPU::NoRegister)
      continue;

    // Scan backwards to find the def.
    auto CopyToExecInst = &*I;
    auto CopyFromExecInst = findExecCopy(*TII, MBB, I, CopyToExec);
    if (CopyFromExecInst == E) {
      auto PrepareExecInst = std::next(I);
      if (PrepareExecInst == E)
        continue;
      // Fold exec = COPY (S_AND_B64 reg, exec) -> exec = S_AND_B64 reg, exec
      if (CopyToExecInst->getOperand(1).isKill() &&
          isLogicalOpOnExec(*PrepareExecInst) == CopyToExec) {
        LLVM_DEBUG(dbgs() << "Fold exec copy: " << *PrepareExecInst);

        PrepareExecInst->getOperand(0).setReg(AMDGPU::EXEC);

        LLVM_DEBUG(dbgs() << "into: " << *PrepareExecInst << '\n');

        CopyToExecInst->eraseFromParent();
      }

      continue;
    }

    if (isLiveOut(MBB, CopyToExec)) {
      // The copied register is live out and has a second use in another block.
      LLVM_DEBUG(dbgs() << "Exec copy source register is live out\n");
      continue;
    }

    unsigned CopyFromExec = CopyFromExecInst->getOperand(0).getReg();
    MachineInstr *SaveExecInst = nullptr;
    SmallVector<MachineInstr *, 4> OtherUseInsts;

    for (MachineBasicBlock::iterator J
           = std::next(CopyFromExecInst->getIterator()), JE = I->getIterator();
         J != JE; ++J) {
      if (SaveExecInst && J->readsRegister(AMDGPU::EXEC, TRI)) {
        LLVM_DEBUG(dbgs() << "exec read prevents saveexec: " << *J << '\n');
        // Make sure this is inserted after any VALU ops that may have been
        // scheduled in between.
        SaveExecInst = nullptr;
        break;
      }

      bool ReadsCopyFromExec = J->readsRegister(CopyFromExec, TRI);

      if (J->modifiesRegister(CopyToExec, TRI)) {
        if (SaveExecInst) {
          LLVM_DEBUG(dbgs() << "Multiple instructions modify "
                            << printReg(CopyToExec, TRI) << '\n');
          SaveExecInst = nullptr;
          break;
        }

        unsigned SaveExecOp = getSaveExecOp(J->getOpcode());
        if (SaveExecOp == AMDGPU::INSTRUCTION_LIST_END)
          break;

        if (ReadsCopyFromExec) {
          SaveExecInst = &*J;
          LLVM_DEBUG(dbgs() << "Found save exec op: " << *SaveExecInst << '\n');
          continue;
        } else {
          LLVM_DEBUG(dbgs()
                     << "Instruction does not read exec copy: " << *J << '\n');
          break;
        }
      } else if (ReadsCopyFromExec && !SaveExecInst) {
        // Make sure no other instruction is trying to use this copy, before it
        // will be rewritten by the saveexec, i.e. hasOneUse. There may have
        // been another use, such as an inserted spill. For example:
        //
        // %sgpr0_sgpr1 = COPY %exec
        // spill %sgpr0_sgpr1
        // %sgpr2_sgpr3 = S_AND_B64 %sgpr0_sgpr1
        //
        LLVM_DEBUG(dbgs() << "Found second use of save inst candidate: " << *J
                          << '\n');
        break;
      }

      if (SaveExecInst && J->readsRegister(CopyToExec, TRI)) {
        assert(SaveExecInst != &*J);
        OtherUseInsts.push_back(&*J);
      }
    }

    if (!SaveExecInst)
      continue;

    LLVM_DEBUG(dbgs() << "Insert save exec op: " << *SaveExecInst << '\n');

    MachineOperand &Src0 = SaveExecInst->getOperand(1);
    MachineOperand &Src1 = SaveExecInst->getOperand(2);

    MachineOperand *OtherOp = nullptr;

    if (Src0.isReg() && Src0.getReg() == CopyFromExec) {
      OtherOp = &Src1;
    } else if (Src1.isReg() && Src1.getReg() == CopyFromExec) {
      if (!SaveExecInst->isCommutable())
        break;

      OtherOp = &Src0;
    } else
      llvm_unreachable("unexpected");

    CopyFromExecInst->eraseFromParent();

    auto InsPt = SaveExecInst->getIterator();
    const DebugLoc &DL = SaveExecInst->getDebugLoc();

    BuildMI(MBB, InsPt, DL, TII->get(getSaveExecOp(SaveExecInst->getOpcode())),
            CopyFromExec)
      .addReg(OtherOp->getReg());
    SaveExecInst->eraseFromParent();

    CopyToExecInst->eraseFromParent();

    for (MachineInstr *OtherInst : OtherUseInsts) {
      OtherInst->substituteRegister(CopyToExec, AMDGPU::EXEC,
                                    AMDGPU::NoSubRegister, *TRI);
    }
  }

  return true;

}
