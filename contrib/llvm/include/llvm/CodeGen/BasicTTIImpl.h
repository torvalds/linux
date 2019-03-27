//===- BasicTTIImpl.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides a helper that implements much of the TTI interface in
/// terms of the target-independent code generator and TargetLowering
/// interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_BASICTTIIMPL_H
#define LLVM_CODEGEN_BASICTTIIMPL_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/TargetTransformInfoImpl.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MachineValueType.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <utility>

namespace llvm {

class Function;
class GlobalValue;
class LLVMContext;
class ScalarEvolution;
class SCEV;
class TargetMachine;

extern cl::opt<unsigned> PartialUnrollingThreshold;

/// Base class which can be used to help build a TTI implementation.
///
/// This class provides as much implementation of the TTI interface as is
/// possible using the target independent parts of the code generator.
///
/// In order to subclass it, your class must implement a getST() method to
/// return the subtarget, and a getTLI() method to return the target lowering.
/// We need these methods implemented in the derived class so that this class
/// doesn't have to duplicate storage for them.
template <typename T>
class BasicTTIImplBase : public TargetTransformInfoImplCRTPBase<T> {
private:
  using BaseT = TargetTransformInfoImplCRTPBase<T>;
  using TTI = TargetTransformInfo;

  /// Estimate a cost of Broadcast as an extract and sequence of insert
  /// operations.
  unsigned getBroadcastShuffleOverhead(Type *Ty) {
    assert(Ty->isVectorTy() && "Can only shuffle vectors");
    unsigned Cost = 0;
    // Broadcast cost is equal to the cost of extracting the zero'th element
    // plus the cost of inserting it into every element of the result vector.
    Cost += static_cast<T *>(this)->getVectorInstrCost(
        Instruction::ExtractElement, Ty, 0);

    for (int i = 0, e = Ty->getVectorNumElements(); i < e; ++i) {
      Cost += static_cast<T *>(this)->getVectorInstrCost(
          Instruction::InsertElement, Ty, i);
    }
    return Cost;
  }

  /// Estimate a cost of shuffle as a sequence of extract and insert
  /// operations.
  unsigned getPermuteShuffleOverhead(Type *Ty) {
    assert(Ty->isVectorTy() && "Can only shuffle vectors");
    unsigned Cost = 0;
    // Shuffle cost is equal to the cost of extracting element from its argument
    // plus the cost of inserting them onto the result vector.

    // e.g. <4 x float> has a mask of <0,5,2,7> i.e we need to extract from
    // index 0 of first vector, index 1 of second vector,index 2 of first
    // vector and finally index 3 of second vector and insert them at index
    // <0,1,2,3> of result vector.
    for (int i = 0, e = Ty->getVectorNumElements(); i < e; ++i) {
      Cost += static_cast<T *>(this)
                  ->getVectorInstrCost(Instruction::InsertElement, Ty, i);
      Cost += static_cast<T *>(this)
                  ->getVectorInstrCost(Instruction::ExtractElement, Ty, i);
    }
    return Cost;
  }

  /// Estimate a cost of subvector extraction as a sequence of extract and
  /// insert operations.
  unsigned getExtractSubvectorOverhead(Type *Ty, int Index, Type *SubTy) {
    assert(Ty && Ty->isVectorTy() && SubTy && SubTy->isVectorTy() &&
           "Can only extract subvectors from vectors");
    int NumSubElts = SubTy->getVectorNumElements();
    assert((Index + NumSubElts) <= (int)Ty->getVectorNumElements() &&
           "SK_ExtractSubvector index out of range");

    unsigned Cost = 0;
    // Subvector extraction cost is equal to the cost of extracting element from
    // the source type plus the cost of inserting them into the result vector
    // type.
    for (int i = 0; i != NumSubElts; ++i) {
      Cost += static_cast<T *>(this)->getVectorInstrCost(
          Instruction::ExtractElement, Ty, i + Index);
      Cost += static_cast<T *>(this)->getVectorInstrCost(
          Instruction::InsertElement, SubTy, i);
    }
    return Cost;
  }

  /// Estimate a cost of subvector insertion as a sequence of extract and
  /// insert operations.
  unsigned getInsertSubvectorOverhead(Type *Ty, int Index, Type *SubTy) {
    assert(Ty && Ty->isVectorTy() && SubTy && SubTy->isVectorTy() &&
           "Can only insert subvectors into vectors");
    int NumSubElts = SubTy->getVectorNumElements();
    assert((Index + NumSubElts) <= (int)Ty->getVectorNumElements() &&
           "SK_InsertSubvector index out of range");

    unsigned Cost = 0;
    // Subvector insertion cost is equal to the cost of extracting element from
    // the source type plus the cost of inserting them into the result vector
    // type.
    for (int i = 0; i != NumSubElts; ++i) {
      Cost += static_cast<T *>(this)->getVectorInstrCost(
          Instruction::ExtractElement, SubTy, i);
      Cost += static_cast<T *>(this)->getVectorInstrCost(
          Instruction::InsertElement, Ty, i + Index);
    }
    return Cost;
  }

  /// Local query method delegates up to T which *must* implement this!
  const TargetSubtargetInfo *getST() const {
    return static_cast<const T *>(this)->getST();
  }

  /// Local query method delegates up to T which *must* implement this!
  const TargetLoweringBase *getTLI() const {
    return static_cast<const T *>(this)->getTLI();
  }

  static ISD::MemIndexedMode getISDIndexedMode(TTI::MemIndexedMode M) {
    switch (M) {
      case TTI::MIM_Unindexed:
        return ISD::UNINDEXED;
      case TTI::MIM_PreInc:
        return ISD::PRE_INC;
      case TTI::MIM_PreDec:
        return ISD::PRE_DEC;
      case TTI::MIM_PostInc:
        return ISD::POST_INC;
      case TTI::MIM_PostDec:
        return ISD::POST_DEC;
    }
    llvm_unreachable("Unexpected MemIndexedMode");
  }

protected:
  explicit BasicTTIImplBase(const TargetMachine *TM, const DataLayout &DL)
      : BaseT(DL) {}

  using TargetTransformInfoImplBase::DL;

public:
  /// \name Scalar TTI Implementations
  /// @{
  bool allowsMisalignedMemoryAccesses(LLVMContext &Context,
                                      unsigned BitWidth, unsigned AddressSpace,
                                      unsigned Alignment, bool *Fast) const {
    EVT E = EVT::getIntegerVT(Context, BitWidth);
    return getTLI()->allowsMisalignedMemoryAccesses(E, AddressSpace, Alignment, Fast);
  }

  bool hasBranchDivergence() { return false; }

  bool isSourceOfDivergence(const Value *V) { return false; }

  bool isAlwaysUniform(const Value *V) { return false; }

  unsigned getFlatAddressSpace() {
    // Return an invalid address space.
    return -1;
  }

  bool isLegalAddImmediate(int64_t imm) {
    return getTLI()->isLegalAddImmediate(imm);
  }

  bool isLegalICmpImmediate(int64_t imm) {
    return getTLI()->isLegalICmpImmediate(imm);
  }

  bool isLegalAddressingMode(Type *Ty, GlobalValue *BaseGV, int64_t BaseOffset,
                             bool HasBaseReg, int64_t Scale,
                             unsigned AddrSpace, Instruction *I = nullptr) {
    TargetLoweringBase::AddrMode AM;
    AM.BaseGV = BaseGV;
    AM.BaseOffs = BaseOffset;
    AM.HasBaseReg = HasBaseReg;
    AM.Scale = Scale;
    return getTLI()->isLegalAddressingMode(DL, AM, Ty, AddrSpace, I);
  }

  bool isIndexedLoadLegal(TTI::MemIndexedMode M, Type *Ty,
                          const DataLayout &DL) const {
    EVT VT = getTLI()->getValueType(DL, Ty);
    return getTLI()->isIndexedLoadLegal(getISDIndexedMode(M), VT);
  }

  bool isIndexedStoreLegal(TTI::MemIndexedMode M, Type *Ty,
                           const DataLayout &DL) const {
    EVT VT = getTLI()->getValueType(DL, Ty);
    return getTLI()->isIndexedStoreLegal(getISDIndexedMode(M), VT);
  }

  bool isLSRCostLess(TTI::LSRCost C1, TTI::LSRCost C2) {
    return TargetTransformInfoImplBase::isLSRCostLess(C1, C2);
  }

  int getScalingFactorCost(Type *Ty, GlobalValue *BaseGV, int64_t BaseOffset,
                           bool HasBaseReg, int64_t Scale, unsigned AddrSpace) {
    TargetLoweringBase::AddrMode AM;
    AM.BaseGV = BaseGV;
    AM.BaseOffs = BaseOffset;
    AM.HasBaseReg = HasBaseReg;
    AM.Scale = Scale;
    return getTLI()->getScalingFactorCost(DL, AM, Ty, AddrSpace);
  }

  bool isTruncateFree(Type *Ty1, Type *Ty2) {
    return getTLI()->isTruncateFree(Ty1, Ty2);
  }

  bool isProfitableToHoist(Instruction *I) {
    return getTLI()->isProfitableToHoist(I);
  }

  bool useAA() const { return getST()->useAA(); }

  bool isTypeLegal(Type *Ty) {
    EVT VT = getTLI()->getValueType(DL, Ty);
    return getTLI()->isTypeLegal(VT);
  }

  int getGEPCost(Type *PointeeType, const Value *Ptr,
                 ArrayRef<const Value *> Operands) {
    return BaseT::getGEPCost(PointeeType, Ptr, Operands);
  }

  int getExtCost(const Instruction *I, const Value *Src) {
    if (getTLI()->isExtFree(I))
      return TargetTransformInfo::TCC_Free;

    if (isa<ZExtInst>(I) || isa<SExtInst>(I))
      if (const LoadInst *LI = dyn_cast<LoadInst>(Src))
        if (getTLI()->isExtLoad(LI, I, DL))
          return TargetTransformInfo::TCC_Free;

    return TargetTransformInfo::TCC_Basic;
  }

  unsigned getIntrinsicCost(Intrinsic::ID IID, Type *RetTy,
                            ArrayRef<const Value *> Arguments) {
    return BaseT::getIntrinsicCost(IID, RetTy, Arguments);
  }

  unsigned getIntrinsicCost(Intrinsic::ID IID, Type *RetTy,
                            ArrayRef<Type *> ParamTys) {
    if (IID == Intrinsic::cttz) {
      if (getTLI()->isCheapToSpeculateCttz())
        return TargetTransformInfo::TCC_Basic;
      return TargetTransformInfo::TCC_Expensive;
    }

    if (IID == Intrinsic::ctlz) {
      if (getTLI()->isCheapToSpeculateCtlz())
        return TargetTransformInfo::TCC_Basic;
      return TargetTransformInfo::TCC_Expensive;
    }

    return BaseT::getIntrinsicCost(IID, RetTy, ParamTys);
  }

  unsigned getEstimatedNumberOfCaseClusters(const SwitchInst &SI,
                                            unsigned &JumpTableSize) {
    /// Try to find the estimated number of clusters. Note that the number of
    /// clusters identified in this function could be different from the actural
    /// numbers found in lowering. This function ignore switches that are
    /// lowered with a mix of jump table / bit test / BTree. This function was
    /// initially intended to be used when estimating the cost of switch in
    /// inline cost heuristic, but it's a generic cost model to be used in other
    /// places (e.g., in loop unrolling).
    unsigned N = SI.getNumCases();
    const TargetLoweringBase *TLI = getTLI();
    const DataLayout &DL = this->getDataLayout();

    JumpTableSize = 0;
    bool IsJTAllowed = TLI->areJTsAllowed(SI.getParent()->getParent());

    // Early exit if both a jump table and bit test are not allowed.
    if (N < 1 || (!IsJTAllowed && DL.getIndexSizeInBits(0u) < N))
      return N;

    APInt MaxCaseVal = SI.case_begin()->getCaseValue()->getValue();
    APInt MinCaseVal = MaxCaseVal;
    for (auto CI : SI.cases()) {
      const APInt &CaseVal = CI.getCaseValue()->getValue();
      if (CaseVal.sgt(MaxCaseVal))
        MaxCaseVal = CaseVal;
      if (CaseVal.slt(MinCaseVal))
        MinCaseVal = CaseVal;
    }

    // Check if suitable for a bit test
    if (N <= DL.getIndexSizeInBits(0u)) {
      SmallPtrSet<const BasicBlock *, 4> Dests;
      for (auto I : SI.cases())
        Dests.insert(I.getCaseSuccessor());

      if (TLI->isSuitableForBitTests(Dests.size(), N, MinCaseVal, MaxCaseVal,
                                     DL))
        return 1;
    }

    // Check if suitable for a jump table.
    if (IsJTAllowed) {
      if (N < 2 || N < TLI->getMinimumJumpTableEntries())
        return N;
      uint64_t Range =
          (MaxCaseVal - MinCaseVal)
              .getLimitedValue(std::numeric_limits<uint64_t>::max() - 1) + 1;
      // Check whether a range of clusters is dense enough for a jump table
      if (TLI->isSuitableForJumpTable(&SI, N, Range)) {
        JumpTableSize = Range;
        return 1;
      }
    }
    return N;
  }

  unsigned getJumpBufAlignment() { return getTLI()->getJumpBufAlignment(); }

  unsigned getJumpBufSize() { return getTLI()->getJumpBufSize(); }

  bool shouldBuildLookupTables() {
    const TargetLoweringBase *TLI = getTLI();
    return TLI->isOperationLegalOrCustom(ISD::BR_JT, MVT::Other) ||
           TLI->isOperationLegalOrCustom(ISD::BRIND, MVT::Other);
  }

  bool haveFastSqrt(Type *Ty) {
    const TargetLoweringBase *TLI = getTLI();
    EVT VT = TLI->getValueType(DL, Ty);
    return TLI->isTypeLegal(VT) &&
           TLI->isOperationLegalOrCustom(ISD::FSQRT, VT);
  }

  bool isFCmpOrdCheaperThanFCmpZero(Type *Ty) {
    return true;
  }

  unsigned getFPOpCost(Type *Ty) {
    // Check whether FADD is available, as a proxy for floating-point in
    // general.
    const TargetLoweringBase *TLI = getTLI();
    EVT VT = TLI->getValueType(DL, Ty);
    if (TLI->isOperationLegalOrCustomOrPromote(ISD::FADD, VT))
      return TargetTransformInfo::TCC_Basic;
    return TargetTransformInfo::TCC_Expensive;
  }

  unsigned getOperationCost(unsigned Opcode, Type *Ty, Type *OpTy) {
    const TargetLoweringBase *TLI = getTLI();
    switch (Opcode) {
    default: break;
    case Instruction::Trunc:
      if (TLI->isTruncateFree(OpTy, Ty))
        return TargetTransformInfo::TCC_Free;
      return TargetTransformInfo::TCC_Basic;
    case Instruction::ZExt:
      if (TLI->isZExtFree(OpTy, Ty))
        return TargetTransformInfo::TCC_Free;
      return TargetTransformInfo::TCC_Basic;
    }

    return BaseT::getOperationCost(Opcode, Ty, OpTy);
  }

  unsigned getInliningThresholdMultiplier() { return 1; }

  void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                               TTI::UnrollingPreferences &UP) {
    // This unrolling functionality is target independent, but to provide some
    // motivation for its intended use, for x86:

    // According to the Intel 64 and IA-32 Architectures Optimization Reference
    // Manual, Intel Core models and later have a loop stream detector (and
    // associated uop queue) that can benefit from partial unrolling.
    // The relevant requirements are:
    //  - The loop must have no more than 4 (8 for Nehalem and later) branches
    //    taken, and none of them may be calls.
    //  - The loop can have no more than 18 (28 for Nehalem and later) uops.

    // According to the Software Optimization Guide for AMD Family 15h
    // Processors, models 30h-4fh (Steamroller and later) have a loop predictor
    // and loop buffer which can benefit from partial unrolling.
    // The relevant requirements are:
    //  - The loop must have fewer than 16 branches
    //  - The loop must have less than 40 uops in all executed loop branches

    // The number of taken branches in a loop is hard to estimate here, and
    // benchmarking has revealed that it is better not to be conservative when
    // estimating the branch count. As a result, we'll ignore the branch limits
    // until someone finds a case where it matters in practice.

    unsigned MaxOps;
    const TargetSubtargetInfo *ST = getST();
    if (PartialUnrollingThreshold.getNumOccurrences() > 0)
      MaxOps = PartialUnrollingThreshold;
    else if (ST->getSchedModel().LoopMicroOpBufferSize > 0)
      MaxOps = ST->getSchedModel().LoopMicroOpBufferSize;
    else
      return;

    // Scan the loop: don't unroll loops with calls.
    for (Loop::block_iterator I = L->block_begin(), E = L->block_end(); I != E;
         ++I) {
      BasicBlock *BB = *I;

      for (BasicBlock::iterator J = BB->begin(), JE = BB->end(); J != JE; ++J)
        if (isa<CallInst>(J) || isa<InvokeInst>(J)) {
          ImmutableCallSite CS(&*J);
          if (const Function *F = CS.getCalledFunction()) {
            if (!static_cast<T *>(this)->isLoweredToCall(F))
              continue;
          }

          return;
        }
    }

    // Enable runtime and partial unrolling up to the specified size.
    // Enable using trip count upper bound to unroll loops.
    UP.Partial = UP.Runtime = UP.UpperBound = true;
    UP.PartialThreshold = MaxOps;

    // Avoid unrolling when optimizing for size.
    UP.OptSizeThreshold = 0;
    UP.PartialOptSizeThreshold = 0;

    // Set number of instructions optimized when "back edge"
    // becomes "fall through" to default value of 2.
    UP.BEInsns = 2;
  }

  int getInstructionLatency(const Instruction *I) {
    if (isa<LoadInst>(I))
      return getST()->getSchedModel().DefaultLoadLatency;

    return BaseT::getInstructionLatency(I);
  }

  /// @}

  /// \name Vector TTI Implementations
  /// @{

  unsigned getNumberOfRegisters(bool Vector) { return Vector ? 0 : 1; }

  unsigned getRegisterBitWidth(bool Vector) const { return 32; }

  /// Estimate the overhead of scalarizing an instruction. Insert and Extract
  /// are set if the result needs to be inserted and/or extracted from vectors.
  unsigned getScalarizationOverhead(Type *Ty, bool Insert, bool Extract) {
    assert(Ty->isVectorTy() && "Can only scalarize vectors");
    unsigned Cost = 0;

    for (int i = 0, e = Ty->getVectorNumElements(); i < e; ++i) {
      if (Insert)
        Cost += static_cast<T *>(this)
                    ->getVectorInstrCost(Instruction::InsertElement, Ty, i);
      if (Extract)
        Cost += static_cast<T *>(this)
                    ->getVectorInstrCost(Instruction::ExtractElement, Ty, i);
    }

    return Cost;
  }

  /// Estimate the overhead of scalarizing an instructions unique
  /// non-constant operands. The types of the arguments are ordinarily
  /// scalar, in which case the costs are multiplied with VF.
  unsigned getOperandsScalarizationOverhead(ArrayRef<const Value *> Args,
                                            unsigned VF) {
    unsigned Cost = 0;
    SmallPtrSet<const Value*, 4> UniqueOperands;
    for (const Value *A : Args) {
      if (!isa<Constant>(A) && UniqueOperands.insert(A).second) {
        Type *VecTy = nullptr;
        if (A->getType()->isVectorTy()) {
          VecTy = A->getType();
          // If A is a vector operand, VF should be 1 or correspond to A.
          assert((VF == 1 || VF == VecTy->getVectorNumElements()) &&
                 "Vector argument does not match VF");
        }
        else
          VecTy = VectorType::get(A->getType(), VF);

        Cost += getScalarizationOverhead(VecTy, false, true);
      }
    }

    return Cost;
  }

  unsigned getScalarizationOverhead(Type *VecTy, ArrayRef<const Value *> Args) {
    assert(VecTy->isVectorTy());

    unsigned Cost = 0;

    Cost += getScalarizationOverhead(VecTy, true, false);
    if (!Args.empty())
      Cost += getOperandsScalarizationOverhead(Args,
                                               VecTy->getVectorNumElements());
    else
      // When no information on arguments is provided, we add the cost
      // associated with one argument as a heuristic.
      Cost += getScalarizationOverhead(VecTy, false, true);

    return Cost;
  }

  unsigned getMaxInterleaveFactor(unsigned VF) { return 1; }

  unsigned getArithmeticInstrCost(
      unsigned Opcode, Type *Ty,
      TTI::OperandValueKind Opd1Info = TTI::OK_AnyValue,
      TTI::OperandValueKind Opd2Info = TTI::OK_AnyValue,
      TTI::OperandValueProperties Opd1PropInfo = TTI::OP_None,
      TTI::OperandValueProperties Opd2PropInfo = TTI::OP_None,
      ArrayRef<const Value *> Args = ArrayRef<const Value *>()) {
    // Check if any of the operands are vector operands.
    const TargetLoweringBase *TLI = getTLI();
    int ISD = TLI->InstructionOpcodeToISD(Opcode);
    assert(ISD && "Invalid opcode");

    std::pair<unsigned, MVT> LT = TLI->getTypeLegalizationCost(DL, Ty);

    bool IsFloat = Ty->isFPOrFPVectorTy();
    // Assume that floating point arithmetic operations cost twice as much as
    // integer operations.
    unsigned OpCost = (IsFloat ? 2 : 1);

    if (TLI->isOperationLegalOrPromote(ISD, LT.second)) {
      // The operation is legal. Assume it costs 1.
      // TODO: Once we have extract/insert subvector cost we need to use them.
      return LT.first * OpCost;
    }

    if (!TLI->isOperationExpand(ISD, LT.second)) {
      // If the operation is custom lowered, then assume that the code is twice
      // as expensive.
      return LT.first * 2 * OpCost;
    }

    // Else, assume that we need to scalarize this op.
    // TODO: If one of the types get legalized by splitting, handle this
    // similarly to what getCastInstrCost() does.
    if (Ty->isVectorTy()) {
      unsigned Num = Ty->getVectorNumElements();
      unsigned Cost = static_cast<T *>(this)
                          ->getArithmeticInstrCost(Opcode, Ty->getScalarType());
      // Return the cost of multiple scalar invocation plus the cost of
      // inserting and extracting the values.
      return getScalarizationOverhead(Ty, Args) + Num * Cost;
    }

    // We don't know anything about this scalar instruction.
    return OpCost;
  }

  unsigned getShuffleCost(TTI::ShuffleKind Kind, Type *Tp, int Index,
                          Type *SubTp) {
    switch (Kind) {
    case TTI::SK_Broadcast:
      return getBroadcastShuffleOverhead(Tp);
    case TTI::SK_Select:
    case TTI::SK_Reverse:
    case TTI::SK_Transpose:
    case TTI::SK_PermuteSingleSrc:
    case TTI::SK_PermuteTwoSrc:
      return getPermuteShuffleOverhead(Tp);
    case TTI::SK_ExtractSubvector:
      return getExtractSubvectorOverhead(Tp, Index, SubTp);
    case TTI::SK_InsertSubvector:
      return getInsertSubvectorOverhead(Tp, Index, SubTp);
    }
    llvm_unreachable("Unknown TTI::ShuffleKind");
  }

  unsigned getCastInstrCost(unsigned Opcode, Type *Dst, Type *Src,
                            const Instruction *I = nullptr) {
    const TargetLoweringBase *TLI = getTLI();
    int ISD = TLI->InstructionOpcodeToISD(Opcode);
    assert(ISD && "Invalid opcode");
    std::pair<unsigned, MVT> SrcLT = TLI->getTypeLegalizationCost(DL, Src);
    std::pair<unsigned, MVT> DstLT = TLI->getTypeLegalizationCost(DL, Dst);

    // Check for NOOP conversions.
    if (SrcLT.first == DstLT.first &&
        SrcLT.second.getSizeInBits() == DstLT.second.getSizeInBits()) {

      // Bitcast between types that are legalized to the same type are free.
      if (Opcode == Instruction::BitCast || Opcode == Instruction::Trunc)
        return 0;
    }

    if (Opcode == Instruction::Trunc &&
        TLI->isTruncateFree(SrcLT.second, DstLT.second))
      return 0;

    if (Opcode == Instruction::ZExt &&
        TLI->isZExtFree(SrcLT.second, DstLT.second))
      return 0;

    if (Opcode == Instruction::AddrSpaceCast &&
        TLI->isNoopAddrSpaceCast(Src->getPointerAddressSpace(),
                                 Dst->getPointerAddressSpace()))
      return 0;

    // If this is a zext/sext of a load, return 0 if the corresponding
    // extending load exists on target.
    if ((Opcode == Instruction::ZExt || Opcode == Instruction::SExt) &&
        I && isa<LoadInst>(I->getOperand(0))) {
        EVT ExtVT = EVT::getEVT(Dst);
        EVT LoadVT = EVT::getEVT(Src);
        unsigned LType =
          ((Opcode == Instruction::ZExt) ? ISD::ZEXTLOAD : ISD::SEXTLOAD);
        if (TLI->isLoadExtLegal(LType, ExtVT, LoadVT))
          return 0;
    }

    // If the cast is marked as legal (or promote) then assume low cost.
    if (SrcLT.first == DstLT.first &&
        TLI->isOperationLegalOrPromote(ISD, DstLT.second))
      return 1;

    // Handle scalar conversions.
    if (!Src->isVectorTy() && !Dst->isVectorTy()) {
      // Scalar bitcasts are usually free.
      if (Opcode == Instruction::BitCast)
        return 0;

      // Just check the op cost. If the operation is legal then assume it costs
      // 1.
      if (!TLI->isOperationExpand(ISD, DstLT.second))
        return 1;

      // Assume that illegal scalar instruction are expensive.
      return 4;
    }

    // Check vector-to-vector casts.
    if (Dst->isVectorTy() && Src->isVectorTy()) {
      // If the cast is between same-sized registers, then the check is simple.
      if (SrcLT.first == DstLT.first &&
          SrcLT.second.getSizeInBits() == DstLT.second.getSizeInBits()) {

        // Assume that Zext is done using AND.
        if (Opcode == Instruction::ZExt)
          return 1;

        // Assume that sext is done using SHL and SRA.
        if (Opcode == Instruction::SExt)
          return 2;

        // Just check the op cost. If the operation is legal then assume it
        // costs
        // 1 and multiply by the type-legalization overhead.
        if (!TLI->isOperationExpand(ISD, DstLT.second))
          return SrcLT.first * 1;
      }

      // If we are legalizing by splitting, query the concrete TTI for the cost
      // of casting the original vector twice. We also need to factor in the
      // cost of the split itself. Count that as 1, to be consistent with
      // TLI->getTypeLegalizationCost().
      if ((TLI->getTypeAction(Src->getContext(), TLI->getValueType(DL, Src)) ==
           TargetLowering::TypeSplitVector) ||
          (TLI->getTypeAction(Dst->getContext(), TLI->getValueType(DL, Dst)) ==
           TargetLowering::TypeSplitVector)) {
        Type *SplitDst = VectorType::get(Dst->getVectorElementType(),
                                         Dst->getVectorNumElements() / 2);
        Type *SplitSrc = VectorType::get(Src->getVectorElementType(),
                                         Src->getVectorNumElements() / 2);
        T *TTI = static_cast<T *>(this);
        return TTI->getVectorSplitCost() +
               (2 * TTI->getCastInstrCost(Opcode, SplitDst, SplitSrc, I));
      }

      // In other cases where the source or destination are illegal, assume
      // the operation will get scalarized.
      unsigned Num = Dst->getVectorNumElements();
      unsigned Cost = static_cast<T *>(this)->getCastInstrCost(
          Opcode, Dst->getScalarType(), Src->getScalarType(), I);

      // Return the cost of multiple scalar invocation plus the cost of
      // inserting and extracting the values.
      return getScalarizationOverhead(Dst, true, true) + Num * Cost;
    }

    // We already handled vector-to-vector and scalar-to-scalar conversions.
    // This
    // is where we handle bitcast between vectors and scalars. We need to assume
    //  that the conversion is scalarized in one way or another.
    if (Opcode == Instruction::BitCast)
      // Illegal bitcasts are done by storing and loading from a stack slot.
      return (Src->isVectorTy() ? getScalarizationOverhead(Src, false, true)
                                : 0) +
             (Dst->isVectorTy() ? getScalarizationOverhead(Dst, true, false)
                                : 0);

    llvm_unreachable("Unhandled cast");
  }

  unsigned getExtractWithExtendCost(unsigned Opcode, Type *Dst,
                                    VectorType *VecTy, unsigned Index) {
    return static_cast<T *>(this)->getVectorInstrCost(
               Instruction::ExtractElement, VecTy, Index) +
           static_cast<T *>(this)->getCastInstrCost(Opcode, Dst,
                                                    VecTy->getElementType());
  }

  unsigned getCFInstrCost(unsigned Opcode) {
    // Branches are assumed to be predicted.
    return 0;
  }

  unsigned getCmpSelInstrCost(unsigned Opcode, Type *ValTy, Type *CondTy,
                              const Instruction *I) {
    const TargetLoweringBase *TLI = getTLI();
    int ISD = TLI->InstructionOpcodeToISD(Opcode);
    assert(ISD && "Invalid opcode");

    // Selects on vectors are actually vector selects.
    if (ISD == ISD::SELECT) {
      assert(CondTy && "CondTy must exist");
      if (CondTy->isVectorTy())
        ISD = ISD::VSELECT;
    }
    std::pair<unsigned, MVT> LT = TLI->getTypeLegalizationCost(DL, ValTy);

    if (!(ValTy->isVectorTy() && !LT.second.isVector()) &&
        !TLI->isOperationExpand(ISD, LT.second)) {
      // The operation is legal. Assume it costs 1. Multiply
      // by the type-legalization overhead.
      return LT.first * 1;
    }

    // Otherwise, assume that the cast is scalarized.
    // TODO: If one of the types get legalized by splitting, handle this
    // similarly to what getCastInstrCost() does.
    if (ValTy->isVectorTy()) {
      unsigned Num = ValTy->getVectorNumElements();
      if (CondTy)
        CondTy = CondTy->getScalarType();
      unsigned Cost = static_cast<T *>(this)->getCmpSelInstrCost(
          Opcode, ValTy->getScalarType(), CondTy, I);

      // Return the cost of multiple scalar invocation plus the cost of
      // inserting and extracting the values.
      return getScalarizationOverhead(ValTy, true, false) + Num * Cost;
    }

    // Unknown scalar opcode.
    return 1;
  }

  unsigned getVectorInstrCost(unsigned Opcode, Type *Val, unsigned Index) {
    std::pair<unsigned, MVT> LT =
        getTLI()->getTypeLegalizationCost(DL, Val->getScalarType());

    return LT.first;
  }

  unsigned getMemoryOpCost(unsigned Opcode, Type *Src, unsigned Alignment,
                       unsigned AddressSpace, const Instruction *I = nullptr) {
    assert(!Src->isVoidTy() && "Invalid type");
    std::pair<unsigned, MVT> LT = getTLI()->getTypeLegalizationCost(DL, Src);

    // Assuming that all loads of legal types cost 1.
    unsigned Cost = LT.first;

    if (Src->isVectorTy() &&
        Src->getPrimitiveSizeInBits() < LT.second.getSizeInBits()) {
      // This is a vector load that legalizes to a larger type than the vector
      // itself. Unless the corresponding extending load or truncating store is
      // legal, then this will scalarize.
      TargetLowering::LegalizeAction LA = TargetLowering::Expand;
      EVT MemVT = getTLI()->getValueType(DL, Src);
      if (Opcode == Instruction::Store)
        LA = getTLI()->getTruncStoreAction(LT.second, MemVT);
      else
        LA = getTLI()->getLoadExtAction(ISD::EXTLOAD, LT.second, MemVT);

      if (LA != TargetLowering::Legal && LA != TargetLowering::Custom) {
        // This is a vector load/store for some illegal type that is scalarized.
        // We must account for the cost of building or decomposing the vector.
        Cost += getScalarizationOverhead(Src, Opcode != Instruction::Store,
                                         Opcode == Instruction::Store);
      }
    }

    return Cost;
  }

  unsigned getInterleavedMemoryOpCost(unsigned Opcode, Type *VecTy,
                                      unsigned Factor,
                                      ArrayRef<unsigned> Indices,
                                      unsigned Alignment, unsigned AddressSpace,
                                      bool UseMaskForCond = false,
                                      bool UseMaskForGaps = false) {
    VectorType *VT = dyn_cast<VectorType>(VecTy);
    assert(VT && "Expect a vector type for interleaved memory op");

    unsigned NumElts = VT->getNumElements();
    assert(Factor > 1 && NumElts % Factor == 0 && "Invalid interleave factor");

    unsigned NumSubElts = NumElts / Factor;
    VectorType *SubVT = VectorType::get(VT->getElementType(), NumSubElts);

    // Firstly, the cost of load/store operation.
    unsigned Cost;
    if (UseMaskForCond || UseMaskForGaps)
      Cost = static_cast<T *>(this)->getMaskedMemoryOpCost(
          Opcode, VecTy, Alignment, AddressSpace);
    else
      Cost = static_cast<T *>(this)->getMemoryOpCost(Opcode, VecTy, Alignment,
                                                     AddressSpace);

    // Legalize the vector type, and get the legalized and unlegalized type
    // sizes.
    MVT VecTyLT = getTLI()->getTypeLegalizationCost(DL, VecTy).second;
    unsigned VecTySize =
        static_cast<T *>(this)->getDataLayout().getTypeStoreSize(VecTy);
    unsigned VecTyLTSize = VecTyLT.getStoreSize();

    // Return the ceiling of dividing A by B.
    auto ceil = [](unsigned A, unsigned B) { return (A + B - 1) / B; };

    // Scale the cost of the memory operation by the fraction of legalized
    // instructions that will actually be used. We shouldn't account for the
    // cost of dead instructions since they will be removed.
    //
    // E.g., An interleaved load of factor 8:
    //       %vec = load <16 x i64>, <16 x i64>* %ptr
    //       %v0 = shufflevector %vec, undef, <0, 8>
    //
    // If <16 x i64> is legalized to 8 v2i64 loads, only 2 of the loads will be
    // used (those corresponding to elements [0:1] and [8:9] of the unlegalized
    // type). The other loads are unused.
    //
    // We only scale the cost of loads since interleaved store groups aren't
    // allowed to have gaps.
    if (Opcode == Instruction::Load && VecTySize > VecTyLTSize) {
      // The number of loads of a legal type it will take to represent a load
      // of the unlegalized vector type.
      unsigned NumLegalInsts = ceil(VecTySize, VecTyLTSize);

      // The number of elements of the unlegalized type that correspond to a
      // single legal instruction.
      unsigned NumEltsPerLegalInst = ceil(NumElts, NumLegalInsts);

      // Determine which legal instructions will be used.
      BitVector UsedInsts(NumLegalInsts, false);
      for (unsigned Index : Indices)
        for (unsigned Elt = 0; Elt < NumSubElts; ++Elt)
          UsedInsts.set((Index + Elt * Factor) / NumEltsPerLegalInst);

      // Scale the cost of the load by the fraction of legal instructions that
      // will be used.
      Cost *= UsedInsts.count() / NumLegalInsts;
    }

    // Then plus the cost of interleave operation.
    if (Opcode == Instruction::Load) {
      // The interleave cost is similar to extract sub vectors' elements
      // from the wide vector, and insert them into sub vectors.
      //
      // E.g. An interleaved load of factor 2 (with one member of index 0):
      //      %vec = load <8 x i32>, <8 x i32>* %ptr
      //      %v0 = shuffle %vec, undef, <0, 2, 4, 6>         ; Index 0
      // The cost is estimated as extract elements at 0, 2, 4, 6 from the
      // <8 x i32> vector and insert them into a <4 x i32> vector.

      assert(Indices.size() <= Factor &&
             "Interleaved memory op has too many members");

      for (unsigned Index : Indices) {
        assert(Index < Factor && "Invalid index for interleaved memory op");

        // Extract elements from loaded vector for each sub vector.
        for (unsigned i = 0; i < NumSubElts; i++)
          Cost += static_cast<T *>(this)->getVectorInstrCost(
              Instruction::ExtractElement, VT, Index + i * Factor);
      }

      unsigned InsSubCost = 0;
      for (unsigned i = 0; i < NumSubElts; i++)
        InsSubCost += static_cast<T *>(this)->getVectorInstrCost(
            Instruction::InsertElement, SubVT, i);

      Cost += Indices.size() * InsSubCost;
    } else {
      // The interleave cost is extract all elements from sub vectors, and
      // insert them into the wide vector.
      //
      // E.g. An interleaved store of factor 2:
      //      %v0_v1 = shuffle %v0, %v1, <0, 4, 1, 5, 2, 6, 3, 7>
      //      store <8 x i32> %interleaved.vec, <8 x i32>* %ptr
      // The cost is estimated as extract all elements from both <4 x i32>
      // vectors and insert into the <8 x i32> vector.

      unsigned ExtSubCost = 0;
      for (unsigned i = 0; i < NumSubElts; i++)
        ExtSubCost += static_cast<T *>(this)->getVectorInstrCost(
            Instruction::ExtractElement, SubVT, i);
      Cost += ExtSubCost * Factor;

      for (unsigned i = 0; i < NumElts; i++)
        Cost += static_cast<T *>(this)
                    ->getVectorInstrCost(Instruction::InsertElement, VT, i);
    }

    if (!UseMaskForCond)
      return Cost;

    Type *I8Type = Type::getInt8Ty(VT->getContext());
    VectorType *MaskVT = VectorType::get(I8Type, NumElts);
    SubVT = VectorType::get(I8Type, NumSubElts);

    // The Mask shuffling cost is extract all the elements of the Mask
    // and insert each of them Factor times into the wide vector:
    //
    // E.g. an interleaved group with factor 3:
    //    %mask = icmp ult <8 x i32> %vec1, %vec2
    //    %interleaved.mask = shufflevector <8 x i1> %mask, <8 x i1> undef,
    //        <24 x i32> <0,0,0,1,1,1,2,2,2,3,3,3,4,4,4,5,5,5,6,6,6,7,7,7>
    // The cost is estimated as extract all mask elements from the <8xi1> mask
    // vector and insert them factor times into the <24xi1> shuffled mask
    // vector.
    for (unsigned i = 0; i < NumSubElts; i++)
      Cost += static_cast<T *>(this)->getVectorInstrCost(
          Instruction::ExtractElement, SubVT, i);

    for (unsigned i = 0; i < NumElts; i++)
      Cost += static_cast<T *>(this)->getVectorInstrCost(
          Instruction::InsertElement, MaskVT, i);

    // The Gaps mask is invariant and created outside the loop, therefore the
    // cost of creating it is not accounted for here. However if we have both
    // a MaskForGaps and some other mask that guards the execution of the
    // memory access, we need to account for the cost of And-ing the two masks
    // inside the loop.
    if (UseMaskForGaps)
      Cost += static_cast<T *>(this)->getArithmeticInstrCost(
          BinaryOperator::And, MaskVT); 

    return Cost;
  }

  /// Get intrinsic cost based on arguments.
  unsigned getIntrinsicInstrCost(Intrinsic::ID IID, Type *RetTy,
                                 ArrayRef<Value *> Args, FastMathFlags FMF,
                                 unsigned VF = 1) {
    unsigned RetVF = (RetTy->isVectorTy() ? RetTy->getVectorNumElements() : 1);
    assert((RetVF == 1 || VF == 1) && "VF > 1 and RetVF is a vector type");
    auto *ConcreteTTI = static_cast<T *>(this);

    switch (IID) {
    default: {
      // Assume that we need to scalarize this intrinsic.
      SmallVector<Type *, 4> Types;
      for (Value *Op : Args) {
        Type *OpTy = Op->getType();
        assert(VF == 1 || !OpTy->isVectorTy());
        Types.push_back(VF == 1 ? OpTy : VectorType::get(OpTy, VF));
      }

      if (VF > 1 && !RetTy->isVoidTy())
        RetTy = VectorType::get(RetTy, VF);

      // Compute the scalarization overhead based on Args for a vector
      // intrinsic. A vectorizer will pass a scalar RetTy and VF > 1, while
      // CostModel will pass a vector RetTy and VF is 1.
      unsigned ScalarizationCost = std::numeric_limits<unsigned>::max();
      if (RetVF > 1 || VF > 1) {
        ScalarizationCost = 0;
        if (!RetTy->isVoidTy())
          ScalarizationCost += getScalarizationOverhead(RetTy, true, false);
        ScalarizationCost += getOperandsScalarizationOverhead(Args, VF);
      }

      return ConcreteTTI->getIntrinsicInstrCost(IID, RetTy, Types, FMF,
                                                ScalarizationCost);
    }
    case Intrinsic::masked_scatter: {
      assert(VF == 1 && "Can't vectorize types here.");
      Value *Mask = Args[3];
      bool VarMask = !isa<Constant>(Mask);
      unsigned Alignment = cast<ConstantInt>(Args[2])->getZExtValue();
      return ConcreteTTI->getGatherScatterOpCost(
          Instruction::Store, Args[0]->getType(), Args[1], VarMask, Alignment);
    }
    case Intrinsic::masked_gather: {
      assert(VF == 1 && "Can't vectorize types here.");
      Value *Mask = Args[2];
      bool VarMask = !isa<Constant>(Mask);
      unsigned Alignment = cast<ConstantInt>(Args[1])->getZExtValue();
      return ConcreteTTI->getGatherScatterOpCost(Instruction::Load, RetTy,
                                                 Args[0], VarMask, Alignment);
    }
    case Intrinsic::experimental_vector_reduce_add:
    case Intrinsic::experimental_vector_reduce_mul:
    case Intrinsic::experimental_vector_reduce_and:
    case Intrinsic::experimental_vector_reduce_or:
    case Intrinsic::experimental_vector_reduce_xor:
    case Intrinsic::experimental_vector_reduce_fadd:
    case Intrinsic::experimental_vector_reduce_fmul:
    case Intrinsic::experimental_vector_reduce_smax:
    case Intrinsic::experimental_vector_reduce_smin:
    case Intrinsic::experimental_vector_reduce_fmax:
    case Intrinsic::experimental_vector_reduce_fmin:
    case Intrinsic::experimental_vector_reduce_umax:
    case Intrinsic::experimental_vector_reduce_umin:
      return getIntrinsicInstrCost(IID, RetTy, Args[0]->getType(), FMF);
    case Intrinsic::fshl:
    case Intrinsic::fshr: {
      Value *X = Args[0];
      Value *Y = Args[1];
      Value *Z = Args[2];
      TTI::OperandValueProperties OpPropsX, OpPropsY, OpPropsZ, OpPropsBW;
      TTI::OperandValueKind OpKindX = TTI::getOperandInfo(X, OpPropsX);
      TTI::OperandValueKind OpKindY = TTI::getOperandInfo(Y, OpPropsY);
      TTI::OperandValueKind OpKindZ = TTI::getOperandInfo(Z, OpPropsZ);
      TTI::OperandValueKind OpKindBW = TTI::OK_UniformConstantValue;
      OpPropsBW = isPowerOf2_32(RetTy->getScalarSizeInBits()) ? TTI::OP_PowerOf2
                                                              : TTI::OP_None;
      // fshl: (X << (Z % BW)) | (Y >> (BW - (Z % BW)))
      // fshr: (X << (BW - (Z % BW))) | (Y >> (Z % BW))
      unsigned Cost = 0;
      Cost += ConcreteTTI->getArithmeticInstrCost(BinaryOperator::Or, RetTy);
      Cost += ConcreteTTI->getArithmeticInstrCost(BinaryOperator::Sub, RetTy);
      Cost += ConcreteTTI->getArithmeticInstrCost(BinaryOperator::Shl, RetTy,
                                                  OpKindX, OpKindZ, OpPropsX);
      Cost += ConcreteTTI->getArithmeticInstrCost(BinaryOperator::LShr, RetTy,
                                                  OpKindY, OpKindZ, OpPropsY);
      // Non-constant shift amounts requires a modulo.
      if (OpKindZ != TTI::OK_UniformConstantValue &&
          OpKindZ != TTI::OK_NonUniformConstantValue)
        Cost += ConcreteTTI->getArithmeticInstrCost(BinaryOperator::URem, RetTy,
                                                    OpKindZ, OpKindBW, OpPropsZ,
                                                    OpPropsBW);
      // For non-rotates (X != Y) we must add shift-by-zero handling costs.
      if (X != Y) {
        Type *CondTy = Type::getInt1Ty(RetTy->getContext());
        if (RetVF > 1)
          CondTy = VectorType::get(CondTy, RetVF);
        Cost += ConcreteTTI->getCmpSelInstrCost(BinaryOperator::ICmp, RetTy,
                                                CondTy, nullptr);
        Cost += ConcreteTTI->getCmpSelInstrCost(BinaryOperator::Select, RetTy,
                                                CondTy, nullptr);
      }
      return Cost;
    }
    }
  }

  /// Get intrinsic cost based on argument types.
  /// If ScalarizationCostPassed is std::numeric_limits<unsigned>::max(), the
  /// cost of scalarizing the arguments and the return value will be computed
  /// based on types.
  unsigned getIntrinsicInstrCost(
      Intrinsic::ID IID, Type *RetTy, ArrayRef<Type *> Tys, FastMathFlags FMF,
      unsigned ScalarizationCostPassed = std::numeric_limits<unsigned>::max()) {
    SmallVector<unsigned, 2> ISDs;
    unsigned SingleCallCost = 10; // Library call cost. Make it expensive.
    switch (IID) {
    default: {
      // Assume that we need to scalarize this intrinsic.
      unsigned ScalarizationCost = ScalarizationCostPassed;
      unsigned ScalarCalls = 1;
      Type *ScalarRetTy = RetTy;
      if (RetTy->isVectorTy()) {
        if (ScalarizationCostPassed == std::numeric_limits<unsigned>::max())
          ScalarizationCost = getScalarizationOverhead(RetTy, true, false);
        ScalarCalls = std::max(ScalarCalls, RetTy->getVectorNumElements());
        ScalarRetTy = RetTy->getScalarType();
      }
      SmallVector<Type *, 4> ScalarTys;
      for (unsigned i = 0, ie = Tys.size(); i != ie; ++i) {
        Type *Ty = Tys[i];
        if (Ty->isVectorTy()) {
          if (ScalarizationCostPassed == std::numeric_limits<unsigned>::max())
            ScalarizationCost += getScalarizationOverhead(Ty, false, true);
          ScalarCalls = std::max(ScalarCalls, Ty->getVectorNumElements());
          Ty = Ty->getScalarType();
        }
        ScalarTys.push_back(Ty);
      }
      if (ScalarCalls == 1)
        return 1; // Return cost of a scalar intrinsic. Assume it to be cheap.

      unsigned ScalarCost = static_cast<T *>(this)->getIntrinsicInstrCost(
          IID, ScalarRetTy, ScalarTys, FMF);

      return ScalarCalls * ScalarCost + ScalarizationCost;
    }
    // Look for intrinsics that can be lowered directly or turned into a scalar
    // intrinsic call.
    case Intrinsic::sqrt:
      ISDs.push_back(ISD::FSQRT);
      break;
    case Intrinsic::sin:
      ISDs.push_back(ISD::FSIN);
      break;
    case Intrinsic::cos:
      ISDs.push_back(ISD::FCOS);
      break;
    case Intrinsic::exp:
      ISDs.push_back(ISD::FEXP);
      break;
    case Intrinsic::exp2:
      ISDs.push_back(ISD::FEXP2);
      break;
    case Intrinsic::log:
      ISDs.push_back(ISD::FLOG);
      break;
    case Intrinsic::log10:
      ISDs.push_back(ISD::FLOG10);
      break;
    case Intrinsic::log2:
      ISDs.push_back(ISD::FLOG2);
      break;
    case Intrinsic::fabs:
      ISDs.push_back(ISD::FABS);
      break;
    case Intrinsic::canonicalize:
      ISDs.push_back(ISD::FCANONICALIZE);
      break;
    case Intrinsic::minnum:
      ISDs.push_back(ISD::FMINNUM);
      if (FMF.noNaNs())
        ISDs.push_back(ISD::FMINIMUM);
      break;
    case Intrinsic::maxnum:
      ISDs.push_back(ISD::FMAXNUM);
      if (FMF.noNaNs())
        ISDs.push_back(ISD::FMAXIMUM);
      break;
    case Intrinsic::copysign:
      ISDs.push_back(ISD::FCOPYSIGN);
      break;
    case Intrinsic::floor:
      ISDs.push_back(ISD::FFLOOR);
      break;
    case Intrinsic::ceil:
      ISDs.push_back(ISD::FCEIL);
      break;
    case Intrinsic::trunc:
      ISDs.push_back(ISD::FTRUNC);
      break;
    case Intrinsic::nearbyint:
      ISDs.push_back(ISD::FNEARBYINT);
      break;
    case Intrinsic::rint:
      ISDs.push_back(ISD::FRINT);
      break;
    case Intrinsic::round:
      ISDs.push_back(ISD::FROUND);
      break;
    case Intrinsic::pow:
      ISDs.push_back(ISD::FPOW);
      break;
    case Intrinsic::fma:
      ISDs.push_back(ISD::FMA);
      break;
    case Intrinsic::fmuladd:
      ISDs.push_back(ISD::FMA);
      break;
    // FIXME: We should return 0 whenever getIntrinsicCost == TCC_Free.
    case Intrinsic::lifetime_start:
    case Intrinsic::lifetime_end:
    case Intrinsic::sideeffect:
      return 0;
    case Intrinsic::masked_store:
      return static_cast<T *>(this)
          ->getMaskedMemoryOpCost(Instruction::Store, Tys[0], 0, 0);
    case Intrinsic::masked_load:
      return static_cast<T *>(this)
          ->getMaskedMemoryOpCost(Instruction::Load, RetTy, 0, 0);
    case Intrinsic::experimental_vector_reduce_add:
      return static_cast<T *>(this)->getArithmeticReductionCost(
          Instruction::Add, Tys[0], /*IsPairwiseForm=*/false);
    case Intrinsic::experimental_vector_reduce_mul:
      return static_cast<T *>(this)->getArithmeticReductionCost(
          Instruction::Mul, Tys[0], /*IsPairwiseForm=*/false);
    case Intrinsic::experimental_vector_reduce_and:
      return static_cast<T *>(this)->getArithmeticReductionCost(
          Instruction::And, Tys[0], /*IsPairwiseForm=*/false);
    case Intrinsic::experimental_vector_reduce_or:
      return static_cast<T *>(this)->getArithmeticReductionCost(
          Instruction::Or, Tys[0], /*IsPairwiseForm=*/false);
    case Intrinsic::experimental_vector_reduce_xor:
      return static_cast<T *>(this)->getArithmeticReductionCost(
          Instruction::Xor, Tys[0], /*IsPairwiseForm=*/false);
    case Intrinsic::experimental_vector_reduce_fadd:
      return static_cast<T *>(this)->getArithmeticReductionCost(
          Instruction::FAdd, Tys[0], /*IsPairwiseForm=*/false);
    case Intrinsic::experimental_vector_reduce_fmul:
      return static_cast<T *>(this)->getArithmeticReductionCost(
          Instruction::FMul, Tys[0], /*IsPairwiseForm=*/false);
    case Intrinsic::experimental_vector_reduce_smax:
    case Intrinsic::experimental_vector_reduce_smin:
    case Intrinsic::experimental_vector_reduce_fmax:
    case Intrinsic::experimental_vector_reduce_fmin:
      return static_cast<T *>(this)->getMinMaxReductionCost(
          Tys[0], CmpInst::makeCmpResultType(Tys[0]), /*IsPairwiseForm=*/false,
          /*IsSigned=*/true);
    case Intrinsic::experimental_vector_reduce_umax:
    case Intrinsic::experimental_vector_reduce_umin:
      return static_cast<T *>(this)->getMinMaxReductionCost(
          Tys[0], CmpInst::makeCmpResultType(Tys[0]), /*IsPairwiseForm=*/false,
          /*IsSigned=*/false);
    case Intrinsic::ctpop:
      ISDs.push_back(ISD::CTPOP);
      // In case of legalization use TCC_Expensive. This is cheaper than a
      // library call but still not a cheap instruction.
      SingleCallCost = TargetTransformInfo::TCC_Expensive;
      break;
    // FIXME: ctlz, cttz, ...
    }

    const TargetLoweringBase *TLI = getTLI();
    std::pair<unsigned, MVT> LT = TLI->getTypeLegalizationCost(DL, RetTy);

    SmallVector<unsigned, 2> LegalCost;
    SmallVector<unsigned, 2> CustomCost;
    for (unsigned ISD : ISDs) {
      if (TLI->isOperationLegalOrPromote(ISD, LT.second)) {
        if (IID == Intrinsic::fabs && LT.second.isFloatingPoint() &&
            TLI->isFAbsFree(LT.second)) {
          return 0;
        }

        // The operation is legal. Assume it costs 1.
        // If the type is split to multiple registers, assume that there is some
        // overhead to this.
        // TODO: Once we have extract/insert subvector cost we need to use them.
        if (LT.first > 1)
          LegalCost.push_back(LT.first * 2);
        else
          LegalCost.push_back(LT.first * 1);
      } else if (!TLI->isOperationExpand(ISD, LT.second)) {
        // If the operation is custom lowered then assume
        // that the code is twice as expensive.
        CustomCost.push_back(LT.first * 2);
      }
    }

    auto MinLegalCostI = std::min_element(LegalCost.begin(), LegalCost.end());
    if (MinLegalCostI != LegalCost.end())
      return *MinLegalCostI;

    auto MinCustomCostI = std::min_element(CustomCost.begin(), CustomCost.end());
    if (MinCustomCostI != CustomCost.end())
      return *MinCustomCostI;

    // If we can't lower fmuladd into an FMA estimate the cost as a floating
    // point mul followed by an add.
    if (IID == Intrinsic::fmuladd)
      return static_cast<T *>(this)
                 ->getArithmeticInstrCost(BinaryOperator::FMul, RetTy) +
             static_cast<T *>(this)
                 ->getArithmeticInstrCost(BinaryOperator::FAdd, RetTy);

    // Else, assume that we need to scalarize this intrinsic. For math builtins
    // this will emit a costly libcall, adding call overhead and spills. Make it
    // very expensive.
    if (RetTy->isVectorTy()) {
      unsigned ScalarizationCost =
          ((ScalarizationCostPassed != std::numeric_limits<unsigned>::max())
               ? ScalarizationCostPassed
               : getScalarizationOverhead(RetTy, true, false));
      unsigned ScalarCalls = RetTy->getVectorNumElements();
      SmallVector<Type *, 4> ScalarTys;
      for (unsigned i = 0, ie = Tys.size(); i != ie; ++i) {
        Type *Ty = Tys[i];
        if (Ty->isVectorTy())
          Ty = Ty->getScalarType();
        ScalarTys.push_back(Ty);
      }
      unsigned ScalarCost = static_cast<T *>(this)->getIntrinsicInstrCost(
          IID, RetTy->getScalarType(), ScalarTys, FMF);
      for (unsigned i = 0, ie = Tys.size(); i != ie; ++i) {
        if (Tys[i]->isVectorTy()) {
          if (ScalarizationCostPassed == std::numeric_limits<unsigned>::max())
            ScalarizationCost += getScalarizationOverhead(Tys[i], false, true);
          ScalarCalls = std::max(ScalarCalls, Tys[i]->getVectorNumElements());
        }
      }

      return ScalarCalls * ScalarCost + ScalarizationCost;
    }

    // This is going to be turned into a library call, make it expensive.
    return SingleCallCost;
  }

  /// Compute a cost of the given call instruction.
  ///
  /// Compute the cost of calling function F with return type RetTy and
  /// argument types Tys. F might be nullptr, in this case the cost of an
  /// arbitrary call with the specified signature will be returned.
  /// This is used, for instance,  when we estimate call of a vector
  /// counterpart of the given function.
  /// \param F Called function, might be nullptr.
  /// \param RetTy Return value types.
  /// \param Tys Argument types.
  /// \returns The cost of Call instruction.
  unsigned getCallInstrCost(Function *F, Type *RetTy, ArrayRef<Type *> Tys) {
    return 10;
  }

  unsigned getNumberOfParts(Type *Tp) {
    std::pair<unsigned, MVT> LT = getTLI()->getTypeLegalizationCost(DL, Tp);
    return LT.first;
  }

  unsigned getAddressComputationCost(Type *Ty, ScalarEvolution *,
                                     const SCEV *) {
    return 0;
  }

  /// Try to calculate arithmetic and shuffle op costs for reduction operations.
  /// We're assuming that reduction operation are performing the following way:
  /// 1. Non-pairwise reduction
  /// %val1 = shufflevector<n x t> %val, <n x t> %undef,
  /// <n x i32> <i32 n/2, i32 n/2 + 1, ..., i32 n, i32 undef, ..., i32 undef>
  ///            \----------------v-------------/  \----------v------------/
  ///                            n/2 elements               n/2 elements
  /// %red1 = op <n x t> %val, <n x t> val1
  /// After this operation we have a vector %red1 where only the first n/2
  /// elements are meaningful, the second n/2 elements are undefined and can be
  /// dropped. All other operations are actually working with the vector of
  /// length n/2, not n, though the real vector length is still n.
  /// %val2 = shufflevector<n x t> %red1, <n x t> %undef,
  /// <n x i32> <i32 n/4, i32 n/4 + 1, ..., i32 n/2, i32 undef, ..., i32 undef>
  ///            \----------------v-------------/  \----------v------------/
  ///                            n/4 elements               3*n/4 elements
  /// %red2 = op <n x t> %red1, <n x t> val2  - working with the vector of
  /// length n/2, the resulting vector has length n/4 etc.
  /// 2. Pairwise reduction:
  /// Everything is the same except for an additional shuffle operation which
  /// is used to produce operands for pairwise kind of reductions.
  /// %val1 = shufflevector<n x t> %val, <n x t> %undef,
  /// <n x i32> <i32 0, i32 2, ..., i32 n-2, i32 undef, ..., i32 undef>
  ///            \-------------v----------/  \----------v------------/
  ///                   n/2 elements               n/2 elements
  /// %val2 = shufflevector<n x t> %val, <n x t> %undef,
  /// <n x i32> <i32 1, i32 3, ..., i32 n-1, i32 undef, ..., i32 undef>
  ///            \-------------v----------/  \----------v------------/
  ///                   n/2 elements               n/2 elements
  /// %red1 = op <n x t> %val1, <n x t> val2
  /// Again, the operation is performed on <n x t> vector, but the resulting
  /// vector %red1 is <n/2 x t> vector.
  ///
  /// The cost model should take into account that the actual length of the
  /// vector is reduced on each iteration.
  unsigned getArithmeticReductionCost(unsigned Opcode, Type *Ty,
                                      bool IsPairwise) {
    assert(Ty->isVectorTy() && "Expect a vector type");
    Type *ScalarTy = Ty->getVectorElementType();
    unsigned NumVecElts = Ty->getVectorNumElements();
    unsigned NumReduxLevels = Log2_32(NumVecElts);
    unsigned ArithCost = 0;
    unsigned ShuffleCost = 0;
    auto *ConcreteTTI = static_cast<T *>(this);
    std::pair<unsigned, MVT> LT =
        ConcreteTTI->getTLI()->getTypeLegalizationCost(DL, Ty);
    unsigned LongVectorCount = 0;
    unsigned MVTLen =
        LT.second.isVector() ? LT.second.getVectorNumElements() : 1;
    while (NumVecElts > MVTLen) {
      NumVecElts /= 2;
      Type *SubTy = VectorType::get(ScalarTy, NumVecElts);
      // Assume the pairwise shuffles add a cost.
      ShuffleCost += (IsPairwise + 1) *
                     ConcreteTTI->getShuffleCost(TTI::SK_ExtractSubvector, Ty,
                                                 NumVecElts, SubTy);
      ArithCost += ConcreteTTI->getArithmeticInstrCost(Opcode, SubTy);
      Ty = SubTy;
      ++LongVectorCount;
    }

    NumReduxLevels -= LongVectorCount;

    // The minimal length of the vector is limited by the real length of vector
    // operations performed on the current platform. That's why several final
    // reduction operations are performed on the vectors with the same
    // architecture-dependent length.

    // Non pairwise reductions need one shuffle per reduction level. Pairwise
    // reductions need two shuffles on every level, but the last one. On that
    // level one of the shuffles is <0, u, u, ...> which is identity.
    unsigned NumShuffles = NumReduxLevels;
    if (IsPairwise && NumReduxLevels >= 1)
      NumShuffles += NumReduxLevels - 1;
    ShuffleCost += NumShuffles *
                   ConcreteTTI->getShuffleCost(TTI::SK_PermuteSingleSrc, Ty,
                                               0, Ty);
    ArithCost += NumReduxLevels *
                 ConcreteTTI->getArithmeticInstrCost(Opcode, Ty);
    return ShuffleCost + ArithCost +
           ConcreteTTI->getVectorInstrCost(Instruction::ExtractElement, Ty, 0);
  }

  /// Try to calculate op costs for min/max reduction operations.
  /// \param CondTy Conditional type for the Select instruction.
  unsigned getMinMaxReductionCost(Type *Ty, Type *CondTy, bool IsPairwise,
                                  bool) {
    assert(Ty->isVectorTy() && "Expect a vector type");
    Type *ScalarTy = Ty->getVectorElementType();
    Type *ScalarCondTy = CondTy->getVectorElementType();
    unsigned NumVecElts = Ty->getVectorNumElements();
    unsigned NumReduxLevels = Log2_32(NumVecElts);
    unsigned CmpOpcode;
    if (Ty->isFPOrFPVectorTy()) {
      CmpOpcode = Instruction::FCmp;
    } else {
      assert(Ty->isIntOrIntVectorTy() &&
             "expecting floating point or integer type for min/max reduction");
      CmpOpcode = Instruction::ICmp;
    }
    unsigned MinMaxCost = 0;
    unsigned ShuffleCost = 0;
    auto *ConcreteTTI = static_cast<T *>(this);
    std::pair<unsigned, MVT> LT =
        ConcreteTTI->getTLI()->getTypeLegalizationCost(DL, Ty);
    unsigned LongVectorCount = 0;
    unsigned MVTLen =
        LT.second.isVector() ? LT.second.getVectorNumElements() : 1;
    while (NumVecElts > MVTLen) {
      NumVecElts /= 2;
      Type *SubTy = VectorType::get(ScalarTy, NumVecElts);
      CondTy = VectorType::get(ScalarCondTy, NumVecElts);

      // Assume the pairwise shuffles add a cost.
      ShuffleCost += (IsPairwise + 1) *
                     ConcreteTTI->getShuffleCost(TTI::SK_ExtractSubvector, Ty,
                                                 NumVecElts, SubTy);
      MinMaxCost +=
          ConcreteTTI->getCmpSelInstrCost(CmpOpcode, SubTy, CondTy, nullptr) +
          ConcreteTTI->getCmpSelInstrCost(Instruction::Select, SubTy, CondTy,
                                          nullptr);
      Ty = SubTy;
      ++LongVectorCount;
    }

    NumReduxLevels -= LongVectorCount;

    // The minimal length of the vector is limited by the real length of vector
    // operations performed on the current platform. That's why several final
    // reduction opertions are perfomed on the vectors with the same
    // architecture-dependent length.

    // Non pairwise reductions need one shuffle per reduction level. Pairwise
    // reductions need two shuffles on every level, but the last one. On that
    // level one of the shuffles is <0, u, u, ...> which is identity.
    unsigned NumShuffles = NumReduxLevels;
    if (IsPairwise && NumReduxLevels >= 1)
      NumShuffles += NumReduxLevels - 1;
    ShuffleCost += NumShuffles *
                   ConcreteTTI->getShuffleCost(TTI::SK_PermuteSingleSrc, Ty,
                                               0, Ty);
    MinMaxCost +=
        NumReduxLevels *
        (ConcreteTTI->getCmpSelInstrCost(CmpOpcode, Ty, CondTy, nullptr) +
         ConcreteTTI->getCmpSelInstrCost(Instruction::Select, Ty, CondTy,
                                         nullptr));
    // The last min/max should be in vector registers and we counted it above.
    // So just need a single extractelement.
    return ShuffleCost + MinMaxCost +
           ConcreteTTI->getVectorInstrCost(Instruction::ExtractElement, Ty, 0);
  }

  unsigned getVectorSplitCost() { return 1; }

  /// @}
};

/// Concrete BasicTTIImpl that can be used if no further customization
/// is needed.
class BasicTTIImpl : public BasicTTIImplBase<BasicTTIImpl> {
  using BaseT = BasicTTIImplBase<BasicTTIImpl>;

  friend class BasicTTIImplBase<BasicTTIImpl>;

  const TargetSubtargetInfo *ST;
  const TargetLoweringBase *TLI;

  const TargetSubtargetInfo *getST() const { return ST; }
  const TargetLoweringBase *getTLI() const { return TLI; }

public:
  explicit BasicTTIImpl(const TargetMachine *TM, const Function &F);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_BASICTTIIMPL_H
