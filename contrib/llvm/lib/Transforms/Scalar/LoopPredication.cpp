//===-- LoopPredication.cpp - Guard based loop predication pass -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The LoopPredication pass tries to convert loop variant range checks to loop
// invariant by widening checks across loop iterations. For example, it will
// convert
//
//   for (i = 0; i < n; i++) {
//     guard(i < len);
//     ...
//   }
//
// to
//
//   for (i = 0; i < n; i++) {
//     guard(n - 1 < len);
//     ...
//   }
//
// After this transformation the condition of the guard is loop invariant, so
// loop-unswitch can later unswitch the loop by this condition which basically
// predicates the loop by the widened condition:
//
//   if (n - 1 < len)
//     for (i = 0; i < n; i++) {
//       ...
//     }
//   else
//     deoptimize
//
// It's tempting to rely on SCEV here, but it has proven to be problematic.
// Generally the facts SCEV provides about the increment step of add
// recurrences are true if the backedge of the loop is taken, which implicitly
// assumes that the guard doesn't fail. Using these facts to optimize the
// guard results in a circular logic where the guard is optimized under the
// assumption that it never fails.
//
// For example, in the loop below the induction variable will be marked as nuw
// basing on the guard. Basing on nuw the guard predicate will be considered
// monotonic. Given a monotonic condition it's tempting to replace the induction
// variable in the condition with its value on the last iteration. But this
// transformation is not correct, e.g. e = 4, b = 5 breaks the loop.
//
//   for (int i = b; i != e; i++)
//     guard(i u< len)
//
// One of the ways to reason about this problem is to use an inductive proof
// approach. Given the loop:
//
//   if (B(0)) {
//     do {
//       I = PHI(0, I.INC)
//       I.INC = I + Step
//       guard(G(I));
//     } while (B(I));
//   }
//
// where B(x) and G(x) are predicates that map integers to booleans, we want a
// loop invariant expression M such the following program has the same semantics
// as the above:
//
//   if (B(0)) {
//     do {
//       I = PHI(0, I.INC)
//       I.INC = I + Step
//       guard(G(0) && M);
//     } while (B(I));
//   }
//
// One solution for M is M = forall X . (G(X) && B(X)) => G(X + Step)
//
// Informal proof that the transformation above is correct:
//
//   By the definition of guards we can rewrite the guard condition to:
//     G(I) && G(0) && M
//
//   Let's prove that for each iteration of the loop:
//     G(0) && M => G(I)
//   And the condition above can be simplified to G(Start) && M.
//
//   Induction base.
//     G(0) && M => G(0)
//
//   Induction step. Assuming G(0) && M => G(I) on the subsequent
//   iteration:
//
//     B(I) is true because it's the backedge condition.
//     G(I) is true because the backedge is guarded by this condition.
//
//   So M = forall X . (G(X) && B(X)) => G(X + Step) implies G(I + Step).
//
// Note that we can use anything stronger than M, i.e. any condition which
// implies M.
//
// When S = 1 (i.e. forward iterating loop), the transformation is supported
// when:
//   * The loop has a single latch with the condition of the form:
//     B(X) = latchStart + X <pred> latchLimit,
//     where <pred> is u<, u<=, s<, or s<=.
//   * The guard condition is of the form
//     G(X) = guardStart + X u< guardLimit
//
//   For the ult latch comparison case M is:
//     forall X . guardStart + X u< guardLimit && latchStart + X <u latchLimit =>
//        guardStart + X + 1 u< guardLimit
//
//   The only way the antecedent can be true and the consequent can be false is
//   if
//     X == guardLimit - 1 - guardStart
//   (and guardLimit is non-zero, but we won't use this latter fact).
//   If X == guardLimit - 1 - guardStart then the second half of the antecedent is
//     latchStart + guardLimit - 1 - guardStart u< latchLimit
//   and its negation is
//     latchStart + guardLimit - 1 - guardStart u>= latchLimit
//
//   In other words, if
//     latchLimit u<= latchStart + guardLimit - 1 - guardStart
//   then:
//   (the ranges below are written in ConstantRange notation, where [A, B) is the
//   set for (I = A; I != B; I++ /*maywrap*/) yield(I);)
//
//      forall X . guardStart + X u< guardLimit &&
//                 latchStart + X u< latchLimit =>
//        guardStart + X + 1 u< guardLimit
//   == forall X . guardStart + X u< guardLimit &&
//                 latchStart + X u< latchStart + guardLimit - 1 - guardStart =>
//        guardStart + X + 1 u< guardLimit
//   == forall X . (guardStart + X) in [0, guardLimit) &&
//                 (latchStart + X) in [0, latchStart + guardLimit - 1 - guardStart) =>
//        (guardStart + X + 1) in [0, guardLimit)
//   == forall X . X in [-guardStart, guardLimit - guardStart) &&
//                 X in [-latchStart, guardLimit - 1 - guardStart) =>
//         X in [-guardStart - 1, guardLimit - guardStart - 1)
//   == true
//
//   So the widened condition is:
//     guardStart u< guardLimit &&
//     latchStart + guardLimit - 1 - guardStart u>= latchLimit
//   Similarly for ule condition the widened condition is:
//     guardStart u< guardLimit &&
//     latchStart + guardLimit - 1 - guardStart u> latchLimit
//   For slt condition the widened condition is:
//     guardStart u< guardLimit &&
//     latchStart + guardLimit - 1 - guardStart s>= latchLimit
//   For sle condition the widened condition is:
//     guardStart u< guardLimit &&
//     latchStart + guardLimit - 1 - guardStart s> latchLimit
//
// When S = -1 (i.e. reverse iterating loop), the transformation is supported
// when:
//   * The loop has a single latch with the condition of the form:
//     B(X) = X <pred> latchLimit, where <pred> is u>, u>=, s>, or s>=.
//   * The guard condition is of the form
//     G(X) = X - 1 u< guardLimit
//
//   For the ugt latch comparison case M is:
//     forall X. X-1 u< guardLimit and X u> latchLimit => X-2 u< guardLimit
//
//   The only way the antecedent can be true and the consequent can be false is if
//     X == 1.
//   If X == 1 then the second half of the antecedent is
//     1 u> latchLimit, and its negation is latchLimit u>= 1.
//
//   So the widened condition is:
//     guardStart u< guardLimit && latchLimit u>= 1.
//   Similarly for sgt condition the widened condition is:
//     guardStart u< guardLimit && latchLimit s>= 1.
//   For uge condition the widened condition is:
//     guardStart u< guardLimit && latchLimit u> 1.
//   For sge condition the widened condition is:
//     guardStart u< guardLimit && latchLimit s> 1.
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LoopPredication.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/GuardUtils.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

#define DEBUG_TYPE "loop-predication"

STATISTIC(TotalConsidered, "Number of guards considered");
STATISTIC(TotalWidened, "Number of checks widened");

using namespace llvm;

static cl::opt<bool> EnableIVTruncation("loop-predication-enable-iv-truncation",
                                        cl::Hidden, cl::init(true));

static cl::opt<bool> EnableCountDownLoop("loop-predication-enable-count-down-loop",
                                        cl::Hidden, cl::init(true));

static cl::opt<bool>
    SkipProfitabilityChecks("loop-predication-skip-profitability-checks",
                            cl::Hidden, cl::init(false));

// This is the scale factor for the latch probability. We use this during
// profitability analysis to find other exiting blocks that have a much higher
// probability of exiting the loop instead of loop exiting via latch.
// This value should be greater than 1 for a sane profitability check.
static cl::opt<float> LatchExitProbabilityScale(
    "loop-predication-latch-probability-scale", cl::Hidden, cl::init(2.0),
    cl::desc("scale factor for the latch probability. Value should be greater "
             "than 1. Lower values are ignored"));

namespace {
class LoopPredication {
  /// Represents an induction variable check:
  ///   icmp Pred, <induction variable>, <loop invariant limit>
  struct LoopICmp {
    ICmpInst::Predicate Pred;
    const SCEVAddRecExpr *IV;
    const SCEV *Limit;
    LoopICmp(ICmpInst::Predicate Pred, const SCEVAddRecExpr *IV,
             const SCEV *Limit)
        : Pred(Pred), IV(IV), Limit(Limit) {}
    LoopICmp() {}
    void dump() {
      dbgs() << "LoopICmp Pred = " << Pred << ", IV = " << *IV
             << ", Limit = " << *Limit << "\n";
    }
  };

  ScalarEvolution *SE;
  BranchProbabilityInfo *BPI;

  Loop *L;
  const DataLayout *DL;
  BasicBlock *Preheader;
  LoopICmp LatchCheck;

  bool isSupportedStep(const SCEV* Step);
  Optional<LoopICmp> parseLoopICmp(ICmpInst *ICI) {
    return parseLoopICmp(ICI->getPredicate(), ICI->getOperand(0),
                         ICI->getOperand(1));
  }
  Optional<LoopICmp> parseLoopICmp(ICmpInst::Predicate Pred, Value *LHS,
                                   Value *RHS);

  Optional<LoopICmp> parseLoopLatchICmp();

  bool CanExpand(const SCEV* S);
  Value *expandCheck(SCEVExpander &Expander, IRBuilder<> &Builder,
                     ICmpInst::Predicate Pred, const SCEV *LHS, const SCEV *RHS,
                     Instruction *InsertAt);

  Optional<Value *> widenICmpRangeCheck(ICmpInst *ICI, SCEVExpander &Expander,
                                        IRBuilder<> &Builder);
  Optional<Value *> widenICmpRangeCheckIncrementingLoop(LoopICmp LatchCheck,
                                                        LoopICmp RangeCheck,
                                                        SCEVExpander &Expander,
                                                        IRBuilder<> &Builder);
  Optional<Value *> widenICmpRangeCheckDecrementingLoop(LoopICmp LatchCheck,
                                                        LoopICmp RangeCheck,
                                                        SCEVExpander &Expander,
                                                        IRBuilder<> &Builder);
  bool widenGuardConditions(IntrinsicInst *II, SCEVExpander &Expander);

  // If the loop always exits through another block in the loop, we should not
  // predicate based on the latch check. For example, the latch check can be a
  // very coarse grained check and there can be more fine grained exit checks
  // within the loop. We identify such unprofitable loops through BPI.
  bool isLoopProfitableToPredicate();

  // When the IV type is wider than the range operand type, we can still do loop
  // predication, by generating SCEVs for the range and latch that are of the
  // same type. We achieve this by generating a SCEV truncate expression for the
  // latch IV. This is done iff truncation of the IV is a safe operation,
  // without loss of information.
  // Another way to achieve this is by generating a wider type SCEV for the
  // range check operand, however, this needs a more involved check that
  // operands do not overflow. This can lead to loss of information when the
  // range operand is of the form: add i32 %offset, %iv. We need to prove that
  // sext(x + y) is same as sext(x) + sext(y).
  // This function returns true if we can safely represent the IV type in
  // the RangeCheckType without loss of information.
  bool isSafeToTruncateWideIVType(Type *RangeCheckType);
  // Return the loopLatchCheck corresponding to the RangeCheckType if safe to do
  // so.
  Optional<LoopICmp> generateLoopLatchCheck(Type *RangeCheckType);

public:
  LoopPredication(ScalarEvolution *SE, BranchProbabilityInfo *BPI)
      : SE(SE), BPI(BPI){};
  bool runOnLoop(Loop *L);
};

class LoopPredicationLegacyPass : public LoopPass {
public:
  static char ID;
  LoopPredicationLegacyPass() : LoopPass(ID) {
    initializeLoopPredicationLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<BranchProbabilityInfoWrapperPass>();
    getLoopAnalysisUsage(AU);
  }

  bool runOnLoop(Loop *L, LPPassManager &LPM) override {
    if (skipLoop(L))
      return false;
    auto *SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
    BranchProbabilityInfo &BPI =
        getAnalysis<BranchProbabilityInfoWrapperPass>().getBPI();
    LoopPredication LP(SE, &BPI);
    return LP.runOnLoop(L);
  }
};

char LoopPredicationLegacyPass::ID = 0;
} // end namespace llvm

INITIALIZE_PASS_BEGIN(LoopPredicationLegacyPass, "loop-predication",
                      "Loop predication", false, false)
INITIALIZE_PASS_DEPENDENCY(BranchProbabilityInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopPass)
INITIALIZE_PASS_END(LoopPredicationLegacyPass, "loop-predication",
                    "Loop predication", false, false)

Pass *llvm::createLoopPredicationPass() {
  return new LoopPredicationLegacyPass();
}

PreservedAnalyses LoopPredicationPass::run(Loop &L, LoopAnalysisManager &AM,
                                           LoopStandardAnalysisResults &AR,
                                           LPMUpdater &U) {
  const auto &FAM =
      AM.getResult<FunctionAnalysisManagerLoopProxy>(L, AR).getManager();
  Function *F = L.getHeader()->getParent();
  auto *BPI = FAM.getCachedResult<BranchProbabilityAnalysis>(*F);
  LoopPredication LP(&AR.SE, BPI);
  if (!LP.runOnLoop(&L))
    return PreservedAnalyses::all();

  return getLoopPassPreservedAnalyses();
}

Optional<LoopPredication::LoopICmp>
LoopPredication::parseLoopICmp(ICmpInst::Predicate Pred, Value *LHS,
                               Value *RHS) {
  const SCEV *LHSS = SE->getSCEV(LHS);
  if (isa<SCEVCouldNotCompute>(LHSS))
    return None;
  const SCEV *RHSS = SE->getSCEV(RHS);
  if (isa<SCEVCouldNotCompute>(RHSS))
    return None;

  // Canonicalize RHS to be loop invariant bound, LHS - a loop computable IV
  if (SE->isLoopInvariant(LHSS, L)) {
    std::swap(LHS, RHS);
    std::swap(LHSS, RHSS);
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }

  const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(LHSS);
  if (!AR || AR->getLoop() != L)
    return None;

  return LoopICmp(Pred, AR, RHSS);
}

Value *LoopPredication::expandCheck(SCEVExpander &Expander,
                                    IRBuilder<> &Builder,
                                    ICmpInst::Predicate Pred, const SCEV *LHS,
                                    const SCEV *RHS, Instruction *InsertAt) {
  // TODO: we can check isLoopEntryGuardedByCond before emitting the check

  Type *Ty = LHS->getType();
  assert(Ty == RHS->getType() && "expandCheck operands have different types?");

  if (SE->isLoopEntryGuardedByCond(L, Pred, LHS, RHS))
    return Builder.getTrue();

  Value *LHSV = Expander.expandCodeFor(LHS, Ty, InsertAt);
  Value *RHSV = Expander.expandCodeFor(RHS, Ty, InsertAt);
  return Builder.CreateICmp(Pred, LHSV, RHSV);
}

Optional<LoopPredication::LoopICmp>
LoopPredication::generateLoopLatchCheck(Type *RangeCheckType) {

  auto *LatchType = LatchCheck.IV->getType();
  if (RangeCheckType == LatchType)
    return LatchCheck;
  // For now, bail out if latch type is narrower than range type.
  if (DL->getTypeSizeInBits(LatchType) < DL->getTypeSizeInBits(RangeCheckType))
    return None;
  if (!isSafeToTruncateWideIVType(RangeCheckType))
    return None;
  // We can now safely identify the truncated version of the IV and limit for
  // RangeCheckType.
  LoopICmp NewLatchCheck;
  NewLatchCheck.Pred = LatchCheck.Pred;
  NewLatchCheck.IV = dyn_cast<SCEVAddRecExpr>(
      SE->getTruncateExpr(LatchCheck.IV, RangeCheckType));
  if (!NewLatchCheck.IV)
    return None;
  NewLatchCheck.Limit = SE->getTruncateExpr(LatchCheck.Limit, RangeCheckType);
  LLVM_DEBUG(dbgs() << "IV of type: " << *LatchType
                    << "can be represented as range check type:"
                    << *RangeCheckType << "\n");
  LLVM_DEBUG(dbgs() << "LatchCheck.IV: " << *NewLatchCheck.IV << "\n");
  LLVM_DEBUG(dbgs() << "LatchCheck.Limit: " << *NewLatchCheck.Limit << "\n");
  return NewLatchCheck;
}

bool LoopPredication::isSupportedStep(const SCEV* Step) {
  return Step->isOne() || (Step->isAllOnesValue() && EnableCountDownLoop);
}

bool LoopPredication::CanExpand(const SCEV* S) {
  return SE->isLoopInvariant(S, L) && isSafeToExpand(S, *SE);
}

Optional<Value *> LoopPredication::widenICmpRangeCheckIncrementingLoop(
    LoopPredication::LoopICmp LatchCheck, LoopPredication::LoopICmp RangeCheck,
    SCEVExpander &Expander, IRBuilder<> &Builder) {
  auto *Ty = RangeCheck.IV->getType();
  // Generate the widened condition for the forward loop:
  //   guardStart u< guardLimit &&
  //   latchLimit <pred> guardLimit - 1 - guardStart + latchStart
  // where <pred> depends on the latch condition predicate. See the file
  // header comment for the reasoning.
  // guardLimit - guardStart + latchStart - 1
  const SCEV *GuardStart = RangeCheck.IV->getStart();
  const SCEV *GuardLimit = RangeCheck.Limit;
  const SCEV *LatchStart = LatchCheck.IV->getStart();
  const SCEV *LatchLimit = LatchCheck.Limit;

  // guardLimit - guardStart + latchStart - 1
  const SCEV *RHS =
      SE->getAddExpr(SE->getMinusSCEV(GuardLimit, GuardStart),
                     SE->getMinusSCEV(LatchStart, SE->getOne(Ty)));
  if (!CanExpand(GuardStart) || !CanExpand(GuardLimit) ||
      !CanExpand(LatchLimit) || !CanExpand(RHS)) {
    LLVM_DEBUG(dbgs() << "Can't expand limit check!\n");
    return None;
  }
  auto LimitCheckPred =
      ICmpInst::getFlippedStrictnessPredicate(LatchCheck.Pred);

  LLVM_DEBUG(dbgs() << "LHS: " << *LatchLimit << "\n");
  LLVM_DEBUG(dbgs() << "RHS: " << *RHS << "\n");
  LLVM_DEBUG(dbgs() << "Pred: " << LimitCheckPred << "\n");

  Instruction *InsertAt = Preheader->getTerminator();
  auto *LimitCheck =
      expandCheck(Expander, Builder, LimitCheckPred, LatchLimit, RHS, InsertAt);
  auto *FirstIterationCheck = expandCheck(Expander, Builder, RangeCheck.Pred,
                                          GuardStart, GuardLimit, InsertAt);
  return Builder.CreateAnd(FirstIterationCheck, LimitCheck);
}

Optional<Value *> LoopPredication::widenICmpRangeCheckDecrementingLoop(
    LoopPredication::LoopICmp LatchCheck, LoopPredication::LoopICmp RangeCheck,
    SCEVExpander &Expander, IRBuilder<> &Builder) {
  auto *Ty = RangeCheck.IV->getType();
  const SCEV *GuardStart = RangeCheck.IV->getStart();
  const SCEV *GuardLimit = RangeCheck.Limit;
  const SCEV *LatchLimit = LatchCheck.Limit;
  if (!CanExpand(GuardStart) || !CanExpand(GuardLimit) ||
      !CanExpand(LatchLimit)) {
    LLVM_DEBUG(dbgs() << "Can't expand limit check!\n");
    return None;
  }
  // The decrement of the latch check IV should be the same as the
  // rangeCheckIV.
  auto *PostDecLatchCheckIV = LatchCheck.IV->getPostIncExpr(*SE);
  if (RangeCheck.IV != PostDecLatchCheckIV) {
    LLVM_DEBUG(dbgs() << "Not the same. PostDecLatchCheckIV: "
                      << *PostDecLatchCheckIV
                      << "  and RangeCheckIV: " << *RangeCheck.IV << "\n");
    return None;
  }

  // Generate the widened condition for CountDownLoop:
  // guardStart u< guardLimit &&
  // latchLimit <pred> 1.
  // See the header comment for reasoning of the checks.
  Instruction *InsertAt = Preheader->getTerminator();
  auto LimitCheckPred =
      ICmpInst::getFlippedStrictnessPredicate(LatchCheck.Pred);
  auto *FirstIterationCheck = expandCheck(Expander, Builder, ICmpInst::ICMP_ULT,
                                          GuardStart, GuardLimit, InsertAt);
  auto *LimitCheck = expandCheck(Expander, Builder, LimitCheckPred, LatchLimit,
                                 SE->getOne(Ty), InsertAt);
  return Builder.CreateAnd(FirstIterationCheck, LimitCheck);
}

/// If ICI can be widened to a loop invariant condition emits the loop
/// invariant condition in the loop preheader and return it, otherwise
/// returns None.
Optional<Value *> LoopPredication::widenICmpRangeCheck(ICmpInst *ICI,
                                                       SCEVExpander &Expander,
                                                       IRBuilder<> &Builder) {
  LLVM_DEBUG(dbgs() << "Analyzing ICmpInst condition:\n");
  LLVM_DEBUG(ICI->dump());

  // parseLoopStructure guarantees that the latch condition is:
  //   ++i <pred> latchLimit, where <pred> is u<, u<=, s<, or s<=.
  // We are looking for the range checks of the form:
  //   i u< guardLimit
  auto RangeCheck = parseLoopICmp(ICI);
  if (!RangeCheck) {
    LLVM_DEBUG(dbgs() << "Failed to parse the loop latch condition!\n");
    return None;
  }
  LLVM_DEBUG(dbgs() << "Guard check:\n");
  LLVM_DEBUG(RangeCheck->dump());
  if (RangeCheck->Pred != ICmpInst::ICMP_ULT) {
    LLVM_DEBUG(dbgs() << "Unsupported range check predicate("
                      << RangeCheck->Pred << ")!\n");
    return None;
  }
  auto *RangeCheckIV = RangeCheck->IV;
  if (!RangeCheckIV->isAffine()) {
    LLVM_DEBUG(dbgs() << "Range check IV is not affine!\n");
    return None;
  }
  auto *Step = RangeCheckIV->getStepRecurrence(*SE);
  // We cannot just compare with latch IV step because the latch and range IVs
  // may have different types.
  if (!isSupportedStep(Step)) {
    LLVM_DEBUG(dbgs() << "Range check and latch have IVs different steps!\n");
    return None;
  }
  auto *Ty = RangeCheckIV->getType();
  auto CurrLatchCheckOpt = generateLoopLatchCheck(Ty);
  if (!CurrLatchCheckOpt) {
    LLVM_DEBUG(dbgs() << "Failed to generate a loop latch check "
                         "corresponding to range type: "
                      << *Ty << "\n");
    return None;
  }

  LoopICmp CurrLatchCheck = *CurrLatchCheckOpt;
  // At this point, the range and latch step should have the same type, but need
  // not have the same value (we support both 1 and -1 steps).
  assert(Step->getType() ==
             CurrLatchCheck.IV->getStepRecurrence(*SE)->getType() &&
         "Range and latch steps should be of same type!");
  if (Step != CurrLatchCheck.IV->getStepRecurrence(*SE)) {
    LLVM_DEBUG(dbgs() << "Range and latch have different step values!\n");
    return None;
  }

  if (Step->isOne())
    return widenICmpRangeCheckIncrementingLoop(CurrLatchCheck, *RangeCheck,
                                               Expander, Builder);
  else {
    assert(Step->isAllOnesValue() && "Step should be -1!");
    return widenICmpRangeCheckDecrementingLoop(CurrLatchCheck, *RangeCheck,
                                               Expander, Builder);
  }
}

bool LoopPredication::widenGuardConditions(IntrinsicInst *Guard,
                                           SCEVExpander &Expander) {
  LLVM_DEBUG(dbgs() << "Processing guard:\n");
  LLVM_DEBUG(Guard->dump());

  TotalConsidered++;

  IRBuilder<> Builder(cast<Instruction>(Preheader->getTerminator()));

  // The guard condition is expected to be in form of:
  //   cond1 && cond2 && cond3 ...
  // Iterate over subconditions looking for icmp conditions which can be
  // widened across loop iterations. Widening these conditions remember the
  // resulting list of subconditions in Checks vector.
  SmallVector<Value *, 4> Worklist(1, Guard->getOperand(0));
  SmallPtrSet<Value *, 4> Visited;

  SmallVector<Value *, 4> Checks;

  unsigned NumWidened = 0;
  do {
    Value *Condition = Worklist.pop_back_val();
    if (!Visited.insert(Condition).second)
      continue;

    Value *LHS, *RHS;
    using namespace llvm::PatternMatch;
    if (match(Condition, m_And(m_Value(LHS), m_Value(RHS)))) {
      Worklist.push_back(LHS);
      Worklist.push_back(RHS);
      continue;
    }

    if (ICmpInst *ICI = dyn_cast<ICmpInst>(Condition)) {
      if (auto NewRangeCheck = widenICmpRangeCheck(ICI, Expander, Builder)) {
        Checks.push_back(NewRangeCheck.getValue());
        NumWidened++;
        continue;
      }
    }

    // Save the condition as is if we can't widen it
    Checks.push_back(Condition);
  } while (Worklist.size() != 0);

  if (NumWidened == 0)
    return false;

  TotalWidened += NumWidened;

  // Emit the new guard condition
  Builder.SetInsertPoint(Guard);
  Value *LastCheck = nullptr;
  for (auto *Check : Checks)
    if (!LastCheck)
      LastCheck = Check;
    else
      LastCheck = Builder.CreateAnd(LastCheck, Check);
  Guard->setOperand(0, LastCheck);

  LLVM_DEBUG(dbgs() << "Widened checks = " << NumWidened << "\n");
  return true;
}

Optional<LoopPredication::LoopICmp> LoopPredication::parseLoopLatchICmp() {
  using namespace PatternMatch;

  BasicBlock *LoopLatch = L->getLoopLatch();
  if (!LoopLatch) {
    LLVM_DEBUG(dbgs() << "The loop doesn't have a single latch!\n");
    return None;
  }

  ICmpInst::Predicate Pred;
  Value *LHS, *RHS;
  BasicBlock *TrueDest, *FalseDest;

  if (!match(LoopLatch->getTerminator(),
             m_Br(m_ICmp(Pred, m_Value(LHS), m_Value(RHS)), TrueDest,
                  FalseDest))) {
    LLVM_DEBUG(dbgs() << "Failed to match the latch terminator!\n");
    return None;
  }
  assert((TrueDest == L->getHeader() || FalseDest == L->getHeader()) &&
         "One of the latch's destinations must be the header");
  if (TrueDest != L->getHeader())
    Pred = ICmpInst::getInversePredicate(Pred);

  auto Result = parseLoopICmp(Pred, LHS, RHS);
  if (!Result) {
    LLVM_DEBUG(dbgs() << "Failed to parse the loop latch condition!\n");
    return None;
  }

  // Check affine first, so if it's not we don't try to compute the step
  // recurrence.
  if (!Result->IV->isAffine()) {
    LLVM_DEBUG(dbgs() << "The induction variable is not affine!\n");
    return None;
  }

  auto *Step = Result->IV->getStepRecurrence(*SE);
  if (!isSupportedStep(Step)) {
    LLVM_DEBUG(dbgs() << "Unsupported loop stride(" << *Step << ")!\n");
    return None;
  }

  auto IsUnsupportedPredicate = [](const SCEV *Step, ICmpInst::Predicate Pred) {
    if (Step->isOne()) {
      return Pred != ICmpInst::ICMP_ULT && Pred != ICmpInst::ICMP_SLT &&
             Pred != ICmpInst::ICMP_ULE && Pred != ICmpInst::ICMP_SLE;
    } else {
      assert(Step->isAllOnesValue() && "Step should be -1!");
      return Pred != ICmpInst::ICMP_UGT && Pred != ICmpInst::ICMP_SGT &&
             Pred != ICmpInst::ICMP_UGE && Pred != ICmpInst::ICMP_SGE;
    }
  };

  if (IsUnsupportedPredicate(Step, Result->Pred)) {
    LLVM_DEBUG(dbgs() << "Unsupported loop latch predicate(" << Result->Pred
                      << ")!\n");
    return None;
  }
  return Result;
}

// Returns true if its safe to truncate the IV to RangeCheckType.
bool LoopPredication::isSafeToTruncateWideIVType(Type *RangeCheckType) {
  if (!EnableIVTruncation)
    return false;
  assert(DL->getTypeSizeInBits(LatchCheck.IV->getType()) >
             DL->getTypeSizeInBits(RangeCheckType) &&
         "Expected latch check IV type to be larger than range check operand "
         "type!");
  // The start and end values of the IV should be known. This is to guarantee
  // that truncating the wide type will not lose information.
  auto *Limit = dyn_cast<SCEVConstant>(LatchCheck.Limit);
  auto *Start = dyn_cast<SCEVConstant>(LatchCheck.IV->getStart());
  if (!Limit || !Start)
    return false;
  // This check makes sure that the IV does not change sign during loop
  // iterations. Consider latchType = i64, LatchStart = 5, Pred = ICMP_SGE,
  // LatchEnd = 2, rangeCheckType = i32. If it's not a monotonic predicate, the
  // IV wraps around, and the truncation of the IV would lose the range of
  // iterations between 2^32 and 2^64.
  bool Increasing;
  if (!SE->isMonotonicPredicate(LatchCheck.IV, LatchCheck.Pred, Increasing))
    return false;
  // The active bits should be less than the bits in the RangeCheckType. This
  // guarantees that truncating the latch check to RangeCheckType is a safe
  // operation.
  auto RangeCheckTypeBitSize = DL->getTypeSizeInBits(RangeCheckType);
  return Start->getAPInt().getActiveBits() < RangeCheckTypeBitSize &&
         Limit->getAPInt().getActiveBits() < RangeCheckTypeBitSize;
}

bool LoopPredication::isLoopProfitableToPredicate() {
  if (SkipProfitabilityChecks || !BPI)
    return true;

  SmallVector<std::pair<const BasicBlock *, const BasicBlock *>, 8> ExitEdges;
  L->getExitEdges(ExitEdges);
  // If there is only one exiting edge in the loop, it is always profitable to
  // predicate the loop.
  if (ExitEdges.size() == 1)
    return true;

  // Calculate the exiting probabilities of all exiting edges from the loop,
  // starting with the LatchExitProbability.
  // Heuristic for profitability: If any of the exiting blocks' probability of
  // exiting the loop is larger than exiting through the latch block, it's not
  // profitable to predicate the loop.
  auto *LatchBlock = L->getLoopLatch();
  assert(LatchBlock && "Should have a single latch at this point!");
  auto *LatchTerm = LatchBlock->getTerminator();
  assert(LatchTerm->getNumSuccessors() == 2 &&
         "expected to be an exiting block with 2 succs!");
  unsigned LatchBrExitIdx =
      LatchTerm->getSuccessor(0) == L->getHeader() ? 1 : 0;
  BranchProbability LatchExitProbability =
      BPI->getEdgeProbability(LatchBlock, LatchBrExitIdx);

  // Protect against degenerate inputs provided by the user. Providing a value
  // less than one, can invert the definition of profitable loop predication.
  float ScaleFactor = LatchExitProbabilityScale;
  if (ScaleFactor < 1) {
    LLVM_DEBUG(
        dbgs()
        << "Ignored user setting for loop-predication-latch-probability-scale: "
        << LatchExitProbabilityScale << "\n");
    LLVM_DEBUG(dbgs() << "The value is set to 1.0\n");
    ScaleFactor = 1.0;
  }
  const auto LatchProbabilityThreshold =
      LatchExitProbability * ScaleFactor;

  for (const auto &ExitEdge : ExitEdges) {
    BranchProbability ExitingBlockProbability =
        BPI->getEdgeProbability(ExitEdge.first, ExitEdge.second);
    // Some exiting edge has higher probability than the latch exiting edge.
    // No longer profitable to predicate.
    if (ExitingBlockProbability > LatchProbabilityThreshold)
      return false;
  }
  // Using BPI, we have concluded that the most probable way to exit from the
  // loop is through the latch (or there's no profile information and all
  // exits are equally likely).
  return true;
}

bool LoopPredication::runOnLoop(Loop *Loop) {
  L = Loop;

  LLVM_DEBUG(dbgs() << "Analyzing ");
  LLVM_DEBUG(L->dump());

  Module *M = L->getHeader()->getModule();

  // There is nothing to do if the module doesn't use guards
  auto *GuardDecl =
      M->getFunction(Intrinsic::getName(Intrinsic::experimental_guard));
  if (!GuardDecl || GuardDecl->use_empty())
    return false;

  DL = &M->getDataLayout();

  Preheader = L->getLoopPreheader();
  if (!Preheader)
    return false;

  auto LatchCheckOpt = parseLoopLatchICmp();
  if (!LatchCheckOpt)
    return false;
  LatchCheck = *LatchCheckOpt;

  LLVM_DEBUG(dbgs() << "Latch check:\n");
  LLVM_DEBUG(LatchCheck.dump());

  if (!isLoopProfitableToPredicate()) {
    LLVM_DEBUG(dbgs() << "Loop not profitable to predicate!\n");
    return false;
  }
  // Collect all the guards into a vector and process later, so as not
  // to invalidate the instruction iterator.
  SmallVector<IntrinsicInst *, 4> Guards;
  for (const auto BB : L->blocks())
    for (auto &I : *BB)
      if (isGuard(&I))
        Guards.push_back(cast<IntrinsicInst>(&I));

  if (Guards.empty())
    return false;

  SCEVExpander Expander(*SE, *DL, "loop-predication");

  bool Changed = false;
  for (auto *Guard : Guards)
    Changed |= widenGuardConditions(Guard, Expander);

  return Changed;
}
