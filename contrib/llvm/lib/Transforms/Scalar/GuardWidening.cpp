//===- GuardWidening.cpp - ---- Guard widening ----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the guard widening pass.  The semantics of the
// @llvm.experimental.guard intrinsic lets LLVM transform it so that it fails
// more often that it did before the transform.  This optimization is called
// "widening" and can be used hoist and common runtime checks in situations like
// these:
//
//    %cmp0 = 7 u< Length
//    call @llvm.experimental.guard(i1 %cmp0) [ "deopt"(...) ]
//    call @unknown_side_effects()
//    %cmp1 = 9 u< Length
//    call @llvm.experimental.guard(i1 %cmp1) [ "deopt"(...) ]
//    ...
//
// =>
//
//    %cmp0 = 9 u< Length
//    call @llvm.experimental.guard(i1 %cmp0) [ "deopt"(...) ]
//    call @unknown_side_effects()
//    ...
//
// If %cmp0 is false, @llvm.experimental.guard will "deoptimize" back to a
// generic implementation of the same function, which will have the correct
// semantics from that point onward.  It is always _legal_ to deoptimize (so
// replacing %cmp0 with false is "correct"), though it may not always be
// profitable to do so.
//
// NB! This pass is a work in progress.  It hasn't been tuned to be "production
// ready" yet.  It is known to have quadriatic running time and will not scale
// to large numbers of guards
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/GuardWidening.h"
#include <functional>
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/GuardUtils.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

using namespace llvm;

#define DEBUG_TYPE "guard-widening"

STATISTIC(GuardsEliminated, "Number of eliminated guards");
STATISTIC(CondBranchEliminated, "Number of eliminated conditional branches");

static cl::opt<bool> WidenFrequentBranches(
    "guard-widening-widen-frequent-branches", cl::Hidden,
    cl::desc("Widen conditions of explicit branches into dominating guards in "
             "case if their taken frequency exceeds threshold set by "
             "guard-widening-frequent-branch-threshold option"),
    cl::init(false));

static cl::opt<unsigned> FrequentBranchThreshold(
    "guard-widening-frequent-branch-threshold", cl::Hidden,
    cl::desc("When WidenFrequentBranches is set to true, this option is used "
             "to determine which branches are frequently taken. The criteria "
             "that a branch is taken more often than "
             "((FrequentBranchThreshold - 1) / FrequentBranchThreshold), then "
             "it is considered frequently taken"),
    cl::init(1000));


namespace {

// Get the condition of \p I. It can either be a guard or a conditional branch.
static Value *getCondition(Instruction *I) {
  if (IntrinsicInst *GI = dyn_cast<IntrinsicInst>(I)) {
    assert(GI->getIntrinsicID() == Intrinsic::experimental_guard &&
           "Bad guard intrinsic?");
    return GI->getArgOperand(0);
  }
  return cast<BranchInst>(I)->getCondition();
}

// Set the condition for \p I to \p NewCond. \p I can either be a guard or a
// conditional branch.
static void setCondition(Instruction *I, Value *NewCond) {
  if (IntrinsicInst *GI = dyn_cast<IntrinsicInst>(I)) {
    assert(GI->getIntrinsicID() == Intrinsic::experimental_guard &&
           "Bad guard intrinsic?");
    GI->setArgOperand(0, NewCond);
    return;
  }
  cast<BranchInst>(I)->setCondition(NewCond);
}

// Eliminates the guard instruction properly.
static void eliminateGuard(Instruction *GuardInst) {
  GuardInst->eraseFromParent();
  ++GuardsEliminated;
}

class GuardWideningImpl {
  DominatorTree &DT;
  PostDominatorTree *PDT;
  LoopInfo &LI;
  BranchProbabilityInfo *BPI;

  /// Together, these describe the region of interest.  This might be all of
  /// the blocks within a function, or only a given loop's blocks and preheader.
  DomTreeNode *Root;
  std::function<bool(BasicBlock*)> BlockFilter;

  /// The set of guards and conditional branches whose conditions have been
  /// widened into dominating guards.
  SmallVector<Instruction *, 16> EliminatedGuardsAndBranches;

  /// The set of guards which have been widened to include conditions to other
  /// guards.
  DenseSet<Instruction *> WidenedGuards;

  /// Try to eliminate guard \p Guard by widening it into an earlier dominating
  /// guard.  \p DFSI is the DFS iterator on the dominator tree that is
  /// currently visiting the block containing \p Guard, and \p GuardsPerBlock
  /// maps BasicBlocks to the set of guards seen in that block.
  bool eliminateGuardViaWidening(
      Instruction *Guard, const df_iterator<DomTreeNode *> &DFSI,
      const DenseMap<BasicBlock *, SmallVector<Instruction *, 8>> &
          GuardsPerBlock, bool InvertCondition = false);

  /// Used to keep track of which widening potential is more effective.
  enum WideningScore {
    /// Don't widen.
    WS_IllegalOrNegative,

    /// Widening is performance neutral as far as the cycles spent in check
    /// conditions goes (but can still help, e.g., code layout, having less
    /// deopt state).
    WS_Neutral,

    /// Widening is profitable.
    WS_Positive,

    /// Widening is very profitable.  Not significantly different from \c
    /// WS_Positive, except by the order.
    WS_VeryPositive
  };

  static StringRef scoreTypeToString(WideningScore WS);

  /// Compute the score for widening the condition in \p DominatedGuard
  /// (contained in \p DominatedGuardLoop) into \p DominatingGuard (contained in
  /// \p DominatingGuardLoop). If \p InvertCond is set, then we widen the
  /// inverted condition of the dominating guard.
  WideningScore computeWideningScore(Instruction *DominatedGuard,
                                     Loop *DominatedGuardLoop,
                                     Instruction *DominatingGuard,
                                     Loop *DominatingGuardLoop,
                                     bool InvertCond);

  /// Helper to check if \p V can be hoisted to \p InsertPos.
  bool isAvailableAt(Value *V, Instruction *InsertPos) {
    SmallPtrSet<Instruction *, 8> Visited;
    return isAvailableAt(V, InsertPos, Visited);
  }

  bool isAvailableAt(Value *V, Instruction *InsertPos,
                     SmallPtrSetImpl<Instruction *> &Visited);

  /// Helper to hoist \p V to \p InsertPos.  Guaranteed to succeed if \c
  /// isAvailableAt returned true.
  void makeAvailableAt(Value *V, Instruction *InsertPos);

  /// Common helper used by \c widenGuard and \c isWideningCondProfitable.  Try
  /// to generate an expression computing the logical AND of \p Cond0 and (\p
  /// Cond1 XOR \p InvertCondition).
  /// Return true if the expression computing the AND is only as
  /// expensive as computing one of the two. If \p InsertPt is true then
  /// actually generate the resulting expression, make it available at \p
  /// InsertPt and return it in \p Result (else no change to the IR is made).
  bool widenCondCommon(Value *Cond0, Value *Cond1, Instruction *InsertPt,
                       Value *&Result, bool InvertCondition);

  /// Represents a range check of the form \c Base + \c Offset u< \c Length,
  /// with the constraint that \c Length is not negative.  \c CheckInst is the
  /// pre-existing instruction in the IR that computes the result of this range
  /// check.
  class RangeCheck {
    Value *Base;
    ConstantInt *Offset;
    Value *Length;
    ICmpInst *CheckInst;

  public:
    explicit RangeCheck(Value *Base, ConstantInt *Offset, Value *Length,
                        ICmpInst *CheckInst)
        : Base(Base), Offset(Offset), Length(Length), CheckInst(CheckInst) {}

    void setBase(Value *NewBase) { Base = NewBase; }
    void setOffset(ConstantInt *NewOffset) { Offset = NewOffset; }

    Value *getBase() const { return Base; }
    ConstantInt *getOffset() const { return Offset; }
    const APInt &getOffsetValue() const { return getOffset()->getValue(); }
    Value *getLength() const { return Length; };
    ICmpInst *getCheckInst() const { return CheckInst; }

    void print(raw_ostream &OS, bool PrintTypes = false) {
      OS << "Base: ";
      Base->printAsOperand(OS, PrintTypes);
      OS << " Offset: ";
      Offset->printAsOperand(OS, PrintTypes);
      OS << " Length: ";
      Length->printAsOperand(OS, PrintTypes);
    }

    LLVM_DUMP_METHOD void dump() {
      print(dbgs());
      dbgs() << "\n";
    }
  };

  /// Parse \p CheckCond into a conjunction (logical-and) of range checks; and
  /// append them to \p Checks.  Returns true on success, may clobber \c Checks
  /// on failure.
  bool parseRangeChecks(Value *CheckCond, SmallVectorImpl<RangeCheck> &Checks) {
    SmallPtrSet<Value *, 8> Visited;
    return parseRangeChecks(CheckCond, Checks, Visited);
  }

  bool parseRangeChecks(Value *CheckCond, SmallVectorImpl<RangeCheck> &Checks,
                        SmallPtrSetImpl<Value *> &Visited);

  /// Combine the checks in \p Checks into a smaller set of checks and append
  /// them into \p CombinedChecks.  Return true on success (i.e. all of checks
  /// in \p Checks were combined into \p CombinedChecks).  Clobbers \p Checks
  /// and \p CombinedChecks on success and on failure.
  bool combineRangeChecks(SmallVectorImpl<RangeCheck> &Checks,
                          SmallVectorImpl<RangeCheck> &CombinedChecks);

  /// Can we compute the logical AND of \p Cond0 and \p Cond1 for the price of
  /// computing only one of the two expressions?
  bool isWideningCondProfitable(Value *Cond0, Value *Cond1, bool InvertCond) {
    Value *ResultUnused;
    return widenCondCommon(Cond0, Cond1, /*InsertPt=*/nullptr, ResultUnused,
                           InvertCond);
  }

  /// If \p InvertCondition is false, Widen \p ToWiden to fail if
  /// \p NewCondition is false, otherwise make it fail if \p NewCondition is
  /// true (in addition to whatever it is already checking).
  void widenGuard(Instruction *ToWiden, Value *NewCondition,
                  bool InvertCondition) {
    Value *Result;
    widenCondCommon(ToWiden->getOperand(0), NewCondition, ToWiden, Result,
                    InvertCondition);
    setCondition(ToWiden, Result);
  }

public:

  explicit GuardWideningImpl(DominatorTree &DT, PostDominatorTree *PDT,
                             LoopInfo &LI, BranchProbabilityInfo *BPI,
                             DomTreeNode *Root,
                             std::function<bool(BasicBlock*)> BlockFilter)
    : DT(DT), PDT(PDT), LI(LI), BPI(BPI), Root(Root), BlockFilter(BlockFilter)
        {}

  /// The entry point for this pass.
  bool run();
};
}

bool GuardWideningImpl::run() {
  DenseMap<BasicBlock *, SmallVector<Instruction *, 8>> GuardsInBlock;
  bool Changed = false;
  Optional<BranchProbability> LikelyTaken = None;
  if (WidenFrequentBranches && BPI) {
    unsigned Threshold = FrequentBranchThreshold;
    assert(Threshold > 0 && "Zero threshold makes no sense!");
    LikelyTaken = BranchProbability(Threshold - 1, Threshold);
  }

  for (auto DFI = df_begin(Root), DFE = df_end(Root);
       DFI != DFE; ++DFI) {
    auto *BB = (*DFI)->getBlock();
    if (!BlockFilter(BB))
      continue;

    auto &CurrentList = GuardsInBlock[BB];

    for (auto &I : *BB)
      if (isGuard(&I))
        CurrentList.push_back(cast<Instruction>(&I));

    for (auto *II : CurrentList)
      Changed |= eliminateGuardViaWidening(II, DFI, GuardsInBlock);
    if (WidenFrequentBranches && BPI)
      if (auto *BI = dyn_cast<BranchInst>(BB->getTerminator()))
        if (BI->isConditional()) {
          // If one of branches of a conditional is likely taken, try to
          // eliminate it.
          if (BPI->getEdgeProbability(BB, 0U) >= *LikelyTaken)
            Changed |= eliminateGuardViaWidening(BI, DFI, GuardsInBlock);
          else if (BPI->getEdgeProbability(BB, 1U) >= *LikelyTaken)
            Changed |= eliminateGuardViaWidening(BI, DFI, GuardsInBlock,
                                                 /*InvertCondition*/true);
        }
  }

  assert(EliminatedGuardsAndBranches.empty() || Changed);
  for (auto *I : EliminatedGuardsAndBranches)
    if (!WidenedGuards.count(I)) {
      assert(isa<ConstantInt>(getCondition(I)) && "Should be!");
      if (isGuard(I))
        eliminateGuard(I);
      else {
        assert(isa<BranchInst>(I) &&
               "Eliminated something other than guard or branch?");
        ++CondBranchEliminated;
      }
    }

  return Changed;
}

bool GuardWideningImpl::eliminateGuardViaWidening(
    Instruction *GuardInst, const df_iterator<DomTreeNode *> &DFSI,
    const DenseMap<BasicBlock *, SmallVector<Instruction *, 8>> &
        GuardsInBlock, bool InvertCondition) {
  // Ignore trivial true or false conditions. These instructions will be
  // trivially eliminated by any cleanup pass. Do not erase them because other
  // guards can possibly be widened into them.
  if (isa<ConstantInt>(getCondition(GuardInst)))
    return false;

  Instruction *BestSoFar = nullptr;
  auto BestScoreSoFar = WS_IllegalOrNegative;
  auto *GuardInstLoop = LI.getLoopFor(GuardInst->getParent());

  // In the set of dominating guards, find the one we can merge GuardInst with
  // for the most profit.
  for (unsigned i = 0, e = DFSI.getPathLength(); i != e; ++i) {
    auto *CurBB = DFSI.getPath(i)->getBlock();
    if (!BlockFilter(CurBB))
      break;
    auto *CurLoop = LI.getLoopFor(CurBB);
    assert(GuardsInBlock.count(CurBB) && "Must have been populated by now!");
    const auto &GuardsInCurBB = GuardsInBlock.find(CurBB)->second;

    auto I = GuardsInCurBB.begin();
    auto E = GuardsInCurBB.end();

#ifndef NDEBUG
    {
      unsigned Index = 0;
      for (auto &I : *CurBB) {
        if (Index == GuardsInCurBB.size())
          break;
        if (GuardsInCurBB[Index] == &I)
          Index++;
      }
      assert(Index == GuardsInCurBB.size() &&
             "Guards expected to be in order!");
    }
#endif

    assert((i == (e - 1)) == (GuardInst->getParent() == CurBB) && "Bad DFS?");

    if (i == (e - 1) && CurBB->getTerminator() != GuardInst) {
      // Corner case: make sure we're only looking at guards strictly dominating
      // GuardInst when visiting GuardInst->getParent().
      auto NewEnd = std::find(I, E, GuardInst);
      assert(NewEnd != E && "GuardInst not in its own block?");
      E = NewEnd;
    }

    for (auto *Candidate : make_range(I, E)) {
      auto Score =
          computeWideningScore(GuardInst, GuardInstLoop, Candidate, CurLoop,
                               InvertCondition);
      LLVM_DEBUG(dbgs() << "Score between " << *getCondition(GuardInst)
                        << " and " << *getCondition(Candidate) << " is "
                        << scoreTypeToString(Score) << "\n");
      if (Score > BestScoreSoFar) {
        BestScoreSoFar = Score;
        BestSoFar = Candidate;
      }
    }
  }

  if (BestScoreSoFar == WS_IllegalOrNegative) {
    LLVM_DEBUG(dbgs() << "Did not eliminate guard " << *GuardInst << "\n");
    return false;
  }

  assert(BestSoFar != GuardInst && "Should have never visited same guard!");
  assert(DT.dominates(BestSoFar, GuardInst) && "Should be!");

  LLVM_DEBUG(dbgs() << "Widening " << *GuardInst << " into " << *BestSoFar
                    << " with score " << scoreTypeToString(BestScoreSoFar)
                    << "\n");
  widenGuard(BestSoFar, getCondition(GuardInst), InvertCondition);
  auto NewGuardCondition = InvertCondition
                               ? ConstantInt::getFalse(GuardInst->getContext())
                               : ConstantInt::getTrue(GuardInst->getContext());
  setCondition(GuardInst, NewGuardCondition);
  EliminatedGuardsAndBranches.push_back(GuardInst);
  WidenedGuards.insert(BestSoFar);
  return true;
}

GuardWideningImpl::WideningScore GuardWideningImpl::computeWideningScore(
    Instruction *DominatedGuard, Loop *DominatedGuardLoop,
    Instruction *DominatingGuard, Loop *DominatingGuardLoop, bool InvertCond) {
  bool HoistingOutOfLoop = false;

  if (DominatingGuardLoop != DominatedGuardLoop) {
    // Be conservative and don't widen into a sibling loop.  TODO: If the
    // sibling is colder, we should consider allowing this.
    if (DominatingGuardLoop &&
        !DominatingGuardLoop->contains(DominatedGuardLoop))
      return WS_IllegalOrNegative;

    HoistingOutOfLoop = true;
  }

  if (!isAvailableAt(getCondition(DominatedGuard), DominatingGuard))
    return WS_IllegalOrNegative;

  // If the guard was conditional executed, it may never be reached
  // dynamically.  There are two potential downsides to hoisting it out of the
  // conditionally executed region: 1) we may spuriously deopt without need and
  // 2) we have the extra cost of computing the guard condition in the common
  // case.  At the moment, we really only consider the second in our heuristic
  // here.  TODO: evaluate cost model for spurious deopt
  // NOTE: As written, this also lets us hoist right over another guard which
  // is essentially just another spelling for control flow.
  if (isWideningCondProfitable(getCondition(DominatedGuard),
                               getCondition(DominatingGuard), InvertCond))
    return HoistingOutOfLoop ? WS_VeryPositive : WS_Positive;

  if (HoistingOutOfLoop)
    return WS_Positive;

  // Returns true if we might be hoisting above explicit control flow.  Note
  // that this completely ignores implicit control flow (guards, calls which
  // throw, etc...).  That choice appears arbitrary.
  auto MaybeHoistingOutOfIf = [&]() {
    auto *DominatingBlock = DominatingGuard->getParent();
    auto *DominatedBlock = DominatedGuard->getParent();

    // Same Block?
    if (DominatedBlock == DominatingBlock)
      return false;
    // Obvious successor (common loop header/preheader case)
    if (DominatedBlock == DominatingBlock->getUniqueSuccessor())
      return false;
    // TODO: diamond, triangle cases
    if (!PDT) return true;
    return !PDT->dominates(DominatedBlock, DominatingBlock);
  };

  return MaybeHoistingOutOfIf() ? WS_IllegalOrNegative : WS_Neutral;
}

bool GuardWideningImpl::isAvailableAt(Value *V, Instruction *Loc,
                                      SmallPtrSetImpl<Instruction *> &Visited) {
  auto *Inst = dyn_cast<Instruction>(V);
  if (!Inst || DT.dominates(Inst, Loc) || Visited.count(Inst))
    return true;

  if (!isSafeToSpeculativelyExecute(Inst, Loc, &DT) ||
      Inst->mayReadFromMemory())
    return false;

  Visited.insert(Inst);

  // We only want to go _up_ the dominance chain when recursing.
  assert(!isa<PHINode>(Loc) &&
         "PHIs should return false for isSafeToSpeculativelyExecute");
  assert(DT.isReachableFromEntry(Inst->getParent()) &&
         "We did a DFS from the block entry!");
  return all_of(Inst->operands(),
                [&](Value *Op) { return isAvailableAt(Op, Loc, Visited); });
}

void GuardWideningImpl::makeAvailableAt(Value *V, Instruction *Loc) {
  auto *Inst = dyn_cast<Instruction>(V);
  if (!Inst || DT.dominates(Inst, Loc))
    return;

  assert(isSafeToSpeculativelyExecute(Inst, Loc, &DT) &&
         !Inst->mayReadFromMemory() && "Should've checked with isAvailableAt!");

  for (Value *Op : Inst->operands())
    makeAvailableAt(Op, Loc);

  Inst->moveBefore(Loc);
}

bool GuardWideningImpl::widenCondCommon(Value *Cond0, Value *Cond1,
                                        Instruction *InsertPt, Value *&Result,
                                        bool InvertCondition) {
  using namespace llvm::PatternMatch;

  {
    // L >u C0 && L >u C1  ->  L >u max(C0, C1)
    ConstantInt *RHS0, *RHS1;
    Value *LHS;
    ICmpInst::Predicate Pred0, Pred1;
    if (match(Cond0, m_ICmp(Pred0, m_Value(LHS), m_ConstantInt(RHS0))) &&
        match(Cond1, m_ICmp(Pred1, m_Specific(LHS), m_ConstantInt(RHS1)))) {
      if (InvertCondition)
        Pred1 = ICmpInst::getInversePredicate(Pred1);

      ConstantRange CR0 =
          ConstantRange::makeExactICmpRegion(Pred0, RHS0->getValue());
      ConstantRange CR1 =
          ConstantRange::makeExactICmpRegion(Pred1, RHS1->getValue());

      // SubsetIntersect is a subset of the actual mathematical intersection of
      // CR0 and CR1, while SupersetIntersect is a superset of the actual
      // mathematical intersection.  If these two ConstantRanges are equal, then
      // we know we were able to represent the actual mathematical intersection
      // of CR0 and CR1, and can use the same to generate an icmp instruction.
      //
      // Given what we're doing here and the semantics of guards, it would
      // actually be correct to just use SubsetIntersect, but that may be too
      // aggressive in cases we care about.
      auto SubsetIntersect = CR0.inverse().unionWith(CR1.inverse()).inverse();
      auto SupersetIntersect = CR0.intersectWith(CR1);

      APInt NewRHSAP;
      CmpInst::Predicate Pred;
      if (SubsetIntersect == SupersetIntersect &&
          SubsetIntersect.getEquivalentICmp(Pred, NewRHSAP)) {
        if (InsertPt) {
          ConstantInt *NewRHS = ConstantInt::get(Cond0->getContext(), NewRHSAP);
          Result = new ICmpInst(InsertPt, Pred, LHS, NewRHS, "wide.chk");
        }
        return true;
      }
    }
  }

  {
    SmallVector<GuardWideningImpl::RangeCheck, 4> Checks, CombinedChecks;
    // TODO: Support InvertCondition case?
    if (!InvertCondition &&
        parseRangeChecks(Cond0, Checks) && parseRangeChecks(Cond1, Checks) &&
        combineRangeChecks(Checks, CombinedChecks)) {
      if (InsertPt) {
        Result = nullptr;
        for (auto &RC : CombinedChecks) {
          makeAvailableAt(RC.getCheckInst(), InsertPt);
          if (Result)
            Result = BinaryOperator::CreateAnd(RC.getCheckInst(), Result, "",
                                               InsertPt);
          else
            Result = RC.getCheckInst();
        }

        Result->setName("wide.chk");
      }
      return true;
    }
  }

  // Base case -- just logical-and the two conditions together.

  if (InsertPt) {
    makeAvailableAt(Cond0, InsertPt);
    makeAvailableAt(Cond1, InsertPt);
    if (InvertCondition)
      Cond1 = BinaryOperator::CreateNot(Cond1, "inverted", InsertPt);
    Result = BinaryOperator::CreateAnd(Cond0, Cond1, "wide.chk", InsertPt);
  }

  // We were not able to compute Cond0 AND Cond1 for the price of one.
  return false;
}

bool GuardWideningImpl::parseRangeChecks(
    Value *CheckCond, SmallVectorImpl<GuardWideningImpl::RangeCheck> &Checks,
    SmallPtrSetImpl<Value *> &Visited) {
  if (!Visited.insert(CheckCond).second)
    return true;

  using namespace llvm::PatternMatch;

  {
    Value *AndLHS, *AndRHS;
    if (match(CheckCond, m_And(m_Value(AndLHS), m_Value(AndRHS))))
      return parseRangeChecks(AndLHS, Checks) &&
             parseRangeChecks(AndRHS, Checks);
  }

  auto *IC = dyn_cast<ICmpInst>(CheckCond);
  if (!IC || !IC->getOperand(0)->getType()->isIntegerTy() ||
      (IC->getPredicate() != ICmpInst::ICMP_ULT &&
       IC->getPredicate() != ICmpInst::ICMP_UGT))
    return false;

  Value *CmpLHS = IC->getOperand(0), *CmpRHS = IC->getOperand(1);
  if (IC->getPredicate() == ICmpInst::ICMP_UGT)
    std::swap(CmpLHS, CmpRHS);

  auto &DL = IC->getModule()->getDataLayout();

  GuardWideningImpl::RangeCheck Check(
      CmpLHS, cast<ConstantInt>(ConstantInt::getNullValue(CmpRHS->getType())),
      CmpRHS, IC);

  if (!isKnownNonNegative(Check.getLength(), DL))
    return false;

  // What we have in \c Check now is a correct interpretation of \p CheckCond.
  // Try to see if we can move some constant offsets into the \c Offset field.

  bool Changed;
  auto &Ctx = CheckCond->getContext();

  do {
    Value *OpLHS;
    ConstantInt *OpRHS;
    Changed = false;

#ifndef NDEBUG
    auto *BaseInst = dyn_cast<Instruction>(Check.getBase());
    assert((!BaseInst || DT.isReachableFromEntry(BaseInst->getParent())) &&
           "Unreachable instruction?");
#endif

    if (match(Check.getBase(), m_Add(m_Value(OpLHS), m_ConstantInt(OpRHS)))) {
      Check.setBase(OpLHS);
      APInt NewOffset = Check.getOffsetValue() + OpRHS->getValue();
      Check.setOffset(ConstantInt::get(Ctx, NewOffset));
      Changed = true;
    } else if (match(Check.getBase(),
                     m_Or(m_Value(OpLHS), m_ConstantInt(OpRHS)))) {
      KnownBits Known = computeKnownBits(OpLHS, DL);
      if ((OpRHS->getValue() & Known.Zero) == OpRHS->getValue()) {
        Check.setBase(OpLHS);
        APInt NewOffset = Check.getOffsetValue() + OpRHS->getValue();
        Check.setOffset(ConstantInt::get(Ctx, NewOffset));
        Changed = true;
      }
    }
  } while (Changed);

  Checks.push_back(Check);
  return true;
}

bool GuardWideningImpl::combineRangeChecks(
    SmallVectorImpl<GuardWideningImpl::RangeCheck> &Checks,
    SmallVectorImpl<GuardWideningImpl::RangeCheck> &RangeChecksOut) {
  unsigned OldCount = Checks.size();
  while (!Checks.empty()) {
    // Pick all of the range checks with a specific base and length, and try to
    // merge them.
    Value *CurrentBase = Checks.front().getBase();
    Value *CurrentLength = Checks.front().getLength();

    SmallVector<GuardWideningImpl::RangeCheck, 3> CurrentChecks;

    auto IsCurrentCheck = [&](GuardWideningImpl::RangeCheck &RC) {
      return RC.getBase() == CurrentBase && RC.getLength() == CurrentLength;
    };

    copy_if(Checks, std::back_inserter(CurrentChecks), IsCurrentCheck);
    Checks.erase(remove_if(Checks, IsCurrentCheck), Checks.end());

    assert(CurrentChecks.size() != 0 && "We know we have at least one!");

    if (CurrentChecks.size() < 3) {
      RangeChecksOut.insert(RangeChecksOut.end(), CurrentChecks.begin(),
                            CurrentChecks.end());
      continue;
    }

    // CurrentChecks.size() will typically be 3 here, but so far there has been
    // no need to hard-code that fact.

    llvm::sort(CurrentChecks, [&](const GuardWideningImpl::RangeCheck &LHS,
                                  const GuardWideningImpl::RangeCheck &RHS) {
      return LHS.getOffsetValue().slt(RHS.getOffsetValue());
    });

    // Note: std::sort should not invalidate the ChecksStart iterator.

    ConstantInt *MinOffset = CurrentChecks.front().getOffset(),
                *MaxOffset = CurrentChecks.back().getOffset();

    unsigned BitWidth = MaxOffset->getValue().getBitWidth();
    if ((MaxOffset->getValue() - MinOffset->getValue())
            .ugt(APInt::getSignedMinValue(BitWidth)))
      return false;

    APInt MaxDiff = MaxOffset->getValue() - MinOffset->getValue();
    const APInt &HighOffset = MaxOffset->getValue();
    auto OffsetOK = [&](const GuardWideningImpl::RangeCheck &RC) {
      return (HighOffset - RC.getOffsetValue()).ult(MaxDiff);
    };

    if (MaxDiff.isMinValue() ||
        !std::all_of(std::next(CurrentChecks.begin()), CurrentChecks.end(),
                     OffsetOK))
      return false;

    // We have a series of f+1 checks as:
    //
    //   I+k_0 u< L   ... Chk_0
    //   I+k_1 u< L   ... Chk_1
    //   ...
    //   I+k_f u< L   ... Chk_f
    //
    //     with forall i in [0,f]: k_f-k_i u< k_f-k_0  ... Precond_0
    //          k_f-k_0 u< INT_MIN+k_f                 ... Precond_1
    //          k_f != k_0                             ... Precond_2
    //
    // Claim:
    //   Chk_0 AND Chk_f  implies all the other checks
    //
    // Informal proof sketch:
    //
    // We will show that the integer range [I+k_0,I+k_f] does not unsigned-wrap
    // (i.e. going from I+k_0 to I+k_f does not cross the -1,0 boundary) and
    // thus I+k_f is the greatest unsigned value in that range.
    //
    // This combined with Ckh_(f+1) shows that everything in that range is u< L.
    // Via Precond_0 we know that all of the indices in Chk_0 through Chk_(f+1)
    // lie in [I+k_0,I+k_f], this proving our claim.
    //
    // To see that [I+k_0,I+k_f] is not a wrapping range, note that there are
    // two possibilities: I+k_0 u< I+k_f or I+k_0 >u I+k_f (they can't be equal
    // since k_0 != k_f).  In the former case, [I+k_0,I+k_f] is not a wrapping
    // range by definition, and the latter case is impossible:
    //
    //   0-----I+k_f---I+k_0----L---INT_MAX,INT_MIN------------------(-1)
    //   xxxxxx             xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
    //
    // For Chk_0 to succeed, we'd have to have k_f-k_0 (the range highlighted
    // with 'x' above) to be at least >u INT_MIN.

    RangeChecksOut.emplace_back(CurrentChecks.front());
    RangeChecksOut.emplace_back(CurrentChecks.back());
  }

  assert(RangeChecksOut.size() <= OldCount && "We pessimized!");
  return RangeChecksOut.size() != OldCount;
}

#ifndef NDEBUG
StringRef GuardWideningImpl::scoreTypeToString(WideningScore WS) {
  switch (WS) {
  case WS_IllegalOrNegative:
    return "IllegalOrNegative";
  case WS_Neutral:
    return "Neutral";
  case WS_Positive:
    return "Positive";
  case WS_VeryPositive:
    return "VeryPositive";
  }

  llvm_unreachable("Fully covered switch above!");
}
#endif

PreservedAnalyses GuardWideningPass::run(Function &F,
                                         FunctionAnalysisManager &AM) {
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &LI = AM.getResult<LoopAnalysis>(F);
  auto &PDT = AM.getResult<PostDominatorTreeAnalysis>(F);
  BranchProbabilityInfo *BPI = nullptr;
  if (WidenFrequentBranches)
    BPI = AM.getCachedResult<BranchProbabilityAnalysis>(F);
  if (!GuardWideningImpl(DT, &PDT, LI, BPI, DT.getRootNode(),
                         [](BasicBlock*) { return true; } ).run())
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}

namespace {
struct GuardWideningLegacyPass : public FunctionPass {
  static char ID;

  GuardWideningLegacyPass() : FunctionPass(ID) {
    initializeGuardWideningLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;
    auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    auto &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    auto &PDT = getAnalysis<PostDominatorTreeWrapperPass>().getPostDomTree();
    BranchProbabilityInfo *BPI = nullptr;
    if (WidenFrequentBranches)
      BPI = &getAnalysis<BranchProbabilityInfoWrapperPass>().getBPI();
    return GuardWideningImpl(DT, &PDT, LI, BPI, DT.getRootNode(),
                         [](BasicBlock*) { return true; } ).run();
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<PostDominatorTreeWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    if (WidenFrequentBranches)
      AU.addRequired<BranchProbabilityInfoWrapperPass>();
  }
};

/// Same as above, but restricted to a single loop at a time.  Can be
/// scheduled with other loop passes w/o breaking out of LPM
struct LoopGuardWideningLegacyPass : public LoopPass {
  static char ID;

  LoopGuardWideningLegacyPass() : LoopPass(ID) {
    initializeLoopGuardWideningLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnLoop(Loop *L, LPPassManager &LPM) override {
    if (skipLoop(L))
      return false;
    auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    auto &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    auto *PDTWP = getAnalysisIfAvailable<PostDominatorTreeWrapperPass>();
    auto *PDT = PDTWP ? &PDTWP->getPostDomTree() : nullptr;
    BasicBlock *RootBB = L->getLoopPredecessor();
    if (!RootBB)
      RootBB = L->getHeader();
    auto BlockFilter = [&](BasicBlock *BB) {
      return BB == RootBB || L->contains(BB);
    };
    BranchProbabilityInfo *BPI = nullptr;
    if (WidenFrequentBranches)
      BPI = &getAnalysis<BranchProbabilityInfoWrapperPass>().getBPI();
    return GuardWideningImpl(DT, PDT, LI, BPI,
                             DT.getNode(RootBB), BlockFilter).run();
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    if (WidenFrequentBranches)
      AU.addRequired<BranchProbabilityInfoWrapperPass>();
    AU.setPreservesCFG();
    getLoopAnalysisUsage(AU);
    AU.addPreserved<PostDominatorTreeWrapperPass>();
  }
};
}

char GuardWideningLegacyPass::ID = 0;
char LoopGuardWideningLegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(GuardWideningLegacyPass, "guard-widening", "Widen guards",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(PostDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
if (WidenFrequentBranches)
  INITIALIZE_PASS_DEPENDENCY(BranchProbabilityInfoWrapperPass)
INITIALIZE_PASS_END(GuardWideningLegacyPass, "guard-widening", "Widen guards",
                    false, false)

INITIALIZE_PASS_BEGIN(LoopGuardWideningLegacyPass, "loop-guard-widening",
                      "Widen guards (within a single loop, as a loop pass)",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(PostDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
if (WidenFrequentBranches)
  INITIALIZE_PASS_DEPENDENCY(BranchProbabilityInfoWrapperPass)
INITIALIZE_PASS_END(LoopGuardWideningLegacyPass, "loop-guard-widening",
                    "Widen guards (within a single loop, as a loop pass)",
                    false, false)

FunctionPass *llvm::createGuardWideningPass() {
  return new GuardWideningLegacyPass();
}

Pass *llvm::createLoopGuardWideningPass() {
  return new LoopGuardWideningLegacyPass();
}
