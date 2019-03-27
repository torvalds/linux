//===- Transform/Utils/BasicBlockUtils.h - BasicBlock Utils -----*- C++ -*-===//
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

#ifndef LLVM_TRANSFORMS_UTILS_BASICBLOCKUTILS_H
#define LLVM_TRANSFORMS_UTILS_BASICBLOCKUTILS_H

// FIXME: Move to this file: BasicBlock::removePredecessor, BB::splitBasicBlock

#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/DomTreeUpdater.h"
#include "llvm/IR/InstrTypes.h"
#include <cassert>

namespace llvm {

class BlockFrequencyInfo;
class BranchProbabilityInfo;
class DominatorTree;
class DomTreeUpdater;
class Function;
class Instruction;
class LoopInfo;
class MDNode;
class MemoryDependenceResults;
class MemorySSAUpdater;
class ReturnInst;
class TargetLibraryInfo;
class Value;

/// Delete the specified block, which must have no predecessors.
void DeleteDeadBlock(BasicBlock *BB, DomTreeUpdater *DTU = nullptr);

/// Delete the specified blocks from \p BB. The set of deleted blocks must have
/// no predecessors that are not being deleted themselves. \p BBs must have no
/// duplicating blocks. If there are loops among this set of blocks, all
/// relevant loop info updates should be done before this function is called.
void DeleteDeadBlocks(SmallVectorImpl <BasicBlock *> &BBs,
                      DomTreeUpdater *DTU = nullptr);

/// We know that BB has one predecessor. If there are any single-entry PHI nodes
/// in it, fold them away. This handles the case when all entries to the PHI
/// nodes in a block are guaranteed equal, such as when the block has exactly
/// one predecessor.
void FoldSingleEntryPHINodes(BasicBlock *BB,
                             MemoryDependenceResults *MemDep = nullptr);

/// Examine each PHI in the given block and delete it if it is dead. Also
/// recursively delete any operands that become dead as a result. This includes
/// tracing the def-use list from the PHI to see if it is ultimately unused or
/// if it reaches an unused cycle. Return true if any PHIs were deleted.
bool DeleteDeadPHIs(BasicBlock *BB, const TargetLibraryInfo *TLI = nullptr);

/// Attempts to merge a block into its predecessor, if possible. The return
/// value indicates success or failure.
bool MergeBlockIntoPredecessor(BasicBlock *BB, DomTreeUpdater *DTU = nullptr,
                               LoopInfo *LI = nullptr,
                               MemorySSAUpdater *MSSAU = nullptr,
                               MemoryDependenceResults *MemDep = nullptr);

/// Replace all uses of an instruction (specified by BI) with a value, then
/// remove and delete the original instruction.
void ReplaceInstWithValue(BasicBlock::InstListType &BIL,
                          BasicBlock::iterator &BI, Value *V);

/// Replace the instruction specified by BI with the instruction specified by I.
/// Copies DebugLoc from BI to I, if I doesn't already have a DebugLoc. The
/// original instruction is deleted and BI is updated to point to the new
/// instruction.
void ReplaceInstWithInst(BasicBlock::InstListType &BIL,
                         BasicBlock::iterator &BI, Instruction *I);

/// Replace the instruction specified by From with the instruction specified by
/// To. Copies DebugLoc from BI to I, if I doesn't already have a DebugLoc.
void ReplaceInstWithInst(Instruction *From, Instruction *To);

/// Option class for critical edge splitting.
///
/// This provides a builder interface for overriding the default options used
/// during critical edge splitting.
struct CriticalEdgeSplittingOptions {
  DominatorTree *DT;
  LoopInfo *LI;
  MemorySSAUpdater *MSSAU;
  bool MergeIdenticalEdges = false;
  bool DontDeleteUselessPHIs = false;
  bool PreserveLCSSA = false;

  CriticalEdgeSplittingOptions(DominatorTree *DT = nullptr,
                               LoopInfo *LI = nullptr,
                               MemorySSAUpdater *MSSAU = nullptr)
      : DT(DT), LI(LI), MSSAU(MSSAU) {}

  CriticalEdgeSplittingOptions &setMergeIdenticalEdges() {
    MergeIdenticalEdges = true;
    return *this;
  }

  CriticalEdgeSplittingOptions &setDontDeleteUselessPHIs() {
    DontDeleteUselessPHIs = true;
    return *this;
  }

  CriticalEdgeSplittingOptions &setPreserveLCSSA() {
    PreserveLCSSA = true;
    return *this;
  }
};

/// If this edge is a critical edge, insert a new node to split the critical
/// edge. This will update the analyses passed in through the option struct.
/// This returns the new block if the edge was split, null otherwise.
///
/// If MergeIdenticalEdges in the options struct is true (not the default),
/// *all* edges from TI to the specified successor will be merged into the same
/// critical edge block. This is most commonly interesting with switch
/// instructions, which may have many edges to any one destination.  This
/// ensures that all edges to that dest go to one block instead of each going
/// to a different block, but isn't the standard definition of a "critical
/// edge".
///
/// It is invalid to call this function on a critical edge that starts at an
/// IndirectBrInst.  Splitting these edges will almost always create an invalid
/// program because the address of the new block won't be the one that is jumped
/// to.
BasicBlock *SplitCriticalEdge(Instruction *TI, unsigned SuccNum,
                              const CriticalEdgeSplittingOptions &Options =
                                  CriticalEdgeSplittingOptions());

inline BasicBlock *
SplitCriticalEdge(BasicBlock *BB, succ_iterator SI,
                  const CriticalEdgeSplittingOptions &Options =
                      CriticalEdgeSplittingOptions()) {
  return SplitCriticalEdge(BB->getTerminator(), SI.getSuccessorIndex(),
                           Options);
}

/// If the edge from *PI to BB is not critical, return false. Otherwise, split
/// all edges between the two blocks and return true. This updates all of the
/// same analyses as the other SplitCriticalEdge function. If P is specified, it
/// updates the analyses described above.
inline bool SplitCriticalEdge(BasicBlock *Succ, pred_iterator PI,
                              const CriticalEdgeSplittingOptions &Options =
                                  CriticalEdgeSplittingOptions()) {
  bool MadeChange = false;
  Instruction *TI = (*PI)->getTerminator();
  for (unsigned i = 0, e = TI->getNumSuccessors(); i != e; ++i)
    if (TI->getSuccessor(i) == Succ)
      MadeChange |= !!SplitCriticalEdge(TI, i, Options);
  return MadeChange;
}

/// If an edge from Src to Dst is critical, split the edge and return true,
/// otherwise return false. This method requires that there be an edge between
/// the two blocks. It updates the analyses passed in the options struct
inline BasicBlock *
SplitCriticalEdge(BasicBlock *Src, BasicBlock *Dst,
                  const CriticalEdgeSplittingOptions &Options =
                      CriticalEdgeSplittingOptions()) {
  Instruction *TI = Src->getTerminator();
  unsigned i = 0;
  while (true) {
    assert(i != TI->getNumSuccessors() && "Edge doesn't exist!");
    if (TI->getSuccessor(i) == Dst)
      return SplitCriticalEdge(TI, i, Options);
    ++i;
  }
}

/// Loop over all of the edges in the CFG, breaking critical edges as they are
/// found. Returns the number of broken edges.
unsigned SplitAllCriticalEdges(Function &F,
                               const CriticalEdgeSplittingOptions &Options =
                                   CriticalEdgeSplittingOptions());

/// Split the edge connecting specified block.
BasicBlock *SplitEdge(BasicBlock *From, BasicBlock *To,
                      DominatorTree *DT = nullptr, LoopInfo *LI = nullptr,
                      MemorySSAUpdater *MSSAU = nullptr);

/// Split the specified block at the specified instruction - everything before
/// SplitPt stays in Old and everything starting with SplitPt moves to a new
/// block. The two blocks are joined by an unconditional branch and the loop
/// info is updated.
BasicBlock *SplitBlock(BasicBlock *Old, Instruction *SplitPt,
                       DominatorTree *DT = nullptr, LoopInfo *LI = nullptr,
                       MemorySSAUpdater *MSSAU = nullptr);

/// This method introduces at least one new basic block into the function and
/// moves some of the predecessors of BB to be predecessors of the new block.
/// The new predecessors are indicated by the Preds array. The new block is
/// given a suffix of 'Suffix'. Returns new basic block to which predecessors
/// from Preds are now pointing.
///
/// If BB is a landingpad block then additional basicblock might be introduced.
/// It will have Suffix+".split_lp". See SplitLandingPadPredecessors for more
/// details on this case.
///
/// This currently updates the LLVM IR, DominatorTree, LoopInfo, and LCCSA but
/// no other analyses. In particular, it does not preserve LoopSimplify
/// (because it's complicated to handle the case where one of the edges being
/// split is an exit of a loop with other exits).
BasicBlock *SplitBlockPredecessors(BasicBlock *BB, ArrayRef<BasicBlock *> Preds,
                                   const char *Suffix,
                                   DominatorTree *DT = nullptr,
                                   LoopInfo *LI = nullptr,
                                   MemorySSAUpdater *MSSAU = nullptr,
                                   bool PreserveLCSSA = false);

/// This method transforms the landing pad, OrigBB, by introducing two new basic
/// blocks into the function. One of those new basic blocks gets the
/// predecessors listed in Preds. The other basic block gets the remaining
/// predecessors of OrigBB. The landingpad instruction OrigBB is clone into both
/// of the new basic blocks. The new blocks are given the suffixes 'Suffix1' and
/// 'Suffix2', and are returned in the NewBBs vector.
///
/// This currently updates the LLVM IR, DominatorTree, LoopInfo, and LCCSA but
/// no other analyses. In particular, it does not preserve LoopSimplify
/// (because it's complicated to handle the case where one of the edges being
/// split is an exit of a loop with other exits).
void SplitLandingPadPredecessors(
    BasicBlock *OrigBB, ArrayRef<BasicBlock *> Preds, const char *Suffix,
    const char *Suffix2, SmallVectorImpl<BasicBlock *> &NewBBs,
    DominatorTree *DT = nullptr, LoopInfo *LI = nullptr,
    MemorySSAUpdater *MSSAU = nullptr, bool PreserveLCSSA = false);

/// This method duplicates the specified return instruction into a predecessor
/// which ends in an unconditional branch. If the return instruction returns a
/// value defined by a PHI, propagate the right value into the return. It
/// returns the new return instruction in the predecessor.
ReturnInst *FoldReturnIntoUncondBranch(ReturnInst *RI, BasicBlock *BB,
                                       BasicBlock *Pred,
                                       DomTreeUpdater *DTU = nullptr);

/// Split the containing block at the specified instruction - everything before
/// SplitBefore stays in the old basic block, and the rest of the instructions
/// in the BB are moved to a new block. The two blocks are connected by a
/// conditional branch (with value of Cmp being the condition).
/// Before:
///   Head
///   SplitBefore
///   Tail
/// After:
///   Head
///   if (Cond)
///     ThenBlock
///   SplitBefore
///   Tail
///
/// If Unreachable is true, then ThenBlock ends with
/// UnreachableInst, otherwise it branches to Tail.
/// Returns the NewBasicBlock's terminator.
///
/// Updates DT and LI if given.
Instruction *SplitBlockAndInsertIfThen(Value *Cond, Instruction *SplitBefore,
                                       bool Unreachable,
                                       MDNode *BranchWeights = nullptr,
                                       DominatorTree *DT = nullptr,
                                       LoopInfo *LI = nullptr);

/// SplitBlockAndInsertIfThenElse is similar to SplitBlockAndInsertIfThen,
/// but also creates the ElseBlock.
/// Before:
///   Head
///   SplitBefore
///   Tail
/// After:
///   Head
///   if (Cond)
///     ThenBlock
///   else
///     ElseBlock
///   SplitBefore
///   Tail
void SplitBlockAndInsertIfThenElse(Value *Cond, Instruction *SplitBefore,
                                   Instruction **ThenTerm,
                                   Instruction **ElseTerm,
                                   MDNode *BranchWeights = nullptr);

/// Check whether BB is the merge point of a if-region.
/// If so, return the boolean condition that determines which entry into
/// BB will be taken.  Also, return by references the block that will be
/// entered from if the condition is true, and the block that will be
/// entered if the condition is false.
///
/// This does no checking to see if the true/false blocks have large or unsavory
/// instructions in them.
Value *GetIfCondition(BasicBlock *BB, BasicBlock *&IfTrue,
                      BasicBlock *&IfFalse);

// Split critical edges where the source of the edge is an indirectbr
// instruction. This isn't always possible, but we can handle some easy cases.
// This is useful because MI is unable to split such critical edges,
// which means it will not be able to sink instructions along those edges.
// This is especially painful for indirect branches with many successors, where
// we end up having to prepare all outgoing values in the origin block.
//
// Our normal algorithm for splitting critical edges requires us to update
// the outgoing edges of the edge origin block, but for an indirectbr this
// is hard, since it would require finding and updating the block addresses
// the indirect branch uses. But if a block only has a single indirectbr
// predecessor, with the others being regular branches, we can do it in a
// different way.
// Say we have A -> D, B -> D, I -> D where only I -> D is an indirectbr.
// We can split D into D0 and D1, where D0 contains only the PHIs from D,
// and D1 is the D block body. We can then duplicate D0 as D0A and D0B, and
// create the following structure:
// A -> D0A, B -> D0A, I -> D0B, D0A -> D1, D0B -> D1
// If BPI and BFI aren't non-null, BPI/BFI will be updated accordingly.
bool SplitIndirectBrCriticalEdges(Function &F,
                                  BranchProbabilityInfo *BPI = nullptr,
                                  BlockFrequencyInfo *BFI = nullptr);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_BASICBLOCKUTILS_H
