//===------------- PPCEarlyReturn.cpp - Form Early Returns ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// A pass that form early (predicated) returns. If-conversion handles some of
// this, but this pass picks up some remaining cases.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PPCPredicates.h"
#include "PPC.h"
#include "PPCInstrBuilder.h"
#include "PPCInstrInfo.h"
#include "PPCMachineFunctionInfo.h"
#include "PPCTargetMachine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "ppc-early-ret"
STATISTIC(NumBCLR, "Number of early conditional returns");
STATISTIC(NumBLR,  "Number of early returns");

namespace llvm {
  void initializePPCEarlyReturnPass(PassRegistry&);
}

namespace {
  // PPCEarlyReturn pass - For simple functions without epilogue code, move
  // returns up, and create conditional returns, to avoid unnecessary
  // branch-to-blr sequences.
  struct PPCEarlyReturn : public MachineFunctionPass {
    static char ID;
    PPCEarlyReturn() : MachineFunctionPass(ID) {
      initializePPCEarlyReturnPass(*PassRegistry::getPassRegistry());
    }

    const TargetInstrInfo *TII;

protected:
    bool processBlock(MachineBasicBlock &ReturnMBB) {
      bool Changed = false;

      MachineBasicBlock::iterator I = ReturnMBB.begin();
      I = ReturnMBB.SkipPHIsLabelsAndDebug(I);

      // The block must be essentially empty except for the blr.
      if (I == ReturnMBB.end() ||
          (I->getOpcode() != PPC::BLR && I->getOpcode() != PPC::BLR8) ||
          I != ReturnMBB.getLastNonDebugInstr())
        return Changed;

      SmallVector<MachineBasicBlock*, 8> PredToRemove;
      for (MachineBasicBlock::pred_iterator PI = ReturnMBB.pred_begin(),
           PIE = ReturnMBB.pred_end(); PI != PIE; ++PI) {
        bool OtherReference = false, BlockChanged = false;

        if ((*PI)->empty())
          continue;

        for (MachineBasicBlock::iterator J = (*PI)->getLastNonDebugInstr();;) {
          if (J == (*PI)->end())
            break;

          if (J->getOpcode() == PPC::B) {
            if (J->getOperand(0).getMBB() == &ReturnMBB) {
              // This is an unconditional branch to the return. Replace the
              // branch with a blr.
              BuildMI(**PI, J, J->getDebugLoc(), TII->get(I->getOpcode()))
                  .copyImplicitOps(*I);
              MachineBasicBlock::iterator K = J--;
              K->eraseFromParent();
              BlockChanged = true;
              ++NumBLR;
              continue;
            }
          } else if (J->getOpcode() == PPC::BCC) {
            if (J->getOperand(2).getMBB() == &ReturnMBB) {
              // This is a conditional branch to the return. Replace the branch
              // with a bclr.
              BuildMI(**PI, J, J->getDebugLoc(), TII->get(PPC::BCCLR))
                  .addImm(J->getOperand(0).getImm())
                  .addReg(J->getOperand(1).getReg())
                  .copyImplicitOps(*I);
              MachineBasicBlock::iterator K = J--;
              K->eraseFromParent();
              BlockChanged = true;
              ++NumBCLR;
              continue;
            }
          } else if (J->getOpcode() == PPC::BC || J->getOpcode() == PPC::BCn) {
            if (J->getOperand(1).getMBB() == &ReturnMBB) {
              // This is a conditional branch to the return. Replace the branch
              // with a bclr.
              BuildMI(
                  **PI, J, J->getDebugLoc(),
                  TII->get(J->getOpcode() == PPC::BC ? PPC::BCLR : PPC::BCLRn))
                  .addReg(J->getOperand(0).getReg())
                  .copyImplicitOps(*I);
              MachineBasicBlock::iterator K = J--;
              K->eraseFromParent();
              BlockChanged = true;
              ++NumBCLR;
              continue;
            }
          } else if (J->isBranch()) {
            if (J->isIndirectBranch()) {
              if (ReturnMBB.hasAddressTaken())
                OtherReference = true;
            } else
              for (unsigned i = 0; i < J->getNumOperands(); ++i)
                if (J->getOperand(i).isMBB() &&
                    J->getOperand(i).getMBB() == &ReturnMBB)
                  OtherReference = true;
          } else if (!J->isTerminator() && !J->isDebugInstr())
            break;

          if (J == (*PI)->begin())
            break;

          --J;
        }

        if ((*PI)->canFallThrough() && (*PI)->isLayoutSuccessor(&ReturnMBB))
          OtherReference = true;

        // Predecessors are stored in a vector and can't be removed here.
        if (!OtherReference && BlockChanged) {
          PredToRemove.push_back(*PI);
        }

        if (BlockChanged)
          Changed = true;
      }

      for (unsigned i = 0, ie = PredToRemove.size(); i != ie; ++i)
        PredToRemove[i]->removeSuccessor(&ReturnMBB, true);

      if (Changed && !ReturnMBB.hasAddressTaken()) {
        // We now might be able to merge this blr-only block into its
        // by-layout predecessor.
        if (ReturnMBB.pred_size() == 1) {
          MachineBasicBlock &PrevMBB = **ReturnMBB.pred_begin();
          if (PrevMBB.isLayoutSuccessor(&ReturnMBB) && PrevMBB.canFallThrough()) {
            // Move the blr into the preceding block.
            PrevMBB.splice(PrevMBB.end(), &ReturnMBB, I);
            PrevMBB.removeSuccessor(&ReturnMBB, true);
          }
        }

        if (ReturnMBB.pred_empty())
          ReturnMBB.eraseFromParent();
      }

      return Changed;
    }

public:
    bool runOnMachineFunction(MachineFunction &MF) override {
      if (skipFunction(MF.getFunction()))
        return false;

      TII = MF.getSubtarget().getInstrInfo();

      bool Changed = false;

      // If the function does not have at least two blocks, then there is
      // nothing to do.
      if (MF.size() < 2)
        return Changed;

      for (MachineFunction::iterator I = MF.begin(); I != MF.end();) {
        MachineBasicBlock &B = *I++;
        if (processBlock(B))
          Changed = true;
      }

      return Changed;
    }

    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      MachineFunctionPass::getAnalysisUsage(AU);
    }
  };
}

INITIALIZE_PASS(PPCEarlyReturn, DEBUG_TYPE,
                "PowerPC Early-Return Creation", false, false)

char PPCEarlyReturn::ID = 0;
FunctionPass*
llvm::createPPCEarlyReturnPass() { return new PPCEarlyReturn(); }
