//===-- AArch64SelectionDAGInfo.cpp - AArch64 SelectionDAG Info -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the AArch64SelectionDAGInfo class.
//
//===----------------------------------------------------------------------===//

#include "AArch64TargetMachine.h"
using namespace llvm;

#define DEBUG_TYPE "aarch64-selectiondag-info"

SDValue AArch64SelectionDAGInfo::EmitTargetCodeForMemset(
    SelectionDAG &DAG, const SDLoc &dl, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, unsigned Align, bool isVolatile,
    MachinePointerInfo DstPtrInfo) const {
  // Check to see if there is a specialized entry-point for memory zeroing.
  ConstantSDNode *V = dyn_cast<ConstantSDNode>(Src);
  ConstantSDNode *SizeValue = dyn_cast<ConstantSDNode>(Size);
  const AArch64Subtarget &STI =
      DAG.getMachineFunction().getSubtarget<AArch64Subtarget>();
  const char *bzeroName = (V && V->isNullValue())
      ? DAG.getTargetLoweringInfo().getLibcallName(RTLIB::BZERO) : nullptr;
  // For small size (< 256), it is not beneficial to use bzero
  // instead of memset.
  if (bzeroName && (!SizeValue || SizeValue->getZExtValue() > 256)) {
    const AArch64TargetLowering &TLI = *STI.getTargetLowering();

    EVT IntPtr = TLI.getPointerTy(DAG.getDataLayout());
    Type *IntPtrTy = DAG.getDataLayout().getIntPtrType(*DAG.getContext());
    TargetLowering::ArgListTy Args;
    TargetLowering::ArgListEntry Entry;
    Entry.Node = Dst;
    Entry.Ty = IntPtrTy;
    Args.push_back(Entry);
    Entry.Node = Size;
    Args.push_back(Entry);
    TargetLowering::CallLoweringInfo CLI(DAG);
    CLI.setDebugLoc(dl)
        .setChain(Chain)
        .setLibCallee(CallingConv::C, Type::getVoidTy(*DAG.getContext()),
                      DAG.getExternalSymbol(bzeroName, IntPtr),
                      std::move(Args))
        .setDiscardResult();
    std::pair<SDValue, SDValue> CallResult = TLI.LowerCallTo(CLI);
    return CallResult.second;
  }
  return SDValue();
}
bool AArch64SelectionDAGInfo::generateFMAsInMachineCombiner(
    CodeGenOpt::Level OptLevel) const {
  return OptLevel >= CodeGenOpt::Aggressive;
}
