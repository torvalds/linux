//===- AMDGPUTargetTransformInfo.h - AMDGPU specific TTI --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file a TargetTransformInfo::Concept conforming object specific to the
/// AMDGPU target machine. It uses the target's detailed information to
/// provide more precise answers to certain TTI queries, while letting the
/// target independent and default TTI implementations handle the rest.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUTARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUTARGETTRANSFORMINFO_H

#include "AMDGPU.h"
#include "AMDGPUSubtarget.h"
#include "AMDGPUTargetMachine.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>

namespace llvm {

class AMDGPUTargetLowering;
class Loop;
class ScalarEvolution;
class Type;
class Value;

class AMDGPUTTIImpl final : public BasicTTIImplBase<AMDGPUTTIImpl> {
  using BaseT = BasicTTIImplBase<AMDGPUTTIImpl>;
  using TTI = TargetTransformInfo;

  friend BaseT;

  Triple TargetTriple;

public:
  explicit AMDGPUTTIImpl(const AMDGPUTargetMachine *TM, const Function &F)
    : BaseT(TM, F.getParent()->getDataLayout()),
      TargetTriple(TM->getTargetTriple()) {}

  void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                               TTI::UnrollingPreferences &UP);
};

class GCNTTIImpl final : public BasicTTIImplBase<GCNTTIImpl> {
  using BaseT = BasicTTIImplBase<GCNTTIImpl>;
  using TTI = TargetTransformInfo;

  friend BaseT;

  const GCNSubtarget *ST;
  const AMDGPUTargetLowering *TLI;
  AMDGPUTTIImpl CommonTTI;
  bool IsGraphicsShader;

  const FeatureBitset InlineFeatureIgnoreList = {
    // Codegen control options which don't matter.
    AMDGPU::FeatureEnableLoadStoreOpt,
    AMDGPU::FeatureEnableSIScheduler,
    AMDGPU::FeatureEnableUnsafeDSOffsetFolding,
    AMDGPU::FeatureFlatForGlobal,
    AMDGPU::FeaturePromoteAlloca,
    AMDGPU::FeatureUnalignedBufferAccess,
    AMDGPU::FeatureUnalignedScratchAccess,

    AMDGPU::FeatureAutoWaitcntBeforeBarrier,
    AMDGPU::FeatureDebuggerEmitPrologue,
    AMDGPU::FeatureDebuggerInsertNops,

    // Property of the kernel/environment which can't actually differ.
    AMDGPU::FeatureSGPRInitBug,
    AMDGPU::FeatureXNACK,
    AMDGPU::FeatureTrapHandler,

    // Perf-tuning features
    AMDGPU::FeatureFastFMAF32,
    AMDGPU::HalfRate64Ops
  };

  const GCNSubtarget *getST() const { return ST; }
  const AMDGPUTargetLowering *getTLI() const { return TLI; }

  static inline int getFullRateInstrCost() {
    return TargetTransformInfo::TCC_Basic;
  }

  static inline int getHalfRateInstrCost() {
    return 2 * TargetTransformInfo::TCC_Basic;
  }

  // TODO: The size is usually 8 bytes, but takes 4x as many cycles. Maybe
  // should be 2 or 4.
  static inline int getQuarterRateInstrCost() {
    return 3 * TargetTransformInfo::TCC_Basic;
  }

   // On some parts, normal fp64 operations are half rate, and others
   // quarter. This also applies to some integer operations.
  inline int get64BitInstrCost() const {
    return ST->hasHalfRate64Ops() ?
      getHalfRateInstrCost() : getQuarterRateInstrCost();
  }

public:
  explicit GCNTTIImpl(const AMDGPUTargetMachine *TM, const Function &F)
    : BaseT(TM, F.getParent()->getDataLayout()),
      ST(static_cast<const GCNSubtarget*>(TM->getSubtargetImpl(F))),
      TLI(ST->getTargetLowering()),
      CommonTTI(TM, F),
      IsGraphicsShader(AMDGPU::isShader(F.getCallingConv())) {}

  bool hasBranchDivergence() { return true; }

  void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                               TTI::UnrollingPreferences &UP);

  TTI::PopcntSupportKind getPopcntSupport(unsigned TyWidth) {
    assert(isPowerOf2_32(TyWidth) && "Ty width must be power of 2");
    return TTI::PSK_FastHardware;
  }

  unsigned getHardwareNumberOfRegisters(bool Vector) const;
  unsigned getNumberOfRegisters(bool Vector) const;
  unsigned getRegisterBitWidth(bool Vector) const;
  unsigned getMinVectorRegisterBitWidth() const;
  unsigned getLoadVectorFactor(unsigned VF, unsigned LoadSize,
                               unsigned ChainSizeInBytes,
                               VectorType *VecTy) const;
  unsigned getStoreVectorFactor(unsigned VF, unsigned StoreSize,
                                unsigned ChainSizeInBytes,
                                VectorType *VecTy) const;
  unsigned getLoadStoreVecRegBitWidth(unsigned AddrSpace) const;

  bool isLegalToVectorizeMemChain(unsigned ChainSizeInBytes,
                                  unsigned Alignment,
                                  unsigned AddrSpace) const;
  bool isLegalToVectorizeLoadChain(unsigned ChainSizeInBytes,
                                   unsigned Alignment,
                                   unsigned AddrSpace) const;
  bool isLegalToVectorizeStoreChain(unsigned ChainSizeInBytes,
                                    unsigned Alignment,
                                    unsigned AddrSpace) const;

  unsigned getMaxInterleaveFactor(unsigned VF);

  bool getTgtMemIntrinsic(IntrinsicInst *Inst, MemIntrinsicInfo &Info) const;

  int getArithmeticInstrCost(
    unsigned Opcode, Type *Ty,
    TTI::OperandValueKind Opd1Info = TTI::OK_AnyValue,
    TTI::OperandValueKind Opd2Info = TTI::OK_AnyValue,
    TTI::OperandValueProperties Opd1PropInfo = TTI::OP_None,
    TTI::OperandValueProperties Opd2PropInfo = TTI::OP_None,
    ArrayRef<const Value *> Args = ArrayRef<const Value *>());

  unsigned getCFInstrCost(unsigned Opcode);

  int getVectorInstrCost(unsigned Opcode, Type *ValTy, unsigned Index);
  bool isSourceOfDivergence(const Value *V) const;
  bool isAlwaysUniform(const Value *V) const;

  unsigned getFlatAddressSpace() const {
    // Don't bother running InferAddressSpaces pass on graphics shaders which
    // don't use flat addressing.
    if (IsGraphicsShader)
      return -1;
    return ST->hasFlatAddressSpace() ?
      AMDGPUAS::FLAT_ADDRESS : AMDGPUAS::UNKNOWN_ADDRESS_SPACE;
  }

  unsigned getVectorSplitCost() { return 0; }

  unsigned getShuffleCost(TTI::ShuffleKind Kind, Type *Tp, int Index,
                          Type *SubTp);

  bool areInlineCompatible(const Function *Caller,
                           const Function *Callee) const;

  unsigned getInliningThresholdMultiplier() { return 9; }

  int getArithmeticReductionCost(unsigned Opcode,
                                 Type *Ty,
                                 bool IsPairwise);
  int getMinMaxReductionCost(Type *Ty, Type *CondTy,
                             bool IsPairwiseForm,
                             bool IsUnsigned);
};

class R600TTIImpl final : public BasicTTIImplBase<R600TTIImpl> {
  using BaseT = BasicTTIImplBase<R600TTIImpl>;
  using TTI = TargetTransformInfo;

  friend BaseT;

  const R600Subtarget *ST;
  const AMDGPUTargetLowering *TLI;
  AMDGPUTTIImpl CommonTTI;

public:
  explicit R600TTIImpl(const AMDGPUTargetMachine *TM, const Function &F)
    : BaseT(TM, F.getParent()->getDataLayout()),
      ST(static_cast<const R600Subtarget*>(TM->getSubtargetImpl(F))),
      TLI(ST->getTargetLowering()),
      CommonTTI(TM, F)	{}

  const R600Subtarget *getST() const { return ST; }
  const AMDGPUTargetLowering *getTLI() const { return TLI; }

  void getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                               TTI::UnrollingPreferences &UP);
  unsigned getHardwareNumberOfRegisters(bool Vec) const;
  unsigned getNumberOfRegisters(bool Vec) const;
  unsigned getRegisterBitWidth(bool Vector) const;
  unsigned getMinVectorRegisterBitWidth() const;
  unsigned getLoadStoreVecRegBitWidth(unsigned AddrSpace) const;
  bool isLegalToVectorizeMemChain(unsigned ChainSizeInBytes, unsigned Alignment,
                                  unsigned AddrSpace) const;
  bool isLegalToVectorizeLoadChain(unsigned ChainSizeInBytes,
		                   unsigned Alignment,
                                   unsigned AddrSpace) const;
  bool isLegalToVectorizeStoreChain(unsigned ChainSizeInBytes,
                                    unsigned Alignment,
                                    unsigned AddrSpace) const;
  unsigned getMaxInterleaveFactor(unsigned VF);
  unsigned getCFInstrCost(unsigned Opcode);
  int getVectorInstrCost(unsigned Opcode, Type *ValTy, unsigned Index);
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_AMDGPUTARGETTRANSFORMINFO_H
