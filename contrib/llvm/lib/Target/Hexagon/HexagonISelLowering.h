//===-- HexagonISelLowering.h - Hexagon DAG Lowering Interface --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that Hexagon uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONISELLOWERING_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONISELLOWERING_H

#include "Hexagon.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/Support/MachineValueType.h"
#include <cstdint>
#include <utility>

namespace llvm {

namespace HexagonISD {

    enum NodeType : unsigned {
      OP_BEGIN = ISD::BUILTIN_OP_END,

      CONST32 = OP_BEGIN,
      CONST32_GP,  // For marking data present in GP.
      ADDC,        // Add with carry: (X, Y, Cin) -> (X+Y, Cout).
      SUBC,        // Sub with carry: (X, Y, Cin) -> (X+~Y+Cin, Cout).
      ALLOCA,

      AT_GOT,      // Index in GOT.
      AT_PCREL,    // Offset relative to PC.

      CALL,        // Function call.
      CALLnr,      // Function call that does not return.
      CALLR,

      RET_FLAG,    // Return with a flag operand.
      BARRIER,     // Memory barrier.
      JT,          // Jump table.
      CP,          // Constant pool.

      COMBINE,
      VSPLAT,      // Generic splat, selection depends on argument/return
                   // types.
      VASL,
      VASR,
      VLSR,

      TSTBIT,
      INSERT,
      EXTRACTU,
      VEXTRACTW,
      VINSERTW0,
      VROR,
      TC_RETURN,
      EH_RETURN,
      DCFETCH,
      READCYCLE,
      D2P,         // Convert 8-byte value to 8-bit predicate register. [*]
      P2D,         // Convert 8-bit predicate register to 8-byte value. [*]
      V2Q,         // Convert HVX vector to a vector predicate reg. [*]
      Q2V,         // Convert vector predicate to an HVX vector. [*]
                   // [*] The equivalence is defined as "Q <=> (V != 0)",
                   //     where the != operation compares bytes.
                   // Note: V != 0 is implemented as V >u 0.
      QCAT,
      QTRUE,
      QFALSE,
      VZERO,
      VSPLATW,     // HVX splat of a 32-bit word with an arbitrary result type.
      TYPECAST,    // No-op that's used to convert between different legal
                   // types in a register.
      VALIGN,      // Align two vectors (in Op0, Op1) to one that would have
                   // been loaded from address in Op2.
      VALIGNADDR,  // Align vector address: Op0 & -Op1, except when it is
                   // an address in a vector load, then it's a no-op.
      OP_END
    };

} // end namespace HexagonISD

  class HexagonSubtarget;

  class HexagonTargetLowering : public TargetLowering {
    int VarArgsFrameOffset;   // Frame offset to start of varargs area.
    const HexagonTargetMachine &HTM;
    const HexagonSubtarget &Subtarget;

    bool CanReturnSmallStruct(const Function* CalleeFn, unsigned& RetSize)
        const;

  public:
    explicit HexagonTargetLowering(const TargetMachine &TM,
                                   const HexagonSubtarget &ST);

    bool isHVXVectorType(MVT Ty) const;

    /// IsEligibleForTailCallOptimization - Check whether the call is eligible
    /// for tail call optimization. Targets which want to do tail call
    /// optimization should implement this function.
    bool IsEligibleForTailCallOptimization(SDValue Callee,
        CallingConv::ID CalleeCC, bool isVarArg, bool isCalleeStructRet,
        bool isCallerStructRet, const SmallVectorImpl<ISD::OutputArg> &Outs,
        const SmallVectorImpl<SDValue> &OutVals,
        const SmallVectorImpl<ISD::InputArg> &Ins, SelectionDAG& DAG) const;

    bool getTgtMemIntrinsic(IntrinsicInfo &Info, const CallInst &I,
                            MachineFunction &MF,
                            unsigned Intrinsic) const override;

    bool isTruncateFree(Type *Ty1, Type *Ty2) const override;
    bool isTruncateFree(EVT VT1, EVT VT2) const override;

    bool isCheapToSpeculateCttz() const override { return true; }
    bool isCheapToSpeculateCtlz() const override { return true; }
    bool isCtlzFast() const override { return true; }

    bool allowTruncateForTailCall(Type *Ty1, Type *Ty2) const override;

    /// Return true if an FMA operation is faster than a pair of mul and add
    /// instructions. fmuladd intrinsics will be expanded to FMAs when this
    /// method returns true (and FMAs are legal), otherwise fmuladd is
    /// expanded to mul + add.
    bool isFMAFasterThanFMulAndFAdd(EVT) const override;

    // Should we expand the build vector with shuffles?
    bool shouldExpandBuildVectorWithShuffles(EVT VT,
        unsigned DefinedValues) const override;

    bool isShuffleMaskLegal(ArrayRef<int> Mask, EVT VT) const override;
    TargetLoweringBase::LegalizeTypeAction getPreferredVectorAction(MVT VT)
        const override;

    SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
    void LowerOperationWrapper(SDNode *N, SmallVectorImpl<SDValue> &Results,
                               SelectionDAG &DAG) const override;
    void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                            SelectionDAG &DAG) const override;

    const char *getTargetNodeName(unsigned Opcode) const override;

    SDValue LowerBUILD_VECTOR(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerCONCAT_VECTORS(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerEXTRACT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerEXTRACT_SUBVECTOR(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerINSERT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerINSERT_SUBVECTOR(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerVECTOR_SHUFFLE(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerVECTOR_SHIFT(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerROTL(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerBITCAST(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerANY_EXTEND(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerSIGN_EXTEND(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerZERO_EXTEND(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerLoad(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerStore(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerUnalignedLoad(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerAddSubCarry(SDValue Op, SelectionDAG &DAG) const;

    SDValue LowerDYNAMIC_STACKALLOC(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerINLINEASM(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerPREFETCH(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerREADCYCLECOUNTER(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerEH_LABEL(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerEH_RETURN(SDValue Op, SelectionDAG &DAG) const;
    SDValue
    LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                         const SmallVectorImpl<ISD::InputArg> &Ins,
                         const SDLoc &dl, SelectionDAG &DAG,
                         SmallVectorImpl<SDValue> &InVals) const override;
    SDValue LowerGLOBALADDRESS(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerBlockAddress(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerToTLSGeneralDynamicModel(GlobalAddressSDNode *GA,
        SelectionDAG &DAG) const;
    SDValue LowerToTLSInitialExecModel(GlobalAddressSDNode *GA,
        SelectionDAG &DAG) const;
    SDValue LowerToTLSLocalExecModel(GlobalAddressSDNode *GA,
        SelectionDAG &DAG) const;
    SDValue GetDynamicTLSAddr(SelectionDAG &DAG, SDValue Chain,
        GlobalAddressSDNode *GA, SDValue InFlag, EVT PtrVT,
        unsigned ReturnReg, unsigned char OperandFlags) const;
    SDValue LowerGLOBAL_OFFSET_TABLE(SDValue Op, SelectionDAG &DAG) const;

    SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
        SmallVectorImpl<SDValue> &InVals) const override;
    SDValue LowerCallResult(SDValue Chain, SDValue InFlag,
                            CallingConv::ID CallConv, bool isVarArg,
                            const SmallVectorImpl<ISD::InputArg> &Ins,
                            const SDLoc &dl, SelectionDAG &DAG,
                            SmallVectorImpl<SDValue> &InVals,
                            const SmallVectorImpl<SDValue> &OutVals,
                            SDValue Callee) const;

    SDValue LowerSETCC(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerVSELECT(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerATOMIC_FENCE(SDValue Op, SelectionDAG& DAG) const;
    SDValue LowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const;

    bool CanLowerReturn(CallingConv::ID CallConv,
                        MachineFunction &MF, bool isVarArg,
                        const SmallVectorImpl<ISD::OutputArg> &Outs,
                        LLVMContext &Context) const override;

    SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                        const SmallVectorImpl<ISD::OutputArg> &Outs,
                        const SmallVectorImpl<SDValue> &OutVals,
                        const SDLoc &dl, SelectionDAG &DAG) const override;

    bool mayBeEmittedAsTailCall(const CallInst *CI) const override;

    unsigned getRegisterByName(const char* RegName, EVT VT,
                               SelectionDAG &DAG) const override;

    /// If a physical register, this returns the register that receives the
    /// exception address on entry to an EH pad.
    unsigned
    getExceptionPointerRegister(const Constant *PersonalityFn) const override {
      return Hexagon::R0;
    }

    /// If a physical register, this returns the register that receives the
    /// exception typeid on entry to a landing pad.
    unsigned
    getExceptionSelectorRegister(const Constant *PersonalityFn) const override {
      return Hexagon::R1;
    }

    SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerConstantPool(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerJumpTable(SDValue Op, SelectionDAG &DAG) const;

    EVT getSetCCResultType(const DataLayout &, LLVMContext &C,
                           EVT VT) const override {
      if (!VT.isVector())
        return MVT::i1;
      else
        return EVT::getVectorVT(C, MVT::i1, VT.getVectorNumElements());
    }

    bool getPostIndexedAddressParts(SDNode *N, SDNode *Op,
                                    SDValue &Base, SDValue &Offset,
                                    ISD::MemIndexedMode &AM,
                                    SelectionDAG &DAG) const override;

    ConstraintType getConstraintType(StringRef Constraint) const override;

    std::pair<unsigned, const TargetRegisterClass *>
    getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                 StringRef Constraint, MVT VT) const override;

    unsigned
    getInlineAsmMemConstraint(StringRef ConstraintCode) const override {
      if (ConstraintCode == "o")
        return InlineAsm::Constraint_o;
      return TargetLowering::getInlineAsmMemConstraint(ConstraintCode);
    }

    // Intrinsics
    SDValue LowerINTRINSIC_WO_CHAIN(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerINTRINSIC_VOID(SDValue Op, SelectionDAG &DAG) const;
    /// isLegalAddressingMode - Return true if the addressing mode represented
    /// by AM is legal for this target, for a load/store of the specified type.
    /// The type may be VoidTy, in which case only return true if the addressing
    /// mode is legal for a load/store of any legal type.
    /// TODO: Handle pre/postinc as well.
    bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM,
                               Type *Ty, unsigned AS,
                               Instruction *I = nullptr) const override;
    /// Return true if folding a constant offset with the given GlobalAddress
    /// is legal.  It is frequently not legal in PIC relocation models.
    bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const override;

    bool isFPImmLegal(const APFloat &Imm, EVT VT) const override;

    /// isLegalICmpImmediate - Return true if the specified immediate is legal
    /// icmp immediate, that is the target has icmp instructions which can
    /// compare a register against the immediate without having to materialize
    /// the immediate into a register.
    bool isLegalICmpImmediate(int64_t Imm) const override;

    EVT getOptimalMemOpType(uint64_t Size, unsigned DstAlign,
        unsigned SrcAlign, bool IsMemset, bool ZeroMemset, bool MemcpyStrSrc,
        MachineFunction &MF) const override;

    bool allowsMisalignedMemoryAccesses(EVT VT, unsigned AddrSpace,
        unsigned Align, bool *Fast) const override;

    /// Returns relocation base for the given PIC jumptable.
    SDValue getPICJumpTableRelocBase(SDValue Table, SelectionDAG &DAG)
                                     const override;

    bool shouldReduceLoadWidth(SDNode *Load, ISD::LoadExtType ExtTy,
                               EVT NewVT) const override;

    // Handling of atomic RMW instructions.
    Value *emitLoadLinked(IRBuilder<> &Builder, Value *Addr,
        AtomicOrdering Ord) const override;
    Value *emitStoreConditional(IRBuilder<> &Builder, Value *Val,
        Value *Addr, AtomicOrdering Ord) const override;
    AtomicExpansionKind shouldExpandAtomicLoadInIR(LoadInst *LI) const override;
    bool shouldExpandAtomicStoreInIR(StoreInst *SI) const override;
    AtomicExpansionKind
    shouldExpandAtomicCmpXchgInIR(AtomicCmpXchgInst *AI) const override;

    AtomicExpansionKind
    shouldExpandAtomicRMWInIR(AtomicRMWInst *AI) const override {
      return AtomicExpansionKind::LLSC;
    }

  private:
    void initializeHVXLowering();
    void validateConstPtrAlignment(SDValue Ptr, const SDLoc &dl,
                                   unsigned NeedAlign) const;

    std::pair<SDValue,int> getBaseAndOffset(SDValue Addr) const;

    bool getBuildVectorConstInts(ArrayRef<SDValue> Values, MVT VecTy,
                                 SelectionDAG &DAG,
                                 MutableArrayRef<ConstantInt*> Consts) const;
    SDValue buildVector32(ArrayRef<SDValue> Elem, const SDLoc &dl, MVT VecTy,
                          SelectionDAG &DAG) const;
    SDValue buildVector64(ArrayRef<SDValue> Elem, const SDLoc &dl, MVT VecTy,
                          SelectionDAG &DAG) const;
    SDValue extractVector(SDValue VecV, SDValue IdxV, const SDLoc &dl,
                          MVT ValTy, MVT ResTy, SelectionDAG &DAG) const;
    SDValue insertVector(SDValue VecV, SDValue ValV, SDValue IdxV,
                         const SDLoc &dl, MVT ValTy, SelectionDAG &DAG) const;
    SDValue expandPredicate(SDValue Vec32, const SDLoc &dl,
                            SelectionDAG &DAG) const;
    SDValue contractPredicate(SDValue Vec64, const SDLoc &dl,
                              SelectionDAG &DAG) const;
    SDValue getVectorShiftByInt(SDValue Op, SelectionDAG &DAG) const;

    bool isUndef(SDValue Op) const {
      if (Op.isMachineOpcode())
        return Op.getMachineOpcode() == TargetOpcode::IMPLICIT_DEF;
      return Op.getOpcode() == ISD::UNDEF;
    }
    SDValue getInstr(unsigned MachineOpc, const SDLoc &dl, MVT Ty,
                     ArrayRef<SDValue> Ops, SelectionDAG &DAG) const {
      SDNode *N = DAG.getMachineNode(MachineOpc, dl, Ty, Ops);
      return SDValue(N, 0);
    }
    SDValue getZero(const SDLoc &dl, MVT Ty, SelectionDAG &DAG) const;

    using VectorPair = std::pair<SDValue, SDValue>;
    using TypePair = std::pair<MVT, MVT>;

    SDValue getInt(unsigned IntId, MVT ResTy, ArrayRef<SDValue> Ops,
                   const SDLoc &dl, SelectionDAG &DAG) const;

    MVT ty(SDValue Op) const {
      return Op.getValueType().getSimpleVT();
    }
    TypePair ty(const VectorPair &Ops) const {
      return { Ops.first.getValueType().getSimpleVT(),
               Ops.second.getValueType().getSimpleVT() };
    }
    MVT tyScalar(MVT Ty) const {
      if (!Ty.isVector())
        return Ty;
      return MVT::getIntegerVT(Ty.getSizeInBits());
    }
    MVT tyVector(MVT Ty, MVT ElemTy) const {
      if (Ty.isVector() && Ty.getVectorElementType() == ElemTy)
        return Ty;
      unsigned TyWidth = Ty.getSizeInBits();
      unsigned ElemWidth = ElemTy.getSizeInBits();
      assert((TyWidth % ElemWidth) == 0);
      return MVT::getVectorVT(ElemTy, TyWidth/ElemWidth);
    }

    MVT typeJoin(const TypePair &Tys) const;
    TypePair typeSplit(MVT Ty) const;
    MVT typeExtElem(MVT VecTy, unsigned Factor) const;
    MVT typeTruncElem(MVT VecTy, unsigned Factor) const;

    SDValue opJoin(const VectorPair &Ops, const SDLoc &dl,
                   SelectionDAG &DAG) const;
    VectorPair opSplit(SDValue Vec, const SDLoc &dl, SelectionDAG &DAG) const;
    SDValue opCastElem(SDValue Vec, MVT ElemTy, SelectionDAG &DAG) const;

    bool isHvxSingleTy(MVT Ty) const;
    bool isHvxPairTy(MVT Ty) const;
    SDValue convertToByteIndex(SDValue ElemIdx, MVT ElemTy,
                               SelectionDAG &DAG) const;
    SDValue getIndexInWord32(SDValue Idx, MVT ElemTy, SelectionDAG &DAG) const;
    SDValue getByteShuffle(const SDLoc &dl, SDValue Op0, SDValue Op1,
                           ArrayRef<int> Mask, SelectionDAG &DAG) const;

    SDValue buildHvxVectorReg(ArrayRef<SDValue> Values, const SDLoc &dl,
                              MVT VecTy, SelectionDAG &DAG) const;
    SDValue buildHvxVectorPred(ArrayRef<SDValue> Values, const SDLoc &dl,
                               MVT VecTy, SelectionDAG &DAG) const;
    SDValue createHvxPrefixPred(SDValue PredV, const SDLoc &dl,
                                unsigned BitBytes, bool ZeroFill,
                                SelectionDAG &DAG) const;
    SDValue extractHvxElementReg(SDValue VecV, SDValue IdxV, const SDLoc &dl,
                                 MVT ResTy, SelectionDAG &DAG) const;
    SDValue extractHvxElementPred(SDValue VecV, SDValue IdxV, const SDLoc &dl,
                                  MVT ResTy, SelectionDAG &DAG) const;
    SDValue insertHvxElementReg(SDValue VecV, SDValue IdxV, SDValue ValV,
                                const SDLoc &dl, SelectionDAG &DAG) const;
    SDValue insertHvxElementPred(SDValue VecV, SDValue IdxV, SDValue ValV,
                                 const SDLoc &dl, SelectionDAG &DAG) const;
    SDValue extractHvxSubvectorReg(SDValue VecV, SDValue IdxV, const SDLoc &dl,
                                   MVT ResTy, SelectionDAG &DAG) const;
    SDValue extractHvxSubvectorPred(SDValue VecV, SDValue IdxV, const SDLoc &dl,
                                    MVT ResTy, SelectionDAG &DAG) const;
    SDValue insertHvxSubvectorReg(SDValue VecV, SDValue SubV, SDValue IdxV,
                                  const SDLoc &dl, SelectionDAG &DAG) const;
    SDValue insertHvxSubvectorPred(SDValue VecV, SDValue SubV, SDValue IdxV,
                                   const SDLoc &dl, SelectionDAG &DAG) const;
    SDValue extendHvxVectorPred(SDValue VecV, const SDLoc &dl, MVT ResTy,
                                bool ZeroExt, SelectionDAG &DAG) const;

    SDValue LowerHvxBuildVector(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerHvxConcatVectors(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerHvxExtractElement(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerHvxInsertElement(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerHvxExtractSubvector(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerHvxInsertSubvector(SDValue Op, SelectionDAG &DAG) const;

    SDValue LowerHvxAnyExt(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerHvxSignExt(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerHvxZeroExt(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerHvxCttz(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerHvxMul(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerHvxMulh(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerHvxSetCC(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerHvxExtend(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerHvxShift(SDValue Op, SelectionDAG &DAG) const;

    SDValue SplitHvxPairOp(SDValue Op, SelectionDAG &DAG) const;
    SDValue SplitHvxMemOp(SDValue Op, SelectionDAG &DAG) const;

    std::pair<const TargetRegisterClass*, uint8_t>
    findRepresentativeClass(const TargetRegisterInfo *TRI, MVT VT)
        const override;

    bool isHvxOperation(SDValue Op) const;
    SDValue LowerHvxOperation(SDValue Op, SelectionDAG &DAG) const;
  };

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_HEXAGONISELLOWERING_H
