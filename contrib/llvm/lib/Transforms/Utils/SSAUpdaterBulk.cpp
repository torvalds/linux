//===- SSAUpdaterBulk.cpp - Unstructured SSA Update Tool ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the SSAUpdaterBulk class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/SSAUpdaterBulk.h"
#include "llvm/Analysis/IteratedDominanceFrontier.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/Value.h"

using namespace llvm;

#define DEBUG_TYPE "ssaupdaterbulk"

/// Helper function for finding a block which should have a value for the given
/// user. For PHI-nodes this block is the corresponding predecessor, for other
/// instructions it's their parent block.
static BasicBlock *getUserBB(Use *U) {
  auto *User = cast<Instruction>(U->getUser());

  if (auto *UserPN = dyn_cast<PHINode>(User))
    return UserPN->getIncomingBlock(*U);
  else
    return User->getParent();
}

/// Add a new variable to the SSA rewriter. This needs to be called before
/// AddAvailableValue or AddUse calls.
unsigned SSAUpdaterBulk::AddVariable(StringRef Name, Type *Ty) {
  unsigned Var = Rewrites.size();
  LLVM_DEBUG(dbgs() << "SSAUpdater: Var=" << Var << ": initialized with Ty = "
                    << *Ty << ", Name = " << Name << "\n");
  RewriteInfo RI(Name, Ty);
  Rewrites.push_back(RI);
  return Var;
}

/// Indicate that a rewritten value is available in the specified block with the
/// specified value.
void SSAUpdaterBulk::AddAvailableValue(unsigned Var, BasicBlock *BB, Value *V) {
  assert(Var < Rewrites.size() && "Variable not found!");
  LLVM_DEBUG(dbgs() << "SSAUpdater: Var=" << Var
                    << ": added new available value" << *V << " in "
                    << BB->getName() << "\n");
  Rewrites[Var].Defines[BB] = V;
}

/// Record a use of the symbolic value. This use will be updated with a
/// rewritten value when RewriteAllUses is called.
void SSAUpdaterBulk::AddUse(unsigned Var, Use *U) {
  assert(Var < Rewrites.size() && "Variable not found!");
  LLVM_DEBUG(dbgs() << "SSAUpdater: Var=" << Var << ": added a use" << *U->get()
                    << " in " << getUserBB(U)->getName() << "\n");
  Rewrites[Var].Uses.push_back(U);
}

/// Return true if the SSAUpdater already has a value for the specified variable
/// in the specified block.
bool SSAUpdaterBulk::HasValueForBlock(unsigned Var, BasicBlock *BB) {
  return (Var < Rewrites.size()) ? Rewrites[Var].Defines.count(BB) : false;
}

// Compute value at the given block BB. We either should already know it, or we
// should be able to recursively reach it going up dominator tree.
Value *SSAUpdaterBulk::computeValueAt(BasicBlock *BB, RewriteInfo &R,
                                      DominatorTree *DT) {
  if (!R.Defines.count(BB)) {
    if (DT->isReachableFromEntry(BB) && PredCache.get(BB).size()) {
      BasicBlock *IDom = DT->getNode(BB)->getIDom()->getBlock();
      Value *V = computeValueAt(IDom, R, DT);
      R.Defines[BB] = V;
    } else
      R.Defines[BB] = UndefValue::get(R.Ty);
  }
  return R.Defines[BB];
}

/// Given sets of UsingBlocks and DefBlocks, compute the set of LiveInBlocks.
/// This is basically a subgraph limited by DefBlocks and UsingBlocks.
static void
ComputeLiveInBlocks(const SmallPtrSetImpl<BasicBlock *> &UsingBlocks,
                    const SmallPtrSetImpl<BasicBlock *> &DefBlocks,
                    SmallPtrSetImpl<BasicBlock *> &LiveInBlocks,
                    PredIteratorCache &PredCache) {
  // To determine liveness, we must iterate through the predecessors of blocks
  // where the def is live.  Blocks are added to the worklist if we need to
  // check their predecessors.  Start with all the using blocks.
  SmallVector<BasicBlock *, 64> LiveInBlockWorklist(UsingBlocks.begin(),
                                                    UsingBlocks.end());

  // Now that we have a set of blocks where the phi is live-in, recursively add
  // their predecessors until we find the full region the value is live.
  while (!LiveInBlockWorklist.empty()) {
    BasicBlock *BB = LiveInBlockWorklist.pop_back_val();

    // The block really is live in here, insert it into the set.  If already in
    // the set, then it has already been processed.
    if (!LiveInBlocks.insert(BB).second)
      continue;

    // Since the value is live into BB, it is either defined in a predecessor or
    // live into it to.  Add the preds to the worklist unless they are a
    // defining block.
    for (BasicBlock *P : PredCache.get(BB)) {
      // The value is not live into a predecessor if it defines the value.
      if (DefBlocks.count(P))
        continue;

      // Otherwise it is, add to the worklist.
      LiveInBlockWorklist.push_back(P);
    }
  }
}

/// Perform all the necessary updates, including new PHI-nodes insertion and the
/// requested uses update.
void SSAUpdaterBulk::RewriteAllUses(DominatorTree *DT,
                                    SmallVectorImpl<PHINode *> *InsertedPHIs) {
  for (auto &R : Rewrites) {
    // Compute locations for new phi-nodes.
    // For that we need to initialize DefBlocks from definitions in R.Defines,
    // UsingBlocks from uses in R.Uses, then compute LiveInBlocks, and then use
    // this set for computing iterated dominance frontier (IDF).
    // The IDF blocks are the blocks where we need to insert new phi-nodes.
    ForwardIDFCalculator IDF(*DT);
    LLVM_DEBUG(dbgs() << "SSAUpdater: rewriting " << R.Uses.size()
                      << " use(s)\n");

    SmallPtrSet<BasicBlock *, 2> DefBlocks;
    for (auto &Def : R.Defines)
      DefBlocks.insert(Def.first);
    IDF.setDefiningBlocks(DefBlocks);

    SmallPtrSet<BasicBlock *, 2> UsingBlocks;
    for (Use *U : R.Uses)
      UsingBlocks.insert(getUserBB(U));

    SmallVector<BasicBlock *, 32> IDFBlocks;
    SmallPtrSet<BasicBlock *, 32> LiveInBlocks;
    ComputeLiveInBlocks(UsingBlocks, DefBlocks, LiveInBlocks, PredCache);
    IDF.resetLiveInBlocks();
    IDF.setLiveInBlocks(LiveInBlocks);
    IDF.calculate(IDFBlocks);

    // We've computed IDF, now insert new phi-nodes there.
    SmallVector<PHINode *, 4> InsertedPHIsForVar;
    for (auto *FrontierBB : IDFBlocks) {
      IRBuilder<> B(FrontierBB, FrontierBB->begin());
      PHINode *PN = B.CreatePHI(R.Ty, 0, R.Name);
      R.Defines[FrontierBB] = PN;
      InsertedPHIsForVar.push_back(PN);
      if (InsertedPHIs)
        InsertedPHIs->push_back(PN);
    }

    // Fill in arguments of the inserted PHIs.
    for (auto *PN : InsertedPHIsForVar) {
      BasicBlock *PBB = PN->getParent();
      for (BasicBlock *Pred : PredCache.get(PBB))
        PN->addIncoming(computeValueAt(Pred, R, DT), Pred);
    }

    // Rewrite actual uses with the inserted definitions.
    SmallPtrSet<Use *, 4> ProcessedUses;
    for (Use *U : R.Uses) {
      if (!ProcessedUses.insert(U).second)
        continue;
      Value *V = computeValueAt(getUserBB(U), R, DT);
      Value *OldVal = U->get();
      assert(OldVal && "Invalid use!");
      // Notify that users of the existing value that it is being replaced.
      if (OldVal != V && OldVal->hasValueHandle())
        ValueHandleBase::ValueIsRAUWd(OldVal, V);
      LLVM_DEBUG(dbgs() << "SSAUpdater: replacing " << *OldVal << " with " << *V
                        << "\n");
      U->set(V);
    }
  }
}
