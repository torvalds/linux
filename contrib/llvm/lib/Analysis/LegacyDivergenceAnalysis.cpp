//===- LegacyDivergenceAnalysis.cpp --------- Legacy Divergence Analysis
//Implementation -==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements divergence analysis which determines whether a branch
// in a GPU program is divergent.It can help branch optimizations such as jump
// threading and loop unswitching to make better decisions.
//
// GPU programs typically use the SIMD execution model, where multiple threads
// in the same execution group have to execute in lock-step. Therefore, if the
// code contains divergent branches (i.e., threads in a group do not agree on
// which path of the branch to take), the group of threads has to execute all
// the paths from that branch with different subsets of threads enabled until
// they converge at the immediately post-dominating BB of the paths.
//
// Due to this execution model, some optimizations such as jump
// threading and loop unswitching can be unfortunately harmful when performed on
// divergent branches. Therefore, an analysis that computes which branches in a
// GPU program are divergent can help the compiler to selectively run these
// optimizations.
//
// This file defines divergence analysis which computes a conservative but
// non-trivial approximation of all divergent branches in a GPU program. It
// partially implements the approach described in
//
//   Divergence Analysis
//   Sampaio, Souza, Collange, Pereira
//   TOPLAS '13
//
// The divergence analysis identifies the sources of divergence (e.g., special
// variables that hold the thread ID), and recursively marks variables that are
// data or sync dependent on a source of divergence as divergent.
//
// While data dependency is a well-known concept, the notion of sync dependency
// is worth more explanation. Sync dependence characterizes the control flow
// aspect of the propagation of branch divergence. For example,
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
// The current implementation has the following limitations:
// 1. intra-procedural. It conservatively considers the arguments of a
//    non-kernel-entry function and the return value of a function call as
//    divergent.
// 2. memory as black box. It conservatively considers values loaded from
//    generic or local address as divergent. This can be improved by leveraging
//    pointer analysis.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/DivergenceAnalysis.h"
#include "llvm/Analysis/LegacyDivergenceAnalysis.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>
using namespace llvm;

#define DEBUG_TYPE "divergence"

// transparently use the GPUDivergenceAnalysis
static cl::opt<bool> UseGPUDA("use-gpu-divergence-analysis", cl::init(false),
                              cl::Hidden,
                              cl::desc("turn the LegacyDivergenceAnalysis into "
                                       "a wrapper for GPUDivergenceAnalysis"));

namespace {

class DivergencePropagator {
public:
  DivergencePropagator(Function &F, TargetTransformInfo &TTI, DominatorTree &DT,
                       PostDominatorTree &PDT, DenseSet<const Value *> &DV)
      : F(F), TTI(TTI), DT(DT), PDT(PDT), DV(DV) {}
  void populateWithSourcesOfDivergence();
  void propagate();

private:
  // A helper function that explores data dependents of V.
  void exploreDataDependency(Value *V);
  // A helper function that explores sync dependents of TI.
  void exploreSyncDependency(Instruction *TI);
  // Computes the influence region from Start to End. This region includes all
  // basic blocks on any simple path from Start to End.
  void computeInfluenceRegion(BasicBlock *Start, BasicBlock *End,
                              DenseSet<BasicBlock *> &InfluenceRegion);
  // Finds all users of I that are outside the influence region, and add these
  // users to Worklist.
  void findUsersOutsideInfluenceRegion(
      Instruction &I, const DenseSet<BasicBlock *> &InfluenceRegion);

  Function &F;
  TargetTransformInfo &TTI;
  DominatorTree &DT;
  PostDominatorTree &PDT;
  std::vector<Value *> Worklist; // Stack for DFS.
  DenseSet<const Value *> &DV;   // Stores all divergent values.
};

void DivergencePropagator::populateWithSourcesOfDivergence() {
  Worklist.clear();
  DV.clear();
  for (auto &I : instructions(F)) {
    if (TTI.isSourceOfDivergence(&I)) {
      Worklist.push_back(&I);
      DV.insert(&I);
    }
  }
  for (auto &Arg : F.args()) {
    if (TTI.isSourceOfDivergence(&Arg)) {
      Worklist.push_back(&Arg);
      DV.insert(&Arg);
    }
  }
}

void DivergencePropagator::exploreSyncDependency(Instruction *TI) {
  // Propagation rule 1: if branch TI is divergent, all PHINodes in TI's
  // immediate post dominator are divergent. This rule handles if-then-else
  // patterns. For example,
  //
  // if (tid < 5)
  //   a1 = 1;
  // else
  //   a2 = 2;
  // a = phi(a1, a2); // sync dependent on (tid < 5)
  BasicBlock *ThisBB = TI->getParent();

  // Unreachable blocks may not be in the dominator tree.
  if (!DT.isReachableFromEntry(ThisBB))
    return;

  // If the function has no exit blocks or doesn't reach any exit blocks, the
  // post dominator may be null.
  DomTreeNode *ThisNode = PDT.getNode(ThisBB);
  if (!ThisNode)
    return;

  BasicBlock *IPostDom = ThisNode->getIDom()->getBlock();
  if (IPostDom == nullptr)
    return;

  for (auto I = IPostDom->begin(); isa<PHINode>(I); ++I) {
    // A PHINode is uniform if it returns the same value no matter which path is
    // taken.
    if (!cast<PHINode>(I)->hasConstantOrUndefValue() && DV.insert(&*I).second)
      Worklist.push_back(&*I);
  }

  // Propagation rule 2: if a value defined in a loop is used outside, the user
  // is sync dependent on the condition of the loop exits that dominate the
  // user. For example,
  //
  // int i = 0;
  // do {
  //   i++;
  //   if (foo(i)) ... // uniform
  // } while (i < tid);
  // if (bar(i)) ...   // divergent
  //
  // A program may contain unstructured loops. Therefore, we cannot leverage
  // LoopInfo, which only recognizes natural loops.
  //
  // The algorithm used here handles both natural and unstructured loops.  Given
  // a branch TI, we first compute its influence region, the union of all simple
  // paths from TI to its immediate post dominator (IPostDom). Then, we search
  // for all the values defined in the influence region but used outside. All
  // these users are sync dependent on TI.
  DenseSet<BasicBlock *> InfluenceRegion;
  computeInfluenceRegion(ThisBB, IPostDom, InfluenceRegion);
  // An insight that can speed up the search process is that all the in-region
  // values that are used outside must dominate TI. Therefore, instead of
  // searching every basic blocks in the influence region, we search all the
  // dominators of TI until it is outside the influence region.
  BasicBlock *InfluencedBB = ThisBB;
  while (InfluenceRegion.count(InfluencedBB)) {
    for (auto &I : *InfluencedBB)
      findUsersOutsideInfluenceRegion(I, InfluenceRegion);
    DomTreeNode *IDomNode = DT.getNode(InfluencedBB)->getIDom();
    if (IDomNode == nullptr)
      break;
    InfluencedBB = IDomNode->getBlock();
  }
}

void DivergencePropagator::findUsersOutsideInfluenceRegion(
    Instruction &I, const DenseSet<BasicBlock *> &InfluenceRegion) {
  for (User *U : I.users()) {
    Instruction *UserInst = cast<Instruction>(U);
    if (!InfluenceRegion.count(UserInst->getParent())) {
      if (DV.insert(UserInst).second)
        Worklist.push_back(UserInst);
    }
  }
}

// A helper function for computeInfluenceRegion that adds successors of "ThisBB"
// to the influence region.
static void
addSuccessorsToInfluenceRegion(BasicBlock *ThisBB, BasicBlock *End,
                               DenseSet<BasicBlock *> &InfluenceRegion,
                               std::vector<BasicBlock *> &InfluenceStack) {
  for (BasicBlock *Succ : successors(ThisBB)) {
    if (Succ != End && InfluenceRegion.insert(Succ).second)
      InfluenceStack.push_back(Succ);
  }
}

void DivergencePropagator::computeInfluenceRegion(
    BasicBlock *Start, BasicBlock *End,
    DenseSet<BasicBlock *> &InfluenceRegion) {
  assert(PDT.properlyDominates(End, Start) &&
         "End does not properly dominate Start");

  // The influence region starts from the end of "Start" to the beginning of
  // "End". Therefore, "Start" should not be in the region unless "Start" is in
  // a loop that doesn't contain "End".
  std::vector<BasicBlock *> InfluenceStack;
  addSuccessorsToInfluenceRegion(Start, End, InfluenceRegion, InfluenceStack);
  while (!InfluenceStack.empty()) {
    BasicBlock *BB = InfluenceStack.back();
    InfluenceStack.pop_back();
    addSuccessorsToInfluenceRegion(BB, End, InfluenceRegion, InfluenceStack);
  }
}

void DivergencePropagator::exploreDataDependency(Value *V) {
  // Follow def-use chains of V.
  for (User *U : V->users()) {
    Instruction *UserInst = cast<Instruction>(U);
    if (!TTI.isAlwaysUniform(U) && DV.insert(UserInst).second)
      Worklist.push_back(UserInst);
  }
}

void DivergencePropagator::propagate() {
  // Traverse the dependency graph using DFS.
  while (!Worklist.empty()) {
    Value *V = Worklist.back();
    Worklist.pop_back();
    if (Instruction *I = dyn_cast<Instruction>(V)) {
      // Terminators with less than two successors won't introduce sync
      // dependency. Ignore them.
      if (I->isTerminator() && I->getNumSuccessors() > 1)
        exploreSyncDependency(I);
    }
    exploreDataDependency(V);
  }
}

} // namespace

// Register this pass.
char LegacyDivergenceAnalysis::ID = 0;
INITIALIZE_PASS_BEGIN(LegacyDivergenceAnalysis, "divergence",
                      "Legacy Divergence Analysis", false, true)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(PostDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(LegacyDivergenceAnalysis, "divergence",
                    "Legacy Divergence Analysis", false, true)

FunctionPass *llvm::createLegacyDivergenceAnalysisPass() {
  return new LegacyDivergenceAnalysis();
}

void LegacyDivergenceAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addRequired<PostDominatorTreeWrapperPass>();
  if (UseGPUDA)
    AU.addRequired<LoopInfoWrapperPass>();
  AU.setPreservesAll();
}

bool LegacyDivergenceAnalysis::shouldUseGPUDivergenceAnalysis(
    const Function &F) const {
  if (!UseGPUDA)
    return false;

  // GPUDivergenceAnalysis requires a reducible CFG.
  auto &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  using RPOTraversal = ReversePostOrderTraversal<const Function *>;
  RPOTraversal FuncRPOT(&F);
  return !containsIrreducibleCFG<const BasicBlock *, const RPOTraversal,
                                 const LoopInfo>(FuncRPOT, LI);
}

bool LegacyDivergenceAnalysis::runOnFunction(Function &F) {
  auto *TTIWP = getAnalysisIfAvailable<TargetTransformInfoWrapperPass>();
  if (TTIWP == nullptr)
    return false;

  TargetTransformInfo &TTI = TTIWP->getTTI(F);
  // Fast path: if the target does not have branch divergence, we do not mark
  // any branch as divergent.
  if (!TTI.hasBranchDivergence())
    return false;

  DivergentValues.clear();
  gpuDA = nullptr;

  auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  auto &PDT = getAnalysis<PostDominatorTreeWrapperPass>().getPostDomTree();

  if (shouldUseGPUDivergenceAnalysis(F)) {
    // run the new GPU divergence analysis
    auto &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    gpuDA = llvm::make_unique<GPUDivergenceAnalysis>(F, DT, PDT, LI, TTI);

  } else {
    // run LLVM's existing DivergenceAnalysis
    DivergencePropagator DP(F, TTI, DT, PDT, DivergentValues);
    DP.populateWithSourcesOfDivergence();
    DP.propagate();
  }

  LLVM_DEBUG(dbgs() << "\nAfter divergence analysis on " << F.getName()
                    << ":\n";
             print(dbgs(), F.getParent()));

  return false;
}

bool LegacyDivergenceAnalysis::isDivergent(const Value *V) const {
  if (gpuDA) {
    return gpuDA->isDivergent(*V);
  }
  return DivergentValues.count(V);
}

void LegacyDivergenceAnalysis::print(raw_ostream &OS, const Module *) const {
  if ((!gpuDA || !gpuDA->hasDivergence()) && DivergentValues.empty())
    return;

  const Function *F = nullptr;
  if (!DivergentValues.empty()) {
    const Value *FirstDivergentValue = *DivergentValues.begin();
    if (const Argument *Arg = dyn_cast<Argument>(FirstDivergentValue)) {
      F = Arg->getParent();
    } else if (const Instruction *I =
                   dyn_cast<Instruction>(FirstDivergentValue)) {
      F = I->getParent()->getParent();
    } else {
      llvm_unreachable("Only arguments and instructions can be divergent");
    }
  } else if (gpuDA) {
    F = &gpuDA->getFunction();
  }
  if (!F)
    return;

  // Dumps all divergent values in F, arguments and then instructions.
  for (auto &Arg : F->args()) {
    OS << (isDivergent(&Arg) ? "DIVERGENT: " : "           ");
    OS << Arg << "\n";
  }
  // Iterate instructions using instructions() to ensure a deterministic order.
  for (auto BI = F->begin(), BE = F->end(); BI != BE; ++BI) {
    auto &BB = *BI;
    OS << "\n           " << BB.getName() << ":\n";
    for (auto &I : BB.instructionsWithoutDebug()) {
      OS << (isDivergent(&I) ? "DIVERGENT:     " : "               ");
      OS << I << "\n";
    }
  }
  OS << "\n";
}
