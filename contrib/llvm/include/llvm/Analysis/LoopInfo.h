//===- llvm/Analysis/LoopInfo.h - Natural Loop Calculator -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the LoopInfo class that is used to identify natural loops
// and determine the loop depth of various nodes of the CFG.  A natural loop
// has exactly one entry-point, which is called the header. Note that natural
// loops may actually be several loops that share the same header node.
//
// This analysis calculates the nesting structure of loops in a function.  For
// each natural loop identified, this analysis identifies natural loops
// contained entirely within the loop and the basic blocks the make up the loop.
//
// It can calculate on the fly various bits of information, for example:
//
//  * whether there is a preheader for the loop
//  * the number of back edges to the header
//  * whether or not a particular block branches out of the loop
//  * the successor blocks of the loop
//  * the loop depth
//  * etc...
//
// Note that this analysis specifically identifies *Loops* not cycles or SCCs
// in the CFG.  There can be strongly connected components in the CFG which
// this analysis will not recognize and that will not be represented by a Loop
// instance.  In particular, a Loop might be inside such a non-loop SCC, or a
// non-loop SCC might contain a sub-SCC which is a Loop.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LOOPINFO_H
#define LLVM_ANALYSIS_LOOPINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Allocator.h"
#include <algorithm>
#include <utility>

namespace llvm {

class DominatorTree;
class LoopInfo;
class Loop;
class MDNode;
class PHINode;
class raw_ostream;
template <class N, bool IsPostDom> class DominatorTreeBase;
template <class N, class M> class LoopInfoBase;
template <class N, class M> class LoopBase;

//===----------------------------------------------------------------------===//
/// Instances of this class are used to represent loops that are detected in the
/// flow graph.
///
template <class BlockT, class LoopT> class LoopBase {
  LoopT *ParentLoop;
  // Loops contained entirely within this one.
  std::vector<LoopT *> SubLoops;

  // The list of blocks in this loop. First entry is the header node.
  std::vector<BlockT *> Blocks;

  SmallPtrSet<const BlockT *, 8> DenseBlockSet;

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  /// Indicator that this loop is no longer a valid loop.
  bool IsInvalid = false;
#endif

  LoopBase(const LoopBase<BlockT, LoopT> &) = delete;
  const LoopBase<BlockT, LoopT> &
  operator=(const LoopBase<BlockT, LoopT> &) = delete;

public:
  /// Return the nesting level of this loop.  An outer-most loop has depth 1,
  /// for consistency with loop depth values used for basic blocks, where depth
  /// 0 is used for blocks not inside any loops.
  unsigned getLoopDepth() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    unsigned D = 1;
    for (const LoopT *CurLoop = ParentLoop; CurLoop;
         CurLoop = CurLoop->ParentLoop)
      ++D;
    return D;
  }
  BlockT *getHeader() const { return getBlocks().front(); }
  LoopT *getParentLoop() const { return ParentLoop; }

  /// This is a raw interface for bypassing addChildLoop.
  void setParentLoop(LoopT *L) {
    assert(!isInvalid() && "Loop not in a valid state!");
    ParentLoop = L;
  }

  /// Return true if the specified loop is contained within in this loop.
  bool contains(const LoopT *L) const {
    assert(!isInvalid() && "Loop not in a valid state!");
    if (L == this)
      return true;
    if (!L)
      return false;
    return contains(L->getParentLoop());
  }

  /// Return true if the specified basic block is in this loop.
  bool contains(const BlockT *BB) const {
    assert(!isInvalid() && "Loop not in a valid state!");
    return DenseBlockSet.count(BB);
  }

  /// Return true if the specified instruction is in this loop.
  template <class InstT> bool contains(const InstT *Inst) const {
    return contains(Inst->getParent());
  }

  /// Return the loops contained entirely within this loop.
  const std::vector<LoopT *> &getSubLoops() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    return SubLoops;
  }
  std::vector<LoopT *> &getSubLoopsVector() {
    assert(!isInvalid() && "Loop not in a valid state!");
    return SubLoops;
  }
  typedef typename std::vector<LoopT *>::const_iterator iterator;
  typedef
      typename std::vector<LoopT *>::const_reverse_iterator reverse_iterator;
  iterator begin() const { return getSubLoops().begin(); }
  iterator end() const { return getSubLoops().end(); }
  reverse_iterator rbegin() const { return getSubLoops().rbegin(); }
  reverse_iterator rend() const { return getSubLoops().rend(); }
  bool empty() const { return getSubLoops().empty(); }

  /// Get a list of the basic blocks which make up this loop.
  ArrayRef<BlockT *> getBlocks() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    return Blocks;
  }
  typedef typename ArrayRef<BlockT *>::const_iterator block_iterator;
  block_iterator block_begin() const { return getBlocks().begin(); }
  block_iterator block_end() const { return getBlocks().end(); }
  inline iterator_range<block_iterator> blocks() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    return make_range(block_begin(), block_end());
  }

  /// Get the number of blocks in this loop in constant time.
  /// Invalidate the loop, indicating that it is no longer a loop.
  unsigned getNumBlocks() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    return Blocks.size();
  }

  /// Return a direct, mutable handle to the blocks vector so that we can
  /// mutate it efficiently with techniques like `std::remove`.
  std::vector<BlockT *> &getBlocksVector() {
    assert(!isInvalid() && "Loop not in a valid state!");
    return Blocks;
  }
  /// Return a direct, mutable handle to the blocks set so that we can
  /// mutate it efficiently.
  SmallPtrSetImpl<const BlockT *> &getBlocksSet() {
    assert(!isInvalid() && "Loop not in a valid state!");
    return DenseBlockSet;
  }

  /// Return a direct, immutable handle to the blocks set.
  const SmallPtrSetImpl<const BlockT *> &getBlocksSet() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    return DenseBlockSet;
  }

  /// Return true if this loop is no longer valid.  The only valid use of this
  /// helper is "assert(L.isInvalid())" or equivalent, since IsInvalid is set to
  /// true by the destructor.  In other words, if this accessor returns true,
  /// the caller has already triggered UB by calling this accessor; and so it
  /// can only be called in a context where a return value of true indicates a
  /// programmer error.
  bool isInvalid() const {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    return IsInvalid;
#else
    return false;
#endif
  }

  /// True if terminator in the block can branch to another block that is
  /// outside of the current loop.
  bool isLoopExiting(const BlockT *BB) const {
    assert(!isInvalid() && "Loop not in a valid state!");
    for (const auto &Succ : children<const BlockT *>(BB)) {
      if (!contains(Succ))
        return true;
    }
    return false;
  }

  /// Returns true if \p BB is a loop-latch.
  /// A latch block is a block that contains a branch back to the header.
  /// This function is useful when there are multiple latches in a loop
  /// because \fn getLoopLatch will return nullptr in that case.
  bool isLoopLatch(const BlockT *BB) const {
    assert(!isInvalid() && "Loop not in a valid state!");
    assert(contains(BB) && "block does not belong to the loop");

    BlockT *Header = getHeader();
    auto PredBegin = GraphTraits<Inverse<BlockT *>>::child_begin(Header);
    auto PredEnd = GraphTraits<Inverse<BlockT *>>::child_end(Header);
    return std::find(PredBegin, PredEnd, BB) != PredEnd;
  }

  /// Calculate the number of back edges to the loop header.
  unsigned getNumBackEdges() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    unsigned NumBackEdges = 0;
    BlockT *H = getHeader();

    for (const auto Pred : children<Inverse<BlockT *>>(H))
      if (contains(Pred))
        ++NumBackEdges;

    return NumBackEdges;
  }

  //===--------------------------------------------------------------------===//
  // APIs for simple analysis of the loop.
  //
  // Note that all of these methods can fail on general loops (ie, there may not
  // be a preheader, etc).  For best success, the loop simplification and
  // induction variable canonicalization pass should be used to normalize loops
  // for easy analysis.  These methods assume canonical loops.

  /// Return all blocks inside the loop that have successors outside of the
  /// loop. These are the blocks _inside of the current loop_ which branch out.
  /// The returned list is always unique.
  void getExitingBlocks(SmallVectorImpl<BlockT *> &ExitingBlocks) const;

  /// If getExitingBlocks would return exactly one block, return that block.
  /// Otherwise return null.
  BlockT *getExitingBlock() const;

  /// Return all of the successor blocks of this loop. These are the blocks
  /// _outside of the current loop_ which are branched to.
  void getExitBlocks(SmallVectorImpl<BlockT *> &ExitBlocks) const;

  /// If getExitBlocks would return exactly one block, return that block.
  /// Otherwise return null.
  BlockT *getExitBlock() const;

  /// Return true if no exit block for the loop has a predecessor that is
  /// outside the loop.
  bool hasDedicatedExits() const;

  /// Return all unique successor blocks of this loop.
  /// These are the blocks _outside of the current loop_ which are branched to.
  /// This assumes that loop exits are in canonical form, i.e. all exits are
  /// dedicated exits.
  void getUniqueExitBlocks(SmallVectorImpl<BlockT *> &ExitBlocks) const;

  /// If getUniqueExitBlocks would return exactly one block, return that block.
  /// Otherwise return null.
  BlockT *getUniqueExitBlock() const;

  /// Edge type.
  typedef std::pair<const BlockT *, const BlockT *> Edge;

  /// Return all pairs of (_inside_block_,_outside_block_).
  void getExitEdges(SmallVectorImpl<Edge> &ExitEdges) const;

  /// If there is a preheader for this loop, return it. A loop has a preheader
  /// if there is only one edge to the header of the loop from outside of the
  /// loop. If this is the case, the block branching to the header of the loop
  /// is the preheader node.
  ///
  /// This method returns null if there is no preheader for the loop.
  BlockT *getLoopPreheader() const;

  /// If the given loop's header has exactly one unique predecessor outside the
  /// loop, return it. Otherwise return null.
  ///  This is less strict that the loop "preheader" concept, which requires
  /// the predecessor to have exactly one successor.
  BlockT *getLoopPredecessor() const;

  /// If there is a single latch block for this loop, return it.
  /// A latch block is a block that contains a branch back to the header.
  BlockT *getLoopLatch() const;

  /// Return all loop latch blocks of this loop. A latch block is a block that
  /// contains a branch back to the header.
  void getLoopLatches(SmallVectorImpl<BlockT *> &LoopLatches) const {
    assert(!isInvalid() && "Loop not in a valid state!");
    BlockT *H = getHeader();
    for (const auto Pred : children<Inverse<BlockT *>>(H))
      if (contains(Pred))
        LoopLatches.push_back(Pred);
  }

  //===--------------------------------------------------------------------===//
  // APIs for updating loop information after changing the CFG
  //

  /// This method is used by other analyses to update loop information.
  /// NewBB is set to be a new member of the current loop.
  /// Because of this, it is added as a member of all parent loops, and is added
  /// to the specified LoopInfo object as being in the current basic block.  It
  /// is not valid to replace the loop header with this method.
  void addBasicBlockToLoop(BlockT *NewBB, LoopInfoBase<BlockT, LoopT> &LI);

  /// This is used when splitting loops up. It replaces the OldChild entry in
  /// our children list with NewChild, and updates the parent pointer of
  /// OldChild to be null and the NewChild to be this loop.
  /// This updates the loop depth of the new child.
  void replaceChildLoopWith(LoopT *OldChild, LoopT *NewChild);

  /// Add the specified loop to be a child of this loop.
  /// This updates the loop depth of the new child.
  void addChildLoop(LoopT *NewChild) {
    assert(!isInvalid() && "Loop not in a valid state!");
    assert(!NewChild->ParentLoop && "NewChild already has a parent!");
    NewChild->ParentLoop = static_cast<LoopT *>(this);
    SubLoops.push_back(NewChild);
  }

  /// This removes the specified child from being a subloop of this loop. The
  /// loop is not deleted, as it will presumably be inserted into another loop.
  LoopT *removeChildLoop(iterator I) {
    assert(!isInvalid() && "Loop not in a valid state!");
    assert(I != SubLoops.end() && "Cannot remove end iterator!");
    LoopT *Child = *I;
    assert(Child->ParentLoop == this && "Child is not a child of this loop!");
    SubLoops.erase(SubLoops.begin() + (I - begin()));
    Child->ParentLoop = nullptr;
    return Child;
  }

  /// This removes the specified child from being a subloop of this loop. The
  /// loop is not deleted, as it will presumably be inserted into another loop.
  LoopT *removeChildLoop(LoopT *Child) {
    return removeChildLoop(llvm::find(*this, Child));
  }

  /// This adds a basic block directly to the basic block list.
  /// This should only be used by transformations that create new loops.  Other
  /// transformations should use addBasicBlockToLoop.
  void addBlockEntry(BlockT *BB) {
    assert(!isInvalid() && "Loop not in a valid state!");
    Blocks.push_back(BB);
    DenseBlockSet.insert(BB);
  }

  /// interface to reverse Blocks[from, end of loop] in this loop
  void reverseBlock(unsigned from) {
    assert(!isInvalid() && "Loop not in a valid state!");
    std::reverse(Blocks.begin() + from, Blocks.end());
  }

  /// interface to do reserve() for Blocks
  void reserveBlocks(unsigned size) {
    assert(!isInvalid() && "Loop not in a valid state!");
    Blocks.reserve(size);
  }

  /// This method is used to move BB (which must be part of this loop) to be the
  /// loop header of the loop (the block that dominates all others).
  void moveToHeader(BlockT *BB) {
    assert(!isInvalid() && "Loop not in a valid state!");
    if (Blocks[0] == BB)
      return;
    for (unsigned i = 0;; ++i) {
      assert(i != Blocks.size() && "Loop does not contain BB!");
      if (Blocks[i] == BB) {
        Blocks[i] = Blocks[0];
        Blocks[0] = BB;
        return;
      }
    }
  }

  /// This removes the specified basic block from the current loop, updating the
  /// Blocks as appropriate. This does not update the mapping in the LoopInfo
  /// class.
  void removeBlockFromLoop(BlockT *BB) {
    assert(!isInvalid() && "Loop not in a valid state!");
    auto I = find(Blocks, BB);
    assert(I != Blocks.end() && "N is not in this list!");
    Blocks.erase(I);

    DenseBlockSet.erase(BB);
  }

  /// Verify loop structure
  void verifyLoop() const;

  /// Verify loop structure of this loop and all nested loops.
  void verifyLoopNest(DenseSet<const LoopT *> *Loops) const;

  /// Returns true if the loop is annotated parallel.
  ///
  /// Derived classes can override this method using static template
  /// polymorphism.
  bool isAnnotatedParallel() const { return false; }

  /// Print loop with all the BBs inside it.
  void print(raw_ostream &OS, unsigned Depth = 0, bool Verbose = false) const;

protected:
  friend class LoopInfoBase<BlockT, LoopT>;

  /// This creates an empty loop.
  LoopBase() : ParentLoop(nullptr) {}

  explicit LoopBase(BlockT *BB) : ParentLoop(nullptr) {
    Blocks.push_back(BB);
    DenseBlockSet.insert(BB);
  }

  // Since loop passes like SCEV are allowed to key analysis results off of
  // `Loop` pointers, we cannot re-use pointers within a loop pass manager.
  // This means loop passes should not be `delete` ing `Loop` objects directly
  // (and risk a later `Loop` allocation re-using the address of a previous one)
  // but should be using LoopInfo::markAsRemoved, which keeps around the `Loop`
  // pointer till the end of the lifetime of the `LoopInfo` object.
  //
  // To make it easier to follow this rule, we mark the destructor as
  // non-public.
  ~LoopBase() {
    for (auto *SubLoop : SubLoops)
      SubLoop->~LoopT();

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    IsInvalid = true;
#endif
    SubLoops.clear();
    Blocks.clear();
    DenseBlockSet.clear();
    ParentLoop = nullptr;
  }
};

template <class BlockT, class LoopT>
raw_ostream &operator<<(raw_ostream &OS, const LoopBase<BlockT, LoopT> &Loop) {
  Loop.print(OS);
  return OS;
}

// Implementation in LoopInfoImpl.h
extern template class LoopBase<BasicBlock, Loop>;

/// Represents a single loop in the control flow graph.  Note that not all SCCs
/// in the CFG are necessarily loops.
class Loop : public LoopBase<BasicBlock, Loop> {
public:
  /// A range representing the start and end location of a loop.
  class LocRange {
    DebugLoc Start;
    DebugLoc End;

  public:
    LocRange() {}
    LocRange(DebugLoc Start) : Start(std::move(Start)), End(std::move(Start)) {}
    LocRange(DebugLoc Start, DebugLoc End)
        : Start(std::move(Start)), End(std::move(End)) {}

    const DebugLoc &getStart() const { return Start; }
    const DebugLoc &getEnd() const { return End; }

    /// Check for null.
    ///
    explicit operator bool() const { return Start && End; }
  };

  /// Return true if the specified value is loop invariant.
  bool isLoopInvariant(const Value *V) const;

  /// Return true if all the operands of the specified instruction are loop
  /// invariant.
  bool hasLoopInvariantOperands(const Instruction *I) const;

  /// If the given value is an instruction inside of the loop and it can be
  /// hoisted, do so to make it trivially loop-invariant.
  /// Return true if the value after any hoisting is loop invariant. This
  /// function can be used as a slightly more aggressive replacement for
  /// isLoopInvariant.
  ///
  /// If InsertPt is specified, it is the point to hoist instructions to.
  /// If null, the terminator of the loop preheader is used.
  bool makeLoopInvariant(Value *V, bool &Changed,
                         Instruction *InsertPt = nullptr) const;

  /// If the given instruction is inside of the loop and it can be hoisted, do
  /// so to make it trivially loop-invariant.
  /// Return true if the instruction after any hoisting is loop invariant. This
  /// function can be used as a slightly more aggressive replacement for
  /// isLoopInvariant.
  ///
  /// If InsertPt is specified, it is the point to hoist instructions to.
  /// If null, the terminator of the loop preheader is used.
  ///
  bool makeLoopInvariant(Instruction *I, bool &Changed,
                         Instruction *InsertPt = nullptr) const;

  /// Check to see if the loop has a canonical induction variable: an integer
  /// recurrence that starts at 0 and increments by one each time through the
  /// loop. If so, return the phi node that corresponds to it.
  ///
  /// The IndVarSimplify pass transforms loops to have a canonical induction
  /// variable.
  ///
  PHINode *getCanonicalInductionVariable() const;

  /// Return true if the Loop is in LCSSA form.
  bool isLCSSAForm(DominatorTree &DT) const;

  /// Return true if this Loop and all inner subloops are in LCSSA form.
  bool isRecursivelyLCSSAForm(DominatorTree &DT, const LoopInfo &LI) const;

  /// Return true if the Loop is in the form that the LoopSimplify form
  /// transforms loops to, which is sometimes called normal form.
  bool isLoopSimplifyForm() const;

  /// Return true if the loop body is safe to clone in practice.
  bool isSafeToClone() const;

  /// Returns true if the loop is annotated parallel.
  ///
  /// A parallel loop can be assumed to not contain any dependencies between
  /// iterations by the compiler. That is, any loop-carried dependency checking
  /// can be skipped completely when parallelizing the loop on the target
  /// machine. Thus, if the parallel loop information originates from the
  /// programmer, e.g. via the OpenMP parallel for pragma, it is the
  /// programmer's responsibility to ensure there are no loop-carried
  /// dependencies. The final execution order of the instructions across
  /// iterations is not guaranteed, thus, the end result might or might not
  /// implement actual concurrent execution of instructions across multiple
  /// iterations.
  bool isAnnotatedParallel() const;

  /// Return the llvm.loop loop id metadata node for this loop if it is present.
  ///
  /// If this loop contains the same llvm.loop metadata on each branch to the
  /// header then the node is returned. If any latch instruction does not
  /// contain llvm.loop or if multiple latches contain different nodes then
  /// 0 is returned.
  MDNode *getLoopID() const;
  /// Set the llvm.loop loop id metadata for this loop.
  ///
  /// The LoopID metadata node will be added to each terminator instruction in
  /// the loop that branches to the loop header.
  ///
  /// The LoopID metadata node should have one or more operands and the first
  /// operand should be the node itself.
  void setLoopID(MDNode *LoopID) const;

  /// Add llvm.loop.unroll.disable to this loop's loop id metadata.
  ///
  /// Remove existing unroll metadata and add unroll disable metadata to
  /// indicate the loop has already been unrolled.  This prevents a loop
  /// from being unrolled more than is directed by a pragma if the loop
  /// unrolling pass is run more than once (which it generally is).
  void setLoopAlreadyUnrolled();

  void dump() const;
  void dumpVerbose() const;

  /// Return the debug location of the start of this loop.
  /// This looks for a BB terminating instruction with a known debug
  /// location by looking at the preheader and header blocks. If it
  /// cannot find a terminating instruction with location information,
  /// it returns an unknown location.
  DebugLoc getStartLoc() const;

  /// Return the source code span of the loop.
  LocRange getLocRange() const;

  StringRef getName() const {
    if (BasicBlock *Header = getHeader())
      if (Header->hasName())
        return Header->getName();
    return "<unnamed loop>";
  }

private:
  Loop() = default;

  friend class LoopInfoBase<BasicBlock, Loop>;
  friend class LoopBase<BasicBlock, Loop>;
  explicit Loop(BasicBlock *BB) : LoopBase<BasicBlock, Loop>(BB) {}
  ~Loop() = default;
};

//===----------------------------------------------------------------------===//
/// This class builds and contains all of the top-level loop
/// structures in the specified function.
///

template <class BlockT, class LoopT> class LoopInfoBase {
  // BBMap - Mapping of basic blocks to the inner most loop they occur in
  DenseMap<const BlockT *, LoopT *> BBMap;
  std::vector<LoopT *> TopLevelLoops;
  BumpPtrAllocator LoopAllocator;

  friend class LoopBase<BlockT, LoopT>;
  friend class LoopInfo;

  void operator=(const LoopInfoBase &) = delete;
  LoopInfoBase(const LoopInfoBase &) = delete;

public:
  LoopInfoBase() {}
  ~LoopInfoBase() { releaseMemory(); }

  LoopInfoBase(LoopInfoBase &&Arg)
      : BBMap(std::move(Arg.BBMap)),
        TopLevelLoops(std::move(Arg.TopLevelLoops)),
        LoopAllocator(std::move(Arg.LoopAllocator)) {
    // We have to clear the arguments top level loops as we've taken ownership.
    Arg.TopLevelLoops.clear();
  }
  LoopInfoBase &operator=(LoopInfoBase &&RHS) {
    BBMap = std::move(RHS.BBMap);

    for (auto *L : TopLevelLoops)
      L->~LoopT();

    TopLevelLoops = std::move(RHS.TopLevelLoops);
    LoopAllocator = std::move(RHS.LoopAllocator);
    RHS.TopLevelLoops.clear();
    return *this;
  }

  void releaseMemory() {
    BBMap.clear();

    for (auto *L : TopLevelLoops)
      L->~LoopT();
    TopLevelLoops.clear();
    LoopAllocator.Reset();
  }

  template <typename... ArgsTy> LoopT *AllocateLoop(ArgsTy &&... Args) {
    LoopT *Storage = LoopAllocator.Allocate<LoopT>();
    return new (Storage) LoopT(std::forward<ArgsTy>(Args)...);
  }

  /// iterator/begin/end - The interface to the top-level loops in the current
  /// function.
  ///
  typedef typename std::vector<LoopT *>::const_iterator iterator;
  typedef
      typename std::vector<LoopT *>::const_reverse_iterator reverse_iterator;
  iterator begin() const { return TopLevelLoops.begin(); }
  iterator end() const { return TopLevelLoops.end(); }
  reverse_iterator rbegin() const { return TopLevelLoops.rbegin(); }
  reverse_iterator rend() const { return TopLevelLoops.rend(); }
  bool empty() const { return TopLevelLoops.empty(); }

  /// Return all of the loops in the function in preorder across the loop
  /// nests, with siblings in forward program order.
  ///
  /// Note that because loops form a forest of trees, preorder is equivalent to
  /// reverse postorder.
  SmallVector<LoopT *, 4> getLoopsInPreorder();

  /// Return all of the loops in the function in preorder across the loop
  /// nests, with siblings in *reverse* program order.
  ///
  /// Note that because loops form a forest of trees, preorder is equivalent to
  /// reverse postorder.
  ///
  /// Also note that this is *not* a reverse preorder. Only the siblings are in
  /// reverse program order.
  SmallVector<LoopT *, 4> getLoopsInReverseSiblingPreorder();

  /// Return the inner most loop that BB lives in. If a basic block is in no
  /// loop (for example the entry node), null is returned.
  LoopT *getLoopFor(const BlockT *BB) const { return BBMap.lookup(BB); }

  /// Same as getLoopFor.
  const LoopT *operator[](const BlockT *BB) const { return getLoopFor(BB); }

  /// Return the loop nesting level of the specified block. A depth of 0 means
  /// the block is not inside any loop.
  unsigned getLoopDepth(const BlockT *BB) const {
    const LoopT *L = getLoopFor(BB);
    return L ? L->getLoopDepth() : 0;
  }

  // True if the block is a loop header node
  bool isLoopHeader(const BlockT *BB) const {
    const LoopT *L = getLoopFor(BB);
    return L && L->getHeader() == BB;
  }

  /// This removes the specified top-level loop from this loop info object.
  /// The loop is not deleted, as it will presumably be inserted into
  /// another loop.
  LoopT *removeLoop(iterator I) {
    assert(I != end() && "Cannot remove end iterator!");
    LoopT *L = *I;
    assert(!L->getParentLoop() && "Not a top-level loop!");
    TopLevelLoops.erase(TopLevelLoops.begin() + (I - begin()));
    return L;
  }

  /// Change the top-level loop that contains BB to the specified loop.
  /// This should be used by transformations that restructure the loop hierarchy
  /// tree.
  void changeLoopFor(BlockT *BB, LoopT *L) {
    if (!L) {
      BBMap.erase(BB);
      return;
    }
    BBMap[BB] = L;
  }

  /// Replace the specified loop in the top-level loops list with the indicated
  /// loop.
  void changeTopLevelLoop(LoopT *OldLoop, LoopT *NewLoop) {
    auto I = find(TopLevelLoops, OldLoop);
    assert(I != TopLevelLoops.end() && "Old loop not at top level!");
    *I = NewLoop;
    assert(!NewLoop->ParentLoop && !OldLoop->ParentLoop &&
           "Loops already embedded into a subloop!");
  }

  /// This adds the specified loop to the collection of top-level loops.
  void addTopLevelLoop(LoopT *New) {
    assert(!New->getParentLoop() && "Loop already in subloop!");
    TopLevelLoops.push_back(New);
  }

  /// This method completely removes BB from all data structures,
  /// including all of the Loop objects it is nested in and our mapping from
  /// BasicBlocks to loops.
  void removeBlock(BlockT *BB) {
    auto I = BBMap.find(BB);
    if (I != BBMap.end()) {
      for (LoopT *L = I->second; L; L = L->getParentLoop())
        L->removeBlockFromLoop(BB);

      BBMap.erase(I);
    }
  }

  // Internals

  static bool isNotAlreadyContainedIn(const LoopT *SubLoop,
                                      const LoopT *ParentLoop) {
    if (!SubLoop)
      return true;
    if (SubLoop == ParentLoop)
      return false;
    return isNotAlreadyContainedIn(SubLoop->getParentLoop(), ParentLoop);
  }

  /// Create the loop forest using a stable algorithm.
  void analyze(const DominatorTreeBase<BlockT, false> &DomTree);

  // Debugging
  void print(raw_ostream &OS) const;

  void verify(const DominatorTreeBase<BlockT, false> &DomTree) const;

  /// Destroy a loop that has been removed from the `LoopInfo` nest.
  ///
  /// This runs the destructor of the loop object making it invalid to
  /// reference afterward. The memory is retained so that the *pointer* to the
  /// loop remains valid.
  ///
  /// The caller is responsible for removing this loop from the loop nest and
  /// otherwise disconnecting it from the broader `LoopInfo` data structures.
  /// Callers that don't naturally handle this themselves should probably call
  /// `erase' instead.
  void destroy(LoopT *L) {
    L->~LoopT();

    // Since LoopAllocator is a BumpPtrAllocator, this Deallocate only poisons
    // \c L, but the pointer remains valid for non-dereferencing uses.
    LoopAllocator.Deallocate(L);
  }
};

// Implementation in LoopInfoImpl.h
extern template class LoopInfoBase<BasicBlock, Loop>;

class LoopInfo : public LoopInfoBase<BasicBlock, Loop> {
  typedef LoopInfoBase<BasicBlock, Loop> BaseT;

  friend class LoopBase<BasicBlock, Loop>;

  void operator=(const LoopInfo &) = delete;
  LoopInfo(const LoopInfo &) = delete;

public:
  LoopInfo() {}
  explicit LoopInfo(const DominatorTreeBase<BasicBlock, false> &DomTree);

  LoopInfo(LoopInfo &&Arg) : BaseT(std::move(static_cast<BaseT &>(Arg))) {}
  LoopInfo &operator=(LoopInfo &&RHS) {
    BaseT::operator=(std::move(static_cast<BaseT &>(RHS)));
    return *this;
  }

  /// Handle invalidation explicitly.
  bool invalidate(Function &F, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &);

  // Most of the public interface is provided via LoopInfoBase.

  /// Update LoopInfo after removing the last backedge from a loop. This updates
  /// the loop forest and parent loops for each block so that \c L is no longer
  /// referenced, but does not actually delete \c L immediately. The pointer
  /// will remain valid until this LoopInfo's memory is released.
  void erase(Loop *L);

  /// Returns true if replacing From with To everywhere is guaranteed to
  /// preserve LCSSA form.
  bool replacementPreservesLCSSAForm(Instruction *From, Value *To) {
    // Preserving LCSSA form is only problematic if the replacing value is an
    // instruction.
    Instruction *I = dyn_cast<Instruction>(To);
    if (!I)
      return true;
    // If both instructions are defined in the same basic block then replacement
    // cannot break LCSSA form.
    if (I->getParent() == From->getParent())
      return true;
    // If the instruction is not defined in a loop then it can safely replace
    // anything.
    Loop *ToLoop = getLoopFor(I->getParent());
    if (!ToLoop)
      return true;
    // If the replacing instruction is defined in the same loop as the original
    // instruction, or in a loop that contains it as an inner loop, then using
    // it as a replacement will not break LCSSA form.
    return ToLoop->contains(getLoopFor(From->getParent()));
  }

  /// Checks if moving a specific instruction can break LCSSA in any loop.
  ///
  /// Return true if moving \p Inst to before \p NewLoc will break LCSSA,
  /// assuming that the function containing \p Inst and \p NewLoc is currently
  /// in LCSSA form.
  bool movementPreservesLCSSAForm(Instruction *Inst, Instruction *NewLoc) {
    assert(Inst->getFunction() == NewLoc->getFunction() &&
           "Can't reason about IPO!");

    auto *OldBB = Inst->getParent();
    auto *NewBB = NewLoc->getParent();

    // Movement within the same loop does not break LCSSA (the equality check is
    // to avoid doing a hashtable lookup in case of intra-block movement).
    if (OldBB == NewBB)
      return true;

    auto *OldLoop = getLoopFor(OldBB);
    auto *NewLoop = getLoopFor(NewBB);

    if (OldLoop == NewLoop)
      return true;

    // Check if Outer contains Inner; with the null loop counting as the
    // "outermost" loop.
    auto Contains = [](const Loop *Outer, const Loop *Inner) {
      return !Outer || Outer->contains(Inner);
    };

    // To check that the movement of Inst to before NewLoc does not break LCSSA,
    // we need to check two sets of uses for possible LCSSA violations at
    // NewLoc: the users of NewInst, and the operands of NewInst.

    // If we know we're hoisting Inst out of an inner loop to an outer loop,
    // then the uses *of* Inst don't need to be checked.

    if (!Contains(NewLoop, OldLoop)) {
      for (Use &U : Inst->uses()) {
        auto *UI = cast<Instruction>(U.getUser());
        auto *UBB = isa<PHINode>(UI) ? cast<PHINode>(UI)->getIncomingBlock(U)
                                     : UI->getParent();
        if (UBB != NewBB && getLoopFor(UBB) != NewLoop)
          return false;
      }
    }

    // If we know we're sinking Inst from an outer loop into an inner loop, then
    // the *operands* of Inst don't need to be checked.

    if (!Contains(OldLoop, NewLoop)) {
      // See below on why we can't handle phi nodes here.
      if (isa<PHINode>(Inst))
        return false;

      for (Use &U : Inst->operands()) {
        auto *DefI = dyn_cast<Instruction>(U.get());
        if (!DefI)
          return false;

        // This would need adjustment if we allow Inst to be a phi node -- the
        // new use block won't simply be NewBB.

        auto *DefBlock = DefI->getParent();
        if (DefBlock != NewBB && getLoopFor(DefBlock) != NewLoop)
          return false;
      }
    }

    return true;
  }
};

// Allow clients to walk the list of nested loops...
template <> struct GraphTraits<const Loop *> {
  typedef const Loop *NodeRef;
  typedef LoopInfo::iterator ChildIteratorType;

  static NodeRef getEntryNode(const Loop *L) { return L; }
  static ChildIteratorType child_begin(NodeRef N) { return N->begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->end(); }
};

template <> struct GraphTraits<Loop *> {
  typedef Loop *NodeRef;
  typedef LoopInfo::iterator ChildIteratorType;

  static NodeRef getEntryNode(Loop *L) { return L; }
  static ChildIteratorType child_begin(NodeRef N) { return N->begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->end(); }
};

/// Analysis pass that exposes the \c LoopInfo for a function.
class LoopAnalysis : public AnalysisInfoMixin<LoopAnalysis> {
  friend AnalysisInfoMixin<LoopAnalysis>;
  static AnalysisKey Key;

public:
  typedef LoopInfo Result;

  LoopInfo run(Function &F, FunctionAnalysisManager &AM);
};

/// Printer pass for the \c LoopAnalysis results.
class LoopPrinterPass : public PassInfoMixin<LoopPrinterPass> {
  raw_ostream &OS;

public:
  explicit LoopPrinterPass(raw_ostream &OS) : OS(OS) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Verifier pass for the \c LoopAnalysis results.
struct LoopVerifierPass : public PassInfoMixin<LoopVerifierPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// The legacy pass manager's analysis pass to compute loop information.
class LoopInfoWrapperPass : public FunctionPass {
  LoopInfo LI;

public:
  static char ID; // Pass identification, replacement for typeid

  LoopInfoWrapperPass() : FunctionPass(ID) {
    initializeLoopInfoWrapperPassPass(*PassRegistry::getPassRegistry());
  }

  LoopInfo &getLoopInfo() { return LI; }
  const LoopInfo &getLoopInfo() const { return LI; }

  /// Calculate the natural loop information for a given function.
  bool runOnFunction(Function &F) override;

  void verifyAnalysis() const override;

  void releaseMemory() override { LI.releaseMemory(); }

  void print(raw_ostream &O, const Module *M = nullptr) const override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

/// Function to print a loop's contents as LLVM's text IR assembly.
void printLoop(Loop &L, raw_ostream &OS, const std::string &Banner = "");

/// Find and return the loop attribute node for the attribute @p Name in
/// @p LoopID. Return nullptr if there is no such attribute.
MDNode *findOptionMDForLoopID(MDNode *LoopID, StringRef Name);

/// Find string metadata for a loop.
///
/// Returns the MDNode where the first operand is the metadata's name. The
/// following operands are the metadata's values. If no metadata with @p Name is
/// found, return nullptr.
MDNode *findOptionMDForLoop(const Loop *TheLoop, StringRef Name);

/// Return whether an MDNode might represent an access group.
///
/// Access group metadata nodes have to be distinct and empty. Being
/// always-empty ensures that it never needs to be changed (which -- because
/// MDNodes are designed immutable -- would require creating a new MDNode). Note
/// that this is not a sufficient condition: not every distinct and empty NDNode
/// is representing an access group.
bool isValidAsAccessGroup(MDNode *AccGroup);

} // End llvm namespace

#endif
