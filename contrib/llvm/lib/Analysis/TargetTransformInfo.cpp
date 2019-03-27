//===- llvm/Analysis/TargetTransformInfo.cpp ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/TargetTransformInfoImpl.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include <utility>

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "tti"

static cl::opt<bool> EnableReduxCost("costmodel-reduxcost", cl::init(false),
                                     cl::Hidden,
                                     cl::desc("Recognize reduction patterns."));

namespace {
/// No-op implementation of the TTI interface using the utility base
/// classes.
///
/// This is used when no target specific information is available.
struct NoTTIImpl : TargetTransformInfoImplCRTPBase<NoTTIImpl> {
  explicit NoTTIImpl(const DataLayout &DL)
      : TargetTransformInfoImplCRTPBase<NoTTIImpl>(DL) {}
};
}

TargetTransformInfo::TargetTransformInfo(const DataLayout &DL)
    : TTIImpl(new Model<NoTTIImpl>(NoTTIImpl(DL))) {}

TargetTransformInfo::~TargetTransformInfo() {}

TargetTransformInfo::TargetTransformInfo(TargetTransformInfo &&Arg)
    : TTIImpl(std::move(Arg.TTIImpl)) {}

TargetTransformInfo &TargetTransformInfo::operator=(TargetTransformInfo &&RHS) {
  TTIImpl = std::move(RHS.TTIImpl);
  return *this;
}

int TargetTransformInfo::getOperationCost(unsigned Opcode, Type *Ty,
                                          Type *OpTy) const {
  int Cost = TTIImpl->getOperationCost(Opcode, Ty, OpTy);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

int TargetTransformInfo::getCallCost(FunctionType *FTy, int NumArgs) const {
  int Cost = TTIImpl->getCallCost(FTy, NumArgs);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

int TargetTransformInfo::getCallCost(const Function *F,
                                     ArrayRef<const Value *> Arguments) const {
  int Cost = TTIImpl->getCallCost(F, Arguments);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

unsigned TargetTransformInfo::getInliningThresholdMultiplier() const {
  return TTIImpl->getInliningThresholdMultiplier();
}

int TargetTransformInfo::getGEPCost(Type *PointeeType, const Value *Ptr,
                                    ArrayRef<const Value *> Operands) const {
  return TTIImpl->getGEPCost(PointeeType, Ptr, Operands);
}

int TargetTransformInfo::getExtCost(const Instruction *I,
                                    const Value *Src) const {
  return TTIImpl->getExtCost(I, Src);
}

int TargetTransformInfo::getIntrinsicCost(
    Intrinsic::ID IID, Type *RetTy, ArrayRef<const Value *> Arguments) const {
  int Cost = TTIImpl->getIntrinsicCost(IID, RetTy, Arguments);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

unsigned
TargetTransformInfo::getEstimatedNumberOfCaseClusters(const SwitchInst &SI,
                                                      unsigned &JTSize) const {
  return TTIImpl->getEstimatedNumberOfCaseClusters(SI, JTSize);
}

int TargetTransformInfo::getUserCost(const User *U,
    ArrayRef<const Value *> Operands) const {
  int Cost = TTIImpl->getUserCost(U, Operands);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

bool TargetTransformInfo::hasBranchDivergence() const {
  return TTIImpl->hasBranchDivergence();
}

bool TargetTransformInfo::isSourceOfDivergence(const Value *V) const {
  return TTIImpl->isSourceOfDivergence(V);
}

bool llvm::TargetTransformInfo::isAlwaysUniform(const Value *V) const {
  return TTIImpl->isAlwaysUniform(V);
}

unsigned TargetTransformInfo::getFlatAddressSpace() const {
  return TTIImpl->getFlatAddressSpace();
}

bool TargetTransformInfo::isLoweredToCall(const Function *F) const {
  return TTIImpl->isLoweredToCall(F);
}

void TargetTransformInfo::getUnrollingPreferences(
    Loop *L, ScalarEvolution &SE, UnrollingPreferences &UP) const {
  return TTIImpl->getUnrollingPreferences(L, SE, UP);
}

bool TargetTransformInfo::isLegalAddImmediate(int64_t Imm) const {
  return TTIImpl->isLegalAddImmediate(Imm);
}

bool TargetTransformInfo::isLegalICmpImmediate(int64_t Imm) const {
  return TTIImpl->isLegalICmpImmediate(Imm);
}

bool TargetTransformInfo::isLegalAddressingMode(Type *Ty, GlobalValue *BaseGV,
                                                int64_t BaseOffset,
                                                bool HasBaseReg,
                                                int64_t Scale,
                                                unsigned AddrSpace,
                                                Instruction *I) const {
  return TTIImpl->isLegalAddressingMode(Ty, BaseGV, BaseOffset, HasBaseReg,
                                        Scale, AddrSpace, I);
}

bool TargetTransformInfo::isLSRCostLess(LSRCost &C1, LSRCost &C2) const {
  return TTIImpl->isLSRCostLess(C1, C2);
}

bool TargetTransformInfo::canMacroFuseCmp() const {
  return TTIImpl->canMacroFuseCmp();
}

bool TargetTransformInfo::shouldFavorPostInc() const {
  return TTIImpl->shouldFavorPostInc();
}

bool TargetTransformInfo::isLegalMaskedStore(Type *DataType) const {
  return TTIImpl->isLegalMaskedStore(DataType);
}

bool TargetTransformInfo::isLegalMaskedLoad(Type *DataType) const {
  return TTIImpl->isLegalMaskedLoad(DataType);
}

bool TargetTransformInfo::isLegalMaskedGather(Type *DataType) const {
  return TTIImpl->isLegalMaskedGather(DataType);
}

bool TargetTransformInfo::isLegalMaskedScatter(Type *DataType) const {
  return TTIImpl->isLegalMaskedScatter(DataType);
}

bool TargetTransformInfo::hasDivRemOp(Type *DataType, bool IsSigned) const {
  return TTIImpl->hasDivRemOp(DataType, IsSigned);
}

bool TargetTransformInfo::hasVolatileVariant(Instruction *I,
                                             unsigned AddrSpace) const {
  return TTIImpl->hasVolatileVariant(I, AddrSpace);
}

bool TargetTransformInfo::prefersVectorizedAddressing() const {
  return TTIImpl->prefersVectorizedAddressing();
}

int TargetTransformInfo::getScalingFactorCost(Type *Ty, GlobalValue *BaseGV,
                                              int64_t BaseOffset,
                                              bool HasBaseReg,
                                              int64_t Scale,
                                              unsigned AddrSpace) const {
  int Cost = TTIImpl->getScalingFactorCost(Ty, BaseGV, BaseOffset, HasBaseReg,
                                           Scale, AddrSpace);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

bool TargetTransformInfo::LSRWithInstrQueries() const {
  return TTIImpl->LSRWithInstrQueries();
}

bool TargetTransformInfo::isTruncateFree(Type *Ty1, Type *Ty2) const {
  return TTIImpl->isTruncateFree(Ty1, Ty2);
}

bool TargetTransformInfo::isProfitableToHoist(Instruction *I) const {
  return TTIImpl->isProfitableToHoist(I);
}

bool TargetTransformInfo::useAA() const { return TTIImpl->useAA(); }

bool TargetTransformInfo::isTypeLegal(Type *Ty) const {
  return TTIImpl->isTypeLegal(Ty);
}

unsigned TargetTransformInfo::getJumpBufAlignment() const {
  return TTIImpl->getJumpBufAlignment();
}

unsigned TargetTransformInfo::getJumpBufSize() const {
  return TTIImpl->getJumpBufSize();
}

bool TargetTransformInfo::shouldBuildLookupTables() const {
  return TTIImpl->shouldBuildLookupTables();
}
bool TargetTransformInfo::shouldBuildLookupTablesForConstant(Constant *C) const {
  return TTIImpl->shouldBuildLookupTablesForConstant(C);
}

bool TargetTransformInfo::useColdCCForColdCall(Function &F) const {
  return TTIImpl->useColdCCForColdCall(F);
}

unsigned TargetTransformInfo::
getScalarizationOverhead(Type *Ty, bool Insert, bool Extract) const {
  return TTIImpl->getScalarizationOverhead(Ty, Insert, Extract);
}

unsigned TargetTransformInfo::
getOperandsScalarizationOverhead(ArrayRef<const Value *> Args,
                                 unsigned VF) const {
  return TTIImpl->getOperandsScalarizationOverhead(Args, VF);
}

bool TargetTransformInfo::supportsEfficientVectorElementLoadStore() const {
  return TTIImpl->supportsEfficientVectorElementLoadStore();
}

bool TargetTransformInfo::enableAggressiveInterleaving(bool LoopHasReductions) const {
  return TTIImpl->enableAggressiveInterleaving(LoopHasReductions);
}

const TargetTransformInfo::MemCmpExpansionOptions *
TargetTransformInfo::enableMemCmpExpansion(bool IsZeroCmp) const {
  return TTIImpl->enableMemCmpExpansion(IsZeroCmp);
}

bool TargetTransformInfo::enableInterleavedAccessVectorization() const {
  return TTIImpl->enableInterleavedAccessVectorization();
}

bool TargetTransformInfo::enableMaskedInterleavedAccessVectorization() const {
  return TTIImpl->enableMaskedInterleavedAccessVectorization();
}

bool TargetTransformInfo::isFPVectorizationPotentiallyUnsafe() const {
  return TTIImpl->isFPVectorizationPotentiallyUnsafe();
}

bool TargetTransformInfo::allowsMisalignedMemoryAccesses(LLVMContext &Context,
                                                         unsigned BitWidth,
                                                         unsigned AddressSpace,
                                                         unsigned Alignment,
                                                         bool *Fast) const {
  return TTIImpl->allowsMisalignedMemoryAccesses(Context, BitWidth, AddressSpace,
                                                 Alignment, Fast);
}

TargetTransformInfo::PopcntSupportKind
TargetTransformInfo::getPopcntSupport(unsigned IntTyWidthInBit) const {
  return TTIImpl->getPopcntSupport(IntTyWidthInBit);
}

bool TargetTransformInfo::haveFastSqrt(Type *Ty) const {
  return TTIImpl->haveFastSqrt(Ty);
}

bool TargetTransformInfo::isFCmpOrdCheaperThanFCmpZero(Type *Ty) const {
  return TTIImpl->isFCmpOrdCheaperThanFCmpZero(Ty);
}

int TargetTransformInfo::getFPOpCost(Type *Ty) const {
  int Cost = TTIImpl->getFPOpCost(Ty);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

int TargetTransformInfo::getIntImmCodeSizeCost(unsigned Opcode, unsigned Idx,
                                               const APInt &Imm,
                                               Type *Ty) const {
  int Cost = TTIImpl->getIntImmCodeSizeCost(Opcode, Idx, Imm, Ty);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

int TargetTransformInfo::getIntImmCost(const APInt &Imm, Type *Ty) const {
  int Cost = TTIImpl->getIntImmCost(Imm, Ty);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

int TargetTransformInfo::getIntImmCost(unsigned Opcode, unsigned Idx,
                                       const APInt &Imm, Type *Ty) const {
  int Cost = TTIImpl->getIntImmCost(Opcode, Idx, Imm, Ty);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

int TargetTransformInfo::getIntImmCost(Intrinsic::ID IID, unsigned Idx,
                                       const APInt &Imm, Type *Ty) const {
  int Cost = TTIImpl->getIntImmCost(IID, Idx, Imm, Ty);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

unsigned TargetTransformInfo::getNumberOfRegisters(bool Vector) const {
  return TTIImpl->getNumberOfRegisters(Vector);
}

unsigned TargetTransformInfo::getRegisterBitWidth(bool Vector) const {
  return TTIImpl->getRegisterBitWidth(Vector);
}

unsigned TargetTransformInfo::getMinVectorRegisterBitWidth() const {
  return TTIImpl->getMinVectorRegisterBitWidth();
}

bool TargetTransformInfo::shouldMaximizeVectorBandwidth(bool OptSize) const {
  return TTIImpl->shouldMaximizeVectorBandwidth(OptSize);
}

unsigned TargetTransformInfo::getMinimumVF(unsigned ElemWidth) const {
  return TTIImpl->getMinimumVF(ElemWidth);
}

bool TargetTransformInfo::shouldConsiderAddressTypePromotion(
    const Instruction &I, bool &AllowPromotionWithoutCommonHeader) const {
  return TTIImpl->shouldConsiderAddressTypePromotion(
      I, AllowPromotionWithoutCommonHeader);
}

unsigned TargetTransformInfo::getCacheLineSize() const {
  return TTIImpl->getCacheLineSize();
}

llvm::Optional<unsigned> TargetTransformInfo::getCacheSize(CacheLevel Level)
  const {
  return TTIImpl->getCacheSize(Level);
}

llvm::Optional<unsigned> TargetTransformInfo::getCacheAssociativity(
  CacheLevel Level) const {
  return TTIImpl->getCacheAssociativity(Level);
}

unsigned TargetTransformInfo::getPrefetchDistance() const {
  return TTIImpl->getPrefetchDistance();
}

unsigned TargetTransformInfo::getMinPrefetchStride() const {
  return TTIImpl->getMinPrefetchStride();
}

unsigned TargetTransformInfo::getMaxPrefetchIterationsAhead() const {
  return TTIImpl->getMaxPrefetchIterationsAhead();
}

unsigned TargetTransformInfo::getMaxInterleaveFactor(unsigned VF) const {
  return TTIImpl->getMaxInterleaveFactor(VF);
}

TargetTransformInfo::OperandValueKind
TargetTransformInfo::getOperandInfo(Value *V, OperandValueProperties &OpProps) {
  OperandValueKind OpInfo = OK_AnyValue;
  OpProps = OP_None;

  if (auto *CI = dyn_cast<ConstantInt>(V)) {
    if (CI->getValue().isPowerOf2())
      OpProps = OP_PowerOf2;
    return OK_UniformConstantValue;
  }

  // A broadcast shuffle creates a uniform value.
  // TODO: Add support for non-zero index broadcasts.
  // TODO: Add support for different source vector width.
  if (auto *ShuffleInst = dyn_cast<ShuffleVectorInst>(V))
    if (ShuffleInst->isZeroEltSplat())
      OpInfo = OK_UniformValue;

  const Value *Splat = getSplatValue(V);

  // Check for a splat of a constant or for a non uniform vector of constants
  // and check if the constant(s) are all powers of two.
  if (isa<ConstantVector>(V) || isa<ConstantDataVector>(V)) {
    OpInfo = OK_NonUniformConstantValue;
    if (Splat) {
      OpInfo = OK_UniformConstantValue;
      if (auto *CI = dyn_cast<ConstantInt>(Splat))
        if (CI->getValue().isPowerOf2())
          OpProps = OP_PowerOf2;
    } else if (auto *CDS = dyn_cast<ConstantDataSequential>(V)) {
      OpProps = OP_PowerOf2;
      for (unsigned I = 0, E = CDS->getNumElements(); I != E; ++I) {
        if (auto *CI = dyn_cast<ConstantInt>(CDS->getElementAsConstant(I)))
          if (CI->getValue().isPowerOf2())
            continue;
        OpProps = OP_None;
        break;
      }
    }
  }

  // Check for a splat of a uniform value. This is not loop aware, so return
  // true only for the obviously uniform cases (argument, globalvalue)
  if (Splat && (isa<Argument>(Splat) || isa<GlobalValue>(Splat)))
    OpInfo = OK_UniformValue;

  return OpInfo;
}

int TargetTransformInfo::getArithmeticInstrCost(
    unsigned Opcode, Type *Ty, OperandValueKind Opd1Info,
    OperandValueKind Opd2Info, OperandValueProperties Opd1PropInfo,
    OperandValueProperties Opd2PropInfo,
    ArrayRef<const Value *> Args) const {
  int Cost = TTIImpl->getArithmeticInstrCost(Opcode, Ty, Opd1Info, Opd2Info,
                                             Opd1PropInfo, Opd2PropInfo, Args);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

int TargetTransformInfo::getShuffleCost(ShuffleKind Kind, Type *Ty, int Index,
                                        Type *SubTp) const {
  int Cost = TTIImpl->getShuffleCost(Kind, Ty, Index, SubTp);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

int TargetTransformInfo::getCastInstrCost(unsigned Opcode, Type *Dst,
                                 Type *Src, const Instruction *I) const {
  assert ((I == nullptr || I->getOpcode() == Opcode) &&
          "Opcode should reflect passed instruction.");
  int Cost = TTIImpl->getCastInstrCost(Opcode, Dst, Src, I);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

int TargetTransformInfo::getExtractWithExtendCost(unsigned Opcode, Type *Dst,
                                                  VectorType *VecTy,
                                                  unsigned Index) const {
  int Cost = TTIImpl->getExtractWithExtendCost(Opcode, Dst, VecTy, Index);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

int TargetTransformInfo::getCFInstrCost(unsigned Opcode) const {
  int Cost = TTIImpl->getCFInstrCost(Opcode);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

int TargetTransformInfo::getCmpSelInstrCost(unsigned Opcode, Type *ValTy,
                                 Type *CondTy, const Instruction *I) const {
  assert ((I == nullptr || I->getOpcode() == Opcode) &&
          "Opcode should reflect passed instruction.");
  int Cost = TTIImpl->getCmpSelInstrCost(Opcode, ValTy, CondTy, I);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

int TargetTransformInfo::getVectorInstrCost(unsigned Opcode, Type *Val,
                                            unsigned Index) const {
  int Cost = TTIImpl->getVectorInstrCost(Opcode, Val, Index);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

int TargetTransformInfo::getMemoryOpCost(unsigned Opcode, Type *Src,
                                         unsigned Alignment,
                                         unsigned AddressSpace,
                                         const Instruction *I) const {
  assert ((I == nullptr || I->getOpcode() == Opcode) &&
          "Opcode should reflect passed instruction.");
  int Cost = TTIImpl->getMemoryOpCost(Opcode, Src, Alignment, AddressSpace, I);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

int TargetTransformInfo::getMaskedMemoryOpCost(unsigned Opcode, Type *Src,
                                               unsigned Alignment,
                                               unsigned AddressSpace) const {
  int Cost =
      TTIImpl->getMaskedMemoryOpCost(Opcode, Src, Alignment, AddressSpace);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

int TargetTransformInfo::getGatherScatterOpCost(unsigned Opcode, Type *DataTy,
                                                Value *Ptr, bool VariableMask,
                                                unsigned Alignment) const {
  int Cost = TTIImpl->getGatherScatterOpCost(Opcode, DataTy, Ptr, VariableMask,
                                             Alignment);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

int TargetTransformInfo::getInterleavedMemoryOpCost(
    unsigned Opcode, Type *VecTy, unsigned Factor, ArrayRef<unsigned> Indices,
    unsigned Alignment, unsigned AddressSpace, bool UseMaskForCond,
    bool UseMaskForGaps) const {
  int Cost = TTIImpl->getInterleavedMemoryOpCost(Opcode, VecTy, Factor, Indices,
                                                 Alignment, AddressSpace,
                                                 UseMaskForCond,
                                                 UseMaskForGaps);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

int TargetTransformInfo::getIntrinsicInstrCost(Intrinsic::ID ID, Type *RetTy,
                                    ArrayRef<Type *> Tys, FastMathFlags FMF,
                                    unsigned ScalarizationCostPassed) const {
  int Cost = TTIImpl->getIntrinsicInstrCost(ID, RetTy, Tys, FMF,
                                            ScalarizationCostPassed);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

int TargetTransformInfo::getIntrinsicInstrCost(Intrinsic::ID ID, Type *RetTy,
           ArrayRef<Value *> Args, FastMathFlags FMF, unsigned VF) const {
  int Cost = TTIImpl->getIntrinsicInstrCost(ID, RetTy, Args, FMF, VF);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

int TargetTransformInfo::getCallInstrCost(Function *F, Type *RetTy,
                                          ArrayRef<Type *> Tys) const {
  int Cost = TTIImpl->getCallInstrCost(F, RetTy, Tys);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

unsigned TargetTransformInfo::getNumberOfParts(Type *Tp) const {
  return TTIImpl->getNumberOfParts(Tp);
}

int TargetTransformInfo::getAddressComputationCost(Type *Tp,
                                                   ScalarEvolution *SE,
                                                   const SCEV *Ptr) const {
  int Cost = TTIImpl->getAddressComputationCost(Tp, SE, Ptr);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

int TargetTransformInfo::getArithmeticReductionCost(unsigned Opcode, Type *Ty,
                                                    bool IsPairwiseForm) const {
  int Cost = TTIImpl->getArithmeticReductionCost(Opcode, Ty, IsPairwiseForm);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

int TargetTransformInfo::getMinMaxReductionCost(Type *Ty, Type *CondTy,
                                                bool IsPairwiseForm,
                                                bool IsUnsigned) const {
  int Cost =
      TTIImpl->getMinMaxReductionCost(Ty, CondTy, IsPairwiseForm, IsUnsigned);
  assert(Cost >= 0 && "TTI should not produce negative costs!");
  return Cost;
}

unsigned
TargetTransformInfo::getCostOfKeepingLiveOverCall(ArrayRef<Type *> Tys) const {
  return TTIImpl->getCostOfKeepingLiveOverCall(Tys);
}

bool TargetTransformInfo::getTgtMemIntrinsic(IntrinsicInst *Inst,
                                             MemIntrinsicInfo &Info) const {
  return TTIImpl->getTgtMemIntrinsic(Inst, Info);
}

unsigned TargetTransformInfo::getAtomicMemIntrinsicMaxElementSize() const {
  return TTIImpl->getAtomicMemIntrinsicMaxElementSize();
}

Value *TargetTransformInfo::getOrCreateResultFromMemIntrinsic(
    IntrinsicInst *Inst, Type *ExpectedType) const {
  return TTIImpl->getOrCreateResultFromMemIntrinsic(Inst, ExpectedType);
}

Type *TargetTransformInfo::getMemcpyLoopLoweringType(LLVMContext &Context,
                                                     Value *Length,
                                                     unsigned SrcAlign,
                                                     unsigned DestAlign) const {
  return TTIImpl->getMemcpyLoopLoweringType(Context, Length, SrcAlign,
                                            DestAlign);
}

void TargetTransformInfo::getMemcpyLoopResidualLoweringType(
    SmallVectorImpl<Type *> &OpsOut, LLVMContext &Context,
    unsigned RemainingBytes, unsigned SrcAlign, unsigned DestAlign) const {
  TTIImpl->getMemcpyLoopResidualLoweringType(OpsOut, Context, RemainingBytes,
                                             SrcAlign, DestAlign);
}

bool TargetTransformInfo::areInlineCompatible(const Function *Caller,
                                              const Function *Callee) const {
  return TTIImpl->areInlineCompatible(Caller, Callee);
}

bool TargetTransformInfo::areFunctionArgsABICompatible(
    const Function *Caller, const Function *Callee,
    SmallPtrSetImpl<Argument *> &Args) const {
  return TTIImpl->areFunctionArgsABICompatible(Caller, Callee, Args);
}

bool TargetTransformInfo::isIndexedLoadLegal(MemIndexedMode Mode,
                                             Type *Ty) const {
  return TTIImpl->isIndexedLoadLegal(Mode, Ty);
}

bool TargetTransformInfo::isIndexedStoreLegal(MemIndexedMode Mode,
                                              Type *Ty) const {
  return TTIImpl->isIndexedStoreLegal(Mode, Ty);
}

unsigned TargetTransformInfo::getLoadStoreVecRegBitWidth(unsigned AS) const {
  return TTIImpl->getLoadStoreVecRegBitWidth(AS);
}

bool TargetTransformInfo::isLegalToVectorizeLoad(LoadInst *LI) const {
  return TTIImpl->isLegalToVectorizeLoad(LI);
}

bool TargetTransformInfo::isLegalToVectorizeStore(StoreInst *SI) const {
  return TTIImpl->isLegalToVectorizeStore(SI);
}

bool TargetTransformInfo::isLegalToVectorizeLoadChain(
    unsigned ChainSizeInBytes, unsigned Alignment, unsigned AddrSpace) const {
  return TTIImpl->isLegalToVectorizeLoadChain(ChainSizeInBytes, Alignment,
                                              AddrSpace);
}

bool TargetTransformInfo::isLegalToVectorizeStoreChain(
    unsigned ChainSizeInBytes, unsigned Alignment, unsigned AddrSpace) const {
  return TTIImpl->isLegalToVectorizeStoreChain(ChainSizeInBytes, Alignment,
                                               AddrSpace);
}

unsigned TargetTransformInfo::getLoadVectorFactor(unsigned VF,
                                                  unsigned LoadSize,
                                                  unsigned ChainSizeInBytes,
                                                  VectorType *VecTy) const {
  return TTIImpl->getLoadVectorFactor(VF, LoadSize, ChainSizeInBytes, VecTy);
}

unsigned TargetTransformInfo::getStoreVectorFactor(unsigned VF,
                                                   unsigned StoreSize,
                                                   unsigned ChainSizeInBytes,
                                                   VectorType *VecTy) const {
  return TTIImpl->getStoreVectorFactor(VF, StoreSize, ChainSizeInBytes, VecTy);
}

bool TargetTransformInfo::useReductionIntrinsic(unsigned Opcode,
                                                Type *Ty, ReductionFlags Flags) const {
  return TTIImpl->useReductionIntrinsic(Opcode, Ty, Flags);
}

bool TargetTransformInfo::shouldExpandReduction(const IntrinsicInst *II) const {
  return TTIImpl->shouldExpandReduction(II);
}

int TargetTransformInfo::getInstructionLatency(const Instruction *I) const {
  return TTIImpl->getInstructionLatency(I);
}

static bool matchPairwiseShuffleMask(ShuffleVectorInst *SI, bool IsLeft,
                                     unsigned Level) {
  // We don't need a shuffle if we just want to have element 0 in position 0 of
  // the vector.
  if (!SI && Level == 0 && IsLeft)
    return true;
  else if (!SI)
    return false;

  SmallVector<int, 32> Mask(SI->getType()->getVectorNumElements(), -1);

  // Build a mask of 0, 2, ... (left) or 1, 3, ... (right) depending on whether
  // we look at the left or right side.
  for (unsigned i = 0, e = (1 << Level), val = !IsLeft; i != e; ++i, val += 2)
    Mask[i] = val;

  SmallVector<int, 16> ActualMask = SI->getShuffleMask();
  return Mask == ActualMask;
}

namespace {
/// Kind of the reduction data.
enum ReductionKind {
  RK_None,           /// Not a reduction.
  RK_Arithmetic,     /// Binary reduction data.
  RK_MinMax,         /// Min/max reduction data.
  RK_UnsignedMinMax, /// Unsigned min/max reduction data.
};
/// Contains opcode + LHS/RHS parts of the reduction operations.
struct ReductionData {
  ReductionData() = delete;
  ReductionData(ReductionKind Kind, unsigned Opcode, Value *LHS, Value *RHS)
      : Opcode(Opcode), LHS(LHS), RHS(RHS), Kind(Kind) {
    assert(Kind != RK_None && "expected binary or min/max reduction only.");
  }
  unsigned Opcode = 0;
  Value *LHS = nullptr;
  Value *RHS = nullptr;
  ReductionKind Kind = RK_None;
  bool hasSameData(ReductionData &RD) const {
    return Kind == RD.Kind && Opcode == RD.Opcode;
  }
};
} // namespace

static Optional<ReductionData> getReductionData(Instruction *I) {
  Value *L, *R;
  if (m_BinOp(m_Value(L), m_Value(R)).match(I))
    return ReductionData(RK_Arithmetic, I->getOpcode(), L, R);
  if (auto *SI = dyn_cast<SelectInst>(I)) {
    if (m_SMin(m_Value(L), m_Value(R)).match(SI) ||
        m_SMax(m_Value(L), m_Value(R)).match(SI) ||
        m_OrdFMin(m_Value(L), m_Value(R)).match(SI) ||
        m_OrdFMax(m_Value(L), m_Value(R)).match(SI) ||
        m_UnordFMin(m_Value(L), m_Value(R)).match(SI) ||
        m_UnordFMax(m_Value(L), m_Value(R)).match(SI)) {
      auto *CI = cast<CmpInst>(SI->getCondition());
      return ReductionData(RK_MinMax, CI->getOpcode(), L, R);
    }
    if (m_UMin(m_Value(L), m_Value(R)).match(SI) ||
        m_UMax(m_Value(L), m_Value(R)).match(SI)) {
      auto *CI = cast<CmpInst>(SI->getCondition());
      return ReductionData(RK_UnsignedMinMax, CI->getOpcode(), L, R);
    }
  }
  return llvm::None;
}

static ReductionKind matchPairwiseReductionAtLevel(Instruction *I,
                                                   unsigned Level,
                                                   unsigned NumLevels) {
  // Match one level of pairwise operations.
  // %rdx.shuf.0.0 = shufflevector <4 x float> %rdx, <4 x float> undef,
  //       <4 x i32> <i32 0, i32 2 , i32 undef, i32 undef>
  // %rdx.shuf.0.1 = shufflevector <4 x float> %rdx, <4 x float> undef,
  //       <4 x i32> <i32 1, i32 3, i32 undef, i32 undef>
  // %bin.rdx.0 = fadd <4 x float> %rdx.shuf.0.0, %rdx.shuf.0.1
  if (!I)
    return RK_None;

  assert(I->getType()->isVectorTy() && "Expecting a vector type");

  Optional<ReductionData> RD = getReductionData(I);
  if (!RD)
    return RK_None;

  ShuffleVectorInst *LS = dyn_cast<ShuffleVectorInst>(RD->LHS);
  if (!LS && Level)
    return RK_None;
  ShuffleVectorInst *RS = dyn_cast<ShuffleVectorInst>(RD->RHS);
  if (!RS && Level)
    return RK_None;

  // On level 0 we can omit one shufflevector instruction.
  if (!Level && !RS && !LS)
    return RK_None;

  // Shuffle inputs must match.
  Value *NextLevelOpL = LS ? LS->getOperand(0) : nullptr;
  Value *NextLevelOpR = RS ? RS->getOperand(0) : nullptr;
  Value *NextLevelOp = nullptr;
  if (NextLevelOpR && NextLevelOpL) {
    // If we have two shuffles their operands must match.
    if (NextLevelOpL != NextLevelOpR)
      return RK_None;

    NextLevelOp = NextLevelOpL;
  } else if (Level == 0 && (NextLevelOpR || NextLevelOpL)) {
    // On the first level we can omit the shufflevector <0, undef,...>. So the
    // input to the other shufflevector <1, undef> must match with one of the
    // inputs to the current binary operation.
    // Example:
    //  %NextLevelOpL = shufflevector %R, <1, undef ...>
    //  %BinOp        = fadd          %NextLevelOpL, %R
    if (NextLevelOpL && NextLevelOpL != RD->RHS)
      return RK_None;
    else if (NextLevelOpR && NextLevelOpR != RD->LHS)
      return RK_None;

    NextLevelOp = NextLevelOpL ? RD->RHS : RD->LHS;
  } else
    return RK_None;

  // Check that the next levels binary operation exists and matches with the
  // current one.
  if (Level + 1 != NumLevels) {
    Optional<ReductionData> NextLevelRD =
        getReductionData(cast<Instruction>(NextLevelOp));
    if (!NextLevelRD || !RD->hasSameData(*NextLevelRD))
      return RK_None;
  }

  // Shuffle mask for pairwise operation must match.
  if (matchPairwiseShuffleMask(LS, /*IsLeft=*/true, Level)) {
    if (!matchPairwiseShuffleMask(RS, /*IsLeft=*/false, Level))
      return RK_None;
  } else if (matchPairwiseShuffleMask(RS, /*IsLeft=*/true, Level)) {
    if (!matchPairwiseShuffleMask(LS, /*IsLeft=*/false, Level))
      return RK_None;
  } else {
    return RK_None;
  }

  if (++Level == NumLevels)
    return RD->Kind;

  // Match next level.
  return matchPairwiseReductionAtLevel(cast<Instruction>(NextLevelOp), Level,
                                       NumLevels);
}

static ReductionKind matchPairwiseReduction(const ExtractElementInst *ReduxRoot,
                                            unsigned &Opcode, Type *&Ty) {
  if (!EnableReduxCost)
    return RK_None;

  // Need to extract the first element.
  ConstantInt *CI = dyn_cast<ConstantInt>(ReduxRoot->getOperand(1));
  unsigned Idx = ~0u;
  if (CI)
    Idx = CI->getZExtValue();
  if (Idx != 0)
    return RK_None;

  auto *RdxStart = dyn_cast<Instruction>(ReduxRoot->getOperand(0));
  if (!RdxStart)
    return RK_None;
  Optional<ReductionData> RD = getReductionData(RdxStart);
  if (!RD)
    return RK_None;

  Type *VecTy = RdxStart->getType();
  unsigned NumVecElems = VecTy->getVectorNumElements();
  if (!isPowerOf2_32(NumVecElems))
    return RK_None;

  // We look for a sequence of shuffle,shuffle,add triples like the following
  // that builds a pairwise reduction tree.
  //
  //  (X0, X1, X2, X3)
  //   (X0 + X1, X2 + X3, undef, undef)
  //    ((X0 + X1) + (X2 + X3), undef, undef, undef)
  //
  // %rdx.shuf.0.0 = shufflevector <4 x float> %rdx, <4 x float> undef,
  //       <4 x i32> <i32 0, i32 2 , i32 undef, i32 undef>
  // %rdx.shuf.0.1 = shufflevector <4 x float> %rdx, <4 x float> undef,
  //       <4 x i32> <i32 1, i32 3, i32 undef, i32 undef>
  // %bin.rdx.0 = fadd <4 x float> %rdx.shuf.0.0, %rdx.shuf.0.1
  // %rdx.shuf.1.0 = shufflevector <4 x float> %bin.rdx.0, <4 x float> undef,
  //       <4 x i32> <i32 0, i32 undef, i32 undef, i32 undef>
  // %rdx.shuf.1.1 = shufflevector <4 x float> %bin.rdx.0, <4 x float> undef,
  //       <4 x i32> <i32 1, i32 undef, i32 undef, i32 undef>
  // %bin.rdx8 = fadd <4 x float> %rdx.shuf.1.0, %rdx.shuf.1.1
  // %r = extractelement <4 x float> %bin.rdx8, i32 0
  if (matchPairwiseReductionAtLevel(RdxStart, 0, Log2_32(NumVecElems)) ==
      RK_None)
    return RK_None;

  Opcode = RD->Opcode;
  Ty = VecTy;

  return RD->Kind;
}

static std::pair<Value *, ShuffleVectorInst *>
getShuffleAndOtherOprd(Value *L, Value *R) {
  ShuffleVectorInst *S = nullptr;

  if ((S = dyn_cast<ShuffleVectorInst>(L)))
    return std::make_pair(R, S);

  S = dyn_cast<ShuffleVectorInst>(R);
  return std::make_pair(L, S);
}

static ReductionKind
matchVectorSplittingReduction(const ExtractElementInst *ReduxRoot,
                              unsigned &Opcode, Type *&Ty) {
  if (!EnableReduxCost)
    return RK_None;

  // Need to extract the first element.
  ConstantInt *CI = dyn_cast<ConstantInt>(ReduxRoot->getOperand(1));
  unsigned Idx = ~0u;
  if (CI)
    Idx = CI->getZExtValue();
  if (Idx != 0)
    return RK_None;

  auto *RdxStart = dyn_cast<Instruction>(ReduxRoot->getOperand(0));
  if (!RdxStart)
    return RK_None;
  Optional<ReductionData> RD = getReductionData(RdxStart);
  if (!RD)
    return RK_None;

  Type *VecTy = ReduxRoot->getOperand(0)->getType();
  unsigned NumVecElems = VecTy->getVectorNumElements();
  if (!isPowerOf2_32(NumVecElems))
    return RK_None;

  // We look for a sequence of shuffles and adds like the following matching one
  // fadd, shuffle vector pair at a time.
  //
  // %rdx.shuf = shufflevector <4 x float> %rdx, <4 x float> undef,
  //                           <4 x i32> <i32 2, i32 3, i32 undef, i32 undef>
  // %bin.rdx = fadd <4 x float> %rdx, %rdx.shuf
  // %rdx.shuf7 = shufflevector <4 x float> %bin.rdx, <4 x float> undef,
  //                          <4 x i32> <i32 1, i32 undef, i32 undef, i32 undef>
  // %bin.rdx8 = fadd <4 x float> %bin.rdx, %rdx.shuf7
  // %r = extractelement <4 x float> %bin.rdx8, i32 0

  unsigned MaskStart = 1;
  Instruction *RdxOp = RdxStart;
  SmallVector<int, 32> ShuffleMask(NumVecElems, 0);
  unsigned NumVecElemsRemain = NumVecElems;
  while (NumVecElemsRemain - 1) {
    // Check for the right reduction operation.
    if (!RdxOp)
      return RK_None;
    Optional<ReductionData> RDLevel = getReductionData(RdxOp);
    if (!RDLevel || !RDLevel->hasSameData(*RD))
      return RK_None;

    Value *NextRdxOp;
    ShuffleVectorInst *Shuffle;
    std::tie(NextRdxOp, Shuffle) =
        getShuffleAndOtherOprd(RDLevel->LHS, RDLevel->RHS);

    // Check the current reduction operation and the shuffle use the same value.
    if (Shuffle == nullptr)
      return RK_None;
    if (Shuffle->getOperand(0) != NextRdxOp)
      return RK_None;

    // Check that shuffle masks matches.
    for (unsigned j = 0; j != MaskStart; ++j)
      ShuffleMask[j] = MaskStart + j;
    // Fill the rest of the mask with -1 for undef.
    std::fill(&ShuffleMask[MaskStart], ShuffleMask.end(), -1);

    SmallVector<int, 16> Mask = Shuffle->getShuffleMask();
    if (ShuffleMask != Mask)
      return RK_None;

    RdxOp = dyn_cast<Instruction>(NextRdxOp);
    NumVecElemsRemain /= 2;
    MaskStart *= 2;
  }

  Opcode = RD->Opcode;
  Ty = VecTy;
  return RD->Kind;
}

int TargetTransformInfo::getInstructionThroughput(const Instruction *I) const {
  switch (I->getOpcode()) {
  case Instruction::GetElementPtr:
    return getUserCost(I);

  case Instruction::Ret:
  case Instruction::PHI:
  case Instruction::Br: {
    return getCFInstrCost(I->getOpcode());
  }
  case Instruction::Add:
  case Instruction::FAdd:
  case Instruction::Sub:
  case Instruction::FSub:
  case Instruction::Mul:
  case Instruction::FMul:
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::FDiv:
  case Instruction::URem:
  case Instruction::SRem:
  case Instruction::FRem:
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor: {
    TargetTransformInfo::OperandValueKind Op1VK, Op2VK;
    TargetTransformInfo::OperandValueProperties Op1VP, Op2VP;
    Op1VK = getOperandInfo(I->getOperand(0), Op1VP);
    Op2VK = getOperandInfo(I->getOperand(1), Op2VP);
    SmallVector<const Value *, 2> Operands(I->operand_values());
    return getArithmeticInstrCost(I->getOpcode(), I->getType(), Op1VK, Op2VK,
                                  Op1VP, Op2VP, Operands);
  }
  case Instruction::Select: {
    const SelectInst *SI = cast<SelectInst>(I);
    Type *CondTy = SI->getCondition()->getType();
    return getCmpSelInstrCost(I->getOpcode(), I->getType(), CondTy, I);
  }
  case Instruction::ICmp:
  case Instruction::FCmp: {
    Type *ValTy = I->getOperand(0)->getType();
    return getCmpSelInstrCost(I->getOpcode(), ValTy, I->getType(), I);
  }
  case Instruction::Store: {
    const StoreInst *SI = cast<StoreInst>(I);
    Type *ValTy = SI->getValueOperand()->getType();
    return getMemoryOpCost(I->getOpcode(), ValTy,
                                SI->getAlignment(),
                                SI->getPointerAddressSpace(), I);
  }
  case Instruction::Load: {
    const LoadInst *LI = cast<LoadInst>(I);
    return getMemoryOpCost(I->getOpcode(), I->getType(),
                                LI->getAlignment(),
                                LI->getPointerAddressSpace(), I);
  }
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::FPExt:
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
  case Instruction::SIToFP:
  case Instruction::UIToFP:
  case Instruction::Trunc:
  case Instruction::FPTrunc:
  case Instruction::BitCast:
  case Instruction::AddrSpaceCast: {
    Type *SrcTy = I->getOperand(0)->getType();
    return getCastInstrCost(I->getOpcode(), I->getType(), SrcTy, I);
  }
  case Instruction::ExtractElement: {
    const ExtractElementInst * EEI = cast<ExtractElementInst>(I);
    ConstantInt *CI = dyn_cast<ConstantInt>(I->getOperand(1));
    unsigned Idx = -1;
    if (CI)
      Idx = CI->getZExtValue();

    // Try to match a reduction sequence (series of shufflevector and vector
    // adds followed by a extractelement).
    unsigned ReduxOpCode;
    Type *ReduxType;

    switch (matchVectorSplittingReduction(EEI, ReduxOpCode, ReduxType)) {
    case RK_Arithmetic:
      return getArithmeticReductionCost(ReduxOpCode, ReduxType,
                                             /*IsPairwiseForm=*/false);
    case RK_MinMax:
      return getMinMaxReductionCost(
          ReduxType, CmpInst::makeCmpResultType(ReduxType),
          /*IsPairwiseForm=*/false, /*IsUnsigned=*/false);
    case RK_UnsignedMinMax:
      return getMinMaxReductionCost(
          ReduxType, CmpInst::makeCmpResultType(ReduxType),
          /*IsPairwiseForm=*/false, /*IsUnsigned=*/true);
    case RK_None:
      break;
    }

    switch (matchPairwiseReduction(EEI, ReduxOpCode, ReduxType)) {
    case RK_Arithmetic:
      return getArithmeticReductionCost(ReduxOpCode, ReduxType,
                                             /*IsPairwiseForm=*/true);
    case RK_MinMax:
      return getMinMaxReductionCost(
          ReduxType, CmpInst::makeCmpResultType(ReduxType),
          /*IsPairwiseForm=*/true, /*IsUnsigned=*/false);
    case RK_UnsignedMinMax:
      return getMinMaxReductionCost(
          ReduxType, CmpInst::makeCmpResultType(ReduxType),
          /*IsPairwiseForm=*/true, /*IsUnsigned=*/true);
    case RK_None:
      break;
    }

    return getVectorInstrCost(I->getOpcode(),
                                   EEI->getOperand(0)->getType(), Idx);
  }
  case Instruction::InsertElement: {
    const InsertElementInst * IE = cast<InsertElementInst>(I);
    ConstantInt *CI = dyn_cast<ConstantInt>(IE->getOperand(2));
    unsigned Idx = -1;
    if (CI)
      Idx = CI->getZExtValue();
    return getVectorInstrCost(I->getOpcode(),
                                   IE->getType(), Idx);
  }
  case Instruction::ShuffleVector: {
    const ShuffleVectorInst *Shuffle = cast<ShuffleVectorInst>(I);
    Type *Ty = Shuffle->getType();
    Type *SrcTy = Shuffle->getOperand(0)->getType();

    // TODO: Identify and add costs for insert subvector, etc.
    int SubIndex;
    if (Shuffle->isExtractSubvectorMask(SubIndex))
      return TTIImpl->getShuffleCost(SK_ExtractSubvector, SrcTy, SubIndex, Ty);

    if (Shuffle->changesLength())
      return -1;

    if (Shuffle->isIdentity())
      return 0;

    if (Shuffle->isReverse())
      return TTIImpl->getShuffleCost(SK_Reverse, Ty, 0, nullptr);

    if (Shuffle->isSelect())
      return TTIImpl->getShuffleCost(SK_Select, Ty, 0, nullptr);

    if (Shuffle->isTranspose())
      return TTIImpl->getShuffleCost(SK_Transpose, Ty, 0, nullptr);

    if (Shuffle->isZeroEltSplat())
      return TTIImpl->getShuffleCost(SK_Broadcast, Ty, 0, nullptr);

    if (Shuffle->isSingleSource())
      return TTIImpl->getShuffleCost(SK_PermuteSingleSrc, Ty, 0, nullptr);

    return TTIImpl->getShuffleCost(SK_PermuteTwoSrc, Ty, 0, nullptr);
  }
  case Instruction::Call:
    if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
      SmallVector<Value *, 4> Args(II->arg_operands());

      FastMathFlags FMF;
      if (auto *FPMO = dyn_cast<FPMathOperator>(II))
        FMF = FPMO->getFastMathFlags();

      return getIntrinsicInstrCost(II->getIntrinsicID(), II->getType(),
                                        Args, FMF);
    }
    return -1;
  default:
    // We don't have any information on this instruction.
    return -1;
  }
}

TargetTransformInfo::Concept::~Concept() {}

TargetIRAnalysis::TargetIRAnalysis() : TTICallback(&getDefaultTTI) {}

TargetIRAnalysis::TargetIRAnalysis(
    std::function<Result(const Function &)> TTICallback)
    : TTICallback(std::move(TTICallback)) {}

TargetIRAnalysis::Result TargetIRAnalysis::run(const Function &F,
                                               FunctionAnalysisManager &) {
  return TTICallback(F);
}

AnalysisKey TargetIRAnalysis::Key;

TargetIRAnalysis::Result TargetIRAnalysis::getDefaultTTI(const Function &F) {
  return Result(F.getParent()->getDataLayout());
}

// Register the basic pass.
INITIALIZE_PASS(TargetTransformInfoWrapperPass, "tti",
                "Target Transform Information", false, true)
char TargetTransformInfoWrapperPass::ID = 0;

void TargetTransformInfoWrapperPass::anchor() {}

TargetTransformInfoWrapperPass::TargetTransformInfoWrapperPass()
    : ImmutablePass(ID) {
  initializeTargetTransformInfoWrapperPassPass(
      *PassRegistry::getPassRegistry());
}

TargetTransformInfoWrapperPass::TargetTransformInfoWrapperPass(
    TargetIRAnalysis TIRA)
    : ImmutablePass(ID), TIRA(std::move(TIRA)) {
  initializeTargetTransformInfoWrapperPassPass(
      *PassRegistry::getPassRegistry());
}

TargetTransformInfo &TargetTransformInfoWrapperPass::getTTI(const Function &F) {
  FunctionAnalysisManager DummyFAM;
  TTI = TIRA.run(F, DummyFAM);
  return *TTI;
}

ImmutablePass *
llvm::createTargetTransformInfoWrapperPass(TargetIRAnalysis TIRA) {
  return new TargetTransformInfoWrapperPass(std::move(TIRA));
}
