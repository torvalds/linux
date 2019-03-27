//===-- R600ISelLowering.h - R600 DAG Lowering Interface -*- C++ -*--------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// R600 DAG Lowering interface definition
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_R600ISELLOWERING_H
#define LLVM_LIB_TARGET_AMDGPU_R600ISELLOWERING_H

#include "AMDGPUISelLowering.h"

namespace llvm {

class R600InstrInfo;
class R600Subtarget;

class R600TargetLowering final : public AMDGPUTargetLowering {

  const R600Subtarget *Subtarget;
public:
  R600TargetLowering(const TargetMachine &TM, const R600Subtarget &STI);

  const R600Subtarget *getSubtarget() const;

  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *BB) const override;
  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
  SDValue PerformDAGCombine(SDNode *N, DAGCombinerInfo &DCI) const override;
  void ReplaceNodeResults(SDNode * N,
                          SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const override;
  CCAssignFn *CCAssignFnForCall(CallingConv::ID CC, bool IsVarArg) const;
  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool isVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &DL, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;
  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &,
                         EVT VT) const override;

  bool canMergeStoresTo(unsigned AS, EVT MemVT,
                        const SelectionDAG &DAG) const override;

  bool allowsMisalignedMemoryAccesses(EVT VT, unsigned AS,
                                      unsigned Align,
                                      bool *IsFast) const override;

private:
  unsigned Gen;
  /// Each OpenCL kernel has nine implicit parameters that are stored in the
  /// first nine dwords of a Vertex Buffer.  These implicit parameters are
  /// lowered to load instructions which retrieve the values from the Vertex
  /// Buffer.
  SDValue LowerImplicitParameter(SelectionDAG &DAG, EVT VT, const SDLoc &DL,
                                 unsigned DwordOffset) const;

  void lowerImplicitParameter(MachineInstr *MI, MachineBasicBlock &BB,
      MachineRegisterInfo & MRI, unsigned dword_offset) const;
  SDValue OptimizeSwizzle(SDValue BuildVector, SDValue Swz[], SelectionDAG &DAG,
                          const SDLoc &DL) const;
  SDValue vectorToVerticalVector(SelectionDAG &DAG, SDValue Vector) const;

  SDValue lowerFrameIndex(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerEXTRACT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerINSERT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerGlobalAddress(AMDGPUMachineFunction *MFI, SDValue Op,
                             SelectionDAG &DAG) const override;
  SDValue LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const;

  SDValue lowerPrivateTruncStore(StoreSDNode *Store, SelectionDAG &DAG) const;
  SDValue LowerSTORE(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerFP_TO_UINT(SDValue Op, SelectionDAG &DAG) const;
  SDValue lowerFP_TO_SINT(SDValue Op, SelectionDAG &DAG) const;

  SDValue lowerPrivateExtLoad(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerLOAD(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBRCOND(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerTrig(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSHLParts(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSRXParts(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerUADDSUBO(SDValue Op, SelectionDAG &DAG,
                        unsigned mainop, unsigned ovf) const;

  SDValue stackPtrToRegIndex(SDValue Ptr, unsigned StackWidth,
                                          SelectionDAG &DAG) const;
  void getStackAddress(unsigned StackWidth, unsigned ElemIdx,
                       unsigned &Channel, unsigned &PtrIncr) const;
  bool isZero(SDValue Op) const;
  bool isHWTrueValue(SDValue Op) const;
  bool isHWFalseValue(SDValue Op) const;

  bool FoldOperand(SDNode *ParentNode, unsigned SrcIdx, SDValue &Src,
                   SDValue &Neg, SDValue &Abs, SDValue &Sel, SDValue &Imm,
                   SelectionDAG &DAG) const;
  SDValue constBufferLoad(LoadSDNode *LoadNode, int Block,
                          SelectionDAG &DAG) const;

  SDNode *PostISelFolding(MachineSDNode *N, SelectionDAG &DAG) const override;
};

} // End namespace llvm;

#endif
