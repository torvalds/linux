//===- BasicBlockUtils.cpp - BasicBlock Utilities --------------------------==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This family of functions perform manipulations on basic blocks, and
// instructions contained within basic blocks.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DomTreeUpdater.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Casting.h"
#include "llvm/Transforms/Utils/Local.h"
#include <cassert>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

void llvm::DeleteDeadBlock(BasicBlock *BB, DomTreeUpdater *DTU) {
  SmallVector<BasicBlock *, 1> BBs = {BB};
  DeleteDeadBlocks(BBs, DTU);
}

void llvm::DeleteDeadBlocks(SmallVectorImpl <BasicBlock *> &BBs,
                            DomTreeUpdater *DTU) {
#ifndef NDEBUG
  // Make sure that all predecessors of each dead block is also dead.
  SmallPtrSet<BasicBlock *, 4> Dead(BBs.begin(), BBs.end());
  assert(Dead.size() == BBs.size() && "Duplicating blocks?");
  for (auto *BB : Dead)
    for (BasicBlock *Pred : predecessors(BB))
      assert(Dead.count(Pred) && "All predecessors must be dead!");
#endif

  SmallVector<DominatorTree::UpdateType, 4> Updates;
  for (auto *BB : BBs) {
    // Loop through all of our successors and make sure they know that one
    // of their predecessors is going away.
    for (BasicBlock *Succ : successors(BB)) {
      Succ->removePredecessor(BB);
      if (DTU)
        Updates.push_back({DominatorTree::Delete, BB, Succ});
    }

    // Zap all the instructions in the block.
    while (!BB->empty()) {
      Instruction &I = BB->back();
      // If this instruction is used, replace uses with an arbitrary value.
      // Because control flow can't get here, we don't care what we replace the
      // value with.  Note that since this block is unreachable, and all values
      // contained within it must dominate their uses, that all uses will
      // eventually be removed (they are themselves dead).
      if (!I.use_empty())
        I.replaceAllUsesWith(UndefValue::get(I.getType()));
      BB->getInstList().pop_back();
    }
    new UnreachableInst(BB->getContext(), BB);
    assert(BB->getInstList().size() == 1 &&
           isa<UnreachableInst>(BB->getTerminator()) &&
           "The successor list of BB isn't empty before "
           "applying corresponding DTU updates.");
  }
  if (DTU)
    DTU->applyUpdates(Updates, /*ForceRemoveDuplicates*/ true);

  for (BasicBlock *BB : BBs)
    if (DTU)
      DTU->deleteBB(BB);
    else
      BB->eraseFromParent();
}

void llvm::FoldSingleEntryPHINodes(BasicBlock *BB,
                                   MemoryDependenceResults *MemDep) {
  if (!isa<PHINode>(BB->begin())) return;

  while (PHINode *PN = dyn_cast<PHINode>(BB->begin())) {
    if (PN->getIncomingValue(0) != PN)
      PN->replaceAllUsesWith(PN->getIncomingValue(0));
    else
      PN->replaceAllUsesWith(UndefValue::get(PN->getType()));

    if (MemDep)
      MemDep->removeInstruction(PN);  // Memdep updates AA itself.

    PN->eraseFromParent();
  }
}

bool llvm::DeleteDeadPHIs(BasicBlock *BB, const TargetLibraryInfo *TLI) {
  // Recursively deleting a PHI may cause multiple PHIs to be deleted
  // or RAUW'd undef, so use an array of WeakTrackingVH for the PHIs to delete.
  SmallVector<WeakTrackingVH, 8> PHIs;
  for (PHINode &PN : BB->phis())
    PHIs.push_back(&PN);

  bool Changed = false;
  for (unsigned i = 0, e = PHIs.size(); i != e; ++i)
    if (PHINode *PN = dyn_cast_or_null<PHINode>(PHIs[i].operator Value*()))
      Changed |= RecursivelyDeleteDeadPHINode(PN, TLI);

  return Changed;
}

bool llvm::MergeBlockIntoPredecessor(BasicBlock *BB, DomTreeUpdater *DTU,
                                     LoopInfo *LI, MemorySSAUpdater *MSSAU,
                                     MemoryDependenceResults *MemDep) {
  if (BB->hasAddressTaken())
    return false;

  // Can't merge if there are multiple predecessors, or no predecessors.
  BasicBlock *PredBB = BB->getUniquePredecessor();
  if (!PredBB) return false;

  // Don't break self-loops.
  if (PredBB == BB) return false;
  // Don't break unwinding instructions.
  if (PredBB->getTerminator()->isExceptionalTerminator())
    return false;

  // Can't merge if there are multiple distinct successors.
  if (PredBB->getUniqueSuccessor() != BB)
    return false;

  // Can't merge if there is PHI loop.
  for (PHINode &PN : BB->phis())
    for (Value *IncValue : PN.incoming_values())
      if (IncValue == &PN)
        return false;

  // Begin by getting rid of unneeded PHIs.
  SmallVector<AssertingVH<Value>, 4> IncomingValues;
  if (isa<PHINode>(BB->front())) {
    for (PHINode &PN : BB->phis())
      if (!isa<PHINode>(PN.getIncomingValue(0)) ||
          cast<PHINode>(PN.getIncomingValue(0))->getParent() != BB)
        IncomingValues.push_back(PN.getIncomingValue(0));
    FoldSingleEntryPHINodes(BB, MemDep);
  }

  // DTU update: Collect all the edges that exit BB.
  // These dominator edges will be redirected from Pred.
  std::vector<DominatorTree::UpdateType> Updates;
  if (DTU) {
    Updates.reserve(1 + (2 * succ_size(BB)));
    Updates.push_back({DominatorTree::Delete, PredBB, BB});
    for (auto I = succ_begin(BB), E = succ_end(BB); I != E; ++I) {
      Updates.push_back({DominatorTree::Delete, BB, *I});
      Updates.push_back({DominatorTree::Insert, PredBB, *I});
    }
  }

  if (MSSAU)
    MSSAU->moveAllAfterMergeBlocks(BB, PredBB, &*(BB->begin()));

  // Delete the unconditional branch from the predecessor...
  PredBB->getInstList().pop_back();

  // Make all PHI nodes that referred to BB now refer to Pred as their
  // source...
  BB->replaceAllUsesWith(PredBB);

  // Move all definitions in the successor to the predecessor...
  PredBB->getInstList().splice(PredBB->end(), BB->getInstList());
  new UnreachableInst(BB->getContext(), BB);

  // Eliminate duplicate dbg.values describing the entry PHI node post-splice.
  for (auto Incoming : IncomingValues) {
    if (isa<Instruction>(*Incoming)) {
      SmallVector<DbgValueInst *, 2> DbgValues;
      SmallDenseSet<std::pair<DILocalVariable *, DIExpression *>, 2>
          DbgValueSet;
      llvm::findDbgValues(DbgValues, Incoming);
      for (auto &DVI : DbgValues) {
        auto R = DbgValueSet.insert({DVI->getVariable(), DVI->getExpression()});
        if (!R.second)
          DVI->eraseFromParent();
      }
    }
  }

  // Inherit predecessors name if it exists.
  if (!PredBB->hasName())
    PredBB->takeName(BB);

  if (LI)
    LI->removeBlock(BB);

  if (MemDep)
    MemDep->invalidateCachedPredecessors();

  // Finally, erase the old block and update dominator info.
  if (DTU) {
    assert(BB->getInstList().size() == 1 &&
           isa<UnreachableInst>(BB->getTerminator()) &&
           "The successor list of BB isn't empty before "
           "applying corresponding DTU updates.");
    DTU->applyUpdates(Updates, /*ForceRemoveDuplicates*/ true);
    DTU->deleteBB(BB);
  }

  else {
    BB->eraseFromParent(); // Nuke BB if DTU is nullptr.
  }
  return true;
}

void llvm::ReplaceInstWithValue(BasicBlock::InstListType &BIL,
                                BasicBlock::iterator &BI, Value *V) {
  Instruction &I = *BI;
  // Replaces all of the uses of the instruction with uses of the value
  I.replaceAllUsesWith(V);

  // Make sure to propagate a name if there is one already.
  if (I.hasName() && !V->hasName())
    V->takeName(&I);

  // Delete the unnecessary instruction now...
  BI = BIL.erase(BI);
}

void llvm::ReplaceInstWithInst(BasicBlock::InstListType &BIL,
                               BasicBlock::iterator &BI, Instruction *I) {
  assert(I->getParent() == nullptr &&
         "ReplaceInstWithInst: Instruction already inserted into basic block!");

  // Copy debug location to newly added instruction, if it wasn't already set
  // by the caller.
  if (!I->getDebugLoc())
    I->setDebugLoc(BI->getDebugLoc());

  // Insert the new instruction into the basic block...
  BasicBlock::iterator New = BIL.insert(BI, I);

  // Replace all uses of the old instruction, and delete it.
  ReplaceInstWithValue(BIL, BI, I);

  // Move BI back to point to the newly inserted instruction
  BI = New;
}

void llvm::ReplaceInstWithInst(Instruction *From, Instruction *To) {
  BasicBlock::iterator BI(From);
  ReplaceInstWithInst(From->getParent()->getInstList(), BI, To);
}

BasicBlock *llvm::SplitEdge(BasicBlock *BB, BasicBlock *Succ, DominatorTree *DT,
                            LoopInfo *LI, MemorySSAUpdater *MSSAU) {
  unsigned SuccNum = GetSuccessorNumber(BB, Succ);

  // If this is a critical edge, let SplitCriticalEdge do it.
  Instruction *LatchTerm = BB->getTerminator();
  if (SplitCriticalEdge(
          LatchTerm, SuccNum,
          CriticalEdgeSplittingOptions(DT, LI, MSSAU).setPreserveLCSSA()))
    return LatchTerm->getSuccessor(SuccNum);

  // If the edge isn't critical, then BB has a single successor or Succ has a
  // single pred.  Split the block.
  if (BasicBlock *SP = Succ->getSinglePredecessor()) {
    // If the successor only has a single pred, split the top of the successor
    // block.
    assert(SP == BB && "CFG broken");
    SP = nullptr;
    return SplitBlock(Succ, &Succ->front(), DT, LI, MSSAU);
  }

  // Otherwise, if BB has a single successor, split it at the bottom of the
  // block.
  assert(BB->getTerminator()->getNumSuccessors() == 1 &&
         "Should have a single succ!");
  return SplitBlock(BB, BB->getTerminator(), DT, LI, MSSAU);
}

unsigned
llvm::SplitAllCriticalEdges(Function &F,
                            const CriticalEdgeSplittingOptions &Options) {
  unsigned NumBroken = 0;
  for (BasicBlock &BB : F) {
    Instruction *TI = BB.getTerminator();
    if (TI->getNumSuccessors() > 1 && !isa<IndirectBrInst>(TI))
      for (unsigned i = 0, e = TI->getNumSuccessors(); i != e; ++i)
        if (SplitCriticalEdge(TI, i, Options))
          ++NumBroken;
  }
  return NumBroken;
}

BasicBlock *llvm::SplitBlock(BasicBlock *Old, Instruction *SplitPt,
                             DominatorTree *DT, LoopInfo *LI,
                             MemorySSAUpdater *MSSAU) {
  BasicBlock::iterator SplitIt = SplitPt->getIterator();
  while (isa<PHINode>(SplitIt) || SplitIt->isEHPad())
    ++SplitIt;
  BasicBlock *New = Old->splitBasicBlock(SplitIt, Old->getName()+".split");

  // The new block lives in whichever loop the old one did. This preserves
  // LCSSA as well, because we force the split point to be after any PHI nodes.
  if (LI)
    if (Loop *L = LI->getLoopFor(Old))
      L->addBasicBlockToLoop(New, *LI);

  if (DT)
    // Old dominates New. New node dominates all other nodes dominated by Old.
    if (DomTreeNode *OldNode = DT->getNode(Old)) {
      std::vector<DomTreeNode *> Children(OldNode->begin(), OldNode->end());

      DomTreeNode *NewNode = DT->addNewBlock(New, Old);
      for (DomTreeNode *I : Children)
        DT->changeImmediateDominator(I, NewNode);
    }

  // Move MemoryAccesses still tracked in Old, but part of New now.
  // Update accesses in successor blocks accordingly.
  if (MSSAU)
    MSSAU->moveAllAfterSpliceBlocks(Old, New, &*(New->begin()));

  return New;
}

/// Update DominatorTree, LoopInfo, and LCCSA analysis information.
static void UpdateAnalysisInformation(BasicBlock *OldBB, BasicBlock *NewBB,
                                      ArrayRef<BasicBlock *> Preds,
                                      DominatorTree *DT, LoopInfo *LI,
                                      MemorySSAUpdater *MSSAU,
                                      bool PreserveLCSSA, bool &HasLoopExit) {
  // Update dominator tree if available.
  if (DT) {
    if (OldBB == DT->getRootNode()->getBlock()) {
      assert(NewBB == &NewBB->getParent()->getEntryBlock());
      DT->setNewRoot(NewBB);
    } else {
      // Split block expects NewBB to have a non-empty set of predecessors.
      DT->splitBlock(NewBB);
    }
  }

  // Update MemoryPhis after split if MemorySSA is available
  if (MSSAU)
    MSSAU->wireOldPredecessorsToNewImmediatePredecessor(OldBB, NewBB, Preds);

  // The rest of the logic is only relevant for updating the loop structures.
  if (!LI)
    return;

  assert(DT && "DT should be available to update LoopInfo!");
  Loop *L = LI->getLoopFor(OldBB);

  // If we need to preserve loop analyses, collect some information about how
  // this split will affect loops.
  bool IsLoopEntry = !!L;
  bool SplitMakesNewLoopHeader = false;
  for (BasicBlock *Pred : Preds) {
    // Preds that are not reachable from entry should not be used to identify if
    // OldBB is a loop entry or if SplitMakesNewLoopHeader. Unreachable blocks
    // are not within any loops, so we incorrectly mark SplitMakesNewLoopHeader
    // as true and make the NewBB the header of some loop. This breaks LI.
    if (!DT->isReachableFromEntry(Pred))
      continue;
    // If we need to preserve LCSSA, determine if any of the preds is a loop
    // exit.
    if (PreserveLCSSA)
      if (Loop *PL = LI->getLoopFor(Pred))
        if (!PL->contains(OldBB))
          HasLoopExit = true;

    // If we need to preserve LoopInfo, note whether any of the preds crosses
    // an interesting loop boundary.
    if (!L)
      continue;
    if (L->contains(Pred))
      IsLoopEntry = false;
    else
      SplitMakesNewLoopHeader = true;
  }

  // Unless we have a loop for OldBB, nothing else to do here.
  if (!L)
    return;

  if (IsLoopEntry) {
    // Add the new block to the nearest enclosing loop (and not an adjacent
    // loop). To find this, examine each of the predecessors and determine which
    // loops enclose them, and select the most-nested loop which contains the
    // loop containing the block being split.
    Loop *InnermostPredLoop = nullptr;
    for (BasicBlock *Pred : Preds) {
      if (Loop *PredLoop = LI->getLoopFor(Pred)) {
        // Seek a loop which actually contains the block being split (to avoid
        // adjacent loops).
        while (PredLoop && !PredLoop->contains(OldBB))
          PredLoop = PredLoop->getParentLoop();

        // Select the most-nested of these loops which contains the block.
        if (PredLoop && PredLoop->contains(OldBB) &&
            (!InnermostPredLoop ||
             InnermostPredLoop->getLoopDepth() < PredLoop->getLoopDepth()))
          InnermostPredLoop = PredLoop;
      }
    }

    if (InnermostPredLoop)
      InnermostPredLoop->addBasicBlockToLoop(NewBB, *LI);
  } else {
    L->addBasicBlockToLoop(NewBB, *LI);
    if (SplitMakesNewLoopHeader)
      L->moveToHeader(NewBB);
  }
}

/// Update the PHI nodes in OrigBB to include the values coming from NewBB.
/// This also updates AliasAnalysis, if available.
static void UpdatePHINodes(BasicBlock *OrigBB, BasicBlock *NewBB,
                           ArrayRef<BasicBlock *> Preds, BranchInst *BI,
                           bool HasLoopExit) {
  // Otherwise, create a new PHI node in NewBB for each PHI node in OrigBB.
  SmallPtrSet<BasicBlock *, 16> PredSet(Preds.begin(), Preds.end());
  for (BasicBlock::iterator I = OrigBB->begin(); isa<PHINode>(I); ) {
    PHINode *PN = cast<PHINode>(I++);

    // Check to see if all of the values coming in are the same.  If so, we
    // don't need to create a new PHI node, unless it's needed for LCSSA.
    Value *InVal = nullptr;
    if (!HasLoopExit) {
      InVal = PN->getIncomingValueForBlock(Preds[0]);
      for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
        if (!PredSet.count(PN->getIncomingBlock(i)))
          continue;
        if (!InVal)
          InVal = PN->getIncomingValue(i);
        else if (InVal != PN->getIncomingValue(i)) {
          InVal = nullptr;
          break;
        }
      }
    }

    if (InVal) {
      // If all incoming values for the new PHI would be the same, just don't
      // make a new PHI.  Instead, just remove the incoming values from the old
      // PHI.

      // NOTE! This loop walks backwards for a reason! First off, this minimizes
      // the cost of removal if we end up removing a large number of values, and
      // second off, this ensures that the indices for the incoming values
      // aren't invalidated when we remove one.
      for (int64_t i = PN->getNumIncomingValues() - 1; i >= 0; --i)
        if (PredSet.count(PN->getIncomingBlock(i)))
          PN->removeIncomingValue(i, false);

      // Add an incoming value to the PHI node in the loop for the preheader
      // edge.
      PN->addIncoming(InVal, NewBB);
      continue;
    }

    // If the values coming into the block are not the same, we need a new
    // PHI.
    // Create the new PHI node, insert it into NewBB at the end of the block
    PHINode *NewPHI =
        PHINode::Create(PN->getType(), Preds.size(), PN->getName() + ".ph", BI);

    // NOTE! This loop walks backwards for a reason! First off, this minimizes
    // the cost of removal if we end up removing a large number of values, and
    // second off, this ensures that the indices for the incoming values aren't
    // invalidated when we remove one.
    for (int64_t i = PN->getNumIncomingValues() - 1; i >= 0; --i) {
      BasicBlock *IncomingBB = PN->getIncomingBlock(i);
      if (PredSet.count(IncomingBB)) {
        Value *V = PN->removeIncomingValue(i, false);
        NewPHI->addIncoming(V, IncomingBB);
      }
    }

    PN->addIncoming(NewPHI, NewBB);
  }
}

BasicBlock *llvm::SplitBlockPredecessors(BasicBlock *BB,
                                         ArrayRef<BasicBlock *> Preds,
                                         const char *Suffix, DominatorTree *DT,
                                         LoopInfo *LI, MemorySSAUpdater *MSSAU,
                                         bool PreserveLCSSA) {
  // Do not attempt to split that which cannot be split.
  if (!BB->canSplitPredecessors())
    return nullptr;

  // For the landingpads we need to act a bit differently.
  // Delegate this work to the SplitLandingPadPredecessors.
  if (BB->isLandingPad()) {
    SmallVector<BasicBlock*, 2> NewBBs;
    std::string NewName = std::string(Suffix) + ".split-lp";

    SplitLandingPadPredecessors(BB, Preds, Suffix, NewName.c_str(), NewBBs, DT,
                                LI, MSSAU, PreserveLCSSA);
    return NewBBs[0];
  }

  // Create new basic block, insert right before the original block.
  BasicBlock *NewBB = BasicBlock::Create(
      BB->getContext(), BB->getName() + Suffix, BB->getParent(), BB);

  // The new block unconditionally branches to the old block.
  BranchInst *BI = BranchInst::Create(BB, NewBB);
  BI->setDebugLoc(BB->getFirstNonPHIOrDbg()->getDebugLoc());

  // Move the edges from Preds to point to NewBB instead of BB.
  for (unsigned i = 0, e = Preds.size(); i != e; ++i) {
    // This is slightly more strict than necessary; the minimum requirement
    // is that there be no more than one indirectbr branching to BB. And
    // all BlockAddress uses would need to be updated.
    assert(!isa<IndirectBrInst>(Preds[i]->getTerminator()) &&
           "Cannot split an edge from an IndirectBrInst");
    Preds[i]->getTerminator()->replaceUsesOfWith(BB, NewBB);
  }

  // Insert a new PHI node into NewBB for every PHI node in BB and that new PHI
  // node becomes an incoming value for BB's phi node.  However, if the Preds
  // list is empty, we need to insert dummy entries into the PHI nodes in BB to
  // account for the newly created predecessor.
  if (Preds.empty()) {
    // Insert dummy values as the incoming value.
    for (BasicBlock::iterator I = BB->begin(); isa<PHINode>(I); ++I)
      cast<PHINode>(I)->addIncoming(UndefValue::get(I->getType()), NewBB);
  }

  // Update DominatorTree, LoopInfo, and LCCSA analysis information.
  bool HasLoopExit = false;
  UpdateAnalysisInformation(BB, NewBB, Preds, DT, LI, MSSAU, PreserveLCSSA,
                            HasLoopExit);

  if (!Preds.empty()) {
    // Update the PHI nodes in BB with the values coming from NewBB.
    UpdatePHINodes(BB, NewBB, Preds, BI, HasLoopExit);
  }

  return NewBB;
}

void llvm::SplitLandingPadPredecessors(BasicBlock *OrigBB,
                                       ArrayRef<BasicBlock *> Preds,
                                       const char *Suffix1, const char *Suffix2,
                                       SmallVectorImpl<BasicBlock *> &NewBBs,
                                       DominatorTree *DT, LoopInfo *LI,
                                       MemorySSAUpdater *MSSAU,
                                       bool PreserveLCSSA) {
  assert(OrigBB->isLandingPad() && "Trying to split a non-landing pad!");

  // Create a new basic block for OrigBB's predecessors listed in Preds. Insert
  // it right before the original block.
  BasicBlock *NewBB1 = BasicBlock::Create(OrigBB->getContext(),
                                          OrigBB->getName() + Suffix1,
                                          OrigBB->getParent(), OrigBB);
  NewBBs.push_back(NewBB1);

  // The new block unconditionally branches to the old block.
  BranchInst *BI1 = BranchInst::Create(OrigBB, NewBB1);
  BI1->setDebugLoc(OrigBB->getFirstNonPHI()->getDebugLoc());

  // Move the edges from Preds to point to NewBB1 instead of OrigBB.
  for (unsigned i = 0, e = Preds.size(); i != e; ++i) {
    // This is slightly more strict than necessary; the minimum requirement
    // is that there be no more than one indirectbr branching to BB. And
    // all BlockAddress uses would need to be updated.
    assert(!isa<IndirectBrInst>(Preds[i]->getTerminator()) &&
           "Cannot split an edge from an IndirectBrInst");
    Preds[i]->getTerminator()->replaceUsesOfWith(OrigBB, NewBB1);
  }

  bool HasLoopExit = false;
  UpdateAnalysisInformation(OrigBB, NewBB1, Preds, DT, LI, MSSAU, PreserveLCSSA,
                            HasLoopExit);

  // Update the PHI nodes in OrigBB with the values coming from NewBB1.
  UpdatePHINodes(OrigBB, NewBB1, Preds, BI1, HasLoopExit);

  // Move the remaining edges from OrigBB to point to NewBB2.
  SmallVector<BasicBlock*, 8> NewBB2Preds;
  for (pred_iterator i = pred_begin(OrigBB), e = pred_end(OrigBB);
       i != e; ) {
    BasicBlock *Pred = *i++;
    if (Pred == NewBB1) continue;
    assert(!isa<IndirectBrInst>(Pred->getTerminator()) &&
           "Cannot split an edge from an IndirectBrInst");
    NewBB2Preds.push_back(Pred);
    e = pred_end(OrigBB);
  }

  BasicBlock *NewBB2 = nullptr;
  if (!NewBB2Preds.empty()) {
    // Create another basic block for the rest of OrigBB's predecessors.
    NewBB2 = BasicBlock::Create(OrigBB->getContext(),
                                OrigBB->getName() + Suffix2,
                                OrigBB->getParent(), OrigBB);
    NewBBs.push_back(NewBB2);

    // The new block unconditionally branches to the old block.
    BranchInst *BI2 = BranchInst::Create(OrigBB, NewBB2);
    BI2->setDebugLoc(OrigBB->getFirstNonPHI()->getDebugLoc());

    // Move the remaining edges from OrigBB to point to NewBB2.
    for (BasicBlock *NewBB2Pred : NewBB2Preds)
      NewBB2Pred->getTerminator()->replaceUsesOfWith(OrigBB, NewBB2);

    // Update DominatorTree, LoopInfo, and LCCSA analysis information.
    HasLoopExit = false;
    UpdateAnalysisInformation(OrigBB, NewBB2, NewBB2Preds, DT, LI, MSSAU,
                              PreserveLCSSA, HasLoopExit);

    // Update the PHI nodes in OrigBB with the values coming from NewBB2.
    UpdatePHINodes(OrigBB, NewBB2, NewBB2Preds, BI2, HasLoopExit);
  }

  LandingPadInst *LPad = OrigBB->getLandingPadInst();
  Instruction *Clone1 = LPad->clone();
  Clone1->setName(Twine("lpad") + Suffix1);
  NewBB1->getInstList().insert(NewBB1->getFirstInsertionPt(), Clone1);

  if (NewBB2) {
    Instruction *Clone2 = LPad->clone();
    Clone2->setName(Twine("lpad") + Suffix2);
    NewBB2->getInstList().insert(NewBB2->getFirstInsertionPt(), Clone2);

    // Create a PHI node for the two cloned landingpad instructions only
    // if the original landingpad instruction has some uses.
    if (!LPad->use_empty()) {
      assert(!LPad->getType()->isTokenTy() &&
             "Split cannot be applied if LPad is token type. Otherwise an "
             "invalid PHINode of token type would be created.");
      PHINode *PN = PHINode::Create(LPad->getType(), 2, "lpad.phi", LPad);
      PN->addIncoming(Clone1, NewBB1);
      PN->addIncoming(Clone2, NewBB2);
      LPad->replaceAllUsesWith(PN);
    }
    LPad->eraseFromParent();
  } else {
    // There is no second clone. Just replace the landing pad with the first
    // clone.
    LPad->replaceAllUsesWith(Clone1);
    LPad->eraseFromParent();
  }
}

ReturnInst *llvm::FoldReturnIntoUncondBranch(ReturnInst *RI, BasicBlock *BB,
                                             BasicBlock *Pred,
                                             DomTreeUpdater *DTU) {
  Instruction *UncondBranch = Pred->getTerminator();
  // Clone the return and add it to the end of the predecessor.
  Instruction *NewRet = RI->clone();
  Pred->getInstList().push_back(NewRet);

  // If the return instruction returns a value, and if the value was a
  // PHI node in "BB", propagate the right value into the return.
  for (User::op_iterator i = NewRet->op_begin(), e = NewRet->op_end();
       i != e; ++i) {
    Value *V = *i;
    Instruction *NewBC = nullptr;
    if (BitCastInst *BCI = dyn_cast<BitCastInst>(V)) {
      // Return value might be bitcasted. Clone and insert it before the
      // return instruction.
      V = BCI->getOperand(0);
      NewBC = BCI->clone();
      Pred->getInstList().insert(NewRet->getIterator(), NewBC);
      *i = NewBC;
    }
    if (PHINode *PN = dyn_cast<PHINode>(V)) {
      if (PN->getParent() == BB) {
        if (NewBC)
          NewBC->setOperand(0, PN->getIncomingValueForBlock(Pred));
        else
          *i = PN->getIncomingValueForBlock(Pred);
      }
    }
  }

  // Update any PHI nodes in the returning block to realize that we no
  // longer branch to them.
  BB->removePredecessor(Pred);
  UncondBranch->eraseFromParent();

  if (DTU)
    DTU->deleteEdge(Pred, BB);

  return cast<ReturnInst>(NewRet);
}

Instruction *llvm::SplitBlockAndInsertIfThen(Value *Cond,
                                             Instruction *SplitBefore,
                                             bool Unreachable,
                                             MDNode *BranchWeights,
                                             DominatorTree *DT, LoopInfo *LI) {
  BasicBlock *Head = SplitBefore->getParent();
  BasicBlock *Tail = Head->splitBasicBlock(SplitBefore->getIterator());
  Instruction *HeadOldTerm = Head->getTerminator();
  LLVMContext &C = Head->getContext();
  BasicBlock *ThenBlock = BasicBlock::Create(C, "", Head->getParent(), Tail);
  Instruction *CheckTerm;
  if (Unreachable)
    CheckTerm = new UnreachableInst(C, ThenBlock);
  else
    CheckTerm = BranchInst::Create(Tail, ThenBlock);
  CheckTerm->setDebugLoc(SplitBefore->getDebugLoc());
  BranchInst *HeadNewTerm =
    BranchInst::Create(/*ifTrue*/ThenBlock, /*ifFalse*/Tail, Cond);
  HeadNewTerm->setMetadata(LLVMContext::MD_prof, BranchWeights);
  ReplaceInstWithInst(HeadOldTerm, HeadNewTerm);

  if (DT) {
    if (DomTreeNode *OldNode = DT->getNode(Head)) {
      std::vector<DomTreeNode *> Children(OldNode->begin(), OldNode->end());

      DomTreeNode *NewNode = DT->addNewBlock(Tail, Head);
      for (DomTreeNode *Child : Children)
        DT->changeImmediateDominator(Child, NewNode);

      // Head dominates ThenBlock.
      DT->addNewBlock(ThenBlock, Head);
    }
  }

  if (LI) {
    if (Loop *L = LI->getLoopFor(Head)) {
      L->addBasicBlockToLoop(ThenBlock, *LI);
      L->addBasicBlockToLoop(Tail, *LI);
    }
  }

  return CheckTerm;
}

void llvm::SplitBlockAndInsertIfThenElse(Value *Cond, Instruction *SplitBefore,
                                         Instruction **ThenTerm,
                                         Instruction **ElseTerm,
                                         MDNode *BranchWeights) {
  BasicBlock *Head = SplitBefore->getParent();
  BasicBlock *Tail = Head->splitBasicBlock(SplitBefore->getIterator());
  Instruction *HeadOldTerm = Head->getTerminator();
  LLVMContext &C = Head->getContext();
  BasicBlock *ThenBlock = BasicBlock::Create(C, "", Head->getParent(), Tail);
  BasicBlock *ElseBlock = BasicBlock::Create(C, "", Head->getParent(), Tail);
  *ThenTerm = BranchInst::Create(Tail, ThenBlock);
  (*ThenTerm)->setDebugLoc(SplitBefore->getDebugLoc());
  *ElseTerm = BranchInst::Create(Tail, ElseBlock);
  (*ElseTerm)->setDebugLoc(SplitBefore->getDebugLoc());
  BranchInst *HeadNewTerm =
    BranchInst::Create(/*ifTrue*/ThenBlock, /*ifFalse*/ElseBlock, Cond);
  HeadNewTerm->setMetadata(LLVMContext::MD_prof, BranchWeights);
  ReplaceInstWithInst(HeadOldTerm, HeadNewTerm);
}

Value *llvm::GetIfCondition(BasicBlock *BB, BasicBlock *&IfTrue,
                             BasicBlock *&IfFalse) {
  PHINode *SomePHI = dyn_cast<PHINode>(BB->begin());
  BasicBlock *Pred1 = nullptr;
  BasicBlock *Pred2 = nullptr;

  if (SomePHI) {
    if (SomePHI->getNumIncomingValues() != 2)
      return nullptr;
    Pred1 = SomePHI->getIncomingBlock(0);
    Pred2 = SomePHI->getIncomingBlock(1);
  } else {
    pred_iterator PI = pred_begin(BB), PE = pred_end(BB);
    if (PI == PE) // No predecessor
      return nullptr;
    Pred1 = *PI++;
    if (PI == PE) // Only one predecessor
      return nullptr;
    Pred2 = *PI++;
    if (PI != PE) // More than two predecessors
      return nullptr;
  }

  // We can only handle branches.  Other control flow will be lowered to
  // branches if possible anyway.
  BranchInst *Pred1Br = dyn_cast<BranchInst>(Pred1->getTerminator());
  BranchInst *Pred2Br = dyn_cast<BranchInst>(Pred2->getTerminator());
  if (!Pred1Br || !Pred2Br)
    return nullptr;

  // Eliminate code duplication by ensuring that Pred1Br is conditional if
  // either are.
  if (Pred2Br->isConditional()) {
    // If both branches are conditional, we don't have an "if statement".  In
    // reality, we could transform this case, but since the condition will be
    // required anyway, we stand no chance of eliminating it, so the xform is
    // probably not profitable.
    if (Pred1Br->isConditional())
      return nullptr;

    std::swap(Pred1, Pred2);
    std::swap(Pred1Br, Pred2Br);
  }

  if (Pred1Br->isConditional()) {
    // The only thing we have to watch out for here is to make sure that Pred2
    // doesn't have incoming edges from other blocks.  If it does, the condition
    // doesn't dominate BB.
    if (!Pred2->getSinglePredecessor())
      return nullptr;

    // If we found a conditional branch predecessor, make sure that it branches
    // to BB and Pred2Br.  If it doesn't, this isn't an "if statement".
    if (Pred1Br->getSuccessor(0) == BB &&
        Pred1Br->getSuccessor(1) == Pred2) {
      IfTrue = Pred1;
      IfFalse = Pred2;
    } else if (Pred1Br->getSuccessor(0) == Pred2 &&
               Pred1Br->getSuccessor(1) == BB) {
      IfTrue = Pred2;
      IfFalse = Pred1;
    } else {
      // We know that one arm of the conditional goes to BB, so the other must
      // go somewhere unrelated, and this must not be an "if statement".
      return nullptr;
    }

    return Pred1Br->getCondition();
  }

  // Ok, if we got here, both predecessors end with an unconditional branch to
  // BB.  Don't panic!  If both blocks only have a single (identical)
  // predecessor, and THAT is a conditional branch, then we're all ok!
  BasicBlock *CommonPred = Pred1->getSinglePredecessor();
  if (CommonPred == nullptr || CommonPred != Pred2->getSinglePredecessor())
    return nullptr;

  // Otherwise, if this is a conditional branch, then we can use it!
  BranchInst *BI = dyn_cast<BranchInst>(CommonPred->getTerminator());
  if (!BI) return nullptr;

  assert(BI->isConditional() && "Two successors but not conditional?");
  if (BI->getSuccessor(0) == Pred1) {
    IfTrue = Pred1;
    IfFalse = Pred2;
  } else {
    IfTrue = Pred2;
    IfFalse = Pred1;
  }
  return BI->getCondition();
}
