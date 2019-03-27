//===-- AMDGPUISelLowering.h - AMDGPU Lowering Interface --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Interface definition of the TargetLowering class that is common
/// to all AMD GPUs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUISELLOWERING_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUISELLOWERING_H

#include "AMDGPU.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {

class AMDGPUMachineFunction;
class AMDGPUSubtarget;
struct ArgDescriptor;

class AMDGPUTargetLowering : public TargetLowering {
private:
  const AMDGPUSubtarget *Subtarget;

  /// \returns AMDGPUISD::FFBH_U32 node if the incoming \p Op may have been
  /// legalized from a smaller type VT. Need to match pre-legalized type because
  /// the generic legalization inserts the add/sub between the select and
  /// compare.
  SDValue getFFBX_U32(SelectionDAG &DAG, SDValue Op, const SDLoc &DL, unsigned Opc) const;

public:
  static unsigned numBitsUnsigned(SDValue Op, SelectionDAG &DAG);
  static unsigned numBitsSigned(SDValue Op, SelectionDAG &DAG);

protected:
  SDValue LowerEXTRACT_SUBVECTOR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerCONCAT_VECTORS(SDValue Op, SelectionDAG &DAG) const;
  /// Split a vector store into multiple scalar stores.
  /// \returns The resulting chain.

  SDValue LowerFREM(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFCEIL(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFTRUNC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFRINT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFNEARBYINT(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerFROUND32_16(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFROUND64(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFROUND(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFFLOOR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFLOG(SDValue Op, SelectionDAG &DAG,
                    double Log2BaseInverted) const;
  SDValue lowerFEXP(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerCTLZ_CTTZ(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerINT_TO_FP32(SDValue Op, SelectionDAG &DAG, bool Signed) const;
  SDValue LowerINT_TO_FP64(SDValue Op, SelectionDAG &DAG, bool Signed) const;
  SDValue LowerUINT_TO_FP(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSINT_TO_FP(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerFP64_TO_INT(SDValue Op, SelectionDAG &DAG, bool Signed) const;
  SDValue LowerFP_TO_FP16(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFP_TO_UINT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFP_TO_SINT(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerSIGN_EXTEND_INREG(SDValue Op, SelectionDAG &DAG) const;

protected:
  bool shouldCombineMemoryType(EVT VT) const;
  SDValue performLoadCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performStoreCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performAssertSZExtCombine(SDNode *N, DAGCombinerInfo &DCI) const;

  SDValue splitBinaryBitConstantOpImpl(DAGCombinerInfo &DCI, const SDLoc &SL,
                                       unsigned Opc, SDValue LHS,
                                       uint32_t ValLo, uint32_t ValHi) const;
  SDValue performShlCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performSraCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performSrlCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performTruncateCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performMulCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performMulhsCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performMulhuCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performMulLoHi24Combine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performCtlz_CttzCombine(const SDLoc &SL, SDValue Cond, SDValue LHS,
                             SDValue RHS, DAGCombinerInfo &DCI) const;
  SDValue performSelectCombine(SDNode *N, DAGCombinerInfo &DCI) const;

  bool isConstantCostlierToNegate(SDValue N) const;
  SDValue performFNegCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performFAbsCombine(SDNode *N, DAGCombinerInfo &DCI) const;
  SDValue performRcpCombine(SDNode *N, DAGCombinerInfo &DCI) const;

  static EVT getEquivalentMemType(LLVMContext &Context, EVT VT);

  virtual SDValue LowerGlobalAddress(AMDGPUMachineFunction *MFI, SDValue Op,
                                     SelectionDAG &DAG) const;

  /// Return 64-bit value Op as two 32-bit integers.
  std::pair<SDValue, SDValue> split64BitValue(SDValue Op,
                                              SelectionDAG &DAG) const;
  SDValue getLoHalf64(SDValue Op, SelectionDAG &DAG) const;
  SDValue getHiHalf64(SDValue Op, SelectionDAG &DAG) const;

  /// Split a vector load into 2 loads of half the vector.
  SDValue SplitVectorLoad(SDValue Op, SelectionDAG &DAG) const;

  /// Split a vector store into 2 stores of half the vector.
  SDValue SplitVectorStore(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerSTORE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSDIVREM(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerUDIVREM(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerDIVREM24(SDValue Op, SelectionDAG &DAG, bool sign) const;
  void LowerUDIVREM64(SDValue Op, SelectionDAG &DAG,
                                    SmallVectorImpl<SDValue> &Results) const;

  void analyzeFormalArgumentsCompute(
    CCState &State,
    const SmallVectorImpl<ISD::InputArg> &Ins) const;

public:
  AMDGPUTargetLowering(const TargetMachine &TM, const AMDGPUSubtarget &STI);

  bool mayIgnoreSignedZero(SDValue Op) const {
    if (getTargetMachine().Options.NoSignedZerosFPMath)
      return true;

    const auto Flags = Op.getNode()->getFlags();
    if (Flags.isDefined())
      return Flags.hasNoSignedZeros();

    return false;
  }

  static inline SDValue stripBitcast(SDValue Val) {
    return Val.getOpcode() == ISD::BITCAST ? Val.getOperand(0) : Val;
  }

  static bool allUsesHaveSourceMods(const SDNode *N,
                                    unsigned CostThreshold = 4);
  bool isFAbsFree(EVT VT) const override;
  bool isFNegFree(EVT VT) const override;
  bool isTruncateFree(EVT Src, EVT Dest) const override;
  bool isTruncateFree(Type *Src, Type *Dest) const override;

  bool isZExtFree(Type *Src, Type *Dest) const override;
  bool isZExtFree(EVT Src, EVT Dest) const override;
  bool isZExtFree(SDValue Val, EVT VT2) const override;

  bool isNarrowingProfitable(EVT VT1, EVT VT2) const override;

  MVT getVectorIdxTy(const DataLayout &) const override;
  bool isSelectSupported(SelectSupportKind) const override;

  bool isFPImmLegal(const APFloat &Imm, EVT VT) const override;
  bool ShouldShrinkFPConstant(EVT VT) const override;
  bool shouldReduceLoadWidth(SDNode *Load,
                             ISD::LoadExtType ExtType,
                             EVT ExtVT) const override;

  bool isLoadBitCastBeneficial(EVT, EVT) const final;

  bool storeOfVectorConstantIsCheap(EVT MemVT,
                                    unsigned NumElem,
                                    unsigned AS) const override;
  bool aggressivelyPreferBuildVectorSources(EVT VecVT) const override;
  bool isCheapToSpeculateCttz() const override;
  bool isCheapToSpeculateCtlz() const override;

  bool isSDNodeAlwaysUniform(const SDNode *N) const override;
  static CCAssignFn *CCAssignFnForCall(CallingConv::ID CC, bool IsVarArg);
  static CCAssignFn *CCAssignFnForReturn(CallingConv::ID CC, bool IsVarArg);

  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
                      SelectionDAG &DAG) const override;

  SDValue addTokenForArgument(SDValue Chain,
                              SelectionDAG &DAG,
                              MachineFrameInfo &MFI,
                              int ClobberedFI) const;

  SDValue lowerUnhandledCall(CallLoweringInfo &CLI,
                             SmallVectorImpl<SDValue> &InVals,
                             StringRef Reason) const;
  SDValue LowerCall(CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerDYNAMIC_STACKALLOC(SDValue Op,
                                  SelectionDAG &DAG) const;

  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
  SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;
  void ReplaceNodeResults(SDNode * N,
                          SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const override;

  SDValue combineFMinMaxLegacy(const SDLoc &DL, EVT VT, SDValue LHS,
                               SDValue RHS, SDValue True, SDValue False,
                               SDValue CC, DAGCombinerInfo &DCI) const;

  const char* getTargetNodeName(unsigned Opcode) const override;

  // FIXME: Turn off MergeConsecutiveStores() before Instruction Selection
  // for AMDGPU.
  // A commit ( git-svn-id: https://llvm.org/svn/llvm-project/llvm/trunk@319036
  // 91177308-0d34-0410-b5e6-96231b3b80d8 ) turned on
  // MergeConsecutiveStores() before Instruction Selection for all targets.
  // Enough AMDGPU compiles go into an infinite loop ( MergeConsecutiveStores()
  // merges two stores; LegalizeStoreOps() un-merges; MergeConsecutiveStores()
  // re-merges, etc. ) to warrant turning it off for now.
  bool mergeStoresAfterLegalization() const override { return false; }

  bool isFsqrtCheap(SDValue Operand, SelectionDAG &DAG) const override {
    return true;
  }
  SDValue getSqrtEstimate(SDValue Operand, SelectionDAG &DAG, int Enabled,
                           int &RefinementSteps, bool &UseOneConstNR,
                           bool Reciprocal) const override;
  SDValue getRecipEstimate(SDValue Operand, SelectionDAG &DAG, int Enabled,
                           int &RefinementSteps) const override;

  virtual SDNode *PostISelFolding(MachineSDNode *N,
                                  SelectionDAG &DAG) const = 0;

  /// Determine which of the bits specified in \p Mask are known to be
  /// either zero or one and return them in the \p KnownZero and \p KnownOne
  /// bitsets.
  void computeKnownBitsForTargetNode(const SDValue Op,
                                     KnownBits &Known,
                                     const APInt &DemandedElts,
                                     const SelectionDAG &DAG,
                                     unsigned Depth = 0) const override;

  unsigned ComputeNumSignBitsForTargetNode(SDValue Op, const APInt &DemandedElts,
                                           const SelectionDAG &DAG,
                                           unsigned Depth = 0) const override;

  bool isKnownNeverNaNForTargetNode(SDValue Op,
                                    const SelectionDAG &DAG,
                                    bool SNaN = false,
                                    unsigned Depth = 0) const override;

  /// Helper function that adds Reg to the LiveIn list of the DAG's
  /// MachineFunction.
  ///
  /// \returns a RegisterSDNode representing Reg if \p RawReg is true, otherwise
  /// a copy from the register.
  SDValue CreateLiveInRegister(SelectionDAG &DAG,
                               const TargetRegisterClass *RC,
                               unsigned Reg, EVT VT,
                               const SDLoc &SL,
                               bool RawReg = false) const;
  SDValue CreateLiveInRegister(SelectionDAG &DAG,
                               const TargetRegisterClass *RC,
                               unsigned Reg, EVT VT) const {
    return CreateLiveInRegister(DAG, RC, Reg, VT, SDLoc(DAG.getEntryNode()));
  }

  // Returns the raw live in register rather than a copy from it.
  SDValue CreateLiveInRegisterRaw(SelectionDAG &DAG,
                                  const TargetRegisterClass *RC,
                                  unsigned Reg, EVT VT) const {
    return CreateLiveInRegister(DAG, RC, Reg, VT, SDLoc(DAG.getEntryNode()), true);
  }

  /// Similar to CreateLiveInRegister, except value maybe loaded from a stack
  /// slot rather than passed in a register.
  SDValue loadStackInputValue(SelectionDAG &DAG,
                              EVT VT,
                              const SDLoc &SL,
                              int64_t Offset) const;

  SDValue storeStackInputValue(SelectionDAG &DAG,
                               const SDLoc &SL,
                               SDValue Chain,
                               SDValue ArgVal,
                               int64_t Offset) const;

  SDValue loadInputValue(SelectionDAG &DAG,
                         const TargetRegisterClass *RC,
                         EVT VT, const SDLoc &SL,
                         const ArgDescriptor &Arg) const;

  enum ImplicitParameter {
    FIRST_IMPLICIT,
    GRID_DIM = FIRST_IMPLICIT,
    GRID_OFFSET,
  };

  /// Helper function that returns the byte offset of the given
  /// type of implicit parameter.
  uint32_t getImplicitParameterOffset(const MachineFunction &MF,
                                      const ImplicitParameter Param) const;

  MVT getFenceOperandTy(const DataLayout &DL) const override {
    return MVT::i32;
  }

  AtomicExpansionKind shouldExpandAtomicRMWInIR(AtomicRMWInst *) const override;
};

namespace AMDGPUISD {

enum NodeType : unsigned {
  // AMDIL ISD Opcodes
  FIRST_NUMBER = ISD::BUILTIN_OP_END,
  UMUL,        // 32bit unsigned multiplication
  BRANCH_COND,
  // End AMDIL ISD Opcodes

  // Function call.
  CALL,
  TC_RETURN,
  TRAP,

  // Masked control flow nodes.
  IF,
  ELSE,
  LOOP,

  // A uniform kernel return that terminates the wavefront.
  ENDPGM,

  // Return to a shader part's epilog code.
  RETURN_TO_EPILOG,

  // Return with values from a non-entry function.
  RET_FLAG,

  DWORDADDR,
  FRACT,

  /// CLAMP value between 0.0 and 1.0. NaN clamped to 0, following clamp output
  /// modifier behavior with dx10_enable.
  CLAMP,

  // This is SETCC with the full mask result which is used for a compare with a
  // result bit per item in the wavefront.
  SETCC,
  SETREG,
  // FP ops with input and output chain.
  FMA_W_CHAIN,
  FMUL_W_CHAIN,

  // SIN_HW, COS_HW - f32 for SI, 1 ULP max error, valid from -100 pi to 100 pi.
  // Denormals handled on some parts.
  COS_HW,
  SIN_HW,
  FMAX_LEGACY,
  FMIN_LEGACY,

  FMAX3,
  SMAX3,
  UMAX3,
  FMIN3,
  SMIN3,
  UMIN3,
  FMED3,
  SMED3,
  UMED3,
  FDOT2,
  URECIP,
  DIV_SCALE,
  DIV_FMAS,
  DIV_FIXUP,
  // For emitting ISD::FMAD when f32 denormals are enabled because mac/mad is
  // treated as an illegal operation.
  FMAD_FTZ,
  TRIG_PREOP, // 1 ULP max error for f64

  // RCP, RSQ - For f32, 1 ULP max error, no denormal handling.
  //            For f64, max error 2^29 ULP, handles denormals.
  RCP,
  RSQ,
  RCP_LEGACY,
  RSQ_LEGACY,
  RCP_IFLAG,
  FMUL_LEGACY,
  RSQ_CLAMP,
  LDEXP,
  FP_CLASS,
  DOT4,
  CARRY,
  BORROW,
  BFE_U32, // Extract range of bits with zero extension to 32-bits.
  BFE_I32, // Extract range of bits with sign extension to 32-bits.
  BFI, // (src0 & src1) | (~src0 & src2)
  BFM, // Insert a range of bits into a 32-bit word.
  FFBH_U32, // ctlz with -1 if input is zero.
  FFBH_I32,
  FFBL_B32, // cttz with -1 if input is zero.
  MUL_U24,
  MUL_I24,
  MULHI_U24,
  MULHI_I24,
  MAD_U24,
  MAD_I24,
  MAD_U64_U32,
  MAD_I64_I32,
  MUL_LOHI_I24,
  MUL_LOHI_U24,
  PERM,
  TEXTURE_FETCH,
  EXPORT, // exp on SI+
  EXPORT_DONE, // exp on SI+ with done bit set
  R600_EXPORT,
  CONST_ADDRESS,
  REGISTER_LOAD,
  REGISTER_STORE,
  SAMPLE,
  SAMPLEB,
  SAMPLED,
  SAMPLEL,

  // These cvt_f32_ubyte* nodes need to remain consecutive and in order.
  CVT_F32_UBYTE0,
  CVT_F32_UBYTE1,
  CVT_F32_UBYTE2,
  CVT_F32_UBYTE3,

  // Convert two float 32 numbers into a single register holding two packed f16
  // with round to zero.
  CVT_PKRTZ_F16_F32,
  CVT_PKNORM_I16_F32,
  CVT_PKNORM_U16_F32,
  CVT_PK_I16_I32,
  CVT_PK_U16_U32,

  // Same as the standard node, except the high bits of the resulting integer
  // are known 0.
  FP_TO_FP16,

  // Wrapper around fp16 results that are known to zero the high bits.
  FP16_ZEXT,

  /// This node is for VLIW targets and it is used to represent a vector
  /// that is stored in consecutive registers with the same channel.
  /// For example:
  ///   |X  |Y|Z|W|
  /// T0|v.x| | | |
  /// T1|v.y| | | |
  /// T2|v.z| | | |
  /// T3|v.w| | | |
  BUILD_VERTICAL_VECTOR,
  /// Pointer to the start of the shader's constant data.
  CONST_DATA_PTR,
  INIT_EXEC,
  INIT_EXEC_FROM_INPUT,
  SENDMSG,
  SENDMSGHALT,
  INTERP_MOV,
  INTERP_P1,
  INTERP_P2,
  PC_ADD_REL_OFFSET,
  KILL,
  DUMMY_CHAIN,
  FIRST_MEM_OPCODE_NUMBER = ISD::FIRST_TARGET_MEMORY_OPCODE,
  STORE_MSKOR,
  LOAD_CONSTANT,
  TBUFFER_STORE_FORMAT,
  TBUFFER_STORE_FORMAT_X3,
  TBUFFER_STORE_FORMAT_D16,
  TBUFFER_LOAD_FORMAT,
  TBUFFER_LOAD_FORMAT_D16,
  DS_ORDERED_COUNT,
  ATOMIC_CMP_SWAP,
  ATOMIC_INC,
  ATOMIC_DEC,
  ATOMIC_LOAD_FADD,
  ATOMIC_LOAD_FMIN,
  ATOMIC_LOAD_FMAX,
  BUFFER_LOAD,
  BUFFER_LOAD_FORMAT,
  BUFFER_LOAD_FORMAT_D16,
  SBUFFER_LOAD,
  BUFFER_STORE,
  BUFFER_STORE_FORMAT,
  BUFFER_STORE_FORMAT_D16,
  BUFFER_ATOMIC_SWAP,
  BUFFER_ATOMIC_ADD,
  BUFFER_ATOMIC_SUB,
  BUFFER_ATOMIC_SMIN,
  BUFFER_ATOMIC_UMIN,
  BUFFER_ATOMIC_SMAX,
  BUFFER_ATOMIC_UMAX,
  BUFFER_ATOMIC_AND,
  BUFFER_ATOMIC_OR,
  BUFFER_ATOMIC_XOR,
  BUFFER_ATOMIC_CMPSWAP,

  LAST_AMDGPU_ISD_NUMBER
};


} // End namespace AMDGPUISD

} // End namespace llvm

#endif
