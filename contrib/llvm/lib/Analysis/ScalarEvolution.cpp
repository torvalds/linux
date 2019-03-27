//===- ScalarEvolution.cpp - Scalar Evolution Analysis --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of the scalar evolution analysis
// engine, which is used primarily to analyze expressions involving induction
// variables in loops.
//
// There are several aspects to this library.  First is the representation of
// scalar expressions, which are represented as subclasses of the SCEV class.
// These classes are used to represent certain types of subexpressions that we
// can handle. We only create one SCEV of a particular shape, so
// pointer-comparisons for equality are legal.
//
// One important aspect of the SCEV objects is that they are never cyclic, even
// if there is a cycle in the dataflow for an expression (ie, a PHI node).  If
// the PHI node is one of the idioms that we can represent (e.g., a polynomial
// recurrence) then we represent it directly as a recurrence node, otherwise we
// represent it as a SCEVUnknown node.
//
// In addition to being able to represent expressions of various types, we also
// have folders that are used to build the *canonical* representation for a
// particular expression.  These folders are capable of using a variety of
// rewrite rules to simplify the expressions.
//
// Once the folders are defined, we can implement the more interesting
// higher-level code, such as the code that recognizes PHI nodes of various
// types, computes the execution count of a loop, etc.
//
// TODO: We should use these routines and value representations to implement
// dependence analysis!
//
//===----------------------------------------------------------------------===//
//
// There are several good references for the techniques used in this analysis.
//
//  Chains of recurrences -- a method to expedite the evaluation
//  of closed-form functions
//  Olaf Bachmann, Paul S. Wang, Eugene V. Zima
//
//  On computational properties of chains of recurrences
//  Eugene V. Zima
//
//  Symbolic Evaluation of Chains of Recurrences for Loop Optimization
//  Robert A. van Engelen
//
//  Efficient Symbolic Analysis for Optimizing Compilers
//  Robert A. van Engelen
//
//  Using the chains of recurrences algebra for data dependence testing and
//  induction variable substitution
//  MS Thesis, Johnie Birch
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "scalar-evolution"

STATISTIC(NumArrayLenItCounts,
          "Number of trip counts computed with array length");
STATISTIC(NumTripCountsComputed,
          "Number of loops with predictable loop counts");
STATISTIC(NumTripCountsNotComputed,
          "Number of loops without predictable loop counts");
STATISTIC(NumBruteForceTripCountsComputed,
          "Number of loops with trip counts computed by force");

static cl::opt<unsigned>
MaxBruteForceIterations("scalar-evolution-max-iterations", cl::ReallyHidden,
                        cl::desc("Maximum number of iterations SCEV will "
                                 "symbolically execute a constant "
                                 "derived loop"),
                        cl::init(100));

// FIXME: Enable this with EXPENSIVE_CHECKS when the test suite is clean.
static cl::opt<bool> VerifySCEV(
    "verify-scev", cl::Hidden,
    cl::desc("Verify ScalarEvolution's backedge taken counts (slow)"));
static cl::opt<bool>
    VerifySCEVMap("verify-scev-maps", cl::Hidden,
                  cl::desc("Verify no dangling value in ScalarEvolution's "
                           "ExprValueMap (slow)"));

static cl::opt<bool> VerifyIR(
    "scev-verify-ir", cl::Hidden,
    cl::desc("Verify IR correctness when making sensitive SCEV queries (slow)"),
    cl::init(false));

static cl::opt<unsigned> MulOpsInlineThreshold(
    "scev-mulops-inline-threshold", cl::Hidden,
    cl::desc("Threshold for inlining multiplication operands into a SCEV"),
    cl::init(32));

static cl::opt<unsigned> AddOpsInlineThreshold(
    "scev-addops-inline-threshold", cl::Hidden,
    cl::desc("Threshold for inlining addition operands into a SCEV"),
    cl::init(500));

static cl::opt<unsigned> MaxSCEVCompareDepth(
    "scalar-evolution-max-scev-compare-depth", cl::Hidden,
    cl::desc("Maximum depth of recursive SCEV complexity comparisons"),
    cl::init(32));

static cl::opt<unsigned> MaxSCEVOperationsImplicationDepth(
    "scalar-evolution-max-scev-operations-implication-depth", cl::Hidden,
    cl::desc("Maximum depth of recursive SCEV operations implication analysis"),
    cl::init(2));

static cl::opt<unsigned> MaxValueCompareDepth(
    "scalar-evolution-max-value-compare-depth", cl::Hidden,
    cl::desc("Maximum depth of recursive value complexity comparisons"),
    cl::init(2));

static cl::opt<unsigned>
    MaxArithDepth("scalar-evolution-max-arith-depth", cl::Hidden,
                  cl::desc("Maximum depth of recursive arithmetics"),
                  cl::init(32));

static cl::opt<unsigned> MaxConstantEvolvingDepth(
    "scalar-evolution-max-constant-evolving-depth", cl::Hidden,
    cl::desc("Maximum depth of recursive constant evolving"), cl::init(32));

static cl::opt<unsigned>
    MaxExtDepth("scalar-evolution-max-ext-depth", cl::Hidden,
                cl::desc("Maximum depth of recursive SExt/ZExt"),
                cl::init(8));

static cl::opt<unsigned>
    MaxAddRecSize("scalar-evolution-max-add-rec-size", cl::Hidden,
                  cl::desc("Max coefficients in AddRec during evolving"),
                  cl::init(8));

//===----------------------------------------------------------------------===//
//                           SCEV class definitions
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Implementation of the SCEV class.
//

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void SCEV::dump() const {
  print(dbgs());
  dbgs() << '\n';
}
#endif

void SCEV::print(raw_ostream &OS) const {
  switch (static_cast<SCEVTypes>(getSCEVType())) {
  case scConstant:
    cast<SCEVConstant>(this)->getValue()->printAsOperand(OS, false);
    return;
  case scTruncate: {
    const SCEVTruncateExpr *Trunc = cast<SCEVTruncateExpr>(this);
    const SCEV *Op = Trunc->getOperand();
    OS << "(trunc " << *Op->getType() << " " << *Op << " to "
       << *Trunc->getType() << ")";
    return;
  }
  case scZeroExtend: {
    const SCEVZeroExtendExpr *ZExt = cast<SCEVZeroExtendExpr>(this);
    const SCEV *Op = ZExt->getOperand();
    OS << "(zext " << *Op->getType() << " " << *Op << " to "
       << *ZExt->getType() << ")";
    return;
  }
  case scSignExtend: {
    const SCEVSignExtendExpr *SExt = cast<SCEVSignExtendExpr>(this);
    const SCEV *Op = SExt->getOperand();
    OS << "(sext " << *Op->getType() << " " << *Op << " to "
       << *SExt->getType() << ")";
    return;
  }
  case scAddRecExpr: {
    const SCEVAddRecExpr *AR = cast<SCEVAddRecExpr>(this);
    OS << "{" << *AR->getOperand(0);
    for (unsigned i = 1, e = AR->getNumOperands(); i != e; ++i)
      OS << ",+," << *AR->getOperand(i);
    OS << "}<";
    if (AR->hasNoUnsignedWrap())
      OS << "nuw><";
    if (AR->hasNoSignedWrap())
      OS << "nsw><";
    if (AR->hasNoSelfWrap() &&
        !AR->getNoWrapFlags((NoWrapFlags)(FlagNUW | FlagNSW)))
      OS << "nw><";
    AR->getLoop()->getHeader()->printAsOperand(OS, /*PrintType=*/false);
    OS << ">";
    return;
  }
  case scAddExpr:
  case scMulExpr:
  case scUMaxExpr:
  case scSMaxExpr: {
    const SCEVNAryExpr *NAry = cast<SCEVNAryExpr>(this);
    const char *OpStr = nullptr;
    switch (NAry->getSCEVType()) {
    case scAddExpr: OpStr = " + "; break;
    case scMulExpr: OpStr = " * "; break;
    case scUMaxExpr: OpStr = " umax "; break;
    case scSMaxExpr: OpStr = " smax "; break;
    }
    OS << "(";
    for (SCEVNAryExpr::op_iterator I = NAry->op_begin(), E = NAry->op_end();
         I != E; ++I) {
      OS << **I;
      if (std::next(I) != E)
        OS << OpStr;
    }
    OS << ")";
    switch (NAry->getSCEVType()) {
    case scAddExpr:
    case scMulExpr:
      if (NAry->hasNoUnsignedWrap())
        OS << "<nuw>";
      if (NAry->hasNoSignedWrap())
        OS << "<nsw>";
    }
    return;
  }
  case scUDivExpr: {
    const SCEVUDivExpr *UDiv = cast<SCEVUDivExpr>(this);
    OS << "(" << *UDiv->getLHS() << " /u " << *UDiv->getRHS() << ")";
    return;
  }
  case scUnknown: {
    const SCEVUnknown *U = cast<SCEVUnknown>(this);
    Type *AllocTy;
    if (U->isSizeOf(AllocTy)) {
      OS << "sizeof(" << *AllocTy << ")";
      return;
    }
    if (U->isAlignOf(AllocTy)) {
      OS << "alignof(" << *AllocTy << ")";
      return;
    }

    Type *CTy;
    Constant *FieldNo;
    if (U->isOffsetOf(CTy, FieldNo)) {
      OS << "offsetof(" << *CTy << ", ";
      FieldNo->printAsOperand(OS, false);
      OS << ")";
      return;
    }

    // Otherwise just print it normally.
    U->getValue()->printAsOperand(OS, false);
    return;
  }
  case scCouldNotCompute:
    OS << "***COULDNOTCOMPUTE***";
    return;
  }
  llvm_unreachable("Unknown SCEV kind!");
}

Type *SCEV::getType() const {
  switch (static_cast<SCEVTypes>(getSCEVType())) {
  case scConstant:
    return cast<SCEVConstant>(this)->getType();
  case scTruncate:
  case scZeroExtend:
  case scSignExtend:
    return cast<SCEVCastExpr>(this)->getType();
  case scAddRecExpr:
  case scMulExpr:
  case scUMaxExpr:
  case scSMaxExpr:
    return cast<SCEVNAryExpr>(this)->getType();
  case scAddExpr:
    return cast<SCEVAddExpr>(this)->getType();
  case scUDivExpr:
    return cast<SCEVUDivExpr>(this)->getType();
  case scUnknown:
    return cast<SCEVUnknown>(this)->getType();
  case scCouldNotCompute:
    llvm_unreachable("Attempt to use a SCEVCouldNotCompute object!");
  }
  llvm_unreachable("Unknown SCEV kind!");
}

bool SCEV::isZero() const {
  if (const SCEVConstant *SC = dyn_cast<SCEVConstant>(this))
    return SC->getValue()->isZero();
  return false;
}

bool SCEV::isOne() const {
  if (const SCEVConstant *SC = dyn_cast<SCEVConstant>(this))
    return SC->getValue()->isOne();
  return false;
}

bool SCEV::isAllOnesValue() const {
  if (const SCEVConstant *SC = dyn_cast<SCEVConstant>(this))
    return SC->getValue()->isMinusOne();
  return false;
}

bool SCEV::isNonConstantNegative() const {
  const SCEVMulExpr *Mul = dyn_cast<SCEVMulExpr>(this);
  if (!Mul) return false;

  // If there is a constant factor, it will be first.
  const SCEVConstant *SC = dyn_cast<SCEVConstant>(Mul->getOperand(0));
  if (!SC) return false;

  // Return true if the value is negative, this matches things like (-42 * V).
  return SC->getAPInt().isNegative();
}

SCEVCouldNotCompute::SCEVCouldNotCompute() :
  SCEV(FoldingSetNodeIDRef(), scCouldNotCompute) {}

bool SCEVCouldNotCompute::classof(const SCEV *S) {
  return S->getSCEVType() == scCouldNotCompute;
}

const SCEV *ScalarEvolution::getConstant(ConstantInt *V) {
  FoldingSetNodeID ID;
  ID.AddInteger(scConstant);
  ID.AddPointer(V);
  void *IP = nullptr;
  if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP)) return S;
  SCEV *S = new (SCEVAllocator) SCEVConstant(ID.Intern(SCEVAllocator), V);
  UniqueSCEVs.InsertNode(S, IP);
  return S;
}

const SCEV *ScalarEvolution::getConstant(const APInt &Val) {
  return getConstant(ConstantInt::get(getContext(), Val));
}

const SCEV *
ScalarEvolution::getConstant(Type *Ty, uint64_t V, bool isSigned) {
  IntegerType *ITy = cast<IntegerType>(getEffectiveSCEVType(Ty));
  return getConstant(ConstantInt::get(ITy, V, isSigned));
}

SCEVCastExpr::SCEVCastExpr(const FoldingSetNodeIDRef ID,
                           unsigned SCEVTy, const SCEV *op, Type *ty)
  : SCEV(ID, SCEVTy), Op(op), Ty(ty) {}

SCEVTruncateExpr::SCEVTruncateExpr(const FoldingSetNodeIDRef ID,
                                   const SCEV *op, Type *ty)
  : SCEVCastExpr(ID, scTruncate, op, ty) {
  assert(Op->getType()->isIntOrPtrTy() && Ty->isIntOrPtrTy() &&
         "Cannot truncate non-integer value!");
}

SCEVZeroExtendExpr::SCEVZeroExtendExpr(const FoldingSetNodeIDRef ID,
                                       const SCEV *op, Type *ty)
  : SCEVCastExpr(ID, scZeroExtend, op, ty) {
  assert(Op->getType()->isIntOrPtrTy() && Ty->isIntOrPtrTy() &&
         "Cannot zero extend non-integer value!");
}

SCEVSignExtendExpr::SCEVSignExtendExpr(const FoldingSetNodeIDRef ID,
                                       const SCEV *op, Type *ty)
  : SCEVCastExpr(ID, scSignExtend, op, ty) {
  assert(Op->getType()->isIntOrPtrTy() && Ty->isIntOrPtrTy() &&
         "Cannot sign extend non-integer value!");
}

void SCEVUnknown::deleted() {
  // Clear this SCEVUnknown from various maps.
  SE->forgetMemoizedResults(this);

  // Remove this SCEVUnknown from the uniquing map.
  SE->UniqueSCEVs.RemoveNode(this);

  // Release the value.
  setValPtr(nullptr);
}

void SCEVUnknown::allUsesReplacedWith(Value *New) {
  // Remove this SCEVUnknown from the uniquing map.
  SE->UniqueSCEVs.RemoveNode(this);

  // Update this SCEVUnknown to point to the new value. This is needed
  // because there may still be outstanding SCEVs which still point to
  // this SCEVUnknown.
  setValPtr(New);
}

bool SCEVUnknown::isSizeOf(Type *&AllocTy) const {
  if (ConstantExpr *VCE = dyn_cast<ConstantExpr>(getValue()))
    if (VCE->getOpcode() == Instruction::PtrToInt)
      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(VCE->getOperand(0)))
        if (CE->getOpcode() == Instruction::GetElementPtr &&
            CE->getOperand(0)->isNullValue() &&
            CE->getNumOperands() == 2)
          if (ConstantInt *CI = dyn_cast<ConstantInt>(CE->getOperand(1)))
            if (CI->isOne()) {
              AllocTy = cast<PointerType>(CE->getOperand(0)->getType())
                                 ->getElementType();
              return true;
            }

  return false;
}

bool SCEVUnknown::isAlignOf(Type *&AllocTy) const {
  if (ConstantExpr *VCE = dyn_cast<ConstantExpr>(getValue()))
    if (VCE->getOpcode() == Instruction::PtrToInt)
      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(VCE->getOperand(0)))
        if (CE->getOpcode() == Instruction::GetElementPtr &&
            CE->getOperand(0)->isNullValue()) {
          Type *Ty =
            cast<PointerType>(CE->getOperand(0)->getType())->getElementType();
          if (StructType *STy = dyn_cast<StructType>(Ty))
            if (!STy->isPacked() &&
                CE->getNumOperands() == 3 &&
                CE->getOperand(1)->isNullValue()) {
              if (ConstantInt *CI = dyn_cast<ConstantInt>(CE->getOperand(2)))
                if (CI->isOne() &&
                    STy->getNumElements() == 2 &&
                    STy->getElementType(0)->isIntegerTy(1)) {
                  AllocTy = STy->getElementType(1);
                  return true;
                }
            }
        }

  return false;
}

bool SCEVUnknown::isOffsetOf(Type *&CTy, Constant *&FieldNo) const {
  if (ConstantExpr *VCE = dyn_cast<ConstantExpr>(getValue()))
    if (VCE->getOpcode() == Instruction::PtrToInt)
      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(VCE->getOperand(0)))
        if (CE->getOpcode() == Instruction::GetElementPtr &&
            CE->getNumOperands() == 3 &&
            CE->getOperand(0)->isNullValue() &&
            CE->getOperand(1)->isNullValue()) {
          Type *Ty =
            cast<PointerType>(CE->getOperand(0)->getType())->getElementType();
          // Ignore vector types here so that ScalarEvolutionExpander doesn't
          // emit getelementptrs that index into vectors.
          if (Ty->isStructTy() || Ty->isArrayTy()) {
            CTy = Ty;
            FieldNo = CE->getOperand(2);
            return true;
          }
        }

  return false;
}

//===----------------------------------------------------------------------===//
//                               SCEV Utilities
//===----------------------------------------------------------------------===//

/// Compare the two values \p LV and \p RV in terms of their "complexity" where
/// "complexity" is a partial (and somewhat ad-hoc) relation used to order
/// operands in SCEV expressions.  \p EqCache is a set of pairs of values that
/// have been previously deemed to be "equally complex" by this routine.  It is
/// intended to avoid exponential time complexity in cases like:
///
///   %a = f(%x, %y)
///   %b = f(%a, %a)
///   %c = f(%b, %b)
///
///   %d = f(%x, %y)
///   %e = f(%d, %d)
///   %f = f(%e, %e)
///
///   CompareValueComplexity(%f, %c)
///
/// Since we do not continue running this routine on expression trees once we
/// have seen unequal values, there is no need to track them in the cache.
static int
CompareValueComplexity(EquivalenceClasses<const Value *> &EqCacheValue,
                       const LoopInfo *const LI, Value *LV, Value *RV,
                       unsigned Depth) {
  if (Depth > MaxValueCompareDepth || EqCacheValue.isEquivalent(LV, RV))
    return 0;

  // Order pointer values after integer values. This helps SCEVExpander form
  // GEPs.
  bool LIsPointer = LV->getType()->isPointerTy(),
       RIsPointer = RV->getType()->isPointerTy();
  if (LIsPointer != RIsPointer)
    return (int)LIsPointer - (int)RIsPointer;

  // Compare getValueID values.
  unsigned LID = LV->getValueID(), RID = RV->getValueID();
  if (LID != RID)
    return (int)LID - (int)RID;

  // Sort arguments by their position.
  if (const auto *LA = dyn_cast<Argument>(LV)) {
    const auto *RA = cast<Argument>(RV);
    unsigned LArgNo = LA->getArgNo(), RArgNo = RA->getArgNo();
    return (int)LArgNo - (int)RArgNo;
  }

  if (const auto *LGV = dyn_cast<GlobalValue>(LV)) {
    const auto *RGV = cast<GlobalValue>(RV);

    const auto IsGVNameSemantic = [&](const GlobalValue *GV) {
      auto LT = GV->getLinkage();
      return !(GlobalValue::isPrivateLinkage(LT) ||
               GlobalValue::isInternalLinkage(LT));
    };

    // Use the names to distinguish the two values, but only if the
    // names are semantically important.
    if (IsGVNameSemantic(LGV) && IsGVNameSemantic(RGV))
      return LGV->getName().compare(RGV->getName());
  }

  // For instructions, compare their loop depth, and their operand count.  This
  // is pretty loose.
  if (const auto *LInst = dyn_cast<Instruction>(LV)) {
    const auto *RInst = cast<Instruction>(RV);

    // Compare loop depths.
    const BasicBlock *LParent = LInst->getParent(),
                     *RParent = RInst->getParent();
    if (LParent != RParent) {
      unsigned LDepth = LI->getLoopDepth(LParent),
               RDepth = LI->getLoopDepth(RParent);
      if (LDepth != RDepth)
        return (int)LDepth - (int)RDepth;
    }

    // Compare the number of operands.
    unsigned LNumOps = LInst->getNumOperands(),
             RNumOps = RInst->getNumOperands();
    if (LNumOps != RNumOps)
      return (int)LNumOps - (int)RNumOps;

    for (unsigned Idx : seq(0u, LNumOps)) {
      int Result =
          CompareValueComplexity(EqCacheValue, LI, LInst->getOperand(Idx),
                                 RInst->getOperand(Idx), Depth + 1);
      if (Result != 0)
        return Result;
    }
  }

  EqCacheValue.unionSets(LV, RV);
  return 0;
}

// Return negative, zero, or positive, if LHS is less than, equal to, or greater
// than RHS, respectively. A three-way result allows recursive comparisons to be
// more efficient.
static int CompareSCEVComplexity(
    EquivalenceClasses<const SCEV *> &EqCacheSCEV,
    EquivalenceClasses<const Value *> &EqCacheValue,
    const LoopInfo *const LI, const SCEV *LHS, const SCEV *RHS,
    DominatorTree &DT, unsigned Depth = 0) {
  // Fast-path: SCEVs are uniqued so we can do a quick equality check.
  if (LHS == RHS)
    return 0;

  // Primarily, sort the SCEVs by their getSCEVType().
  unsigned LType = LHS->getSCEVType(), RType = RHS->getSCEVType();
  if (LType != RType)
    return (int)LType - (int)RType;

  if (Depth > MaxSCEVCompareDepth || EqCacheSCEV.isEquivalent(LHS, RHS))
    return 0;
  // Aside from the getSCEVType() ordering, the particular ordering
  // isn't very important except that it's beneficial to be consistent,
  // so that (a + b) and (b + a) don't end up as different expressions.
  switch (static_cast<SCEVTypes>(LType)) {
  case scUnknown: {
    const SCEVUnknown *LU = cast<SCEVUnknown>(LHS);
    const SCEVUnknown *RU = cast<SCEVUnknown>(RHS);

    int X = CompareValueComplexity(EqCacheValue, LI, LU->getValue(),
                                   RU->getValue(), Depth + 1);
    if (X == 0)
      EqCacheSCEV.unionSets(LHS, RHS);
    return X;
  }

  case scConstant: {
    const SCEVConstant *LC = cast<SCEVConstant>(LHS);
    const SCEVConstant *RC = cast<SCEVConstant>(RHS);

    // Compare constant values.
    const APInt &LA = LC->getAPInt();
    const APInt &RA = RC->getAPInt();
    unsigned LBitWidth = LA.getBitWidth(), RBitWidth = RA.getBitWidth();
    if (LBitWidth != RBitWidth)
      return (int)LBitWidth - (int)RBitWidth;
    return LA.ult(RA) ? -1 : 1;
  }

  case scAddRecExpr: {
    const SCEVAddRecExpr *LA = cast<SCEVAddRecExpr>(LHS);
    const SCEVAddRecExpr *RA = cast<SCEVAddRecExpr>(RHS);

    // There is always a dominance between two recs that are used by one SCEV,
    // so we can safely sort recs by loop header dominance. We require such
    // order in getAddExpr.
    const Loop *LLoop = LA->getLoop(), *RLoop = RA->getLoop();
    if (LLoop != RLoop) {
      const BasicBlock *LHead = LLoop->getHeader(), *RHead = RLoop->getHeader();
      assert(LHead != RHead && "Two loops share the same header?");
      if (DT.dominates(LHead, RHead))
        return 1;
      else
        assert(DT.dominates(RHead, LHead) &&
               "No dominance between recurrences used by one SCEV?");
      return -1;
    }

    // Addrec complexity grows with operand count.
    unsigned LNumOps = LA->getNumOperands(), RNumOps = RA->getNumOperands();
    if (LNumOps != RNumOps)
      return (int)LNumOps - (int)RNumOps;

    // Lexicographically compare.
    for (unsigned i = 0; i != LNumOps; ++i) {
      int X = CompareSCEVComplexity(EqCacheSCEV, EqCacheValue, LI,
                                    LA->getOperand(i), RA->getOperand(i), DT,
                                    Depth + 1);
      if (X != 0)
        return X;
    }
    EqCacheSCEV.unionSets(LHS, RHS);
    return 0;
  }

  case scAddExpr:
  case scMulExpr:
  case scSMaxExpr:
  case scUMaxExpr: {
    const SCEVNAryExpr *LC = cast<SCEVNAryExpr>(LHS);
    const SCEVNAryExpr *RC = cast<SCEVNAryExpr>(RHS);

    // Lexicographically compare n-ary expressions.
    unsigned LNumOps = LC->getNumOperands(), RNumOps = RC->getNumOperands();
    if (LNumOps != RNumOps)
      return (int)LNumOps - (int)RNumOps;

    for (unsigned i = 0; i != LNumOps; ++i) {
      int X = CompareSCEVComplexity(EqCacheSCEV, EqCacheValue, LI,
                                    LC->getOperand(i), RC->getOperand(i), DT,
                                    Depth + 1);
      if (X != 0)
        return X;
    }
    EqCacheSCEV.unionSets(LHS, RHS);
    return 0;
  }

  case scUDivExpr: {
    const SCEVUDivExpr *LC = cast<SCEVUDivExpr>(LHS);
    const SCEVUDivExpr *RC = cast<SCEVUDivExpr>(RHS);

    // Lexicographically compare udiv expressions.
    int X = CompareSCEVComplexity(EqCacheSCEV, EqCacheValue, LI, LC->getLHS(),
                                  RC->getLHS(), DT, Depth + 1);
    if (X != 0)
      return X;
    X = CompareSCEVComplexity(EqCacheSCEV, EqCacheValue, LI, LC->getRHS(),
                              RC->getRHS(), DT, Depth + 1);
    if (X == 0)
      EqCacheSCEV.unionSets(LHS, RHS);
    return X;
  }

  case scTruncate:
  case scZeroExtend:
  case scSignExtend: {
    const SCEVCastExpr *LC = cast<SCEVCastExpr>(LHS);
    const SCEVCastExpr *RC = cast<SCEVCastExpr>(RHS);

    // Compare cast expressions by operand.
    int X = CompareSCEVComplexity(EqCacheSCEV, EqCacheValue, LI,
                                  LC->getOperand(), RC->getOperand(), DT,
                                  Depth + 1);
    if (X == 0)
      EqCacheSCEV.unionSets(LHS, RHS);
    return X;
  }

  case scCouldNotCompute:
    llvm_unreachable("Attempt to use a SCEVCouldNotCompute object!");
  }
  llvm_unreachable("Unknown SCEV kind!");
}

/// Given a list of SCEV objects, order them by their complexity, and group
/// objects of the same complexity together by value.  When this routine is
/// finished, we know that any duplicates in the vector are consecutive and that
/// complexity is monotonically increasing.
///
/// Note that we go take special precautions to ensure that we get deterministic
/// results from this routine.  In other words, we don't want the results of
/// this to depend on where the addresses of various SCEV objects happened to
/// land in memory.
static void GroupByComplexity(SmallVectorImpl<const SCEV *> &Ops,
                              LoopInfo *LI, DominatorTree &DT) {
  if (Ops.size() < 2) return;  // Noop

  EquivalenceClasses<const SCEV *> EqCacheSCEV;
  EquivalenceClasses<const Value *> EqCacheValue;
  if (Ops.size() == 2) {
    // This is the common case, which also happens to be trivially simple.
    // Special case it.
    const SCEV *&LHS = Ops[0], *&RHS = Ops[1];
    if (CompareSCEVComplexity(EqCacheSCEV, EqCacheValue, LI, RHS, LHS, DT) < 0)
      std::swap(LHS, RHS);
    return;
  }

  // Do the rough sort by complexity.
  std::stable_sort(Ops.begin(), Ops.end(),
                   [&](const SCEV *LHS, const SCEV *RHS) {
                     return CompareSCEVComplexity(EqCacheSCEV, EqCacheValue, LI,
                                                  LHS, RHS, DT) < 0;
                   });

  // Now that we are sorted by complexity, group elements of the same
  // complexity.  Note that this is, at worst, N^2, but the vector is likely to
  // be extremely short in practice.  Note that we take this approach because we
  // do not want to depend on the addresses of the objects we are grouping.
  for (unsigned i = 0, e = Ops.size(); i != e-2; ++i) {
    const SCEV *S = Ops[i];
    unsigned Complexity = S->getSCEVType();

    // If there are any objects of the same complexity and same value as this
    // one, group them.
    for (unsigned j = i+1; j != e && Ops[j]->getSCEVType() == Complexity; ++j) {
      if (Ops[j] == S) { // Found a duplicate.
        // Move it to immediately after i'th element.
        std::swap(Ops[i+1], Ops[j]);
        ++i;   // no need to rescan it.
        if (i == e-2) return;  // Done!
      }
    }
  }
}

// Returns the size of the SCEV S.
static inline int sizeOfSCEV(const SCEV *S) {
  struct FindSCEVSize {
    int Size = 0;

    FindSCEVSize() = default;

    bool follow(const SCEV *S) {
      ++Size;
      // Keep looking at all operands of S.
      return true;
    }

    bool isDone() const {
      return false;
    }
  };

  FindSCEVSize F;
  SCEVTraversal<FindSCEVSize> ST(F);
  ST.visitAll(S);
  return F.Size;
}

namespace {

struct SCEVDivision : public SCEVVisitor<SCEVDivision, void> {
public:
  // Computes the Quotient and Remainder of the division of Numerator by
  // Denominator.
  static void divide(ScalarEvolution &SE, const SCEV *Numerator,
                     const SCEV *Denominator, const SCEV **Quotient,
                     const SCEV **Remainder) {
    assert(Numerator && Denominator && "Uninitialized SCEV");

    SCEVDivision D(SE, Numerator, Denominator);

    // Check for the trivial case here to avoid having to check for it in the
    // rest of the code.
    if (Numerator == Denominator) {
      *Quotient = D.One;
      *Remainder = D.Zero;
      return;
    }

    if (Numerator->isZero()) {
      *Quotient = D.Zero;
      *Remainder = D.Zero;
      return;
    }

    // A simple case when N/1. The quotient is N.
    if (Denominator->isOne()) {
      *Quotient = Numerator;
      *Remainder = D.Zero;
      return;
    }

    // Split the Denominator when it is a product.
    if (const SCEVMulExpr *T = dyn_cast<SCEVMulExpr>(Denominator)) {
      const SCEV *Q, *R;
      *Quotient = Numerator;
      for (const SCEV *Op : T->operands()) {
        divide(SE, *Quotient, Op, &Q, &R);
        *Quotient = Q;

        // Bail out when the Numerator is not divisible by one of the terms of
        // the Denominator.
        if (!R->isZero()) {
          *Quotient = D.Zero;
          *Remainder = Numerator;
          return;
        }
      }
      *Remainder = D.Zero;
      return;
    }

    D.visit(Numerator);
    *Quotient = D.Quotient;
    *Remainder = D.Remainder;
  }

  // Except in the trivial case described above, we do not know how to divide
  // Expr by Denominator for the following functions with empty implementation.
  void visitTruncateExpr(const SCEVTruncateExpr *Numerator) {}
  void visitZeroExtendExpr(const SCEVZeroExtendExpr *Numerator) {}
  void visitSignExtendExpr(const SCEVSignExtendExpr *Numerator) {}
  void visitUDivExpr(const SCEVUDivExpr *Numerator) {}
  void visitSMaxExpr(const SCEVSMaxExpr *Numerator) {}
  void visitUMaxExpr(const SCEVUMaxExpr *Numerator) {}
  void visitUnknown(const SCEVUnknown *Numerator) {}
  void visitCouldNotCompute(const SCEVCouldNotCompute *Numerator) {}

  void visitConstant(const SCEVConstant *Numerator) {
    if (const SCEVConstant *D = dyn_cast<SCEVConstant>(Denominator)) {
      APInt NumeratorVal = Numerator->getAPInt();
      APInt DenominatorVal = D->getAPInt();
      uint32_t NumeratorBW = NumeratorVal.getBitWidth();
      uint32_t DenominatorBW = DenominatorVal.getBitWidth();

      if (NumeratorBW > DenominatorBW)
        DenominatorVal = DenominatorVal.sext(NumeratorBW);
      else if (NumeratorBW < DenominatorBW)
        NumeratorVal = NumeratorVal.sext(DenominatorBW);

      APInt QuotientVal(NumeratorVal.getBitWidth(), 0);
      APInt RemainderVal(NumeratorVal.getBitWidth(), 0);
      APInt::sdivrem(NumeratorVal, DenominatorVal, QuotientVal, RemainderVal);
      Quotient = SE.getConstant(QuotientVal);
      Remainder = SE.getConstant(RemainderVal);
      return;
    }
  }

  void visitAddRecExpr(const SCEVAddRecExpr *Numerator) {
    const SCEV *StartQ, *StartR, *StepQ, *StepR;
    if (!Numerator->isAffine())
      return cannotDivide(Numerator);
    divide(SE, Numerator->getStart(), Denominator, &StartQ, &StartR);
    divide(SE, Numerator->getStepRecurrence(SE), Denominator, &StepQ, &StepR);
    // Bail out if the types do not match.
    Type *Ty = Denominator->getType();
    if (Ty != StartQ->getType() || Ty != StartR->getType() ||
        Ty != StepQ->getType() || Ty != StepR->getType())
      return cannotDivide(Numerator);
    Quotient = SE.getAddRecExpr(StartQ, StepQ, Numerator->getLoop(),
                                Numerator->getNoWrapFlags());
    Remainder = SE.getAddRecExpr(StartR, StepR, Numerator->getLoop(),
                                 Numerator->getNoWrapFlags());
  }

  void visitAddExpr(const SCEVAddExpr *Numerator) {
    SmallVector<const SCEV *, 2> Qs, Rs;
    Type *Ty = Denominator->getType();

    for (const SCEV *Op : Numerator->operands()) {
      const SCEV *Q, *R;
      divide(SE, Op, Denominator, &Q, &R);

      // Bail out if types do not match.
      if (Ty != Q->getType() || Ty != R->getType())
        return cannotDivide(Numerator);

      Qs.push_back(Q);
      Rs.push_back(R);
    }

    if (Qs.size() == 1) {
      Quotient = Qs[0];
      Remainder = Rs[0];
      return;
    }

    Quotient = SE.getAddExpr(Qs);
    Remainder = SE.getAddExpr(Rs);
  }

  void visitMulExpr(const SCEVMulExpr *Numerator) {
    SmallVector<const SCEV *, 2> Qs;
    Type *Ty = Denominator->getType();

    bool FoundDenominatorTerm = false;
    for (const SCEV *Op : Numerator->operands()) {
      // Bail out if types do not match.
      if (Ty != Op->getType())
        return cannotDivide(Numerator);

      if (FoundDenominatorTerm) {
        Qs.push_back(Op);
        continue;
      }

      // Check whether Denominator divides one of the product operands.
      const SCEV *Q, *R;
      divide(SE, Op, Denominator, &Q, &R);
      if (!R->isZero()) {
        Qs.push_back(Op);
        continue;
      }

      // Bail out if types do not match.
      if (Ty != Q->getType())
        return cannotDivide(Numerator);

      FoundDenominatorTerm = true;
      Qs.push_back(Q);
    }

    if (FoundDenominatorTerm) {
      Remainder = Zero;
      if (Qs.size() == 1)
        Quotient = Qs[0];
      else
        Quotient = SE.getMulExpr(Qs);
      return;
    }

    if (!isa<SCEVUnknown>(Denominator))
      return cannotDivide(Numerator);

    // The Remainder is obtained by replacing Denominator by 0 in Numerator.
    ValueToValueMap RewriteMap;
    RewriteMap[cast<SCEVUnknown>(Denominator)->getValue()] =
        cast<SCEVConstant>(Zero)->getValue();
    Remainder = SCEVParameterRewriter::rewrite(Numerator, SE, RewriteMap, true);

    if (Remainder->isZero()) {
      // The Quotient is obtained by replacing Denominator by 1 in Numerator.
      RewriteMap[cast<SCEVUnknown>(Denominator)->getValue()] =
          cast<SCEVConstant>(One)->getValue();
      Quotient =
          SCEVParameterRewriter::rewrite(Numerator, SE, RewriteMap, true);
      return;
    }

    // Quotient is (Numerator - Remainder) divided by Denominator.
    const SCEV *Q, *R;
    const SCEV *Diff = SE.getMinusSCEV(Numerator, Remainder);
    // This SCEV does not seem to simplify: fail the division here.
    if (sizeOfSCEV(Diff) > sizeOfSCEV(Numerator))
      return cannotDivide(Numerator);
    divide(SE, Diff, Denominator, &Q, &R);
    if (R != Zero)
      return cannotDivide(Numerator);
    Quotient = Q;
  }

private:
  SCEVDivision(ScalarEvolution &S, const SCEV *Numerator,
               const SCEV *Denominator)
      : SE(S), Denominator(Denominator) {
    Zero = SE.getZero(Denominator->getType());
    One = SE.getOne(Denominator->getType());

    // We generally do not know how to divide Expr by Denominator. We
    // initialize the division to a "cannot divide" state to simplify the rest
    // of the code.
    cannotDivide(Numerator);
  }

  // Convenience function for giving up on the division. We set the quotient to
  // be equal to zero and the remainder to be equal to the numerator.
  void cannotDivide(const SCEV *Numerator) {
    Quotient = Zero;
    Remainder = Numerator;
  }

  ScalarEvolution &SE;
  const SCEV *Denominator, *Quotient, *Remainder, *Zero, *One;
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
//                      Simple SCEV method implementations
//===----------------------------------------------------------------------===//

/// Compute BC(It, K).  The result has width W.  Assume, K > 0.
static const SCEV *BinomialCoefficient(const SCEV *It, unsigned K,
                                       ScalarEvolution &SE,
                                       Type *ResultTy) {
  // Handle the simplest case efficiently.
  if (K == 1)
    return SE.getTruncateOrZeroExtend(It, ResultTy);

  // We are using the following formula for BC(It, K):
  //
  //   BC(It, K) = (It * (It - 1) * ... * (It - K + 1)) / K!
  //
  // Suppose, W is the bitwidth of the return value.  We must be prepared for
  // overflow.  Hence, we must assure that the result of our computation is
  // equal to the accurate one modulo 2^W.  Unfortunately, division isn't
  // safe in modular arithmetic.
  //
  // However, this code doesn't use exactly that formula; the formula it uses
  // is something like the following, where T is the number of factors of 2 in
  // K! (i.e. trailing zeros in the binary representation of K!), and ^ is
  // exponentiation:
  //
  //   BC(It, K) = (It * (It - 1) * ... * (It - K + 1)) / 2^T / (K! / 2^T)
  //
  // This formula is trivially equivalent to the previous formula.  However,
  // this formula can be implemented much more efficiently.  The trick is that
  // K! / 2^T is odd, and exact division by an odd number *is* safe in modular
  // arithmetic.  To do exact division in modular arithmetic, all we have
  // to do is multiply by the inverse.  Therefore, this step can be done at
  // width W.
  //
  // The next issue is how to safely do the division by 2^T.  The way this
  // is done is by doing the multiplication step at a width of at least W + T
  // bits.  This way, the bottom W+T bits of the product are accurate. Then,
  // when we perform the division by 2^T (which is equivalent to a right shift
  // by T), the bottom W bits are accurate.  Extra bits are okay; they'll get
  // truncated out after the division by 2^T.
  //
  // In comparison to just directly using the first formula, this technique
  // is much more efficient; using the first formula requires W * K bits,
  // but this formula less than W + K bits. Also, the first formula requires
  // a division step, whereas this formula only requires multiplies and shifts.
  //
  // It doesn't matter whether the subtraction step is done in the calculation
  // width or the input iteration count's width; if the subtraction overflows,
  // the result must be zero anyway.  We prefer here to do it in the width of
  // the induction variable because it helps a lot for certain cases; CodeGen
  // isn't smart enough to ignore the overflow, which leads to much less
  // efficient code if the width of the subtraction is wider than the native
  // register width.
  //
  // (It's possible to not widen at all by pulling out factors of 2 before
  // the multiplication; for example, K=2 can be calculated as
  // It/2*(It+(It*INT_MIN/INT_MIN)+-1). However, it requires
  // extra arithmetic, so it's not an obvious win, and it gets
  // much more complicated for K > 3.)

  // Protection from insane SCEVs; this bound is conservative,
  // but it probably doesn't matter.
  if (K > 1000)
    return SE.getCouldNotCompute();

  unsigned W = SE.getTypeSizeInBits(ResultTy);

  // Calculate K! / 2^T and T; we divide out the factors of two before
  // multiplying for calculating K! / 2^T to avoid overflow.
  // Other overflow doesn't matter because we only care about the bottom
  // W bits of the result.
  APInt OddFactorial(W, 1);
  unsigned T = 1;
  for (unsigned i = 3; i <= K; ++i) {
    APInt Mult(W, i);
    unsigned TwoFactors = Mult.countTrailingZeros();
    T += TwoFactors;
    Mult.lshrInPlace(TwoFactors);
    OddFactorial *= Mult;
  }

  // We need at least W + T bits for the multiplication step
  unsigned CalculationBits = W + T;

  // Calculate 2^T, at width T+W.
  APInt DivFactor = APInt::getOneBitSet(CalculationBits, T);

  // Calculate the multiplicative inverse of K! / 2^T;
  // this multiplication factor will perform the exact division by
  // K! / 2^T.
  APInt Mod = APInt::getSignedMinValue(W+1);
  APInt MultiplyFactor = OddFactorial.zext(W+1);
  MultiplyFactor = MultiplyFactor.multiplicativeInverse(Mod);
  MultiplyFactor = MultiplyFactor.trunc(W);

  // Calculate the product, at width T+W
  IntegerType *CalculationTy = IntegerType::get(SE.getContext(),
                                                      CalculationBits);
  const SCEV *Dividend = SE.getTruncateOrZeroExtend(It, CalculationTy);
  for (unsigned i = 1; i != K; ++i) {
    const SCEV *S = SE.getMinusSCEV(It, SE.getConstant(It->getType(), i));
    Dividend = SE.getMulExpr(Dividend,
                             SE.getTruncateOrZeroExtend(S, CalculationTy));
  }

  // Divide by 2^T
  const SCEV *DivResult = SE.getUDivExpr(Dividend, SE.getConstant(DivFactor));

  // Truncate the result, and divide by K! / 2^T.

  return SE.getMulExpr(SE.getConstant(MultiplyFactor),
                       SE.getTruncateOrZeroExtend(DivResult, ResultTy));
}

/// Return the value of this chain of recurrences at the specified iteration
/// number.  We can evaluate this recurrence by multiplying each element in the
/// chain by the binomial coefficient corresponding to it.  In other words, we
/// can evaluate {A,+,B,+,C,+,D} as:
///
///   A*BC(It, 0) + B*BC(It, 1) + C*BC(It, 2) + D*BC(It, 3)
///
/// where BC(It, k) stands for binomial coefficient.
const SCEV *SCEVAddRecExpr::evaluateAtIteration(const SCEV *It,
                                                ScalarEvolution &SE) const {
  const SCEV *Result = getStart();
  for (unsigned i = 1, e = getNumOperands(); i != e; ++i) {
    // The computation is correct in the face of overflow provided that the
    // multiplication is performed _after_ the evaluation of the binomial
    // coefficient.
    const SCEV *Coeff = BinomialCoefficient(It, i, SE, getType());
    if (isa<SCEVCouldNotCompute>(Coeff))
      return Coeff;

    Result = SE.getAddExpr(Result, SE.getMulExpr(getOperand(i), Coeff));
  }
  return Result;
}

//===----------------------------------------------------------------------===//
//                    SCEV Expression folder implementations
//===----------------------------------------------------------------------===//

const SCEV *ScalarEvolution::getTruncateExpr(const SCEV *Op,
                                             Type *Ty) {
  assert(getTypeSizeInBits(Op->getType()) > getTypeSizeInBits(Ty) &&
         "This is not a truncating conversion!");
  assert(isSCEVable(Ty) &&
         "This is not a conversion to a SCEVable type!");
  Ty = getEffectiveSCEVType(Ty);

  FoldingSetNodeID ID;
  ID.AddInteger(scTruncate);
  ID.AddPointer(Op);
  ID.AddPointer(Ty);
  void *IP = nullptr;
  if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP)) return S;

  // Fold if the operand is constant.
  if (const SCEVConstant *SC = dyn_cast<SCEVConstant>(Op))
    return getConstant(
      cast<ConstantInt>(ConstantExpr::getTrunc(SC->getValue(), Ty)));

  // trunc(trunc(x)) --> trunc(x)
  if (const SCEVTruncateExpr *ST = dyn_cast<SCEVTruncateExpr>(Op))
    return getTruncateExpr(ST->getOperand(), Ty);

  // trunc(sext(x)) --> sext(x) if widening or trunc(x) if narrowing
  if (const SCEVSignExtendExpr *SS = dyn_cast<SCEVSignExtendExpr>(Op))
    return getTruncateOrSignExtend(SS->getOperand(), Ty);

  // trunc(zext(x)) --> zext(x) if widening or trunc(x) if narrowing
  if (const SCEVZeroExtendExpr *SZ = dyn_cast<SCEVZeroExtendExpr>(Op))
    return getTruncateOrZeroExtend(SZ->getOperand(), Ty);

  // trunc(x1 + ... + xN) --> trunc(x1) + ... + trunc(xN) and
  // trunc(x1 * ... * xN) --> trunc(x1) * ... * trunc(xN),
  // if after transforming we have at most one truncate, not counting truncates
  // that replace other casts.
  if (isa<SCEVAddExpr>(Op) || isa<SCEVMulExpr>(Op)) {
    auto *CommOp = cast<SCEVCommutativeExpr>(Op);
    SmallVector<const SCEV *, 4> Operands;
    unsigned numTruncs = 0;
    for (unsigned i = 0, e = CommOp->getNumOperands(); i != e && numTruncs < 2;
         ++i) {
      const SCEV *S = getTruncateExpr(CommOp->getOperand(i), Ty);
      if (!isa<SCEVCastExpr>(CommOp->getOperand(i)) && isa<SCEVTruncateExpr>(S))
        numTruncs++;
      Operands.push_back(S);
    }
    if (numTruncs < 2) {
      if (isa<SCEVAddExpr>(Op))
        return getAddExpr(Operands);
      else if (isa<SCEVMulExpr>(Op))
        return getMulExpr(Operands);
      else
        llvm_unreachable("Unexpected SCEV type for Op.");
    }
    // Although we checked in the beginning that ID is not in the cache, it is
    // possible that during recursion and different modification ID was inserted
    // into the cache. So if we find it, just return it.
    if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP))
      return S;
  }

  // If the input value is a chrec scev, truncate the chrec's operands.
  if (const SCEVAddRecExpr *AddRec = dyn_cast<SCEVAddRecExpr>(Op)) {
    SmallVector<const SCEV *, 4> Operands;
    for (const SCEV *Op : AddRec->operands())
      Operands.push_back(getTruncateExpr(Op, Ty));
    return getAddRecExpr(Operands, AddRec->getLoop(), SCEV::FlagAnyWrap);
  }

  // The cast wasn't folded; create an explicit cast node. We can reuse
  // the existing insert position since if we get here, we won't have
  // made any changes which would invalidate it.
  SCEV *S = new (SCEVAllocator) SCEVTruncateExpr(ID.Intern(SCEVAllocator),
                                                 Op, Ty);
  UniqueSCEVs.InsertNode(S, IP);
  addToLoopUseLists(S);
  return S;
}

// Get the limit of a recurrence such that incrementing by Step cannot cause
// signed overflow as long as the value of the recurrence within the
// loop does not exceed this limit before incrementing.
static const SCEV *getSignedOverflowLimitForStep(const SCEV *Step,
                                                 ICmpInst::Predicate *Pred,
                                                 ScalarEvolution *SE) {
  unsigned BitWidth = SE->getTypeSizeInBits(Step->getType());
  if (SE->isKnownPositive(Step)) {
    *Pred = ICmpInst::ICMP_SLT;
    return SE->getConstant(APInt::getSignedMinValue(BitWidth) -
                           SE->getSignedRangeMax(Step));
  }
  if (SE->isKnownNegative(Step)) {
    *Pred = ICmpInst::ICMP_SGT;
    return SE->getConstant(APInt::getSignedMaxValue(BitWidth) -
                           SE->getSignedRangeMin(Step));
  }
  return nullptr;
}

// Get the limit of a recurrence such that incrementing by Step cannot cause
// unsigned overflow as long as the value of the recurrence within the loop does
// not exceed this limit before incrementing.
static const SCEV *getUnsignedOverflowLimitForStep(const SCEV *Step,
                                                   ICmpInst::Predicate *Pred,
                                                   ScalarEvolution *SE) {
  unsigned BitWidth = SE->getTypeSizeInBits(Step->getType());
  *Pred = ICmpInst::ICMP_ULT;

  return SE->getConstant(APInt::getMinValue(BitWidth) -
                         SE->getUnsignedRangeMax(Step));
}

namespace {

struct ExtendOpTraitsBase {
  typedef const SCEV *(ScalarEvolution::*GetExtendExprTy)(const SCEV *, Type *,
                                                          unsigned);
};

// Used to make code generic over signed and unsigned overflow.
template <typename ExtendOp> struct ExtendOpTraits {
  // Members present:
  //
  // static const SCEV::NoWrapFlags WrapType;
  //
  // static const ExtendOpTraitsBase::GetExtendExprTy GetExtendExpr;
  //
  // static const SCEV *getOverflowLimitForStep(const SCEV *Step,
  //                                           ICmpInst::Predicate *Pred,
  //                                           ScalarEvolution *SE);
};

template <>
struct ExtendOpTraits<SCEVSignExtendExpr> : public ExtendOpTraitsBase {
  static const SCEV::NoWrapFlags WrapType = SCEV::FlagNSW;

  static const GetExtendExprTy GetExtendExpr;

  static const SCEV *getOverflowLimitForStep(const SCEV *Step,
                                             ICmpInst::Predicate *Pred,
                                             ScalarEvolution *SE) {
    return getSignedOverflowLimitForStep(Step, Pred, SE);
  }
};

const ExtendOpTraitsBase::GetExtendExprTy ExtendOpTraits<
    SCEVSignExtendExpr>::GetExtendExpr = &ScalarEvolution::getSignExtendExpr;

template <>
struct ExtendOpTraits<SCEVZeroExtendExpr> : public ExtendOpTraitsBase {
  static const SCEV::NoWrapFlags WrapType = SCEV::FlagNUW;

  static const GetExtendExprTy GetExtendExpr;

  static const SCEV *getOverflowLimitForStep(const SCEV *Step,
                                             ICmpInst::Predicate *Pred,
                                             ScalarEvolution *SE) {
    return getUnsignedOverflowLimitForStep(Step, Pred, SE);
  }
};

const ExtendOpTraitsBase::GetExtendExprTy ExtendOpTraits<
    SCEVZeroExtendExpr>::GetExtendExpr = &ScalarEvolution::getZeroExtendExpr;

} // end anonymous namespace

// The recurrence AR has been shown to have no signed/unsigned wrap or something
// close to it. Typically, if we can prove NSW/NUW for AR, then we can just as
// easily prove NSW/NUW for its preincrement or postincrement sibling. This
// allows normalizing a sign/zero extended AddRec as such: {sext/zext(Step +
// Start),+,Step} => {(Step + sext/zext(Start),+,Step} As a result, the
// expression "Step + sext/zext(PreIncAR)" is congruent with
// "sext/zext(PostIncAR)"
template <typename ExtendOpTy>
static const SCEV *getPreStartForExtend(const SCEVAddRecExpr *AR, Type *Ty,
                                        ScalarEvolution *SE, unsigned Depth) {
  auto WrapType = ExtendOpTraits<ExtendOpTy>::WrapType;
  auto GetExtendExpr = ExtendOpTraits<ExtendOpTy>::GetExtendExpr;

  const Loop *L = AR->getLoop();
  const SCEV *Start = AR->getStart();
  const SCEV *Step = AR->getStepRecurrence(*SE);

  // Check for a simple looking step prior to loop entry.
  const SCEVAddExpr *SA = dyn_cast<SCEVAddExpr>(Start);
  if (!SA)
    return nullptr;

  // Create an AddExpr for "PreStart" after subtracting Step. Full SCEV
  // subtraction is expensive. For this purpose, perform a quick and dirty
  // difference, by checking for Step in the operand list.
  SmallVector<const SCEV *, 4> DiffOps;
  for (const SCEV *Op : SA->operands())
    if (Op != Step)
      DiffOps.push_back(Op);

  if (DiffOps.size() == SA->getNumOperands())
    return nullptr;

  // Try to prove `WrapType` (SCEV::FlagNSW or SCEV::FlagNUW) on `PreStart` +
  // `Step`:

  // 1. NSW/NUW flags on the step increment.
  auto PreStartFlags =
    ScalarEvolution::maskFlags(SA->getNoWrapFlags(), SCEV::FlagNUW);
  const SCEV *PreStart = SE->getAddExpr(DiffOps, PreStartFlags);
  const SCEVAddRecExpr *PreAR = dyn_cast<SCEVAddRecExpr>(
      SE->getAddRecExpr(PreStart, Step, L, SCEV::FlagAnyWrap));

  // "{S,+,X} is <nsw>/<nuw>" and "the backedge is taken at least once" implies
  // "S+X does not sign/unsign-overflow".
  //

  const SCEV *BECount = SE->getBackedgeTakenCount(L);
  if (PreAR && PreAR->getNoWrapFlags(WrapType) &&
      !isa<SCEVCouldNotCompute>(BECount) && SE->isKnownPositive(BECount))
    return PreStart;

  // 2. Direct overflow check on the step operation's expression.
  unsigned BitWidth = SE->getTypeSizeInBits(AR->getType());
  Type *WideTy = IntegerType::get(SE->getContext(), BitWidth * 2);
  const SCEV *OperandExtendedStart =
      SE->getAddExpr((SE->*GetExtendExpr)(PreStart, WideTy, Depth),
                     (SE->*GetExtendExpr)(Step, WideTy, Depth));
  if ((SE->*GetExtendExpr)(Start, WideTy, Depth) == OperandExtendedStart) {
    if (PreAR && AR->getNoWrapFlags(WrapType)) {
      // If we know `AR` == {`PreStart`+`Step`,+,`Step`} is `WrapType` (FlagNSW
      // or FlagNUW) and that `PreStart` + `Step` is `WrapType` too, then
      // `PreAR` == {`PreStart`,+,`Step`} is also `WrapType`.  Cache this fact.
      const_cast<SCEVAddRecExpr *>(PreAR)->setNoWrapFlags(WrapType);
    }
    return PreStart;
  }

  // 3. Loop precondition.
  ICmpInst::Predicate Pred;
  const SCEV *OverflowLimit =
      ExtendOpTraits<ExtendOpTy>::getOverflowLimitForStep(Step, &Pred, SE);

  if (OverflowLimit &&
      SE->isLoopEntryGuardedByCond(L, Pred, PreStart, OverflowLimit))
    return PreStart;

  return nullptr;
}

// Get the normalized zero or sign extended expression for this AddRec's Start.
template <typename ExtendOpTy>
static const SCEV *getExtendAddRecStart(const SCEVAddRecExpr *AR, Type *Ty,
                                        ScalarEvolution *SE,
                                        unsigned Depth) {
  auto GetExtendExpr = ExtendOpTraits<ExtendOpTy>::GetExtendExpr;

  const SCEV *PreStart = getPreStartForExtend<ExtendOpTy>(AR, Ty, SE, Depth);
  if (!PreStart)
    return (SE->*GetExtendExpr)(AR->getStart(), Ty, Depth);

  return SE->getAddExpr((SE->*GetExtendExpr)(AR->getStepRecurrence(*SE), Ty,
                                             Depth),
                        (SE->*GetExtendExpr)(PreStart, Ty, Depth));
}

// Try to prove away overflow by looking at "nearby" add recurrences.  A
// motivating example for this rule: if we know `{0,+,4}` is `ult` `-1` and it
// does not itself wrap then we can conclude that `{1,+,4}` is `nuw`.
//
// Formally:
//
//     {S,+,X} == {S-T,+,X} + T
//  => Ext({S,+,X}) == Ext({S-T,+,X} + T)
//
// If ({S-T,+,X} + T) does not overflow  ... (1)
//
//  RHS == Ext({S-T,+,X} + T) == Ext({S-T,+,X}) + Ext(T)
//
// If {S-T,+,X} does not overflow  ... (2)
//
//  RHS == Ext({S-T,+,X}) + Ext(T) == {Ext(S-T),+,Ext(X)} + Ext(T)
//      == {Ext(S-T)+Ext(T),+,Ext(X)}
//
// If (S-T)+T does not overflow  ... (3)
//
//  RHS == {Ext(S-T)+Ext(T),+,Ext(X)} == {Ext(S-T+T),+,Ext(X)}
//      == {Ext(S),+,Ext(X)} == LHS
//
// Thus, if (1), (2) and (3) are true for some T, then
//   Ext({S,+,X}) == {Ext(S),+,Ext(X)}
//
// (3) is implied by (1) -- "(S-T)+T does not overflow" is simply "({S-T,+,X}+T)
// does not overflow" restricted to the 0th iteration.  Therefore we only need
// to check for (1) and (2).
//
// In the current context, S is `Start`, X is `Step`, Ext is `ExtendOpTy` and T
// is `Delta` (defined below).
template <typename ExtendOpTy>
bool ScalarEvolution::proveNoWrapByVaryingStart(const SCEV *Start,
                                                const SCEV *Step,
                                                const Loop *L) {
  auto WrapType = ExtendOpTraits<ExtendOpTy>::WrapType;

  // We restrict `Start` to a constant to prevent SCEV from spending too much
  // time here.  It is correct (but more expensive) to continue with a
  // non-constant `Start` and do a general SCEV subtraction to compute
  // `PreStart` below.
  const SCEVConstant *StartC = dyn_cast<SCEVConstant>(Start);
  if (!StartC)
    return false;

  APInt StartAI = StartC->getAPInt();

  for (unsigned Delta : {-2, -1, 1, 2}) {
    const SCEV *PreStart = getConstant(StartAI - Delta);

    FoldingSetNodeID ID;
    ID.AddInteger(scAddRecExpr);
    ID.AddPointer(PreStart);
    ID.AddPointer(Step);
    ID.AddPointer(L);
    void *IP = nullptr;
    const auto *PreAR =
      static_cast<SCEVAddRecExpr *>(UniqueSCEVs.FindNodeOrInsertPos(ID, IP));

    // Give up if we don't already have the add recurrence we need because
    // actually constructing an add recurrence is relatively expensive.
    if (PreAR && PreAR->getNoWrapFlags(WrapType)) {  // proves (2)
      const SCEV *DeltaS = getConstant(StartC->getType(), Delta);
      ICmpInst::Predicate Pred = ICmpInst::BAD_ICMP_PREDICATE;
      const SCEV *Limit = ExtendOpTraits<ExtendOpTy>::getOverflowLimitForStep(
          DeltaS, &Pred, this);
      if (Limit && isKnownPredicate(Pred, PreAR, Limit))  // proves (1)
        return true;
    }
  }

  return false;
}

// Finds an integer D for an expression (C + x + y + ...) such that the top
// level addition in (D + (C - D + x + y + ...)) would not wrap (signed or
// unsigned) and the number of trailing zeros of (C - D + x + y + ...) is
// maximized, where C is the \p ConstantTerm, x, y, ... are arbitrary SCEVs, and
// the (C + x + y + ...) expression is \p WholeAddExpr.
static APInt extractConstantWithoutWrapping(ScalarEvolution &SE,
                                            const SCEVConstant *ConstantTerm,
                                            const SCEVAddExpr *WholeAddExpr) {
  const APInt C = ConstantTerm->getAPInt();
  const unsigned BitWidth = C.getBitWidth();
  // Find number of trailing zeros of (x + y + ...) w/o the C first:
  uint32_t TZ = BitWidth;
  for (unsigned I = 1, E = WholeAddExpr->getNumOperands(); I < E && TZ; ++I)
    TZ = std::min(TZ, SE.GetMinTrailingZeros(WholeAddExpr->getOperand(I)));
  if (TZ) {
    // Set D to be as many least significant bits of C as possible while still
    // guaranteeing that adding D to (C - D + x + y + ...) won't cause a wrap:
    return TZ < BitWidth ? C.trunc(TZ).zext(BitWidth) : C;
  }
  return APInt(BitWidth, 0);
}

// Finds an integer D for an affine AddRec expression {C,+,x} such that the top
// level addition in (D + {C-D,+,x}) would not wrap (signed or unsigned) and the
// number of trailing zeros of (C - D + x * n) is maximized, where C is the \p
// ConstantStart, x is an arbitrary \p Step, and n is the loop trip count.
static APInt extractConstantWithoutWrapping(ScalarEvolution &SE,
                                            const APInt &ConstantStart,
                                            const SCEV *Step) {
  const unsigned BitWidth = ConstantStart.getBitWidth();
  const uint32_t TZ = SE.GetMinTrailingZeros(Step);
  if (TZ)
    return TZ < BitWidth ? ConstantStart.trunc(TZ).zext(BitWidth)
                         : ConstantStart;
  return APInt(BitWidth, 0);
}

const SCEV *
ScalarEvolution::getZeroExtendExpr(const SCEV *Op, Type *Ty, unsigned Depth) {
  assert(getTypeSizeInBits(Op->getType()) < getTypeSizeInBits(Ty) &&
         "This is not an extending conversion!");
  assert(isSCEVable(Ty) &&
         "This is not a conversion to a SCEVable type!");
  Ty = getEffectiveSCEVType(Ty);

  // Fold if the operand is constant.
  if (const SCEVConstant *SC = dyn_cast<SCEVConstant>(Op))
    return getConstant(
      cast<ConstantInt>(ConstantExpr::getZExt(SC->getValue(), Ty)));

  // zext(zext(x)) --> zext(x)
  if (const SCEVZeroExtendExpr *SZ = dyn_cast<SCEVZeroExtendExpr>(Op))
    return getZeroExtendExpr(SZ->getOperand(), Ty, Depth + 1);

  // Before doing any expensive analysis, check to see if we've already
  // computed a SCEV for this Op and Ty.
  FoldingSetNodeID ID;
  ID.AddInteger(scZeroExtend);
  ID.AddPointer(Op);
  ID.AddPointer(Ty);
  void *IP = nullptr;
  if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP)) return S;
  if (Depth > MaxExtDepth) {
    SCEV *S = new (SCEVAllocator) SCEVZeroExtendExpr(ID.Intern(SCEVAllocator),
                                                     Op, Ty);
    UniqueSCEVs.InsertNode(S, IP);
    addToLoopUseLists(S);
    return S;
  }

  // zext(trunc(x)) --> zext(x) or x or trunc(x)
  if (const SCEVTruncateExpr *ST = dyn_cast<SCEVTruncateExpr>(Op)) {
    // It's possible the bits taken off by the truncate were all zero bits. If
    // so, we should be able to simplify this further.
    const SCEV *X = ST->getOperand();
    ConstantRange CR = getUnsignedRange(X);
    unsigned TruncBits = getTypeSizeInBits(ST->getType());
    unsigned NewBits = getTypeSizeInBits(Ty);
    if (CR.truncate(TruncBits).zeroExtend(NewBits).contains(
            CR.zextOrTrunc(NewBits)))
      return getTruncateOrZeroExtend(X, Ty);
  }

  // If the input value is a chrec scev, and we can prove that the value
  // did not overflow the old, smaller, value, we can zero extend all of the
  // operands (often constants).  This allows analysis of something like
  // this:  for (unsigned char X = 0; X < 100; ++X) { int Y = X; }
  if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(Op))
    if (AR->isAffine()) {
      const SCEV *Start = AR->getStart();
      const SCEV *Step = AR->getStepRecurrence(*this);
      unsigned BitWidth = getTypeSizeInBits(AR->getType());
      const Loop *L = AR->getLoop();

      if (!AR->hasNoUnsignedWrap()) {
        auto NewFlags = proveNoWrapViaConstantRanges(AR);
        const_cast<SCEVAddRecExpr *>(AR)->setNoWrapFlags(NewFlags);
      }

      // If we have special knowledge that this addrec won't overflow,
      // we don't need to do any further analysis.
      if (AR->hasNoUnsignedWrap())
        return getAddRecExpr(
            getExtendAddRecStart<SCEVZeroExtendExpr>(AR, Ty, this, Depth + 1),
            getZeroExtendExpr(Step, Ty, Depth + 1), L, AR->getNoWrapFlags());

      // Check whether the backedge-taken count is SCEVCouldNotCompute.
      // Note that this serves two purposes: It filters out loops that are
      // simply not analyzable, and it covers the case where this code is
      // being called from within backedge-taken count analysis, such that
      // attempting to ask for the backedge-taken count would likely result
      // in infinite recursion. In the later case, the analysis code will
      // cope with a conservative value, and it will take care to purge
      // that value once it has finished.
      const SCEV *MaxBECount = getMaxBackedgeTakenCount(L);
      if (!isa<SCEVCouldNotCompute>(MaxBECount)) {
        // Manually compute the final value for AR, checking for
        // overflow.

        // Check whether the backedge-taken count can be losslessly casted to
        // the addrec's type. The count is always unsigned.
        const SCEV *CastedMaxBECount =
          getTruncateOrZeroExtend(MaxBECount, Start->getType());
        const SCEV *RecastedMaxBECount =
          getTruncateOrZeroExtend(CastedMaxBECount, MaxBECount->getType());
        if (MaxBECount == RecastedMaxBECount) {
          Type *WideTy = IntegerType::get(getContext(), BitWidth * 2);
          // Check whether Start+Step*MaxBECount has no unsigned overflow.
          const SCEV *ZMul = getMulExpr(CastedMaxBECount, Step,
                                        SCEV::FlagAnyWrap, Depth + 1);
          const SCEV *ZAdd = getZeroExtendExpr(getAddExpr(Start, ZMul,
                                                          SCEV::FlagAnyWrap,
                                                          Depth + 1),
                                               WideTy, Depth + 1);
          const SCEV *WideStart = getZeroExtendExpr(Start, WideTy, Depth + 1);
          const SCEV *WideMaxBECount =
            getZeroExtendExpr(CastedMaxBECount, WideTy, Depth + 1);
          const SCEV *OperandExtendedAdd =
            getAddExpr(WideStart,
                       getMulExpr(WideMaxBECount,
                                  getZeroExtendExpr(Step, WideTy, Depth + 1),
                                  SCEV::FlagAnyWrap, Depth + 1),
                       SCEV::FlagAnyWrap, Depth + 1);
          if (ZAdd == OperandExtendedAdd) {
            // Cache knowledge of AR NUW, which is propagated to this AddRec.
            const_cast<SCEVAddRecExpr *>(AR)->setNoWrapFlags(SCEV::FlagNUW);
            // Return the expression with the addrec on the outside.
            return getAddRecExpr(
                getExtendAddRecStart<SCEVZeroExtendExpr>(AR, Ty, this,
                                                         Depth + 1),
                getZeroExtendExpr(Step, Ty, Depth + 1), L,
                AR->getNoWrapFlags());
          }
          // Similar to above, only this time treat the step value as signed.
          // This covers loops that count down.
          OperandExtendedAdd =
            getAddExpr(WideStart,
                       getMulExpr(WideMaxBECount,
                                  getSignExtendExpr(Step, WideTy, Depth + 1),
                                  SCEV::FlagAnyWrap, Depth + 1),
                       SCEV::FlagAnyWrap, Depth + 1);
          if (ZAdd == OperandExtendedAdd) {
            // Cache knowledge of AR NW, which is propagated to this AddRec.
            // Negative step causes unsigned wrap, but it still can't self-wrap.
            const_cast<SCEVAddRecExpr *>(AR)->setNoWrapFlags(SCEV::FlagNW);
            // Return the expression with the addrec on the outside.
            return getAddRecExpr(
                getExtendAddRecStart<SCEVZeroExtendExpr>(AR, Ty, this,
                                                         Depth + 1),
                getSignExtendExpr(Step, Ty, Depth + 1), L,
                AR->getNoWrapFlags());
          }
        }
      }

      // Normally, in the cases we can prove no-overflow via a
      // backedge guarding condition, we can also compute a backedge
      // taken count for the loop.  The exceptions are assumptions and
      // guards present in the loop -- SCEV is not great at exploiting
      // these to compute max backedge taken counts, but can still use
      // these to prove lack of overflow.  Use this fact to avoid
      // doing extra work that may not pay off.
      if (!isa<SCEVCouldNotCompute>(MaxBECount) || HasGuards ||
          !AC.assumptions().empty()) {
        // If the backedge is guarded by a comparison with the pre-inc
        // value the addrec is safe. Also, if the entry is guarded by
        // a comparison with the start value and the backedge is
        // guarded by a comparison with the post-inc value, the addrec
        // is safe.
        if (isKnownPositive(Step)) {
          const SCEV *N = getConstant(APInt::getMinValue(BitWidth) -
                                      getUnsignedRangeMax(Step));
          if (isLoopBackedgeGuardedByCond(L, ICmpInst::ICMP_ULT, AR, N) ||
              isKnownOnEveryIteration(ICmpInst::ICMP_ULT, AR, N)) {
            // Cache knowledge of AR NUW, which is propagated to this
            // AddRec.
            const_cast<SCEVAddRecExpr *>(AR)->setNoWrapFlags(SCEV::FlagNUW);
            // Return the expression with the addrec on the outside.
            return getAddRecExpr(
                getExtendAddRecStart<SCEVZeroExtendExpr>(AR, Ty, this,
                                                         Depth + 1),
                getZeroExtendExpr(Step, Ty, Depth + 1), L,
                AR->getNoWrapFlags());
          }
        } else if (isKnownNegative(Step)) {
          const SCEV *N = getConstant(APInt::getMaxValue(BitWidth) -
                                      getSignedRangeMin(Step));
          if (isLoopBackedgeGuardedByCond(L, ICmpInst::ICMP_UGT, AR, N) ||
              isKnownOnEveryIteration(ICmpInst::ICMP_UGT, AR, N)) {
            // Cache knowledge of AR NW, which is propagated to this
            // AddRec.  Negative step causes unsigned wrap, but it
            // still can't self-wrap.
            const_cast<SCEVAddRecExpr *>(AR)->setNoWrapFlags(SCEV::FlagNW);
            // Return the expression with the addrec on the outside.
            return getAddRecExpr(
                getExtendAddRecStart<SCEVZeroExtendExpr>(AR, Ty, this,
                                                         Depth + 1),
                getSignExtendExpr(Step, Ty, Depth + 1), L,
                AR->getNoWrapFlags());
          }
        }
      }

      // zext({C,+,Step}) --> (zext(D) + zext({C-D,+,Step}))<nuw><nsw>
      // if D + (C - D + Step * n) could be proven to not unsigned wrap
      // where D maximizes the number of trailing zeros of (C - D + Step * n)
      if (const auto *SC = dyn_cast<SCEVConstant>(Start)) {
        const APInt &C = SC->getAPInt();
        const APInt &D = extractConstantWithoutWrapping(*this, C, Step);
        if (D != 0) {
          const SCEV *SZExtD = getZeroExtendExpr(getConstant(D), Ty, Depth);
          const SCEV *SResidual =
              getAddRecExpr(getConstant(C - D), Step, L, AR->getNoWrapFlags());
          const SCEV *SZExtR = getZeroExtendExpr(SResidual, Ty, Depth + 1);
          return getAddExpr(SZExtD, SZExtR,
                            (SCEV::NoWrapFlags)(SCEV::FlagNSW | SCEV::FlagNUW),
                            Depth + 1);
        }
      }

      if (proveNoWrapByVaryingStart<SCEVZeroExtendExpr>(Start, Step, L)) {
        const_cast<SCEVAddRecExpr *>(AR)->setNoWrapFlags(SCEV::FlagNUW);
        return getAddRecExpr(
            getExtendAddRecStart<SCEVZeroExtendExpr>(AR, Ty, this, Depth + 1),
            getZeroExtendExpr(Step, Ty, Depth + 1), L, AR->getNoWrapFlags());
      }
    }

  // zext(A % B) --> zext(A) % zext(B)
  {
    const SCEV *LHS;
    const SCEV *RHS;
    if (matchURem(Op, LHS, RHS))
      return getURemExpr(getZeroExtendExpr(LHS, Ty, Depth + 1),
                         getZeroExtendExpr(RHS, Ty, Depth + 1));
  }

  // zext(A / B) --> zext(A) / zext(B).
  if (auto *Div = dyn_cast<SCEVUDivExpr>(Op))
    return getUDivExpr(getZeroExtendExpr(Div->getLHS(), Ty, Depth + 1),
                       getZeroExtendExpr(Div->getRHS(), Ty, Depth + 1));

  if (auto *SA = dyn_cast<SCEVAddExpr>(Op)) {
    // zext((A + B + ...)<nuw>) --> (zext(A) + zext(B) + ...)<nuw>
    if (SA->hasNoUnsignedWrap()) {
      // If the addition does not unsign overflow then we can, by definition,
      // commute the zero extension with the addition operation.
      SmallVector<const SCEV *, 4> Ops;
      for (const auto *Op : SA->operands())
        Ops.push_back(getZeroExtendExpr(Op, Ty, Depth + 1));
      return getAddExpr(Ops, SCEV::FlagNUW, Depth + 1);
    }

    // zext(C + x + y + ...) --> (zext(D) + zext((C - D) + x + y + ...))
    // if D + (C - D + x + y + ...) could be proven to not unsigned wrap
    // where D maximizes the number of trailing zeros of (C - D + x + y + ...)
    //
    // Often address arithmetics contain expressions like
    // (zext (add (shl X, C1), C2)), for instance, (zext (5 + (4 * X))).
    // This transformation is useful while proving that such expressions are
    // equal or differ by a small constant amount, see LoadStoreVectorizer pass.
    if (const auto *SC = dyn_cast<SCEVConstant>(SA->getOperand(0))) {
      const APInt &D = extractConstantWithoutWrapping(*this, SC, SA);
      if (D != 0) {
        const SCEV *SZExtD = getZeroExtendExpr(getConstant(D), Ty, Depth);
        const SCEV *SResidual =
            getAddExpr(getConstant(-D), SA, SCEV::FlagAnyWrap, Depth);
        const SCEV *SZExtR = getZeroExtendExpr(SResidual, Ty, Depth + 1);
        return getAddExpr(SZExtD, SZExtR,
                          (SCEV::NoWrapFlags)(SCEV::FlagNSW | SCEV::FlagNUW),
                          Depth + 1);
      }
    }
  }

  if (auto *SM = dyn_cast<SCEVMulExpr>(Op)) {
    // zext((A * B * ...)<nuw>) --> (zext(A) * zext(B) * ...)<nuw>
    if (SM->hasNoUnsignedWrap()) {
      // If the multiply does not unsign overflow then we can, by definition,
      // commute the zero extension with the multiply operation.
      SmallVector<const SCEV *, 4> Ops;
      for (const auto *Op : SM->operands())
        Ops.push_back(getZeroExtendExpr(Op, Ty, Depth + 1));
      return getMulExpr(Ops, SCEV::FlagNUW, Depth + 1);
    }

    // zext(2^K * (trunc X to iN)) to iM ->
    // 2^K * (zext(trunc X to i{N-K}) to iM)<nuw>
    //
    // Proof:
    //
    //     zext(2^K * (trunc X to iN)) to iM
    //   = zext((trunc X to iN) << K) to iM
    //   = zext((trunc X to i{N-K}) << K)<nuw> to iM
    //     (because shl removes the top K bits)
    //   = zext((2^K * (trunc X to i{N-K}))<nuw>) to iM
    //   = (2^K * (zext(trunc X to i{N-K}) to iM))<nuw>.
    //
    if (SM->getNumOperands() == 2)
      if (auto *MulLHS = dyn_cast<SCEVConstant>(SM->getOperand(0)))
        if (MulLHS->getAPInt().isPowerOf2())
          if (auto *TruncRHS = dyn_cast<SCEVTruncateExpr>(SM->getOperand(1))) {
            int NewTruncBits = getTypeSizeInBits(TruncRHS->getType()) -
                               MulLHS->getAPInt().logBase2();
            Type *NewTruncTy = IntegerType::get(getContext(), NewTruncBits);
            return getMulExpr(
                getZeroExtendExpr(MulLHS, Ty),
                getZeroExtendExpr(
                    getTruncateExpr(TruncRHS->getOperand(), NewTruncTy), Ty),
                SCEV::FlagNUW, Depth + 1);
          }
  }

  // The cast wasn't folded; create an explicit cast node.
  // Recompute the insert position, as it may have been invalidated.
  if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP)) return S;
  SCEV *S = new (SCEVAllocator) SCEVZeroExtendExpr(ID.Intern(SCEVAllocator),
                                                   Op, Ty);
  UniqueSCEVs.InsertNode(S, IP);
  addToLoopUseLists(S);
  return S;
}

const SCEV *
ScalarEvolution::getSignExtendExpr(const SCEV *Op, Type *Ty, unsigned Depth) {
  assert(getTypeSizeInBits(Op->getType()) < getTypeSizeInBits(Ty) &&
         "This is not an extending conversion!");
  assert(isSCEVable(Ty) &&
         "This is not a conversion to a SCEVable type!");
  Ty = getEffectiveSCEVType(Ty);

  // Fold if the operand is constant.
  if (const SCEVConstant *SC = dyn_cast<SCEVConstant>(Op))
    return getConstant(
      cast<ConstantInt>(ConstantExpr::getSExt(SC->getValue(), Ty)));

  // sext(sext(x)) --> sext(x)
  if (const SCEVSignExtendExpr *SS = dyn_cast<SCEVSignExtendExpr>(Op))
    return getSignExtendExpr(SS->getOperand(), Ty, Depth + 1);

  // sext(zext(x)) --> zext(x)
  if (const SCEVZeroExtendExpr *SZ = dyn_cast<SCEVZeroExtendExpr>(Op))
    return getZeroExtendExpr(SZ->getOperand(), Ty, Depth + 1);

  // Before doing any expensive analysis, check to see if we've already
  // computed a SCEV for this Op and Ty.
  FoldingSetNodeID ID;
  ID.AddInteger(scSignExtend);
  ID.AddPointer(Op);
  ID.AddPointer(Ty);
  void *IP = nullptr;
  if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP)) return S;
  // Limit recursion depth.
  if (Depth > MaxExtDepth) {
    SCEV *S = new (SCEVAllocator) SCEVSignExtendExpr(ID.Intern(SCEVAllocator),
                                                     Op, Ty);
    UniqueSCEVs.InsertNode(S, IP);
    addToLoopUseLists(S);
    return S;
  }

  // sext(trunc(x)) --> sext(x) or x or trunc(x)
  if (const SCEVTruncateExpr *ST = dyn_cast<SCEVTruncateExpr>(Op)) {
    // It's possible the bits taken off by the truncate were all sign bits. If
    // so, we should be able to simplify this further.
    const SCEV *X = ST->getOperand();
    ConstantRange CR = getSignedRange(X);
    unsigned TruncBits = getTypeSizeInBits(ST->getType());
    unsigned NewBits = getTypeSizeInBits(Ty);
    if (CR.truncate(TruncBits).signExtend(NewBits).contains(
            CR.sextOrTrunc(NewBits)))
      return getTruncateOrSignExtend(X, Ty);
  }

  if (auto *SA = dyn_cast<SCEVAddExpr>(Op)) {
    // sext((A + B + ...)<nsw>) --> (sext(A) + sext(B) + ...)<nsw>
    if (SA->hasNoSignedWrap()) {
      // If the addition does not sign overflow then we can, by definition,
      // commute the sign extension with the addition operation.
      SmallVector<const SCEV *, 4> Ops;
      for (const auto *Op : SA->operands())
        Ops.push_back(getSignExtendExpr(Op, Ty, Depth + 1));
      return getAddExpr(Ops, SCEV::FlagNSW, Depth + 1);
    }

    // sext(C + x + y + ...) --> (sext(D) + sext((C - D) + x + y + ...))
    // if D + (C - D + x + y + ...) could be proven to not signed wrap
    // where D maximizes the number of trailing zeros of (C - D + x + y + ...)
    //
    // For instance, this will bring two seemingly different expressions:
    //     1 + sext(5 + 20 * %x + 24 * %y)  and
    //         sext(6 + 20 * %x + 24 * %y)
    // to the same form:
    //     2 + sext(4 + 20 * %x + 24 * %y)
    if (const auto *SC = dyn_cast<SCEVConstant>(SA->getOperand(0))) {
      const APInt &D = extractConstantWithoutWrapping(*this, SC, SA);
      if (D != 0) {
        const SCEV *SSExtD = getSignExtendExpr(getConstant(D), Ty, Depth);
        const SCEV *SResidual =
            getAddExpr(getConstant(-D), SA, SCEV::FlagAnyWrap, Depth);
        const SCEV *SSExtR = getSignExtendExpr(SResidual, Ty, Depth + 1);
        return getAddExpr(SSExtD, SSExtR,
                          (SCEV::NoWrapFlags)(SCEV::FlagNSW | SCEV::FlagNUW),
                          Depth + 1);
      }
    }
  }
  // If the input value is a chrec scev, and we can prove that the value
  // did not overflow the old, smaller, value, we can sign extend all of the
  // operands (often constants).  This allows analysis of something like
  // this:  for (signed char X = 0; X < 100; ++X) { int Y = X; }
  if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(Op))
    if (AR->isAffine()) {
      const SCEV *Start = AR->getStart();
      const SCEV *Step = AR->getStepRecurrence(*this);
      unsigned BitWidth = getTypeSizeInBits(AR->getType());
      const Loop *L = AR->getLoop();

      if (!AR->hasNoSignedWrap()) {
        auto NewFlags = proveNoWrapViaConstantRanges(AR);
        const_cast<SCEVAddRecExpr *>(AR)->setNoWrapFlags(NewFlags);
      }

      // If we have special knowledge that this addrec won't overflow,
      // we don't need to do any further analysis.
      if (AR->hasNoSignedWrap())
        return getAddRecExpr(
            getExtendAddRecStart<SCEVSignExtendExpr>(AR, Ty, this, Depth + 1),
            getSignExtendExpr(Step, Ty, Depth + 1), L, SCEV::FlagNSW);

      // Check whether the backedge-taken count is SCEVCouldNotCompute.
      // Note that this serves two purposes: It filters out loops that are
      // simply not analyzable, and it covers the case where this code is
      // being called from within backedge-taken count analysis, such that
      // attempting to ask for the backedge-taken count would likely result
      // in infinite recursion. In the later case, the analysis code will
      // cope with a conservative value, and it will take care to purge
      // that value once it has finished.
      const SCEV *MaxBECount = getMaxBackedgeTakenCount(L);
      if (!isa<SCEVCouldNotCompute>(MaxBECount)) {
        // Manually compute the final value for AR, checking for
        // overflow.

        // Check whether the backedge-taken count can be losslessly casted to
        // the addrec's type. The count is always unsigned.
        const SCEV *CastedMaxBECount =
          getTruncateOrZeroExtend(MaxBECount, Start->getType());
        const SCEV *RecastedMaxBECount =
          getTruncateOrZeroExtend(CastedMaxBECount, MaxBECount->getType());
        if (MaxBECount == RecastedMaxBECount) {
          Type *WideTy = IntegerType::get(getContext(), BitWidth * 2);
          // Check whether Start+Step*MaxBECount has no signed overflow.
          const SCEV *SMul = getMulExpr(CastedMaxBECount, Step,
                                        SCEV::FlagAnyWrap, Depth + 1);
          const SCEV *SAdd = getSignExtendExpr(getAddExpr(Start, SMul,
                                                          SCEV::FlagAnyWrap,
                                                          Depth + 1),
                                               WideTy, Depth + 1);
          const SCEV *WideStart = getSignExtendExpr(Start, WideTy, Depth + 1);
          const SCEV *WideMaxBECount =
            getZeroExtendExpr(CastedMaxBECount, WideTy, Depth + 1);
          const SCEV *OperandExtendedAdd =
            getAddExpr(WideStart,
                       getMulExpr(WideMaxBECount,
                                  getSignExtendExpr(Step, WideTy, Depth + 1),
                                  SCEV::FlagAnyWrap, Depth + 1),
                       SCEV::FlagAnyWrap, Depth + 1);
          if (SAdd == OperandExtendedAdd) {
            // Cache knowledge of AR NSW, which is propagated to this AddRec.
            const_cast<SCEVAddRecExpr *>(AR)->setNoWrapFlags(SCEV::FlagNSW);
            // Return the expression with the addrec on the outside.
            return getAddRecExpr(
                getExtendAddRecStart<SCEVSignExtendExpr>(AR, Ty, this,
                                                         Depth + 1),
                getSignExtendExpr(Step, Ty, Depth + 1), L,
                AR->getNoWrapFlags());
          }
          // Similar to above, only this time treat the step value as unsigned.
          // This covers loops that count up with an unsigned step.
          OperandExtendedAdd =
            getAddExpr(WideStart,
                       getMulExpr(WideMaxBECount,
                                  getZeroExtendExpr(Step, WideTy, Depth + 1),
                                  SCEV::FlagAnyWrap, Depth + 1),
                       SCEV::FlagAnyWrap, Depth + 1);
          if (SAdd == OperandExtendedAdd) {
            // If AR wraps around then
            //
            //    abs(Step) * MaxBECount > unsigned-max(AR->getType())
            // => SAdd != OperandExtendedAdd
            //
            // Thus (AR is not NW => SAdd != OperandExtendedAdd) <=>
            // (SAdd == OperandExtendedAdd => AR is NW)

            const_cast<SCEVAddRecExpr *>(AR)->setNoWrapFlags(SCEV::FlagNW);

            // Return the expression with the addrec on the outside.
            return getAddRecExpr(
                getExtendAddRecStart<SCEVSignExtendExpr>(AR, Ty, this,
                                                         Depth + 1),
                getZeroExtendExpr(Step, Ty, Depth + 1), L,
                AR->getNoWrapFlags());
          }
        }
      }

      // Normally, in the cases we can prove no-overflow via a
      // backedge guarding condition, we can also compute a backedge
      // taken count for the loop.  The exceptions are assumptions and
      // guards present in the loop -- SCEV is not great at exploiting
      // these to compute max backedge taken counts, but can still use
      // these to prove lack of overflow.  Use this fact to avoid
      // doing extra work that may not pay off.

      if (!isa<SCEVCouldNotCompute>(MaxBECount) || HasGuards ||
          !AC.assumptions().empty()) {
        // If the backedge is guarded by a comparison with the pre-inc
        // value the addrec is safe. Also, if the entry is guarded by
        // a comparison with the start value and the backedge is
        // guarded by a comparison with the post-inc value, the addrec
        // is safe.
        ICmpInst::Predicate Pred;
        const SCEV *OverflowLimit =
            getSignedOverflowLimitForStep(Step, &Pred, this);
        if (OverflowLimit &&
            (isLoopBackedgeGuardedByCond(L, Pred, AR, OverflowLimit) ||
             isKnownOnEveryIteration(Pred, AR, OverflowLimit))) {
          // Cache knowledge of AR NSW, then propagate NSW to the wide AddRec.
          const_cast<SCEVAddRecExpr *>(AR)->setNoWrapFlags(SCEV::FlagNSW);
          return getAddRecExpr(
              getExtendAddRecStart<SCEVSignExtendExpr>(AR, Ty, this, Depth + 1),
              getSignExtendExpr(Step, Ty, Depth + 1), L, AR->getNoWrapFlags());
        }
      }

      // sext({C,+,Step}) --> (sext(D) + sext({C-D,+,Step}))<nuw><nsw>
      // if D + (C - D + Step * n) could be proven to not signed wrap
      // where D maximizes the number of trailing zeros of (C - D + Step * n)
      if (const auto *SC = dyn_cast<SCEVConstant>(Start)) {
        const APInt &C = SC->getAPInt();
        const APInt &D = extractConstantWithoutWrapping(*this, C, Step);
        if (D != 0) {
          const SCEV *SSExtD = getSignExtendExpr(getConstant(D), Ty, Depth);
          const SCEV *SResidual =
              getAddRecExpr(getConstant(C - D), Step, L, AR->getNoWrapFlags());
          const SCEV *SSExtR = getSignExtendExpr(SResidual, Ty, Depth + 1);
          return getAddExpr(SSExtD, SSExtR,
                            (SCEV::NoWrapFlags)(SCEV::FlagNSW | SCEV::FlagNUW),
                            Depth + 1);
        }
      }

      if (proveNoWrapByVaryingStart<SCEVSignExtendExpr>(Start, Step, L)) {
        const_cast<SCEVAddRecExpr *>(AR)->setNoWrapFlags(SCEV::FlagNSW);
        return getAddRecExpr(
            getExtendAddRecStart<SCEVSignExtendExpr>(AR, Ty, this, Depth + 1),
            getSignExtendExpr(Step, Ty, Depth + 1), L, AR->getNoWrapFlags());
      }
    }

  // If the input value is provably positive and we could not simplify
  // away the sext build a zext instead.
  if (isKnownNonNegative(Op))
    return getZeroExtendExpr(Op, Ty, Depth + 1);

  // The cast wasn't folded; create an explicit cast node.
  // Recompute the insert position, as it may have been invalidated.
  if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP)) return S;
  SCEV *S = new (SCEVAllocator) SCEVSignExtendExpr(ID.Intern(SCEVAllocator),
                                                   Op, Ty);
  UniqueSCEVs.InsertNode(S, IP);
  addToLoopUseLists(S);
  return S;
}

/// getAnyExtendExpr - Return a SCEV for the given operand extended with
/// unspecified bits out to the given type.
const SCEV *ScalarEvolution::getAnyExtendExpr(const SCEV *Op,
                                              Type *Ty) {
  assert(getTypeSizeInBits(Op->getType()) < getTypeSizeInBits(Ty) &&
         "This is not an extending conversion!");
  assert(isSCEVable(Ty) &&
         "This is not a conversion to a SCEVable type!");
  Ty = getEffectiveSCEVType(Ty);

  // Sign-extend negative constants.
  if (const SCEVConstant *SC = dyn_cast<SCEVConstant>(Op))
    if (SC->getAPInt().isNegative())
      return getSignExtendExpr(Op, Ty);

  // Peel off a truncate cast.
  if (const SCEVTruncateExpr *T = dyn_cast<SCEVTruncateExpr>(Op)) {
    const SCEV *NewOp = T->getOperand();
    if (getTypeSizeInBits(NewOp->getType()) < getTypeSizeInBits(Ty))
      return getAnyExtendExpr(NewOp, Ty);
    return getTruncateOrNoop(NewOp, Ty);
  }

  // Next try a zext cast. If the cast is folded, use it.
  const SCEV *ZExt = getZeroExtendExpr(Op, Ty);
  if (!isa<SCEVZeroExtendExpr>(ZExt))
    return ZExt;

  // Next try a sext cast. If the cast is folded, use it.
  const SCEV *SExt = getSignExtendExpr(Op, Ty);
  if (!isa<SCEVSignExtendExpr>(SExt))
    return SExt;

  // Force the cast to be folded into the operands of an addrec.
  if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(Op)) {
    SmallVector<const SCEV *, 4> Ops;
    for (const SCEV *Op : AR->operands())
      Ops.push_back(getAnyExtendExpr(Op, Ty));
    return getAddRecExpr(Ops, AR->getLoop(), SCEV::FlagNW);
  }

  // If the expression is obviously signed, use the sext cast value.
  if (isa<SCEVSMaxExpr>(Op))
    return SExt;

  // Absent any other information, use the zext cast value.
  return ZExt;
}

/// Process the given Ops list, which is a list of operands to be added under
/// the given scale, update the given map. This is a helper function for
/// getAddRecExpr. As an example of what it does, given a sequence of operands
/// that would form an add expression like this:
///
///    m + n + 13 + (A * (o + p + (B * (q + m + 29)))) + r + (-1 * r)
///
/// where A and B are constants, update the map with these values:
///
///    (m, 1+A*B), (n, 1), (o, A), (p, A), (q, A*B), (r, 0)
///
/// and add 13 + A*B*29 to AccumulatedConstant.
/// This will allow getAddRecExpr to produce this:
///
///    13+A*B*29 + n + (m * (1+A*B)) + ((o + p) * A) + (q * A*B)
///
/// This form often exposes folding opportunities that are hidden in
/// the original operand list.
///
/// Return true iff it appears that any interesting folding opportunities
/// may be exposed. This helps getAddRecExpr short-circuit extra work in
/// the common case where no interesting opportunities are present, and
/// is also used as a check to avoid infinite recursion.
static bool
CollectAddOperandsWithScales(DenseMap<const SCEV *, APInt> &M,
                             SmallVectorImpl<const SCEV *> &NewOps,
                             APInt &AccumulatedConstant,
                             const SCEV *const *Ops, size_t NumOperands,
                             const APInt &Scale,
                             ScalarEvolution &SE) {
  bool Interesting = false;

  // Iterate over the add operands. They are sorted, with constants first.
  unsigned i = 0;
  while (const SCEVConstant *C = dyn_cast<SCEVConstant>(Ops[i])) {
    ++i;
    // Pull a buried constant out to the outside.
    if (Scale != 1 || AccumulatedConstant != 0 || C->getValue()->isZero())
      Interesting = true;
    AccumulatedConstant += Scale * C->getAPInt();
  }

  // Next comes everything else. We're especially interested in multiplies
  // here, but they're in the middle, so just visit the rest with one loop.
  for (; i != NumOperands; ++i) {
    const SCEVMulExpr *Mul = dyn_cast<SCEVMulExpr>(Ops[i]);
    if (Mul && isa<SCEVConstant>(Mul->getOperand(0))) {
      APInt NewScale =
          Scale * cast<SCEVConstant>(Mul->getOperand(0))->getAPInt();
      if (Mul->getNumOperands() == 2 && isa<SCEVAddExpr>(Mul->getOperand(1))) {
        // A multiplication of a constant with another add; recurse.
        const SCEVAddExpr *Add = cast<SCEVAddExpr>(Mul->getOperand(1));
        Interesting |=
          CollectAddOperandsWithScales(M, NewOps, AccumulatedConstant,
                                       Add->op_begin(), Add->getNumOperands(),
                                       NewScale, SE);
      } else {
        // A multiplication of a constant with some other value. Update
        // the map.
        SmallVector<const SCEV *, 4> MulOps(Mul->op_begin()+1, Mul->op_end());
        const SCEV *Key = SE.getMulExpr(MulOps);
        auto Pair = M.insert({Key, NewScale});
        if (Pair.second) {
          NewOps.push_back(Pair.first->first);
        } else {
          Pair.first->second += NewScale;
          // The map already had an entry for this value, which may indicate
          // a folding opportunity.
          Interesting = true;
        }
      }
    } else {
      // An ordinary operand. Update the map.
      std::pair<DenseMap<const SCEV *, APInt>::iterator, bool> Pair =
          M.insert({Ops[i], Scale});
      if (Pair.second) {
        NewOps.push_back(Pair.first->first);
      } else {
        Pair.first->second += Scale;
        // The map already had an entry for this value, which may indicate
        // a folding opportunity.
        Interesting = true;
      }
    }
  }

  return Interesting;
}

// We're trying to construct a SCEV of type `Type' with `Ops' as operands and
// `OldFlags' as can't-wrap behavior.  Infer a more aggressive set of
// can't-overflow flags for the operation if possible.
static SCEV::NoWrapFlags
StrengthenNoWrapFlags(ScalarEvolution *SE, SCEVTypes Type,
                      const SmallVectorImpl<const SCEV *> &Ops,
                      SCEV::NoWrapFlags Flags) {
  using namespace std::placeholders;

  using OBO = OverflowingBinaryOperator;

  bool CanAnalyze =
      Type == scAddExpr || Type == scAddRecExpr || Type == scMulExpr;
  (void)CanAnalyze;
  assert(CanAnalyze && "don't call from other places!");

  int SignOrUnsignMask = SCEV::FlagNUW | SCEV::FlagNSW;
  SCEV::NoWrapFlags SignOrUnsignWrap =
      ScalarEvolution::maskFlags(Flags, SignOrUnsignMask);

  // If FlagNSW is true and all the operands are non-negative, infer FlagNUW.
  auto IsKnownNonNegative = [&](const SCEV *S) {
    return SE->isKnownNonNegative(S);
  };

  if (SignOrUnsignWrap == SCEV::FlagNSW && all_of(Ops, IsKnownNonNegative))
    Flags =
        ScalarEvolution::setFlags(Flags, (SCEV::NoWrapFlags)SignOrUnsignMask);

  SignOrUnsignWrap = ScalarEvolution::maskFlags(Flags, SignOrUnsignMask);

  if (SignOrUnsignWrap != SignOrUnsignMask &&
      (Type == scAddExpr || Type == scMulExpr) && Ops.size() == 2 &&
      isa<SCEVConstant>(Ops[0])) {

    auto Opcode = [&] {
      switch (Type) {
      case scAddExpr:
        return Instruction::Add;
      case scMulExpr:
        return Instruction::Mul;
      default:
        llvm_unreachable("Unexpected SCEV op.");
      }
    }();

    const APInt &C = cast<SCEVConstant>(Ops[0])->getAPInt();

    // (A <opcode> C) --> (A <opcode> C)<nsw> if the op doesn't sign overflow.
    if (!(SignOrUnsignWrap & SCEV::FlagNSW)) {
      auto NSWRegion = ConstantRange::makeGuaranteedNoWrapRegion(
          Opcode, C, OBO::NoSignedWrap);
      if (NSWRegion.contains(SE->getSignedRange(Ops[1])))
        Flags = ScalarEvolution::setFlags(Flags, SCEV::FlagNSW);
    }

    // (A <opcode> C) --> (A <opcode> C)<nuw> if the op doesn't unsign overflow.
    if (!(SignOrUnsignWrap & SCEV::FlagNUW)) {
      auto NUWRegion = ConstantRange::makeGuaranteedNoWrapRegion(
          Opcode, C, OBO::NoUnsignedWrap);
      if (NUWRegion.contains(SE->getUnsignedRange(Ops[1])))
        Flags = ScalarEvolution::setFlags(Flags, SCEV::FlagNUW);
    }
  }

  return Flags;
}

bool ScalarEvolution::isAvailableAtLoopEntry(const SCEV *S, const Loop *L) {
  return isLoopInvariant(S, L) && properlyDominates(S, L->getHeader());
}

/// Get a canonical add expression, or something simpler if possible.
const SCEV *ScalarEvolution::getAddExpr(SmallVectorImpl<const SCEV *> &Ops,
                                        SCEV::NoWrapFlags Flags,
                                        unsigned Depth) {
  assert(!(Flags & ~(SCEV::FlagNUW | SCEV::FlagNSW)) &&
         "only nuw or nsw allowed");
  assert(!Ops.empty() && "Cannot get empty add!");
  if (Ops.size() == 1) return Ops[0];
#ifndef NDEBUG
  Type *ETy = getEffectiveSCEVType(Ops[0]->getType());
  for (unsigned i = 1, e = Ops.size(); i != e; ++i)
    assert(getEffectiveSCEVType(Ops[i]->getType()) == ETy &&
           "SCEVAddExpr operand types don't match!");
#endif

  // Sort by complexity, this groups all similar expression types together.
  GroupByComplexity(Ops, &LI, DT);

  Flags = StrengthenNoWrapFlags(this, scAddExpr, Ops, Flags);

  // If there are any constants, fold them together.
  unsigned Idx = 0;
  if (const SCEVConstant *LHSC = dyn_cast<SCEVConstant>(Ops[0])) {
    ++Idx;
    assert(Idx < Ops.size());
    while (const SCEVConstant *RHSC = dyn_cast<SCEVConstant>(Ops[Idx])) {
      // We found two constants, fold them together!
      Ops[0] = getConstant(LHSC->getAPInt() + RHSC->getAPInt());
      if (Ops.size() == 2) return Ops[0];
      Ops.erase(Ops.begin()+1);  // Erase the folded element
      LHSC = cast<SCEVConstant>(Ops[0]);
    }

    // If we are left with a constant zero being added, strip it off.
    if (LHSC->getValue()->isZero()) {
      Ops.erase(Ops.begin());
      --Idx;
    }

    if (Ops.size() == 1) return Ops[0];
  }

  // Limit recursion calls depth.
  if (Depth > MaxArithDepth)
    return getOrCreateAddExpr(Ops, Flags);

  // Okay, check to see if the same value occurs in the operand list more than
  // once.  If so, merge them together into an multiply expression.  Since we
  // sorted the list, these values are required to be adjacent.
  Type *Ty = Ops[0]->getType();
  bool FoundMatch = false;
  for (unsigned i = 0, e = Ops.size(); i != e-1; ++i)
    if (Ops[i] == Ops[i+1]) {      //  X + Y + Y  -->  X + Y*2
      // Scan ahead to count how many equal operands there are.
      unsigned Count = 2;
      while (i+Count != e && Ops[i+Count] == Ops[i])
        ++Count;
      // Merge the values into a multiply.
      const SCEV *Scale = getConstant(Ty, Count);
      const SCEV *Mul = getMulExpr(Scale, Ops[i], SCEV::FlagAnyWrap, Depth + 1);
      if (Ops.size() == Count)
        return Mul;
      Ops[i] = Mul;
      Ops.erase(Ops.begin()+i+1, Ops.begin()+i+Count);
      --i; e -= Count - 1;
      FoundMatch = true;
    }
  if (FoundMatch)
    return getAddExpr(Ops, Flags, Depth + 1);

  // Check for truncates. If all the operands are truncated from the same
  // type, see if factoring out the truncate would permit the result to be
  // folded. eg., n*trunc(x) + m*trunc(y) --> trunc(trunc(m)*x + trunc(n)*y)
  // if the contents of the resulting outer trunc fold to something simple.
  auto FindTruncSrcType = [&]() -> Type * {
    // We're ultimately looking to fold an addrec of truncs and muls of only
    // constants and truncs, so if we find any other types of SCEV
    // as operands of the addrec then we bail and return nullptr here.
    // Otherwise, we return the type of the operand of a trunc that we find.
    if (auto *T = dyn_cast<SCEVTruncateExpr>(Ops[Idx]))
      return T->getOperand()->getType();
    if (const auto *Mul = dyn_cast<SCEVMulExpr>(Ops[Idx])) {
      const auto *LastOp = Mul->getOperand(Mul->getNumOperands() - 1);
      if (const auto *T = dyn_cast<SCEVTruncateExpr>(LastOp))
        return T->getOperand()->getType();
    }
    return nullptr;
  };
  if (auto *SrcType = FindTruncSrcType()) {
    SmallVector<const SCEV *, 8> LargeOps;
    bool Ok = true;
    // Check all the operands to see if they can be represented in the
    // source type of the truncate.
    for (unsigned i = 0, e = Ops.size(); i != e; ++i) {
      if (const SCEVTruncateExpr *T = dyn_cast<SCEVTruncateExpr>(Ops[i])) {
        if (T->getOperand()->getType() != SrcType) {
          Ok = false;
          break;
        }
        LargeOps.push_back(T->getOperand());
      } else if (const SCEVConstant *C = dyn_cast<SCEVConstant>(Ops[i])) {
        LargeOps.push_back(getAnyExtendExpr(C, SrcType));
      } else if (const SCEVMulExpr *M = dyn_cast<SCEVMulExpr>(Ops[i])) {
        SmallVector<const SCEV *, 8> LargeMulOps;
        for (unsigned j = 0, f = M->getNumOperands(); j != f && Ok; ++j) {
          if (const SCEVTruncateExpr *T =
                dyn_cast<SCEVTruncateExpr>(M->getOperand(j))) {
            if (T->getOperand()->getType() != SrcType) {
              Ok = false;
              break;
            }
            LargeMulOps.push_back(T->getOperand());
          } else if (const auto *C = dyn_cast<SCEVConstant>(M->getOperand(j))) {
            LargeMulOps.push_back(getAnyExtendExpr(C, SrcType));
          } else {
            Ok = false;
            break;
          }
        }
        if (Ok)
          LargeOps.push_back(getMulExpr(LargeMulOps, SCEV::FlagAnyWrap, Depth + 1));
      } else {
        Ok = false;
        break;
      }
    }
    if (Ok) {
      // Evaluate the expression in the larger type.
      const SCEV *Fold = getAddExpr(LargeOps, SCEV::FlagAnyWrap, Depth + 1);
      // If it folds to something simple, use it. Otherwise, don't.
      if (isa<SCEVConstant>(Fold) || isa<SCEVUnknown>(Fold))
        return getTruncateExpr(Fold, Ty);
    }
  }

  // Skip past any other cast SCEVs.
  while (Idx < Ops.size() && Ops[Idx]->getSCEVType() < scAddExpr)
    ++Idx;

  // If there are add operands they would be next.
  if (Idx < Ops.size()) {
    bool DeletedAdd = false;
    while (const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(Ops[Idx])) {
      if (Ops.size() > AddOpsInlineThreshold ||
          Add->getNumOperands() > AddOpsInlineThreshold)
        break;
      // If we have an add, expand the add operands onto the end of the operands
      // list.
      Ops.erase(Ops.begin()+Idx);
      Ops.append(Add->op_begin(), Add->op_end());
      DeletedAdd = true;
    }

    // If we deleted at least one add, we added operands to the end of the list,
    // and they are not necessarily sorted.  Recurse to resort and resimplify
    // any operands we just acquired.
    if (DeletedAdd)
      return getAddExpr(Ops, SCEV::FlagAnyWrap, Depth + 1);
  }

  // Skip over the add expression until we get to a multiply.
  while (Idx < Ops.size() && Ops[Idx]->getSCEVType() < scMulExpr)
    ++Idx;

  // Check to see if there are any folding opportunities present with
  // operands multiplied by constant values.
  if (Idx < Ops.size() && isa<SCEVMulExpr>(Ops[Idx])) {
    uint64_t BitWidth = getTypeSizeInBits(Ty);
    DenseMap<const SCEV *, APInt> M;
    SmallVector<const SCEV *, 8> NewOps;
    APInt AccumulatedConstant(BitWidth, 0);
    if (CollectAddOperandsWithScales(M, NewOps, AccumulatedConstant,
                                     Ops.data(), Ops.size(),
                                     APInt(BitWidth, 1), *this)) {
      struct APIntCompare {
        bool operator()(const APInt &LHS, const APInt &RHS) const {
          return LHS.ult(RHS);
        }
      };

      // Some interesting folding opportunity is present, so its worthwhile to
      // re-generate the operands list. Group the operands by constant scale,
      // to avoid multiplying by the same constant scale multiple times.
      std::map<APInt, SmallVector<const SCEV *, 4>, APIntCompare> MulOpLists;
      for (const SCEV *NewOp : NewOps)
        MulOpLists[M.find(NewOp)->second].push_back(NewOp);
      // Re-generate the operands list.
      Ops.clear();
      if (AccumulatedConstant != 0)
        Ops.push_back(getConstant(AccumulatedConstant));
      for (auto &MulOp : MulOpLists)
        if (MulOp.first != 0)
          Ops.push_back(getMulExpr(
              getConstant(MulOp.first),
              getAddExpr(MulOp.second, SCEV::FlagAnyWrap, Depth + 1),
              SCEV::FlagAnyWrap, Depth + 1));
      if (Ops.empty())
        return getZero(Ty);
      if (Ops.size() == 1)
        return Ops[0];
      return getAddExpr(Ops, SCEV::FlagAnyWrap, Depth + 1);
    }
  }

  // If we are adding something to a multiply expression, make sure the
  // something is not already an operand of the multiply.  If so, merge it into
  // the multiply.
  for (; Idx < Ops.size() && isa<SCEVMulExpr>(Ops[Idx]); ++Idx) {
    const SCEVMulExpr *Mul = cast<SCEVMulExpr>(Ops[Idx]);
    for (unsigned MulOp = 0, e = Mul->getNumOperands(); MulOp != e; ++MulOp) {
      const SCEV *MulOpSCEV = Mul->getOperand(MulOp);
      if (isa<SCEVConstant>(MulOpSCEV))
        continue;
      for (unsigned AddOp = 0, e = Ops.size(); AddOp != e; ++AddOp)
        if (MulOpSCEV == Ops[AddOp]) {
          // Fold W + X + (X * Y * Z)  -->  W + (X * ((Y*Z)+1))
          const SCEV *InnerMul = Mul->getOperand(MulOp == 0);
          if (Mul->getNumOperands() != 2) {
            // If the multiply has more than two operands, we must get the
            // Y*Z term.
            SmallVector<const SCEV *, 4> MulOps(Mul->op_begin(),
                                                Mul->op_begin()+MulOp);
            MulOps.append(Mul->op_begin()+MulOp+1, Mul->op_end());
            InnerMul = getMulExpr(MulOps, SCEV::FlagAnyWrap, Depth + 1);
          }
          SmallVector<const SCEV *, 2> TwoOps = {getOne(Ty), InnerMul};
          const SCEV *AddOne = getAddExpr(TwoOps, SCEV::FlagAnyWrap, Depth + 1);
          const SCEV *OuterMul = getMulExpr(AddOne, MulOpSCEV,
                                            SCEV::FlagAnyWrap, Depth + 1);
          if (Ops.size() == 2) return OuterMul;
          if (AddOp < Idx) {
            Ops.erase(Ops.begin()+AddOp);
            Ops.erase(Ops.begin()+Idx-1);
          } else {
            Ops.erase(Ops.begin()+Idx);
            Ops.erase(Ops.begin()+AddOp-1);
          }
          Ops.push_back(OuterMul);
          return getAddExpr(Ops, SCEV::FlagAnyWrap, Depth + 1);
        }

      // Check this multiply against other multiplies being added together.
      for (unsigned OtherMulIdx = Idx+1;
           OtherMulIdx < Ops.size() && isa<SCEVMulExpr>(Ops[OtherMulIdx]);
           ++OtherMulIdx) {
        const SCEVMulExpr *OtherMul = cast<SCEVMulExpr>(Ops[OtherMulIdx]);
        // If MulOp occurs in OtherMul, we can fold the two multiplies
        // together.
        for (unsigned OMulOp = 0, e = OtherMul->getNumOperands();
             OMulOp != e; ++OMulOp)
          if (OtherMul->getOperand(OMulOp) == MulOpSCEV) {
            // Fold X + (A*B*C) + (A*D*E) --> X + (A*(B*C+D*E))
            const SCEV *InnerMul1 = Mul->getOperand(MulOp == 0);
            if (Mul->getNumOperands() != 2) {
              SmallVector<const SCEV *, 4> MulOps(Mul->op_begin(),
                                                  Mul->op_begin()+MulOp);
              MulOps.append(Mul->op_begin()+MulOp+1, Mul->op_end());
              InnerMul1 = getMulExpr(MulOps, SCEV::FlagAnyWrap, Depth + 1);
            }
            const SCEV *InnerMul2 = OtherMul->getOperand(OMulOp == 0);
            if (OtherMul->getNumOperands() != 2) {
              SmallVector<const SCEV *, 4> MulOps(OtherMul->op_begin(),
                                                  OtherMul->op_begin()+OMulOp);
              MulOps.append(OtherMul->op_begin()+OMulOp+1, OtherMul->op_end());
              InnerMul2 = getMulExpr(MulOps, SCEV::FlagAnyWrap, Depth + 1);
            }
            SmallVector<const SCEV *, 2> TwoOps = {InnerMul1, InnerMul2};
            const SCEV *InnerMulSum =
                getAddExpr(TwoOps, SCEV::FlagAnyWrap, Depth + 1);
            const SCEV *OuterMul = getMulExpr(MulOpSCEV, InnerMulSum,
                                              SCEV::FlagAnyWrap, Depth + 1);
            if (Ops.size() == 2) return OuterMul;
            Ops.erase(Ops.begin()+Idx);
            Ops.erase(Ops.begin()+OtherMulIdx-1);
            Ops.push_back(OuterMul);
            return getAddExpr(Ops, SCEV::FlagAnyWrap, Depth + 1);
          }
      }
    }
  }

  // If there are any add recurrences in the operands list, see if any other
  // added values are loop invariant.  If so, we can fold them into the
  // recurrence.
  while (Idx < Ops.size() && Ops[Idx]->getSCEVType() < scAddRecExpr)
    ++Idx;

  // Scan over all recurrences, trying to fold loop invariants into them.
  for (; Idx < Ops.size() && isa<SCEVAddRecExpr>(Ops[Idx]); ++Idx) {
    // Scan all of the other operands to this add and add them to the vector if
    // they are loop invariant w.r.t. the recurrence.
    SmallVector<const SCEV *, 8> LIOps;
    const SCEVAddRecExpr *AddRec = cast<SCEVAddRecExpr>(Ops[Idx]);
    const Loop *AddRecLoop = AddRec->getLoop();
    for (unsigned i = 0, e = Ops.size(); i != e; ++i)
      if (isAvailableAtLoopEntry(Ops[i], AddRecLoop)) {
        LIOps.push_back(Ops[i]);
        Ops.erase(Ops.begin()+i);
        --i; --e;
      }

    // If we found some loop invariants, fold them into the recurrence.
    if (!LIOps.empty()) {
      //  NLI + LI + {Start,+,Step}  -->  NLI + {LI+Start,+,Step}
      LIOps.push_back(AddRec->getStart());

      SmallVector<const SCEV *, 4> AddRecOps(AddRec->op_begin(),
                                             AddRec->op_end());
      // This follows from the fact that the no-wrap flags on the outer add
      // expression are applicable on the 0th iteration, when the add recurrence
      // will be equal to its start value.
      AddRecOps[0] = getAddExpr(LIOps, Flags, Depth + 1);

      // Build the new addrec. Propagate the NUW and NSW flags if both the
      // outer add and the inner addrec are guaranteed to have no overflow.
      // Always propagate NW.
      Flags = AddRec->getNoWrapFlags(setFlags(Flags, SCEV::FlagNW));
      const SCEV *NewRec = getAddRecExpr(AddRecOps, AddRecLoop, Flags);

      // If all of the other operands were loop invariant, we are done.
      if (Ops.size() == 1) return NewRec;

      // Otherwise, add the folded AddRec by the non-invariant parts.
      for (unsigned i = 0;; ++i)
        if (Ops[i] == AddRec) {
          Ops[i] = NewRec;
          break;
        }
      return getAddExpr(Ops, SCEV::FlagAnyWrap, Depth + 1);
    }

    // Okay, if there weren't any loop invariants to be folded, check to see if
    // there are multiple AddRec's with the same loop induction variable being
    // added together.  If so, we can fold them.
    for (unsigned OtherIdx = Idx+1;
         OtherIdx < Ops.size() && isa<SCEVAddRecExpr>(Ops[OtherIdx]);
         ++OtherIdx) {
      // We expect the AddRecExpr's to be sorted in reverse dominance order,
      // so that the 1st found AddRecExpr is dominated by all others.
      assert(DT.dominates(
           cast<SCEVAddRecExpr>(Ops[OtherIdx])->getLoop()->getHeader(),
           AddRec->getLoop()->getHeader()) &&
        "AddRecExprs are not sorted in reverse dominance order?");
      if (AddRecLoop == cast<SCEVAddRecExpr>(Ops[OtherIdx])->getLoop()) {
        // Other + {A,+,B}<L> + {C,+,D}<L>  -->  Other + {A+C,+,B+D}<L>
        SmallVector<const SCEV *, 4> AddRecOps(AddRec->op_begin(),
                                               AddRec->op_end());
        for (; OtherIdx != Ops.size() && isa<SCEVAddRecExpr>(Ops[OtherIdx]);
             ++OtherIdx) {
          const auto *OtherAddRec = cast<SCEVAddRecExpr>(Ops[OtherIdx]);
          if (OtherAddRec->getLoop() == AddRecLoop) {
            for (unsigned i = 0, e = OtherAddRec->getNumOperands();
                 i != e; ++i) {
              if (i >= AddRecOps.size()) {
                AddRecOps.append(OtherAddRec->op_begin()+i,
                                 OtherAddRec->op_end());
                break;
              }
              SmallVector<const SCEV *, 2> TwoOps = {
                  AddRecOps[i], OtherAddRec->getOperand(i)};
              AddRecOps[i] = getAddExpr(TwoOps, SCEV::FlagAnyWrap, Depth + 1);
            }
            Ops.erase(Ops.begin() + OtherIdx); --OtherIdx;
          }
        }
        // Step size has changed, so we cannot guarantee no self-wraparound.
        Ops[Idx] = getAddRecExpr(AddRecOps, AddRecLoop, SCEV::FlagAnyWrap);
        return getAddExpr(Ops, SCEV::FlagAnyWrap, Depth + 1);
      }
    }

    // Otherwise couldn't fold anything into this recurrence.  Move onto the
    // next one.
  }

  // Okay, it looks like we really DO need an add expr.  Check to see if we
  // already have one, otherwise create a new one.
  return getOrCreateAddExpr(Ops, Flags);
}

const SCEV *
ScalarEvolution::getOrCreateAddExpr(SmallVectorImpl<const SCEV *> &Ops,
                                    SCEV::NoWrapFlags Flags) {
  FoldingSetNodeID ID;
  ID.AddInteger(scAddExpr);
  for (const SCEV *Op : Ops)
    ID.AddPointer(Op);
  void *IP = nullptr;
  SCEVAddExpr *S =
      static_cast<SCEVAddExpr *>(UniqueSCEVs.FindNodeOrInsertPos(ID, IP));
  if (!S) {
    const SCEV **O = SCEVAllocator.Allocate<const SCEV *>(Ops.size());
    std::uninitialized_copy(Ops.begin(), Ops.end(), O);
    S = new (SCEVAllocator)
        SCEVAddExpr(ID.Intern(SCEVAllocator), O, Ops.size());
    UniqueSCEVs.InsertNode(S, IP);
    addToLoopUseLists(S);
  }
  S->setNoWrapFlags(Flags);
  return S;
}

const SCEV *
ScalarEvolution::getOrCreateAddRecExpr(SmallVectorImpl<const SCEV *> &Ops,
                                       const Loop *L, SCEV::NoWrapFlags Flags) {
  FoldingSetNodeID ID;
  ID.AddInteger(scAddRecExpr);
  for (unsigned i = 0, e = Ops.size(); i != e; ++i)
    ID.AddPointer(Ops[i]);
  ID.AddPointer(L);
  void *IP = nullptr;
  SCEVAddRecExpr *S =
      static_cast<SCEVAddRecExpr *>(UniqueSCEVs.FindNodeOrInsertPos(ID, IP));
  if (!S) {
    const SCEV **O = SCEVAllocator.Allocate<const SCEV *>(Ops.size());
    std::uninitialized_copy(Ops.begin(), Ops.end(), O);
    S = new (SCEVAllocator)
        SCEVAddRecExpr(ID.Intern(SCEVAllocator), O, Ops.size(), L);
    UniqueSCEVs.InsertNode(S, IP);
    addToLoopUseLists(S);
  }
  S->setNoWrapFlags(Flags);
  return S;
}

const SCEV *
ScalarEvolution::getOrCreateMulExpr(SmallVectorImpl<const SCEV *> &Ops,
                                    SCEV::NoWrapFlags Flags) {
  FoldingSetNodeID ID;
  ID.AddInteger(scMulExpr);
  for (unsigned i = 0, e = Ops.size(); i != e; ++i)
    ID.AddPointer(Ops[i]);
  void *IP = nullptr;
  SCEVMulExpr *S =
    static_cast<SCEVMulExpr *>(UniqueSCEVs.FindNodeOrInsertPos(ID, IP));
  if (!S) {
    const SCEV **O = SCEVAllocator.Allocate<const SCEV *>(Ops.size());
    std::uninitialized_copy(Ops.begin(), Ops.end(), O);
    S = new (SCEVAllocator) SCEVMulExpr(ID.Intern(SCEVAllocator),
                                        O, Ops.size());
    UniqueSCEVs.InsertNode(S, IP);
    addToLoopUseLists(S);
  }
  S->setNoWrapFlags(Flags);
  return S;
}

static uint64_t umul_ov(uint64_t i, uint64_t j, bool &Overflow) {
  uint64_t k = i*j;
  if (j > 1 && k / j != i) Overflow = true;
  return k;
}

/// Compute the result of "n choose k", the binomial coefficient.  If an
/// intermediate computation overflows, Overflow will be set and the return will
/// be garbage. Overflow is not cleared on absence of overflow.
static uint64_t Choose(uint64_t n, uint64_t k, bool &Overflow) {
  // We use the multiplicative formula:
  //     n(n-1)(n-2)...(n-(k-1)) / k(k-1)(k-2)...1 .
  // At each iteration, we take the n-th term of the numeral and divide by the
  // (k-n)th term of the denominator.  This division will always produce an
  // integral result, and helps reduce the chance of overflow in the
  // intermediate computations. However, we can still overflow even when the
  // final result would fit.

  if (n == 0 || n == k) return 1;
  if (k > n) return 0;

  if (k > n/2)
    k = n-k;

  uint64_t r = 1;
  for (uint64_t i = 1; i <= k; ++i) {
    r = umul_ov(r, n-(i-1), Overflow);
    r /= i;
  }
  return r;
}

/// Determine if any of the operands in this SCEV are a constant or if
/// any of the add or multiply expressions in this SCEV contain a constant.
static bool containsConstantInAddMulChain(const SCEV *StartExpr) {
  struct FindConstantInAddMulChain {
    bool FoundConstant = false;

    bool follow(const SCEV *S) {
      FoundConstant |= isa<SCEVConstant>(S);
      return isa<SCEVAddExpr>(S) || isa<SCEVMulExpr>(S);
    }

    bool isDone() const {
      return FoundConstant;
    }
  };

  FindConstantInAddMulChain F;
  SCEVTraversal<FindConstantInAddMulChain> ST(F);
  ST.visitAll(StartExpr);
  return F.FoundConstant;
}

/// Get a canonical multiply expression, or something simpler if possible.
const SCEV *ScalarEvolution::getMulExpr(SmallVectorImpl<const SCEV *> &Ops,
                                        SCEV::NoWrapFlags Flags,
                                        unsigned Depth) {
  assert(Flags == maskFlags(Flags, SCEV::FlagNUW | SCEV::FlagNSW) &&
         "only nuw or nsw allowed");
  assert(!Ops.empty() && "Cannot get empty mul!");
  if (Ops.size() == 1) return Ops[0];
#ifndef NDEBUG
  Type *ETy = getEffectiveSCEVType(Ops[0]->getType());
  for (unsigned i = 1, e = Ops.size(); i != e; ++i)
    assert(getEffectiveSCEVType(Ops[i]->getType()) == ETy &&
           "SCEVMulExpr operand types don't match!");
#endif

  // Sort by complexity, this groups all similar expression types together.
  GroupByComplexity(Ops, &LI, DT);

  Flags = StrengthenNoWrapFlags(this, scMulExpr, Ops, Flags);

  // Limit recursion calls depth.
  if (Depth > MaxArithDepth)
    return getOrCreateMulExpr(Ops, Flags);

  // If there are any constants, fold them together.
  unsigned Idx = 0;
  if (const SCEVConstant *LHSC = dyn_cast<SCEVConstant>(Ops[0])) {

    if (Ops.size() == 2)
      // C1*(C2+V) -> C1*C2 + C1*V
      if (const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(Ops[1]))
        // If any of Add's ops are Adds or Muls with a constant, apply this
        // transformation as well.
        //
        // TODO: There are some cases where this transformation is not
        // profitable; for example, Add = (C0 + X) * Y + Z.  Maybe the scope of
        // this transformation should be narrowed down.
        if (Add->getNumOperands() == 2 && containsConstantInAddMulChain(Add))
          return getAddExpr(getMulExpr(LHSC, Add->getOperand(0),
                                       SCEV::FlagAnyWrap, Depth + 1),
                            getMulExpr(LHSC, Add->getOperand(1),
                                       SCEV::FlagAnyWrap, Depth + 1),
                            SCEV::FlagAnyWrap, Depth + 1);

    ++Idx;
    while (const SCEVConstant *RHSC = dyn_cast<SCEVConstant>(Ops[Idx])) {
      // We found two constants, fold them together!
      ConstantInt *Fold =
          ConstantInt::get(getContext(), LHSC->getAPInt() * RHSC->getAPInt());
      Ops[0] = getConstant(Fold);
      Ops.erase(Ops.begin()+1);  // Erase the folded element
      if (Ops.size() == 1) return Ops[0];
      LHSC = cast<SCEVConstant>(Ops[0]);
    }

    // If we are left with a constant one being multiplied, strip it off.
    if (cast<SCEVConstant>(Ops[0])->getValue()->isOne()) {
      Ops.erase(Ops.begin());
      --Idx;
    } else if (cast<SCEVConstant>(Ops[0])->getValue()->isZero()) {
      // If we have a multiply of zero, it will always be zero.
      return Ops[0];
    } else if (Ops[0]->isAllOnesValue()) {
      // If we have a mul by -1 of an add, try distributing the -1 among the
      // add operands.
      if (Ops.size() == 2) {
        if (const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(Ops[1])) {
          SmallVector<const SCEV *, 4> NewOps;
          bool AnyFolded = false;
          for (const SCEV *AddOp : Add->operands()) {
            const SCEV *Mul = getMulExpr(Ops[0], AddOp, SCEV::FlagAnyWrap,
                                         Depth + 1);
            if (!isa<SCEVMulExpr>(Mul)) AnyFolded = true;
            NewOps.push_back(Mul);
          }
          if (AnyFolded)
            return getAddExpr(NewOps, SCEV::FlagAnyWrap, Depth + 1);
        } else if (const auto *AddRec = dyn_cast<SCEVAddRecExpr>(Ops[1])) {
          // Negation preserves a recurrence's no self-wrap property.
          SmallVector<const SCEV *, 4> Operands;
          for (const SCEV *AddRecOp : AddRec->operands())
            Operands.push_back(getMulExpr(Ops[0], AddRecOp, SCEV::FlagAnyWrap,
                                          Depth + 1));

          return getAddRecExpr(Operands, AddRec->getLoop(),
                               AddRec->getNoWrapFlags(SCEV::FlagNW));
        }
      }
    }

    if (Ops.size() == 1)
      return Ops[0];
  }

  // Skip over the add expression until we get to a multiply.
  while (Idx < Ops.size() && Ops[Idx]->getSCEVType() < scMulExpr)
    ++Idx;

  // If there are mul operands inline them all into this expression.
  if (Idx < Ops.size()) {
    bool DeletedMul = false;
    while (const SCEVMulExpr *Mul = dyn_cast<SCEVMulExpr>(Ops[Idx])) {
      if (Ops.size() > MulOpsInlineThreshold)
        break;
      // If we have an mul, expand the mul operands onto the end of the
      // operands list.
      Ops.erase(Ops.begin()+Idx);
      Ops.append(Mul->op_begin(), Mul->op_end());
      DeletedMul = true;
    }

    // If we deleted at least one mul, we added operands to the end of the
    // list, and they are not necessarily sorted.  Recurse to resort and
    // resimplify any operands we just acquired.
    if (DeletedMul)
      return getMulExpr(Ops, SCEV::FlagAnyWrap, Depth + 1);
  }

  // If there are any add recurrences in the operands list, see if any other
  // added values are loop invariant.  If so, we can fold them into the
  // recurrence.
  while (Idx < Ops.size() && Ops[Idx]->getSCEVType() < scAddRecExpr)
    ++Idx;

  // Scan over all recurrences, trying to fold loop invariants into them.
  for (; Idx < Ops.size() && isa<SCEVAddRecExpr>(Ops[Idx]); ++Idx) {
    // Scan all of the other operands to this mul and add them to the vector
    // if they are loop invariant w.r.t. the recurrence.
    SmallVector<const SCEV *, 8> LIOps;
    const SCEVAddRecExpr *AddRec = cast<SCEVAddRecExpr>(Ops[Idx]);
    const Loop *AddRecLoop = AddRec->getLoop();
    for (unsigned i = 0, e = Ops.size(); i != e; ++i)
      if (isAvailableAtLoopEntry(Ops[i], AddRecLoop)) {
        LIOps.push_back(Ops[i]);
        Ops.erase(Ops.begin()+i);
        --i; --e;
      }

    // If we found some loop invariants, fold them into the recurrence.
    if (!LIOps.empty()) {
      //  NLI * LI * {Start,+,Step}  -->  NLI * {LI*Start,+,LI*Step}
      SmallVector<const SCEV *, 4> NewOps;
      NewOps.reserve(AddRec->getNumOperands());
      const SCEV *Scale = getMulExpr(LIOps, SCEV::FlagAnyWrap, Depth + 1);
      for (unsigned i = 0, e = AddRec->getNumOperands(); i != e; ++i)
        NewOps.push_back(getMulExpr(Scale, AddRec->getOperand(i),
                                    SCEV::FlagAnyWrap, Depth + 1));

      // Build the new addrec. Propagate the NUW and NSW flags if both the
      // outer mul and the inner addrec are guaranteed to have no overflow.
      //
      // No self-wrap cannot be guaranteed after changing the step size, but
      // will be inferred if either NUW or NSW is true.
      Flags = AddRec->getNoWrapFlags(clearFlags(Flags, SCEV::FlagNW));
      const SCEV *NewRec = getAddRecExpr(NewOps, AddRecLoop, Flags);

      // If all of the other operands were loop invariant, we are done.
      if (Ops.size() == 1) return NewRec;

      // Otherwise, multiply the folded AddRec by the non-invariant parts.
      for (unsigned i = 0;; ++i)
        if (Ops[i] == AddRec) {
          Ops[i] = NewRec;
          break;
        }
      return getMulExpr(Ops, SCEV::FlagAnyWrap, Depth + 1);
    }

    // Okay, if there weren't any loop invariants to be folded, check to see
    // if there are multiple AddRec's with the same loop induction variable
    // being multiplied together.  If so, we can fold them.

    // {A1,+,A2,+,...,+,An}<L> * {B1,+,B2,+,...,+,Bn}<L>
    // = {x=1 in [ sum y=x..2x [ sum z=max(y-x, y-n)..min(x,n) [
    //       choose(x, 2x)*choose(2x-y, x-z)*A_{y-z}*B_z
    //   ]]],+,...up to x=2n}.
    // Note that the arguments to choose() are always integers with values
    // known at compile time, never SCEV objects.
    //
    // The implementation avoids pointless extra computations when the two
    // addrec's are of different length (mathematically, it's equivalent to
    // an infinite stream of zeros on the right).
    bool OpsModified = false;
    for (unsigned OtherIdx = Idx+1;
         OtherIdx != Ops.size() && isa<SCEVAddRecExpr>(Ops[OtherIdx]);
         ++OtherIdx) {
      const SCEVAddRecExpr *OtherAddRec =
        dyn_cast<SCEVAddRecExpr>(Ops[OtherIdx]);
      if (!OtherAddRec || OtherAddRec->getLoop() != AddRecLoop)
        continue;

      // Limit max number of arguments to avoid creation of unreasonably big
      // SCEVAddRecs with very complex operands.
      if (AddRec->getNumOperands() + OtherAddRec->getNumOperands() - 1 >
          MaxAddRecSize)
        continue;

      bool Overflow = false;
      Type *Ty = AddRec->getType();
      bool LargerThan64Bits = getTypeSizeInBits(Ty) > 64;
      SmallVector<const SCEV*, 7> AddRecOps;
      for (int x = 0, xe = AddRec->getNumOperands() +
             OtherAddRec->getNumOperands() - 1; x != xe && !Overflow; ++x) {
        SmallVector <const SCEV *, 7> SumOps;
        for (int y = x, ye = 2*x+1; y != ye && !Overflow; ++y) {
          uint64_t Coeff1 = Choose(x, 2*x - y, Overflow);
          for (int z = std::max(y-x, y-(int)AddRec->getNumOperands()+1),
                 ze = std::min(x+1, (int)OtherAddRec->getNumOperands());
               z < ze && !Overflow; ++z) {
            uint64_t Coeff2 = Choose(2*x - y, x-z, Overflow);
            uint64_t Coeff;
            if (LargerThan64Bits)
              Coeff = umul_ov(Coeff1, Coeff2, Overflow);
            else
              Coeff = Coeff1*Coeff2;
            const SCEV *CoeffTerm = getConstant(Ty, Coeff);
            const SCEV *Term1 = AddRec->getOperand(y-z);
            const SCEV *Term2 = OtherAddRec->getOperand(z);
            SumOps.push_back(getMulExpr(CoeffTerm, Term1, Term2,
                                        SCEV::FlagAnyWrap, Depth + 1));
          }
        }
        if (SumOps.empty())
          SumOps.push_back(getZero(Ty));
        AddRecOps.push_back(getAddExpr(SumOps, SCEV::FlagAnyWrap, Depth + 1));
      }
      if (!Overflow) {
        const SCEV *NewAddRec = getAddRecExpr(AddRecOps, AddRec->getLoop(),
                                              SCEV::FlagAnyWrap);
        if (Ops.size() == 2) return NewAddRec;
        Ops[Idx] = NewAddRec;
        Ops.erase(Ops.begin() + OtherIdx); --OtherIdx;
        OpsModified = true;
        AddRec = dyn_cast<SCEVAddRecExpr>(NewAddRec);
        if (!AddRec)
          break;
      }
    }
    if (OpsModified)
      return getMulExpr(Ops, SCEV::FlagAnyWrap, Depth + 1);

    // Otherwise couldn't fold anything into this recurrence.  Move onto the
    // next one.
  }

  // Okay, it looks like we really DO need an mul expr.  Check to see if we
  // already have one, otherwise create a new one.
  return getOrCreateMulExpr(Ops, Flags);
}

/// Represents an unsigned remainder expression based on unsigned division.
const SCEV *ScalarEvolution::getURemExpr(const SCEV *LHS,
                                         const SCEV *RHS) {
  assert(getEffectiveSCEVType(LHS->getType()) ==
         getEffectiveSCEVType(RHS->getType()) &&
         "SCEVURemExpr operand types don't match!");

  // Short-circuit easy cases
  if (const SCEVConstant *RHSC = dyn_cast<SCEVConstant>(RHS)) {
    // If constant is one, the result is trivial
    if (RHSC->getValue()->isOne())
      return getZero(LHS->getType()); // X urem 1 --> 0

    // If constant is a power of two, fold into a zext(trunc(LHS)).
    if (RHSC->getAPInt().isPowerOf2()) {
      Type *FullTy = LHS->getType();
      Type *TruncTy =
          IntegerType::get(getContext(), RHSC->getAPInt().logBase2());
      return getZeroExtendExpr(getTruncateExpr(LHS, TruncTy), FullTy);
    }
  }

  // Fallback to %a == %x urem %y == %x -<nuw> ((%x udiv %y) *<nuw> %y)
  const SCEV *UDiv = getUDivExpr(LHS, RHS);
  const SCEV *Mult = getMulExpr(UDiv, RHS, SCEV::FlagNUW);
  return getMinusSCEV(LHS, Mult, SCEV::FlagNUW);
}

/// Get a canonical unsigned division expression, or something simpler if
/// possible.
const SCEV *ScalarEvolution::getUDivExpr(const SCEV *LHS,
                                         const SCEV *RHS) {
  assert(getEffectiveSCEVType(LHS->getType()) ==
         getEffectiveSCEVType(RHS->getType()) &&
         "SCEVUDivExpr operand types don't match!");

  if (const SCEVConstant *RHSC = dyn_cast<SCEVConstant>(RHS)) {
    if (RHSC->getValue()->isOne())
      return LHS;                               // X udiv 1 --> x
    // If the denominator is zero, the result of the udiv is undefined. Don't
    // try to analyze it, because the resolution chosen here may differ from
    // the resolution chosen in other parts of the compiler.
    if (!RHSC->getValue()->isZero()) {
      // Determine if the division can be folded into the operands of
      // its operands.
      // TODO: Generalize this to non-constants by using known-bits information.
      Type *Ty = LHS->getType();
      unsigned LZ = RHSC->getAPInt().countLeadingZeros();
      unsigned MaxShiftAmt = getTypeSizeInBits(Ty) - LZ - 1;
      // For non-power-of-two values, effectively round the value up to the
      // nearest power of two.
      if (!RHSC->getAPInt().isPowerOf2())
        ++MaxShiftAmt;
      IntegerType *ExtTy =
        IntegerType::get(getContext(), getTypeSizeInBits(Ty) + MaxShiftAmt);
      if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(LHS))
        if (const SCEVConstant *Step =
            dyn_cast<SCEVConstant>(AR->getStepRecurrence(*this))) {
          // {X,+,N}/C --> {X/C,+,N/C} if safe and N/C can be folded.
          const APInt &StepInt = Step->getAPInt();
          const APInt &DivInt = RHSC->getAPInt();
          if (!StepInt.urem(DivInt) &&
              getZeroExtendExpr(AR, ExtTy) ==
              getAddRecExpr(getZeroExtendExpr(AR->getStart(), ExtTy),
                            getZeroExtendExpr(Step, ExtTy),
                            AR->getLoop(), SCEV::FlagAnyWrap)) {
            SmallVector<const SCEV *, 4> Operands;
            for (const SCEV *Op : AR->operands())
              Operands.push_back(getUDivExpr(Op, RHS));
            return getAddRecExpr(Operands, AR->getLoop(), SCEV::FlagNW);
          }
          /// Get a canonical UDivExpr for a recurrence.
          /// {X,+,N}/C => {Y,+,N}/C where Y=X-(X%N). Safe when C%N=0.
          // We can currently only fold X%N if X is constant.
          const SCEVConstant *StartC = dyn_cast<SCEVConstant>(AR->getStart());
          if (StartC && !DivInt.urem(StepInt) &&
              getZeroExtendExpr(AR, ExtTy) ==
              getAddRecExpr(getZeroExtendExpr(AR->getStart(), ExtTy),
                            getZeroExtendExpr(Step, ExtTy),
                            AR->getLoop(), SCEV::FlagAnyWrap)) {
            const APInt &StartInt = StartC->getAPInt();
            const APInt &StartRem = StartInt.urem(StepInt);
            if (StartRem != 0)
              LHS = getAddRecExpr(getConstant(StartInt - StartRem), Step,
                                  AR->getLoop(), SCEV::FlagNW);
          }
        }
      // (A*B)/C --> A*(B/C) if safe and B/C can be folded.
      if (const SCEVMulExpr *M = dyn_cast<SCEVMulExpr>(LHS)) {
        SmallVector<const SCEV *, 4> Operands;
        for (const SCEV *Op : M->operands())
          Operands.push_back(getZeroExtendExpr(Op, ExtTy));
        if (getZeroExtendExpr(M, ExtTy) == getMulExpr(Operands))
          // Find an operand that's safely divisible.
          for (unsigned i = 0, e = M->getNumOperands(); i != e; ++i) {
            const SCEV *Op = M->getOperand(i);
            const SCEV *Div = getUDivExpr(Op, RHSC);
            if (!isa<SCEVUDivExpr>(Div) && getMulExpr(Div, RHSC) == Op) {
              Operands = SmallVector<const SCEV *, 4>(M->op_begin(),
                                                      M->op_end());
              Operands[i] = Div;
              return getMulExpr(Operands);
            }
          }
      }

      // (A/B)/C --> A/(B*C) if safe and B*C can be folded.
      if (const SCEVUDivExpr *OtherDiv = dyn_cast<SCEVUDivExpr>(LHS)) {
        if (auto *DivisorConstant =
                dyn_cast<SCEVConstant>(OtherDiv->getRHS())) {
          bool Overflow = false;
          APInt NewRHS =
              DivisorConstant->getAPInt().umul_ov(RHSC->getAPInt(), Overflow);
          if (Overflow) {
            return getConstant(RHSC->getType(), 0, false);
          }
          return getUDivExpr(OtherDiv->getLHS(), getConstant(NewRHS));
        }
      }

      // (A+B)/C --> (A/C + B/C) if safe and A/C and B/C can be folded.
      if (const SCEVAddExpr *A = dyn_cast<SCEVAddExpr>(LHS)) {
        SmallVector<const SCEV *, 4> Operands;
        for (const SCEV *Op : A->operands())
          Operands.push_back(getZeroExtendExpr(Op, ExtTy));
        if (getZeroExtendExpr(A, ExtTy) == getAddExpr(Operands)) {
          Operands.clear();
          for (unsigned i = 0, e = A->getNumOperands(); i != e; ++i) {
            const SCEV *Op = getUDivExpr(A->getOperand(i), RHS);
            if (isa<SCEVUDivExpr>(Op) ||
                getMulExpr(Op, RHS) != A->getOperand(i))
              break;
            Operands.push_back(Op);
          }
          if (Operands.size() == A->getNumOperands())
            return getAddExpr(Operands);
        }
      }

      // Fold if both operands are constant.
      if (const SCEVConstant *LHSC = dyn_cast<SCEVConstant>(LHS)) {
        Constant *LHSCV = LHSC->getValue();
        Constant *RHSCV = RHSC->getValue();
        return getConstant(cast<ConstantInt>(ConstantExpr::getUDiv(LHSCV,
                                                                   RHSCV)));
      }
    }
  }

  FoldingSetNodeID ID;
  ID.AddInteger(scUDivExpr);
  ID.AddPointer(LHS);
  ID.AddPointer(RHS);
  void *IP = nullptr;
  if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP)) return S;
  SCEV *S = new (SCEVAllocator) SCEVUDivExpr(ID.Intern(SCEVAllocator),
                                             LHS, RHS);
  UniqueSCEVs.InsertNode(S, IP);
  addToLoopUseLists(S);
  return S;
}

static const APInt gcd(const SCEVConstant *C1, const SCEVConstant *C2) {
  APInt A = C1->getAPInt().abs();
  APInt B = C2->getAPInt().abs();
  uint32_t ABW = A.getBitWidth();
  uint32_t BBW = B.getBitWidth();

  if (ABW > BBW)
    B = B.zext(ABW);
  else if (ABW < BBW)
    A = A.zext(BBW);

  return APIntOps::GreatestCommonDivisor(std::move(A), std::move(B));
}

/// Get a canonical unsigned division expression, or something simpler if
/// possible. There is no representation for an exact udiv in SCEV IR, but we
/// can attempt to remove factors from the LHS and RHS.  We can't do this when
/// it's not exact because the udiv may be clearing bits.
const SCEV *ScalarEvolution::getUDivExactExpr(const SCEV *LHS,
                                              const SCEV *RHS) {
  // TODO: we could try to find factors in all sorts of things, but for now we
  // just deal with u/exact (multiply, constant). See SCEVDivision towards the
  // end of this file for inspiration.

  const SCEVMulExpr *Mul = dyn_cast<SCEVMulExpr>(LHS);
  if (!Mul || !Mul->hasNoUnsignedWrap())
    return getUDivExpr(LHS, RHS);

  if (const SCEVConstant *RHSCst = dyn_cast<SCEVConstant>(RHS)) {
    // If the mulexpr multiplies by a constant, then that constant must be the
    // first element of the mulexpr.
    if (const auto *LHSCst = dyn_cast<SCEVConstant>(Mul->getOperand(0))) {
      if (LHSCst == RHSCst) {
        SmallVector<const SCEV *, 2> Operands;
        Operands.append(Mul->op_begin() + 1, Mul->op_end());
        return getMulExpr(Operands);
      }

      // We can't just assume that LHSCst divides RHSCst cleanly, it could be
      // that there's a factor provided by one of the other terms. We need to
      // check.
      APInt Factor = gcd(LHSCst, RHSCst);
      if (!Factor.isIntN(1)) {
        LHSCst =
            cast<SCEVConstant>(getConstant(LHSCst->getAPInt().udiv(Factor)));
        RHSCst =
            cast<SCEVConstant>(getConstant(RHSCst->getAPInt().udiv(Factor)));
        SmallVector<const SCEV *, 2> Operands;
        Operands.push_back(LHSCst);
        Operands.append(Mul->op_begin() + 1, Mul->op_end());
        LHS = getMulExpr(Operands);
        RHS = RHSCst;
        Mul = dyn_cast<SCEVMulExpr>(LHS);
        if (!Mul)
          return getUDivExactExpr(LHS, RHS);
      }
    }
  }

  for (int i = 0, e = Mul->getNumOperands(); i != e; ++i) {
    if (Mul->getOperand(i) == RHS) {
      SmallVector<const SCEV *, 2> Operands;
      Operands.append(Mul->op_begin(), Mul->op_begin() + i);
      Operands.append(Mul->op_begin() + i + 1, Mul->op_end());
      return getMulExpr(Operands);
    }
  }

  return getUDivExpr(LHS, RHS);
}

/// Get an add recurrence expression for the specified loop.  Simplify the
/// expression as much as possible.
const SCEV *ScalarEvolution::getAddRecExpr(const SCEV *Start, const SCEV *Step,
                                           const Loop *L,
                                           SCEV::NoWrapFlags Flags) {
  SmallVector<const SCEV *, 4> Operands;
  Operands.push_back(Start);
  if (const SCEVAddRecExpr *StepChrec = dyn_cast<SCEVAddRecExpr>(Step))
    if (StepChrec->getLoop() == L) {
      Operands.append(StepChrec->op_begin(), StepChrec->op_end());
      return getAddRecExpr(Operands, L, maskFlags(Flags, SCEV::FlagNW));
    }

  Operands.push_back(Step);
  return getAddRecExpr(Operands, L, Flags);
}

/// Get an add recurrence expression for the specified loop.  Simplify the
/// expression as much as possible.
const SCEV *
ScalarEvolution::getAddRecExpr(SmallVectorImpl<const SCEV *> &Operands,
                               const Loop *L, SCEV::NoWrapFlags Flags) {
  if (Operands.size() == 1) return Operands[0];
#ifndef NDEBUG
  Type *ETy = getEffectiveSCEVType(Operands[0]->getType());
  for (unsigned i = 1, e = Operands.size(); i != e; ++i)
    assert(getEffectiveSCEVType(Operands[i]->getType()) == ETy &&
           "SCEVAddRecExpr operand types don't match!");
  for (unsigned i = 0, e = Operands.size(); i != e; ++i)
    assert(isLoopInvariant(Operands[i], L) &&
           "SCEVAddRecExpr operand is not loop-invariant!");
#endif

  if (Operands.back()->isZero()) {
    Operands.pop_back();
    return getAddRecExpr(Operands, L, SCEV::FlagAnyWrap); // {X,+,0}  -->  X
  }

  // It's tempting to want to call getMaxBackedgeTakenCount count here and
  // use that information to infer NUW and NSW flags. However, computing a
  // BE count requires calling getAddRecExpr, so we may not yet have a
  // meaningful BE count at this point (and if we don't, we'd be stuck
  // with a SCEVCouldNotCompute as the cached BE count).

  Flags = StrengthenNoWrapFlags(this, scAddRecExpr, Operands, Flags);

  // Canonicalize nested AddRecs in by nesting them in order of loop depth.
  if (const SCEVAddRecExpr *NestedAR = dyn_cast<SCEVAddRecExpr>(Operands[0])) {
    const Loop *NestedLoop = NestedAR->getLoop();
    if (L->contains(NestedLoop)
            ? (L->getLoopDepth() < NestedLoop->getLoopDepth())
            : (!NestedLoop->contains(L) &&
               DT.dominates(L->getHeader(), NestedLoop->getHeader()))) {
      SmallVector<const SCEV *, 4> NestedOperands(NestedAR->op_begin(),
                                                  NestedAR->op_end());
      Operands[0] = NestedAR->getStart();
      // AddRecs require their operands be loop-invariant with respect to their
      // loops. Don't perform this transformation if it would break this
      // requirement.
      bool AllInvariant = all_of(
          Operands, [&](const SCEV *Op) { return isLoopInvariant(Op, L); });

      if (AllInvariant) {
        // Create a recurrence for the outer loop with the same step size.
        //
        // The outer recurrence keeps its NW flag but only keeps NUW/NSW if the
        // inner recurrence has the same property.
        SCEV::NoWrapFlags OuterFlags =
          maskFlags(Flags, SCEV::FlagNW | NestedAR->getNoWrapFlags());

        NestedOperands[0] = getAddRecExpr(Operands, L, OuterFlags);
        AllInvariant = all_of(NestedOperands, [&](const SCEV *Op) {
          return isLoopInvariant(Op, NestedLoop);
        });

        if (AllInvariant) {
          // Ok, both add recurrences are valid after the transformation.
          //
          // The inner recurrence keeps its NW flag but only keeps NUW/NSW if
          // the outer recurrence has the same property.
          SCEV::NoWrapFlags InnerFlags =
            maskFlags(NestedAR->getNoWrapFlags(), SCEV::FlagNW | Flags);
          return getAddRecExpr(NestedOperands, NestedLoop, InnerFlags);
        }
      }
      // Reset Operands to its original state.
      Operands[0] = NestedAR;
    }
  }

  // Okay, it looks like we really DO need an addrec expr.  Check to see if we
  // already have one, otherwise create a new one.
  return getOrCreateAddRecExpr(Operands, L, Flags);
}

const SCEV *
ScalarEvolution::getGEPExpr(GEPOperator *GEP,
                            const SmallVectorImpl<const SCEV *> &IndexExprs) {
  const SCEV *BaseExpr = getSCEV(GEP->getPointerOperand());
  // getSCEV(Base)->getType() has the same address space as Base->getType()
  // because SCEV::getType() preserves the address space.
  Type *IntPtrTy = getEffectiveSCEVType(BaseExpr->getType());
  // FIXME(PR23527): Don't blindly transfer the inbounds flag from the GEP
  // instruction to its SCEV, because the Instruction may be guarded by control
  // flow and the no-overflow bits may not be valid for the expression in any
  // context. This can be fixed similarly to how these flags are handled for
  // adds.
  SCEV::NoWrapFlags Wrap = GEP->isInBounds() ? SCEV::FlagNSW
                                             : SCEV::FlagAnyWrap;

  const SCEV *TotalOffset = getZero(IntPtrTy);
  // The array size is unimportant. The first thing we do on CurTy is getting
  // its element type.
  Type *CurTy = ArrayType::get(GEP->getSourceElementType(), 0);
  for (const SCEV *IndexExpr : IndexExprs) {
    // Compute the (potentially symbolic) offset in bytes for this index.
    if (StructType *STy = dyn_cast<StructType>(CurTy)) {
      // For a struct, add the member offset.
      ConstantInt *Index = cast<SCEVConstant>(IndexExpr)->getValue();
      unsigned FieldNo = Index->getZExtValue();
      const SCEV *FieldOffset = getOffsetOfExpr(IntPtrTy, STy, FieldNo);

      // Add the field offset to the running total offset.
      TotalOffset = getAddExpr(TotalOffset, FieldOffset);

      // Update CurTy to the type of the field at Index.
      CurTy = STy->getTypeAtIndex(Index);
    } else {
      // Update CurTy to its element type.
      CurTy = cast<SequentialType>(CurTy)->getElementType();
      // For an array, add the element offset, explicitly scaled.
      const SCEV *ElementSize = getSizeOfExpr(IntPtrTy, CurTy);
      // Getelementptr indices are signed.
      IndexExpr = getTruncateOrSignExtend(IndexExpr, IntPtrTy);

      // Multiply the index by the element size to compute the element offset.
      const SCEV *LocalOffset = getMulExpr(IndexExpr, ElementSize, Wrap);

      // Add the element offset to the running total offset.
      TotalOffset = getAddExpr(TotalOffset, LocalOffset);
    }
  }

  // Add the total offset from all the GEP indices to the base.
  return getAddExpr(BaseExpr, TotalOffset, Wrap);
}

const SCEV *ScalarEvolution::getSMaxExpr(const SCEV *LHS,
                                         const SCEV *RHS) {
  SmallVector<const SCEV *, 2> Ops = {LHS, RHS};
  return getSMaxExpr(Ops);
}

const SCEV *
ScalarEvolution::getSMaxExpr(SmallVectorImpl<const SCEV *> &Ops) {
  assert(!Ops.empty() && "Cannot get empty smax!");
  if (Ops.size() == 1) return Ops[0];
#ifndef NDEBUG
  Type *ETy = getEffectiveSCEVType(Ops[0]->getType());
  for (unsigned i = 1, e = Ops.size(); i != e; ++i)
    assert(getEffectiveSCEVType(Ops[i]->getType()) == ETy &&
           "SCEVSMaxExpr operand types don't match!");
#endif

  // Sort by complexity, this groups all similar expression types together.
  GroupByComplexity(Ops, &LI, DT);

  // If there are any constants, fold them together.
  unsigned Idx = 0;
  if (const SCEVConstant *LHSC = dyn_cast<SCEVConstant>(Ops[0])) {
    ++Idx;
    assert(Idx < Ops.size());
    while (const SCEVConstant *RHSC = dyn_cast<SCEVConstant>(Ops[Idx])) {
      // We found two constants, fold them together!
      ConstantInt *Fold = ConstantInt::get(
          getContext(), APIntOps::smax(LHSC->getAPInt(), RHSC->getAPInt()));
      Ops[0] = getConstant(Fold);
      Ops.erase(Ops.begin()+1);  // Erase the folded element
      if (Ops.size() == 1) return Ops[0];
      LHSC = cast<SCEVConstant>(Ops[0]);
    }

    // If we are left with a constant minimum-int, strip it off.
    if (cast<SCEVConstant>(Ops[0])->getValue()->isMinValue(true)) {
      Ops.erase(Ops.begin());
      --Idx;
    } else if (cast<SCEVConstant>(Ops[0])->getValue()->isMaxValue(true)) {
      // If we have an smax with a constant maximum-int, it will always be
      // maximum-int.
      return Ops[0];
    }

    if (Ops.size() == 1) return Ops[0];
  }

  // Find the first SMax
  while (Idx < Ops.size() && Ops[Idx]->getSCEVType() < scSMaxExpr)
    ++Idx;

  // Check to see if one of the operands is an SMax. If so, expand its operands
  // onto our operand list, and recurse to simplify.
  if (Idx < Ops.size()) {
    bool DeletedSMax = false;
    while (const SCEVSMaxExpr *SMax = dyn_cast<SCEVSMaxExpr>(Ops[Idx])) {
      Ops.erase(Ops.begin()+Idx);
      Ops.append(SMax->op_begin(), SMax->op_end());
      DeletedSMax = true;
    }

    if (DeletedSMax)
      return getSMaxExpr(Ops);
  }

  // Okay, check to see if the same value occurs in the operand list twice.  If
  // so, delete one.  Since we sorted the list, these values are required to
  // be adjacent.
  for (unsigned i = 0, e = Ops.size()-1; i != e; ++i)
    //  X smax Y smax Y  -->  X smax Y
    //  X smax Y         -->  X, if X is always greater than Y
    if (Ops[i] == Ops[i+1] ||
        isKnownPredicate(ICmpInst::ICMP_SGE, Ops[i], Ops[i+1])) {
      Ops.erase(Ops.begin()+i+1, Ops.begin()+i+2);
      --i; --e;
    } else if (isKnownPredicate(ICmpInst::ICMP_SLE, Ops[i], Ops[i+1])) {
      Ops.erase(Ops.begin()+i, Ops.begin()+i+1);
      --i; --e;
    }

  if (Ops.size() == 1) return Ops[0];

  assert(!Ops.empty() && "Reduced smax down to nothing!");

  // Okay, it looks like we really DO need an smax expr.  Check to see if we
  // already have one, otherwise create a new one.
  FoldingSetNodeID ID;
  ID.AddInteger(scSMaxExpr);
  for (unsigned i = 0, e = Ops.size(); i != e; ++i)
    ID.AddPointer(Ops[i]);
  void *IP = nullptr;
  if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP)) return S;
  const SCEV **O = SCEVAllocator.Allocate<const SCEV *>(Ops.size());
  std::uninitialized_copy(Ops.begin(), Ops.end(), O);
  SCEV *S = new (SCEVAllocator) SCEVSMaxExpr(ID.Intern(SCEVAllocator),
                                             O, Ops.size());
  UniqueSCEVs.InsertNode(S, IP);
  addToLoopUseLists(S);
  return S;
}

const SCEV *ScalarEvolution::getUMaxExpr(const SCEV *LHS,
                                         const SCEV *RHS) {
  SmallVector<const SCEV *, 2> Ops = {LHS, RHS};
  return getUMaxExpr(Ops);
}

const SCEV *
ScalarEvolution::getUMaxExpr(SmallVectorImpl<const SCEV *> &Ops) {
  assert(!Ops.empty() && "Cannot get empty umax!");
  if (Ops.size() == 1) return Ops[0];
#ifndef NDEBUG
  Type *ETy = getEffectiveSCEVType(Ops[0]->getType());
  for (unsigned i = 1, e = Ops.size(); i != e; ++i)
    assert(getEffectiveSCEVType(Ops[i]->getType()) == ETy &&
           "SCEVUMaxExpr operand types don't match!");
#endif

  // Sort by complexity, this groups all similar expression types together.
  GroupByComplexity(Ops, &LI, DT);

  // If there are any constants, fold them together.
  unsigned Idx = 0;
  if (const SCEVConstant *LHSC = dyn_cast<SCEVConstant>(Ops[0])) {
    ++Idx;
    assert(Idx < Ops.size());
    while (const SCEVConstant *RHSC = dyn_cast<SCEVConstant>(Ops[Idx])) {
      // We found two constants, fold them together!
      ConstantInt *Fold = ConstantInt::get(
          getContext(), APIntOps::umax(LHSC->getAPInt(), RHSC->getAPInt()));
      Ops[0] = getConstant(Fold);
      Ops.erase(Ops.begin()+1);  // Erase the folded element
      if (Ops.size() == 1) return Ops[0];
      LHSC = cast<SCEVConstant>(Ops[0]);
    }

    // If we are left with a constant minimum-int, strip it off.
    if (cast<SCEVConstant>(Ops[0])->getValue()->isMinValue(false)) {
      Ops.erase(Ops.begin());
      --Idx;
    } else if (cast<SCEVConstant>(Ops[0])->getValue()->isMaxValue(false)) {
      // If we have an umax with a constant maximum-int, it will always be
      // maximum-int.
      return Ops[0];
    }

    if (Ops.size() == 1) return Ops[0];
  }

  // Find the first UMax
  while (Idx < Ops.size() && Ops[Idx]->getSCEVType() < scUMaxExpr)
    ++Idx;

  // Check to see if one of the operands is a UMax. If so, expand its operands
  // onto our operand list, and recurse to simplify.
  if (Idx < Ops.size()) {
    bool DeletedUMax = false;
    while (const SCEVUMaxExpr *UMax = dyn_cast<SCEVUMaxExpr>(Ops[Idx])) {
      Ops.erase(Ops.begin()+Idx);
      Ops.append(UMax->op_begin(), UMax->op_end());
      DeletedUMax = true;
    }

    if (DeletedUMax)
      return getUMaxExpr(Ops);
  }

  // Okay, check to see if the same value occurs in the operand list twice.  If
  // so, delete one.  Since we sorted the list, these values are required to
  // be adjacent.
  for (unsigned i = 0, e = Ops.size()-1; i != e; ++i)
    //  X umax Y umax Y  -->  X umax Y
    //  X umax Y         -->  X, if X is always greater than Y
    if (Ops[i] == Ops[i + 1] || isKnownViaNonRecursiveReasoning(
                                    ICmpInst::ICMP_UGE, Ops[i], Ops[i + 1])) {
      Ops.erase(Ops.begin() + i + 1, Ops.begin() + i + 2);
      --i; --e;
    } else if (isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_ULE, Ops[i],
                                               Ops[i + 1])) {
      Ops.erase(Ops.begin() + i, Ops.begin() + i + 1);
      --i; --e;
    }

  if (Ops.size() == 1) return Ops[0];

  assert(!Ops.empty() && "Reduced umax down to nothing!");

  // Okay, it looks like we really DO need a umax expr.  Check to see if we
  // already have one, otherwise create a new one.
  FoldingSetNodeID ID;
  ID.AddInteger(scUMaxExpr);
  for (unsigned i = 0, e = Ops.size(); i != e; ++i)
    ID.AddPointer(Ops[i]);
  void *IP = nullptr;
  if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP)) return S;
  const SCEV **O = SCEVAllocator.Allocate<const SCEV *>(Ops.size());
  std::uninitialized_copy(Ops.begin(), Ops.end(), O);
  SCEV *S = new (SCEVAllocator) SCEVUMaxExpr(ID.Intern(SCEVAllocator),
                                             O, Ops.size());
  UniqueSCEVs.InsertNode(S, IP);
  addToLoopUseLists(S);
  return S;
}

const SCEV *ScalarEvolution::getSMinExpr(const SCEV *LHS,
                                         const SCEV *RHS) {
  SmallVector<const SCEV *, 2> Ops = { LHS, RHS };
  return getSMinExpr(Ops);
}

const SCEV *ScalarEvolution::getSMinExpr(SmallVectorImpl<const SCEV *> &Ops) {
  // ~smax(~x, ~y, ~z) == smin(x, y, z).
  SmallVector<const SCEV *, 2> NotOps;
  for (auto *S : Ops)
    NotOps.push_back(getNotSCEV(S));
  return getNotSCEV(getSMaxExpr(NotOps));
}

const SCEV *ScalarEvolution::getUMinExpr(const SCEV *LHS,
                                         const SCEV *RHS) {
  SmallVector<const SCEV *, 2> Ops = { LHS, RHS };
  return getUMinExpr(Ops);
}

const SCEV *ScalarEvolution::getUMinExpr(SmallVectorImpl<const SCEV *> &Ops) {
  assert(!Ops.empty() && "At least one operand must be!");
  // Trivial case.
  if (Ops.size() == 1)
    return Ops[0];

  // ~umax(~x, ~y, ~z) == umin(x, y, z).
  SmallVector<const SCEV *, 2> NotOps;
  for (auto *S : Ops)
    NotOps.push_back(getNotSCEV(S));
  return getNotSCEV(getUMaxExpr(NotOps));
}

const SCEV *ScalarEvolution::getSizeOfExpr(Type *IntTy, Type *AllocTy) {
  // We can bypass creating a target-independent
  // constant expression and then folding it back into a ConstantInt.
  // This is just a compile-time optimization.
  return getConstant(IntTy, getDataLayout().getTypeAllocSize(AllocTy));
}

const SCEV *ScalarEvolution::getOffsetOfExpr(Type *IntTy,
                                             StructType *STy,
                                             unsigned FieldNo) {
  // We can bypass creating a target-independent
  // constant expression and then folding it back into a ConstantInt.
  // This is just a compile-time optimization.
  return getConstant(
      IntTy, getDataLayout().getStructLayout(STy)->getElementOffset(FieldNo));
}

const SCEV *ScalarEvolution::getUnknown(Value *V) {
  // Don't attempt to do anything other than create a SCEVUnknown object
  // here.  createSCEV only calls getUnknown after checking for all other
  // interesting possibilities, and any other code that calls getUnknown
  // is doing so in order to hide a value from SCEV canonicalization.

  FoldingSetNodeID ID;
  ID.AddInteger(scUnknown);
  ID.AddPointer(V);
  void *IP = nullptr;
  if (SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP)) {
    assert(cast<SCEVUnknown>(S)->getValue() == V &&
           "Stale SCEVUnknown in uniquing map!");
    return S;
  }
  SCEV *S = new (SCEVAllocator) SCEVUnknown(ID.Intern(SCEVAllocator), V, this,
                                            FirstUnknown);
  FirstUnknown = cast<SCEVUnknown>(S);
  UniqueSCEVs.InsertNode(S, IP);
  return S;
}

//===----------------------------------------------------------------------===//
//            Basic SCEV Analysis and PHI Idiom Recognition Code
//

/// Test if values of the given type are analyzable within the SCEV
/// framework. This primarily includes integer types, and it can optionally
/// include pointer types if the ScalarEvolution class has access to
/// target-specific information.
bool ScalarEvolution::isSCEVable(Type *Ty) const {
  // Integers and pointers are always SCEVable.
  return Ty->isIntOrPtrTy();
}

/// Return the size in bits of the specified type, for which isSCEVable must
/// return true.
uint64_t ScalarEvolution::getTypeSizeInBits(Type *Ty) const {
  assert(isSCEVable(Ty) && "Type is not SCEVable!");
  if (Ty->isPointerTy())
    return getDataLayout().getIndexTypeSizeInBits(Ty);
  return getDataLayout().getTypeSizeInBits(Ty);
}

/// Return a type with the same bitwidth as the given type and which represents
/// how SCEV will treat the given type, for which isSCEVable must return
/// true. For pointer types, this is the pointer-sized integer type.
Type *ScalarEvolution::getEffectiveSCEVType(Type *Ty) const {
  assert(isSCEVable(Ty) && "Type is not SCEVable!");

  if (Ty->isIntegerTy())
    return Ty;

  // The only other support type is pointer.
  assert(Ty->isPointerTy() && "Unexpected non-pointer non-integer type!");
  return getDataLayout().getIntPtrType(Ty);
}

Type *ScalarEvolution::getWiderType(Type *T1, Type *T2) const {
  return  getTypeSizeInBits(T1) >= getTypeSizeInBits(T2) ? T1 : T2;
}

const SCEV *ScalarEvolution::getCouldNotCompute() {
  return CouldNotCompute.get();
}

bool ScalarEvolution::checkValidity(const SCEV *S) const {
  bool ContainsNulls = SCEVExprContains(S, [](const SCEV *S) {
    auto *SU = dyn_cast<SCEVUnknown>(S);
    return SU && SU->getValue() == nullptr;
  });

  return !ContainsNulls;
}

bool ScalarEvolution::containsAddRecurrence(const SCEV *S) {
  HasRecMapType::iterator I = HasRecMap.find(S);
  if (I != HasRecMap.end())
    return I->second;

  bool FoundAddRec = SCEVExprContains(S, isa<SCEVAddRecExpr, const SCEV *>);
  HasRecMap.insert({S, FoundAddRec});
  return FoundAddRec;
}

/// Try to split a SCEVAddExpr into a pair of {SCEV, ConstantInt}.
/// If \p S is a SCEVAddExpr and is composed of a sub SCEV S' and an
/// offset I, then return {S', I}, else return {\p S, nullptr}.
static std::pair<const SCEV *, ConstantInt *> splitAddExpr(const SCEV *S) {
  const auto *Add = dyn_cast<SCEVAddExpr>(S);
  if (!Add)
    return {S, nullptr};

  if (Add->getNumOperands() != 2)
    return {S, nullptr};

  auto *ConstOp = dyn_cast<SCEVConstant>(Add->getOperand(0));
  if (!ConstOp)
    return {S, nullptr};

  return {Add->getOperand(1), ConstOp->getValue()};
}

/// Return the ValueOffsetPair set for \p S. \p S can be represented
/// by the value and offset from any ValueOffsetPair in the set.
SetVector<ScalarEvolution::ValueOffsetPair> *
ScalarEvolution::getSCEVValues(const SCEV *S) {
  ExprValueMapType::iterator SI = ExprValueMap.find_as(S);
  if (SI == ExprValueMap.end())
    return nullptr;
#ifndef NDEBUG
  if (VerifySCEVMap) {
    // Check there is no dangling Value in the set returned.
    for (const auto &VE : SI->second)
      assert(ValueExprMap.count(VE.first));
  }
#endif
  return &SI->second;
}

/// Erase Value from ValueExprMap and ExprValueMap. ValueExprMap.erase(V)
/// cannot be used separately. eraseValueFromMap should be used to remove
/// V from ValueExprMap and ExprValueMap at the same time.
void ScalarEvolution::eraseValueFromMap(Value *V) {
  ValueExprMapType::iterator I = ValueExprMap.find_as(V);
  if (I != ValueExprMap.end()) {
    const SCEV *S = I->second;
    // Remove {V, 0} from the set of ExprValueMap[S]
    if (SetVector<ValueOffsetPair> *SV = getSCEVValues(S))
      SV->remove({V, nullptr});

    // Remove {V, Offset} from the set of ExprValueMap[Stripped]
    const SCEV *Stripped;
    ConstantInt *Offset;
    std::tie(Stripped, Offset) = splitAddExpr(S);
    if (Offset != nullptr) {
      if (SetVector<ValueOffsetPair> *SV = getSCEVValues(Stripped))
        SV->remove({V, Offset});
    }
    ValueExprMap.erase(V);
  }
}

/// Check whether value has nuw/nsw/exact set but SCEV does not.
/// TODO: In reality it is better to check the poison recursevely
/// but this is better than nothing.
static bool SCEVLostPoisonFlags(const SCEV *S, const Value *V) {
  if (auto *I = dyn_cast<Instruction>(V)) {
    if (isa<OverflowingBinaryOperator>(I)) {
      if (auto *NS = dyn_cast<SCEVNAryExpr>(S)) {
        if (I->hasNoSignedWrap() && !NS->hasNoSignedWrap())
          return true;
        if (I->hasNoUnsignedWrap() && !NS->hasNoUnsignedWrap())
          return true;
      }
    } else if (isa<PossiblyExactOperator>(I) && I->isExact())
      return true;
  }
  return false;
}

/// Return an existing SCEV if it exists, otherwise analyze the expression and
/// create a new one.
const SCEV *ScalarEvolution::getSCEV(Value *V) {
  assert(isSCEVable(V->getType()) && "Value is not SCEVable!");

  const SCEV *S = getExistingSCEV(V);
  if (S == nullptr) {
    S = createSCEV(V);
    // During PHI resolution, it is possible to create two SCEVs for the same
    // V, so it is needed to double check whether V->S is inserted into
    // ValueExprMap before insert S->{V, 0} into ExprValueMap.
    std::pair<ValueExprMapType::iterator, bool> Pair =
        ValueExprMap.insert({SCEVCallbackVH(V, this), S});
    if (Pair.second && !SCEVLostPoisonFlags(S, V)) {
      ExprValueMap[S].insert({V, nullptr});

      // If S == Stripped + Offset, add Stripped -> {V, Offset} into
      // ExprValueMap.
      const SCEV *Stripped = S;
      ConstantInt *Offset = nullptr;
      std::tie(Stripped, Offset) = splitAddExpr(S);
      // If stripped is SCEVUnknown, don't bother to save
      // Stripped -> {V, offset}. It doesn't simplify and sometimes even
      // increase the complexity of the expansion code.
      // If V is GetElementPtrInst, don't save Stripped -> {V, offset}
      // because it may generate add/sub instead of GEP in SCEV expansion.
      if (Offset != nullptr && !isa<SCEVUnknown>(Stripped) &&
          !isa<GetElementPtrInst>(V))
        ExprValueMap[Stripped].insert({V, Offset});
    }
  }
  return S;
}

const SCEV *ScalarEvolution::getExistingSCEV(Value *V) {
  assert(isSCEVable(V->getType()) && "Value is not SCEVable!");

  ValueExprMapType::iterator I = ValueExprMap.find_as(V);
  if (I != ValueExprMap.end()) {
    const SCEV *S = I->second;
    if (checkValidity(S))
      return S;
    eraseValueFromMap(V);
    forgetMemoizedResults(S);
  }
  return nullptr;
}

/// Return a SCEV corresponding to -V = -1*V
const SCEV *ScalarEvolution::getNegativeSCEV(const SCEV *V,
                                             SCEV::NoWrapFlags Flags) {
  if (const SCEVConstant *VC = dyn_cast<SCEVConstant>(V))
    return getConstant(
               cast<ConstantInt>(ConstantExpr::getNeg(VC->getValue())));

  Type *Ty = V->getType();
  Ty = getEffectiveSCEVType(Ty);
  return getMulExpr(
      V, getConstant(cast<ConstantInt>(Constant::getAllOnesValue(Ty))), Flags);
}

/// Return a SCEV corresponding to ~V = -1-V
const SCEV *ScalarEvolution::getNotSCEV(const SCEV *V) {
  if (const SCEVConstant *VC = dyn_cast<SCEVConstant>(V))
    return getConstant(
                cast<ConstantInt>(ConstantExpr::getNot(VC->getValue())));

  Type *Ty = V->getType();
  Ty = getEffectiveSCEVType(Ty);
  const SCEV *AllOnes =
                   getConstant(cast<ConstantInt>(Constant::getAllOnesValue(Ty)));
  return getMinusSCEV(AllOnes, V);
}

const SCEV *ScalarEvolution::getMinusSCEV(const SCEV *LHS, const SCEV *RHS,
                                          SCEV::NoWrapFlags Flags,
                                          unsigned Depth) {
  // Fast path: X - X --> 0.
  if (LHS == RHS)
    return getZero(LHS->getType());

  // We represent LHS - RHS as LHS + (-1)*RHS. This transformation
  // makes it so that we cannot make much use of NUW.
  auto AddFlags = SCEV::FlagAnyWrap;
  const bool RHSIsNotMinSigned =
      !getSignedRangeMin(RHS).isMinSignedValue();
  if (maskFlags(Flags, SCEV::FlagNSW) == SCEV::FlagNSW) {
    // Let M be the minimum representable signed value. Then (-1)*RHS
    // signed-wraps if and only if RHS is M. That can happen even for
    // a NSW subtraction because e.g. (-1)*M signed-wraps even though
    // -1 - M does not. So to transfer NSW from LHS - RHS to LHS +
    // (-1)*RHS, we need to prove that RHS != M.
    //
    // If LHS is non-negative and we know that LHS - RHS does not
    // signed-wrap, then RHS cannot be M. So we can rule out signed-wrap
    // either by proving that RHS > M or that LHS >= 0.
    if (RHSIsNotMinSigned || isKnownNonNegative(LHS)) {
      AddFlags = SCEV::FlagNSW;
    }
  }

  // FIXME: Find a correct way to transfer NSW to (-1)*M when LHS -
  // RHS is NSW and LHS >= 0.
  //
  // The difficulty here is that the NSW flag may have been proven
  // relative to a loop that is to be found in a recurrence in LHS and
  // not in RHS. Applying NSW to (-1)*M may then let the NSW have a
  // larger scope than intended.
  auto NegFlags = RHSIsNotMinSigned ? SCEV::FlagNSW : SCEV::FlagAnyWrap;

  return getAddExpr(LHS, getNegativeSCEV(RHS, NegFlags), AddFlags, Depth);
}

const SCEV *
ScalarEvolution::getTruncateOrZeroExtend(const SCEV *V, Type *Ty) {
  Type *SrcTy = V->getType();
  assert(SrcTy->isIntOrPtrTy() && Ty->isIntOrPtrTy() &&
         "Cannot truncate or zero extend with non-integer arguments!");
  if (getTypeSizeInBits(SrcTy) == getTypeSizeInBits(Ty))
    return V;  // No conversion
  if (getTypeSizeInBits(SrcTy) > getTypeSizeInBits(Ty))
    return getTruncateExpr(V, Ty);
  return getZeroExtendExpr(V, Ty);
}

const SCEV *
ScalarEvolution::getTruncateOrSignExtend(const SCEV *V,
                                         Type *Ty) {
  Type *SrcTy = V->getType();
  assert(SrcTy->isIntOrPtrTy() && Ty->isIntOrPtrTy() &&
         "Cannot truncate or zero extend with non-integer arguments!");
  if (getTypeSizeInBits(SrcTy) == getTypeSizeInBits(Ty))
    return V;  // No conversion
  if (getTypeSizeInBits(SrcTy) > getTypeSizeInBits(Ty))
    return getTruncateExpr(V, Ty);
  return getSignExtendExpr(V, Ty);
}

const SCEV *
ScalarEvolution::getNoopOrZeroExtend(const SCEV *V, Type *Ty) {
  Type *SrcTy = V->getType();
  assert(SrcTy->isIntOrPtrTy() && Ty->isIntOrPtrTy() &&
         "Cannot noop or zero extend with non-integer arguments!");
  assert(getTypeSizeInBits(SrcTy) <= getTypeSizeInBits(Ty) &&
         "getNoopOrZeroExtend cannot truncate!");
  if (getTypeSizeInBits(SrcTy) == getTypeSizeInBits(Ty))
    return V;  // No conversion
  return getZeroExtendExpr(V, Ty);
}

const SCEV *
ScalarEvolution::getNoopOrSignExtend(const SCEV *V, Type *Ty) {
  Type *SrcTy = V->getType();
  assert(SrcTy->isIntOrPtrTy() && Ty->isIntOrPtrTy() &&
         "Cannot noop or sign extend with non-integer arguments!");
  assert(getTypeSizeInBits(SrcTy) <= getTypeSizeInBits(Ty) &&
         "getNoopOrSignExtend cannot truncate!");
  if (getTypeSizeInBits(SrcTy) == getTypeSizeInBits(Ty))
    return V;  // No conversion
  return getSignExtendExpr(V, Ty);
}

const SCEV *
ScalarEvolution::getNoopOrAnyExtend(const SCEV *V, Type *Ty) {
  Type *SrcTy = V->getType();
  assert(SrcTy->isIntOrPtrTy() && Ty->isIntOrPtrTy() &&
         "Cannot noop or any extend with non-integer arguments!");
  assert(getTypeSizeInBits(SrcTy) <= getTypeSizeInBits(Ty) &&
         "getNoopOrAnyExtend cannot truncate!");
  if (getTypeSizeInBits(SrcTy) == getTypeSizeInBits(Ty))
    return V;  // No conversion
  return getAnyExtendExpr(V, Ty);
}

const SCEV *
ScalarEvolution::getTruncateOrNoop(const SCEV *V, Type *Ty) {
  Type *SrcTy = V->getType();
  assert(SrcTy->isIntOrPtrTy() && Ty->isIntOrPtrTy() &&
         "Cannot truncate or noop with non-integer arguments!");
  assert(getTypeSizeInBits(SrcTy) >= getTypeSizeInBits(Ty) &&
         "getTruncateOrNoop cannot extend!");
  if (getTypeSizeInBits(SrcTy) == getTypeSizeInBits(Ty))
    return V;  // No conversion
  return getTruncateExpr(V, Ty);
}

const SCEV *ScalarEvolution::getUMaxFromMismatchedTypes(const SCEV *LHS,
                                                        const SCEV *RHS) {
  const SCEV *PromotedLHS = LHS;
  const SCEV *PromotedRHS = RHS;

  if (getTypeSizeInBits(LHS->getType()) > getTypeSizeInBits(RHS->getType()))
    PromotedRHS = getZeroExtendExpr(RHS, LHS->getType());
  else
    PromotedLHS = getNoopOrZeroExtend(LHS, RHS->getType());

  return getUMaxExpr(PromotedLHS, PromotedRHS);
}

const SCEV *ScalarEvolution::getUMinFromMismatchedTypes(const SCEV *LHS,
                                                        const SCEV *RHS) {
  SmallVector<const SCEV *, 2> Ops = { LHS, RHS };
  return getUMinFromMismatchedTypes(Ops);
}

const SCEV *ScalarEvolution::getUMinFromMismatchedTypes(
    SmallVectorImpl<const SCEV *> &Ops) {
  assert(!Ops.empty() && "At least one operand must be!");
  // Trivial case.
  if (Ops.size() == 1)
    return Ops[0];

  // Find the max type first.
  Type *MaxType = nullptr;
  for (auto *S : Ops)
    if (MaxType)
      MaxType = getWiderType(MaxType, S->getType());
    else
      MaxType = S->getType();

  // Extend all ops to max type.
  SmallVector<const SCEV *, 2> PromotedOps;
  for (auto *S : Ops)
    PromotedOps.push_back(getNoopOrZeroExtend(S, MaxType));

  // Generate umin.
  return getUMinExpr(PromotedOps);
}

const SCEV *ScalarEvolution::getPointerBase(const SCEV *V) {
  // A pointer operand may evaluate to a nonpointer expression, such as null.
  if (!V->getType()->isPointerTy())
    return V;

  if (const SCEVCastExpr *Cast = dyn_cast<SCEVCastExpr>(V)) {
    return getPointerBase(Cast->getOperand());
  } else if (const SCEVNAryExpr *NAry = dyn_cast<SCEVNAryExpr>(V)) {
    const SCEV *PtrOp = nullptr;
    for (const SCEV *NAryOp : NAry->operands()) {
      if (NAryOp->getType()->isPointerTy()) {
        // Cannot find the base of an expression with multiple pointer operands.
        if (PtrOp)
          return V;
        PtrOp = NAryOp;
      }
    }
    if (!PtrOp)
      return V;
    return getPointerBase(PtrOp);
  }
  return V;
}

/// Push users of the given Instruction onto the given Worklist.
static void
PushDefUseChildren(Instruction *I,
                   SmallVectorImpl<Instruction *> &Worklist) {
  // Push the def-use children onto the Worklist stack.
  for (User *U : I->users())
    Worklist.push_back(cast<Instruction>(U));
}

void ScalarEvolution::forgetSymbolicName(Instruction *PN, const SCEV *SymName) {
  SmallVector<Instruction *, 16> Worklist;
  PushDefUseChildren(PN, Worklist);

  SmallPtrSet<Instruction *, 8> Visited;
  Visited.insert(PN);
  while (!Worklist.empty()) {
    Instruction *I = Worklist.pop_back_val();
    if (!Visited.insert(I).second)
      continue;

    auto It = ValueExprMap.find_as(static_cast<Value *>(I));
    if (It != ValueExprMap.end()) {
      const SCEV *Old = It->second;

      // Short-circuit the def-use traversal if the symbolic name
      // ceases to appear in expressions.
      if (Old != SymName && !hasOperand(Old, SymName))
        continue;

      // SCEVUnknown for a PHI either means that it has an unrecognized
      // structure, it's a PHI that's in the progress of being computed
      // by createNodeForPHI, or it's a single-value PHI. In the first case,
      // additional loop trip count information isn't going to change anything.
      // In the second case, createNodeForPHI will perform the necessary
      // updates on its own when it gets to that point. In the third, we do
      // want to forget the SCEVUnknown.
      if (!isa<PHINode>(I) ||
          !isa<SCEVUnknown>(Old) ||
          (I != PN && Old == SymName)) {
        eraseValueFromMap(It->first);
        forgetMemoizedResults(Old);
      }
    }

    PushDefUseChildren(I, Worklist);
  }
}

namespace {

/// Takes SCEV S and Loop L. For each AddRec sub-expression, use its start
/// expression in case its Loop is L. If it is not L then
/// if IgnoreOtherLoops is true then use AddRec itself
/// otherwise rewrite cannot be done.
/// If SCEV contains non-invariant unknown SCEV rewrite cannot be done.
class SCEVInitRewriter : public SCEVRewriteVisitor<SCEVInitRewriter> {
public:
  static const SCEV *rewrite(const SCEV *S, const Loop *L, ScalarEvolution &SE,
                             bool IgnoreOtherLoops = true) {
    SCEVInitRewriter Rewriter(L, SE);
    const SCEV *Result = Rewriter.visit(S);
    if (Rewriter.hasSeenLoopVariantSCEVUnknown())
      return SE.getCouldNotCompute();
    return Rewriter.hasSeenOtherLoops() && !IgnoreOtherLoops
               ? SE.getCouldNotCompute()
               : Result;
  }

  const SCEV *visitUnknown(const SCEVUnknown *Expr) {
    if (!SE.isLoopInvariant(Expr, L))
      SeenLoopVariantSCEVUnknown = true;
    return Expr;
  }

  const SCEV *visitAddRecExpr(const SCEVAddRecExpr *Expr) {
    // Only re-write AddRecExprs for this loop.
    if (Expr->getLoop() == L)
      return Expr->getStart();
    SeenOtherLoops = true;
    return Expr;
  }

  bool hasSeenLoopVariantSCEVUnknown() { return SeenLoopVariantSCEVUnknown; }

  bool hasSeenOtherLoops() { return SeenOtherLoops; }

private:
  explicit SCEVInitRewriter(const Loop *L, ScalarEvolution &SE)
      : SCEVRewriteVisitor(SE), L(L) {}

  const Loop *L;
  bool SeenLoopVariantSCEVUnknown = false;
  bool SeenOtherLoops = false;
};

/// Takes SCEV S and Loop L. For each AddRec sub-expression, use its post
/// increment expression in case its Loop is L. If it is not L then
/// use AddRec itself.
/// If SCEV contains non-invariant unknown SCEV rewrite cannot be done.
class SCEVPostIncRewriter : public SCEVRewriteVisitor<SCEVPostIncRewriter> {
public:
  static const SCEV *rewrite(const SCEV *S, const Loop *L, ScalarEvolution &SE) {
    SCEVPostIncRewriter Rewriter(L, SE);
    const SCEV *Result = Rewriter.visit(S);
    return Rewriter.hasSeenLoopVariantSCEVUnknown()
        ? SE.getCouldNotCompute()
        : Result;
  }

  const SCEV *visitUnknown(const SCEVUnknown *Expr) {
    if (!SE.isLoopInvariant(Expr, L))
      SeenLoopVariantSCEVUnknown = true;
    return Expr;
  }

  const SCEV *visitAddRecExpr(const SCEVAddRecExpr *Expr) {
    // Only re-write AddRecExprs for this loop.
    if (Expr->getLoop() == L)
      return Expr->getPostIncExpr(SE);
    SeenOtherLoops = true;
    return Expr;
  }

  bool hasSeenLoopVariantSCEVUnknown() { return SeenLoopVariantSCEVUnknown; }

  bool hasSeenOtherLoops() { return SeenOtherLoops; }

private:
  explicit SCEVPostIncRewriter(const Loop *L, ScalarEvolution &SE)
      : SCEVRewriteVisitor(SE), L(L) {}

  const Loop *L;
  bool SeenLoopVariantSCEVUnknown = false;
  bool SeenOtherLoops = false;
};

/// This class evaluates the compare condition by matching it against the
/// condition of loop latch. If there is a match we assume a true value
/// for the condition while building SCEV nodes.
class SCEVBackedgeConditionFolder
    : public SCEVRewriteVisitor<SCEVBackedgeConditionFolder> {
public:
  static const SCEV *rewrite(const SCEV *S, const Loop *L,
                             ScalarEvolution &SE) {
    bool IsPosBECond = false;
    Value *BECond = nullptr;
    if (BasicBlock *Latch = L->getLoopLatch()) {
      BranchInst *BI = dyn_cast<BranchInst>(Latch->getTerminator());
      if (BI && BI->isConditional()) {
        assert(BI->getSuccessor(0) != BI->getSuccessor(1) &&
               "Both outgoing branches should not target same header!");
        BECond = BI->getCondition();
        IsPosBECond = BI->getSuccessor(0) == L->getHeader();
      } else {
        return S;
      }
    }
    SCEVBackedgeConditionFolder Rewriter(L, BECond, IsPosBECond, SE);
    return Rewriter.visit(S);
  }

  const SCEV *visitUnknown(const SCEVUnknown *Expr) {
    const SCEV *Result = Expr;
    bool InvariantF = SE.isLoopInvariant(Expr, L);

    if (!InvariantF) {
      Instruction *I = cast<Instruction>(Expr->getValue());
      switch (I->getOpcode()) {
      case Instruction::Select: {
        SelectInst *SI = cast<SelectInst>(I);
        Optional<const SCEV *> Res =
            compareWithBackedgeCondition(SI->getCondition());
        if (Res.hasValue()) {
          bool IsOne = cast<SCEVConstant>(Res.getValue())->getValue()->isOne();
          Result = SE.getSCEV(IsOne ? SI->getTrueValue() : SI->getFalseValue());
        }
        break;
      }
      default: {
        Optional<const SCEV *> Res = compareWithBackedgeCondition(I);
        if (Res.hasValue())
          Result = Res.getValue();
        break;
      }
      }
    }
    return Result;
  }

private:
  explicit SCEVBackedgeConditionFolder(const Loop *L, Value *BECond,
                                       bool IsPosBECond, ScalarEvolution &SE)
      : SCEVRewriteVisitor(SE), L(L), BackedgeCond(BECond),
        IsPositiveBECond(IsPosBECond) {}

  Optional<const SCEV *> compareWithBackedgeCondition(Value *IC);

  const Loop *L;
  /// Loop back condition.
  Value *BackedgeCond = nullptr;
  /// Set to true if loop back is on positive branch condition.
  bool IsPositiveBECond;
};

Optional<const SCEV *>
SCEVBackedgeConditionFolder::compareWithBackedgeCondition(Value *IC) {

  // If value matches the backedge condition for loop latch,
  // then return a constant evolution node based on loopback
  // branch taken.
  if (BackedgeCond == IC)
    return IsPositiveBECond ? SE.getOne(Type::getInt1Ty(SE.getContext()))
                            : SE.getZero(Type::getInt1Ty(SE.getContext()));
  return None;
}

class SCEVShiftRewriter : public SCEVRewriteVisitor<SCEVShiftRewriter> {
public:
  static const SCEV *rewrite(const SCEV *S, const Loop *L,
                             ScalarEvolution &SE) {
    SCEVShiftRewriter Rewriter(L, SE);
    const SCEV *Result = Rewriter.visit(S);
    return Rewriter.isValid() ? Result : SE.getCouldNotCompute();
  }

  const SCEV *visitUnknown(const SCEVUnknown *Expr) {
    // Only allow AddRecExprs for this loop.
    if (!SE.isLoopInvariant(Expr, L))
      Valid = false;
    return Expr;
  }

  const SCEV *visitAddRecExpr(const SCEVAddRecExpr *Expr) {
    if (Expr->getLoop() == L && Expr->isAffine())
      return SE.getMinusSCEV(Expr, Expr->getStepRecurrence(SE));
    Valid = false;
    return Expr;
  }

  bool isValid() { return Valid; }

private:
  explicit SCEVShiftRewriter(const Loop *L, ScalarEvolution &SE)
      : SCEVRewriteVisitor(SE), L(L) {}

  const Loop *L;
  bool Valid = true;
};

} // end anonymous namespace

SCEV::NoWrapFlags
ScalarEvolution::proveNoWrapViaConstantRanges(const SCEVAddRecExpr *AR) {
  if (!AR->isAffine())
    return SCEV::FlagAnyWrap;

  using OBO = OverflowingBinaryOperator;

  SCEV::NoWrapFlags Result = SCEV::FlagAnyWrap;

  if (!AR->hasNoSignedWrap()) {
    ConstantRange AddRecRange = getSignedRange(AR);
    ConstantRange IncRange = getSignedRange(AR->getStepRecurrence(*this));

    auto NSWRegion = ConstantRange::makeGuaranteedNoWrapRegion(
        Instruction::Add, IncRange, OBO::NoSignedWrap);
    if (NSWRegion.contains(AddRecRange))
      Result = ScalarEvolution::setFlags(Result, SCEV::FlagNSW);
  }

  if (!AR->hasNoUnsignedWrap()) {
    ConstantRange AddRecRange = getUnsignedRange(AR);
    ConstantRange IncRange = getUnsignedRange(AR->getStepRecurrence(*this));

    auto NUWRegion = ConstantRange::makeGuaranteedNoWrapRegion(
        Instruction::Add, IncRange, OBO::NoUnsignedWrap);
    if (NUWRegion.contains(AddRecRange))
      Result = ScalarEvolution::setFlags(Result, SCEV::FlagNUW);
  }

  return Result;
}

namespace {

/// Represents an abstract binary operation.  This may exist as a
/// normal instruction or constant expression, or may have been
/// derived from an expression tree.
struct BinaryOp {
  unsigned Opcode;
  Value *LHS;
  Value *RHS;
  bool IsNSW = false;
  bool IsNUW = false;

  /// Op is set if this BinaryOp corresponds to a concrete LLVM instruction or
  /// constant expression.
  Operator *Op = nullptr;

  explicit BinaryOp(Operator *Op)
      : Opcode(Op->getOpcode()), LHS(Op->getOperand(0)), RHS(Op->getOperand(1)),
        Op(Op) {
    if (auto *OBO = dyn_cast<OverflowingBinaryOperator>(Op)) {
      IsNSW = OBO->hasNoSignedWrap();
      IsNUW = OBO->hasNoUnsignedWrap();
    }
  }

  explicit BinaryOp(unsigned Opcode, Value *LHS, Value *RHS, bool IsNSW = false,
                    bool IsNUW = false)
      : Opcode(Opcode), LHS(LHS), RHS(RHS), IsNSW(IsNSW), IsNUW(IsNUW) {}
};

} // end anonymous namespace

/// Try to map \p V into a BinaryOp, and return \c None on failure.
static Optional<BinaryOp> MatchBinaryOp(Value *V, DominatorTree &DT) {
  auto *Op = dyn_cast<Operator>(V);
  if (!Op)
    return None;

  // Implementation detail: all the cleverness here should happen without
  // creating new SCEV expressions -- our caller knowns tricks to avoid creating
  // SCEV expressions when possible, and we should not break that.

  switch (Op->getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::UDiv:
  case Instruction::URem:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::AShr:
  case Instruction::Shl:
    return BinaryOp(Op);

  case Instruction::Xor:
    if (auto *RHSC = dyn_cast<ConstantInt>(Op->getOperand(1)))
      // If the RHS of the xor is a signmask, then this is just an add.
      // Instcombine turns add of signmask into xor as a strength reduction step.
      if (RHSC->getValue().isSignMask())
        return BinaryOp(Instruction::Add, Op->getOperand(0), Op->getOperand(1));
    return BinaryOp(Op);

  case Instruction::LShr:
    // Turn logical shift right of a constant into a unsigned divide.
    if (ConstantInt *SA = dyn_cast<ConstantInt>(Op->getOperand(1))) {
      uint32_t BitWidth = cast<IntegerType>(Op->getType())->getBitWidth();

      // If the shift count is not less than the bitwidth, the result of
      // the shift is undefined. Don't try to analyze it, because the
      // resolution chosen here may differ from the resolution chosen in
      // other parts of the compiler.
      if (SA->getValue().ult(BitWidth)) {
        Constant *X =
            ConstantInt::get(SA->getContext(),
                             APInt::getOneBitSet(BitWidth, SA->getZExtValue()));
        return BinaryOp(Instruction::UDiv, Op->getOperand(0), X);
      }
    }
    return BinaryOp(Op);

  case Instruction::ExtractValue: {
    auto *EVI = cast<ExtractValueInst>(Op);
    if (EVI->getNumIndices() != 1 || EVI->getIndices()[0] != 0)
      break;

    auto *CI = dyn_cast<CallInst>(EVI->getAggregateOperand());
    if (!CI)
      break;

    if (auto *F = CI->getCalledFunction())
      switch (F->getIntrinsicID()) {
      case Intrinsic::sadd_with_overflow:
      case Intrinsic::uadd_with_overflow:
        if (!isOverflowIntrinsicNoWrap(cast<IntrinsicInst>(CI), DT))
          return BinaryOp(Instruction::Add, CI->getArgOperand(0),
                          CI->getArgOperand(1));

        // Now that we know that all uses of the arithmetic-result component of
        // CI are guarded by the overflow check, we can go ahead and pretend
        // that the arithmetic is non-overflowing.
        if (F->getIntrinsicID() == Intrinsic::sadd_with_overflow)
          return BinaryOp(Instruction::Add, CI->getArgOperand(0),
                          CI->getArgOperand(1), /* IsNSW = */ true,
                          /* IsNUW = */ false);
        else
          return BinaryOp(Instruction::Add, CI->getArgOperand(0),
                          CI->getArgOperand(1), /* IsNSW = */ false,
                          /* IsNUW*/ true);
      case Intrinsic::ssub_with_overflow:
      case Intrinsic::usub_with_overflow:
        if (!isOverflowIntrinsicNoWrap(cast<IntrinsicInst>(CI), DT))
          return BinaryOp(Instruction::Sub, CI->getArgOperand(0),
                          CI->getArgOperand(1));

        // The same reasoning as sadd/uadd above.
        if (F->getIntrinsicID() == Intrinsic::ssub_with_overflow)
          return BinaryOp(Instruction::Sub, CI->getArgOperand(0),
                          CI->getArgOperand(1), /* IsNSW = */ true,
                          /* IsNUW = */ false);
        else
          return BinaryOp(Instruction::Sub, CI->getArgOperand(0),
                          CI->getArgOperand(1), /* IsNSW = */ false,
                          /* IsNUW = */ true);
      case Intrinsic::smul_with_overflow:
      case Intrinsic::umul_with_overflow:
        return BinaryOp(Instruction::Mul, CI->getArgOperand(0),
                        CI->getArgOperand(1));
      default:
        break;
      }
    break;
  }

  default:
    break;
  }

  return None;
}

/// Helper function to createAddRecFromPHIWithCasts. We have a phi
/// node whose symbolic (unknown) SCEV is \p SymbolicPHI, which is updated via
/// the loop backedge by a SCEVAddExpr, possibly also with a few casts on the
/// way. This function checks if \p Op, an operand of this SCEVAddExpr,
/// follows one of the following patterns:
/// Op == (SExt ix (Trunc iy (%SymbolicPHI) to ix) to iy)
/// Op == (ZExt ix (Trunc iy (%SymbolicPHI) to ix) to iy)
/// If the SCEV expression of \p Op conforms with one of the expected patterns
/// we return the type of the truncation operation, and indicate whether the
/// truncated type should be treated as signed/unsigned by setting
/// \p Signed to true/false, respectively.
static Type *isSimpleCastedPHI(const SCEV *Op, const SCEVUnknown *SymbolicPHI,
                               bool &Signed, ScalarEvolution &SE) {
  // The case where Op == SymbolicPHI (that is, with no type conversions on
  // the way) is handled by the regular add recurrence creating logic and
  // would have already been triggered in createAddRecForPHI. Reaching it here
  // means that createAddRecFromPHI had failed for this PHI before (e.g.,
  // because one of the other operands of the SCEVAddExpr updating this PHI is
  // not invariant).
  //
  // Here we look for the case where Op = (ext(trunc(SymbolicPHI))), and in
  // this case predicates that allow us to prove that Op == SymbolicPHI will
  // be added.
  if (Op == SymbolicPHI)
    return nullptr;

  unsigned SourceBits = SE.getTypeSizeInBits(SymbolicPHI->getType());
  unsigned NewBits = SE.getTypeSizeInBits(Op->getType());
  if (SourceBits != NewBits)
    return nullptr;

  const SCEVSignExtendExpr *SExt = dyn_cast<SCEVSignExtendExpr>(Op);
  const SCEVZeroExtendExpr *ZExt = dyn_cast<SCEVZeroExtendExpr>(Op);
  if (!SExt && !ZExt)
    return nullptr;
  const SCEVTruncateExpr *Trunc =
      SExt ? dyn_cast<SCEVTruncateExpr>(SExt->getOperand())
           : dyn_cast<SCEVTruncateExpr>(ZExt->getOperand());
  if (!Trunc)
    return nullptr;
  const SCEV *X = Trunc->getOperand();
  if (X != SymbolicPHI)
    return nullptr;
  Signed = SExt != nullptr;
  return Trunc->getType();
}

static const Loop *isIntegerLoopHeaderPHI(const PHINode *PN, LoopInfo &LI) {
  if (!PN->getType()->isIntegerTy())
    return nullptr;
  const Loop *L = LI.getLoopFor(PN->getParent());
  if (!L || L->getHeader() != PN->getParent())
    return nullptr;
  return L;
}

// Analyze \p SymbolicPHI, a SCEV expression of a phi node, and check if the
// computation that updates the phi follows the following pattern:
//   (SExt/ZExt ix (Trunc iy (%SymbolicPHI) to ix) to iy) + InvariantAccum
// which correspond to a phi->trunc->sext/zext->add->phi update chain.
// If so, try to see if it can be rewritten as an AddRecExpr under some
// Predicates. If successful, return them as a pair. Also cache the results
// of the analysis.
//
// Example usage scenario:
//    Say the Rewriter is called for the following SCEV:
//         8 * ((sext i32 (trunc i64 %X to i32) to i64) + %Step)
//    where:
//         %X = phi i64 (%Start, %BEValue)
//    It will visitMul->visitAdd->visitSExt->visitTrunc->visitUnknown(%X),
//    and call this function with %SymbolicPHI = %X.
//
//    The analysis will find that the value coming around the backedge has
//    the following SCEV:
//         BEValue = ((sext i32 (trunc i64 %X to i32) to i64) + %Step)
//    Upon concluding that this matches the desired pattern, the function
//    will return the pair {NewAddRec, SmallPredsVec} where:
//         NewAddRec = {%Start,+,%Step}
//         SmallPredsVec = {P1, P2, P3} as follows:
//           P1(WrapPred): AR: {trunc(%Start),+,(trunc %Step)}<nsw> Flags: <nssw>
//           P2(EqualPred): %Start == (sext i32 (trunc i64 %Start to i32) to i64)
//           P3(EqualPred): %Step == (sext i32 (trunc i64 %Step to i32) to i64)
//    The returned pair means that SymbolicPHI can be rewritten into NewAddRec
//    under the predicates {P1,P2,P3}.
//    This predicated rewrite will be cached in PredicatedSCEVRewrites:
//         PredicatedSCEVRewrites[{%X,L}] = {NewAddRec, {P1,P2,P3)}
//
// TODO's:
//
// 1) Extend the Induction descriptor to also support inductions that involve
//    casts: When needed (namely, when we are called in the context of the
//    vectorizer induction analysis), a Set of cast instructions will be
//    populated by this method, and provided back to isInductionPHI. This is
//    needed to allow the vectorizer to properly record them to be ignored by
//    the cost model and to avoid vectorizing them (otherwise these casts,
//    which are redundant under the runtime overflow checks, will be
//    vectorized, which can be costly).
//
// 2) Support additional induction/PHISCEV patterns: We also want to support
//    inductions where the sext-trunc / zext-trunc operations (partly) occur
//    after the induction update operation (the induction increment):
//
//      (Trunc iy (SExt/ZExt ix (%SymbolicPHI + InvariantAccum) to iy) to ix)
//    which correspond to a phi->add->trunc->sext/zext->phi update chain.
//
//      (Trunc iy ((SExt/ZExt ix (%SymbolicPhi) to iy) + InvariantAccum) to ix)
//    which correspond to a phi->trunc->add->sext/zext->phi update chain.
//
// 3) Outline common code with createAddRecFromPHI to avoid duplication.
Optional<std::pair<const SCEV *, SmallVector<const SCEVPredicate *, 3>>>
ScalarEvolution::createAddRecFromPHIWithCastsImpl(const SCEVUnknown *SymbolicPHI) {
  SmallVector<const SCEVPredicate *, 3> Predicates;

  // *** Part1: Analyze if we have a phi-with-cast pattern for which we can
  // return an AddRec expression under some predicate.

  auto *PN = cast<PHINode>(SymbolicPHI->getValue());
  const Loop *L = isIntegerLoopHeaderPHI(PN, LI);
  assert(L && "Expecting an integer loop header phi");

  // The loop may have multiple entrances or multiple exits; we can analyze
  // this phi as an addrec if it has a unique entry value and a unique
  // backedge value.
  Value *BEValueV = nullptr, *StartValueV = nullptr;
  for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
    Value *V = PN->getIncomingValue(i);
    if (L->contains(PN->getIncomingBlock(i))) {
      if (!BEValueV) {
        BEValueV = V;
      } else if (BEValueV != V) {
        BEValueV = nullptr;
        break;
      }
    } else if (!StartValueV) {
      StartValueV = V;
    } else if (StartValueV != V) {
      StartValueV = nullptr;
      break;
    }
  }
  if (!BEValueV || !StartValueV)
    return None;

  const SCEV *BEValue = getSCEV(BEValueV);

  // If the value coming around the backedge is an add with the symbolic
  // value we just inserted, possibly with casts that we can ignore under
  // an appropriate runtime guard, then we found a simple induction variable!
  const auto *Add = dyn_cast<SCEVAddExpr>(BEValue);
  if (!Add)
    return None;

  // If there is a single occurrence of the symbolic value, possibly
  // casted, replace it with a recurrence.
  unsigned FoundIndex = Add->getNumOperands();
  Type *TruncTy = nullptr;
  bool Signed;
  for (unsigned i = 0, e = Add->getNumOperands(); i != e; ++i)
    if ((TruncTy =
             isSimpleCastedPHI(Add->getOperand(i), SymbolicPHI, Signed, *this)))
      if (FoundIndex == e) {
        FoundIndex = i;
        break;
      }

  if (FoundIndex == Add->getNumOperands())
    return None;

  // Create an add with everything but the specified operand.
  SmallVector<const SCEV *, 8> Ops;
  for (unsigned i = 0, e = Add->getNumOperands(); i != e; ++i)
    if (i != FoundIndex)
      Ops.push_back(Add->getOperand(i));
  const SCEV *Accum = getAddExpr(Ops);

  // The runtime checks will not be valid if the step amount is
  // varying inside the loop.
  if (!isLoopInvariant(Accum, L))
    return None;

  // *** Part2: Create the predicates

  // Analysis was successful: we have a phi-with-cast pattern for which we
  // can return an AddRec expression under the following predicates:
  //
  // P1: A Wrap predicate that guarantees that Trunc(Start) + i*Trunc(Accum)
  //     fits within the truncated type (does not overflow) for i = 0 to n-1.
  // P2: An Equal predicate that guarantees that
  //     Start = (Ext ix (Trunc iy (Start) to ix) to iy)
  // P3: An Equal predicate that guarantees that
  //     Accum = (Ext ix (Trunc iy (Accum) to ix) to iy)
  //
  // As we next prove, the above predicates guarantee that:
  //     Start + i*Accum = (Ext ix (Trunc iy ( Start + i*Accum ) to ix) to iy)
  //
  //
  // More formally, we want to prove that:
  //     Expr(i+1) = Start + (i+1) * Accum
  //               = (Ext ix (Trunc iy (Expr(i)) to ix) to iy) + Accum
  //
  // Given that:
  // 1) Expr(0) = Start
  // 2) Expr(1) = Start + Accum
  //            = (Ext ix (Trunc iy (Start) to ix) to iy) + Accum :: from P2
  // 3) Induction hypothesis (step i):
  //    Expr(i) = (Ext ix (Trunc iy (Expr(i-1)) to ix) to iy) + Accum
  //
  // Proof:
  //  Expr(i+1) =
  //   = Start + (i+1)*Accum
  //   = (Start + i*Accum) + Accum
  //   = Expr(i) + Accum
  //   = (Ext ix (Trunc iy (Expr(i-1)) to ix) to iy) + Accum + Accum
  //                                                             :: from step i
  //
  //   = (Ext ix (Trunc iy (Start + (i-1)*Accum) to ix) to iy) + Accum + Accum
  //
  //   = (Ext ix (Trunc iy (Start + (i-1)*Accum) to ix) to iy)
  //     + (Ext ix (Trunc iy (Accum) to ix) to iy)
  //     + Accum                                                     :: from P3
  //
  //   = (Ext ix (Trunc iy ((Start + (i-1)*Accum) + Accum) to ix) to iy)
  //     + Accum                            :: from P1: Ext(x)+Ext(y)=>Ext(x+y)
  //
  //   = (Ext ix (Trunc iy (Start + i*Accum) to ix) to iy) + Accum
  //   = (Ext ix (Trunc iy (Expr(i)) to ix) to iy) + Accum
  //
  // By induction, the same applies to all iterations 1<=i<n:
  //

  // Create a truncated addrec for which we will add a no overflow check (P1).
  const SCEV *StartVal = getSCEV(StartValueV);
  const SCEV *PHISCEV =
      getAddRecExpr(getTruncateExpr(StartVal, TruncTy),
                    getTruncateExpr(Accum, TruncTy), L, SCEV::FlagAnyWrap);

  // PHISCEV can be either a SCEVConstant or a SCEVAddRecExpr.
  // ex: If truncated Accum is 0 and StartVal is a constant, then PHISCEV
  // will be constant.
  //
  //  If PHISCEV is a constant, then P1 degenerates into P2 or P3, so we don't
  // add P1.
  if (const auto *AR = dyn_cast<SCEVAddRecExpr>(PHISCEV)) {
    SCEVWrapPredicate::IncrementWrapFlags AddedFlags =
        Signed ? SCEVWrapPredicate::IncrementNSSW
               : SCEVWrapPredicate::IncrementNUSW;
    const SCEVPredicate *AddRecPred = getWrapPredicate(AR, AddedFlags);
    Predicates.push_back(AddRecPred);
  }

  // Create the Equal Predicates P2,P3:

  // It is possible that the predicates P2 and/or P3 are computable at
  // compile time due to StartVal and/or Accum being constants.
  // If either one is, then we can check that now and escape if either P2
  // or P3 is false.

  // Construct the extended SCEV: (Ext ix (Trunc iy (Expr) to ix) to iy)
  // for each of StartVal and Accum
  auto getExtendedExpr = [&](const SCEV *Expr,
                             bool CreateSignExtend) -> const SCEV * {
    assert(isLoopInvariant(Expr, L) && "Expr is expected to be invariant");
    const SCEV *TruncatedExpr = getTruncateExpr(Expr, TruncTy);
    const SCEV *ExtendedExpr =
        CreateSignExtend ? getSignExtendExpr(TruncatedExpr, Expr->getType())
                         : getZeroExtendExpr(TruncatedExpr, Expr->getType());
    return ExtendedExpr;
  };

  // Given:
  //  ExtendedExpr = (Ext ix (Trunc iy (Expr) to ix) to iy
  //               = getExtendedExpr(Expr)
  // Determine whether the predicate P: Expr == ExtendedExpr
  // is known to be false at compile time
  auto PredIsKnownFalse = [&](const SCEV *Expr,
                              const SCEV *ExtendedExpr) -> bool {
    return Expr != ExtendedExpr &&
           isKnownPredicate(ICmpInst::ICMP_NE, Expr, ExtendedExpr);
  };

  const SCEV *StartExtended = getExtendedExpr(StartVal, Signed);
  if (PredIsKnownFalse(StartVal, StartExtended)) {
    LLVM_DEBUG(dbgs() << "P2 is compile-time false\n";);
    return None;
  }

  // The Step is always Signed (because the overflow checks are either
  // NSSW or NUSW)
  const SCEV *AccumExtended = getExtendedExpr(Accum, /*CreateSignExtend=*/true);
  if (PredIsKnownFalse(Accum, AccumExtended)) {
    LLVM_DEBUG(dbgs() << "P3 is compile-time false\n";);
    return None;
  }

  auto AppendPredicate = [&](const SCEV *Expr,
                             const SCEV *ExtendedExpr) -> void {
    if (Expr != ExtendedExpr &&
        !isKnownPredicate(ICmpInst::ICMP_EQ, Expr, ExtendedExpr)) {
      const SCEVPredicate *Pred = getEqualPredicate(Expr, ExtendedExpr);
      LLVM_DEBUG(dbgs() << "Added Predicate: " << *Pred);
      Predicates.push_back(Pred);
    }
  };

  AppendPredicate(StartVal, StartExtended);
  AppendPredicate(Accum, AccumExtended);

  // *** Part3: Predicates are ready. Now go ahead and create the new addrec in
  // which the casts had been folded away. The caller can rewrite SymbolicPHI
  // into NewAR if it will also add the runtime overflow checks specified in
  // Predicates.
  auto *NewAR = getAddRecExpr(StartVal, Accum, L, SCEV::FlagAnyWrap);

  std::pair<const SCEV *, SmallVector<const SCEVPredicate *, 3>> PredRewrite =
      std::make_pair(NewAR, Predicates);
  // Remember the result of the analysis for this SCEV at this locayyytion.
  PredicatedSCEVRewrites[{SymbolicPHI, L}] = PredRewrite;
  return PredRewrite;
}

Optional<std::pair<const SCEV *, SmallVector<const SCEVPredicate *, 3>>>
ScalarEvolution::createAddRecFromPHIWithCasts(const SCEVUnknown *SymbolicPHI) {
  auto *PN = cast<PHINode>(SymbolicPHI->getValue());
  const Loop *L = isIntegerLoopHeaderPHI(PN, LI);
  if (!L)
    return None;

  // Check to see if we already analyzed this PHI.
  auto I = PredicatedSCEVRewrites.find({SymbolicPHI, L});
  if (I != PredicatedSCEVRewrites.end()) {
    std::pair<const SCEV *, SmallVector<const SCEVPredicate *, 3>> Rewrite =
        I->second;
    // Analysis was done before and failed to create an AddRec:
    if (Rewrite.first == SymbolicPHI)
      return None;
    // Analysis was done before and succeeded to create an AddRec under
    // a predicate:
    assert(isa<SCEVAddRecExpr>(Rewrite.first) && "Expected an AddRec");
    assert(!(Rewrite.second).empty() && "Expected to find Predicates");
    return Rewrite;
  }

  Optional<std::pair<const SCEV *, SmallVector<const SCEVPredicate *, 3>>>
    Rewrite = createAddRecFromPHIWithCastsImpl(SymbolicPHI);

  // Record in the cache that the analysis failed
  if (!Rewrite) {
    SmallVector<const SCEVPredicate *, 3> Predicates;
    PredicatedSCEVRewrites[{SymbolicPHI, L}] = {SymbolicPHI, Predicates};
    return None;
  }

  return Rewrite;
}

// FIXME: This utility is currently required because the Rewriter currently
// does not rewrite this expression:
// {0, +, (sext ix (trunc iy to ix) to iy)}
// into {0, +, %step},
// even when the following Equal predicate exists:
// "%step == (sext ix (trunc iy to ix) to iy)".
bool PredicatedScalarEvolution::areAddRecsEqualWithPreds(
    const SCEVAddRecExpr *AR1, const SCEVAddRecExpr *AR2) const {
  if (AR1 == AR2)
    return true;

  auto areExprsEqual = [&](const SCEV *Expr1, const SCEV *Expr2) -> bool {
    if (Expr1 != Expr2 && !Preds.implies(SE.getEqualPredicate(Expr1, Expr2)) &&
        !Preds.implies(SE.getEqualPredicate(Expr2, Expr1)))
      return false;
    return true;
  };

  if (!areExprsEqual(AR1->getStart(), AR2->getStart()) ||
      !areExprsEqual(AR1->getStepRecurrence(SE), AR2->getStepRecurrence(SE)))
    return false;
  return true;
}

/// A helper function for createAddRecFromPHI to handle simple cases.
///
/// This function tries to find an AddRec expression for the simplest (yet most
/// common) cases: PN = PHI(Start, OP(Self, LoopInvariant)).
/// If it fails, createAddRecFromPHI will use a more general, but slow,
/// technique for finding the AddRec expression.
const SCEV *ScalarEvolution::createSimpleAffineAddRec(PHINode *PN,
                                                      Value *BEValueV,
                                                      Value *StartValueV) {
  const Loop *L = LI.getLoopFor(PN->getParent());
  assert(L && L->getHeader() == PN->getParent());
  assert(BEValueV && StartValueV);

  auto BO = MatchBinaryOp(BEValueV, DT);
  if (!BO)
    return nullptr;

  if (BO->Opcode != Instruction::Add)
    return nullptr;

  const SCEV *Accum = nullptr;
  if (BO->LHS == PN && L->isLoopInvariant(BO->RHS))
    Accum = getSCEV(BO->RHS);
  else if (BO->RHS == PN && L->isLoopInvariant(BO->LHS))
    Accum = getSCEV(BO->LHS);

  if (!Accum)
    return nullptr;

  SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap;
  if (BO->IsNUW)
    Flags = setFlags(Flags, SCEV::FlagNUW);
  if (BO->IsNSW)
    Flags = setFlags(Flags, SCEV::FlagNSW);

  const SCEV *StartVal = getSCEV(StartValueV);
  const SCEV *PHISCEV = getAddRecExpr(StartVal, Accum, L, Flags);

  ValueExprMap[SCEVCallbackVH(PN, this)] = PHISCEV;

  // We can add Flags to the post-inc expression only if we
  // know that it is *undefined behavior* for BEValueV to
  // overflow.
  if (auto *BEInst = dyn_cast<Instruction>(BEValueV))
    if (isLoopInvariant(Accum, L) && isAddRecNeverPoison(BEInst, L))
      (void)getAddRecExpr(getAddExpr(StartVal, Accum), Accum, L, Flags);

  return PHISCEV;
}

const SCEV *ScalarEvolution::createAddRecFromPHI(PHINode *PN) {
  const Loop *L = LI.getLoopFor(PN->getParent());
  if (!L || L->getHeader() != PN->getParent())
    return nullptr;

  // The loop may have multiple entrances or multiple exits; we can analyze
  // this phi as an addrec if it has a unique entry value and a unique
  // backedge value.
  Value *BEValueV = nullptr, *StartValueV = nullptr;
  for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
    Value *V = PN->getIncomingValue(i);
    if (L->contains(PN->getIncomingBlock(i))) {
      if (!BEValueV) {
        BEValueV = V;
      } else if (BEValueV != V) {
        BEValueV = nullptr;
        break;
      }
    } else if (!StartValueV) {
      StartValueV = V;
    } else if (StartValueV != V) {
      StartValueV = nullptr;
      break;
    }
  }
  if (!BEValueV || !StartValueV)
    return nullptr;

  assert(ValueExprMap.find_as(PN) == ValueExprMap.end() &&
         "PHI node already processed?");

  // First, try to find AddRec expression without creating a fictituos symbolic
  // value for PN.
  if (auto *S = createSimpleAffineAddRec(PN, BEValueV, StartValueV))
    return S;

  // Handle PHI node value symbolically.
  const SCEV *SymbolicName = getUnknown(PN);
  ValueExprMap.insert({SCEVCallbackVH(PN, this), SymbolicName});

  // Using this symbolic name for the PHI, analyze the value coming around
  // the back-edge.
  const SCEV *BEValue = getSCEV(BEValueV);

  // NOTE: If BEValue is loop invariant, we know that the PHI node just
  // has a special value for the first iteration of the loop.

  // If the value coming around the backedge is an add with the symbolic
  // value we just inserted, then we found a simple induction variable!
  if (const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(BEValue)) {
    // If there is a single occurrence of the symbolic value, replace it
    // with a recurrence.
    unsigned FoundIndex = Add->getNumOperands();
    for (unsigned i = 0, e = Add->getNumOperands(); i != e; ++i)
      if (Add->getOperand(i) == SymbolicName)
        if (FoundIndex == e) {
          FoundIndex = i;
          break;
        }

    if (FoundIndex != Add->getNumOperands()) {
      // Create an add with everything but the specified operand.
      SmallVector<const SCEV *, 8> Ops;
      for (unsigned i = 0, e = Add->getNumOperands(); i != e; ++i)
        if (i != FoundIndex)
          Ops.push_back(SCEVBackedgeConditionFolder::rewrite(Add->getOperand(i),
                                                             L, *this));
      const SCEV *Accum = getAddExpr(Ops);

      // This is not a valid addrec if the step amount is varying each
      // loop iteration, but is not itself an addrec in this loop.
      if (isLoopInvariant(Accum, L) ||
          (isa<SCEVAddRecExpr>(Accum) &&
           cast<SCEVAddRecExpr>(Accum)->getLoop() == L)) {
        SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap;

        if (auto BO = MatchBinaryOp(BEValueV, DT)) {
          if (BO->Opcode == Instruction::Add && BO->LHS == PN) {
            if (BO->IsNUW)
              Flags = setFlags(Flags, SCEV::FlagNUW);
            if (BO->IsNSW)
              Flags = setFlags(Flags, SCEV::FlagNSW);
          }
        } else if (GEPOperator *GEP = dyn_cast<GEPOperator>(BEValueV)) {
          // If the increment is an inbounds GEP, then we know the address
          // space cannot be wrapped around. We cannot make any guarantee
          // about signed or unsigned overflow because pointers are
          // unsigned but we may have a negative index from the base
          // pointer. We can guarantee that no unsigned wrap occurs if the
          // indices form a positive value.
          if (GEP->isInBounds() && GEP->getOperand(0) == PN) {
            Flags = setFlags(Flags, SCEV::FlagNW);

            const SCEV *Ptr = getSCEV(GEP->getPointerOperand());
            if (isKnownPositive(getMinusSCEV(getSCEV(GEP), Ptr)))
              Flags = setFlags(Flags, SCEV::FlagNUW);
          }

          // We cannot transfer nuw and nsw flags from subtraction
          // operations -- sub nuw X, Y is not the same as add nuw X, -Y
          // for instance.
        }

        const SCEV *StartVal = getSCEV(StartValueV);
        const SCEV *PHISCEV = getAddRecExpr(StartVal, Accum, L, Flags);

        // Okay, for the entire analysis of this edge we assumed the PHI
        // to be symbolic.  We now need to go back and purge all of the
        // entries for the scalars that use the symbolic expression.
        forgetSymbolicName(PN, SymbolicName);
        ValueExprMap[SCEVCallbackVH(PN, this)] = PHISCEV;

        // We can add Flags to the post-inc expression only if we
        // know that it is *undefined behavior* for BEValueV to
        // overflow.
        if (auto *BEInst = dyn_cast<Instruction>(BEValueV))
          if (isLoopInvariant(Accum, L) && isAddRecNeverPoison(BEInst, L))
            (void)getAddRecExpr(getAddExpr(StartVal, Accum), Accum, L, Flags);

        return PHISCEV;
      }
    }
  } else {
    // Otherwise, this could be a loop like this:
    //     i = 0;  for (j = 1; ..; ++j) { ....  i = j; }
    // In this case, j = {1,+,1}  and BEValue is j.
    // Because the other in-value of i (0) fits the evolution of BEValue
    // i really is an addrec evolution.
    //
    // We can generalize this saying that i is the shifted value of BEValue
    // by one iteration:
    //   PHI(f(0), f({1,+,1})) --> f({0,+,1})
    const SCEV *Shifted = SCEVShiftRewriter::rewrite(BEValue, L, *this);
    const SCEV *Start = SCEVInitRewriter::rewrite(Shifted, L, *this, false);
    if (Shifted != getCouldNotCompute() &&
        Start != getCouldNotCompute()) {
      const SCEV *StartVal = getSCEV(StartValueV);
      if (Start == StartVal) {
        // Okay, for the entire analysis of this edge we assumed the PHI
        // to be symbolic.  We now need to go back and purge all of the
        // entries for the scalars that use the symbolic expression.
        forgetSymbolicName(PN, SymbolicName);
        ValueExprMap[SCEVCallbackVH(PN, this)] = Shifted;
        return Shifted;
      }
    }
  }

  // Remove the temporary PHI node SCEV that has been inserted while intending
  // to create an AddRecExpr for this PHI node. We can not keep this temporary
  // as it will prevent later (possibly simpler) SCEV expressions to be added
  // to the ValueExprMap.
  eraseValueFromMap(PN);

  return nullptr;
}

// Checks if the SCEV S is available at BB.  S is considered available at BB
// if S can be materialized at BB without introducing a fault.
static bool IsAvailableOnEntry(const Loop *L, DominatorTree &DT, const SCEV *S,
                               BasicBlock *BB) {
  struct CheckAvailable {
    bool TraversalDone = false;
    bool Available = true;

    const Loop *L = nullptr;  // The loop BB is in (can be nullptr)
    BasicBlock *BB = nullptr;
    DominatorTree &DT;

    CheckAvailable(const Loop *L, BasicBlock *BB, DominatorTree &DT)
      : L(L), BB(BB), DT(DT) {}

    bool setUnavailable() {
      TraversalDone = true;
      Available = false;
      return false;
    }

    bool follow(const SCEV *S) {
      switch (S->getSCEVType()) {
      case scConstant: case scTruncate: case scZeroExtend: case scSignExtend:
      case scAddExpr: case scMulExpr: case scUMaxExpr: case scSMaxExpr:
        // These expressions are available if their operand(s) is/are.
        return true;

      case scAddRecExpr: {
        // We allow add recurrences that are on the loop BB is in, or some
        // outer loop.  This guarantees availability because the value of the
        // add recurrence at BB is simply the "current" value of the induction
        // variable.  We can relax this in the future; for instance an add
        // recurrence on a sibling dominating loop is also available at BB.
        const auto *ARLoop = cast<SCEVAddRecExpr>(S)->getLoop();
        if (L && (ARLoop == L || ARLoop->contains(L)))
          return true;

        return setUnavailable();
      }

      case scUnknown: {
        // For SCEVUnknown, we check for simple dominance.
        const auto *SU = cast<SCEVUnknown>(S);
        Value *V = SU->getValue();

        if (isa<Argument>(V))
          return false;

        if (isa<Instruction>(V) && DT.dominates(cast<Instruction>(V), BB))
          return false;

        return setUnavailable();
      }

      case scUDivExpr:
      case scCouldNotCompute:
        // We do not try to smart about these at all.
        return setUnavailable();
      }
      llvm_unreachable("switch should be fully covered!");
    }

    bool isDone() { return TraversalDone; }
  };

  CheckAvailable CA(L, BB, DT);
  SCEVTraversal<CheckAvailable> ST(CA);

  ST.visitAll(S);
  return CA.Available;
}

// Try to match a control flow sequence that branches out at BI and merges back
// at Merge into a "C ? LHS : RHS" select pattern.  Return true on a successful
// match.
static bool BrPHIToSelect(DominatorTree &DT, BranchInst *BI, PHINode *Merge,
                          Value *&C, Value *&LHS, Value *&RHS) {
  C = BI->getCondition();

  BasicBlockEdge LeftEdge(BI->getParent(), BI->getSuccessor(0));
  BasicBlockEdge RightEdge(BI->getParent(), BI->getSuccessor(1));

  if (!LeftEdge.isSingleEdge())
    return false;

  assert(RightEdge.isSingleEdge() && "Follows from LeftEdge.isSingleEdge()");

  Use &LeftUse = Merge->getOperandUse(0);
  Use &RightUse = Merge->getOperandUse(1);

  if (DT.dominates(LeftEdge, LeftUse) && DT.dominates(RightEdge, RightUse)) {
    LHS = LeftUse;
    RHS = RightUse;
    return true;
  }

  if (DT.dominates(LeftEdge, RightUse) && DT.dominates(RightEdge, LeftUse)) {
    LHS = RightUse;
    RHS = LeftUse;
    return true;
  }

  return false;
}

const SCEV *ScalarEvolution::createNodeFromSelectLikePHI(PHINode *PN) {
  auto IsReachable =
      [&](BasicBlock *BB) { return DT.isReachableFromEntry(BB); };
  if (PN->getNumIncomingValues() == 2 && all_of(PN->blocks(), IsReachable)) {
    const Loop *L = LI.getLoopFor(PN->getParent());

    // We don't want to break LCSSA, even in a SCEV expression tree.
    for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i)
      if (LI.getLoopFor(PN->getIncomingBlock(i)) != L)
        return nullptr;

    // Try to match
    //
    //  br %cond, label %left, label %right
    // left:
    //  br label %merge
    // right:
    //  br label %merge
    // merge:
    //  V = phi [ %x, %left ], [ %y, %right ]
    //
    // as "select %cond, %x, %y"

    BasicBlock *IDom = DT[PN->getParent()]->getIDom()->getBlock();
    assert(IDom && "At least the entry block should dominate PN");

    auto *BI = dyn_cast<BranchInst>(IDom->getTerminator());
    Value *Cond = nullptr, *LHS = nullptr, *RHS = nullptr;

    if (BI && BI->isConditional() &&
        BrPHIToSelect(DT, BI, PN, Cond, LHS, RHS) &&
        IsAvailableOnEntry(L, DT, getSCEV(LHS), PN->getParent()) &&
        IsAvailableOnEntry(L, DT, getSCEV(RHS), PN->getParent()))
      return createNodeForSelectOrPHI(PN, Cond, LHS, RHS);
  }

  return nullptr;
}

const SCEV *ScalarEvolution::createNodeForPHI(PHINode *PN) {
  if (const SCEV *S = createAddRecFromPHI(PN))
    return S;

  if (const SCEV *S = createNodeFromSelectLikePHI(PN))
    return S;

  // If the PHI has a single incoming value, follow that value, unless the
  // PHI's incoming blocks are in a different loop, in which case doing so
  // risks breaking LCSSA form. Instcombine would normally zap these, but
  // it doesn't have DominatorTree information, so it may miss cases.
  if (Value *V = SimplifyInstruction(PN, {getDataLayout(), &TLI, &DT, &AC}))
    if (LI.replacementPreservesLCSSAForm(PN, V))
      return getSCEV(V);

  // If it's not a loop phi, we can't handle it yet.
  return getUnknown(PN);
}

const SCEV *ScalarEvolution::createNodeForSelectOrPHI(Instruction *I,
                                                      Value *Cond,
                                                      Value *TrueVal,
                                                      Value *FalseVal) {
  // Handle "constant" branch or select. This can occur for instance when a
  // loop pass transforms an inner loop and moves on to process the outer loop.
  if (auto *CI = dyn_cast<ConstantInt>(Cond))
    return getSCEV(CI->isOne() ? TrueVal : FalseVal);

  // Try to match some simple smax or umax patterns.
  auto *ICI = dyn_cast<ICmpInst>(Cond);
  if (!ICI)
    return getUnknown(I);

  Value *LHS = ICI->getOperand(0);
  Value *RHS = ICI->getOperand(1);

  switch (ICI->getPredicate()) {
  case ICmpInst::ICMP_SLT:
  case ICmpInst::ICMP_SLE:
    std::swap(LHS, RHS);
    LLVM_FALLTHROUGH;
  case ICmpInst::ICMP_SGT:
  case ICmpInst::ICMP_SGE:
    // a >s b ? a+x : b+x  ->  smax(a, b)+x
    // a >s b ? b+x : a+x  ->  smin(a, b)+x
    if (getTypeSizeInBits(LHS->getType()) <= getTypeSizeInBits(I->getType())) {
      const SCEV *LS = getNoopOrSignExtend(getSCEV(LHS), I->getType());
      const SCEV *RS = getNoopOrSignExtend(getSCEV(RHS), I->getType());
      const SCEV *LA = getSCEV(TrueVal);
      const SCEV *RA = getSCEV(FalseVal);
      const SCEV *LDiff = getMinusSCEV(LA, LS);
      const SCEV *RDiff = getMinusSCEV(RA, RS);
      if (LDiff == RDiff)
        return getAddExpr(getSMaxExpr(LS, RS), LDiff);
      LDiff = getMinusSCEV(LA, RS);
      RDiff = getMinusSCEV(RA, LS);
      if (LDiff == RDiff)
        return getAddExpr(getSMinExpr(LS, RS), LDiff);
    }
    break;
  case ICmpInst::ICMP_ULT:
  case ICmpInst::ICMP_ULE:
    std::swap(LHS, RHS);
    LLVM_FALLTHROUGH;
  case ICmpInst::ICMP_UGT:
  case ICmpInst::ICMP_UGE:
    // a >u b ? a+x : b+x  ->  umax(a, b)+x
    // a >u b ? b+x : a+x  ->  umin(a, b)+x
    if (getTypeSizeInBits(LHS->getType()) <= getTypeSizeInBits(I->getType())) {
      const SCEV *LS = getNoopOrZeroExtend(getSCEV(LHS), I->getType());
      const SCEV *RS = getNoopOrZeroExtend(getSCEV(RHS), I->getType());
      const SCEV *LA = getSCEV(TrueVal);
      const SCEV *RA = getSCEV(FalseVal);
      const SCEV *LDiff = getMinusSCEV(LA, LS);
      const SCEV *RDiff = getMinusSCEV(RA, RS);
      if (LDiff == RDiff)
        return getAddExpr(getUMaxExpr(LS, RS), LDiff);
      LDiff = getMinusSCEV(LA, RS);
      RDiff = getMinusSCEV(RA, LS);
      if (LDiff == RDiff)
        return getAddExpr(getUMinExpr(LS, RS), LDiff);
    }
    break;
  case ICmpInst::ICMP_NE:
    // n != 0 ? n+x : 1+x  ->  umax(n, 1)+x
    if (getTypeSizeInBits(LHS->getType()) <= getTypeSizeInBits(I->getType()) &&
        isa<ConstantInt>(RHS) && cast<ConstantInt>(RHS)->isZero()) {
      const SCEV *One = getOne(I->getType());
      const SCEV *LS = getNoopOrZeroExtend(getSCEV(LHS), I->getType());
      const SCEV *LA = getSCEV(TrueVal);
      const SCEV *RA = getSCEV(FalseVal);
      const SCEV *LDiff = getMinusSCEV(LA, LS);
      const SCEV *RDiff = getMinusSCEV(RA, One);
      if (LDiff == RDiff)
        return getAddExpr(getUMaxExpr(One, LS), LDiff);
    }
    break;
  case ICmpInst::ICMP_EQ:
    // n == 0 ? 1+x : n+x  ->  umax(n, 1)+x
    if (getTypeSizeInBits(LHS->getType()) <= getTypeSizeInBits(I->getType()) &&
        isa<ConstantInt>(RHS) && cast<ConstantInt>(RHS)->isZero()) {
      const SCEV *One = getOne(I->getType());
      const SCEV *LS = getNoopOrZeroExtend(getSCEV(LHS), I->getType());
      const SCEV *LA = getSCEV(TrueVal);
      const SCEV *RA = getSCEV(FalseVal);
      const SCEV *LDiff = getMinusSCEV(LA, One);
      const SCEV *RDiff = getMinusSCEV(RA, LS);
      if (LDiff == RDiff)
        return getAddExpr(getUMaxExpr(One, LS), LDiff);
    }
    break;
  default:
    break;
  }

  return getUnknown(I);
}

/// Expand GEP instructions into add and multiply operations. This allows them
/// to be analyzed by regular SCEV code.
const SCEV *ScalarEvolution::createNodeForGEP(GEPOperator *GEP) {
  // Don't attempt to analyze GEPs over unsized objects.
  if (!GEP->getSourceElementType()->isSized())
    return getUnknown(GEP);

  SmallVector<const SCEV *, 4> IndexExprs;
  for (auto Index = GEP->idx_begin(); Index != GEP->idx_end(); ++Index)
    IndexExprs.push_back(getSCEV(*Index));
  return getGEPExpr(GEP, IndexExprs);
}

uint32_t ScalarEvolution::GetMinTrailingZerosImpl(const SCEV *S) {
  if (const SCEVConstant *C = dyn_cast<SCEVConstant>(S))
    return C->getAPInt().countTrailingZeros();

  if (const SCEVTruncateExpr *T = dyn_cast<SCEVTruncateExpr>(S))
    return std::min(GetMinTrailingZeros(T->getOperand()),
                    (uint32_t)getTypeSizeInBits(T->getType()));

  if (const SCEVZeroExtendExpr *E = dyn_cast<SCEVZeroExtendExpr>(S)) {
    uint32_t OpRes = GetMinTrailingZeros(E->getOperand());
    return OpRes == getTypeSizeInBits(E->getOperand()->getType())
               ? getTypeSizeInBits(E->getType())
               : OpRes;
  }

  if (const SCEVSignExtendExpr *E = dyn_cast<SCEVSignExtendExpr>(S)) {
    uint32_t OpRes = GetMinTrailingZeros(E->getOperand());
    return OpRes == getTypeSizeInBits(E->getOperand()->getType())
               ? getTypeSizeInBits(E->getType())
               : OpRes;
  }

  if (const SCEVAddExpr *A = dyn_cast<SCEVAddExpr>(S)) {
    // The result is the min of all operands results.
    uint32_t MinOpRes = GetMinTrailingZeros(A->getOperand(0));
    for (unsigned i = 1, e = A->getNumOperands(); MinOpRes && i != e; ++i)
      MinOpRes = std::min(MinOpRes, GetMinTrailingZeros(A->getOperand(i)));
    return MinOpRes;
  }

  if (const SCEVMulExpr *M = dyn_cast<SCEVMulExpr>(S)) {
    // The result is the sum of all operands results.
    uint32_t SumOpRes = GetMinTrailingZeros(M->getOperand(0));
    uint32_t BitWidth = getTypeSizeInBits(M->getType());
    for (unsigned i = 1, e = M->getNumOperands();
         SumOpRes != BitWidth && i != e; ++i)
      SumOpRes =
          std::min(SumOpRes + GetMinTrailingZeros(M->getOperand(i)), BitWidth);
    return SumOpRes;
  }

  if (const SCEVAddRecExpr *A = dyn_cast<SCEVAddRecExpr>(S)) {
    // The result is the min of all operands results.
    uint32_t MinOpRes = GetMinTrailingZeros(A->getOperand(0));
    for (unsigned i = 1, e = A->getNumOperands(); MinOpRes && i != e; ++i)
      MinOpRes = std::min(MinOpRes, GetMinTrailingZeros(A->getOperand(i)));
    return MinOpRes;
  }

  if (const SCEVSMaxExpr *M = dyn_cast<SCEVSMaxExpr>(S)) {
    // The result is the min of all operands results.
    uint32_t MinOpRes = GetMinTrailingZeros(M->getOperand(0));
    for (unsigned i = 1, e = M->getNumOperands(); MinOpRes && i != e; ++i)
      MinOpRes = std::min(MinOpRes, GetMinTrailingZeros(M->getOperand(i)));
    return MinOpRes;
  }

  if (const SCEVUMaxExpr *M = dyn_cast<SCEVUMaxExpr>(S)) {
    // The result is the min of all operands results.
    uint32_t MinOpRes = GetMinTrailingZeros(M->getOperand(0));
    for (unsigned i = 1, e = M->getNumOperands(); MinOpRes && i != e; ++i)
      MinOpRes = std::min(MinOpRes, GetMinTrailingZeros(M->getOperand(i)));
    return MinOpRes;
  }

  if (const SCEVUnknown *U = dyn_cast<SCEVUnknown>(S)) {
    // For a SCEVUnknown, ask ValueTracking.
    KnownBits Known = computeKnownBits(U->getValue(), getDataLayout(), 0, &AC, nullptr, &DT);
    return Known.countMinTrailingZeros();
  }

  // SCEVUDivExpr
  return 0;
}

uint32_t ScalarEvolution::GetMinTrailingZeros(const SCEV *S) {
  auto I = MinTrailingZerosCache.find(S);
  if (I != MinTrailingZerosCache.end())
    return I->second;

  uint32_t Result = GetMinTrailingZerosImpl(S);
  auto InsertPair = MinTrailingZerosCache.insert({S, Result});
  assert(InsertPair.second && "Should insert a new key");
  return InsertPair.first->second;
}

/// Helper method to assign a range to V from metadata present in the IR.
static Optional<ConstantRange> GetRangeFromMetadata(Value *V) {
  if (Instruction *I = dyn_cast<Instruction>(V))
    if (MDNode *MD = I->getMetadata(LLVMContext::MD_range))
      return getConstantRangeFromMetadata(*MD);

  return None;
}

/// Determine the range for a particular SCEV.  If SignHint is
/// HINT_RANGE_UNSIGNED (resp. HINT_RANGE_SIGNED) then getRange prefers ranges
/// with a "cleaner" unsigned (resp. signed) representation.
const ConstantRange &
ScalarEvolution::getRangeRef(const SCEV *S,
                             ScalarEvolution::RangeSignHint SignHint) {
  DenseMap<const SCEV *, ConstantRange> &Cache =
      SignHint == ScalarEvolution::HINT_RANGE_UNSIGNED ? UnsignedRanges
                                                       : SignedRanges;

  // See if we've computed this range already.
  DenseMap<const SCEV *, ConstantRange>::iterator I = Cache.find(S);
  if (I != Cache.end())
    return I->second;

  if (const SCEVConstant *C = dyn_cast<SCEVConstant>(S))
    return setRange(C, SignHint, ConstantRange(C->getAPInt()));

  unsigned BitWidth = getTypeSizeInBits(S->getType());
  ConstantRange ConservativeResult(BitWidth, /*isFullSet=*/true);

  // If the value has known zeros, the maximum value will have those known zeros
  // as well.
  uint32_t TZ = GetMinTrailingZeros(S);
  if (TZ != 0) {
    if (SignHint == ScalarEvolution::HINT_RANGE_UNSIGNED)
      ConservativeResult =
          ConstantRange(APInt::getMinValue(BitWidth),
                        APInt::getMaxValue(BitWidth).lshr(TZ).shl(TZ) + 1);
    else
      ConservativeResult = ConstantRange(
          APInt::getSignedMinValue(BitWidth),
          APInt::getSignedMaxValue(BitWidth).ashr(TZ).shl(TZ) + 1);
  }

  if (const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(S)) {
    ConstantRange X = getRangeRef(Add->getOperand(0), SignHint);
    for (unsigned i = 1, e = Add->getNumOperands(); i != e; ++i)
      X = X.add(getRangeRef(Add->getOperand(i), SignHint));
    return setRange(Add, SignHint, ConservativeResult.intersectWith(X));
  }

  if (const SCEVMulExpr *Mul = dyn_cast<SCEVMulExpr>(S)) {
    ConstantRange X = getRangeRef(Mul->getOperand(0), SignHint);
    for (unsigned i = 1, e = Mul->getNumOperands(); i != e; ++i)
      X = X.multiply(getRangeRef(Mul->getOperand(i), SignHint));
    return setRange(Mul, SignHint, ConservativeResult.intersectWith(X));
  }

  if (const SCEVSMaxExpr *SMax = dyn_cast<SCEVSMaxExpr>(S)) {
    ConstantRange X = getRangeRef(SMax->getOperand(0), SignHint);
    for (unsigned i = 1, e = SMax->getNumOperands(); i != e; ++i)
      X = X.smax(getRangeRef(SMax->getOperand(i), SignHint));
    return setRange(SMax, SignHint, ConservativeResult.intersectWith(X));
  }

  if (const SCEVUMaxExpr *UMax = dyn_cast<SCEVUMaxExpr>(S)) {
    ConstantRange X = getRangeRef(UMax->getOperand(0), SignHint);
    for (unsigned i = 1, e = UMax->getNumOperands(); i != e; ++i)
      X = X.umax(getRangeRef(UMax->getOperand(i), SignHint));
    return setRange(UMax, SignHint, ConservativeResult.intersectWith(X));
  }

  if (const SCEVUDivExpr *UDiv = dyn_cast<SCEVUDivExpr>(S)) {
    ConstantRange X = getRangeRef(UDiv->getLHS(), SignHint);
    ConstantRange Y = getRangeRef(UDiv->getRHS(), SignHint);
    return setRange(UDiv, SignHint,
                    ConservativeResult.intersectWith(X.udiv(Y)));
  }

  if (const SCEVZeroExtendExpr *ZExt = dyn_cast<SCEVZeroExtendExpr>(S)) {
    ConstantRange X = getRangeRef(ZExt->getOperand(), SignHint);
    return setRange(ZExt, SignHint,
                    ConservativeResult.intersectWith(X.zeroExtend(BitWidth)));
  }

  if (const SCEVSignExtendExpr *SExt = dyn_cast<SCEVSignExtendExpr>(S)) {
    ConstantRange X = getRangeRef(SExt->getOperand(), SignHint);
    return setRange(SExt, SignHint,
                    ConservativeResult.intersectWith(X.signExtend(BitWidth)));
  }

  if (const SCEVTruncateExpr *Trunc = dyn_cast<SCEVTruncateExpr>(S)) {
    ConstantRange X = getRangeRef(Trunc->getOperand(), SignHint);
    return setRange(Trunc, SignHint,
                    ConservativeResult.intersectWith(X.truncate(BitWidth)));
  }

  if (const SCEVAddRecExpr *AddRec = dyn_cast<SCEVAddRecExpr>(S)) {
    // If there's no unsigned wrap, the value will never be less than its
    // initial value.
    if (AddRec->hasNoUnsignedWrap())
      if (const SCEVConstant *C = dyn_cast<SCEVConstant>(AddRec->getStart()))
        if (!C->getValue()->isZero())
          ConservativeResult = ConservativeResult.intersectWith(
              ConstantRange(C->getAPInt(), APInt(BitWidth, 0)));

    // If there's no signed wrap, and all the operands have the same sign or
    // zero, the value won't ever change sign.
    if (AddRec->hasNoSignedWrap()) {
      bool AllNonNeg = true;
      bool AllNonPos = true;
      for (unsigned i = 0, e = AddRec->getNumOperands(); i != e; ++i) {
        if (!isKnownNonNegative(AddRec->getOperand(i))) AllNonNeg = false;
        if (!isKnownNonPositive(AddRec->getOperand(i))) AllNonPos = false;
      }
      if (AllNonNeg)
        ConservativeResult = ConservativeResult.intersectWith(
          ConstantRange(APInt(BitWidth, 0),
                        APInt::getSignedMinValue(BitWidth)));
      else if (AllNonPos)
        ConservativeResult = ConservativeResult.intersectWith(
          ConstantRange(APInt::getSignedMinValue(BitWidth),
                        APInt(BitWidth, 1)));
    }

    // TODO: non-affine addrec
    if (AddRec->isAffine()) {
      const SCEV *MaxBECount = getMaxBackedgeTakenCount(AddRec->getLoop());
      if (!isa<SCEVCouldNotCompute>(MaxBECount) &&
          getTypeSizeInBits(MaxBECount->getType()) <= BitWidth) {
        auto RangeFromAffine = getRangeForAffineAR(
            AddRec->getStart(), AddRec->getStepRecurrence(*this), MaxBECount,
            BitWidth);
        if (!RangeFromAffine.isFullSet())
          ConservativeResult =
              ConservativeResult.intersectWith(RangeFromAffine);

        auto RangeFromFactoring = getRangeViaFactoring(
            AddRec->getStart(), AddRec->getStepRecurrence(*this), MaxBECount,
            BitWidth);
        if (!RangeFromFactoring.isFullSet())
          ConservativeResult =
              ConservativeResult.intersectWith(RangeFromFactoring);
      }
    }

    return setRange(AddRec, SignHint, std::move(ConservativeResult));
  }

  if (const SCEVUnknown *U = dyn_cast<SCEVUnknown>(S)) {
    // Check if the IR explicitly contains !range metadata.
    Optional<ConstantRange> MDRange = GetRangeFromMetadata(U->getValue());
    if (MDRange.hasValue())
      ConservativeResult = ConservativeResult.intersectWith(MDRange.getValue());

    // Split here to avoid paying the compile-time cost of calling both
    // computeKnownBits and ComputeNumSignBits.  This restriction can be lifted
    // if needed.
    const DataLayout &DL = getDataLayout();
    if (SignHint == ScalarEvolution::HINT_RANGE_UNSIGNED) {
      // For a SCEVUnknown, ask ValueTracking.
      KnownBits Known = computeKnownBits(U->getValue(), DL, 0, &AC, nullptr, &DT);
      if (Known.One != ~Known.Zero + 1)
        ConservativeResult =
            ConservativeResult.intersectWith(ConstantRange(Known.One,
                                                           ~Known.Zero + 1));
    } else {
      assert(SignHint == ScalarEvolution::HINT_RANGE_SIGNED &&
             "generalize as needed!");
      unsigned NS = ComputeNumSignBits(U->getValue(), DL, 0, &AC, nullptr, &DT);
      if (NS > 1)
        ConservativeResult = ConservativeResult.intersectWith(
            ConstantRange(APInt::getSignedMinValue(BitWidth).ashr(NS - 1),
                          APInt::getSignedMaxValue(BitWidth).ashr(NS - 1) + 1));
    }

    // A range of Phi is a subset of union of all ranges of its input.
    if (const PHINode *Phi = dyn_cast<PHINode>(U->getValue())) {
      // Make sure that we do not run over cycled Phis.
      if (PendingPhiRanges.insert(Phi).second) {
        ConstantRange RangeFromOps(BitWidth, /*isFullSet=*/false);
        for (auto &Op : Phi->operands()) {
          auto OpRange = getRangeRef(getSCEV(Op), SignHint);
          RangeFromOps = RangeFromOps.unionWith(OpRange);
          // No point to continue if we already have a full set.
          if (RangeFromOps.isFullSet())
            break;
        }
        ConservativeResult = ConservativeResult.intersectWith(RangeFromOps);
        bool Erased = PendingPhiRanges.erase(Phi);
        assert(Erased && "Failed to erase Phi properly?");
        (void) Erased;
      }
    }

    return setRange(U, SignHint, std::move(ConservativeResult));
  }

  return setRange(S, SignHint, std::move(ConservativeResult));
}

// Given a StartRange, Step and MaxBECount for an expression compute a range of
// values that the expression can take. Initially, the expression has a value
// from StartRange and then is changed by Step up to MaxBECount times. Signed
// argument defines if we treat Step as signed or unsigned.
static ConstantRange getRangeForAffineARHelper(APInt Step,
                                               const ConstantRange &StartRange,
                                               const APInt &MaxBECount,
                                               unsigned BitWidth, bool Signed) {
  // If either Step or MaxBECount is 0, then the expression won't change, and we
  // just need to return the initial range.
  if (Step == 0 || MaxBECount == 0)
    return StartRange;

  // If we don't know anything about the initial value (i.e. StartRange is
  // FullRange), then we don't know anything about the final range either.
  // Return FullRange.
  if (StartRange.isFullSet())
    return ConstantRange(BitWidth, /* isFullSet = */ true);

  // If Step is signed and negative, then we use its absolute value, but we also
  // note that we're moving in the opposite direction.
  bool Descending = Signed && Step.isNegative();

  if (Signed)
    // This is correct even for INT_SMIN. Let's look at i8 to illustrate this:
    // abs(INT_SMIN) = abs(-128) = abs(0x80) = -0x80 = 0x80 = 128.
    // This equations hold true due to the well-defined wrap-around behavior of
    // APInt.
    Step = Step.abs();

  // Check if Offset is more than full span of BitWidth. If it is, the
  // expression is guaranteed to overflow.
  if (APInt::getMaxValue(StartRange.getBitWidth()).udiv(Step).ult(MaxBECount))
    return ConstantRange(BitWidth, /* isFullSet = */ true);

  // Offset is by how much the expression can change. Checks above guarantee no
  // overflow here.
  APInt Offset = Step * MaxBECount;

  // Minimum value of the final range will match the minimal value of StartRange
  // if the expression is increasing and will be decreased by Offset otherwise.
  // Maximum value of the final range will match the maximal value of StartRange
  // if the expression is decreasing and will be increased by Offset otherwise.
  APInt StartLower = StartRange.getLower();
  APInt StartUpper = StartRange.getUpper() - 1;
  APInt MovedBoundary = Descending ? (StartLower - std::move(Offset))
                                   : (StartUpper + std::move(Offset));

  // It's possible that the new minimum/maximum value will fall into the initial
  // range (due to wrap around). This means that the expression can take any
  // value in this bitwidth, and we have to return full range.
  if (StartRange.contains(MovedBoundary))
    return ConstantRange(BitWidth, /* isFullSet = */ true);

  APInt NewLower =
      Descending ? std::move(MovedBoundary) : std::move(StartLower);
  APInt NewUpper =
      Descending ? std::move(StartUpper) : std::move(MovedBoundary);
  NewUpper += 1;

  // If we end up with full range, return a proper full range.
  if (NewLower == NewUpper)
    return ConstantRange(BitWidth, /* isFullSet = */ true);

  // No overflow detected, return [StartLower, StartUpper + Offset + 1) range.
  return ConstantRange(std::move(NewLower), std::move(NewUpper));
}

ConstantRange ScalarEvolution::getRangeForAffineAR(const SCEV *Start,
                                                   const SCEV *Step,
                                                   const SCEV *MaxBECount,
                                                   unsigned BitWidth) {
  assert(!isa<SCEVCouldNotCompute>(MaxBECount) &&
         getTypeSizeInBits(MaxBECount->getType()) <= BitWidth &&
         "Precondition!");

  MaxBECount = getNoopOrZeroExtend(MaxBECount, Start->getType());
  APInt MaxBECountValue = getUnsignedRangeMax(MaxBECount);

  // First, consider step signed.
  ConstantRange StartSRange = getSignedRange(Start);
  ConstantRange StepSRange = getSignedRange(Step);

  // If Step can be both positive and negative, we need to find ranges for the
  // maximum absolute step values in both directions and union them.
  ConstantRange SR =
      getRangeForAffineARHelper(StepSRange.getSignedMin(), StartSRange,
                                MaxBECountValue, BitWidth, /* Signed = */ true);
  SR = SR.unionWith(getRangeForAffineARHelper(StepSRange.getSignedMax(),
                                              StartSRange, MaxBECountValue,
                                              BitWidth, /* Signed = */ true));

  // Next, consider step unsigned.
  ConstantRange UR = getRangeForAffineARHelper(
      getUnsignedRangeMax(Step), getUnsignedRange(Start),
      MaxBECountValue, BitWidth, /* Signed = */ false);

  // Finally, intersect signed and unsigned ranges.
  return SR.intersectWith(UR);
}

ConstantRange ScalarEvolution::getRangeViaFactoring(const SCEV *Start,
                                                    const SCEV *Step,
                                                    const SCEV *MaxBECount,
                                                    unsigned BitWidth) {
  //    RangeOf({C?A:B,+,C?P:Q}) == RangeOf(C?{A,+,P}:{B,+,Q})
  // == RangeOf({A,+,P}) union RangeOf({B,+,Q})

  struct SelectPattern {
    Value *Condition = nullptr;
    APInt TrueValue;
    APInt FalseValue;

    explicit SelectPattern(ScalarEvolution &SE, unsigned BitWidth,
                           const SCEV *S) {
      Optional<unsigned> CastOp;
      APInt Offset(BitWidth, 0);

      assert(SE.getTypeSizeInBits(S->getType()) == BitWidth &&
             "Should be!");

      // Peel off a constant offset:
      if (auto *SA = dyn_cast<SCEVAddExpr>(S)) {
        // In the future we could consider being smarter here and handle
        // {Start+Step,+,Step} too.
        if (SA->getNumOperands() != 2 || !isa<SCEVConstant>(SA->getOperand(0)))
          return;

        Offset = cast<SCEVConstant>(SA->getOperand(0))->getAPInt();
        S = SA->getOperand(1);
      }

      // Peel off a cast operation
      if (auto *SCast = dyn_cast<SCEVCastExpr>(S)) {
        CastOp = SCast->getSCEVType();
        S = SCast->getOperand();
      }

      using namespace llvm::PatternMatch;

      auto *SU = dyn_cast<SCEVUnknown>(S);
      const APInt *TrueVal, *FalseVal;
      if (!SU ||
          !match(SU->getValue(), m_Select(m_Value(Condition), m_APInt(TrueVal),
                                          m_APInt(FalseVal)))) {
        Condition = nullptr;
        return;
      }

      TrueValue = *TrueVal;
      FalseValue = *FalseVal;

      // Re-apply the cast we peeled off earlier
      if (CastOp.hasValue())
        switch (*CastOp) {
        default:
          llvm_unreachable("Unknown SCEV cast type!");

        case scTruncate:
          TrueValue = TrueValue.trunc(BitWidth);
          FalseValue = FalseValue.trunc(BitWidth);
          break;
        case scZeroExtend:
          TrueValue = TrueValue.zext(BitWidth);
          FalseValue = FalseValue.zext(BitWidth);
          break;
        case scSignExtend:
          TrueValue = TrueValue.sext(BitWidth);
          FalseValue = FalseValue.sext(BitWidth);
          break;
        }

      // Re-apply the constant offset we peeled off earlier
      TrueValue += Offset;
      FalseValue += Offset;
    }

    bool isRecognized() { return Condition != nullptr; }
  };

  SelectPattern StartPattern(*this, BitWidth, Start);
  if (!StartPattern.isRecognized())
    return ConstantRange(BitWidth, /* isFullSet = */ true);

  SelectPattern StepPattern(*this, BitWidth, Step);
  if (!StepPattern.isRecognized())
    return ConstantRange(BitWidth, /* isFullSet = */ true);

  if (StartPattern.Condition != StepPattern.Condition) {
    // We don't handle this case today; but we could, by considering four
    // possibilities below instead of two. I'm not sure if there are cases where
    // that will help over what getRange already does, though.
    return ConstantRange(BitWidth, /* isFullSet = */ true);
  }

  // NB! Calling ScalarEvolution::getConstant is fine, but we should not try to
  // construct arbitrary general SCEV expressions here.  This function is called
  // from deep in the call stack, and calling getSCEV (on a sext instruction,
  // say) can end up caching a suboptimal value.

  // FIXME: without the explicit `this` receiver below, MSVC errors out with
  // C2352 and C2512 (otherwise it isn't needed).

  const SCEV *TrueStart = this->getConstant(StartPattern.TrueValue);
  const SCEV *TrueStep = this->getConstant(StepPattern.TrueValue);
  const SCEV *FalseStart = this->getConstant(StartPattern.FalseValue);
  const SCEV *FalseStep = this->getConstant(StepPattern.FalseValue);

  ConstantRange TrueRange =
      this->getRangeForAffineAR(TrueStart, TrueStep, MaxBECount, BitWidth);
  ConstantRange FalseRange =
      this->getRangeForAffineAR(FalseStart, FalseStep, MaxBECount, BitWidth);

  return TrueRange.unionWith(FalseRange);
}

SCEV::NoWrapFlags ScalarEvolution::getNoWrapFlagsFromUB(const Value *V) {
  if (isa<ConstantExpr>(V)) return SCEV::FlagAnyWrap;
  const BinaryOperator *BinOp = cast<BinaryOperator>(V);

  // Return early if there are no flags to propagate to the SCEV.
  SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap;
  if (BinOp->hasNoUnsignedWrap())
    Flags = ScalarEvolution::setFlags(Flags, SCEV::FlagNUW);
  if (BinOp->hasNoSignedWrap())
    Flags = ScalarEvolution::setFlags(Flags, SCEV::FlagNSW);
  if (Flags == SCEV::FlagAnyWrap)
    return SCEV::FlagAnyWrap;

  return isSCEVExprNeverPoison(BinOp) ? Flags : SCEV::FlagAnyWrap;
}

bool ScalarEvolution::isSCEVExprNeverPoison(const Instruction *I) {
  // Here we check that I is in the header of the innermost loop containing I,
  // since we only deal with instructions in the loop header. The actual loop we
  // need to check later will come from an add recurrence, but getting that
  // requires computing the SCEV of the operands, which can be expensive. This
  // check we can do cheaply to rule out some cases early.
  Loop *InnermostContainingLoop = LI.getLoopFor(I->getParent());
  if (InnermostContainingLoop == nullptr ||
      InnermostContainingLoop->getHeader() != I->getParent())
    return false;

  // Only proceed if we can prove that I does not yield poison.
  if (!programUndefinedIfFullPoison(I))
    return false;

  // At this point we know that if I is executed, then it does not wrap
  // according to at least one of NSW or NUW. If I is not executed, then we do
  // not know if the calculation that I represents would wrap. Multiple
  // instructions can map to the same SCEV. If we apply NSW or NUW from I to
  // the SCEV, we must guarantee no wrapping for that SCEV also when it is
  // derived from other instructions that map to the same SCEV. We cannot make
  // that guarantee for cases where I is not executed. So we need to find the
  // loop that I is considered in relation to and prove that I is executed for
  // every iteration of that loop. That implies that the value that I
  // calculates does not wrap anywhere in the loop, so then we can apply the
  // flags to the SCEV.
  //
  // We check isLoopInvariant to disambiguate in case we are adding recurrences
  // from different loops, so that we know which loop to prove that I is
  // executed in.
  for (unsigned OpIndex = 0; OpIndex < I->getNumOperands(); ++OpIndex) {
    // I could be an extractvalue from a call to an overflow intrinsic.
    // TODO: We can do better here in some cases.
    if (!isSCEVable(I->getOperand(OpIndex)->getType()))
      return false;
    const SCEV *Op = getSCEV(I->getOperand(OpIndex));
    if (auto *AddRec = dyn_cast<SCEVAddRecExpr>(Op)) {
      bool AllOtherOpsLoopInvariant = true;
      for (unsigned OtherOpIndex = 0; OtherOpIndex < I->getNumOperands();
           ++OtherOpIndex) {
        if (OtherOpIndex != OpIndex) {
          const SCEV *OtherOp = getSCEV(I->getOperand(OtherOpIndex));
          if (!isLoopInvariant(OtherOp, AddRec->getLoop())) {
            AllOtherOpsLoopInvariant = false;
            break;
          }
        }
      }
      if (AllOtherOpsLoopInvariant &&
          isGuaranteedToExecuteForEveryIteration(I, AddRec->getLoop()))
        return true;
    }
  }
  return false;
}

bool ScalarEvolution::isAddRecNeverPoison(const Instruction *I, const Loop *L) {
  // If we know that \c I can never be poison period, then that's enough.
  if (isSCEVExprNeverPoison(I))
    return true;

  // For an add recurrence specifically, we assume that infinite loops without
  // side effects are undefined behavior, and then reason as follows:
  //
  // If the add recurrence is poison in any iteration, it is poison on all
  // future iterations (since incrementing poison yields poison). If the result
  // of the add recurrence is fed into the loop latch condition and the loop
  // does not contain any throws or exiting blocks other than the latch, we now
  // have the ability to "choose" whether the backedge is taken or not (by
  // choosing a sufficiently evil value for the poison feeding into the branch)
  // for every iteration including and after the one in which \p I first became
  // poison.  There are two possibilities (let's call the iteration in which \p
  // I first became poison as K):
  //
  //  1. In the set of iterations including and after K, the loop body executes
  //     no side effects.  In this case executing the backege an infinte number
  //     of times will yield undefined behavior.
  //
  //  2. In the set of iterations including and after K, the loop body executes
  //     at least one side effect.  In this case, that specific instance of side
  //     effect is control dependent on poison, which also yields undefined
  //     behavior.

  auto *ExitingBB = L->getExitingBlock();
  auto *LatchBB = L->getLoopLatch();
  if (!ExitingBB || !LatchBB || ExitingBB != LatchBB)
    return false;

  SmallPtrSet<const Instruction *, 16> Pushed;
  SmallVector<const Instruction *, 8> PoisonStack;

  // We start by assuming \c I, the post-inc add recurrence, is poison.  Only
  // things that are known to be fully poison under that assumption go on the
  // PoisonStack.
  Pushed.insert(I);
  PoisonStack.push_back(I);

  bool LatchControlDependentOnPoison = false;
  while (!PoisonStack.empty() && !LatchControlDependentOnPoison) {
    const Instruction *Poison = PoisonStack.pop_back_val();

    for (auto *PoisonUser : Poison->users()) {
      if (propagatesFullPoison(cast<Instruction>(PoisonUser))) {
        if (Pushed.insert(cast<Instruction>(PoisonUser)).second)
          PoisonStack.push_back(cast<Instruction>(PoisonUser));
      } else if (auto *BI = dyn_cast<BranchInst>(PoisonUser)) {
        assert(BI->isConditional() && "Only possibility!");
        if (BI->getParent() == LatchBB) {
          LatchControlDependentOnPoison = true;
          break;
        }
      }
    }
  }

  return LatchControlDependentOnPoison && loopHasNoAbnormalExits(L);
}

ScalarEvolution::LoopProperties
ScalarEvolution::getLoopProperties(const Loop *L) {
  using LoopProperties = ScalarEvolution::LoopProperties;

  auto Itr = LoopPropertiesCache.find(L);
  if (Itr == LoopPropertiesCache.end()) {
    auto HasSideEffects = [](Instruction *I) {
      if (auto *SI = dyn_cast<StoreInst>(I))
        return !SI->isSimple();

      return I->mayHaveSideEffects();
    };

    LoopProperties LP = {/* HasNoAbnormalExits */ true,
                         /*HasNoSideEffects*/ true};

    for (auto *BB : L->getBlocks())
      for (auto &I : *BB) {
        if (!isGuaranteedToTransferExecutionToSuccessor(&I))
          LP.HasNoAbnormalExits = false;
        if (HasSideEffects(&I))
          LP.HasNoSideEffects = false;
        if (!LP.HasNoAbnormalExits && !LP.HasNoSideEffects)
          break; // We're already as pessimistic as we can get.
      }

    auto InsertPair = LoopPropertiesCache.insert({L, LP});
    assert(InsertPair.second && "We just checked!");
    Itr = InsertPair.first;
  }

  return Itr->second;
}

const SCEV *ScalarEvolution::createSCEV(Value *V) {
  if (!isSCEVable(V->getType()))
    return getUnknown(V);

  if (Instruction *I = dyn_cast<Instruction>(V)) {
    // Don't attempt to analyze instructions in blocks that aren't
    // reachable. Such instructions don't matter, and they aren't required
    // to obey basic rules for definitions dominating uses which this
    // analysis depends on.
    if (!DT.isReachableFromEntry(I->getParent()))
      return getUnknown(V);
  } else if (ConstantInt *CI = dyn_cast<ConstantInt>(V))
    return getConstant(CI);
  else if (isa<ConstantPointerNull>(V))
    return getZero(V->getType());
  else if (GlobalAlias *GA = dyn_cast<GlobalAlias>(V))
    return GA->isInterposable() ? getUnknown(V) : getSCEV(GA->getAliasee());
  else if (!isa<ConstantExpr>(V))
    return getUnknown(V);

  Operator *U = cast<Operator>(V);
  if (auto BO = MatchBinaryOp(U, DT)) {
    switch (BO->Opcode) {
    case Instruction::Add: {
      // The simple thing to do would be to just call getSCEV on both operands
      // and call getAddExpr with the result. However if we're looking at a
      // bunch of things all added together, this can be quite inefficient,
      // because it leads to N-1 getAddExpr calls for N ultimate operands.
      // Instead, gather up all the operands and make a single getAddExpr call.
      // LLVM IR canonical form means we need only traverse the left operands.
      SmallVector<const SCEV *, 4> AddOps;
      do {
        if (BO->Op) {
          if (auto *OpSCEV = getExistingSCEV(BO->Op)) {
            AddOps.push_back(OpSCEV);
            break;
          }

          // If a NUW or NSW flag can be applied to the SCEV for this
          // addition, then compute the SCEV for this addition by itself
          // with a separate call to getAddExpr. We need to do that
          // instead of pushing the operands of the addition onto AddOps,
          // since the flags are only known to apply to this particular
          // addition - they may not apply to other additions that can be
          // formed with operands from AddOps.
          const SCEV *RHS = getSCEV(BO->RHS);
          SCEV::NoWrapFlags Flags = getNoWrapFlagsFromUB(BO->Op);
          if (Flags != SCEV::FlagAnyWrap) {
            const SCEV *LHS = getSCEV(BO->LHS);
            if (BO->Opcode == Instruction::Sub)
              AddOps.push_back(getMinusSCEV(LHS, RHS, Flags));
            else
              AddOps.push_back(getAddExpr(LHS, RHS, Flags));
            break;
          }
        }

        if (BO->Opcode == Instruction::Sub)
          AddOps.push_back(getNegativeSCEV(getSCEV(BO->RHS)));
        else
          AddOps.push_back(getSCEV(BO->RHS));

        auto NewBO = MatchBinaryOp(BO->LHS, DT);
        if (!NewBO || (NewBO->Opcode != Instruction::Add &&
                       NewBO->Opcode != Instruction::Sub)) {
          AddOps.push_back(getSCEV(BO->LHS));
          break;
        }
        BO = NewBO;
      } while (true);

      return getAddExpr(AddOps);
    }

    case Instruction::Mul: {
      SmallVector<const SCEV *, 4> MulOps;
      do {
        if (BO->Op) {
          if (auto *OpSCEV = getExistingSCEV(BO->Op)) {
            MulOps.push_back(OpSCEV);
            break;
          }

          SCEV::NoWrapFlags Flags = getNoWrapFlagsFromUB(BO->Op);
          if (Flags != SCEV::FlagAnyWrap) {
            MulOps.push_back(
                getMulExpr(getSCEV(BO->LHS), getSCEV(BO->RHS), Flags));
            break;
          }
        }

        MulOps.push_back(getSCEV(BO->RHS));
        auto NewBO = MatchBinaryOp(BO->LHS, DT);
        if (!NewBO || NewBO->Opcode != Instruction::Mul) {
          MulOps.push_back(getSCEV(BO->LHS));
          break;
        }
        BO = NewBO;
      } while (true);

      return getMulExpr(MulOps);
    }
    case Instruction::UDiv:
      return getUDivExpr(getSCEV(BO->LHS), getSCEV(BO->RHS));
    case Instruction::URem:
      return getURemExpr(getSCEV(BO->LHS), getSCEV(BO->RHS));
    case Instruction::Sub: {
      SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap;
      if (BO->Op)
        Flags = getNoWrapFlagsFromUB(BO->Op);
      return getMinusSCEV(getSCEV(BO->LHS), getSCEV(BO->RHS), Flags);
    }
    case Instruction::And:
      // For an expression like x&255 that merely masks off the high bits,
      // use zext(trunc(x)) as the SCEV expression.
      if (ConstantInt *CI = dyn_cast<ConstantInt>(BO->RHS)) {
        if (CI->isZero())
          return getSCEV(BO->RHS);
        if (CI->isMinusOne())
          return getSCEV(BO->LHS);
        const APInt &A = CI->getValue();

        // Instcombine's ShrinkDemandedConstant may strip bits out of
        // constants, obscuring what would otherwise be a low-bits mask.
        // Use computeKnownBits to compute what ShrinkDemandedConstant
        // knew about to reconstruct a low-bits mask value.
        unsigned LZ = A.countLeadingZeros();
        unsigned TZ = A.countTrailingZeros();
        unsigned BitWidth = A.getBitWidth();
        KnownBits Known(BitWidth);
        computeKnownBits(BO->LHS, Known, getDataLayout(),
                         0, &AC, nullptr, &DT);

        APInt EffectiveMask =
            APInt::getLowBitsSet(BitWidth, BitWidth - LZ - TZ).shl(TZ);
        if ((LZ != 0 || TZ != 0) && !((~A & ~Known.Zero) & EffectiveMask)) {
          const SCEV *MulCount = getConstant(APInt::getOneBitSet(BitWidth, TZ));
          const SCEV *LHS = getSCEV(BO->LHS);
          const SCEV *ShiftedLHS = nullptr;
          if (auto *LHSMul = dyn_cast<SCEVMulExpr>(LHS)) {
            if (auto *OpC = dyn_cast<SCEVConstant>(LHSMul->getOperand(0))) {
              // For an expression like (x * 8) & 8, simplify the multiply.
              unsigned MulZeros = OpC->getAPInt().countTrailingZeros();
              unsigned GCD = std::min(MulZeros, TZ);
              APInt DivAmt = APInt::getOneBitSet(BitWidth, TZ - GCD);
              SmallVector<const SCEV*, 4> MulOps;
              MulOps.push_back(getConstant(OpC->getAPInt().lshr(GCD)));
              MulOps.append(LHSMul->op_begin() + 1, LHSMul->op_end());
              auto *NewMul = getMulExpr(MulOps, LHSMul->getNoWrapFlags());
              ShiftedLHS = getUDivExpr(NewMul, getConstant(DivAmt));
            }
          }
          if (!ShiftedLHS)
            ShiftedLHS = getUDivExpr(LHS, MulCount);
          return getMulExpr(
              getZeroExtendExpr(
                  getTruncateExpr(ShiftedLHS,
                      IntegerType::get(getContext(), BitWidth - LZ - TZ)),
                  BO->LHS->getType()),
              MulCount);
        }
      }
      break;

    case Instruction::Or:
      // If the RHS of the Or is a constant, we may have something like:
      // X*4+1 which got turned into X*4|1.  Handle this as an Add so loop
      // optimizations will transparently handle this case.
      //
      // In order for this transformation to be safe, the LHS must be of the
      // form X*(2^n) and the Or constant must be less than 2^n.
      if (ConstantInt *CI = dyn_cast<ConstantInt>(BO->RHS)) {
        const SCEV *LHS = getSCEV(BO->LHS);
        const APInt &CIVal = CI->getValue();
        if (GetMinTrailingZeros(LHS) >=
            (CIVal.getBitWidth() - CIVal.countLeadingZeros())) {
          // Build a plain add SCEV.
          const SCEV *S = getAddExpr(LHS, getSCEV(CI));
          // If the LHS of the add was an addrec and it has no-wrap flags,
          // transfer the no-wrap flags, since an or won't introduce a wrap.
          if (const SCEVAddRecExpr *NewAR = dyn_cast<SCEVAddRecExpr>(S)) {
            const SCEVAddRecExpr *OldAR = cast<SCEVAddRecExpr>(LHS);
            const_cast<SCEVAddRecExpr *>(NewAR)->setNoWrapFlags(
                OldAR->getNoWrapFlags());
          }
          return S;
        }
      }
      break;

    case Instruction::Xor:
      if (ConstantInt *CI = dyn_cast<ConstantInt>(BO->RHS)) {
        // If the RHS of xor is -1, then this is a not operation.
        if (CI->isMinusOne())
          return getNotSCEV(getSCEV(BO->LHS));

        // Model xor(and(x, C), C) as and(~x, C), if C is a low-bits mask.
        // This is a variant of the check for xor with -1, and it handles
        // the case where instcombine has trimmed non-demanded bits out
        // of an xor with -1.
        if (auto *LBO = dyn_cast<BinaryOperator>(BO->LHS))
          if (ConstantInt *LCI = dyn_cast<ConstantInt>(LBO->getOperand(1)))
            if (LBO->getOpcode() == Instruction::And &&
                LCI->getValue() == CI->getValue())
              if (const SCEVZeroExtendExpr *Z =
                      dyn_cast<SCEVZeroExtendExpr>(getSCEV(BO->LHS))) {
                Type *UTy = BO->LHS->getType();
                const SCEV *Z0 = Z->getOperand();
                Type *Z0Ty = Z0->getType();
                unsigned Z0TySize = getTypeSizeInBits(Z0Ty);

                // If C is a low-bits mask, the zero extend is serving to
                // mask off the high bits. Complement the operand and
                // re-apply the zext.
                if (CI->getValue().isMask(Z0TySize))
                  return getZeroExtendExpr(getNotSCEV(Z0), UTy);

                // If C is a single bit, it may be in the sign-bit position
                // before the zero-extend. In this case, represent the xor
                // using an add, which is equivalent, and re-apply the zext.
                APInt Trunc = CI->getValue().trunc(Z0TySize);
                if (Trunc.zext(getTypeSizeInBits(UTy)) == CI->getValue() &&
                    Trunc.isSignMask())
                  return getZeroExtendExpr(getAddExpr(Z0, getConstant(Trunc)),
                                           UTy);
              }
      }
      break;

    case Instruction::Shl:
      // Turn shift left of a constant amount into a multiply.
      if (ConstantInt *SA = dyn_cast<ConstantInt>(BO->RHS)) {
        uint32_t BitWidth = cast<IntegerType>(SA->getType())->getBitWidth();

        // If the shift count is not less than the bitwidth, the result of
        // the shift is undefined. Don't try to analyze it, because the
        // resolution chosen here may differ from the resolution chosen in
        // other parts of the compiler.
        if (SA->getValue().uge(BitWidth))
          break;

        // It is currently not resolved how to interpret NSW for left
        // shift by BitWidth - 1, so we avoid applying flags in that
        // case. Remove this check (or this comment) once the situation
        // is resolved. See
        // http://lists.llvm.org/pipermail/llvm-dev/2015-April/084195.html
        // and http://reviews.llvm.org/D8890 .
        auto Flags = SCEV::FlagAnyWrap;
        if (BO->Op && SA->getValue().ult(BitWidth - 1))
          Flags = getNoWrapFlagsFromUB(BO->Op);

        Constant *X = ConstantInt::get(
            getContext(), APInt::getOneBitSet(BitWidth, SA->getZExtValue()));
        return getMulExpr(getSCEV(BO->LHS), getSCEV(X), Flags);
      }
      break;

    case Instruction::AShr: {
      // AShr X, C, where C is a constant.
      ConstantInt *CI = dyn_cast<ConstantInt>(BO->RHS);
      if (!CI)
        break;

      Type *OuterTy = BO->LHS->getType();
      uint64_t BitWidth = getTypeSizeInBits(OuterTy);
      // If the shift count is not less than the bitwidth, the result of
      // the shift is undefined. Don't try to analyze it, because the
      // resolution chosen here may differ from the resolution chosen in
      // other parts of the compiler.
      if (CI->getValue().uge(BitWidth))
        break;

      if (CI->isZero())
        return getSCEV(BO->LHS); // shift by zero --> noop

      uint64_t AShrAmt = CI->getZExtValue();
      Type *TruncTy = IntegerType::get(getContext(), BitWidth - AShrAmt);

      Operator *L = dyn_cast<Operator>(BO->LHS);
      if (L && L->getOpcode() == Instruction::Shl) {
        // X = Shl A, n
        // Y = AShr X, m
        // Both n and m are constant.

        const SCEV *ShlOp0SCEV = getSCEV(L->getOperand(0));
        if (L->getOperand(1) == BO->RHS)
          // For a two-shift sext-inreg, i.e. n = m,
          // use sext(trunc(x)) as the SCEV expression.
          return getSignExtendExpr(
              getTruncateExpr(ShlOp0SCEV, TruncTy), OuterTy);

        ConstantInt *ShlAmtCI = dyn_cast<ConstantInt>(L->getOperand(1));
        if (ShlAmtCI && ShlAmtCI->getValue().ult(BitWidth)) {
          uint64_t ShlAmt = ShlAmtCI->getZExtValue();
          if (ShlAmt > AShrAmt) {
            // When n > m, use sext(mul(trunc(x), 2^(n-m)))) as the SCEV
            // expression. We already checked that ShlAmt < BitWidth, so
            // the multiplier, 1 << (ShlAmt - AShrAmt), fits into TruncTy as
            // ShlAmt - AShrAmt < Amt.
            APInt Mul = APInt::getOneBitSet(BitWidth - AShrAmt,
                                            ShlAmt - AShrAmt);
            return getSignExtendExpr(
                getMulExpr(getTruncateExpr(ShlOp0SCEV, TruncTy),
                getConstant(Mul)), OuterTy);
          }
        }
      }
      break;
    }
    }
  }

  switch (U->getOpcode()) {
  case Instruction::Trunc:
    return getTruncateExpr(getSCEV(U->getOperand(0)), U->getType());

  case Instruction::ZExt:
    return getZeroExtendExpr(getSCEV(U->getOperand(0)), U->getType());

  case Instruction::SExt:
    if (auto BO = MatchBinaryOp(U->getOperand(0), DT)) {
      // The NSW flag of a subtract does not always survive the conversion to
      // A + (-1)*B.  By pushing sign extension onto its operands we are much
      // more likely to preserve NSW and allow later AddRec optimisations.
      //
      // NOTE: This is effectively duplicating this logic from getSignExtend:
      //   sext((A + B + ...)<nsw>) --> (sext(A) + sext(B) + ...)<nsw>
      // but by that point the NSW information has potentially been lost.
      if (BO->Opcode == Instruction::Sub && BO->IsNSW) {
        Type *Ty = U->getType();
        auto *V1 = getSignExtendExpr(getSCEV(BO->LHS), Ty);
        auto *V2 = getSignExtendExpr(getSCEV(BO->RHS), Ty);
        return getMinusSCEV(V1, V2, SCEV::FlagNSW);
      }
    }
    return getSignExtendExpr(getSCEV(U->getOperand(0)), U->getType());

  case Instruction::BitCast:
    // BitCasts are no-op casts so we just eliminate the cast.
    if (isSCEVable(U->getType()) && isSCEVable(U->getOperand(0)->getType()))
      return getSCEV(U->getOperand(0));
    break;

  // It's tempting to handle inttoptr and ptrtoint as no-ops, however this can
  // lead to pointer expressions which cannot safely be expanded to GEPs,
  // because ScalarEvolution doesn't respect the GEP aliasing rules when
  // simplifying integer expressions.

  case Instruction::GetElementPtr:
    return createNodeForGEP(cast<GEPOperator>(U));

  case Instruction::PHI:
    return createNodeForPHI(cast<PHINode>(U));

  case Instruction::Select:
    // U can also be a select constant expr, which let fall through.  Since
    // createNodeForSelect only works for a condition that is an `ICmpInst`, and
    // constant expressions cannot have instructions as operands, we'd have
    // returned getUnknown for a select constant expressions anyway.
    if (isa<Instruction>(U))
      return createNodeForSelectOrPHI(cast<Instruction>(U), U->getOperand(0),
                                      U->getOperand(1), U->getOperand(2));
    break;

  case Instruction::Call:
  case Instruction::Invoke:
    if (Value *RV = CallSite(U).getReturnedArgOperand())
      return getSCEV(RV);
    break;
  }

  return getUnknown(V);
}

//===----------------------------------------------------------------------===//
//                   Iteration Count Computation Code
//

static unsigned getConstantTripCount(const SCEVConstant *ExitCount) {
  if (!ExitCount)
    return 0;

  ConstantInt *ExitConst = ExitCount->getValue();

  // Guard against huge trip counts.
  if (ExitConst->getValue().getActiveBits() > 32)
    return 0;

  // In case of integer overflow, this returns 0, which is correct.
  return ((unsigned)ExitConst->getZExtValue()) + 1;
}

unsigned ScalarEvolution::getSmallConstantTripCount(const Loop *L) {
  if (BasicBlock *ExitingBB = L->getExitingBlock())
    return getSmallConstantTripCount(L, ExitingBB);

  // No trip count information for multiple exits.
  return 0;
}

unsigned ScalarEvolution::getSmallConstantTripCount(const Loop *L,
                                                    BasicBlock *ExitingBlock) {
  assert(ExitingBlock && "Must pass a non-null exiting block!");
  assert(L->isLoopExiting(ExitingBlock) &&
         "Exiting block must actually branch out of the loop!");
  const SCEVConstant *ExitCount =
      dyn_cast<SCEVConstant>(getExitCount(L, ExitingBlock));
  return getConstantTripCount(ExitCount);
}

unsigned ScalarEvolution::getSmallConstantMaxTripCount(const Loop *L) {
  const auto *MaxExitCount =
      dyn_cast<SCEVConstant>(getMaxBackedgeTakenCount(L));
  return getConstantTripCount(MaxExitCount);
}

unsigned ScalarEvolution::getSmallConstantTripMultiple(const Loop *L) {
  if (BasicBlock *ExitingBB = L->getExitingBlock())
    return getSmallConstantTripMultiple(L, ExitingBB);

  // No trip multiple information for multiple exits.
  return 0;
}

/// Returns the largest constant divisor of the trip count of this loop as a
/// normal unsigned value, if possible. This means that the actual trip count is
/// always a multiple of the returned value (don't forget the trip count could
/// very well be zero as well!).
///
/// Returns 1 if the trip count is unknown or not guaranteed to be the
/// multiple of a constant (which is also the case if the trip count is simply
/// constant, use getSmallConstantTripCount for that case), Will also return 1
/// if the trip count is very large (>= 2^32).
///
/// As explained in the comments for getSmallConstantTripCount, this assumes
/// that control exits the loop via ExitingBlock.
unsigned
ScalarEvolution::getSmallConstantTripMultiple(const Loop *L,
                                              BasicBlock *ExitingBlock) {
  assert(ExitingBlock && "Must pass a non-null exiting block!");
  assert(L->isLoopExiting(ExitingBlock) &&
         "Exiting block must actually branch out of the loop!");
  const SCEV *ExitCount = getExitCount(L, ExitingBlock);
  if (ExitCount == getCouldNotCompute())
    return 1;

  // Get the trip count from the BE count by adding 1.
  const SCEV *TCExpr = getAddExpr(ExitCount, getOne(ExitCount->getType()));

  const SCEVConstant *TC = dyn_cast<SCEVConstant>(TCExpr);
  if (!TC)
    // Attempt to factor more general cases. Returns the greatest power of
    // two divisor. If overflow happens, the trip count expression is still
    // divisible by the greatest power of 2 divisor returned.
    return 1U << std::min((uint32_t)31, GetMinTrailingZeros(TCExpr));

  ConstantInt *Result = TC->getValue();

  // Guard against huge trip counts (this requires checking
  // for zero to handle the case where the trip count == -1 and the
  // addition wraps).
  if (!Result || Result->getValue().getActiveBits() > 32 ||
      Result->getValue().getActiveBits() == 0)
    return 1;

  return (unsigned)Result->getZExtValue();
}

/// Get the expression for the number of loop iterations for which this loop is
/// guaranteed not to exit via ExitingBlock. Otherwise return
/// SCEVCouldNotCompute.
const SCEV *ScalarEvolution::getExitCount(const Loop *L,
                                          BasicBlock *ExitingBlock) {
  return getBackedgeTakenInfo(L).getExact(ExitingBlock, this);
}

const SCEV *
ScalarEvolution::getPredicatedBackedgeTakenCount(const Loop *L,
                                                 SCEVUnionPredicate &Preds) {
  return getPredicatedBackedgeTakenInfo(L).getExact(L, this, &Preds);
}

const SCEV *ScalarEvolution::getBackedgeTakenCount(const Loop *L) {
  return getBackedgeTakenInfo(L).getExact(L, this);
}

/// Similar to getBackedgeTakenCount, except return the least SCEV value that is
/// known never to be less than the actual backedge taken count.
const SCEV *ScalarEvolution::getMaxBackedgeTakenCount(const Loop *L) {
  return getBackedgeTakenInfo(L).getMax(this);
}

bool ScalarEvolution::isBackedgeTakenCountMaxOrZero(const Loop *L) {
  return getBackedgeTakenInfo(L).isMaxOrZero(this);
}

/// Push PHI nodes in the header of the given loop onto the given Worklist.
static void
PushLoopPHIs(const Loop *L, SmallVectorImpl<Instruction *> &Worklist) {
  BasicBlock *Header = L->getHeader();

  // Push all Loop-header PHIs onto the Worklist stack.
  for (PHINode &PN : Header->phis())
    Worklist.push_back(&PN);
}

const ScalarEvolution::BackedgeTakenInfo &
ScalarEvolution::getPredicatedBackedgeTakenInfo(const Loop *L) {
  auto &BTI = getBackedgeTakenInfo(L);
  if (BTI.hasFullInfo())
    return BTI;

  auto Pair = PredicatedBackedgeTakenCounts.insert({L, BackedgeTakenInfo()});

  if (!Pair.second)
    return Pair.first->second;

  BackedgeTakenInfo Result =
      computeBackedgeTakenCount(L, /*AllowPredicates=*/true);

  return PredicatedBackedgeTakenCounts.find(L)->second = std::move(Result);
}

const ScalarEvolution::BackedgeTakenInfo &
ScalarEvolution::getBackedgeTakenInfo(const Loop *L) {
  // Initially insert an invalid entry for this loop. If the insertion
  // succeeds, proceed to actually compute a backedge-taken count and
  // update the value. The temporary CouldNotCompute value tells SCEV
  // code elsewhere that it shouldn't attempt to request a new
  // backedge-taken count, which could result in infinite recursion.
  std::pair<DenseMap<const Loop *, BackedgeTakenInfo>::iterator, bool> Pair =
      BackedgeTakenCounts.insert({L, BackedgeTakenInfo()});
  if (!Pair.second)
    return Pair.first->second;

  // computeBackedgeTakenCount may allocate memory for its result. Inserting it
  // into the BackedgeTakenCounts map transfers ownership. Otherwise, the result
  // must be cleared in this scope.
  BackedgeTakenInfo Result = computeBackedgeTakenCount(L);

  // In product build, there are no usage of statistic.
  (void)NumTripCountsComputed;
  (void)NumTripCountsNotComputed;
#if LLVM_ENABLE_STATS || !defined(NDEBUG)
  const SCEV *BEExact = Result.getExact(L, this);
  if (BEExact != getCouldNotCompute()) {
    assert(isLoopInvariant(BEExact, L) &&
           isLoopInvariant(Result.getMax(this), L) &&
           "Computed backedge-taken count isn't loop invariant for loop!");
    ++NumTripCountsComputed;
  }
  else if (Result.getMax(this) == getCouldNotCompute() &&
           isa<PHINode>(L->getHeader()->begin())) {
    // Only count loops that have phi nodes as not being computable.
    ++NumTripCountsNotComputed;
  }
#endif // LLVM_ENABLE_STATS || !defined(NDEBUG)

  // Now that we know more about the trip count for this loop, forget any
  // existing SCEV values for PHI nodes in this loop since they are only
  // conservative estimates made without the benefit of trip count
  // information. This is similar to the code in forgetLoop, except that
  // it handles SCEVUnknown PHI nodes specially.
  if (Result.hasAnyInfo()) {
    SmallVector<Instruction *, 16> Worklist;
    PushLoopPHIs(L, Worklist);

    SmallPtrSet<Instruction *, 8> Discovered;
    while (!Worklist.empty()) {
      Instruction *I = Worklist.pop_back_val();

      ValueExprMapType::iterator It =
        ValueExprMap.find_as(static_cast<Value *>(I));
      if (It != ValueExprMap.end()) {
        const SCEV *Old = It->second;

        // SCEVUnknown for a PHI either means that it has an unrecognized
        // structure, or it's a PHI that's in the progress of being computed
        // by createNodeForPHI.  In the former case, additional loop trip
        // count information isn't going to change anything. In the later
        // case, createNodeForPHI will perform the necessary updates on its
        // own when it gets to that point.
        if (!isa<PHINode>(I) || !isa<SCEVUnknown>(Old)) {
          eraseValueFromMap(It->first);
          forgetMemoizedResults(Old);
        }
        if (PHINode *PN = dyn_cast<PHINode>(I))
          ConstantEvolutionLoopExitValue.erase(PN);
      }

      // Since we don't need to invalidate anything for correctness and we're
      // only invalidating to make SCEV's results more precise, we get to stop
      // early to avoid invalidating too much.  This is especially important in
      // cases like:
      //
      //   %v = f(pn0, pn1) // pn0 and pn1 used through some other phi node
      // loop0:
      //   %pn0 = phi
      //   ...
      // loop1:
      //   %pn1 = phi
      //   ...
      //
      // where both loop0 and loop1's backedge taken count uses the SCEV
      // expression for %v.  If we don't have the early stop below then in cases
      // like the above, getBackedgeTakenInfo(loop1) will clear out the trip
      // count for loop0 and getBackedgeTakenInfo(loop0) will clear out the trip
      // count for loop1, effectively nullifying SCEV's trip count cache.
      for (auto *U : I->users())
        if (auto *I = dyn_cast<Instruction>(U)) {
          auto *LoopForUser = LI.getLoopFor(I->getParent());
          if (LoopForUser && L->contains(LoopForUser) &&
              Discovered.insert(I).second)
            Worklist.push_back(I);
        }
    }
  }

  // Re-lookup the insert position, since the call to
  // computeBackedgeTakenCount above could result in a
  // recusive call to getBackedgeTakenInfo (on a different
  // loop), which would invalidate the iterator computed
  // earlier.
  return BackedgeTakenCounts.find(L)->second = std::move(Result);
}

void ScalarEvolution::forgetLoop(const Loop *L) {
  // Drop any stored trip count value.
  auto RemoveLoopFromBackedgeMap =
      [](DenseMap<const Loop *, BackedgeTakenInfo> &Map, const Loop *L) {
        auto BTCPos = Map.find(L);
        if (BTCPos != Map.end()) {
          BTCPos->second.clear();
          Map.erase(BTCPos);
        }
      };

  SmallVector<const Loop *, 16> LoopWorklist(1, L);
  SmallVector<Instruction *, 32> Worklist;
  SmallPtrSet<Instruction *, 16> Visited;

  // Iterate over all the loops and sub-loops to drop SCEV information.
  while (!LoopWorklist.empty()) {
    auto *CurrL = LoopWorklist.pop_back_val();

    RemoveLoopFromBackedgeMap(BackedgeTakenCounts, CurrL);
    RemoveLoopFromBackedgeMap(PredicatedBackedgeTakenCounts, CurrL);

    // Drop information about predicated SCEV rewrites for this loop.
    for (auto I = PredicatedSCEVRewrites.begin();
         I != PredicatedSCEVRewrites.end();) {
      std::pair<const SCEV *, const Loop *> Entry = I->first;
      if (Entry.second == CurrL)
        PredicatedSCEVRewrites.erase(I++);
      else
        ++I;
    }

    auto LoopUsersItr = LoopUsers.find(CurrL);
    if (LoopUsersItr != LoopUsers.end()) {
      for (auto *S : LoopUsersItr->second)
        forgetMemoizedResults(S);
      LoopUsers.erase(LoopUsersItr);
    }

    // Drop information about expressions based on loop-header PHIs.
    PushLoopPHIs(CurrL, Worklist);

    while (!Worklist.empty()) {
      Instruction *I = Worklist.pop_back_val();
      if (!Visited.insert(I).second)
        continue;

      ValueExprMapType::iterator It =
          ValueExprMap.find_as(static_cast<Value *>(I));
      if (It != ValueExprMap.end()) {
        eraseValueFromMap(It->first);
        forgetMemoizedResults(It->second);
        if (PHINode *PN = dyn_cast<PHINode>(I))
          ConstantEvolutionLoopExitValue.erase(PN);
      }

      PushDefUseChildren(I, Worklist);
    }

    LoopPropertiesCache.erase(CurrL);
    // Forget all contained loops too, to avoid dangling entries in the
    // ValuesAtScopes map.
    LoopWorklist.append(CurrL->begin(), CurrL->end());
  }
}

void ScalarEvolution::forgetTopmostLoop(const Loop *L) {
  while (Loop *Parent = L->getParentLoop())
    L = Parent;
  forgetLoop(L);
}

void ScalarEvolution::forgetValue(Value *V) {
  Instruction *I = dyn_cast<Instruction>(V);
  if (!I) return;

  // Drop information about expressions based on loop-header PHIs.
  SmallVector<Instruction *, 16> Worklist;
  Worklist.push_back(I);

  SmallPtrSet<Instruction *, 8> Visited;
  while (!Worklist.empty()) {
    I = Worklist.pop_back_val();
    if (!Visited.insert(I).second)
      continue;

    ValueExprMapType::iterator It =
      ValueExprMap.find_as(static_cast<Value *>(I));
    if (It != ValueExprMap.end()) {
      eraseValueFromMap(It->first);
      forgetMemoizedResults(It->second);
      if (PHINode *PN = dyn_cast<PHINode>(I))
        ConstantEvolutionLoopExitValue.erase(PN);
    }

    PushDefUseChildren(I, Worklist);
  }
}

/// Get the exact loop backedge taken count considering all loop exits. A
/// computable result can only be returned for loops with all exiting blocks
/// dominating the latch. howFarToZero assumes that the limit of each loop test
/// is never skipped. This is a valid assumption as long as the loop exits via
/// that test. For precise results, it is the caller's responsibility to specify
/// the relevant loop exiting block using getExact(ExitingBlock, SE).
const SCEV *
ScalarEvolution::BackedgeTakenInfo::getExact(const Loop *L, ScalarEvolution *SE,
                                             SCEVUnionPredicate *Preds) const {
  // If any exits were not computable, the loop is not computable.
  if (!isComplete() || ExitNotTaken.empty())
    return SE->getCouldNotCompute();

  const BasicBlock *Latch = L->getLoopLatch();
  // All exiting blocks we have collected must dominate the only backedge.
  if (!Latch)
    return SE->getCouldNotCompute();

  // All exiting blocks we have gathered dominate loop's latch, so exact trip
  // count is simply a minimum out of all these calculated exit counts.
  SmallVector<const SCEV *, 2> Ops;
  for (auto &ENT : ExitNotTaken) {
    const SCEV *BECount = ENT.ExactNotTaken;
    assert(BECount != SE->getCouldNotCompute() && "Bad exit SCEV!");
    assert(SE->DT.dominates(ENT.ExitingBlock, Latch) &&
           "We should only have known counts for exiting blocks that dominate "
           "latch!");

    Ops.push_back(BECount);

    if (Preds && !ENT.hasAlwaysTruePredicate())
      Preds->add(ENT.Predicate.get());

    assert((Preds || ENT.hasAlwaysTruePredicate()) &&
           "Predicate should be always true!");
  }

  return SE->getUMinFromMismatchedTypes(Ops);
}

/// Get the exact not taken count for this loop exit.
const SCEV *
ScalarEvolution::BackedgeTakenInfo::getExact(BasicBlock *ExitingBlock,
                                             ScalarEvolution *SE) const {
  for (auto &ENT : ExitNotTaken)
    if (ENT.ExitingBlock == ExitingBlock && ENT.hasAlwaysTruePredicate())
      return ENT.ExactNotTaken;

  return SE->getCouldNotCompute();
}

/// getMax - Get the max backedge taken count for the loop.
const SCEV *
ScalarEvolution::BackedgeTakenInfo::getMax(ScalarEvolution *SE) const {
  auto PredicateNotAlwaysTrue = [](const ExitNotTakenInfo &ENT) {
    return !ENT.hasAlwaysTruePredicate();
  };

  if (any_of(ExitNotTaken, PredicateNotAlwaysTrue) || !getMax())
    return SE->getCouldNotCompute();

  assert((isa<SCEVCouldNotCompute>(getMax()) || isa<SCEVConstant>(getMax())) &&
         "No point in having a non-constant max backedge taken count!");
  return getMax();
}

bool ScalarEvolution::BackedgeTakenInfo::isMaxOrZero(ScalarEvolution *SE) const {
  auto PredicateNotAlwaysTrue = [](const ExitNotTakenInfo &ENT) {
    return !ENT.hasAlwaysTruePredicate();
  };
  return MaxOrZero && !any_of(ExitNotTaken, PredicateNotAlwaysTrue);
}

bool ScalarEvolution::BackedgeTakenInfo::hasOperand(const SCEV *S,
                                                    ScalarEvolution *SE) const {
  if (getMax() && getMax() != SE->getCouldNotCompute() &&
      SE->hasOperand(getMax(), S))
    return true;

  for (auto &ENT : ExitNotTaken)
    if (ENT.ExactNotTaken != SE->getCouldNotCompute() &&
        SE->hasOperand(ENT.ExactNotTaken, S))
      return true;

  return false;
}

ScalarEvolution::ExitLimit::ExitLimit(const SCEV *E)
    : ExactNotTaken(E), MaxNotTaken(E) {
  assert((isa<SCEVCouldNotCompute>(MaxNotTaken) ||
          isa<SCEVConstant>(MaxNotTaken)) &&
         "No point in having a non-constant max backedge taken count!");
}

ScalarEvolution::ExitLimit::ExitLimit(
    const SCEV *E, const SCEV *M, bool MaxOrZero,
    ArrayRef<const SmallPtrSetImpl<const SCEVPredicate *> *> PredSetList)
    : ExactNotTaken(E), MaxNotTaken(M), MaxOrZero(MaxOrZero) {
  assert((isa<SCEVCouldNotCompute>(ExactNotTaken) ||
          !isa<SCEVCouldNotCompute>(MaxNotTaken)) &&
         "Exact is not allowed to be less precise than Max");
  assert((isa<SCEVCouldNotCompute>(MaxNotTaken) ||
          isa<SCEVConstant>(MaxNotTaken)) &&
         "No point in having a non-constant max backedge taken count!");
  for (auto *PredSet : PredSetList)
    for (auto *P : *PredSet)
      addPredicate(P);
}

ScalarEvolution::ExitLimit::ExitLimit(
    const SCEV *E, const SCEV *M, bool MaxOrZero,
    const SmallPtrSetImpl<const SCEVPredicate *> &PredSet)
    : ExitLimit(E, M, MaxOrZero, {&PredSet}) {
  assert((isa<SCEVCouldNotCompute>(MaxNotTaken) ||
          isa<SCEVConstant>(MaxNotTaken)) &&
         "No point in having a non-constant max backedge taken count!");
}

ScalarEvolution::ExitLimit::ExitLimit(const SCEV *E, const SCEV *M,
                                      bool MaxOrZero)
    : ExitLimit(E, M, MaxOrZero, None) {
  assert((isa<SCEVCouldNotCompute>(MaxNotTaken) ||
          isa<SCEVConstant>(MaxNotTaken)) &&
         "No point in having a non-constant max backedge taken count!");
}

/// Allocate memory for BackedgeTakenInfo and copy the not-taken count of each
/// computable exit into a persistent ExitNotTakenInfo array.
ScalarEvolution::BackedgeTakenInfo::BackedgeTakenInfo(
    SmallVectorImpl<ScalarEvolution::BackedgeTakenInfo::EdgeExitInfo>
        &&ExitCounts,
    bool Complete, const SCEV *MaxCount, bool MaxOrZero)
    : MaxAndComplete(MaxCount, Complete), MaxOrZero(MaxOrZero) {
  using EdgeExitInfo = ScalarEvolution::BackedgeTakenInfo::EdgeExitInfo;

  ExitNotTaken.reserve(ExitCounts.size());
  std::transform(
      ExitCounts.begin(), ExitCounts.end(), std::back_inserter(ExitNotTaken),
      [&](const EdgeExitInfo &EEI) {
        BasicBlock *ExitBB = EEI.first;
        const ExitLimit &EL = EEI.second;
        if (EL.Predicates.empty())
          return ExitNotTakenInfo(ExitBB, EL.ExactNotTaken, nullptr);

        std::unique_ptr<SCEVUnionPredicate> Predicate(new SCEVUnionPredicate);
        for (auto *Pred : EL.Predicates)
          Predicate->add(Pred);

        return ExitNotTakenInfo(ExitBB, EL.ExactNotTaken, std::move(Predicate));
      });
  assert((isa<SCEVCouldNotCompute>(MaxCount) || isa<SCEVConstant>(MaxCount)) &&
         "No point in having a non-constant max backedge taken count!");
}

/// Invalidate this result and free the ExitNotTakenInfo array.
void ScalarEvolution::BackedgeTakenInfo::clear() {
  ExitNotTaken.clear();
}

/// Compute the number of times the backedge of the specified loop will execute.
ScalarEvolution::BackedgeTakenInfo
ScalarEvolution::computeBackedgeTakenCount(const Loop *L,
                                           bool AllowPredicates) {
  SmallVector<BasicBlock *, 8> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);

  using EdgeExitInfo = ScalarEvolution::BackedgeTakenInfo::EdgeExitInfo;

  SmallVector<EdgeExitInfo, 4> ExitCounts;
  bool CouldComputeBECount = true;
  BasicBlock *Latch = L->getLoopLatch(); // may be NULL.
  const SCEV *MustExitMaxBECount = nullptr;
  const SCEV *MayExitMaxBECount = nullptr;
  bool MustExitMaxOrZero = false;

  // Compute the ExitLimit for each loop exit. Use this to populate ExitCounts
  // and compute maxBECount.
  // Do a union of all the predicates here.
  for (unsigned i = 0, e = ExitingBlocks.size(); i != e; ++i) {
    BasicBlock *ExitBB = ExitingBlocks[i];
    ExitLimit EL = computeExitLimit(L, ExitBB, AllowPredicates);

    assert((AllowPredicates || EL.Predicates.empty()) &&
           "Predicated exit limit when predicates are not allowed!");

    // 1. For each exit that can be computed, add an entry to ExitCounts.
    // CouldComputeBECount is true only if all exits can be computed.
    if (EL.ExactNotTaken == getCouldNotCompute())
      // We couldn't compute an exact value for this exit, so
      // we won't be able to compute an exact value for the loop.
      CouldComputeBECount = false;
    else
      ExitCounts.emplace_back(ExitBB, EL);

    // 2. Derive the loop's MaxBECount from each exit's max number of
    // non-exiting iterations. Partition the loop exits into two kinds:
    // LoopMustExits and LoopMayExits.
    //
    // If the exit dominates the loop latch, it is a LoopMustExit otherwise it
    // is a LoopMayExit.  If any computable LoopMustExit is found, then
    // MaxBECount is the minimum EL.MaxNotTaken of computable
    // LoopMustExits. Otherwise, MaxBECount is conservatively the maximum
    // EL.MaxNotTaken, where CouldNotCompute is considered greater than any
    // computable EL.MaxNotTaken.
    if (EL.MaxNotTaken != getCouldNotCompute() && Latch &&
        DT.dominates(ExitBB, Latch)) {
      if (!MustExitMaxBECount) {
        MustExitMaxBECount = EL.MaxNotTaken;
        MustExitMaxOrZero = EL.MaxOrZero;
      } else {
        MustExitMaxBECount =
            getUMinFromMismatchedTypes(MustExitMaxBECount, EL.MaxNotTaken);
      }
    } else if (MayExitMaxBECount != getCouldNotCompute()) {
      if (!MayExitMaxBECount || EL.MaxNotTaken == getCouldNotCompute())
        MayExitMaxBECount = EL.MaxNotTaken;
      else {
        MayExitMaxBECount =
            getUMaxFromMismatchedTypes(MayExitMaxBECount, EL.MaxNotTaken);
      }
    }
  }
  const SCEV *MaxBECount = MustExitMaxBECount ? MustExitMaxBECount :
    (MayExitMaxBECount ? MayExitMaxBECount : getCouldNotCompute());
  // The loop backedge will be taken the maximum or zero times if there's
  // a single exit that must be taken the maximum or zero times.
  bool MaxOrZero = (MustExitMaxOrZero && ExitingBlocks.size() == 1);
  return BackedgeTakenInfo(std::move(ExitCounts), CouldComputeBECount,
                           MaxBECount, MaxOrZero);
}

ScalarEvolution::ExitLimit
ScalarEvolution::computeExitLimit(const Loop *L, BasicBlock *ExitingBlock,
                                      bool AllowPredicates) {
  assert(L->contains(ExitingBlock) && "Exit count for non-loop block?");
  // If our exiting block does not dominate the latch, then its connection with
  // loop's exit limit may be far from trivial.
  const BasicBlock *Latch = L->getLoopLatch();
  if (!Latch || !DT.dominates(ExitingBlock, Latch))
    return getCouldNotCompute();

  bool IsOnlyExit = (L->getExitingBlock() != nullptr);
  Instruction *Term = ExitingBlock->getTerminator();
  if (BranchInst *BI = dyn_cast<BranchInst>(Term)) {
    assert(BI->isConditional() && "If unconditional, it can't be in loop!");
    bool ExitIfTrue = !L->contains(BI->getSuccessor(0));
    assert(ExitIfTrue == L->contains(BI->getSuccessor(1)) &&
           "It should have one successor in loop and one exit block!");
    // Proceed to the next level to examine the exit condition expression.
    return computeExitLimitFromCond(
        L, BI->getCondition(), ExitIfTrue,
        /*ControlsExit=*/IsOnlyExit, AllowPredicates);
  }

  if (SwitchInst *SI = dyn_cast<SwitchInst>(Term)) {
    // For switch, make sure that there is a single exit from the loop.
    BasicBlock *Exit = nullptr;
    for (auto *SBB : successors(ExitingBlock))
      if (!L->contains(SBB)) {
        if (Exit) // Multiple exit successors.
          return getCouldNotCompute();
        Exit = SBB;
      }
    assert(Exit && "Exiting block must have at least one exit");
    return computeExitLimitFromSingleExitSwitch(L, SI, Exit,
                                                /*ControlsExit=*/IsOnlyExit);
  }

  return getCouldNotCompute();
}

ScalarEvolution::ExitLimit ScalarEvolution::computeExitLimitFromCond(
    const Loop *L, Value *ExitCond, bool ExitIfTrue,
    bool ControlsExit, bool AllowPredicates) {
  ScalarEvolution::ExitLimitCacheTy Cache(L, ExitIfTrue, AllowPredicates);
  return computeExitLimitFromCondCached(Cache, L, ExitCond, ExitIfTrue,
                                        ControlsExit, AllowPredicates);
}

Optional<ScalarEvolution::ExitLimit>
ScalarEvolution::ExitLimitCache::find(const Loop *L, Value *ExitCond,
                                      bool ExitIfTrue, bool ControlsExit,
                                      bool AllowPredicates) {
  (void)this->L;
  (void)this->ExitIfTrue;
  (void)this->AllowPredicates;

  assert(this->L == L && this->ExitIfTrue == ExitIfTrue &&
         this->AllowPredicates == AllowPredicates &&
         "Variance in assumed invariant key components!");
  auto Itr = TripCountMap.find({ExitCond, ControlsExit});
  if (Itr == TripCountMap.end())
    return None;
  return Itr->second;
}

void ScalarEvolution::ExitLimitCache::insert(const Loop *L, Value *ExitCond,
                                             bool ExitIfTrue,
                                             bool ControlsExit,
                                             bool AllowPredicates,
                                             const ExitLimit &EL) {
  assert(this->L == L && this->ExitIfTrue == ExitIfTrue &&
         this->AllowPredicates == AllowPredicates &&
         "Variance in assumed invariant key components!");

  auto InsertResult = TripCountMap.insert({{ExitCond, ControlsExit}, EL});
  assert(InsertResult.second && "Expected successful insertion!");
  (void)InsertResult;
  (void)ExitIfTrue;
}

ScalarEvolution::ExitLimit ScalarEvolution::computeExitLimitFromCondCached(
    ExitLimitCacheTy &Cache, const Loop *L, Value *ExitCond, bool ExitIfTrue,
    bool ControlsExit, bool AllowPredicates) {

  if (auto MaybeEL =
          Cache.find(L, ExitCond, ExitIfTrue, ControlsExit, AllowPredicates))
    return *MaybeEL;

  ExitLimit EL = computeExitLimitFromCondImpl(Cache, L, ExitCond, ExitIfTrue,
                                              ControlsExit, AllowPredicates);
  Cache.insert(L, ExitCond, ExitIfTrue, ControlsExit, AllowPredicates, EL);
  return EL;
}

ScalarEvolution::ExitLimit ScalarEvolution::computeExitLimitFromCondImpl(
    ExitLimitCacheTy &Cache, const Loop *L, Value *ExitCond, bool ExitIfTrue,
    bool ControlsExit, bool AllowPredicates) {
  // Check if the controlling expression for this loop is an And or Or.
  if (BinaryOperator *BO = dyn_cast<BinaryOperator>(ExitCond)) {
    if (BO->getOpcode() == Instruction::And) {
      // Recurse on the operands of the and.
      bool EitherMayExit = !ExitIfTrue;
      ExitLimit EL0 = computeExitLimitFromCondCached(
          Cache, L, BO->getOperand(0), ExitIfTrue,
          ControlsExit && !EitherMayExit, AllowPredicates);
      ExitLimit EL1 = computeExitLimitFromCondCached(
          Cache, L, BO->getOperand(1), ExitIfTrue,
          ControlsExit && !EitherMayExit, AllowPredicates);
      const SCEV *BECount = getCouldNotCompute();
      const SCEV *MaxBECount = getCouldNotCompute();
      if (EitherMayExit) {
        // Both conditions must be true for the loop to continue executing.
        // Choose the less conservative count.
        if (EL0.ExactNotTaken == getCouldNotCompute() ||
            EL1.ExactNotTaken == getCouldNotCompute())
          BECount = getCouldNotCompute();
        else
          BECount =
              getUMinFromMismatchedTypes(EL0.ExactNotTaken, EL1.ExactNotTaken);
        if (EL0.MaxNotTaken == getCouldNotCompute())
          MaxBECount = EL1.MaxNotTaken;
        else if (EL1.MaxNotTaken == getCouldNotCompute())
          MaxBECount = EL0.MaxNotTaken;
        else
          MaxBECount =
              getUMinFromMismatchedTypes(EL0.MaxNotTaken, EL1.MaxNotTaken);
      } else {
        // Both conditions must be true at the same time for the loop to exit.
        // For now, be conservative.
        if (EL0.MaxNotTaken == EL1.MaxNotTaken)
          MaxBECount = EL0.MaxNotTaken;
        if (EL0.ExactNotTaken == EL1.ExactNotTaken)
          BECount = EL0.ExactNotTaken;
      }

      // There are cases (e.g. PR26207) where computeExitLimitFromCond is able
      // to be more aggressive when computing BECount than when computing
      // MaxBECount.  In these cases it is possible for EL0.ExactNotTaken and
      // EL1.ExactNotTaken to match, but for EL0.MaxNotTaken and EL1.MaxNotTaken
      // to not.
      if (isa<SCEVCouldNotCompute>(MaxBECount) &&
          !isa<SCEVCouldNotCompute>(BECount))
        MaxBECount = getConstant(getUnsignedRangeMax(BECount));

      return ExitLimit(BECount, MaxBECount, false,
                       {&EL0.Predicates, &EL1.Predicates});
    }
    if (BO->getOpcode() == Instruction::Or) {
      // Recurse on the operands of the or.
      bool EitherMayExit = ExitIfTrue;
      ExitLimit EL0 = computeExitLimitFromCondCached(
          Cache, L, BO->getOperand(0), ExitIfTrue,
          ControlsExit && !EitherMayExit, AllowPredicates);
      ExitLimit EL1 = computeExitLimitFromCondCached(
          Cache, L, BO->getOperand(1), ExitIfTrue,
          ControlsExit && !EitherMayExit, AllowPredicates);
      const SCEV *BECount = getCouldNotCompute();
      const SCEV *MaxBECount = getCouldNotCompute();
      if (EitherMayExit) {
        // Both conditions must be false for the loop to continue executing.
        // Choose the less conservative count.
        if (EL0.ExactNotTaken == getCouldNotCompute() ||
            EL1.ExactNotTaken == getCouldNotCompute())
          BECount = getCouldNotCompute();
        else
          BECount =
              getUMinFromMismatchedTypes(EL0.ExactNotTaken, EL1.ExactNotTaken);
        if (EL0.MaxNotTaken == getCouldNotCompute())
          MaxBECount = EL1.MaxNotTaken;
        else if (EL1.MaxNotTaken == getCouldNotCompute())
          MaxBECount = EL0.MaxNotTaken;
        else
          MaxBECount =
              getUMinFromMismatchedTypes(EL0.MaxNotTaken, EL1.MaxNotTaken);
      } else {
        // Both conditions must be false at the same time for the loop to exit.
        // For now, be conservative.
        if (EL0.MaxNotTaken == EL1.MaxNotTaken)
          MaxBECount = EL0.MaxNotTaken;
        if (EL0.ExactNotTaken == EL1.ExactNotTaken)
          BECount = EL0.ExactNotTaken;
      }

      return ExitLimit(BECount, MaxBECount, false,
                       {&EL0.Predicates, &EL1.Predicates});
    }
  }

  // With an icmp, it may be feasible to compute an exact backedge-taken count.
  // Proceed to the next level to examine the icmp.
  if (ICmpInst *ExitCondICmp = dyn_cast<ICmpInst>(ExitCond)) {
    ExitLimit EL =
        computeExitLimitFromICmp(L, ExitCondICmp, ExitIfTrue, ControlsExit);
    if (EL.hasFullInfo() || !AllowPredicates)
      return EL;

    // Try again, but use SCEV predicates this time.
    return computeExitLimitFromICmp(L, ExitCondICmp, ExitIfTrue, ControlsExit,
                                    /*AllowPredicates=*/true);
  }

  // Check for a constant condition. These are normally stripped out by
  // SimplifyCFG, but ScalarEvolution may be used by a pass which wishes to
  // preserve the CFG and is temporarily leaving constant conditions
  // in place.
  if (ConstantInt *CI = dyn_cast<ConstantInt>(ExitCond)) {
    if (ExitIfTrue == !CI->getZExtValue())
      // The backedge is always taken.
      return getCouldNotCompute();
    else
      // The backedge is never taken.
      return getZero(CI->getType());
  }

  // If it's not an integer or pointer comparison then compute it the hard way.
  return computeExitCountExhaustively(L, ExitCond, ExitIfTrue);
}

ScalarEvolution::ExitLimit
ScalarEvolution::computeExitLimitFromICmp(const Loop *L,
                                          ICmpInst *ExitCond,
                                          bool ExitIfTrue,
                                          bool ControlsExit,
                                          bool AllowPredicates) {
  // If the condition was exit on true, convert the condition to exit on false
  ICmpInst::Predicate Pred;
  if (!ExitIfTrue)
    Pred = ExitCond->getPredicate();
  else
    Pred = ExitCond->getInversePredicate();
  const ICmpInst::Predicate OriginalPred = Pred;

  // Handle common loops like: for (X = "string"; *X; ++X)
  if (LoadInst *LI = dyn_cast<LoadInst>(ExitCond->getOperand(0)))
    if (Constant *RHS = dyn_cast<Constant>(ExitCond->getOperand(1))) {
      ExitLimit ItCnt =
        computeLoadConstantCompareExitLimit(LI, RHS, L, Pred);
      if (ItCnt.hasAnyInfo())
        return ItCnt;
    }

  const SCEV *LHS = getSCEV(ExitCond->getOperand(0));
  const SCEV *RHS = getSCEV(ExitCond->getOperand(1));

  // Try to evaluate any dependencies out of the loop.
  LHS = getSCEVAtScope(LHS, L);
  RHS = getSCEVAtScope(RHS, L);

  // At this point, we would like to compute how many iterations of the
  // loop the predicate will return true for these inputs.
  if (isLoopInvariant(LHS, L) && !isLoopInvariant(RHS, L)) {
    // If there is a loop-invariant, force it into the RHS.
    std::swap(LHS, RHS);
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }

  // Simplify the operands before analyzing them.
  (void)SimplifyICmpOperands(Pred, LHS, RHS);

  // If we have a comparison of a chrec against a constant, try to use value
  // ranges to answer this query.
  if (const SCEVConstant *RHSC = dyn_cast<SCEVConstant>(RHS))
    if (const SCEVAddRecExpr *AddRec = dyn_cast<SCEVAddRecExpr>(LHS))
      if (AddRec->getLoop() == L) {
        // Form the constant range.
        ConstantRange CompRange =
            ConstantRange::makeExactICmpRegion(Pred, RHSC->getAPInt());

        const SCEV *Ret = AddRec->getNumIterationsInRange(CompRange, *this);
        if (!isa<SCEVCouldNotCompute>(Ret)) return Ret;
      }

  switch (Pred) {
  case ICmpInst::ICMP_NE: {                     // while (X != Y)
    // Convert to: while (X-Y != 0)
    ExitLimit EL = howFarToZero(getMinusSCEV(LHS, RHS), L, ControlsExit,
                                AllowPredicates);
    if (EL.hasAnyInfo()) return EL;
    break;
  }
  case ICmpInst::ICMP_EQ: {                     // while (X == Y)
    // Convert to: while (X-Y == 0)
    ExitLimit EL = howFarToNonZero(getMinusSCEV(LHS, RHS), L);
    if (EL.hasAnyInfo()) return EL;
    break;
  }
  case ICmpInst::ICMP_SLT:
  case ICmpInst::ICMP_ULT: {                    // while (X < Y)
    bool IsSigned = Pred == ICmpInst::ICMP_SLT;
    ExitLimit EL = howManyLessThans(LHS, RHS, L, IsSigned, ControlsExit,
                                    AllowPredicates);
    if (EL.hasAnyInfo()) return EL;
    break;
  }
  case ICmpInst::ICMP_SGT:
  case ICmpInst::ICMP_UGT: {                    // while (X > Y)
    bool IsSigned = Pred == ICmpInst::ICMP_SGT;
    ExitLimit EL =
        howManyGreaterThans(LHS, RHS, L, IsSigned, ControlsExit,
                            AllowPredicates);
    if (EL.hasAnyInfo()) return EL;
    break;
  }
  default:
    break;
  }

  auto *ExhaustiveCount =
      computeExitCountExhaustively(L, ExitCond, ExitIfTrue);

  if (!isa<SCEVCouldNotCompute>(ExhaustiveCount))
    return ExhaustiveCount;

  return computeShiftCompareExitLimit(ExitCond->getOperand(0),
                                      ExitCond->getOperand(1), L, OriginalPred);
}

ScalarEvolution::ExitLimit
ScalarEvolution::computeExitLimitFromSingleExitSwitch(const Loop *L,
                                                      SwitchInst *Switch,
                                                      BasicBlock *ExitingBlock,
                                                      bool ControlsExit) {
  assert(!L->contains(ExitingBlock) && "Not an exiting block!");

  // Give up if the exit is the default dest of a switch.
  if (Switch->getDefaultDest() == ExitingBlock)
    return getCouldNotCompute();

  assert(L->contains(Switch->getDefaultDest()) &&
         "Default case must not exit the loop!");
  const SCEV *LHS = getSCEVAtScope(Switch->getCondition(), L);
  const SCEV *RHS = getConstant(Switch->findCaseDest(ExitingBlock));

  // while (X != Y) --> while (X-Y != 0)
  ExitLimit EL = howFarToZero(getMinusSCEV(LHS, RHS), L, ControlsExit);
  if (EL.hasAnyInfo())
    return EL;

  return getCouldNotCompute();
}

static ConstantInt *
EvaluateConstantChrecAtConstant(const SCEVAddRecExpr *AddRec, ConstantInt *C,
                                ScalarEvolution &SE) {
  const SCEV *InVal = SE.getConstant(C);
  const SCEV *Val = AddRec->evaluateAtIteration(InVal, SE);
  assert(isa<SCEVConstant>(Val) &&
         "Evaluation of SCEV at constant didn't fold correctly?");
  return cast<SCEVConstant>(Val)->getValue();
}

/// Given an exit condition of 'icmp op load X, cst', try to see if we can
/// compute the backedge execution count.
ScalarEvolution::ExitLimit
ScalarEvolution::computeLoadConstantCompareExitLimit(
  LoadInst *LI,
  Constant *RHS,
  const Loop *L,
  ICmpInst::Predicate predicate) {
  if (LI->isVolatile()) return getCouldNotCompute();

  // Check to see if the loaded pointer is a getelementptr of a global.
  // TODO: Use SCEV instead of manually grubbing with GEPs.
  GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(LI->getOperand(0));
  if (!GEP) return getCouldNotCompute();

  // Make sure that it is really a constant global we are gepping, with an
  // initializer, and make sure the first IDX is really 0.
  GlobalVariable *GV = dyn_cast<GlobalVariable>(GEP->getOperand(0));
  if (!GV || !GV->isConstant() || !GV->hasDefinitiveInitializer() ||
      GEP->getNumOperands() < 3 || !isa<Constant>(GEP->getOperand(1)) ||
      !cast<Constant>(GEP->getOperand(1))->isNullValue())
    return getCouldNotCompute();

  // Okay, we allow one non-constant index into the GEP instruction.
  Value *VarIdx = nullptr;
  std::vector<Constant*> Indexes;
  unsigned VarIdxNum = 0;
  for (unsigned i = 2, e = GEP->getNumOperands(); i != e; ++i)
    if (ConstantInt *CI = dyn_cast<ConstantInt>(GEP->getOperand(i))) {
      Indexes.push_back(CI);
    } else if (!isa<ConstantInt>(GEP->getOperand(i))) {
      if (VarIdx) return getCouldNotCompute();  // Multiple non-constant idx's.
      VarIdx = GEP->getOperand(i);
      VarIdxNum = i-2;
      Indexes.push_back(nullptr);
    }

  // Loop-invariant loads may be a byproduct of loop optimization. Skip them.
  if (!VarIdx)
    return getCouldNotCompute();

  // Okay, we know we have a (load (gep GV, 0, X)) comparison with a constant.
  // Check to see if X is a loop variant variable value now.
  const SCEV *Idx = getSCEV(VarIdx);
  Idx = getSCEVAtScope(Idx, L);

  // We can only recognize very limited forms of loop index expressions, in
  // particular, only affine AddRec's like {C1,+,C2}.
  const SCEVAddRecExpr *IdxExpr = dyn_cast<SCEVAddRecExpr>(Idx);
  if (!IdxExpr || !IdxExpr->isAffine() || isLoopInvariant(IdxExpr, L) ||
      !isa<SCEVConstant>(IdxExpr->getOperand(0)) ||
      !isa<SCEVConstant>(IdxExpr->getOperand(1)))
    return getCouldNotCompute();

  unsigned MaxSteps = MaxBruteForceIterations;
  for (unsigned IterationNum = 0; IterationNum != MaxSteps; ++IterationNum) {
    ConstantInt *ItCst = ConstantInt::get(
                           cast<IntegerType>(IdxExpr->getType()), IterationNum);
    ConstantInt *Val = EvaluateConstantChrecAtConstant(IdxExpr, ItCst, *this);

    // Form the GEP offset.
    Indexes[VarIdxNum] = Val;

    Constant *Result = ConstantFoldLoadThroughGEPIndices(GV->getInitializer(),
                                                         Indexes);
    if (!Result) break;  // Cannot compute!

    // Evaluate the condition for this iteration.
    Result = ConstantExpr::getICmp(predicate, Result, RHS);
    if (!isa<ConstantInt>(Result)) break;  // Couldn't decide for sure
    if (cast<ConstantInt>(Result)->getValue().isMinValue()) {
      ++NumArrayLenItCounts;
      return getConstant(ItCst);   // Found terminating iteration!
    }
  }
  return getCouldNotCompute();
}

ScalarEvolution::ExitLimit ScalarEvolution::computeShiftCompareExitLimit(
    Value *LHS, Value *RHSV, const Loop *L, ICmpInst::Predicate Pred) {
  ConstantInt *RHS = dyn_cast<ConstantInt>(RHSV);
  if (!RHS)
    return getCouldNotCompute();

  const BasicBlock *Latch = L->getLoopLatch();
  if (!Latch)
    return getCouldNotCompute();

  const BasicBlock *Predecessor = L->getLoopPredecessor();
  if (!Predecessor)
    return getCouldNotCompute();

  // Return true if V is of the form "LHS `shift_op` <positive constant>".
  // Return LHS in OutLHS and shift_opt in OutOpCode.
  auto MatchPositiveShift =
      [](Value *V, Value *&OutLHS, Instruction::BinaryOps &OutOpCode) {

    using namespace PatternMatch;

    ConstantInt *ShiftAmt;
    if (match(V, m_LShr(m_Value(OutLHS), m_ConstantInt(ShiftAmt))))
      OutOpCode = Instruction::LShr;
    else if (match(V, m_AShr(m_Value(OutLHS), m_ConstantInt(ShiftAmt))))
      OutOpCode = Instruction::AShr;
    else if (match(V, m_Shl(m_Value(OutLHS), m_ConstantInt(ShiftAmt))))
      OutOpCode = Instruction::Shl;
    else
      return false;

    return ShiftAmt->getValue().isStrictlyPositive();
  };

  // Recognize a "shift recurrence" either of the form %iv or of %iv.shifted in
  //
  // loop:
  //   %iv = phi i32 [ %iv.shifted, %loop ], [ %val, %preheader ]
  //   %iv.shifted = lshr i32 %iv, <positive constant>
  //
  // Return true on a successful match.  Return the corresponding PHI node (%iv
  // above) in PNOut and the opcode of the shift operation in OpCodeOut.
  auto MatchShiftRecurrence =
      [&](Value *V, PHINode *&PNOut, Instruction::BinaryOps &OpCodeOut) {
    Optional<Instruction::BinaryOps> PostShiftOpCode;

    {
      Instruction::BinaryOps OpC;
      Value *V;

      // If we encounter a shift instruction, "peel off" the shift operation,
      // and remember that we did so.  Later when we inspect %iv's backedge
      // value, we will make sure that the backedge value uses the same
      // operation.
      //
      // Note: the peeled shift operation does not have to be the same
      // instruction as the one feeding into the PHI's backedge value.  We only
      // really care about it being the same *kind* of shift instruction --
      // that's all that is required for our later inferences to hold.
      if (MatchPositiveShift(LHS, V, OpC)) {
        PostShiftOpCode = OpC;
        LHS = V;
      }
    }

    PNOut = dyn_cast<PHINode>(LHS);
    if (!PNOut || PNOut->getParent() != L->getHeader())
      return false;

    Value *BEValue = PNOut->getIncomingValueForBlock(Latch);
    Value *OpLHS;

    return
        // The backedge value for the PHI node must be a shift by a positive
        // amount
        MatchPositiveShift(BEValue, OpLHS, OpCodeOut) &&

        // of the PHI node itself
        OpLHS == PNOut &&

        // and the kind of shift should be match the kind of shift we peeled
        // off, if any.
        (!PostShiftOpCode.hasValue() || *PostShiftOpCode == OpCodeOut);
  };

  PHINode *PN;
  Instruction::BinaryOps OpCode;
  if (!MatchShiftRecurrence(LHS, PN, OpCode))
    return getCouldNotCompute();

  const DataLayout &DL = getDataLayout();

  // The key rationale for this optimization is that for some kinds of shift
  // recurrences, the value of the recurrence "stabilizes" to either 0 or -1
  // within a finite number of iterations.  If the condition guarding the
  // backedge (in the sense that the backedge is taken if the condition is true)
  // is false for the value the shift recurrence stabilizes to, then we know
  // that the backedge is taken only a finite number of times.

  ConstantInt *StableValue = nullptr;
  switch (OpCode) {
  default:
    llvm_unreachable("Impossible case!");

  case Instruction::AShr: {
    // {K,ashr,<positive-constant>} stabilizes to signum(K) in at most
    // bitwidth(K) iterations.
    Value *FirstValue = PN->getIncomingValueForBlock(Predecessor);
    KnownBits Known = computeKnownBits(FirstValue, DL, 0, nullptr,
                                       Predecessor->getTerminator(), &DT);
    auto *Ty = cast<IntegerType>(RHS->getType());
    if (Known.isNonNegative())
      StableValue = ConstantInt::get(Ty, 0);
    else if (Known.isNegative())
      StableValue = ConstantInt::get(Ty, -1, true);
    else
      return getCouldNotCompute();

    break;
  }
  case Instruction::LShr:
  case Instruction::Shl:
    // Both {K,lshr,<positive-constant>} and {K,shl,<positive-constant>}
    // stabilize to 0 in at most bitwidth(K) iterations.
    StableValue = ConstantInt::get(cast<IntegerType>(RHS->getType()), 0);
    break;
  }

  auto *Result =
      ConstantFoldCompareInstOperands(Pred, StableValue, RHS, DL, &TLI);
  assert(Result->getType()->isIntegerTy(1) &&
         "Otherwise cannot be an operand to a branch instruction");

  if (Result->isZeroValue()) {
    unsigned BitWidth = getTypeSizeInBits(RHS->getType());
    const SCEV *UpperBound =
        getConstant(getEffectiveSCEVType(RHS->getType()), BitWidth);
    return ExitLimit(getCouldNotCompute(), UpperBound, false);
  }

  return getCouldNotCompute();
}

/// Return true if we can constant fold an instruction of the specified type,
/// assuming that all operands were constants.
static bool CanConstantFold(const Instruction *I) {
  if (isa<BinaryOperator>(I) || isa<CmpInst>(I) ||
      isa<SelectInst>(I) || isa<CastInst>(I) || isa<GetElementPtrInst>(I) ||
      isa<LoadInst>(I))
    return true;

  if (const CallInst *CI = dyn_cast<CallInst>(I))
    if (const Function *F = CI->getCalledFunction())
      return canConstantFoldCallTo(CI, F);
  return false;
}

/// Determine whether this instruction can constant evolve within this loop
/// assuming its operands can all constant evolve.
static bool canConstantEvolve(Instruction *I, const Loop *L) {
  // An instruction outside of the loop can't be derived from a loop PHI.
  if (!L->contains(I)) return false;

  if (isa<PHINode>(I)) {
    // We don't currently keep track of the control flow needed to evaluate
    // PHIs, so we cannot handle PHIs inside of loops.
    return L->getHeader() == I->getParent();
  }

  // If we won't be able to constant fold this expression even if the operands
  // are constants, bail early.
  return CanConstantFold(I);
}

/// getConstantEvolvingPHIOperands - Implement getConstantEvolvingPHI by
/// recursing through each instruction operand until reaching a loop header phi.
static PHINode *
getConstantEvolvingPHIOperands(Instruction *UseInst, const Loop *L,
                               DenseMap<Instruction *, PHINode *> &PHIMap,
                               unsigned Depth) {
  if (Depth > MaxConstantEvolvingDepth)
    return nullptr;

  // Otherwise, we can evaluate this instruction if all of its operands are
  // constant or derived from a PHI node themselves.
  PHINode *PHI = nullptr;
  for (Value *Op : UseInst->operands()) {
    if (isa<Constant>(Op)) continue;

    Instruction *OpInst = dyn_cast<Instruction>(Op);
    if (!OpInst || !canConstantEvolve(OpInst, L)) return nullptr;

    PHINode *P = dyn_cast<PHINode>(OpInst);
    if (!P)
      // If this operand is already visited, reuse the prior result.
      // We may have P != PHI if this is the deepest point at which the
      // inconsistent paths meet.
      P = PHIMap.lookup(OpInst);
    if (!P) {
      // Recurse and memoize the results, whether a phi is found or not.
      // This recursive call invalidates pointers into PHIMap.
      P = getConstantEvolvingPHIOperands(OpInst, L, PHIMap, Depth + 1);
      PHIMap[OpInst] = P;
    }
    if (!P)
      return nullptr;  // Not evolving from PHI
    if (PHI && PHI != P)
      return nullptr;  // Evolving from multiple different PHIs.
    PHI = P;
  }
  // This is a expression evolving from a constant PHI!
  return PHI;
}

/// getConstantEvolvingPHI - Given an LLVM value and a loop, return a PHI node
/// in the loop that V is derived from.  We allow arbitrary operations along the
/// way, but the operands of an operation must either be constants or a value
/// derived from a constant PHI.  If this expression does not fit with these
/// constraints, return null.
static PHINode *getConstantEvolvingPHI(Value *V, const Loop *L) {
  Instruction *I = dyn_cast<Instruction>(V);
  if (!I || !canConstantEvolve(I, L)) return nullptr;

  if (PHINode *PN = dyn_cast<PHINode>(I))
    return PN;

  // Record non-constant instructions contained by the loop.
  DenseMap<Instruction *, PHINode *> PHIMap;
  return getConstantEvolvingPHIOperands(I, L, PHIMap, 0);
}

/// EvaluateExpression - Given an expression that passes the
/// getConstantEvolvingPHI predicate, evaluate its value assuming the PHI node
/// in the loop has the value PHIVal.  If we can't fold this expression for some
/// reason, return null.
static Constant *EvaluateExpression(Value *V, const Loop *L,
                                    DenseMap<Instruction *, Constant *> &Vals,
                                    const DataLayout &DL,
                                    const TargetLibraryInfo *TLI) {
  // Convenient constant check, but redundant for recursive calls.
  if (Constant *C = dyn_cast<Constant>(V)) return C;
  Instruction *I = dyn_cast<Instruction>(V);
  if (!I) return nullptr;

  if (Constant *C = Vals.lookup(I)) return C;

  // An instruction inside the loop depends on a value outside the loop that we
  // weren't given a mapping for, or a value such as a call inside the loop.
  if (!canConstantEvolve(I, L)) return nullptr;

  // An unmapped PHI can be due to a branch or another loop inside this loop,
  // or due to this not being the initial iteration through a loop where we
  // couldn't compute the evolution of this particular PHI last time.
  if (isa<PHINode>(I)) return nullptr;

  std::vector<Constant*> Operands(I->getNumOperands());

  for (unsigned i = 0, e = I->getNumOperands(); i != e; ++i) {
    Instruction *Operand = dyn_cast<Instruction>(I->getOperand(i));
    if (!Operand) {
      Operands[i] = dyn_cast<Constant>(I->getOperand(i));
      if (!Operands[i]) return nullptr;
      continue;
    }
    Constant *C = EvaluateExpression(Operand, L, Vals, DL, TLI);
    Vals[Operand] = C;
    if (!C) return nullptr;
    Operands[i] = C;
  }

  if (CmpInst *CI = dyn_cast<CmpInst>(I))
    return ConstantFoldCompareInstOperands(CI->getPredicate(), Operands[0],
                                           Operands[1], DL, TLI);
  if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
    if (!LI->isVolatile())
      return ConstantFoldLoadFromConstPtr(Operands[0], LI->getType(), DL);
  }
  return ConstantFoldInstOperands(I, Operands, DL, TLI);
}


// If every incoming value to PN except the one for BB is a specific Constant,
// return that, else return nullptr.
static Constant *getOtherIncomingValue(PHINode *PN, BasicBlock *BB) {
  Constant *IncomingVal = nullptr;

  for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
    if (PN->getIncomingBlock(i) == BB)
      continue;

    auto *CurrentVal = dyn_cast<Constant>(PN->getIncomingValue(i));
    if (!CurrentVal)
      return nullptr;

    if (IncomingVal != CurrentVal) {
      if (IncomingVal)
        return nullptr;
      IncomingVal = CurrentVal;
    }
  }

  return IncomingVal;
}

/// getConstantEvolutionLoopExitValue - If we know that the specified Phi is
/// in the header of its containing loop, we know the loop executes a
/// constant number of times, and the PHI node is just a recurrence
/// involving constants, fold it.
Constant *
ScalarEvolution::getConstantEvolutionLoopExitValue(PHINode *PN,
                                                   const APInt &BEs,
                                                   const Loop *L) {
  auto I = ConstantEvolutionLoopExitValue.find(PN);
  if (I != ConstantEvolutionLoopExitValue.end())
    return I->second;

  if (BEs.ugt(MaxBruteForceIterations))
    return ConstantEvolutionLoopExitValue[PN] = nullptr;  // Not going to evaluate it.

  Constant *&RetVal = ConstantEvolutionLoopExitValue[PN];

  DenseMap<Instruction *, Constant *> CurrentIterVals;
  BasicBlock *Header = L->getHeader();
  assert(PN->getParent() == Header && "Can't evaluate PHI not in loop header!");

  BasicBlock *Latch = L->getLoopLatch();
  if (!Latch)
    return nullptr;

  for (PHINode &PHI : Header->phis()) {
    if (auto *StartCST = getOtherIncomingValue(&PHI, Latch))
      CurrentIterVals[&PHI] = StartCST;
  }
  if (!CurrentIterVals.count(PN))
    return RetVal = nullptr;

  Value *BEValue = PN->getIncomingValueForBlock(Latch);

  // Execute the loop symbolically to determine the exit value.
  assert(BEs.getActiveBits() < CHAR_BIT * sizeof(unsigned) &&
         "BEs is <= MaxBruteForceIterations which is an 'unsigned'!");

  unsigned NumIterations = BEs.getZExtValue(); // must be in range
  unsigned IterationNum = 0;
  const DataLayout &DL = getDataLayout();
  for (; ; ++IterationNum) {
    if (IterationNum == NumIterations)
      return RetVal = CurrentIterVals[PN];  // Got exit value!

    // Compute the value of the PHIs for the next iteration.
    // EvaluateExpression adds non-phi values to the CurrentIterVals map.
    DenseMap<Instruction *, Constant *> NextIterVals;
    Constant *NextPHI =
        EvaluateExpression(BEValue, L, CurrentIterVals, DL, &TLI);
    if (!NextPHI)
      return nullptr;        // Couldn't evaluate!
    NextIterVals[PN] = NextPHI;

    bool StoppedEvolving = NextPHI == CurrentIterVals[PN];

    // Also evaluate the other PHI nodes.  However, we don't get to stop if we
    // cease to be able to evaluate one of them or if they stop evolving,
    // because that doesn't necessarily prevent us from computing PN.
    SmallVector<std::pair<PHINode *, Constant *>, 8> PHIsToCompute;
    for (const auto &I : CurrentIterVals) {
      PHINode *PHI = dyn_cast<PHINode>(I.first);
      if (!PHI || PHI == PN || PHI->getParent() != Header) continue;
      PHIsToCompute.emplace_back(PHI, I.second);
    }
    // We use two distinct loops because EvaluateExpression may invalidate any
    // iterators into CurrentIterVals.
    for (const auto &I : PHIsToCompute) {
      PHINode *PHI = I.first;
      Constant *&NextPHI = NextIterVals[PHI];
      if (!NextPHI) {   // Not already computed.
        Value *BEValue = PHI->getIncomingValueForBlock(Latch);
        NextPHI = EvaluateExpression(BEValue, L, CurrentIterVals, DL, &TLI);
      }
      if (NextPHI != I.second)
        StoppedEvolving = false;
    }

    // If all entries in CurrentIterVals == NextIterVals then we can stop
    // iterating, the loop can't continue to change.
    if (StoppedEvolving)
      return RetVal = CurrentIterVals[PN];

    CurrentIterVals.swap(NextIterVals);
  }
}

const SCEV *ScalarEvolution::computeExitCountExhaustively(const Loop *L,
                                                          Value *Cond,
                                                          bool ExitWhen) {
  PHINode *PN = getConstantEvolvingPHI(Cond, L);
  if (!PN) return getCouldNotCompute();

  // If the loop is canonicalized, the PHI will have exactly two entries.
  // That's the only form we support here.
  if (PN->getNumIncomingValues() != 2) return getCouldNotCompute();

  DenseMap<Instruction *, Constant *> CurrentIterVals;
  BasicBlock *Header = L->getHeader();
  assert(PN->getParent() == Header && "Can't evaluate PHI not in loop header!");

  BasicBlock *Latch = L->getLoopLatch();
  assert(Latch && "Should follow from NumIncomingValues == 2!");

  for (PHINode &PHI : Header->phis()) {
    if (auto *StartCST = getOtherIncomingValue(&PHI, Latch))
      CurrentIterVals[&PHI] = StartCST;
  }
  if (!CurrentIterVals.count(PN))
    return getCouldNotCompute();

  // Okay, we find a PHI node that defines the trip count of this loop.  Execute
  // the loop symbolically to determine when the condition gets a value of
  // "ExitWhen".
  unsigned MaxIterations = MaxBruteForceIterations;   // Limit analysis.
  const DataLayout &DL = getDataLayout();
  for (unsigned IterationNum = 0; IterationNum != MaxIterations;++IterationNum){
    auto *CondVal = dyn_cast_or_null<ConstantInt>(
        EvaluateExpression(Cond, L, CurrentIterVals, DL, &TLI));

    // Couldn't symbolically evaluate.
    if (!CondVal) return getCouldNotCompute();

    if (CondVal->getValue() == uint64_t(ExitWhen)) {
      ++NumBruteForceTripCountsComputed;
      return getConstant(Type::getInt32Ty(getContext()), IterationNum);
    }

    // Update all the PHI nodes for the next iteration.
    DenseMap<Instruction *, Constant *> NextIterVals;

    // Create a list of which PHIs we need to compute. We want to do this before
    // calling EvaluateExpression on them because that may invalidate iterators
    // into CurrentIterVals.
    SmallVector<PHINode *, 8> PHIsToCompute;
    for (const auto &I : CurrentIterVals) {
      PHINode *PHI = dyn_cast<PHINode>(I.first);
      if (!PHI || PHI->getParent() != Header) continue;
      PHIsToCompute.push_back(PHI);
    }
    for (PHINode *PHI : PHIsToCompute) {
      Constant *&NextPHI = NextIterVals[PHI];
      if (NextPHI) continue;    // Already computed!

      Value *BEValue = PHI->getIncomingValueForBlock(Latch);
      NextPHI = EvaluateExpression(BEValue, L, CurrentIterVals, DL, &TLI);
    }
    CurrentIterVals.swap(NextIterVals);
  }

  // Too many iterations were needed to evaluate.
  return getCouldNotCompute();
}

const SCEV *ScalarEvolution::getSCEVAtScope(const SCEV *V, const Loop *L) {
  SmallVector<std::pair<const Loop *, const SCEV *>, 2> &Values =
      ValuesAtScopes[V];
  // Check to see if we've folded this expression at this loop before.
  for (auto &LS : Values)
    if (LS.first == L)
      return LS.second ? LS.second : V;

  Values.emplace_back(L, nullptr);

  // Otherwise compute it.
  const SCEV *C = computeSCEVAtScope(V, L);
  for (auto &LS : reverse(ValuesAtScopes[V]))
    if (LS.first == L) {
      LS.second = C;
      break;
    }
  return C;
}

/// This builds up a Constant using the ConstantExpr interface.  That way, we
/// will return Constants for objects which aren't represented by a
/// SCEVConstant, because SCEVConstant is restricted to ConstantInt.
/// Returns NULL if the SCEV isn't representable as a Constant.
static Constant *BuildConstantFromSCEV(const SCEV *V) {
  switch (static_cast<SCEVTypes>(V->getSCEVType())) {
    case scCouldNotCompute:
    case scAddRecExpr:
      break;
    case scConstant:
      return cast<SCEVConstant>(V)->getValue();
    case scUnknown:
      return dyn_cast<Constant>(cast<SCEVUnknown>(V)->getValue());
    case scSignExtend: {
      const SCEVSignExtendExpr *SS = cast<SCEVSignExtendExpr>(V);
      if (Constant *CastOp = BuildConstantFromSCEV(SS->getOperand()))
        return ConstantExpr::getSExt(CastOp, SS->getType());
      break;
    }
    case scZeroExtend: {
      const SCEVZeroExtendExpr *SZ = cast<SCEVZeroExtendExpr>(V);
      if (Constant *CastOp = BuildConstantFromSCEV(SZ->getOperand()))
        return ConstantExpr::getZExt(CastOp, SZ->getType());
      break;
    }
    case scTruncate: {
      const SCEVTruncateExpr *ST = cast<SCEVTruncateExpr>(V);
      if (Constant *CastOp = BuildConstantFromSCEV(ST->getOperand()))
        return ConstantExpr::getTrunc(CastOp, ST->getType());
      break;
    }
    case scAddExpr: {
      const SCEVAddExpr *SA = cast<SCEVAddExpr>(V);
      if (Constant *C = BuildConstantFromSCEV(SA->getOperand(0))) {
        if (PointerType *PTy = dyn_cast<PointerType>(C->getType())) {
          unsigned AS = PTy->getAddressSpace();
          Type *DestPtrTy = Type::getInt8PtrTy(C->getContext(), AS);
          C = ConstantExpr::getBitCast(C, DestPtrTy);
        }
        for (unsigned i = 1, e = SA->getNumOperands(); i != e; ++i) {
          Constant *C2 = BuildConstantFromSCEV(SA->getOperand(i));
          if (!C2) return nullptr;

          // First pointer!
          if (!C->getType()->isPointerTy() && C2->getType()->isPointerTy()) {
            unsigned AS = C2->getType()->getPointerAddressSpace();
            std::swap(C, C2);
            Type *DestPtrTy = Type::getInt8PtrTy(C->getContext(), AS);
            // The offsets have been converted to bytes.  We can add bytes to an
            // i8* by GEP with the byte count in the first index.
            C = ConstantExpr::getBitCast(C, DestPtrTy);
          }

          // Don't bother trying to sum two pointers. We probably can't
          // statically compute a load that results from it anyway.
          if (C2->getType()->isPointerTy())
            return nullptr;

          if (PointerType *PTy = dyn_cast<PointerType>(C->getType())) {
            if (PTy->getElementType()->isStructTy())
              C2 = ConstantExpr::getIntegerCast(
                  C2, Type::getInt32Ty(C->getContext()), true);
            C = ConstantExpr::getGetElementPtr(PTy->getElementType(), C, C2);
          } else
            C = ConstantExpr::getAdd(C, C2);
        }
        return C;
      }
      break;
    }
    case scMulExpr: {
      const SCEVMulExpr *SM = cast<SCEVMulExpr>(V);
      if (Constant *C = BuildConstantFromSCEV(SM->getOperand(0))) {
        // Don't bother with pointers at all.
        if (C->getType()->isPointerTy()) return nullptr;
        for (unsigned i = 1, e = SM->getNumOperands(); i != e; ++i) {
          Constant *C2 = BuildConstantFromSCEV(SM->getOperand(i));
          if (!C2 || C2->getType()->isPointerTy()) return nullptr;
          C = ConstantExpr::getMul(C, C2);
        }
        return C;
      }
      break;
    }
    case scUDivExpr: {
      const SCEVUDivExpr *SU = cast<SCEVUDivExpr>(V);
      if (Constant *LHS = BuildConstantFromSCEV(SU->getLHS()))
        if (Constant *RHS = BuildConstantFromSCEV(SU->getRHS()))
          if (LHS->getType() == RHS->getType())
            return ConstantExpr::getUDiv(LHS, RHS);
      break;
    }
    case scSMaxExpr:
    case scUMaxExpr:
      break; // TODO: smax, umax.
  }
  return nullptr;
}

const SCEV *ScalarEvolution::computeSCEVAtScope(const SCEV *V, const Loop *L) {
  if (isa<SCEVConstant>(V)) return V;

  // If this instruction is evolved from a constant-evolving PHI, compute the
  // exit value from the loop without using SCEVs.
  if (const SCEVUnknown *SU = dyn_cast<SCEVUnknown>(V)) {
    if (Instruction *I = dyn_cast<Instruction>(SU->getValue())) {
      const Loop *LI = this->LI[I->getParent()];
      if (LI && LI->getParentLoop() == L)  // Looking for loop exit value.
        if (PHINode *PN = dyn_cast<PHINode>(I))
          if (PN->getParent() == LI->getHeader()) {
            // Okay, there is no closed form solution for the PHI node.  Check
            // to see if the loop that contains it has a known backedge-taken
            // count.  If so, we may be able to force computation of the exit
            // value.
            const SCEV *BackedgeTakenCount = getBackedgeTakenCount(LI);
            if (const SCEVConstant *BTCC =
                  dyn_cast<SCEVConstant>(BackedgeTakenCount)) {

              // This trivial case can show up in some degenerate cases where
              // the incoming IR has not yet been fully simplified.
              if (BTCC->getValue()->isZero()) {
                Value *InitValue = nullptr;
                bool MultipleInitValues = false;
                for (unsigned i = 0; i < PN->getNumIncomingValues(); i++) {
                  if (!LI->contains(PN->getIncomingBlock(i))) {
                    if (!InitValue)
                      InitValue = PN->getIncomingValue(i);
                    else if (InitValue != PN->getIncomingValue(i)) {
                      MultipleInitValues = true;
                      break;
                    }
                  }
                  if (!MultipleInitValues && InitValue)
                    return getSCEV(InitValue);
                }
              }
              // Okay, we know how many times the containing loop executes.  If
              // this is a constant evolving PHI node, get the final value at
              // the specified iteration number.
              Constant *RV =
                  getConstantEvolutionLoopExitValue(PN, BTCC->getAPInt(), LI);
              if (RV) return getSCEV(RV);
            }
          }

      // Okay, this is an expression that we cannot symbolically evaluate
      // into a SCEV.  Check to see if it's possible to symbolically evaluate
      // the arguments into constants, and if so, try to constant propagate the
      // result.  This is particularly useful for computing loop exit values.
      if (CanConstantFold(I)) {
        SmallVector<Constant *, 4> Operands;
        bool MadeImprovement = false;
        for (Value *Op : I->operands()) {
          if (Constant *C = dyn_cast<Constant>(Op)) {
            Operands.push_back(C);
            continue;
          }

          // If any of the operands is non-constant and if they are
          // non-integer and non-pointer, don't even try to analyze them
          // with scev techniques.
          if (!isSCEVable(Op->getType()))
            return V;

          const SCEV *OrigV = getSCEV(Op);
          const SCEV *OpV = getSCEVAtScope(OrigV, L);
          MadeImprovement |= OrigV != OpV;

          Constant *C = BuildConstantFromSCEV(OpV);
          if (!C) return V;
          if (C->getType() != Op->getType())
            C = ConstantExpr::getCast(CastInst::getCastOpcode(C, false,
                                                              Op->getType(),
                                                              false),
                                      C, Op->getType());
          Operands.push_back(C);
        }

        // Check to see if getSCEVAtScope actually made an improvement.
        if (MadeImprovement) {
          Constant *C = nullptr;
          const DataLayout &DL = getDataLayout();
          if (const CmpInst *CI = dyn_cast<CmpInst>(I))
            C = ConstantFoldCompareInstOperands(CI->getPredicate(), Operands[0],
                                                Operands[1], DL, &TLI);
          else if (const LoadInst *LI = dyn_cast<LoadInst>(I)) {
            if (!LI->isVolatile())
              C = ConstantFoldLoadFromConstPtr(Operands[0], LI->getType(), DL);
          } else
            C = ConstantFoldInstOperands(I, Operands, DL, &TLI);
          if (!C) return V;
          return getSCEV(C);
        }
      }
    }

    // This is some other type of SCEVUnknown, just return it.
    return V;
  }

  if (const SCEVCommutativeExpr *Comm = dyn_cast<SCEVCommutativeExpr>(V)) {
    // Avoid performing the look-up in the common case where the specified
    // expression has no loop-variant portions.
    for (unsigned i = 0, e = Comm->getNumOperands(); i != e; ++i) {
      const SCEV *OpAtScope = getSCEVAtScope(Comm->getOperand(i), L);
      if (OpAtScope != Comm->getOperand(i)) {
        // Okay, at least one of these operands is loop variant but might be
        // foldable.  Build a new instance of the folded commutative expression.
        SmallVector<const SCEV *, 8> NewOps(Comm->op_begin(),
                                            Comm->op_begin()+i);
        NewOps.push_back(OpAtScope);

        for (++i; i != e; ++i) {
          OpAtScope = getSCEVAtScope(Comm->getOperand(i), L);
          NewOps.push_back(OpAtScope);
        }
        if (isa<SCEVAddExpr>(Comm))
          return getAddExpr(NewOps);
        if (isa<SCEVMulExpr>(Comm))
          return getMulExpr(NewOps);
        if (isa<SCEVSMaxExpr>(Comm))
          return getSMaxExpr(NewOps);
        if (isa<SCEVUMaxExpr>(Comm))
          return getUMaxExpr(NewOps);
        llvm_unreachable("Unknown commutative SCEV type!");
      }
    }
    // If we got here, all operands are loop invariant.
    return Comm;
  }

  if (const SCEVUDivExpr *Div = dyn_cast<SCEVUDivExpr>(V)) {
    const SCEV *LHS = getSCEVAtScope(Div->getLHS(), L);
    const SCEV *RHS = getSCEVAtScope(Div->getRHS(), L);
    if (LHS == Div->getLHS() && RHS == Div->getRHS())
      return Div;   // must be loop invariant
    return getUDivExpr(LHS, RHS);
  }

  // If this is a loop recurrence for a loop that does not contain L, then we
  // are dealing with the final value computed by the loop.
  if (const SCEVAddRecExpr *AddRec = dyn_cast<SCEVAddRecExpr>(V)) {
    // First, attempt to evaluate each operand.
    // Avoid performing the look-up in the common case where the specified
    // expression has no loop-variant portions.
    for (unsigned i = 0, e = AddRec->getNumOperands(); i != e; ++i) {
      const SCEV *OpAtScope = getSCEVAtScope(AddRec->getOperand(i), L);
      if (OpAtScope == AddRec->getOperand(i))
        continue;

      // Okay, at least one of these operands is loop variant but might be
      // foldable.  Build a new instance of the folded commutative expression.
      SmallVector<const SCEV *, 8> NewOps(AddRec->op_begin(),
                                          AddRec->op_begin()+i);
      NewOps.push_back(OpAtScope);
      for (++i; i != e; ++i)
        NewOps.push_back(getSCEVAtScope(AddRec->getOperand(i), L));

      const SCEV *FoldedRec =
        getAddRecExpr(NewOps, AddRec->getLoop(),
                      AddRec->getNoWrapFlags(SCEV::FlagNW));
      AddRec = dyn_cast<SCEVAddRecExpr>(FoldedRec);
      // The addrec may be folded to a nonrecurrence, for example, if the
      // induction variable is multiplied by zero after constant folding. Go
      // ahead and return the folded value.
      if (!AddRec)
        return FoldedRec;
      break;
    }

    // If the scope is outside the addrec's loop, evaluate it by using the
    // loop exit value of the addrec.
    if (!AddRec->getLoop()->contains(L)) {
      // To evaluate this recurrence, we need to know how many times the AddRec
      // loop iterates.  Compute this now.
      const SCEV *BackedgeTakenCount = getBackedgeTakenCount(AddRec->getLoop());
      if (BackedgeTakenCount == getCouldNotCompute()) return AddRec;

      // Then, evaluate the AddRec.
      return AddRec->evaluateAtIteration(BackedgeTakenCount, *this);
    }

    return AddRec;
  }

  if (const SCEVZeroExtendExpr *Cast = dyn_cast<SCEVZeroExtendExpr>(V)) {
    const SCEV *Op = getSCEVAtScope(Cast->getOperand(), L);
    if (Op == Cast->getOperand())
      return Cast;  // must be loop invariant
    return getZeroExtendExpr(Op, Cast->getType());
  }

  if (const SCEVSignExtendExpr *Cast = dyn_cast<SCEVSignExtendExpr>(V)) {
    const SCEV *Op = getSCEVAtScope(Cast->getOperand(), L);
    if (Op == Cast->getOperand())
      return Cast;  // must be loop invariant
    return getSignExtendExpr(Op, Cast->getType());
  }

  if (const SCEVTruncateExpr *Cast = dyn_cast<SCEVTruncateExpr>(V)) {
    const SCEV *Op = getSCEVAtScope(Cast->getOperand(), L);
    if (Op == Cast->getOperand())
      return Cast;  // must be loop invariant
    return getTruncateExpr(Op, Cast->getType());
  }

  llvm_unreachable("Unknown SCEV type!");
}

const SCEV *ScalarEvolution::getSCEVAtScope(Value *V, const Loop *L) {
  return getSCEVAtScope(getSCEV(V), L);
}

const SCEV *ScalarEvolution::stripInjectiveFunctions(const SCEV *S) const {
  if (const SCEVZeroExtendExpr *ZExt = dyn_cast<SCEVZeroExtendExpr>(S))
    return stripInjectiveFunctions(ZExt->getOperand());
  if (const SCEVSignExtendExpr *SExt = dyn_cast<SCEVSignExtendExpr>(S))
    return stripInjectiveFunctions(SExt->getOperand());
  return S;
}

/// Finds the minimum unsigned root of the following equation:
///
///     A * X = B (mod N)
///
/// where N = 2^BW and BW is the common bit width of A and B. The signedness of
/// A and B isn't important.
///
/// If the equation does not have a solution, SCEVCouldNotCompute is returned.
static const SCEV *SolveLinEquationWithOverflow(const APInt &A, const SCEV *B,
                                               ScalarEvolution &SE) {
  uint32_t BW = A.getBitWidth();
  assert(BW == SE.getTypeSizeInBits(B->getType()));
  assert(A != 0 && "A must be non-zero.");

  // 1. D = gcd(A, N)
  //
  // The gcd of A and N may have only one prime factor: 2. The number of
  // trailing zeros in A is its multiplicity
  uint32_t Mult2 = A.countTrailingZeros();
  // D = 2^Mult2

  // 2. Check if B is divisible by D.
  //
  // B is divisible by D if and only if the multiplicity of prime factor 2 for B
  // is not less than multiplicity of this prime factor for D.
  if (SE.GetMinTrailingZeros(B) < Mult2)
    return SE.getCouldNotCompute();

  // 3. Compute I: the multiplicative inverse of (A / D) in arithmetic
  // modulo (N / D).
  //
  // If D == 1, (N / D) == N == 2^BW, so we need one extra bit to represent
  // (N / D) in general. The inverse itself always fits into BW bits, though,
  // so we immediately truncate it.
  APInt AD = A.lshr(Mult2).zext(BW + 1);  // AD = A / D
  APInt Mod(BW + 1, 0);
  Mod.setBit(BW - Mult2);  // Mod = N / D
  APInt I = AD.multiplicativeInverse(Mod).trunc(BW);

  // 4. Compute the minimum unsigned root of the equation:
  // I * (B / D) mod (N / D)
  // To simplify the computation, we factor out the divide by D:
  // (I * B mod N) / D
  const SCEV *D = SE.getConstant(APInt::getOneBitSet(BW, Mult2));
  return SE.getUDivExactExpr(SE.getMulExpr(B, SE.getConstant(I)), D);
}

/// For a given quadratic addrec, generate coefficients of the corresponding
/// quadratic equation, multiplied by a common value to ensure that they are
/// integers.
/// The returned value is a tuple { A, B, C, M, BitWidth }, where
/// Ax^2 + Bx + C is the quadratic function, M is the value that A, B and C
/// were multiplied by, and BitWidth is the bit width of the original addrec
/// coefficients.
/// This function returns None if the addrec coefficients are not compile-
/// time constants.
static Optional<std::tuple<APInt, APInt, APInt, APInt, unsigned>>
GetQuadraticEquation(const SCEVAddRecExpr *AddRec) {
  assert(AddRec->getNumOperands() == 3 && "This is not a quadratic chrec!");
  const SCEVConstant *LC = dyn_cast<SCEVConstant>(AddRec->getOperand(0));
  const SCEVConstant *MC = dyn_cast<SCEVConstant>(AddRec->getOperand(1));
  const SCEVConstant *NC = dyn_cast<SCEVConstant>(AddRec->getOperand(2));
  LLVM_DEBUG(dbgs() << __func__ << ": analyzing quadratic addrec: "
                    << *AddRec << '\n');

  // We currently can only solve this if the coefficients are constants.
  if (!LC || !MC || !NC) {
    LLVM_DEBUG(dbgs() << __func__ << ": coefficients are not constant\n");
    return None;
  }

  APInt L = LC->getAPInt();
  APInt M = MC->getAPInt();
  APInt N = NC->getAPInt();
  assert(!N.isNullValue() && "This is not a quadratic addrec");

  unsigned BitWidth = LC->getAPInt().getBitWidth();
  unsigned NewWidth = BitWidth + 1;
  LLVM_DEBUG(dbgs() << __func__ << ": addrec coeff bw: "
                    << BitWidth << '\n');
  // The sign-extension (as opposed to a zero-extension) here matches the
  // extension used in SolveQuadraticEquationWrap (with the same motivation).
  N = N.sext(NewWidth);
  M = M.sext(NewWidth);
  L = L.sext(NewWidth);

  // The increments are M, M+N, M+2N, ..., so the accumulated values are
  //   L+M, (L+M)+(M+N), (L+M)+(M+N)+(M+2N), ..., that is,
  //   L+M, L+2M+N, L+3M+3N, ...
  // After n iterations the accumulated value Acc is L + nM + n(n-1)/2 N.
  //
  // The equation Acc = 0 is then
  //   L + nM + n(n-1)/2 N = 0,  or  2L + 2M n + n(n-1) N = 0.
  // In a quadratic form it becomes:
  //   N n^2 + (2M-N) n + 2L = 0.

  APInt A = N;
  APInt B = 2 * M - A;
  APInt C = 2 * L;
  APInt T = APInt(NewWidth, 2);
  LLVM_DEBUG(dbgs() << __func__ << ": equation " << A << "x^2 + " << B
                    << "x + " << C << ", coeff bw: " << NewWidth
                    << ", multiplied by " << T << '\n');
  return std::make_tuple(A, B, C, T, BitWidth);
}

/// Helper function to compare optional APInts:
/// (a) if X and Y both exist, return min(X, Y),
/// (b) if neither X nor Y exist, return None,
/// (c) if exactly one of X and Y exists, return that value.
static Optional<APInt> MinOptional(Optional<APInt> X, Optional<APInt> Y) {
  if (X.hasValue() && Y.hasValue()) {
    unsigned W = std::max(X->getBitWidth(), Y->getBitWidth());
    APInt XW = X->sextOrSelf(W);
    APInt YW = Y->sextOrSelf(W);
    return XW.slt(YW) ? *X : *Y;
  }
  if (!X.hasValue() && !Y.hasValue())
    return None;
  return X.hasValue() ? *X : *Y;
}

/// Helper function to truncate an optional APInt to a given BitWidth.
/// When solving addrec-related equations, it is preferable to return a value
/// that has the same bit width as the original addrec's coefficients. If the
/// solution fits in the original bit width, truncate it (except for i1).
/// Returning a value of a different bit width may inhibit some optimizations.
///
/// In general, a solution to a quadratic equation generated from an addrec
/// may require BW+1 bits, where BW is the bit width of the addrec's
/// coefficients. The reason is that the coefficients of the quadratic
/// equation are BW+1 bits wide (to avoid truncation when converting from
/// the addrec to the equation).
static Optional<APInt> TruncIfPossible(Optional<APInt> X, unsigned BitWidth) {
  if (!X.hasValue())
    return None;
  unsigned W = X->getBitWidth();
  if (BitWidth > 1 && BitWidth < W && X->isIntN(BitWidth))
    return X->trunc(BitWidth);
  return X;
}

/// Let c(n) be the value of the quadratic chrec {L,+,M,+,N} after n
/// iterations. The values L, M, N are assumed to be signed, and they
/// should all have the same bit widths.
/// Find the least n >= 0 such that c(n) = 0 in the arithmetic modulo 2^BW,
/// where BW is the bit width of the addrec's coefficients.
/// If the calculated value is a BW-bit integer (for BW > 1), it will be
/// returned as such, otherwise the bit width of the returned value may
/// be greater than BW.
///
/// This function returns None if
/// (a) the addrec coefficients are not constant, or
/// (b) SolveQuadraticEquationWrap was unable to find a solution. For cases
///     like x^2 = 5, no integer solutions exist, in other cases an integer
///     solution may exist, but SolveQuadraticEquationWrap may fail to find it.
static Optional<APInt>
SolveQuadraticAddRecExact(const SCEVAddRecExpr *AddRec, ScalarEvolution &SE) {
  APInt A, B, C, M;
  unsigned BitWidth;
  auto T = GetQuadraticEquation(AddRec);
  if (!T.hasValue())
    return None;

  std::tie(A, B, C, M, BitWidth) = *T;
  LLVM_DEBUG(dbgs() << __func__ << ": solving for unsigned overflow\n");
  Optional<APInt> X = APIntOps::SolveQuadraticEquationWrap(A, B, C, BitWidth+1);
  if (!X.hasValue())
    return None;

  ConstantInt *CX = ConstantInt::get(SE.getContext(), *X);
  ConstantInt *V = EvaluateConstantChrecAtConstant(AddRec, CX, SE);
  if (!V->isZero())
    return None;

  return TruncIfPossible(X, BitWidth);
}

/// Let c(n) be the value of the quadratic chrec {0,+,M,+,N} after n
/// iterations. The values M, N are assumed to be signed, and they
/// should all have the same bit widths.
/// Find the least n such that c(n) does not belong to the given range,
/// while c(n-1) does.
///
/// This function returns None if
/// (a) the addrec coefficients are not constant, or
/// (b) SolveQuadraticEquationWrap was unable to find a solution for the
///     bounds of the range.
static Optional<APInt>
SolveQuadraticAddRecRange(const SCEVAddRecExpr *AddRec,
                          const ConstantRange &Range, ScalarEvolution &SE) {
  assert(AddRec->getOperand(0)->isZero() &&
         "Starting value of addrec should be 0");
  LLVM_DEBUG(dbgs() << __func__ << ": solving boundary crossing for range "
                    << Range << ", addrec " << *AddRec << '\n');
  // This case is handled in getNumIterationsInRange. Here we can assume that
  // we start in the range.
  assert(Range.contains(APInt(SE.getTypeSizeInBits(AddRec->getType()), 0)) &&
         "Addrec's initial value should be in range");

  APInt A, B, C, M;
  unsigned BitWidth;
  auto T = GetQuadraticEquation(AddRec);
  if (!T.hasValue())
    return None;

  // Be careful about the return value: there can be two reasons for not
  // returning an actual number. First, if no solutions to the equations
  // were found, and second, if the solutions don't leave the given range.
  // The first case means that the actual solution is "unknown", the second
  // means that it's known, but not valid. If the solution is unknown, we
  // cannot make any conclusions.
  // Return a pair: the optional solution and a flag indicating if the
  // solution was found.
  auto SolveForBoundary = [&](APInt Bound) -> std::pair<Optional<APInt>,bool> {
    // Solve for signed overflow and unsigned overflow, pick the lower
    // solution.
    LLVM_DEBUG(dbgs() << "SolveQuadraticAddRecRange: checking boundary "
                      << Bound << " (before multiplying by " << M << ")\n");
    Bound *= M; // The quadratic equation multiplier.

    Optional<APInt> SO = None;
    if (BitWidth > 1) {
      LLVM_DEBUG(dbgs() << "SolveQuadraticAddRecRange: solving for "
                           "signed overflow\n");
      SO = APIntOps::SolveQuadraticEquationWrap(A, B, -Bound, BitWidth);
    }
    LLVM_DEBUG(dbgs() << "SolveQuadraticAddRecRange: solving for "
                         "unsigned overflow\n");
    Optional<APInt> UO = APIntOps::SolveQuadraticEquationWrap(A, B, -Bound,
                                                              BitWidth+1);

    auto LeavesRange = [&] (const APInt &X) {
      ConstantInt *C0 = ConstantInt::get(SE.getContext(), X);
      ConstantInt *V0 = EvaluateConstantChrecAtConstant(AddRec, C0, SE);
      if (Range.contains(V0->getValue()))
        return false;
      // X should be at least 1, so X-1 is non-negative.
      ConstantInt *C1 = ConstantInt::get(SE.getContext(), X-1);
      ConstantInt *V1 = EvaluateConstantChrecAtConstant(AddRec, C1, SE);
      if (Range.contains(V1->getValue()))
        return true;
      return false;
    };

    // If SolveQuadraticEquationWrap returns None, it means that there can
    // be a solution, but the function failed to find it. We cannot treat it
    // as "no solution".
    if (!SO.hasValue() || !UO.hasValue())
      return { None, false };

    // Check the smaller value first to see if it leaves the range.
    // At this point, both SO and UO must have values.
    Optional<APInt> Min = MinOptional(SO, UO);
    if (LeavesRange(*Min))
      return { Min, true };
    Optional<APInt> Max = Min == SO ? UO : SO;
    if (LeavesRange(*Max))
      return { Max, true };

    // Solutions were found, but were eliminated, hence the "true".
    return { None, true };
  };

  std::tie(A, B, C, M, BitWidth) = *T;
  // Lower bound is inclusive, subtract 1 to represent the exiting value.
  APInt Lower = Range.getLower().sextOrSelf(A.getBitWidth()) - 1;
  APInt Upper = Range.getUpper().sextOrSelf(A.getBitWidth());
  auto SL = SolveForBoundary(Lower);
  auto SU = SolveForBoundary(Upper);
  // If any of the solutions was unknown, no meaninigful conclusions can
  // be made.
  if (!SL.second || !SU.second)
    return None;

  // Claim: The correct solution is not some value between Min and Max.
  //
  // Justification: Assuming that Min and Max are different values, one of
  // them is when the first signed overflow happens, the other is when the
  // first unsigned overflow happens. Crossing the range boundary is only
  // possible via an overflow (treating 0 as a special case of it, modeling
  // an overflow as crossing k*2^W for some k).
  //
  // The interesting case here is when Min was eliminated as an invalid
  // solution, but Max was not. The argument is that if there was another
  // overflow between Min and Max, it would also have been eliminated if
  // it was considered.
  //
  // For a given boundary, it is possible to have two overflows of the same
  // type (signed/unsigned) without having the other type in between: this
  // can happen when the vertex of the parabola is between the iterations
  // corresponding to the overflows. This is only possible when the two
  // overflows cross k*2^W for the same k. In such case, if the second one
  // left the range (and was the first one to do so), the first overflow
  // would have to enter the range, which would mean that either we had left
  // the range before or that we started outside of it. Both of these cases
  // are contradictions.
  //
  // Claim: In the case where SolveForBoundary returns None, the correct
  // solution is not some value between the Max for this boundary and the
  // Min of the other boundary.
  //
  // Justification: Assume that we had such Max_A and Min_B corresponding
  // to range boundaries A and B and such that Max_A < Min_B. If there was
  // a solution between Max_A and Min_B, it would have to be caused by an
  // overflow corresponding to either A or B. It cannot correspond to B,
  // since Min_B is the first occurrence of such an overflow. If it
  // corresponded to A, it would have to be either a signed or an unsigned
  // overflow that is larger than both eliminated overflows for A. But
  // between the eliminated overflows and this overflow, the values would
  // cover the entire value space, thus crossing the other boundary, which
  // is a contradiction.

  return TruncIfPossible(MinOptional(SL.first, SU.first), BitWidth);
}

ScalarEvolution::ExitLimit
ScalarEvolution::howFarToZero(const SCEV *V, const Loop *L, bool ControlsExit,
                              bool AllowPredicates) {

  // This is only used for loops with a "x != y" exit test. The exit condition
  // is now expressed as a single expression, V = x-y. So the exit test is
  // effectively V != 0.  We know and take advantage of the fact that this
  // expression only being used in a comparison by zero context.

  SmallPtrSet<const SCEVPredicate *, 4> Predicates;
  // If the value is a constant
  if (const SCEVConstant *C = dyn_cast<SCEVConstant>(V)) {
    // If the value is already zero, the branch will execute zero times.
    if (C->getValue()->isZero()) return C;
    return getCouldNotCompute();  // Otherwise it will loop infinitely.
  }

  const SCEVAddRecExpr *AddRec =
      dyn_cast<SCEVAddRecExpr>(stripInjectiveFunctions(V));

  if (!AddRec && AllowPredicates)
    // Try to make this an AddRec using runtime tests, in the first X
    // iterations of this loop, where X is the SCEV expression found by the
    // algorithm below.
    AddRec = convertSCEVToAddRecWithPredicates(V, L, Predicates);

  if (!AddRec || AddRec->getLoop() != L)
    return getCouldNotCompute();

  // If this is a quadratic (3-term) AddRec {L,+,M,+,N}, find the roots of
  // the quadratic equation to solve it.
  if (AddRec->isQuadratic() && AddRec->getType()->isIntegerTy()) {
    // We can only use this value if the chrec ends up with an exact zero
    // value at this index.  When solving for "X*X != 5", for example, we
    // should not accept a root of 2.
    if (auto S = SolveQuadraticAddRecExact(AddRec, *this)) {
      const auto *R = cast<SCEVConstant>(getConstant(S.getValue()));
      return ExitLimit(R, R, false, Predicates);
    }
    return getCouldNotCompute();
  }

  // Otherwise we can only handle this if it is affine.
  if (!AddRec->isAffine())
    return getCouldNotCompute();

  // If this is an affine expression, the execution count of this branch is
  // the minimum unsigned root of the following equation:
  //
  //     Start + Step*N = 0 (mod 2^BW)
  //
  // equivalent to:
  //
  //             Step*N = -Start (mod 2^BW)
  //
  // where BW is the common bit width of Start and Step.

  // Get the initial value for the loop.
  const SCEV *Start = getSCEVAtScope(AddRec->getStart(), L->getParentLoop());
  const SCEV *Step = getSCEVAtScope(AddRec->getOperand(1), L->getParentLoop());

  // For now we handle only constant steps.
  //
  // TODO: Handle a nonconstant Step given AddRec<NUW>. If the
  // AddRec is NUW, then (in an unsigned sense) it cannot be counting up to wrap
  // to 0, it must be counting down to equal 0. Consequently, N = Start / -Step.
  // We have not yet seen any such cases.
  const SCEVConstant *StepC = dyn_cast<SCEVConstant>(Step);
  if (!StepC || StepC->getValue()->isZero())
    return getCouldNotCompute();

  // For positive steps (counting up until unsigned overflow):
  //   N = -Start/Step (as unsigned)
  // For negative steps (counting down to zero):
  //   N = Start/-Step
  // First compute the unsigned distance from zero in the direction of Step.
  bool CountDown = StepC->getAPInt().isNegative();
  const SCEV *Distance = CountDown ? Start : getNegativeSCEV(Start);

  // Handle unitary steps, which cannot wraparound.
  // 1*N = -Start; -1*N = Start (mod 2^BW), so:
  //   N = Distance (as unsigned)
  if (StepC->getValue()->isOne() || StepC->getValue()->isMinusOne()) {
    APInt MaxBECount = getUnsignedRangeMax(Distance);

    // When a loop like "for (int i = 0; i != n; ++i) { /* body */ }" is rotated,
    // we end up with a loop whose backedge-taken count is n - 1.  Detect this
    // case, and see if we can improve the bound.
    //
    // Explicitly handling this here is necessary because getUnsignedRange
    // isn't context-sensitive; it doesn't know that we only care about the
    // range inside the loop.
    const SCEV *Zero = getZero(Distance->getType());
    const SCEV *One = getOne(Distance->getType());
    const SCEV *DistancePlusOne = getAddExpr(Distance, One);
    if (isLoopEntryGuardedByCond(L, ICmpInst::ICMP_NE, DistancePlusOne, Zero)) {
      // If Distance + 1 doesn't overflow, we can compute the maximum distance
      // as "unsigned_max(Distance + 1) - 1".
      ConstantRange CR = getUnsignedRange(DistancePlusOne);
      MaxBECount = APIntOps::umin(MaxBECount, CR.getUnsignedMax() - 1);
    }
    return ExitLimit(Distance, getConstant(MaxBECount), false, Predicates);
  }

  // If the condition controls loop exit (the loop exits only if the expression
  // is true) and the addition is no-wrap we can use unsigned divide to
  // compute the backedge count.  In this case, the step may not divide the
  // distance, but we don't care because if the condition is "missed" the loop
  // will have undefined behavior due to wrapping.
  if (ControlsExit && AddRec->hasNoSelfWrap() &&
      loopHasNoAbnormalExits(AddRec->getLoop())) {
    const SCEV *Exact =
        getUDivExpr(Distance, CountDown ? getNegativeSCEV(Step) : Step);
    const SCEV *Max =
        Exact == getCouldNotCompute()
            ? Exact
            : getConstant(getUnsignedRangeMax(Exact));
    return ExitLimit(Exact, Max, false, Predicates);
  }

  // Solve the general equation.
  const SCEV *E = SolveLinEquationWithOverflow(StepC->getAPInt(),
                                               getNegativeSCEV(Start), *this);
  const SCEV *M = E == getCouldNotCompute()
                      ? E
                      : getConstant(getUnsignedRangeMax(E));
  return ExitLimit(E, M, false, Predicates);
}

ScalarEvolution::ExitLimit
ScalarEvolution::howFarToNonZero(const SCEV *V, const Loop *L) {
  // Loops that look like: while (X == 0) are very strange indeed.  We don't
  // handle them yet except for the trivial case.  This could be expanded in the
  // future as needed.

  // If the value is a constant, check to see if it is known to be non-zero
  // already.  If so, the backedge will execute zero times.
  if (const SCEVConstant *C = dyn_cast<SCEVConstant>(V)) {
    if (!C->getValue()->isZero())
      return getZero(C->getType());
    return getCouldNotCompute();  // Otherwise it will loop infinitely.
  }

  // We could implement others, but I really doubt anyone writes loops like
  // this, and if they did, they would already be constant folded.
  return getCouldNotCompute();
}

std::pair<BasicBlock *, BasicBlock *>
ScalarEvolution::getPredecessorWithUniqueSuccessorForBB(BasicBlock *BB) {
  // If the block has a unique predecessor, then there is no path from the
  // predecessor to the block that does not go through the direct edge
  // from the predecessor to the block.
  if (BasicBlock *Pred = BB->getSinglePredecessor())
    return {Pred, BB};

  // A loop's header is defined to be a block that dominates the loop.
  // If the header has a unique predecessor outside the loop, it must be
  // a block that has exactly one successor that can reach the loop.
  if (Loop *L = LI.getLoopFor(BB))
    return {L->getLoopPredecessor(), L->getHeader()};

  return {nullptr, nullptr};
}

/// SCEV structural equivalence is usually sufficient for testing whether two
/// expressions are equal, however for the purposes of looking for a condition
/// guarding a loop, it can be useful to be a little more general, since a
/// front-end may have replicated the controlling expression.
static bool HasSameValue(const SCEV *A, const SCEV *B) {
  // Quick check to see if they are the same SCEV.
  if (A == B) return true;

  auto ComputesEqualValues = [](const Instruction *A, const Instruction *B) {
    // Not all instructions that are "identical" compute the same value.  For
    // instance, two distinct alloca instructions allocating the same type are
    // identical and do not read memory; but compute distinct values.
    return A->isIdenticalTo(B) && (isa<BinaryOperator>(A) || isa<GetElementPtrInst>(A));
  };

  // Otherwise, if they're both SCEVUnknown, it's possible that they hold
  // two different instructions with the same value. Check for this case.
  if (const SCEVUnknown *AU = dyn_cast<SCEVUnknown>(A))
    if (const SCEVUnknown *BU = dyn_cast<SCEVUnknown>(B))
      if (const Instruction *AI = dyn_cast<Instruction>(AU->getValue()))
        if (const Instruction *BI = dyn_cast<Instruction>(BU->getValue()))
          if (ComputesEqualValues(AI, BI))
            return true;

  // Otherwise assume they may have a different value.
  return false;
}

bool ScalarEvolution::SimplifyICmpOperands(ICmpInst::Predicate &Pred,
                                           const SCEV *&LHS, const SCEV *&RHS,
                                           unsigned Depth) {
  bool Changed = false;
  // Simplifies ICMP to trivial true or false by turning it into '0 == 0' or
  // '0 != 0'.
  auto TrivialCase = [&](bool TriviallyTrue) {
    LHS = RHS = getConstant(ConstantInt::getFalse(getContext()));
    Pred = TriviallyTrue ? ICmpInst::ICMP_EQ : ICmpInst::ICMP_NE;
    return true;
  };
  // If we hit the max recursion limit bail out.
  if (Depth >= 3)
    return false;

  // Canonicalize a constant to the right side.
  if (const SCEVConstant *LHSC = dyn_cast<SCEVConstant>(LHS)) {
    // Check for both operands constant.
    if (const SCEVConstant *RHSC = dyn_cast<SCEVConstant>(RHS)) {
      if (ConstantExpr::getICmp(Pred,
                                LHSC->getValue(),
                                RHSC->getValue())->isNullValue())
        return TrivialCase(false);
      else
        return TrivialCase(true);
    }
    // Otherwise swap the operands to put the constant on the right.
    std::swap(LHS, RHS);
    Pred = ICmpInst::getSwappedPredicate(Pred);
    Changed = true;
  }

  // If we're comparing an addrec with a value which is loop-invariant in the
  // addrec's loop, put the addrec on the left. Also make a dominance check,
  // as both operands could be addrecs loop-invariant in each other's loop.
  if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(RHS)) {
    const Loop *L = AR->getLoop();
    if (isLoopInvariant(LHS, L) && properlyDominates(LHS, L->getHeader())) {
      std::swap(LHS, RHS);
      Pred = ICmpInst::getSwappedPredicate(Pred);
      Changed = true;
    }
  }

  // If there's a constant operand, canonicalize comparisons with boundary
  // cases, and canonicalize *-or-equal comparisons to regular comparisons.
  if (const SCEVConstant *RC = dyn_cast<SCEVConstant>(RHS)) {
    const APInt &RA = RC->getAPInt();

    bool SimplifiedByConstantRange = false;

    if (!ICmpInst::isEquality(Pred)) {
      ConstantRange ExactCR = ConstantRange::makeExactICmpRegion(Pred, RA);
      if (ExactCR.isFullSet())
        return TrivialCase(true);
      else if (ExactCR.isEmptySet())
        return TrivialCase(false);

      APInt NewRHS;
      CmpInst::Predicate NewPred;
      if (ExactCR.getEquivalentICmp(NewPred, NewRHS) &&
          ICmpInst::isEquality(NewPred)) {
        // We were able to convert an inequality to an equality.
        Pred = NewPred;
        RHS = getConstant(NewRHS);
        Changed = SimplifiedByConstantRange = true;
      }
    }

    if (!SimplifiedByConstantRange) {
      switch (Pred) {
      default:
        break;
      case ICmpInst::ICMP_EQ:
      case ICmpInst::ICMP_NE:
        // Fold ((-1) * %a) + %b == 0 (equivalent to %b-%a == 0) into %a == %b.
        if (!RA)
          if (const SCEVAddExpr *AE = dyn_cast<SCEVAddExpr>(LHS))
            if (const SCEVMulExpr *ME =
                    dyn_cast<SCEVMulExpr>(AE->getOperand(0)))
              if (AE->getNumOperands() == 2 && ME->getNumOperands() == 2 &&
                  ME->getOperand(0)->isAllOnesValue()) {
                RHS = AE->getOperand(1);
                LHS = ME->getOperand(1);
                Changed = true;
              }
        break;


        // The "Should have been caught earlier!" messages refer to the fact
        // that the ExactCR.isFullSet() or ExactCR.isEmptySet() check above
        // should have fired on the corresponding cases, and canonicalized the
        // check to trivial case.

      case ICmpInst::ICMP_UGE:
        assert(!RA.isMinValue() && "Should have been caught earlier!");
        Pred = ICmpInst::ICMP_UGT;
        RHS = getConstant(RA - 1);
        Changed = true;
        break;
      case ICmpInst::ICMP_ULE:
        assert(!RA.isMaxValue() && "Should have been caught earlier!");
        Pred = ICmpInst::ICMP_ULT;
        RHS = getConstant(RA + 1);
        Changed = true;
        break;
      case ICmpInst::ICMP_SGE:
        assert(!RA.isMinSignedValue() && "Should have been caught earlier!");
        Pred = ICmpInst::ICMP_SGT;
        RHS = getConstant(RA - 1);
        Changed = true;
        break;
      case ICmpInst::ICMP_SLE:
        assert(!RA.isMaxSignedValue() && "Should have been caught earlier!");
        Pred = ICmpInst::ICMP_SLT;
        RHS = getConstant(RA + 1);
        Changed = true;
        break;
      }
    }
  }

  // Check for obvious equality.
  if (HasSameValue(LHS, RHS)) {
    if (ICmpInst::isTrueWhenEqual(Pred))
      return TrivialCase(true);
    if (ICmpInst::isFalseWhenEqual(Pred))
      return TrivialCase(false);
  }

  // If possible, canonicalize GE/LE comparisons to GT/LT comparisons, by
  // adding or subtracting 1 from one of the operands.
  switch (Pred) {
  case ICmpInst::ICMP_SLE:
    if (!getSignedRangeMax(RHS).isMaxSignedValue()) {
      RHS = getAddExpr(getConstant(RHS->getType(), 1, true), RHS,
                       SCEV::FlagNSW);
      Pred = ICmpInst::ICMP_SLT;
      Changed = true;
    } else if (!getSignedRangeMin(LHS).isMinSignedValue()) {
      LHS = getAddExpr(getConstant(RHS->getType(), (uint64_t)-1, true), LHS,
                       SCEV::FlagNSW);
      Pred = ICmpInst::ICMP_SLT;
      Changed = true;
    }
    break;
  case ICmpInst::ICMP_SGE:
    if (!getSignedRangeMin(RHS).isMinSignedValue()) {
      RHS = getAddExpr(getConstant(RHS->getType(), (uint64_t)-1, true), RHS,
                       SCEV::FlagNSW);
      Pred = ICmpInst::ICMP_SGT;
      Changed = true;
    } else if (!getSignedRangeMax(LHS).isMaxSignedValue()) {
      LHS = getAddExpr(getConstant(RHS->getType(), 1, true), LHS,
                       SCEV::FlagNSW);
      Pred = ICmpInst::ICMP_SGT;
      Changed = true;
    }
    break;
  case ICmpInst::ICMP_ULE:
    if (!getUnsignedRangeMax(RHS).isMaxValue()) {
      RHS = getAddExpr(getConstant(RHS->getType(), 1, true), RHS,
                       SCEV::FlagNUW);
      Pred = ICmpInst::ICMP_ULT;
      Changed = true;
    } else if (!getUnsignedRangeMin(LHS).isMinValue()) {
      LHS = getAddExpr(getConstant(RHS->getType(), (uint64_t)-1, true), LHS);
      Pred = ICmpInst::ICMP_ULT;
      Changed = true;
    }
    break;
  case ICmpInst::ICMP_UGE:
    if (!getUnsignedRangeMin(RHS).isMinValue()) {
      RHS = getAddExpr(getConstant(RHS->getType(), (uint64_t)-1, true), RHS);
      Pred = ICmpInst::ICMP_UGT;
      Changed = true;
    } else if (!getUnsignedRangeMax(LHS).isMaxValue()) {
      LHS = getAddExpr(getConstant(RHS->getType(), 1, true), LHS,
                       SCEV::FlagNUW);
      Pred = ICmpInst::ICMP_UGT;
      Changed = true;
    }
    break;
  default:
    break;
  }

  // TODO: More simplifications are possible here.

  // Recursively simplify until we either hit a recursion limit or nothing
  // changes.
  if (Changed)
    return SimplifyICmpOperands(Pred, LHS, RHS, Depth+1);

  return Changed;
}

bool ScalarEvolution::isKnownNegative(const SCEV *S) {
  return getSignedRangeMax(S).isNegative();
}

bool ScalarEvolution::isKnownPositive(const SCEV *S) {
  return getSignedRangeMin(S).isStrictlyPositive();
}

bool ScalarEvolution::isKnownNonNegative(const SCEV *S) {
  return !getSignedRangeMin(S).isNegative();
}

bool ScalarEvolution::isKnownNonPositive(const SCEV *S) {
  return !getSignedRangeMax(S).isStrictlyPositive();
}

bool ScalarEvolution::isKnownNonZero(const SCEV *S) {
  return isKnownNegative(S) || isKnownPositive(S);
}

std::pair<const SCEV *, const SCEV *>
ScalarEvolution::SplitIntoInitAndPostInc(const Loop *L, const SCEV *S) {
  // Compute SCEV on entry of loop L.
  const SCEV *Start = SCEVInitRewriter::rewrite(S, L, *this);
  if (Start == getCouldNotCompute())
    return { Start, Start };
  // Compute post increment SCEV for loop L.
  const SCEV *PostInc = SCEVPostIncRewriter::rewrite(S, L, *this);
  assert(PostInc != getCouldNotCompute() && "Unexpected could not compute");
  return { Start, PostInc };
}

bool ScalarEvolution::isKnownViaInduction(ICmpInst::Predicate Pred,
                                          const SCEV *LHS, const SCEV *RHS) {
  // First collect all loops.
  SmallPtrSet<const Loop *, 8> LoopsUsed;
  getUsedLoops(LHS, LoopsUsed);
  getUsedLoops(RHS, LoopsUsed);

  if (LoopsUsed.empty())
    return false;

  // Domination relationship must be a linear order on collected loops.
#ifndef NDEBUG
  for (auto *L1 : LoopsUsed)
    for (auto *L2 : LoopsUsed)
      assert((DT.dominates(L1->getHeader(), L2->getHeader()) ||
              DT.dominates(L2->getHeader(), L1->getHeader())) &&
             "Domination relationship is not a linear order");
#endif

  const Loop *MDL =
      *std::max_element(LoopsUsed.begin(), LoopsUsed.end(),
                        [&](const Loop *L1, const Loop *L2) {
         return DT.properlyDominates(L1->getHeader(), L2->getHeader());
       });

  // Get init and post increment value for LHS.
  auto SplitLHS = SplitIntoInitAndPostInc(MDL, LHS);
  // if LHS contains unknown non-invariant SCEV then bail out.
  if (SplitLHS.first == getCouldNotCompute())
    return false;
  assert (SplitLHS.second != getCouldNotCompute() && "Unexpected CNC");
  // Get init and post increment value for RHS.
  auto SplitRHS = SplitIntoInitAndPostInc(MDL, RHS);
  // if RHS contains unknown non-invariant SCEV then bail out.
  if (SplitRHS.first == getCouldNotCompute())
    return false;
  assert (SplitRHS.second != getCouldNotCompute() && "Unexpected CNC");
  // It is possible that init SCEV contains an invariant load but it does
  // not dominate MDL and is not available at MDL loop entry, so we should
  // check it here.
  if (!isAvailableAtLoopEntry(SplitLHS.first, MDL) ||
      !isAvailableAtLoopEntry(SplitRHS.first, MDL))
    return false;

  return isLoopEntryGuardedByCond(MDL, Pred, SplitLHS.first, SplitRHS.first) &&
         isLoopBackedgeGuardedByCond(MDL, Pred, SplitLHS.second,
                                     SplitRHS.second);
}

bool ScalarEvolution::isKnownPredicate(ICmpInst::Predicate Pred,
                                       const SCEV *LHS, const SCEV *RHS) {
  // Canonicalize the inputs first.
  (void)SimplifyICmpOperands(Pred, LHS, RHS);

  if (isKnownViaInduction(Pred, LHS, RHS))
    return true;

  if (isKnownPredicateViaSplitting(Pred, LHS, RHS))
    return true;

  // Otherwise see what can be done with some simple reasoning.
  return isKnownViaNonRecursiveReasoning(Pred, LHS, RHS);
}

bool ScalarEvolution::isKnownOnEveryIteration(ICmpInst::Predicate Pred,
                                              const SCEVAddRecExpr *LHS,
                                              const SCEV *RHS) {
  const Loop *L = LHS->getLoop();
  return isLoopEntryGuardedByCond(L, Pred, LHS->getStart(), RHS) &&
         isLoopBackedgeGuardedByCond(L, Pred, LHS->getPostIncExpr(*this), RHS);
}

bool ScalarEvolution::isMonotonicPredicate(const SCEVAddRecExpr *LHS,
                                           ICmpInst::Predicate Pred,
                                           bool &Increasing) {
  bool Result = isMonotonicPredicateImpl(LHS, Pred, Increasing);

#ifndef NDEBUG
  // Verify an invariant: inverting the predicate should turn a monotonically
  // increasing change to a monotonically decreasing one, and vice versa.
  bool IncreasingSwapped;
  bool ResultSwapped = isMonotonicPredicateImpl(
      LHS, ICmpInst::getSwappedPredicate(Pred), IncreasingSwapped);

  assert(Result == ResultSwapped && "should be able to analyze both!");
  if (ResultSwapped)
    assert(Increasing == !IncreasingSwapped &&
           "monotonicity should flip as we flip the predicate");
#endif

  return Result;
}

bool ScalarEvolution::isMonotonicPredicateImpl(const SCEVAddRecExpr *LHS,
                                               ICmpInst::Predicate Pred,
                                               bool &Increasing) {

  // A zero step value for LHS means the induction variable is essentially a
  // loop invariant value. We don't really depend on the predicate actually
  // flipping from false to true (for increasing predicates, and the other way
  // around for decreasing predicates), all we care about is that *if* the
  // predicate changes then it only changes from false to true.
  //
  // A zero step value in itself is not very useful, but there may be places
  // where SCEV can prove X >= 0 but not prove X > 0, so it is helpful to be
  // as general as possible.

  switch (Pred) {
  default:
    return false; // Conservative answer

  case ICmpInst::ICMP_UGT:
  case ICmpInst::ICMP_UGE:
  case ICmpInst::ICMP_ULT:
  case ICmpInst::ICMP_ULE:
    if (!LHS->hasNoUnsignedWrap())
      return false;

    Increasing = Pred == ICmpInst::ICMP_UGT || Pred == ICmpInst::ICMP_UGE;
    return true;

  case ICmpInst::ICMP_SGT:
  case ICmpInst::ICMP_SGE:
  case ICmpInst::ICMP_SLT:
  case ICmpInst::ICMP_SLE: {
    if (!LHS->hasNoSignedWrap())
      return false;

    const SCEV *Step = LHS->getStepRecurrence(*this);

    if (isKnownNonNegative(Step)) {
      Increasing = Pred == ICmpInst::ICMP_SGT || Pred == ICmpInst::ICMP_SGE;
      return true;
    }

    if (isKnownNonPositive(Step)) {
      Increasing = Pred == ICmpInst::ICMP_SLT || Pred == ICmpInst::ICMP_SLE;
      return true;
    }

    return false;
  }

  }

  llvm_unreachable("switch has default clause!");
}

bool ScalarEvolution::isLoopInvariantPredicate(
    ICmpInst::Predicate Pred, const SCEV *LHS, const SCEV *RHS, const Loop *L,
    ICmpInst::Predicate &InvariantPred, const SCEV *&InvariantLHS,
    const SCEV *&InvariantRHS) {

  // If there is a loop-invariant, force it into the RHS, otherwise bail out.
  if (!isLoopInvariant(RHS, L)) {
    if (!isLoopInvariant(LHS, L))
      return false;

    std::swap(LHS, RHS);
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }

  const SCEVAddRecExpr *ArLHS = dyn_cast<SCEVAddRecExpr>(LHS);
  if (!ArLHS || ArLHS->getLoop() != L)
    return false;

  bool Increasing;
  if (!isMonotonicPredicate(ArLHS, Pred, Increasing))
    return false;

  // If the predicate "ArLHS `Pred` RHS" monotonically increases from false to
  // true as the loop iterates, and the backedge is control dependent on
  // "ArLHS `Pred` RHS" == true then we can reason as follows:
  //
  //   * if the predicate was false in the first iteration then the predicate
  //     is never evaluated again, since the loop exits without taking the
  //     backedge.
  //   * if the predicate was true in the first iteration then it will
  //     continue to be true for all future iterations since it is
  //     monotonically increasing.
  //
  // For both the above possibilities, we can replace the loop varying
  // predicate with its value on the first iteration of the loop (which is
  // loop invariant).
  //
  // A similar reasoning applies for a monotonically decreasing predicate, by
  // replacing true with false and false with true in the above two bullets.

  auto P = Increasing ? Pred : ICmpInst::getInversePredicate(Pred);

  if (!isLoopBackedgeGuardedByCond(L, P, LHS, RHS))
    return false;

  InvariantPred = Pred;
  InvariantLHS = ArLHS->getStart();
  InvariantRHS = RHS;
  return true;
}

bool ScalarEvolution::isKnownPredicateViaConstantRanges(
    ICmpInst::Predicate Pred, const SCEV *LHS, const SCEV *RHS) {
  if (HasSameValue(LHS, RHS))
    return ICmpInst::isTrueWhenEqual(Pred);

  // This code is split out from isKnownPredicate because it is called from
  // within isLoopEntryGuardedByCond.

  auto CheckRanges =
      [&](const ConstantRange &RangeLHS, const ConstantRange &RangeRHS) {
    return ConstantRange::makeSatisfyingICmpRegion(Pred, RangeRHS)
        .contains(RangeLHS);
  };

  // The check at the top of the function catches the case where the values are
  // known to be equal.
  if (Pred == CmpInst::ICMP_EQ)
    return false;

  if (Pred == CmpInst::ICMP_NE)
    return CheckRanges(getSignedRange(LHS), getSignedRange(RHS)) ||
           CheckRanges(getUnsignedRange(LHS), getUnsignedRange(RHS)) ||
           isKnownNonZero(getMinusSCEV(LHS, RHS));

  if (CmpInst::isSigned(Pred))
    return CheckRanges(getSignedRange(LHS), getSignedRange(RHS));

  return CheckRanges(getUnsignedRange(LHS), getUnsignedRange(RHS));
}

bool ScalarEvolution::isKnownPredicateViaNoOverflow(ICmpInst::Predicate Pred,
                                                    const SCEV *LHS,
                                                    const SCEV *RHS) {
  // Match Result to (X + Y)<ExpectedFlags> where Y is a constant integer.
  // Return Y via OutY.
  auto MatchBinaryAddToConst =
      [this](const SCEV *Result, const SCEV *X, APInt &OutY,
             SCEV::NoWrapFlags ExpectedFlags) {
    const SCEV *NonConstOp, *ConstOp;
    SCEV::NoWrapFlags FlagsPresent;

    if (!splitBinaryAdd(Result, ConstOp, NonConstOp, FlagsPresent) ||
        !isa<SCEVConstant>(ConstOp) || NonConstOp != X)
      return false;

    OutY = cast<SCEVConstant>(ConstOp)->getAPInt();
    return (FlagsPresent & ExpectedFlags) == ExpectedFlags;
  };

  APInt C;

  switch (Pred) {
  default:
    break;

  case ICmpInst::ICMP_SGE:
    std::swap(LHS, RHS);
    LLVM_FALLTHROUGH;
  case ICmpInst::ICMP_SLE:
    // X s<= (X + C)<nsw> if C >= 0
    if (MatchBinaryAddToConst(RHS, LHS, C, SCEV::FlagNSW) && C.isNonNegative())
      return true;

    // (X + C)<nsw> s<= X if C <= 0
    if (MatchBinaryAddToConst(LHS, RHS, C, SCEV::FlagNSW) &&
        !C.isStrictlyPositive())
      return true;
    break;

  case ICmpInst::ICMP_SGT:
    std::swap(LHS, RHS);
    LLVM_FALLTHROUGH;
  case ICmpInst::ICMP_SLT:
    // X s< (X + C)<nsw> if C > 0
    if (MatchBinaryAddToConst(RHS, LHS, C, SCEV::FlagNSW) &&
        C.isStrictlyPositive())
      return true;

    // (X + C)<nsw> s< X if C < 0
    if (MatchBinaryAddToConst(LHS, RHS, C, SCEV::FlagNSW) && C.isNegative())
      return true;
    break;
  }

  return false;
}

bool ScalarEvolution::isKnownPredicateViaSplitting(ICmpInst::Predicate Pred,
                                                   const SCEV *LHS,
                                                   const SCEV *RHS) {
  if (Pred != ICmpInst::ICMP_ULT || ProvingSplitPredicate)
    return false;

  // Allowing arbitrary number of activations of isKnownPredicateViaSplitting on
  // the stack can result in exponential time complexity.
  SaveAndRestore<bool> Restore(ProvingSplitPredicate, true);

  // If L >= 0 then I `ult` L <=> I >= 0 && I `slt` L
  //
  // To prove L >= 0 we use isKnownNonNegative whereas to prove I >= 0 we use
  // isKnownPredicate.  isKnownPredicate is more powerful, but also more
  // expensive; and using isKnownNonNegative(RHS) is sufficient for most of the
  // interesting cases seen in practice.  We can consider "upgrading" L >= 0 to
  // use isKnownPredicate later if needed.
  return isKnownNonNegative(RHS) &&
         isKnownPredicate(CmpInst::ICMP_SGE, LHS, getZero(LHS->getType())) &&
         isKnownPredicate(CmpInst::ICMP_SLT, LHS, RHS);
}

bool ScalarEvolution::isImpliedViaGuard(BasicBlock *BB,
                                        ICmpInst::Predicate Pred,
                                        const SCEV *LHS, const SCEV *RHS) {
  // No need to even try if we know the module has no guards.
  if (!HasGuards)
    return false;

  return any_of(*BB, [&](Instruction &I) {
    using namespace llvm::PatternMatch;

    Value *Condition;
    return match(&I, m_Intrinsic<Intrinsic::experimental_guard>(
                         m_Value(Condition))) &&
           isImpliedCond(Pred, LHS, RHS, Condition, false);
  });
}

/// isLoopBackedgeGuardedByCond - Test whether the backedge of the loop is
/// protected by a conditional between LHS and RHS.  This is used to
/// to eliminate casts.
bool
ScalarEvolution::isLoopBackedgeGuardedByCond(const Loop *L,
                                             ICmpInst::Predicate Pred,
                                             const SCEV *LHS, const SCEV *RHS) {
  // Interpret a null as meaning no loop, where there is obviously no guard
  // (interprocedural conditions notwithstanding).
  if (!L) return true;

  if (VerifyIR)
    assert(!verifyFunction(*L->getHeader()->getParent(), &dbgs()) &&
           "This cannot be done on broken IR!");


  if (isKnownViaNonRecursiveReasoning(Pred, LHS, RHS))
    return true;

  BasicBlock *Latch = L->getLoopLatch();
  if (!Latch)
    return false;

  BranchInst *LoopContinuePredicate =
    dyn_cast<BranchInst>(Latch->getTerminator());
  if (LoopContinuePredicate && LoopContinuePredicate->isConditional() &&
      isImpliedCond(Pred, LHS, RHS,
                    LoopContinuePredicate->getCondition(),
                    LoopContinuePredicate->getSuccessor(0) != L->getHeader()))
    return true;

  // We don't want more than one activation of the following loops on the stack
  // -- that can lead to O(n!) time complexity.
  if (WalkingBEDominatingConds)
    return false;

  SaveAndRestore<bool> ClearOnExit(WalkingBEDominatingConds, true);

  // See if we can exploit a trip count to prove the predicate.
  const auto &BETakenInfo = getBackedgeTakenInfo(L);
  const SCEV *LatchBECount = BETakenInfo.getExact(Latch, this);
  if (LatchBECount != getCouldNotCompute()) {
    // We know that Latch branches back to the loop header exactly
    // LatchBECount times.  This means the backdege condition at Latch is
    // equivalent to  "{0,+,1} u< LatchBECount".
    Type *Ty = LatchBECount->getType();
    auto NoWrapFlags = SCEV::NoWrapFlags(SCEV::FlagNUW | SCEV::FlagNW);
    const SCEV *LoopCounter =
      getAddRecExpr(getZero(Ty), getOne(Ty), L, NoWrapFlags);
    if (isImpliedCond(Pred, LHS, RHS, ICmpInst::ICMP_ULT, LoopCounter,
                      LatchBECount))
      return true;
  }

  // Check conditions due to any @llvm.assume intrinsics.
  for (auto &AssumeVH : AC.assumptions()) {
    if (!AssumeVH)
      continue;
    auto *CI = cast<CallInst>(AssumeVH);
    if (!DT.dominates(CI, Latch->getTerminator()))
      continue;

    if (isImpliedCond(Pred, LHS, RHS, CI->getArgOperand(0), false))
      return true;
  }

  // If the loop is not reachable from the entry block, we risk running into an
  // infinite loop as we walk up into the dom tree.  These loops do not matter
  // anyway, so we just return a conservative answer when we see them.
  if (!DT.isReachableFromEntry(L->getHeader()))
    return false;

  if (isImpliedViaGuard(Latch, Pred, LHS, RHS))
    return true;

  for (DomTreeNode *DTN = DT[Latch], *HeaderDTN = DT[L->getHeader()];
       DTN != HeaderDTN; DTN = DTN->getIDom()) {
    assert(DTN && "should reach the loop header before reaching the root!");

    BasicBlock *BB = DTN->getBlock();
    if (isImpliedViaGuard(BB, Pred, LHS, RHS))
      return true;

    BasicBlock *PBB = BB->getSinglePredecessor();
    if (!PBB)
      continue;

    BranchInst *ContinuePredicate = dyn_cast<BranchInst>(PBB->getTerminator());
    if (!ContinuePredicate || !ContinuePredicate->isConditional())
      continue;

    Value *Condition = ContinuePredicate->getCondition();

    // If we have an edge `E` within the loop body that dominates the only
    // latch, the condition guarding `E` also guards the backedge.  This
    // reasoning works only for loops with a single latch.

    BasicBlockEdge DominatingEdge(PBB, BB);
    if (DominatingEdge.isSingleEdge()) {
      // We're constructively (and conservatively) enumerating edges within the
      // loop body that dominate the latch.  The dominator tree better agree
      // with us on this:
      assert(DT.dominates(DominatingEdge, Latch) && "should be!");

      if (isImpliedCond(Pred, LHS, RHS, Condition,
                        BB != ContinuePredicate->getSuccessor(0)))
        return true;
    }
  }

  return false;
}

bool
ScalarEvolution::isLoopEntryGuardedByCond(const Loop *L,
                                          ICmpInst::Predicate Pred,
                                          const SCEV *LHS, const SCEV *RHS) {
  // Interpret a null as meaning no loop, where there is obviously no guard
  // (interprocedural conditions notwithstanding).
  if (!L) return false;

  if (VerifyIR)
    assert(!verifyFunction(*L->getHeader()->getParent(), &dbgs()) &&
           "This cannot be done on broken IR!");

  // Both LHS and RHS must be available at loop entry.
  assert(isAvailableAtLoopEntry(LHS, L) &&
         "LHS is not available at Loop Entry");
  assert(isAvailableAtLoopEntry(RHS, L) &&
         "RHS is not available at Loop Entry");

  if (isKnownViaNonRecursiveReasoning(Pred, LHS, RHS))
    return true;

  // If we cannot prove strict comparison (e.g. a > b), maybe we can prove
  // the facts (a >= b && a != b) separately. A typical situation is when the
  // non-strict comparison is known from ranges and non-equality is known from
  // dominating predicates. If we are proving strict comparison, we always try
  // to prove non-equality and non-strict comparison separately.
  auto NonStrictPredicate = ICmpInst::getNonStrictPredicate(Pred);
  const bool ProvingStrictComparison = (Pred != NonStrictPredicate);
  bool ProvedNonStrictComparison = false;
  bool ProvedNonEquality = false;

  if (ProvingStrictComparison) {
    ProvedNonStrictComparison =
        isKnownViaNonRecursiveReasoning(NonStrictPredicate, LHS, RHS);
    ProvedNonEquality =
        isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_NE, LHS, RHS);
    if (ProvedNonStrictComparison && ProvedNonEquality)
      return true;
  }

  // Try to prove (Pred, LHS, RHS) using isImpliedViaGuard.
  auto ProveViaGuard = [&](BasicBlock *Block) {
    if (isImpliedViaGuard(Block, Pred, LHS, RHS))
      return true;
    if (ProvingStrictComparison) {
      if (!ProvedNonStrictComparison)
        ProvedNonStrictComparison =
            isImpliedViaGuard(Block, NonStrictPredicate, LHS, RHS);
      if (!ProvedNonEquality)
        ProvedNonEquality =
            isImpliedViaGuard(Block, ICmpInst::ICMP_NE, LHS, RHS);
      if (ProvedNonStrictComparison && ProvedNonEquality)
        return true;
    }
    return false;
  };

  // Try to prove (Pred, LHS, RHS) using isImpliedCond.
  auto ProveViaCond = [&](Value *Condition, bool Inverse) {
    if (isImpliedCond(Pred, LHS, RHS, Condition, Inverse))
      return true;
    if (ProvingStrictComparison) {
      if (!ProvedNonStrictComparison)
        ProvedNonStrictComparison =
            isImpliedCond(NonStrictPredicate, LHS, RHS, Condition, Inverse);
      if (!ProvedNonEquality)
        ProvedNonEquality =
            isImpliedCond(ICmpInst::ICMP_NE, LHS, RHS, Condition, Inverse);
      if (ProvedNonStrictComparison && ProvedNonEquality)
        return true;
    }
    return false;
  };

  // Starting at the loop predecessor, climb up the predecessor chain, as long
  // as there are predecessors that can be found that have unique successors
  // leading to the original header.
  for (std::pair<BasicBlock *, BasicBlock *>
         Pair(L->getLoopPredecessor(), L->getHeader());
       Pair.first;
       Pair = getPredecessorWithUniqueSuccessorForBB(Pair.first)) {

    if (ProveViaGuard(Pair.first))
      return true;

    BranchInst *LoopEntryPredicate =
      dyn_cast<BranchInst>(Pair.first->getTerminator());
    if (!LoopEntryPredicate ||
        LoopEntryPredicate->isUnconditional())
      continue;

    if (ProveViaCond(LoopEntryPredicate->getCondition(),
                     LoopEntryPredicate->getSuccessor(0) != Pair.second))
      return true;
  }

  // Check conditions due to any @llvm.assume intrinsics.
  for (auto &AssumeVH : AC.assumptions()) {
    if (!AssumeVH)
      continue;
    auto *CI = cast<CallInst>(AssumeVH);
    if (!DT.dominates(CI, L->getHeader()))
      continue;

    if (ProveViaCond(CI->getArgOperand(0), false))
      return true;
  }

  return false;
}

bool ScalarEvolution::isImpliedCond(ICmpInst::Predicate Pred,
                                    const SCEV *LHS, const SCEV *RHS,
                                    Value *FoundCondValue,
                                    bool Inverse) {
  if (!PendingLoopPredicates.insert(FoundCondValue).second)
    return false;

  auto ClearOnExit =
      make_scope_exit([&]() { PendingLoopPredicates.erase(FoundCondValue); });

  // Recursively handle And and Or conditions.
  if (BinaryOperator *BO = dyn_cast<BinaryOperator>(FoundCondValue)) {
    if (BO->getOpcode() == Instruction::And) {
      if (!Inverse)
        return isImpliedCond(Pred, LHS, RHS, BO->getOperand(0), Inverse) ||
               isImpliedCond(Pred, LHS, RHS, BO->getOperand(1), Inverse);
    } else if (BO->getOpcode() == Instruction::Or) {
      if (Inverse)
        return isImpliedCond(Pred, LHS, RHS, BO->getOperand(0), Inverse) ||
               isImpliedCond(Pred, LHS, RHS, BO->getOperand(1), Inverse);
    }
  }

  ICmpInst *ICI = dyn_cast<ICmpInst>(FoundCondValue);
  if (!ICI) return false;

  // Now that we found a conditional branch that dominates the loop or controls
  // the loop latch. Check to see if it is the comparison we are looking for.
  ICmpInst::Predicate FoundPred;
  if (Inverse)
    FoundPred = ICI->getInversePredicate();
  else
    FoundPred = ICI->getPredicate();

  const SCEV *FoundLHS = getSCEV(ICI->getOperand(0));
  const SCEV *FoundRHS = getSCEV(ICI->getOperand(1));

  return isImpliedCond(Pred, LHS, RHS, FoundPred, FoundLHS, FoundRHS);
}

bool ScalarEvolution::isImpliedCond(ICmpInst::Predicate Pred, const SCEV *LHS,
                                    const SCEV *RHS,
                                    ICmpInst::Predicate FoundPred,
                                    const SCEV *FoundLHS,
                                    const SCEV *FoundRHS) {
  // Balance the types.
  if (getTypeSizeInBits(LHS->getType()) <
      getTypeSizeInBits(FoundLHS->getType())) {
    if (CmpInst::isSigned(Pred)) {
      LHS = getSignExtendExpr(LHS, FoundLHS->getType());
      RHS = getSignExtendExpr(RHS, FoundLHS->getType());
    } else {
      LHS = getZeroExtendExpr(LHS, FoundLHS->getType());
      RHS = getZeroExtendExpr(RHS, FoundLHS->getType());
    }
  } else if (getTypeSizeInBits(LHS->getType()) >
      getTypeSizeInBits(FoundLHS->getType())) {
    if (CmpInst::isSigned(FoundPred)) {
      FoundLHS = getSignExtendExpr(FoundLHS, LHS->getType());
      FoundRHS = getSignExtendExpr(FoundRHS, LHS->getType());
    } else {
      FoundLHS = getZeroExtendExpr(FoundLHS, LHS->getType());
      FoundRHS = getZeroExtendExpr(FoundRHS, LHS->getType());
    }
  }

  // Canonicalize the query to match the way instcombine will have
  // canonicalized the comparison.
  if (SimplifyICmpOperands(Pred, LHS, RHS))
    if (LHS == RHS)
      return CmpInst::isTrueWhenEqual(Pred);
  if (SimplifyICmpOperands(FoundPred, FoundLHS, FoundRHS))
    if (FoundLHS == FoundRHS)
      return CmpInst::isFalseWhenEqual(FoundPred);

  // Check to see if we can make the LHS or RHS match.
  if (LHS == FoundRHS || RHS == FoundLHS) {
    if (isa<SCEVConstant>(RHS)) {
      std::swap(FoundLHS, FoundRHS);
      FoundPred = ICmpInst::getSwappedPredicate(FoundPred);
    } else {
      std::swap(LHS, RHS);
      Pred = ICmpInst::getSwappedPredicate(Pred);
    }
  }

  // Check whether the found predicate is the same as the desired predicate.
  if (FoundPred == Pred)
    return isImpliedCondOperands(Pred, LHS, RHS, FoundLHS, FoundRHS);

  // Check whether swapping the found predicate makes it the same as the
  // desired predicate.
  if (ICmpInst::getSwappedPredicate(FoundPred) == Pred) {
    if (isa<SCEVConstant>(RHS))
      return isImpliedCondOperands(Pred, LHS, RHS, FoundRHS, FoundLHS);
    else
      return isImpliedCondOperands(ICmpInst::getSwappedPredicate(Pred),
                                   RHS, LHS, FoundLHS, FoundRHS);
  }

  // Unsigned comparison is the same as signed comparison when both the operands
  // are non-negative.
  if (CmpInst::isUnsigned(FoundPred) &&
      CmpInst::getSignedPredicate(FoundPred) == Pred &&
      isKnownNonNegative(FoundLHS) && isKnownNonNegative(FoundRHS))
    return isImpliedCondOperands(Pred, LHS, RHS, FoundLHS, FoundRHS);

  // Check if we can make progress by sharpening ranges.
  if (FoundPred == ICmpInst::ICMP_NE &&
      (isa<SCEVConstant>(FoundLHS) || isa<SCEVConstant>(FoundRHS))) {

    const SCEVConstant *C = nullptr;
    const SCEV *V = nullptr;

    if (isa<SCEVConstant>(FoundLHS)) {
      C = cast<SCEVConstant>(FoundLHS);
      V = FoundRHS;
    } else {
      C = cast<SCEVConstant>(FoundRHS);
      V = FoundLHS;
    }

    // The guarding predicate tells us that C != V. If the known range
    // of V is [C, t), we can sharpen the range to [C + 1, t).  The
    // range we consider has to correspond to same signedness as the
    // predicate we're interested in folding.

    APInt Min = ICmpInst::isSigned(Pred) ?
        getSignedRangeMin(V) : getUnsignedRangeMin(V);

    if (Min == C->getAPInt()) {
      // Given (V >= Min && V != Min) we conclude V >= (Min + 1).
      // This is true even if (Min + 1) wraps around -- in case of
      // wraparound, (Min + 1) < Min, so (V >= Min => V >= (Min + 1)).

      APInt SharperMin = Min + 1;

      switch (Pred) {
        case ICmpInst::ICMP_SGE:
        case ICmpInst::ICMP_UGE:
          // We know V `Pred` SharperMin.  If this implies LHS `Pred`
          // RHS, we're done.
          if (isImpliedCondOperands(Pred, LHS, RHS, V,
                                    getConstant(SharperMin)))
            return true;
          LLVM_FALLTHROUGH;

        case ICmpInst::ICMP_SGT:
        case ICmpInst::ICMP_UGT:
          // We know from the range information that (V `Pred` Min ||
          // V == Min).  We know from the guarding condition that !(V
          // == Min).  This gives us
          //
          //       V `Pred` Min || V == Min && !(V == Min)
          //   =>  V `Pred` Min
          //
          // If V `Pred` Min implies LHS `Pred` RHS, we're done.

          if (isImpliedCondOperands(Pred, LHS, RHS, V, getConstant(Min)))
            return true;
          LLVM_FALLTHROUGH;

        default:
          // No change
          break;
      }
    }
  }

  // Check whether the actual condition is beyond sufficient.
  if (FoundPred == ICmpInst::ICMP_EQ)
    if (ICmpInst::isTrueWhenEqual(Pred))
      if (isImpliedCondOperands(Pred, LHS, RHS, FoundLHS, FoundRHS))
        return true;
  if (Pred == ICmpInst::ICMP_NE)
    if (!ICmpInst::isTrueWhenEqual(FoundPred))
      if (isImpliedCondOperands(FoundPred, LHS, RHS, FoundLHS, FoundRHS))
        return true;

  // Otherwise assume the worst.
  return false;
}

bool ScalarEvolution::splitBinaryAdd(const SCEV *Expr,
                                     const SCEV *&L, const SCEV *&R,
                                     SCEV::NoWrapFlags &Flags) {
  const auto *AE = dyn_cast<SCEVAddExpr>(Expr);
  if (!AE || AE->getNumOperands() != 2)
    return false;

  L = AE->getOperand(0);
  R = AE->getOperand(1);
  Flags = AE->getNoWrapFlags();
  return true;
}

Optional<APInt> ScalarEvolution::computeConstantDifference(const SCEV *More,
                                                           const SCEV *Less) {
  // We avoid subtracting expressions here because this function is usually
  // fairly deep in the call stack (i.e. is called many times).

  if (isa<SCEVAddRecExpr>(Less) && isa<SCEVAddRecExpr>(More)) {
    const auto *LAR = cast<SCEVAddRecExpr>(Less);
    const auto *MAR = cast<SCEVAddRecExpr>(More);

    if (LAR->getLoop() != MAR->getLoop())
      return None;

    // We look at affine expressions only; not for correctness but to keep
    // getStepRecurrence cheap.
    if (!LAR->isAffine() || !MAR->isAffine())
      return None;

    if (LAR->getStepRecurrence(*this) != MAR->getStepRecurrence(*this))
      return None;

    Less = LAR->getStart();
    More = MAR->getStart();

    // fall through
  }

  if (isa<SCEVConstant>(Less) && isa<SCEVConstant>(More)) {
    const auto &M = cast<SCEVConstant>(More)->getAPInt();
    const auto &L = cast<SCEVConstant>(Less)->getAPInt();
    return M - L;
  }

  SCEV::NoWrapFlags Flags;
  const SCEV *LLess = nullptr, *RLess = nullptr;
  const SCEV *LMore = nullptr, *RMore = nullptr;
  const SCEVConstant *C1 = nullptr, *C2 = nullptr;
  // Compare (X + C1) vs X.
  if (splitBinaryAdd(Less, LLess, RLess, Flags))
    if ((C1 = dyn_cast<SCEVConstant>(LLess)))
      if (RLess == More)
        return -(C1->getAPInt());

  // Compare X vs (X + C2).
  if (splitBinaryAdd(More, LMore, RMore, Flags))
    if ((C2 = dyn_cast<SCEVConstant>(LMore)))
      if (RMore == Less)
        return C2->getAPInt();

  // Compare (X + C1) vs (X + C2).
  if (C1 && C2 && RLess == RMore)
    return C2->getAPInt() - C1->getAPInt();

  return None;
}

bool ScalarEvolution::isImpliedCondOperandsViaNoOverflow(
    ICmpInst::Predicate Pred, const SCEV *LHS, const SCEV *RHS,
    const SCEV *FoundLHS, const SCEV *FoundRHS) {
  if (Pred != CmpInst::ICMP_SLT && Pred != CmpInst::ICMP_ULT)
    return false;

  const auto *AddRecLHS = dyn_cast<SCEVAddRecExpr>(LHS);
  if (!AddRecLHS)
    return false;

  const auto *AddRecFoundLHS = dyn_cast<SCEVAddRecExpr>(FoundLHS);
  if (!AddRecFoundLHS)
    return false;

  // We'd like to let SCEV reason about control dependencies, so we constrain
  // both the inequalities to be about add recurrences on the same loop.  This
  // way we can use isLoopEntryGuardedByCond later.

  const Loop *L = AddRecFoundLHS->getLoop();
  if (L != AddRecLHS->getLoop())
    return false;

  //  FoundLHS u< FoundRHS u< -C =>  (FoundLHS + C) u< (FoundRHS + C) ... (1)
  //
  //  FoundLHS s< FoundRHS s< INT_MIN - C => (FoundLHS + C) s< (FoundRHS + C)
  //                                                                  ... (2)
  //
  // Informal proof for (2), assuming (1) [*]:
  //
  // We'll also assume (A s< B) <=> ((A + INT_MIN) u< (B + INT_MIN)) ... (3)[**]
  //
  // Then
  //
  //       FoundLHS s< FoundRHS s< INT_MIN - C
  // <=>  (FoundLHS + INT_MIN) u< (FoundRHS + INT_MIN) u< -C   [ using (3) ]
  // <=>  (FoundLHS + INT_MIN + C) u< (FoundRHS + INT_MIN + C) [ using (1) ]
  // <=>  (FoundLHS + INT_MIN + C + INT_MIN) s<
  //                        (FoundRHS + INT_MIN + C + INT_MIN) [ using (3) ]
  // <=>  FoundLHS + C s< FoundRHS + C
  //
  // [*]: (1) can be proved by ruling out overflow.
  //
  // [**]: This can be proved by analyzing all the four possibilities:
  //    (A s< 0, B s< 0), (A s< 0, B s>= 0), (A s>= 0, B s< 0) and
  //    (A s>= 0, B s>= 0).
  //
  // Note:
  // Despite (2), "FoundRHS s< INT_MIN - C" does not mean that "FoundRHS + C"
  // will not sign underflow.  For instance, say FoundLHS = (i8 -128), FoundRHS
  // = (i8 -127) and C = (i8 -100).  Then INT_MIN - C = (i8 -28), and FoundRHS
  // s< (INT_MIN - C).  Lack of sign overflow / underflow in "FoundRHS + C" is
  // neither necessary nor sufficient to prove "(FoundLHS + C) s< (FoundRHS +
  // C)".

  Optional<APInt> LDiff = computeConstantDifference(LHS, FoundLHS);
  Optional<APInt> RDiff = computeConstantDifference(RHS, FoundRHS);
  if (!LDiff || !RDiff || *LDiff != *RDiff)
    return false;

  if (LDiff->isMinValue())
    return true;

  APInt FoundRHSLimit;

  if (Pred == CmpInst::ICMP_ULT) {
    FoundRHSLimit = -(*RDiff);
  } else {
    assert(Pred == CmpInst::ICMP_SLT && "Checked above!");
    FoundRHSLimit = APInt::getSignedMinValue(getTypeSizeInBits(RHS->getType())) - *RDiff;
  }

  // Try to prove (1) or (2), as needed.
  return isAvailableAtLoopEntry(FoundRHS, L) &&
         isLoopEntryGuardedByCond(L, Pred, FoundRHS,
                                  getConstant(FoundRHSLimit));
}

bool ScalarEvolution::isImpliedViaMerge(ICmpInst::Predicate Pred,
                                        const SCEV *LHS, const SCEV *RHS,
                                        const SCEV *FoundLHS,
                                        const SCEV *FoundRHS, unsigned Depth) {
  const PHINode *LPhi = nullptr, *RPhi = nullptr;

  auto ClearOnExit = make_scope_exit([&]() {
    if (LPhi) {
      bool Erased = PendingMerges.erase(LPhi);
      assert(Erased && "Failed to erase LPhi!");
      (void)Erased;
    }
    if (RPhi) {
      bool Erased = PendingMerges.erase(RPhi);
      assert(Erased && "Failed to erase RPhi!");
      (void)Erased;
    }
  });

  // Find respective Phis and check that they are not being pending.
  if (const SCEVUnknown *LU = dyn_cast<SCEVUnknown>(LHS))
    if (auto *Phi = dyn_cast<PHINode>(LU->getValue())) {
      if (!PendingMerges.insert(Phi).second)
        return false;
      LPhi = Phi;
    }
  if (const SCEVUnknown *RU = dyn_cast<SCEVUnknown>(RHS))
    if (auto *Phi = dyn_cast<PHINode>(RU->getValue())) {
      // If we detect a loop of Phi nodes being processed by this method, for
      // example:
      //
      //   %a = phi i32 [ %some1, %preheader ], [ %b, %latch ]
      //   %b = phi i32 [ %some2, %preheader ], [ %a, %latch ]
      //
      // we don't want to deal with a case that complex, so return conservative
      // answer false.
      if (!PendingMerges.insert(Phi).second)
        return false;
      RPhi = Phi;
    }

  // If none of LHS, RHS is a Phi, nothing to do here.
  if (!LPhi && !RPhi)
    return false;

  // If there is a SCEVUnknown Phi we are interested in, make it left.
  if (!LPhi) {
    std::swap(LHS, RHS);
    std::swap(FoundLHS, FoundRHS);
    std::swap(LPhi, RPhi);
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }

  assert(LPhi && "LPhi should definitely be a SCEVUnknown Phi!");
  const BasicBlock *LBB = LPhi->getParent();
  const SCEVAddRecExpr *RAR = dyn_cast<SCEVAddRecExpr>(RHS);

  auto ProvedEasily = [&](const SCEV *S1, const SCEV *S2) {
    return isKnownViaNonRecursiveReasoning(Pred, S1, S2) ||
           isImpliedCondOperandsViaRanges(Pred, S1, S2, FoundLHS, FoundRHS) ||
           isImpliedViaOperations(Pred, S1, S2, FoundLHS, FoundRHS, Depth);
  };

  if (RPhi && RPhi->getParent() == LBB) {
    // Case one: RHS is also a SCEVUnknown Phi from the same basic block.
    // If we compare two Phis from the same block, and for each entry block
    // the predicate is true for incoming values from this block, then the
    // predicate is also true for the Phis.
    for (const BasicBlock *IncBB : predecessors(LBB)) {
      const SCEV *L = getSCEV(LPhi->getIncomingValueForBlock(IncBB));
      const SCEV *R = getSCEV(RPhi->getIncomingValueForBlock(IncBB));
      if (!ProvedEasily(L, R))
        return false;
    }
  } else if (RAR && RAR->getLoop()->getHeader() == LBB) {
    // Case two: RHS is also a Phi from the same basic block, and it is an
    // AddRec. It means that there is a loop which has both AddRec and Unknown
    // PHIs, for it we can compare incoming values of AddRec from above the loop
    // and latch with their respective incoming values of LPhi.
    // TODO: Generalize to handle loops with many inputs in a header.
    if (LPhi->getNumIncomingValues() != 2) return false;

    auto *RLoop = RAR->getLoop();
    auto *Predecessor = RLoop->getLoopPredecessor();
    assert(Predecessor && "Loop with AddRec with no predecessor?");
    const SCEV *L1 = getSCEV(LPhi->getIncomingValueForBlock(Predecessor));
    if (!ProvedEasily(L1, RAR->getStart()))
      return false;
    auto *Latch = RLoop->getLoopLatch();
    assert(Latch && "Loop with AddRec with no latch?");
    const SCEV *L2 = getSCEV(LPhi->getIncomingValueForBlock(Latch));
    if (!ProvedEasily(L2, RAR->getPostIncExpr(*this)))
      return false;
  } else {
    // In all other cases go over inputs of LHS and compare each of them to RHS,
    // the predicate is true for (LHS, RHS) if it is true for all such pairs.
    // At this point RHS is either a non-Phi, or it is a Phi from some block
    // different from LBB.
    for (const BasicBlock *IncBB : predecessors(LBB)) {
      // Check that RHS is available in this block.
      if (!dominates(RHS, IncBB))
        return false;
      const SCEV *L = getSCEV(LPhi->getIncomingValueForBlock(IncBB));
      if (!ProvedEasily(L, RHS))
        return false;
    }
  }
  return true;
}

bool ScalarEvolution::isImpliedCondOperands(ICmpInst::Predicate Pred,
                                            const SCEV *LHS, const SCEV *RHS,
                                            const SCEV *FoundLHS,
                                            const SCEV *FoundRHS) {
  if (isImpliedCondOperandsViaRanges(Pred, LHS, RHS, FoundLHS, FoundRHS))
    return true;

  if (isImpliedCondOperandsViaNoOverflow(Pred, LHS, RHS, FoundLHS, FoundRHS))
    return true;

  return isImpliedCondOperandsHelper(Pred, LHS, RHS,
                                     FoundLHS, FoundRHS) ||
         // ~x < ~y --> x > y
         isImpliedCondOperandsHelper(Pred, LHS, RHS,
                                     getNotSCEV(FoundRHS),
                                     getNotSCEV(FoundLHS));
}

/// If Expr computes ~A, return A else return nullptr
static const SCEV *MatchNotExpr(const SCEV *Expr) {
  const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(Expr);
  if (!Add || Add->getNumOperands() != 2 ||
      !Add->getOperand(0)->isAllOnesValue())
    return nullptr;

  const SCEVMulExpr *AddRHS = dyn_cast<SCEVMulExpr>(Add->getOperand(1));
  if (!AddRHS || AddRHS->getNumOperands() != 2 ||
      !AddRHS->getOperand(0)->isAllOnesValue())
    return nullptr;

  return AddRHS->getOperand(1);
}

/// Is MaybeMaxExpr an SMax or UMax of Candidate and some other values?
template<typename MaxExprType>
static bool IsMaxConsistingOf(const SCEV *MaybeMaxExpr,
                              const SCEV *Candidate) {
  const MaxExprType *MaxExpr = dyn_cast<MaxExprType>(MaybeMaxExpr);
  if (!MaxExpr) return false;

  return find(MaxExpr->operands(), Candidate) != MaxExpr->op_end();
}

/// Is MaybeMinExpr an SMin or UMin of Candidate and some other values?
template<typename MaxExprType>
static bool IsMinConsistingOf(ScalarEvolution &SE,
                              const SCEV *MaybeMinExpr,
                              const SCEV *Candidate) {
  const SCEV *MaybeMaxExpr = MatchNotExpr(MaybeMinExpr);
  if (!MaybeMaxExpr)
    return false;

  return IsMaxConsistingOf<MaxExprType>(MaybeMaxExpr, SE.getNotSCEV(Candidate));
}

static bool IsKnownPredicateViaAddRecStart(ScalarEvolution &SE,
                                           ICmpInst::Predicate Pred,
                                           const SCEV *LHS, const SCEV *RHS) {
  // If both sides are affine addrecs for the same loop, with equal
  // steps, and we know the recurrences don't wrap, then we only
  // need to check the predicate on the starting values.

  if (!ICmpInst::isRelational(Pred))
    return false;

  const SCEVAddRecExpr *LAR = dyn_cast<SCEVAddRecExpr>(LHS);
  if (!LAR)
    return false;
  const SCEVAddRecExpr *RAR = dyn_cast<SCEVAddRecExpr>(RHS);
  if (!RAR)
    return false;
  if (LAR->getLoop() != RAR->getLoop())
    return false;
  if (!LAR->isAffine() || !RAR->isAffine())
    return false;

  if (LAR->getStepRecurrence(SE) != RAR->getStepRecurrence(SE))
    return false;

  SCEV::NoWrapFlags NW = ICmpInst::isSigned(Pred) ?
                         SCEV::FlagNSW : SCEV::FlagNUW;
  if (!LAR->getNoWrapFlags(NW) || !RAR->getNoWrapFlags(NW))
    return false;

  return SE.isKnownPredicate(Pred, LAR->getStart(), RAR->getStart());
}

/// Is LHS `Pred` RHS true on the virtue of LHS or RHS being a Min or Max
/// expression?
static bool IsKnownPredicateViaMinOrMax(ScalarEvolution &SE,
                                        ICmpInst::Predicate Pred,
                                        const SCEV *LHS, const SCEV *RHS) {
  switch (Pred) {
  default:
    return false;

  case ICmpInst::ICMP_SGE:
    std::swap(LHS, RHS);
    LLVM_FALLTHROUGH;
  case ICmpInst::ICMP_SLE:
    return
      // min(A, ...) <= A
      IsMinConsistingOf<SCEVSMaxExpr>(SE, LHS, RHS) ||
      // A <= max(A, ...)
      IsMaxConsistingOf<SCEVSMaxExpr>(RHS, LHS);

  case ICmpInst::ICMP_UGE:
    std::swap(LHS, RHS);
    LLVM_FALLTHROUGH;
  case ICmpInst::ICMP_ULE:
    return
      // min(A, ...) <= A
      IsMinConsistingOf<SCEVUMaxExpr>(SE, LHS, RHS) ||
      // A <= max(A, ...)
      IsMaxConsistingOf<SCEVUMaxExpr>(RHS, LHS);
  }

  llvm_unreachable("covered switch fell through?!");
}

bool ScalarEvolution::isImpliedViaOperations(ICmpInst::Predicate Pred,
                                             const SCEV *LHS, const SCEV *RHS,
                                             const SCEV *FoundLHS,
                                             const SCEV *FoundRHS,
                                             unsigned Depth) {
  assert(getTypeSizeInBits(LHS->getType()) ==
             getTypeSizeInBits(RHS->getType()) &&
         "LHS and RHS have different sizes?");
  assert(getTypeSizeInBits(FoundLHS->getType()) ==
             getTypeSizeInBits(FoundRHS->getType()) &&
         "FoundLHS and FoundRHS have different sizes?");
  // We want to avoid hurting the compile time with analysis of too big trees.
  if (Depth > MaxSCEVOperationsImplicationDepth)
    return false;
  // We only want to work with ICMP_SGT comparison so far.
  // TODO: Extend to ICMP_UGT?
  if (Pred == ICmpInst::ICMP_SLT) {
    Pred = ICmpInst::ICMP_SGT;
    std::swap(LHS, RHS);
    std::swap(FoundLHS, FoundRHS);
  }
  if (Pred != ICmpInst::ICMP_SGT)
    return false;

  auto GetOpFromSExt = [&](const SCEV *S) {
    if (auto *Ext = dyn_cast<SCEVSignExtendExpr>(S))
      return Ext->getOperand();
    // TODO: If S is a SCEVConstant then you can cheaply "strip" the sext off
    // the constant in some cases.
    return S;
  };

  // Acquire values from extensions.
  auto *OrigLHS = LHS;
  auto *OrigFoundLHS = FoundLHS;
  LHS = GetOpFromSExt(LHS);
  FoundLHS = GetOpFromSExt(FoundLHS);

  // Is the SGT predicate can be proved trivially or using the found context.
  auto IsSGTViaContext = [&](const SCEV *S1, const SCEV *S2) {
    return isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_SGT, S1, S2) ||
           isImpliedViaOperations(ICmpInst::ICMP_SGT, S1, S2, OrigFoundLHS,
                                  FoundRHS, Depth + 1);
  };

  if (auto *LHSAddExpr = dyn_cast<SCEVAddExpr>(LHS)) {
    // We want to avoid creation of any new non-constant SCEV. Since we are
    // going to compare the operands to RHS, we should be certain that we don't
    // need any size extensions for this. So let's decline all cases when the
    // sizes of types of LHS and RHS do not match.
    // TODO: Maybe try to get RHS from sext to catch more cases?
    if (getTypeSizeInBits(LHS->getType()) != getTypeSizeInBits(RHS->getType()))
      return false;

    // Should not overflow.
    if (!LHSAddExpr->hasNoSignedWrap())
      return false;

    auto *LL = LHSAddExpr->getOperand(0);
    auto *LR = LHSAddExpr->getOperand(1);
    auto *MinusOne = getNegativeSCEV(getOne(RHS->getType()));

    // Checks that S1 >= 0 && S2 > RHS, trivially or using the found context.
    auto IsSumGreaterThanRHS = [&](const SCEV *S1, const SCEV *S2) {
      return IsSGTViaContext(S1, MinusOne) && IsSGTViaContext(S2, RHS);
    };
    // Try to prove the following rule:
    // (LHS = LL + LR) && (LL >= 0) && (LR > RHS) => (LHS > RHS).
    // (LHS = LL + LR) && (LR >= 0) && (LL > RHS) => (LHS > RHS).
    if (IsSumGreaterThanRHS(LL, LR) || IsSumGreaterThanRHS(LR, LL))
      return true;
  } else if (auto *LHSUnknownExpr = dyn_cast<SCEVUnknown>(LHS)) {
    Value *LL, *LR;
    // FIXME: Once we have SDiv implemented, we can get rid of this matching.

    using namespace llvm::PatternMatch;

    if (match(LHSUnknownExpr->getValue(), m_SDiv(m_Value(LL), m_Value(LR)))) {
      // Rules for division.
      // We are going to perform some comparisons with Denominator and its
      // derivative expressions. In general case, creating a SCEV for it may
      // lead to a complex analysis of the entire graph, and in particular it
      // can request trip count recalculation for the same loop. This would
      // cache as SCEVCouldNotCompute to avoid the infinite recursion. To avoid
      // this, we only want to create SCEVs that are constants in this section.
      // So we bail if Denominator is not a constant.
      if (!isa<ConstantInt>(LR))
        return false;

      auto *Denominator = cast<SCEVConstant>(getSCEV(LR));

      // We want to make sure that LHS = FoundLHS / Denominator. If it is so,
      // then a SCEV for the numerator already exists and matches with FoundLHS.
      auto *Numerator = getExistingSCEV(LL);
      if (!Numerator || Numerator->getType() != FoundLHS->getType())
        return false;

      // Make sure that the numerator matches with FoundLHS and the denominator
      // is positive.
      if (!HasSameValue(Numerator, FoundLHS) || !isKnownPositive(Denominator))
        return false;

      auto *DTy = Denominator->getType();
      auto *FRHSTy = FoundRHS->getType();
      if (DTy->isPointerTy() != FRHSTy->isPointerTy())
        // One of types is a pointer and another one is not. We cannot extend
        // them properly to a wider type, so let us just reject this case.
        // TODO: Usage of getEffectiveSCEVType for DTy, FRHSTy etc should help
        // to avoid this check.
        return false;

      // Given that:
      // FoundLHS > FoundRHS, LHS = FoundLHS / Denominator, Denominator > 0.
      auto *WTy = getWiderType(DTy, FRHSTy);
      auto *DenominatorExt = getNoopOrSignExtend(Denominator, WTy);
      auto *FoundRHSExt = getNoopOrSignExtend(FoundRHS, WTy);

      // Try to prove the following rule:
      // (FoundRHS > Denominator - 2) && (RHS <= 0) => (LHS > RHS).
      // For example, given that FoundLHS > 2. It means that FoundLHS is at
      // least 3. If we divide it by Denominator < 4, we will have at least 1.
      auto *DenomMinusTwo = getMinusSCEV(DenominatorExt, getConstant(WTy, 2));
      if (isKnownNonPositive(RHS) &&
          IsSGTViaContext(FoundRHSExt, DenomMinusTwo))
        return true;

      // Try to prove the following rule:
      // (FoundRHS > -1 - Denominator) && (RHS < 0) => (LHS > RHS).
      // For example, given that FoundLHS > -3. Then FoundLHS is at least -2.
      // If we divide it by Denominator > 2, then:
      // 1. If FoundLHS is negative, then the result is 0.
      // 2. If FoundLHS is non-negative, then the result is non-negative.
      // Anyways, the result is non-negative.
      auto *MinusOne = getNegativeSCEV(getOne(WTy));
      auto *NegDenomMinusOne = getMinusSCEV(MinusOne, DenominatorExt);
      if (isKnownNegative(RHS) &&
          IsSGTViaContext(FoundRHSExt, NegDenomMinusOne))
        return true;
    }
  }

  // If our expression contained SCEVUnknown Phis, and we split it down and now
  // need to prove something for them, try to prove the predicate for every
  // possible incoming values of those Phis.
  if (isImpliedViaMerge(Pred, OrigLHS, RHS, OrigFoundLHS, FoundRHS, Depth + 1))
    return true;

  return false;
}

bool
ScalarEvolution::isKnownViaNonRecursiveReasoning(ICmpInst::Predicate Pred,
                                           const SCEV *LHS, const SCEV *RHS) {
  return isKnownPredicateViaConstantRanges(Pred, LHS, RHS) ||
         IsKnownPredicateViaMinOrMax(*this, Pred, LHS, RHS) ||
         IsKnownPredicateViaAddRecStart(*this, Pred, LHS, RHS) ||
         isKnownPredicateViaNoOverflow(Pred, LHS, RHS);
}

bool
ScalarEvolution::isImpliedCondOperandsHelper(ICmpInst::Predicate Pred,
                                             const SCEV *LHS, const SCEV *RHS,
                                             const SCEV *FoundLHS,
                                             const SCEV *FoundRHS) {
  switch (Pred) {
  default: llvm_unreachable("Unexpected ICmpInst::Predicate value!");
  case ICmpInst::ICMP_EQ:
  case ICmpInst::ICMP_NE:
    if (HasSameValue(LHS, FoundLHS) && HasSameValue(RHS, FoundRHS))
      return true;
    break;
  case ICmpInst::ICMP_SLT:
  case ICmpInst::ICMP_SLE:
    if (isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_SLE, LHS, FoundLHS) &&
        isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_SGE, RHS, FoundRHS))
      return true;
    break;
  case ICmpInst::ICMP_SGT:
  case ICmpInst::ICMP_SGE:
    if (isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_SGE, LHS, FoundLHS) &&
        isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_SLE, RHS, FoundRHS))
      return true;
    break;
  case ICmpInst::ICMP_ULT:
  case ICmpInst::ICMP_ULE:
    if (isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_ULE, LHS, FoundLHS) &&
        isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_UGE, RHS, FoundRHS))
      return true;
    break;
  case ICmpInst::ICMP_UGT:
  case ICmpInst::ICMP_UGE:
    if (isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_UGE, LHS, FoundLHS) &&
        isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_ULE, RHS, FoundRHS))
      return true;
    break;
  }

  // Maybe it can be proved via operations?
  if (isImpliedViaOperations(Pred, LHS, RHS, FoundLHS, FoundRHS))
    return true;

  return false;
}

bool ScalarEvolution::isImpliedCondOperandsViaRanges(ICmpInst::Predicate Pred,
                                                     const SCEV *LHS,
                                                     const SCEV *RHS,
                                                     const SCEV *FoundLHS,
                                                     const SCEV *FoundRHS) {
  if (!isa<SCEVConstant>(RHS) || !isa<SCEVConstant>(FoundRHS))
    // The restriction on `FoundRHS` be lifted easily -- it exists only to
    // reduce the compile time impact of this optimization.
    return false;

  Optional<APInt> Addend = computeConstantDifference(LHS, FoundLHS);
  if (!Addend)
    return false;

  const APInt &ConstFoundRHS = cast<SCEVConstant>(FoundRHS)->getAPInt();

  // `FoundLHSRange` is the range we know `FoundLHS` to be in by virtue of the
  // antecedent "`FoundLHS` `Pred` `FoundRHS`".
  ConstantRange FoundLHSRange =
      ConstantRange::makeAllowedICmpRegion(Pred, ConstFoundRHS);

  // Since `LHS` is `FoundLHS` + `Addend`, we can compute a range for `LHS`:
  ConstantRange LHSRange = FoundLHSRange.add(ConstantRange(*Addend));

  // We can also compute the range of values for `LHS` that satisfy the
  // consequent, "`LHS` `Pred` `RHS`":
  const APInt &ConstRHS = cast<SCEVConstant>(RHS)->getAPInt();
  ConstantRange SatisfyingLHSRange =
      ConstantRange::makeSatisfyingICmpRegion(Pred, ConstRHS);

  // The antecedent implies the consequent if every value of `LHS` that
  // satisfies the antecedent also satisfies the consequent.
  return SatisfyingLHSRange.contains(LHSRange);
}

bool ScalarEvolution::doesIVOverflowOnLT(const SCEV *RHS, const SCEV *Stride,
                                         bool IsSigned, bool NoWrap) {
  assert(isKnownPositive(Stride) && "Positive stride expected!");

  if (NoWrap) return false;

  unsigned BitWidth = getTypeSizeInBits(RHS->getType());
  const SCEV *One = getOne(Stride->getType());

  if (IsSigned) {
    APInt MaxRHS = getSignedRangeMax(RHS);
    APInt MaxValue = APInt::getSignedMaxValue(BitWidth);
    APInt MaxStrideMinusOne = getSignedRangeMax(getMinusSCEV(Stride, One));

    // SMaxRHS + SMaxStrideMinusOne > SMaxValue => overflow!
    return (std::move(MaxValue) - MaxStrideMinusOne).slt(MaxRHS);
  }

  APInt MaxRHS = getUnsignedRangeMax(RHS);
  APInt MaxValue = APInt::getMaxValue(BitWidth);
  APInt MaxStrideMinusOne = getUnsignedRangeMax(getMinusSCEV(Stride, One));

  // UMaxRHS + UMaxStrideMinusOne > UMaxValue => overflow!
  return (std::move(MaxValue) - MaxStrideMinusOne).ult(MaxRHS);
}

bool ScalarEvolution::doesIVOverflowOnGT(const SCEV *RHS, const SCEV *Stride,
                                         bool IsSigned, bool NoWrap) {
  if (NoWrap) return false;

  unsigned BitWidth = getTypeSizeInBits(RHS->getType());
  const SCEV *One = getOne(Stride->getType());

  if (IsSigned) {
    APInt MinRHS = getSignedRangeMin(RHS);
    APInt MinValue = APInt::getSignedMinValue(BitWidth);
    APInt MaxStrideMinusOne = getSignedRangeMax(getMinusSCEV(Stride, One));

    // SMinRHS - SMaxStrideMinusOne < SMinValue => overflow!
    return (std::move(MinValue) + MaxStrideMinusOne).sgt(MinRHS);
  }

  APInt MinRHS = getUnsignedRangeMin(RHS);
  APInt MinValue = APInt::getMinValue(BitWidth);
  APInt MaxStrideMinusOne = getUnsignedRangeMax(getMinusSCEV(Stride, One));

  // UMinRHS - UMaxStrideMinusOne < UMinValue => overflow!
  return (std::move(MinValue) + MaxStrideMinusOne).ugt(MinRHS);
}

const SCEV *ScalarEvolution::computeBECount(const SCEV *Delta, const SCEV *Step,
                                            bool Equality) {
  const SCEV *One = getOne(Step->getType());
  Delta = Equality ? getAddExpr(Delta, Step)
                   : getAddExpr(Delta, getMinusSCEV(Step, One));
  return getUDivExpr(Delta, Step);
}

const SCEV *ScalarEvolution::computeMaxBECountForLT(const SCEV *Start,
                                                    const SCEV *Stride,
                                                    const SCEV *End,
                                                    unsigned BitWidth,
                                                    bool IsSigned) {

  assert(!isKnownNonPositive(Stride) &&
         "Stride is expected strictly positive!");
  // Calculate the maximum backedge count based on the range of values
  // permitted by Start, End, and Stride.
  const SCEV *MaxBECount;
  APInt MinStart =
      IsSigned ? getSignedRangeMin(Start) : getUnsignedRangeMin(Start);

  APInt StrideForMaxBECount =
      IsSigned ? getSignedRangeMin(Stride) : getUnsignedRangeMin(Stride);

  // We already know that the stride is positive, so we paper over conservatism
  // in our range computation by forcing StrideForMaxBECount to be at least one.
  // In theory this is unnecessary, but we expect MaxBECount to be a
  // SCEVConstant, and (udiv <constant> 0) is not constant folded by SCEV (there
  // is nothing to constant fold it to).
  APInt One(BitWidth, 1, IsSigned);
  StrideForMaxBECount = APIntOps::smax(One, StrideForMaxBECount);

  APInt MaxValue = IsSigned ? APInt::getSignedMaxValue(BitWidth)
                            : APInt::getMaxValue(BitWidth);
  APInt Limit = MaxValue - (StrideForMaxBECount - 1);

  // Although End can be a MAX expression we estimate MaxEnd considering only
  // the case End = RHS of the loop termination condition. This is safe because
  // in the other case (End - Start) is zero, leading to a zero maximum backedge
  // taken count.
  APInt MaxEnd = IsSigned ? APIntOps::smin(getSignedRangeMax(End), Limit)
                          : APIntOps::umin(getUnsignedRangeMax(End), Limit);

  MaxBECount = computeBECount(getConstant(MaxEnd - MinStart) /* Delta */,
                              getConstant(StrideForMaxBECount) /* Step */,
                              false /* Equality */);

  return MaxBECount;
}

ScalarEvolution::ExitLimit
ScalarEvolution::howManyLessThans(const SCEV *LHS, const SCEV *RHS,
                                  const Loop *L, bool IsSigned,
                                  bool ControlsExit, bool AllowPredicates) {
  SmallPtrSet<const SCEVPredicate *, 4> Predicates;

  const SCEVAddRecExpr *IV = dyn_cast<SCEVAddRecExpr>(LHS);
  bool PredicatedIV = false;

  if (!IV && AllowPredicates) {
    // Try to make this an AddRec using runtime tests, in the first X
    // iterations of this loop, where X is the SCEV expression found by the
    // algorithm below.
    IV = convertSCEVToAddRecWithPredicates(LHS, L, Predicates);
    PredicatedIV = true;
  }

  // Avoid weird loops
  if (!IV || IV->getLoop() != L || !IV->isAffine())
    return getCouldNotCompute();

  bool NoWrap = ControlsExit &&
                IV->getNoWrapFlags(IsSigned ? SCEV::FlagNSW : SCEV::FlagNUW);

  const SCEV *Stride = IV->getStepRecurrence(*this);

  bool PositiveStride = isKnownPositive(Stride);

  // Avoid negative or zero stride values.
  if (!PositiveStride) {
    // We can compute the correct backedge taken count for loops with unknown
    // strides if we can prove that the loop is not an infinite loop with side
    // effects. Here's the loop structure we are trying to handle -
    //
    // i = start
    // do {
    //   A[i] = i;
    //   i += s;
    // } while (i < end);
    //
    // The backedge taken count for such loops is evaluated as -
    // (max(end, start + stride) - start - 1) /u stride
    //
    // The additional preconditions that we need to check to prove correctness
    // of the above formula is as follows -
    //
    // a) IV is either nuw or nsw depending upon signedness (indicated by the
    //    NoWrap flag).
    // b) loop is single exit with no side effects.
    //
    //
    // Precondition a) implies that if the stride is negative, this is a single
    // trip loop. The backedge taken count formula reduces to zero in this case.
    //
    // Precondition b) implies that the unknown stride cannot be zero otherwise
    // we have UB.
    //
    // The positive stride case is the same as isKnownPositive(Stride) returning
    // true (original behavior of the function).
    //
    // We want to make sure that the stride is truly unknown as there are edge
    // cases where ScalarEvolution propagates no wrap flags to the
    // post-increment/decrement IV even though the increment/decrement operation
    // itself is wrapping. The computed backedge taken count may be wrong in
    // such cases. This is prevented by checking that the stride is not known to
    // be either positive or non-positive. For example, no wrap flags are
    // propagated to the post-increment IV of this loop with a trip count of 2 -
    //
    // unsigned char i;
    // for(i=127; i<128; i+=129)
    //   A[i] = i;
    //
    if (PredicatedIV || !NoWrap || isKnownNonPositive(Stride) ||
        !loopHasNoSideEffects(L))
      return getCouldNotCompute();
  } else if (!Stride->isOne() &&
             doesIVOverflowOnLT(RHS, Stride, IsSigned, NoWrap))
    // Avoid proven overflow cases: this will ensure that the backedge taken
    // count will not generate any unsigned overflow. Relaxed no-overflow
    // conditions exploit NoWrapFlags, allowing to optimize in presence of
    // undefined behaviors like the case of C language.
    return getCouldNotCompute();

  ICmpInst::Predicate Cond = IsSigned ? ICmpInst::ICMP_SLT
                                      : ICmpInst::ICMP_ULT;
  const SCEV *Start = IV->getStart();
  const SCEV *End = RHS;
  // When the RHS is not invariant, we do not know the end bound of the loop and
  // cannot calculate the ExactBECount needed by ExitLimit. However, we can
  // calculate the MaxBECount, given the start, stride and max value for the end
  // bound of the loop (RHS), and the fact that IV does not overflow (which is
  // checked above).
  if (!isLoopInvariant(RHS, L)) {
    const SCEV *MaxBECount = computeMaxBECountForLT(
        Start, Stride, RHS, getTypeSizeInBits(LHS->getType()), IsSigned);
    return ExitLimit(getCouldNotCompute() /* ExactNotTaken */, MaxBECount,
                     false /*MaxOrZero*/, Predicates);
  }
  // If the backedge is taken at least once, then it will be taken
  // (End-Start)/Stride times (rounded up to a multiple of Stride), where Start
  // is the LHS value of the less-than comparison the first time it is evaluated
  // and End is the RHS.
  const SCEV *BECountIfBackedgeTaken =
    computeBECount(getMinusSCEV(End, Start), Stride, false);
  // If the loop entry is guarded by the result of the backedge test of the
  // first loop iteration, then we know the backedge will be taken at least
  // once and so the backedge taken count is as above. If not then we use the
  // expression (max(End,Start)-Start)/Stride to describe the backedge count,
  // as if the backedge is taken at least once max(End,Start) is End and so the
  // result is as above, and if not max(End,Start) is Start so we get a backedge
  // count of zero.
  const SCEV *BECount;
  if (isLoopEntryGuardedByCond(L, Cond, getMinusSCEV(Start, Stride), RHS))
    BECount = BECountIfBackedgeTaken;
  else {
    End = IsSigned ? getSMaxExpr(RHS, Start) : getUMaxExpr(RHS, Start);
    BECount = computeBECount(getMinusSCEV(End, Start), Stride, false);
  }

  const SCEV *MaxBECount;
  bool MaxOrZero = false;
  if (isa<SCEVConstant>(BECount))
    MaxBECount = BECount;
  else if (isa<SCEVConstant>(BECountIfBackedgeTaken)) {
    // If we know exactly how many times the backedge will be taken if it's
    // taken at least once, then the backedge count will either be that or
    // zero.
    MaxBECount = BECountIfBackedgeTaken;
    MaxOrZero = true;
  } else {
    MaxBECount = computeMaxBECountForLT(
        Start, Stride, RHS, getTypeSizeInBits(LHS->getType()), IsSigned);
  }

  if (isa<SCEVCouldNotCompute>(MaxBECount) &&
      !isa<SCEVCouldNotCompute>(BECount))
    MaxBECount = getConstant(getUnsignedRangeMax(BECount));

  return ExitLimit(BECount, MaxBECount, MaxOrZero, Predicates);
}

ScalarEvolution::ExitLimit
ScalarEvolution::howManyGreaterThans(const SCEV *LHS, const SCEV *RHS,
                                     const Loop *L, bool IsSigned,
                                     bool ControlsExit, bool AllowPredicates) {
  SmallPtrSet<const SCEVPredicate *, 4> Predicates;
  // We handle only IV > Invariant
  if (!isLoopInvariant(RHS, L))
    return getCouldNotCompute();

  const SCEVAddRecExpr *IV = dyn_cast<SCEVAddRecExpr>(LHS);
  if (!IV && AllowPredicates)
    // Try to make this an AddRec using runtime tests, in the first X
    // iterations of this loop, where X is the SCEV expression found by the
    // algorithm below.
    IV = convertSCEVToAddRecWithPredicates(LHS, L, Predicates);

  // Avoid weird loops
  if (!IV || IV->getLoop() != L || !IV->isAffine())
    return getCouldNotCompute();

  bool NoWrap = ControlsExit &&
                IV->getNoWrapFlags(IsSigned ? SCEV::FlagNSW : SCEV::FlagNUW);

  const SCEV *Stride = getNegativeSCEV(IV->getStepRecurrence(*this));

  // Avoid negative or zero stride values
  if (!isKnownPositive(Stride))
    return getCouldNotCompute();

  // Avoid proven overflow cases: this will ensure that the backedge taken count
  // will not generate any unsigned overflow. Relaxed no-overflow conditions
  // exploit NoWrapFlags, allowing to optimize in presence of undefined
  // behaviors like the case of C language.
  if (!Stride->isOne() && doesIVOverflowOnGT(RHS, Stride, IsSigned, NoWrap))
    return getCouldNotCompute();

  ICmpInst::Predicate Cond = IsSigned ? ICmpInst::ICMP_SGT
                                      : ICmpInst::ICMP_UGT;

  const SCEV *Start = IV->getStart();
  const SCEV *End = RHS;
  if (!isLoopEntryGuardedByCond(L, Cond, getAddExpr(Start, Stride), RHS))
    End = IsSigned ? getSMinExpr(RHS, Start) : getUMinExpr(RHS, Start);

  const SCEV *BECount = computeBECount(getMinusSCEV(Start, End), Stride, false);

  APInt MaxStart = IsSigned ? getSignedRangeMax(Start)
                            : getUnsignedRangeMax(Start);

  APInt MinStride = IsSigned ? getSignedRangeMin(Stride)
                             : getUnsignedRangeMin(Stride);

  unsigned BitWidth = getTypeSizeInBits(LHS->getType());
  APInt Limit = IsSigned ? APInt::getSignedMinValue(BitWidth) + (MinStride - 1)
                         : APInt::getMinValue(BitWidth) + (MinStride - 1);

  // Although End can be a MIN expression we estimate MinEnd considering only
  // the case End = RHS. This is safe because in the other case (Start - End)
  // is zero, leading to a zero maximum backedge taken count.
  APInt MinEnd =
    IsSigned ? APIntOps::smax(getSignedRangeMin(RHS), Limit)
             : APIntOps::umax(getUnsignedRangeMin(RHS), Limit);


  const SCEV *MaxBECount = getCouldNotCompute();
  if (isa<SCEVConstant>(BECount))
    MaxBECount = BECount;
  else
    MaxBECount = computeBECount(getConstant(MaxStart - MinEnd),
                                getConstant(MinStride), false);

  if (isa<SCEVCouldNotCompute>(MaxBECount))
    MaxBECount = BECount;

  return ExitLimit(BECount, MaxBECount, false, Predicates);
}

const SCEV *SCEVAddRecExpr::getNumIterationsInRange(const ConstantRange &Range,
                                                    ScalarEvolution &SE) const {
  if (Range.isFullSet())  // Infinite loop.
    return SE.getCouldNotCompute();

  // If the start is a non-zero constant, shift the range to simplify things.
  if (const SCEVConstant *SC = dyn_cast<SCEVConstant>(getStart()))
    if (!SC->getValue()->isZero()) {
      SmallVector<const SCEV *, 4> Operands(op_begin(), op_end());
      Operands[0] = SE.getZero(SC->getType());
      const SCEV *Shifted = SE.getAddRecExpr(Operands, getLoop(),
                                             getNoWrapFlags(FlagNW));
      if (const auto *ShiftedAddRec = dyn_cast<SCEVAddRecExpr>(Shifted))
        return ShiftedAddRec->getNumIterationsInRange(
            Range.subtract(SC->getAPInt()), SE);
      // This is strange and shouldn't happen.
      return SE.getCouldNotCompute();
    }

  // The only time we can solve this is when we have all constant indices.
  // Otherwise, we cannot determine the overflow conditions.
  if (any_of(operands(), [](const SCEV *Op) { return !isa<SCEVConstant>(Op); }))
    return SE.getCouldNotCompute();

  // Okay at this point we know that all elements of the chrec are constants and
  // that the start element is zero.

  // First check to see if the range contains zero.  If not, the first
  // iteration exits.
  unsigned BitWidth = SE.getTypeSizeInBits(getType());
  if (!Range.contains(APInt(BitWidth, 0)))
    return SE.getZero(getType());

  if (isAffine()) {
    // If this is an affine expression then we have this situation:
    //   Solve {0,+,A} in Range  ===  Ax in Range

    // We know that zero is in the range.  If A is positive then we know that
    // the upper value of the range must be the first possible exit value.
    // If A is negative then the lower of the range is the last possible loop
    // value.  Also note that we already checked for a full range.
    APInt A = cast<SCEVConstant>(getOperand(1))->getAPInt();
    APInt End = A.sge(1) ? (Range.getUpper() - 1) : Range.getLower();

    // The exit value should be (End+A)/A.
    APInt ExitVal = (End + A).udiv(A);
    ConstantInt *ExitValue = ConstantInt::get(SE.getContext(), ExitVal);

    // Evaluate at the exit value.  If we really did fall out of the valid
    // range, then we computed our trip count, otherwise wrap around or other
    // things must have happened.
    ConstantInt *Val = EvaluateConstantChrecAtConstant(this, ExitValue, SE);
    if (Range.contains(Val->getValue()))
      return SE.getCouldNotCompute();  // Something strange happened

    // Ensure that the previous value is in the range.  This is a sanity check.
    assert(Range.contains(
           EvaluateConstantChrecAtConstant(this,
           ConstantInt::get(SE.getContext(), ExitVal - 1), SE)->getValue()) &&
           "Linear scev computation is off in a bad way!");
    return SE.getConstant(ExitValue);
  }

  if (isQuadratic()) {
    if (auto S = SolveQuadraticAddRecRange(this, Range, SE))
      return SE.getConstant(S.getValue());
  }

  return SE.getCouldNotCompute();
}

const SCEVAddRecExpr *
SCEVAddRecExpr::getPostIncExpr(ScalarEvolution &SE) const {
  assert(getNumOperands() > 1 && "AddRec with zero step?");
  // There is a temptation to just call getAddExpr(this, getStepRecurrence(SE)),
  // but in this case we cannot guarantee that the value returned will be an
  // AddRec because SCEV does not have a fixed point where it stops
  // simplification: it is legal to return ({rec1} + {rec2}). For example, it
  // may happen if we reach arithmetic depth limit while simplifying. So we
  // construct the returned value explicitly.
  SmallVector<const SCEV *, 3> Ops;
  // If this is {A,+,B,+,C,...,+,N}, then its step is {B,+,C,+,...,+,N}, and
  // (this + Step) is {A+B,+,B+C,+...,+,N}.
  for (unsigned i = 0, e = getNumOperands() - 1; i < e; ++i)
    Ops.push_back(SE.getAddExpr(getOperand(i), getOperand(i + 1)));
  // We know that the last operand is not a constant zero (otherwise it would
  // have been popped out earlier). This guarantees us that if the result has
  // the same last operand, then it will also not be popped out, meaning that
  // the returned value will be an AddRec.
  const SCEV *Last = getOperand(getNumOperands() - 1);
  assert(!Last->isZero() && "Recurrency with zero step?");
  Ops.push_back(Last);
  return cast<SCEVAddRecExpr>(SE.getAddRecExpr(Ops, getLoop(),
                                               SCEV::FlagAnyWrap));
}

// Return true when S contains at least an undef value.
static inline bool containsUndefs(const SCEV *S) {
  return SCEVExprContains(S, [](const SCEV *S) {
    if (const auto *SU = dyn_cast<SCEVUnknown>(S))
      return isa<UndefValue>(SU->getValue());
    else if (const auto *SC = dyn_cast<SCEVConstant>(S))
      return isa<UndefValue>(SC->getValue());
    return false;
  });
}

namespace {

// Collect all steps of SCEV expressions.
struct SCEVCollectStrides {
  ScalarEvolution &SE;
  SmallVectorImpl<const SCEV *> &Strides;

  SCEVCollectStrides(ScalarEvolution &SE, SmallVectorImpl<const SCEV *> &S)
      : SE(SE), Strides(S) {}

  bool follow(const SCEV *S) {
    if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(S))
      Strides.push_back(AR->getStepRecurrence(SE));
    return true;
  }

  bool isDone() const { return false; }
};

// Collect all SCEVUnknown and SCEVMulExpr expressions.
struct SCEVCollectTerms {
  SmallVectorImpl<const SCEV *> &Terms;

  SCEVCollectTerms(SmallVectorImpl<const SCEV *> &T) : Terms(T) {}

  bool follow(const SCEV *S) {
    if (isa<SCEVUnknown>(S) || isa<SCEVMulExpr>(S) ||
        isa<SCEVSignExtendExpr>(S)) {
      if (!containsUndefs(S))
        Terms.push_back(S);

      // Stop recursion: once we collected a term, do not walk its operands.
      return false;
    }

    // Keep looking.
    return true;
  }

  bool isDone() const { return false; }
};

// Check if a SCEV contains an AddRecExpr.
struct SCEVHasAddRec {
  bool &ContainsAddRec;

  SCEVHasAddRec(bool &ContainsAddRec) : ContainsAddRec(ContainsAddRec) {
    ContainsAddRec = false;
  }

  bool follow(const SCEV *S) {
    if (isa<SCEVAddRecExpr>(S)) {
      ContainsAddRec = true;

      // Stop recursion: once we collected a term, do not walk its operands.
      return false;
    }

    // Keep looking.
    return true;
  }

  bool isDone() const { return false; }
};

// Find factors that are multiplied with an expression that (possibly as a
// subexpression) contains an AddRecExpr. In the expression:
//
//  8 * (100 +  %p * %q * (%a + {0, +, 1}_loop))
//
// "%p * %q" are factors multiplied by the expression "(%a + {0, +, 1}_loop)"
// that contains the AddRec {0, +, 1}_loop. %p * %q are likely to be array size
// parameters as they form a product with an induction variable.
//
// This collector expects all array size parameters to be in the same MulExpr.
// It might be necessary to later add support for collecting parameters that are
// spread over different nested MulExpr.
struct SCEVCollectAddRecMultiplies {
  SmallVectorImpl<const SCEV *> &Terms;
  ScalarEvolution &SE;

  SCEVCollectAddRecMultiplies(SmallVectorImpl<const SCEV *> &T, ScalarEvolution &SE)
      : Terms(T), SE(SE) {}

  bool follow(const SCEV *S) {
    if (auto *Mul = dyn_cast<SCEVMulExpr>(S)) {
      bool HasAddRec = false;
      SmallVector<const SCEV *, 0> Operands;
      for (auto Op : Mul->operands()) {
        const SCEVUnknown *Unknown = dyn_cast<SCEVUnknown>(Op);
        if (Unknown && !isa<CallInst>(Unknown->getValue())) {
          Operands.push_back(Op);
        } else if (Unknown) {
          HasAddRec = true;
        } else {
          bool ContainsAddRec;
          SCEVHasAddRec ContiansAddRec(ContainsAddRec);
          visitAll(Op, ContiansAddRec);
          HasAddRec |= ContainsAddRec;
        }
      }
      if (Operands.size() == 0)
        return true;

      if (!HasAddRec)
        return false;

      Terms.push_back(SE.getMulExpr(Operands));
      // Stop recursion: once we collected a term, do not walk its operands.
      return false;
    }

    // Keep looking.
    return true;
  }

  bool isDone() const { return false; }
};

} // end anonymous namespace

/// Find parametric terms in this SCEVAddRecExpr. We first for parameters in
/// two places:
///   1) The strides of AddRec expressions.
///   2) Unknowns that are multiplied with AddRec expressions.
void ScalarEvolution::collectParametricTerms(const SCEV *Expr,
    SmallVectorImpl<const SCEV *> &Terms) {
  SmallVector<const SCEV *, 4> Strides;
  SCEVCollectStrides StrideCollector(*this, Strides);
  visitAll(Expr, StrideCollector);

  LLVM_DEBUG({
    dbgs() << "Strides:\n";
    for (const SCEV *S : Strides)
      dbgs() << *S << "\n";
  });

  for (const SCEV *S : Strides) {
    SCEVCollectTerms TermCollector(Terms);
    visitAll(S, TermCollector);
  }

  LLVM_DEBUG({
    dbgs() << "Terms:\n";
    for (const SCEV *T : Terms)
      dbgs() << *T << "\n";
  });

  SCEVCollectAddRecMultiplies MulCollector(Terms, *this);
  visitAll(Expr, MulCollector);
}

static bool findArrayDimensionsRec(ScalarEvolution &SE,
                                   SmallVectorImpl<const SCEV *> &Terms,
                                   SmallVectorImpl<const SCEV *> &Sizes) {
  int Last = Terms.size() - 1;
  const SCEV *Step = Terms[Last];

  // End of recursion.
  if (Last == 0) {
    if (const SCEVMulExpr *M = dyn_cast<SCEVMulExpr>(Step)) {
      SmallVector<const SCEV *, 2> Qs;
      for (const SCEV *Op : M->operands())
        if (!isa<SCEVConstant>(Op))
          Qs.push_back(Op);

      Step = SE.getMulExpr(Qs);
    }

    Sizes.push_back(Step);
    return true;
  }

  for (const SCEV *&Term : Terms) {
    // Normalize the terms before the next call to findArrayDimensionsRec.
    const SCEV *Q, *R;
    SCEVDivision::divide(SE, Term, Step, &Q, &R);

    // Bail out when GCD does not evenly divide one of the terms.
    if (!R->isZero())
      return false;

    Term = Q;
  }

  // Remove all SCEVConstants.
  Terms.erase(
      remove_if(Terms, [](const SCEV *E) { return isa<SCEVConstant>(E); }),
      Terms.end());

  if (Terms.size() > 0)
    if (!findArrayDimensionsRec(SE, Terms, Sizes))
      return false;

  Sizes.push_back(Step);
  return true;
}

// Returns true when one of the SCEVs of Terms contains a SCEVUnknown parameter.
static inline bool containsParameters(SmallVectorImpl<const SCEV *> &Terms) {
  for (const SCEV *T : Terms)
    if (SCEVExprContains(T, isa<SCEVUnknown, const SCEV *>))
      return true;
  return false;
}

// Return the number of product terms in S.
static inline int numberOfTerms(const SCEV *S) {
  if (const SCEVMulExpr *Expr = dyn_cast<SCEVMulExpr>(S))
    return Expr->getNumOperands();
  return 1;
}

static const SCEV *removeConstantFactors(ScalarEvolution &SE, const SCEV *T) {
  if (isa<SCEVConstant>(T))
    return nullptr;

  if (isa<SCEVUnknown>(T))
    return T;

  if (const SCEVMulExpr *M = dyn_cast<SCEVMulExpr>(T)) {
    SmallVector<const SCEV *, 2> Factors;
    for (const SCEV *Op : M->operands())
      if (!isa<SCEVConstant>(Op))
        Factors.push_back(Op);

    return SE.getMulExpr(Factors);
  }

  return T;
}

/// Return the size of an element read or written by Inst.
const SCEV *ScalarEvolution::getElementSize(Instruction *Inst) {
  Type *Ty;
  if (StoreInst *Store = dyn_cast<StoreInst>(Inst))
    Ty = Store->getValueOperand()->getType();
  else if (LoadInst *Load = dyn_cast<LoadInst>(Inst))
    Ty = Load->getType();
  else
    return nullptr;

  Type *ETy = getEffectiveSCEVType(PointerType::getUnqual(Ty));
  return getSizeOfExpr(ETy, Ty);
}

void ScalarEvolution::findArrayDimensions(SmallVectorImpl<const SCEV *> &Terms,
                                          SmallVectorImpl<const SCEV *> &Sizes,
                                          const SCEV *ElementSize) {
  if (Terms.size() < 1 || !ElementSize)
    return;

  // Early return when Terms do not contain parameters: we do not delinearize
  // non parametric SCEVs.
  if (!containsParameters(Terms))
    return;

  LLVM_DEBUG({
    dbgs() << "Terms:\n";
    for (const SCEV *T : Terms)
      dbgs() << *T << "\n";
  });

  // Remove duplicates.
  array_pod_sort(Terms.begin(), Terms.end());
  Terms.erase(std::unique(Terms.begin(), Terms.end()), Terms.end());

  // Put larger terms first.
  llvm::sort(Terms, [](const SCEV *LHS, const SCEV *RHS) {
    return numberOfTerms(LHS) > numberOfTerms(RHS);
  });

  // Try to divide all terms by the element size. If term is not divisible by
  // element size, proceed with the original term.
  for (const SCEV *&Term : Terms) {
    const SCEV *Q, *R;
    SCEVDivision::divide(*this, Term, ElementSize, &Q, &R);
    if (!Q->isZero())
      Term = Q;
  }

  SmallVector<const SCEV *, 4> NewTerms;

  // Remove constant factors.
  for (const SCEV *T : Terms)
    if (const SCEV *NewT = removeConstantFactors(*this, T))
      NewTerms.push_back(NewT);

  LLVM_DEBUG({
    dbgs() << "Terms after sorting:\n";
    for (const SCEV *T : NewTerms)
      dbgs() << *T << "\n";
  });

  if (NewTerms.empty() || !findArrayDimensionsRec(*this, NewTerms, Sizes)) {
    Sizes.clear();
    return;
  }

  // The last element to be pushed into Sizes is the size of an element.
  Sizes.push_back(ElementSize);

  LLVM_DEBUG({
    dbgs() << "Sizes:\n";
    for (const SCEV *S : Sizes)
      dbgs() << *S << "\n";
  });
}

void ScalarEvolution::computeAccessFunctions(
    const SCEV *Expr, SmallVectorImpl<const SCEV *> &Subscripts,
    SmallVectorImpl<const SCEV *> &Sizes) {
  // Early exit in case this SCEV is not an affine multivariate function.
  if (Sizes.empty())
    return;

  if (auto *AR = dyn_cast<SCEVAddRecExpr>(Expr))
    if (!AR->isAffine())
      return;

  const SCEV *Res = Expr;
  int Last = Sizes.size() - 1;
  for (int i = Last; i >= 0; i--) {
    const SCEV *Q, *R;
    SCEVDivision::divide(*this, Res, Sizes[i], &Q, &R);

    LLVM_DEBUG({
      dbgs() << "Res: " << *Res << "\n";
      dbgs() << "Sizes[i]: " << *Sizes[i] << "\n";
      dbgs() << "Res divided by Sizes[i]:\n";
      dbgs() << "Quotient: " << *Q << "\n";
      dbgs() << "Remainder: " << *R << "\n";
    });

    Res = Q;

    // Do not record the last subscript corresponding to the size of elements in
    // the array.
    if (i == Last) {

      // Bail out if the remainder is too complex.
      if (isa<SCEVAddRecExpr>(R)) {
        Subscripts.clear();
        Sizes.clear();
        return;
      }

      continue;
    }

    // Record the access function for the current subscript.
    Subscripts.push_back(R);
  }

  // Also push in last position the remainder of the last division: it will be
  // the access function of the innermost dimension.
  Subscripts.push_back(Res);

  std::reverse(Subscripts.begin(), Subscripts.end());

  LLVM_DEBUG({
    dbgs() << "Subscripts:\n";
    for (const SCEV *S : Subscripts)
      dbgs() << *S << "\n";
  });
}

/// Splits the SCEV into two vectors of SCEVs representing the subscripts and
/// sizes of an array access. Returns the remainder of the delinearization that
/// is the offset start of the array.  The SCEV->delinearize algorithm computes
/// the multiples of SCEV coefficients: that is a pattern matching of sub
/// expressions in the stride and base of a SCEV corresponding to the
/// computation of a GCD (greatest common divisor) of base and stride.  When
/// SCEV->delinearize fails, it returns the SCEV unchanged.
///
/// For example: when analyzing the memory access A[i][j][k] in this loop nest
///
///  void foo(long n, long m, long o, double A[n][m][o]) {
///
///    for (long i = 0; i < n; i++)
///      for (long j = 0; j < m; j++)
///        for (long k = 0; k < o; k++)
///          A[i][j][k] = 1.0;
///  }
///
/// the delinearization input is the following AddRec SCEV:
///
///  AddRec: {{{%A,+,(8 * %m * %o)}<%for.i>,+,(8 * %o)}<%for.j>,+,8}<%for.k>
///
/// From this SCEV, we are able to say that the base offset of the access is %A
/// because it appears as an offset that does not divide any of the strides in
/// the loops:
///
///  CHECK: Base offset: %A
///
/// and then SCEV->delinearize determines the size of some of the dimensions of
/// the array as these are the multiples by which the strides are happening:
///
///  CHECK: ArrayDecl[UnknownSize][%m][%o] with elements of sizeof(double) bytes.
///
/// Note that the outermost dimension remains of UnknownSize because there are
/// no strides that would help identifying the size of the last dimension: when
/// the array has been statically allocated, one could compute the size of that
/// dimension by dividing the overall size of the array by the size of the known
/// dimensions: %m * %o * 8.
///
/// Finally delinearize provides the access functions for the array reference
/// that does correspond to A[i][j][k] of the above C testcase:
///
///  CHECK: ArrayRef[{0,+,1}<%for.i>][{0,+,1}<%for.j>][{0,+,1}<%for.k>]
///
/// The testcases are checking the output of a function pass:
/// DelinearizationPass that walks through all loads and stores of a function
/// asking for the SCEV of the memory access with respect to all enclosing
/// loops, calling SCEV->delinearize on that and printing the results.
void ScalarEvolution::delinearize(const SCEV *Expr,
                                 SmallVectorImpl<const SCEV *> &Subscripts,
                                 SmallVectorImpl<const SCEV *> &Sizes,
                                 const SCEV *ElementSize) {
  // First step: collect parametric terms.
  SmallVector<const SCEV *, 4> Terms;
  collectParametricTerms(Expr, Terms);

  if (Terms.empty())
    return;

  // Second step: find subscript sizes.
  findArrayDimensions(Terms, Sizes, ElementSize);

  if (Sizes.empty())
    return;

  // Third step: compute the access functions for each subscript.
  computeAccessFunctions(Expr, Subscripts, Sizes);

  if (Subscripts.empty())
    return;

  LLVM_DEBUG({
    dbgs() << "succeeded to delinearize " << *Expr << "\n";
    dbgs() << "ArrayDecl[UnknownSize]";
    for (const SCEV *S : Sizes)
      dbgs() << "[" << *S << "]";

    dbgs() << "\nArrayRef";
    for (const SCEV *S : Subscripts)
      dbgs() << "[" << *S << "]";
    dbgs() << "\n";
  });
}

//===----------------------------------------------------------------------===//
//                   SCEVCallbackVH Class Implementation
//===----------------------------------------------------------------------===//

void ScalarEvolution::SCEVCallbackVH::deleted() {
  assert(SE && "SCEVCallbackVH called with a null ScalarEvolution!");
  if (PHINode *PN = dyn_cast<PHINode>(getValPtr()))
    SE->ConstantEvolutionLoopExitValue.erase(PN);
  SE->eraseValueFromMap(getValPtr());
  // this now dangles!
}

void ScalarEvolution::SCEVCallbackVH::allUsesReplacedWith(Value *V) {
  assert(SE && "SCEVCallbackVH called with a null ScalarEvolution!");

  // Forget all the expressions associated with users of the old value,
  // so that future queries will recompute the expressions using the new
  // value.
  Value *Old = getValPtr();
  SmallVector<User *, 16> Worklist(Old->user_begin(), Old->user_end());
  SmallPtrSet<User *, 8> Visited;
  while (!Worklist.empty()) {
    User *U = Worklist.pop_back_val();
    // Deleting the Old value will cause this to dangle. Postpone
    // that until everything else is done.
    if (U == Old)
      continue;
    if (!Visited.insert(U).second)
      continue;
    if (PHINode *PN = dyn_cast<PHINode>(U))
      SE->ConstantEvolutionLoopExitValue.erase(PN);
    SE->eraseValueFromMap(U);
    Worklist.insert(Worklist.end(), U->user_begin(), U->user_end());
  }
  // Delete the Old value.
  if (PHINode *PN = dyn_cast<PHINode>(Old))
    SE->ConstantEvolutionLoopExitValue.erase(PN);
  SE->eraseValueFromMap(Old);
  // this now dangles!
}

ScalarEvolution::SCEVCallbackVH::SCEVCallbackVH(Value *V, ScalarEvolution *se)
  : CallbackVH(V), SE(se) {}

//===----------------------------------------------------------------------===//
//                   ScalarEvolution Class Implementation
//===----------------------------------------------------------------------===//

ScalarEvolution::ScalarEvolution(Function &F, TargetLibraryInfo &TLI,
                                 AssumptionCache &AC, DominatorTree &DT,
                                 LoopInfo &LI)
    : F(F), TLI(TLI), AC(AC), DT(DT), LI(LI),
      CouldNotCompute(new SCEVCouldNotCompute()), ValuesAtScopes(64),
      LoopDispositions(64), BlockDispositions(64) {
  // To use guards for proving predicates, we need to scan every instruction in
  // relevant basic blocks, and not just terminators.  Doing this is a waste of
  // time if the IR does not actually contain any calls to
  // @llvm.experimental.guard, so do a quick check and remember this beforehand.
  //
  // This pessimizes the case where a pass that preserves ScalarEvolution wants
  // to _add_ guards to the module when there weren't any before, and wants
  // ScalarEvolution to optimize based on those guards.  For now we prefer to be
  // efficient in lieu of being smart in that rather obscure case.

  auto *GuardDecl = F.getParent()->getFunction(
      Intrinsic::getName(Intrinsic::experimental_guard));
  HasGuards = GuardDecl && !GuardDecl->use_empty();
}

ScalarEvolution::ScalarEvolution(ScalarEvolution &&Arg)
    : F(Arg.F), HasGuards(Arg.HasGuards), TLI(Arg.TLI), AC(Arg.AC), DT(Arg.DT),
      LI(Arg.LI), CouldNotCompute(std::move(Arg.CouldNotCompute)),
      ValueExprMap(std::move(Arg.ValueExprMap)),
      PendingLoopPredicates(std::move(Arg.PendingLoopPredicates)),
      PendingPhiRanges(std::move(Arg.PendingPhiRanges)),
      PendingMerges(std::move(Arg.PendingMerges)),
      MinTrailingZerosCache(std::move(Arg.MinTrailingZerosCache)),
      BackedgeTakenCounts(std::move(Arg.BackedgeTakenCounts)),
      PredicatedBackedgeTakenCounts(
          std::move(Arg.PredicatedBackedgeTakenCounts)),
      ConstantEvolutionLoopExitValue(
          std::move(Arg.ConstantEvolutionLoopExitValue)),
      ValuesAtScopes(std::move(Arg.ValuesAtScopes)),
      LoopDispositions(std::move(Arg.LoopDispositions)),
      LoopPropertiesCache(std::move(Arg.LoopPropertiesCache)),
      BlockDispositions(std::move(Arg.BlockDispositions)),
      UnsignedRanges(std::move(Arg.UnsignedRanges)),
      SignedRanges(std::move(Arg.SignedRanges)),
      UniqueSCEVs(std::move(Arg.UniqueSCEVs)),
      UniquePreds(std::move(Arg.UniquePreds)),
      SCEVAllocator(std::move(Arg.SCEVAllocator)),
      LoopUsers(std::move(Arg.LoopUsers)),
      PredicatedSCEVRewrites(std::move(Arg.PredicatedSCEVRewrites)),
      FirstUnknown(Arg.FirstUnknown) {
  Arg.FirstUnknown = nullptr;
}

ScalarEvolution::~ScalarEvolution() {
  // Iterate through all the SCEVUnknown instances and call their
  // destructors, so that they release their references to their values.
  for (SCEVUnknown *U = FirstUnknown; U;) {
    SCEVUnknown *Tmp = U;
    U = U->Next;
    Tmp->~SCEVUnknown();
  }
  FirstUnknown = nullptr;

  ExprValueMap.clear();
  ValueExprMap.clear();
  HasRecMap.clear();

  // Free any extra memory created for ExitNotTakenInfo in the unlikely event
  // that a loop had multiple computable exits.
  for (auto &BTCI : BackedgeTakenCounts)
    BTCI.second.clear();
  for (auto &BTCI : PredicatedBackedgeTakenCounts)
    BTCI.second.clear();

  assert(PendingLoopPredicates.empty() && "isImpliedCond garbage");
  assert(PendingPhiRanges.empty() && "getRangeRef garbage");
  assert(PendingMerges.empty() && "isImpliedViaMerge garbage");
  assert(!WalkingBEDominatingConds && "isLoopBackedgeGuardedByCond garbage!");
  assert(!ProvingSplitPredicate && "ProvingSplitPredicate garbage!");
}

bool ScalarEvolution::hasLoopInvariantBackedgeTakenCount(const Loop *L) {
  return !isa<SCEVCouldNotCompute>(getBackedgeTakenCount(L));
}

static void PrintLoopInfo(raw_ostream &OS, ScalarEvolution *SE,
                          const Loop *L) {
  // Print all inner loops first
  for (Loop *I : *L)
    PrintLoopInfo(OS, SE, I);

  OS << "Loop ";
  L->getHeader()->printAsOperand(OS, /*PrintType=*/false);
  OS << ": ";

  SmallVector<BasicBlock *, 8> ExitBlocks;
  L->getExitBlocks(ExitBlocks);
  if (ExitBlocks.size() != 1)
    OS << "<multiple exits> ";

  if (SE->hasLoopInvariantBackedgeTakenCount(L)) {
    OS << "backedge-taken count is " << *SE->getBackedgeTakenCount(L);
  } else {
    OS << "Unpredictable backedge-taken count. ";
  }

  OS << "\n"
        "Loop ";
  L->getHeader()->printAsOperand(OS, /*PrintType=*/false);
  OS << ": ";

  if (!isa<SCEVCouldNotCompute>(SE->getMaxBackedgeTakenCount(L))) {
    OS << "max backedge-taken count is " << *SE->getMaxBackedgeTakenCount(L);
    if (SE->isBackedgeTakenCountMaxOrZero(L))
      OS << ", actual taken count either this or zero.";
  } else {
    OS << "Unpredictable max backedge-taken count. ";
  }

  OS << "\n"
        "Loop ";
  L->getHeader()->printAsOperand(OS, /*PrintType=*/false);
  OS << ": ";

  SCEVUnionPredicate Pred;
  auto PBT = SE->getPredicatedBackedgeTakenCount(L, Pred);
  if (!isa<SCEVCouldNotCompute>(PBT)) {
    OS << "Predicated backedge-taken count is " << *PBT << "\n";
    OS << " Predicates:\n";
    Pred.print(OS, 4);
  } else {
    OS << "Unpredictable predicated backedge-taken count. ";
  }
  OS << "\n";

  if (SE->hasLoopInvariantBackedgeTakenCount(L)) {
    OS << "Loop ";
    L->getHeader()->printAsOperand(OS, /*PrintType=*/false);
    OS << ": ";
    OS << "Trip multiple is " << SE->getSmallConstantTripMultiple(L) << "\n";
  }
}

static StringRef loopDispositionToStr(ScalarEvolution::LoopDisposition LD) {
  switch (LD) {
  case ScalarEvolution::LoopVariant:
    return "Variant";
  case ScalarEvolution::LoopInvariant:
    return "Invariant";
  case ScalarEvolution::LoopComputable:
    return "Computable";
  }
  llvm_unreachable("Unknown ScalarEvolution::LoopDisposition kind!");
}

void ScalarEvolution::print(raw_ostream &OS) const {
  // ScalarEvolution's implementation of the print method is to print
  // out SCEV values of all instructions that are interesting. Doing
  // this potentially causes it to create new SCEV objects though,
  // which technically conflicts with the const qualifier. This isn't
  // observable from outside the class though, so casting away the
  // const isn't dangerous.
  ScalarEvolution &SE = *const_cast<ScalarEvolution *>(this);

  OS << "Classifying expressions for: ";
  F.printAsOperand(OS, /*PrintType=*/false);
  OS << "\n";
  for (Instruction &I : instructions(F))
    if (isSCEVable(I.getType()) && !isa<CmpInst>(I)) {
      OS << I << '\n';
      OS << "  -->  ";
      const SCEV *SV = SE.getSCEV(&I);
      SV->print(OS);
      if (!isa<SCEVCouldNotCompute>(SV)) {
        OS << " U: ";
        SE.getUnsignedRange(SV).print(OS);
        OS << " S: ";
        SE.getSignedRange(SV).print(OS);
      }

      const Loop *L = LI.getLoopFor(I.getParent());

      const SCEV *AtUse = SE.getSCEVAtScope(SV, L);
      if (AtUse != SV) {
        OS << "  -->  ";
        AtUse->print(OS);
        if (!isa<SCEVCouldNotCompute>(AtUse)) {
          OS << " U: ";
          SE.getUnsignedRange(AtUse).print(OS);
          OS << " S: ";
          SE.getSignedRange(AtUse).print(OS);
        }
      }

      if (L) {
        OS << "\t\t" "Exits: ";
        const SCEV *ExitValue = SE.getSCEVAtScope(SV, L->getParentLoop());
        if (!SE.isLoopInvariant(ExitValue, L)) {
          OS << "<<Unknown>>";
        } else {
          OS << *ExitValue;
        }

        bool First = true;
        for (auto *Iter = L; Iter; Iter = Iter->getParentLoop()) {
          if (First) {
            OS << "\t\t" "LoopDispositions: { ";
            First = false;
          } else {
            OS << ", ";
          }

          Iter->getHeader()->printAsOperand(OS, /*PrintType=*/false);
          OS << ": " << loopDispositionToStr(SE.getLoopDisposition(SV, Iter));
        }

        for (auto *InnerL : depth_first(L)) {
          if (InnerL == L)
            continue;
          if (First) {
            OS << "\t\t" "LoopDispositions: { ";
            First = false;
          } else {
            OS << ", ";
          }

          InnerL->getHeader()->printAsOperand(OS, /*PrintType=*/false);
          OS << ": " << loopDispositionToStr(SE.getLoopDisposition(SV, InnerL));
        }

        OS << " }";
      }

      OS << "\n";
    }

  OS << "Determining loop execution counts for: ";
  F.printAsOperand(OS, /*PrintType=*/false);
  OS << "\n";
  for (Loop *I : LI)
    PrintLoopInfo(OS, &SE, I);
}

ScalarEvolution::LoopDisposition
ScalarEvolution::getLoopDisposition(const SCEV *S, const Loop *L) {
  auto &Values = LoopDispositions[S];
  for (auto &V : Values) {
    if (V.getPointer() == L)
      return V.getInt();
  }
  Values.emplace_back(L, LoopVariant);
  LoopDisposition D = computeLoopDisposition(S, L);
  auto &Values2 = LoopDispositions[S];
  for (auto &V : make_range(Values2.rbegin(), Values2.rend())) {
    if (V.getPointer() == L) {
      V.setInt(D);
      break;
    }
  }
  return D;
}

ScalarEvolution::LoopDisposition
ScalarEvolution::computeLoopDisposition(const SCEV *S, const Loop *L) {
  switch (static_cast<SCEVTypes>(S->getSCEVType())) {
  case scConstant:
    return LoopInvariant;
  case scTruncate:
  case scZeroExtend:
  case scSignExtend:
    return getLoopDisposition(cast<SCEVCastExpr>(S)->getOperand(), L);
  case scAddRecExpr: {
    const SCEVAddRecExpr *AR = cast<SCEVAddRecExpr>(S);

    // If L is the addrec's loop, it's computable.
    if (AR->getLoop() == L)
      return LoopComputable;

    // Add recurrences are never invariant in the function-body (null loop).
    if (!L)
      return LoopVariant;

    // Everything that is not defined at loop entry is variant.
    if (DT.dominates(L->getHeader(), AR->getLoop()->getHeader()))
      return LoopVariant;
    assert(!L->contains(AR->getLoop()) && "Containing loop's header does not"
           " dominate the contained loop's header?");

    // This recurrence is invariant w.r.t. L if AR's loop contains L.
    if (AR->getLoop()->contains(L))
      return LoopInvariant;

    // This recurrence is variant w.r.t. L if any of its operands
    // are variant.
    for (auto *Op : AR->operands())
      if (!isLoopInvariant(Op, L))
        return LoopVariant;

    // Otherwise it's loop-invariant.
    return LoopInvariant;
  }
  case scAddExpr:
  case scMulExpr:
  case scUMaxExpr:
  case scSMaxExpr: {
    bool HasVarying = false;
    for (auto *Op : cast<SCEVNAryExpr>(S)->operands()) {
      LoopDisposition D = getLoopDisposition(Op, L);
      if (D == LoopVariant)
        return LoopVariant;
      if (D == LoopComputable)
        HasVarying = true;
    }
    return HasVarying ? LoopComputable : LoopInvariant;
  }
  case scUDivExpr: {
    const SCEVUDivExpr *UDiv = cast<SCEVUDivExpr>(S);
    LoopDisposition LD = getLoopDisposition(UDiv->getLHS(), L);
    if (LD == LoopVariant)
      return LoopVariant;
    LoopDisposition RD = getLoopDisposition(UDiv->getRHS(), L);
    if (RD == LoopVariant)
      return LoopVariant;
    return (LD == LoopInvariant && RD == LoopInvariant) ?
           LoopInvariant : LoopComputable;
  }
  case scUnknown:
    // All non-instruction values are loop invariant.  All instructions are loop
    // invariant if they are not contained in the specified loop.
    // Instructions are never considered invariant in the function body
    // (null loop) because they are defined within the "loop".
    if (auto *I = dyn_cast<Instruction>(cast<SCEVUnknown>(S)->getValue()))
      return (L && !L->contains(I)) ? LoopInvariant : LoopVariant;
    return LoopInvariant;
  case scCouldNotCompute:
    llvm_unreachable("Attempt to use a SCEVCouldNotCompute object!");
  }
  llvm_unreachable("Unknown SCEV kind!");
}

bool ScalarEvolution::isLoopInvariant(const SCEV *S, const Loop *L) {
  return getLoopDisposition(S, L) == LoopInvariant;
}

bool ScalarEvolution::hasComputableLoopEvolution(const SCEV *S, const Loop *L) {
  return getLoopDisposition(S, L) == LoopComputable;
}

ScalarEvolution::BlockDisposition
ScalarEvolution::getBlockDisposition(const SCEV *S, const BasicBlock *BB) {
  auto &Values = BlockDispositions[S];
  for (auto &V : Values) {
    if (V.getPointer() == BB)
      return V.getInt();
  }
  Values.emplace_back(BB, DoesNotDominateBlock);
  BlockDisposition D = computeBlockDisposition(S, BB);
  auto &Values2 = BlockDispositions[S];
  for (auto &V : make_range(Values2.rbegin(), Values2.rend())) {
    if (V.getPointer() == BB) {
      V.setInt(D);
      break;
    }
  }
  return D;
}

ScalarEvolution::BlockDisposition
ScalarEvolution::computeBlockDisposition(const SCEV *S, const BasicBlock *BB) {
  switch (static_cast<SCEVTypes>(S->getSCEVType())) {
  case scConstant:
    return ProperlyDominatesBlock;
  case scTruncate:
  case scZeroExtend:
  case scSignExtend:
    return getBlockDisposition(cast<SCEVCastExpr>(S)->getOperand(), BB);
  case scAddRecExpr: {
    // This uses a "dominates" query instead of "properly dominates" query
    // to test for proper dominance too, because the instruction which
    // produces the addrec's value is a PHI, and a PHI effectively properly
    // dominates its entire containing block.
    const SCEVAddRecExpr *AR = cast<SCEVAddRecExpr>(S);
    if (!DT.dominates(AR->getLoop()->getHeader(), BB))
      return DoesNotDominateBlock;

    // Fall through into SCEVNAryExpr handling.
    LLVM_FALLTHROUGH;
  }
  case scAddExpr:
  case scMulExpr:
  case scUMaxExpr:
  case scSMaxExpr: {
    const SCEVNAryExpr *NAry = cast<SCEVNAryExpr>(S);
    bool Proper = true;
    for (const SCEV *NAryOp : NAry->operands()) {
      BlockDisposition D = getBlockDisposition(NAryOp, BB);
      if (D == DoesNotDominateBlock)
        return DoesNotDominateBlock;
      if (D == DominatesBlock)
        Proper = false;
    }
    return Proper ? ProperlyDominatesBlock : DominatesBlock;
  }
  case scUDivExpr: {
    const SCEVUDivExpr *UDiv = cast<SCEVUDivExpr>(S);
    const SCEV *LHS = UDiv->getLHS(), *RHS = UDiv->getRHS();
    BlockDisposition LD = getBlockDisposition(LHS, BB);
    if (LD == DoesNotDominateBlock)
      return DoesNotDominateBlock;
    BlockDisposition RD = getBlockDisposition(RHS, BB);
    if (RD == DoesNotDominateBlock)
      return DoesNotDominateBlock;
    return (LD == ProperlyDominatesBlock && RD == ProperlyDominatesBlock) ?
      ProperlyDominatesBlock : DominatesBlock;
  }
  case scUnknown:
    if (Instruction *I =
          dyn_cast<Instruction>(cast<SCEVUnknown>(S)->getValue())) {
      if (I->getParent() == BB)
        return DominatesBlock;
      if (DT.properlyDominates(I->getParent(), BB))
        return ProperlyDominatesBlock;
      return DoesNotDominateBlock;
    }
    return ProperlyDominatesBlock;
  case scCouldNotCompute:
    llvm_unreachable("Attempt to use a SCEVCouldNotCompute object!");
  }
  llvm_unreachable("Unknown SCEV kind!");
}

bool ScalarEvolution::dominates(const SCEV *S, const BasicBlock *BB) {
  return getBlockDisposition(S, BB) >= DominatesBlock;
}

bool ScalarEvolution::properlyDominates(const SCEV *S, const BasicBlock *BB) {
  return getBlockDisposition(S, BB) == ProperlyDominatesBlock;
}

bool ScalarEvolution::hasOperand(const SCEV *S, const SCEV *Op) const {
  return SCEVExprContains(S, [&](const SCEV *Expr) { return Expr == Op; });
}

bool ScalarEvolution::ExitLimit::hasOperand(const SCEV *S) const {
  auto IsS = [&](const SCEV *X) { return S == X; };
  auto ContainsS = [&](const SCEV *X) {
    return !isa<SCEVCouldNotCompute>(X) && SCEVExprContains(X, IsS);
  };
  return ContainsS(ExactNotTaken) || ContainsS(MaxNotTaken);
}

void
ScalarEvolution::forgetMemoizedResults(const SCEV *S) {
  ValuesAtScopes.erase(S);
  LoopDispositions.erase(S);
  BlockDispositions.erase(S);
  UnsignedRanges.erase(S);
  SignedRanges.erase(S);
  ExprValueMap.erase(S);
  HasRecMap.erase(S);
  MinTrailingZerosCache.erase(S);

  for (auto I = PredicatedSCEVRewrites.begin();
       I != PredicatedSCEVRewrites.end();) {
    std::pair<const SCEV *, const Loop *> Entry = I->first;
    if (Entry.first == S)
      PredicatedSCEVRewrites.erase(I++);
    else
      ++I;
  }

  auto RemoveSCEVFromBackedgeMap =
      [S, this](DenseMap<const Loop *, BackedgeTakenInfo> &Map) {
        for (auto I = Map.begin(), E = Map.end(); I != E;) {
          BackedgeTakenInfo &BEInfo = I->second;
          if (BEInfo.hasOperand(S, this)) {
            BEInfo.clear();
            Map.erase(I++);
          } else
            ++I;
        }
      };

  RemoveSCEVFromBackedgeMap(BackedgeTakenCounts);
  RemoveSCEVFromBackedgeMap(PredicatedBackedgeTakenCounts);
}

void
ScalarEvolution::getUsedLoops(const SCEV *S,
                              SmallPtrSetImpl<const Loop *> &LoopsUsed) {
  struct FindUsedLoops {
    FindUsedLoops(SmallPtrSetImpl<const Loop *> &LoopsUsed)
        : LoopsUsed(LoopsUsed) {}
    SmallPtrSetImpl<const Loop *> &LoopsUsed;
    bool follow(const SCEV *S) {
      if (auto *AR = dyn_cast<SCEVAddRecExpr>(S))
        LoopsUsed.insert(AR->getLoop());
      return true;
    }

    bool isDone() const { return false; }
  };

  FindUsedLoops F(LoopsUsed);
  SCEVTraversal<FindUsedLoops>(F).visitAll(S);
}

void ScalarEvolution::addToLoopUseLists(const SCEV *S) {
  SmallPtrSet<const Loop *, 8> LoopsUsed;
  getUsedLoops(S, LoopsUsed);
  for (auto *L : LoopsUsed)
    LoopUsers[L].push_back(S);
}

void ScalarEvolution::verify() const {
  ScalarEvolution &SE = *const_cast<ScalarEvolution *>(this);
  ScalarEvolution SE2(F, TLI, AC, DT, LI);

  SmallVector<Loop *, 8> LoopStack(LI.begin(), LI.end());

  // Map's SCEV expressions from one ScalarEvolution "universe" to another.
  struct SCEVMapper : public SCEVRewriteVisitor<SCEVMapper> {
    SCEVMapper(ScalarEvolution &SE) : SCEVRewriteVisitor<SCEVMapper>(SE) {}

    const SCEV *visitConstant(const SCEVConstant *Constant) {
      return SE.getConstant(Constant->getAPInt());
    }

    const SCEV *visitUnknown(const SCEVUnknown *Expr) {
      return SE.getUnknown(Expr->getValue());
    }

    const SCEV *visitCouldNotCompute(const SCEVCouldNotCompute *Expr) {
      return SE.getCouldNotCompute();
    }
  };

  SCEVMapper SCM(SE2);

  while (!LoopStack.empty()) {
    auto *L = LoopStack.pop_back_val();
    LoopStack.insert(LoopStack.end(), L->begin(), L->end());

    auto *CurBECount = SCM.visit(
        const_cast<ScalarEvolution *>(this)->getBackedgeTakenCount(L));
    auto *NewBECount = SE2.getBackedgeTakenCount(L);

    if (CurBECount == SE2.getCouldNotCompute() ||
        NewBECount == SE2.getCouldNotCompute()) {
      // NB! This situation is legal, but is very suspicious -- whatever pass
      // change the loop to make a trip count go from could not compute to
      // computable or vice-versa *should have* invalidated SCEV.  However, we
      // choose not to assert here (for now) since we don't want false
      // positives.
      continue;
    }

    if (containsUndefs(CurBECount) || containsUndefs(NewBECount)) {
      // SCEV treats "undef" as an unknown but consistent value (i.e. it does
      // not propagate undef aggressively).  This means we can (and do) fail
      // verification in cases where a transform makes the trip count of a loop
      // go from "undef" to "undef+1" (say).  The transform is fine, since in
      // both cases the loop iterates "undef" times, but SCEV thinks we
      // increased the trip count of the loop by 1 incorrectly.
      continue;
    }

    if (SE.getTypeSizeInBits(CurBECount->getType()) >
        SE.getTypeSizeInBits(NewBECount->getType()))
      NewBECount = SE2.getZeroExtendExpr(NewBECount, CurBECount->getType());
    else if (SE.getTypeSizeInBits(CurBECount->getType()) <
             SE.getTypeSizeInBits(NewBECount->getType()))
      CurBECount = SE2.getZeroExtendExpr(CurBECount, NewBECount->getType());

    auto *ConstantDelta =
        dyn_cast<SCEVConstant>(SE2.getMinusSCEV(CurBECount, NewBECount));

    if (ConstantDelta && ConstantDelta->getAPInt() != 0) {
      dbgs() << "Trip Count Changed!\n";
      dbgs() << "Old: " << *CurBECount << "\n";
      dbgs() << "New: " << *NewBECount << "\n";
      dbgs() << "Delta: " << *ConstantDelta << "\n";
      std::abort();
    }
  }
}

bool ScalarEvolution::invalidate(
    Function &F, const PreservedAnalyses &PA,
    FunctionAnalysisManager::Invalidator &Inv) {
  // Invalidate the ScalarEvolution object whenever it isn't preserved or one
  // of its dependencies is invalidated.
  auto PAC = PA.getChecker<ScalarEvolutionAnalysis>();
  return !(PAC.preserved() || PAC.preservedSet<AllAnalysesOn<Function>>()) ||
         Inv.invalidate<AssumptionAnalysis>(F, PA) ||
         Inv.invalidate<DominatorTreeAnalysis>(F, PA) ||
         Inv.invalidate<LoopAnalysis>(F, PA);
}

AnalysisKey ScalarEvolutionAnalysis::Key;

ScalarEvolution ScalarEvolutionAnalysis::run(Function &F,
                                             FunctionAnalysisManager &AM) {
  return ScalarEvolution(F, AM.getResult<TargetLibraryAnalysis>(F),
                         AM.getResult<AssumptionAnalysis>(F),
                         AM.getResult<DominatorTreeAnalysis>(F),
                         AM.getResult<LoopAnalysis>(F));
}

PreservedAnalyses
ScalarEvolutionPrinterPass::run(Function &F, FunctionAnalysisManager &AM) {
  AM.getResult<ScalarEvolutionAnalysis>(F).print(OS);
  return PreservedAnalyses::all();
}

INITIALIZE_PASS_BEGIN(ScalarEvolutionWrapperPass, "scalar-evolution",
                      "Scalar Evolution Analysis", false, true)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(ScalarEvolutionWrapperPass, "scalar-evolution",
                    "Scalar Evolution Analysis", false, true)

char ScalarEvolutionWrapperPass::ID = 0;

ScalarEvolutionWrapperPass::ScalarEvolutionWrapperPass() : FunctionPass(ID) {
  initializeScalarEvolutionWrapperPassPass(*PassRegistry::getPassRegistry());
}

bool ScalarEvolutionWrapperPass::runOnFunction(Function &F) {
  SE.reset(new ScalarEvolution(
      F, getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(),
      getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F),
      getAnalysis<DominatorTreeWrapperPass>().getDomTree(),
      getAnalysis<LoopInfoWrapperPass>().getLoopInfo()));
  return false;
}

void ScalarEvolutionWrapperPass::releaseMemory() { SE.reset(); }

void ScalarEvolutionWrapperPass::print(raw_ostream &OS, const Module *) const {
  SE->print(OS);
}

void ScalarEvolutionWrapperPass::verifyAnalysis() const {
  if (!VerifySCEV)
    return;

  SE->verify();
}

void ScalarEvolutionWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequiredTransitive<AssumptionCacheTracker>();
  AU.addRequiredTransitive<LoopInfoWrapperPass>();
  AU.addRequiredTransitive<DominatorTreeWrapperPass>();
  AU.addRequiredTransitive<TargetLibraryInfoWrapperPass>();
}

const SCEVPredicate *ScalarEvolution::getEqualPredicate(const SCEV *LHS,
                                                        const SCEV *RHS) {
  FoldingSetNodeID ID;
  assert(LHS->getType() == RHS->getType() &&
         "Type mismatch between LHS and RHS");
  // Unique this node based on the arguments
  ID.AddInteger(SCEVPredicate::P_Equal);
  ID.AddPointer(LHS);
  ID.AddPointer(RHS);
  void *IP = nullptr;
  if (const auto *S = UniquePreds.FindNodeOrInsertPos(ID, IP))
    return S;
  SCEVEqualPredicate *Eq = new (SCEVAllocator)
      SCEVEqualPredicate(ID.Intern(SCEVAllocator), LHS, RHS);
  UniquePreds.InsertNode(Eq, IP);
  return Eq;
}

const SCEVPredicate *ScalarEvolution::getWrapPredicate(
    const SCEVAddRecExpr *AR,
    SCEVWrapPredicate::IncrementWrapFlags AddedFlags) {
  FoldingSetNodeID ID;
  // Unique this node based on the arguments
  ID.AddInteger(SCEVPredicate::P_Wrap);
  ID.AddPointer(AR);
  ID.AddInteger(AddedFlags);
  void *IP = nullptr;
  if (const auto *S = UniquePreds.FindNodeOrInsertPos(ID, IP))
    return S;
  auto *OF = new (SCEVAllocator)
      SCEVWrapPredicate(ID.Intern(SCEVAllocator), AR, AddedFlags);
  UniquePreds.InsertNode(OF, IP);
  return OF;
}

namespace {

class SCEVPredicateRewriter : public SCEVRewriteVisitor<SCEVPredicateRewriter> {
public:

  /// Rewrites \p S in the context of a loop L and the SCEV predication
  /// infrastructure.
  ///
  /// If \p Pred is non-null, the SCEV expression is rewritten to respect the
  /// equivalences present in \p Pred.
  ///
  /// If \p NewPreds is non-null, rewrite is free to add further predicates to
  /// \p NewPreds such that the result will be an AddRecExpr.
  static const SCEV *rewrite(const SCEV *S, const Loop *L, ScalarEvolution &SE,
                             SmallPtrSetImpl<const SCEVPredicate *> *NewPreds,
                             SCEVUnionPredicate *Pred) {
    SCEVPredicateRewriter Rewriter(L, SE, NewPreds, Pred);
    return Rewriter.visit(S);
  }

  const SCEV *visitUnknown(const SCEVUnknown *Expr) {
    if (Pred) {
      auto ExprPreds = Pred->getPredicatesForExpr(Expr);
      for (auto *Pred : ExprPreds)
        if (const auto *IPred = dyn_cast<SCEVEqualPredicate>(Pred))
          if (IPred->getLHS() == Expr)
            return IPred->getRHS();
    }
    return convertToAddRecWithPreds(Expr);
  }

  const SCEV *visitZeroExtendExpr(const SCEVZeroExtendExpr *Expr) {
    const SCEV *Operand = visit(Expr->getOperand());
    const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(Operand);
    if (AR && AR->getLoop() == L && AR->isAffine()) {
      // This couldn't be folded because the operand didn't have the nuw
      // flag. Add the nusw flag as an assumption that we could make.
      const SCEV *Step = AR->getStepRecurrence(SE);
      Type *Ty = Expr->getType();
      if (addOverflowAssumption(AR, SCEVWrapPredicate::IncrementNUSW))
        return SE.getAddRecExpr(SE.getZeroExtendExpr(AR->getStart(), Ty),
                                SE.getSignExtendExpr(Step, Ty), L,
                                AR->getNoWrapFlags());
    }
    return SE.getZeroExtendExpr(Operand, Expr->getType());
  }

  const SCEV *visitSignExtendExpr(const SCEVSignExtendExpr *Expr) {
    const SCEV *Operand = visit(Expr->getOperand());
    const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(Operand);
    if (AR && AR->getLoop() == L && AR->isAffine()) {
      // This couldn't be folded because the operand didn't have the nsw
      // flag. Add the nssw flag as an assumption that we could make.
      const SCEV *Step = AR->getStepRecurrence(SE);
      Type *Ty = Expr->getType();
      if (addOverflowAssumption(AR, SCEVWrapPredicate::IncrementNSSW))
        return SE.getAddRecExpr(SE.getSignExtendExpr(AR->getStart(), Ty),
                                SE.getSignExtendExpr(Step, Ty), L,
                                AR->getNoWrapFlags());
    }
    return SE.getSignExtendExpr(Operand, Expr->getType());
  }

private:
  explicit SCEVPredicateRewriter(const Loop *L, ScalarEvolution &SE,
                        SmallPtrSetImpl<const SCEVPredicate *> *NewPreds,
                        SCEVUnionPredicate *Pred)
      : SCEVRewriteVisitor(SE), NewPreds(NewPreds), Pred(Pred), L(L) {}

  bool addOverflowAssumption(const SCEVPredicate *P) {
    if (!NewPreds) {
      // Check if we've already made this assumption.
      return Pred && Pred->implies(P);
    }
    NewPreds->insert(P);
    return true;
  }

  bool addOverflowAssumption(const SCEVAddRecExpr *AR,
                             SCEVWrapPredicate::IncrementWrapFlags AddedFlags) {
    auto *A = SE.getWrapPredicate(AR, AddedFlags);
    return addOverflowAssumption(A);
  }

  // If \p Expr represents a PHINode, we try to see if it can be represented
  // as an AddRec, possibly under a predicate (PHISCEVPred). If it is possible
  // to add this predicate as a runtime overflow check, we return the AddRec.
  // If \p Expr does not meet these conditions (is not a PHI node, or we
  // couldn't create an AddRec for it, or couldn't add the predicate), we just
  // return \p Expr.
  const SCEV *convertToAddRecWithPreds(const SCEVUnknown *Expr) {
    if (!isa<PHINode>(Expr->getValue()))
      return Expr;
    Optional<std::pair<const SCEV *, SmallVector<const SCEVPredicate *, 3>>>
    PredicatedRewrite = SE.createAddRecFromPHIWithCasts(Expr);
    if (!PredicatedRewrite)
      return Expr;
    for (auto *P : PredicatedRewrite->second){
      // Wrap predicates from outer loops are not supported.
      if (auto *WP = dyn_cast<const SCEVWrapPredicate>(P)) {
        auto *AR = cast<const SCEVAddRecExpr>(WP->getExpr());
        if (L != AR->getLoop())
          return Expr;
      }
      if (!addOverflowAssumption(P))
        return Expr;
    }
    return PredicatedRewrite->first;
  }

  SmallPtrSetImpl<const SCEVPredicate *> *NewPreds;
  SCEVUnionPredicate *Pred;
  const Loop *L;
};

} // end anonymous namespace

const SCEV *ScalarEvolution::rewriteUsingPredicate(const SCEV *S, const Loop *L,
                                                   SCEVUnionPredicate &Preds) {
  return SCEVPredicateRewriter::rewrite(S, L, *this, nullptr, &Preds);
}

const SCEVAddRecExpr *ScalarEvolution::convertSCEVToAddRecWithPredicates(
    const SCEV *S, const Loop *L,
    SmallPtrSetImpl<const SCEVPredicate *> &Preds) {
  SmallPtrSet<const SCEVPredicate *, 4> TransformPreds;
  S = SCEVPredicateRewriter::rewrite(S, L, *this, &TransformPreds, nullptr);
  auto *AddRec = dyn_cast<SCEVAddRecExpr>(S);

  if (!AddRec)
    return nullptr;

  // Since the transformation was successful, we can now transfer the SCEV
  // predicates.
  for (auto *P : TransformPreds)
    Preds.insert(P);

  return AddRec;
}

/// SCEV predicates
SCEVPredicate::SCEVPredicate(const FoldingSetNodeIDRef ID,
                             SCEVPredicateKind Kind)
    : FastID(ID), Kind(Kind) {}

SCEVEqualPredicate::SCEVEqualPredicate(const FoldingSetNodeIDRef ID,
                                       const SCEV *LHS, const SCEV *RHS)
    : SCEVPredicate(ID, P_Equal), LHS(LHS), RHS(RHS) {
  assert(LHS->getType() == RHS->getType() && "LHS and RHS types don't match");
  assert(LHS != RHS && "LHS and RHS are the same SCEV");
}

bool SCEVEqualPredicate::implies(const SCEVPredicate *N) const {
  const auto *Op = dyn_cast<SCEVEqualPredicate>(N);

  if (!Op)
    return false;

  return Op->LHS == LHS && Op->RHS == RHS;
}

bool SCEVEqualPredicate::isAlwaysTrue() const { return false; }

const SCEV *SCEVEqualPredicate::getExpr() const { return LHS; }

void SCEVEqualPredicate::print(raw_ostream &OS, unsigned Depth) const {
  OS.indent(Depth) << "Equal predicate: " << *LHS << " == " << *RHS << "\n";
}

SCEVWrapPredicate::SCEVWrapPredicate(const FoldingSetNodeIDRef ID,
                                     const SCEVAddRecExpr *AR,
                                     IncrementWrapFlags Flags)
    : SCEVPredicate(ID, P_Wrap), AR(AR), Flags(Flags) {}

const SCEV *SCEVWrapPredicate::getExpr() const { return AR; }

bool SCEVWrapPredicate::implies(const SCEVPredicate *N) const {
  const auto *Op = dyn_cast<SCEVWrapPredicate>(N);

  return Op && Op->AR == AR && setFlags(Flags, Op->Flags) == Flags;
}

bool SCEVWrapPredicate::isAlwaysTrue() const {
  SCEV::NoWrapFlags ScevFlags = AR->getNoWrapFlags();
  IncrementWrapFlags IFlags = Flags;

  if (ScalarEvolution::setFlags(ScevFlags, SCEV::FlagNSW) == ScevFlags)
    IFlags = clearFlags(IFlags, IncrementNSSW);

  return IFlags == IncrementAnyWrap;
}

void SCEVWrapPredicate::print(raw_ostream &OS, unsigned Depth) const {
  OS.indent(Depth) << *getExpr() << " Added Flags: ";
  if (SCEVWrapPredicate::IncrementNUSW & getFlags())
    OS << "<nusw>";
  if (SCEVWrapPredicate::IncrementNSSW & getFlags())
    OS << "<nssw>";
  OS << "\n";
}

SCEVWrapPredicate::IncrementWrapFlags
SCEVWrapPredicate::getImpliedFlags(const SCEVAddRecExpr *AR,
                                   ScalarEvolution &SE) {
  IncrementWrapFlags ImpliedFlags = IncrementAnyWrap;
  SCEV::NoWrapFlags StaticFlags = AR->getNoWrapFlags();

  // We can safely transfer the NSW flag as NSSW.
  if (ScalarEvolution::setFlags(StaticFlags, SCEV::FlagNSW) == StaticFlags)
    ImpliedFlags = IncrementNSSW;

  if (ScalarEvolution::setFlags(StaticFlags, SCEV::FlagNUW) == StaticFlags) {
    // If the increment is positive, the SCEV NUW flag will also imply the
    // WrapPredicate NUSW flag.
    if (const auto *Step = dyn_cast<SCEVConstant>(AR->getStepRecurrence(SE)))
      if (Step->getValue()->getValue().isNonNegative())
        ImpliedFlags = setFlags(ImpliedFlags, IncrementNUSW);
  }

  return ImpliedFlags;
}

/// Union predicates don't get cached so create a dummy set ID for it.
SCEVUnionPredicate::SCEVUnionPredicate()
    : SCEVPredicate(FoldingSetNodeIDRef(nullptr, 0), P_Union) {}

bool SCEVUnionPredicate::isAlwaysTrue() const {
  return all_of(Preds,
                [](const SCEVPredicate *I) { return I->isAlwaysTrue(); });
}

ArrayRef<const SCEVPredicate *>
SCEVUnionPredicate::getPredicatesForExpr(const SCEV *Expr) {
  auto I = SCEVToPreds.find(Expr);
  if (I == SCEVToPreds.end())
    return ArrayRef<const SCEVPredicate *>();
  return I->second;
}

bool SCEVUnionPredicate::implies(const SCEVPredicate *N) const {
  if (const auto *Set = dyn_cast<SCEVUnionPredicate>(N))
    return all_of(Set->Preds,
                  [this](const SCEVPredicate *I) { return this->implies(I); });

  auto ScevPredsIt = SCEVToPreds.find(N->getExpr());
  if (ScevPredsIt == SCEVToPreds.end())
    return false;
  auto &SCEVPreds = ScevPredsIt->second;

  return any_of(SCEVPreds,
                [N](const SCEVPredicate *I) { return I->implies(N); });
}

const SCEV *SCEVUnionPredicate::getExpr() const { return nullptr; }

void SCEVUnionPredicate::print(raw_ostream &OS, unsigned Depth) const {
  for (auto Pred : Preds)
    Pred->print(OS, Depth);
}

void SCEVUnionPredicate::add(const SCEVPredicate *N) {
  if (const auto *Set = dyn_cast<SCEVUnionPredicate>(N)) {
    for (auto Pred : Set->Preds)
      add(Pred);
    return;
  }

  if (implies(N))
    return;

  const SCEV *Key = N->getExpr();
  assert(Key && "Only SCEVUnionPredicate doesn't have an "
                " associated expression!");

  SCEVToPreds[Key].push_back(N);
  Preds.push_back(N);
}

PredicatedScalarEvolution::PredicatedScalarEvolution(ScalarEvolution &SE,
                                                     Loop &L)
    : SE(SE), L(L) {}

const SCEV *PredicatedScalarEvolution::getSCEV(Value *V) {
  const SCEV *Expr = SE.getSCEV(V);
  RewriteEntry &Entry = RewriteMap[Expr];

  // If we already have an entry and the version matches, return it.
  if (Entry.second && Generation == Entry.first)
    return Entry.second;

  // We found an entry but it's stale. Rewrite the stale entry
  // according to the current predicate.
  if (Entry.second)
    Expr = Entry.second;

  const SCEV *NewSCEV = SE.rewriteUsingPredicate(Expr, &L, Preds);
  Entry = {Generation, NewSCEV};

  return NewSCEV;
}

const SCEV *PredicatedScalarEvolution::getBackedgeTakenCount() {
  if (!BackedgeCount) {
    SCEVUnionPredicate BackedgePred;
    BackedgeCount = SE.getPredicatedBackedgeTakenCount(&L, BackedgePred);
    addPredicate(BackedgePred);
  }
  return BackedgeCount;
}

void PredicatedScalarEvolution::addPredicate(const SCEVPredicate &Pred) {
  if (Preds.implies(&Pred))
    return;
  Preds.add(&Pred);
  updateGeneration();
}

const SCEVUnionPredicate &PredicatedScalarEvolution::getUnionPredicate() const {
  return Preds;
}

void PredicatedScalarEvolution::updateGeneration() {
  // If the generation number wrapped recompute everything.
  if (++Generation == 0) {
    for (auto &II : RewriteMap) {
      const SCEV *Rewritten = II.second.second;
      II.second = {Generation, SE.rewriteUsingPredicate(Rewritten, &L, Preds)};
    }
  }
}

void PredicatedScalarEvolution::setNoOverflow(
    Value *V, SCEVWrapPredicate::IncrementWrapFlags Flags) {
  const SCEV *Expr = getSCEV(V);
  const auto *AR = cast<SCEVAddRecExpr>(Expr);

  auto ImpliedFlags = SCEVWrapPredicate::getImpliedFlags(AR, SE);

  // Clear the statically implied flags.
  Flags = SCEVWrapPredicate::clearFlags(Flags, ImpliedFlags);
  addPredicate(*SE.getWrapPredicate(AR, Flags));

  auto II = FlagsMap.insert({V, Flags});
  if (!II.second)
    II.first->second = SCEVWrapPredicate::setFlags(Flags, II.first->second);
}

bool PredicatedScalarEvolution::hasNoOverflow(
    Value *V, SCEVWrapPredicate::IncrementWrapFlags Flags) {
  const SCEV *Expr = getSCEV(V);
  const auto *AR = cast<SCEVAddRecExpr>(Expr);

  Flags = SCEVWrapPredicate::clearFlags(
      Flags, SCEVWrapPredicate::getImpliedFlags(AR, SE));

  auto II = FlagsMap.find(V);

  if (II != FlagsMap.end())
    Flags = SCEVWrapPredicate::clearFlags(Flags, II->second);

  return Flags == SCEVWrapPredicate::IncrementAnyWrap;
}

const SCEVAddRecExpr *PredicatedScalarEvolution::getAsAddRec(Value *V) {
  const SCEV *Expr = this->getSCEV(V);
  SmallPtrSet<const SCEVPredicate *, 4> NewPreds;
  auto *New = SE.convertSCEVToAddRecWithPredicates(Expr, &L, NewPreds);

  if (!New)
    return nullptr;

  for (auto *P : NewPreds)
    Preds.add(P);

  updateGeneration();
  RewriteMap[SE.getSCEV(V)] = {Generation, New};
  return New;
}

PredicatedScalarEvolution::PredicatedScalarEvolution(
    const PredicatedScalarEvolution &Init)
    : RewriteMap(Init.RewriteMap), SE(Init.SE), L(Init.L), Preds(Init.Preds),
      Generation(Init.Generation), BackedgeCount(Init.BackedgeCount) {
  for (const auto &I : Init.FlagsMap)
    FlagsMap.insert(I);
}

void PredicatedScalarEvolution::print(raw_ostream &OS, unsigned Depth) const {
  // For each block.
  for (auto *BB : L.getBlocks())
    for (auto &I : *BB) {
      if (!SE.isSCEVable(I.getType()))
        continue;

      auto *Expr = SE.getSCEV(&I);
      auto II = RewriteMap.find(Expr);

      if (II == RewriteMap.end())
        continue;

      // Don't print things that are not interesting.
      if (II->second.second == Expr)
        continue;

      OS.indent(Depth) << "[PSE]" << I << ":\n";
      OS.indent(Depth + 2) << *Expr << "\n";
      OS.indent(Depth + 2) << "--> " << *II->second.second << "\n";
    }
}

// Match the mathematical pattern A - (A / B) * B, where A and B can be
// arbitrary expressions.
// It's not always easy, as A and B can be folded (imagine A is X / 2, and B is
// 4, A / B becomes X / 8).
bool ScalarEvolution::matchURem(const SCEV *Expr, const SCEV *&LHS,
                                const SCEV *&RHS) {
  const auto *Add = dyn_cast<SCEVAddExpr>(Expr);
  if (Add == nullptr || Add->getNumOperands() != 2)
    return false;

  const SCEV *A = Add->getOperand(1);
  const auto *Mul = dyn_cast<SCEVMulExpr>(Add->getOperand(0));

  if (Mul == nullptr)
    return false;

  const auto MatchURemWithDivisor = [&](const SCEV *B) {
    // (SomeExpr + (-(SomeExpr / B) * B)).
    if (Expr == getURemExpr(A, B)) {
      LHS = A;
      RHS = B;
      return true;
    }
    return false;
  };

  // (SomeExpr + (-1 * (SomeExpr / B) * B)).
  if (Mul->getNumOperands() == 3 && isa<SCEVConstant>(Mul->getOperand(0)))
    return MatchURemWithDivisor(Mul->getOperand(1)) ||
           MatchURemWithDivisor(Mul->getOperand(2));

  // (SomeExpr + ((-SomeExpr / B) * B)) or (SomeExpr + ((SomeExpr / B) * -B)).
  if (Mul->getNumOperands() == 2)
    return MatchURemWithDivisor(Mul->getOperand(1)) ||
           MatchURemWithDivisor(Mul->getOperand(0)) ||
           MatchURemWithDivisor(getNegativeSCEV(Mul->getOperand(1))) ||
           MatchURemWithDivisor(getNegativeSCEV(Mul->getOperand(0)));
  return false;
}
