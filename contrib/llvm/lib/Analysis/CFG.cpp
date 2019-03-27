//===-- CFG.cpp - BasicBlock analysis --------------------------------------==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This family of functions performs analyses on basic blocks, and instructions
// contained within basic blocks.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/CFG.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"

using namespace llvm;

/// FindFunctionBackedges - Analyze the specified function to find all of the
/// loop backedges in the function and return them.  This is a relatively cheap
/// (compared to computing dominators and loop info) analysis.
///
/// The output is added to Result, as pairs of <from,to> edge info.
void llvm::FindFunctionBackedges(const Function &F,
     SmallVectorImpl<std::pair<const BasicBlock*,const BasicBlock*> > &Result) {
  const BasicBlock *BB = &F.getEntryBlock();
  if (succ_empty(BB))
    return;

  SmallPtrSet<const BasicBlock*, 8> Visited;
  SmallVector<std::pair<const BasicBlock*, succ_const_iterator>, 8> VisitStack;
  SmallPtrSet<const BasicBlock*, 8> InStack;

  Visited.insert(BB);
  VisitStack.push_back(std::make_pair(BB, succ_begin(BB)));
  InStack.insert(BB);
  do {
    std::pair<const BasicBlock*, succ_const_iterator> &Top = VisitStack.back();
    const BasicBlock *ParentBB = Top.first;
    succ_const_iterator &I = Top.second;

    bool FoundNew = false;
    while (I != succ_end(ParentBB)) {
      BB = *I++;
      if (Visited.insert(BB).second) {
        FoundNew = true;
        break;
      }
      // Successor is in VisitStack, it's a back edge.
      if (InStack.count(BB))
        Result.push_back(std::make_pair(ParentBB, BB));
    }

    if (FoundNew) {
      // Go down one level if there is a unvisited successor.
      InStack.insert(BB);
      VisitStack.push_back(std::make_pair(BB, succ_begin(BB)));
    } else {
      // Go up one level.
      InStack.erase(VisitStack.pop_back_val().first);
    }
  } while (!VisitStack.empty());
}

/// GetSuccessorNumber - Search for the specified successor of basic block BB
/// and return its position in the terminator instruction's list of
/// successors.  It is an error to call this with a block that is not a
/// successor.
unsigned llvm::GetSuccessorNumber(const BasicBlock *BB,
    const BasicBlock *Succ) {
  const Instruction *Term = BB->getTerminator();
#ifndef NDEBUG
  unsigned e = Term->getNumSuccessors();
#endif
  for (unsigned i = 0; ; ++i) {
    assert(i != e && "Didn't find edge?");
    if (Term->getSuccessor(i) == Succ)
      return i;
  }
}

/// isCriticalEdge - Return true if the specified edge is a critical edge.
/// Critical edges are edges from a block with multiple successors to a block
/// with multiple predecessors.
bool llvm::isCriticalEdge(const Instruction *TI, unsigned SuccNum,
                          bool AllowIdenticalEdges) {
  assert(TI->isTerminator() && "Must be a terminator to have successors!");
  assert(SuccNum < TI->getNumSuccessors() && "Illegal edge specification!");
  if (TI->getNumSuccessors() == 1) return false;

  const BasicBlock *Dest = TI->getSuccessor(SuccNum);
  const_pred_iterator I = pred_begin(Dest), E = pred_end(Dest);

  // If there is more than one predecessor, this is a critical edge...
  assert(I != E && "No preds, but we have an edge to the block?");
  const BasicBlock *FirstPred = *I;
  ++I;        // Skip one edge due to the incoming arc from TI.
  if (!AllowIdenticalEdges)
    return I != E;

  // If AllowIdenticalEdges is true, then we allow this edge to be considered
  // non-critical iff all preds come from TI's block.
  for (; I != E; ++I)
    if (*I != FirstPred)
      return true;
  return false;
}

// LoopInfo contains a mapping from basic block to the innermost loop. Find
// the outermost loop in the loop nest that contains BB.
static const Loop *getOutermostLoop(const LoopInfo *LI, const BasicBlock *BB) {
  const Loop *L = LI->getLoopFor(BB);
  if (L) {
    while (const Loop *Parent = L->getParentLoop())
      L = Parent;
  }
  return L;
}

// True if there is a loop which contains both BB1 and BB2.
static bool loopContainsBoth(const LoopInfo *LI,
                             const BasicBlock *BB1, const BasicBlock *BB2) {
  const Loop *L1 = getOutermostLoop(LI, BB1);
  const Loop *L2 = getOutermostLoop(LI, BB2);
  return L1 != nullptr && L1 == L2;
}

bool llvm::isPotentiallyReachableFromMany(
    SmallVectorImpl<BasicBlock *> &Worklist, BasicBlock *StopBB,
    const DominatorTree *DT, const LoopInfo *LI) {
  // When the stop block is unreachable, it's dominated from everywhere,
  // regardless of whether there's a path between the two blocks.
  if (DT && !DT->isReachableFromEntry(StopBB))
    DT = nullptr;

  // Limit the number of blocks we visit. The goal is to avoid run-away compile
  // times on large CFGs without hampering sensible code. Arbitrarily chosen.
  unsigned Limit = 32;
  SmallPtrSet<const BasicBlock*, 32> Visited;
  do {
    BasicBlock *BB = Worklist.pop_back_val();
    if (!Visited.insert(BB).second)
      continue;
    if (BB == StopBB)
      return true;
    if (DT && DT->dominates(BB, StopBB))
      return true;
    if (LI && loopContainsBoth(LI, BB, StopBB))
      return true;

    if (!--Limit) {
      // We haven't been able to prove it one way or the other. Conservatively
      // answer true -- that there is potentially a path.
      return true;
    }

    if (const Loop *Outer = LI ? getOutermostLoop(LI, BB) : nullptr) {
      // All blocks in a single loop are reachable from all other blocks. From
      // any of these blocks, we can skip directly to the exits of the loop,
      // ignoring any other blocks inside the loop body.
      Outer->getExitBlocks(Worklist);
    } else {
      Worklist.append(succ_begin(BB), succ_end(BB));
    }
  } while (!Worklist.empty());

  // We have exhausted all possible paths and are certain that 'To' can not be
  // reached from 'From'.
  return false;
}

bool llvm::isPotentiallyReachable(const BasicBlock *A, const BasicBlock *B,
                                  const DominatorTree *DT, const LoopInfo *LI) {
  assert(A->getParent() == B->getParent() &&
         "This analysis is function-local!");

  SmallVector<BasicBlock*, 32> Worklist;
  Worklist.push_back(const_cast<BasicBlock*>(A));

  return isPotentiallyReachableFromMany(Worklist, const_cast<BasicBlock *>(B),
                                        DT, LI);
}

bool llvm::isPotentiallyReachable(const Instruction *A, const Instruction *B,
                                  const DominatorTree *DT, const LoopInfo *LI) {
  assert(A->getParent()->getParent() == B->getParent()->getParent() &&
         "This analysis is function-local!");

  SmallVector<BasicBlock*, 32> Worklist;

  if (A->getParent() == B->getParent()) {
    // The same block case is special because it's the only time we're looking
    // within a single block to see which instruction comes first. Once we
    // start looking at multiple blocks, the first instruction of the block is
    // reachable, so we only need to determine reachability between whole
    // blocks.
    BasicBlock *BB = const_cast<BasicBlock *>(A->getParent());

    // If the block is in a loop then we can reach any instruction in the block
    // from any other instruction in the block by going around a backedge.
    if (LI && LI->getLoopFor(BB) != nullptr)
      return true;

    // Linear scan, start at 'A', see whether we hit 'B' or the end first.
    for (BasicBlock::const_iterator I = A->getIterator(), E = BB->end(); I != E;
         ++I) {
      if (&*I == B)
        return true;
    }

    // Can't be in a loop if it's the entry block -- the entry block may not
    // have predecessors.
    if (BB == &BB->getParent()->getEntryBlock())
      return false;

    // Otherwise, continue doing the normal per-BB CFG walk.
    Worklist.append(succ_begin(BB), succ_end(BB));

    if (Worklist.empty()) {
      // We've proven that there's no path!
      return false;
    }
  } else {
    Worklist.push_back(const_cast<BasicBlock*>(A->getParent()));
  }

  if (A->getParent() == &A->getParent()->getParent()->getEntryBlock())
    return true;
  if (B->getParent() == &A->getParent()->getParent()->getEntryBlock())
    return false;

  return isPotentiallyReachableFromMany(
      Worklist, const_cast<BasicBlock *>(B->getParent()), DT, LI);
}
