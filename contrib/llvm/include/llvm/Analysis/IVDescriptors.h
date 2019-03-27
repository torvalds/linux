//===- llvm/Analysis/IVDescriptors.h - IndVar Descriptors -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file "describes" induction and recurrence variables.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_IVDESCRIPTORS_H
#define LLVM_ANALYSIS_IVDESCRIPTORS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/DemandedBits.h"
#include "llvm/Analysis/EHPersonalities.h"
#include "llvm/Analysis/MustExecute.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Casting.h"

namespace llvm {

class AliasSet;
class AliasSetTracker;
class BasicBlock;
class DataLayout;
class Loop;
class LoopInfo;
class OptimizationRemarkEmitter;
class PredicatedScalarEvolution;
class PredIteratorCache;
class ScalarEvolution;
class SCEV;
class TargetLibraryInfo;
class TargetTransformInfo;

/// The RecurrenceDescriptor is used to identify recurrences variables in a
/// loop. Reduction is a special case of recurrence that has uses of the
/// recurrence variable outside the loop. The method isReductionPHI identifies
/// reductions that are basic recurrences.
///
/// Basic recurrences are defined as the summation, product, OR, AND, XOR, min,
/// or max of a set of terms. For example: for(i=0; i<n; i++) { total +=
/// array[i]; } is a summation of array elements. Basic recurrences are a
/// special case of chains of recurrences (CR). See ScalarEvolution for CR
/// references.

/// This struct holds information about recurrence variables.
class RecurrenceDescriptor {
public:
  /// This enum represents the kinds of recurrences that we support.
  enum RecurrenceKind {
    RK_NoRecurrence,  ///< Not a recurrence.
    RK_IntegerAdd,    ///< Sum of integers.
    RK_IntegerMult,   ///< Product of integers.
    RK_IntegerOr,     ///< Bitwise or logical OR of numbers.
    RK_IntegerAnd,    ///< Bitwise or logical AND of numbers.
    RK_IntegerXor,    ///< Bitwise or logical XOR of numbers.
    RK_IntegerMinMax, ///< Min/max implemented in terms of select(cmp()).
    RK_FloatAdd,      ///< Sum of floats.
    RK_FloatMult,     ///< Product of floats.
    RK_FloatMinMax    ///< Min/max implemented in terms of select(cmp()).
  };

  // This enum represents the kind of minmax recurrence.
  enum MinMaxRecurrenceKind {
    MRK_Invalid,
    MRK_UIntMin,
    MRK_UIntMax,
    MRK_SIntMin,
    MRK_SIntMax,
    MRK_FloatMin,
    MRK_FloatMax
  };

  RecurrenceDescriptor() = default;

  RecurrenceDescriptor(Value *Start, Instruction *Exit, RecurrenceKind K,
                       MinMaxRecurrenceKind MK, Instruction *UAI, Type *RT,
                       bool Signed, SmallPtrSetImpl<Instruction *> &CI)
      : StartValue(Start), LoopExitInstr(Exit), Kind(K), MinMaxKind(MK),
        UnsafeAlgebraInst(UAI), RecurrenceType(RT), IsSigned(Signed) {
    CastInsts.insert(CI.begin(), CI.end());
  }

  /// This POD struct holds information about a potential recurrence operation.
  class InstDesc {
  public:
    InstDesc(bool IsRecur, Instruction *I, Instruction *UAI = nullptr)
        : IsRecurrence(IsRecur), PatternLastInst(I), MinMaxKind(MRK_Invalid),
          UnsafeAlgebraInst(UAI) {}

    InstDesc(Instruction *I, MinMaxRecurrenceKind K, Instruction *UAI = nullptr)
        : IsRecurrence(true), PatternLastInst(I), MinMaxKind(K),
          UnsafeAlgebraInst(UAI) {}

    bool isRecurrence() { return IsRecurrence; }

    bool hasUnsafeAlgebra() { return UnsafeAlgebraInst != nullptr; }

    Instruction *getUnsafeAlgebraInst() { return UnsafeAlgebraInst; }

    MinMaxRecurrenceKind getMinMaxKind() { return MinMaxKind; }

    Instruction *getPatternInst() { return PatternLastInst; }

  private:
    // Is this instruction a recurrence candidate.
    bool IsRecurrence;
    // The last instruction in a min/max pattern (select of the select(icmp())
    // pattern), or the current recurrence instruction otherwise.
    Instruction *PatternLastInst;
    // If this is a min/max pattern the comparison predicate.
    MinMaxRecurrenceKind MinMaxKind;
    // Recurrence has unsafe algebra.
    Instruction *UnsafeAlgebraInst;
  };

  /// Returns a struct describing if the instruction 'I' can be a recurrence
  /// variable of type 'Kind'. If the recurrence is a min/max pattern of
  /// select(icmp()) this function advances the instruction pointer 'I' from the
  /// compare instruction to the select instruction and stores this pointer in
  /// 'PatternLastInst' member of the returned struct.
  static InstDesc isRecurrenceInstr(Instruction *I, RecurrenceKind Kind,
                                    InstDesc &Prev, bool HasFunNoNaNAttr);

  /// Returns true if instruction I has multiple uses in Insts
  static bool hasMultipleUsesOf(Instruction *I,
                                SmallPtrSetImpl<Instruction *> &Insts,
                                unsigned MaxNumUses);

  /// Returns true if all uses of the instruction I is within the Set.
  static bool areAllUsesIn(Instruction *I, SmallPtrSetImpl<Instruction *> &Set);

  /// Returns a struct describing if the instruction if the instruction is a
  /// Select(ICmp(X, Y), X, Y) instruction pattern corresponding to a min(X, Y)
  /// or max(X, Y).
  static InstDesc isMinMaxSelectCmpPattern(Instruction *I, InstDesc &Prev);

  /// Returns a struct describing if the instruction is a
  /// Select(FCmp(X, Y), (Z = X op PHINode), PHINode) instruction pattern.
  static InstDesc isConditionalRdxPattern(RecurrenceKind Kind, Instruction *I);

  /// Returns identity corresponding to the RecurrenceKind.
  static Constant *getRecurrenceIdentity(RecurrenceKind K, Type *Tp);

  /// Returns the opcode of binary operation corresponding to the
  /// RecurrenceKind.
  static unsigned getRecurrenceBinOp(RecurrenceKind Kind);

  /// Returns true if Phi is a reduction of type Kind and adds it to the
  /// RecurrenceDescriptor. If either \p DB is non-null or \p AC and \p DT are
  /// non-null, the minimal bit width needed to compute the reduction will be
  /// computed.
  static bool AddReductionVar(PHINode *Phi, RecurrenceKind Kind, Loop *TheLoop,
                              bool HasFunNoNaNAttr,
                              RecurrenceDescriptor &RedDes,
                              DemandedBits *DB = nullptr,
                              AssumptionCache *AC = nullptr,
                              DominatorTree *DT = nullptr);

  /// Returns true if Phi is a reduction in TheLoop. The RecurrenceDescriptor
  /// is returned in RedDes. If either \p DB is non-null or \p AC and \p DT are
  /// non-null, the minimal bit width needed to compute the reduction will be
  /// computed.
  static bool isReductionPHI(PHINode *Phi, Loop *TheLoop,
                             RecurrenceDescriptor &RedDes,
                             DemandedBits *DB = nullptr,
                             AssumptionCache *AC = nullptr,
                             DominatorTree *DT = nullptr);

  /// Returns true if Phi is a first-order recurrence. A first-order recurrence
  /// is a non-reduction recurrence relation in which the value of the
  /// recurrence in the current loop iteration equals a value defined in the
  /// previous iteration. \p SinkAfter includes pairs of instructions where the
  /// first will be rescheduled to appear after the second if/when the loop is
  /// vectorized. It may be augmented with additional pairs if needed in order
  /// to handle Phi as a first-order recurrence.
  static bool
  isFirstOrderRecurrence(PHINode *Phi, Loop *TheLoop,
                         DenseMap<Instruction *, Instruction *> &SinkAfter,
                         DominatorTree *DT);

  RecurrenceKind getRecurrenceKind() { return Kind; }

  MinMaxRecurrenceKind getMinMaxRecurrenceKind() { return MinMaxKind; }

  TrackingVH<Value> getRecurrenceStartValue() { return StartValue; }

  Instruction *getLoopExitInstr() { return LoopExitInstr; }

  /// Returns true if the recurrence has unsafe algebra which requires a relaxed
  /// floating-point model.
  bool hasUnsafeAlgebra() { return UnsafeAlgebraInst != nullptr; }

  /// Returns first unsafe algebra instruction in the PHI node's use-chain.
  Instruction *getUnsafeAlgebraInst() { return UnsafeAlgebraInst; }

  /// Returns true if the recurrence kind is an integer kind.
  static bool isIntegerRecurrenceKind(RecurrenceKind Kind);

  /// Returns true if the recurrence kind is a floating point kind.
  static bool isFloatingPointRecurrenceKind(RecurrenceKind Kind);

  /// Returns true if the recurrence kind is an arithmetic kind.
  static bool isArithmeticRecurrenceKind(RecurrenceKind Kind);

  /// Returns the type of the recurrence. This type can be narrower than the
  /// actual type of the Phi if the recurrence has been type-promoted.
  Type *getRecurrenceType() { return RecurrenceType; }

  /// Returns a reference to the instructions used for type-promoting the
  /// recurrence.
  SmallPtrSet<Instruction *, 8> &getCastInsts() { return CastInsts; }

  /// Returns true if all source operands of the recurrence are SExtInsts.
  bool isSigned() { return IsSigned; }

private:
  // The starting value of the recurrence.
  // It does not have to be zero!
  TrackingVH<Value> StartValue;
  // The instruction who's value is used outside the loop.
  Instruction *LoopExitInstr = nullptr;
  // The kind of the recurrence.
  RecurrenceKind Kind = RK_NoRecurrence;
  // If this a min/max recurrence the kind of recurrence.
  MinMaxRecurrenceKind MinMaxKind = MRK_Invalid;
  // First occurrence of unasfe algebra in the PHI's use-chain.
  Instruction *UnsafeAlgebraInst = nullptr;
  // The type of the recurrence.
  Type *RecurrenceType = nullptr;
  // True if all source operands of the recurrence are SExtInsts.
  bool IsSigned = false;
  // Instructions used for type-promoting the recurrence.
  SmallPtrSet<Instruction *, 8> CastInsts;
};

/// A struct for saving information about induction variables.
class InductionDescriptor {
public:
  /// This enum represents the kinds of inductions that we support.
  enum InductionKind {
    IK_NoInduction,  ///< Not an induction variable.
    IK_IntInduction, ///< Integer induction variable. Step = C.
    IK_PtrInduction, ///< Pointer induction var. Step = C / sizeof(elem).
    IK_FpInduction   ///< Floating point induction variable.
  };

public:
  /// Default constructor - creates an invalid induction.
  InductionDescriptor() = default;

  /// Get the consecutive direction. Returns:
  ///   0 - unknown or non-consecutive.
  ///   1 - consecutive and increasing.
  ///  -1 - consecutive and decreasing.
  int getConsecutiveDirection() const;

  Value *getStartValue() const { return StartValue; }
  InductionKind getKind() const { return IK; }
  const SCEV *getStep() const { return Step; }
  BinaryOperator *getInductionBinOp() const { return InductionBinOp; }
  ConstantInt *getConstIntStepValue() const;

  /// Returns true if \p Phi is an induction in the loop \p L. If \p Phi is an
  /// induction, the induction descriptor \p D will contain the data describing
  /// this induction. If by some other means the caller has a better SCEV
  /// expression for \p Phi than the one returned by the ScalarEvolution
  /// analysis, it can be passed through \p Expr. If the def-use chain
  /// associated with the phi includes casts (that we know we can ignore
  /// under proper runtime checks), they are passed through \p CastsToIgnore.
  static bool
  isInductionPHI(PHINode *Phi, const Loop *L, ScalarEvolution *SE,
                 InductionDescriptor &D, const SCEV *Expr = nullptr,
                 SmallVectorImpl<Instruction *> *CastsToIgnore = nullptr);

  /// Returns true if \p Phi is a floating point induction in the loop \p L.
  /// If \p Phi is an induction, the induction descriptor \p D will contain
  /// the data describing this induction.
  static bool isFPInductionPHI(PHINode *Phi, const Loop *L, ScalarEvolution *SE,
                               InductionDescriptor &D);

  /// Returns true if \p Phi is a loop \p L induction, in the context associated
  /// with the run-time predicate of PSE. If \p Assume is true, this can add
  /// further SCEV predicates to \p PSE in order to prove that \p Phi is an
  /// induction.
  /// If \p Phi is an induction, \p D will contain the data describing this
  /// induction.
  static bool isInductionPHI(PHINode *Phi, const Loop *L,
                             PredicatedScalarEvolution &PSE,
                             InductionDescriptor &D, bool Assume = false);

  /// Returns true if the induction type is FP and the binary operator does
  /// not have the "fast-math" property. Such operation requires a relaxed FP
  /// mode.
  bool hasUnsafeAlgebra() {
    return InductionBinOp && !cast<FPMathOperator>(InductionBinOp)->isFast();
  }

  /// Returns induction operator that does not have "fast-math" property
  /// and requires FP unsafe mode.
  Instruction *getUnsafeAlgebraInst() {
    if (!InductionBinOp || cast<FPMathOperator>(InductionBinOp)->isFast())
      return nullptr;
    return InductionBinOp;
  }

  /// Returns binary opcode of the induction operator.
  Instruction::BinaryOps getInductionOpcode() const {
    return InductionBinOp ? InductionBinOp->getOpcode()
                          : Instruction::BinaryOpsEnd;
  }

  /// Returns a reference to the type cast instructions in the induction
  /// update chain, that are redundant when guarded with a runtime
  /// SCEV overflow check.
  const SmallVectorImpl<Instruction *> &getCastInsts() const {
    return RedundantCasts;
  }

private:
  /// Private constructor - used by \c isInductionPHI.
  InductionDescriptor(Value *Start, InductionKind K, const SCEV *Step,
                      BinaryOperator *InductionBinOp = nullptr,
                      SmallVectorImpl<Instruction *> *Casts = nullptr);

  /// Start value.
  TrackingVH<Value> StartValue;
  /// Induction kind.
  InductionKind IK = IK_NoInduction;
  /// Step value.
  const SCEV *Step = nullptr;
  // Instruction that advances induction variable.
  BinaryOperator *InductionBinOp = nullptr;
  // Instructions used for type-casts of the induction variable,
  // that are redundant when guarded with a runtime SCEV overflow check.
  SmallVector<Instruction *, 2> RedundantCasts;
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_IVDESCRIPTORS_H
