//===- FlatternCFG.cpp - Code to perform CFG flattening -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Reduce conditional branches in CFG.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <cassert>

using namespace llvm;

#define DEBUG_TYPE "flattencfg"

namespace {

class FlattenCFGOpt {
  AliasAnalysis *AA;

  /// Use parallel-and or parallel-or to generate conditions for
  /// conditional branches.
  bool FlattenParallelAndOr(BasicBlock *BB, IRBuilder<> &Builder);

  /// If \param BB is the merge block of an if-region, attempt to merge
  /// the if-region with an adjacent if-region upstream if two if-regions
  /// contain identical instructions.
  bool MergeIfRegion(BasicBlock *BB, IRBuilder<> &Builder);

  /// Compare a pair of blocks: \p Block1 and \p Block2, which
  /// are from two if-regions whose entry blocks are \p Head1 and \p
  /// Head2.  \returns true if \p Block1 and \p Block2 contain identical
  /// instructions, and have no memory reference alias with \p Head2.
  /// This is used as a legality check for merging if-regions.
  bool CompareIfRegionBlock(BasicBlock *Head1, BasicBlock *Head2,
                            BasicBlock *Block1, BasicBlock *Block2);

public:
  FlattenCFGOpt(AliasAnalysis *AA) : AA(AA) {}

  bool run(BasicBlock *BB);
};

} // end anonymous namespace

/// If \param [in] BB has more than one predecessor that is a conditional
/// branch, attempt to use parallel and/or for the branch condition. \returns
/// true on success.
///
/// Before:
///   ......
///   %cmp10 = fcmp une float %tmp1, %tmp2
///   br i1 %cmp1, label %if.then, label %lor.rhs
///
/// lor.rhs:
///   ......
///   %cmp11 = fcmp une float %tmp3, %tmp4
///   br i1 %cmp11, label %if.then, label %ifend
///
/// if.end:  // the merge block
///   ......
///
/// if.then: // has two predecessors, both of them contains conditional branch.
///   ......
///   br label %if.end;
///
/// After:
///  ......
///  %cmp10 = fcmp une float %tmp1, %tmp2
///  ......
///  %cmp11 = fcmp une float %tmp3, %tmp4
///  %cmp12 = or i1 %cmp10, %cmp11    // parallel-or mode.
///  br i1 %cmp12, label %if.then, label %ifend
///
///  if.end:
///    ......
///
///  if.then:
///    ......
///    br label %if.end;
///
///  Current implementation handles two cases.
///  Case 1: \param BB is on the else-path.
///
///          BB1
///        /     |
///       BB2    |
///      /   \   |
///     BB3   \  |     where, BB1, BB2 contain conditional branches.
///      \    |  /     BB3 contains unconditional branch.
///       \   | /      BB4 corresponds to \param BB which is also the merge.
///  BB => BB4
///
///
///  Corresponding source code:
///
///  if (a == b && c == d)
///    statement; // BB3
///
///  Case 2: \param BB BB is on the then-path.
///
///             BB1
///          /      |
///         |      BB2
///         \    /    |  where BB1, BB2 contain conditional branches.
///  BB =>   BB3      |  BB3 contains unconditiona branch and corresponds
///           \     /    to \param BB.  BB4 is the merge.
///             BB4
///
///  Corresponding source code:
///
///  if (a == b || c == d)
///    statement;  // BB3
///
///  In both cases,  \param BB is the common successor of conditional branches.
///  In Case 1, \param BB (BB4) has an unconditional branch (BB3) as
///  its predecessor.  In Case 2, \param BB (BB3) only has conditional branches
///  as its predecessors.
bool FlattenCFGOpt::FlattenParallelAndOr(BasicBlock *BB, IRBuilder<> &Builder) {
  PHINode *PHI = dyn_cast<PHINode>(BB->begin());
  if (PHI)
    return false; // For simplicity, avoid cases containing PHI nodes.

  BasicBlock *LastCondBlock = nullptr;
  BasicBlock *FirstCondBlock = nullptr;
  BasicBlock *UnCondBlock = nullptr;
  int Idx = -1;

  // Check predecessors of \param BB.
  SmallPtrSet<BasicBlock *, 16> Preds(pred_begin(BB), pred_end(BB));
  for (SmallPtrSetIterator<BasicBlock *> PI = Preds.begin(), PE = Preds.end();
       PI != PE; ++PI) {
    BasicBlock *Pred = *PI;
    BranchInst *PBI = dyn_cast<BranchInst>(Pred->getTerminator());

    // All predecessors should terminate with a branch.
    if (!PBI)
      return false;

    BasicBlock *PP = Pred->getSinglePredecessor();

    if (PBI->isUnconditional()) {
      // Case 1: Pred (BB3) is an unconditional block, it should
      // have a single predecessor (BB2) that is also a predecessor
      // of \param BB (BB4) and should not have address-taken.
      // There should exist only one such unconditional
      // branch among the predecessors.
      if (UnCondBlock || !PP || (Preds.count(PP) == 0) ||
          Pred->hasAddressTaken())
        return false;

      UnCondBlock = Pred;
      continue;
    }

    // Only conditional branches are allowed beyond this point.
    assert(PBI->isConditional());

    // Condition's unique use should be the branch instruction.
    Value *PC = PBI->getCondition();
    if (!PC || !PC->hasOneUse())
      return false;

    if (PP && Preds.count(PP)) {
      // These are internal condition blocks to be merged from, e.g.,
      // BB2 in both cases.
      // Should not be address-taken.
      if (Pred->hasAddressTaken())
        return false;

      // Instructions in the internal condition blocks should be safe
      // to hoist up.
      for (BasicBlock::iterator BI = Pred->begin(), BE = PBI->getIterator();
           BI != BE;) {
        Instruction *CI = &*BI++;
        if (isa<PHINode>(CI) || !isSafeToSpeculativelyExecute(CI))
          return false;
      }
    } else {
      // This is the condition block to be merged into, e.g. BB1 in
      // both cases.
      if (FirstCondBlock)
        return false;
      FirstCondBlock = Pred;
    }

    // Find whether BB is uniformly on the true (or false) path
    // for all of its predecessors.
    BasicBlock *PS1 = PBI->getSuccessor(0);
    BasicBlock *PS2 = PBI->getSuccessor(1);
    BasicBlock *PS = (PS1 == BB) ? PS2 : PS1;
    int CIdx = (PS1 == BB) ? 0 : 1;

    if (Idx == -1)
      Idx = CIdx;
    else if (CIdx != Idx)
      return false;

    // PS is the successor which is not BB. Check successors to identify
    // the last conditional branch.
    if (Preds.count(PS) == 0) {
      // Case 2.
      LastCondBlock = Pred;
    } else {
      // Case 1
      BranchInst *BPS = dyn_cast<BranchInst>(PS->getTerminator());
      if (BPS && BPS->isUnconditional()) {
        // Case 1: PS(BB3) should be an unconditional branch.
        LastCondBlock = Pred;
      }
    }
  }

  if (!FirstCondBlock || !LastCondBlock || (FirstCondBlock == LastCondBlock))
    return false;

  Instruction *TBB = LastCondBlock->getTerminator();
  BasicBlock *PS1 = TBB->getSuccessor(0);
  BasicBlock *PS2 = TBB->getSuccessor(1);
  BranchInst *PBI1 = dyn_cast<BranchInst>(PS1->getTerminator());
  BranchInst *PBI2 = dyn_cast<BranchInst>(PS2->getTerminator());

  // If PS1 does not jump into PS2, but PS2 jumps into PS1,
  // attempt branch inversion.
  if (!PBI1 || !PBI1->isUnconditional() ||
      (PS1->getTerminator()->getSuccessor(0) != PS2)) {
    // Check whether PS2 jumps into PS1.
    if (!PBI2 || !PBI2->isUnconditional() ||
        (PS2->getTerminator()->getSuccessor(0) != PS1))
      return false;

    // Do branch inversion.
    BasicBlock *CurrBlock = LastCondBlock;
    bool EverChanged = false;
    for (; CurrBlock != FirstCondBlock;
         CurrBlock = CurrBlock->getSinglePredecessor()) {
      BranchInst *BI = dyn_cast<BranchInst>(CurrBlock->getTerminator());
      CmpInst *CI = dyn_cast<CmpInst>(BI->getCondition());
      if (!CI)
        continue;

      CmpInst::Predicate Predicate = CI->getPredicate();
      // Canonicalize icmp_ne -> icmp_eq, fcmp_one -> fcmp_oeq
      if ((Predicate == CmpInst::ICMP_NE) || (Predicate == CmpInst::FCMP_ONE)) {
        CI->setPredicate(ICmpInst::getInversePredicate(Predicate));
        BI->swapSuccessors();
        EverChanged = true;
      }
    }
    return EverChanged;
  }

  // PS1 must have a conditional branch.
  if (!PBI1 || !PBI1->isUnconditional())
    return false;

  // PS2 should not contain PHI node.
  PHI = dyn_cast<PHINode>(PS2->begin());
  if (PHI)
    return false;

  // Do the transformation.
  BasicBlock *CB;
  BranchInst *PBI = dyn_cast<BranchInst>(FirstCondBlock->getTerminator());
  bool Iteration = true;
  IRBuilder<>::InsertPointGuard Guard(Builder);
  Value *PC = PBI->getCondition();

  do {
    CB = PBI->getSuccessor(1 - Idx);
    // Delete the conditional branch.
    FirstCondBlock->getInstList().pop_back();
    FirstCondBlock->getInstList()
        .splice(FirstCondBlock->end(), CB->getInstList());
    PBI = cast<BranchInst>(FirstCondBlock->getTerminator());
    Value *CC = PBI->getCondition();
    // Merge conditions.
    Builder.SetInsertPoint(PBI);
    Value *NC;
    if (Idx == 0)
      // Case 2, use parallel or.
      NC = Builder.CreateOr(PC, CC);
    else
      // Case 1, use parallel and.
      NC = Builder.CreateAnd(PC, CC);

    PBI->replaceUsesOfWith(CC, NC);
    PC = NC;
    if (CB == LastCondBlock)
      Iteration = false;
    // Remove internal conditional branches.
    CB->dropAllReferences();
    // make CB unreachable and let downstream to delete the block.
    new UnreachableInst(CB->getContext(), CB);
  } while (Iteration);

  LLVM_DEBUG(dbgs() << "Use parallel and/or in:\n" << *FirstCondBlock);
  return true;
}

/// Compare blocks from two if-regions, where \param Head1 is the entry of the
/// 1st if-region. \param Head2 is the entry of the 2nd if-region. \param
/// Block1 is a block in the 1st if-region to compare. \param Block2 is a block
//  in the 2nd if-region to compare.  \returns true if \param Block1 and \param
/// Block2 have identical instructions and do not have memory reference alias
/// with \param Head2.
bool FlattenCFGOpt::CompareIfRegionBlock(BasicBlock *Head1, BasicBlock *Head2,
                                         BasicBlock *Block1,
                                         BasicBlock *Block2) {
  Instruction *PTI2 = Head2->getTerminator();
  Instruction *PBI2 = &Head2->front();

  bool eq1 = (Block1 == Head1);
  bool eq2 = (Block2 == Head2);
  if (eq1 || eq2) {
    // An empty then-path or else-path.
    return (eq1 == eq2);
  }

  // Check whether instructions in Block1 and Block2 are identical
  // and do not alias with instructions in Head2.
  BasicBlock::iterator iter1 = Block1->begin();
  BasicBlock::iterator end1 = Block1->getTerminator()->getIterator();
  BasicBlock::iterator iter2 = Block2->begin();
  BasicBlock::iterator end2 = Block2->getTerminator()->getIterator();

  while (true) {
    if (iter1 == end1) {
      if (iter2 != end2)
        return false;
      break;
    }

    if (!iter1->isIdenticalTo(&*iter2))
      return false;

    // Illegal to remove instructions with side effects except
    // non-volatile stores.
    if (iter1->mayHaveSideEffects()) {
      Instruction *CurI = &*iter1;
      StoreInst *SI = dyn_cast<StoreInst>(CurI);
      if (!SI || SI->isVolatile())
        return false;
    }

    // For simplicity and speed, data dependency check can be
    // avoided if read from memory doesn't exist.
    if (iter1->mayReadFromMemory())
      return false;

    if (iter1->mayWriteToMemory()) {
      for (BasicBlock::iterator BI(PBI2), BE(PTI2); BI != BE; ++BI) {
        if (BI->mayReadFromMemory() || BI->mayWriteToMemory()) {
          // Check alias with Head2.
          if (!AA || AA->alias(&*iter1, &*BI))
            return false;
        }
      }
    }
    ++iter1;
    ++iter2;
  }

  return true;
}

/// Check whether \param BB is the merge block of a if-region.  If yes, check
/// whether there exists an adjacent if-region upstream, the two if-regions
/// contain identical instructions and can be legally merged.  \returns true if
/// the two if-regions are merged.
///
/// From:
/// if (a)
///   statement;
/// if (b)
///   statement;
///
/// To:
/// if (a || b)
///   statement;
bool FlattenCFGOpt::MergeIfRegion(BasicBlock *BB, IRBuilder<> &Builder) {
  BasicBlock *IfTrue2, *IfFalse2;
  Value *IfCond2 = GetIfCondition(BB, IfTrue2, IfFalse2);
  Instruction *CInst2 = dyn_cast_or_null<Instruction>(IfCond2);
  if (!CInst2)
    return false;

  BasicBlock *SecondEntryBlock = CInst2->getParent();
  if (SecondEntryBlock->hasAddressTaken())
    return false;

  BasicBlock *IfTrue1, *IfFalse1;
  Value *IfCond1 = GetIfCondition(SecondEntryBlock, IfTrue1, IfFalse1);
  Instruction *CInst1 = dyn_cast_or_null<Instruction>(IfCond1);
  if (!CInst1)
    return false;

  BasicBlock *FirstEntryBlock = CInst1->getParent();

  // Either then-path or else-path should be empty.
  if ((IfTrue1 != FirstEntryBlock) && (IfFalse1 != FirstEntryBlock))
    return false;
  if ((IfTrue2 != SecondEntryBlock) && (IfFalse2 != SecondEntryBlock))
    return false;

  Instruction *PTI2 = SecondEntryBlock->getTerminator();
  Instruction *PBI2 = &SecondEntryBlock->front();

  if (!CompareIfRegionBlock(FirstEntryBlock, SecondEntryBlock, IfTrue1,
                            IfTrue2))
    return false;

  if (!CompareIfRegionBlock(FirstEntryBlock, SecondEntryBlock, IfFalse1,
                            IfFalse2))
    return false;

  // Check whether \param SecondEntryBlock has side-effect and is safe to
  // speculate.
  for (BasicBlock::iterator BI(PBI2), BE(PTI2); BI != BE; ++BI) {
    Instruction *CI = &*BI;
    if (isa<PHINode>(CI) || CI->mayHaveSideEffects() ||
        !isSafeToSpeculativelyExecute(CI))
      return false;
  }

  // Merge \param SecondEntryBlock into \param FirstEntryBlock.
  FirstEntryBlock->getInstList().pop_back();
  FirstEntryBlock->getInstList()
      .splice(FirstEntryBlock->end(), SecondEntryBlock->getInstList());
  BranchInst *PBI = dyn_cast<BranchInst>(FirstEntryBlock->getTerminator());
  Value *CC = PBI->getCondition();
  BasicBlock *SaveInsertBB = Builder.GetInsertBlock();
  BasicBlock::iterator SaveInsertPt = Builder.GetInsertPoint();
  Builder.SetInsertPoint(PBI);
  Value *NC = Builder.CreateOr(CInst1, CC);
  PBI->replaceUsesOfWith(CC, NC);
  Builder.SetInsertPoint(SaveInsertBB, SaveInsertPt);

  // Remove IfTrue1
  if (IfTrue1 != FirstEntryBlock) {
    IfTrue1->dropAllReferences();
    IfTrue1->eraseFromParent();
  }

  // Remove IfFalse1
  if (IfFalse1 != FirstEntryBlock) {
    IfFalse1->dropAllReferences();
    IfFalse1->eraseFromParent();
  }

  // Remove \param SecondEntryBlock
  SecondEntryBlock->dropAllReferences();
  SecondEntryBlock->eraseFromParent();
  LLVM_DEBUG(dbgs() << "If conditions merged into:\n" << *FirstEntryBlock);
  return true;
}

bool FlattenCFGOpt::run(BasicBlock *BB) {
  assert(BB && BB->getParent() && "Block not embedded in function!");
  assert(BB->getTerminator() && "Degenerate basic block encountered!");

  IRBuilder<> Builder(BB);

  if (FlattenParallelAndOr(BB, Builder) || MergeIfRegion(BB, Builder))
    return true;
  return false;
}

/// FlattenCFG - This function is used to flatten a CFG.  For
/// example, it uses parallel-and and parallel-or mode to collapse
/// if-conditions and merge if-regions with identical statements.
bool llvm::FlattenCFG(BasicBlock *BB, AliasAnalysis *AA) {
  return FlattenCFGOpt(AA).run(BB);
}
