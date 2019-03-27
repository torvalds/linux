//===-- XCoreISelLowering.h - XCore DAG Lowering Interface ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that XCore uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_XCORE_XCOREISELLOWERING_H
#define LLVM_LIB_TARGET_XCORE_XCOREISELLOWERING_H

#include "XCore.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {

  // Forward delcarations
  class XCoreSubtarget;
  class XCoreTargetMachine;

  namespace XCoreISD {
    enum NodeType : unsigned {
      // Start the numbering where the builtin ops and target ops leave off.
      FIRST_NUMBER = ISD::BUILTIN_OP_END,

      // Branch and link (call)
      BL,

      // pc relative address
      PCRelativeWrapper,

      // dp relative address
      DPRelativeWrapper,

      // cp relative address
      CPRelativeWrapper,

      // Load word from stack
      LDWSP,

      // Store word to stack
      STWSP,

      // Corresponds to retsp instruction
      RETSP,

      // Corresponds to LADD instruction
      LADD,

      // Corresponds to LSUB instruction
      LSUB,

      // Corresponds to LMUL instruction
      LMUL,

      // Corresponds to MACCU instruction
      MACCU,

      // Corresponds to MACCS instruction
      MACCS,

      // Corresponds to CRC8 instruction
      CRC8,

      // Jumptable branch.
      BR_JT,

      // Jumptable branch using long branches for each entry.
      BR_JT32,

      // Offset from frame pointer to the first (possible) on-stack argument
      FRAME_TO_ARGS_OFFSET,

      // Exception handler return. The stack is restored to the first
      // followed by a jump to the second argument.
      EH_RETURN,

      // Memory barrier.
      MEMBARRIER
    };
  }

  //===--------------------------------------------------------------------===//
  // TargetLowering Implementation
  //===--------------------------------------------------------------------===//
  class XCoreTargetLowering : public TargetLowering
  {
  public:
    explicit XCoreTargetLowering(const TargetMachine &TM,
                                 const XCoreSubtarget &Subtarget);

    using TargetLowering::isZExtFree;
    bool isZExtFree(SDValue Val, EVT VT2) const override;


    unsigned getJumpTableEncoding() const override;
    MVT getScalarShiftAmountTy(const DataLayout &DL, EVT) const override {
      return MVT::i32;
    }

    /// LowerOperation - Provide custom lowering hooks for some operations.
    SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

    /// ReplaceNodeResults - Replace the results of node with an illegal result
    /// type with new values built out of custom code.
    ///
    void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue>&Results,
                            SelectionDAG &DAG) const override;

    /// getTargetNodeName - This method returns the name of a target specific
    //  DAG node.
    const char *getTargetNodeName(unsigned Opcode) const override;

    MachineBasicBlock *
    EmitInstrWithCustomInserter(MachineInstr &MI,
                                MachineBasicBlock *MBB) const override;

    bool isLegalAddressingMode(const DataLayout &DL, const AddrMode &AM,
                               Type *Ty, unsigned AS,
                               Instruction *I = nullptr) const override;

    /// If a physical register, this returns the register that receives the
    /// exception address on entry to an EH pad.
    unsigned
    getExceptionPointerRegister(const Constant *PersonalityFn) const override {
      return XCore::R0;
    }

    /// If a physical register, this returns the register that receives the
    /// exception typeid on entry to a landing pad.
    unsigned
    getExceptionSelectorRegister(const Constant *PersonalityFn) const override {
      return XCore::R1;
    }

  private:
    const TargetMachine &TM;
    const XCoreSubtarget &Subtarget;

    // Lower Operand helpers
    SDValue LowerCCCArguments(SDValue Chain, CallingConv::ID CallConv,
                              bool isVarArg,
                              const SmallVectorImpl<ISD::InputArg> &Ins,
                              const SDLoc &dl, SelectionDAG &DAG,
                              SmallVectorImpl<SDValue> &InVals) const;
    SDValue LowerCCCCallTo(SDValue Chain, SDValue Callee,
                           CallingConv::ID CallConv, bool isVarArg,
                           bool isTailCall,
                           const SmallVectorImpl<ISD::OutputArg> &Outs,
                           const SmallVectorImpl<SDValue> &OutVals,
                           const SmallVectorImpl<ISD::InputArg> &Ins,
                           const SDLoc &dl, SelectionDAG &DAG,
                           SmallVectorImpl<SDValue> &InVals) const;
    SDValue getReturnAddressFrameIndex(SelectionDAG &DAG) const;
    SDValue getGlobalAddressWrapper(SDValue GA, const GlobalValue *GV,
                                    SelectionDAG &DAG) const;
    SDValue lowerLoadWordFromAlignedBasePlusOffset(const SDLoc &DL,
                                                   SDValue Chain, SDValue Base,
                                                   int64_t Offset,
                                                   SelectionDAG &DAG) const;

    // Lower Operand specifics
    SDValue LowerLOAD(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerSTORE(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerEH_RETURN(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerBlockAddress(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerConstantPool(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerBR_JT(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerVAARG(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerUMUL_LOHI(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerSMUL_LOHI(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerFRAME_TO_ARGS_OFFSET(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerINIT_TRAMPOLINE(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerADJUST_TRAMPOLINE(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerINTRINSIC_WO_CHAIN(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerATOMIC_FENCE(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerATOMIC_LOAD(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerATOMIC_STORE(SDValue Op, SelectionDAG &DAG) const;

    // Inline asm support
    std::pair<unsigned, const TargetRegisterClass *>
    getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                 StringRef Constraint, MVT VT) const override;

    // Expand specifics
    SDValue TryExpandADDWithMul(SDNode *Op, SelectionDAG &DAG) const;
    SDValue ExpandADDSUB(SDNode *Op, SelectionDAG &DAG) const;

    SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;

    void computeKnownBitsForTargetNode(const SDValue Op,
                                       KnownBits &Known,
                                       const APInt &DemandedElts,
                                       const SelectionDAG &DAG,
                                       unsigned Depth = 0) const override;

    SDValue
    LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                         const SmallVectorImpl<ISD::InputArg> &Ins,
                         const SDLoc &dl, SelectionDAG &DAG,
                         SmallVectorImpl<SDValue> &InVals) const override;

    SDValue
      LowerCall(TargetLowering::CallLoweringInfo &CLI,
                SmallVectorImpl<SDValue> &InVals) const override;

    SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
                        const SmallVectorImpl<ISD::OutputArg> &Outs,
                        const SmallVectorImpl<SDValue> &OutVals,
                        const SDLoc &dl, SelectionDAG &DAG) const override;

    bool
      CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                     bool isVarArg,
                     const SmallVectorImpl<ISD::OutputArg> &ArgsFlags,
                     LLVMContext &Context) const override;
    bool shouldInsertFencesForAtomic(const Instruction *I) const override {
      return true;
    }
  };
}

#endif
