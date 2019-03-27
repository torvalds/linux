//===- InstructionCombining.cpp - Combine multiple instructions -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// InstructionCombining - Combine instructions to form fewer, simple
// instructions.  This pass does not modify the CFG.  This pass is where
// algebraic simplification happens.
//
// This pass combines things like:
//    %Y = add i32 %X, 1
//    %Z = add i32 %Y, 1
// into:
//    %Z = add i32 %X, 2
//
// This is a simple worklist driven algorithm.
//
// This pass guarantees that the following canonicalizations are performed on
// the program:
//    1. If a binary operator has a constant operand, it is moved to the RHS
//    2. Bitwise operators with constant operands are always grouped so that
//       shifts are performed first, then or's, then and's, then xor's.
//    3. Compare instructions are converted from <,>,<=,>= to ==,!= if possible
//    4. All cmp instructions on boolean values are replaced with logical ops
//    5. add X, X is represented as (X*2) => (X << 1)
//    6. Multiplies with a power-of-two constant argument are transformed into
//       shifts.
//   ... etc.
//
//===----------------------------------------------------------------------===//

#include "InstCombineInternal.h"
#include "llvm-c/Initialization.h"
#include "llvm-c/Transforms/InstCombine.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/EHPersonalities.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TargetFolder.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/InstCombine/InstCombineWorklist.h"
#include "llvm/Transforms/Utils/Local.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

using namespace llvm;
using namespace llvm::PatternMatch;

#define DEBUG_TYPE "instcombine"

STATISTIC(NumCombined , "Number of insts combined");
STATISTIC(NumConstProp, "Number of constant folds");
STATISTIC(NumDeadInst , "Number of dead inst eliminated");
STATISTIC(NumSunkInst , "Number of instructions sunk");
STATISTIC(NumExpand,    "Number of expansions");
STATISTIC(NumFactor   , "Number of factorizations");
STATISTIC(NumReassoc  , "Number of reassociations");
DEBUG_COUNTER(VisitCounter, "instcombine-visit",
              "Controls which instructions are visited");

static cl::opt<bool>
EnableCodeSinking("instcombine-code-sinking", cl::desc("Enable code sinking"),
                                              cl::init(true));

static cl::opt<bool>
EnableExpensiveCombines("expensive-combines",
                        cl::desc("Enable expensive instruction combines"));

static cl::opt<unsigned>
MaxArraySize("instcombine-maxarray-size", cl::init(1024),
             cl::desc("Maximum array size considered when doing a combine"));

// FIXME: Remove this flag when it is no longer necessary to convert
// llvm.dbg.declare to avoid inaccurate debug info. Setting this to false
// increases variable availability at the cost of accuracy. Variables that
// cannot be promoted by mem2reg or SROA will be described as living in memory
// for their entire lifetime. However, passes like DSE and instcombine can
// delete stores to the alloca, leading to misleading and inaccurate debug
// information. This flag can be removed when those passes are fixed.
static cl::opt<unsigned> ShouldLowerDbgDeclare("instcombine-lower-dbg-declare",
                                               cl::Hidden, cl::init(true));

Value *InstCombiner::EmitGEPOffset(User *GEP) {
  return llvm::EmitGEPOffset(&Builder, DL, GEP);
}

/// Return true if it is desirable to convert an integer computation from a
/// given bit width to a new bit width.
/// We don't want to convert from a legal to an illegal type or from a smaller
/// to a larger illegal type. A width of '1' is always treated as a legal type
/// because i1 is a fundamental type in IR, and there are many specialized
/// optimizations for i1 types. Widths of 8, 16 or 32 are equally treated as
/// legal to convert to, in order to open up more combining opportunities.
/// NOTE: this treats i8, i16 and i32 specially, due to them being so common
/// from frontend languages.
bool InstCombiner::shouldChangeType(unsigned FromWidth,
                                    unsigned ToWidth) const {
  bool FromLegal = FromWidth == 1 || DL.isLegalInteger(FromWidth);
  bool ToLegal = ToWidth == 1 || DL.isLegalInteger(ToWidth);

  // Convert to widths of 8, 16 or 32 even if they are not legal types. Only
  // shrink types, to prevent infinite loops.
  if (ToWidth < FromWidth && (ToWidth == 8 || ToWidth == 16 || ToWidth == 32))
    return true;

  // If this is a legal integer from type, and the result would be an illegal
  // type, don't do the transformation.
  if (FromLegal && !ToLegal)
    return false;

  // Otherwise, if both are illegal, do not increase the size of the result. We
  // do allow things like i160 -> i64, but not i64 -> i160.
  if (!FromLegal && !ToLegal && ToWidth > FromWidth)
    return false;

  return true;
}

/// Return true if it is desirable to convert a computation from 'From' to 'To'.
/// We don't want to convert from a legal to an illegal type or from a smaller
/// to a larger illegal type. i1 is always treated as a legal type because it is
/// a fundamental type in IR, and there are many specialized optimizations for
/// i1 types.
bool InstCombiner::shouldChangeType(Type *From, Type *To) const {
  // TODO: This could be extended to allow vectors. Datalayout changes might be
  // needed to properly support that.
  if (!From->isIntegerTy() || !To->isIntegerTy())
    return false;

  unsigned FromWidth = From->getPrimitiveSizeInBits();
  unsigned ToWidth = To->getPrimitiveSizeInBits();
  return shouldChangeType(FromWidth, ToWidth);
}

// Return true, if No Signed Wrap should be maintained for I.
// The No Signed Wrap flag can be kept if the operation "B (I.getOpcode) C",
// where both B and C should be ConstantInts, results in a constant that does
// not overflow. This function only handles the Add and Sub opcodes. For
// all other opcodes, the function conservatively returns false.
static bool MaintainNoSignedWrap(BinaryOperator &I, Value *B, Value *C) {
  OverflowingBinaryOperator *OBO = dyn_cast<OverflowingBinaryOperator>(&I);
  if (!OBO || !OBO->hasNoSignedWrap())
    return false;

  // We reason about Add and Sub Only.
  Instruction::BinaryOps Opcode = I.getOpcode();
  if (Opcode != Instruction::Add && Opcode != Instruction::Sub)
    return false;

  const APInt *BVal, *CVal;
  if (!match(B, m_APInt(BVal)) || !match(C, m_APInt(CVal)))
    return false;

  bool Overflow = false;
  if (Opcode == Instruction::Add)
    (void)BVal->sadd_ov(*CVal, Overflow);
  else
    (void)BVal->ssub_ov(*CVal, Overflow);

  return !Overflow;
}

/// Conservatively clears subclassOptionalData after a reassociation or
/// commutation. We preserve fast-math flags when applicable as they can be
/// preserved.
static void ClearSubclassDataAfterReassociation(BinaryOperator &I) {
  FPMathOperator *FPMO = dyn_cast<FPMathOperator>(&I);
  if (!FPMO) {
    I.clearSubclassOptionalData();
    return;
  }

  FastMathFlags FMF = I.getFastMathFlags();
  I.clearSubclassOptionalData();
  I.setFastMathFlags(FMF);
}

/// Combine constant operands of associative operations either before or after a
/// cast to eliminate one of the associative operations:
/// (op (cast (op X, C2)), C1) --> (cast (op X, op (C1, C2)))
/// (op (cast (op X, C2)), C1) --> (op (cast X), op (C1, C2))
static bool simplifyAssocCastAssoc(BinaryOperator *BinOp1) {
  auto *Cast = dyn_cast<CastInst>(BinOp1->getOperand(0));
  if (!Cast || !Cast->hasOneUse())
    return false;

  // TODO: Enhance logic for other casts and remove this check.
  auto CastOpcode = Cast->getOpcode();
  if (CastOpcode != Instruction::ZExt)
    return false;

  // TODO: Enhance logic for other BinOps and remove this check.
  if (!BinOp1->isBitwiseLogicOp())
    return false;

  auto AssocOpcode = BinOp1->getOpcode();
  auto *BinOp2 = dyn_cast<BinaryOperator>(Cast->getOperand(0));
  if (!BinOp2 || !BinOp2->hasOneUse() || BinOp2->getOpcode() != AssocOpcode)
    return false;

  Constant *C1, *C2;
  if (!match(BinOp1->getOperand(1), m_Constant(C1)) ||
      !match(BinOp2->getOperand(1), m_Constant(C2)))
    return false;

  // TODO: This assumes a zext cast.
  // Eg, if it was a trunc, we'd cast C1 to the source type because casting C2
  // to the destination type might lose bits.

  // Fold the constants together in the destination type:
  // (op (cast (op X, C2)), C1) --> (op (cast X), FoldedC)
  Type *DestTy = C1->getType();
  Constant *CastC2 = ConstantExpr::getCast(CastOpcode, C2, DestTy);
  Constant *FoldedC = ConstantExpr::get(AssocOpcode, C1, CastC2);
  Cast->setOperand(0, BinOp2->getOperand(0));
  BinOp1->setOperand(1, FoldedC);
  return true;
}

/// This performs a few simplifications for operators that are associative or
/// commutative:
///
///  Commutative operators:
///
///  1. Order operands such that they are listed from right (least complex) to
///     left (most complex).  This puts constants before unary operators before
///     binary operators.
///
///  Associative operators:
///
///  2. Transform: "(A op B) op C" ==> "A op (B op C)" if "B op C" simplifies.
///  3. Transform: "A op (B op C)" ==> "(A op B) op C" if "A op B" simplifies.
///
///  Associative and commutative operators:
///
///  4. Transform: "(A op B) op C" ==> "(C op A) op B" if "C op A" simplifies.
///  5. Transform: "A op (B op C)" ==> "B op (C op A)" if "C op A" simplifies.
///  6. Transform: "(A op C1) op (B op C2)" ==> "(A op B) op (C1 op C2)"
///     if C1 and C2 are constants.
bool InstCombiner::SimplifyAssociativeOrCommutative(BinaryOperator &I) {
  Instruction::BinaryOps Opcode = I.getOpcode();
  bool Changed = false;

  do {
    // Order operands such that they are listed from right (least complex) to
    // left (most complex).  This puts constants before unary operators before
    // binary operators.
    if (I.isCommutative() && getComplexity(I.getOperand(0)) <
        getComplexity(I.getOperand(1)))
      Changed = !I.swapOperands();

    BinaryOperator *Op0 = dyn_cast<BinaryOperator>(I.getOperand(0));
    BinaryOperator *Op1 = dyn_cast<BinaryOperator>(I.getOperand(1));

    if (I.isAssociative()) {
      // Transform: "(A op B) op C" ==> "A op (B op C)" if "B op C" simplifies.
      if (Op0 && Op0->getOpcode() == Opcode) {
        Value *A = Op0->getOperand(0);
        Value *B = Op0->getOperand(1);
        Value *C = I.getOperand(1);

        // Does "B op C" simplify?
        if (Value *V = SimplifyBinOp(Opcode, B, C, SQ.getWithInstruction(&I))) {
          // It simplifies to V.  Form "A op V".
          I.setOperand(0, A);
          I.setOperand(1, V);
          // Conservatively clear the optional flags, since they may not be
          // preserved by the reassociation.
          if (MaintainNoSignedWrap(I, B, C) &&
              (!Op0 || (isa<BinaryOperator>(Op0) && Op0->hasNoSignedWrap()))) {
            // Note: this is only valid because SimplifyBinOp doesn't look at
            // the operands to Op0.
            I.clearSubclassOptionalData();
            I.setHasNoSignedWrap(true);
          } else {
            ClearSubclassDataAfterReassociation(I);
          }

          Changed = true;
          ++NumReassoc;
          continue;
        }
      }

      // Transform: "A op (B op C)" ==> "(A op B) op C" if "A op B" simplifies.
      if (Op1 && Op1->getOpcode() == Opcode) {
        Value *A = I.getOperand(0);
        Value *B = Op1->getOperand(0);
        Value *C = Op1->getOperand(1);

        // Does "A op B" simplify?
        if (Value *V = SimplifyBinOp(Opcode, A, B, SQ.getWithInstruction(&I))) {
          // It simplifies to V.  Form "V op C".
          I.setOperand(0, V);
          I.setOperand(1, C);
          // Conservatively clear the optional flags, since they may not be
          // preserved by the reassociation.
          ClearSubclassDataAfterReassociation(I);
          Changed = true;
          ++NumReassoc;
          continue;
        }
      }
    }

    if (I.isAssociative() && I.isCommutative()) {
      if (simplifyAssocCastAssoc(&I)) {
        Changed = true;
        ++NumReassoc;
        continue;
      }

      // Transform: "(A op B) op C" ==> "(C op A) op B" if "C op A" simplifies.
      if (Op0 && Op0->getOpcode() == Opcode) {
        Value *A = Op0->getOperand(0);
        Value *B = Op0->getOperand(1);
        Value *C = I.getOperand(1);

        // Does "C op A" simplify?
        if (Value *V = SimplifyBinOp(Opcode, C, A, SQ.getWithInstruction(&I))) {
          // It simplifies to V.  Form "V op B".
          I.setOperand(0, V);
          I.setOperand(1, B);
          // Conservatively clear the optional flags, since they may not be
          // preserved by the reassociation.
          ClearSubclassDataAfterReassociation(I);
          Changed = true;
          ++NumReassoc;
          continue;
        }
      }

      // Transform: "A op (B op C)" ==> "B op (C op A)" if "C op A" simplifies.
      if (Op1 && Op1->getOpcode() == Opcode) {
        Value *A = I.getOperand(0);
        Value *B = Op1->getOperand(0);
        Value *C = Op1->getOperand(1);

        // Does "C op A" simplify?
        if (Value *V = SimplifyBinOp(Opcode, C, A, SQ.getWithInstruction(&I))) {
          // It simplifies to V.  Form "B op V".
          I.setOperand(0, B);
          I.setOperand(1, V);
          // Conservatively clear the optional flags, since they may not be
          // preserved by the reassociation.
          ClearSubclassDataAfterReassociation(I);
          Changed = true;
          ++NumReassoc;
          continue;
        }
      }

      // Transform: "(A op C1) op (B op C2)" ==> "(A op B) op (C1 op C2)"
      // if C1 and C2 are constants.
      Value *A, *B;
      Constant *C1, *C2;
      if (Op0 && Op1 &&
          Op0->getOpcode() == Opcode && Op1->getOpcode() == Opcode &&
          match(Op0, m_OneUse(m_BinOp(m_Value(A), m_Constant(C1)))) &&
          match(Op1, m_OneUse(m_BinOp(m_Value(B), m_Constant(C2))))) {
        BinaryOperator *NewBO = BinaryOperator::Create(Opcode, A, B);
        if (isa<FPMathOperator>(NewBO)) {
          FastMathFlags Flags = I.getFastMathFlags();
          Flags &= Op0->getFastMathFlags();
          Flags &= Op1->getFastMathFlags();
          NewBO->setFastMathFlags(Flags);
        }
        InsertNewInstWith(NewBO, I);
        NewBO->takeName(Op1);
        I.setOperand(0, NewBO);
        I.setOperand(1, ConstantExpr::get(Opcode, C1, C2));
        // Conservatively clear the optional flags, since they may not be
        // preserved by the reassociation.
        ClearSubclassDataAfterReassociation(I);

        Changed = true;
        continue;
      }
    }

    // No further simplifications.
    return Changed;
  } while (true);
}

/// Return whether "X LOp (Y ROp Z)" is always equal to
/// "(X LOp Y) ROp (X LOp Z)".
static bool leftDistributesOverRight(Instruction::BinaryOps LOp,
                                     Instruction::BinaryOps ROp) {
  // X & (Y | Z) <--> (X & Y) | (X & Z)
  // X & (Y ^ Z) <--> (X & Y) ^ (X & Z)
  if (LOp == Instruction::And)
    return ROp == Instruction::Or || ROp == Instruction::Xor;

  // X | (Y & Z) <--> (X | Y) & (X | Z)
  if (LOp == Instruction::Or)
    return ROp == Instruction::And;

  // X * (Y + Z) <--> (X * Y) + (X * Z)
  // X * (Y - Z) <--> (X * Y) - (X * Z)
  if (LOp == Instruction::Mul)
    return ROp == Instruction::Add || ROp == Instruction::Sub;

  return false;
}

/// Return whether "(X LOp Y) ROp Z" is always equal to
/// "(X ROp Z) LOp (Y ROp Z)".
static bool rightDistributesOverLeft(Instruction::BinaryOps LOp,
                                     Instruction::BinaryOps ROp) {
  if (Instruction::isCommutative(ROp))
    return leftDistributesOverRight(ROp, LOp);

  // (X {&|^} Y) >> Z <--> (X >> Z) {&|^} (Y >> Z) for all shifts.
  return Instruction::isBitwiseLogicOp(LOp) && Instruction::isShift(ROp);

  // TODO: It would be nice to handle division, aka "(X + Y)/Z = X/Z + Y/Z",
  // but this requires knowing that the addition does not overflow and other
  // such subtleties.
}

/// This function returns identity value for given opcode, which can be used to
/// factor patterns like (X * 2) + X ==> (X * 2) + (X * 1) ==> X * (2 + 1).
static Value *getIdentityValue(Instruction::BinaryOps Opcode, Value *V) {
  if (isa<Constant>(V))
    return nullptr;

  return ConstantExpr::getBinOpIdentity(Opcode, V->getType());
}

/// This function predicates factorization using distributive laws. By default,
/// it just returns the 'Op' inputs. But for special-cases like
/// 'add(shl(X, 5), ...)', this function will have TopOpcode == Instruction::Add
/// and Op = shl(X, 5). The 'shl' is treated as the more general 'mul X, 32' to
/// allow more factorization opportunities.
static Instruction::BinaryOps
getBinOpsForFactorization(Instruction::BinaryOps TopOpcode, BinaryOperator *Op,
                          Value *&LHS, Value *&RHS) {
  assert(Op && "Expected a binary operator");
  LHS = Op->getOperand(0);
  RHS = Op->getOperand(1);
  if (TopOpcode == Instruction::Add || TopOpcode == Instruction::Sub) {
    Constant *C;
    if (match(Op, m_Shl(m_Value(), m_Constant(C)))) {
      // X << C --> X * (1 << C)
      RHS = ConstantExpr::getShl(ConstantInt::get(Op->getType(), 1), C);
      return Instruction::Mul;
    }
    // TODO: We can add other conversions e.g. shr => div etc.
  }
  return Op->getOpcode();
}

/// This tries to simplify binary operations by factorizing out common terms
/// (e. g. "(A*B)+(A*C)" -> "A*(B+C)").
Value *InstCombiner::tryFactorization(BinaryOperator &I,
                                      Instruction::BinaryOps InnerOpcode,
                                      Value *A, Value *B, Value *C, Value *D) {
  assert(A && B && C && D && "All values must be provided");

  Value *V = nullptr;
  Value *SimplifiedInst = nullptr;
  Value *LHS = I.getOperand(0), *RHS = I.getOperand(1);
  Instruction::BinaryOps TopLevelOpcode = I.getOpcode();

  // Does "X op' Y" always equal "Y op' X"?
  bool InnerCommutative = Instruction::isCommutative(InnerOpcode);

  // Does "X op' (Y op Z)" always equal "(X op' Y) op (X op' Z)"?
  if (leftDistributesOverRight(InnerOpcode, TopLevelOpcode))
    // Does the instruction have the form "(A op' B) op (A op' D)" or, in the
    // commutative case, "(A op' B) op (C op' A)"?
    if (A == C || (InnerCommutative && A == D)) {
      if (A != C)
        std::swap(C, D);
      // Consider forming "A op' (B op D)".
      // If "B op D" simplifies then it can be formed with no cost.
      V = SimplifyBinOp(TopLevelOpcode, B, D, SQ.getWithInstruction(&I));
      // If "B op D" doesn't simplify then only go on if both of the existing
      // operations "A op' B" and "C op' D" will be zapped as no longer used.
      if (!V && LHS->hasOneUse() && RHS->hasOneUse())
        V = Builder.CreateBinOp(TopLevelOpcode, B, D, RHS->getName());
      if (V) {
        SimplifiedInst = Builder.CreateBinOp(InnerOpcode, A, V);
      }
    }

  // Does "(X op Y) op' Z" always equal "(X op' Z) op (Y op' Z)"?
  if (!SimplifiedInst && rightDistributesOverLeft(TopLevelOpcode, InnerOpcode))
    // Does the instruction have the form "(A op' B) op (C op' B)" or, in the
    // commutative case, "(A op' B) op (B op' D)"?
    if (B == D || (InnerCommutative && B == C)) {
      if (B != D)
        std::swap(C, D);
      // Consider forming "(A op C) op' B".
      // If "A op C" simplifies then it can be formed with no cost.
      V = SimplifyBinOp(TopLevelOpcode, A, C, SQ.getWithInstruction(&I));

      // If "A op C" doesn't simplify then only go on if both of the existing
      // operations "A op' B" and "C op' D" will be zapped as no longer used.
      if (!V && LHS->hasOneUse() && RHS->hasOneUse())
        V = Builder.CreateBinOp(TopLevelOpcode, A, C, LHS->getName());
      if (V) {
        SimplifiedInst = Builder.CreateBinOp(InnerOpcode, V, B);
      }
    }

  if (SimplifiedInst) {
    ++NumFactor;
    SimplifiedInst->takeName(&I);

    // Check if we can add NSW flag to SimplifiedInst. If so, set NSW flag.
    // TODO: Check for NUW.
    if (BinaryOperator *BO = dyn_cast<BinaryOperator>(SimplifiedInst)) {
      if (isa<OverflowingBinaryOperator>(SimplifiedInst)) {
        bool HasNSW = false;
        if (isa<OverflowingBinaryOperator>(&I))
          HasNSW = I.hasNoSignedWrap();

        if (auto *LOBO = dyn_cast<OverflowingBinaryOperator>(LHS))
          HasNSW &= LOBO->hasNoSignedWrap();

        if (auto *ROBO = dyn_cast<OverflowingBinaryOperator>(RHS))
          HasNSW &= ROBO->hasNoSignedWrap();

        // We can propagate 'nsw' if we know that
        //  %Y = mul nsw i16 %X, C
        //  %Z = add nsw i16 %Y, %X
        // =>
        //  %Z = mul nsw i16 %X, C+1
        //
        // iff C+1 isn't INT_MIN
        const APInt *CInt;
        if (TopLevelOpcode == Instruction::Add &&
            InnerOpcode == Instruction::Mul)
          if (match(V, m_APInt(CInt)) && !CInt->isMinSignedValue())
            BO->setHasNoSignedWrap(HasNSW);
      }
    }
  }
  return SimplifiedInst;
}

/// This tries to simplify binary operations which some other binary operation
/// distributes over either by factorizing out common terms
/// (eg "(A*B)+(A*C)" -> "A*(B+C)") or expanding out if this results in
/// simplifications (eg: "A & (B | C) -> (A&B) | (A&C)" if this is a win).
/// Returns the simplified value, or null if it didn't simplify.
Value *InstCombiner::SimplifyUsingDistributiveLaws(BinaryOperator &I) {
  Value *LHS = I.getOperand(0), *RHS = I.getOperand(1);
  BinaryOperator *Op0 = dyn_cast<BinaryOperator>(LHS);
  BinaryOperator *Op1 = dyn_cast<BinaryOperator>(RHS);
  Instruction::BinaryOps TopLevelOpcode = I.getOpcode();

  {
    // Factorization.
    Value *A, *B, *C, *D;
    Instruction::BinaryOps LHSOpcode, RHSOpcode;
    if (Op0)
      LHSOpcode = getBinOpsForFactorization(TopLevelOpcode, Op0, A, B);
    if (Op1)
      RHSOpcode = getBinOpsForFactorization(TopLevelOpcode, Op1, C, D);

    // The instruction has the form "(A op' B) op (C op' D)".  Try to factorize
    // a common term.
    if (Op0 && Op1 && LHSOpcode == RHSOpcode)
      if (Value *V = tryFactorization(I, LHSOpcode, A, B, C, D))
        return V;

    // The instruction has the form "(A op' B) op (C)".  Try to factorize common
    // term.
    if (Op0)
      if (Value *Ident = getIdentityValue(LHSOpcode, RHS))
        if (Value *V = tryFactorization(I, LHSOpcode, A, B, RHS, Ident))
          return V;

    // The instruction has the form "(B) op (C op' D)".  Try to factorize common
    // term.
    if (Op1)
      if (Value *Ident = getIdentityValue(RHSOpcode, LHS))
        if (Value *V = tryFactorization(I, RHSOpcode, LHS, Ident, C, D))
          return V;
  }

  // Expansion.
  if (Op0 && rightDistributesOverLeft(Op0->getOpcode(), TopLevelOpcode)) {
    // The instruction has the form "(A op' B) op C".  See if expanding it out
    // to "(A op C) op' (B op C)" results in simplifications.
    Value *A = Op0->getOperand(0), *B = Op0->getOperand(1), *C = RHS;
    Instruction::BinaryOps InnerOpcode = Op0->getOpcode(); // op'

    Value *L = SimplifyBinOp(TopLevelOpcode, A, C, SQ.getWithInstruction(&I));
    Value *R = SimplifyBinOp(TopLevelOpcode, B, C, SQ.getWithInstruction(&I));

    // Do "A op C" and "B op C" both simplify?
    if (L && R) {
      // They do! Return "L op' R".
      ++NumExpand;
      C = Builder.CreateBinOp(InnerOpcode, L, R);
      C->takeName(&I);
      return C;
    }

    // Does "A op C" simplify to the identity value for the inner opcode?
    if (L && L == ConstantExpr::getBinOpIdentity(InnerOpcode, L->getType())) {
      // They do! Return "B op C".
      ++NumExpand;
      C = Builder.CreateBinOp(TopLevelOpcode, B, C);
      C->takeName(&I);
      return C;
    }

    // Does "B op C" simplify to the identity value for the inner opcode?
    if (R && R == ConstantExpr::getBinOpIdentity(InnerOpcode, R->getType())) {
      // They do! Return "A op C".
      ++NumExpand;
      C = Builder.CreateBinOp(TopLevelOpcode, A, C);
      C->takeName(&I);
      return C;
    }
  }

  if (Op1 && leftDistributesOverRight(TopLevelOpcode, Op1->getOpcode())) {
    // The instruction has the form "A op (B op' C)".  See if expanding it out
    // to "(A op B) op' (A op C)" results in simplifications.
    Value *A = LHS, *B = Op1->getOperand(0), *C = Op1->getOperand(1);
    Instruction::BinaryOps InnerOpcode = Op1->getOpcode(); // op'

    Value *L = SimplifyBinOp(TopLevelOpcode, A, B, SQ.getWithInstruction(&I));
    Value *R = SimplifyBinOp(TopLevelOpcode, A, C, SQ.getWithInstruction(&I));

    // Do "A op B" and "A op C" both simplify?
    if (L && R) {
      // They do! Return "L op' R".
      ++NumExpand;
      A = Builder.CreateBinOp(InnerOpcode, L, R);
      A->takeName(&I);
      return A;
    }

    // Does "A op B" simplify to the identity value for the inner opcode?
    if (L && L == ConstantExpr::getBinOpIdentity(InnerOpcode, L->getType())) {
      // They do! Return "A op C".
      ++NumExpand;
      A = Builder.CreateBinOp(TopLevelOpcode, A, C);
      A->takeName(&I);
      return A;
    }

    // Does "A op C" simplify to the identity value for the inner opcode?
    if (R && R == ConstantExpr::getBinOpIdentity(InnerOpcode, R->getType())) {
      // They do! Return "A op B".
      ++NumExpand;
      A = Builder.CreateBinOp(TopLevelOpcode, A, B);
      A->takeName(&I);
      return A;
    }
  }

  return SimplifySelectsFeedingBinaryOp(I, LHS, RHS);
}

Value *InstCombiner::SimplifySelectsFeedingBinaryOp(BinaryOperator &I,
                                                    Value *LHS, Value *RHS) {
  Instruction::BinaryOps Opcode = I.getOpcode();
  // (op (select (a, b, c)), (select (a, d, e))) -> (select (a, (op b, d), (op
  // c, e)))
  Value *A, *B, *C, *D, *E;
  Value *SI = nullptr;
  if (match(LHS, m_Select(m_Value(A), m_Value(B), m_Value(C))) &&
      match(RHS, m_Select(m_Specific(A), m_Value(D), m_Value(E)))) {
    bool SelectsHaveOneUse = LHS->hasOneUse() && RHS->hasOneUse();
    BuilderTy::FastMathFlagGuard Guard(Builder);
    if (isa<FPMathOperator>(&I))
      Builder.setFastMathFlags(I.getFastMathFlags());

    Value *V1 = SimplifyBinOp(Opcode, C, E, SQ.getWithInstruction(&I));
    Value *V2 = SimplifyBinOp(Opcode, B, D, SQ.getWithInstruction(&I));
    if (V1 && V2)
      SI = Builder.CreateSelect(A, V2, V1);
    else if (V2 && SelectsHaveOneUse)
      SI = Builder.CreateSelect(A, V2, Builder.CreateBinOp(Opcode, C, E));
    else if (V1 && SelectsHaveOneUse)
      SI = Builder.CreateSelect(A, Builder.CreateBinOp(Opcode, B, D), V1);

    if (SI)
      SI->takeName(&I);
  }

  return SI;
}

/// Given a 'sub' instruction, return the RHS of the instruction if the LHS is a
/// constant zero (which is the 'negate' form).
Value *InstCombiner::dyn_castNegVal(Value *V) const {
  Value *NegV;
  if (match(V, m_Neg(m_Value(NegV))))
    return NegV;

  // Constants can be considered to be negated values if they can be folded.
  if (ConstantInt *C = dyn_cast<ConstantInt>(V))
    return ConstantExpr::getNeg(C);

  if (ConstantDataVector *C = dyn_cast<ConstantDataVector>(V))
    if (C->getType()->getElementType()->isIntegerTy())
      return ConstantExpr::getNeg(C);

  if (ConstantVector *CV = dyn_cast<ConstantVector>(V)) {
    for (unsigned i = 0, e = CV->getNumOperands(); i != e; ++i) {
      Constant *Elt = CV->getAggregateElement(i);
      if (!Elt)
        return nullptr;

      if (isa<UndefValue>(Elt))
        continue;

      if (!isa<ConstantInt>(Elt))
        return nullptr;
    }
    return ConstantExpr::getNeg(CV);
  }

  return nullptr;
}

static Value *foldOperationIntoSelectOperand(Instruction &I, Value *SO,
                                             InstCombiner::BuilderTy &Builder) {
  if (auto *Cast = dyn_cast<CastInst>(&I))
    return Builder.CreateCast(Cast->getOpcode(), SO, I.getType());

  assert(I.isBinaryOp() && "Unexpected opcode for select folding");

  // Figure out if the constant is the left or the right argument.
  bool ConstIsRHS = isa<Constant>(I.getOperand(1));
  Constant *ConstOperand = cast<Constant>(I.getOperand(ConstIsRHS));

  if (auto *SOC = dyn_cast<Constant>(SO)) {
    if (ConstIsRHS)
      return ConstantExpr::get(I.getOpcode(), SOC, ConstOperand);
    return ConstantExpr::get(I.getOpcode(), ConstOperand, SOC);
  }

  Value *Op0 = SO, *Op1 = ConstOperand;
  if (!ConstIsRHS)
    std::swap(Op0, Op1);

  auto *BO = cast<BinaryOperator>(&I);
  Value *RI = Builder.CreateBinOp(BO->getOpcode(), Op0, Op1,
                                  SO->getName() + ".op");
  auto *FPInst = dyn_cast<Instruction>(RI);
  if (FPInst && isa<FPMathOperator>(FPInst))
    FPInst->copyFastMathFlags(BO);
  return RI;
}

Instruction *InstCombiner::FoldOpIntoSelect(Instruction &Op, SelectInst *SI) {
  // Don't modify shared select instructions.
  if (!SI->hasOneUse())
    return nullptr;

  Value *TV = SI->getTrueValue();
  Value *FV = SI->getFalseValue();
  if (!(isa<Constant>(TV) || isa<Constant>(FV)))
    return nullptr;

  // Bool selects with constant operands can be folded to logical ops.
  if (SI->getType()->isIntOrIntVectorTy(1))
    return nullptr;

  // If it's a bitcast involving vectors, make sure it has the same number of
  // elements on both sides.
  if (auto *BC = dyn_cast<BitCastInst>(&Op)) {
    VectorType *DestTy = dyn_cast<VectorType>(BC->getDestTy());
    VectorType *SrcTy = dyn_cast<VectorType>(BC->getSrcTy());

    // Verify that either both or neither are vectors.
    if ((SrcTy == nullptr) != (DestTy == nullptr))
      return nullptr;

    // If vectors, verify that they have the same number of elements.
    if (SrcTy && SrcTy->getNumElements() != DestTy->getNumElements())
      return nullptr;
  }

  // Test if a CmpInst instruction is used exclusively by a select as
  // part of a minimum or maximum operation. If so, refrain from doing
  // any other folding. This helps out other analyses which understand
  // non-obfuscated minimum and maximum idioms, such as ScalarEvolution
  // and CodeGen. And in this case, at least one of the comparison
  // operands has at least one user besides the compare (the select),
  // which would often largely negate the benefit of folding anyway.
  if (auto *CI = dyn_cast<CmpInst>(SI->getCondition())) {
    if (CI->hasOneUse()) {
      Value *Op0 = CI->getOperand(0), *Op1 = CI->getOperand(1);
      if ((SI->getOperand(1) == Op0 && SI->getOperand(2) == Op1) ||
          (SI->getOperand(2) == Op0 && SI->getOperand(1) == Op1))
        return nullptr;
    }
  }

  Value *NewTV = foldOperationIntoSelectOperand(Op, TV, Builder);
  Value *NewFV = foldOperationIntoSelectOperand(Op, FV, Builder);
  return SelectInst::Create(SI->getCondition(), NewTV, NewFV, "", nullptr, SI);
}

static Value *foldOperationIntoPhiValue(BinaryOperator *I, Value *InV,
                                        InstCombiner::BuilderTy &Builder) {
  bool ConstIsRHS = isa<Constant>(I->getOperand(1));
  Constant *C = cast<Constant>(I->getOperand(ConstIsRHS));

  if (auto *InC = dyn_cast<Constant>(InV)) {
    if (ConstIsRHS)
      return ConstantExpr::get(I->getOpcode(), InC, C);
    return ConstantExpr::get(I->getOpcode(), C, InC);
  }

  Value *Op0 = InV, *Op1 = C;
  if (!ConstIsRHS)
    std::swap(Op0, Op1);

  Value *RI = Builder.CreateBinOp(I->getOpcode(), Op0, Op1, "phitmp");
  auto *FPInst = dyn_cast<Instruction>(RI);
  if (FPInst && isa<FPMathOperator>(FPInst))
    FPInst->copyFastMathFlags(I);
  return RI;
}

Instruction *InstCombiner::foldOpIntoPhi(Instruction &I, PHINode *PN) {
  unsigned NumPHIValues = PN->getNumIncomingValues();
  if (NumPHIValues == 0)
    return nullptr;

  // We normally only transform phis with a single use.  However, if a PHI has
  // multiple uses and they are all the same operation, we can fold *all* of the
  // uses into the PHI.
  if (!PN->hasOneUse()) {
    // Walk the use list for the instruction, comparing them to I.
    for (User *U : PN->users()) {
      Instruction *UI = cast<Instruction>(U);
      if (UI != &I && !I.isIdenticalTo(UI))
        return nullptr;
    }
    // Otherwise, we can replace *all* users with the new PHI we form.
  }

  // Check to see if all of the operands of the PHI are simple constants
  // (constantint/constantfp/undef).  If there is one non-constant value,
  // remember the BB it is in.  If there is more than one or if *it* is a PHI,
  // bail out.  We don't do arbitrary constant expressions here because moving
  // their computation can be expensive without a cost model.
  BasicBlock *NonConstBB = nullptr;
  for (unsigned i = 0; i != NumPHIValues; ++i) {
    Value *InVal = PN->getIncomingValue(i);
    if (isa<Constant>(InVal) && !isa<ConstantExpr>(InVal))
      continue;

    if (isa<PHINode>(InVal)) return nullptr;  // Itself a phi.
    if (NonConstBB) return nullptr;  // More than one non-const value.

    NonConstBB = PN->getIncomingBlock(i);

    // If the InVal is an invoke at the end of the pred block, then we can't
    // insert a computation after it without breaking the edge.
    if (InvokeInst *II = dyn_cast<InvokeInst>(InVal))
      if (II->getParent() == NonConstBB)
        return nullptr;

    // If the incoming non-constant value is in I's block, we will remove one
    // instruction, but insert another equivalent one, leading to infinite
    // instcombine.
    if (isPotentiallyReachable(I.getParent(), NonConstBB, &DT, LI))
      return nullptr;
  }

  // If there is exactly one non-constant value, we can insert a copy of the
  // operation in that block.  However, if this is a critical edge, we would be
  // inserting the computation on some other paths (e.g. inside a loop).  Only
  // do this if the pred block is unconditionally branching into the phi block.
  if (NonConstBB != nullptr) {
    BranchInst *BI = dyn_cast<BranchInst>(NonConstBB->getTerminator());
    if (!BI || !BI->isUnconditional()) return nullptr;
  }

  // Okay, we can do the transformation: create the new PHI node.
  PHINode *NewPN = PHINode::Create(I.getType(), PN->getNumIncomingValues());
  InsertNewInstBefore(NewPN, *PN);
  NewPN->takeName(PN);

  // If we are going to have to insert a new computation, do so right before the
  // predecessor's terminator.
  if (NonConstBB)
    Builder.SetInsertPoint(NonConstBB->getTerminator());

  // Next, add all of the operands to the PHI.
  if (SelectInst *SI = dyn_cast<SelectInst>(&I)) {
    // We only currently try to fold the condition of a select when it is a phi,
    // not the true/false values.
    Value *TrueV = SI->getTrueValue();
    Value *FalseV = SI->getFalseValue();
    BasicBlock *PhiTransBB = PN->getParent();
    for (unsigned i = 0; i != NumPHIValues; ++i) {
      BasicBlock *ThisBB = PN->getIncomingBlock(i);
      Value *TrueVInPred = TrueV->DoPHITranslation(PhiTransBB, ThisBB);
      Value *FalseVInPred = FalseV->DoPHITranslation(PhiTransBB, ThisBB);
      Value *InV = nullptr;
      // Beware of ConstantExpr:  it may eventually evaluate to getNullValue,
      // even if currently isNullValue gives false.
      Constant *InC = dyn_cast<Constant>(PN->getIncomingValue(i));
      // For vector constants, we cannot use isNullValue to fold into
      // FalseVInPred versus TrueVInPred. When we have individual nonzero
      // elements in the vector, we will incorrectly fold InC to
      // `TrueVInPred`.
      if (InC && !isa<ConstantExpr>(InC) && isa<ConstantInt>(InC))
        InV = InC->isNullValue() ? FalseVInPred : TrueVInPred;
      else {
        // Generate the select in the same block as PN's current incoming block.
        // Note: ThisBB need not be the NonConstBB because vector constants
        // which are constants by definition are handled here.
        // FIXME: This can lead to an increase in IR generation because we might
        // generate selects for vector constant phi operand, that could not be
        // folded to TrueVInPred or FalseVInPred as done for ConstantInt. For
        // non-vector phis, this transformation was always profitable because
        // the select would be generated exactly once in the NonConstBB.
        Builder.SetInsertPoint(ThisBB->getTerminator());
        InV = Builder.CreateSelect(PN->getIncomingValue(i), TrueVInPred,
                                   FalseVInPred, "phitmp");
      }
      NewPN->addIncoming(InV, ThisBB);
    }
  } else if (CmpInst *CI = dyn_cast<CmpInst>(&I)) {
    Constant *C = cast<Constant>(I.getOperand(1));
    for (unsigned i = 0; i != NumPHIValues; ++i) {
      Value *InV = nullptr;
      if (Constant *InC = dyn_cast<Constant>(PN->getIncomingValue(i)))
        InV = ConstantExpr::getCompare(CI->getPredicate(), InC, C);
      else if (isa<ICmpInst>(CI))
        InV = Builder.CreateICmp(CI->getPredicate(), PN->getIncomingValue(i),
                                 C, "phitmp");
      else
        InV = Builder.CreateFCmp(CI->getPredicate(), PN->getIncomingValue(i),
                                 C, "phitmp");
      NewPN->addIncoming(InV, PN->getIncomingBlock(i));
    }
  } else if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
    for (unsigned i = 0; i != NumPHIValues; ++i) {
      Value *InV = foldOperationIntoPhiValue(BO, PN->getIncomingValue(i),
                                             Builder);
      NewPN->addIncoming(InV, PN->getIncomingBlock(i));
    }
  } else {
    CastInst *CI = cast<CastInst>(&I);
    Type *RetTy = CI->getType();
    for (unsigned i = 0; i != NumPHIValues; ++i) {
      Value *InV;
      if (Constant *InC = dyn_cast<Constant>(PN->getIncomingValue(i)))
        InV = ConstantExpr::getCast(CI->getOpcode(), InC, RetTy);
      else
        InV = Builder.CreateCast(CI->getOpcode(), PN->getIncomingValue(i),
                                 I.getType(), "phitmp");
      NewPN->addIncoming(InV, PN->getIncomingBlock(i));
    }
  }

  for (auto UI = PN->user_begin(), E = PN->user_end(); UI != E;) {
    Instruction *User = cast<Instruction>(*UI++);
    if (User == &I) continue;
    replaceInstUsesWith(*User, NewPN);
    eraseInstFromFunction(*User);
  }
  return replaceInstUsesWith(I, NewPN);
}

Instruction *InstCombiner::foldBinOpIntoSelectOrPhi(BinaryOperator &I) {
  if (!isa<Constant>(I.getOperand(1)))
    return nullptr;

  if (auto *Sel = dyn_cast<SelectInst>(I.getOperand(0))) {
    if (Instruction *NewSel = FoldOpIntoSelect(I, Sel))
      return NewSel;
  } else if (auto *PN = dyn_cast<PHINode>(I.getOperand(0))) {
    if (Instruction *NewPhi = foldOpIntoPhi(I, PN))
      return NewPhi;
  }
  return nullptr;
}

/// Given a pointer type and a constant offset, determine whether or not there
/// is a sequence of GEP indices into the pointed type that will land us at the
/// specified offset. If so, fill them into NewIndices and return the resultant
/// element type, otherwise return null.
Type *InstCombiner::FindElementAtOffset(PointerType *PtrTy, int64_t Offset,
                                        SmallVectorImpl<Value *> &NewIndices) {
  Type *Ty = PtrTy->getElementType();
  if (!Ty->isSized())
    return nullptr;

  // Start with the index over the outer type.  Note that the type size
  // might be zero (even if the offset isn't zero) if the indexed type
  // is something like [0 x {int, int}]
  Type *IndexTy = DL.getIndexType(PtrTy);
  int64_t FirstIdx = 0;
  if (int64_t TySize = DL.getTypeAllocSize(Ty)) {
    FirstIdx = Offset/TySize;
    Offset -= FirstIdx*TySize;

    // Handle hosts where % returns negative instead of values [0..TySize).
    if (Offset < 0) {
      --FirstIdx;
      Offset += TySize;
      assert(Offset >= 0);
    }
    assert((uint64_t)Offset < (uint64_t)TySize && "Out of range offset");
  }

  NewIndices.push_back(ConstantInt::get(IndexTy, FirstIdx));

  // Index into the types.  If we fail, set OrigBase to null.
  while (Offset) {
    // Indexing into tail padding between struct/array elements.
    if (uint64_t(Offset * 8) >= DL.getTypeSizeInBits(Ty))
      return nullptr;

    if (StructType *STy = dyn_cast<StructType>(Ty)) {
      const StructLayout *SL = DL.getStructLayout(STy);
      assert(Offset < (int64_t)SL->getSizeInBytes() &&
             "Offset must stay within the indexed type");

      unsigned Elt = SL->getElementContainingOffset(Offset);
      NewIndices.push_back(ConstantInt::get(Type::getInt32Ty(Ty->getContext()),
                                            Elt));

      Offset -= SL->getElementOffset(Elt);
      Ty = STy->getElementType(Elt);
    } else if (ArrayType *AT = dyn_cast<ArrayType>(Ty)) {
      uint64_t EltSize = DL.getTypeAllocSize(AT->getElementType());
      assert(EltSize && "Cannot index into a zero-sized array");
      NewIndices.push_back(ConstantInt::get(IndexTy,Offset/EltSize));
      Offset %= EltSize;
      Ty = AT->getElementType();
    } else {
      // Otherwise, we can't index into the middle of this atomic type, bail.
      return nullptr;
    }
  }

  return Ty;
}

static bool shouldMergeGEPs(GEPOperator &GEP, GEPOperator &Src) {
  // If this GEP has only 0 indices, it is the same pointer as
  // Src. If Src is not a trivial GEP too, don't combine
  // the indices.
  if (GEP.hasAllZeroIndices() && !Src.hasAllZeroIndices() &&
      !Src.hasOneUse())
    return false;
  return true;
}

/// Return a value X such that Val = X * Scale, or null if none.
/// If the multiplication is known not to overflow, then NoSignedWrap is set.
Value *InstCombiner::Descale(Value *Val, APInt Scale, bool &NoSignedWrap) {
  assert(isa<IntegerType>(Val->getType()) && "Can only descale integers!");
  assert(cast<IntegerType>(Val->getType())->getBitWidth() ==
         Scale.getBitWidth() && "Scale not compatible with value!");

  // If Val is zero or Scale is one then Val = Val * Scale.
  if (match(Val, m_Zero()) || Scale == 1) {
    NoSignedWrap = true;
    return Val;
  }

  // If Scale is zero then it does not divide Val.
  if (Scale.isMinValue())
    return nullptr;

  // Look through chains of multiplications, searching for a constant that is
  // divisible by Scale.  For example, descaling X*(Y*(Z*4)) by a factor of 4
  // will find the constant factor 4 and produce X*(Y*Z).  Descaling X*(Y*8) by
  // a factor of 4 will produce X*(Y*2).  The principle of operation is to bore
  // down from Val:
  //
  //     Val = M1 * X          ||   Analysis starts here and works down
  //      M1 = M2 * Y          ||   Doesn't descend into terms with more
  //      M2 =  Z * 4          \/   than one use
  //
  // Then to modify a term at the bottom:
  //
  //     Val = M1 * X
  //      M1 =  Z * Y          ||   Replaced M2 with Z
  //
  // Then to work back up correcting nsw flags.

  // Op - the term we are currently analyzing.  Starts at Val then drills down.
  // Replaced with its descaled value before exiting from the drill down loop.
  Value *Op = Val;

  // Parent - initially null, but after drilling down notes where Op came from.
  // In the example above, Parent is (Val, 0) when Op is M1, because M1 is the
  // 0'th operand of Val.
  std::pair<Instruction *, unsigned> Parent;

  // Set if the transform requires a descaling at deeper levels that doesn't
  // overflow.
  bool RequireNoSignedWrap = false;

  // Log base 2 of the scale. Negative if not a power of 2.
  int32_t logScale = Scale.exactLogBase2();

  for (;; Op = Parent.first->getOperand(Parent.second)) { // Drill down
    if (ConstantInt *CI = dyn_cast<ConstantInt>(Op)) {
      // If Op is a constant divisible by Scale then descale to the quotient.
      APInt Quotient(Scale), Remainder(Scale); // Init ensures right bitwidth.
      APInt::sdivrem(CI->getValue(), Scale, Quotient, Remainder);
      if (!Remainder.isMinValue())
        // Not divisible by Scale.
        return nullptr;
      // Replace with the quotient in the parent.
      Op = ConstantInt::get(CI->getType(), Quotient);
      NoSignedWrap = true;
      break;
    }

    if (BinaryOperator *BO = dyn_cast<BinaryOperator>(Op)) {
      if (BO->getOpcode() == Instruction::Mul) {
        // Multiplication.
        NoSignedWrap = BO->hasNoSignedWrap();
        if (RequireNoSignedWrap && !NoSignedWrap)
          return nullptr;

        // There are three cases for multiplication: multiplication by exactly
        // the scale, multiplication by a constant different to the scale, and
        // multiplication by something else.
        Value *LHS = BO->getOperand(0);
        Value *RHS = BO->getOperand(1);

        if (ConstantInt *CI = dyn_cast<ConstantInt>(RHS)) {
          // Multiplication by a constant.
          if (CI->getValue() == Scale) {
            // Multiplication by exactly the scale, replace the multiplication
            // by its left-hand side in the parent.
            Op = LHS;
            break;
          }

          // Otherwise drill down into the constant.
          if (!Op->hasOneUse())
            return nullptr;

          Parent = std::make_pair(BO, 1);
          continue;
        }

        // Multiplication by something else. Drill down into the left-hand side
        // since that's where the reassociate pass puts the good stuff.
        if (!Op->hasOneUse())
          return nullptr;

        Parent = std::make_pair(BO, 0);
        continue;
      }

      if (logScale > 0 && BO->getOpcode() == Instruction::Shl &&
          isa<ConstantInt>(BO->getOperand(1))) {
        // Multiplication by a power of 2.
        NoSignedWrap = BO->hasNoSignedWrap();
        if (RequireNoSignedWrap && !NoSignedWrap)
          return nullptr;

        Value *LHS = BO->getOperand(0);
        int32_t Amt = cast<ConstantInt>(BO->getOperand(1))->
          getLimitedValue(Scale.getBitWidth());
        // Op = LHS << Amt.

        if (Amt == logScale) {
          // Multiplication by exactly the scale, replace the multiplication
          // by its left-hand side in the parent.
          Op = LHS;
          break;
        }
        if (Amt < logScale || !Op->hasOneUse())
          return nullptr;

        // Multiplication by more than the scale.  Reduce the multiplying amount
        // by the scale in the parent.
        Parent = std::make_pair(BO, 1);
        Op = ConstantInt::get(BO->getType(), Amt - logScale);
        break;
      }
    }

    if (!Op->hasOneUse())
      return nullptr;

    if (CastInst *Cast = dyn_cast<CastInst>(Op)) {
      if (Cast->getOpcode() == Instruction::SExt) {
        // Op is sign-extended from a smaller type, descale in the smaller type.
        unsigned SmallSize = Cast->getSrcTy()->getPrimitiveSizeInBits();
        APInt SmallScale = Scale.trunc(SmallSize);
        // Suppose Op = sext X, and we descale X as Y * SmallScale.  We want to
        // descale Op as (sext Y) * Scale.  In order to have
        //   sext (Y * SmallScale) = (sext Y) * Scale
        // some conditions need to hold however: SmallScale must sign-extend to
        // Scale and the multiplication Y * SmallScale should not overflow.
        if (SmallScale.sext(Scale.getBitWidth()) != Scale)
          // SmallScale does not sign-extend to Scale.
          return nullptr;
        assert(SmallScale.exactLogBase2() == logScale);
        // Require that Y * SmallScale must not overflow.
        RequireNoSignedWrap = true;

        // Drill down through the cast.
        Parent = std::make_pair(Cast, 0);
        Scale = SmallScale;
        continue;
      }

      if (Cast->getOpcode() == Instruction::Trunc) {
        // Op is truncated from a larger type, descale in the larger type.
        // Suppose Op = trunc X, and we descale X as Y * sext Scale.  Then
        //   trunc (Y * sext Scale) = (trunc Y) * Scale
        // always holds.  However (trunc Y) * Scale may overflow even if
        // trunc (Y * sext Scale) does not, so nsw flags need to be cleared
        // from this point up in the expression (see later).
        if (RequireNoSignedWrap)
          return nullptr;

        // Drill down through the cast.
        unsigned LargeSize = Cast->getSrcTy()->getPrimitiveSizeInBits();
        Parent = std::make_pair(Cast, 0);
        Scale = Scale.sext(LargeSize);
        if (logScale + 1 == (int32_t)Cast->getType()->getPrimitiveSizeInBits())
          logScale = -1;
        assert(Scale.exactLogBase2() == logScale);
        continue;
      }
    }

    // Unsupported expression, bail out.
    return nullptr;
  }

  // If Op is zero then Val = Op * Scale.
  if (match(Op, m_Zero())) {
    NoSignedWrap = true;
    return Op;
  }

  // We know that we can successfully descale, so from here on we can safely
  // modify the IR.  Op holds the descaled version of the deepest term in the
  // expression.  NoSignedWrap is 'true' if multiplying Op by Scale is known
  // not to overflow.

  if (!Parent.first)
    // The expression only had one term.
    return Op;

  // Rewrite the parent using the descaled version of its operand.
  assert(Parent.first->hasOneUse() && "Drilled down when more than one use!");
  assert(Op != Parent.first->getOperand(Parent.second) &&
         "Descaling was a no-op?");
  Parent.first->setOperand(Parent.second, Op);
  Worklist.Add(Parent.first);

  // Now work back up the expression correcting nsw flags.  The logic is based
  // on the following observation: if X * Y is known not to overflow as a signed
  // multiplication, and Y is replaced by a value Z with smaller absolute value,
  // then X * Z will not overflow as a signed multiplication either.  As we work
  // our way up, having NoSignedWrap 'true' means that the descaled value at the
  // current level has strictly smaller absolute value than the original.
  Instruction *Ancestor = Parent.first;
  do {
    if (BinaryOperator *BO = dyn_cast<BinaryOperator>(Ancestor)) {
      // If the multiplication wasn't nsw then we can't say anything about the
      // value of the descaled multiplication, and we have to clear nsw flags
      // from this point on up.
      bool OpNoSignedWrap = BO->hasNoSignedWrap();
      NoSignedWrap &= OpNoSignedWrap;
      if (NoSignedWrap != OpNoSignedWrap) {
        BO->setHasNoSignedWrap(NoSignedWrap);
        Worklist.Add(Ancestor);
      }
    } else if (Ancestor->getOpcode() == Instruction::Trunc) {
      // The fact that the descaled input to the trunc has smaller absolute
      // value than the original input doesn't tell us anything useful about
      // the absolute values of the truncations.
      NoSignedWrap = false;
    }
    assert((Ancestor->getOpcode() != Instruction::SExt || NoSignedWrap) &&
           "Failed to keep proper track of nsw flags while drilling down?");

    if (Ancestor == Val)
      // Got to the top, all done!
      return Val;

    // Move up one level in the expression.
    assert(Ancestor->hasOneUse() && "Drilled down when more than one use!");
    Ancestor = Ancestor->user_back();
  } while (true);
}

Instruction *InstCombiner::foldVectorBinop(BinaryOperator &Inst) {
  if (!Inst.getType()->isVectorTy()) return nullptr;

  BinaryOperator::BinaryOps Opcode = Inst.getOpcode();
  unsigned NumElts = cast<VectorType>(Inst.getType())->getNumElements();
  Value *LHS = Inst.getOperand(0), *RHS = Inst.getOperand(1);
  assert(cast<VectorType>(LHS->getType())->getNumElements() == NumElts);
  assert(cast<VectorType>(RHS->getType())->getNumElements() == NumElts);

  // If both operands of the binop are vector concatenations, then perform the
  // narrow binop on each pair of the source operands followed by concatenation
  // of the results.
  Value *L0, *L1, *R0, *R1;
  Constant *Mask;
  if (match(LHS, m_ShuffleVector(m_Value(L0), m_Value(L1), m_Constant(Mask))) &&
      match(RHS, m_ShuffleVector(m_Value(R0), m_Value(R1), m_Specific(Mask))) &&
      LHS->hasOneUse() && RHS->hasOneUse() &&
      cast<ShuffleVectorInst>(LHS)->isConcat() &&
      cast<ShuffleVectorInst>(RHS)->isConcat()) {
    // This transform does not have the speculative execution constraint as
    // below because the shuffle is a concatenation. The new binops are
    // operating on exactly the same elements as the existing binop.
    // TODO: We could ease the mask requirement to allow different undef lanes,
    //       but that requires an analysis of the binop-with-undef output value.
    Value *NewBO0 = Builder.CreateBinOp(Opcode, L0, R0);
    if (auto *BO = dyn_cast<BinaryOperator>(NewBO0))
      BO->copyIRFlags(&Inst);
    Value *NewBO1 = Builder.CreateBinOp(Opcode, L1, R1);
    if (auto *BO = dyn_cast<BinaryOperator>(NewBO1))
      BO->copyIRFlags(&Inst);
    return new ShuffleVectorInst(NewBO0, NewBO1, Mask);
  }

  // It may not be safe to reorder shuffles and things like div, urem, etc.
  // because we may trap when executing those ops on unknown vector elements.
  // See PR20059.
  if (!isSafeToSpeculativelyExecute(&Inst))
    return nullptr;

  auto createBinOpShuffle = [&](Value *X, Value *Y, Constant *M) {
    Value *XY = Builder.CreateBinOp(Opcode, X, Y);
    if (auto *BO = dyn_cast<BinaryOperator>(XY))
      BO->copyIRFlags(&Inst);
    return new ShuffleVectorInst(XY, UndefValue::get(XY->getType()), M);
  };

  // If both arguments of the binary operation are shuffles that use the same
  // mask and shuffle within a single vector, move the shuffle after the binop.
  Value *V1, *V2;
  if (match(LHS, m_ShuffleVector(m_Value(V1), m_Undef(), m_Constant(Mask))) &&
      match(RHS, m_ShuffleVector(m_Value(V2), m_Undef(), m_Specific(Mask))) &&
      V1->getType() == V2->getType() &&
      (LHS->hasOneUse() || RHS->hasOneUse() || LHS == RHS)) {
    // Op(shuffle(V1, Mask), shuffle(V2, Mask)) -> shuffle(Op(V1, V2), Mask)
    return createBinOpShuffle(V1, V2, Mask);
  }

  // If one argument is a shuffle within one vector and the other is a constant,
  // try moving the shuffle after the binary operation. This canonicalization
  // intends to move shuffles closer to other shuffles and binops closer to
  // other binops, so they can be folded. It may also enable demanded elements
  // transforms.
  Constant *C;
  if (match(&Inst, m_c_BinOp(
          m_OneUse(m_ShuffleVector(m_Value(V1), m_Undef(), m_Constant(Mask))),
          m_Constant(C))) &&
      V1->getType()->getVectorNumElements() <= NumElts) {
    assert(Inst.getType()->getScalarType() == V1->getType()->getScalarType() &&
           "Shuffle should not change scalar type");

    // Find constant NewC that has property:
    //   shuffle(NewC, ShMask) = C
    // If such constant does not exist (example: ShMask=<0,0> and C=<1,2>)
    // reorder is not possible. A 1-to-1 mapping is not required. Example:
    // ShMask = <1,1,2,2> and C = <5,5,6,6> --> NewC = <undef,5,6,undef>
    bool ConstOp1 = isa<Constant>(RHS);
    SmallVector<int, 16> ShMask;
    ShuffleVectorInst::getShuffleMask(Mask, ShMask);
    unsigned SrcVecNumElts = V1->getType()->getVectorNumElements();
    UndefValue *UndefScalar = UndefValue::get(C->getType()->getScalarType());
    SmallVector<Constant *, 16> NewVecC(SrcVecNumElts, UndefScalar);
    bool MayChange = true;
    for (unsigned I = 0; I < NumElts; ++I) {
      Constant *CElt = C->getAggregateElement(I);
      if (ShMask[I] >= 0) {
        assert(ShMask[I] < (int)NumElts && "Not expecting narrowing shuffle");
        Constant *NewCElt = NewVecC[ShMask[I]];
        // Bail out if:
        // 1. The constant vector contains a constant expression.
        // 2. The shuffle needs an element of the constant vector that can't
        //    be mapped to a new constant vector.
        // 3. This is a widening shuffle that copies elements of V1 into the
        //    extended elements (extending with undef is allowed).
        if (!CElt || (!isa<UndefValue>(NewCElt) && NewCElt != CElt) ||
            I >= SrcVecNumElts) {
          MayChange = false;
          break;
        }
        NewVecC[ShMask[I]] = CElt;
      }
      // If this is a widening shuffle, we must be able to extend with undef
      // elements. If the original binop does not produce an undef in the high
      // lanes, then this transform is not safe.
      // TODO: We could shuffle those non-undef constant values into the
      //       result by using a constant vector (rather than an undef vector)
      //       as operand 1 of the new binop, but that might be too aggressive
      //       for target-independent shuffle creation.
      if (I >= SrcVecNumElts) {
        Constant *MaybeUndef =
            ConstOp1 ? ConstantExpr::get(Opcode, UndefScalar, CElt)
                     : ConstantExpr::get(Opcode, CElt, UndefScalar);
        if (!isa<UndefValue>(MaybeUndef)) {
          MayChange = false;
          break;
        }
      }
    }
    if (MayChange) {
      Constant *NewC = ConstantVector::get(NewVecC);
      // It may not be safe to execute a binop on a vector with undef elements
      // because the entire instruction can be folded to undef or create poison
      // that did not exist in the original code.
      if (Inst.isIntDivRem() || (Inst.isShift() && ConstOp1))
        NewC = getSafeVectorConstantForBinop(Opcode, NewC, ConstOp1);

      // Op(shuffle(V1, Mask), C) -> shuffle(Op(V1, NewC), Mask)
      // Op(C, shuffle(V1, Mask)) -> shuffle(Op(NewC, V1), Mask)
      Value *NewLHS = ConstOp1 ? V1 : NewC;
      Value *NewRHS = ConstOp1 ? NewC : V1;
      return createBinOpShuffle(NewLHS, NewRHS, Mask);
    }
  }

  return nullptr;
}

/// Try to narrow the width of a binop if at least 1 operand is an extend of
/// of a value. This requires a potentially expensive known bits check to make
/// sure the narrow op does not overflow.
Instruction *InstCombiner::narrowMathIfNoOverflow(BinaryOperator &BO) {
  // We need at least one extended operand.
  Value *Op0 = BO.getOperand(0), *Op1 = BO.getOperand(1);

  // If this is a sub, we swap the operands since we always want an extension
  // on the RHS. The LHS can be an extension or a constant.
  if (BO.getOpcode() == Instruction::Sub)
    std::swap(Op0, Op1);

  Value *X;
  bool IsSext = match(Op0, m_SExt(m_Value(X)));
  if (!IsSext && !match(Op0, m_ZExt(m_Value(X))))
    return nullptr;

  // If both operands are the same extension from the same source type and we
  // can eliminate at least one (hasOneUse), this might work.
  CastInst::CastOps CastOpc = IsSext ? Instruction::SExt : Instruction::ZExt;
  Value *Y;
  if (!(match(Op1, m_ZExtOrSExt(m_Value(Y))) && X->getType() == Y->getType() &&
        cast<Operator>(Op1)->getOpcode() == CastOpc &&
        (Op0->hasOneUse() || Op1->hasOneUse()))) {
    // If that did not match, see if we have a suitable constant operand.
    // Truncating and extending must produce the same constant.
    Constant *WideC;
    if (!Op0->hasOneUse() || !match(Op1, m_Constant(WideC)))
      return nullptr;
    Constant *NarrowC = ConstantExpr::getTrunc(WideC, X->getType());
    if (ConstantExpr::getCast(CastOpc, NarrowC, BO.getType()) != WideC)
      return nullptr;
    Y = NarrowC;
  }

  // Swap back now that we found our operands.
  if (BO.getOpcode() == Instruction::Sub)
    std::swap(X, Y);

  // Both operands have narrow versions. Last step: the math must not overflow
  // in the narrow width.
  if (!willNotOverflow(BO.getOpcode(), X, Y, BO, IsSext))
    return nullptr;

  // bo (ext X), (ext Y) --> ext (bo X, Y)
  // bo (ext X), C       --> ext (bo X, C')
  Value *NarrowBO = Builder.CreateBinOp(BO.getOpcode(), X, Y, "narrow");
  if (auto *NewBinOp = dyn_cast<BinaryOperator>(NarrowBO)) {
    if (IsSext)
      NewBinOp->setHasNoSignedWrap();
    else
      NewBinOp->setHasNoUnsignedWrap();
  }
  return CastInst::Create(CastOpc, NarrowBO, BO.getType());
}

Instruction *InstCombiner::visitGetElementPtrInst(GetElementPtrInst &GEP) {
  SmallVector<Value*, 8> Ops(GEP.op_begin(), GEP.op_end());
  Type *GEPType = GEP.getType();
  Type *GEPEltType = GEP.getSourceElementType();
  if (Value *V = SimplifyGEPInst(GEPEltType, Ops, SQ.getWithInstruction(&GEP)))
    return replaceInstUsesWith(GEP, V);

  Value *PtrOp = GEP.getOperand(0);

  // Eliminate unneeded casts for indices, and replace indices which displace
  // by multiples of a zero size type with zero.
  bool MadeChange = false;

  // Index width may not be the same width as pointer width.
  // Data layout chooses the right type based on supported integer types.
  Type *NewScalarIndexTy =
      DL.getIndexType(GEP.getPointerOperandType()->getScalarType());

  gep_type_iterator GTI = gep_type_begin(GEP);
  for (User::op_iterator I = GEP.op_begin() + 1, E = GEP.op_end(); I != E;
       ++I, ++GTI) {
    // Skip indices into struct types.
    if (GTI.isStruct())
      continue;

    Type *IndexTy = (*I)->getType();
    Type *NewIndexType =
        IndexTy->isVectorTy()
            ? VectorType::get(NewScalarIndexTy, IndexTy->getVectorNumElements())
            : NewScalarIndexTy;

    // If the element type has zero size then any index over it is equivalent
    // to an index of zero, so replace it with zero if it is not zero already.
    Type *EltTy = GTI.getIndexedType();
    if (EltTy->isSized() && DL.getTypeAllocSize(EltTy) == 0)
      if (!isa<Constant>(*I) || !cast<Constant>(*I)->isNullValue()) {
        *I = Constant::getNullValue(NewIndexType);
        MadeChange = true;
      }

    if (IndexTy != NewIndexType) {
      // If we are using a wider index than needed for this platform, shrink
      // it to what we need.  If narrower, sign-extend it to what we need.
      // This explicit cast can make subsequent optimizations more obvious.
      *I = Builder.CreateIntCast(*I, NewIndexType, true);
      MadeChange = true;
    }
  }
  if (MadeChange)
    return &GEP;

  // Check to see if the inputs to the PHI node are getelementptr instructions.
  if (auto *PN = dyn_cast<PHINode>(PtrOp)) {
    auto *Op1 = dyn_cast<GetElementPtrInst>(PN->getOperand(0));
    if (!Op1)
      return nullptr;

    // Don't fold a GEP into itself through a PHI node. This can only happen
    // through the back-edge of a loop. Folding a GEP into itself means that
    // the value of the previous iteration needs to be stored in the meantime,
    // thus requiring an additional register variable to be live, but not
    // actually achieving anything (the GEP still needs to be executed once per
    // loop iteration).
    if (Op1 == &GEP)
      return nullptr;

    int DI = -1;

    for (auto I = PN->op_begin()+1, E = PN->op_end(); I !=E; ++I) {
      auto *Op2 = dyn_cast<GetElementPtrInst>(*I);
      if (!Op2 || Op1->getNumOperands() != Op2->getNumOperands())
        return nullptr;

      // As for Op1 above, don't try to fold a GEP into itself.
      if (Op2 == &GEP)
        return nullptr;

      // Keep track of the type as we walk the GEP.
      Type *CurTy = nullptr;

      for (unsigned J = 0, F = Op1->getNumOperands(); J != F; ++J) {
        if (Op1->getOperand(J)->getType() != Op2->getOperand(J)->getType())
          return nullptr;

        if (Op1->getOperand(J) != Op2->getOperand(J)) {
          if (DI == -1) {
            // We have not seen any differences yet in the GEPs feeding the
            // PHI yet, so we record this one if it is allowed to be a
            // variable.

            // The first two arguments can vary for any GEP, the rest have to be
            // static for struct slots
            if (J > 1 && CurTy->isStructTy())
              return nullptr;

            DI = J;
          } else {
            // The GEP is different by more than one input. While this could be
            // extended to support GEPs that vary by more than one variable it
            // doesn't make sense since it greatly increases the complexity and
            // would result in an R+R+R addressing mode which no backend
            // directly supports and would need to be broken into several
            // simpler instructions anyway.
            return nullptr;
          }
        }

        // Sink down a layer of the type for the next iteration.
        if (J > 0) {
          if (J == 1) {
            CurTy = Op1->getSourceElementType();
          } else if (auto *CT = dyn_cast<CompositeType>(CurTy)) {
            CurTy = CT->getTypeAtIndex(Op1->getOperand(J));
          } else {
            CurTy = nullptr;
          }
        }
      }
    }

    // If not all GEPs are identical we'll have to create a new PHI node.
    // Check that the old PHI node has only one use so that it will get
    // removed.
    if (DI != -1 && !PN->hasOneUse())
      return nullptr;

    auto *NewGEP = cast<GetElementPtrInst>(Op1->clone());
    if (DI == -1) {
      // All the GEPs feeding the PHI are identical. Clone one down into our
      // BB so that it can be merged with the current GEP.
      GEP.getParent()->getInstList().insert(
          GEP.getParent()->getFirstInsertionPt(), NewGEP);
    } else {
      // All the GEPs feeding the PHI differ at a single offset. Clone a GEP
      // into the current block so it can be merged, and create a new PHI to
      // set that index.
      PHINode *NewPN;
      {
        IRBuilderBase::InsertPointGuard Guard(Builder);
        Builder.SetInsertPoint(PN);
        NewPN = Builder.CreatePHI(Op1->getOperand(DI)->getType(),
                                  PN->getNumOperands());
      }

      for (auto &I : PN->operands())
        NewPN->addIncoming(cast<GEPOperator>(I)->getOperand(DI),
                           PN->getIncomingBlock(I));

      NewGEP->setOperand(DI, NewPN);
      GEP.getParent()->getInstList().insert(
          GEP.getParent()->getFirstInsertionPt(), NewGEP);
      NewGEP->setOperand(DI, NewPN);
    }

    GEP.setOperand(0, NewGEP);
    PtrOp = NewGEP;
  }

  // Combine Indices - If the source pointer to this getelementptr instruction
  // is a getelementptr instruction, combine the indices of the two
  // getelementptr instructions into a single instruction.
  if (auto *Src = dyn_cast<GEPOperator>(PtrOp)) {
    if (!shouldMergeGEPs(*cast<GEPOperator>(&GEP), *Src))
      return nullptr;

    // Try to reassociate loop invariant GEP chains to enable LICM.
    if (LI && Src->getNumOperands() == 2 && GEP.getNumOperands() == 2 &&
        Src->hasOneUse()) {
      if (Loop *L = LI->getLoopFor(GEP.getParent())) {
        Value *GO1 = GEP.getOperand(1);
        Value *SO1 = Src->getOperand(1);
        // Reassociate the two GEPs if SO1 is variant in the loop and GO1 is
        // invariant: this breaks the dependence between GEPs and allows LICM
        // to hoist the invariant part out of the loop.
        if (L->isLoopInvariant(GO1) && !L->isLoopInvariant(SO1)) {
          // We have to be careful here.
          // We have something like:
          //  %src = getelementptr <ty>, <ty>* %base, <ty> %idx
          //  %gep = getelementptr <ty>, <ty>* %src, <ty> %idx2
          // If we just swap idx & idx2 then we could inadvertantly
          // change %src from a vector to a scalar, or vice versa.
          // Cases:
          //  1) %base a scalar & idx a scalar & idx2 a vector
          //      => Swapping idx & idx2 turns %src into a vector type.
          //  2) %base a scalar & idx a vector & idx2 a scalar
          //      => Swapping idx & idx2 turns %src in a scalar type
          //  3) %base, %idx, and %idx2 are scalars
          //      => %src & %gep are scalars
          //      => swapping idx & idx2 is safe
          //  4) %base a vector
          //      => %src is a vector
          //      => swapping idx & idx2 is safe.
          auto *SO0 = Src->getOperand(0);
          auto *SO0Ty = SO0->getType();
          if (!isa<VectorType>(GEPType) || // case 3
              isa<VectorType>(SO0Ty)) {    // case 4
            Src->setOperand(1, GO1);
            GEP.setOperand(1, SO1);
            return &GEP;
          } else {
            // Case 1 or 2
            // -- have to recreate %src & %gep
            // put NewSrc at same location as %src
            Builder.SetInsertPoint(cast<Instruction>(PtrOp));
            auto *NewSrc = cast<GetElementPtrInst>(
                Builder.CreateGEP(SO0, GO1, Src->getName()));
            NewSrc->setIsInBounds(Src->isInBounds());
            auto *NewGEP = GetElementPtrInst::Create(nullptr, NewSrc, {SO1});
            NewGEP->setIsInBounds(GEP.isInBounds());
            return NewGEP;
          }
        }
      }
    }

    // Note that if our source is a gep chain itself then we wait for that
    // chain to be resolved before we perform this transformation.  This
    // avoids us creating a TON of code in some cases.
    if (auto *SrcGEP = dyn_cast<GEPOperator>(Src->getOperand(0)))
      if (SrcGEP->getNumOperands() == 2 && shouldMergeGEPs(*Src, *SrcGEP))
        return nullptr;   // Wait until our source is folded to completion.

    SmallVector<Value*, 8> Indices;

    // Find out whether the last index in the source GEP is a sequential idx.
    bool EndsWithSequential = false;
    for (gep_type_iterator I = gep_type_begin(*Src), E = gep_type_end(*Src);
         I != E; ++I)
      EndsWithSequential = I.isSequential();

    // Can we combine the two pointer arithmetics offsets?
    if (EndsWithSequential) {
      // Replace: gep (gep %P, long B), long A, ...
      // With:    T = long A+B; gep %P, T, ...
      Value *SO1 = Src->getOperand(Src->getNumOperands()-1);
      Value *GO1 = GEP.getOperand(1);

      // If they aren't the same type, then the input hasn't been processed
      // by the loop above yet (which canonicalizes sequential index types to
      // intptr_t).  Just avoid transforming this until the input has been
      // normalized.
      if (SO1->getType() != GO1->getType())
        return nullptr;

      Value *Sum =
          SimplifyAddInst(GO1, SO1, false, false, SQ.getWithInstruction(&GEP));
      // Only do the combine when we are sure the cost after the
      // merge is never more than that before the merge.
      if (Sum == nullptr)
        return nullptr;

      // Update the GEP in place if possible.
      if (Src->getNumOperands() == 2) {
        GEP.setOperand(0, Src->getOperand(0));
        GEP.setOperand(1, Sum);
        return &GEP;
      }
      Indices.append(Src->op_begin()+1, Src->op_end()-1);
      Indices.push_back(Sum);
      Indices.append(GEP.op_begin()+2, GEP.op_end());
    } else if (isa<Constant>(*GEP.idx_begin()) &&
               cast<Constant>(*GEP.idx_begin())->isNullValue() &&
               Src->getNumOperands() != 1) {
      // Otherwise we can do the fold if the first index of the GEP is a zero
      Indices.append(Src->op_begin()+1, Src->op_end());
      Indices.append(GEP.idx_begin()+1, GEP.idx_end());
    }

    if (!Indices.empty())
      return GEP.isInBounds() && Src->isInBounds()
                 ? GetElementPtrInst::CreateInBounds(
                       Src->getSourceElementType(), Src->getOperand(0), Indices,
                       GEP.getName())
                 : GetElementPtrInst::Create(Src->getSourceElementType(),
                                             Src->getOperand(0), Indices,
                                             GEP.getName());
  }

  if (GEP.getNumIndices() == 1) {
    unsigned AS = GEP.getPointerAddressSpace();
    if (GEP.getOperand(1)->getType()->getScalarSizeInBits() ==
        DL.getIndexSizeInBits(AS)) {
      uint64_t TyAllocSize = DL.getTypeAllocSize(GEPEltType);

      bool Matched = false;
      uint64_t C;
      Value *V = nullptr;
      if (TyAllocSize == 1) {
        V = GEP.getOperand(1);
        Matched = true;
      } else if (match(GEP.getOperand(1),
                       m_AShr(m_Value(V), m_ConstantInt(C)))) {
        if (TyAllocSize == 1ULL << C)
          Matched = true;
      } else if (match(GEP.getOperand(1),
                       m_SDiv(m_Value(V), m_ConstantInt(C)))) {
        if (TyAllocSize == C)
          Matched = true;
      }

      if (Matched) {
        // Canonicalize (gep i8* X, -(ptrtoint Y))
        // to (inttoptr (sub (ptrtoint X), (ptrtoint Y)))
        // The GEP pattern is emitted by the SCEV expander for certain kinds of
        // pointer arithmetic.
        if (match(V, m_Neg(m_PtrToInt(m_Value())))) {
          Operator *Index = cast<Operator>(V);
          Value *PtrToInt = Builder.CreatePtrToInt(PtrOp, Index->getType());
          Value *NewSub = Builder.CreateSub(PtrToInt, Index->getOperand(1));
          return CastInst::Create(Instruction::IntToPtr, NewSub, GEPType);
        }
        // Canonicalize (gep i8* X, (ptrtoint Y)-(ptrtoint X))
        // to (bitcast Y)
        Value *Y;
        if (match(V, m_Sub(m_PtrToInt(m_Value(Y)),
                           m_PtrToInt(m_Specific(GEP.getOperand(0))))))
          return CastInst::CreatePointerBitCastOrAddrSpaceCast(Y, GEPType);
      }
    }
  }

  // We do not handle pointer-vector geps here.
  if (GEPType->isVectorTy())
    return nullptr;

  // Handle gep(bitcast x) and gep(gep x, 0, 0, 0).
  Value *StrippedPtr = PtrOp->stripPointerCasts();
  PointerType *StrippedPtrTy = cast<PointerType>(StrippedPtr->getType());

  if (StrippedPtr != PtrOp) {
    bool HasZeroPointerIndex = false;
    if (auto *C = dyn_cast<ConstantInt>(GEP.getOperand(1)))
      HasZeroPointerIndex = C->isZero();

    // Transform: GEP (bitcast [10 x i8]* X to [0 x i8]*), i32 0, ...
    // into     : GEP [10 x i8]* X, i32 0, ...
    //
    // Likewise, transform: GEP (bitcast i8* X to [0 x i8]*), i32 0, ...
    //           into     : GEP i8* X, ...
    //
    // This occurs when the program declares an array extern like "int X[];"
    if (HasZeroPointerIndex) {
      if (auto *CATy = dyn_cast<ArrayType>(GEPEltType)) {
        // GEP (bitcast i8* X to [0 x i8]*), i32 0, ... ?
        if (CATy->getElementType() == StrippedPtrTy->getElementType()) {
          // -> GEP i8* X, ...
          SmallVector<Value*, 8> Idx(GEP.idx_begin()+1, GEP.idx_end());
          GetElementPtrInst *Res = GetElementPtrInst::Create(
              StrippedPtrTy->getElementType(), StrippedPtr, Idx, GEP.getName());
          Res->setIsInBounds(GEP.isInBounds());
          if (StrippedPtrTy->getAddressSpace() == GEP.getAddressSpace())
            return Res;
          // Insert Res, and create an addrspacecast.
          // e.g.,
          // GEP (addrspacecast i8 addrspace(1)* X to [0 x i8]*), i32 0, ...
          // ->
          // %0 = GEP i8 addrspace(1)* X, ...
          // addrspacecast i8 addrspace(1)* %0 to i8*
          return new AddrSpaceCastInst(Builder.Insert(Res), GEPType);
        }

        if (auto *XATy = dyn_cast<ArrayType>(StrippedPtrTy->getElementType())) {
          // GEP (bitcast [10 x i8]* X to [0 x i8]*), i32 0, ... ?
          if (CATy->getElementType() == XATy->getElementType()) {
            // -> GEP [10 x i8]* X, i32 0, ...
            // At this point, we know that the cast source type is a pointer
            // to an array of the same type as the destination pointer
            // array.  Because the array type is never stepped over (there
            // is a leading zero) we can fold the cast into this GEP.
            if (StrippedPtrTy->getAddressSpace() == GEP.getAddressSpace()) {
              GEP.setOperand(0, StrippedPtr);
              GEP.setSourceElementType(XATy);
              return &GEP;
            }
            // Cannot replace the base pointer directly because StrippedPtr's
            // address space is different. Instead, create a new GEP followed by
            // an addrspacecast.
            // e.g.,
            // GEP (addrspacecast [10 x i8] addrspace(1)* X to [0 x i8]*),
            //   i32 0, ...
            // ->
            // %0 = GEP [10 x i8] addrspace(1)* X, ...
            // addrspacecast i8 addrspace(1)* %0 to i8*
            SmallVector<Value*, 8> Idx(GEP.idx_begin(), GEP.idx_end());
            Value *NewGEP = GEP.isInBounds()
                                ? Builder.CreateInBoundsGEP(
                                      nullptr, StrippedPtr, Idx, GEP.getName())
                                : Builder.CreateGEP(nullptr, StrippedPtr, Idx,
                                                    GEP.getName());
            return new AddrSpaceCastInst(NewGEP, GEPType);
          }
        }
      }
    } else if (GEP.getNumOperands() == 2) {
      // Transform things like:
      // %t = getelementptr i32* bitcast ([2 x i32]* %str to i32*), i32 %V
      // into:  %t1 = getelementptr [2 x i32]* %str, i32 0, i32 %V; bitcast
      Type *SrcEltTy = StrippedPtrTy->getElementType();
      if (SrcEltTy->isArrayTy() &&
          DL.getTypeAllocSize(SrcEltTy->getArrayElementType()) ==
              DL.getTypeAllocSize(GEPEltType)) {
        Type *IdxType = DL.getIndexType(GEPType);
        Value *Idx[2] = { Constant::getNullValue(IdxType), GEP.getOperand(1) };
        Value *NewGEP =
            GEP.isInBounds()
                ? Builder.CreateInBoundsGEP(nullptr, StrippedPtr, Idx,
                                            GEP.getName())
                : Builder.CreateGEP(nullptr, StrippedPtr, Idx, GEP.getName());

        // V and GEP are both pointer types --> BitCast
        return CastInst::CreatePointerBitCastOrAddrSpaceCast(NewGEP, GEPType);
      }

      // Transform things like:
      // %V = mul i64 %N, 4
      // %t = getelementptr i8* bitcast (i32* %arr to i8*), i32 %V
      // into:  %t1 = getelementptr i32* %arr, i32 %N; bitcast
      if (GEPEltType->isSized() && SrcEltTy->isSized()) {
        // Check that changing the type amounts to dividing the index by a scale
        // factor.
        uint64_t ResSize = DL.getTypeAllocSize(GEPEltType);
        uint64_t SrcSize = DL.getTypeAllocSize(SrcEltTy);
        if (ResSize && SrcSize % ResSize == 0) {
          Value *Idx = GEP.getOperand(1);
          unsigned BitWidth = Idx->getType()->getPrimitiveSizeInBits();
          uint64_t Scale = SrcSize / ResSize;

          // Earlier transforms ensure that the index has the right type
          // according to Data Layout, which considerably simplifies the
          // logic by eliminating implicit casts.
          assert(Idx->getType() == DL.getIndexType(GEPType) &&
                 "Index type does not match the Data Layout preferences");

          bool NSW;
          if (Value *NewIdx = Descale(Idx, APInt(BitWidth, Scale), NSW)) {
            // Successfully decomposed Idx as NewIdx * Scale, form a new GEP.
            // If the multiplication NewIdx * Scale may overflow then the new
            // GEP may not be "inbounds".
            Value *NewGEP =
                GEP.isInBounds() && NSW
                    ? Builder.CreateInBoundsGEP(nullptr, StrippedPtr, NewIdx,
                                                GEP.getName())
                    : Builder.CreateGEP(nullptr, StrippedPtr, NewIdx,
                                        GEP.getName());

            // The NewGEP must be pointer typed, so must the old one -> BitCast
            return CastInst::CreatePointerBitCastOrAddrSpaceCast(NewGEP,
                                                                 GEPType);
          }
        }
      }

      // Similarly, transform things like:
      // getelementptr i8* bitcast ([100 x double]* X to i8*), i32 %tmp
      //   (where tmp = 8*tmp2) into:
      // getelementptr [100 x double]* %arr, i32 0, i32 %tmp2; bitcast
      if (GEPEltType->isSized() && SrcEltTy->isSized() &&
          SrcEltTy->isArrayTy()) {
        // Check that changing to the array element type amounts to dividing the
        // index by a scale factor.
        uint64_t ResSize = DL.getTypeAllocSize(GEPEltType);
        uint64_t ArrayEltSize =
            DL.getTypeAllocSize(SrcEltTy->getArrayElementType());
        if (ResSize && ArrayEltSize % ResSize == 0) {
          Value *Idx = GEP.getOperand(1);
          unsigned BitWidth = Idx->getType()->getPrimitiveSizeInBits();
          uint64_t Scale = ArrayEltSize / ResSize;

          // Earlier transforms ensure that the index has the right type
          // according to the Data Layout, which considerably simplifies
          // the logic by eliminating implicit casts.
          assert(Idx->getType() == DL.getIndexType(GEPType) &&
                 "Index type does not match the Data Layout preferences");

          bool NSW;
          if (Value *NewIdx = Descale(Idx, APInt(BitWidth, Scale), NSW)) {
            // Successfully decomposed Idx as NewIdx * Scale, form a new GEP.
            // If the multiplication NewIdx * Scale may overflow then the new
            // GEP may not be "inbounds".
            Type *IndTy = DL.getIndexType(GEPType);
            Value *Off[2] = {Constant::getNullValue(IndTy), NewIdx};

            Value *NewGEP = GEP.isInBounds() && NSW
                                ? Builder.CreateInBoundsGEP(
                                      SrcEltTy, StrippedPtr, Off, GEP.getName())
                                : Builder.CreateGEP(SrcEltTy, StrippedPtr, Off,
                                                    GEP.getName());
            // The NewGEP must be pointer typed, so must the old one -> BitCast
            return CastInst::CreatePointerBitCastOrAddrSpaceCast(NewGEP,
                                                                 GEPType);
          }
        }
      }
    }
  }

  // addrspacecast between types is canonicalized as a bitcast, then an
  // addrspacecast. To take advantage of the below bitcast + struct GEP, look
  // through the addrspacecast.
  Value *ASCStrippedPtrOp = PtrOp;
  if (auto *ASC = dyn_cast<AddrSpaceCastInst>(PtrOp)) {
    //   X = bitcast A addrspace(1)* to B addrspace(1)*
    //   Y = addrspacecast A addrspace(1)* to B addrspace(2)*
    //   Z = gep Y, <...constant indices...>
    // Into an addrspacecasted GEP of the struct.
    if (auto *BC = dyn_cast<BitCastInst>(ASC->getOperand(0)))
      ASCStrippedPtrOp = BC;
  }

  if (auto *BCI = dyn_cast<BitCastInst>(ASCStrippedPtrOp)) {
    Value *SrcOp = BCI->getOperand(0);
    PointerType *SrcType = cast<PointerType>(BCI->getSrcTy());
    Type *SrcEltType = SrcType->getElementType();

    // GEP directly using the source operand if this GEP is accessing an element
    // of a bitcasted pointer to vector or array of the same dimensions:
    // gep (bitcast <c x ty>* X to [c x ty]*), Y, Z --> gep X, Y, Z
    // gep (bitcast [c x ty]* X to <c x ty>*), Y, Z --> gep X, Y, Z
    auto areMatchingArrayAndVecTypes = [](Type *ArrTy, Type *VecTy) {
      return ArrTy->getArrayElementType() == VecTy->getVectorElementType() &&
             ArrTy->getArrayNumElements() == VecTy->getVectorNumElements();
    };
    if (GEP.getNumOperands() == 3 &&
        ((GEPEltType->isArrayTy() && SrcEltType->isVectorTy() &&
          areMatchingArrayAndVecTypes(GEPEltType, SrcEltType)) ||
         (GEPEltType->isVectorTy() && SrcEltType->isArrayTy() &&
          areMatchingArrayAndVecTypes(SrcEltType, GEPEltType)))) {

      // Create a new GEP here, as using `setOperand()` followed by
      // `setSourceElementType()` won't actually update the type of the
      // existing GEP Value. Causing issues if this Value is accessed when
      // constructing an AddrSpaceCastInst
      Value *NGEP =
          GEP.isInBounds()
              ? Builder.CreateInBoundsGEP(nullptr, SrcOp, {Ops[1], Ops[2]})
              : Builder.CreateGEP(nullptr, SrcOp, {Ops[1], Ops[2]});
      NGEP->takeName(&GEP);

      // Preserve GEP address space to satisfy users
      if (NGEP->getType()->getPointerAddressSpace() != GEP.getAddressSpace())
        return new AddrSpaceCastInst(NGEP, GEPType);

      return replaceInstUsesWith(GEP, NGEP);
    }

    // See if we can simplify:
    //   X = bitcast A* to B*
    //   Y = gep X, <...constant indices...>
    // into a gep of the original struct. This is important for SROA and alias
    // analysis of unions. If "A" is also a bitcast, wait for A/X to be merged.
    unsigned OffsetBits = DL.getIndexTypeSizeInBits(GEPType);
    APInt Offset(OffsetBits, 0);
    if (!isa<BitCastInst>(SrcOp) && GEP.accumulateConstantOffset(DL, Offset)) {
      // If this GEP instruction doesn't move the pointer, just replace the GEP
      // with a bitcast of the real input to the dest type.
      if (!Offset) {
        // If the bitcast is of an allocation, and the allocation will be
        // converted to match the type of the cast, don't touch this.
        if (isa<AllocaInst>(SrcOp) || isAllocationFn(SrcOp, &TLI)) {
          // See if the bitcast simplifies, if so, don't nuke this GEP yet.
          if (Instruction *I = visitBitCast(*BCI)) {
            if (I != BCI) {
              I->takeName(BCI);
              BCI->getParent()->getInstList().insert(BCI->getIterator(), I);
              replaceInstUsesWith(*BCI, I);
            }
            return &GEP;
          }
        }

        if (SrcType->getPointerAddressSpace() != GEP.getAddressSpace())
          return new AddrSpaceCastInst(SrcOp, GEPType);
        return new BitCastInst(SrcOp, GEPType);
      }

      // Otherwise, if the offset is non-zero, we need to find out if there is a
      // field at Offset in 'A's type.  If so, we can pull the cast through the
      // GEP.
      SmallVector<Value*, 8> NewIndices;
      if (FindElementAtOffset(SrcType, Offset.getSExtValue(), NewIndices)) {
        Value *NGEP =
            GEP.isInBounds()
                ? Builder.CreateInBoundsGEP(nullptr, SrcOp, NewIndices)
                : Builder.CreateGEP(nullptr, SrcOp, NewIndices);

        if (NGEP->getType() == GEPType)
          return replaceInstUsesWith(GEP, NGEP);
        NGEP->takeName(&GEP);

        if (NGEP->getType()->getPointerAddressSpace() != GEP.getAddressSpace())
          return new AddrSpaceCastInst(NGEP, GEPType);
        return new BitCastInst(NGEP, GEPType);
      }
    }
  }

  if (!GEP.isInBounds()) {
    unsigned IdxWidth =
        DL.getIndexSizeInBits(PtrOp->getType()->getPointerAddressSpace());
    APInt BasePtrOffset(IdxWidth, 0);
    Value *UnderlyingPtrOp =
            PtrOp->stripAndAccumulateInBoundsConstantOffsets(DL,
                                                             BasePtrOffset);
    if (auto *AI = dyn_cast<AllocaInst>(UnderlyingPtrOp)) {
      if (GEP.accumulateConstantOffset(DL, BasePtrOffset) &&
          BasePtrOffset.isNonNegative()) {
        APInt AllocSize(IdxWidth, DL.getTypeAllocSize(AI->getAllocatedType()));
        if (BasePtrOffset.ule(AllocSize)) {
          return GetElementPtrInst::CreateInBounds(
              PtrOp, makeArrayRef(Ops).slice(1), GEP.getName());
        }
      }
    }
  }

  return nullptr;
}

static bool isNeverEqualToUnescapedAlloc(Value *V, const TargetLibraryInfo *TLI,
                                         Instruction *AI) {
  if (isa<ConstantPointerNull>(V))
    return true;
  if (auto *LI = dyn_cast<LoadInst>(V))
    return isa<GlobalVariable>(LI->getPointerOperand());
  // Two distinct allocations will never be equal.
  // We rely on LookThroughBitCast in isAllocLikeFn being false, since looking
  // through bitcasts of V can cause
  // the result statement below to be true, even when AI and V (ex:
  // i8* ->i32* ->i8* of AI) are the same allocations.
  return isAllocLikeFn(V, TLI) && V != AI;
}

static bool isAllocSiteRemovable(Instruction *AI,
                                 SmallVectorImpl<WeakTrackingVH> &Users,
                                 const TargetLibraryInfo *TLI) {
  SmallVector<Instruction*, 4> Worklist;
  Worklist.push_back(AI);

  do {
    Instruction *PI = Worklist.pop_back_val();
    for (User *U : PI->users()) {
      Instruction *I = cast<Instruction>(U);
      switch (I->getOpcode()) {
      default:
        // Give up the moment we see something we can't handle.
        return false;

      case Instruction::AddrSpaceCast:
      case Instruction::BitCast:
      case Instruction::GetElementPtr:
        Users.emplace_back(I);
        Worklist.push_back(I);
        continue;

      case Instruction::ICmp: {
        ICmpInst *ICI = cast<ICmpInst>(I);
        // We can fold eq/ne comparisons with null to false/true, respectively.
        // We also fold comparisons in some conditions provided the alloc has
        // not escaped (see isNeverEqualToUnescapedAlloc).
        if (!ICI->isEquality())
          return false;
        unsigned OtherIndex = (ICI->getOperand(0) == PI) ? 1 : 0;
        if (!isNeverEqualToUnescapedAlloc(ICI->getOperand(OtherIndex), TLI, AI))
          return false;
        Users.emplace_back(I);
        continue;
      }

      case Instruction::Call:
        // Ignore no-op and store intrinsics.
        if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
          switch (II->getIntrinsicID()) {
          default:
            return false;

          case Intrinsic::memmove:
          case Intrinsic::memcpy:
          case Intrinsic::memset: {
            MemIntrinsic *MI = cast<MemIntrinsic>(II);
            if (MI->isVolatile() || MI->getRawDest() != PI)
              return false;
            LLVM_FALLTHROUGH;
          }
          case Intrinsic::invariant_start:
          case Intrinsic::invariant_end:
          case Intrinsic::lifetime_start:
          case Intrinsic::lifetime_end:
          case Intrinsic::objectsize:
            Users.emplace_back(I);
            continue;
          }
        }

        if (isFreeCall(I, TLI)) {
          Users.emplace_back(I);
          continue;
        }
        return false;

      case Instruction::Store: {
        StoreInst *SI = cast<StoreInst>(I);
        if (SI->isVolatile() || SI->getPointerOperand() != PI)
          return false;
        Users.emplace_back(I);
        continue;
      }
      }
      llvm_unreachable("missing a return?");
    }
  } while (!Worklist.empty());
  return true;
}

Instruction *InstCombiner::visitAllocSite(Instruction &MI) {
  // If we have a malloc call which is only used in any amount of comparisons to
  // null and free calls, delete the calls and replace the comparisons with true
  // or false as appropriate.

  // This is based on the principle that we can substitute our own allocation
  // function (which will never return null) rather than knowledge of the
  // specific function being called. In some sense this can change the permitted
  // outputs of a program (when we convert a malloc to an alloca, the fact that
  // the allocation is now on the stack is potentially visible, for example),
  // but we believe in a permissible manner.
  SmallVector<WeakTrackingVH, 64> Users;

  // If we are removing an alloca with a dbg.declare, insert dbg.value calls
  // before each store.
  TinyPtrVector<DbgVariableIntrinsic *> DIIs;
  std::unique_ptr<DIBuilder> DIB;
  if (isa<AllocaInst>(MI)) {
    DIIs = FindDbgAddrUses(&MI);
    DIB.reset(new DIBuilder(*MI.getModule(), /*AllowUnresolved=*/false));
  }

  if (isAllocSiteRemovable(&MI, Users, &TLI)) {
    for (unsigned i = 0, e = Users.size(); i != e; ++i) {
      // Lowering all @llvm.objectsize calls first because they may
      // use a bitcast/GEP of the alloca we are removing.
      if (!Users[i])
       continue;

      Instruction *I = cast<Instruction>(&*Users[i]);

      if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
        if (II->getIntrinsicID() == Intrinsic::objectsize) {
          ConstantInt *Result = lowerObjectSizeCall(II, DL, &TLI,
                                                    /*MustSucceed=*/true);
          replaceInstUsesWith(*I, Result);
          eraseInstFromFunction(*I);
          Users[i] = nullptr; // Skip examining in the next loop.
        }
      }
    }
    for (unsigned i = 0, e = Users.size(); i != e; ++i) {
      if (!Users[i])
        continue;

      Instruction *I = cast<Instruction>(&*Users[i]);

      if (ICmpInst *C = dyn_cast<ICmpInst>(I)) {
        replaceInstUsesWith(*C,
                            ConstantInt::get(Type::getInt1Ty(C->getContext()),
                                             C->isFalseWhenEqual()));
      } else if (isa<BitCastInst>(I) || isa<GetElementPtrInst>(I) ||
                 isa<AddrSpaceCastInst>(I)) {
        replaceInstUsesWith(*I, UndefValue::get(I->getType()));
      } else if (auto *SI = dyn_cast<StoreInst>(I)) {
        for (auto *DII : DIIs)
          ConvertDebugDeclareToDebugValue(DII, SI, *DIB);
      }
      eraseInstFromFunction(*I);
    }

    if (InvokeInst *II = dyn_cast<InvokeInst>(&MI)) {
      // Replace invoke with a NOP intrinsic to maintain the original CFG
      Module *M = II->getModule();
      Function *F = Intrinsic::getDeclaration(M, Intrinsic::donothing);
      InvokeInst::Create(F, II->getNormalDest(), II->getUnwindDest(),
                         None, "", II->getParent());
    }

    for (auto *DII : DIIs)
      eraseInstFromFunction(*DII);

    return eraseInstFromFunction(MI);
  }
  return nullptr;
}

/// Move the call to free before a NULL test.
///
/// Check if this free is accessed after its argument has been test
/// against NULL (property 0).
/// If yes, it is legal to move this call in its predecessor block.
///
/// The move is performed only if the block containing the call to free
/// will be removed, i.e.:
/// 1. it has only one predecessor P, and P has two successors
/// 2. it contains the call, noops, and an unconditional branch
/// 3. its successor is the same as its predecessor's successor
///
/// The profitability is out-of concern here and this function should
/// be called only if the caller knows this transformation would be
/// profitable (e.g., for code size).
static Instruction *tryToMoveFreeBeforeNullTest(CallInst &FI,
                                                const DataLayout &DL) {
  Value *Op = FI.getArgOperand(0);
  BasicBlock *FreeInstrBB = FI.getParent();
  BasicBlock *PredBB = FreeInstrBB->getSinglePredecessor();

  // Validate part of constraint #1: Only one predecessor
  // FIXME: We can extend the number of predecessor, but in that case, we
  //        would duplicate the call to free in each predecessor and it may
  //        not be profitable even for code size.
  if (!PredBB)
    return nullptr;

  // Validate constraint #2: Does this block contains only the call to
  //                         free, noops, and an unconditional branch?
  BasicBlock *SuccBB;
  Instruction *FreeInstrBBTerminator = FreeInstrBB->getTerminator();
  if (!match(FreeInstrBBTerminator, m_UnconditionalBr(SuccBB)))
    return nullptr;

  // If there are only 2 instructions in the block, at this point,
  // this is the call to free and unconditional.
  // If there are more than 2 instructions, check that they are noops
  // i.e., they won't hurt the performance of the generated code.
  if (FreeInstrBB->size() != 2) {
    for (const Instruction &Inst : *FreeInstrBB) {
      if (&Inst == &FI || &Inst == FreeInstrBBTerminator)
        continue;
      auto *Cast = dyn_cast<CastInst>(&Inst);
      if (!Cast || !Cast->isNoopCast(DL))
        return nullptr;
    }
  }
  // Validate the rest of constraint #1 by matching on the pred branch.
  Instruction *TI = PredBB->getTerminator();
  BasicBlock *TrueBB, *FalseBB;
  ICmpInst::Predicate Pred;
  if (!match(TI, m_Br(m_ICmp(Pred,
                             m_CombineOr(m_Specific(Op),
                                         m_Specific(Op->stripPointerCasts())),
                             m_Zero()),
                      TrueBB, FalseBB)))
    return nullptr;
  if (Pred != ICmpInst::ICMP_EQ && Pred != ICmpInst::ICMP_NE)
    return nullptr;

  // Validate constraint #3: Ensure the null case just falls through.
  if (SuccBB != (Pred == ICmpInst::ICMP_EQ ? TrueBB : FalseBB))
    return nullptr;
  assert(FreeInstrBB == (Pred == ICmpInst::ICMP_EQ ? FalseBB : TrueBB) &&
         "Broken CFG: missing edge from predecessor to successor");

  // At this point, we know that everything in FreeInstrBB can be moved
  // before TI.
  for (BasicBlock::iterator It = FreeInstrBB->begin(), End = FreeInstrBB->end();
       It != End;) {
    Instruction &Instr = *It++;
    if (&Instr == FreeInstrBBTerminator)
      break;
    Instr.moveBefore(TI);
  }
  assert(FreeInstrBB->size() == 1 &&
         "Only the branch instruction should remain");
  return &FI;
}

Instruction *InstCombiner::visitFree(CallInst &FI) {
  Value *Op = FI.getArgOperand(0);

  // free undef -> unreachable.
  if (isa<UndefValue>(Op)) {
    // Insert a new store to null because we cannot modify the CFG here.
    Builder.CreateStore(ConstantInt::getTrue(FI.getContext()),
                        UndefValue::get(Type::getInt1PtrTy(FI.getContext())));
    return eraseInstFromFunction(FI);
  }

  // If we have 'free null' delete the instruction.  This can happen in stl code
  // when lots of inlining happens.
  if (isa<ConstantPointerNull>(Op))
    return eraseInstFromFunction(FI);

  // If we optimize for code size, try to move the call to free before the null
  // test so that simplify cfg can remove the empty block and dead code
  // elimination the branch. I.e., helps to turn something like:
  // if (foo) free(foo);
  // into
  // free(foo);
  if (MinimizeSize)
    if (Instruction *I = tryToMoveFreeBeforeNullTest(FI, DL))
      return I;

  return nullptr;
}

Instruction *InstCombiner::visitReturnInst(ReturnInst &RI) {
  if (RI.getNumOperands() == 0) // ret void
    return nullptr;

  Value *ResultOp = RI.getOperand(0);
  Type *VTy = ResultOp->getType();
  if (!VTy->isIntegerTy())
    return nullptr;

  // There might be assume intrinsics dominating this return that completely
  // determine the value. If so, constant fold it.
  KnownBits Known = computeKnownBits(ResultOp, 0, &RI);
  if (Known.isConstant())
    RI.setOperand(0, Constant::getIntegerValue(VTy, Known.getConstant()));

  return nullptr;
}

Instruction *InstCombiner::visitBranchInst(BranchInst &BI) {
  // Change br (not X), label True, label False to: br X, label False, True
  Value *X = nullptr;
  BasicBlock *TrueDest;
  BasicBlock *FalseDest;
  if (match(&BI, m_Br(m_Not(m_Value(X)), TrueDest, FalseDest)) &&
      !isa<Constant>(X)) {
    // Swap Destinations and condition...
    BI.setCondition(X);
    BI.swapSuccessors();
    return &BI;
  }

  // If the condition is irrelevant, remove the use so that other
  // transforms on the condition become more effective.
  if (BI.isConditional() && !isa<ConstantInt>(BI.getCondition()) &&
      BI.getSuccessor(0) == BI.getSuccessor(1)) {
    BI.setCondition(ConstantInt::getFalse(BI.getCondition()->getType()));
    return &BI;
  }

  // Canonicalize, for example, icmp_ne -> icmp_eq or fcmp_one -> fcmp_oeq.
  CmpInst::Predicate Pred;
  if (match(&BI, m_Br(m_OneUse(m_Cmp(Pred, m_Value(), m_Value())), TrueDest,
                      FalseDest)) &&
      !isCanonicalPredicate(Pred)) {
    // Swap destinations and condition.
    CmpInst *Cond = cast<CmpInst>(BI.getCondition());
    Cond->setPredicate(CmpInst::getInversePredicate(Pred));
    BI.swapSuccessors();
    Worklist.Add(Cond);
    return &BI;
  }

  return nullptr;
}

Instruction *InstCombiner::visitSwitchInst(SwitchInst &SI) {
  Value *Cond = SI.getCondition();
  Value *Op0;
  ConstantInt *AddRHS;
  if (match(Cond, m_Add(m_Value(Op0), m_ConstantInt(AddRHS)))) {
    // Change 'switch (X+4) case 1:' into 'switch (X) case -3'.
    for (auto Case : SI.cases()) {
      Constant *NewCase = ConstantExpr::getSub(Case.getCaseValue(), AddRHS);
      assert(isa<ConstantInt>(NewCase) &&
             "Result of expression should be constant");
      Case.setValue(cast<ConstantInt>(NewCase));
    }
    SI.setCondition(Op0);
    return &SI;
  }

  KnownBits Known = computeKnownBits(Cond, 0, &SI);
  unsigned LeadingKnownZeros = Known.countMinLeadingZeros();
  unsigned LeadingKnownOnes = Known.countMinLeadingOnes();

  // Compute the number of leading bits we can ignore.
  // TODO: A better way to determine this would use ComputeNumSignBits().
  for (auto &C : SI.cases()) {
    LeadingKnownZeros = std::min(
        LeadingKnownZeros, C.getCaseValue()->getValue().countLeadingZeros());
    LeadingKnownOnes = std::min(
        LeadingKnownOnes, C.getCaseValue()->getValue().countLeadingOnes());
  }

  unsigned NewWidth = Known.getBitWidth() - std::max(LeadingKnownZeros, LeadingKnownOnes);

  // Shrink the condition operand if the new type is smaller than the old type.
  // But do not shrink to a non-standard type, because backend can't generate 
  // good code for that yet.
  // TODO: We can make it aggressive again after fixing PR39569.
  if (NewWidth > 0 && NewWidth < Known.getBitWidth() &&
      shouldChangeType(Known.getBitWidth(), NewWidth)) {
    IntegerType *Ty = IntegerType::get(SI.getContext(), NewWidth);
    Builder.SetInsertPoint(&SI);
    Value *NewCond = Builder.CreateTrunc(Cond, Ty, "trunc");
    SI.setCondition(NewCond);

    for (auto Case : SI.cases()) {
      APInt TruncatedCase = Case.getCaseValue()->getValue().trunc(NewWidth);
      Case.setValue(ConstantInt::get(SI.getContext(), TruncatedCase));
    }
    return &SI;
  }

  return nullptr;
}

Instruction *InstCombiner::visitExtractValueInst(ExtractValueInst &EV) {
  Value *Agg = EV.getAggregateOperand();

  if (!EV.hasIndices())
    return replaceInstUsesWith(EV, Agg);

  if (Value *V = SimplifyExtractValueInst(Agg, EV.getIndices(),
                                          SQ.getWithInstruction(&EV)))
    return replaceInstUsesWith(EV, V);

  if (InsertValueInst *IV = dyn_cast<InsertValueInst>(Agg)) {
    // We're extracting from an insertvalue instruction, compare the indices
    const unsigned *exti, *exte, *insi, *inse;
    for (exti = EV.idx_begin(), insi = IV->idx_begin(),
         exte = EV.idx_end(), inse = IV->idx_end();
         exti != exte && insi != inse;
         ++exti, ++insi) {
      if (*insi != *exti)
        // The insert and extract both reference distinctly different elements.
        // This means the extract is not influenced by the insert, and we can
        // replace the aggregate operand of the extract with the aggregate
        // operand of the insert. i.e., replace
        // %I = insertvalue { i32, { i32 } } %A, { i32 } { i32 42 }, 1
        // %E = extractvalue { i32, { i32 } } %I, 0
        // with
        // %E = extractvalue { i32, { i32 } } %A, 0
        return ExtractValueInst::Create(IV->getAggregateOperand(),
                                        EV.getIndices());
    }
    if (exti == exte && insi == inse)
      // Both iterators are at the end: Index lists are identical. Replace
      // %B = insertvalue { i32, { i32 } } %A, i32 42, 1, 0
      // %C = extractvalue { i32, { i32 } } %B, 1, 0
      // with "i32 42"
      return replaceInstUsesWith(EV, IV->getInsertedValueOperand());
    if (exti == exte) {
      // The extract list is a prefix of the insert list. i.e. replace
      // %I = insertvalue { i32, { i32 } } %A, i32 42, 1, 0
      // %E = extractvalue { i32, { i32 } } %I, 1
      // with
      // %X = extractvalue { i32, { i32 } } %A, 1
      // %E = insertvalue { i32 } %X, i32 42, 0
      // by switching the order of the insert and extract (though the
      // insertvalue should be left in, since it may have other uses).
      Value *NewEV = Builder.CreateExtractValue(IV->getAggregateOperand(),
                                                EV.getIndices());
      return InsertValueInst::Create(NewEV, IV->getInsertedValueOperand(),
                                     makeArrayRef(insi, inse));
    }
    if (insi == inse)
      // The insert list is a prefix of the extract list
      // We can simply remove the common indices from the extract and make it
      // operate on the inserted value instead of the insertvalue result.
      // i.e., replace
      // %I = insertvalue { i32, { i32 } } %A, { i32 } { i32 42 }, 1
      // %E = extractvalue { i32, { i32 } } %I, 1, 0
      // with
      // %E extractvalue { i32 } { i32 42 }, 0
      return ExtractValueInst::Create(IV->getInsertedValueOperand(),
                                      makeArrayRef(exti, exte));
  }
  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(Agg)) {
    // We're extracting from an intrinsic, see if we're the only user, which
    // allows us to simplify multiple result intrinsics to simpler things that
    // just get one value.
    if (II->hasOneUse()) {
      // Check if we're grabbing the overflow bit or the result of a 'with
      // overflow' intrinsic.  If it's the latter we can remove the intrinsic
      // and replace it with a traditional binary instruction.
      switch (II->getIntrinsicID()) {
      case Intrinsic::uadd_with_overflow:
      case Intrinsic::sadd_with_overflow:
        if (*EV.idx_begin() == 0) {  // Normal result.
          Value *LHS = II->getArgOperand(0), *RHS = II->getArgOperand(1);
          replaceInstUsesWith(*II, UndefValue::get(II->getType()));
          eraseInstFromFunction(*II);
          return BinaryOperator::CreateAdd(LHS, RHS);
        }

        // If the normal result of the add is dead, and the RHS is a constant,
        // we can transform this into a range comparison.
        // overflow = uadd a, -4  -->  overflow = icmp ugt a, 3
        if (II->getIntrinsicID() == Intrinsic::uadd_with_overflow)
          if (ConstantInt *CI = dyn_cast<ConstantInt>(II->getArgOperand(1)))
            return new ICmpInst(ICmpInst::ICMP_UGT, II->getArgOperand(0),
                                ConstantExpr::getNot(CI));
        break;
      case Intrinsic::usub_with_overflow:
      case Intrinsic::ssub_with_overflow:
        if (*EV.idx_begin() == 0) {  // Normal result.
          Value *LHS = II->getArgOperand(0), *RHS = II->getArgOperand(1);
          replaceInstUsesWith(*II, UndefValue::get(II->getType()));
          eraseInstFromFunction(*II);
          return BinaryOperator::CreateSub(LHS, RHS);
        }
        break;
      case Intrinsic::umul_with_overflow:
      case Intrinsic::smul_with_overflow:
        if (*EV.idx_begin() == 0) {  // Normal result.
          Value *LHS = II->getArgOperand(0), *RHS = II->getArgOperand(1);
          replaceInstUsesWith(*II, UndefValue::get(II->getType()));
          eraseInstFromFunction(*II);
          return BinaryOperator::CreateMul(LHS, RHS);
        }
        break;
      default:
        break;
      }
    }
  }
  if (LoadInst *L = dyn_cast<LoadInst>(Agg))
    // If the (non-volatile) load only has one use, we can rewrite this to a
    // load from a GEP. This reduces the size of the load. If a load is used
    // only by extractvalue instructions then this either must have been
    // optimized before, or it is a struct with padding, in which case we
    // don't want to do the transformation as it loses padding knowledge.
    if (L->isSimple() && L->hasOneUse()) {
      // extractvalue has integer indices, getelementptr has Value*s. Convert.
      SmallVector<Value*, 4> Indices;
      // Prefix an i32 0 since we need the first element.
      Indices.push_back(Builder.getInt32(0));
      for (ExtractValueInst::idx_iterator I = EV.idx_begin(), E = EV.idx_end();
            I != E; ++I)
        Indices.push_back(Builder.getInt32(*I));

      // We need to insert these at the location of the old load, not at that of
      // the extractvalue.
      Builder.SetInsertPoint(L);
      Value *GEP = Builder.CreateInBoundsGEP(L->getType(),
                                             L->getPointerOperand(), Indices);
      Instruction *NL = Builder.CreateLoad(GEP);
      // Whatever aliasing information we had for the orignal load must also
      // hold for the smaller load, so propagate the annotations.
      AAMDNodes Nodes;
      L->getAAMetadata(Nodes);
      NL->setAAMetadata(Nodes);
      // Returning the load directly will cause the main loop to insert it in
      // the wrong spot, so use replaceInstUsesWith().
      return replaceInstUsesWith(EV, NL);
    }
  // We could simplify extracts from other values. Note that nested extracts may
  // already be simplified implicitly by the above: extract (extract (insert) )
  // will be translated into extract ( insert ( extract ) ) first and then just
  // the value inserted, if appropriate. Similarly for extracts from single-use
  // loads: extract (extract (load)) will be translated to extract (load (gep))
  // and if again single-use then via load (gep (gep)) to load (gep).
  // However, double extracts from e.g. function arguments or return values
  // aren't handled yet.
  return nullptr;
}

/// Return 'true' if the given typeinfo will match anything.
static bool isCatchAll(EHPersonality Personality, Constant *TypeInfo) {
  switch (Personality) {
  case EHPersonality::GNU_C:
  case EHPersonality::GNU_C_SjLj:
  case EHPersonality::Rust:
    // The GCC C EH and Rust personality only exists to support cleanups, so
    // it's not clear what the semantics of catch clauses are.
    return false;
  case EHPersonality::Unknown:
    return false;
  case EHPersonality::GNU_Ada:
    // While __gnat_all_others_value will match any Ada exception, it doesn't
    // match foreign exceptions (or didn't, before gcc-4.7).
    return false;
  case EHPersonality::GNU_CXX:
  case EHPersonality::GNU_CXX_SjLj:
  case EHPersonality::GNU_ObjC:
  case EHPersonality::MSVC_X86SEH:
  case EHPersonality::MSVC_Win64SEH:
  case EHPersonality::MSVC_CXX:
  case EHPersonality::CoreCLR:
  case EHPersonality::Wasm_CXX:
    return TypeInfo->isNullValue();
  }
  llvm_unreachable("invalid enum");
}

static bool shorter_filter(const Value *LHS, const Value *RHS) {
  return
    cast<ArrayType>(LHS->getType())->getNumElements()
  <
    cast<ArrayType>(RHS->getType())->getNumElements();
}

Instruction *InstCombiner::visitLandingPadInst(LandingPadInst &LI) {
  // The logic here should be correct for any real-world personality function.
  // However if that turns out not to be true, the offending logic can always
  // be conditioned on the personality function, like the catch-all logic is.
  EHPersonality Personality =
      classifyEHPersonality(LI.getParent()->getParent()->getPersonalityFn());

  // Simplify the list of clauses, eg by removing repeated catch clauses
  // (these are often created by inlining).
  bool MakeNewInstruction = false; // If true, recreate using the following:
  SmallVector<Constant *, 16> NewClauses; // - Clauses for the new instruction;
  bool CleanupFlag = LI.isCleanup();   // - The new instruction is a cleanup.

  SmallPtrSet<Value *, 16> AlreadyCaught; // Typeinfos known caught already.
  for (unsigned i = 0, e = LI.getNumClauses(); i != e; ++i) {
    bool isLastClause = i + 1 == e;
    if (LI.isCatch(i)) {
      // A catch clause.
      Constant *CatchClause = LI.getClause(i);
      Constant *TypeInfo = CatchClause->stripPointerCasts();

      // If we already saw this clause, there is no point in having a second
      // copy of it.
      if (AlreadyCaught.insert(TypeInfo).second) {
        // This catch clause was not already seen.
        NewClauses.push_back(CatchClause);
      } else {
        // Repeated catch clause - drop the redundant copy.
        MakeNewInstruction = true;
      }

      // If this is a catch-all then there is no point in keeping any following
      // clauses or marking the landingpad as having a cleanup.
      if (isCatchAll(Personality, TypeInfo)) {
        if (!isLastClause)
          MakeNewInstruction = true;
        CleanupFlag = false;
        break;
      }
    } else {
      // A filter clause.  If any of the filter elements were already caught
      // then they can be dropped from the filter.  It is tempting to try to
      // exploit the filter further by saying that any typeinfo that does not
      // occur in the filter can't be caught later (and thus can be dropped).
      // However this would be wrong, since typeinfos can match without being
      // equal (for example if one represents a C++ class, and the other some
      // class derived from it).
      assert(LI.isFilter(i) && "Unsupported landingpad clause!");
      Constant *FilterClause = LI.getClause(i);
      ArrayType *FilterType = cast<ArrayType>(FilterClause->getType());
      unsigned NumTypeInfos = FilterType->getNumElements();

      // An empty filter catches everything, so there is no point in keeping any
      // following clauses or marking the landingpad as having a cleanup.  By
      // dealing with this case here the following code is made a bit simpler.
      if (!NumTypeInfos) {
        NewClauses.push_back(FilterClause);
        if (!isLastClause)
          MakeNewInstruction = true;
        CleanupFlag = false;
        break;
      }

      bool MakeNewFilter = false; // If true, make a new filter.
      SmallVector<Constant *, 16> NewFilterElts; // New elements.
      if (isa<ConstantAggregateZero>(FilterClause)) {
        // Not an empty filter - it contains at least one null typeinfo.
        assert(NumTypeInfos > 0 && "Should have handled empty filter already!");
        Constant *TypeInfo =
          Constant::getNullValue(FilterType->getElementType());
        // If this typeinfo is a catch-all then the filter can never match.
        if (isCatchAll(Personality, TypeInfo)) {
          // Throw the filter away.
          MakeNewInstruction = true;
          continue;
        }

        // There is no point in having multiple copies of this typeinfo, so
        // discard all but the first copy if there is more than one.
        NewFilterElts.push_back(TypeInfo);
        if (NumTypeInfos > 1)
          MakeNewFilter = true;
      } else {
        ConstantArray *Filter = cast<ConstantArray>(FilterClause);
        SmallPtrSet<Value *, 16> SeenInFilter; // For uniquing the elements.
        NewFilterElts.reserve(NumTypeInfos);

        // Remove any filter elements that were already caught or that already
        // occurred in the filter.  While there, see if any of the elements are
        // catch-alls.  If so, the filter can be discarded.
        bool SawCatchAll = false;
        for (unsigned j = 0; j != NumTypeInfos; ++j) {
          Constant *Elt = Filter->getOperand(j);
          Constant *TypeInfo = Elt->stripPointerCasts();
          if (isCatchAll(Personality, TypeInfo)) {
            // This element is a catch-all.  Bail out, noting this fact.
            SawCatchAll = true;
            break;
          }

          // Even if we've seen a type in a catch clause, we don't want to
          // remove it from the filter.  An unexpected type handler may be
          // set up for a call site which throws an exception of the same
          // type caught.  In order for the exception thrown by the unexpected
          // handler to propagate correctly, the filter must be correctly
          // described for the call site.
          //
          // Example:
          //
          // void unexpected() { throw 1;}
          // void foo() throw (int) {
          //   std::set_unexpected(unexpected);
          //   try {
          //     throw 2.0;
          //   } catch (int i) {}
          // }

          // There is no point in having multiple copies of the same typeinfo in
          // a filter, so only add it if we didn't already.
          if (SeenInFilter.insert(TypeInfo).second)
            NewFilterElts.push_back(cast<Constant>(Elt));
        }
        // A filter containing a catch-all cannot match anything by definition.
        if (SawCatchAll) {
          // Throw the filter away.
          MakeNewInstruction = true;
          continue;
        }

        // If we dropped something from the filter, make a new one.
        if (NewFilterElts.size() < NumTypeInfos)
          MakeNewFilter = true;
      }
      if (MakeNewFilter) {
        FilterType = ArrayType::get(FilterType->getElementType(),
                                    NewFilterElts.size());
        FilterClause = ConstantArray::get(FilterType, NewFilterElts);
        MakeNewInstruction = true;
      }

      NewClauses.push_back(FilterClause);

      // If the new filter is empty then it will catch everything so there is
      // no point in keeping any following clauses or marking the landingpad
      // as having a cleanup.  The case of the original filter being empty was
      // already handled above.
      if (MakeNewFilter && !NewFilterElts.size()) {
        assert(MakeNewInstruction && "New filter but not a new instruction!");
        CleanupFlag = false;
        break;
      }
    }
  }

  // If several filters occur in a row then reorder them so that the shortest
  // filters come first (those with the smallest number of elements).  This is
  // advantageous because shorter filters are more likely to match, speeding up
  // unwinding, but mostly because it increases the effectiveness of the other
  // filter optimizations below.
  for (unsigned i = 0, e = NewClauses.size(); i + 1 < e; ) {
    unsigned j;
    // Find the maximal 'j' s.t. the range [i, j) consists entirely of filters.
    for (j = i; j != e; ++j)
      if (!isa<ArrayType>(NewClauses[j]->getType()))
        break;

    // Check whether the filters are already sorted by length.  We need to know
    // if sorting them is actually going to do anything so that we only make a
    // new landingpad instruction if it does.
    for (unsigned k = i; k + 1 < j; ++k)
      if (shorter_filter(NewClauses[k+1], NewClauses[k])) {
        // Not sorted, so sort the filters now.  Doing an unstable sort would be
        // correct too but reordering filters pointlessly might confuse users.
        std::stable_sort(NewClauses.begin() + i, NewClauses.begin() + j,
                         shorter_filter);
        MakeNewInstruction = true;
        break;
      }

    // Look for the next batch of filters.
    i = j + 1;
  }

  // If typeinfos matched if and only if equal, then the elements of a filter L
  // that occurs later than a filter F could be replaced by the intersection of
  // the elements of F and L.  In reality two typeinfos can match without being
  // equal (for example if one represents a C++ class, and the other some class
  // derived from it) so it would be wrong to perform this transform in general.
  // However the transform is correct and useful if F is a subset of L.  In that
  // case L can be replaced by F, and thus removed altogether since repeating a
  // filter is pointless.  So here we look at all pairs of filters F and L where
  // L follows F in the list of clauses, and remove L if every element of F is
  // an element of L.  This can occur when inlining C++ functions with exception
  // specifications.
  for (unsigned i = 0; i + 1 < NewClauses.size(); ++i) {
    // Examine each filter in turn.
    Value *Filter = NewClauses[i];
    ArrayType *FTy = dyn_cast<ArrayType>(Filter->getType());
    if (!FTy)
      // Not a filter - skip it.
      continue;
    unsigned FElts = FTy->getNumElements();
    // Examine each filter following this one.  Doing this backwards means that
    // we don't have to worry about filters disappearing under us when removed.
    for (unsigned j = NewClauses.size() - 1; j != i; --j) {
      Value *LFilter = NewClauses[j];
      ArrayType *LTy = dyn_cast<ArrayType>(LFilter->getType());
      if (!LTy)
        // Not a filter - skip it.
        continue;
      // If Filter is a subset of LFilter, i.e. every element of Filter is also
      // an element of LFilter, then discard LFilter.
      SmallVectorImpl<Constant *>::iterator J = NewClauses.begin() + j;
      // If Filter is empty then it is a subset of LFilter.
      if (!FElts) {
        // Discard LFilter.
        NewClauses.erase(J);
        MakeNewInstruction = true;
        // Move on to the next filter.
        continue;
      }
      unsigned LElts = LTy->getNumElements();
      // If Filter is longer than LFilter then it cannot be a subset of it.
      if (FElts > LElts)
        // Move on to the next filter.
        continue;
      // At this point we know that LFilter has at least one element.
      if (isa<ConstantAggregateZero>(LFilter)) { // LFilter only contains zeros.
        // Filter is a subset of LFilter iff Filter contains only zeros (as we
        // already know that Filter is not longer than LFilter).
        if (isa<ConstantAggregateZero>(Filter)) {
          assert(FElts <= LElts && "Should have handled this case earlier!");
          // Discard LFilter.
          NewClauses.erase(J);
          MakeNewInstruction = true;
        }
        // Move on to the next filter.
        continue;
      }
      ConstantArray *LArray = cast<ConstantArray>(LFilter);
      if (isa<ConstantAggregateZero>(Filter)) { // Filter only contains zeros.
        // Since Filter is non-empty and contains only zeros, it is a subset of
        // LFilter iff LFilter contains a zero.
        assert(FElts > 0 && "Should have eliminated the empty filter earlier!");
        for (unsigned l = 0; l != LElts; ++l)
          if (LArray->getOperand(l)->isNullValue()) {
            // LFilter contains a zero - discard it.
            NewClauses.erase(J);
            MakeNewInstruction = true;
            break;
          }
        // Move on to the next filter.
        continue;
      }
      // At this point we know that both filters are ConstantArrays.  Loop over
      // operands to see whether every element of Filter is also an element of
      // LFilter.  Since filters tend to be short this is probably faster than
      // using a method that scales nicely.
      ConstantArray *FArray = cast<ConstantArray>(Filter);
      bool AllFound = true;
      for (unsigned f = 0; f != FElts; ++f) {
        Value *FTypeInfo = FArray->getOperand(f)->stripPointerCasts();
        AllFound = false;
        for (unsigned l = 0; l != LElts; ++l) {
          Value *LTypeInfo = LArray->getOperand(l)->stripPointerCasts();
          if (LTypeInfo == FTypeInfo) {
            AllFound = true;
            break;
          }
        }
        if (!AllFound)
          break;
      }
      if (AllFound) {
        // Discard LFilter.
        NewClauses.erase(J);
        MakeNewInstruction = true;
      }
      // Move on to the next filter.
    }
  }

  // If we changed any of the clauses, replace the old landingpad instruction
  // with a new one.
  if (MakeNewInstruction) {
    LandingPadInst *NLI = LandingPadInst::Create(LI.getType(),
                                                 NewClauses.size());
    for (unsigned i = 0, e = NewClauses.size(); i != e; ++i)
      NLI->addClause(NewClauses[i]);
    // A landing pad with no clauses must have the cleanup flag set.  It is
    // theoretically possible, though highly unlikely, that we eliminated all
    // clauses.  If so, force the cleanup flag to true.
    if (NewClauses.empty())
      CleanupFlag = true;
    NLI->setCleanup(CleanupFlag);
    return NLI;
  }

  // Even if none of the clauses changed, we may nonetheless have understood
  // that the cleanup flag is pointless.  Clear it if so.
  if (LI.isCleanup() != CleanupFlag) {
    assert(!CleanupFlag && "Adding a cleanup, not removing one?!");
    LI.setCleanup(CleanupFlag);
    return &LI;
  }

  return nullptr;
}

/// Try to move the specified instruction from its current block into the
/// beginning of DestBlock, which can only happen if it's safe to move the
/// instruction past all of the instructions between it and the end of its
/// block.
static bool TryToSinkInstruction(Instruction *I, BasicBlock *DestBlock) {
  assert(I->hasOneUse() && "Invariants didn't hold!");
  BasicBlock *SrcBlock = I->getParent();

  // Cannot move control-flow-involving, volatile loads, vaarg, etc.
  if (isa<PHINode>(I) || I->isEHPad() || I->mayHaveSideEffects() ||
      I->isTerminator())
    return false;

  // Do not sink static or dynamic alloca instructions. Static allocas must
  // remain in the entry block, and dynamic allocas must not be sunk in between
  // a stacksave / stackrestore pair, which would incorrectly shorten its
  // lifetime.
  if (isa<AllocaInst>(I))
    return false;

  // Do not sink into catchswitch blocks.
  if (isa<CatchSwitchInst>(DestBlock->getTerminator()))
    return false;

  // Do not sink convergent call instructions.
  if (auto *CI = dyn_cast<CallInst>(I)) {
    if (CI->isConvergent())
      return false;
  }
  // We can only sink load instructions if there is nothing between the load and
  // the end of block that could change the value.
  if (I->mayReadFromMemory()) {
    for (BasicBlock::iterator Scan = I->getIterator(),
                              E = I->getParent()->end();
         Scan != E; ++Scan)
      if (Scan->mayWriteToMemory())
        return false;
  }
  BasicBlock::iterator InsertPos = DestBlock->getFirstInsertionPt();
  I->moveBefore(&*InsertPos);
  ++NumSunkInst;

  // Also sink all related debug uses from the source basic block. Otherwise we
  // get debug use before the def.
  SmallVector<DbgVariableIntrinsic *, 1> DbgUsers;
  findDbgUsers(DbgUsers, I);
  for (auto *DII : DbgUsers) {
    if (DII->getParent() == SrcBlock) {
      DII->moveBefore(&*InsertPos);
      LLVM_DEBUG(dbgs() << "SINK: " << *DII << '\n');
    }
  }
  return true;
}

bool InstCombiner::run() {
  while (!Worklist.isEmpty()) {
    Instruction *I = Worklist.RemoveOne();
    if (I == nullptr) continue;  // skip null values.

    // Check to see if we can DCE the instruction.
    if (isInstructionTriviallyDead(I, &TLI)) {
      LLVM_DEBUG(dbgs() << "IC: DCE: " << *I << '\n');
      eraseInstFromFunction(*I);
      ++NumDeadInst;
      MadeIRChange = true;
      continue;
    }

    if (!DebugCounter::shouldExecute(VisitCounter))
      continue;

    // Instruction isn't dead, see if we can constant propagate it.
    if (!I->use_empty() &&
        (I->getNumOperands() == 0 || isa<Constant>(I->getOperand(0)))) {
      if (Constant *C = ConstantFoldInstruction(I, DL, &TLI)) {
        LLVM_DEBUG(dbgs() << "IC: ConstFold to: " << *C << " from: " << *I
                          << '\n');

        // Add operands to the worklist.
        replaceInstUsesWith(*I, C);
        ++NumConstProp;
        if (isInstructionTriviallyDead(I, &TLI))
          eraseInstFromFunction(*I);
        MadeIRChange = true;
        continue;
      }
    }

    // In general, it is possible for computeKnownBits to determine all bits in
    // a value even when the operands are not all constants.
    Type *Ty = I->getType();
    if (ExpensiveCombines && !I->use_empty() && Ty->isIntOrIntVectorTy()) {
      KnownBits Known = computeKnownBits(I, /*Depth*/0, I);
      if (Known.isConstant()) {
        Constant *C = ConstantInt::get(Ty, Known.getConstant());
        LLVM_DEBUG(dbgs() << "IC: ConstFold (all bits known) to: " << *C
                          << " from: " << *I << '\n');

        // Add operands to the worklist.
        replaceInstUsesWith(*I, C);
        ++NumConstProp;
        if (isInstructionTriviallyDead(I, &TLI))
          eraseInstFromFunction(*I);
        MadeIRChange = true;
        continue;
      }
    }

    // See if we can trivially sink this instruction to a successor basic block.
    if (EnableCodeSinking && I->hasOneUse()) {
      BasicBlock *BB = I->getParent();
      Instruction *UserInst = cast<Instruction>(*I->user_begin());
      BasicBlock *UserParent;

      // Get the block the use occurs in.
      if (PHINode *PN = dyn_cast<PHINode>(UserInst))
        UserParent = PN->getIncomingBlock(*I->use_begin());
      else
        UserParent = UserInst->getParent();

      if (UserParent != BB) {
        bool UserIsSuccessor = false;
        // See if the user is one of our successors.
        for (succ_iterator SI = succ_begin(BB), E = succ_end(BB); SI != E; ++SI)
          if (*SI == UserParent) {
            UserIsSuccessor = true;
            break;
          }

        // If the user is one of our immediate successors, and if that successor
        // only has us as a predecessors (we'd have to split the critical edge
        // otherwise), we can keep going.
        if (UserIsSuccessor && UserParent->getUniquePredecessor()) {
          // Okay, the CFG is simple enough, try to sink this instruction.
          if (TryToSinkInstruction(I, UserParent)) {
            LLVM_DEBUG(dbgs() << "IC: Sink: " << *I << '\n');
            MadeIRChange = true;
            // We'll add uses of the sunk instruction below, but since sinking
            // can expose opportunities for it's *operands* add them to the
            // worklist
            for (Use &U : I->operands())
              if (Instruction *OpI = dyn_cast<Instruction>(U.get()))
                Worklist.Add(OpI);
          }
        }
      }
    }

    // Now that we have an instruction, try combining it to simplify it.
    Builder.SetInsertPoint(I);
    Builder.SetCurrentDebugLocation(I->getDebugLoc());

#ifndef NDEBUG
    std::string OrigI;
#endif
    LLVM_DEBUG(raw_string_ostream SS(OrigI); I->print(SS); OrigI = SS.str(););
    LLVM_DEBUG(dbgs() << "IC: Visiting: " << OrigI << '\n');

    if (Instruction *Result = visit(*I)) {
      ++NumCombined;
      // Should we replace the old instruction with a new one?
      if (Result != I) {
        LLVM_DEBUG(dbgs() << "IC: Old = " << *I << '\n'
                          << "    New = " << *Result << '\n');

        if (I->getDebugLoc())
          Result->setDebugLoc(I->getDebugLoc());
        // Everything uses the new instruction now.
        I->replaceAllUsesWith(Result);

        // Move the name to the new instruction first.
        Result->takeName(I);

        // Push the new instruction and any users onto the worklist.
        Worklist.AddUsersToWorkList(*Result);
        Worklist.Add(Result);

        // Insert the new instruction into the basic block...
        BasicBlock *InstParent = I->getParent();
        BasicBlock::iterator InsertPos = I->getIterator();

        // If we replace a PHI with something that isn't a PHI, fix up the
        // insertion point.
        if (!isa<PHINode>(Result) && isa<PHINode>(InsertPos))
          InsertPos = InstParent->getFirstInsertionPt();

        InstParent->getInstList().insert(InsertPos, Result);

        eraseInstFromFunction(*I);
      } else {
        LLVM_DEBUG(dbgs() << "IC: Mod = " << OrigI << '\n'
                          << "    New = " << *I << '\n');

        // If the instruction was modified, it's possible that it is now dead.
        // if so, remove it.
        if (isInstructionTriviallyDead(I, &TLI)) {
          eraseInstFromFunction(*I);
        } else {
          Worklist.AddUsersToWorkList(*I);
          Worklist.Add(I);
        }
      }
      MadeIRChange = true;
    }
  }

  Worklist.Zap();
  return MadeIRChange;
}

/// Walk the function in depth-first order, adding all reachable code to the
/// worklist.
///
/// This has a couple of tricks to make the code faster and more powerful.  In
/// particular, we constant fold and DCE instructions as we go, to avoid adding
/// them to the worklist (this significantly speeds up instcombine on code where
/// many instructions are dead or constant).  Additionally, if we find a branch
/// whose condition is a known constant, we only visit the reachable successors.
static bool AddReachableCodeToWorklist(BasicBlock *BB, const DataLayout &DL,
                                       SmallPtrSetImpl<BasicBlock *> &Visited,
                                       InstCombineWorklist &ICWorklist,
                                       const TargetLibraryInfo *TLI) {
  bool MadeIRChange = false;
  SmallVector<BasicBlock*, 256> Worklist;
  Worklist.push_back(BB);

  SmallVector<Instruction*, 128> InstrsForInstCombineWorklist;
  DenseMap<Constant *, Constant *> FoldedConstants;

  do {
    BB = Worklist.pop_back_val();

    // We have now visited this block!  If we've already been here, ignore it.
    if (!Visited.insert(BB).second)
      continue;

    for (BasicBlock::iterator BBI = BB->begin(), E = BB->end(); BBI != E; ) {
      Instruction *Inst = &*BBI++;

      // DCE instruction if trivially dead.
      if (isInstructionTriviallyDead(Inst, TLI)) {
        ++NumDeadInst;
        LLVM_DEBUG(dbgs() << "IC: DCE: " << *Inst << '\n');
        salvageDebugInfo(*Inst);
        Inst->eraseFromParent();
        MadeIRChange = true;
        continue;
      }

      // ConstantProp instruction if trivially constant.
      if (!Inst->use_empty() &&
          (Inst->getNumOperands() == 0 || isa<Constant>(Inst->getOperand(0))))
        if (Constant *C = ConstantFoldInstruction(Inst, DL, TLI)) {
          LLVM_DEBUG(dbgs() << "IC: ConstFold to: " << *C << " from: " << *Inst
                            << '\n');
          Inst->replaceAllUsesWith(C);
          ++NumConstProp;
          if (isInstructionTriviallyDead(Inst, TLI))
            Inst->eraseFromParent();
          MadeIRChange = true;
          continue;
        }

      // See if we can constant fold its operands.
      for (Use &U : Inst->operands()) {
        if (!isa<ConstantVector>(U) && !isa<ConstantExpr>(U))
          continue;

        auto *C = cast<Constant>(U);
        Constant *&FoldRes = FoldedConstants[C];
        if (!FoldRes)
          FoldRes = ConstantFoldConstant(C, DL, TLI);
        if (!FoldRes)
          FoldRes = C;

        if (FoldRes != C) {
          LLVM_DEBUG(dbgs() << "IC: ConstFold operand of: " << *Inst
                            << "\n    Old = " << *C
                            << "\n    New = " << *FoldRes << '\n');
          U = FoldRes;
          MadeIRChange = true;
        }
      }

      // Skip processing debug intrinsics in InstCombine. Processing these call instructions
      // consumes non-trivial amount of time and provides no value for the optimization.
      if (!isa<DbgInfoIntrinsic>(Inst))
        InstrsForInstCombineWorklist.push_back(Inst);
    }

    // Recursively visit successors.  If this is a branch or switch on a
    // constant, only visit the reachable successor.
    Instruction *TI = BB->getTerminator();
    if (BranchInst *BI = dyn_cast<BranchInst>(TI)) {
      if (BI->isConditional() && isa<ConstantInt>(BI->getCondition())) {
        bool CondVal = cast<ConstantInt>(BI->getCondition())->getZExtValue();
        BasicBlock *ReachableBB = BI->getSuccessor(!CondVal);
        Worklist.push_back(ReachableBB);
        continue;
      }
    } else if (SwitchInst *SI = dyn_cast<SwitchInst>(TI)) {
      if (ConstantInt *Cond = dyn_cast<ConstantInt>(SI->getCondition())) {
        Worklist.push_back(SI->findCaseValue(Cond)->getCaseSuccessor());
        continue;
      }
    }

    for (BasicBlock *SuccBB : successors(TI))
      Worklist.push_back(SuccBB);
  } while (!Worklist.empty());

  // Once we've found all of the instructions to add to instcombine's worklist,
  // add them in reverse order.  This way instcombine will visit from the top
  // of the function down.  This jives well with the way that it adds all uses
  // of instructions to the worklist after doing a transformation, thus avoiding
  // some N^2 behavior in pathological cases.
  ICWorklist.AddInitialGroup(InstrsForInstCombineWorklist);

  return MadeIRChange;
}

/// Populate the IC worklist from a function, and prune any dead basic
/// blocks discovered in the process.
///
/// This also does basic constant propagation and other forward fixing to make
/// the combiner itself run much faster.
static bool prepareICWorklistFromFunction(Function &F, const DataLayout &DL,
                                          TargetLibraryInfo *TLI,
                                          InstCombineWorklist &ICWorklist) {
  bool MadeIRChange = false;

  // Do a depth-first traversal of the function, populate the worklist with
  // the reachable instructions.  Ignore blocks that are not reachable.  Keep
  // track of which blocks we visit.
  SmallPtrSet<BasicBlock *, 32> Visited;
  MadeIRChange |=
      AddReachableCodeToWorklist(&F.front(), DL, Visited, ICWorklist, TLI);

  // Do a quick scan over the function.  If we find any blocks that are
  // unreachable, remove any instructions inside of them.  This prevents
  // the instcombine code from having to deal with some bad special cases.
  for (BasicBlock &BB : F) {
    if (Visited.count(&BB))
      continue;

    unsigned NumDeadInstInBB = removeAllNonTerminatorAndEHPadInstructions(&BB);
    MadeIRChange |= NumDeadInstInBB > 0;
    NumDeadInst += NumDeadInstInBB;
  }

  return MadeIRChange;
}

static bool combineInstructionsOverFunction(
    Function &F, InstCombineWorklist &Worklist, AliasAnalysis *AA,
    AssumptionCache &AC, TargetLibraryInfo &TLI, DominatorTree &DT,
    OptimizationRemarkEmitter &ORE, bool ExpensiveCombines = true,
    LoopInfo *LI = nullptr) {
  auto &DL = F.getParent()->getDataLayout();
  ExpensiveCombines |= EnableExpensiveCombines;

  /// Builder - This is an IRBuilder that automatically inserts new
  /// instructions into the worklist when they are created.
  IRBuilder<TargetFolder, IRBuilderCallbackInserter> Builder(
      F.getContext(), TargetFolder(DL),
      IRBuilderCallbackInserter([&Worklist, &AC](Instruction *I) {
        Worklist.Add(I);
        if (match(I, m_Intrinsic<Intrinsic::assume>()))
          AC.registerAssumption(cast<CallInst>(I));
      }));

  // Lower dbg.declare intrinsics otherwise their value may be clobbered
  // by instcombiner.
  bool MadeIRChange = false;
  if (ShouldLowerDbgDeclare)
    MadeIRChange = LowerDbgDeclare(F);

  // Iterate while there is work to do.
  int Iteration = 0;
  while (true) {
    ++Iteration;
    LLVM_DEBUG(dbgs() << "\n\nINSTCOMBINE ITERATION #" << Iteration << " on "
                      << F.getName() << "\n");

    MadeIRChange |= prepareICWorklistFromFunction(F, DL, &TLI, Worklist);

    InstCombiner IC(Worklist, Builder, F.optForMinSize(), ExpensiveCombines, AA,
                    AC, TLI, DT, ORE, DL, LI);
    IC.MaxArraySizeForCombine = MaxArraySize;

    if (!IC.run())
      break;
  }

  return MadeIRChange || Iteration > 1;
}

PreservedAnalyses InstCombinePass::run(Function &F,
                                       FunctionAnalysisManager &AM) {
  auto &AC = AM.getResult<AssumptionAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  auto &ORE = AM.getResult<OptimizationRemarkEmitterAnalysis>(F);

  auto *LI = AM.getCachedResult<LoopAnalysis>(F);

  auto *AA = &AM.getResult<AAManager>(F);
  if (!combineInstructionsOverFunction(F, Worklist, AA, AC, TLI, DT, ORE,
                                       ExpensiveCombines, LI))
    // No changes, all analyses are preserved.
    return PreservedAnalyses::all();

  // Mark all the analyses that instcombine updates as preserved.
  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  PA.preserve<AAManager>();
  PA.preserve<BasicAA>();
  PA.preserve<GlobalsAA>();
  return PA;
}

void InstructionCombiningPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addRequired<AAResultsWrapperPass>();
  AU.addRequired<AssumptionCacheTracker>();
  AU.addRequired<TargetLibraryInfoWrapperPass>();
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addRequired<OptimizationRemarkEmitterWrapperPass>();
  AU.addPreserved<DominatorTreeWrapperPass>();
  AU.addPreserved<AAResultsWrapperPass>();
  AU.addPreserved<BasicAAWrapperPass>();
  AU.addPreserved<GlobalsAAWrapperPass>();
}

bool InstructionCombiningPass::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  // Required analyses.
  auto AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
  auto &AC = getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
  auto &TLI = getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
  auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  auto &ORE = getAnalysis<OptimizationRemarkEmitterWrapperPass>().getORE();

  // Optional analyses.
  auto *LIWP = getAnalysisIfAvailable<LoopInfoWrapperPass>();
  auto *LI = LIWP ? &LIWP->getLoopInfo() : nullptr;

  return combineInstructionsOverFunction(F, Worklist, AA, AC, TLI, DT, ORE,
                                         ExpensiveCombines, LI);
}

char InstructionCombiningPass::ID = 0;

INITIALIZE_PASS_BEGIN(InstructionCombiningPass, "instcombine",
                      "Combine redundant instructions", false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(GlobalsAAWrapperPass)
INITIALIZE_PASS_DEPENDENCY(OptimizationRemarkEmitterWrapperPass)
INITIALIZE_PASS_END(InstructionCombiningPass, "instcombine",
                    "Combine redundant instructions", false, false)

// Initialization Routines
void llvm::initializeInstCombine(PassRegistry &Registry) {
  initializeInstructionCombiningPassPass(Registry);
}

void LLVMInitializeInstCombine(LLVMPassRegistryRef R) {
  initializeInstructionCombiningPassPass(*unwrap(R));
}

FunctionPass *llvm::createInstructionCombiningPass(bool ExpensiveCombines) {
  return new InstructionCombiningPass(ExpensiveCombines);
}

void LLVMAddInstructionCombiningPass(LLVMPassManagerRef PM) {
  unwrap(PM)->add(createInstructionCombiningPass());
}
