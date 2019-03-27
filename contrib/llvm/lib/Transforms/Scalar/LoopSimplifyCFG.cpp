//===--------- LoopSimplifyCFG.cpp - Loop CFG Simplification Pass ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Loop SimplifyCFG Pass. This pass is responsible for
// basic loop CFG cleanup, primarily to assist other loop passes. If you
// encounter a noncanonical CFG construct that causes another loop pass to
// perform suboptimally, this is the place to fix it up.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LoopSimplifyCFG.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionAliasAnalysis.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/DomTreeUpdater.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
using namespace llvm;

#define DEBUG_TYPE "loop-simplifycfg"

static cl::opt<bool> EnableTermFolding("enable-loop-simplifycfg-term-folding",
                                       cl::init(false));

STATISTIC(NumTerminatorsFolded,
          "Number of terminators folded to unconditional branches");
STATISTIC(NumLoopBlocksDeleted,
          "Number of loop blocks deleted");
STATISTIC(NumLoopExitsDeleted,
          "Number of loop exiting edges deleted");

/// If \p BB is a switch or a conditional branch, but only one of its successors
/// can be reached from this block in runtime, return this successor. Otherwise,
/// return nullptr.
static BasicBlock *getOnlyLiveSuccessor(BasicBlock *BB) {
  Instruction *TI = BB->getTerminator();
  if (BranchInst *BI = dyn_cast<BranchInst>(TI)) {
    if (BI->isUnconditional())
      return nullptr;
    if (BI->getSuccessor(0) == BI->getSuccessor(1))
      return BI->getSuccessor(0);
    ConstantInt *Cond = dyn_cast<ConstantInt>(BI->getCondition());
    if (!Cond)
      return nullptr;
    return Cond->isZero() ? BI->getSuccessor(1) : BI->getSuccessor(0);
  }

  if (SwitchInst *SI = dyn_cast<SwitchInst>(TI)) {
    auto *CI = dyn_cast<ConstantInt>(SI->getCondition());
    if (!CI)
      return nullptr;
    for (auto Case : SI->cases())
      if (Case.getCaseValue() == CI)
        return Case.getCaseSuccessor();
    return SI->getDefaultDest();
  }

  return nullptr;
}

namespace {
/// Helper class that can turn branches and switches with constant conditions
/// into unconditional branches.
class ConstantTerminatorFoldingImpl {
private:
  Loop &L;
  LoopInfo &LI;
  DominatorTree &DT;
  ScalarEvolution &SE;
  MemorySSAUpdater *MSSAU;

  // Whether or not the current loop has irreducible CFG.
  bool HasIrreducibleCFG = false;
  // Whether or not the current loop will still exist after terminator constant
  // folding will be done. In theory, there are two ways how it can happen:
  // 1. Loop's latch(es) become unreachable from loop header;
  // 2. Loop's header becomes unreachable from method entry.
  // In practice, the second situation is impossible because we only modify the
  // current loop and its preheader and do not affect preheader's reachibility
  // from any other block. So this variable set to true means that loop's latch
  // has become unreachable from loop header.
  bool DeleteCurrentLoop = false;

  // The blocks of the original loop that will still be reachable from entry
  // after the constant folding.
  SmallPtrSet<BasicBlock *, 8> LiveLoopBlocks;
  // The blocks of the original loop that will become unreachable from entry
  // after the constant folding.
  SmallVector<BasicBlock *, 8> DeadLoopBlocks;
  // The exits of the original loop that will still be reachable from entry
  // after the constant folding.
  SmallPtrSet<BasicBlock *, 8> LiveExitBlocks;
  // The exits of the original loop that will become unreachable from entry
  // after the constant folding.
  SmallVector<BasicBlock *, 8> DeadExitBlocks;
  // The blocks that will still be a part of the current loop after folding.
  SmallPtrSet<BasicBlock *, 8> BlocksInLoopAfterFolding;
  // The blocks that have terminators with constant condition that can be
  // folded. Note: fold candidates should be in L but not in any of its
  // subloops to avoid complex LI updates.
  SmallVector<BasicBlock *, 8> FoldCandidates;

  void dump() const {
    dbgs() << "Constant terminator folding for loop " << L << "\n";
    dbgs() << "After terminator constant-folding, the loop will";
    if (!DeleteCurrentLoop)
      dbgs() << " not";
    dbgs() << " be destroyed\n";
    auto PrintOutVector = [&](const char *Message,
                           const SmallVectorImpl<BasicBlock *> &S) {
      dbgs() << Message << "\n";
      for (const BasicBlock *BB : S)
        dbgs() << "\t" << BB->getName() << "\n";
    };
    auto PrintOutSet = [&](const char *Message,
                           const SmallPtrSetImpl<BasicBlock *> &S) {
      dbgs() << Message << "\n";
      for (const BasicBlock *BB : S)
        dbgs() << "\t" << BB->getName() << "\n";
    };
    PrintOutVector("Blocks in which we can constant-fold terminator:",
                   FoldCandidates);
    PrintOutSet("Live blocks from the original loop:", LiveLoopBlocks);
    PrintOutVector("Dead blocks from the original loop:", DeadLoopBlocks);
    PrintOutSet("Live exit blocks:", LiveExitBlocks);
    PrintOutVector("Dead exit blocks:", DeadExitBlocks);
    if (!DeleteCurrentLoop)
      PrintOutSet("The following blocks will still be part of the loop:",
                  BlocksInLoopAfterFolding);
  }

  /// Whether or not the current loop has irreducible CFG.
  bool hasIrreducibleCFG(LoopBlocksDFS &DFS) {
    assert(DFS.isComplete() && "DFS is expected to be finished");
    // Index of a basic block in RPO traversal.
    DenseMap<const BasicBlock *, unsigned> RPO;
    unsigned Current = 0;
    for (auto I = DFS.beginRPO(), E = DFS.endRPO(); I != E; ++I)
      RPO[*I] = Current++;

    for (auto I = DFS.beginRPO(), E = DFS.endRPO(); I != E; ++I) {
      BasicBlock *BB = *I;
      for (auto *Succ : successors(BB))
        if (L.contains(Succ) && !LI.isLoopHeader(Succ) && RPO[BB] > RPO[Succ])
          // If an edge goes from a block with greater order number into a block
          // with lesses number, and it is not a loop backedge, then it can only
          // be a part of irreducible non-loop cycle.
          return true;
    }
    return false;
  }

  /// Fill all information about status of blocks and exits of the current loop
  /// if constant folding of all branches will be done.
  void analyze() {
    LoopBlocksDFS DFS(&L);
    DFS.perform(&LI);
    assert(DFS.isComplete() && "DFS is expected to be finished");

    // TODO: The algorithm below relies on both RPO and Postorder traversals.
    // When the loop has only reducible CFG inside, then the invariant "all
    // predecessors of X are processed before X in RPO" is preserved. However
    // an irreducible loop can break this invariant (e.g. latch does not have to
    // be the last block in the traversal in this case, and the algorithm relies
    // on this). We can later decide to support such cases by altering the
    // algorithms, but so far we just give up analyzing them.
    if (hasIrreducibleCFG(DFS)) {
      HasIrreducibleCFG = true;
      return;
    }

    // Collect live and dead loop blocks and exits.
    LiveLoopBlocks.insert(L.getHeader());
    for (auto I = DFS.beginRPO(), E = DFS.endRPO(); I != E; ++I) {
      BasicBlock *BB = *I;

      // If a loop block wasn't marked as live so far, then it's dead.
      if (!LiveLoopBlocks.count(BB)) {
        DeadLoopBlocks.push_back(BB);
        continue;
      }

      BasicBlock *TheOnlySucc = getOnlyLiveSuccessor(BB);

      // If a block has only one live successor, it's a candidate on constant
      // folding. Only handle blocks from current loop: branches in child loops
      // are skipped because if they can be folded, they should be folded during
      // the processing of child loops.
      if (TheOnlySucc && LI.getLoopFor(BB) == &L)
        FoldCandidates.push_back(BB);

      // Handle successors.
      for (BasicBlock *Succ : successors(BB))
        if (!TheOnlySucc || TheOnlySucc == Succ) {
          if (L.contains(Succ))
            LiveLoopBlocks.insert(Succ);
          else
            LiveExitBlocks.insert(Succ);
        }
    }

    // Sanity check: amount of dead and live loop blocks should match the total
    // number of blocks in loop.
    assert(L.getNumBlocks() == LiveLoopBlocks.size() + DeadLoopBlocks.size() &&
           "Malformed block sets?");

    // Now, all exit blocks that are not marked as live are dead.
    SmallVector<BasicBlock *, 8> ExitBlocks;
    L.getExitBlocks(ExitBlocks);
    for (auto *ExitBlock : ExitBlocks)
      if (!LiveExitBlocks.count(ExitBlock))
        DeadExitBlocks.push_back(ExitBlock);

    // Whether or not the edge From->To will still be present in graph after the
    // folding.
    auto IsEdgeLive = [&](BasicBlock *From, BasicBlock *To) {
      if (!LiveLoopBlocks.count(From))
        return false;
      BasicBlock *TheOnlySucc = getOnlyLiveSuccessor(From);
      return !TheOnlySucc || TheOnlySucc == To;
    };

    // The loop will not be destroyed if its latch is live.
    DeleteCurrentLoop = !IsEdgeLive(L.getLoopLatch(), L.getHeader());

    // If we are going to delete the current loop completely, no extra analysis
    // is needed.
    if (DeleteCurrentLoop)
      return;

    // Otherwise, we should check which blocks will still be a part of the
    // current loop after the transform.
    BlocksInLoopAfterFolding.insert(L.getLoopLatch());
    // If the loop is live, then we should compute what blocks are still in
    // loop after all branch folding has been done. A block is in loop if
    // it has a live edge to another block that is in the loop; by definition,
    // latch is in the loop.
    auto BlockIsInLoop = [&](BasicBlock *BB) {
      return any_of(successors(BB), [&](BasicBlock *Succ) {
        return BlocksInLoopAfterFolding.count(Succ) && IsEdgeLive(BB, Succ);
      });
    };
    for (auto I = DFS.beginPostorder(), E = DFS.endPostorder(); I != E; ++I) {
      BasicBlock *BB = *I;
      if (BlockIsInLoop(BB))
        BlocksInLoopAfterFolding.insert(BB);
    }

    // Sanity check: header must be in loop.
    assert(BlocksInLoopAfterFolding.count(L.getHeader()) &&
           "Header not in loop?");
    assert(BlocksInLoopAfterFolding.size() <= LiveLoopBlocks.size() &&
           "All blocks that stay in loop should be live!");
  }

  /// We need to preserve static reachibility of all loop exit blocks (this is)
  /// required by loop pass manager. In order to do it, we make the following
  /// trick:
  ///
  ///  preheader:
  ///    <preheader code>
  ///    br label %loop_header
  ///
  ///  loop_header:
  ///    ...
  ///    br i1 false, label %dead_exit, label %loop_block
  ///    ...
  ///
  /// We cannot simply remove edge from the loop to dead exit because in this
  /// case dead_exit (and its successors) may become unreachable. To avoid that,
  /// we insert the following fictive preheader:
  ///
  ///  preheader:
  ///    <preheader code>
  ///    switch i32 0, label %preheader-split,
  ///                  [i32 1, label %dead_exit_1],
  ///                  [i32 2, label %dead_exit_2],
  ///                  ...
  ///                  [i32 N, label %dead_exit_N],
  ///
  ///  preheader-split:
  ///    br label %loop_header
  ///
  ///  loop_header:
  ///    ...
  ///    br i1 false, label %dead_exit_N, label %loop_block
  ///    ...
  ///
  /// Doing so, we preserve static reachibility of all dead exits and can later
  /// remove edges from the loop to these blocks.
  void handleDeadExits() {
    // If no dead exits, nothing to do.
    if (DeadExitBlocks.empty())
      return;

    // Construct split preheader and the dummy switch to thread edges from it to
    // dead exits.
    DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Eager);
    BasicBlock *Preheader = L.getLoopPreheader();
    BasicBlock *NewPreheader = Preheader->splitBasicBlock(
        Preheader->getTerminator(),
        Twine(Preheader->getName()).concat("-split"));
    DTU.deleteEdge(Preheader, L.getHeader());
    DTU.insertEdge(NewPreheader, L.getHeader());
    DTU.insertEdge(Preheader, NewPreheader);
    IRBuilder<> Builder(Preheader->getTerminator());
    SwitchInst *DummySwitch =
        Builder.CreateSwitch(Builder.getInt32(0), NewPreheader);
    Preheader->getTerminator()->eraseFromParent();

    unsigned DummyIdx = 1;
    for (BasicBlock *BB : DeadExitBlocks) {
      SmallVector<Instruction *, 4> DeadPhis;
      for (auto &PN : BB->phis())
        DeadPhis.push_back(&PN);

      // Eliminate all Phis from dead exits.
      for (Instruction *PN : DeadPhis) {
        PN->replaceAllUsesWith(UndefValue::get(PN->getType()));
        PN->eraseFromParent();
      }
      assert(DummyIdx != 0 && "Too many dead exits!");
      DummySwitch->addCase(Builder.getInt32(DummyIdx++), BB);
      DTU.insertEdge(Preheader, BB);
      ++NumLoopExitsDeleted;
    }

    assert(L.getLoopPreheader() == NewPreheader && "Malformed CFG?");
    if (Loop *OuterLoop = LI.getLoopFor(Preheader)) {
      OuterLoop->addBasicBlockToLoop(NewPreheader, LI);

      // When we break dead edges, the outer loop may become unreachable from
      // the current loop. We need to fix loop info accordingly. For this, we
      // find the most nested loop that still contains L and remove L from all
      // loops that are inside of it.
      Loop *StillReachable = nullptr;
      for (BasicBlock *BB : LiveExitBlocks) {
        Loop *BBL = LI.getLoopFor(BB);
        if (BBL && BBL->contains(L.getHeader()))
          if (!StillReachable ||
              BBL->getLoopDepth() > StillReachable->getLoopDepth())
            StillReachable = BBL;
      }

      // Okay, our loop is no longer in the outer loop (and maybe not in some of
      // its parents as well). Make the fixup.
      if (StillReachable != OuterLoop) {
        LI.changeLoopFor(NewPreheader, StillReachable);
        for (Loop *NotContaining = OuterLoop; NotContaining != StillReachable;
             NotContaining = NotContaining->getParentLoop()) {
          NotContaining->removeBlockFromLoop(NewPreheader);
          for (auto *BB : L.blocks())
            NotContaining->removeBlockFromLoop(BB);
        }
        OuterLoop->removeChildLoop(&L);
        if (StillReachable)
          StillReachable->addChildLoop(&L);
        else
          LI.addTopLevelLoop(&L);
      }
    }
  }

  /// Delete loop blocks that have become unreachable after folding. Make all
  /// relevant updates to DT and LI.
  void deleteDeadLoopBlocks() {
    DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Eager);
    if (MSSAU) {
      SmallPtrSet<BasicBlock *, 8> DeadLoopBlocksSet(DeadLoopBlocks.begin(),
                                                     DeadLoopBlocks.end());
      MSSAU->removeBlocks(DeadLoopBlocksSet);
    }
    for (auto *BB : DeadLoopBlocks) {
      assert(BB != L.getHeader() &&
             "Header of the current loop cannot be dead!");
      LLVM_DEBUG(dbgs() << "Deleting dead loop block " << BB->getName()
                        << "\n");
      if (LI.isLoopHeader(BB)) {
        assert(LI.getLoopFor(BB) != &L && "Attempt to remove current loop!");
        LI.erase(LI.getLoopFor(BB));
      }
      LI.removeBlock(BB);
      DeleteDeadBlock(BB, &DTU);
      ++NumLoopBlocksDeleted;
    }
  }

  /// Constant-fold terminators of blocks acculumated in FoldCandidates into the
  /// unconditional branches.
  void foldTerminators() {
    DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Eager);

    for (BasicBlock *BB : FoldCandidates) {
      assert(LI.getLoopFor(BB) == &L && "Should be a loop block!");
      BasicBlock *TheOnlySucc = getOnlyLiveSuccessor(BB);
      assert(TheOnlySucc && "Should have one live successor!");

      LLVM_DEBUG(dbgs() << "Replacing terminator of " << BB->getName()
                        << " with an unconditional branch to the block "
                        << TheOnlySucc->getName() << "\n");

      SmallPtrSet<BasicBlock *, 2> DeadSuccessors;
      // Remove all BB's successors except for the live one.
      unsigned TheOnlySuccDuplicates = 0;
      for (auto *Succ : successors(BB))
        if (Succ != TheOnlySucc) {
          DeadSuccessors.insert(Succ);
          // If our successor lies in a different loop, we don't want to remove
          // the one-input Phi because it is a LCSSA Phi.
          bool PreserveLCSSAPhi = !L.contains(Succ);
          Succ->removePredecessor(BB, PreserveLCSSAPhi);
          if (MSSAU)
            MSSAU->removeEdge(BB, Succ);
        } else
          ++TheOnlySuccDuplicates;

      assert(TheOnlySuccDuplicates > 0 && "Should be!");
      // If TheOnlySucc was BB's successor more than once, after transform it
      // will be its successor only once. Remove redundant inputs from
      // TheOnlySucc's Phis.
      bool PreserveLCSSAPhi = !L.contains(TheOnlySucc);
      for (unsigned Dup = 1; Dup < TheOnlySuccDuplicates; ++Dup)
        TheOnlySucc->removePredecessor(BB, PreserveLCSSAPhi);
      if (MSSAU && TheOnlySuccDuplicates > 1)
        MSSAU->removeDuplicatePhiEdgesBetween(BB, TheOnlySucc);

      IRBuilder<> Builder(BB->getContext());
      Instruction *Term = BB->getTerminator();
      Builder.SetInsertPoint(Term);
      Builder.CreateBr(TheOnlySucc);
      Term->eraseFromParent();

      for (auto *DeadSucc : DeadSuccessors)
        DTU.deleteEdge(BB, DeadSucc);

      ++NumTerminatorsFolded;
    }
  }

public:
  ConstantTerminatorFoldingImpl(Loop &L, LoopInfo &LI, DominatorTree &DT,
                                ScalarEvolution &SE,
                                MemorySSAUpdater *MSSAU)
      : L(L), LI(LI), DT(DT), SE(SE), MSSAU(MSSAU) {}
  bool run() {
    assert(L.getLoopLatch() && "Should be single latch!");

    // Collect all available information about status of blocks after constant
    // folding.
    analyze();

    LLVM_DEBUG(dbgs() << "In function " << L.getHeader()->getParent()->getName()
                      << ": ");

    if (HasIrreducibleCFG) {
      LLVM_DEBUG(dbgs() << "Loops with irreducible CFG are not supported!\n");
      return false;
    }

    // Nothing to constant-fold.
    if (FoldCandidates.empty()) {
      LLVM_DEBUG(
          dbgs() << "No constant terminator folding candidates found in loop "
                 << L.getHeader()->getName() << "\n");
      return false;
    }

    // TODO: Support deletion of the current loop.
    if (DeleteCurrentLoop) {
      LLVM_DEBUG(
          dbgs()
          << "Give up constant terminator folding in loop "
          << L.getHeader()->getName()
          << ": we don't currently support deletion of the current loop.\n");
      return false;
    }

    // TODO: Support blocks that are not dead, but also not in loop after the
    // folding.
    if (BlocksInLoopAfterFolding.size() + DeadLoopBlocks.size() !=
        L.getNumBlocks()) {
      LLVM_DEBUG(
          dbgs() << "Give up constant terminator folding in loop "
                 << L.getHeader()->getName()
                 << ": we don't currently"
                    " support blocks that are not dead, but will stop "
                    "being a part of the loop after constant-folding.\n");
      return false;
    }

    SE.forgetTopmostLoop(&L);
    // Dump analysis results.
    LLVM_DEBUG(dump());

    LLVM_DEBUG(dbgs() << "Constant-folding " << FoldCandidates.size()
                      << " terminators in loop " << L.getHeader()->getName()
                      << "\n");

    // Make the actual transforms.
    handleDeadExits();
    foldTerminators();

    if (!DeadLoopBlocks.empty()) {
      LLVM_DEBUG(dbgs() << "Deleting " << DeadLoopBlocks.size()
                    << " dead blocks in loop " << L.getHeader()->getName()
                    << "\n");
      deleteDeadLoopBlocks();
    }

#ifndef NDEBUG
    // Make sure that we have preserved all data structures after the transform.
    DT.verify();
    assert(DT.isReachableFromEntry(L.getHeader()));
    LI.verify(DT);
#endif

    return true;
  }
};
} // namespace

/// Turn branches and switches with known constant conditions into unconditional
/// branches.
static bool constantFoldTerminators(Loop &L, DominatorTree &DT, LoopInfo &LI,
                                    ScalarEvolution &SE,
                                    MemorySSAUpdater *MSSAU) {
  if (!EnableTermFolding)
    return false;

  // To keep things simple, only process loops with single latch. We
  // canonicalize most loops to this form. We can support multi-latch if needed.
  if (!L.getLoopLatch())
    return false;

  ConstantTerminatorFoldingImpl BranchFolder(L, LI, DT, SE, MSSAU);
  return BranchFolder.run();
}

static bool mergeBlocksIntoPredecessors(Loop &L, DominatorTree &DT,
                                        LoopInfo &LI, MemorySSAUpdater *MSSAU) {
  bool Changed = false;
  DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Eager);
  // Copy blocks into a temporary array to avoid iterator invalidation issues
  // as we remove them.
  SmallVector<WeakTrackingVH, 16> Blocks(L.blocks());

  for (auto &Block : Blocks) {
    // Attempt to merge blocks in the trivial case. Don't modify blocks which
    // belong to other loops.
    BasicBlock *Succ = cast_or_null<BasicBlock>(Block);
    if (!Succ)
      continue;

    BasicBlock *Pred = Succ->getSinglePredecessor();
    if (!Pred || !Pred->getSingleSuccessor() || LI.getLoopFor(Pred) != &L)
      continue;

    // Merge Succ into Pred and delete it.
    MergeBlockIntoPredecessor(Succ, &DTU, &LI, MSSAU);

    Changed = true;
  }

  return Changed;
}

static bool simplifyLoopCFG(Loop &L, DominatorTree &DT, LoopInfo &LI,
                            ScalarEvolution &SE, MemorySSAUpdater *MSSAU) {
  bool Changed = false;

  // Constant-fold terminators with known constant conditions.
  Changed |= constantFoldTerminators(L, DT, LI, SE, MSSAU);

  // Eliminate unconditional branches by merging blocks into their predecessors.
  Changed |= mergeBlocksIntoPredecessors(L, DT, LI, MSSAU);

  if (Changed)
    SE.forgetTopmostLoop(&L);

  return Changed;
}

PreservedAnalyses LoopSimplifyCFGPass::run(Loop &L, LoopAnalysisManager &AM,
                                           LoopStandardAnalysisResults &AR,
                                           LPMUpdater &) {
  Optional<MemorySSAUpdater> MSSAU;
  if (EnableMSSALoopDependency && AR.MSSA)
    MSSAU = MemorySSAUpdater(AR.MSSA);
  if (!simplifyLoopCFG(L, AR.DT, AR.LI, AR.SE,
                       MSSAU.hasValue() ? MSSAU.getPointer() : nullptr))
    return PreservedAnalyses::all();

  return getLoopPassPreservedAnalyses();
}

namespace {
class LoopSimplifyCFGLegacyPass : public LoopPass {
public:
  static char ID; // Pass ID, replacement for typeid
  LoopSimplifyCFGLegacyPass() : LoopPass(ID) {
    initializeLoopSimplifyCFGLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnLoop(Loop *L, LPPassManager &) override {
    if (skipLoop(L))
      return false;

    DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    ScalarEvolution &SE = getAnalysis<ScalarEvolutionWrapperPass>().getSE();
    Optional<MemorySSAUpdater> MSSAU;
    if (EnableMSSALoopDependency) {
      MemorySSA *MSSA = &getAnalysis<MemorySSAWrapperPass>().getMSSA();
      MSSAU = MemorySSAUpdater(MSSA);
      if (VerifyMemorySSA)
        MSSA->verifyMemorySSA();
    }
    return simplifyLoopCFG(*L, DT, LI, SE,
                           MSSAU.hasValue() ? MSSAU.getPointer() : nullptr);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    if (EnableMSSALoopDependency) {
      AU.addRequired<MemorySSAWrapperPass>();
      AU.addPreserved<MemorySSAWrapperPass>();
    }
    AU.addPreserved<DependenceAnalysisWrapperPass>();
    getLoopAnalysisUsage(AU);
  }
};
}

char LoopSimplifyCFGLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(LoopSimplifyCFGLegacyPass, "loop-simplifycfg",
                      "Simplify loop CFG", false, false)
INITIALIZE_PASS_DEPENDENCY(LoopPass)
INITIALIZE_PASS_DEPENDENCY(MemorySSAWrapperPass)
INITIALIZE_PASS_END(LoopSimplifyCFGLegacyPass, "loop-simplifycfg",
                    "Simplify loop CFG", false, false)

Pass *llvm::createLoopSimplifyCFGPass() {
  return new LoopSimplifyCFGLegacyPass();
}
