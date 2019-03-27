//===- InstCombineInternal.h - InstCombine pass internals -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
///
/// This file provides internal interfaces used to implement the InstCombine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TRANSFORMS_INSTCOMBINE_INSTCOMBINEINTERNAL_H
#define LLVM_LIB_TRANSFORMS_INSTCOMBINE_INSTCOMBINEINTERNAL_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/TargetFolder.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/InstCombine/InstCombineWorklist.h"
#include "llvm/Transforms/Utils/Local.h"
#include <cassert>
#include <cstdint>

#define DEBUG_TYPE "instcombine"

using namespace llvm::PatternMatch;

namespace llvm {

class APInt;
class AssumptionCache;
class CallSite;
class DataLayout;
class DominatorTree;
class GEPOperator;
class GlobalVariable;
class LoopInfo;
class OptimizationRemarkEmitter;
class TargetLibraryInfo;
class User;

/// Assign a complexity or rank value to LLVM Values. This is used to reduce
/// the amount of pattern matching needed for compares and commutative
/// instructions. For example, if we have:
///   icmp ugt X, Constant
/// or
///   xor (add X, Constant), cast Z
///
/// We do not have to consider the commuted variants of these patterns because
/// canonicalization based on complexity guarantees the above ordering.
///
/// This routine maps IR values to various complexity ranks:
///   0 -> undef
///   1 -> Constants
///   2 -> Other non-instructions
///   3 -> Arguments
///   4 -> Cast and (f)neg/not instructions
///   5 -> Other instructions
static inline unsigned getComplexity(Value *V) {
  if (isa<Instruction>(V)) {
    if (isa<CastInst>(V) || match(V, m_Neg(m_Value())) ||
        match(V, m_Not(m_Value())) || match(V, m_FNeg(m_Value())))
      return 4;
    return 5;
  }
  if (isa<Argument>(V))
    return 3;
  return isa<Constant>(V) ? (isa<UndefValue>(V) ? 0 : 1) : 2;
}

/// Predicate canonicalization reduces the number of patterns that need to be
/// matched by other transforms. For example, we may swap the operands of a
/// conditional branch or select to create a compare with a canonical (inverted)
/// predicate which is then more likely to be matched with other values.
static inline bool isCanonicalPredicate(CmpInst::Predicate Pred) {
  switch (Pred) {
  case CmpInst::ICMP_NE:
  case CmpInst::ICMP_ULE:
  case CmpInst::ICMP_SLE:
  case CmpInst::ICMP_UGE:
  case CmpInst::ICMP_SGE:
  // TODO: There are 16 FCMP predicates. Should others be (not) canonical?
  case CmpInst::FCMP_ONE:
  case CmpInst::FCMP_OLE:
  case CmpInst::FCMP_OGE:
    return false;
  default:
    return true;
  }
}

/// Return the source operand of a potentially bitcasted value while optionally
/// checking if it has one use. If there is no bitcast or the one use check is
/// not met, return the input value itself.
static inline Value *peekThroughBitcast(Value *V, bool OneUseOnly = false) {
  if (auto *BitCast = dyn_cast<BitCastInst>(V))
    if (!OneUseOnly || BitCast->hasOneUse())
      return BitCast->getOperand(0);

  // V is not a bitcast or V has more than one use and OneUseOnly is true.
  return V;
}

/// Add one to a Constant
static inline Constant *AddOne(Constant *C) {
  return ConstantExpr::getAdd(C, ConstantInt::get(C->getType(), 1));
}

/// Subtract one from a Constant
static inline Constant *SubOne(Constant *C) {
  return ConstantExpr::getSub(C, ConstantInt::get(C->getType(), 1));
}

/// Return true if the specified value is free to invert (apply ~ to).
/// This happens in cases where the ~ can be eliminated.  If WillInvertAllUses
/// is true, work under the assumption that the caller intends to remove all
/// uses of V and only keep uses of ~V.
static inline bool IsFreeToInvert(Value *V, bool WillInvertAllUses) {
  // ~(~(X)) -> X.
  if (match(V, m_Not(m_Value())))
    return true;

  // Constants can be considered to be not'ed values.
  if (isa<ConstantInt>(V))
    return true;

  // A vector of constant integers can be inverted easily.
  if (V->getType()->isVectorTy() && isa<Constant>(V)) {
    unsigned NumElts = V->getType()->getVectorNumElements();
    for (unsigned i = 0; i != NumElts; ++i) {
      Constant *Elt = cast<Constant>(V)->getAggregateElement(i);
      if (!Elt)
        return false;

      if (isa<UndefValue>(Elt))
        continue;

      if (!isa<ConstantInt>(Elt))
        return false;
    }
    return true;
  }

  // Compares can be inverted if all of their uses are being modified to use the
  // ~V.
  if (isa<CmpInst>(V))
    return WillInvertAllUses;

  // If `V` is of the form `A + Constant` then `-1 - V` can be folded into `(-1
  // - Constant) - A` if we are willing to invert all of the uses.
  if (BinaryOperator *BO = dyn_cast<BinaryOperator>(V))
    if (BO->getOpcode() == Instruction::Add ||
        BO->getOpcode() == Instruction::Sub)
      if (isa<Constant>(BO->getOperand(0)) || isa<Constant>(BO->getOperand(1)))
        return WillInvertAllUses;

  // Selects with invertible operands are freely invertible
  if (match(V, m_Select(m_Value(), m_Not(m_Value()), m_Not(m_Value()))))
    return WillInvertAllUses;

  return false;
}

/// Specific patterns of overflow check idioms that we match.
enum OverflowCheckFlavor {
  OCF_UNSIGNED_ADD,
  OCF_SIGNED_ADD,
  OCF_UNSIGNED_SUB,
  OCF_SIGNED_SUB,
  OCF_UNSIGNED_MUL,
  OCF_SIGNED_MUL,

  OCF_INVALID
};

/// Returns the OverflowCheckFlavor corresponding to a overflow_with_op
/// intrinsic.
static inline OverflowCheckFlavor
IntrinsicIDToOverflowCheckFlavor(unsigned ID) {
  switch (ID) {
  default:
    return OCF_INVALID;
  case Intrinsic::uadd_with_overflow:
    return OCF_UNSIGNED_ADD;
  case Intrinsic::sadd_with_overflow:
    return OCF_SIGNED_ADD;
  case Intrinsic::usub_with_overflow:
    return OCF_UNSIGNED_SUB;
  case Intrinsic::ssub_with_overflow:
    return OCF_SIGNED_SUB;
  case Intrinsic::umul_with_overflow:
    return OCF_UNSIGNED_MUL;
  case Intrinsic::smul_with_overflow:
    return OCF_SIGNED_MUL;
  }
}

/// Some binary operators require special handling to avoid poison and undefined
/// behavior. If a constant vector has undef elements, replace those undefs with
/// identity constants if possible because those are always safe to execute.
/// If no identity constant exists, replace undef with some other safe constant.
static inline Constant *getSafeVectorConstantForBinop(
      BinaryOperator::BinaryOps Opcode, Constant *In, bool IsRHSConstant) {
  assert(In->getType()->isVectorTy() && "Not expecting scalars here");

  Type *EltTy = In->getType()->getVectorElementType();
  auto *SafeC = ConstantExpr::getBinOpIdentity(Opcode, EltTy, IsRHSConstant);
  if (!SafeC) {
    // TODO: Should this be available as a constant utility function? It is
    // similar to getBinOpAbsorber().
    if (IsRHSConstant) {
      switch (Opcode) {
      case Instruction::SRem: // X % 1 = 0
      case Instruction::URem: // X %u 1 = 0
        SafeC = ConstantInt::get(EltTy, 1);
        break;
      case Instruction::FRem: // X % 1.0 (doesn't simplify, but it is safe)
        SafeC = ConstantFP::get(EltTy, 1.0);
        break;
      default:
        llvm_unreachable("Only rem opcodes have no identity constant for RHS");
      }
    } else {
      switch (Opcode) {
      case Instruction::Shl:  // 0 << X = 0
      case Instruction::LShr: // 0 >>u X = 0
      case Instruction::AShr: // 0 >> X = 0
      case Instruction::SDiv: // 0 / X = 0
      case Instruction::UDiv: // 0 /u X = 0
      case Instruction::SRem: // 0 % X = 0
      case Instruction::URem: // 0 %u X = 0
      case Instruction::Sub:  // 0 - X (doesn't simplify, but it is safe)
      case Instruction::FSub: // 0.0 - X (doesn't simplify, but it is safe)
      case Instruction::FDiv: // 0.0 / X (doesn't simplify, but it is safe)
      case Instruction::FRem: // 0.0 % X = 0
        SafeC = Constant::getNullValue(EltTy);
        break;
      default:
        llvm_unreachable("Expected to find identity constant for opcode");
      }
    }
  }
  assert(SafeC && "Must have safe constant for binop");
  unsigned NumElts = In->getType()->getVectorNumElements();
  SmallVector<Constant *, 16> Out(NumElts);
  for (unsigned i = 0; i != NumElts; ++i) {
    Constant *C = In->getAggregateElement(i);
    Out[i] = isa<UndefValue>(C) ? SafeC : C;
  }
  return ConstantVector::get(Out);
}

/// The core instruction combiner logic.
///
/// This class provides both the logic to recursively visit instructions and
/// combine them.
class LLVM_LIBRARY_VISIBILITY InstCombiner
    : public InstVisitor<InstCombiner, Instruction *> {
  // FIXME: These members shouldn't be public.
public:
  /// A worklist of the instructions that need to be simplified.
  InstCombineWorklist &Worklist;

  /// An IRBuilder that automatically inserts new instructions into the
  /// worklist.
  using BuilderTy = IRBuilder<TargetFolder, IRBuilderCallbackInserter>;
  BuilderTy &Builder;

private:
  // Mode in which we are running the combiner.
  const bool MinimizeSize;

  /// Enable combines that trigger rarely but are costly in compiletime.
  const bool ExpensiveCombines;

  AliasAnalysis *AA;

  // Required analyses.
  AssumptionCache &AC;
  TargetLibraryInfo &TLI;
  DominatorTree &DT;
  const DataLayout &DL;
  const SimplifyQuery SQ;
  OptimizationRemarkEmitter &ORE;

  // Optional analyses. When non-null, these can both be used to do better
  // combining and will be updated to reflect any changes.
  LoopInfo *LI;

  bool MadeIRChange = false;

public:
  InstCombiner(InstCombineWorklist &Worklist, BuilderTy &Builder,
               bool MinimizeSize, bool ExpensiveCombines, AliasAnalysis *AA,
               AssumptionCache &AC, TargetLibraryInfo &TLI, DominatorTree &DT,
               OptimizationRemarkEmitter &ORE, const DataLayout &DL,
               LoopInfo *LI)
      : Worklist(Worklist), Builder(Builder), MinimizeSize(MinimizeSize),
        ExpensiveCombines(ExpensiveCombines), AA(AA), AC(AC), TLI(TLI), DT(DT),
        DL(DL), SQ(DL, &TLI, &DT, &AC), ORE(ORE), LI(LI) {}

  /// Run the combiner over the entire worklist until it is empty.
  ///
  /// \returns true if the IR is changed.
  bool run();

  AssumptionCache &getAssumptionCache() const { return AC; }

  const DataLayout &getDataLayout() const { return DL; }

  DominatorTree &getDominatorTree() const { return DT; }

  LoopInfo *getLoopInfo() const { return LI; }

  TargetLibraryInfo &getTargetLibraryInfo() const { return TLI; }

  // Visitation implementation - Implement instruction combining for different
  // instruction types.  The semantics are as follows:
  // Return Value:
  //    null        - No change was made
  //     I          - Change was made, I is still valid, I may be dead though
  //   otherwise    - Change was made, replace I with returned instruction
  //
  Instruction *visitAdd(BinaryOperator &I);
  Instruction *visitFAdd(BinaryOperator &I);
  Value *OptimizePointerDifference(Value *LHS, Value *RHS, Type *Ty);
  Instruction *visitSub(BinaryOperator &I);
  Instruction *visitFSub(BinaryOperator &I);
  Instruction *visitMul(BinaryOperator &I);
  Instruction *visitFMul(BinaryOperator &I);
  Instruction *visitURem(BinaryOperator &I);
  Instruction *visitSRem(BinaryOperator &I);
  Instruction *visitFRem(BinaryOperator &I);
  bool simplifyDivRemOfSelectWithZeroOp(BinaryOperator &I);
  Instruction *commonRemTransforms(BinaryOperator &I);
  Instruction *commonIRemTransforms(BinaryOperator &I);
  Instruction *commonDivTransforms(BinaryOperator &I);
  Instruction *commonIDivTransforms(BinaryOperator &I);
  Instruction *visitUDiv(BinaryOperator &I);
  Instruction *visitSDiv(BinaryOperator &I);
  Instruction *visitFDiv(BinaryOperator &I);
  Value *simplifyRangeCheck(ICmpInst *Cmp0, ICmpInst *Cmp1, bool Inverted);
  Instruction *visitAnd(BinaryOperator &I);
  Instruction *visitOr(BinaryOperator &I);
  Instruction *visitXor(BinaryOperator &I);
  Instruction *visitShl(BinaryOperator &I);
  Instruction *visitAShr(BinaryOperator &I);
  Instruction *visitLShr(BinaryOperator &I);
  Instruction *commonShiftTransforms(BinaryOperator &I);
  Instruction *visitFCmpInst(FCmpInst &I);
  Instruction *visitICmpInst(ICmpInst &I);
  Instruction *FoldShiftByConstant(Value *Op0, Constant *Op1,
                                   BinaryOperator &I);
  Instruction *commonCastTransforms(CastInst &CI);
  Instruction *commonPointerCastTransforms(CastInst &CI);
  Instruction *visitTrunc(TruncInst &CI);
  Instruction *visitZExt(ZExtInst &CI);
  Instruction *visitSExt(SExtInst &CI);
  Instruction *visitFPTrunc(FPTruncInst &CI);
  Instruction *visitFPExt(CastInst &CI);
  Instruction *visitFPToUI(FPToUIInst &FI);
  Instruction *visitFPToSI(FPToSIInst &FI);
  Instruction *visitUIToFP(CastInst &CI);
  Instruction *visitSIToFP(CastInst &CI);
  Instruction *visitPtrToInt(PtrToIntInst &CI);
  Instruction *visitIntToPtr(IntToPtrInst &CI);
  Instruction *visitBitCast(BitCastInst &CI);
  Instruction *visitAddrSpaceCast(AddrSpaceCastInst &CI);
  Instruction *FoldItoFPtoI(Instruction &FI);
  Instruction *visitSelectInst(SelectInst &SI);
  Instruction *visitCallInst(CallInst &CI);
  Instruction *visitInvokeInst(InvokeInst &II);

  Instruction *SliceUpIllegalIntegerPHI(PHINode &PN);
  Instruction *visitPHINode(PHINode &PN);
  Instruction *visitGetElementPtrInst(GetElementPtrInst &GEP);
  Instruction *visitAllocaInst(AllocaInst &AI);
  Instruction *visitAllocSite(Instruction &FI);
  Instruction *visitFree(CallInst &FI);
  Instruction *visitLoadInst(LoadInst &LI);
  Instruction *visitStoreInst(StoreInst &SI);
  Instruction *visitBranchInst(BranchInst &BI);
  Instruction *visitFenceInst(FenceInst &FI);
  Instruction *visitSwitchInst(SwitchInst &SI);
  Instruction *visitReturnInst(ReturnInst &RI);
  Instruction *visitInsertValueInst(InsertValueInst &IV);
  Instruction *visitInsertElementInst(InsertElementInst &IE);
  Instruction *visitExtractElementInst(ExtractElementInst &EI);
  Instruction *visitShuffleVectorInst(ShuffleVectorInst &SVI);
  Instruction *visitExtractValueInst(ExtractValueInst &EV);
  Instruction *visitLandingPadInst(LandingPadInst &LI);
  Instruction *visitVAStartInst(VAStartInst &I);
  Instruction *visitVACopyInst(VACopyInst &I);

  /// Specify what to return for unhandled instructions.
  Instruction *visitInstruction(Instruction &I) { return nullptr; }

  /// True when DB dominates all uses of DI except UI.
  /// UI must be in the same block as DI.
  /// The routine checks that the DI parent and DB are different.
  bool dominatesAllUses(const Instruction *DI, const Instruction *UI,
                        const BasicBlock *DB) const;

  /// Try to replace select with select operand SIOpd in SI-ICmp sequence.
  bool replacedSelectWithOperand(SelectInst *SI, const ICmpInst *Icmp,
                                 const unsigned SIOpd);

  /// Try to replace instruction \p I with value \p V which are pointers
  /// in different address space.
  /// \return true if successful.
  bool replacePointer(Instruction &I, Value *V);

private:
  bool shouldChangeType(unsigned FromBitWidth, unsigned ToBitWidth) const;
  bool shouldChangeType(Type *From, Type *To) const;
  Value *dyn_castNegVal(Value *V) const;
  Type *FindElementAtOffset(PointerType *PtrTy, int64_t Offset,
                            SmallVectorImpl<Value *> &NewIndices);

  /// Classify whether a cast is worth optimizing.
  ///
  /// This is a helper to decide whether the simplification of
  /// logic(cast(A), cast(B)) to cast(logic(A, B)) should be performed.
  ///
  /// \param CI The cast we are interested in.
  ///
  /// \return true if this cast actually results in any code being generated and
  /// if it cannot already be eliminated by some other transformation.
  bool shouldOptimizeCast(CastInst *CI);

  /// Try to optimize a sequence of instructions checking if an operation
  /// on LHS and RHS overflows.
  ///
  /// If this overflow check is done via one of the overflow check intrinsics,
  /// then CtxI has to be the call instruction calling that intrinsic.  If this
  /// overflow check is done by arithmetic followed by a compare, then CtxI has
  /// to be the arithmetic instruction.
  ///
  /// If a simplification is possible, stores the simplified result of the
  /// operation in OperationResult and result of the overflow check in
  /// OverflowResult, and return true.  If no simplification is possible,
  /// returns false.
  bool OptimizeOverflowCheck(OverflowCheckFlavor OCF, Value *LHS, Value *RHS,
                             Instruction &CtxI, Value *&OperationResult,
                             Constant *&OverflowResult);

  Instruction *visitCallSite(CallSite CS);
  Instruction *tryOptimizeCall(CallInst *CI);
  bool transformConstExprCastCall(CallSite CS);
  Instruction *transformCallThroughTrampoline(CallSite CS,
                                              IntrinsicInst *Tramp);

  /// Transform (zext icmp) to bitwise / integer operations in order to
  /// eliminate it.
  ///
  /// \param ICI The icmp of the (zext icmp) pair we are interested in.
  /// \parem CI The zext of the (zext icmp) pair we are interested in.
  /// \param DoTransform Pass false to just test whether the given (zext icmp)
  /// would be transformed. Pass true to actually perform the transformation.
  ///
  /// \return null if the transformation cannot be performed. If the
  /// transformation can be performed the new instruction that replaces the
  /// (zext icmp) pair will be returned (if \p DoTransform is false the
  /// unmodified \p ICI will be returned in this case).
  Instruction *transformZExtICmp(ICmpInst *ICI, ZExtInst &CI,
                                 bool DoTransform = true);

  Instruction *transformSExtICmp(ICmpInst *ICI, Instruction &CI);

  bool willNotOverflowSignedAdd(const Value *LHS, const Value *RHS,
                                const Instruction &CxtI) const {
    return computeOverflowForSignedAdd(LHS, RHS, &CxtI) ==
           OverflowResult::NeverOverflows;
  }

  bool willNotOverflowUnsignedAdd(const Value *LHS, const Value *RHS,
                                  const Instruction &CxtI) const {
    return computeOverflowForUnsignedAdd(LHS, RHS, &CxtI) ==
           OverflowResult::NeverOverflows;
  }

  bool willNotOverflowAdd(const Value *LHS, const Value *RHS,
                          const Instruction &CxtI, bool IsSigned) const {
    return IsSigned ? willNotOverflowSignedAdd(LHS, RHS, CxtI)
                    : willNotOverflowUnsignedAdd(LHS, RHS, CxtI);
  }

  bool willNotOverflowSignedSub(const Value *LHS, const Value *RHS,
                                const Instruction &CxtI) const {
    return computeOverflowForSignedSub(LHS, RHS, &CxtI) ==
           OverflowResult::NeverOverflows;
  }

  bool willNotOverflowUnsignedSub(const Value *LHS, const Value *RHS,
                                  const Instruction &CxtI) const {
    return computeOverflowForUnsignedSub(LHS, RHS, &CxtI) ==
           OverflowResult::NeverOverflows;
  }

  bool willNotOverflowSub(const Value *LHS, const Value *RHS,
                          const Instruction &CxtI, bool IsSigned) const {
    return IsSigned ? willNotOverflowSignedSub(LHS, RHS, CxtI)
                    : willNotOverflowUnsignedSub(LHS, RHS, CxtI);
  }

  bool willNotOverflowSignedMul(const Value *LHS, const Value *RHS,
                                const Instruction &CxtI) const {
    return computeOverflowForSignedMul(LHS, RHS, &CxtI) ==
           OverflowResult::NeverOverflows;
  }

  bool willNotOverflowUnsignedMul(const Value *LHS, const Value *RHS,
                                  const Instruction &CxtI) const {
    return computeOverflowForUnsignedMul(LHS, RHS, &CxtI) ==
           OverflowResult::NeverOverflows;
  }

  bool willNotOverflowMul(const Value *LHS, const Value *RHS,
                          const Instruction &CxtI, bool IsSigned) const {
    return IsSigned ? willNotOverflowSignedMul(LHS, RHS, CxtI)
                    : willNotOverflowUnsignedMul(LHS, RHS, CxtI);
  }

  bool willNotOverflow(BinaryOperator::BinaryOps Opcode, const Value *LHS,
                       const Value *RHS, const Instruction &CxtI,
                       bool IsSigned) const {
    switch (Opcode) {
    case Instruction::Add: return willNotOverflowAdd(LHS, RHS, CxtI, IsSigned);
    case Instruction::Sub: return willNotOverflowSub(LHS, RHS, CxtI, IsSigned);
    case Instruction::Mul: return willNotOverflowMul(LHS, RHS, CxtI, IsSigned);
    default: llvm_unreachable("Unexpected opcode for overflow query");
    }
  }

  Value *EmitGEPOffset(User *GEP);
  Instruction *scalarizePHI(ExtractElementInst &EI, PHINode *PN);
  Instruction *foldCastedBitwiseLogic(BinaryOperator &I);
  Instruction *narrowBinOp(TruncInst &Trunc);
  Instruction *narrowMaskedBinOp(BinaryOperator &And);
  Instruction *narrowMathIfNoOverflow(BinaryOperator &I);
  Instruction *narrowRotate(TruncInst &Trunc);
  Instruction *optimizeBitCastFromPhi(CastInst &CI, PHINode *PN);

  /// Determine if a pair of casts can be replaced by a single cast.
  ///
  /// \param CI1 The first of a pair of casts.
  /// \param CI2 The second of a pair of casts.
  ///
  /// \return 0 if the cast pair cannot be eliminated, otherwise returns an
  /// Instruction::CastOps value for a cast that can replace the pair, casting
  /// CI1->getSrcTy() to CI2->getDstTy().
  ///
  /// \see CastInst::isEliminableCastPair
  Instruction::CastOps isEliminableCastPair(const CastInst *CI1,
                                            const CastInst *CI2);

  Value *foldAndOfICmps(ICmpInst *LHS, ICmpInst *RHS, Instruction &CxtI);
  Value *foldOrOfICmps(ICmpInst *LHS, ICmpInst *RHS, Instruction &CxtI);
  Value *foldXorOfICmps(ICmpInst *LHS, ICmpInst *RHS);

  /// Optimize (fcmp)&(fcmp) or (fcmp)|(fcmp).
  /// NOTE: Unlike most of instcombine, this returns a Value which should
  /// already be inserted into the function.
  Value *foldLogicOfFCmps(FCmpInst *LHS, FCmpInst *RHS, bool IsAnd);

  Value *foldAndOrOfICmpsOfAndWithPow2(ICmpInst *LHS, ICmpInst *RHS,
                                       bool JoinedByAnd, Instruction &CxtI);
  Value *matchSelectFromAndOr(Value *A, Value *B, Value *C, Value *D);
  Value *getSelectCondition(Value *A, Value *B);

public:
  /// Inserts an instruction \p New before instruction \p Old
  ///
  /// Also adds the new instruction to the worklist and returns \p New so that
  /// it is suitable for use as the return from the visitation patterns.
  Instruction *InsertNewInstBefore(Instruction *New, Instruction &Old) {
    assert(New && !New->getParent() &&
           "New instruction already inserted into a basic block!");
    BasicBlock *BB = Old.getParent();
    BB->getInstList().insert(Old.getIterator(), New); // Insert inst
    Worklist.Add(New);
    return New;
  }

  /// Same as InsertNewInstBefore, but also sets the debug loc.
  Instruction *InsertNewInstWith(Instruction *New, Instruction &Old) {
    New->setDebugLoc(Old.getDebugLoc());
    return InsertNewInstBefore(New, Old);
  }

  /// A combiner-aware RAUW-like routine.
  ///
  /// This method is to be used when an instruction is found to be dead,
  /// replaceable with another preexisting expression. Here we add all uses of
  /// I to the worklist, replace all uses of I with the new value, then return
  /// I, so that the inst combiner will know that I was modified.
  Instruction *replaceInstUsesWith(Instruction &I, Value *V) {
    // If there are no uses to replace, then we return nullptr to indicate that
    // no changes were made to the program.
    if (I.use_empty()) return nullptr;

    Worklist.AddUsersToWorkList(I); // Add all modified instrs to worklist.

    // If we are replacing the instruction with itself, this must be in a
    // segment of unreachable code, so just clobber the instruction.
    if (&I == V)
      V = UndefValue::get(I.getType());

    LLVM_DEBUG(dbgs() << "IC: Replacing " << I << "\n"
                      << "    with " << *V << '\n');

    I.replaceAllUsesWith(V);
    return &I;
  }

  /// Creates a result tuple for an overflow intrinsic \p II with a given
  /// \p Result and a constant \p Overflow value.
  Instruction *CreateOverflowTuple(IntrinsicInst *II, Value *Result,
                                   Constant *Overflow) {
    Constant *V[] = {UndefValue::get(Result->getType()), Overflow};
    StructType *ST = cast<StructType>(II->getType());
    Constant *Struct = ConstantStruct::get(ST, V);
    return InsertValueInst::Create(Struct, Result, 0);
  }

  /// Combiner aware instruction erasure.
  ///
  /// When dealing with an instruction that has side effects or produces a void
  /// value, we can't rely on DCE to delete the instruction. Instead, visit
  /// methods should return the value returned by this function.
  Instruction *eraseInstFromFunction(Instruction &I) {
    LLVM_DEBUG(dbgs() << "IC: ERASE " << I << '\n');
    assert(I.use_empty() && "Cannot erase instruction that is used!");
    salvageDebugInfo(I);

    // Make sure that we reprocess all operands now that we reduced their
    // use counts.
    if (I.getNumOperands() < 8) {
      for (Use &Operand : I.operands())
        if (auto *Inst = dyn_cast<Instruction>(Operand))
          Worklist.Add(Inst);
    }
    Worklist.Remove(&I);
    I.eraseFromParent();
    MadeIRChange = true;
    return nullptr; // Don't do anything with FI
  }

  void computeKnownBits(const Value *V, KnownBits &Known,
                        unsigned Depth, const Instruction *CxtI) const {
    llvm::computeKnownBits(V, Known, DL, Depth, &AC, CxtI, &DT);
  }

  KnownBits computeKnownBits(const Value *V, unsigned Depth,
                             const Instruction *CxtI) const {
    return llvm::computeKnownBits(V, DL, Depth, &AC, CxtI, &DT);
  }

  bool isKnownToBeAPowerOfTwo(const Value *V, bool OrZero = false,
                              unsigned Depth = 0,
                              const Instruction *CxtI = nullptr) {
    return llvm::isKnownToBeAPowerOfTwo(V, DL, OrZero, Depth, &AC, CxtI, &DT);
  }

  bool MaskedValueIsZero(const Value *V, const APInt &Mask, unsigned Depth = 0,
                         const Instruction *CxtI = nullptr) const {
    return llvm::MaskedValueIsZero(V, Mask, DL, Depth, &AC, CxtI, &DT);
  }

  unsigned ComputeNumSignBits(const Value *Op, unsigned Depth = 0,
                              const Instruction *CxtI = nullptr) const {
    return llvm::ComputeNumSignBits(Op, DL, Depth, &AC, CxtI, &DT);
  }

  OverflowResult computeOverflowForUnsignedMul(const Value *LHS,
                                               const Value *RHS,
                                               const Instruction *CxtI) const {
    return llvm::computeOverflowForUnsignedMul(LHS, RHS, DL, &AC, CxtI, &DT);
  }

  OverflowResult computeOverflowForSignedMul(const Value *LHS,
	                                         const Value *RHS,
                                             const Instruction *CxtI) const {
    return llvm::computeOverflowForSignedMul(LHS, RHS, DL, &AC, CxtI, &DT);
  }

  OverflowResult computeOverflowForUnsignedAdd(const Value *LHS,
                                               const Value *RHS,
                                               const Instruction *CxtI) const {
    return llvm::computeOverflowForUnsignedAdd(LHS, RHS, DL, &AC, CxtI, &DT);
  }

  OverflowResult computeOverflowForSignedAdd(const Value *LHS,
                                             const Value *RHS,
                                             const Instruction *CxtI) const {
    return llvm::computeOverflowForSignedAdd(LHS, RHS, DL, &AC, CxtI, &DT);
  }

  OverflowResult computeOverflowForUnsignedSub(const Value *LHS,
                                               const Value *RHS,
                                               const Instruction *CxtI) const {
    return llvm::computeOverflowForUnsignedSub(LHS, RHS, DL, &AC, CxtI, &DT);
  }

  OverflowResult computeOverflowForSignedSub(const Value *LHS, const Value *RHS,
                                             const Instruction *CxtI) const {
    return llvm::computeOverflowForSignedSub(LHS, RHS, DL, &AC, CxtI, &DT);
  }

  /// Maximum size of array considered when transforming.
  uint64_t MaxArraySizeForCombine;

private:
  /// Performs a few simplifications for operators which are associative
  /// or commutative.
  bool SimplifyAssociativeOrCommutative(BinaryOperator &I);

  /// Tries to simplify binary operations which some other binary
  /// operation distributes over.
  ///
  /// It does this by either by factorizing out common terms (eg "(A*B)+(A*C)"
  /// -> "A*(B+C)") or expanding out if this results in simplifications (eg: "A
  /// & (B | C) -> (A&B) | (A&C)" if this is a win).  Returns the simplified
  /// value, or null if it didn't simplify.
  Value *SimplifyUsingDistributiveLaws(BinaryOperator &I);

  /// Tries to simplify add operations using the definition of remainder.
  ///
  /// The definition of remainder is X % C = X - (X / C ) * C. The add
  /// expression X % C0 + (( X / C0 ) % C1) * C0 can be simplified to
  /// X % (C0 * C1)
  Value *SimplifyAddWithRemainder(BinaryOperator &I);

  // Binary Op helper for select operations where the expression can be
  // efficiently reorganized.
  Value *SimplifySelectsFeedingBinaryOp(BinaryOperator &I, Value *LHS,
                                        Value *RHS);

  /// This tries to simplify binary operations by factorizing out common terms
  /// (e. g. "(A*B)+(A*C)" -> "A*(B+C)").
  Value *tryFactorization(BinaryOperator &, Instruction::BinaryOps, Value *,
                          Value *, Value *, Value *);

  /// Match a select chain which produces one of three values based on whether
  /// the LHS is less than, equal to, or greater than RHS respectively.
  /// Return true if we matched a three way compare idiom. The LHS, RHS, Less,
  /// Equal and Greater values are saved in the matching process and returned to
  /// the caller.
  bool matchThreeWayIntCompare(SelectInst *SI, Value *&LHS, Value *&RHS,
                               ConstantInt *&Less, ConstantInt *&Equal,
                               ConstantInt *&Greater);

  /// Attempts to replace V with a simpler value based on the demanded
  /// bits.
  Value *SimplifyDemandedUseBits(Value *V, APInt DemandedMask, KnownBits &Known,
                                 unsigned Depth, Instruction *CxtI);
  bool SimplifyDemandedBits(Instruction *I, unsigned Op,
                            const APInt &DemandedMask, KnownBits &Known,
                            unsigned Depth = 0);

  /// Helper routine of SimplifyDemandedUseBits. It computes KnownZero/KnownOne
  /// bits. It also tries to handle simplifications that can be done based on
  /// DemandedMask, but without modifying the Instruction.
  Value *SimplifyMultipleUseDemandedBits(Instruction *I,
                                         const APInt &DemandedMask,
                                         KnownBits &Known,
                                         unsigned Depth, Instruction *CxtI);

  /// Helper routine of SimplifyDemandedUseBits. It tries to simplify demanded
  /// bit for "r1 = shr x, c1; r2 = shl r1, c2" instruction sequence.
  Value *simplifyShrShlDemandedBits(
      Instruction *Shr, const APInt &ShrOp1, Instruction *Shl,
      const APInt &ShlOp1, const APInt &DemandedMask, KnownBits &Known);

  /// Tries to simplify operands to an integer instruction based on its
  /// demanded bits.
  bool SimplifyDemandedInstructionBits(Instruction &Inst);

  Value *simplifyAMDGCNMemoryIntrinsicDemanded(IntrinsicInst *II,
                                               APInt DemandedElts,
                                               int DmaskIdx = -1,
                                               int TFCIdx = -1);

  Value *SimplifyDemandedVectorElts(Value *V, APInt DemandedElts,
                                    APInt &UndefElts, unsigned Depth = 0);

  /// Canonicalize the position of binops relative to shufflevector.
  Instruction *foldVectorBinop(BinaryOperator &Inst);

  /// Given a binary operator, cast instruction, or select which has a PHI node
  /// as operand #0, see if we can fold the instruction into the PHI (which is
  /// only possible if all operands to the PHI are constants).
  Instruction *foldOpIntoPhi(Instruction &I, PHINode *PN);

  /// Given an instruction with a select as one operand and a constant as the
  /// other operand, try to fold the binary operator into the select arguments.
  /// This also works for Cast instructions, which obviously do not have a
  /// second operand.
  Instruction *FoldOpIntoSelect(Instruction &Op, SelectInst *SI);

  /// This is a convenience wrapper function for the above two functions.
  Instruction *foldBinOpIntoSelectOrPhi(BinaryOperator &I);

  Instruction *foldAddWithConstant(BinaryOperator &Add);

  /// Try to rotate an operation below a PHI node, using PHI nodes for
  /// its operands.
  Instruction *FoldPHIArgOpIntoPHI(PHINode &PN);
  Instruction *FoldPHIArgBinOpIntoPHI(PHINode &PN);
  Instruction *FoldPHIArgGEPIntoPHI(PHINode &PN);
  Instruction *FoldPHIArgLoadIntoPHI(PHINode &PN);
  Instruction *FoldPHIArgZextsIntoPHI(PHINode &PN);

  /// If an integer typed PHI has only one use which is an IntToPtr operation,
  /// replace the PHI with an existing pointer typed PHI if it exists. Otherwise
  /// insert a new pointer typed PHI and replace the original one.
  Instruction *FoldIntegerTypedPHI(PHINode &PN);

  /// Helper function for FoldPHIArgXIntoPHI() to set debug location for the
  /// folded operation.
  void PHIArgMergedDebugLoc(Instruction *Inst, PHINode &PN);

  Instruction *foldGEPICmp(GEPOperator *GEPLHS, Value *RHS,
                           ICmpInst::Predicate Cond, Instruction &I);
  Instruction *foldAllocaCmp(ICmpInst &ICI, const AllocaInst *Alloca,
                             const Value *Other);
  Instruction *foldCmpLoadFromIndexedGlobal(GetElementPtrInst *GEP,
                                            GlobalVariable *GV, CmpInst &ICI,
                                            ConstantInt *AndCst = nullptr);
  Instruction *foldFCmpIntToFPConst(FCmpInst &I, Instruction *LHSI,
                                    Constant *RHSC);
  Instruction *foldICmpAddOpConst(Value *X, const APInt &C,
                                  ICmpInst::Predicate Pred);
  Instruction *foldICmpWithCastAndCast(ICmpInst &ICI);

  Instruction *foldICmpUsingKnownBits(ICmpInst &Cmp);
  Instruction *foldICmpWithDominatingICmp(ICmpInst &Cmp);
  Instruction *foldICmpWithConstant(ICmpInst &Cmp);
  Instruction *foldICmpInstWithConstant(ICmpInst &Cmp);
  Instruction *foldICmpInstWithConstantNotInt(ICmpInst &Cmp);
  Instruction *foldICmpBinOp(ICmpInst &Cmp);
  Instruction *foldICmpEquality(ICmpInst &Cmp);
  Instruction *foldICmpWithZero(ICmpInst &Cmp);

  Instruction *foldICmpSelectConstant(ICmpInst &Cmp, SelectInst *Select,
                                      ConstantInt *C);
  Instruction *foldICmpBitCastConstant(ICmpInst &Cmp, BitCastInst *Bitcast,
                                       const APInt &C);
  Instruction *foldICmpTruncConstant(ICmpInst &Cmp, TruncInst *Trunc,
                                     const APInt &C);
  Instruction *foldICmpAndConstant(ICmpInst &Cmp, BinaryOperator *And,
                                   const APInt &C);
  Instruction *foldICmpXorConstant(ICmpInst &Cmp, BinaryOperator *Xor,
                                   const APInt &C);
  Instruction *foldICmpOrConstant(ICmpInst &Cmp, BinaryOperator *Or,
                                  const APInt &C);
  Instruction *foldICmpMulConstant(ICmpInst &Cmp, BinaryOperator *Mul,
                                   const APInt &C);
  Instruction *foldICmpShlConstant(ICmpInst &Cmp, BinaryOperator *Shl,
                                   const APInt &C);
  Instruction *foldICmpShrConstant(ICmpInst &Cmp, BinaryOperator *Shr,
                                   const APInt &C);
  Instruction *foldICmpUDivConstant(ICmpInst &Cmp, BinaryOperator *UDiv,
                                    const APInt &C);
  Instruction *foldICmpDivConstant(ICmpInst &Cmp, BinaryOperator *Div,
                                   const APInt &C);
  Instruction *foldICmpSubConstant(ICmpInst &Cmp, BinaryOperator *Sub,
                                   const APInt &C);
  Instruction *foldICmpAddConstant(ICmpInst &Cmp, BinaryOperator *Add,
                                   const APInt &C);
  Instruction *foldICmpAndConstConst(ICmpInst &Cmp, BinaryOperator *And,
                                     const APInt &C1);
  Instruction *foldICmpAndShift(ICmpInst &Cmp, BinaryOperator *And,
                                const APInt &C1, const APInt &C2);
  Instruction *foldICmpShrConstConst(ICmpInst &I, Value *ShAmt, const APInt &C1,
                                     const APInt &C2);
  Instruction *foldICmpShlConstConst(ICmpInst &I, Value *ShAmt, const APInt &C1,
                                     const APInt &C2);

  Instruction *foldICmpBinOpEqualityWithConstant(ICmpInst &Cmp,
                                                 BinaryOperator *BO,
                                                 const APInt &C);
  Instruction *foldICmpIntrinsicWithConstant(ICmpInst &ICI, const APInt &C);

  // Helpers of visitSelectInst().
  Instruction *foldSelectExtConst(SelectInst &Sel);
  Instruction *foldSelectOpOp(SelectInst &SI, Instruction *TI, Instruction *FI);
  Instruction *foldSelectIntoOp(SelectInst &SI, Value *, Value *);
  Instruction *foldSPFofSPF(Instruction *Inner, SelectPatternFlavor SPF1,
                            Value *A, Value *B, Instruction &Outer,
                            SelectPatternFlavor SPF2, Value *C);
  Instruction *foldSelectInstWithICmp(SelectInst &SI, ICmpInst *ICI);

  Instruction *OptAndOp(BinaryOperator *Op, ConstantInt *OpRHS,
                        ConstantInt *AndRHS, BinaryOperator &TheAnd);

  Value *insertRangeTest(Value *V, const APInt &Lo, const APInt &Hi,
                         bool isSigned, bool Inside);
  Instruction *PromoteCastOfAllocation(BitCastInst &CI, AllocaInst &AI);
  bool mergeStoreIntoSuccessor(StoreInst &SI);

  /// Given an 'or' instruction, check to see if it is part of a bswap idiom.
  /// If so, return the equivalent bswap intrinsic.
  Instruction *matchBSwap(BinaryOperator &Or);

  Instruction *SimplifyAnyMemTransfer(AnyMemTransferInst *MI);
  Instruction *SimplifyAnyMemSet(AnyMemSetInst *MI);

  Value *EvaluateInDifferentType(Value *V, Type *Ty, bool isSigned);

  /// Returns a value X such that Val = X * Scale, or null if none.
  ///
  /// If the multiplication is known not to overflow then NoSignedWrap is set.
  Value *Descale(Value *Val, APInt Scale, bool &NoSignedWrap);
};

} // end namespace llvm

#undef DEBUG_TYPE

#endif // LLVM_LIB_TRANSFORMS_INSTCOMBINE_INSTCOMBINEINTERNAL_H
