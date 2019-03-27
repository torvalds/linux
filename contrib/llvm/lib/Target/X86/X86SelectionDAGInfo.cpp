//===-- X86SelectionDAGInfo.cpp - X86 SelectionDAG Info -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the X86SelectionDAGInfo class.
//
//===----------------------------------------------------------------------===//

#include "X86SelectionDAGInfo.h"
#include "X86ISelLowering.h"
#include "X86InstrInfo.h"
#include "X86RegisterInfo.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/DerivedTypes.h"

using namespace llvm;

#define DEBUG_TYPE "x86-selectiondag-info"

bool X86SelectionDAGInfo::isBaseRegConflictPossible(
    SelectionDAG &DAG, ArrayRef<MCPhysReg> ClobberSet) const {
  // We cannot use TRI->hasBasePointer() until *after* we select all basic
  // blocks.  Legalization may introduce new stack temporaries with large
  // alignment requirements.  Fall back to generic code if there are any
  // dynamic stack adjustments (hopefully rare) and the base pointer would
  // conflict if we had to use it.
  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  if (!MFI.hasVarSizedObjects() && !MFI.hasOpaqueSPAdjustment())
    return false;

  const X86RegisterInfo *TRI = static_cast<const X86RegisterInfo *>(
      DAG.getSubtarget().getRegisterInfo());
  unsigned BaseReg = TRI->getBaseRegister();
  for (unsigned R : ClobberSet)
    if (BaseReg == R)
      return true;
  return false;
}

namespace {

// Represents a cover of a buffer of Size bytes with Count() blocks of type AVT
// (of size UBytes() bytes), as well as how many bytes remain (BytesLeft() is
// always smaller than the block size).
struct RepMovsRepeats {
  RepMovsRepeats(uint64_t Size) : Size(Size) {}

  uint64_t Count() const { return Size / UBytes(); }
  uint64_t BytesLeft() const { return Size % UBytes(); }
  uint64_t UBytes() const { return AVT.getSizeInBits() / 8; }

  const uint64_t Size;
  MVT AVT = MVT::i8;
};

}  // namespace

SDValue X86SelectionDAGInfo::EmitTargetCodeForMemset(
    SelectionDAG &DAG, const SDLoc &dl, SDValue Chain, SDValue Dst, SDValue Val,
    SDValue Size, unsigned Align, bool isVolatile,
    MachinePointerInfo DstPtrInfo) const {
  ConstantSDNode *ConstantSize = dyn_cast<ConstantSDNode>(Size);
  const X86Subtarget &Subtarget =
      DAG.getMachineFunction().getSubtarget<X86Subtarget>();

#ifndef NDEBUG
  // If the base register might conflict with our physical registers, bail out.
  const MCPhysReg ClobberSet[] = {X86::RCX, X86::RAX, X86::RDI,
                                  X86::ECX, X86::EAX, X86::EDI};
  assert(!isBaseRegConflictPossible(DAG, ClobberSet));
#endif

  // If to a segment-relative address space, use the default lowering.
  if (DstPtrInfo.getAddrSpace() >= 256)
    return SDValue();

  // If not DWORD aligned or size is more than the threshold, call the library.
  // The libc version is likely to be faster for these cases. It can use the
  // address value and run time information about the CPU.
  if ((Align & 3) != 0 || !ConstantSize ||
      ConstantSize->getZExtValue() > Subtarget.getMaxInlineSizeThreshold()) {
    // Check to see if there is a specialized entry-point for memory zeroing.
    ConstantSDNode *ValC = dyn_cast<ConstantSDNode>(Val);

    if (const char *bzeroName = (ValC && ValC->isNullValue())
        ? DAG.getTargetLoweringInfo().getLibcallName(RTLIB::BZERO)
        : nullptr) {
      const TargetLowering &TLI = DAG.getTargetLoweringInfo();
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

      std::pair<SDValue,SDValue> CallResult = TLI.LowerCallTo(CLI);
      return CallResult.second;
    }

    // Otherwise have the target-independent code call memset.
    return SDValue();
  }

  uint64_t SizeVal = ConstantSize->getZExtValue();
  SDValue InFlag;
  EVT AVT;
  SDValue Count;
  ConstantSDNode *ValC = dyn_cast<ConstantSDNode>(Val);
  unsigned BytesLeft = 0;
  if (ValC) {
    unsigned ValReg;
    uint64_t Val = ValC->getZExtValue() & 255;

    // If the value is a constant, then we can potentially use larger sets.
    switch (Align & 3) {
    case 2:   // WORD aligned
      AVT = MVT::i16;
      ValReg = X86::AX;
      Val = (Val << 8) | Val;
      break;
    case 0:  // DWORD aligned
      AVT = MVT::i32;
      ValReg = X86::EAX;
      Val = (Val << 8)  | Val;
      Val = (Val << 16) | Val;
      if (Subtarget.is64Bit() && ((Align & 0x7) == 0)) {  // QWORD aligned
        AVT = MVT::i64;
        ValReg = X86::RAX;
        Val = (Val << 32) | Val;
      }
      break;
    default:  // Byte aligned
      AVT = MVT::i8;
      ValReg = X86::AL;
      Count = DAG.getIntPtrConstant(SizeVal, dl);
      break;
    }

    if (AVT.bitsGT(MVT::i8)) {
      unsigned UBytes = AVT.getSizeInBits() / 8;
      Count = DAG.getIntPtrConstant(SizeVal / UBytes, dl);
      BytesLeft = SizeVal % UBytes;
    }

    Chain = DAG.getCopyToReg(Chain, dl, ValReg, DAG.getConstant(Val, dl, AVT),
                             InFlag);
    InFlag = Chain.getValue(1);
  } else {
    AVT = MVT::i8;
    Count  = DAG.getIntPtrConstant(SizeVal, dl);
    Chain  = DAG.getCopyToReg(Chain, dl, X86::AL, Val, InFlag);
    InFlag = Chain.getValue(1);
  }

  bool Use64BitRegs = Subtarget.isTarget64BitLP64();
  Chain = DAG.getCopyToReg(Chain, dl, Use64BitRegs ? X86::RCX : X86::ECX,
                           Count, InFlag);
  InFlag = Chain.getValue(1);
  Chain = DAG.getCopyToReg(Chain, dl, Use64BitRegs ? X86::RDI : X86::EDI,
                           Dst, InFlag);
  InFlag = Chain.getValue(1);

  SDVTList Tys = DAG.getVTList(MVT::Other, MVT::Glue);
  SDValue Ops[] = { Chain, DAG.getValueType(AVT), InFlag };
  Chain = DAG.getNode(X86ISD::REP_STOS, dl, Tys, Ops);

  if (BytesLeft) {
    // Handle the last 1 - 7 bytes.
    unsigned Offset = SizeVal - BytesLeft;
    EVT AddrVT = Dst.getValueType();
    EVT SizeVT = Size.getValueType();

    Chain = DAG.getMemset(Chain, dl,
                          DAG.getNode(ISD::ADD, dl, AddrVT, Dst,
                                      DAG.getConstant(Offset, dl, AddrVT)),
                          Val,
                          DAG.getConstant(BytesLeft, dl, SizeVT),
                          Align, isVolatile, false,
                          DstPtrInfo.getWithOffset(Offset));
  }

  // TODO: Use a Tokenfactor, as in memcpy, instead of a single chain.
  return Chain;
}

SDValue X86SelectionDAGInfo::EmitTargetCodeForMemcpy(
    SelectionDAG &DAG, const SDLoc &dl, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, unsigned Align, bool isVolatile, bool AlwaysInline,
    MachinePointerInfo DstPtrInfo, MachinePointerInfo SrcPtrInfo) const {
  // This requires the copy size to be a constant, preferably
  // within a subtarget-specific limit.
  ConstantSDNode *ConstantSize = dyn_cast<ConstantSDNode>(Size);
  const X86Subtarget &Subtarget =
      DAG.getMachineFunction().getSubtarget<X86Subtarget>();
  if (!ConstantSize)
    return SDValue();
  RepMovsRepeats Repeats(ConstantSize->getZExtValue());
  if (!AlwaysInline && Repeats.Size > Subtarget.getMaxInlineSizeThreshold())
    return SDValue();

  /// If not DWORD aligned, it is more efficient to call the library.  However
  /// if calling the library is not allowed (AlwaysInline), then soldier on as
  /// the code generated here is better than the long load-store sequence we
  /// would otherwise get.
  if (!AlwaysInline && (Align & 3) != 0)
    return SDValue();

  // If to a segment-relative address space, use the default lowering.
  if (DstPtrInfo.getAddrSpace() >= 256 ||
      SrcPtrInfo.getAddrSpace() >= 256)
    return SDValue();

  // If the base register might conflict with our physical registers, bail out.
  const MCPhysReg ClobberSet[] = {X86::RCX, X86::RSI, X86::RDI,
                                  X86::ECX, X86::ESI, X86::EDI};
  if (isBaseRegConflictPossible(DAG, ClobberSet))
    return SDValue();

  // If the target has enhanced REPMOVSB, then it's at least as fast to use
  // REP MOVSB instead of REP MOVS{W,D,Q}, and it avoids having to handle
  // BytesLeft.
  if (!Subtarget.hasERMSB() && !(Align & 1)) {
    if (Align & 2)
      // WORD aligned
      Repeats.AVT = MVT::i16;
    else if (Align & 4)
      // DWORD aligned
      Repeats.AVT = MVT::i32;
    else
      // QWORD aligned
      Repeats.AVT = Subtarget.is64Bit() ? MVT::i64 : MVT::i32;

    if (Repeats.BytesLeft() > 0 &&
        DAG.getMachineFunction().getFunction().optForMinSize()) {
      // When aggressively optimizing for size, avoid generating the code to
      // handle BytesLeft.
      Repeats.AVT = MVT::i8;
    }
  }

  bool Use64BitRegs = Subtarget.isTarget64BitLP64();
  SDValue InFlag;
  Chain = DAG.getCopyToReg(Chain, dl, Use64BitRegs ? X86::RCX : X86::ECX,
                           DAG.getIntPtrConstant(Repeats.Count(), dl), InFlag);
  InFlag = Chain.getValue(1);
  Chain = DAG.getCopyToReg(Chain, dl, Use64BitRegs ? X86::RDI : X86::EDI,
                           Dst, InFlag);
  InFlag = Chain.getValue(1);
  Chain = DAG.getCopyToReg(Chain, dl, Use64BitRegs ? X86::RSI : X86::ESI,
                           Src, InFlag);
  InFlag = Chain.getValue(1);

  SDVTList Tys = DAG.getVTList(MVT::Other, MVT::Glue);
  SDValue Ops[] = { Chain, DAG.getValueType(Repeats.AVT), InFlag };
  SDValue RepMovs = DAG.getNode(X86ISD::REP_MOVS, dl, Tys, Ops);

  SmallVector<SDValue, 4> Results;
  Results.push_back(RepMovs);
  if (Repeats.BytesLeft()) {
    // Handle the last 1 - 7 bytes.
    unsigned Offset = Repeats.Size - Repeats.BytesLeft();
    EVT DstVT = Dst.getValueType();
    EVT SrcVT = Src.getValueType();
    EVT SizeVT = Size.getValueType();
    Results.push_back(DAG.getMemcpy(Chain, dl,
                                    DAG.getNode(ISD::ADD, dl, DstVT, Dst,
                                                DAG.getConstant(Offset, dl,
                                                                DstVT)),
                                    DAG.getNode(ISD::ADD, dl, SrcVT, Src,
                                                DAG.getConstant(Offset, dl,
                                                                SrcVT)),
                                    DAG.getConstant(Repeats.BytesLeft(), dl,
                                                    SizeVT),
                                    Align, isVolatile, AlwaysInline, false,
                                    DstPtrInfo.getWithOffset(Offset),
                                    SrcPtrInfo.getWithOffset(Offset)));
  }

  return DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Results);
}
