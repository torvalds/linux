//===- TargetTransformInfoImpl.h --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides helpers for the implementation of
/// a TargetTransformInfo-conforming class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_TARGETTRANSFORMINFOIMPL_H
#define LLVM_ANALYSIS_TARGETTRANSFORMINFOIMPL_H

#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"

namespace llvm {

/// Base class for use as a mix-in that aids implementing
/// a TargetTransformInfo-compatible class.
class TargetTransformInfoImplBase {
protected:
  typedef TargetTransformInfo TTI;

  const DataLayout &DL;

  explicit TargetTransformInfoImplBase(const DataLayout &DL) : DL(DL) {}

public:
  // Provide value semantics. MSVC requires that we spell all of these out.
  TargetTransformInfoImplBase(const TargetTransformInfoImplBase &Arg)
      : DL(Arg.DL) {}
  TargetTransformInfoImplBase(TargetTransformInfoImplBase &&Arg) : DL(Arg.DL) {}

  const DataLayout &getDataLayout() const { return DL; }

  unsigned getOperationCost(unsigned Opcode, Type *Ty, Type *OpTy) {
    switch (Opcode) {
    default:
      // By default, just classify everything as 'basic'.
      return TTI::TCC_Basic;

    case Instruction::GetElementPtr:
      llvm_unreachable("Use getGEPCost for GEP operations!");

    case Instruction::BitCast:
      assert(OpTy && "Cast instructions must provide the operand type");
      if (Ty == OpTy || (Ty->isPointerTy() && OpTy->isPointerTy()))
        // Identity and pointer-to-pointer casts are free.
        return TTI::TCC_Free;

      // Otherwise, the default basic cost is used.
      return TTI::TCC_Basic;

    case Instruction::FDiv:
    case Instruction::FRem:
    case Instruction::SDiv:
    case Instruction::SRem:
    case Instruction::UDiv:
    case Instruction::URem:
      return TTI::TCC_Expensive;

    case Instruction::IntToPtr: {
      // An inttoptr cast is free so long as the input is a legal integer type
      // which doesn't contain values outside the range of a pointer.
      unsigned OpSize = OpTy->getScalarSizeInBits();
      if (DL.isLegalInteger(OpSize) &&
          OpSize <= DL.getPointerTypeSizeInBits(Ty))
        return TTI::TCC_Free;

      // Otherwise it's not a no-op.
      return TTI::TCC_Basic;
    }
    case Instruction::PtrToInt: {
      // A ptrtoint cast is free so long as the result is large enough to store
      // the pointer, and a legal integer type.
      unsigned DestSize = Ty->getScalarSizeInBits();
      if (DL.isLegalInteger(DestSize) &&
          DestSize >= DL.getPointerTypeSizeInBits(OpTy))
        return TTI::TCC_Free;

      // Otherwise it's not a no-op.
      return TTI::TCC_Basic;
    }
    case Instruction::Trunc:
      // trunc to a native type is free (assuming the target has compare and
      // shift-right of the same width).
      if (DL.isLegalInteger(DL.getTypeSizeInBits(Ty)))
        return TTI::TCC_Free;

      return TTI::TCC_Basic;
    }
  }

  int getGEPCost(Type *PointeeType, const Value *Ptr,
                 ArrayRef<const Value *> Operands) {
    // In the basic model, we just assume that all-constant GEPs will be folded
    // into their uses via addressing modes.
    for (unsigned Idx = 0, Size = Operands.size(); Idx != Size; ++Idx)
      if (!isa<Constant>(Operands[Idx]))
        return TTI::TCC_Basic;

    return TTI::TCC_Free;
  }

  unsigned getEstimatedNumberOfCaseClusters(const SwitchInst &SI,
                                            unsigned &JTSize) {
    JTSize = 0;
    return SI.getNumCases();
  }

  int getExtCost(const Instruction *I, const Value *Src) {
    return TTI::TCC_Basic;
  }

  unsigned getCallCost(FunctionType *FTy, int NumArgs) {
    assert(FTy && "FunctionType must be provided to this routine.");

    // The target-independent implementation just measures the size of the
    // function by approximating that each argument will take on average one
    // instruction to prepare.

    if (NumArgs < 0)
      // Set the argument number to the number of explicit arguments in the
      // function.
      NumArgs = FTy->getNumParams();

    return TTI::TCC_Basic * (NumArgs + 1);
  }

  unsigned getInliningThresholdMultiplier() { return 1; }

  unsigned getIntrinsicCost(Intrinsic::ID IID, Type *RetTy,
                            ArrayRef<Type *> ParamTys) {
    switch (IID) {
    default:
      // Intrinsics rarely (if ever) have normal argument setup constraints.
      // Model them as having a basic instruction cost.
      // FIXME: This is wrong for libc intrinsics.
      return TTI::TCC_Basic;

    case Intrinsic::annotation:
    case Intrinsic::assume:
    case Intrinsic::sideeffect:
    case Intrinsic::dbg_declare:
    case Intrinsic::dbg_value:
    case Intrinsic::dbg_label:
    case Intrinsic::invariant_start:
    case Intrinsic::invariant_end:
    case Intrinsic::launder_invariant_group:
    case Intrinsic::strip_invariant_group:
    case Intrinsic::is_constant:
    case Intrinsic::lifetime_start:
    case Intrinsic::lifetime_end:
    case Intrinsic::objectsize:
    case Intrinsic::ptr_annotation:
    case Intrinsic::var_annotation:
    case Intrinsic::experimental_gc_result:
    case Intrinsic::experimental_gc_relocate:
    case Intrinsic::coro_alloc:
    case Intrinsic::coro_begin:
    case Intrinsic::coro_free:
    case Intrinsic::coro_end:
    case Intrinsic::coro_frame:
    case Intrinsic::coro_size:
    case Intrinsic::coro_suspend:
    case Intrinsic::coro_param:
    case Intrinsic::coro_subfn_addr:
      // These intrinsics don't actually represent code after lowering.
      return TTI::TCC_Free;
    }
  }

  bool hasBranchDivergence() { return false; }

  bool isSourceOfDivergence(const Value *V) { return false; }

  bool isAlwaysUniform(const Value *V) { return false; }

  unsigned getFlatAddressSpace () {
    return -1;
  }

  bool isLoweredToCall(const Function *F) {
    assert(F && "A concrete function must be provided to this routine.");

    // FIXME: These should almost certainly not be handled here, and instead
    // handled with the help of TLI or the target itself. This was largely
    // ported from existing analysis heuristics here so that such refactorings
    // can take place in the future.

    if (F->isIntrinsic())
      return false;

    if (F->hasLocalLinkage() || !F->hasName())
      return true;

    StringRef Name = F->getName();

    // These will all likely lower to a single selection DAG node.
    if (Name == "copysign" || Name == "copysignf" || Name == "copysignl" ||
        Name == "fabs" || Name == "fabsf" || Name == "fabsl" || Name == "sin" ||
        Name == "fmin" || Name == "fminf" || Name == "fminl" ||
        Name == "fmax" || Name == "fmaxf" || Name == "fmaxl" ||
        Name == "sinf" || Name == "sinl" || Name == "cos" || Name == "cosf" ||
        Name == "cosl" || Name == "sqrt" || Name == "sqrtf" || Name == "sqrtl")
      return false;

    // These are all likely to be optimized into something smaller.
    if (Name == "pow" || Name == "powf" || Name == "powl" || Name == "exp2" ||
        Name == "exp2l" || Name == "exp2f" || Name == "floor" ||
        Name == "floorf" || Name == "ceil" || Name == "round" ||
        Name == "ffs" || Name == "ffsl" || Name == "abs" || Name == "labs" ||
        Name == "llabs")
      return false;

    return true;
  }

  void getUnrollingPreferences(Loop *, ScalarEvolution &,
                               TTI::UnrollingPreferences &) {}

  bool isLegalAddImmediate(int64_t Imm) { return false; }

  bool isLegalICmpImmediate(int64_t Imm) { return false; }

  bool isLegalAddressingMode(Type *Ty, GlobalValue *BaseGV, int64_t BaseOffset,
                             bool HasBaseReg, int64_t Scale,
                             unsigned AddrSpace, Instruction *I = nullptr) {
    // Guess that only reg and reg+reg addressing is allowed. This heuristic is
    // taken from the implementation of LSR.
    return !BaseGV && BaseOffset == 0 && (Scale == 0 || Scale == 1);
  }

  bool isLSRCostLess(TTI::LSRCost &C1, TTI::LSRCost &C2) {
    return std::tie(C1.NumRegs, C1.AddRecCost, C1.NumIVMuls, C1.NumBaseAdds,
                    C1.ScaleCost, C1.ImmCost, C1.SetupCost) <
           std::tie(C2.NumRegs, C2.AddRecCost, C2.NumIVMuls, C2.NumBaseAdds,
                    C2.ScaleCost, C2.ImmCost, C2.SetupCost);
  }

  bool canMacroFuseCmp() { return false; }

  bool shouldFavorPostInc() const { return false; }

  bool isLegalMaskedStore(Type *DataType) { return false; }

  bool isLegalMaskedLoad(Type *DataType) { return false; }

  bool isLegalMaskedScatter(Type *DataType) { return false; }

  bool isLegalMaskedGather(Type *DataType) { return false; }

  bool hasDivRemOp(Type *DataType, bool IsSigned) { return false; }

  bool hasVolatileVariant(Instruction *I, unsigned AddrSpace) { return false; }

  bool prefersVectorizedAddressing() { return true; }

  int getScalingFactorCost(Type *Ty, GlobalValue *BaseGV, int64_t BaseOffset,
                           bool HasBaseReg, int64_t Scale, unsigned AddrSpace) {
    // Guess that all legal addressing mode are free.
    if (isLegalAddressingMode(Ty, BaseGV, BaseOffset, HasBaseReg,
                              Scale, AddrSpace))
      return 0;
    return -1;
  }

  bool LSRWithInstrQueries() { return false; }

  bool isTruncateFree(Type *Ty1, Type *Ty2) { return false; }

  bool isProfitableToHoist(Instruction *I) { return true; }

  bool useAA() { return false; }

  bool isTypeLegal(Type *Ty) { return false; }

  unsigned getJumpBufAlignment() { return 0; }

  unsigned getJumpBufSize() { return 0; }

  bool shouldBuildLookupTables() { return true; }
  bool shouldBuildLookupTablesForConstant(Constant *C) { return true; }

  bool useColdCCForColdCall(Function &F) { return false; }

  unsigned getScalarizationOverhead(Type *Ty, bool Insert, bool Extract) {
    return 0;
  }

  unsigned getOperandsScalarizationOverhead(ArrayRef<const Value *> Args,
                                            unsigned VF) { return 0; }

  bool supportsEfficientVectorElementLoadStore() { return false; }

  bool enableAggressiveInterleaving(bool LoopHasReductions) { return false; }

  const TTI::MemCmpExpansionOptions *enableMemCmpExpansion(
      bool IsZeroCmp) const {
    return nullptr;
  }

  bool enableInterleavedAccessVectorization() { return false; }

  bool enableMaskedInterleavedAccessVectorization() { return false; }

  bool isFPVectorizationPotentiallyUnsafe() { return false; }

  bool allowsMisalignedMemoryAccesses(LLVMContext &Context,
                                      unsigned BitWidth,
                                      unsigned AddressSpace,
                                      unsigned Alignment,
                                      bool *Fast) { return false; }

  TTI::PopcntSupportKind getPopcntSupport(unsigned IntTyWidthInBit) {
    return TTI::PSK_Software;
  }

  bool haveFastSqrt(Type *Ty) { return false; }

  bool isFCmpOrdCheaperThanFCmpZero(Type *Ty) { return true; }

  unsigned getFPOpCost(Type *Ty) { return TargetTransformInfo::TCC_Basic; }

  int getIntImmCodeSizeCost(unsigned Opcode, unsigned Idx, const APInt &Imm,
                            Type *Ty) {
    return 0;
  }

  unsigned getIntImmCost(const APInt &Imm, Type *Ty) { return TTI::TCC_Basic; }

  unsigned getIntImmCost(unsigned Opcode, unsigned Idx, const APInt &Imm,
                         Type *Ty) {
    return TTI::TCC_Free;
  }

  unsigned getIntImmCost(Intrinsic::ID IID, unsigned Idx, const APInt &Imm,
                         Type *Ty) {
    return TTI::TCC_Free;
  }

  unsigned getNumberOfRegisters(bool Vector) { return 8; }

  unsigned getRegisterBitWidth(bool Vector) const { return 32; }

  unsigned getMinVectorRegisterBitWidth() { return 128; }

  bool shouldMaximizeVectorBandwidth(bool OptSize) const { return false; }

  unsigned getMinimumVF(unsigned ElemWidth) const { return 0; }

  bool
  shouldConsiderAddressTypePromotion(const Instruction &I,
                                     bool &AllowPromotionWithoutCommonHeader) {
    AllowPromotionWithoutCommonHeader = false;
    return false;
  }

  unsigned getCacheLineSize() { return 0; }

  llvm::Optional<unsigned> getCacheSize(TargetTransformInfo::CacheLevel Level) {
    switch (Level) {
    case TargetTransformInfo::CacheLevel::L1D:
      LLVM_FALLTHROUGH;
    case TargetTransformInfo::CacheLevel::L2D:
      return llvm::Optional<unsigned>();
    }

    llvm_unreachable("Unknown TargetTransformInfo::CacheLevel");
  }

  llvm::Optional<unsigned> getCacheAssociativity(
    TargetTransformInfo::CacheLevel Level) {
    switch (Level) {
    case TargetTransformInfo::CacheLevel::L1D:
      LLVM_FALLTHROUGH;
    case TargetTransformInfo::CacheLevel::L2D:
      return llvm::Optional<unsigned>();
    }

    llvm_unreachable("Unknown TargetTransformInfo::CacheLevel");
  }

  unsigned getPrefetchDistance() { return 0; }

  unsigned getMinPrefetchStride() { return 1; }

  unsigned getMaxPrefetchIterationsAhead() { return UINT_MAX; }

  unsigned getMaxInterleaveFactor(unsigned VF) { return 1; }

  unsigned getArithmeticInstrCost(unsigned Opcode, Type *Ty,
                                  TTI::OperandValueKind Opd1Info,
                                  TTI::OperandValueKind Opd2Info,
                                  TTI::OperandValueProperties Opd1PropInfo,
                                  TTI::OperandValueProperties Opd2PropInfo,
                                  ArrayRef<const Value *> Args) {
    return 1;
  }

  unsigned getShuffleCost(TTI::ShuffleKind Kind, Type *Ty, int Index,
                          Type *SubTp) {
    return 1;
  }

  unsigned getCastInstrCost(unsigned Opcode, Type *Dst, Type *Src,
                            const Instruction *I) { return 1; }

  unsigned getExtractWithExtendCost(unsigned Opcode, Type *Dst,
                                    VectorType *VecTy, unsigned Index) {
    return 1;
  }

  unsigned getCFInstrCost(unsigned Opcode) { return 1; }

  unsigned getCmpSelInstrCost(unsigned Opcode, Type *ValTy, Type *CondTy,
                              const Instruction *I) {
    return 1;
  }

  unsigned getVectorInstrCost(unsigned Opcode, Type *Val, unsigned Index) {
    return 1;
  }

  unsigned getMemoryOpCost(unsigned Opcode, Type *Src, unsigned Alignment,
                           unsigned AddressSpace, const Instruction *I) {
    return 1;
  }

  unsigned getMaskedMemoryOpCost(unsigned Opcode, Type *Src, unsigned Alignment,
                                 unsigned AddressSpace) {
    return 1;
  }

  unsigned getGatherScatterOpCost(unsigned Opcode, Type *DataTy, Value *Ptr,
                                  bool VariableMask,
                                  unsigned Alignment) {
    return 1;
  }

  unsigned getInterleavedMemoryOpCost(unsigned Opcode, Type *VecTy,
                                      unsigned Factor,
                                      ArrayRef<unsigned> Indices,
                                      unsigned Alignment, unsigned AddressSpace,
                                      bool UseMaskForCond = false,
                                      bool UseMaskForGaps = false) {
    return 1;
  }

  unsigned getIntrinsicInstrCost(Intrinsic::ID ID, Type *RetTy,
                                 ArrayRef<Type *> Tys, FastMathFlags FMF,
                                 unsigned ScalarizationCostPassed) {
    return 1;
  }
  unsigned getIntrinsicInstrCost(Intrinsic::ID ID, Type *RetTy,
            ArrayRef<Value *> Args, FastMathFlags FMF, unsigned VF) {
    return 1;
  }

  unsigned getCallInstrCost(Function *F, Type *RetTy, ArrayRef<Type *> Tys) {
    return 1;
  }

  unsigned getNumberOfParts(Type *Tp) { return 0; }

  unsigned getAddressComputationCost(Type *Tp, ScalarEvolution *,
                                     const SCEV *) {
    return 0;
  }

  unsigned getArithmeticReductionCost(unsigned, Type *, bool) { return 1; }

  unsigned getMinMaxReductionCost(Type *, Type *, bool, bool) { return 1; }

  unsigned getCostOfKeepingLiveOverCall(ArrayRef<Type *> Tys) { return 0; }

  bool getTgtMemIntrinsic(IntrinsicInst *Inst, MemIntrinsicInfo &Info) {
    return false;
  }

  unsigned getAtomicMemIntrinsicMaxElementSize() const {
    // Note for overrides: You must ensure for all element unordered-atomic
    // memory intrinsics that all power-of-2 element sizes up to, and
    // including, the return value of this method have a corresponding
    // runtime lib call. These runtime lib call definitions can be found
    // in RuntimeLibcalls.h
    return 0;
  }

  Value *getOrCreateResultFromMemIntrinsic(IntrinsicInst *Inst,
                                           Type *ExpectedType) {
    return nullptr;
  }

  Type *getMemcpyLoopLoweringType(LLVMContext &Context, Value *Length,
                                  unsigned SrcAlign, unsigned DestAlign) const {
    return Type::getInt8Ty(Context);
  }

  void getMemcpyLoopResidualLoweringType(SmallVectorImpl<Type *> &OpsOut,
                                         LLVMContext &Context,
                                         unsigned RemainingBytes,
                                         unsigned SrcAlign,
                                         unsigned DestAlign) const {
    for (unsigned i = 0; i != RemainingBytes; ++i)
      OpsOut.push_back(Type::getInt8Ty(Context));
  }

  bool areInlineCompatible(const Function *Caller,
                           const Function *Callee) const {
    return (Caller->getFnAttribute("target-cpu") ==
            Callee->getFnAttribute("target-cpu")) &&
           (Caller->getFnAttribute("target-features") ==
            Callee->getFnAttribute("target-features"));
  }

  bool areFunctionArgsABICompatible(const Function *Caller, const Function *Callee,
                                    SmallPtrSetImpl<Argument *> &Args) const {
    return (Caller->getFnAttribute("target-cpu") ==
            Callee->getFnAttribute("target-cpu")) &&
           (Caller->getFnAttribute("target-features") ==
            Callee->getFnAttribute("target-features"));
  }

  bool isIndexedLoadLegal(TTI::MemIndexedMode Mode, Type *Ty,
                          const DataLayout &DL) const {
    return false;
  }

  bool isIndexedStoreLegal(TTI::MemIndexedMode Mode, Type *Ty,
                           const DataLayout &DL) const {
    return false;
  }

  unsigned getLoadStoreVecRegBitWidth(unsigned AddrSpace) const { return 128; }

  bool isLegalToVectorizeLoad(LoadInst *LI) const { return true; }

  bool isLegalToVectorizeStore(StoreInst *SI) const { return true; }

  bool isLegalToVectorizeLoadChain(unsigned ChainSizeInBytes,
                                   unsigned Alignment,
                                   unsigned AddrSpace) const {
    return true;
  }

  bool isLegalToVectorizeStoreChain(unsigned ChainSizeInBytes,
                                    unsigned Alignment,
                                    unsigned AddrSpace) const {
    return true;
  }

  unsigned getLoadVectorFactor(unsigned VF, unsigned LoadSize,
                               unsigned ChainSizeInBytes,
                               VectorType *VecTy) const {
    return VF;
  }

  unsigned getStoreVectorFactor(unsigned VF, unsigned StoreSize,
                                unsigned ChainSizeInBytes,
                                VectorType *VecTy) const {
    return VF;
  }

  bool useReductionIntrinsic(unsigned Opcode, Type *Ty,
                             TTI::ReductionFlags Flags) const {
    return false;
  }

  bool shouldExpandReduction(const IntrinsicInst *II) const {
    return true;
  }

protected:
  // Obtain the minimum required size to hold the value (without the sign)
  // In case of a vector it returns the min required size for one element.
  unsigned minRequiredElementSize(const Value* Val, bool &isSigned) {
    if (isa<ConstantDataVector>(Val) || isa<ConstantVector>(Val)) {
      const auto* VectorValue = cast<Constant>(Val);

      // In case of a vector need to pick the max between the min
      // required size for each element
      auto *VT = cast<VectorType>(Val->getType());

      // Assume unsigned elements
      isSigned = false;

      // The max required size is the total vector width divided by num
      // of elements in the vector
      unsigned MaxRequiredSize = VT->getBitWidth() / VT->getNumElements();

      unsigned MinRequiredSize = 0;
      for(unsigned i = 0, e = VT->getNumElements(); i < e; ++i) {
        if (auto* IntElement =
              dyn_cast<ConstantInt>(VectorValue->getAggregateElement(i))) {
          bool signedElement = IntElement->getValue().isNegative();
          // Get the element min required size.
          unsigned ElementMinRequiredSize =
            IntElement->getValue().getMinSignedBits() - 1;
          // In case one element is signed then all the vector is signed.
          isSigned |= signedElement;
          // Save the max required bit size between all the elements.
          MinRequiredSize = std::max(MinRequiredSize, ElementMinRequiredSize);
        }
        else {
          // not an int constant element
          return MaxRequiredSize;
        }
      }
      return MinRequiredSize;
    }

    if (const auto* CI = dyn_cast<ConstantInt>(Val)) {
      isSigned = CI->getValue().isNegative();
      return CI->getValue().getMinSignedBits() - 1;
    }

    if (const auto* Cast = dyn_cast<SExtInst>(Val)) {
      isSigned = true;
      return Cast->getSrcTy()->getScalarSizeInBits() - 1;
    }

    if (const auto* Cast = dyn_cast<ZExtInst>(Val)) {
      isSigned = false;
      return Cast->getSrcTy()->getScalarSizeInBits();
    }

    isSigned = false;
    return Val->getType()->getScalarSizeInBits();
  }

  bool isStridedAccess(const SCEV *Ptr) {
    return Ptr && isa<SCEVAddRecExpr>(Ptr);
  }

  const SCEVConstant *getConstantStrideStep(ScalarEvolution *SE,
                                            const SCEV *Ptr) {
    if (!isStridedAccess(Ptr))
      return nullptr;
    const SCEVAddRecExpr *AddRec = cast<SCEVAddRecExpr>(Ptr);
    return dyn_cast<SCEVConstant>(AddRec->getStepRecurrence(*SE));
  }

  bool isConstantStridedAccessLessThan(ScalarEvolution *SE, const SCEV *Ptr,
                                       int64_t MergeDistance) {
    const SCEVConstant *Step = getConstantStrideStep(SE, Ptr);
    if (!Step)
      return false;
    APInt StrideVal = Step->getAPInt();
    if (StrideVal.getBitWidth() > 64)
      return false;
    // FIXME: Need to take absolute value for negative stride case.
    return StrideVal.getSExtValue() < MergeDistance;
  }
};

/// CRTP base class for use as a mix-in that aids implementing
/// a TargetTransformInfo-compatible class.
template <typename T>
class TargetTransformInfoImplCRTPBase : public TargetTransformInfoImplBase {
private:
  typedef TargetTransformInfoImplBase BaseT;

protected:
  explicit TargetTransformInfoImplCRTPBase(const DataLayout &DL) : BaseT(DL) {}

public:
  using BaseT::getCallCost;

  unsigned getCallCost(const Function *F, int NumArgs) {
    assert(F && "A concrete function must be provided to this routine.");

    if (NumArgs < 0)
      // Set the argument number to the number of explicit arguments in the
      // function.
      NumArgs = F->arg_size();

    if (Intrinsic::ID IID = F->getIntrinsicID()) {
      FunctionType *FTy = F->getFunctionType();
      SmallVector<Type *, 8> ParamTys(FTy->param_begin(), FTy->param_end());
      return static_cast<T *>(this)
          ->getIntrinsicCost(IID, FTy->getReturnType(), ParamTys);
    }

    if (!static_cast<T *>(this)->isLoweredToCall(F))
      return TTI::TCC_Basic; // Give a basic cost if it will be lowered
                             // directly.

    return static_cast<T *>(this)->getCallCost(F->getFunctionType(), NumArgs);
  }

  unsigned getCallCost(const Function *F, ArrayRef<const Value *> Arguments) {
    // Simply delegate to generic handling of the call.
    // FIXME: We should use instsimplify or something else to catch calls which
    // will constant fold with these arguments.
    return static_cast<T *>(this)->getCallCost(F, Arguments.size());
  }

  using BaseT::getGEPCost;

  int getGEPCost(Type *PointeeType, const Value *Ptr,
                 ArrayRef<const Value *> Operands) {
    const GlobalValue *BaseGV = nullptr;
    if (Ptr != nullptr) {
      // TODO: will remove this when pointers have an opaque type.
      assert(Ptr->getType()->getScalarType()->getPointerElementType() ==
                 PointeeType &&
             "explicit pointee type doesn't match operand's pointee type");
      BaseGV = dyn_cast<GlobalValue>(Ptr->stripPointerCasts());
    }
    bool HasBaseReg = (BaseGV == nullptr);

    auto PtrSizeBits = DL.getPointerTypeSizeInBits(Ptr->getType());
    APInt BaseOffset(PtrSizeBits, 0);
    int64_t Scale = 0;

    auto GTI = gep_type_begin(PointeeType, Operands);
    Type *TargetType = nullptr;

    // Handle the case where the GEP instruction has a single operand,
    // the basis, therefore TargetType is a nullptr.
    if (Operands.empty())
      return !BaseGV ? TTI::TCC_Free : TTI::TCC_Basic;

    for (auto I = Operands.begin(); I != Operands.end(); ++I, ++GTI) {
      TargetType = GTI.getIndexedType();
      // We assume that the cost of Scalar GEP with constant index and the
      // cost of Vector GEP with splat constant index are the same.
      const ConstantInt *ConstIdx = dyn_cast<ConstantInt>(*I);
      if (!ConstIdx)
        if (auto Splat = getSplatValue(*I))
          ConstIdx = dyn_cast<ConstantInt>(Splat);
      if (StructType *STy = GTI.getStructTypeOrNull()) {
        // For structures the index is always splat or scalar constant
        assert(ConstIdx && "Unexpected GEP index");
        uint64_t Field = ConstIdx->getZExtValue();
        BaseOffset += DL.getStructLayout(STy)->getElementOffset(Field);
      } else {
        int64_t ElementSize = DL.getTypeAllocSize(GTI.getIndexedType());
        if (ConstIdx) {
          BaseOffset +=
              ConstIdx->getValue().sextOrTrunc(PtrSizeBits) * ElementSize;
        } else {
          // Needs scale register.
          if (Scale != 0)
            // No addressing mode takes two scale registers.
            return TTI::TCC_Basic;
          Scale = ElementSize;
        }
      }
    }

    // Assumes the address space is 0 when Ptr is nullptr.
    unsigned AS =
        (Ptr == nullptr ? 0 : Ptr->getType()->getPointerAddressSpace());

    if (static_cast<T *>(this)->isLegalAddressingMode(
            TargetType, const_cast<GlobalValue *>(BaseGV),
            BaseOffset.sextOrTrunc(64).getSExtValue(), HasBaseReg, Scale, AS))
      return TTI::TCC_Free;
    return TTI::TCC_Basic;
  }

  using BaseT::getIntrinsicCost;

  unsigned getIntrinsicCost(Intrinsic::ID IID, Type *RetTy,
                            ArrayRef<const Value *> Arguments) {
    // Delegate to the generic intrinsic handling code. This mostly provides an
    // opportunity for targets to (for example) special case the cost of
    // certain intrinsics based on constants used as arguments.
    SmallVector<Type *, 8> ParamTys;
    ParamTys.reserve(Arguments.size());
    for (unsigned Idx = 0, Size = Arguments.size(); Idx != Size; ++Idx)
      ParamTys.push_back(Arguments[Idx]->getType());
    return static_cast<T *>(this)->getIntrinsicCost(IID, RetTy, ParamTys);
  }

  unsigned getUserCost(const User *U, ArrayRef<const Value *> Operands) {
    if (isa<PHINode>(U))
      return TTI::TCC_Free; // Model all PHI nodes as free.

    // Static alloca doesn't generate target instructions.
    if (auto *A = dyn_cast<AllocaInst>(U))
      if (A->isStaticAlloca())
        return TTI::TCC_Free;

    if (const GEPOperator *GEP = dyn_cast<GEPOperator>(U)) {
      return static_cast<T *>(this)->getGEPCost(GEP->getSourceElementType(),
                                                GEP->getPointerOperand(),
                                                Operands.drop_front());
    }

    if (auto CS = ImmutableCallSite(U)) {
      const Function *F = CS.getCalledFunction();
      if (!F) {
        // Just use the called value type.
        Type *FTy = CS.getCalledValue()->getType()->getPointerElementType();
        return static_cast<T *>(this)
            ->getCallCost(cast<FunctionType>(FTy), CS.arg_size());
      }

      SmallVector<const Value *, 8> Arguments(CS.arg_begin(), CS.arg_end());
      return static_cast<T *>(this)->getCallCost(F, Arguments);
    }

    if (const CastInst *CI = dyn_cast<CastInst>(U)) {
      // Result of a cmp instruction is often extended (to be used by other
      // cmp instructions, logical or return instructions). These are usually
      // nop on most sane targets.
      if (isa<CmpInst>(CI->getOperand(0)))
        return TTI::TCC_Free;
      if (isa<SExtInst>(CI) || isa<ZExtInst>(CI) || isa<FPExtInst>(CI))
        return static_cast<T *>(this)->getExtCost(CI, Operands.back());
    }

    return static_cast<T *>(this)->getOperationCost(
        Operator::getOpcode(U), U->getType(),
        U->getNumOperands() == 1 ? U->getOperand(0)->getType() : nullptr);
  }

  int getInstructionLatency(const Instruction *I) {
    SmallVector<const Value *, 4> Operands(I->value_op_begin(),
                                           I->value_op_end());
    if (getUserCost(I, Operands) == TTI::TCC_Free)
      return 0;

    if (isa<LoadInst>(I))
      return 4;

    Type *DstTy = I->getType();

    // Usually an intrinsic is a simple instruction.
    // A real function call is much slower.
    if (auto *CI = dyn_cast<CallInst>(I)) {
      const Function *F = CI->getCalledFunction();
      if (!F || static_cast<T *>(this)->isLoweredToCall(F))
        return 40;
      // Some intrinsics return a value and a flag, we use the value type
      // to decide its latency.
      if (StructType* StructTy = dyn_cast<StructType>(DstTy))
        DstTy = StructTy->getElementType(0);
      // Fall through to simple instructions.
    }

    if (VectorType *VectorTy = dyn_cast<VectorType>(DstTy))
      DstTy = VectorTy->getElementType();
    if (DstTy->isFloatingPointTy())
      return 3;

    return 1;
  }
};
}

#endif
