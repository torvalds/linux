//===- BreakCriticalEdges.cpp - Critical Edge Elimination Pass ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// BreakCriticalEdges pass - Break all of the critical edges in the CFG by
// inserting a dummy basic block.  This pass may be "required" by passes that
// cannot deal with critical edges.  For this usage, the structure type is
// forward declared.  This pass obviously invalidates the CFG, but can update
// dominator trees.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/BreakCriticalEdges.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
using namespace llvm;

#define DEBUG_TYPE "break-crit-edges"

STATISTIC(NumBroken, "Number of blocks inserted");

namespace {
  struct BreakCriticalEdges : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    BreakCriticalEdges() : FunctionPass(ID) {
      initializeBreakCriticalEdgesPass(*PassRegistry::getPassRegistry());
    }

    bool runOnFunction(Function &F) override {
      auto *DTWP = getAnalysisIfAvailable<DominatorTreeWrapperPass>();
      auto *DT = DTWP ? &DTWP->getDomTree() : nullptr;
      auto *LIWP = getAnalysisIfAvailable<LoopInfoWrapperPass>();
      auto *LI = LIWP ? &LIWP->getLoopInfo() : nullptr;
      unsigned N =
          SplitAllCriticalEdges(F, CriticalEdgeSplittingOptions(DT, LI));
      NumBroken += N;
      return N > 0;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addPreserved<DominatorTreeWrapperPass>();
      AU.addPreserved<LoopInfoWrapperPass>();

      // No loop canonicalization guarantees are broken by this pass.
      AU.addPreservedID(LoopSimplifyID);
    }
  };
}

char BreakCriticalEdges::ID = 0;
INITIALIZE_PASS(BreakCriticalEdges, "break-crit-edges",
                "Break critical edges in CFG", false, false)

// Publicly exposed interface to pass...
char &llvm::BreakCriticalEdgesID = BreakCriticalEdges::ID;
FunctionPass *llvm::createBreakCriticalEdgesPass() {
  return new BreakCriticalEdges();
}

PreservedAnalyses BreakCriticalEdgesPass::run(Function &F,
                                              FunctionAnalysisManager &AM) {
  auto *DT = AM.getCachedResult<DominatorTreeAnalysis>(F);
  auto *LI = AM.getCachedResult<LoopAnalysis>(F);
  unsigned N = SplitAllCriticalEdges(F, CriticalEdgeSplittingOptions(DT, LI));
  NumBroken += N;
  if (N == 0)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  PA.preserve<LoopAnalysis>();
  return PA;
}

//===----------------------------------------------------------------------===//
//    Implementation of the external critical edge manipulation functions
//===----------------------------------------------------------------------===//

/// When a loop exit edge is split, LCSSA form may require new PHIs in the new
/// exit block. This function inserts the new PHIs, as needed. Preds is a list
/// of preds inside the loop, SplitBB is the new loop exit block, and DestBB is
/// the old loop exit, now the successor of SplitBB.
static void createPHIsForSplitLoopExit(ArrayRef<BasicBlock *> Preds,
                                       BasicBlock *SplitBB,
                                       BasicBlock *DestBB) {
  // SplitBB shouldn't have anything non-trivial in it yet.
  assert((SplitBB->getFirstNonPHI() == SplitBB->getTerminator() ||
          SplitBB->isLandingPad()) && "SplitBB has non-PHI nodes!");

  // For each PHI in the destination block.
  for (PHINode &PN : DestBB->phis()) {
    unsigned Idx = PN.getBasicBlockIndex(SplitBB);
    Value *V = PN.getIncomingValue(Idx);

    // If the input is a PHI which already satisfies LCSSA, don't create
    // a new one.
    if (const PHINode *VP = dyn_cast<PHINode>(V))
      if (VP->getParent() == SplitBB)
        continue;

    // Otherwise a new PHI is needed. Create one and populate it.
    PHINode *NewPN = PHINode::Create(
        PN.getType(), Preds.size(), "split",
        SplitBB->isLandingPad() ? &SplitBB->front() : SplitBB->getTerminator());
    for (unsigned i = 0, e = Preds.size(); i != e; ++i)
      NewPN->addIncoming(V, Preds[i]);

    // Update the original PHI.
    PN.setIncomingValue(Idx, NewPN);
  }
}

BasicBlock *
llvm::SplitCriticalEdge(Instruction *TI, unsigned SuccNum,
                        const CriticalEdgeSplittingOptions &Options) {
  if (!isCriticalEdge(TI, SuccNum, Options.MergeIdenticalEdges))
    return nullptr;

  assert(!isa<IndirectBrInst>(TI) &&
         "Cannot split critical edge from IndirectBrInst");

  BasicBlock *TIBB = TI->getParent();
  BasicBlock *DestBB = TI->getSuccessor(SuccNum);

  // Splitting the critical edge to a pad block is non-trivial. Don't do
  // it in this generic function.
  if (DestBB->isEHPad()) return nullptr;

  // Create a new basic block, linking it into the CFG.
  BasicBlock *NewBB = BasicBlock::Create(TI->getContext(),
                      TIBB->getName() + "." + DestBB->getName() + "_crit_edge");
  // Create our unconditional branch.
  BranchInst *NewBI = BranchInst::Create(DestBB, NewBB);
  NewBI->setDebugLoc(TI->getDebugLoc());

  // Branch to the new block, breaking the edge.
  TI->setSuccessor(SuccNum, NewBB);

  // Insert the block into the function... right after the block TI lives in.
  Function &F = *TIBB->getParent();
  Function::iterator FBBI = TIBB->getIterator();
  F.getBasicBlockList().insert(++FBBI, NewBB);

  // If there are any PHI nodes in DestBB, we need to update them so that they
  // merge incoming values from NewBB instead of from TIBB.
  {
    unsigned BBIdx = 0;
    for (BasicBlock::iterator I = DestBB->begin(); isa<PHINode>(I); ++I) {
      // We no longer enter through TIBB, now we come in through NewBB.
      // Revector exactly one entry in the PHI node that used to come from
      // TIBB to come from NewBB.
      PHINode *PN = cast<PHINode>(I);

      // Reuse the previous value of BBIdx if it lines up.  In cases where we
      // have multiple phi nodes with *lots* of predecessors, this is a speed
      // win because we don't have to scan the PHI looking for TIBB.  This
      // happens because the BB list of PHI nodes are usually in the same
      // order.
      if (PN->getIncomingBlock(BBIdx) != TIBB)
        BBIdx = PN->getBasicBlockIndex(TIBB);
      PN->setIncomingBlock(BBIdx, NewBB);
    }
  }

  // If there are any other edges from TIBB to DestBB, update those to go
  // through the split block, making those edges non-critical as well (and
  // reducing the number of phi entries in the DestBB if relevant).
  if (Options.MergeIdenticalEdges) {
    for (unsigned i = SuccNum+1, e = TI->getNumSuccessors(); i != e; ++i) {
      if (TI->getSuccessor(i) != DestBB) continue;

      // Remove an entry for TIBB from DestBB phi nodes.
      DestBB->removePredecessor(TIBB, Options.DontDeleteUselessPHIs);

      // We found another edge to DestBB, go to NewBB instead.
      TI->setSuccessor(i, NewBB);
    }
  }

  // If we have nothing to update, just return.
  auto *DT = Options.DT;
  auto *LI = Options.LI;
  auto *MSSAU = Options.MSSAU;
  if (MSSAU)
    MSSAU->wireOldPredecessorsToNewImmediatePredecessor(
        DestBB, NewBB, {TIBB}, Options.MergeIdenticalEdges);

  if (!DT && !LI)
    return NewBB;

  if (DT) {
    // Update the DominatorTree.
    //       ---> NewBB -----\
    //      /                 V
    //  TIBB -------\\------> DestBB
    //
    // First, inform the DT about the new path from TIBB to DestBB via NewBB,
    // then delete the old edge from TIBB to DestBB. By doing this in that order
    // DestBB stays reachable in the DT the whole time and its subtree doesn't
    // get disconnected.
    SmallVector<DominatorTree::UpdateType, 3> Updates;
    Updates.push_back({DominatorTree::Insert, TIBB, NewBB});
    Updates.push_back({DominatorTree::Insert, NewBB, DestBB});
    if (llvm::find(successors(TIBB), DestBB) == succ_end(TIBB))
      Updates.push_back({DominatorTree::Delete, TIBB, DestBB});

    DT->applyUpdates(Updates);
  }

  // Update LoopInfo if it is around.
  if (LI) {
    if (Loop *TIL = LI->getLoopFor(TIBB)) {
      // If one or the other blocks were not in a loop, the new block is not
      // either, and thus LI doesn't need to be updated.
      if (Loop *DestLoop = LI->getLoopFor(DestBB)) {
        if (TIL == DestLoop) {
          // Both in the same loop, the NewBB joins loop.
          DestLoop->addBasicBlockToLoop(NewBB, *LI);
        } else if (TIL->contains(DestLoop)) {
          // Edge from an outer loop to an inner loop.  Add to the outer loop.
          TIL->addBasicBlockToLoop(NewBB, *LI);
        } else if (DestLoop->contains(TIL)) {
          // Edge from an inner loop to an outer loop.  Add to the outer loop.
          DestLoop->addBasicBlockToLoop(NewBB, *LI);
        } else {
          // Edge from two loops with no containment relation.  Because these
          // are natural loops, we know that the destination block must be the
          // header of its loop (adding a branch into a loop elsewhere would
          // create an irreducible loop).
          assert(DestLoop->getHeader() == DestBB &&
                 "Should not create irreducible loops!");
          if (Loop *P = DestLoop->getParentLoop())
            P->addBasicBlockToLoop(NewBB, *LI);
        }
      }

      // If TIBB is in a loop and DestBB is outside of that loop, we may need
      // to update LoopSimplify form and LCSSA form.
      if (!TIL->contains(DestBB)) {
        assert(!TIL->contains(NewBB) &&
               "Split point for loop exit is contained in loop!");

        // Update LCSSA form in the newly created exit block.
        if (Options.PreserveLCSSA) {
          createPHIsForSplitLoopExit(TIBB, NewBB, DestBB);
        }

        // The only that we can break LoopSimplify form by splitting a critical
        // edge is if after the split there exists some edge from TIL to DestBB
        // *and* the only edge into DestBB from outside of TIL is that of
        // NewBB. If the first isn't true, then LoopSimplify still holds, NewBB
        // is the new exit block and it has no non-loop predecessors. If the
        // second isn't true, then DestBB was not in LoopSimplify form prior to
        // the split as it had a non-loop predecessor. In both of these cases,
        // the predecessor must be directly in TIL, not in a subloop, or again
        // LoopSimplify doesn't hold.
        SmallVector<BasicBlock *, 4> LoopPreds;
        for (pred_iterator I = pred_begin(DestBB), E = pred_end(DestBB); I != E;
             ++I) {
          BasicBlock *P = *I;
          if (P == NewBB)
            continue; // The new block is known.
          if (LI->getLoopFor(P) != TIL) {
            // No need to re-simplify, it wasn't to start with.
            LoopPreds.clear();
            break;
          }
          LoopPreds.push_back(P);
        }
        if (!LoopPreds.empty()) {
          assert(!DestBB->isEHPad() && "We don't split edges to EH pads!");
          BasicBlock *NewExitBB = SplitBlockPredecessors(
              DestBB, LoopPreds, "split", DT, LI, MSSAU, Options.PreserveLCSSA);
          if (Options.PreserveLCSSA)
            createPHIsForSplitLoopExit(LoopPreds, NewExitBB, DestBB);
        }
      }
    }
  }

  return NewBB;
}

// Return the unique indirectbr predecessor of a block. This may return null
// even if such a predecessor exists, if it's not useful for splitting.
// If a predecessor is found, OtherPreds will contain all other (non-indirectbr)
// predecessors of BB.
static BasicBlock *
findIBRPredecessor(BasicBlock *BB, SmallVectorImpl<BasicBlock *> &OtherPreds) {
  // If the block doesn't have any PHIs, we don't care about it, since there's
  // no point in splitting it.
  PHINode *PN = dyn_cast<PHINode>(BB->begin());
  if (!PN)
    return nullptr;

  // Verify we have exactly one IBR predecessor.
  // Conservatively bail out if one of the other predecessors is not a "regular"
  // terminator (that is, not a switch or a br).
  BasicBlock *IBB = nullptr;
  for (unsigned Pred = 0, E = PN->getNumIncomingValues(); Pred != E; ++Pred) {
    BasicBlock *PredBB = PN->getIncomingBlock(Pred);
    Instruction *PredTerm = PredBB->getTerminator();
    switch (PredTerm->getOpcode()) {
    case Instruction::IndirectBr:
      if (IBB)
        return nullptr;
      IBB = PredBB;
      break;
    case Instruction::Br:
    case Instruction::Switch:
      OtherPreds.push_back(PredBB);
      continue;
    default:
      return nullptr;
    }
  }

  return IBB;
}

bool llvm::SplitIndirectBrCriticalEdges(Function &F,
                                        BranchProbabilityInfo *BPI,
                                        BlockFrequencyInfo *BFI) {
  // Check whether the function has any indirectbrs, and collect which blocks
  // they may jump to. Since most functions don't have indirect branches,
  // this lowers the common case's overhead to O(Blocks) instead of O(Edges).
  SmallSetVector<BasicBlock *, 16> Targets;
  for (auto &BB : F) {
    auto *IBI = dyn_cast<IndirectBrInst>(BB.getTerminator());
    if (!IBI)
      continue;

    for (unsigned Succ = 0, E = IBI->getNumSuccessors(); Succ != E; ++Succ)
      Targets.insert(IBI->getSuccessor(Succ));
  }

  if (Targets.empty())
    return false;

  bool ShouldUpdateAnalysis = BPI && BFI;
  bool Changed = false;
  for (BasicBlock *Target : Targets) {
    SmallVector<BasicBlock *, 16> OtherPreds;
    BasicBlock *IBRPred = findIBRPredecessor(Target, OtherPreds);
    // If we did not found an indirectbr, or the indirectbr is the only
    // incoming edge, this isn't the kind of edge we're looking for.
    if (!IBRPred || OtherPreds.empty())
      continue;

    // Don't even think about ehpads/landingpads.
    Instruction *FirstNonPHI = Target->getFirstNonPHI();
    if (FirstNonPHI->isEHPad() || Target->isLandingPad())
      continue;

    BasicBlock *BodyBlock = Target->splitBasicBlock(FirstNonPHI, ".split");
    if (ShouldUpdateAnalysis) {
      // Copy the BFI/BPI from Target to BodyBlock.
      for (unsigned I = 0, E = BodyBlock->getTerminator()->getNumSuccessors();
           I < E; ++I)
        BPI->setEdgeProbability(BodyBlock, I,
                                BPI->getEdgeProbability(Target, I));
      BFI->setBlockFreq(BodyBlock, BFI->getBlockFreq(Target).getFrequency());
    }
    // It's possible Target was its own successor through an indirectbr.
    // In this case, the indirectbr now comes from BodyBlock.
    if (IBRPred == Target)
      IBRPred = BodyBlock;

    // At this point Target only has PHIs, and BodyBlock has the rest of the
    // block's body. Create a copy of Target that will be used by the "direct"
    // preds.
    ValueToValueMapTy VMap;
    BasicBlock *DirectSucc = CloneBasicBlock(Target, VMap, ".clone", &F);

    BlockFrequency BlockFreqForDirectSucc;
    for (BasicBlock *Pred : OtherPreds) {
      // If the target is a loop to itself, then the terminator of the split
      // block (BodyBlock) needs to be updated.
      BasicBlock *Src = Pred != Target ? Pred : BodyBlock;
      Src->getTerminator()->replaceUsesOfWith(Target, DirectSucc);
      if (ShouldUpdateAnalysis)
        BlockFreqForDirectSucc += BFI->getBlockFreq(Src) *
            BPI->getEdgeProbability(Src, DirectSucc);
    }
    if (ShouldUpdateAnalysis) {
      BFI->setBlockFreq(DirectSucc, BlockFreqForDirectSucc.getFrequency());
      BlockFrequency NewBlockFreqForTarget =
          BFI->getBlockFreq(Target) - BlockFreqForDirectSucc;
      BFI->setBlockFreq(Target, NewBlockFreqForTarget.getFrequency());
      BPI->eraseBlock(Target);
    }

    // Ok, now fix up the PHIs. We know the two blocks only have PHIs, and that
    // they are clones, so the number of PHIs are the same.
    // (a) Remove the edge coming from IBRPred from the "Direct" PHI
    // (b) Leave that as the only edge in the "Indirect" PHI.
    // (c) Merge the two in the body block.
    BasicBlock::iterator Indirect = Target->begin(),
                         End = Target->getFirstNonPHI()->getIterator();
    BasicBlock::iterator Direct = DirectSucc->begin();
    BasicBlock::iterator MergeInsert = BodyBlock->getFirstInsertionPt();

    assert(&*End == Target->getTerminator() &&
           "Block was expected to only contain PHIs");

    while (Indirect != End) {
      PHINode *DirPHI = cast<PHINode>(Direct);
      PHINode *IndPHI = cast<PHINode>(Indirect);

      // Now, clean up - the direct block shouldn't get the indirect value,
      // and vice versa.
      DirPHI->removeIncomingValue(IBRPred);
      Direct++;

      // Advance the pointer here, to avoid invalidation issues when the old
      // PHI is erased.
      Indirect++;

      PHINode *NewIndPHI = PHINode::Create(IndPHI->getType(), 1, "ind", IndPHI);
      NewIndPHI->addIncoming(IndPHI->getIncomingValueForBlock(IBRPred),
                             IBRPred);

      // Create a PHI in the body block, to merge the direct and indirect
      // predecessors.
      PHINode *MergePHI =
          PHINode::Create(IndPHI->getType(), 2, "merge", &*MergeInsert);
      MergePHI->addIncoming(NewIndPHI, Target);
      MergePHI->addIncoming(DirPHI, DirectSucc);

      IndPHI->replaceAllUsesWith(MergePHI);
      IndPHI->eraseFromParent();
    }

    Changed = true;
  }

  return Changed;
}
