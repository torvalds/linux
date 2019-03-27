//===- BranchRelaxation.cpp -----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <iterator>
#include <memory>

using namespace llvm;

#define DEBUG_TYPE "branch-relaxation"

STATISTIC(NumSplit, "Number of basic blocks split");
STATISTIC(NumConditionalRelaxed, "Number of conditional branches relaxed");
STATISTIC(NumUnconditionalRelaxed, "Number of unconditional branches relaxed");

#define BRANCH_RELAX_NAME "Branch relaxation pass"

namespace {

class BranchRelaxation : public MachineFunctionPass {
  /// BasicBlockInfo - Information about the offset and size of a single
  /// basic block.
  struct BasicBlockInfo {
    /// Offset - Distance from the beginning of the function to the beginning
    /// of this basic block.
    ///
    /// The offset is always aligned as required by the basic block.
    unsigned Offset = 0;

    /// Size - Size of the basic block in bytes.  If the block contains
    /// inline assembly, this is a worst case estimate.
    ///
    /// The size does not include any alignment padding whether from the
    /// beginning of the block, or from an aligned jump table at the end.
    unsigned Size = 0;

    BasicBlockInfo() = default;

    /// Compute the offset immediately following this block. \p MBB is the next
    /// block.
    unsigned postOffset(const MachineBasicBlock &MBB) const {
      unsigned PO = Offset + Size;
      unsigned Align = MBB.getAlignment();
      if (Align == 0)
        return PO;

      unsigned AlignAmt = 1 << Align;
      unsigned ParentAlign = MBB.getParent()->getAlignment();
      if (Align <= ParentAlign)
        return PO + OffsetToAlignment(PO, AlignAmt);

      // The alignment of this MBB is larger than the function's alignment, so we
      // can't tell whether or not it will insert nops. Assume that it will.
      return PO + AlignAmt + OffsetToAlignment(PO, AlignAmt);
    }
  };

  SmallVector<BasicBlockInfo, 16> BlockInfo;
  std::unique_ptr<RegScavenger> RS;
  LivePhysRegs LiveRegs;

  MachineFunction *MF;
  const TargetRegisterInfo *TRI;
  const TargetInstrInfo *TII;

  bool relaxBranchInstructions();
  void scanFunction();

  MachineBasicBlock *createNewBlockAfter(MachineBasicBlock &BB);

  MachineBasicBlock *splitBlockBeforeInstr(MachineInstr &MI,
                                           MachineBasicBlock *DestBB);
  void adjustBlockOffsets(MachineBasicBlock &Start);
  bool isBlockInRange(const MachineInstr &MI, const MachineBasicBlock &BB) const;

  bool fixupConditionalBranch(MachineInstr &MI);
  bool fixupUnconditionalBranch(MachineInstr &MI);
  uint64_t computeBlockSize(const MachineBasicBlock &MBB) const;
  unsigned getInstrOffset(const MachineInstr &MI) const;
  void dumpBBs();
  void verify();

public:
  static char ID;

  BranchRelaxation() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return BRANCH_RELAX_NAME; }
};

} // end anonymous namespace

char BranchRelaxation::ID = 0;

char &llvm::BranchRelaxationPassID = BranchRelaxation::ID;

INITIALIZE_PASS(BranchRelaxation, DEBUG_TYPE, BRANCH_RELAX_NAME, false, false)

/// verify - check BBOffsets, BBSizes, alignment of islands
void BranchRelaxation::verify() {
#ifndef NDEBUG
  unsigned PrevNum = MF->begin()->getNumber();
  for (MachineBasicBlock &MBB : *MF) {
    unsigned Align = MBB.getAlignment();
    unsigned Num = MBB.getNumber();
    assert(BlockInfo[Num].Offset % (1u << Align) == 0);
    assert(!Num || BlockInfo[PrevNum].postOffset(MBB) <= BlockInfo[Num].Offset);
    assert(BlockInfo[Num].Size == computeBlockSize(MBB));
    PrevNum = Num;
  }
#endif
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
/// print block size and offset information - debugging
LLVM_DUMP_METHOD void BranchRelaxation::dumpBBs() {
  for (auto &MBB : *MF) {
    const BasicBlockInfo &BBI = BlockInfo[MBB.getNumber()];
    dbgs() << format("%bb.%u\toffset=%08x\t", MBB.getNumber(), BBI.Offset)
           << format("size=%#x\n", BBI.Size);
  }
}
#endif

/// scanFunction - Do the initial scan of the function, building up
/// information about each block.
void BranchRelaxation::scanFunction() {
  BlockInfo.clear();
  BlockInfo.resize(MF->getNumBlockIDs());

  // First thing, compute the size of all basic blocks, and see if the function
  // has any inline assembly in it. If so, we have to be conservative about
  // alignment assumptions, as we don't know for sure the size of any
  // instructions in the inline assembly.
  for (MachineBasicBlock &MBB : *MF)
    BlockInfo[MBB.getNumber()].Size = computeBlockSize(MBB);

  // Compute block offsets and known bits.
  adjustBlockOffsets(*MF->begin());
}

/// computeBlockSize - Compute the size for MBB.
uint64_t BranchRelaxation::computeBlockSize(const MachineBasicBlock &MBB) const {
  uint64_t Size = 0;
  for (const MachineInstr &MI : MBB)
    Size += TII->getInstSizeInBytes(MI);
  return Size;
}

/// getInstrOffset - Return the current offset of the specified machine
/// instruction from the start of the function.  This offset changes as stuff is
/// moved around inside the function.
unsigned BranchRelaxation::getInstrOffset(const MachineInstr &MI) const {
  const MachineBasicBlock *MBB = MI.getParent();

  // The offset is composed of two things: the sum of the sizes of all MBB's
  // before this instruction's block, and the offset from the start of the block
  // it is in.
  unsigned Offset = BlockInfo[MBB->getNumber()].Offset;

  // Sum instructions before MI in MBB.
  for (MachineBasicBlock::const_iterator I = MBB->begin(); &*I != &MI; ++I) {
    assert(I != MBB->end() && "Didn't find MI in its own basic block?");
    Offset += TII->getInstSizeInBytes(*I);
  }

  return Offset;
}

void BranchRelaxation::adjustBlockOffsets(MachineBasicBlock &Start) {
  unsigned PrevNum = Start.getNumber();
  for (auto &MBB : make_range(MachineFunction::iterator(Start), MF->end())) {
    unsigned Num = MBB.getNumber();
    if (!Num) // block zero is never changed from offset zero.
      continue;
    // Get the offset and known bits at the end of the layout predecessor.
    // Include the alignment of the current block.
    BlockInfo[Num].Offset = BlockInfo[PrevNum].postOffset(MBB);

    PrevNum = Num;
  }
}

/// Insert a new empty basic block and insert it after \BB
MachineBasicBlock *BranchRelaxation::createNewBlockAfter(MachineBasicBlock &BB) {
  // Create a new MBB for the code after the OrigBB.
  MachineBasicBlock *NewBB =
      MF->CreateMachineBasicBlock(BB.getBasicBlock());
  MF->insert(++BB.getIterator(), NewBB);

  // Insert an entry into BlockInfo to align it properly with the block numbers.
  BlockInfo.insert(BlockInfo.begin() + NewBB->getNumber(), BasicBlockInfo());

  return NewBB;
}

/// Split the basic block containing MI into two blocks, which are joined by
/// an unconditional branch.  Update data structures and renumber blocks to
/// account for this change and returns the newly created block.
MachineBasicBlock *BranchRelaxation::splitBlockBeforeInstr(MachineInstr &MI,
                                                           MachineBasicBlock *DestBB) {
  MachineBasicBlock *OrigBB = MI.getParent();

  // Create a new MBB for the code after the OrigBB.
  MachineBasicBlock *NewBB =
      MF->CreateMachineBasicBlock(OrigBB->getBasicBlock());
  MF->insert(++OrigBB->getIterator(), NewBB);

  // Splice the instructions starting with MI over to NewBB.
  NewBB->splice(NewBB->end(), OrigBB, MI.getIterator(), OrigBB->end());

  // Add an unconditional branch from OrigBB to NewBB.
  // Note the new unconditional branch is not being recorded.
  // There doesn't seem to be meaningful DebugInfo available; this doesn't
  // correspond to anything in the source.
  TII->insertUnconditionalBranch(*OrigBB, NewBB, DebugLoc());

  // Insert an entry into BlockInfo to align it properly with the block numbers.
  BlockInfo.insert(BlockInfo.begin() + NewBB->getNumber(), BasicBlockInfo());

  NewBB->transferSuccessors(OrigBB);
  OrigBB->addSuccessor(NewBB);
  OrigBB->addSuccessor(DestBB);

  // Cleanup potential unconditional branch to successor block.
  // Note that updateTerminator may change the size of the blocks.
  NewBB->updateTerminator();
  OrigBB->updateTerminator();

  // Figure out how large the OrigBB is.  As the first half of the original
  // block, it cannot contain a tablejump.  The size includes
  // the new jump we added.  (It should be possible to do this without
  // recounting everything, but it's very confusing, and this is rarely
  // executed.)
  BlockInfo[OrigBB->getNumber()].Size = computeBlockSize(*OrigBB);

  // Figure out how large the NewMBB is. As the second half of the original
  // block, it may contain a tablejump.
  BlockInfo[NewBB->getNumber()].Size = computeBlockSize(*NewBB);

  // All BBOffsets following these blocks must be modified.
  adjustBlockOffsets(*OrigBB);

  // Need to fix live-in lists if we track liveness.
  if (TRI->trackLivenessAfterRegAlloc(*MF))
    computeAndAddLiveIns(LiveRegs, *NewBB);

  ++NumSplit;

  return NewBB;
}

/// isBlockInRange - Returns true if the distance between specific MI and
/// specific BB can fit in MI's displacement field.
bool BranchRelaxation::isBlockInRange(
  const MachineInstr &MI, const MachineBasicBlock &DestBB) const {
  int64_t BrOffset = getInstrOffset(MI);
  int64_t DestOffset = BlockInfo[DestBB.getNumber()].Offset;

  if (TII->isBranchOffsetInRange(MI.getOpcode(), DestOffset - BrOffset))
    return true;

  LLVM_DEBUG(dbgs() << "Out of range branch to destination "
                    << printMBBReference(DestBB) << " from "
                    << printMBBReference(*MI.getParent()) << " to "
                    << DestOffset << " offset " << DestOffset - BrOffset << '\t'
                    << MI);

  return false;
}

/// fixupConditionalBranch - Fix up a conditional branch whose destination is
/// too far away to fit in its displacement field. It is converted to an inverse
/// conditional branch + an unconditional branch to the destination.
bool BranchRelaxation::fixupConditionalBranch(MachineInstr &MI) {
  DebugLoc DL = MI.getDebugLoc();
  MachineBasicBlock *MBB = MI.getParent();
  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  MachineBasicBlock *NewBB = nullptr;
  SmallVector<MachineOperand, 4> Cond;

  auto insertUncondBranch = [&](MachineBasicBlock *MBB,
                                MachineBasicBlock *DestBB) {
    unsigned &BBSize = BlockInfo[MBB->getNumber()].Size;
    int NewBrSize = 0;
    TII->insertUnconditionalBranch(*MBB, DestBB, DL, &NewBrSize);
    BBSize += NewBrSize;
  };
  auto insertBranch = [&](MachineBasicBlock *MBB, MachineBasicBlock *TBB,
                          MachineBasicBlock *FBB,
                          SmallVectorImpl<MachineOperand>& Cond) {
    unsigned &BBSize = BlockInfo[MBB->getNumber()].Size;
    int NewBrSize = 0;
    TII->insertBranch(*MBB, TBB, FBB, Cond, DL, &NewBrSize);
    BBSize += NewBrSize;
  };
  auto removeBranch = [&](MachineBasicBlock *MBB) {
    unsigned &BBSize = BlockInfo[MBB->getNumber()].Size;
    int RemovedSize = 0;
    TII->removeBranch(*MBB, &RemovedSize);
    BBSize -= RemovedSize;
  };

  auto finalizeBlockChanges = [&](MachineBasicBlock *MBB,
                                  MachineBasicBlock *NewBB) {
    // Keep the block offsets up to date.
    adjustBlockOffsets(*MBB);

    // Need to fix live-in lists if we track liveness.
    if (NewBB && TRI->trackLivenessAfterRegAlloc(*MF))
      computeAndAddLiveIns(LiveRegs, *NewBB);
  };

  bool Fail = TII->analyzeBranch(*MBB, TBB, FBB, Cond);
  assert(!Fail && "branches to be relaxed must be analyzable");
  (void)Fail;

  // Add an unconditional branch to the destination and invert the branch
  // condition to jump over it:
  // tbz L1
  // =>
  // tbnz L2
  // b   L1
  // L2:

  bool ReversedCond = !TII->reverseBranchCondition(Cond);
  if (ReversedCond) {
    if (FBB && isBlockInRange(MI, *FBB)) {
      // Last MI in the BB is an unconditional branch. We can simply invert the
      // condition and swap destinations:
      // beq L1
      // b   L2
      // =>
      // bne L2
      // b   L1
      LLVM_DEBUG(dbgs() << "  Invert condition and swap "
                           "its destination with "
                        << MBB->back());

      removeBranch(MBB);
      insertBranch(MBB, FBB, TBB, Cond);
      finalizeBlockChanges(MBB, nullptr);
      return true;
    }
    if (FBB) {
      // We need to split the basic block here to obtain two long-range
      // unconditional branches.
      NewBB = createNewBlockAfter(*MBB);

      insertUncondBranch(NewBB, FBB);
      // Update the succesor lists according to the transformation to follow.
      // Do it here since if there's no split, no update is needed.
      MBB->replaceSuccessor(FBB, NewBB);
      NewBB->addSuccessor(FBB);
    }

    // We now have an appropriate fall-through block in place (either naturally or
    // just created), so we can use the inverted the condition.
    MachineBasicBlock &NextBB = *std::next(MachineFunction::iterator(MBB));

    LLVM_DEBUG(dbgs() << "  Insert B to " << printMBBReference(*TBB)
                      << ", invert condition and change dest. to "
                      << printMBBReference(NextBB) << '\n');

    removeBranch(MBB);
    // Insert a new conditional branch and a new unconditional branch.
    insertBranch(MBB, &NextBB, TBB, Cond);

    finalizeBlockChanges(MBB, NewBB);
    return true;
  }
  // Branch cond can't be inverted.
  // In this case we always add a block after the MBB.
  LLVM_DEBUG(dbgs() << "  The branch condition can't be inverted. "
                    << "  Insert a new BB after " << MBB->back());

  if (!FBB)
    FBB = &(*std::next(MachineFunction::iterator(MBB)));

  // This is the block with cond. branch and the distance to TBB is too long.
  //    beq L1
  // L2:

  // We do the following transformation:
  //    beq NewBB
  //    b L2
  // NewBB:
  //    b L1
  // L2:

  NewBB = createNewBlockAfter(*MBB);
  insertUncondBranch(NewBB, TBB);

  LLVM_DEBUG(dbgs() << "  Insert cond B to the new BB "
                    << printMBBReference(*NewBB)
                    << "  Keep the exiting condition.\n"
                    << "  Insert B to " << printMBBReference(*FBB) << ".\n"
                    << "  In the new BB: Insert B to "
                    << printMBBReference(*TBB) << ".\n");

  // Update the successor lists according to the transformation to follow.
  MBB->replaceSuccessor(TBB, NewBB);
  NewBB->addSuccessor(TBB);

  // Replace branch in the current (MBB) block.
  removeBranch(MBB);
  insertBranch(MBB, NewBB, FBB, Cond);

  finalizeBlockChanges(MBB, NewBB);
  return true;
}

bool BranchRelaxation::fixupUnconditionalBranch(MachineInstr &MI) {
  MachineBasicBlock *MBB = MI.getParent();

  unsigned OldBrSize = TII->getInstSizeInBytes(MI);
  MachineBasicBlock *DestBB = TII->getBranchDestBlock(MI);

  int64_t DestOffset = BlockInfo[DestBB->getNumber()].Offset;
  int64_t SrcOffset = getInstrOffset(MI);

  assert(!TII->isBranchOffsetInRange(MI.getOpcode(), DestOffset - SrcOffset));

  BlockInfo[MBB->getNumber()].Size -= OldBrSize;

  MachineBasicBlock *BranchBB = MBB;

  // If this was an expanded conditional branch, there is already a single
  // unconditional branch in a block.
  if (!MBB->empty()) {
    BranchBB = createNewBlockAfter(*MBB);

    // Add live outs.
    for (const MachineBasicBlock *Succ : MBB->successors()) {
      for (const MachineBasicBlock::RegisterMaskPair &LiveIn : Succ->liveins())
        BranchBB->addLiveIn(LiveIn);
    }

    BranchBB->sortUniqueLiveIns();
    BranchBB->addSuccessor(DestBB);
    MBB->replaceSuccessor(DestBB, BranchBB);
  }

  DebugLoc DL = MI.getDebugLoc();
  MI.eraseFromParent();
  BlockInfo[BranchBB->getNumber()].Size += TII->insertIndirectBranch(
    *BranchBB, *DestBB, DL, DestOffset - SrcOffset, RS.get());

  adjustBlockOffsets(*MBB);
  return true;
}

bool BranchRelaxation::relaxBranchInstructions() {
  bool Changed = false;

  // Relaxing branches involves creating new basic blocks, so re-eval
  // end() for termination.
  for (MachineFunction::iterator I = MF->begin(); I != MF->end(); ++I) {
    MachineBasicBlock &MBB = *I;

    // Empty block?
    MachineBasicBlock::iterator Last = MBB.getLastNonDebugInstr();
    if (Last == MBB.end())
      continue;

    // Expand the unconditional branch first if necessary. If there is a
    // conditional branch, this will end up changing the branch destination of
    // it to be over the newly inserted indirect branch block, which may avoid
    // the need to try expanding the conditional branch first, saving an extra
    // jump.
    if (Last->isUnconditionalBranch()) {
      // Unconditional branch destination might be unanalyzable, assume these
      // are OK.
      if (MachineBasicBlock *DestBB = TII->getBranchDestBlock(*Last)) {
        if (!isBlockInRange(*Last, *DestBB)) {
          fixupUnconditionalBranch(*Last);
          ++NumUnconditionalRelaxed;
          Changed = true;
        }
      }
    }

    // Loop over the conditional branches.
    MachineBasicBlock::iterator Next;
    for (MachineBasicBlock::iterator J = MBB.getFirstTerminator();
         J != MBB.end(); J = Next) {
      Next = std::next(J);
      MachineInstr &MI = *J;

      if (MI.isConditionalBranch()) {
        MachineBasicBlock *DestBB = TII->getBranchDestBlock(MI);
        if (!isBlockInRange(MI, *DestBB)) {
          if (Next != MBB.end() && Next->isConditionalBranch()) {
            // If there are multiple conditional branches, this isn't an
            // analyzable block. Split later terminators into a new block so
            // each one will be analyzable.

            splitBlockBeforeInstr(*Next, DestBB);
          } else {
            fixupConditionalBranch(MI);
            ++NumConditionalRelaxed;
          }

          Changed = true;

          // This may have modified all of the terminators, so start over.
          Next = MBB.getFirstTerminator();
        }
      }
    }
  }

  return Changed;
}

bool BranchRelaxation::runOnMachineFunction(MachineFunction &mf) {
  MF = &mf;

  LLVM_DEBUG(dbgs() << "***** BranchRelaxation *****\n");

  const TargetSubtargetInfo &ST = MF->getSubtarget();
  TII = ST.getInstrInfo();

  TRI = ST.getRegisterInfo();
  if (TRI->trackLivenessAfterRegAlloc(*MF))
    RS.reset(new RegScavenger());

  // Renumber all of the machine basic blocks in the function, guaranteeing that
  // the numbers agree with the position of the block in the function.
  MF->RenumberBlocks();

  // Do the initial scan of the function, building up information about the
  // sizes of each block.
  scanFunction();

  LLVM_DEBUG(dbgs() << "  Basic blocks before relaxation\n"; dumpBBs(););

  bool MadeChange = false;
  while (relaxBranchInstructions())
    MadeChange = true;

  // After a while, this might be made debug-only, but it is not expensive.
  verify();

  LLVM_DEBUG(dbgs() << "  Basic blocks after relaxation\n\n"; dumpBBs());

  BlockInfo.clear();

  return MadeChange;
}
