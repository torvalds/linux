//==-- AArch64ISelLowering.h - AArch64 DAG Lowering Interface ----*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that AArch64 uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64ISELLOWERING_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64ISELLOWERING_H

#include "AArch64.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Instruction.h"

namespace llvm {

namespace AArch64ISD {

enum NodeType : unsigned {
  FIRST_NUMBER = ISD::BUILTIN_OP_END,
  WrapperLarge, // 4-instruction MOVZ/MOVK sequence for 64-bit addresses.
  CALL,         // Function call.

  // Produces the full sequence of instructions for getting the thread pointer
  // offset of a variable into X0, using the TLSDesc model.
  TLSDESC_CALLSEQ,
  ADRP,     // Page address of a TargetGlobalAddress operand.
  ADR,      // ADR
  ADDlow,   // Add the low 12 bits of a TargetGlobalAddress operand.
  LOADgot,  // Load from automatically generated descriptor (e.g. Global
            // Offset Table, TLS record).
  RET_FLAG, // Return with a flag operand. Operand 0 is the chain operand.
  BRCOND,   // Conditional branch instruction; "b.cond".
  CSEL,
  FCSEL, // Conditional move instruction.
  CSINV, // Conditional select invert.
  CSNEG, // Conditional select negate.
  CSINC, // Conditional select increment.

  // Pointer to the thread's local storage area. Materialised from TPIDR_EL0 on
  // ELF.
  THREAD_POINTER,
  ADC,
  SBC, // adc, sbc instructions

  // Arithmetic instructions which write flags.
  ADDS,
  SUBS,
  ADCS,
  SBCS,
  ANDS,

  // Conditional compares. Operands: left,right,falsecc,cc,flags
  CCMP,
  CCMN,
  FCCMP,

  // Floating point comparison
  FCMP,

  // Scalar extract
  EXTR,

  // Scalar-to-vector duplication
  DUP,
  DUPLANE8,
  DUPLANE16,
  DUPLANE32,
  DUPLANE64,

  // Vector immedate moves
  MOVI,
  MOVIshift,
  MOVIedit,
  MOVImsl,
  FMOV,
  MVNIshift,
  MVNImsl,

  // Vector immediate ops
  BICi,
  ORRi,

  // Vector bit select: similar to ISD::VSELECT but not all bits within an
  // element must be identical.
  BSL,

  // Vector arithmetic negation
  NEG,

  // Vector shuffles
  ZIP1,
  ZIP2,
  UZP1,
  UZP2,
  TRN1,
  TRN2,
  REV16,
  REV32,
  REV64,
  EXT,

  // Vector shift by scalar
  VSHL,
  VLSHR,
  VASHR,

  // Vector shift by scalar (again)
  SQSHL_I,
  UQSHL_I,
  SQSHLU_I,
  SRSHR_I,
  URSHR_I,

  // Vector comparisons
  CMEQ,
  CMGE,
  CMGT,
  CMHI,
  CMHS,
  FCMEQ,
  FCMGE,
  FCMGT,

  // Vector zero comparisons
  CMEQz,
  CMGEz,
  CMGTz,
  CMLEz,
  CMLTz,
  FCMEQz,
  FCMGEz,
  FCMGTz,
  FCMLEz,
  FCMLTz,

  // Vector across-lanes addition
  // Only the lower result lane is defined.
  SADDV,
  UADDV,

  // Vector across-lanes min/max
  // Only the lower result lane is defined.
  SMINV,
  UMINV,
  SMAXV,
  UMAXV,

  // Vector bitwise negation
  NOT,

  // Vector bitwise selection
  BIT,

  // Compare-and-branch
  CBZ,
  CBNZ,
  TBZ,
  TBNZ,

  // Tail calls
  TC_RETURN,

  // Custom prefetch handling
  PREFETCH,

  // {s|u}int to FP within a FP register.
  SITOF,
  UITOF,

  /// Natural vector cast. ISD::BITCAST is not natural in the big-endian
  /// world w.r.t vectors; which causes additional REV instructions to be
  /// generated to compensate for the byte-swapping. But sometimes we do
  /// need to re-interpret the data in SIMD vector registers in big-endian
  /// mode without emitting such REV instructions.
  NVCAST,

  SMULL,
  UMULL,

  // Reciprocal estimates and steps.
  FRECPE, FRECPS,
  FRSQRTE, FRSQRTS,

  // NEON Load/Store with post-increment base updates
  LD2post = ISD::FIRST_TARGET_MEMORY_OPCODE,
  LD3post,
  LD4post,
  ST2post,
  ST3post,
  ST4post,
  LD1x2post,
  LD1x3post,
  LD1x4post,
  ST1x2post,
  ST1x3post,
  ST1x4post,
  LD1DUPpost,
  LD2DUPpost,
  LD3DUPpost,
  LD4DUPpost,
  LD1LANEpost,
  LD2LANEpost,
  LD3LANEpost,
  LD4LANEpost,
  ST2LANEpost,
  ST3LANEpost,
  ST4LANEpost
};

} // end namespace AArch64ISD

namespace {

// Any instruction that defines a 32-bit result zeros out the high half of the
// register. Truncate can be lowered to EXTRACT_SUBREG. CopyFromReg may
// be copying from a truncate. But any other 32-bit operation will zero-extend
// up to 64 bits.
// FIXME: X86 also checks for CMOV here. Do we need something similar?
static inline bool isDef32(const SDNode &N) {
  unsigned Opc = N.getOpcode();
  return Opc != ISD::TRUNCATE && Opc != TargetOpcode::EXTRACT_SUBREG &&
         Opc != ISD::CopyFromReg;
}

} // end anonymous namespace

class AArch64Subtarget;
class AArch64TargetMachine;

class AArch64TargetLowering : public TargetLowering {
public:
  explicit AArch64TargetLowering(const TargetMachine &TM,
                                 const AArch64Subtarget &STI);

  /// Selects the correct CCAssignFn for a given CallingConvention value.
  CCAssignFn *CCAssignFnForCall(CallingConv::ID CC, bool IsVarArg) const;

  /// Selects the correct CCAssignFn for a given CallingConvention value.
  CCAssignFn *CCAssignFnForReturn(CallingConv::ID CC) const;

  /// Determine which of the bits specified in Mask are known to be either zero
  /// or one and return them in the KnownZero/KnownOne bitsets.
  void computeKnownBitsForTargetNode(const SDValue Op, KnownBits &Known,
                                     const APInt &DemandedElts,
                                     const SelectionDAG &DAG,
                                     unsigned Depth = 0) const override;

  bool targetShrinkDemandedConstant(SDValue Op, const APInt &Demanded,
                                    TargetLoweringOpt &TLO) const override;

  MVT getScalarShiftAmountTy(const DataLayout &DL, EVT) const override;

  /// Returns true if the target allows unaligned memory accesses of the
  /// specified type.
  bool allowsMisalignedMemoryAccesses(EVT VT, unsigned AddrSpace = 0,
                                      unsigned Align = 1,
                                      bool *Fast = nullptr) const override;

  /// Provide custom lowering hooks for some operations.
  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

  const char *getTargetNodeName(unsigned Opcode) const override;

  SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;

  /// Returns true if a cast between SrcAS and DestAS is a noop.
  bool isNoopAddrSpaceCast(unsigned SrcAS, unsigned DestAS) const override {
    // Addrspacecasts are always noops.
    return true;
  }

  /// This method returns a target specific FastISel object, or null if the
  /// target does not support "fast" ISel.
  FastISel *createFastISel(FunctionLoweringInfo &funcInfo,
                           const TargetLibraryInfo *libInfo) const override;

  bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const override;

  bool isFPImmLegal(const APFloat &Imm, EVT VT) const override;

  /// Return true if the given shuffle mask can be codegen'd directly, or if it
  /// should be stack expanded.
  bool isShuffleMaskLegal(ArrayRef<int> M, EVT VT) const override;

  /// Return the ISD::SETCC ValueType.
  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                         EVT VT) const override;

  SDValue ReconstructShuffle(SDValue Op, SelectionDAG &DAG) const;

  MachineBasicBlock *EmitF128CSEL(MachineInstr &MI,
                                  MachineBasicBlock *BB) const;

  MachineBasicBlock *EmitLoweredCatchRet(MachineInstr &MI,
                                           MachineBasicBlock *BB) const;

  MachineBasicBlock *EmitLoweredCatchPad(MachineInstr &MI,
                                         MachineBasicBlock *BB) const;

  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *MBB) const override;

  bool getTgtMemIntrinsic(IntrinsicInfo &Info, const CallInst &I,
                          MachineFunction &MF,
                          unsigned Intrinsic) const override;

  bool shouldReduceLoadWidth(SDNode *Load, ISD::LoadExtType ExtTy,
                             EVT NewVT) const override;

  bool isTruncateFree(Type *Ty1, Type *Ty2) const override;
  bool isTruncateFree(EVT VT1, EVT VT2) const override;

  bool isProfitableToHoist(Instruction *I) const override;

  bool isZExtFree(Type *Ty1, Type *Ty2) const override;
  bool isZExtFree(EVT VT1, EVT VT2) const override;
  bool isZExtFree(SDValue Val, EVT VT2) const override;

  bool hasPairedLoad(EVT LoadedType, unsigned &RequiredAligment) const override;

  unsigned getMaxSupportedInterleaveFactor() const override { return 4; }

  bool lowerInterleavedLoad(LoadInst *LI,
                            ArrayRef<ShuffleVectorInst *> Shuffles,
                            ArrayRef<unsigned> Indices,
                            unsigned Factor) const override;
  bool lowerInterleavedStore(StoreInst *SI, ShuffleVectorInst *SVI,
                             unsigned Factor) const override;

  bool isLegalAddImmediate(int64_t) const override;
  bool isLegalICmpImmediate(int64_t) const override;

  bool shouldConsiderGEPOffsetSplit() const override;

  EVT getOptimalMemOpType(uint64_t Size, unsigned DstAlign, unsigned SrcAlign,
                          bool IsMemset, bool ZeroMemset, bool MemcpyStrSrc,
                          MachineFunction &MF) const override;

  /// Return true if the addressing mode represented by AM is legal for this
  /// target, for a load/store of the specified type.
  bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM, Type *Ty,
                             unsigned AS,
                             Instruction *I = nullptr) const override;

  /// Return the cost of the scaling factor used in the addressing
  /// mode represented by AM for this target, for a load/store
  /// of the specified type.
  /// If the AM is supported, the return value must be >= 0.
  /// If the AM is not supported, it returns a negative value.
  int getScalingFactorCost(const DataLayout &DL, const AddrMode &AM, Type *Ty,
                           unsigned AS) const override;

  /// Return true if an FMA operation is faster than a pair of fmul and fadd
  /// instructions. fmuladd intrinsics will be expanded to FMAs when this method
  /// returns true, otherwise fmuladd is expanded to fmul + fadd.
  bool isFMAFasterThanFMulAndFAdd(EVT VT) const override;

  const MCPhysReg *getScratchRegisters(CallingConv::ID CC) const override;

  /// Returns false if N is a bit extraction pattern of (X >> C) & Mask.
  bool isDesirableToCommuteWithShift(const SDNode *N,
                                     CombineLevel Level) const override;

  /// Returns true if it is beneficial to convert a load of a constant
  /// to just the constant itself.
  bool shouldConvertConstantLoadToIntImm(const APInt &Imm,
                                         Type *Ty) const override;

  /// Return true if EXTRACT_SUBVECTOR is cheap for this result type
  /// with this index.
  bool isExtractSubvectorCheap(EVT ResVT, EVT SrcVT,
                               unsigned Index) const override;

  Value *emitLoadLinked(IRBuilder<> &Builder, Value *Addr,
                        AtomicOrdering Ord) const override;
  Value *emitStoreConditional(IRBuilder<> &Builder, Value *Val,
                              Value *Addr, AtomicOrdering Ord) const override;

  void emitAtomicCmpXchgNoStoreLLBalance(IRBuilder<> &Builder) const override;

  TargetLoweringBase::AtomicExpansionKind
  shouldExpandAtomicLoadInIR(LoadInst *LI) const override;
  bool shouldExpandAtomicStoreInIR(StoreInst *SI) const override;
  TargetLoweringBase::AtomicExpansionKind
  shouldExpandAtomicRMWInIR(AtomicRMWInst *AI) const override;

  TargetLoweringBase::AtomicExpansionKind
  shouldExpandAtomicCmpXchgInIR(AtomicCmpXchgInst *AI) const override;

  bool useLoadStackGuardNode() const override;
  TargetLoweringBase::LegalizeTypeAction
  getPreferredVectorAction(MVT VT) const override;

  /// If the target has a standard location for the stack protector cookie,
  /// returns the address of that location. Otherwise, returns nullptr.
  Value *getIRStackGuard(IRBuilder<> &IRB) const override;

  void insertSSPDeclarations(Module &M) const override;
  Value *getSDagStackGuard(const Module &M) const override;
  Value *getSSPStackGuardCheck(const Module &M) const override;

  /// If the target has a standard location for the unsafe stack pointer,
  /// returns the address of that location. Otherwise, returns nullptr.
  Value *getSafeStackPointerLocation(IRBuilder<> &IRB) const override;

  /// If a physical register, this returns the register that receives the
  /// exception address on entry to an EH pad.
  unsigned
  getExceptionPointerRegister(const Constant *PersonalityFn) const override {
    // FIXME: This is a guess. Has this been defined yet?
    return AArch64::X0;
  }

  /// If a physical register, this returns the register that receives the
  /// exception typeid on entry to a landing pad.
  unsigned
  getExceptionSelectorRegister(const Constant *PersonalityFn) const override {
    // FIXME: This is a guess. Has this been defined yet?
    return AArch64::X1;
  }

  bool isIntDivCheap(EVT VT, AttributeList Attr) const override;

  bool canMergeStoresTo(unsigned AddressSpace, EVT MemVT,
                        const SelectionDAG &DAG) const override {
    // Do not merge to float value size (128 bytes) if no implicit
    // float attribute is set.

    bool NoFloat = DAG.getMachineFunction().getFunction().hasFnAttribute(
        Attribute::NoImplicitFloat);

    if (NoFloat)
      return (MemVT.getSizeInBits() <= 64);
    return true;
  }

  bool isCheapToSpeculateCttz() const override {
    return true;
  }

  bool isCheapToSpeculateCtlz() const override {
    return true;
  }

  bool isMaskAndCmp0FoldingBeneficial(const Instruction &AndI) const override;

  bool hasAndNotCompare(SDValue V) const override {
    // We can use bics for any scalar.
    return V.getValueType().isScalarInteger();
  }

  bool hasAndNot(SDValue Y) const override {
    EVT VT = Y.getValueType();

    if (!VT.isVector())
      return hasAndNotCompare(Y);

    return VT.getSizeInBits() >= 64; // vector 'bic'
  }

  bool shouldTransformSignedTruncationCheck(EVT XVT,
                                            unsigned KeptBits) const override {
    // For vectors, we don't have a preference..
    if (XVT.isVector())
      return false;

    auto VTIsOk = [](EVT VT) -> bool {
      return VT == MVT::i8 || VT == MVT::i16 || VT == MVT::i32 ||
             VT == MVT::i64;
    };

    // We are ok with KeptBitsVT being byte/word/dword, what SXT supports.
    // XVT will be larger than KeptBitsVT.
    MVT KeptBitsVT = MVT::getIntegerVT(KeptBits);
    return VTIsOk(XVT) && VTIsOk(KeptBitsVT);
  }

  bool hasBitPreservingFPLogic(EVT VT) const override {
    // FIXME: Is this always true? It should be true for vectors at least.
    return VT == MVT::f32 || VT == MVT::f64;
  }

  bool supportSplitCSR(MachineFunction *MF) const override {
    return MF->getFunction().getCallingConv() == CallingConv::CXX_FAST_TLS &&
           MF->getFunction().hasFnAttribute(Attribute::NoUnwind);
  }
  void initializeSplitCSR(MachineBasicBlock *Entry) const override;
  void insertCopiesSplitCSR(
      MachineBasicBlock *Entry,
      const SmallVectorImpl<MachineBasicBlock *> &Exits) const override;

  bool supportSwiftError() const override {
    return true;
  }

  /// Enable aggressive FMA fusion on targets that want it.
  bool enableAggressiveFMAFusion(EVT VT) const override;

  /// Returns the size of the platform's va_list object.
  unsigned getVaListSizeInBits(const DataLayout &DL) const override;

  /// Returns true if \p VecTy is a legal interleaved access type. This
  /// function checks the vector element type and the overall width of the
  /// vector.
  bool isLegalInterleavedAccessType(VectorType *VecTy,
                                    const DataLayout &DL) const;

  /// Returns the number of interleaved accesses that will be generated when
  /// lowering accesses of the given type.
  unsigned getNumInterleavedAccesses(VectorType *VecTy,
                                     const DataLayout &DL) const;

  MachineMemOperand::Flags getMMOFlags(const Instruction &I) const override;

  bool functionArgumentNeedsConsecutiveRegisters(Type *Ty,
                                                 CallingConv::ID CallConv,
                                                 bool isVarArg) const override;
  /// Used for exception handling on Win64.
  bool needsFixedCatchObjects() const override;
private:
  /// Keep a pointer to the AArch64Subtarget around so that we can
  /// make the right decision when generating code for different targets.
  const AArch64Subtarget *Subtarget;

  bool isExtFreeImpl(const Instruction *Ext) const override;

  void addTypeForNEON(MVT VT, MVT PromotedBitwiseVT);
  void addDRTypeForNEON(MVT VT);
  void addQRTypeForNEON(MVT VT);

  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool isVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &DL, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerCall(CallLoweringInfo & /*CLI*/,
                    SmallVectorImpl<SDValue> &InVals) const override;

  SDValue LowerCallResult(SDValue Chain, SDValue InFlag,
                          CallingConv::ID CallConv, bool isVarArg,
                          const SmallVectorImpl<ISD::InputArg> &Ins,
                          const SDLoc &DL, SelectionDAG &DAG,
                          SmallVectorImpl<SDValue> &InVals, bool isThisReturn,
                          SDValue ThisVal) const;

  SDValue LowerSTORE(SDValue Op, SelectionDAG &DAG) const;

  SDValue LowerINTRINSIC_WO_CHAIN(SDValue Op, SelectionDAG &DAG) const;

  bool isEligibleForTailCallOptimization(
      SDValue Callee, CallingConv::ID CalleeCC, bool isVarArg,
      const SmallVectorImpl<ISD::OutputArg> &Outs,
      const SmallVectorImpl<SDValue> &OutVals,
      const SmallVectorImpl<ISD::InputArg> &Ins, SelectionDAG &DAG) const;

  /// Finds the incoming stack arguments which overlap the given fixed stack
  /// object and incorporates their load into the current chain. This prevents
  /// an upcoming store from clobbering the stack argument before it's used.
  SDValue addTokenForArgument(SDValue Chain, SelectionDAG &DAG,
                              MachineFrameInfo &MFI, int ClobberedFI) const;

  bool DoesCalleeRestoreStack(CallingConv::ID CallCC, bool TailCallOpt) const;

  void saveVarArgRegisters(CCState &CCInfo, SelectionDAG &DAG, const SDLoc &DL,
                           SDValue &Chain) const;

  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                      bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      LLVMContext &Context) const override;

  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
                      SelectionDAG &DAG) const override;

  SDValue getTargetNode(GlobalAddressSDNode *N, EVT Ty, SelectionDAG &DAG,
                        unsigned Flag) const;
  SDValue getTargetNode(JumpTableSDNode *N, EVT Ty, SelectionDAG &DAG,
                        unsigned Flag) const;
  SDValue getTargetNode(ConstantPoolSDNode *N, EVT Ty, SelectionDAG &DAG,
                        unsigned Flag) const;
  SDValue getTargetNode(BlockAddressSDNode *N, EVT Ty, SelectionDAG &DAG,
                        unsigned Flag) const;
  template <class NodeTy>
  SDValue getGOT(NodeTy *N, SelectionDAG &DAG, unsigned Flags = 0) const;
  template <class NodeTy>
  SDValue getAddrLarge(NodeTy *N, SelectionDAG &DAG, unsigned Flags = 0) const;
  template <class NodeTy>
  SDValue getAddr(NodeTy *N, SelectionDAG &DAG, unsigned Flags = 0) const;
  template <class NodeTy>
  SDValue getAddrTiny(NodeTy *N, SelectionDAG &DAG, unsigned Flags = 0) const;
  SDValue LowerADDROFRETURNADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerDarwinGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerELFGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerELFTLSDescCallSeq(SDValue SymAddr, const SDLoc &DL,
                                 SelectionDAG &DAG) const;
  SDValue LowerWindowsGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSETCC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBR_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSELECT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSELECT_CC(ISD::CondCode CC, SDValue LHS, SDValue RHS,
                         SDValue TVal, SDValue FVal, const SDLoc &dl,
                         SelectionDAG &DAG) const;
  SDValue LowerJumpTable(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBR_JT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerConstantPool(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBlockAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerAAPCS_VASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerDarwin_VASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerWin64_VASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVACOPY(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVAARG(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSPONENTRY(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFLT_ROUNDS_(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerINSERT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerEXTRACT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSCALAR_TO_VECTOR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBUILD_VECTOR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVECTOR_SHUFFLE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerEXTRACT_SUBVECTOR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVectorSRA_SRL_SHL(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerShiftLeftParts(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerShiftRightParts(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVSETCC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerCTPOP(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerF128Call(SDValue Op, SelectionDAG &DAG,
                        RTLIB::Libcall Call) const;
  SDValue LowerFCOPYSIGN(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFP_EXTEND(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFP_ROUND(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFP_TO_INT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerINT_TO_FP(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVectorAND(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVectorOR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerCONCAT_VECTORS(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFSINCOS(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVECREDUCE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerATOMIC_LOAD_SUB(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerATOMIC_LOAD_AND(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerDYNAMIC_STACKALLOC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerWindowsDYNAMIC_STACKALLOC(SDValue Op, SDValue Chain,
                                         SDValue &Size,
                                         SelectionDAG &DAG) const;

  SDValue BuildSDIVPow2(SDNode *N, const APInt &Divisor, SelectionDAG &DAG,
                        SmallVectorImpl<SDNode *> &Created) const override;
  SDValue getSqrtEstimate(SDValue Operand, SelectionDAG &DAG, int Enabled,
                          int &ExtraSteps, bool &UseOneConst,
                          bool Reciprocal) const override;
  SDValue getRecipEstimate(SDValue Operand, SelectionDAG &DAG, int Enabled,
                           int &ExtraSteps) const override;
  unsigned combineRepeatedFPDivisors() const override;

  ConstraintType getConstraintType(StringRef Constraint) const override;
  unsigned getRegisterByName(const char* RegName, EVT VT,
                             SelectionDAG &DAG) const override;

  /// Examine constraint string and operand type and determine a weight value.
  /// The operand object must already have been set up with the operand type.
  ConstraintWeight
  getSingleConstraintMatchWeight(AsmOperandInfo &info,
                                 const char *constraint) const override;

  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;

  const char *LowerXConstraint(EVT ConstraintVT) const override;

  void LowerAsmOperandForConstraint(SDValue Op, std::string &Constraint,
                                    std::vector<SDValue> &Ops,
                                    SelectionDAG &DAG) const override;

  unsigned getInlineAsmMemConstraint(StringRef ConstraintCode) const override {
    if (ConstraintCode == "Q")
      return InlineAsm::Constraint_Q;
    // FIXME: clang has code for 'Ump', 'Utf', 'Usa', and 'Ush' but these are
    //        followed by llvm_unreachable so we'll leave them unimplemented in
    //        the backend for now.
    return TargetLowering::getInlineAsmMemConstraint(ConstraintCode);
  }

  bool isUsedByReturnOnly(SDNode *N, SDValue &Chain) const override;
  bool mayBeEmittedAsTailCall(const CallInst *CI) const override;
  bool getIndexedAddressParts(SDNode *Op, SDValue &Base, SDValue &Offset,
                              ISD::MemIndexedMode &AM, bool &IsInc,
                              SelectionDAG &DAG) const;
  bool getPreIndexedAddressParts(SDNode *N, SDValue &Base, SDValue &Offset,
                                 ISD::MemIndexedMode &AM,
                                 SelectionDAG &DAG) const override;
  bool getPostIndexedAddressParts(SDNode *N, SDNode *Op, SDValue &Base,
                                  SDValue &Offset, ISD::MemIndexedMode &AM,
                                  SelectionDAG &DAG) const override;

  void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const override;

  bool shouldNormalizeToSelectSequence(LLVMContext &, EVT) const override;

  void finalizeLowering(MachineFunction &MF) const override;
};

namespace AArch64 {
FastISel *createFastISel(FunctionLoweringInfo &funcInfo,
                         const TargetLibraryInfo *libInfo);
} // end namespace AArch64

} // end namespace llvm

#endif
