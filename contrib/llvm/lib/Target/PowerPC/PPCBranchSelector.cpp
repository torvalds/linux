//===-- PPCBranchSelector.cpp - Emit long conditional branches ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that scans a machine function to determine which
// conditional branches need more than 16 bits of displacement to reach their
// target basic block.  It does this in two passes; a calculation of basic block
// positions pass, and a branch pseudo op to machine branch opcode pass.  This
// pass should be run last, just before the assembly printer.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PPCPredicates.h"
#include "PPC.h"
#include "PPCInstrBuilder.h"
#include "PPCInstrInfo.h"
#include "PPCSubtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

#define DEBUG_TYPE "ppc-branch-select"

STATISTIC(NumExpanded, "Number of branches expanded to long format");

namespace llvm {
  void initializePPCBSelPass(PassRegistry&);
}

namespace {
  struct PPCBSel : public MachineFunctionPass {
    static char ID;
    PPCBSel() : MachineFunctionPass(ID) {
      initializePPCBSelPass(*PassRegistry::getPassRegistry());
    }

    // The sizes of the basic blocks in the function (the first
    // element of the pair); the second element of the pair is the amount of the
    // size that is due to potential padding.
    std::vector<std::pair<unsigned, unsigned>> BlockSizes;

    bool runOnMachineFunction(MachineFunction &Fn) override;

    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }

    StringRef getPassName() const override { return "PowerPC Branch Selector"; }
  };
  char PPCBSel::ID = 0;
}

INITIALIZE_PASS(PPCBSel, "ppc-branch-select", "PowerPC Branch Selector",
                false, false)

/// createPPCBranchSelectionPass - returns an instance of the Branch Selection
/// Pass
///
FunctionPass *llvm::createPPCBranchSelectionPass() {
  return new PPCBSel();
}

bool PPCBSel::runOnMachineFunction(MachineFunction &Fn) {
  const PPCInstrInfo *TII =
      static_cast<const PPCInstrInfo *>(Fn.getSubtarget().getInstrInfo());
  // Give the blocks of the function a dense, in-order, numbering.
  Fn.RenumberBlocks();
  BlockSizes.resize(Fn.getNumBlockIDs());

  auto GetAlignmentAdjustment =
    [](MachineBasicBlock &MBB, unsigned Offset) -> unsigned {
    unsigned Align = MBB.getAlignment();
    if (!Align)
      return 0;

    unsigned AlignAmt = 1 << Align;
    unsigned ParentAlign = MBB.getParent()->getAlignment();

    if (Align <= ParentAlign)
      return OffsetToAlignment(Offset, AlignAmt);

    // The alignment of this MBB is larger than the function's alignment, so we
    // can't tell whether or not it will insert nops. Assume that it will.
    return AlignAmt + OffsetToAlignment(Offset, AlignAmt);
  };

  // We need to be careful about the offset of the first block in the function
  // because it might not have the function's alignment. This happens because,
  // under the ELFv2 ABI, for functions which require a TOC pointer, we add a
  // two-instruction sequence to the start of the function.
  // Note: This needs to be synchronized with the check in
  // PPCLinuxAsmPrinter::EmitFunctionBodyStart.
  unsigned InitialOffset = 0;
  if (Fn.getSubtarget<PPCSubtarget>().isELFv2ABI() &&
      !Fn.getRegInfo().use_empty(PPC::X2))
    InitialOffset = 8;

  // Measure each MBB and compute a size for the entire function.
  unsigned FuncSize = InitialOffset;
  for (MachineFunction::iterator MFI = Fn.begin(), E = Fn.end(); MFI != E;
       ++MFI) {
    MachineBasicBlock *MBB = &*MFI;

    // The end of the previous block may have extra nops if this block has an
    // alignment requirement.
    if (MBB->getNumber() > 0) {
      unsigned AlignExtra = GetAlignmentAdjustment(*MBB, FuncSize);

      auto &BS = BlockSizes[MBB->getNumber()-1];
      BS.first += AlignExtra;
      BS.second = AlignExtra;

      FuncSize += AlignExtra;
    }

    unsigned BlockSize = 0;
    for (MachineInstr &MI : *MBB)
      BlockSize += TII->getInstSizeInBytes(MI);

    BlockSizes[MBB->getNumber()].first = BlockSize;
    FuncSize += BlockSize;
  }

  // If the entire function is smaller than the displacement of a branch field,
  // we know we don't need to shrink any branches in this function.  This is a
  // common case.
  if (FuncSize < (1 << 15)) {
    BlockSizes.clear();
    return false;
  }

  // For each conditional branch, if the offset to its destination is larger
  // than the offset field allows, transform it into a long branch sequence
  // like this:
  //   short branch:
  //     bCC MBB
  //   long branch:
  //     b!CC $PC+8
  //     b MBB
  //
  bool MadeChange = true;
  bool EverMadeChange = false;
  while (MadeChange) {
    // Iteratively expand branches until we reach a fixed point.
    MadeChange = false;

    for (MachineFunction::iterator MFI = Fn.begin(), E = Fn.end(); MFI != E;
         ++MFI) {
      MachineBasicBlock &MBB = *MFI;
      unsigned MBBStartOffset = 0;
      for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end();
           I != E; ++I) {
        MachineBasicBlock *Dest = nullptr;
        if (I->getOpcode() == PPC::BCC && !I->getOperand(2).isImm())
          Dest = I->getOperand(2).getMBB();
        else if ((I->getOpcode() == PPC::BC || I->getOpcode() == PPC::BCn) &&
                 !I->getOperand(1).isImm())
          Dest = I->getOperand(1).getMBB();
        else if ((I->getOpcode() == PPC::BDNZ8 || I->getOpcode() == PPC::BDNZ ||
                  I->getOpcode() == PPC::BDZ8  || I->getOpcode() == PPC::BDZ) &&
                 !I->getOperand(0).isImm())
          Dest = I->getOperand(0).getMBB();

        if (!Dest) {
          MBBStartOffset += TII->getInstSizeInBytes(*I);
          continue;
        }

        // Determine the offset from the current branch to the destination
        // block.
        int BranchSize;
        if (Dest->getNumber() <= MBB.getNumber()) {
          // If this is a backwards branch, the delta is the offset from the
          // start of this block to this branch, plus the sizes of all blocks
          // from this block to the dest.
          BranchSize = MBBStartOffset;

          for (unsigned i = Dest->getNumber(), e = MBB.getNumber(); i != e; ++i)
            BranchSize += BlockSizes[i].first;
        } else {
          // Otherwise, add the size of the blocks between this block and the
          // dest to the number of bytes left in this block.
          BranchSize = -MBBStartOffset;

          for (unsigned i = MBB.getNumber(), e = Dest->getNumber(); i != e; ++i)
            BranchSize += BlockSizes[i].first;
        }

        // If this branch is in range, ignore it.
        if (isInt<16>(BranchSize)) {
          MBBStartOffset += 4;
          continue;
        }

        // Otherwise, we have to expand it to a long branch.
        MachineInstr &OldBranch = *I;
        DebugLoc dl = OldBranch.getDebugLoc();

        if (I->getOpcode() == PPC::BCC) {
          // The BCC operands are:
          // 0. PPC branch predicate
          // 1. CR register
          // 2. Target MBB
          PPC::Predicate Pred = (PPC::Predicate)I->getOperand(0).getImm();
          unsigned CRReg = I->getOperand(1).getReg();

          // Jump over the uncond branch inst (i.e. $PC+8) on opposite condition.
          BuildMI(MBB, I, dl, TII->get(PPC::BCC))
            .addImm(PPC::InvertPredicate(Pred)).addReg(CRReg).addImm(2);
        } else if (I->getOpcode() == PPC::BC) {
          unsigned CRBit = I->getOperand(0).getReg();
          BuildMI(MBB, I, dl, TII->get(PPC::BCn)).addReg(CRBit).addImm(2);
        } else if (I->getOpcode() == PPC::BCn) {
          unsigned CRBit = I->getOperand(0).getReg();
          BuildMI(MBB, I, dl, TII->get(PPC::BC)).addReg(CRBit).addImm(2);
        } else if (I->getOpcode() == PPC::BDNZ) {
          BuildMI(MBB, I, dl, TII->get(PPC::BDZ)).addImm(2);
        } else if (I->getOpcode() == PPC::BDNZ8) {
          BuildMI(MBB, I, dl, TII->get(PPC::BDZ8)).addImm(2);
        } else if (I->getOpcode() == PPC::BDZ) {
          BuildMI(MBB, I, dl, TII->get(PPC::BDNZ)).addImm(2);
        } else if (I->getOpcode() == PPC::BDZ8) {
          BuildMI(MBB, I, dl, TII->get(PPC::BDNZ8)).addImm(2);
        } else {
           llvm_unreachable("Unhandled branch type!");
        }

        // Uncond branch to the real destination.
        I = BuildMI(MBB, I, dl, TII->get(PPC::B)).addMBB(Dest);

        // Remove the old branch from the function.
        OldBranch.eraseFromParent();

        // Remember that this instruction is 8-bytes, increase the size of the
        // block by 4, remember to iterate.
        BlockSizes[MBB.getNumber()].first += 4;
        MBBStartOffset += 8;
        ++NumExpanded;
        MadeChange = true;
      }
    }

    if (MadeChange) {
      // If we're going to iterate again, make sure we've updated our
      // padding-based contributions to the block sizes.
      unsigned Offset = InitialOffset;
      for (MachineFunction::iterator MFI = Fn.begin(), E = Fn.end(); MFI != E;
           ++MFI) {
        MachineBasicBlock *MBB = &*MFI;

        if (MBB->getNumber() > 0) {
          auto &BS = BlockSizes[MBB->getNumber()-1];
          BS.first -= BS.second;
          Offset -= BS.second;

          unsigned AlignExtra = GetAlignmentAdjustment(*MBB, Offset);

          BS.first += AlignExtra;
          BS.second = AlignExtra;

          Offset += AlignExtra;
        }

        Offset += BlockSizes[MBB->getNumber()].first;
      }
    }

    EverMadeChange |= MadeChange;
  }

  BlockSizes.clear();
  return true;
}
