//===- llvm/Analysis/LoopInfoImpl.h - Natural Loop Calculator ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the generic implementation of LoopInfo used for both Loops and
// MachineLoops.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LOOPINFOIMPL_H
#define LLVM_ANALYSIS_LOOPINFOIMPL_H

#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"

namespace llvm {

//===----------------------------------------------------------------------===//
// APIs for simple analysis of the loop. See header notes.

/// getExitingBlocks - Return all blocks inside the loop that have successors
/// outside of the loop.  These are the blocks _inside of the current loop_
/// which branch out.  The returned list is always unique.
///
template <class BlockT, class LoopT>
void LoopBase<BlockT, LoopT>::getExitingBlocks(
    SmallVectorImpl<BlockT *> &ExitingBlocks) const {
  assert(!isInvalid() && "Loop not in a valid state!");
  for (const auto BB : blocks())
    for (const auto &Succ : children<BlockT *>(BB))
      if (!contains(Succ)) {
        // Not in current loop? It must be an exit block.
        ExitingBlocks.push_back(BB);
        break;
      }
}

/// getExitingBlock - If getExitingBlocks would return exactly one block,
/// return that block. Otherwise return null.
template <class BlockT, class LoopT>
BlockT *LoopBase<BlockT, LoopT>::getExitingBlock() const {
  assert(!isInvalid() && "Loop not in a valid state!");
  SmallVector<BlockT *, 8> ExitingBlocks;
  getExitingBlocks(ExitingBlocks);
  if (ExitingBlocks.size() == 1)
    return ExitingBlocks[0];
  return nullptr;
}

/// getExitBlocks - Return all of the successor blocks of this loop.  These
/// are the blocks _outside of the current loop_ which are branched to.
///
template <class BlockT, class LoopT>
void LoopBase<BlockT, LoopT>::getExitBlocks(
    SmallVectorImpl<BlockT *> &ExitBlocks) const {
  assert(!isInvalid() && "Loop not in a valid state!");
  for (const auto BB : blocks())
    for (const auto &Succ : children<BlockT *>(BB))
      if (!contains(Succ))
        // Not in current loop? It must be an exit block.
        ExitBlocks.push_back(Succ);
}

/// getExitBlock - If getExitBlocks would return exactly one block,
/// return that block. Otherwise return null.
template <class BlockT, class LoopT>
BlockT *LoopBase<BlockT, LoopT>::getExitBlock() const {
  assert(!isInvalid() && "Loop not in a valid state!");
  SmallVector<BlockT *, 8> ExitBlocks;
  getExitBlocks(ExitBlocks);
  if (ExitBlocks.size() == 1)
    return ExitBlocks[0];
  return nullptr;
}

template <class BlockT, class LoopT>
bool LoopBase<BlockT, LoopT>::hasDedicatedExits() const {
  // Each predecessor of each exit block of a normal loop is contained
  // within the loop.
  SmallVector<BlockT *, 4> ExitBlocks;
  getExitBlocks(ExitBlocks);
  for (BlockT *EB : ExitBlocks)
    for (BlockT *Predecessor : children<Inverse<BlockT *>>(EB))
      if (!contains(Predecessor))
        return false;
  // All the requirements are met.
  return true;
}

template <class BlockT, class LoopT>
void LoopBase<BlockT, LoopT>::getUniqueExitBlocks(
    SmallVectorImpl<BlockT *> &ExitBlocks) const {
  typedef GraphTraits<BlockT *> BlockTraits;
  typedef GraphTraits<Inverse<BlockT *>> InvBlockTraits;

  assert(hasDedicatedExits() &&
         "getUniqueExitBlocks assumes the loop has canonical form exits!");

  SmallVector<BlockT *, 32> SwitchExitBlocks;
  for (BlockT *Block : this->blocks()) {
    SwitchExitBlocks.clear();
    for (BlockT *Successor : children<BlockT *>(Block)) {
      // If block is inside the loop then it is not an exit block.
      if (contains(Successor))
        continue;

      BlockT *FirstPred = *InvBlockTraits::child_begin(Successor);

      // If current basic block is this exit block's first predecessor then only
      // insert exit block in to the output ExitBlocks vector. This ensures that
      // same exit block is not inserted twice into ExitBlocks vector.
      if (Block != FirstPred)
        continue;

      // If a terminator has more then two successors, for example SwitchInst,
      // then it is possible that there are multiple edges from current block to
      // one exit block.
      if (std::distance(BlockTraits::child_begin(Block),
                        BlockTraits::child_end(Block)) <= 2) {
        ExitBlocks.push_back(Successor);
        continue;
      }

      // In case of multiple edges from current block to exit block, collect
      // only one edge in ExitBlocks. Use switchExitBlocks to keep track of
      // duplicate edges.
      if (!is_contained(SwitchExitBlocks, Successor)) {
        SwitchExitBlocks.push_back(Successor);
        ExitBlocks.push_back(Successor);
      }
    }
  }
}

template <class BlockT, class LoopT>
BlockT *LoopBase<BlockT, LoopT>::getUniqueExitBlock() const {
  SmallVector<BlockT *, 8> UniqueExitBlocks;
  getUniqueExitBlocks(UniqueExitBlocks);
  if (UniqueExitBlocks.size() == 1)
    return UniqueExitBlocks[0];
  return nullptr;
}

/// getExitEdges - Return all pairs of (_inside_block_,_outside_block_).
template <class BlockT, class LoopT>
void LoopBase<BlockT, LoopT>::getExitEdges(
    SmallVectorImpl<Edge> &ExitEdges) const {
  assert(!isInvalid() && "Loop not in a valid state!");
  for (const auto BB : blocks())
    for (const auto &Succ : children<BlockT *>(BB))
      if (!contains(Succ))
        // Not in current loop? It must be an exit block.
        ExitEdges.emplace_back(BB, Succ);
}

/// getLoopPreheader - If there is a preheader for this loop, return it.  A
/// loop has a preheader if there is only one edge to the header of the loop
/// from outside of the loop and it is legal to hoist instructions into the
/// predecessor. If this is the case, the block branching to the header of the
/// loop is the preheader node.
///
/// This method returns null if there is no preheader for the loop.
///
template <class BlockT, class LoopT>
BlockT *LoopBase<BlockT, LoopT>::getLoopPreheader() const {
  assert(!isInvalid() && "Loop not in a valid state!");
  // Keep track of nodes outside the loop branching to the header...
  BlockT *Out = getLoopPredecessor();
  if (!Out)
    return nullptr;

  // Make sure we are allowed to hoist instructions into the predecessor.
  if (!Out->isLegalToHoistInto())
    return nullptr;

  // Make sure there is only one exit out of the preheader.
  typedef GraphTraits<BlockT *> BlockTraits;
  typename BlockTraits::ChildIteratorType SI = BlockTraits::child_begin(Out);
  ++SI;
  if (SI != BlockTraits::child_end(Out))
    return nullptr; // Multiple exits from the block, must not be a preheader.

  // The predecessor has exactly one successor, so it is a preheader.
  return Out;
}

/// getLoopPredecessor - If the given loop's header has exactly one unique
/// predecessor outside the loop, return it. Otherwise return null.
/// This is less strict that the loop "preheader" concept, which requires
/// the predecessor to have exactly one successor.
///
template <class BlockT, class LoopT>
BlockT *LoopBase<BlockT, LoopT>::getLoopPredecessor() const {
  assert(!isInvalid() && "Loop not in a valid state!");
  // Keep track of nodes outside the loop branching to the header...
  BlockT *Out = nullptr;

  // Loop over the predecessors of the header node...
  BlockT *Header = getHeader();
  for (const auto Pred : children<Inverse<BlockT *>>(Header)) {
    if (!contains(Pred)) { // If the block is not in the loop...
      if (Out && Out != Pred)
        return nullptr; // Multiple predecessors outside the loop
      Out = Pred;
    }
  }

  // Make sure there is only one exit out of the preheader.
  assert(Out && "Header of loop has no predecessors from outside loop?");
  return Out;
}

/// getLoopLatch - If there is a single latch block for this loop, return it.
/// A latch block is a block that contains a branch back to the header.
template <class BlockT, class LoopT>
BlockT *LoopBase<BlockT, LoopT>::getLoopLatch() const {
  assert(!isInvalid() && "Loop not in a valid state!");
  BlockT *Header = getHeader();
  BlockT *Latch = nullptr;
  for (const auto Pred : children<Inverse<BlockT *>>(Header)) {
    if (contains(Pred)) {
      if (Latch)
        return nullptr;
      Latch = Pred;
    }
  }

  return Latch;
}

//===----------------------------------------------------------------------===//
// APIs for updating loop information after changing the CFG
//

/// addBasicBlockToLoop - This method is used by other analyses to update loop
/// information.  NewBB is set to be a new member of the current loop.
/// Because of this, it is added as a member of all parent loops, and is added
/// to the specified LoopInfo object as being in the current basic block.  It
/// is not valid to replace the loop header with this method.
///
template <class BlockT, class LoopT>
void LoopBase<BlockT, LoopT>::addBasicBlockToLoop(
    BlockT *NewBB, LoopInfoBase<BlockT, LoopT> &LIB) {
  assert(!isInvalid() && "Loop not in a valid state!");
#ifndef NDEBUG
  if (!Blocks.empty()) {
    auto SameHeader = LIB[getHeader()];
    assert(contains(SameHeader) && getHeader() == SameHeader->getHeader() &&
           "Incorrect LI specified for this loop!");
  }
#endif
  assert(NewBB && "Cannot add a null basic block to the loop!");
  assert(!LIB[NewBB] && "BasicBlock already in the loop!");

  LoopT *L = static_cast<LoopT *>(this);

  // Add the loop mapping to the LoopInfo object...
  LIB.BBMap[NewBB] = L;

  // Add the basic block to this loop and all parent loops...
  while (L) {
    L->addBlockEntry(NewBB);
    L = L->getParentLoop();
  }
}

/// replaceChildLoopWith - This is used when splitting loops up.  It replaces
/// the OldChild entry in our children list with NewChild, and updates the
/// parent pointer of OldChild to be null and the NewChild to be this loop.
/// This updates the loop depth of the new child.
template <class BlockT, class LoopT>
void LoopBase<BlockT, LoopT>::replaceChildLoopWith(LoopT *OldChild,
                                                   LoopT *NewChild) {
  assert(!isInvalid() && "Loop not in a valid state!");
  assert(OldChild->ParentLoop == this && "This loop is already broken!");
  assert(!NewChild->ParentLoop && "NewChild already has a parent!");
  typename std::vector<LoopT *>::iterator I = find(SubLoops, OldChild);
  assert(I != SubLoops.end() && "OldChild not in loop!");
  *I = NewChild;
  OldChild->ParentLoop = nullptr;
  NewChild->ParentLoop = static_cast<LoopT *>(this);
}

/// verifyLoop - Verify loop structure
template <class BlockT, class LoopT>
void LoopBase<BlockT, LoopT>::verifyLoop() const {
  assert(!isInvalid() && "Loop not in a valid state!");
#ifndef NDEBUG
  assert(!Blocks.empty() && "Loop header is missing");

  // Setup for using a depth-first iterator to visit every block in the loop.
  SmallVector<BlockT *, 8> ExitBBs;
  getExitBlocks(ExitBBs);
  df_iterator_default_set<BlockT *> VisitSet;
  VisitSet.insert(ExitBBs.begin(), ExitBBs.end());
  df_ext_iterator<BlockT *, df_iterator_default_set<BlockT *>>
      BI = df_ext_begin(getHeader(), VisitSet),
      BE = df_ext_end(getHeader(), VisitSet);

  // Keep track of the BBs visited.
  SmallPtrSet<BlockT *, 8> VisitedBBs;

  // Check the individual blocks.
  for (; BI != BE; ++BI) {
    BlockT *BB = *BI;

    assert(std::any_of(GraphTraits<BlockT *>::child_begin(BB),
                       GraphTraits<BlockT *>::child_end(BB),
                       [&](BlockT *B) { return contains(B); }) &&
           "Loop block has no in-loop successors!");

    assert(std::any_of(GraphTraits<Inverse<BlockT *>>::child_begin(BB),
                       GraphTraits<Inverse<BlockT *>>::child_end(BB),
                       [&](BlockT *B) { return contains(B); }) &&
           "Loop block has no in-loop predecessors!");

    SmallVector<BlockT *, 2> OutsideLoopPreds;
    std::for_each(GraphTraits<Inverse<BlockT *>>::child_begin(BB),
                  GraphTraits<Inverse<BlockT *>>::child_end(BB),
                  [&](BlockT *B) {
                    if (!contains(B))
                      OutsideLoopPreds.push_back(B);
                  });

    if (BB == getHeader()) {
      assert(!OutsideLoopPreds.empty() && "Loop is unreachable!");
    } else if (!OutsideLoopPreds.empty()) {
      // A non-header loop shouldn't be reachable from outside the loop,
      // though it is permitted if the predecessor is not itself actually
      // reachable.
      BlockT *EntryBB = &BB->getParent()->front();
      for (BlockT *CB : depth_first(EntryBB))
        for (unsigned i = 0, e = OutsideLoopPreds.size(); i != e; ++i)
          assert(CB != OutsideLoopPreds[i] &&
                 "Loop has multiple entry points!");
    }
    assert(BB != &getHeader()->getParent()->front() &&
           "Loop contains function entry block!");

    VisitedBBs.insert(BB);
  }

  if (VisitedBBs.size() != getNumBlocks()) {
    dbgs() << "The following blocks are unreachable in the loop: ";
    for (auto BB : Blocks) {
      if (!VisitedBBs.count(BB)) {
        dbgs() << *BB << "\n";
      }
    }
    assert(false && "Unreachable block in loop");
  }

  // Check the subloops.
  for (iterator I = begin(), E = end(); I != E; ++I)
    // Each block in each subloop should be contained within this loop.
    for (block_iterator BI = (*I)->block_begin(), BE = (*I)->block_end();
         BI != BE; ++BI) {
      assert(contains(*BI) &&
             "Loop does not contain all the blocks of a subloop!");
    }

  // Check the parent loop pointer.
  if (ParentLoop) {
    assert(is_contained(*ParentLoop, this) &&
           "Loop is not a subloop of its parent!");
  }
#endif
}

/// verifyLoop - Verify loop structure of this loop and all nested loops.
template <class BlockT, class LoopT>
void LoopBase<BlockT, LoopT>::verifyLoopNest(
    DenseSet<const LoopT *> *Loops) const {
  assert(!isInvalid() && "Loop not in a valid state!");
  Loops->insert(static_cast<const LoopT *>(this));
  // Verify this loop.
  verifyLoop();
  // Verify the subloops.
  for (iterator I = begin(), E = end(); I != E; ++I)
    (*I)->verifyLoopNest(Loops);
}

template <class BlockT, class LoopT>
void LoopBase<BlockT, LoopT>::print(raw_ostream &OS, unsigned Depth,
                                    bool Verbose) const {
  OS.indent(Depth * 2);
  if (static_cast<const LoopT *>(this)->isAnnotatedParallel())
    OS << "Parallel ";
  OS << "Loop at depth " << getLoopDepth() << " containing: ";

  BlockT *H = getHeader();
  for (unsigned i = 0; i < getBlocks().size(); ++i) {
    BlockT *BB = getBlocks()[i];
    if (!Verbose) {
      if (i)
        OS << ",";
      BB->printAsOperand(OS, false);
    } else
      OS << "\n";

    if (BB == H)
      OS << "<header>";
    if (isLoopLatch(BB))
      OS << "<latch>";
    if (isLoopExiting(BB))
      OS << "<exiting>";
    if (Verbose)
      BB->print(OS);
  }
  OS << "\n";

  for (iterator I = begin(), E = end(); I != E; ++I)
    (*I)->print(OS, Depth + 2);
}

//===----------------------------------------------------------------------===//
/// Stable LoopInfo Analysis - Build a loop tree using stable iterators so the
/// result does / not depend on use list (block predecessor) order.
///

/// Discover a subloop with the specified backedges such that: All blocks within
/// this loop are mapped to this loop or a subloop. And all subloops within this
/// loop have their parent loop set to this loop or a subloop.
template <class BlockT, class LoopT>
static void discoverAndMapSubloop(LoopT *L, ArrayRef<BlockT *> Backedges,
                                  LoopInfoBase<BlockT, LoopT> *LI,
                                  const DomTreeBase<BlockT> &DomTree) {
  typedef GraphTraits<Inverse<BlockT *>> InvBlockTraits;

  unsigned NumBlocks = 0;
  unsigned NumSubloops = 0;

  // Perform a backward CFG traversal using a worklist.
  std::vector<BlockT *> ReverseCFGWorklist(Backedges.begin(), Backedges.end());
  while (!ReverseCFGWorklist.empty()) {
    BlockT *PredBB = ReverseCFGWorklist.back();
    ReverseCFGWorklist.pop_back();

    LoopT *Subloop = LI->getLoopFor(PredBB);
    if (!Subloop) {
      if (!DomTree.isReachableFromEntry(PredBB))
        continue;

      // This is an undiscovered block. Map it to the current loop.
      LI->changeLoopFor(PredBB, L);
      ++NumBlocks;
      if (PredBB == L->getHeader())
        continue;
      // Push all block predecessors on the worklist.
      ReverseCFGWorklist.insert(ReverseCFGWorklist.end(),
                                InvBlockTraits::child_begin(PredBB),
                                InvBlockTraits::child_end(PredBB));
    } else {
      // This is a discovered block. Find its outermost discovered loop.
      while (LoopT *Parent = Subloop->getParentLoop())
        Subloop = Parent;

      // If it is already discovered to be a subloop of this loop, continue.
      if (Subloop == L)
        continue;

      // Discover a subloop of this loop.
      Subloop->setParentLoop(L);
      ++NumSubloops;
      NumBlocks += Subloop->getBlocksVector().capacity();
      PredBB = Subloop->getHeader();
      // Continue traversal along predecessors that are not loop-back edges from
      // within this subloop tree itself. Note that a predecessor may directly
      // reach another subloop that is not yet discovered to be a subloop of
      // this loop, which we must traverse.
      for (const auto Pred : children<Inverse<BlockT *>>(PredBB)) {
        if (LI->getLoopFor(Pred) != Subloop)
          ReverseCFGWorklist.push_back(Pred);
      }
    }
  }
  L->getSubLoopsVector().reserve(NumSubloops);
  L->reserveBlocks(NumBlocks);
}

/// Populate all loop data in a stable order during a single forward DFS.
template <class BlockT, class LoopT> class PopulateLoopsDFS {
  typedef GraphTraits<BlockT *> BlockTraits;
  typedef typename BlockTraits::ChildIteratorType SuccIterTy;

  LoopInfoBase<BlockT, LoopT> *LI;

public:
  PopulateLoopsDFS(LoopInfoBase<BlockT, LoopT> *li) : LI(li) {}

  void traverse(BlockT *EntryBlock);

protected:
  void insertIntoLoop(BlockT *Block);
};

/// Top-level driver for the forward DFS within the loop.
template <class BlockT, class LoopT>
void PopulateLoopsDFS<BlockT, LoopT>::traverse(BlockT *EntryBlock) {
  for (BlockT *BB : post_order(EntryBlock))
    insertIntoLoop(BB);
}

/// Add a single Block to its ancestor loops in PostOrder. If the block is a
/// subloop header, add the subloop to its parent in PostOrder, then reverse the
/// Block and Subloop vectors of the now complete subloop to achieve RPO.
template <class BlockT, class LoopT>
void PopulateLoopsDFS<BlockT, LoopT>::insertIntoLoop(BlockT *Block) {
  LoopT *Subloop = LI->getLoopFor(Block);
  if (Subloop && Block == Subloop->getHeader()) {
    // We reach this point once per subloop after processing all the blocks in
    // the subloop.
    if (Subloop->getParentLoop())
      Subloop->getParentLoop()->getSubLoopsVector().push_back(Subloop);
    else
      LI->addTopLevelLoop(Subloop);

    // For convenience, Blocks and Subloops are inserted in postorder. Reverse
    // the lists, except for the loop header, which is always at the beginning.
    Subloop->reverseBlock(1);
    std::reverse(Subloop->getSubLoopsVector().begin(),
                 Subloop->getSubLoopsVector().end());

    Subloop = Subloop->getParentLoop();
  }
  for (; Subloop; Subloop = Subloop->getParentLoop())
    Subloop->addBlockEntry(Block);
}

/// Analyze LoopInfo discovers loops during a postorder DominatorTree traversal
/// interleaved with backward CFG traversals within each subloop
/// (discoverAndMapSubloop). The backward traversal skips inner subloops, so
/// this part of the algorithm is linear in the number of CFG edges. Subloop and
/// Block vectors are then populated during a single forward CFG traversal
/// (PopulateLoopDFS).
///
/// During the two CFG traversals each block is seen three times:
/// 1) Discovered and mapped by a reverse CFG traversal.
/// 2) Visited during a forward DFS CFG traversal.
/// 3) Reverse-inserted in the loop in postorder following forward DFS.
///
/// The Block vectors are inclusive, so step 3 requires loop-depth number of
/// insertions per block.
template <class BlockT, class LoopT>
void LoopInfoBase<BlockT, LoopT>::analyze(const DomTreeBase<BlockT> &DomTree) {
  // Postorder traversal of the dominator tree.
  const DomTreeNodeBase<BlockT> *DomRoot = DomTree.getRootNode();
  for (auto DomNode : post_order(DomRoot)) {

    BlockT *Header = DomNode->getBlock();
    SmallVector<BlockT *, 4> Backedges;

    // Check each predecessor of the potential loop header.
    for (const auto Backedge : children<Inverse<BlockT *>>(Header)) {
      // If Header dominates predBB, this is a new loop. Collect the backedges.
      if (DomTree.dominates(Header, Backedge) &&
          DomTree.isReachableFromEntry(Backedge)) {
        Backedges.push_back(Backedge);
      }
    }
    // Perform a backward CFG traversal to discover and map blocks in this loop.
    if (!Backedges.empty()) {
      LoopT *L = AllocateLoop(Header);
      discoverAndMapSubloop(L, ArrayRef<BlockT *>(Backedges), this, DomTree);
    }
  }
  // Perform a single forward CFG traversal to populate block and subloop
  // vectors for all loops.
  PopulateLoopsDFS<BlockT, LoopT> DFS(this);
  DFS.traverse(DomRoot->getBlock());
}

template <class BlockT, class LoopT>
SmallVector<LoopT *, 4> LoopInfoBase<BlockT, LoopT>::getLoopsInPreorder() {
  SmallVector<LoopT *, 4> PreOrderLoops, PreOrderWorklist;
  // The outer-most loop actually goes into the result in the same relative
  // order as we walk it. But LoopInfo stores the top level loops in reverse
  // program order so for here we reverse it to get forward program order.
  // FIXME: If we change the order of LoopInfo we will want to remove the
  // reverse here.
  for (LoopT *RootL : reverse(*this)) {
    assert(PreOrderWorklist.empty() &&
           "Must start with an empty preorder walk worklist.");
    PreOrderWorklist.push_back(RootL);
    do {
      LoopT *L = PreOrderWorklist.pop_back_val();
      // Sub-loops are stored in forward program order, but will process the
      // worklist backwards so append them in reverse order.
      PreOrderWorklist.append(L->rbegin(), L->rend());
      PreOrderLoops.push_back(L);
    } while (!PreOrderWorklist.empty());
  }

  return PreOrderLoops;
}

template <class BlockT, class LoopT>
SmallVector<LoopT *, 4>
LoopInfoBase<BlockT, LoopT>::getLoopsInReverseSiblingPreorder() {
  SmallVector<LoopT *, 4> PreOrderLoops, PreOrderWorklist;
  // The outer-most loop actually goes into the result in the same relative
  // order as we walk it. LoopInfo stores the top level loops in reverse
  // program order so we walk in order here.
  // FIXME: If we change the order of LoopInfo we will want to add a reverse
  // here.
  for (LoopT *RootL : *this) {
    assert(PreOrderWorklist.empty() &&
           "Must start with an empty preorder walk worklist.");
    PreOrderWorklist.push_back(RootL);
    do {
      LoopT *L = PreOrderWorklist.pop_back_val();
      // Sub-loops are stored in forward program order, but will process the
      // worklist backwards so we can just append them in order.
      PreOrderWorklist.append(L->begin(), L->end());
      PreOrderLoops.push_back(L);
    } while (!PreOrderWorklist.empty());
  }

  return PreOrderLoops;
}

// Debugging
template <class BlockT, class LoopT>
void LoopInfoBase<BlockT, LoopT>::print(raw_ostream &OS) const {
  for (unsigned i = 0; i < TopLevelLoops.size(); ++i)
    TopLevelLoops[i]->print(OS);
#if 0
  for (DenseMap<BasicBlock*, LoopT*>::const_iterator I = BBMap.begin(),
         E = BBMap.end(); I != E; ++I)
    OS << "BB '" << I->first->getName() << "' level = "
       << I->second->getLoopDepth() << "\n";
#endif
}

template <typename T>
bool compareVectors(std::vector<T> &BB1, std::vector<T> &BB2) {
  llvm::sort(BB1);
  llvm::sort(BB2);
  return BB1 == BB2;
}

template <class BlockT, class LoopT>
void addInnerLoopsToHeadersMap(DenseMap<BlockT *, const LoopT *> &LoopHeaders,
                               const LoopInfoBase<BlockT, LoopT> &LI,
                               const LoopT &L) {
  LoopHeaders[L.getHeader()] = &L;
  for (LoopT *SL : L)
    addInnerLoopsToHeadersMap(LoopHeaders, LI, *SL);
}

#ifndef NDEBUG
template <class BlockT, class LoopT>
static void compareLoops(const LoopT *L, const LoopT *OtherL,
                         DenseMap<BlockT *, const LoopT *> &OtherLoopHeaders) {
  BlockT *H = L->getHeader();
  BlockT *OtherH = OtherL->getHeader();
  assert(H == OtherH &&
         "Mismatched headers even though found in the same map entry!");

  assert(L->getLoopDepth() == OtherL->getLoopDepth() &&
         "Mismatched loop depth!");
  const LoopT *ParentL = L, *OtherParentL = OtherL;
  do {
    assert(ParentL->getHeader() == OtherParentL->getHeader() &&
           "Mismatched parent loop headers!");
    ParentL = ParentL->getParentLoop();
    OtherParentL = OtherParentL->getParentLoop();
  } while (ParentL);

  for (const LoopT *SubL : *L) {
    BlockT *SubH = SubL->getHeader();
    const LoopT *OtherSubL = OtherLoopHeaders.lookup(SubH);
    assert(OtherSubL && "Inner loop is missing in computed loop info!");
    OtherLoopHeaders.erase(SubH);
    compareLoops(SubL, OtherSubL, OtherLoopHeaders);
  }

  std::vector<BlockT *> BBs = L->getBlocks();
  std::vector<BlockT *> OtherBBs = OtherL->getBlocks();
  assert(compareVectors(BBs, OtherBBs) &&
         "Mismatched basic blocks in the loops!");

  const SmallPtrSetImpl<const BlockT *> &BlocksSet = L->getBlocksSet();
  const SmallPtrSetImpl<const BlockT *> &OtherBlocksSet = L->getBlocksSet();
  assert(BlocksSet.size() == OtherBlocksSet.size() &&
         std::all_of(BlocksSet.begin(), BlocksSet.end(),
                     [&OtherBlocksSet](const BlockT *BB) {
                       return OtherBlocksSet.count(BB);
                     }) &&
         "Mismatched basic blocks in BlocksSets!");
}
#endif

template <class BlockT, class LoopT>
void LoopInfoBase<BlockT, LoopT>::verify(
    const DomTreeBase<BlockT> &DomTree) const {
  DenseSet<const LoopT *> Loops;
  for (iterator I = begin(), E = end(); I != E; ++I) {
    assert(!(*I)->getParentLoop() && "Top-level loop has a parent!");
    (*I)->verifyLoopNest(&Loops);
  }

// Verify that blocks are mapped to valid loops.
#ifndef NDEBUG
  for (auto &Entry : BBMap) {
    const BlockT *BB = Entry.first;
    LoopT *L = Entry.second;
    assert(Loops.count(L) && "orphaned loop");
    assert(L->contains(BB) && "orphaned block");
    for (LoopT *ChildLoop : *L)
      assert(!ChildLoop->contains(BB) &&
             "BBMap should point to the innermost loop containing BB");
  }

  // Recompute LoopInfo to verify loops structure.
  LoopInfoBase<BlockT, LoopT> OtherLI;
  OtherLI.analyze(DomTree);

  // Build a map we can use to move from our LI to the computed one. This
  // allows us to ignore the particular order in any layer of the loop forest
  // while still comparing the structure.
  DenseMap<BlockT *, const LoopT *> OtherLoopHeaders;
  for (LoopT *L : OtherLI)
    addInnerLoopsToHeadersMap(OtherLoopHeaders, OtherLI, *L);

  // Walk the top level loops and ensure there is a corresponding top-level
  // loop in the computed version and then recursively compare those loop
  // nests.
  for (LoopT *L : *this) {
    BlockT *Header = L->getHeader();
    const LoopT *OtherL = OtherLoopHeaders.lookup(Header);
    assert(OtherL && "Top level loop is missing in computed loop info!");
    // Now that we've matched this loop, erase its header from the map.
    OtherLoopHeaders.erase(Header);
    // And recursively compare these loops.
    compareLoops(L, OtherL, OtherLoopHeaders);
  }

  // Any remaining entries in the map are loops which were found when computing
  // a fresh LoopInfo but not present in the current one.
  if (!OtherLoopHeaders.empty()) {
    for (const auto &HeaderAndLoop : OtherLoopHeaders)
      dbgs() << "Found new loop: " << *HeaderAndLoop.second << "\n";
    llvm_unreachable("Found new loops when recomputing LoopInfo!");
  }
#endif
}

} // End llvm namespace

#endif
