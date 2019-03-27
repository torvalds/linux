//===-- NVPTXISelLowering.h - NVPTX DAG Lowering Interface ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that NVPTX uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_NVPTXISELLOWERING_H
#define LLVM_LIB_TARGET_NVPTX_NVPTXISELLOWERING_H

#include "NVPTX.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {
namespace NVPTXISD {
enum NodeType : unsigned {
  // Start the numbering from where ISD NodeType finishes.
  FIRST_NUMBER = ISD::BUILTIN_OP_END,
  Wrapper,
  CALL,
  RET_FLAG,
  LOAD_PARAM,
  DeclareParam,
  DeclareScalarParam,
  DeclareRetParam,
  DeclareRet,
  DeclareScalarRet,
  PrintCall,
  PrintConvergentCall,
  PrintCallUni,
  PrintConvergentCallUni,
  CallArgBegin,
  CallArg,
  LastCallArg,
  CallArgEnd,
  CallVoid,
  CallVal,
  CallSymbol,
  Prototype,
  MoveParam,
  PseudoUseParam,
  RETURN,
  CallSeqBegin,
  CallSeqEnd,
  CallPrototype,
  ProxyReg,
  FUN_SHFL_CLAMP,
  FUN_SHFR_CLAMP,
  MUL_WIDE_SIGNED,
  MUL_WIDE_UNSIGNED,
  IMAD,
  SETP_F16X2,
  Dummy,

  LoadV2 = ISD::FIRST_TARGET_MEMORY_OPCODE,
  LoadV4,
  LDGV2, // LDG.v2
  LDGV4, // LDG.v4
  LDUV2, // LDU.v2
  LDUV4, // LDU.v4
  StoreV2,
  StoreV4,
  LoadParam,
  LoadParamV2,
  LoadParamV4,
  StoreParam,
  StoreParamV2,
  StoreParamV4,
  StoreParamS32, // to sext and store a <32bit value, not used currently
  StoreParamU32, // to zext and store a <32bit value, not used currently
  StoreRetval,
  StoreRetvalV2,
  StoreRetvalV4,

  // Texture intrinsics
  Tex1DFloatS32,
  Tex1DFloatFloat,
  Tex1DFloatFloatLevel,
  Tex1DFloatFloatGrad,
  Tex1DS32S32,
  Tex1DS32Float,
  Tex1DS32FloatLevel,
  Tex1DS32FloatGrad,
  Tex1DU32S32,
  Tex1DU32Float,
  Tex1DU32FloatLevel,
  Tex1DU32FloatGrad,
  Tex1DArrayFloatS32,
  Tex1DArrayFloatFloat,
  Tex1DArrayFloatFloatLevel,
  Tex1DArrayFloatFloatGrad,
  Tex1DArrayS32S32,
  Tex1DArrayS32Float,
  Tex1DArrayS32FloatLevel,
  Tex1DArrayS32FloatGrad,
  Tex1DArrayU32S32,
  Tex1DArrayU32Float,
  Tex1DArrayU32FloatLevel,
  Tex1DArrayU32FloatGrad,
  Tex2DFloatS32,
  Tex2DFloatFloat,
  Tex2DFloatFloatLevel,
  Tex2DFloatFloatGrad,
  Tex2DS32S32,
  Tex2DS32Float,
  Tex2DS32FloatLevel,
  Tex2DS32FloatGrad,
  Tex2DU32S32,
  Tex2DU32Float,
  Tex2DU32FloatLevel,
  Tex2DU32FloatGrad,
  Tex2DArrayFloatS32,
  Tex2DArrayFloatFloat,
  Tex2DArrayFloatFloatLevel,
  Tex2DArrayFloatFloatGrad,
  Tex2DArrayS32S32,
  Tex2DArrayS32Float,
  Tex2DArrayS32FloatLevel,
  Tex2DArrayS32FloatGrad,
  Tex2DArrayU32S32,
  Tex2DArrayU32Float,
  Tex2DArrayU32FloatLevel,
  Tex2DArrayU32FloatGrad,
  Tex3DFloatS32,
  Tex3DFloatFloat,
  Tex3DFloatFloatLevel,
  Tex3DFloatFloatGrad,
  Tex3DS32S32,
  Tex3DS32Float,
  Tex3DS32FloatLevel,
  Tex3DS32FloatGrad,
  Tex3DU32S32,
  Tex3DU32Float,
  Tex3DU32FloatLevel,
  Tex3DU32FloatGrad,
  TexCubeFloatFloat,
  TexCubeFloatFloatLevel,
  TexCubeS32Float,
  TexCubeS32FloatLevel,
  TexCubeU32Float,
  TexCubeU32FloatLevel,
  TexCubeArrayFloatFloat,
  TexCubeArrayFloatFloatLevel,
  TexCubeArrayS32Float,
  TexCubeArrayS32FloatLevel,
  TexCubeArrayU32Float,
  TexCubeArrayU32FloatLevel,
  Tld4R2DFloatFloat,
  Tld4G2DFloatFloat,
  Tld4B2DFloatFloat,
  Tld4A2DFloatFloat,
  Tld4R2DS64Float,
  Tld4G2DS64Float,
  Tld4B2DS64Float,
  Tld4A2DS64Float,
  Tld4R2DU64Float,
  Tld4G2DU64Float,
  Tld4B2DU64Float,
  Tld4A2DU64Float,
  TexUnified1DFloatS32,
  TexUnified1DFloatFloat,
  TexUnified1DFloatFloatLevel,
  TexUnified1DFloatFloatGrad,
  TexUnified1DS32S32,
  TexUnified1DS32Float,
  TexUnified1DS32FloatLevel,
  TexUnified1DS32FloatGrad,
  TexUnified1DU32S32,
  TexUnified1DU32Float,
  TexUnified1DU32FloatLevel,
  TexUnified1DU32FloatGrad,
  TexUnified1DArrayFloatS32,
  TexUnified1DArrayFloatFloat,
  TexUnified1DArrayFloatFloatLevel,
  TexUnified1DArrayFloatFloatGrad,
  TexUnified1DArrayS32S32,
  TexUnified1DArrayS32Float,
  TexUnified1DArrayS32FloatLevel,
  TexUnified1DArrayS32FloatGrad,
  TexUnified1DArrayU32S32,
  TexUnified1DArrayU32Float,
  TexUnified1DArrayU32FloatLevel,
  TexUnified1DArrayU32FloatGrad,
  TexUnified2DFloatS32,
  TexUnified2DFloatFloat,
  TexUnified2DFloatFloatLevel,
  TexUnified2DFloatFloatGrad,
  TexUnified2DS32S32,
  TexUnified2DS32Float,
  TexUnified2DS32FloatLevel,
  TexUnified2DS32FloatGrad,
  TexUnified2DU32S32,
  TexUnified2DU32Float,
  TexUnified2DU32FloatLevel,
  TexUnified2DU32FloatGrad,
  TexUnified2DArrayFloatS32,
  TexUnified2DArrayFloatFloat,
  TexUnified2DArrayFloatFloatLevel,
  TexUnified2DArrayFloatFloatGrad,
  TexUnified2DArrayS32S32,
  TexUnified2DArrayS32Float,
  TexUnified2DArrayS32FloatLevel,
  TexUnified2DArrayS32FloatGrad,
  TexUnified2DArrayU32S32,
  TexUnified2DArrayU32Float,
  TexUnified2DArrayU32FloatLevel,
  TexUnified2DArrayU32FloatGrad,
  TexUnified3DFloatS32,
  TexUnified3DFloatFloat,
  TexUnified3DFloatFloatLevel,
  TexUnified3DFloatFloatGrad,
  TexUnified3DS32S32,
  TexUnified3DS32Float,
  TexUnified3DS32FloatLevel,
  TexUnified3DS32FloatGrad,
  TexUnified3DU32S32,
  TexUnified3DU32Float,
  TexUnified3DU32FloatLevel,
  TexUnified3DU32FloatGrad,
  TexUnifiedCubeFloatFloat,
  TexUnifiedCubeFloatFloatLevel,
  TexUnifiedCubeS32Float,
  TexUnifiedCubeS32FloatLevel,
  TexUnifiedCubeU32Float,
  TexUnifiedCubeU32FloatLevel,
  TexUnifiedCubeArrayFloatFloat,
  TexUnifiedCubeArrayFloatFloatLevel,
  TexUnifiedCubeArrayS32Float,
  TexUnifiedCubeArrayS32FloatLevel,
  TexUnifiedCubeArrayU32Float,
  TexUnifiedCubeArrayU32FloatLevel,
  Tld4UnifiedR2DFloatFloat,
  Tld4UnifiedG2DFloatFloat,
  Tld4UnifiedB2DFloatFloat,
  Tld4UnifiedA2DFloatFloat,
  Tld4UnifiedR2DS64Float,
  Tld4UnifiedG2DS64Float,
  Tld4UnifiedB2DS64Float,
  Tld4UnifiedA2DS64Float,
  Tld4UnifiedR2DU64Float,
  Tld4UnifiedG2DU64Float,
  Tld4UnifiedB2DU64Float,
  Tld4UnifiedA2DU64Float,

  // Surface intrinsics
  Suld1DI8Clamp,
  Suld1DI16Clamp,
  Suld1DI32Clamp,
  Suld1DI64Clamp,
  Suld1DV2I8Clamp,
  Suld1DV2I16Clamp,
  Suld1DV2I32Clamp,
  Suld1DV2I64Clamp,
  Suld1DV4I8Clamp,
  Suld1DV4I16Clamp,
  Suld1DV4I32Clamp,

  Suld1DArrayI8Clamp,
  Suld1DArrayI16Clamp,
  Suld1DArrayI32Clamp,
  Suld1DArrayI64Clamp,
  Suld1DArrayV2I8Clamp,
  Suld1DArrayV2I16Clamp,
  Suld1DArrayV2I32Clamp,
  Suld1DArrayV2I64Clamp,
  Suld1DArrayV4I8Clamp,
  Suld1DArrayV4I16Clamp,
  Suld1DArrayV4I32Clamp,

  Suld2DI8Clamp,
  Suld2DI16Clamp,
  Suld2DI32Clamp,
  Suld2DI64Clamp,
  Suld2DV2I8Clamp,
  Suld2DV2I16Clamp,
  Suld2DV2I32Clamp,
  Suld2DV2I64Clamp,
  Suld2DV4I8Clamp,
  Suld2DV4I16Clamp,
  Suld2DV4I32Clamp,

  Suld2DArrayI8Clamp,
  Suld2DArrayI16Clamp,
  Suld2DArrayI32Clamp,
  Suld2DArrayI64Clamp,
  Suld2DArrayV2I8Clamp,
  Suld2DArrayV2I16Clamp,
  Suld2DArrayV2I32Clamp,
  Suld2DArrayV2I64Clamp,
  Suld2DArrayV4I8Clamp,
  Suld2DArrayV4I16Clamp,
  Suld2DArrayV4I32Clamp,

  Suld3DI8Clamp,
  Suld3DI16Clamp,
  Suld3DI32Clamp,
  Suld3DI64Clamp,
  Suld3DV2I8Clamp,
  Suld3DV2I16Clamp,
  Suld3DV2I32Clamp,
  Suld3DV2I64Clamp,
  Suld3DV4I8Clamp,
  Suld3DV4I16Clamp,
  Suld3DV4I32Clamp,

  Suld1DI8Trap,
  Suld1DI16Trap,
  Suld1DI32Trap,
  Suld1DI64Trap,
  Suld1DV2I8Trap,
  Suld1DV2I16Trap,
  Suld1DV2I32Trap,
  Suld1DV2I64Trap,
  Suld1DV4I8Trap,
  Suld1DV4I16Trap,
  Suld1DV4I32Trap,

  Suld1DArrayI8Trap,
  Suld1DArrayI16Trap,
  Suld1DArrayI32Trap,
  Suld1DArrayI64Trap,
  Suld1DArrayV2I8Trap,
  Suld1DArrayV2I16Trap,
  Suld1DArrayV2I32Trap,
  Suld1DArrayV2I64Trap,
  Suld1DArrayV4I8Trap,
  Suld1DArrayV4I16Trap,
  Suld1DArrayV4I32Trap,

  Suld2DI8Trap,
  Suld2DI16Trap,
  Suld2DI32Trap,
  Suld2DI64Trap,
  Suld2DV2I8Trap,
  Suld2DV2I16Trap,
  Suld2DV2I32Trap,
  Suld2DV2I64Trap,
  Suld2DV4I8Trap,
  Suld2DV4I16Trap,
  Suld2DV4I32Trap,

  Suld2DArrayI8Trap,
  Suld2DArrayI16Trap,
  Suld2DArrayI32Trap,
  Suld2DArrayI64Trap,
  Suld2DArrayV2I8Trap,
  Suld2DArrayV2I16Trap,
  Suld2DArrayV2I32Trap,
  Suld2DArrayV2I64Trap,
  Suld2DArrayV4I8Trap,
  Suld2DArrayV4I16Trap,
  Suld2DArrayV4I32Trap,

  Suld3DI8Trap,
  Suld3DI16Trap,
  Suld3DI32Trap,
  Suld3DI64Trap,
  Suld3DV2I8Trap,
  Suld3DV2I16Trap,
  Suld3DV2I32Trap,
  Suld3DV2I64Trap,
  Suld3DV4I8Trap,
  Suld3DV4I16Trap,
  Suld3DV4I32Trap,

  Suld1DI8Zero,
  Suld1DI16Zero,
  Suld1DI32Zero,
  Suld1DI64Zero,
  Suld1DV2I8Zero,
  Suld1DV2I16Zero,
  Suld1DV2I32Zero,
  Suld1DV2I64Zero,
  Suld1DV4I8Zero,
  Suld1DV4I16Zero,
  Suld1DV4I32Zero,

  Suld1DArrayI8Zero,
  Suld1DArrayI16Zero,
  Suld1DArrayI32Zero,
  Suld1DArrayI64Zero,
  Suld1DArrayV2I8Zero,
  Suld1DArrayV2I16Zero,
  Suld1DArrayV2I32Zero,
  Suld1DArrayV2I64Zero,
  Suld1DArrayV4I8Zero,
  Suld1DArrayV4I16Zero,
  Suld1DArrayV4I32Zero,

  Suld2DI8Zero,
  Suld2DI16Zero,
  Suld2DI32Zero,
  Suld2DI64Zero,
  Suld2DV2I8Zero,
  Suld2DV2I16Zero,
  Suld2DV2I32Zero,
  Suld2DV2I64Zero,
  Suld2DV4I8Zero,
  Suld2DV4I16Zero,
  Suld2DV4I32Zero,

  Suld2DArrayI8Zero,
  Suld2DArrayI16Zero,
  Suld2DArrayI32Zero,
  Suld2DArrayI64Zero,
  Suld2DArrayV2I8Zero,
  Suld2DArrayV2I16Zero,
  Suld2DArrayV2I32Zero,
  Suld2DArrayV2I64Zero,
  Suld2DArrayV4I8Zero,
  Suld2DArrayV4I16Zero,
  Suld2DArrayV4I32Zero,

  Suld3DI8Zero,
  Suld3DI16Zero,
  Suld3DI32Zero,
  Suld3DI64Zero,
  Suld3DV2I8Zero,
  Suld3DV2I16Zero,
  Suld3DV2I32Zero,
  Suld3DV2I64Zero,
  Suld3DV4I8Zero,
  Suld3DV4I16Zero,
  Suld3DV4I32Zero
};
}

class NVPTXSubtarget;

//===--------------------------------------------------------------------===//
// TargetLowering Implementation
//===--------------------------------------------------------------------===//
class NVPTXTargetLowering : public TargetLowering {
public:
  explicit NVPTXTargetLowering(const NVPTXTargetMachine &TM,
                               const NVPTXSubtarget &STI);
  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

  SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;

  const char *getTargetNodeName(unsigned Opcode) const override;

  bool getTgtMemIntrinsic(IntrinsicInfo &Info, const CallInst &I,
                          MachineFunction &MF,
                          unsigned Intrinsic) const override;

  /// isLegalAddressingMode - Return true if the addressing mode represented
  /// by AM is legal for this target, for a load/store of the specified type
  /// Used to guide target specific optimizations, like loop strength
  /// reduction (LoopStrengthReduce.cpp) and memory optimization for
  /// address mode (CodeGenPrepare.cpp)
  bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM, Type *Ty,
                             unsigned AS,
                             Instruction *I = nullptr) const override;

  bool isTruncateFree(Type *SrcTy, Type *DstTy) const override {
    // Truncating 64-bit to 32-bit is free in SASS.
    if (!SrcTy->isIntegerTy() || !DstTy->isIntegerTy())
      return false;
    return SrcTy->getPrimitiveSizeInBits() == 64 &&
           DstTy->getPrimitiveSizeInBits() == 32;
  }

  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Ctx,
                         EVT VT) const override {
    if (VT.isVector())
      return EVT::getVectorVT(Ctx, MVT::i1, VT.getVectorNumElements());
    return MVT::i1;
  }

  ConstraintType getConstraintType(StringRef Constraint) const override;
  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;

  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool isVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &dl, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerCall(CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;

  std::string getPrototype(const DataLayout &DL, Type *, const ArgListTy &,
                           const SmallVectorImpl<ISD::OutputArg> &,
                           unsigned retAlignment,
                           ImmutableCallSite CS) const;

  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &dl,
                      SelectionDAG &DAG) const override;

  void LowerAsmOperandForConstraint(SDValue Op, std::string &Constraint,
                                    std::vector<SDValue> &Ops,
                                    SelectionDAG &DAG) const override;

  const NVPTXTargetMachine *nvTM;

  // PTX always uses 32-bit shift amounts
  MVT getScalarShiftAmountTy(const DataLayout &, EVT) const override {
    return MVT::i32;
  }

  TargetLoweringBase::LegalizeTypeAction
  getPreferredVectorAction(MVT VT) const override;

  // Get the degree of precision we want from 32-bit floating point division
  // operations.
  //
  //  0 - Use ptx div.approx
  //  1 - Use ptx.div.full (approximate, but less so than div.approx)
  //  2 - Use IEEE-compliant div instructions, if available.
  int getDivF32Level() const;

  // Get whether we should use a precise or approximate 32-bit floating point
  // sqrt instruction.
  bool usePrecSqrtF32() const;

  // Get whether we should use instructions that flush floating-point denormals
  // to sign-preserving zero.
  bool useF32FTZ(const MachineFunction &MF) const;

  SDValue getSqrtEstimate(SDValue Operand, SelectionDAG &DAG, int Enabled,
                          int &ExtraSteps, bool &UseOneConst,
                          bool Reciprocal) const override;

  unsigned combineRepeatedFPDivisors() const override { return 2; }

  bool allowFMA(MachineFunction &MF, CodeGenOpt::Level OptLevel) const;
  bool allowUnsafeFPMath(MachineFunction &MF) const;

  bool isFMAFasterThanFMulAndFAdd(EVT) const override { return true; }

  bool enableAggressiveFMAFusion(EVT VT) const override { return true; }

  // The default is to transform llvm.ctlz(x, false) (where false indicates that
  // x == 0 is not undefined behavior) into a branch that checks whether x is 0
  // and avoids calling ctlz in that case.  We have a dedicated ctlz
  // instruction, so we say that ctlz is cheap to speculate.
  bool isCheapToSpeculateCtlz() const override { return true; }

private:
  const NVPTXSubtarget &STI; // cache the subtarget here
  SDValue getParamSymbol(SelectionDAG &DAG, int idx, EVT) const;

  SDValue LowerBUILD_VECTOR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerCONCAT_VECTORS(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerEXTRACT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerLOAD(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerLOADi1(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerSTORE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSTOREi1(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSTOREVector(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerShiftRightParts(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerShiftLeftParts(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerSelect(SDValue Op, SelectionDAG &DAG) const;

  void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const override;
  SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;

  unsigned getArgumentAlignment(SDValue Callee, ImmutableCallSite CS, Type *Ty,
                                unsigned Idx, const DataLayout &DL) const;
};
} // namespace llvm

#endif
