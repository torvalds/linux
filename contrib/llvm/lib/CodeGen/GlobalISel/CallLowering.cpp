//===-- lib/CodeGen/GlobalISel/CallLowering.cpp - Call lowering -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements some simple delegations needed for call lowering.
///
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

using namespace llvm;

void CallLowering::anchor() {}

bool CallLowering::lowerCall(
    MachineIRBuilder &MIRBuilder, ImmutableCallSite CS, unsigned ResReg,
    ArrayRef<unsigned> ArgRegs, std::function<unsigned()> GetCalleeReg) const {
  auto &DL = CS.getParent()->getParent()->getParent()->getDataLayout();

  // First step is to marshall all the function's parameters into the correct
  // physregs and memory locations. Gather the sequence of argument types that
  // we'll pass to the assigner function.
  SmallVector<ArgInfo, 8> OrigArgs;
  unsigned i = 0;
  unsigned NumFixedArgs = CS.getFunctionType()->getNumParams();
  for (auto &Arg : CS.args()) {
    ArgInfo OrigArg{ArgRegs[i], Arg->getType(), ISD::ArgFlagsTy{},
                    i < NumFixedArgs};
    setArgFlags(OrigArg, i + AttributeList::FirstArgIndex, DL, CS);
    // We don't currently support swifterror or swiftself args.
    if (OrigArg.Flags.isSwiftError() || OrigArg.Flags.isSwiftSelf())
      return false;
    OrigArgs.push_back(OrigArg);
    ++i;
  }

  MachineOperand Callee = MachineOperand::CreateImm(0);
  if (const Function *F = CS.getCalledFunction())
    Callee = MachineOperand::CreateGA(F, 0);
  else
    Callee = MachineOperand::CreateReg(GetCalleeReg(), false);

  ArgInfo OrigRet{ResReg, CS.getType(), ISD::ArgFlagsTy{}};
  if (!OrigRet.Ty->isVoidTy())
    setArgFlags(OrigRet, AttributeList::ReturnIndex, DL, CS);

  return lowerCall(MIRBuilder, CS.getCallingConv(), Callee, OrigRet, OrigArgs);
}

template <typename FuncInfoTy>
void CallLowering::setArgFlags(CallLowering::ArgInfo &Arg, unsigned OpIdx,
                               const DataLayout &DL,
                               const FuncInfoTy &FuncInfo) const {
  const AttributeList &Attrs = FuncInfo.getAttributes();
  if (Attrs.hasAttribute(OpIdx, Attribute::ZExt))
    Arg.Flags.setZExt();
  if (Attrs.hasAttribute(OpIdx, Attribute::SExt))
    Arg.Flags.setSExt();
  if (Attrs.hasAttribute(OpIdx, Attribute::InReg))
    Arg.Flags.setInReg();
  if (Attrs.hasAttribute(OpIdx, Attribute::StructRet))
    Arg.Flags.setSRet();
  if (Attrs.hasAttribute(OpIdx, Attribute::SwiftSelf))
    Arg.Flags.setSwiftSelf();
  if (Attrs.hasAttribute(OpIdx, Attribute::SwiftError))
    Arg.Flags.setSwiftError();
  if (Attrs.hasAttribute(OpIdx, Attribute::ByVal))
    Arg.Flags.setByVal();
  if (Attrs.hasAttribute(OpIdx, Attribute::InAlloca))
    Arg.Flags.setInAlloca();

  if (Arg.Flags.isByVal() || Arg.Flags.isInAlloca()) {
    Type *ElementTy = cast<PointerType>(Arg.Ty)->getElementType();
    Arg.Flags.setByValSize(DL.getTypeAllocSize(ElementTy));
    // For ByVal, alignment should be passed from FE.  BE will guess if
    // this info is not there but there are cases it cannot get right.
    unsigned FrameAlign;
    if (FuncInfo.getParamAlignment(OpIdx - 2))
      FrameAlign = FuncInfo.getParamAlignment(OpIdx - 2);
    else
      FrameAlign = getTLI()->getByValTypeAlignment(ElementTy, DL);
    Arg.Flags.setByValAlign(FrameAlign);
  }
  if (Attrs.hasAttribute(OpIdx, Attribute::Nest))
    Arg.Flags.setNest();
  Arg.Flags.setOrigAlign(DL.getABITypeAlignment(Arg.Ty));
}

template void
CallLowering::setArgFlags<Function>(CallLowering::ArgInfo &Arg, unsigned OpIdx,
                                    const DataLayout &DL,
                                    const Function &FuncInfo) const;

template void
CallLowering::setArgFlags<CallInst>(CallLowering::ArgInfo &Arg, unsigned OpIdx,
                                    const DataLayout &DL,
                                    const CallInst &FuncInfo) const;

bool CallLowering::handleAssignments(MachineIRBuilder &MIRBuilder,
                                     ArrayRef<ArgInfo> Args,
                                     ValueHandler &Handler) const {
  MachineFunction &MF = MIRBuilder.getMF();
  const Function &F = MF.getFunction();
  const DataLayout &DL = F.getParent()->getDataLayout();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(F.getCallingConv(), F.isVarArg(), MF, ArgLocs, F.getContext());

  unsigned NumArgs = Args.size();
  for (unsigned i = 0; i != NumArgs; ++i) {
    MVT CurVT = MVT::getVT(Args[i].Ty);
    if (Handler.assignArg(i, CurVT, CurVT, CCValAssign::Full, Args[i], CCInfo))
      return false;
  }

  for (unsigned i = 0, e = Args.size(), j = 0; i != e; ++i, ++j) {
    assert(j < ArgLocs.size() && "Skipped too many arg locs");

    CCValAssign &VA = ArgLocs[j];
    assert(VA.getValNo() == i && "Location doesn't correspond to current arg");

    if (VA.needsCustom()) {
      j += Handler.assignCustomValue(Args[i], makeArrayRef(ArgLocs).slice(j));
      continue;
    }

    if (VA.isRegLoc())
      Handler.assignValueToReg(Args[i].Reg, VA.getLocReg(), VA);
    else if (VA.isMemLoc()) {
      unsigned Size = VA.getValVT() == MVT::iPTR
                          ? DL.getPointerSize()
                          : alignTo(VA.getValVT().getSizeInBits(), 8) / 8;
      unsigned Offset = VA.getLocMemOffset();
      MachinePointerInfo MPO;
      unsigned StackAddr = Handler.getStackAddress(Size, Offset, MPO);
      Handler.assignValueToAddress(Args[i].Reg, StackAddr, Size, MPO, VA);
    } else {
      // FIXME: Support byvals and other weirdness
      return false;
    }
  }
  return true;
}

unsigned CallLowering::ValueHandler::extendRegister(unsigned ValReg,
                                                    CCValAssign &VA) {
  LLT LocTy{VA.getLocVT()};
  switch (VA.getLocInfo()) {
  default: break;
  case CCValAssign::Full:
  case CCValAssign::BCvt:
    // FIXME: bitconverting between vector types may or may not be a
    // nop in big-endian situations.
    return ValReg;
  case CCValAssign::AExt: {
    auto MIB = MIRBuilder.buildAnyExt(LocTy, ValReg);
    return MIB->getOperand(0).getReg();
  }
  case CCValAssign::SExt: {
    unsigned NewReg = MRI.createGenericVirtualRegister(LocTy);
    MIRBuilder.buildSExt(NewReg, ValReg);
    return NewReg;
  }
  case CCValAssign::ZExt: {
    unsigned NewReg = MRI.createGenericVirtualRegister(LocTy);
    MIRBuilder.buildZExt(NewReg, ValReg);
    return NewReg;
  }
  }
  llvm_unreachable("unable to extend register");
}

void CallLowering::ValueHandler::anchor() {}
