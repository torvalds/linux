//===-- X86TargetTransformInfo.h - X86 specific TTI -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file a TargetTransformInfo::Concept conforming object specific to the
/// X86 target machine. It uses the target's detailed information to
/// provide more precise answers to certain TTI queries, while letting the
/// target independent and default TTI implementations handle the rest.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86TARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_X86_X86TARGETTRANSFORMINFO_H

#include "X86.h"
#include "X86TargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {

class X86TTIImpl : public BasicTTIImplBase<X86TTIImpl> {
  typedef BasicTTIImplBase<X86TTIImpl> BaseT;
  typedef TargetTransformInfo TTI;
  friend BaseT;

  const X86Subtarget *ST;
  const X86TargetLowering *TLI;

  const X86Subtarget *getST() const { return ST; }
  const X86TargetLowering *getTLI() const { return TLI; }

public:
  explicit X86TTIImpl(const X86TargetMachine *TM, const Function &F)
      : BaseT(TM, F.getParent()->getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}

  /// \name Scalar TTI Implementations
  /// @{
  TTI::PopcntSupportKind getPopcntSupport(unsigned TyWidth);

  /// @}

  /// \name Cache TTI Implementation
  /// @{
  llvm::Optional<unsigned> getCacheSize(
    TargetTransformInfo::CacheLevel Level) const;
  llvm::Optional<unsigned> getCacheAssociativity(
    TargetTransformInfo::CacheLevel Level) const;
  /// @}

  /// \name Vector TTI Implementations
  /// @{

  unsigned getNumberOfRegisters(bool Vector);
  unsigned getRegisterBitWidth(bool Vector) const;
  unsigned getLoadStoreVecRegBitWidth(unsigned AS) const;
  unsigned getMaxInterleaveFactor(unsigned VF);
  int getArithmeticInstrCost(
      unsigned Opcode, Type *Ty,
      TTI::OperandValueKind Opd1Info = TTI::OK_AnyValue,
      TTI::OperandValueKind Opd2Info = TTI::OK_AnyValue,
      TTI::OperandValueProperties Opd1PropInfo = TTI::OP_None,
      TTI::OperandValueProperties Opd2PropInfo = TTI::OP_None,
      ArrayRef<const Value *> Args = ArrayRef<const Value *>());
  int getShuffleCost(TTI::ShuffleKind Kind, Type *Tp, int Index, Type *SubTp);
  int getCastInstrCost(unsigned Opcode, Type *Dst, Type *Src,
                       const Instruction *I = nullptr);
  int getCmpSelInstrCost(unsigned Opcode, Type *ValTy, Type *CondTy,
                         const Instruction *I = nullptr);
  int getVectorInstrCost(unsigned Opcode, Type *Val, unsigned Index);
  int getMemoryOpCost(unsigned Opcode, Type *Src, unsigned Alignment,
                      unsigned AddressSpace, const Instruction *I = nullptr);
  int getMaskedMemoryOpCost(unsigned Opcode, Type *Src, unsigned Alignment,
                            unsigned AddressSpace);
  int getGatherScatterOpCost(unsigned Opcode, Type *DataTy, Value *Ptr,
                             bool VariableMask, unsigned Alignment);
  int getAddressComputationCost(Type *PtrTy, ScalarEvolution *SE,
                                const SCEV *Ptr);

  unsigned getAtomicMemIntrinsicMaxElementSize() const;

  int getIntrinsicInstrCost(Intrinsic::ID IID, Type *RetTy,
                            ArrayRef<Type *> Tys, FastMathFlags FMF,
                            unsigned ScalarizationCostPassed = UINT_MAX);
  int getIntrinsicInstrCost(Intrinsic::ID IID, Type *RetTy,
                            ArrayRef<Value *> Args, FastMathFlags FMF,
                            unsigned VF = 1);

  int getArithmeticReductionCost(unsigned Opcode, Type *Ty,
                                 bool IsPairwiseForm);

  int getMinMaxReductionCost(Type *Ty, Type *CondTy, bool IsPairwiseForm,
                             bool IsUnsigned);

  int getInterleavedMemoryOpCost(unsigned Opcode, Type *VecTy,
                                 unsigned Factor, ArrayRef<unsigned> Indices,
                                 unsigned Alignment, unsigned AddressSpace,
                                 bool UseMaskForCond = false,
                                 bool UseMaskForGaps = false);
  int getInterleavedMemoryOpCostAVX512(unsigned Opcode, Type *VecTy,
                                 unsigned Factor, ArrayRef<unsigned> Indices,
                                 unsigned Alignment, unsigned AddressSpace,
                                 bool UseMaskForCond = false,
                                 bool UseMaskForGaps = false);
  int getInterleavedMemoryOpCostAVX2(unsigned Opcode, Type *VecTy,
                                 unsigned Factor, ArrayRef<unsigned> Indices,
                                 unsigned Alignment, unsigned AddressSpace,
                                 bool UseMaskForCond = false,
                                 bool UseMaskForGaps = false);

  int getIntImmCost(int64_t);

  int getIntImmCost(const APInt &Imm, Type *Ty);

  unsigned getUserCost(const User *U, ArrayRef<const Value *> Operands);

  int getIntImmCost(unsigned Opcode, unsigned Idx, const APInt &Imm, Type *Ty);
  int getIntImmCost(Intrinsic::ID IID, unsigned Idx, const APInt &Imm,
                    Type *Ty);
  bool isLSRCostLess(TargetTransformInfo::LSRCost &C1,
                     TargetTransformInfo::LSRCost &C2);
  bool canMacroFuseCmp();
  bool isLegalMaskedLoad(Type *DataType);
  bool isLegalMaskedStore(Type *DataType);
  bool isLegalMaskedGather(Type *DataType);
  bool isLegalMaskedScatter(Type *DataType);
  bool hasDivRemOp(Type *DataType, bool IsSigned);
  bool isFCmpOrdCheaperThanFCmpZero(Type *Ty);
  bool areInlineCompatible(const Function *Caller,
                           const Function *Callee) const;
  const TTI::MemCmpExpansionOptions *enableMemCmpExpansion(
      bool IsZeroCmp) const;
  bool enableInterleavedAccessVectorization();
private:
  int getGSScalarCost(unsigned Opcode, Type *DataTy, bool VariableMask,
                      unsigned Alignment, unsigned AddressSpace);
  int getGSVectorCost(unsigned Opcode, Type *DataTy, Value *Ptr,
                      unsigned Alignment, unsigned AddressSpace);

  /// @}
};

} // end namespace llvm

#endif
