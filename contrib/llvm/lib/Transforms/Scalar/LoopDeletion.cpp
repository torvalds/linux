//===- LoopDeletion.cpp - Dead Loop Deletion Pass ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Dead Loop Deletion Pass. This pass is responsible
// for eliminating loops with non-infinite computable trip counts that have no
// side effects or volatile instructions, and do not contribute to the
// computation of the function's return value.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LoopDeletion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
using namespace llvm;

#define DEBUG_TYPE "loop-delete"

STATISTIC(NumDeleted, "Number of loops deleted");

enum class LoopDeletionResult {
  Unmodified,
  Modified,
  Deleted,
};

/// Determines if a loop is dead.
///
/// This assumes that we've already checked for unique exit and exiting blocks,
/// and that the code is in LCSSA form.
static bool isLoopDead(Loop *L, ScalarEvolution &SE,
                       SmallVectorImpl<BasicBlock *> &ExitingBlocks,
                       BasicBlock *ExitBlock, bool &Changed,
                       BasicBlock *Preheader) {
  // Make sure that all PHI entries coming from the loop are loop invariant.
  // Because the code is in LCSSA form, any values used outside of the loop
  // must pass through a PHI in the exit block, meaning that this check is
  // sufficient to guarantee that no loop-variant values are used outside
  // of the loop.
  bool AllEntriesInvariant = true;
  bool AllOutgoingValuesSame = true;
  for (PHINode &P : ExitBlock->phis()) {
    Value *incoming = P.getIncomingValueForBlock(ExitingBlocks[0]);

    // Make sure all exiting blocks produce the same incoming value for the exit
    // block.  If there are different incoming values for different exiting
    // blocks, then it is impossible to statically determine which value should
    // be used.
    AllOutgoingValuesSame =
        all_of(makeArrayRef(ExitingBlocks).slice(1), [&](BasicBlock *BB) {
          return incoming == P.getIncomingValueForBlock(BB);
        });

    if (!AllOutgoingValuesSame)
      break;

    if (Instruction *I = dyn_cast<Instruction>(incoming))
      if (!L->makeLoopInvariant(I, Changed, Preheader->getTerminator())) {
        AllEntriesInvariant = false;
        break;
      }
  }

  if (Changed)
    SE.forgetLoopDispositions(L);

  if (!AllEntriesInvariant || !AllOutgoingValuesSame)
    return false;

  // Make sure that no instructions in the block have potential side-effects.
  // This includes instructions that could write to memory, and loads that are
  // marked volatile.
  for (auto &I : L->blocks())
    if (any_of(*I, [](Instruction &I) { return I.mayHaveSideEffects(); }))
      return false;
  return true;
}

/// This function returns true if there is no viable path from the
/// entry block to the header of \p L. Right now, it only does
/// a local search to save compile time.
static bool isLoopNeverExecuted(Loop *L) {
  using namespace PatternMatch;

  auto *Preheader = L->getLoopPreheader();
  // TODO: We can relax this constraint, since we just need a loop
  // predecessor.
  assert(Preheader && "Needs preheader!");

  if (Preheader == &Preheader->getParent()->getEntryBlock())
    return false;
  // All predecessors of the preheader should have a constant conditional
  // branch, with the loop's preheader as not-taken.
  for (auto *Pred: predecessors(Preheader)) {
    BasicBlock *Taken, *NotTaken;
    ConstantInt *Cond;
    if (!match(Pred->getTerminator(),
               m_Br(m_ConstantInt(Cond), Taken, NotTaken)))
      return false;
    if (!Cond->getZExtValue())
      std::swap(Taken, NotTaken);
    if (Taken == Preheader)
      return false;
  }
  assert(!pred_empty(Preheader) &&
         "Preheader should have predecessors at this point!");
  // All the predecessors have the loop preheader as not-taken target.
  return true;
}

/// Remove a loop if it is dead.
///
/// A loop is considered dead if it does not impact the observable behavior of
/// the program other than finite running time. This never removes a loop that
/// might be infinite (unless it is never executed), as doing so could change
/// the halting/non-halting nature of a program.
///
/// This entire process relies pretty heavily on LoopSimplify form and LCSSA in
/// order to make various safety checks work.
///
/// \returns true if any changes were made. This may mutate the loop even if it
/// is unable to delete it due to hoisting trivially loop invariant
/// instructions out of the loop.
static LoopDeletionResult deleteLoopIfDead(Loop *L, DominatorTree &DT,
                                           ScalarEvolution &SE, LoopInfo &LI) {
  assert(L->isLCSSAForm(DT) && "Expected LCSSA!");

  // We can only remove the loop if there is a preheader that we can branch from
  // after removing it. Also, if LoopSimplify form is not available, stay out
  // of trouble.
  BasicBlock *Preheader = L->getLoopPreheader();
  if (!Preheader || !L->hasDedicatedExits()) {
    LLVM_DEBUG(
        dbgs()
        << "Deletion requires Loop with preheader and dedicated exits.\n");
    return LoopDeletionResult::Unmodified;
  }
  // We can't remove loops that contain subloops.  If the subloops were dead,
  // they would already have been removed in earlier executions of this pass.
  if (L->begin() != L->end()) {
    LLVM_DEBUG(dbgs() << "Loop contains subloops.\n");
    return LoopDeletionResult::Unmodified;
  }


  BasicBlock *ExitBlock = L->getUniqueExitBlock();

  if (ExitBlock && isLoopNeverExecuted(L)) {
    LLVM_DEBUG(dbgs() << "Loop is proven to never execute, delete it!");
    // Set incoming value to undef for phi nodes in the exit block.
    for (PHINode &P : ExitBlock->phis()) {
      std::fill(P.incoming_values().begin(), P.incoming_values().end(),
                UndefValue::get(P.getType()));
    }
    deleteDeadLoop(L, &DT, &SE, &LI);
    ++NumDeleted;
    return LoopDeletionResult::Deleted;
  }

  // The remaining checks below are for a loop being dead because all statements
  // in the loop are invariant.
  SmallVector<BasicBlock *, 4> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);

  // We require that the loop only have a single exit block.  Otherwise, we'd
  // be in the situation of needing to be able to solve statically which exit
  // block will be branched to, or trying to preserve the branching logic in
  // a loop invariant manner.
  if (!ExitBlock) {
    LLVM_DEBUG(dbgs() << "Deletion requires single exit block\n");
    return LoopDeletionResult::Unmodified;
  }
  // Finally, we have to check that the loop really is dead.
  bool Changed = false;
  if (!isLoopDead(L, SE, ExitingBlocks, ExitBlock, Changed, Preheader)) {
    LLVM_DEBUG(dbgs() << "Loop is not invariant, cannot delete.\n");
    return Changed ? LoopDeletionResult::Modified
                   : LoopDeletionResult::Unmodified;
  }

  // Don't remove loops for which we can't solve the trip count.
  // They could be infinite, in which case we'd be changing program behavior.
  const SCEV *S = SE.getMaxBackedgeTakenCount(L);
  if (isa<SCEVCouldNotCompute>(S)) {
    LLVM_DEBUG(dbgs() << "Could not compute SCEV MaxBackedgeTakenCount.\n");
    return Changed ? LoopDeletionResult::Modified
                   : LoopDeletionResult::Unmodified;
  }

  LLVM_DEBUG(dbgs() << "Loop is invariant, delete it!");
  deleteDeadLoop(L, &DT, &SE, &LI);
  ++NumDeleted;

  return LoopDeletionResult::Deleted;
}

PreservedAnalyses LoopDeletionPass::run(Loop &L, LoopAnalysisManager &AM,
                                        LoopStandardAnalysisResults &AR,
                                        LPMUpdater &Updater) {

  LLVM_DEBUG(dbgs() << "Analyzing Loop for deletion: ");
  LLVM_DEBUG(L.dump());
  std::string LoopName = L.getName();
  auto Result = deleteLoopIfDead(&L, AR.DT, AR.SE, AR.LI);
  if (Result == LoopDeletionResult::Unmodified)
    return PreservedAnalyses::all();

  if (Result == LoopDeletionResult::Deleted)
    Updater.markLoopAsDeleted(L, LoopName);

  return getLoopPassPreservedAnalyses();
}

namespace {
class LoopDeletionLegacyPass : public LoopPass {
public:
  static char ID; // Pass ID, replacement for typeid
  LoopDeletionLegacyPass() : LoopPass(ID) {
    initializeLoopDeletionLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  // Possibly eliminate loop L if it is dead.
  bool runOnLoop(Loop *L, LPPassManager &) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    getLoopAnalysisUsage(AU);
  }
};
}

char LoopDeletionLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(LoopDeletionLegacyPass, "loop-deletion",
                      "Delete dead loops", false, false)
INITIALIZE_PASS_DEPENDENCY(LoopPass)
INITIALIZE_PASS_END(LoopDeletionLegacyPass, "loop-deletion",
                    "Delete dead loops", false, false)

Pass *llvm::createLoopDeletionPass() { return new LoopDeletionLegacyPass(); }

bool LoopDeletionLegacyPass::runOnLoop(Loop *L, LPPassManager &LPM) {
  if (skipLoop(L))
    return false;
  DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  ScalarEvolution &SE = getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();

  LLVM_DEBUG(dbgs() << "Analyzing Loop for deletion: ");
  LLVM_DEBUG(L->dump());

  LoopDeletionResult Result = deleteLoopIfDead(L, DT, SE, LI);

  if (Result == LoopDeletionResult::Deleted)
    LPM.markLoopAsDeleted(*L);

  return Result != LoopDeletionResult::Unmodified;
}
