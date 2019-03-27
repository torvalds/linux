//===-- SimplifyIndVar.cpp - Induction variable simplification ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements induction variable simplification. It does
// not define any actual pass or policy, but provides a single function to
// simplify a loop's induction variables based on ScalarEvolution.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/SimplifyIndVar.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Local.h"

using namespace llvm;

#define DEBUG_TYPE "indvars"

STATISTIC(NumElimIdentity, "Number of IV identities eliminated");
STATISTIC(NumElimOperand,  "Number of IV operands folded into a use");
STATISTIC(NumFoldedUser, "Number of IV users folded into a constant");
STATISTIC(NumElimRem     , "Number of IV remainder operations eliminated");
STATISTIC(
    NumSimplifiedSDiv,
    "Number of IV signed division operations converted to unsigned division");
STATISTIC(
    NumSimplifiedSRem,
    "Number of IV signed remainder operations converted to unsigned remainder");
STATISTIC(NumElimCmp     , "Number of IV comparisons eliminated");

namespace {
  /// This is a utility for simplifying induction variables
  /// based on ScalarEvolution. It is the primary instrument of the
  /// IndvarSimplify pass, but it may also be directly invoked to cleanup after
  /// other loop passes that preserve SCEV.
  class SimplifyIndvar {
    Loop             *L;
    LoopInfo         *LI;
    ScalarEvolution  *SE;
    DominatorTree    *DT;
    SCEVExpander     &Rewriter;
    SmallVectorImpl<WeakTrackingVH> &DeadInsts;

    bool Changed;

  public:
    SimplifyIndvar(Loop *Loop, ScalarEvolution *SE, DominatorTree *DT,
                   LoopInfo *LI, SCEVExpander &Rewriter,
                   SmallVectorImpl<WeakTrackingVH> &Dead)
        : L(Loop), LI(LI), SE(SE), DT(DT), Rewriter(Rewriter), DeadInsts(Dead),
          Changed(false) {
      assert(LI && "IV simplification requires LoopInfo");
    }

    bool hasChanged() const { return Changed; }

    /// Iteratively perform simplification on a worklist of users of the
    /// specified induction variable. This is the top-level driver that applies
    /// all simplifications to users of an IV.
    void simplifyUsers(PHINode *CurrIV, IVVisitor *V = nullptr);

    Value *foldIVUser(Instruction *UseInst, Instruction *IVOperand);

    bool eliminateIdentitySCEV(Instruction *UseInst, Instruction *IVOperand);
    bool replaceIVUserWithLoopInvariant(Instruction *UseInst);

    bool eliminateOverflowIntrinsic(CallInst *CI);
    bool eliminateTrunc(TruncInst *TI);
    bool eliminateIVUser(Instruction *UseInst, Instruction *IVOperand);
    bool makeIVComparisonInvariant(ICmpInst *ICmp, Value *IVOperand);
    void eliminateIVComparison(ICmpInst *ICmp, Value *IVOperand);
    void simplifyIVRemainder(BinaryOperator *Rem, Value *IVOperand,
                             bool IsSigned);
    void replaceRemWithNumerator(BinaryOperator *Rem);
    void replaceRemWithNumeratorOrZero(BinaryOperator *Rem);
    void replaceSRemWithURem(BinaryOperator *Rem);
    bool eliminateSDiv(BinaryOperator *SDiv);
    bool strengthenOverflowingOperation(BinaryOperator *OBO, Value *IVOperand);
    bool strengthenRightShift(BinaryOperator *BO, Value *IVOperand);
  };
}

/// Fold an IV operand into its use.  This removes increments of an
/// aligned IV when used by a instruction that ignores the low bits.
///
/// IVOperand is guaranteed SCEVable, but UseInst may not be.
///
/// Return the operand of IVOperand for this induction variable if IVOperand can
/// be folded (in case more folding opportunities have been exposed).
/// Otherwise return null.
Value *SimplifyIndvar::foldIVUser(Instruction *UseInst, Instruction *IVOperand) {
  Value *IVSrc = nullptr;
  const unsigned OperIdx = 0;
  const SCEV *FoldedExpr = nullptr;
  bool MustDropExactFlag = false;
  switch (UseInst->getOpcode()) {
  default:
    return nullptr;
  case Instruction::UDiv:
  case Instruction::LShr:
    // We're only interested in the case where we know something about
    // the numerator and have a constant denominator.
    if (IVOperand != UseInst->getOperand(OperIdx) ||
        !isa<ConstantInt>(UseInst->getOperand(1)))
      return nullptr;

    // Attempt to fold a binary operator with constant operand.
    // e.g. ((I + 1) >> 2) => I >> 2
    if (!isa<BinaryOperator>(IVOperand)
        || !isa<ConstantInt>(IVOperand->getOperand(1)))
      return nullptr;

    IVSrc = IVOperand->getOperand(0);
    // IVSrc must be the (SCEVable) IV, since the other operand is const.
    assert(SE->isSCEVable(IVSrc->getType()) && "Expect SCEVable IV operand");

    ConstantInt *D = cast<ConstantInt>(UseInst->getOperand(1));
    if (UseInst->getOpcode() == Instruction::LShr) {
      // Get a constant for the divisor. See createSCEV.
      uint32_t BitWidth = cast<IntegerType>(UseInst->getType())->getBitWidth();
      if (D->getValue().uge(BitWidth))
        return nullptr;

      D = ConstantInt::get(UseInst->getContext(),
                           APInt::getOneBitSet(BitWidth, D->getZExtValue()));
    }
    FoldedExpr = SE->getUDivExpr(SE->getSCEV(IVSrc), SE->getSCEV(D));
    // We might have 'exact' flag set at this point which will no longer be
    // correct after we make the replacement.
    if (UseInst->isExact() &&
        SE->getSCEV(IVSrc) != SE->getMulExpr(FoldedExpr, SE->getSCEV(D)))
      MustDropExactFlag = true;
  }
  // We have something that might fold it's operand. Compare SCEVs.
  if (!SE->isSCEVable(UseInst->getType()))
    return nullptr;

  // Bypass the operand if SCEV can prove it has no effect.
  if (SE->getSCEV(UseInst) != FoldedExpr)
    return nullptr;

  LLVM_DEBUG(dbgs() << "INDVARS: Eliminated IV operand: " << *IVOperand
                    << " -> " << *UseInst << '\n');

  UseInst->setOperand(OperIdx, IVSrc);
  assert(SE->getSCEV(UseInst) == FoldedExpr && "bad SCEV with folded oper");

  if (MustDropExactFlag)
    UseInst->dropPoisonGeneratingFlags();

  ++NumElimOperand;
  Changed = true;
  if (IVOperand->use_empty())
    DeadInsts.emplace_back(IVOperand);
  return IVSrc;
}

bool SimplifyIndvar::makeIVComparisonInvariant(ICmpInst *ICmp,
                                               Value *IVOperand) {
  unsigned IVOperIdx = 0;
  ICmpInst::Predicate Pred = ICmp->getPredicate();
  if (IVOperand != ICmp->getOperand(0)) {
    // Swapped
    assert(IVOperand == ICmp->getOperand(1) && "Can't find IVOperand");
    IVOperIdx = 1;
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }

  // Get the SCEVs for the ICmp operands (in the specific context of the
  // current loop)
  const Loop *ICmpLoop = LI->getLoopFor(ICmp->getParent());
  const SCEV *S = SE->getSCEVAtScope(ICmp->getOperand(IVOperIdx), ICmpLoop);
  const SCEV *X = SE->getSCEVAtScope(ICmp->getOperand(1 - IVOperIdx), ICmpLoop);

  ICmpInst::Predicate InvariantPredicate;
  const SCEV *InvariantLHS, *InvariantRHS;

  auto *PN = dyn_cast<PHINode>(IVOperand);
  if (!PN)
    return false;
  if (!SE->isLoopInvariantPredicate(Pred, S, X, L, InvariantPredicate,
                                    InvariantLHS, InvariantRHS))
    return false;

  // Rewrite the comparison to a loop invariant comparison if it can be done
  // cheaply, where cheaply means "we don't need to emit any new
  // instructions".

  SmallDenseMap<const SCEV*, Value*> CheapExpansions;
  CheapExpansions[S] = ICmp->getOperand(IVOperIdx);
  CheapExpansions[X] = ICmp->getOperand(1 - IVOperIdx);

  // TODO: Support multiple entry loops?  (We currently bail out of these in
  // the IndVarSimplify pass)
  if (auto *BB = L->getLoopPredecessor()) {
    const int Idx = PN->getBasicBlockIndex(BB);
    if (Idx >= 0) {
      Value *Incoming = PN->getIncomingValue(Idx);
      const SCEV *IncomingS = SE->getSCEV(Incoming);
      CheapExpansions[IncomingS] = Incoming;
    }
  }
  Value *NewLHS = CheapExpansions[InvariantLHS];
  Value *NewRHS = CheapExpansions[InvariantRHS];

  if (!NewLHS)
    if (auto *ConstLHS = dyn_cast<SCEVConstant>(InvariantLHS))
      NewLHS = ConstLHS->getValue();
  if (!NewRHS)
    if (auto *ConstRHS = dyn_cast<SCEVConstant>(InvariantRHS))
      NewRHS = ConstRHS->getValue();

  if (!NewLHS || !NewRHS)
    // We could not find an existing value to replace either LHS or RHS.
    // Generating new instructions has subtler tradeoffs, so avoid doing that
    // for now.
    return false;

  LLVM_DEBUG(dbgs() << "INDVARS: Simplified comparison: " << *ICmp << '\n');
  ICmp->setPredicate(InvariantPredicate);
  ICmp->setOperand(0, NewLHS);
  ICmp->setOperand(1, NewRHS);
  return true;
}

/// SimplifyIVUsers helper for eliminating useless
/// comparisons against an induction variable.
void SimplifyIndvar::eliminateIVComparison(ICmpInst *ICmp, Value *IVOperand) {
  unsigned IVOperIdx = 0;
  ICmpInst::Predicate Pred = ICmp->getPredicate();
  ICmpInst::Predicate OriginalPred = Pred;
  if (IVOperand != ICmp->getOperand(0)) {
    // Swapped
    assert(IVOperand == ICmp->getOperand(1) && "Can't find IVOperand");
    IVOperIdx = 1;
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }

  // Get the SCEVs for the ICmp operands (in the specific context of the
  // current loop)
  const Loop *ICmpLoop = LI->getLoopFor(ICmp->getParent());
  const SCEV *S = SE->getSCEVAtScope(ICmp->getOperand(IVOperIdx), ICmpLoop);
  const SCEV *X = SE->getSCEVAtScope(ICmp->getOperand(1 - IVOperIdx), ICmpLoop);

  // If the condition is always true or always false, replace it with
  // a constant value.
  if (SE->isKnownPredicate(Pred, S, X)) {
    ICmp->replaceAllUsesWith(ConstantInt::getTrue(ICmp->getContext()));
    DeadInsts.emplace_back(ICmp);
    LLVM_DEBUG(dbgs() << "INDVARS: Eliminated comparison: " << *ICmp << '\n');
  } else if (SE->isKnownPredicate(ICmpInst::getInversePredicate(Pred), S, X)) {
    ICmp->replaceAllUsesWith(ConstantInt::getFalse(ICmp->getContext()));
    DeadInsts.emplace_back(ICmp);
    LLVM_DEBUG(dbgs() << "INDVARS: Eliminated comparison: " << *ICmp << '\n');
  } else if (makeIVComparisonInvariant(ICmp, IVOperand)) {
    // fallthrough to end of function
  } else if (ICmpInst::isSigned(OriginalPred) &&
             SE->isKnownNonNegative(S) && SE->isKnownNonNegative(X)) {
    // If we were unable to make anything above, all we can is to canonicalize
    // the comparison hoping that it will open the doors for other
    // optimizations. If we find out that we compare two non-negative values,
    // we turn the instruction's predicate to its unsigned version. Note that
    // we cannot rely on Pred here unless we check if we have swapped it.
    assert(ICmp->getPredicate() == OriginalPred && "Predicate changed?");
    LLVM_DEBUG(dbgs() << "INDVARS: Turn to unsigned comparison: " << *ICmp
                      << '\n');
    ICmp->setPredicate(ICmpInst::getUnsignedPredicate(OriginalPred));
  } else
    return;

  ++NumElimCmp;
  Changed = true;
}

bool SimplifyIndvar::eliminateSDiv(BinaryOperator *SDiv) {
  // Get the SCEVs for the ICmp operands.
  auto *N = SE->getSCEV(SDiv->getOperand(0));
  auto *D = SE->getSCEV(SDiv->getOperand(1));

  // Simplify unnecessary loops away.
  const Loop *L = LI->getLoopFor(SDiv->getParent());
  N = SE->getSCEVAtScope(N, L);
  D = SE->getSCEVAtScope(D, L);

  // Replace sdiv by udiv if both of the operands are non-negative
  if (SE->isKnownNonNegative(N) && SE->isKnownNonNegative(D)) {
    auto *UDiv = BinaryOperator::Create(
        BinaryOperator::UDiv, SDiv->getOperand(0), SDiv->getOperand(1),
        SDiv->getName() + ".udiv", SDiv);
    UDiv->setIsExact(SDiv->isExact());
    SDiv->replaceAllUsesWith(UDiv);
    LLVM_DEBUG(dbgs() << "INDVARS: Simplified sdiv: " << *SDiv << '\n');
    ++NumSimplifiedSDiv;
    Changed = true;
    DeadInsts.push_back(SDiv);
    return true;
  }

  return false;
}

// i %s n -> i %u n if i >= 0 and n >= 0
void SimplifyIndvar::replaceSRemWithURem(BinaryOperator *Rem) {
  auto *N = Rem->getOperand(0), *D = Rem->getOperand(1);
  auto *URem = BinaryOperator::Create(BinaryOperator::URem, N, D,
                                      Rem->getName() + ".urem", Rem);
  Rem->replaceAllUsesWith(URem);
  LLVM_DEBUG(dbgs() << "INDVARS: Simplified srem: " << *Rem << '\n');
  ++NumSimplifiedSRem;
  Changed = true;
  DeadInsts.emplace_back(Rem);
}

// i % n  -->  i  if i is in [0,n).
void SimplifyIndvar::replaceRemWithNumerator(BinaryOperator *Rem) {
  Rem->replaceAllUsesWith(Rem->getOperand(0));
  LLVM_DEBUG(dbgs() << "INDVARS: Simplified rem: " << *Rem << '\n');
  ++NumElimRem;
  Changed = true;
  DeadInsts.emplace_back(Rem);
}

// (i+1) % n  -->  (i+1)==n?0:(i+1)  if i is in [0,n).
void SimplifyIndvar::replaceRemWithNumeratorOrZero(BinaryOperator *Rem) {
  auto *T = Rem->getType();
  auto *N = Rem->getOperand(0), *D = Rem->getOperand(1);
  ICmpInst *ICmp = new ICmpInst(Rem, ICmpInst::ICMP_EQ, N, D);
  SelectInst *Sel =
      SelectInst::Create(ICmp, ConstantInt::get(T, 0), N, "iv.rem", Rem);
  Rem->replaceAllUsesWith(Sel);
  LLVM_DEBUG(dbgs() << "INDVARS: Simplified rem: " << *Rem << '\n');
  ++NumElimRem;
  Changed = true;
  DeadInsts.emplace_back(Rem);
}

/// SimplifyIVUsers helper for eliminating useless remainder operations
/// operating on an induction variable or replacing srem by urem.
void SimplifyIndvar::simplifyIVRemainder(BinaryOperator *Rem, Value *IVOperand,
                                         bool IsSigned) {
  auto *NValue = Rem->getOperand(0);
  auto *DValue = Rem->getOperand(1);
  // We're only interested in the case where we know something about
  // the numerator, unless it is a srem, because we want to replace srem by urem
  // in general.
  bool UsedAsNumerator = IVOperand == NValue;
  if (!UsedAsNumerator && !IsSigned)
    return;

  const SCEV *N = SE->getSCEV(NValue);

  // Simplify unnecessary loops away.
  const Loop *ICmpLoop = LI->getLoopFor(Rem->getParent());
  N = SE->getSCEVAtScope(N, ICmpLoop);

  bool IsNumeratorNonNegative = !IsSigned || SE->isKnownNonNegative(N);

  // Do not proceed if the Numerator may be negative
  if (!IsNumeratorNonNegative)
    return;

  const SCEV *D = SE->getSCEV(DValue);
  D = SE->getSCEVAtScope(D, ICmpLoop);

  if (UsedAsNumerator) {
    auto LT = IsSigned ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_ULT;
    if (SE->isKnownPredicate(LT, N, D)) {
      replaceRemWithNumerator(Rem);
      return;
    }

    auto *T = Rem->getType();
    const auto *NLessOne = SE->getMinusSCEV(N, SE->getOne(T));
    if (SE->isKnownPredicate(LT, NLessOne, D)) {
      replaceRemWithNumeratorOrZero(Rem);
      return;
    }
  }

  // Try to replace SRem with URem, if both N and D are known non-negative.
  // Since we had already check N, we only need to check D now
  if (!IsSigned || !SE->isKnownNonNegative(D))
    return;

  replaceSRemWithURem(Rem);
}

bool SimplifyIndvar::eliminateOverflowIntrinsic(CallInst *CI) {
  auto *F = CI->getCalledFunction();
  if (!F)
    return false;

  typedef const SCEV *(ScalarEvolution::*OperationFunctionTy)(
      const SCEV *, const SCEV *, SCEV::NoWrapFlags, unsigned);
  typedef const SCEV *(ScalarEvolution::*ExtensionFunctionTy)(
      const SCEV *, Type *, unsigned);

  OperationFunctionTy Operation;
  ExtensionFunctionTy Extension;

  Instruction::BinaryOps RawOp;

  // We always have exactly one of nsw or nuw.  If NoSignedOverflow is false, we
  // have nuw.
  bool NoSignedOverflow;

  switch (F->getIntrinsicID()) {
  default:
    return false;

  case Intrinsic::sadd_with_overflow:
    Operation = &ScalarEvolution::getAddExpr;
    Extension = &ScalarEvolution::getSignExtendExpr;
    RawOp = Instruction::Add;
    NoSignedOverflow = true;
    break;

  case Intrinsic::uadd_with_overflow:
    Operation = &ScalarEvolution::getAddExpr;
    Extension = &ScalarEvolution::getZeroExtendExpr;
    RawOp = Instruction::Add;
    NoSignedOverflow = false;
    break;

  case Intrinsic::ssub_with_overflow:
    Operation = &ScalarEvolution::getMinusSCEV;
    Extension = &ScalarEvolution::getSignExtendExpr;
    RawOp = Instruction::Sub;
    NoSignedOverflow = true;
    break;

  case Intrinsic::usub_with_overflow:
    Operation = &ScalarEvolution::getMinusSCEV;
    Extension = &ScalarEvolution::getZeroExtendExpr;
    RawOp = Instruction::Sub;
    NoSignedOverflow = false;
    break;
  }

  const SCEV *LHS = SE->getSCEV(CI->getArgOperand(0));
  const SCEV *RHS = SE->getSCEV(CI->getArgOperand(1));

  auto *NarrowTy = cast<IntegerType>(LHS->getType());
  auto *WideTy =
    IntegerType::get(NarrowTy->getContext(), NarrowTy->getBitWidth() * 2);

  const SCEV *A =
      (SE->*Extension)((SE->*Operation)(LHS, RHS, SCEV::FlagAnyWrap, 0),
                       WideTy, 0);
  const SCEV *B =
      (SE->*Operation)((SE->*Extension)(LHS, WideTy, 0),
                       (SE->*Extension)(RHS, WideTy, 0), SCEV::FlagAnyWrap, 0);

  if (A != B)
    return false;

  // Proved no overflow, nuke the overflow check and, if possible, the overflow
  // intrinsic as well.

  BinaryOperator *NewResult = BinaryOperator::Create(
      RawOp, CI->getArgOperand(0), CI->getArgOperand(1), "", CI);

  if (NoSignedOverflow)
    NewResult->setHasNoSignedWrap(true);
  else
    NewResult->setHasNoUnsignedWrap(true);

  SmallVector<ExtractValueInst *, 4> ToDelete;

  for (auto *U : CI->users()) {
    if (auto *EVI = dyn_cast<ExtractValueInst>(U)) {
      if (EVI->getIndices()[0] == 1)
        EVI->replaceAllUsesWith(ConstantInt::getFalse(CI->getContext()));
      else {
        assert(EVI->getIndices()[0] == 0 && "Only two possibilities!");
        EVI->replaceAllUsesWith(NewResult);
      }
      ToDelete.push_back(EVI);
    }
  }

  for (auto *EVI : ToDelete)
    EVI->eraseFromParent();

  if (CI->use_empty())
    CI->eraseFromParent();

  return true;
}

bool SimplifyIndvar::eliminateTrunc(TruncInst *TI) {
  // It is always legal to replace
  //   icmp <pred> i32 trunc(iv), n
  // with
  //   icmp <pred> i64 sext(trunc(iv)), sext(n), if pred is signed predicate.
  // Or with
  //   icmp <pred> i64 zext(trunc(iv)), zext(n), if pred is unsigned predicate.
  // Or with either of these if pred is an equality predicate.
  //
  // If we can prove that iv == sext(trunc(iv)) or iv == zext(trunc(iv)) for
  // every comparison which uses trunc, it means that we can replace each of
  // them with comparison of iv against sext/zext(n). We no longer need trunc
  // after that.
  //
  // TODO: Should we do this if we can widen *some* comparisons, but not all
  // of them? Sometimes it is enough to enable other optimizations, but the
  // trunc instruction will stay in the loop.
  Value *IV = TI->getOperand(0);
  Type *IVTy = IV->getType();
  const SCEV *IVSCEV = SE->getSCEV(IV);
  const SCEV *TISCEV = SE->getSCEV(TI);

  // Check if iv == zext(trunc(iv)) and if iv == sext(trunc(iv)). If so, we can
  // get rid of trunc
  bool DoesSExtCollapse = false;
  bool DoesZExtCollapse = false;
  if (IVSCEV == SE->getSignExtendExpr(TISCEV, IVTy))
    DoesSExtCollapse = true;
  if (IVSCEV == SE->getZeroExtendExpr(TISCEV, IVTy))
    DoesZExtCollapse = true;

  // If neither sext nor zext does collapse, it is not profitable to do any
  // transform. Bail.
  if (!DoesSExtCollapse && !DoesZExtCollapse)
    return false;

  // Collect users of the trunc that look like comparisons against invariants.
  // Bail if we find something different.
  SmallVector<ICmpInst *, 4> ICmpUsers;
  for (auto *U : TI->users()) {
    // We don't care about users in unreachable blocks.
    if (isa<Instruction>(U) &&
        !DT->isReachableFromEntry(cast<Instruction>(U)->getParent()))
      continue;
    if (ICmpInst *ICI = dyn_cast<ICmpInst>(U)) {
      if (ICI->getOperand(0) == TI && L->isLoopInvariant(ICI->getOperand(1))) {
        assert(L->contains(ICI->getParent()) && "LCSSA form broken?");
        // If we cannot get rid of trunc, bail.
        if (ICI->isSigned() && !DoesSExtCollapse)
          return false;
        if (ICI->isUnsigned() && !DoesZExtCollapse)
          return false;
        // For equality, either signed or unsigned works.
        ICmpUsers.push_back(ICI);
      } else
        return false;
    } else
      return false;
  }

  auto CanUseZExt = [&](ICmpInst *ICI) {
    // Unsigned comparison can be widened as unsigned.
    if (ICI->isUnsigned())
      return true;
    // Is it profitable to do zext?
    if (!DoesZExtCollapse)
      return false;
    // For equality, we can safely zext both parts.
    if (ICI->isEquality())
      return true;
    // Otherwise we can only use zext when comparing two non-negative or two
    // negative values. But in practice, we will never pass DoesZExtCollapse
    // check for a negative value, because zext(trunc(x)) is non-negative. So
    // it only make sense to check for non-negativity here.
    const SCEV *SCEVOP1 = SE->getSCEV(ICI->getOperand(0));
    const SCEV *SCEVOP2 = SE->getSCEV(ICI->getOperand(1));
    return SE->isKnownNonNegative(SCEVOP1) && SE->isKnownNonNegative(SCEVOP2);
  };
  // Replace all comparisons against trunc with comparisons against IV.
  for (auto *ICI : ICmpUsers) {
    auto *Op1 = ICI->getOperand(1);
    Instruction *Ext = nullptr;
    // For signed/unsigned predicate, replace the old comparison with comparison
    // of immediate IV against sext/zext of the invariant argument. If we can
    // use either sext or zext (i.e. we are dealing with equality predicate),
    // then prefer zext as a more canonical form.
    // TODO: If we see a signed comparison which can be turned into unsigned,
    // we can do it here for canonicalization purposes.
    ICmpInst::Predicate Pred = ICI->getPredicate();
    if (CanUseZExt(ICI)) {
      assert(DoesZExtCollapse && "Unprofitable zext?");
      Ext = new ZExtInst(Op1, IVTy, "zext", ICI);
      Pred = ICmpInst::getUnsignedPredicate(Pred);
    } else {
      assert(DoesSExtCollapse && "Unprofitable sext?");
      Ext = new SExtInst(Op1, IVTy, "sext", ICI);
      assert(Pred == ICmpInst::getSignedPredicate(Pred) && "Must be signed!");
    }
    bool Changed;
    L->makeLoopInvariant(Ext, Changed);
    (void)Changed;
    ICmpInst *NewICI = new ICmpInst(ICI, Pred, IV, Ext);
    ICI->replaceAllUsesWith(NewICI);
    DeadInsts.emplace_back(ICI);
  }

  // Trunc no longer needed.
  TI->replaceAllUsesWith(UndefValue::get(TI->getType()));
  DeadInsts.emplace_back(TI);
  return true;
}

/// Eliminate an operation that consumes a simple IV and has no observable
/// side-effect given the range of IV values.  IVOperand is guaranteed SCEVable,
/// but UseInst may not be.
bool SimplifyIndvar::eliminateIVUser(Instruction *UseInst,
                                     Instruction *IVOperand) {
  if (ICmpInst *ICmp = dyn_cast<ICmpInst>(UseInst)) {
    eliminateIVComparison(ICmp, IVOperand);
    return true;
  }
  if (BinaryOperator *Bin = dyn_cast<BinaryOperator>(UseInst)) {
    bool IsSRem = Bin->getOpcode() == Instruction::SRem;
    if (IsSRem || Bin->getOpcode() == Instruction::URem) {
      simplifyIVRemainder(Bin, IVOperand, IsSRem);
      return true;
    }

    if (Bin->getOpcode() == Instruction::SDiv)
      return eliminateSDiv(Bin);
  }

  if (auto *CI = dyn_cast<CallInst>(UseInst))
    if (eliminateOverflowIntrinsic(CI))
      return true;

  if (auto *TI = dyn_cast<TruncInst>(UseInst))
    if (eliminateTrunc(TI))
      return true;

  if (eliminateIdentitySCEV(UseInst, IVOperand))
    return true;

  return false;
}

static Instruction *GetLoopInvariantInsertPosition(Loop *L, Instruction *Hint) {
  if (auto *BB = L->getLoopPreheader())
    return BB->getTerminator();

  return Hint;
}

/// Replace the UseInst with a constant if possible.
bool SimplifyIndvar::replaceIVUserWithLoopInvariant(Instruction *I) {
  if (!SE->isSCEVable(I->getType()))
    return false;

  // Get the symbolic expression for this instruction.
  const SCEV *S = SE->getSCEV(I);

  if (!SE->isLoopInvariant(S, L))
    return false;

  // Do not generate something ridiculous even if S is loop invariant.
  if (Rewriter.isHighCostExpansion(S, L, I))
    return false;

  auto *IP = GetLoopInvariantInsertPosition(L, I);
  auto *Invariant = Rewriter.expandCodeFor(S, I->getType(), IP);

  I->replaceAllUsesWith(Invariant);
  LLVM_DEBUG(dbgs() << "INDVARS: Replace IV user: " << *I
                    << " with loop invariant: " << *S << '\n');
  ++NumFoldedUser;
  Changed = true;
  DeadInsts.emplace_back(I);
  return true;
}

/// Eliminate any operation that SCEV can prove is an identity function.
bool SimplifyIndvar::eliminateIdentitySCEV(Instruction *UseInst,
                                           Instruction *IVOperand) {
  if (!SE->isSCEVable(UseInst->getType()) ||
      (UseInst->getType() != IVOperand->getType()) ||
      (SE->getSCEV(UseInst) != SE->getSCEV(IVOperand)))
    return false;

  // getSCEV(X) == getSCEV(Y) does not guarantee that X and Y are related in the
  // dominator tree, even if X is an operand to Y.  For instance, in
  //
  //     %iv = phi i32 {0,+,1}
  //     br %cond, label %left, label %merge
  //
  //   left:
  //     %X = add i32 %iv, 0
  //     br label %merge
  //
  //   merge:
  //     %M = phi (%X, %iv)
  //
  // getSCEV(%M) == getSCEV(%X) == {0,+,1}, but %X does not dominate %M, and
  // %M.replaceAllUsesWith(%X) would be incorrect.

  if (isa<PHINode>(UseInst))
    // If UseInst is not a PHI node then we know that IVOperand dominates
    // UseInst directly from the legality of SSA.
    if (!DT || !DT->dominates(IVOperand, UseInst))
      return false;

  if (!LI->replacementPreservesLCSSAForm(UseInst, IVOperand))
    return false;

  LLVM_DEBUG(dbgs() << "INDVARS: Eliminated identity: " << *UseInst << '\n');

  UseInst->replaceAllUsesWith(IVOperand);
  ++NumElimIdentity;
  Changed = true;
  DeadInsts.emplace_back(UseInst);
  return true;
}

/// Annotate BO with nsw / nuw if it provably does not signed-overflow /
/// unsigned-overflow.  Returns true if anything changed, false otherwise.
bool SimplifyIndvar::strengthenOverflowingOperation(BinaryOperator *BO,
                                                    Value *IVOperand) {

  // Fastpath: we don't have any work to do if `BO` is `nuw` and `nsw`.
  if (BO->hasNoUnsignedWrap() && BO->hasNoSignedWrap())
    return false;

  const SCEV *(ScalarEvolution::*GetExprForBO)(const SCEV *, const SCEV *,
                                               SCEV::NoWrapFlags, unsigned);
  switch (BO->getOpcode()) {
  default:
    return false;

  case Instruction::Add:
    GetExprForBO = &ScalarEvolution::getAddExpr;
    break;

  case Instruction::Sub:
    GetExprForBO = &ScalarEvolution::getMinusSCEV;
    break;

  case Instruction::Mul:
    GetExprForBO = &ScalarEvolution::getMulExpr;
    break;
  }

  unsigned BitWidth = cast<IntegerType>(BO->getType())->getBitWidth();
  Type *WideTy = IntegerType::get(BO->getContext(), BitWidth * 2);
  const SCEV *LHS = SE->getSCEV(BO->getOperand(0));
  const SCEV *RHS = SE->getSCEV(BO->getOperand(1));

  bool Changed = false;

  if (!BO->hasNoUnsignedWrap()) {
    const SCEV *ExtendAfterOp = SE->getZeroExtendExpr(SE->getSCEV(BO), WideTy);
    const SCEV *OpAfterExtend = (SE->*GetExprForBO)(
      SE->getZeroExtendExpr(LHS, WideTy), SE->getZeroExtendExpr(RHS, WideTy),
      SCEV::FlagAnyWrap, 0u);
    if (ExtendAfterOp == OpAfterExtend) {
      BO->setHasNoUnsignedWrap();
      SE->forgetValue(BO);
      Changed = true;
    }
  }

  if (!BO->hasNoSignedWrap()) {
    const SCEV *ExtendAfterOp = SE->getSignExtendExpr(SE->getSCEV(BO), WideTy);
    const SCEV *OpAfterExtend = (SE->*GetExprForBO)(
      SE->getSignExtendExpr(LHS, WideTy), SE->getSignExtendExpr(RHS, WideTy),
      SCEV::FlagAnyWrap, 0u);
    if (ExtendAfterOp == OpAfterExtend) {
      BO->setHasNoSignedWrap();
      SE->forgetValue(BO);
      Changed = true;
    }
  }

  return Changed;
}

/// Annotate the Shr in (X << IVOperand) >> C as exact using the
/// information from the IV's range. Returns true if anything changed, false
/// otherwise.
bool SimplifyIndvar::strengthenRightShift(BinaryOperator *BO,
                                          Value *IVOperand) {
  using namespace llvm::PatternMatch;

  if (BO->getOpcode() == Instruction::Shl) {
    bool Changed = false;
    ConstantRange IVRange = SE->getUnsignedRange(SE->getSCEV(IVOperand));
    for (auto *U : BO->users()) {
      const APInt *C;
      if (match(U,
                m_AShr(m_Shl(m_Value(), m_Specific(IVOperand)), m_APInt(C))) ||
          match(U,
                m_LShr(m_Shl(m_Value(), m_Specific(IVOperand)), m_APInt(C)))) {
        BinaryOperator *Shr = cast<BinaryOperator>(U);
        if (!Shr->isExact() && IVRange.getUnsignedMin().uge(*C)) {
          Shr->setIsExact(true);
          Changed = true;
        }
      }
    }
    return Changed;
  }

  return false;
}

/// Add all uses of Def to the current IV's worklist.
static void pushIVUsers(
  Instruction *Def, Loop *L,
  SmallPtrSet<Instruction*,16> &Simplified,
  SmallVectorImpl< std::pair<Instruction*,Instruction*> > &SimpleIVUsers) {

  for (User *U : Def->users()) {
    Instruction *UI = cast<Instruction>(U);

    // Avoid infinite or exponential worklist processing.
    // Also ensure unique worklist users.
    // If Def is a LoopPhi, it may not be in the Simplified set, so check for
    // self edges first.
    if (UI == Def)
      continue;

    // Only change the current Loop, do not change the other parts (e.g. other
    // Loops).
    if (!L->contains(UI))
      continue;

    // Do not push the same instruction more than once.
    if (!Simplified.insert(UI).second)
      continue;

    SimpleIVUsers.push_back(std::make_pair(UI, Def));
  }
}

/// Return true if this instruction generates a simple SCEV
/// expression in terms of that IV.
///
/// This is similar to IVUsers' isInteresting() but processes each instruction
/// non-recursively when the operand is already known to be a simpleIVUser.
///
static bool isSimpleIVUser(Instruction *I, const Loop *L, ScalarEvolution *SE) {
  if (!SE->isSCEVable(I->getType()))
    return false;

  // Get the symbolic expression for this instruction.
  const SCEV *S = SE->getSCEV(I);

  // Only consider affine recurrences.
  const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(S);
  if (AR && AR->getLoop() == L)
    return true;

  return false;
}

/// Iteratively perform simplification on a worklist of users
/// of the specified induction variable. Each successive simplification may push
/// more users which may themselves be candidates for simplification.
///
/// This algorithm does not require IVUsers analysis. Instead, it simplifies
/// instructions in-place during analysis. Rather than rewriting induction
/// variables bottom-up from their users, it transforms a chain of IVUsers
/// top-down, updating the IR only when it encounters a clear optimization
/// opportunity.
///
/// Once DisableIVRewrite is default, LSR will be the only client of IVUsers.
///
void SimplifyIndvar::simplifyUsers(PHINode *CurrIV, IVVisitor *V) {
  if (!SE->isSCEVable(CurrIV->getType()))
    return;

  // Instructions processed by SimplifyIndvar for CurrIV.
  SmallPtrSet<Instruction*,16> Simplified;

  // Use-def pairs if IV users waiting to be processed for CurrIV.
  SmallVector<std::pair<Instruction*, Instruction*>, 8> SimpleIVUsers;

  // Push users of the current LoopPhi. In rare cases, pushIVUsers may be
  // called multiple times for the same LoopPhi. This is the proper thing to
  // do for loop header phis that use each other.
  pushIVUsers(CurrIV, L, Simplified, SimpleIVUsers);

  while (!SimpleIVUsers.empty()) {
    std::pair<Instruction*, Instruction*> UseOper =
      SimpleIVUsers.pop_back_val();
    Instruction *UseInst = UseOper.first;

    // If a user of the IndVar is trivially dead, we prefer just to mark it dead
    // rather than try to do some complex analysis or transformation (such as
    // widening) basing on it.
    // TODO: Propagate TLI and pass it here to handle more cases.
    if (isInstructionTriviallyDead(UseInst, /* TLI */ nullptr)) {
      DeadInsts.emplace_back(UseInst);
      continue;
    }

    // Bypass back edges to avoid extra work.
    if (UseInst == CurrIV) continue;

    // Try to replace UseInst with a loop invariant before any other
    // simplifications.
    if (replaceIVUserWithLoopInvariant(UseInst))
      continue;

    Instruction *IVOperand = UseOper.second;
    for (unsigned N = 0; IVOperand; ++N) {
      assert(N <= Simplified.size() && "runaway iteration");

      Value *NewOper = foldIVUser(UseInst, IVOperand);
      if (!NewOper)
        break; // done folding
      IVOperand = dyn_cast<Instruction>(NewOper);
    }
    if (!IVOperand)
      continue;

    if (eliminateIVUser(UseInst, IVOperand)) {
      pushIVUsers(IVOperand, L, Simplified, SimpleIVUsers);
      continue;
    }

    if (BinaryOperator *BO = dyn_cast<BinaryOperator>(UseInst)) {
      if ((isa<OverflowingBinaryOperator>(BO) &&
           strengthenOverflowingOperation(BO, IVOperand)) ||
          (isa<ShlOperator>(BO) && strengthenRightShift(BO, IVOperand))) {
        // re-queue uses of the now modified binary operator and fall
        // through to the checks that remain.
        pushIVUsers(IVOperand, L, Simplified, SimpleIVUsers);
      }
    }

    CastInst *Cast = dyn_cast<CastInst>(UseInst);
    if (V && Cast) {
      V->visitCast(Cast);
      continue;
    }
    if (isSimpleIVUser(UseInst, L, SE)) {
      pushIVUsers(UseInst, L, Simplified, SimpleIVUsers);
    }
  }
}

namespace llvm {

void IVVisitor::anchor() { }

/// Simplify instructions that use this induction variable
/// by using ScalarEvolution to analyze the IV's recurrence.
bool simplifyUsersOfIV(PHINode *CurrIV, ScalarEvolution *SE, DominatorTree *DT,
                       LoopInfo *LI, SmallVectorImpl<WeakTrackingVH> &Dead,
                       SCEVExpander &Rewriter, IVVisitor *V) {
  SimplifyIndvar SIV(LI->getLoopFor(CurrIV->getParent()), SE, DT, LI, Rewriter,
                     Dead);
  SIV.simplifyUsers(CurrIV, V);
  return SIV.hasChanged();
}

/// Simplify users of induction variables within this
/// loop. This does not actually change or add IVs.
bool simplifyLoopIVs(Loop *L, ScalarEvolution *SE, DominatorTree *DT,
                     LoopInfo *LI, SmallVectorImpl<WeakTrackingVH> &Dead) {
  SCEVExpander Rewriter(*SE, SE->getDataLayout(), "indvars");
#ifndef NDEBUG
  Rewriter.setDebugType(DEBUG_TYPE);
#endif
  bool Changed = false;
  for (BasicBlock::iterator I = L->getHeader()->begin(); isa<PHINode>(I); ++I) {
    Changed |= simplifyUsersOfIV(cast<PHINode>(I), SE, DT, LI, Dead, Rewriter);
  }
  return Changed;
}

} // namespace llvm
