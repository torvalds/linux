//===- SyncDependenceAnalysis.cpp - Divergent Branch Dependence Calculation
//--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements an algorithm that returns for a divergent branch
// the set of basic blocks whose phi nodes become divergent due to divergent
// control. These are the blocks that are reachable by two disjoint paths from
// the branch or loop exits that have a reaching path that is disjoint from a
// path to the loop latch.
//
// The SyncDependenceAnalysis is used in the DivergenceAnalysis to model
// control-induced divergence in phi nodes.
//
// -- Summary --
// The SyncDependenceAnalysis lazily computes sync dependences [3].
// The analysis evaluates the disjoint path criterion [2] by a reduction
// to SSA construction. The SSA construction algorithm is implemented as
// a simple data-flow analysis [1].
//
// [1] "A Simple, Fast Dominance Algorithm", SPI '01, Cooper, Harvey and Kennedy
// [2] "Efficiently Computing Static Single Assignment Form
//     and the Control Dependence Graph", TOPLAS '91,
//           Cytron, Ferrante, Rosen, Wegman and Zadeck
// [3] "Improving Performance of OpenCL on CPUs", CC '12, Karrenberg and Hack
// [4] "Divergence Analysis", TOPLAS '13, Sampaio, Souza, Collange and Pereira
//
// -- Sync dependence --
// Sync dependence [4] characterizes the control flow aspect of the
// propagation of branch divergence. For example,
//
//   %cond = icmp slt i32 %tid, 10
//   br i1 %cond, label %then, label %else
// then:
//   br label %merge
// else:
//   br label %merge
// merge:
//   %a = phi i32 [ 0, %then ], [ 1, %else ]
//
// Suppose %tid holds the thread ID. Although %a is not data dependent on %tid
// because %tid is not on its use-def chains, %a is sync dependent on %tid
// because the branch "br i1 %cond" depends on %tid and affects which value %a
// is assigned to.
//
// -- Reduction to SSA construction --
// There are two disjoint paths from A to X, if a certain variant of SSA
// construction places a phi node in X under the following set-up scheme [2].
//
// This variant of SSA construction ignores incoming undef values.
// That is paths from the entry without a definition do not result in
// phi nodes.
//
//       entry
//     /      \
//    A        \
//  /   \       Y
// B     C     /
//  \   /  \  /
//    D     E
//     \   /
//       F
// Assume that A contains a divergent branch. We are interested
// in the set of all blocks where each block is reachable from A
// via two disjoint paths. This would be the set {D, F} in this
// case.
// To generally reduce this query to SSA construction we introduce
// a virtual variable x and assign to x different values in each
// successor block of A.
//           entry
//         /      \
//        A        \
//      /   \       Y
// x = 0   x = 1   /
//      \  /   \  /
//        D     E
//         \   /
//           F
// Our flavor of SSA construction for x will construct the following
//            entry
//          /      \
//         A        \
//       /   \       Y
// x0 = 0   x1 = 1  /
//       \   /   \ /
//      x2=phi    E
//         \     /
//          x3=phi
// The blocks D and F contain phi nodes and are thus each reachable
// by two disjoins paths from A.
//
// -- Remarks --
// In case of loop exits we need to check the disjoint path criterion for loops
// [2]. To this end, we check whether the definition of x differs between the
// loop exit and the loop header (_after_ SSA construction).
//
//===----------------------------------------------------------------------===//
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/SyncDependenceAnalysis.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"

#include <stack>
#include <unordered_set>

#define DEBUG_TYPE "sync-dependence"

namespace llvm {

ConstBlockSet SyncDependenceAnalysis::EmptyBlockSet;

SyncDependenceAnalysis::SyncDependenceAnalysis(const DominatorTree &DT,
                                               const PostDominatorTree &PDT,
                                               const LoopInfo &LI)
    : FuncRPOT(DT.getRoot()->getParent()), DT(DT), PDT(PDT), LI(LI) {}

SyncDependenceAnalysis::~SyncDependenceAnalysis() {}

using FunctionRPOT = ReversePostOrderTraversal<const Function *>;

// divergence propagator for reducible CFGs
struct DivergencePropagator {
  const FunctionRPOT &FuncRPOT;
  const DominatorTree &DT;
  const PostDominatorTree &PDT;
  const LoopInfo &LI;

  // identified join points
  std::unique_ptr<ConstBlockSet> JoinBlocks;

  // reached loop exits (by a path disjoint to a path to the loop header)
  SmallPtrSet<const BasicBlock *, 4> ReachedLoopExits;

  // if DefMap[B] == C then C is the dominating definition at block B
  // if DefMap[B] ~ undef then we haven't seen B yet
  // if DefMap[B] == B then B is a join point of disjoint paths from X or B is
  // an immediate successor of X (initial value).
  using DefiningBlockMap = std::map<const BasicBlock *, const BasicBlock *>;
  DefiningBlockMap DefMap;

  // all blocks with pending visits
  std::unordered_set<const BasicBlock *> PendingUpdates;

  DivergencePropagator(const FunctionRPOT &FuncRPOT, const DominatorTree &DT,
                       const PostDominatorTree &PDT, const LoopInfo &LI)
      : FuncRPOT(FuncRPOT), DT(DT), PDT(PDT), LI(LI),
        JoinBlocks(new ConstBlockSet) {}

  // set the definition at @block and mark @block as pending for a visit
  void addPending(const BasicBlock &Block, const BasicBlock &DefBlock) {
    bool WasAdded = DefMap.emplace(&Block, &DefBlock).second;
    if (WasAdded)
      PendingUpdates.insert(&Block);
  }

  void printDefs(raw_ostream &Out) {
    Out << "Propagator::DefMap {\n";
    for (const auto *Block : FuncRPOT) {
      auto It = DefMap.find(Block);
      Out << Block->getName() << " : ";
      if (It == DefMap.end()) {
        Out << "\n";
      } else {
        const auto *DefBlock = It->second;
        Out << (DefBlock ? DefBlock->getName() : "<null>") << "\n";
      }
    }
    Out << "}\n";
  }

  // process @succBlock with reaching definition @defBlock
  // the original divergent branch was in @parentLoop (if any)
  void visitSuccessor(const BasicBlock &SuccBlock, const Loop *ParentLoop,
                      const BasicBlock &DefBlock) {

    // @succBlock is a loop exit
    if (ParentLoop && !ParentLoop->contains(&SuccBlock)) {
      DefMap.emplace(&SuccBlock, &DefBlock);
      ReachedLoopExits.insert(&SuccBlock);
      return;
    }

    // first reaching def?
    auto ItLastDef = DefMap.find(&SuccBlock);
    if (ItLastDef == DefMap.end()) {
      addPending(SuccBlock, DefBlock);
      return;
    }

    // a join of at least two definitions
    if (ItLastDef->second != &DefBlock) {
      // do we know this join already?
      if (!JoinBlocks->insert(&SuccBlock).second)
        return;

      // update the definition
      addPending(SuccBlock, SuccBlock);
    }
  }

  // find all blocks reachable by two disjoint paths from @rootTerm.
  // This method works for both divergent terminators and loops with
  // divergent exits.
  // @rootBlock is either the block containing the branch or the header of the
  // divergent loop.
  // @nodeSuccessors is the set of successors of the node (Loop or Terminator)
  // headed by @rootBlock.
  // @parentLoop is the parent loop of the Loop or the loop that contains the
  // Terminator.
  template <typename SuccessorIterable>
  std::unique_ptr<ConstBlockSet>
  computeJoinPoints(const BasicBlock &RootBlock,
                    SuccessorIterable NodeSuccessors, const Loop *ParentLoop) {
    assert(JoinBlocks);

    // immediate post dominator (no join block beyond that block)
    const auto *PdNode = PDT.getNode(const_cast<BasicBlock *>(&RootBlock));
    const auto *IpdNode = PdNode->getIDom();
    const auto *PdBoundBlock = IpdNode ? IpdNode->getBlock() : nullptr;

    // bootstrap with branch targets
    for (const auto *SuccBlock : NodeSuccessors) {
      DefMap.emplace(SuccBlock, SuccBlock);

      if (ParentLoop && !ParentLoop->contains(SuccBlock)) {
        // immediate loop exit from node.
        ReachedLoopExits.insert(SuccBlock);
        continue;
      } else {
        // regular successor
        PendingUpdates.insert(SuccBlock);
      }
    }

    auto ItBeginRPO = FuncRPOT.begin();

    // skip until term (TODO RPOT won't let us start at @term directly)
    for (; *ItBeginRPO != &RootBlock; ++ItBeginRPO) {}

    auto ItEndRPO = FuncRPOT.end();
    assert(ItBeginRPO != ItEndRPO);

    // propagate definitions at the immediate successors of the node in RPO
    auto ItBlockRPO = ItBeginRPO;
    while (++ItBlockRPO != ItEndRPO && *ItBlockRPO != PdBoundBlock) {
      const auto *Block = *ItBlockRPO;

      // skip @block if not pending update
      auto ItPending = PendingUpdates.find(Block);
      if (ItPending == PendingUpdates.end())
        continue;
      PendingUpdates.erase(ItPending);

      // propagate definition at @block to its successors
      auto ItDef = DefMap.find(Block);
      const auto *DefBlock = ItDef->second;
      assert(DefBlock);

      auto *BlockLoop = LI.getLoopFor(Block);
      if (ParentLoop &&
          (ParentLoop != BlockLoop && ParentLoop->contains(BlockLoop))) {
        // if the successor is the header of a nested loop pretend its a
        // single node with the loop's exits as successors
        SmallVector<BasicBlock *, 4> BlockLoopExits;
        BlockLoop->getExitBlocks(BlockLoopExits);
        for (const auto *BlockLoopExit : BlockLoopExits) {
          visitSuccessor(*BlockLoopExit, ParentLoop, *DefBlock);
        }

      } else {
        // the successors are either on the same loop level or loop exits
        for (const auto *SuccBlock : successors(Block)) {
          visitSuccessor(*SuccBlock, ParentLoop, *DefBlock);
        }
      }
    }

    // We need to know the definition at the parent loop header to decide
    // whether the definition at the header is different from the definition at
    // the loop exits, which would indicate a divergent loop exits.
    //
    // A // loop header
    // |
    // B // nested loop header
    // |
    // C -> X (exit from B loop) -..-> (A latch)
    // |
    // D -> back to B (B latch)
    // |
    // proper exit from both loops
    //
    // D post-dominates B as it is the only proper exit from the "A loop".
    // If C has a divergent branch, propagation will therefore stop at D.
    // That implies that B will never receive a definition.
    // But that definition can only be the same as at D (D itself in thise case)
    // because all paths to anywhere have to pass through D.
    //
    const BasicBlock *ParentLoopHeader =
        ParentLoop ? ParentLoop->getHeader() : nullptr;
    if (ParentLoop && ParentLoop->contains(PdBoundBlock)) {
      DefMap[ParentLoopHeader] = DefMap[PdBoundBlock];
    }

    // analyze reached loop exits
    if (!ReachedLoopExits.empty()) {
      assert(ParentLoop);
      const auto *HeaderDefBlock = DefMap[ParentLoopHeader];
      LLVM_DEBUG(printDefs(dbgs()));
      assert(HeaderDefBlock && "no definition in header of carrying loop");

      for (const auto *ExitBlock : ReachedLoopExits) {
        auto ItExitDef = DefMap.find(ExitBlock);
        assert((ItExitDef != DefMap.end()) &&
               "no reaching def at reachable loop exit");
        if (ItExitDef->second != HeaderDefBlock) {
          JoinBlocks->insert(ExitBlock);
        }
      }
    }

    return std::move(JoinBlocks);
  }
};

const ConstBlockSet &SyncDependenceAnalysis::join_blocks(const Loop &Loop) {
  using LoopExitVec = SmallVector<BasicBlock *, 4>;
  LoopExitVec LoopExits;
  Loop.getExitBlocks(LoopExits);
  if (LoopExits.size() < 1) {
    return EmptyBlockSet;
  }

  // already available in cache?
  auto ItCached = CachedLoopExitJoins.find(&Loop);
  if (ItCached != CachedLoopExitJoins.end())
    return *ItCached->second;

  // compute all join points
  DivergencePropagator Propagator{FuncRPOT, DT, PDT, LI};
  auto JoinBlocks = Propagator.computeJoinPoints<const LoopExitVec &>(
      *Loop.getHeader(), LoopExits, Loop.getParentLoop());

  auto ItInserted = CachedLoopExitJoins.emplace(&Loop, std::move(JoinBlocks));
  assert(ItInserted.second);
  return *ItInserted.first->second;
}

const ConstBlockSet &
SyncDependenceAnalysis::join_blocks(const Instruction &Term) {
  // trivial case
  if (Term.getNumSuccessors() < 1) {
    return EmptyBlockSet;
  }

  // already available in cache?
  auto ItCached = CachedBranchJoins.find(&Term);
  if (ItCached != CachedBranchJoins.end())
    return *ItCached->second;

  // compute all join points
  DivergencePropagator Propagator{FuncRPOT, DT, PDT, LI};
  const auto &TermBlock = *Term.getParent();
  auto JoinBlocks = Propagator.computeJoinPoints<succ_const_range>(
      TermBlock, successors(Term.getParent()), LI.getLoopFor(&TermBlock));

  auto ItInserted = CachedBranchJoins.emplace(&Term, std::move(JoinBlocks));
  assert(ItInserted.second);
  return *ItInserted.first->second;
}

} // namespace llvm
