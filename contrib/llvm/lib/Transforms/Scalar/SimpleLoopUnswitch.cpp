///===- SimpleLoopUnswitch.cpp - Hoist loop-invariant control flow ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/SimpleLoopUnswitch.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/GuardUtils.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/Utils/Local.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/GenericDomTree.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar/SimpleLoopUnswitch.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <numeric>
#include <utility>

#define DEBUG_TYPE "simple-loop-unswitch"

using namespace llvm;

STATISTIC(NumBranches, "Number of branches unswitched");
STATISTIC(NumSwitches, "Number of switches unswitched");
STATISTIC(NumGuards, "Number of guards turned into branches for unswitching");
STATISTIC(NumTrivial, "Number of unswitches that are trivial");
STATISTIC(
    NumCostMultiplierSkipped,
    "Number of unswitch candidates that had their cost multiplier skipped");

static cl::opt<bool> EnableNonTrivialUnswitch(
    "enable-nontrivial-unswitch", cl::init(false), cl::Hidden,
    cl::desc("Forcibly enables non-trivial loop unswitching rather than "
             "following the configuration passed into the pass."));

static cl::opt<int>
    UnswitchThreshold("unswitch-threshold", cl::init(50), cl::Hidden,
                      cl::desc("The cost threshold for unswitching a loop."));

static cl::opt<bool> EnableUnswitchCostMultiplier(
    "enable-unswitch-cost-multiplier", cl::init(true), cl::Hidden,
    cl::desc("Enable unswitch cost multiplier that prohibits exponential "
             "explosion in nontrivial unswitch."));
static cl::opt<int> UnswitchSiblingsToplevelDiv(
    "unswitch-siblings-toplevel-div", cl::init(2), cl::Hidden,
    cl::desc("Toplevel siblings divisor for cost multiplier."));
static cl::opt<int> UnswitchNumInitialUnscaledCandidates(
    "unswitch-num-initial-unscaled-candidates", cl::init(8), cl::Hidden,
    cl::desc("Number of unswitch candidates that are ignored when calculating "
             "cost multiplier."));
static cl::opt<bool> UnswitchGuards(
    "simple-loop-unswitch-guards", cl::init(true), cl::Hidden,
    cl::desc("If enabled, simple loop unswitching will also consider "
             "llvm.experimental.guard intrinsics as unswitch candidates."));

/// Collect all of the loop invariant input values transitively used by the
/// homogeneous instruction graph from a given root.
///
/// This essentially walks from a root recursively through loop variant operands
/// which have the exact same opcode and finds all inputs which are loop
/// invariant. For some operations these can be re-associated and unswitched out
/// of the loop entirely.
static TinyPtrVector<Value *>
collectHomogenousInstGraphLoopInvariants(Loop &L, Instruction &Root,
                                         LoopInfo &LI) {
  assert(!L.isLoopInvariant(&Root) &&
         "Only need to walk the graph if root itself is not invariant.");
  TinyPtrVector<Value *> Invariants;

  // Build a worklist and recurse through operators collecting invariants.
  SmallVector<Instruction *, 4> Worklist;
  SmallPtrSet<Instruction *, 8> Visited;
  Worklist.push_back(&Root);
  Visited.insert(&Root);
  do {
    Instruction &I = *Worklist.pop_back_val();
    for (Value *OpV : I.operand_values()) {
      // Skip constants as unswitching isn't interesting for them.
      if (isa<Constant>(OpV))
        continue;

      // Add it to our result if loop invariant.
      if (L.isLoopInvariant(OpV)) {
        Invariants.push_back(OpV);
        continue;
      }

      // If not an instruction with the same opcode, nothing we can do.
      Instruction *OpI = dyn_cast<Instruction>(OpV);
      if (!OpI || OpI->getOpcode() != Root.getOpcode())
        continue;

      // Visit this operand.
      if (Visited.insert(OpI).second)
        Worklist.push_back(OpI);
    }
  } while (!Worklist.empty());

  return Invariants;
}

static void replaceLoopInvariantUses(Loop &L, Value *Invariant,
                                     Constant &Replacement) {
  assert(!isa<Constant>(Invariant) && "Why are we unswitching on a constant?");

  // Replace uses of LIC in the loop with the given constant.
  for (auto UI = Invariant->use_begin(), UE = Invariant->use_end(); UI != UE;) {
    // Grab the use and walk past it so we can clobber it in the use list.
    Use *U = &*UI++;
    Instruction *UserI = dyn_cast<Instruction>(U->getUser());

    // Replace this use within the loop body.
    if (UserI && L.contains(UserI))
      U->set(&Replacement);
  }
}

/// Check that all the LCSSA PHI nodes in the loop exit block have trivial
/// incoming values along this edge.
static bool areLoopExitPHIsLoopInvariant(Loop &L, BasicBlock &ExitingBB,
                                         BasicBlock &ExitBB) {
  for (Instruction &I : ExitBB) {
    auto *PN = dyn_cast<PHINode>(&I);
    if (!PN)
      // No more PHIs to check.
      return true;

    // If the incoming value for this edge isn't loop invariant the unswitch
    // won't be trivial.
    if (!L.isLoopInvariant(PN->getIncomingValueForBlock(&ExitingBB)))
      return false;
  }
  llvm_unreachable("Basic blocks should never be empty!");
}

/// Insert code to test a set of loop invariant values, and conditionally branch
/// on them.
static void buildPartialUnswitchConditionalBranch(BasicBlock &BB,
                                                  ArrayRef<Value *> Invariants,
                                                  bool Direction,
                                                  BasicBlock &UnswitchedSucc,
                                                  BasicBlock &NormalSucc) {
  IRBuilder<> IRB(&BB);
  Value *Cond = Invariants.front();
  for (Value *Invariant :
       make_range(std::next(Invariants.begin()), Invariants.end()))
    if (Direction)
      Cond = IRB.CreateOr(Cond, Invariant);
    else
      Cond = IRB.CreateAnd(Cond, Invariant);

  IRB.CreateCondBr(Cond, Direction ? &UnswitchedSucc : &NormalSucc,
                   Direction ? &NormalSucc : &UnswitchedSucc);
}

/// Rewrite the PHI nodes in an unswitched loop exit basic block.
///
/// Requires that the loop exit and unswitched basic block are the same, and
/// that the exiting block was a unique predecessor of that block. Rewrites the
/// PHI nodes in that block such that what were LCSSA PHI nodes become trivial
/// PHI nodes from the old preheader that now contains the unswitched
/// terminator.
static void rewritePHINodesForUnswitchedExitBlock(BasicBlock &UnswitchedBB,
                                                  BasicBlock &OldExitingBB,
                                                  BasicBlock &OldPH) {
  for (PHINode &PN : UnswitchedBB.phis()) {
    // When the loop exit is directly unswitched we just need to update the
    // incoming basic block. We loop to handle weird cases with repeated
    // incoming blocks, but expect to typically only have one operand here.
    for (auto i : seq<int>(0, PN.getNumOperands())) {
      assert(PN.getIncomingBlock(i) == &OldExitingBB &&
             "Found incoming block different from unique predecessor!");
      PN.setIncomingBlock(i, &OldPH);
    }
  }
}

/// Rewrite the PHI nodes in the loop exit basic block and the split off
/// unswitched block.
///
/// Because the exit block remains an exit from the loop, this rewrites the
/// LCSSA PHI nodes in it to remove the unswitched edge and introduces PHI
/// nodes into the unswitched basic block to select between the value in the
/// old preheader and the loop exit.
static void rewritePHINodesForExitAndUnswitchedBlocks(BasicBlock &ExitBB,
                                                      BasicBlock &UnswitchedBB,
                                                      BasicBlock &OldExitingBB,
                                                      BasicBlock &OldPH,
                                                      bool FullUnswitch) {
  assert(&ExitBB != &UnswitchedBB &&
         "Must have different loop exit and unswitched blocks!");
  Instruction *InsertPt = &*UnswitchedBB.begin();
  for (PHINode &PN : ExitBB.phis()) {
    auto *NewPN = PHINode::Create(PN.getType(), /*NumReservedValues*/ 2,
                                  PN.getName() + ".split", InsertPt);

    // Walk backwards over the old PHI node's inputs to minimize the cost of
    // removing each one. We have to do this weird loop manually so that we
    // create the same number of new incoming edges in the new PHI as we expect
    // each case-based edge to be included in the unswitched switch in some
    // cases.
    // FIXME: This is really, really gross. It would be much cleaner if LLVM
    // allowed us to create a single entry for a predecessor block without
    // having separate entries for each "edge" even though these edges are
    // required to produce identical results.
    for (int i = PN.getNumIncomingValues() - 1; i >= 0; --i) {
      if (PN.getIncomingBlock(i) != &OldExitingBB)
        continue;

      Value *Incoming = PN.getIncomingValue(i);
      if (FullUnswitch)
        // No more edge from the old exiting block to the exit block.
        PN.removeIncomingValue(i);

      NewPN->addIncoming(Incoming, &OldPH);
    }

    // Now replace the old PHI with the new one and wire the old one in as an
    // input to the new one.
    PN.replaceAllUsesWith(NewPN);
    NewPN->addIncoming(&PN, &ExitBB);
  }
}

/// Hoist the current loop up to the innermost loop containing a remaining exit.
///
/// Because we've removed an exit from the loop, we may have changed the set of
/// loops reachable and need to move the current loop up the loop nest or even
/// to an entirely separate nest.
static void hoistLoopToNewParent(Loop &L, BasicBlock &Preheader,
                                 DominatorTree &DT, LoopInfo &LI) {
  // If the loop is already at the top level, we can't hoist it anywhere.
  Loop *OldParentL = L.getParentLoop();
  if (!OldParentL)
    return;

  SmallVector<BasicBlock *, 4> Exits;
  L.getExitBlocks(Exits);
  Loop *NewParentL = nullptr;
  for (auto *ExitBB : Exits)
    if (Loop *ExitL = LI.getLoopFor(ExitBB))
      if (!NewParentL || NewParentL->contains(ExitL))
        NewParentL = ExitL;

  if (NewParentL == OldParentL)
    return;

  // The new parent loop (if different) should always contain the old one.
  if (NewParentL)
    assert(NewParentL->contains(OldParentL) &&
           "Can only hoist this loop up the nest!");

  // The preheader will need to move with the body of this loop. However,
  // because it isn't in this loop we also need to update the primary loop map.
  assert(OldParentL == LI.getLoopFor(&Preheader) &&
         "Parent loop of this loop should contain this loop's preheader!");
  LI.changeLoopFor(&Preheader, NewParentL);

  // Remove this loop from its old parent.
  OldParentL->removeChildLoop(&L);

  // Add the loop either to the new parent or as a top-level loop.
  if (NewParentL)
    NewParentL->addChildLoop(&L);
  else
    LI.addTopLevelLoop(&L);

  // Remove this loops blocks from the old parent and every other loop up the
  // nest until reaching the new parent. Also update all of these
  // no-longer-containing loops to reflect the nesting change.
  for (Loop *OldContainingL = OldParentL; OldContainingL != NewParentL;
       OldContainingL = OldContainingL->getParentLoop()) {
    llvm::erase_if(OldContainingL->getBlocksVector(),
                   [&](const BasicBlock *BB) {
                     return BB == &Preheader || L.contains(BB);
                   });

    OldContainingL->getBlocksSet().erase(&Preheader);
    for (BasicBlock *BB : L.blocks())
      OldContainingL->getBlocksSet().erase(BB);

    // Because we just hoisted a loop out of this one, we have essentially
    // created new exit paths from it. That means we need to form LCSSA PHI
    // nodes for values used in the no-longer-nested loop.
    formLCSSA(*OldContainingL, DT, &LI, nullptr);

    // We shouldn't need to form dedicated exits because the exit introduced
    // here is the (just split by unswitching) preheader. However, after trivial
    // unswitching it is possible to get new non-dedicated exits out of parent
    // loop so let's conservatively form dedicated exit blocks and figure out
    // if we can optimize later.
    formDedicatedExitBlocks(OldContainingL, &DT, &LI, /*PreserveLCSSA*/ true);
  }
}

/// Unswitch a trivial branch if the condition is loop invariant.
///
/// This routine should only be called when loop code leading to the branch has
/// been validated as trivial (no side effects). This routine checks if the
/// condition is invariant and one of the successors is a loop exit. This
/// allows us to unswitch without duplicating the loop, making it trivial.
///
/// If this routine fails to unswitch the branch it returns false.
///
/// If the branch can be unswitched, this routine splits the preheader and
/// hoists the branch above that split. Preserves loop simplified form
/// (splitting the exit block as necessary). It simplifies the branch within
/// the loop to an unconditional branch but doesn't remove it entirely. Further
/// cleanup can be done with some simplify-cfg like pass.
///
/// If `SE` is not null, it will be updated based on the potential loop SCEVs
/// invalidated by this.
static bool unswitchTrivialBranch(Loop &L, BranchInst &BI, DominatorTree &DT,
                                  LoopInfo &LI, ScalarEvolution *SE,
                                  MemorySSAUpdater *MSSAU) {
  assert(BI.isConditional() && "Can only unswitch a conditional branch!");
  LLVM_DEBUG(dbgs() << "  Trying to unswitch branch: " << BI << "\n");

  // The loop invariant values that we want to unswitch.
  TinyPtrVector<Value *> Invariants;

  // When true, we're fully unswitching the branch rather than just unswitching
  // some input conditions to the branch.
  bool FullUnswitch = false;

  if (L.isLoopInvariant(BI.getCondition())) {
    Invariants.push_back(BI.getCondition());
    FullUnswitch = true;
  } else {
    if (auto *CondInst = dyn_cast<Instruction>(BI.getCondition()))
      Invariants = collectHomogenousInstGraphLoopInvariants(L, *CondInst, LI);
    if (Invariants.empty())
      // Couldn't find invariant inputs!
      return false;
  }

  // Check that one of the branch's successors exits, and which one.
  bool ExitDirection = true;
  int LoopExitSuccIdx = 0;
  auto *LoopExitBB = BI.getSuccessor(0);
  if (L.contains(LoopExitBB)) {
    ExitDirection = false;
    LoopExitSuccIdx = 1;
    LoopExitBB = BI.getSuccessor(1);
    if (L.contains(LoopExitBB))
      return false;
  }
  auto *ContinueBB = BI.getSuccessor(1 - LoopExitSuccIdx);
  auto *ParentBB = BI.getParent();
  if (!areLoopExitPHIsLoopInvariant(L, *ParentBB, *LoopExitBB))
    return false;

  // When unswitching only part of the branch's condition, we need the exit
  // block to be reached directly from the partially unswitched input. This can
  // be done when the exit block is along the true edge and the branch condition
  // is a graph of `or` operations, or the exit block is along the false edge
  // and the condition is a graph of `and` operations.
  if (!FullUnswitch) {
    if (ExitDirection) {
      if (cast<Instruction>(BI.getCondition())->getOpcode() != Instruction::Or)
        return false;
    } else {
      if (cast<Instruction>(BI.getCondition())->getOpcode() != Instruction::And)
        return false;
    }
  }

  LLVM_DEBUG({
    dbgs() << "    unswitching trivial invariant conditions for: " << BI
           << "\n";
    for (Value *Invariant : Invariants) {
      dbgs() << "      " << *Invariant << " == true";
      if (Invariant != Invariants.back())
        dbgs() << " ||";
      dbgs() << "\n";
    }
  });

  // If we have scalar evolutions, we need to invalidate them including this
  // loop and the loop containing the exit block.
  if (SE) {
    if (Loop *ExitL = LI.getLoopFor(LoopExitBB))
      SE->forgetLoop(ExitL);
    else
      // Forget the entire nest as this exits the entire nest.
      SE->forgetTopmostLoop(&L);
  }

  if (MSSAU && VerifyMemorySSA)
    MSSAU->getMemorySSA()->verifyMemorySSA();

  // Split the preheader, so that we know that there is a safe place to insert
  // the conditional branch. We will change the preheader to have a conditional
  // branch on LoopCond.
  BasicBlock *OldPH = L.getLoopPreheader();
  BasicBlock *NewPH = SplitEdge(OldPH, L.getHeader(), &DT, &LI, MSSAU);

  // Now that we have a place to insert the conditional branch, create a place
  // to branch to: this is the exit block out of the loop that we are
  // unswitching. We need to split this if there are other loop predecessors.
  // Because the loop is in simplified form, *any* other predecessor is enough.
  BasicBlock *UnswitchedBB;
  if (FullUnswitch && LoopExitBB->getUniquePredecessor()) {
    assert(LoopExitBB->getUniquePredecessor() == BI.getParent() &&
           "A branch's parent isn't a predecessor!");
    UnswitchedBB = LoopExitBB;
  } else {
    UnswitchedBB =
        SplitBlock(LoopExitBB, &LoopExitBB->front(), &DT, &LI, MSSAU);
  }

  if (MSSAU && VerifyMemorySSA)
    MSSAU->getMemorySSA()->verifyMemorySSA();

  // Actually move the invariant uses into the unswitched position. If possible,
  // we do this by moving the instructions, but when doing partial unswitching
  // we do it by building a new merge of the values in the unswitched position.
  OldPH->getTerminator()->eraseFromParent();
  if (FullUnswitch) {
    // If fully unswitching, we can use the existing branch instruction.
    // Splice it into the old PH to gate reaching the new preheader and re-point
    // its successors.
    OldPH->getInstList().splice(OldPH->end(), BI.getParent()->getInstList(),
                                BI);
    if (MSSAU) {
      // Temporarily clone the terminator, to make MSSA update cheaper by
      // separating "insert edge" updates from "remove edge" ones.
      ParentBB->getInstList().push_back(BI.clone());
    } else {
      // Create a new unconditional branch that will continue the loop as a new
      // terminator.
      BranchInst::Create(ContinueBB, ParentBB);
    }
    BI.setSuccessor(LoopExitSuccIdx, UnswitchedBB);
    BI.setSuccessor(1 - LoopExitSuccIdx, NewPH);
  } else {
    // Only unswitching a subset of inputs to the condition, so we will need to
    // build a new branch that merges the invariant inputs.
    if (ExitDirection)
      assert(cast<Instruction>(BI.getCondition())->getOpcode() ==
                 Instruction::Or &&
             "Must have an `or` of `i1`s for the condition!");
    else
      assert(cast<Instruction>(BI.getCondition())->getOpcode() ==
                 Instruction::And &&
             "Must have an `and` of `i1`s for the condition!");
    buildPartialUnswitchConditionalBranch(*OldPH, Invariants, ExitDirection,
                                          *UnswitchedBB, *NewPH);
  }

  // Update the dominator tree with the added edge.
  DT.insertEdge(OldPH, UnswitchedBB);

  // After the dominator tree was updated with the added edge, update MemorySSA
  // if available.
  if (MSSAU) {
    SmallVector<CFGUpdate, 1> Updates;
    Updates.push_back({cfg::UpdateKind::Insert, OldPH, UnswitchedBB});
    MSSAU->applyInsertUpdates(Updates, DT);
  }

  // Finish updating dominator tree and memory ssa for full unswitch.
  if (FullUnswitch) {
    if (MSSAU) {
      // Remove the cloned branch instruction.
      ParentBB->getTerminator()->eraseFromParent();
      // Create unconditional branch now.
      BranchInst::Create(ContinueBB, ParentBB);
      MSSAU->removeEdge(ParentBB, LoopExitBB);
    }
    DT.deleteEdge(ParentBB, LoopExitBB);
  }

  if (MSSAU && VerifyMemorySSA)
    MSSAU->getMemorySSA()->verifyMemorySSA();

  // Rewrite the relevant PHI nodes.
  if (UnswitchedBB == LoopExitBB)
    rewritePHINodesForUnswitchedExitBlock(*UnswitchedBB, *ParentBB, *OldPH);
  else
    rewritePHINodesForExitAndUnswitchedBlocks(*LoopExitBB, *UnswitchedBB,
                                              *ParentBB, *OldPH, FullUnswitch);

  // The constant we can replace all of our invariants with inside the loop
  // body. If any of the invariants have a value other than this the loop won't
  // be entered.
  ConstantInt *Replacement = ExitDirection
                                 ? ConstantInt::getFalse(BI.getContext())
                                 : ConstantInt::getTrue(BI.getContext());

  // Since this is an i1 condition we can also trivially replace uses of it
  // within the loop with a constant.
  for (Value *Invariant : Invariants)
    replaceLoopInvariantUses(L, Invariant, *Replacement);

  // If this was full unswitching, we may have changed the nesting relationship
  // for this loop so hoist it to its correct parent if needed.
  if (FullUnswitch)
    hoistLoopToNewParent(L, *NewPH, DT, LI);

  LLVM_DEBUG(dbgs() << "    done: unswitching trivial branch...\n");
  ++NumTrivial;
  ++NumBranches;
  return true;
}

/// Unswitch a trivial switch if the condition is loop invariant.
///
/// This routine should only be called when loop code leading to the switch has
/// been validated as trivial (no side effects). This routine checks if the
/// condition is invariant and that at least one of the successors is a loop
/// exit. This allows us to unswitch without duplicating the loop, making it
/// trivial.
///
/// If this routine fails to unswitch the switch it returns false.
///
/// If the switch can be unswitched, this routine splits the preheader and
/// copies the switch above that split. If the default case is one of the
/// exiting cases, it copies the non-exiting cases and points them at the new
/// preheader. If the default case is not exiting, it copies the exiting cases
/// and points the default at the preheader. It preserves loop simplified form
/// (splitting the exit blocks as necessary). It simplifies the switch within
/// the loop by removing now-dead cases. If the default case is one of those
/// unswitched, it replaces its destination with a new basic block containing
/// only unreachable. Such basic blocks, while technically loop exits, are not
/// considered for unswitching so this is a stable transform and the same
/// switch will not be revisited. If after unswitching there is only a single
/// in-loop successor, the switch is further simplified to an unconditional
/// branch. Still more cleanup can be done with some simplify-cfg like pass.
///
/// If `SE` is not null, it will be updated based on the potential loop SCEVs
/// invalidated by this.
static bool unswitchTrivialSwitch(Loop &L, SwitchInst &SI, DominatorTree &DT,
                                  LoopInfo &LI, ScalarEvolution *SE,
                                  MemorySSAUpdater *MSSAU) {
  LLVM_DEBUG(dbgs() << "  Trying to unswitch switch: " << SI << "\n");
  Value *LoopCond = SI.getCondition();

  // If this isn't switching on an invariant condition, we can't unswitch it.
  if (!L.isLoopInvariant(LoopCond))
    return false;

  auto *ParentBB = SI.getParent();

  SmallVector<int, 4> ExitCaseIndices;
  for (auto Case : SI.cases()) {
    auto *SuccBB = Case.getCaseSuccessor();
    if (!L.contains(SuccBB) &&
        areLoopExitPHIsLoopInvariant(L, *ParentBB, *SuccBB))
      ExitCaseIndices.push_back(Case.getCaseIndex());
  }
  BasicBlock *DefaultExitBB = nullptr;
  if (!L.contains(SI.getDefaultDest()) &&
      areLoopExitPHIsLoopInvariant(L, *ParentBB, *SI.getDefaultDest()) &&
      !isa<UnreachableInst>(SI.getDefaultDest()->getTerminator()))
    DefaultExitBB = SI.getDefaultDest();
  else if (ExitCaseIndices.empty())
    return false;

  LLVM_DEBUG(dbgs() << "    unswitching trivial switch...\n");

  if (MSSAU && VerifyMemorySSA)
    MSSAU->getMemorySSA()->verifyMemorySSA();

  // We may need to invalidate SCEVs for the outermost loop reached by any of
  // the exits.
  Loop *OuterL = &L;

  if (DefaultExitBB) {
    // Clear out the default destination temporarily to allow accurate
    // predecessor lists to be examined below.
    SI.setDefaultDest(nullptr);
    // Check the loop containing this exit.
    Loop *ExitL = LI.getLoopFor(DefaultExitBB);
    if (!ExitL || ExitL->contains(OuterL))
      OuterL = ExitL;
  }

  // Store the exit cases into a separate data structure and remove them from
  // the switch.
  SmallVector<std::pair<ConstantInt *, BasicBlock *>, 4> ExitCases;
  ExitCases.reserve(ExitCaseIndices.size());
  // We walk the case indices backwards so that we remove the last case first
  // and don't disrupt the earlier indices.
  for (unsigned Index : reverse(ExitCaseIndices)) {
    auto CaseI = SI.case_begin() + Index;
    // Compute the outer loop from this exit.
    Loop *ExitL = LI.getLoopFor(CaseI->getCaseSuccessor());
    if (!ExitL || ExitL->contains(OuterL))
      OuterL = ExitL;
    // Save the value of this case.
    ExitCases.push_back({CaseI->getCaseValue(), CaseI->getCaseSuccessor()});
    // Delete the unswitched cases.
    SI.removeCase(CaseI);
  }

  if (SE) {
    if (OuterL)
      SE->forgetLoop(OuterL);
    else
      SE->forgetTopmostLoop(&L);
  }

  // Check if after this all of the remaining cases point at the same
  // successor.
  BasicBlock *CommonSuccBB = nullptr;
  if (SI.getNumCases() > 0 &&
      std::all_of(std::next(SI.case_begin()), SI.case_end(),
                  [&SI](const SwitchInst::CaseHandle &Case) {
                    return Case.getCaseSuccessor() ==
                           SI.case_begin()->getCaseSuccessor();
                  }))
    CommonSuccBB = SI.case_begin()->getCaseSuccessor();
  if (!DefaultExitBB) {
    // If we're not unswitching the default, we need it to match any cases to
    // have a common successor or if we have no cases it is the common
    // successor.
    if (SI.getNumCases() == 0)
      CommonSuccBB = SI.getDefaultDest();
    else if (SI.getDefaultDest() != CommonSuccBB)
      CommonSuccBB = nullptr;
  }

  // Split the preheader, so that we know that there is a safe place to insert
  // the switch.
  BasicBlock *OldPH = L.getLoopPreheader();
  BasicBlock *NewPH = SplitEdge(OldPH, L.getHeader(), &DT, &LI, MSSAU);
  OldPH->getTerminator()->eraseFromParent();

  // Now add the unswitched switch.
  auto *NewSI = SwitchInst::Create(LoopCond, NewPH, ExitCases.size(), OldPH);

  // Rewrite the IR for the unswitched basic blocks. This requires two steps.
  // First, we split any exit blocks with remaining in-loop predecessors. Then
  // we update the PHIs in one of two ways depending on if there was a split.
  // We walk in reverse so that we split in the same order as the cases
  // appeared. This is purely for convenience of reading the resulting IR, but
  // it doesn't cost anything really.
  SmallPtrSet<BasicBlock *, 2> UnswitchedExitBBs;
  SmallDenseMap<BasicBlock *, BasicBlock *, 2> SplitExitBBMap;
  // Handle the default exit if necessary.
  // FIXME: It'd be great if we could merge this with the loop below but LLVM's
  // ranges aren't quite powerful enough yet.
  if (DefaultExitBB) {
    if (pred_empty(DefaultExitBB)) {
      UnswitchedExitBBs.insert(DefaultExitBB);
      rewritePHINodesForUnswitchedExitBlock(*DefaultExitBB, *ParentBB, *OldPH);
    } else {
      auto *SplitBB =
          SplitBlock(DefaultExitBB, &DefaultExitBB->front(), &DT, &LI, MSSAU);
      rewritePHINodesForExitAndUnswitchedBlocks(*DefaultExitBB, *SplitBB,
                                                *ParentBB, *OldPH,
                                                /*FullUnswitch*/ true);
      DefaultExitBB = SplitExitBBMap[DefaultExitBB] = SplitBB;
    }
  }
  // Note that we must use a reference in the for loop so that we update the
  // container.
  for (auto &CasePair : reverse(ExitCases)) {
    // Grab a reference to the exit block in the pair so that we can update it.
    BasicBlock *ExitBB = CasePair.second;

    // If this case is the last edge into the exit block, we can simply reuse it
    // as it will no longer be a loop exit. No mapping necessary.
    if (pred_empty(ExitBB)) {
      // Only rewrite once.
      if (UnswitchedExitBBs.insert(ExitBB).second)
        rewritePHINodesForUnswitchedExitBlock(*ExitBB, *ParentBB, *OldPH);
      continue;
    }

    // Otherwise we need to split the exit block so that we retain an exit
    // block from the loop and a target for the unswitched condition.
    BasicBlock *&SplitExitBB = SplitExitBBMap[ExitBB];
    if (!SplitExitBB) {
      // If this is the first time we see this, do the split and remember it.
      SplitExitBB = SplitBlock(ExitBB, &ExitBB->front(), &DT, &LI, MSSAU);
      rewritePHINodesForExitAndUnswitchedBlocks(*ExitBB, *SplitExitBB,
                                                *ParentBB, *OldPH,
                                                /*FullUnswitch*/ true);
    }
    // Update the case pair to point to the split block.
    CasePair.second = SplitExitBB;
  }

  // Now add the unswitched cases. We do this in reverse order as we built them
  // in reverse order.
  for (auto CasePair : reverse(ExitCases)) {
    ConstantInt *CaseVal = CasePair.first;
    BasicBlock *UnswitchedBB = CasePair.second;

    NewSI->addCase(CaseVal, UnswitchedBB);
  }

  // If the default was unswitched, re-point it and add explicit cases for
  // entering the loop.
  if (DefaultExitBB) {
    NewSI->setDefaultDest(DefaultExitBB);

    // We removed all the exit cases, so we just copy the cases to the
    // unswitched switch.
    for (auto Case : SI.cases())
      NewSI->addCase(Case.getCaseValue(), NewPH);
  }

  // If we ended up with a common successor for every path through the switch
  // after unswitching, rewrite it to an unconditional branch to make it easy
  // to recognize. Otherwise we potentially have to recognize the default case
  // pointing at unreachable and other complexity.
  if (CommonSuccBB) {
    BasicBlock *BB = SI.getParent();
    // We may have had multiple edges to this common successor block, so remove
    // them as predecessors. We skip the first one, either the default or the
    // actual first case.
    bool SkippedFirst = DefaultExitBB == nullptr;
    for (auto Case : SI.cases()) {
      assert(Case.getCaseSuccessor() == CommonSuccBB &&
             "Non-common successor!");
      (void)Case;
      if (!SkippedFirst) {
        SkippedFirst = true;
        continue;
      }
      CommonSuccBB->removePredecessor(BB,
                                      /*DontDeleteUselessPHIs*/ true);
    }
    // Now nuke the switch and replace it with a direct branch.
    SI.eraseFromParent();
    BranchInst::Create(CommonSuccBB, BB);
  } else if (DefaultExitBB) {
    assert(SI.getNumCases() > 0 &&
           "If we had no cases we'd have a common successor!");
    // Move the last case to the default successor. This is valid as if the
    // default got unswitched it cannot be reached. This has the advantage of
    // being simple and keeping the number of edges from this switch to
    // successors the same, and avoiding any PHI update complexity.
    auto LastCaseI = std::prev(SI.case_end());
    SI.setDefaultDest(LastCaseI->getCaseSuccessor());
    SI.removeCase(LastCaseI);
  }

  // Walk the unswitched exit blocks and the unswitched split blocks and update
  // the dominator tree based on the CFG edits. While we are walking unordered
  // containers here, the API for applyUpdates takes an unordered list of
  // updates and requires them to not contain duplicates.
  SmallVector<DominatorTree::UpdateType, 4> DTUpdates;
  for (auto *UnswitchedExitBB : UnswitchedExitBBs) {
    DTUpdates.push_back({DT.Delete, ParentBB, UnswitchedExitBB});
    DTUpdates.push_back({DT.Insert, OldPH, UnswitchedExitBB});
  }
  for (auto SplitUnswitchedPair : SplitExitBBMap) {
    auto *UnswitchedBB = SplitUnswitchedPair.second;
    DTUpdates.push_back({DT.Delete, ParentBB, UnswitchedBB});
    DTUpdates.push_back({DT.Insert, OldPH, UnswitchedBB});
  }
  DT.applyUpdates(DTUpdates);

  if (MSSAU) {
    MSSAU->applyUpdates(DTUpdates, DT);
    if (VerifyMemorySSA)
      MSSAU->getMemorySSA()->verifyMemorySSA();
  }

  assert(DT.verify(DominatorTree::VerificationLevel::Fast));

  // We may have changed the nesting relationship for this loop so hoist it to
  // its correct parent if needed.
  hoistLoopToNewParent(L, *NewPH, DT, LI);

  ++NumTrivial;
  ++NumSwitches;
  LLVM_DEBUG(dbgs() << "    done: unswitching trivial switch...\n");
  return true;
}

/// This routine scans the loop to find a branch or switch which occurs before
/// any side effects occur. These can potentially be unswitched without
/// duplicating the loop. If a branch or switch is successfully unswitched the
/// scanning continues to see if subsequent branches or switches have become
/// trivial. Once all trivial candidates have been unswitched, this routine
/// returns.
///
/// The return value indicates whether anything was unswitched (and therefore
/// changed).
///
/// If `SE` is not null, it will be updated based on the potential loop SCEVs
/// invalidated by this.
static bool unswitchAllTrivialConditions(Loop &L, DominatorTree &DT,
                                         LoopInfo &LI, ScalarEvolution *SE,
                                         MemorySSAUpdater *MSSAU) {
  bool Changed = false;

  // If loop header has only one reachable successor we should keep looking for
  // trivial condition candidates in the successor as well. An alternative is
  // to constant fold conditions and merge successors into loop header (then we
  // only need to check header's terminator). The reason for not doing this in
  // LoopUnswitch pass is that it could potentially break LoopPassManager's
  // invariants. Folding dead branches could either eliminate the current loop
  // or make other loops unreachable. LCSSA form might also not be preserved
  // after deleting branches. The following code keeps traversing loop header's
  // successors until it finds the trivial condition candidate (condition that
  // is not a constant). Since unswitching generates branches with constant
  // conditions, this scenario could be very common in practice.
  BasicBlock *CurrentBB = L.getHeader();
  SmallPtrSet<BasicBlock *, 8> Visited;
  Visited.insert(CurrentBB);
  do {
    // Check if there are any side-effecting instructions (e.g. stores, calls,
    // volatile loads) in the part of the loop that the code *would* execute
    // without unswitching.
    if (llvm::any_of(*CurrentBB,
                     [](Instruction &I) { return I.mayHaveSideEffects(); }))
      return Changed;

    Instruction *CurrentTerm = CurrentBB->getTerminator();

    if (auto *SI = dyn_cast<SwitchInst>(CurrentTerm)) {
      // Don't bother trying to unswitch past a switch with a constant
      // condition. This should be removed prior to running this pass by
      // simplify-cfg.
      if (isa<Constant>(SI->getCondition()))
        return Changed;

      if (!unswitchTrivialSwitch(L, *SI, DT, LI, SE, MSSAU))
        // Couldn't unswitch this one so we're done.
        return Changed;

      // Mark that we managed to unswitch something.
      Changed = true;

      // If unswitching turned the terminator into an unconditional branch then
      // we can continue. The unswitching logic specifically works to fold any
      // cases it can into an unconditional branch to make it easier to
      // recognize here.
      auto *BI = dyn_cast<BranchInst>(CurrentBB->getTerminator());
      if (!BI || BI->isConditional())
        return Changed;

      CurrentBB = BI->getSuccessor(0);
      continue;
    }

    auto *BI = dyn_cast<BranchInst>(CurrentTerm);
    if (!BI)
      // We do not understand other terminator instructions.
      return Changed;

    // Don't bother trying to unswitch past an unconditional branch or a branch
    // with a constant value. These should be removed by simplify-cfg prior to
    // running this pass.
    if (!BI->isConditional() || isa<Constant>(BI->getCondition()))
      return Changed;

    // Found a trivial condition candidate: non-foldable conditional branch. If
    // we fail to unswitch this, we can't do anything else that is trivial.
    if (!unswitchTrivialBranch(L, *BI, DT, LI, SE, MSSAU))
      return Changed;

    // Mark that we managed to unswitch something.
    Changed = true;

    // If we only unswitched some of the conditions feeding the branch, we won't
    // have collapsed it to a single successor.
    BI = cast<BranchInst>(CurrentBB->getTerminator());
    if (BI->isConditional())
      return Changed;

    // Follow the newly unconditional branch into its successor.
    CurrentBB = BI->getSuccessor(0);

    // When continuing, if we exit the loop or reach a previous visited block,
    // then we can not reach any trivial condition candidates (unfoldable
    // branch instructions or switch instructions) and no unswitch can happen.
  } while (L.contains(CurrentBB) && Visited.insert(CurrentBB).second);

  return Changed;
}

/// Build the cloned blocks for an unswitched copy of the given loop.
///
/// The cloned blocks are inserted before the loop preheader (`LoopPH`) and
/// after the split block (`SplitBB`) that will be used to select between the
/// cloned and original loop.
///
/// This routine handles cloning all of the necessary loop blocks and exit
/// blocks including rewriting their instructions and the relevant PHI nodes.
/// Any loop blocks or exit blocks which are dominated by a different successor
/// than the one for this clone of the loop blocks can be trivially skipped. We
/// use the `DominatingSucc` map to determine whether a block satisfies that
/// property with a simple map lookup.
///
/// It also correctly creates the unconditional branch in the cloned
/// unswitched parent block to only point at the unswitched successor.
///
/// This does not handle most of the necessary updates to `LoopInfo`. Only exit
/// block splitting is correctly reflected in `LoopInfo`, essentially all of
/// the cloned blocks (and their loops) are left without full `LoopInfo`
/// updates. This also doesn't fully update `DominatorTree`. It adds the cloned
/// blocks to them but doesn't create the cloned `DominatorTree` structure and
/// instead the caller must recompute an accurate DT. It *does* correctly
/// update the `AssumptionCache` provided in `AC`.
static BasicBlock *buildClonedLoopBlocks(
    Loop &L, BasicBlock *LoopPH, BasicBlock *SplitBB,
    ArrayRef<BasicBlock *> ExitBlocks, BasicBlock *ParentBB,
    BasicBlock *UnswitchedSuccBB, BasicBlock *ContinueSuccBB,
    const SmallDenseMap<BasicBlock *, BasicBlock *, 16> &DominatingSucc,
    ValueToValueMapTy &VMap,
    SmallVectorImpl<DominatorTree::UpdateType> &DTUpdates, AssumptionCache &AC,
    DominatorTree &DT, LoopInfo &LI, MemorySSAUpdater *MSSAU) {
  SmallVector<BasicBlock *, 4> NewBlocks;
  NewBlocks.reserve(L.getNumBlocks() + ExitBlocks.size());

  // We will need to clone a bunch of blocks, wrap up the clone operation in
  // a helper.
  auto CloneBlock = [&](BasicBlock *OldBB) {
    // Clone the basic block and insert it before the new preheader.
    BasicBlock *NewBB = CloneBasicBlock(OldBB, VMap, ".us", OldBB->getParent());
    NewBB->moveBefore(LoopPH);

    // Record this block and the mapping.
    NewBlocks.push_back(NewBB);
    VMap[OldBB] = NewBB;

    return NewBB;
  };

  // We skip cloning blocks when they have a dominating succ that is not the
  // succ we are cloning for.
  auto SkipBlock = [&](BasicBlock *BB) {
    auto It = DominatingSucc.find(BB);
    return It != DominatingSucc.end() && It->second != UnswitchedSuccBB;
  };

  // First, clone the preheader.
  auto *ClonedPH = CloneBlock(LoopPH);

  // Then clone all the loop blocks, skipping the ones that aren't necessary.
  for (auto *LoopBB : L.blocks())
    if (!SkipBlock(LoopBB))
      CloneBlock(LoopBB);

  // Split all the loop exit edges so that when we clone the exit blocks, if
  // any of the exit blocks are *also* a preheader for some other loop, we
  // don't create multiple predecessors entering the loop header.
  for (auto *ExitBB : ExitBlocks) {
    if (SkipBlock(ExitBB))
      continue;

    // When we are going to clone an exit, we don't need to clone all the
    // instructions in the exit block and we want to ensure we have an easy
    // place to merge the CFG, so split the exit first. This is always safe to
    // do because there cannot be any non-loop predecessors of a loop exit in
    // loop simplified form.
    auto *MergeBB = SplitBlock(ExitBB, &ExitBB->front(), &DT, &LI, MSSAU);

    // Rearrange the names to make it easier to write test cases by having the
    // exit block carry the suffix rather than the merge block carrying the
    // suffix.
    MergeBB->takeName(ExitBB);
    ExitBB->setName(Twine(MergeBB->getName()) + ".split");

    // Now clone the original exit block.
    auto *ClonedExitBB = CloneBlock(ExitBB);
    assert(ClonedExitBB->getTerminator()->getNumSuccessors() == 1 &&
           "Exit block should have been split to have one successor!");
    assert(ClonedExitBB->getTerminator()->getSuccessor(0) == MergeBB &&
           "Cloned exit block has the wrong successor!");

    // Remap any cloned instructions and create a merge phi node for them.
    for (auto ZippedInsts : llvm::zip_first(
             llvm::make_range(ExitBB->begin(), std::prev(ExitBB->end())),
             llvm::make_range(ClonedExitBB->begin(),
                              std::prev(ClonedExitBB->end())))) {
      Instruction &I = std::get<0>(ZippedInsts);
      Instruction &ClonedI = std::get<1>(ZippedInsts);

      // The only instructions in the exit block should be PHI nodes and
      // potentially a landing pad.
      assert(
          (isa<PHINode>(I) || isa<LandingPadInst>(I) || isa<CatchPadInst>(I)) &&
          "Bad instruction in exit block!");
      // We should have a value map between the instruction and its clone.
      assert(VMap.lookup(&I) == &ClonedI && "Mismatch in the value map!");

      auto *MergePN =
          PHINode::Create(I.getType(), /*NumReservedValues*/ 2, ".us-phi",
                          &*MergeBB->getFirstInsertionPt());
      I.replaceAllUsesWith(MergePN);
      MergePN->addIncoming(&I, ExitBB);
      MergePN->addIncoming(&ClonedI, ClonedExitBB);
    }
  }

  // Rewrite the instructions in the cloned blocks to refer to the instructions
  // in the cloned blocks. We have to do this as a second pass so that we have
  // everything available. Also, we have inserted new instructions which may
  // include assume intrinsics, so we update the assumption cache while
  // processing this.
  for (auto *ClonedBB : NewBlocks)
    for (Instruction &I : *ClonedBB) {
      RemapInstruction(&I, VMap,
                       RF_NoModuleLevelChanges | RF_IgnoreMissingLocals);
      if (auto *II = dyn_cast<IntrinsicInst>(&I))
        if (II->getIntrinsicID() == Intrinsic::assume)
          AC.registerAssumption(II);
    }

  // Update any PHI nodes in the cloned successors of the skipped blocks to not
  // have spurious incoming values.
  for (auto *LoopBB : L.blocks())
    if (SkipBlock(LoopBB))
      for (auto *SuccBB : successors(LoopBB))
        if (auto *ClonedSuccBB = cast_or_null<BasicBlock>(VMap.lookup(SuccBB)))
          for (PHINode &PN : ClonedSuccBB->phis())
            PN.removeIncomingValue(LoopBB, /*DeletePHIIfEmpty*/ false);

  // Remove the cloned parent as a predecessor of any successor we ended up
  // cloning other than the unswitched one.
  auto *ClonedParentBB = cast<BasicBlock>(VMap.lookup(ParentBB));
  for (auto *SuccBB : successors(ParentBB)) {
    if (SuccBB == UnswitchedSuccBB)
      continue;

    auto *ClonedSuccBB = cast_or_null<BasicBlock>(VMap.lookup(SuccBB));
    if (!ClonedSuccBB)
      continue;

    ClonedSuccBB->removePredecessor(ClonedParentBB,
                                    /*DontDeleteUselessPHIs*/ true);
  }

  // Replace the cloned branch with an unconditional branch to the cloned
  // unswitched successor.
  auto *ClonedSuccBB = cast<BasicBlock>(VMap.lookup(UnswitchedSuccBB));
  ClonedParentBB->getTerminator()->eraseFromParent();
  BranchInst::Create(ClonedSuccBB, ClonedParentBB);

  // If there are duplicate entries in the PHI nodes because of multiple edges
  // to the unswitched successor, we need to nuke all but one as we replaced it
  // with a direct branch.
  for (PHINode &PN : ClonedSuccBB->phis()) {
    bool Found = false;
    // Loop over the incoming operands backwards so we can easily delete as we
    // go without invalidating the index.
    for (int i = PN.getNumOperands() - 1; i >= 0; --i) {
      if (PN.getIncomingBlock(i) != ClonedParentBB)
        continue;
      if (!Found) {
        Found = true;
        continue;
      }
      PN.removeIncomingValue(i, /*DeletePHIIfEmpty*/ false);
    }
  }

  // Record the domtree updates for the new blocks.
  SmallPtrSet<BasicBlock *, 4> SuccSet;
  for (auto *ClonedBB : NewBlocks) {
    for (auto *SuccBB : successors(ClonedBB))
      if (SuccSet.insert(SuccBB).second)
        DTUpdates.push_back({DominatorTree::Insert, ClonedBB, SuccBB});
    SuccSet.clear();
  }

  return ClonedPH;
}

/// Recursively clone the specified loop and all of its children.
///
/// The target parent loop for the clone should be provided, or can be null if
/// the clone is a top-level loop. While cloning, all the blocks are mapped
/// with the provided value map. The entire original loop must be present in
/// the value map. The cloned loop is returned.
static Loop *cloneLoopNest(Loop &OrigRootL, Loop *RootParentL,
                           const ValueToValueMapTy &VMap, LoopInfo &LI) {
  auto AddClonedBlocksToLoop = [&](Loop &OrigL, Loop &ClonedL) {
    assert(ClonedL.getBlocks().empty() && "Must start with an empty loop!");
    ClonedL.reserveBlocks(OrigL.getNumBlocks());
    for (auto *BB : OrigL.blocks()) {
      auto *ClonedBB = cast<BasicBlock>(VMap.lookup(BB));
      ClonedL.addBlockEntry(ClonedBB);
      if (LI.getLoopFor(BB) == &OrigL)
        LI.changeLoopFor(ClonedBB, &ClonedL);
    }
  };

  // We specially handle the first loop because it may get cloned into
  // a different parent and because we most commonly are cloning leaf loops.
  Loop *ClonedRootL = LI.AllocateLoop();
  if (RootParentL)
    RootParentL->addChildLoop(ClonedRootL);
  else
    LI.addTopLevelLoop(ClonedRootL);
  AddClonedBlocksToLoop(OrigRootL, *ClonedRootL);

  if (OrigRootL.empty())
    return ClonedRootL;

  // If we have a nest, we can quickly clone the entire loop nest using an
  // iterative approach because it is a tree. We keep the cloned parent in the
  // data structure to avoid repeatedly querying through a map to find it.
  SmallVector<std::pair<Loop *, Loop *>, 16> LoopsToClone;
  // Build up the loops to clone in reverse order as we'll clone them from the
  // back.
  for (Loop *ChildL : llvm::reverse(OrigRootL))
    LoopsToClone.push_back({ClonedRootL, ChildL});
  do {
    Loop *ClonedParentL, *L;
    std::tie(ClonedParentL, L) = LoopsToClone.pop_back_val();
    Loop *ClonedL = LI.AllocateLoop();
    ClonedParentL->addChildLoop(ClonedL);
    AddClonedBlocksToLoop(*L, *ClonedL);
    for (Loop *ChildL : llvm::reverse(*L))
      LoopsToClone.push_back({ClonedL, ChildL});
  } while (!LoopsToClone.empty());

  return ClonedRootL;
}

/// Build the cloned loops of an original loop from unswitching.
///
/// Because unswitching simplifies the CFG of the loop, this isn't a trivial
/// operation. We need to re-verify that there even is a loop (as the backedge
/// may not have been cloned), and even if there are remaining backedges the
/// backedge set may be different. However, we know that each child loop is
/// undisturbed, we only need to find where to place each child loop within
/// either any parent loop or within a cloned version of the original loop.
///
/// Because child loops may end up cloned outside of any cloned version of the
/// original loop, multiple cloned sibling loops may be created. All of them
/// are returned so that the newly introduced loop nest roots can be
/// identified.
static void buildClonedLoops(Loop &OrigL, ArrayRef<BasicBlock *> ExitBlocks,
                             const ValueToValueMapTy &VMap, LoopInfo &LI,
                             SmallVectorImpl<Loop *> &NonChildClonedLoops) {
  Loop *ClonedL = nullptr;

  auto *OrigPH = OrigL.getLoopPreheader();
  auto *OrigHeader = OrigL.getHeader();

  auto *ClonedPH = cast<BasicBlock>(VMap.lookup(OrigPH));
  auto *ClonedHeader = cast<BasicBlock>(VMap.lookup(OrigHeader));

  // We need to know the loops of the cloned exit blocks to even compute the
  // accurate parent loop. If we only clone exits to some parent of the
  // original parent, we want to clone into that outer loop. We also keep track
  // of the loops that our cloned exit blocks participate in.
  Loop *ParentL = nullptr;
  SmallVector<BasicBlock *, 4> ClonedExitsInLoops;
  SmallDenseMap<BasicBlock *, Loop *, 16> ExitLoopMap;
  ClonedExitsInLoops.reserve(ExitBlocks.size());
  for (auto *ExitBB : ExitBlocks)
    if (auto *ClonedExitBB = cast_or_null<BasicBlock>(VMap.lookup(ExitBB)))
      if (Loop *ExitL = LI.getLoopFor(ExitBB)) {
        ExitLoopMap[ClonedExitBB] = ExitL;
        ClonedExitsInLoops.push_back(ClonedExitBB);
        if (!ParentL || (ParentL != ExitL && ParentL->contains(ExitL)))
          ParentL = ExitL;
      }
  assert((!ParentL || ParentL == OrigL.getParentLoop() ||
          ParentL->contains(OrigL.getParentLoop())) &&
         "The computed parent loop should always contain (or be) the parent of "
         "the original loop.");

  // We build the set of blocks dominated by the cloned header from the set of
  // cloned blocks out of the original loop. While not all of these will
  // necessarily be in the cloned loop, it is enough to establish that they
  // aren't in unreachable cycles, etc.
  SmallSetVector<BasicBlock *, 16> ClonedLoopBlocks;
  for (auto *BB : OrigL.blocks())
    if (auto *ClonedBB = cast_or_null<BasicBlock>(VMap.lookup(BB)))
      ClonedLoopBlocks.insert(ClonedBB);

  // Rebuild the set of blocks that will end up in the cloned loop. We may have
  // skipped cloning some region of this loop which can in turn skip some of
  // the backedges so we have to rebuild the blocks in the loop based on the
  // backedges that remain after cloning.
  SmallVector<BasicBlock *, 16> Worklist;
  SmallPtrSet<BasicBlock *, 16> BlocksInClonedLoop;
  for (auto *Pred : predecessors(ClonedHeader)) {
    // The only possible non-loop header predecessor is the preheader because
    // we know we cloned the loop in simplified form.
    if (Pred == ClonedPH)
      continue;

    // Because the loop was in simplified form, the only non-loop predecessor
    // should be the preheader.
    assert(ClonedLoopBlocks.count(Pred) && "Found a predecessor of the loop "
                                           "header other than the preheader "
                                           "that is not part of the loop!");

    // Insert this block into the loop set and on the first visit (and if it
    // isn't the header we're currently walking) put it into the worklist to
    // recurse through.
    if (BlocksInClonedLoop.insert(Pred).second && Pred != ClonedHeader)
      Worklist.push_back(Pred);
  }

  // If we had any backedges then there *is* a cloned loop. Put the header into
  // the loop set and then walk the worklist backwards to find all the blocks
  // that remain within the loop after cloning.
  if (!BlocksInClonedLoop.empty()) {
    BlocksInClonedLoop.insert(ClonedHeader);

    while (!Worklist.empty()) {
      BasicBlock *BB = Worklist.pop_back_val();
      assert(BlocksInClonedLoop.count(BB) &&
             "Didn't put block into the loop set!");

      // Insert any predecessors that are in the possible set into the cloned
      // set, and if the insert is successful, add them to the worklist. Note
      // that we filter on the blocks that are definitely reachable via the
      // backedge to the loop header so we may prune out dead code within the
      // cloned loop.
      for (auto *Pred : predecessors(BB))
        if (ClonedLoopBlocks.count(Pred) &&
            BlocksInClonedLoop.insert(Pred).second)
          Worklist.push_back(Pred);
    }

    ClonedL = LI.AllocateLoop();
    if (ParentL) {
      ParentL->addBasicBlockToLoop(ClonedPH, LI);
      ParentL->addChildLoop(ClonedL);
    } else {
      LI.addTopLevelLoop(ClonedL);
    }
    NonChildClonedLoops.push_back(ClonedL);

    ClonedL->reserveBlocks(BlocksInClonedLoop.size());
    // We don't want to just add the cloned loop blocks based on how we
    // discovered them. The original order of blocks was carefully built in
    // a way that doesn't rely on predecessor ordering. Rather than re-invent
    // that logic, we just re-walk the original blocks (and those of the child
    // loops) and filter them as we add them into the cloned loop.
    for (auto *BB : OrigL.blocks()) {
      auto *ClonedBB = cast_or_null<BasicBlock>(VMap.lookup(BB));
      if (!ClonedBB || !BlocksInClonedLoop.count(ClonedBB))
        continue;

      // Directly add the blocks that are only in this loop.
      if (LI.getLoopFor(BB) == &OrigL) {
        ClonedL->addBasicBlockToLoop(ClonedBB, LI);
        continue;
      }

      // We want to manually add it to this loop and parents.
      // Registering it with LoopInfo will happen when we clone the top
      // loop for this block.
      for (Loop *PL = ClonedL; PL; PL = PL->getParentLoop())
        PL->addBlockEntry(ClonedBB);
    }

    // Now add each child loop whose header remains within the cloned loop. All
    // of the blocks within the loop must satisfy the same constraints as the
    // header so once we pass the header checks we can just clone the entire
    // child loop nest.
    for (Loop *ChildL : OrigL) {
      auto *ClonedChildHeader =
          cast_or_null<BasicBlock>(VMap.lookup(ChildL->getHeader()));
      if (!ClonedChildHeader || !BlocksInClonedLoop.count(ClonedChildHeader))
        continue;

#ifndef NDEBUG
      // We should never have a cloned child loop header but fail to have
      // all of the blocks for that child loop.
      for (auto *ChildLoopBB : ChildL->blocks())
        assert(BlocksInClonedLoop.count(
                   cast<BasicBlock>(VMap.lookup(ChildLoopBB))) &&
               "Child cloned loop has a header within the cloned outer "
               "loop but not all of its blocks!");
#endif

      cloneLoopNest(*ChildL, ClonedL, VMap, LI);
    }
  }

  // Now that we've handled all the components of the original loop that were
  // cloned into a new loop, we still need to handle anything from the original
  // loop that wasn't in a cloned loop.

  // Figure out what blocks are left to place within any loop nest containing
  // the unswitched loop. If we never formed a loop, the cloned PH is one of
  // them.
  SmallPtrSet<BasicBlock *, 16> UnloopedBlockSet;
  if (BlocksInClonedLoop.empty())
    UnloopedBlockSet.insert(ClonedPH);
  for (auto *ClonedBB : ClonedLoopBlocks)
    if (!BlocksInClonedLoop.count(ClonedBB))
      UnloopedBlockSet.insert(ClonedBB);

  // Copy the cloned exits and sort them in ascending loop depth, we'll work
  // backwards across these to process them inside out. The order shouldn't
  // matter as we're just trying to build up the map from inside-out; we use
  // the map in a more stably ordered way below.
  auto OrderedClonedExitsInLoops = ClonedExitsInLoops;
  llvm::sort(OrderedClonedExitsInLoops, [&](BasicBlock *LHS, BasicBlock *RHS) {
    return ExitLoopMap.lookup(LHS)->getLoopDepth() <
           ExitLoopMap.lookup(RHS)->getLoopDepth();
  });

  // Populate the existing ExitLoopMap with everything reachable from each
  // exit, starting from the inner most exit.
  while (!UnloopedBlockSet.empty() && !OrderedClonedExitsInLoops.empty()) {
    assert(Worklist.empty() && "Didn't clear worklist!");

    BasicBlock *ExitBB = OrderedClonedExitsInLoops.pop_back_val();
    Loop *ExitL = ExitLoopMap.lookup(ExitBB);

    // Walk the CFG back until we hit the cloned PH adding everything reachable
    // and in the unlooped set to this exit block's loop.
    Worklist.push_back(ExitBB);
    do {
      BasicBlock *BB = Worklist.pop_back_val();
      // We can stop recursing at the cloned preheader (if we get there).
      if (BB == ClonedPH)
        continue;

      for (BasicBlock *PredBB : predecessors(BB)) {
        // If this pred has already been moved to our set or is part of some
        // (inner) loop, no update needed.
        if (!UnloopedBlockSet.erase(PredBB)) {
          assert(
              (BlocksInClonedLoop.count(PredBB) || ExitLoopMap.count(PredBB)) &&
              "Predecessor not mapped to a loop!");
          continue;
        }

        // We just insert into the loop set here. We'll add these blocks to the
        // exit loop after we build up the set in an order that doesn't rely on
        // predecessor order (which in turn relies on use list order).
        bool Inserted = ExitLoopMap.insert({PredBB, ExitL}).second;
        (void)Inserted;
        assert(Inserted && "Should only visit an unlooped block once!");

        // And recurse through to its predecessors.
        Worklist.push_back(PredBB);
      }
    } while (!Worklist.empty());
  }

  // Now that the ExitLoopMap gives as  mapping for all the non-looping cloned
  // blocks to their outer loops, walk the cloned blocks and the cloned exits
  // in their original order adding them to the correct loop.

  // We need a stable insertion order. We use the order of the original loop
  // order and map into the correct parent loop.
  for (auto *BB : llvm::concat<BasicBlock *const>(
           makeArrayRef(ClonedPH), ClonedLoopBlocks, ClonedExitsInLoops))
    if (Loop *OuterL = ExitLoopMap.lookup(BB))
      OuterL->addBasicBlockToLoop(BB, LI);

#ifndef NDEBUG
  for (auto &BBAndL : ExitLoopMap) {
    auto *BB = BBAndL.first;
    auto *OuterL = BBAndL.second;
    assert(LI.getLoopFor(BB) == OuterL &&
           "Failed to put all blocks into outer loops!");
  }
#endif

  // Now that all the blocks are placed into the correct containing loop in the
  // absence of child loops, find all the potentially cloned child loops and
  // clone them into whatever outer loop we placed their header into.
  for (Loop *ChildL : OrigL) {
    auto *ClonedChildHeader =
        cast_or_null<BasicBlock>(VMap.lookup(ChildL->getHeader()));
    if (!ClonedChildHeader || BlocksInClonedLoop.count(ClonedChildHeader))
      continue;

#ifndef NDEBUG
    for (auto *ChildLoopBB : ChildL->blocks())
      assert(VMap.count(ChildLoopBB) &&
             "Cloned a child loop header but not all of that loops blocks!");
#endif

    NonChildClonedLoops.push_back(cloneLoopNest(
        *ChildL, ExitLoopMap.lookup(ClonedChildHeader), VMap, LI));
  }
}

static void
deleteDeadClonedBlocks(Loop &L, ArrayRef<BasicBlock *> ExitBlocks,
                       ArrayRef<std::unique_ptr<ValueToValueMapTy>> VMaps,
                       DominatorTree &DT, MemorySSAUpdater *MSSAU) {
  // Find all the dead clones, and remove them from their successors.
  SmallVector<BasicBlock *, 16> DeadBlocks;
  for (BasicBlock *BB : llvm::concat<BasicBlock *const>(L.blocks(), ExitBlocks))
    for (auto &VMap : VMaps)
      if (BasicBlock *ClonedBB = cast_or_null<BasicBlock>(VMap->lookup(BB)))
        if (!DT.isReachableFromEntry(ClonedBB)) {
          for (BasicBlock *SuccBB : successors(ClonedBB))
            SuccBB->removePredecessor(ClonedBB);
          DeadBlocks.push_back(ClonedBB);
        }

  // Remove all MemorySSA in the dead blocks
  if (MSSAU) {
    SmallPtrSet<BasicBlock *, 16> DeadBlockSet(DeadBlocks.begin(),
                                               DeadBlocks.end());
    MSSAU->removeBlocks(DeadBlockSet);
  }

  // Drop any remaining references to break cycles.
  for (BasicBlock *BB : DeadBlocks)
    BB->dropAllReferences();
  // Erase them from the IR.
  for (BasicBlock *BB : DeadBlocks)
    BB->eraseFromParent();
}

static void deleteDeadBlocksFromLoop(Loop &L,
                                     SmallVectorImpl<BasicBlock *> &ExitBlocks,
                                     DominatorTree &DT, LoopInfo &LI,
                                     MemorySSAUpdater *MSSAU) {
  // Find all the dead blocks tied to this loop, and remove them from their
  // successors.
  SmallPtrSet<BasicBlock *, 16> DeadBlockSet;

  // Start with loop/exit blocks and get a transitive closure of reachable dead
  // blocks.
  SmallVector<BasicBlock *, 16> DeathCandidates(ExitBlocks.begin(),
                                                ExitBlocks.end());
  DeathCandidates.append(L.blocks().begin(), L.blocks().end());
  while (!DeathCandidates.empty()) {
    auto *BB = DeathCandidates.pop_back_val();
    if (!DeadBlockSet.count(BB) && !DT.isReachableFromEntry(BB)) {
      for (BasicBlock *SuccBB : successors(BB)) {
        SuccBB->removePredecessor(BB);
        DeathCandidates.push_back(SuccBB);
      }
      DeadBlockSet.insert(BB);
    }
  }

  // Remove all MemorySSA in the dead blocks
  if (MSSAU)
    MSSAU->removeBlocks(DeadBlockSet);

  // Filter out the dead blocks from the exit blocks list so that it can be
  // used in the caller.
  llvm::erase_if(ExitBlocks,
                 [&](BasicBlock *BB) { return DeadBlockSet.count(BB); });

  // Walk from this loop up through its parents removing all of the dead blocks.
  for (Loop *ParentL = &L; ParentL; ParentL = ParentL->getParentLoop()) {
    for (auto *BB : DeadBlockSet)
      ParentL->getBlocksSet().erase(BB);
    llvm::erase_if(ParentL->getBlocksVector(),
                   [&](BasicBlock *BB) { return DeadBlockSet.count(BB); });
  }

  // Now delete the dead child loops. This raw delete will clear them
  // recursively.
  llvm::erase_if(L.getSubLoopsVector(), [&](Loop *ChildL) {
    if (!DeadBlockSet.count(ChildL->getHeader()))
      return false;

    assert(llvm::all_of(ChildL->blocks(),
                        [&](BasicBlock *ChildBB) {
                          return DeadBlockSet.count(ChildBB);
                        }) &&
           "If the child loop header is dead all blocks in the child loop must "
           "be dead as well!");
    LI.destroy(ChildL);
    return true;
  });

  // Remove the loop mappings for the dead blocks and drop all the references
  // from these blocks to others to handle cyclic references as we start
  // deleting the blocks themselves.
  for (auto *BB : DeadBlockSet) {
    // Check that the dominator tree has already been updated.
    assert(!DT.getNode(BB) && "Should already have cleared domtree!");
    LI.changeLoopFor(BB, nullptr);
    BB->dropAllReferences();
  }

  // Actually delete the blocks now that they've been fully unhooked from the
  // IR.
  for (auto *BB : DeadBlockSet)
    BB->eraseFromParent();
}

/// Recompute the set of blocks in a loop after unswitching.
///
/// This walks from the original headers predecessors to rebuild the loop. We
/// take advantage of the fact that new blocks can't have been added, and so we
/// filter by the original loop's blocks. This also handles potentially
/// unreachable code that we don't want to explore but might be found examining
/// the predecessors of the header.
///
/// If the original loop is no longer a loop, this will return an empty set. If
/// it remains a loop, all the blocks within it will be added to the set
/// (including those blocks in inner loops).
static SmallPtrSet<const BasicBlock *, 16> recomputeLoopBlockSet(Loop &L,
                                                                 LoopInfo &LI) {
  SmallPtrSet<const BasicBlock *, 16> LoopBlockSet;

  auto *PH = L.getLoopPreheader();
  auto *Header = L.getHeader();

  // A worklist to use while walking backwards from the header.
  SmallVector<BasicBlock *, 16> Worklist;

  // First walk the predecessors of the header to find the backedges. This will
  // form the basis of our walk.
  for (auto *Pred : predecessors(Header)) {
    // Skip the preheader.
    if (Pred == PH)
      continue;

    // Because the loop was in simplified form, the only non-loop predecessor
    // is the preheader.
    assert(L.contains(Pred) && "Found a predecessor of the loop header other "
                               "than the preheader that is not part of the "
                               "loop!");

    // Insert this block into the loop set and on the first visit and, if it
    // isn't the header we're currently walking, put it into the worklist to
    // recurse through.
    if (LoopBlockSet.insert(Pred).second && Pred != Header)
      Worklist.push_back(Pred);
  }

  // If no backedges were found, we're done.
  if (LoopBlockSet.empty())
    return LoopBlockSet;

  // We found backedges, recurse through them to identify the loop blocks.
  while (!Worklist.empty()) {
    BasicBlock *BB = Worklist.pop_back_val();
    assert(LoopBlockSet.count(BB) && "Didn't put block into the loop set!");

    // No need to walk past the header.
    if (BB == Header)
      continue;

    // Because we know the inner loop structure remains valid we can use the
    // loop structure to jump immediately across the entire nested loop.
    // Further, because it is in loop simplified form, we can directly jump
    // to its preheader afterward.
    if (Loop *InnerL = LI.getLoopFor(BB))
      if (InnerL != &L) {
        assert(L.contains(InnerL) &&
               "Should not reach a loop *outside* this loop!");
        // The preheader is the only possible predecessor of the loop so
        // insert it into the set and check whether it was already handled.
        auto *InnerPH = InnerL->getLoopPreheader();
        assert(L.contains(InnerPH) && "Cannot contain an inner loop block "
                                      "but not contain the inner loop "
                                      "preheader!");
        if (!LoopBlockSet.insert(InnerPH).second)
          // The only way to reach the preheader is through the loop body
          // itself so if it has been visited the loop is already handled.
          continue;

        // Insert all of the blocks (other than those already present) into
        // the loop set. We expect at least the block that led us to find the
        // inner loop to be in the block set, but we may also have other loop
        // blocks if they were already enqueued as predecessors of some other
        // outer loop block.
        for (auto *InnerBB : InnerL->blocks()) {
          if (InnerBB == BB) {
            assert(LoopBlockSet.count(InnerBB) &&
                   "Block should already be in the set!");
            continue;
          }

          LoopBlockSet.insert(InnerBB);
        }

        // Add the preheader to the worklist so we will continue past the
        // loop body.
        Worklist.push_back(InnerPH);
        continue;
      }

    // Insert any predecessors that were in the original loop into the new
    // set, and if the insert is successful, add them to the worklist.
    for (auto *Pred : predecessors(BB))
      if (L.contains(Pred) && LoopBlockSet.insert(Pred).second)
        Worklist.push_back(Pred);
  }

  assert(LoopBlockSet.count(Header) && "Cannot fail to add the header!");

  // We've found all the blocks participating in the loop, return our completed
  // set.
  return LoopBlockSet;
}

/// Rebuild a loop after unswitching removes some subset of blocks and edges.
///
/// The removal may have removed some child loops entirely but cannot have
/// disturbed any remaining child loops. However, they may need to be hoisted
/// to the parent loop (or to be top-level loops). The original loop may be
/// completely removed.
///
/// The sibling loops resulting from this update are returned. If the original
/// loop remains a valid loop, it will be the first entry in this list with all
/// of the newly sibling loops following it.
///
/// Returns true if the loop remains a loop after unswitching, and false if it
/// is no longer a loop after unswitching (and should not continue to be
/// referenced).
static bool rebuildLoopAfterUnswitch(Loop &L, ArrayRef<BasicBlock *> ExitBlocks,
                                     LoopInfo &LI,
                                     SmallVectorImpl<Loop *> &HoistedLoops) {
  auto *PH = L.getLoopPreheader();

  // Compute the actual parent loop from the exit blocks. Because we may have
  // pruned some exits the loop may be different from the original parent.
  Loop *ParentL = nullptr;
  SmallVector<Loop *, 4> ExitLoops;
  SmallVector<BasicBlock *, 4> ExitsInLoops;
  ExitsInLoops.reserve(ExitBlocks.size());
  for (auto *ExitBB : ExitBlocks)
    if (Loop *ExitL = LI.getLoopFor(ExitBB)) {
      ExitLoops.push_back(ExitL);
      ExitsInLoops.push_back(ExitBB);
      if (!ParentL || (ParentL != ExitL && ParentL->contains(ExitL)))
        ParentL = ExitL;
    }

  // Recompute the blocks participating in this loop. This may be empty if it
  // is no longer a loop.
  auto LoopBlockSet = recomputeLoopBlockSet(L, LI);

  // If we still have a loop, we need to re-set the loop's parent as the exit
  // block set changing may have moved it within the loop nest. Note that this
  // can only happen when this loop has a parent as it can only hoist the loop
  // *up* the nest.
  if (!LoopBlockSet.empty() && L.getParentLoop() != ParentL) {
    // Remove this loop's (original) blocks from all of the intervening loops.
    for (Loop *IL = L.getParentLoop(); IL != ParentL;
         IL = IL->getParentLoop()) {
      IL->getBlocksSet().erase(PH);
      for (auto *BB : L.blocks())
        IL->getBlocksSet().erase(BB);
      llvm::erase_if(IL->getBlocksVector(), [&](BasicBlock *BB) {
        return BB == PH || L.contains(BB);
      });
    }

    LI.changeLoopFor(PH, ParentL);
    L.getParentLoop()->removeChildLoop(&L);
    if (ParentL)
      ParentL->addChildLoop(&L);
    else
      LI.addTopLevelLoop(&L);
  }

  // Now we update all the blocks which are no longer within the loop.
  auto &Blocks = L.getBlocksVector();
  auto BlocksSplitI =
      LoopBlockSet.empty()
          ? Blocks.begin()
          : std::stable_partition(
                Blocks.begin(), Blocks.end(),
                [&](BasicBlock *BB) { return LoopBlockSet.count(BB); });

  // Before we erase the list of unlooped blocks, build a set of them.
  SmallPtrSet<BasicBlock *, 16> UnloopedBlocks(BlocksSplitI, Blocks.end());
  if (LoopBlockSet.empty())
    UnloopedBlocks.insert(PH);

  // Now erase these blocks from the loop.
  for (auto *BB : make_range(BlocksSplitI, Blocks.end()))
    L.getBlocksSet().erase(BB);
  Blocks.erase(BlocksSplitI, Blocks.end());

  // Sort the exits in ascending loop depth, we'll work backwards across these
  // to process them inside out.
  std::stable_sort(ExitsInLoops.begin(), ExitsInLoops.end(),
                   [&](BasicBlock *LHS, BasicBlock *RHS) {
                     return LI.getLoopDepth(LHS) < LI.getLoopDepth(RHS);
                   });

  // We'll build up a set for each exit loop.
  SmallPtrSet<BasicBlock *, 16> NewExitLoopBlocks;
  Loop *PrevExitL = L.getParentLoop(); // The deepest possible exit loop.

  auto RemoveUnloopedBlocksFromLoop =
      [](Loop &L, SmallPtrSetImpl<BasicBlock *> &UnloopedBlocks) {
        for (auto *BB : UnloopedBlocks)
          L.getBlocksSet().erase(BB);
        llvm::erase_if(L.getBlocksVector(), [&](BasicBlock *BB) {
          return UnloopedBlocks.count(BB);
        });
      };

  SmallVector<BasicBlock *, 16> Worklist;
  while (!UnloopedBlocks.empty() && !ExitsInLoops.empty()) {
    assert(Worklist.empty() && "Didn't clear worklist!");
    assert(NewExitLoopBlocks.empty() && "Didn't clear loop set!");

    // Grab the next exit block, in decreasing loop depth order.
    BasicBlock *ExitBB = ExitsInLoops.pop_back_val();
    Loop &ExitL = *LI.getLoopFor(ExitBB);
    assert(ExitL.contains(&L) && "Exit loop must contain the inner loop!");

    // Erase all of the unlooped blocks from the loops between the previous
    // exit loop and this exit loop. This works because the ExitInLoops list is
    // sorted in increasing order of loop depth and thus we visit loops in
    // decreasing order of loop depth.
    for (; PrevExitL != &ExitL; PrevExitL = PrevExitL->getParentLoop())
      RemoveUnloopedBlocksFromLoop(*PrevExitL, UnloopedBlocks);

    // Walk the CFG back until we hit the cloned PH adding everything reachable
    // and in the unlooped set to this exit block's loop.
    Worklist.push_back(ExitBB);
    do {
      BasicBlock *BB = Worklist.pop_back_val();
      // We can stop recursing at the cloned preheader (if we get there).
      if (BB == PH)
        continue;

      for (BasicBlock *PredBB : predecessors(BB)) {
        // If this pred has already been moved to our set or is part of some
        // (inner) loop, no update needed.
        if (!UnloopedBlocks.erase(PredBB)) {
          assert((NewExitLoopBlocks.count(PredBB) ||
                  ExitL.contains(LI.getLoopFor(PredBB))) &&
                 "Predecessor not in a nested loop (or already visited)!");
          continue;
        }

        // We just insert into the loop set here. We'll add these blocks to the
        // exit loop after we build up the set in a deterministic order rather
        // than the predecessor-influenced visit order.
        bool Inserted = NewExitLoopBlocks.insert(PredBB).second;
        (void)Inserted;
        assert(Inserted && "Should only visit an unlooped block once!");

        // And recurse through to its predecessors.
        Worklist.push_back(PredBB);
      }
    } while (!Worklist.empty());

    // If blocks in this exit loop were directly part of the original loop (as
    // opposed to a child loop) update the map to point to this exit loop. This
    // just updates a map and so the fact that the order is unstable is fine.
    for (auto *BB : NewExitLoopBlocks)
      if (Loop *BBL = LI.getLoopFor(BB))
        if (BBL == &L || !L.contains(BBL))
          LI.changeLoopFor(BB, &ExitL);

    // We will remove the remaining unlooped blocks from this loop in the next
    // iteration or below.
    NewExitLoopBlocks.clear();
  }

  // Any remaining unlooped blocks are no longer part of any loop unless they
  // are part of some child loop.
  for (; PrevExitL; PrevExitL = PrevExitL->getParentLoop())
    RemoveUnloopedBlocksFromLoop(*PrevExitL, UnloopedBlocks);
  for (auto *BB : UnloopedBlocks)
    if (Loop *BBL = LI.getLoopFor(BB))
      if (BBL == &L || !L.contains(BBL))
        LI.changeLoopFor(BB, nullptr);

  // Sink all the child loops whose headers are no longer in the loop set to
  // the parent (or to be top level loops). We reach into the loop and directly
  // update its subloop vector to make this batch update efficient.
  auto &SubLoops = L.getSubLoopsVector();
  auto SubLoopsSplitI =
      LoopBlockSet.empty()
          ? SubLoops.begin()
          : std::stable_partition(
                SubLoops.begin(), SubLoops.end(), [&](Loop *SubL) {
                  return LoopBlockSet.count(SubL->getHeader());
                });
  for (auto *HoistedL : make_range(SubLoopsSplitI, SubLoops.end())) {
    HoistedLoops.push_back(HoistedL);
    HoistedL->setParentLoop(nullptr);

    // To compute the new parent of this hoisted loop we look at where we
    // placed the preheader above. We can't lookup the header itself because we
    // retained the mapping from the header to the hoisted loop. But the
    // preheader and header should have the exact same new parent computed
    // based on the set of exit blocks from the original loop as the preheader
    // is a predecessor of the header and so reached in the reverse walk. And
    // because the loops were all in simplified form the preheader of the
    // hoisted loop can't be part of some *other* loop.
    if (auto *NewParentL = LI.getLoopFor(HoistedL->getLoopPreheader()))
      NewParentL->addChildLoop(HoistedL);
    else
      LI.addTopLevelLoop(HoistedL);
  }
  SubLoops.erase(SubLoopsSplitI, SubLoops.end());

  // Actually delete the loop if nothing remained within it.
  if (Blocks.empty()) {
    assert(SubLoops.empty() &&
           "Failed to remove all subloops from the original loop!");
    if (Loop *ParentL = L.getParentLoop())
      ParentL->removeChildLoop(llvm::find(*ParentL, &L));
    else
      LI.removeLoop(llvm::find(LI, &L));
    LI.destroy(&L);
    return false;
  }

  return true;
}

/// Helper to visit a dominator subtree, invoking a callable on each node.
///
/// Returning false at any point will stop walking past that node of the tree.
template <typename CallableT>
void visitDomSubTree(DominatorTree &DT, BasicBlock *BB, CallableT Callable) {
  SmallVector<DomTreeNode *, 4> DomWorklist;
  DomWorklist.push_back(DT[BB]);
#ifndef NDEBUG
  SmallPtrSet<DomTreeNode *, 4> Visited;
  Visited.insert(DT[BB]);
#endif
  do {
    DomTreeNode *N = DomWorklist.pop_back_val();

    // Visit this node.
    if (!Callable(N->getBlock()))
      continue;

    // Accumulate the child nodes.
    for (DomTreeNode *ChildN : *N) {
      assert(Visited.insert(ChildN).second &&
             "Cannot visit a node twice when walking a tree!");
      DomWorklist.push_back(ChildN);
    }
  } while (!DomWorklist.empty());
}

static void unswitchNontrivialInvariants(
    Loop &L, Instruction &TI, ArrayRef<Value *> Invariants,
    SmallVectorImpl<BasicBlock *> &ExitBlocks, DominatorTree &DT, LoopInfo &LI,
    AssumptionCache &AC, function_ref<void(bool, ArrayRef<Loop *>)> UnswitchCB,
    ScalarEvolution *SE, MemorySSAUpdater *MSSAU) {
  auto *ParentBB = TI.getParent();
  BranchInst *BI = dyn_cast<BranchInst>(&TI);
  SwitchInst *SI = BI ? nullptr : cast<SwitchInst>(&TI);

  // We can only unswitch switches, conditional branches with an invariant
  // condition, or combining invariant conditions with an instruction.
  assert((SI || BI->isConditional()) &&
         "Can only unswitch switches and conditional branch!");
  bool FullUnswitch = SI || BI->getCondition() == Invariants[0];
  if (FullUnswitch)
    assert(Invariants.size() == 1 &&
           "Cannot have other invariants with full unswitching!");
  else
    assert(isa<Instruction>(BI->getCondition()) &&
           "Partial unswitching requires an instruction as the condition!");

  if (MSSAU && VerifyMemorySSA)
    MSSAU->getMemorySSA()->verifyMemorySSA();

  // Constant and BBs tracking the cloned and continuing successor. When we are
  // unswitching the entire condition, this can just be trivially chosen to
  // unswitch towards `true`. However, when we are unswitching a set of
  // invariants combined with `and` or `or`, the combining operation determines
  // the best direction to unswitch: we want to unswitch the direction that will
  // collapse the branch.
  bool Direction = true;
  int ClonedSucc = 0;
  if (!FullUnswitch) {
    if (cast<Instruction>(BI->getCondition())->getOpcode() != Instruction::Or) {
      assert(cast<Instruction>(BI->getCondition())->getOpcode() ==
                 Instruction::And &&
             "Only `or` and `and` instructions can combine invariants being "
             "unswitched.");
      Direction = false;
      ClonedSucc = 1;
    }
  }

  BasicBlock *RetainedSuccBB =
      BI ? BI->getSuccessor(1 - ClonedSucc) : SI->getDefaultDest();
  SmallSetVector<BasicBlock *, 4> UnswitchedSuccBBs;
  if (BI)
    UnswitchedSuccBBs.insert(BI->getSuccessor(ClonedSucc));
  else
    for (auto Case : SI->cases())
      if (Case.getCaseSuccessor() != RetainedSuccBB)
        UnswitchedSuccBBs.insert(Case.getCaseSuccessor());

  assert(!UnswitchedSuccBBs.count(RetainedSuccBB) &&
         "Should not unswitch the same successor we are retaining!");

  // The branch should be in this exact loop. Any inner loop's invariant branch
  // should be handled by unswitching that inner loop. The caller of this
  // routine should filter out any candidates that remain (but were skipped for
  // whatever reason).
  assert(LI.getLoopFor(ParentBB) == &L && "Branch in an inner loop!");

  // Compute the parent loop now before we start hacking on things.
  Loop *ParentL = L.getParentLoop();
  // Get blocks in RPO order for MSSA update, before changing the CFG.
  LoopBlocksRPO LBRPO(&L);
  if (MSSAU)
    LBRPO.perform(&LI);

  // Compute the outer-most loop containing one of our exit blocks. This is the
  // furthest up our loopnest which can be mutated, which we will use below to
  // update things.
  Loop *OuterExitL = &L;
  for (auto *ExitBB : ExitBlocks) {
    Loop *NewOuterExitL = LI.getLoopFor(ExitBB);
    if (!NewOuterExitL) {
      // We exited the entire nest with this block, so we're done.
      OuterExitL = nullptr;
      break;
    }
    if (NewOuterExitL != OuterExitL && NewOuterExitL->contains(OuterExitL))
      OuterExitL = NewOuterExitL;
  }

  // At this point, we're definitely going to unswitch something so invalidate
  // any cached information in ScalarEvolution for the outer most loop
  // containing an exit block and all nested loops.
  if (SE) {
    if (OuterExitL)
      SE->forgetLoop(OuterExitL);
    else
      SE->forgetTopmostLoop(&L);
  }

  // If the edge from this terminator to a successor dominates that successor,
  // store a map from each block in its dominator subtree to it. This lets us
  // tell when cloning for a particular successor if a block is dominated by
  // some *other* successor with a single data structure. We use this to
  // significantly reduce cloning.
  SmallDenseMap<BasicBlock *, BasicBlock *, 16> DominatingSucc;
  for (auto *SuccBB : llvm::concat<BasicBlock *const>(
           makeArrayRef(RetainedSuccBB), UnswitchedSuccBBs))
    if (SuccBB->getUniquePredecessor() ||
        llvm::all_of(predecessors(SuccBB), [&](BasicBlock *PredBB) {
          return PredBB == ParentBB || DT.dominates(SuccBB, PredBB);
        }))
      visitDomSubTree(DT, SuccBB, [&](BasicBlock *BB) {
        DominatingSucc[BB] = SuccBB;
        return true;
      });

  // Split the preheader, so that we know that there is a safe place to insert
  // the conditional branch. We will change the preheader to have a conditional
  // branch on LoopCond. The original preheader will become the split point
  // between the unswitched versions, and we will have a new preheader for the
  // original loop.
  BasicBlock *SplitBB = L.getLoopPreheader();
  BasicBlock *LoopPH = SplitEdge(SplitBB, L.getHeader(), &DT, &LI, MSSAU);

  // Keep track of the dominator tree updates needed.
  SmallVector<DominatorTree::UpdateType, 4> DTUpdates;

  // Clone the loop for each unswitched successor.
  SmallVector<std::unique_ptr<ValueToValueMapTy>, 4> VMaps;
  VMaps.reserve(UnswitchedSuccBBs.size());
  SmallDenseMap<BasicBlock *, BasicBlock *, 4> ClonedPHs;
  for (auto *SuccBB : UnswitchedSuccBBs) {
    VMaps.emplace_back(new ValueToValueMapTy());
    ClonedPHs[SuccBB] = buildClonedLoopBlocks(
        L, LoopPH, SplitBB, ExitBlocks, ParentBB, SuccBB, RetainedSuccBB,
        DominatingSucc, *VMaps.back(), DTUpdates, AC, DT, LI, MSSAU);
  }

  // The stitching of the branched code back together depends on whether we're
  // doing full unswitching or not with the exception that we always want to
  // nuke the initial terminator placed in the split block.
  SplitBB->getTerminator()->eraseFromParent();
  if (FullUnswitch) {
    // Splice the terminator from the original loop and rewrite its
    // successors.
    SplitBB->getInstList().splice(SplitBB->end(), ParentBB->getInstList(), TI);

    // Keep a clone of the terminator for MSSA updates.
    Instruction *NewTI = TI.clone();
    ParentBB->getInstList().push_back(NewTI);

    // First wire up the moved terminator to the preheaders.
    if (BI) {
      BasicBlock *ClonedPH = ClonedPHs.begin()->second;
      BI->setSuccessor(ClonedSucc, ClonedPH);
      BI->setSuccessor(1 - ClonedSucc, LoopPH);
      DTUpdates.push_back({DominatorTree::Insert, SplitBB, ClonedPH});
    } else {
      assert(SI && "Must either be a branch or switch!");

      // Walk the cases and directly update their successors.
      assert(SI->getDefaultDest() == RetainedSuccBB &&
             "Not retaining default successor!");
      SI->setDefaultDest(LoopPH);
      for (auto &Case : SI->cases())
        if (Case.getCaseSuccessor() == RetainedSuccBB)
          Case.setSuccessor(LoopPH);
        else
          Case.setSuccessor(ClonedPHs.find(Case.getCaseSuccessor())->second);

      // We need to use the set to populate domtree updates as even when there
      // are multiple cases pointing at the same successor we only want to
      // remove and insert one edge in the domtree.
      for (BasicBlock *SuccBB : UnswitchedSuccBBs)
        DTUpdates.push_back(
            {DominatorTree::Insert, SplitBB, ClonedPHs.find(SuccBB)->second});
    }

    if (MSSAU) {
      DT.applyUpdates(DTUpdates);
      DTUpdates.clear();

      // Remove all but one edge to the retained block and all unswitched
      // blocks. This is to avoid having duplicate entries in the cloned Phis,
      // when we know we only keep a single edge for each case.
      MSSAU->removeDuplicatePhiEdgesBetween(ParentBB, RetainedSuccBB);
      for (BasicBlock *SuccBB : UnswitchedSuccBBs)
        MSSAU->removeDuplicatePhiEdgesBetween(ParentBB, SuccBB);

      for (auto &VMap : VMaps)
        MSSAU->updateForClonedLoop(LBRPO, ExitBlocks, *VMap,
                                   /*IgnoreIncomingWithNoClones=*/true);
      MSSAU->updateExitBlocksForClonedLoop(ExitBlocks, VMaps, DT);

      // Remove all edges to unswitched blocks.
      for (BasicBlock *SuccBB : UnswitchedSuccBBs)
        MSSAU->removeEdge(ParentBB, SuccBB);
    }

    // Now unhook the successor relationship as we'll be replacing
    // the terminator with a direct branch. This is much simpler for branches
    // than switches so we handle those first.
    if (BI) {
      // Remove the parent as a predecessor of the unswitched successor.
      assert(UnswitchedSuccBBs.size() == 1 &&
             "Only one possible unswitched block for a branch!");
      BasicBlock *UnswitchedSuccBB = *UnswitchedSuccBBs.begin();
      UnswitchedSuccBB->removePredecessor(ParentBB,
                                          /*DontDeleteUselessPHIs*/ true);
      DTUpdates.push_back({DominatorTree::Delete, ParentBB, UnswitchedSuccBB});
    } else {
      // Note that we actually want to remove the parent block as a predecessor
      // of *every* case successor. The case successor is either unswitched,
      // completely eliminating an edge from the parent to that successor, or it
      // is a duplicate edge to the retained successor as the retained successor
      // is always the default successor and as we'll replace this with a direct
      // branch we no longer need the duplicate entries in the PHI nodes.
      SwitchInst *NewSI = cast<SwitchInst>(NewTI);
      assert(NewSI->getDefaultDest() == RetainedSuccBB &&
             "Not retaining default successor!");
      for (auto &Case : NewSI->cases())
        Case.getCaseSuccessor()->removePredecessor(
            ParentBB,
            /*DontDeleteUselessPHIs*/ true);

      // We need to use the set to populate domtree updates as even when there
      // are multiple cases pointing at the same successor we only want to
      // remove and insert one edge in the domtree.
      for (BasicBlock *SuccBB : UnswitchedSuccBBs)
        DTUpdates.push_back({DominatorTree::Delete, ParentBB, SuccBB});
    }

    // After MSSAU update, remove the cloned terminator instruction NewTI.
    ParentBB->getTerminator()->eraseFromParent();

    // Create a new unconditional branch to the continuing block (as opposed to
    // the one cloned).
    BranchInst::Create(RetainedSuccBB, ParentBB);
  } else {
    assert(BI && "Only branches have partial unswitching.");
    assert(UnswitchedSuccBBs.size() == 1 &&
           "Only one possible unswitched block for a branch!");
    BasicBlock *ClonedPH = ClonedPHs.begin()->second;
    // When doing a partial unswitch, we have to do a bit more work to build up
    // the branch in the split block.
    buildPartialUnswitchConditionalBranch(*SplitBB, Invariants, Direction,
                                          *ClonedPH, *LoopPH);
    DTUpdates.push_back({DominatorTree::Insert, SplitBB, ClonedPH});
  }

  // Apply the updates accumulated above to get an up-to-date dominator tree.
  DT.applyUpdates(DTUpdates);
  if (!FullUnswitch && MSSAU) {
    // Update MSSA for partial unswitch, after DT update.
    SmallVector<CFGUpdate, 1> Updates;
    Updates.push_back(
        {cfg::UpdateKind::Insert, SplitBB, ClonedPHs.begin()->second});
    MSSAU->applyInsertUpdates(Updates, DT);
  }

  // Now that we have an accurate dominator tree, first delete the dead cloned
  // blocks so that we can accurately build any cloned loops. It is important to
  // not delete the blocks from the original loop yet because we still want to
  // reference the original loop to understand the cloned loop's structure.
  deleteDeadClonedBlocks(L, ExitBlocks, VMaps, DT, MSSAU);

  // Build the cloned loop structure itself. This may be substantially
  // different from the original structure due to the simplified CFG. This also
  // handles inserting all the cloned blocks into the correct loops.
  SmallVector<Loop *, 4> NonChildClonedLoops;
  for (std::unique_ptr<ValueToValueMapTy> &VMap : VMaps)
    buildClonedLoops(L, ExitBlocks, *VMap, LI, NonChildClonedLoops);

  // Now that our cloned loops have been built, we can update the original loop.
  // First we delete the dead blocks from it and then we rebuild the loop
  // structure taking these deletions into account.
  deleteDeadBlocksFromLoop(L, ExitBlocks, DT, LI, MSSAU);

  if (MSSAU && VerifyMemorySSA)
    MSSAU->getMemorySSA()->verifyMemorySSA();

  SmallVector<Loop *, 4> HoistedLoops;
  bool IsStillLoop = rebuildLoopAfterUnswitch(L, ExitBlocks, LI, HoistedLoops);

  if (MSSAU && VerifyMemorySSA)
    MSSAU->getMemorySSA()->verifyMemorySSA();

  // This transformation has a high risk of corrupting the dominator tree, and
  // the below steps to rebuild loop structures will result in hard to debug
  // errors in that case so verify that the dominator tree is sane first.
  // FIXME: Remove this when the bugs stop showing up and rely on existing
  // verification steps.
  assert(DT.verify(DominatorTree::VerificationLevel::Fast));

  if (BI) {
    // If we unswitched a branch which collapses the condition to a known
    // constant we want to replace all the uses of the invariants within both
    // the original and cloned blocks. We do this here so that we can use the
    // now updated dominator tree to identify which side the users are on.
    assert(UnswitchedSuccBBs.size() == 1 &&
           "Only one possible unswitched block for a branch!");
    BasicBlock *ClonedPH = ClonedPHs.begin()->second;

    // When considering multiple partially-unswitched invariants
    // we cant just go replace them with constants in both branches.
    //
    // For 'AND' we infer that true branch ("continue") means true
    // for each invariant operand.
    // For 'OR' we can infer that false branch ("continue") means false
    // for each invariant operand.
    // So it happens that for multiple-partial case we dont replace
    // in the unswitched branch.
    bool ReplaceUnswitched = FullUnswitch || (Invariants.size() == 1);

    ConstantInt *UnswitchedReplacement =
        Direction ? ConstantInt::getTrue(BI->getContext())
                  : ConstantInt::getFalse(BI->getContext());
    ConstantInt *ContinueReplacement =
        Direction ? ConstantInt::getFalse(BI->getContext())
                  : ConstantInt::getTrue(BI->getContext());
    for (Value *Invariant : Invariants)
      for (auto UI = Invariant->use_begin(), UE = Invariant->use_end();
           UI != UE;) {
        // Grab the use and walk past it so we can clobber it in the use list.
        Use *U = &*UI++;
        Instruction *UserI = dyn_cast<Instruction>(U->getUser());
        if (!UserI)
          continue;

        // Replace it with the 'continue' side if in the main loop body, and the
        // unswitched if in the cloned blocks.
        if (DT.dominates(LoopPH, UserI->getParent()))
          U->set(ContinueReplacement);
        else if (ReplaceUnswitched &&
                 DT.dominates(ClonedPH, UserI->getParent()))
          U->set(UnswitchedReplacement);
      }
  }

  // We can change which blocks are exit blocks of all the cloned sibling
  // loops, the current loop, and any parent loops which shared exit blocks
  // with the current loop. As a consequence, we need to re-form LCSSA for
  // them. But we shouldn't need to re-form LCSSA for any child loops.
  // FIXME: This could be made more efficient by tracking which exit blocks are
  // new, and focusing on them, but that isn't likely to be necessary.
  //
  // In order to reasonably rebuild LCSSA we need to walk inside-out across the
  // loop nest and update every loop that could have had its exits changed. We
  // also need to cover any intervening loops. We add all of these loops to
  // a list and sort them by loop depth to achieve this without updating
  // unnecessary loops.
  auto UpdateLoop = [&](Loop &UpdateL) {
#ifndef NDEBUG
    UpdateL.verifyLoop();
    for (Loop *ChildL : UpdateL) {
      ChildL->verifyLoop();
      assert(ChildL->isRecursivelyLCSSAForm(DT, LI) &&
             "Perturbed a child loop's LCSSA form!");
    }
#endif
    // First build LCSSA for this loop so that we can preserve it when
    // forming dedicated exits. We don't want to perturb some other loop's
    // LCSSA while doing that CFG edit.
    formLCSSA(UpdateL, DT, &LI, nullptr);

    // For loops reached by this loop's original exit blocks we may
    // introduced new, non-dedicated exits. At least try to re-form dedicated
    // exits for these loops. This may fail if they couldn't have dedicated
    // exits to start with.
    formDedicatedExitBlocks(&UpdateL, &DT, &LI, /*PreserveLCSSA*/ true);
  };

  // For non-child cloned loops and hoisted loops, we just need to update LCSSA
  // and we can do it in any order as they don't nest relative to each other.
  //
  // Also check if any of the loops we have updated have become top-level loops
  // as that will necessitate widening the outer loop scope.
  for (Loop *UpdatedL :
       llvm::concat<Loop *>(NonChildClonedLoops, HoistedLoops)) {
    UpdateLoop(*UpdatedL);
    if (!UpdatedL->getParentLoop())
      OuterExitL = nullptr;
  }
  if (IsStillLoop) {
    UpdateLoop(L);
    if (!L.getParentLoop())
      OuterExitL = nullptr;
  }

  // If the original loop had exit blocks, walk up through the outer most loop
  // of those exit blocks to update LCSSA and form updated dedicated exits.
  if (OuterExitL != &L)
    for (Loop *OuterL = ParentL; OuterL != OuterExitL;
         OuterL = OuterL->getParentLoop())
      UpdateLoop(*OuterL);

#ifndef NDEBUG
  // Verify the entire loop structure to catch any incorrect updates before we
  // progress in the pass pipeline.
  LI.verify(DT);
#endif

  // Now that we've unswitched something, make callbacks to report the changes.
  // For that we need to merge together the updated loops and the cloned loops
  // and check whether the original loop survived.
  SmallVector<Loop *, 4> SibLoops;
  for (Loop *UpdatedL : llvm::concat<Loop *>(NonChildClonedLoops, HoistedLoops))
    if (UpdatedL->getParentLoop() == ParentL)
      SibLoops.push_back(UpdatedL);
  UnswitchCB(IsStillLoop, SibLoops);

  if (MSSAU && VerifyMemorySSA)
    MSSAU->getMemorySSA()->verifyMemorySSA();

  if (BI)
    ++NumBranches;
  else
    ++NumSwitches;
}

/// Recursively compute the cost of a dominator subtree based on the per-block
/// cost map provided.
///
/// The recursive computation is memozied into the provided DT-indexed cost map
/// to allow querying it for most nodes in the domtree without it becoming
/// quadratic.
static int
computeDomSubtreeCost(DomTreeNode &N,
                      const SmallDenseMap<BasicBlock *, int, 4> &BBCostMap,
                      SmallDenseMap<DomTreeNode *, int, 4> &DTCostMap) {
  // Don't accumulate cost (or recurse through) blocks not in our block cost
  // map and thus not part of the duplication cost being considered.
  auto BBCostIt = BBCostMap.find(N.getBlock());
  if (BBCostIt == BBCostMap.end())
    return 0;

  // Lookup this node to see if we already computed its cost.
  auto DTCostIt = DTCostMap.find(&N);
  if (DTCostIt != DTCostMap.end())
    return DTCostIt->second;

  // If not, we have to compute it. We can't use insert above and update
  // because computing the cost may insert more things into the map.
  int Cost = std::accumulate(
      N.begin(), N.end(), BBCostIt->second, [&](int Sum, DomTreeNode *ChildN) {
        return Sum + computeDomSubtreeCost(*ChildN, BBCostMap, DTCostMap);
      });
  bool Inserted = DTCostMap.insert({&N, Cost}).second;
  (void)Inserted;
  assert(Inserted && "Should not insert a node while visiting children!");
  return Cost;
}

/// Turns a llvm.experimental.guard intrinsic into implicit control flow branch,
/// making the following replacement:
///
///   --code before guard--
///   call void (i1, ...) @llvm.experimental.guard(i1 %cond) [ "deopt"() ]
///   --code after guard--
///
/// into
///
///   --code before guard--
///   br i1 %cond, label %guarded, label %deopt
///
/// guarded:
///   --code after guard--
///
/// deopt:
///   call void (i1, ...) @llvm.experimental.guard(i1 false) [ "deopt"() ]
///   unreachable
///
/// It also makes all relevant DT and LI updates, so that all structures are in
/// valid state after this transform.
static BranchInst *
turnGuardIntoBranch(IntrinsicInst *GI, Loop &L,
                    SmallVectorImpl<BasicBlock *> &ExitBlocks,
                    DominatorTree &DT, LoopInfo &LI, MemorySSAUpdater *MSSAU) {
  SmallVector<DominatorTree::UpdateType, 4> DTUpdates;
  LLVM_DEBUG(dbgs() << "Turning " << *GI << " into a branch.\n");
  BasicBlock *CheckBB = GI->getParent();

  if (MSSAU && VerifyMemorySSA)
     MSSAU->getMemorySSA()->verifyMemorySSA();

  // Remove all CheckBB's successors from DomTree. A block can be seen among
  // successors more than once, but for DomTree it should be added only once.
  SmallPtrSet<BasicBlock *, 4> Successors;
  for (auto *Succ : successors(CheckBB))
    if (Successors.insert(Succ).second)
      DTUpdates.push_back({DominatorTree::Delete, CheckBB, Succ});

  Instruction *DeoptBlockTerm =
      SplitBlockAndInsertIfThen(GI->getArgOperand(0), GI, true);
  BranchInst *CheckBI = cast<BranchInst>(CheckBB->getTerminator());
  // SplitBlockAndInsertIfThen inserts control flow that branches to
  // DeoptBlockTerm if the condition is true.  We want the opposite.
  CheckBI->swapSuccessors();

  BasicBlock *GuardedBlock = CheckBI->getSuccessor(0);
  GuardedBlock->setName("guarded");
  CheckBI->getSuccessor(1)->setName("deopt");
  BasicBlock *DeoptBlock = CheckBI->getSuccessor(1);

  // We now have a new exit block.
  ExitBlocks.push_back(CheckBI->getSuccessor(1));

  if (MSSAU)
    MSSAU->moveAllAfterSpliceBlocks(CheckBB, GuardedBlock, GI);

  GI->moveBefore(DeoptBlockTerm);
  GI->setArgOperand(0, ConstantInt::getFalse(GI->getContext()));

  // Add new successors of CheckBB into DomTree.
  for (auto *Succ : successors(CheckBB))
    DTUpdates.push_back({DominatorTree::Insert, CheckBB, Succ});

  // Now the blocks that used to be CheckBB's successors are GuardedBlock's
  // successors.
  for (auto *Succ : Successors)
    DTUpdates.push_back({DominatorTree::Insert, GuardedBlock, Succ});

  // Make proper changes to DT.
  DT.applyUpdates(DTUpdates);
  // Inform LI of a new loop block.
  L.addBasicBlockToLoop(GuardedBlock, LI);

  if (MSSAU) {
    MemoryDef *MD = cast<MemoryDef>(MSSAU->getMemorySSA()->getMemoryAccess(GI));
    MSSAU->moveToPlace(MD, DeoptBlock, MemorySSA::End);
    if (VerifyMemorySSA)
      MSSAU->getMemorySSA()->verifyMemorySSA();
  }

  ++NumGuards;
  return CheckBI;
}

/// Cost multiplier is a way to limit potentially exponential behavior
/// of loop-unswitch. Cost is multipied in proportion of 2^number of unswitch
/// candidates available. Also accounting for the number of "sibling" loops with
/// the idea to account for previous unswitches that already happened on this
/// cluster of loops. There was an attempt to keep this formula simple,
/// just enough to limit the worst case behavior. Even if it is not that simple
/// now it is still not an attempt to provide a detailed heuristic size
/// prediction.
///
/// TODO: Make a proper accounting of "explosion" effect for all kinds of
/// unswitch candidates, making adequate predictions instead of wild guesses.
/// That requires knowing not just the number of "remaining" candidates but
/// also costs of unswitching for each of these candidates.
static int calculateUnswitchCostMultiplier(
    Instruction &TI, Loop &L, LoopInfo &LI, DominatorTree &DT,
    ArrayRef<std::pair<Instruction *, TinyPtrVector<Value *>>>
        UnswitchCandidates) {

  // Guards and other exiting conditions do not contribute to exponential
  // explosion as soon as they dominate the latch (otherwise there might be
  // another path to the latch remaining that does not allow to eliminate the
  // loop copy on unswitch).
  BasicBlock *Latch = L.getLoopLatch();
  BasicBlock *CondBlock = TI.getParent();
  if (DT.dominates(CondBlock, Latch) &&
      (isGuard(&TI) ||
       llvm::count_if(successors(&TI), [&L](BasicBlock *SuccBB) {
         return L.contains(SuccBB);
       }) <= 1)) {
    NumCostMultiplierSkipped++;
    return 1;
  }

  auto *ParentL = L.getParentLoop();
  int SiblingsCount = (ParentL ? ParentL->getSubLoopsVector().size()
                               : std::distance(LI.begin(), LI.end()));
  // Count amount of clones that all the candidates might cause during
  // unswitching. Branch/guard counts as 1, switch counts as log2 of its cases.
  int UnswitchedClones = 0;
  for (auto Candidate : UnswitchCandidates) {
    Instruction *CI = Candidate.first;
    BasicBlock *CondBlock = CI->getParent();
    bool SkipExitingSuccessors = DT.dominates(CondBlock, Latch);
    if (isGuard(CI)) {
      if (!SkipExitingSuccessors)
        UnswitchedClones++;
      continue;
    }
    int NonExitingSuccessors = llvm::count_if(
        successors(CondBlock), [SkipExitingSuccessors, &L](BasicBlock *SuccBB) {
          return !SkipExitingSuccessors || L.contains(SuccBB);
        });
    UnswitchedClones += Log2_32(NonExitingSuccessors);
  }

  // Ignore up to the "unscaled candidates" number of unswitch candidates
  // when calculating the power-of-two scaling of the cost. The main idea
  // with this control is to allow a small number of unswitches to happen
  // and rely more on siblings multiplier (see below) when the number
  // of candidates is small.
  unsigned ClonesPower =
      std::max(UnswitchedClones - (int)UnswitchNumInitialUnscaledCandidates, 0);

  // Allowing top-level loops to spread a bit more than nested ones.
  int SiblingsMultiplier =
      std::max((ParentL ? SiblingsCount
                        : SiblingsCount / (int)UnswitchSiblingsToplevelDiv),
               1);
  // Compute the cost multiplier in a way that won't overflow by saturating
  // at an upper bound.
  int CostMultiplier;
  if (ClonesPower > Log2_32(UnswitchThreshold) ||
      SiblingsMultiplier > UnswitchThreshold)
    CostMultiplier = UnswitchThreshold;
  else
    CostMultiplier = std::min(SiblingsMultiplier * (1 << ClonesPower),
                              (int)UnswitchThreshold);

  LLVM_DEBUG(dbgs() << "  Computed multiplier  " << CostMultiplier
                    << " (siblings " << SiblingsMultiplier << " * clones "
                    << (1 << ClonesPower) << ")"
                    << " for unswitch candidate: " << TI << "\n");
  return CostMultiplier;
}

static bool
unswitchBestCondition(Loop &L, DominatorTree &DT, LoopInfo &LI,
                      AssumptionCache &AC, TargetTransformInfo &TTI,
                      function_ref<void(bool, ArrayRef<Loop *>)> UnswitchCB,
                      ScalarEvolution *SE, MemorySSAUpdater *MSSAU) {
  // Collect all invariant conditions within this loop (as opposed to an inner
  // loop which would be handled when visiting that inner loop).
  SmallVector<std::pair<Instruction *, TinyPtrVector<Value *>>, 4>
      UnswitchCandidates;

  // Whether or not we should also collect guards in the loop.
  bool CollectGuards = false;
  if (UnswitchGuards) {
    auto *GuardDecl = L.getHeader()->getParent()->getParent()->getFunction(
        Intrinsic::getName(Intrinsic::experimental_guard));
    if (GuardDecl && !GuardDecl->use_empty())
      CollectGuards = true;
  }

  for (auto *BB : L.blocks()) {
    if (LI.getLoopFor(BB) != &L)
      continue;

    if (CollectGuards)
      for (auto &I : *BB)
        if (isGuard(&I)) {
          auto *Cond = cast<IntrinsicInst>(&I)->getArgOperand(0);
          // TODO: Support AND, OR conditions and partial unswitching.
          if (!isa<Constant>(Cond) && L.isLoopInvariant(Cond))
            UnswitchCandidates.push_back({&I, {Cond}});
        }

    if (auto *SI = dyn_cast<SwitchInst>(BB->getTerminator())) {
      // We can only consider fully loop-invariant switch conditions as we need
      // to completely eliminate the switch after unswitching.
      if (!isa<Constant>(SI->getCondition()) &&
          L.isLoopInvariant(SI->getCondition()))
        UnswitchCandidates.push_back({SI, {SI->getCondition()}});
      continue;
    }

    auto *BI = dyn_cast<BranchInst>(BB->getTerminator());
    if (!BI || !BI->isConditional() || isa<Constant>(BI->getCondition()) ||
        BI->getSuccessor(0) == BI->getSuccessor(1))
      continue;

    if (L.isLoopInvariant(BI->getCondition())) {
      UnswitchCandidates.push_back({BI, {BI->getCondition()}});
      continue;
    }

    Instruction &CondI = *cast<Instruction>(BI->getCondition());
    if (CondI.getOpcode() != Instruction::And &&
      CondI.getOpcode() != Instruction::Or)
      continue;

    TinyPtrVector<Value *> Invariants =
        collectHomogenousInstGraphLoopInvariants(L, CondI, LI);
    if (Invariants.empty())
      continue;

    UnswitchCandidates.push_back({BI, std::move(Invariants)});
  }

  // If we didn't find any candidates, we're done.
  if (UnswitchCandidates.empty())
    return false;

  // Check if there are irreducible CFG cycles in this loop. If so, we cannot
  // easily unswitch non-trivial edges out of the loop. Doing so might turn the
  // irreducible control flow into reducible control flow and introduce new
  // loops "out of thin air". If we ever discover important use cases for doing
  // this, we can add support to loop unswitch, but it is a lot of complexity
  // for what seems little or no real world benefit.
  LoopBlocksRPO RPOT(&L);
  RPOT.perform(&LI);
  if (containsIrreducibleCFG<const BasicBlock *>(RPOT, LI))
    return false;

  SmallVector<BasicBlock *, 4> ExitBlocks;
  L.getUniqueExitBlocks(ExitBlocks);

  // We cannot unswitch if exit blocks contain a cleanuppad instruction as we
  // don't know how to split those exit blocks.
  // FIXME: We should teach SplitBlock to handle this and remove this
  // restriction.
  for (auto *ExitBB : ExitBlocks)
    if (isa<CleanupPadInst>(ExitBB->getFirstNonPHI())) {
      dbgs() << "Cannot unswitch because of cleanuppad in exit block\n";
      return false;
    }

  LLVM_DEBUG(
      dbgs() << "Considering " << UnswitchCandidates.size()
             << " non-trivial loop invariant conditions for unswitching.\n");

  // Given that unswitching these terminators will require duplicating parts of
  // the loop, so we need to be able to model that cost. Compute the ephemeral
  // values and set up a data structure to hold per-BB costs. We cache each
  // block's cost so that we don't recompute this when considering different
  // subsets of the loop for duplication during unswitching.
  SmallPtrSet<const Value *, 4> EphValues;
  CodeMetrics::collectEphemeralValues(&L, &AC, EphValues);
  SmallDenseMap<BasicBlock *, int, 4> BBCostMap;

  // Compute the cost of each block, as well as the total loop cost. Also, bail
  // out if we see instructions which are incompatible with loop unswitching
  // (convergent, noduplicate, or cross-basic-block tokens).
  // FIXME: We might be able to safely handle some of these in non-duplicated
  // regions.
  int LoopCost = 0;
  for (auto *BB : L.blocks()) {
    int Cost = 0;
    for (auto &I : *BB) {
      if (EphValues.count(&I))
        continue;

      if (I.getType()->isTokenTy() && I.isUsedOutsideOfBlock(BB))
        return false;
      if (auto CS = CallSite(&I))
        if (CS.isConvergent() || CS.cannotDuplicate())
          return false;

      Cost += TTI.getUserCost(&I);
    }
    assert(Cost >= 0 && "Must not have negative costs!");
    LoopCost += Cost;
    assert(LoopCost >= 0 && "Must not have negative loop costs!");
    BBCostMap[BB] = Cost;
  }
  LLVM_DEBUG(dbgs() << "  Total loop cost: " << LoopCost << "\n");

  // Now we find the best candidate by searching for the one with the following
  // properties in order:
  //
  // 1) An unswitching cost below the threshold
  // 2) The smallest number of duplicated unswitch candidates (to avoid
  //    creating redundant subsequent unswitching)
  // 3) The smallest cost after unswitching.
  //
  // We prioritize reducing fanout of unswitch candidates provided the cost
  // remains below the threshold because this has a multiplicative effect.
  //
  // This requires memoizing each dominator subtree to avoid redundant work.
  //
  // FIXME: Need to actually do the number of candidates part above.
  SmallDenseMap<DomTreeNode *, int, 4> DTCostMap;
  // Given a terminator which might be unswitched, computes the non-duplicated
  // cost for that terminator.
  auto ComputeUnswitchedCost = [&](Instruction &TI, bool FullUnswitch) {
    BasicBlock &BB = *TI.getParent();
    SmallPtrSet<BasicBlock *, 4> Visited;

    int Cost = LoopCost;
    for (BasicBlock *SuccBB : successors(&BB)) {
      // Don't count successors more than once.
      if (!Visited.insert(SuccBB).second)
        continue;

      // If this is a partial unswitch candidate, then it must be a conditional
      // branch with a condition of either `or` or `and`. In that case, one of
      // the successors is necessarily duplicated, so don't even try to remove
      // its cost.
      if (!FullUnswitch) {
        auto &BI = cast<BranchInst>(TI);
        if (cast<Instruction>(BI.getCondition())->getOpcode() ==
            Instruction::And) {
          if (SuccBB == BI.getSuccessor(1))
            continue;
        } else {
          assert(cast<Instruction>(BI.getCondition())->getOpcode() ==
                     Instruction::Or &&
                 "Only `and` and `or` conditions can result in a partial "
                 "unswitch!");
          if (SuccBB == BI.getSuccessor(0))
            continue;
        }
      }

      // This successor's domtree will not need to be duplicated after
      // unswitching if the edge to the successor dominates it (and thus the
      // entire tree). This essentially means there is no other path into this
      // subtree and so it will end up live in only one clone of the loop.
      if (SuccBB->getUniquePredecessor() ||
          llvm::all_of(predecessors(SuccBB), [&](BasicBlock *PredBB) {
            return PredBB == &BB || DT.dominates(SuccBB, PredBB);
          })) {
        Cost -= computeDomSubtreeCost(*DT[SuccBB], BBCostMap, DTCostMap);
        assert(Cost >= 0 &&
               "Non-duplicated cost should never exceed total loop cost!");
      }
    }

    // Now scale the cost by the number of unique successors minus one. We
    // subtract one because there is already at least one copy of the entire
    // loop. This is computing the new cost of unswitching a condition.
    // Note that guards always have 2 unique successors that are implicit and
    // will be materialized if we decide to unswitch it.
    int SuccessorsCount = isGuard(&TI) ? 2 : Visited.size();
    assert(SuccessorsCount > 1 &&
           "Cannot unswitch a condition without multiple distinct successors!");
    return Cost * (SuccessorsCount - 1);
  };
  Instruction *BestUnswitchTI = nullptr;
  int BestUnswitchCost;
  ArrayRef<Value *> BestUnswitchInvariants;
  for (auto &TerminatorAndInvariants : UnswitchCandidates) {
    Instruction &TI = *TerminatorAndInvariants.first;
    ArrayRef<Value *> Invariants = TerminatorAndInvariants.second;
    BranchInst *BI = dyn_cast<BranchInst>(&TI);
    int CandidateCost = ComputeUnswitchedCost(
        TI, /*FullUnswitch*/ !BI || (Invariants.size() == 1 &&
                                     Invariants[0] == BI->getCondition()));
    // Calculate cost multiplier which is a tool to limit potentially
    // exponential behavior of loop-unswitch.
    if (EnableUnswitchCostMultiplier) {
      int CostMultiplier =
          calculateUnswitchCostMultiplier(TI, L, LI, DT, UnswitchCandidates);
      assert(
          (CostMultiplier > 0 && CostMultiplier <= UnswitchThreshold) &&
          "cost multiplier needs to be in the range of 1..UnswitchThreshold");
      CandidateCost *= CostMultiplier;
      LLVM_DEBUG(dbgs() << "  Computed cost of " << CandidateCost
                        << " (multiplier: " << CostMultiplier << ")"
                        << " for unswitch candidate: " << TI << "\n");
    } else {
      LLVM_DEBUG(dbgs() << "  Computed cost of " << CandidateCost
                        << " for unswitch candidate: " << TI << "\n");
    }

    if (!BestUnswitchTI || CandidateCost < BestUnswitchCost) {
      BestUnswitchTI = &TI;
      BestUnswitchCost = CandidateCost;
      BestUnswitchInvariants = Invariants;
    }
  }

  if (BestUnswitchCost >= UnswitchThreshold) {
    LLVM_DEBUG(dbgs() << "Cannot unswitch, lowest cost found: "
                      << BestUnswitchCost << "\n");
    return false;
  }

  // If the best candidate is a guard, turn it into a branch.
  if (isGuard(BestUnswitchTI))
    BestUnswitchTI = turnGuardIntoBranch(cast<IntrinsicInst>(BestUnswitchTI), L,
                                         ExitBlocks, DT, LI, MSSAU);

  LLVM_DEBUG(dbgs() << "  Unswitching non-trivial (cost = "
                    << BestUnswitchCost << ") terminator: " << *BestUnswitchTI
                    << "\n");
  unswitchNontrivialInvariants(L, *BestUnswitchTI, BestUnswitchInvariants,
                               ExitBlocks, DT, LI, AC, UnswitchCB, SE, MSSAU);
  return true;
}

/// Unswitch control flow predicated on loop invariant conditions.
///
/// This first hoists all branches or switches which are trivial (IE, do not
/// require duplicating any part of the loop) out of the loop body. It then
/// looks at other loop invariant control flows and tries to unswitch those as
/// well by cloning the loop if the result is small enough.
///
/// The `DT`, `LI`, `AC`, `TTI` parameters are required analyses that are also
/// updated based on the unswitch.
/// The `MSSA` analysis is also updated if valid (i.e. its use is enabled).
///
/// If either `NonTrivial` is true or the flag `EnableNonTrivialUnswitch` is
/// true, we will attempt to do non-trivial unswitching as well as trivial
/// unswitching.
///
/// The `UnswitchCB` callback provided will be run after unswitching is
/// complete, with the first parameter set to `true` if the provided loop
/// remains a loop, and a list of new sibling loops created.
///
/// If `SE` is non-null, we will update that analysis based on the unswitching
/// done.
static bool unswitchLoop(Loop &L, DominatorTree &DT, LoopInfo &LI,
                         AssumptionCache &AC, TargetTransformInfo &TTI,
                         bool NonTrivial,
                         function_ref<void(bool, ArrayRef<Loop *>)> UnswitchCB,
                         ScalarEvolution *SE, MemorySSAUpdater *MSSAU) {
  assert(L.isRecursivelyLCSSAForm(DT, LI) &&
         "Loops must be in LCSSA form before unswitching.");
  bool Changed = false;

  // Must be in loop simplified form: we need a preheader and dedicated exits.
  if (!L.isLoopSimplifyForm())
    return false;

  // Try trivial unswitch first before loop over other basic blocks in the loop.
  if (unswitchAllTrivialConditions(L, DT, LI, SE, MSSAU)) {
    // If we unswitched successfully we will want to clean up the loop before
    // processing it further so just mark it as unswitched and return.
    UnswitchCB(/*CurrentLoopValid*/ true, {});
    return true;
  }

  // If we're not doing non-trivial unswitching, we're done. We both accept
  // a parameter but also check a local flag that can be used for testing
  // a debugging.
  if (!NonTrivial && !EnableNonTrivialUnswitch)
    return false;

  // For non-trivial unswitching, because it often creates new loops, we rely on
  // the pass manager to iterate on the loops rather than trying to immediately
  // reach a fixed point. There is no substantial advantage to iterating
  // internally, and if any of the new loops are simplified enough to contain
  // trivial unswitching we want to prefer those.

  // Try to unswitch the best invariant condition. We prefer this full unswitch to
  // a partial unswitch when possible below the threshold.
  if (unswitchBestCondition(L, DT, LI, AC, TTI, UnswitchCB, SE, MSSAU))
    return true;

  // No other opportunities to unswitch.
  return Changed;
}

PreservedAnalyses SimpleLoopUnswitchPass::run(Loop &L, LoopAnalysisManager &AM,
                                              LoopStandardAnalysisResults &AR,
                                              LPMUpdater &U) {
  Function &F = *L.getHeader()->getParent();
  (void)F;

  LLVM_DEBUG(dbgs() << "Unswitching loop in " << F.getName() << ": " << L
                    << "\n");

  // Save the current loop name in a variable so that we can report it even
  // after it has been deleted.
  std::string LoopName = L.getName();

  auto UnswitchCB = [&L, &U, &LoopName](bool CurrentLoopValid,
                                        ArrayRef<Loop *> NewLoops) {
    // If we did a non-trivial unswitch, we have added new (cloned) loops.
    if (!NewLoops.empty())
      U.addSiblingLoops(NewLoops);

    // If the current loop remains valid, we should revisit it to catch any
    // other unswitch opportunities. Otherwise, we need to mark it as deleted.
    if (CurrentLoopValid)
      U.revisitCurrentLoop();
    else
      U.markLoopAsDeleted(L, LoopName);
  };

  Optional<MemorySSAUpdater> MSSAU;
  if (AR.MSSA) {
    MSSAU = MemorySSAUpdater(AR.MSSA);
    if (VerifyMemorySSA)
      AR.MSSA->verifyMemorySSA();
  }
  if (!unswitchLoop(L, AR.DT, AR.LI, AR.AC, AR.TTI, NonTrivial, UnswitchCB,
                    &AR.SE, MSSAU.hasValue() ? MSSAU.getPointer() : nullptr))
    return PreservedAnalyses::all();

  if (AR.MSSA && VerifyMemorySSA)
    AR.MSSA->verifyMemorySSA();

  // Historically this pass has had issues with the dominator tree so verify it
  // in asserts builds.
  assert(AR.DT.verify(DominatorTree::VerificationLevel::Fast));
  return getLoopPassPreservedAnalyses();
}

namespace {

class SimpleLoopUnswitchLegacyPass : public LoopPass {
  bool NonTrivial;

public:
  static char ID; // Pass ID, replacement for typeid

  explicit SimpleLoopUnswitchLegacyPass(bool NonTrivial = false)
      : LoopPass(ID), NonTrivial(NonTrivial) {
    initializeSimpleLoopUnswitchLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }

  bool runOnLoop(Loop *L, LPPassManager &LPM) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    if (EnableMSSALoopDependency) {
      AU.addRequired<MemorySSAWrapperPass>();
      AU.addPreserved<MemorySSAWrapperPass>();
    }
    getLoopAnalysisUsage(AU);
  }
};

} // end anonymous namespace

bool SimpleLoopUnswitchLegacyPass::runOnLoop(Loop *L, LPPassManager &LPM) {
  if (skipLoop(L))
    return false;

  Function &F = *L->getHeader()->getParent();

  LLVM_DEBUG(dbgs() << "Unswitching loop in " << F.getName() << ": " << *L
                    << "\n");

  auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  auto &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  auto &AC = getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
  auto &TTI = getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
  MemorySSA *MSSA = nullptr;
  Optional<MemorySSAUpdater> MSSAU;
  if (EnableMSSALoopDependency) {
    MSSA = &getAnalysis<MemorySSAWrapperPass>().getMSSA();
    MSSAU = MemorySSAUpdater(MSSA);
  }

  auto *SEWP = getAnalysisIfAvailable<ScalarEvolutionWrapperPass>();
  auto *SE = SEWP ? &SEWP->getSE() : nullptr;

  auto UnswitchCB = [&L, &LPM](bool CurrentLoopValid,
                               ArrayRef<Loop *> NewLoops) {
    // If we did a non-trivial unswitch, we have added new (cloned) loops.
    for (auto *NewL : NewLoops)
      LPM.addLoop(*NewL);

    // If the current loop remains valid, re-add it to the queue. This is
    // a little wasteful as we'll finish processing the current loop as well,
    // but it is the best we can do in the old PM.
    if (CurrentLoopValid)
      LPM.addLoop(*L);
    else
      LPM.markLoopAsDeleted(*L);
  };

  if (MSSA && VerifyMemorySSA)
    MSSA->verifyMemorySSA();

  bool Changed = unswitchLoop(*L, DT, LI, AC, TTI, NonTrivial, UnswitchCB, SE,
                              MSSAU.hasValue() ? MSSAU.getPointer() : nullptr);

  if (MSSA && VerifyMemorySSA)
    MSSA->verifyMemorySSA();

  // If anything was unswitched, also clear any cached information about this
  // loop.
  LPM.deleteSimpleAnalysisLoop(L);

  // Historically this pass has had issues with the dominator tree so verify it
  // in asserts builds.
  assert(DT.verify(DominatorTree::VerificationLevel::Fast));

  return Changed;
}

char SimpleLoopUnswitchLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(SimpleLoopUnswitchLegacyPass, "simple-loop-unswitch",
                      "Simple unswitch loops", false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopPass)
INITIALIZE_PASS_DEPENDENCY(MemorySSAWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_END(SimpleLoopUnswitchLegacyPass, "simple-loop-unswitch",
                    "Simple unswitch loops", false, false)

Pass *llvm::createSimpleLoopUnswitchLegacyPass(bool NonTrivial) {
  return new SimpleLoopUnswitchLegacyPass(NonTrivial);
}
