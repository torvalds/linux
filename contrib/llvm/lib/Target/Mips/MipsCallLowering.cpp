//===- MipsCallLowering.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file implements the lowering of LLVM calls to machine code calls for
/// GlobalISel.
//
//===----------------------------------------------------------------------===//

#include "MipsCallLowering.h"
#include "MipsCCState.h"
#include "MipsTargetMachine.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"

using namespace llvm;

MipsCallLowering::MipsCallLowering(const MipsTargetLowering &TLI)
    : CallLowering(&TLI) {}

bool MipsCallLowering::MipsHandler::assign(unsigned VReg,
                                           const CCValAssign &VA) {
  if (VA.isRegLoc()) {
    assignValueToReg(VReg, VA);
  } else if (VA.isMemLoc()) {
    assignValueToAddress(VReg, VA);
  } else {
    return false;
  }
  return true;
}

bool MipsCallLowering::MipsHandler::assignVRegs(ArrayRef<unsigned> VRegs,
                                                ArrayRef<CCValAssign> ArgLocs,
                                                unsigned ArgLocsStartIndex) {
  for (unsigned i = 0; i < VRegs.size(); ++i)
    if (!assign(VRegs[i], ArgLocs[ArgLocsStartIndex + i]))
      return false;
  return true;
}

void MipsCallLowering::MipsHandler::setLeastSignificantFirst(
    SmallVectorImpl<unsigned> &VRegs) {
  if (!MIRBuilder.getMF().getDataLayout().isLittleEndian())
    std::reverse(VRegs.begin(), VRegs.end());
}

bool MipsCallLowering::MipsHandler::handle(
    ArrayRef<CCValAssign> ArgLocs, ArrayRef<CallLowering::ArgInfo> Args) {
  SmallVector<unsigned, 4> VRegs;
  unsigned SplitLength;
  const Function &F = MIRBuilder.getMF().getFunction();
  const DataLayout &DL = F.getParent()->getDataLayout();
  const MipsTargetLowering &TLI = *static_cast<const MipsTargetLowering *>(
      MIRBuilder.getMF().getSubtarget().getTargetLowering());

  for (unsigned ArgsIndex = 0, ArgLocsIndex = 0; ArgsIndex < Args.size();
       ++ArgsIndex, ArgLocsIndex += SplitLength) {
    EVT VT = TLI.getValueType(DL, Args[ArgsIndex].Ty);
    SplitLength = TLI.getNumRegistersForCallingConv(F.getContext(),
                                                    F.getCallingConv(), VT);
    if (SplitLength > 1) {
      VRegs.clear();
      MVT RegisterVT = TLI.getRegisterTypeForCallingConv(
          F.getContext(), F.getCallingConv(), VT);
      for (unsigned i = 0; i < SplitLength; ++i)
        VRegs.push_back(MRI.createGenericVirtualRegister(LLT{RegisterVT}));

      if (!handleSplit(VRegs, ArgLocs, ArgLocsIndex, Args[ArgsIndex].Reg))
        return false;
    } else {
      if (!assign(Args[ArgsIndex].Reg, ArgLocs[ArgLocsIndex]))
        return false;
    }
  }
  return true;
}

namespace {
class IncomingValueHandler : public MipsCallLowering::MipsHandler {
public:
  IncomingValueHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI)
      : MipsHandler(MIRBuilder, MRI) {}

private:
  void assignValueToReg(unsigned ValVReg, const CCValAssign &VA) override;

  unsigned getStackAddress(const CCValAssign &VA,
                           MachineMemOperand *&MMO) override;

  void assignValueToAddress(unsigned ValVReg, const CCValAssign &VA) override;

  bool handleSplit(SmallVectorImpl<unsigned> &VRegs,
                   ArrayRef<CCValAssign> ArgLocs, unsigned ArgLocsStartIndex,
                   unsigned ArgsReg) override;

  virtual void markPhysRegUsed(unsigned PhysReg) {
    MIRBuilder.getMBB().addLiveIn(PhysReg);
  }

  void buildLoad(unsigned Val, const CCValAssign &VA) {
    MachineMemOperand *MMO;
    unsigned Addr = getStackAddress(VA, MMO);
    MIRBuilder.buildLoad(Val, Addr, *MMO);
  }
};

class CallReturnHandler : public IncomingValueHandler {
public:
  CallReturnHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI,
                    MachineInstrBuilder &MIB)
      : IncomingValueHandler(MIRBuilder, MRI), MIB(MIB) {}

private:
  void markPhysRegUsed(unsigned PhysReg) override {
    MIB.addDef(PhysReg, RegState::Implicit);
  }

  MachineInstrBuilder &MIB;
};

} // end anonymous namespace

void IncomingValueHandler::assignValueToReg(unsigned ValVReg,
                                            const CCValAssign &VA) {
  unsigned PhysReg = VA.getLocReg();
  switch (VA.getLocInfo()) {
  case CCValAssign::LocInfo::SExt:
  case CCValAssign::LocInfo::ZExt:
  case CCValAssign::LocInfo::AExt: {
    auto Copy = MIRBuilder.buildCopy(LLT{VA.getLocVT()}, PhysReg);
    MIRBuilder.buildTrunc(ValVReg, Copy);
    break;
  }
  default:
    MIRBuilder.buildCopy(ValVReg, PhysReg);
    break;
  }
  markPhysRegUsed(PhysReg);
}

unsigned IncomingValueHandler::getStackAddress(const CCValAssign &VA,
                                               MachineMemOperand *&MMO) {
  unsigned Size = alignTo(VA.getValVT().getSizeInBits(), 8) / 8;
  unsigned Offset = VA.getLocMemOffset();
  MachineFrameInfo &MFI = MIRBuilder.getMF().getFrameInfo();

  int FI = MFI.CreateFixedObject(Size, Offset, true);
  MachinePointerInfo MPO =
      MachinePointerInfo::getFixedStack(MIRBuilder.getMF(), FI);
  MMO = MIRBuilder.getMF().getMachineMemOperand(MPO, MachineMemOperand::MOLoad,
                                                Size, /* Alignment */ 0);

  unsigned AddrReg = MRI.createGenericVirtualRegister(LLT::pointer(0, 32));
  MIRBuilder.buildFrameIndex(AddrReg, FI);

  return AddrReg;
}

void IncomingValueHandler::assignValueToAddress(unsigned ValVReg,
                                                const CCValAssign &VA) {
  if (VA.getLocInfo() == CCValAssign::SExt ||
      VA.getLocInfo() == CCValAssign::ZExt ||
      VA.getLocInfo() == CCValAssign::AExt) {
    unsigned LoadReg = MRI.createGenericVirtualRegister(LLT::scalar(32));
    buildLoad(LoadReg, VA);
    MIRBuilder.buildTrunc(ValVReg, LoadReg);
  } else
    buildLoad(ValVReg, VA);
}

bool IncomingValueHandler::handleSplit(SmallVectorImpl<unsigned> &VRegs,
                                       ArrayRef<CCValAssign> ArgLocs,
                                       unsigned ArgLocsStartIndex,
                                       unsigned ArgsReg) {
  if (!assignVRegs(VRegs, ArgLocs, ArgLocsStartIndex))
    return false;
  setLeastSignificantFirst(VRegs);
  MIRBuilder.buildMerge(ArgsReg, VRegs);
  return true;
}

namespace {
class OutgoingValueHandler : public MipsCallLowering::MipsHandler {
public:
  OutgoingValueHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI,
                       MachineInstrBuilder &MIB)
      : MipsHandler(MIRBuilder, MRI), MIB(MIB) {}

private:
  void assignValueToReg(unsigned ValVReg, const CCValAssign &VA) override;

  unsigned getStackAddress(const CCValAssign &VA,
                           MachineMemOperand *&MMO) override;

  void assignValueToAddress(unsigned ValVReg, const CCValAssign &VA) override;

  bool handleSplit(SmallVectorImpl<unsigned> &VRegs,
                   ArrayRef<CCValAssign> ArgLocs, unsigned ArgLocsStartIndex,
                   unsigned ArgsReg) override;

  unsigned extendRegister(unsigned ValReg, const CCValAssign &VA);

  MachineInstrBuilder &MIB;
};
} // end anonymous namespace

void OutgoingValueHandler::assignValueToReg(unsigned ValVReg,
                                            const CCValAssign &VA) {
  unsigned PhysReg = VA.getLocReg();
  unsigned ExtReg = extendRegister(ValVReg, VA);
  MIRBuilder.buildCopy(PhysReg, ExtReg);
  MIB.addUse(PhysReg, RegState::Implicit);
}

unsigned OutgoingValueHandler::getStackAddress(const CCValAssign &VA,
                                               MachineMemOperand *&MMO) {
  LLT p0 = LLT::pointer(0, 32);
  LLT s32 = LLT::scalar(32);
  unsigned SPReg = MRI.createGenericVirtualRegister(p0);
  MIRBuilder.buildCopy(SPReg, Mips::SP);

  unsigned OffsetReg = MRI.createGenericVirtualRegister(s32);
  unsigned Offset = VA.getLocMemOffset();
  MIRBuilder.buildConstant(OffsetReg, Offset);

  unsigned AddrReg = MRI.createGenericVirtualRegister(p0);
  MIRBuilder.buildGEP(AddrReg, SPReg, OffsetReg);

  MachinePointerInfo MPO =
      MachinePointerInfo::getStack(MIRBuilder.getMF(), Offset);
  unsigned Size = alignTo(VA.getValVT().getSizeInBits(), 8) / 8;
  MMO = MIRBuilder.getMF().getMachineMemOperand(MPO, MachineMemOperand::MOStore,
                                                Size, /* Alignment */ 0);

  return AddrReg;
}

void OutgoingValueHandler::assignValueToAddress(unsigned ValVReg,
                                                const CCValAssign &VA) {
  MachineMemOperand *MMO;
  unsigned Addr = getStackAddress(VA, MMO);
  unsigned ExtReg = extendRegister(ValVReg, VA);
  MIRBuilder.buildStore(ExtReg, Addr, *MMO);
}

unsigned OutgoingValueHandler::extendRegister(unsigned ValReg,
                                              const CCValAssign &VA) {
  LLT LocTy{VA.getLocVT()};
  switch (VA.getLocInfo()) {
  case CCValAssign::SExt: {
    unsigned ExtReg = MRI.createGenericVirtualRegister(LocTy);
    MIRBuilder.buildSExt(ExtReg, ValReg);
    return ExtReg;
  }
  case CCValAssign::ZExt: {
    unsigned ExtReg = MRI.createGenericVirtualRegister(LocTy);
    MIRBuilder.buildZExt(ExtReg, ValReg);
    return ExtReg;
  }
  case CCValAssign::AExt: {
    unsigned ExtReg = MRI.createGenericVirtualRegister(LocTy);
    MIRBuilder.buildAnyExt(ExtReg, ValReg);
    return ExtReg;
  }
  // TODO : handle upper extends
  case CCValAssign::Full:
    return ValReg;
  default:
    break;
  }
  llvm_unreachable("unable to extend register");
}

bool OutgoingValueHandler::handleSplit(SmallVectorImpl<unsigned> &VRegs,
                                       ArrayRef<CCValAssign> ArgLocs,
                                       unsigned ArgLocsStartIndex,
                                       unsigned ArgsReg) {
  MIRBuilder.buildUnmerge(VRegs, ArgsReg);
  setLeastSignificantFirst(VRegs);
  if (!assignVRegs(VRegs, ArgLocs, ArgLocsStartIndex))
    return false;

  return true;
}

static bool isSupportedType(Type *T) {
  if (T->isIntegerTy())
    return true;
  if (T->isPointerTy())
    return true;
  return false;
}

static CCValAssign::LocInfo determineLocInfo(const MVT RegisterVT, const EVT VT,
                                             const ISD::ArgFlagsTy &Flags) {
  // > does not mean loss of information as type RegisterVT can't hold type VT,
  // it means that type VT is split into multiple registers of type RegisterVT
  if (VT.getSizeInBits() >= RegisterVT.getSizeInBits())
    return CCValAssign::LocInfo::Full;
  if (Flags.isSExt())
    return CCValAssign::LocInfo::SExt;
  if (Flags.isZExt())
    return CCValAssign::LocInfo::ZExt;
  return CCValAssign::LocInfo::AExt;
}

template <typename T>
static void setLocInfo(SmallVectorImpl<CCValAssign> &ArgLocs,
                       const SmallVectorImpl<T> &Arguments) {
  for (unsigned i = 0; i < ArgLocs.size(); ++i) {
    const CCValAssign &VA = ArgLocs[i];
    CCValAssign::LocInfo LocInfo = determineLocInfo(
        Arguments[i].VT, Arguments[i].ArgVT, Arguments[i].Flags);
    if (VA.isMemLoc())
      ArgLocs[i] =
          CCValAssign::getMem(VA.getValNo(), VA.getValVT(),
                              VA.getLocMemOffset(), VA.getLocVT(), LocInfo);
    else
      ArgLocs[i] = CCValAssign::getReg(VA.getValNo(), VA.getValVT(),
                                       VA.getLocReg(), VA.getLocVT(), LocInfo);
  }
}

bool MipsCallLowering::lowerReturn(MachineIRBuilder &MIRBuilder,
                                   const Value *Val,
                                   ArrayRef<unsigned> VRegs) const {

  MachineInstrBuilder Ret = MIRBuilder.buildInstrNoInsert(Mips::RetRA);

  if (Val != nullptr && !isSupportedType(Val->getType()))
    return false;

  if (!VRegs.empty()) {
    MachineFunction &MF = MIRBuilder.getMF();
    const Function &F = MF.getFunction();
    const DataLayout &DL = MF.getDataLayout();
    const MipsTargetLowering &TLI = *getTLI<MipsTargetLowering>();
    LLVMContext &Ctx = Val->getType()->getContext();

    SmallVector<EVT, 4> SplitEVTs;
    ComputeValueVTs(TLI, DL, Val->getType(), SplitEVTs);
    assert(VRegs.size() == SplitEVTs.size() &&
           "For each split Type there should be exactly one VReg.");

    SmallVector<ArgInfo, 8> RetInfos;
    SmallVector<unsigned, 8> OrigArgIndices;

    for (unsigned i = 0; i < SplitEVTs.size(); ++i) {
      ArgInfo CurArgInfo = ArgInfo{VRegs[i], SplitEVTs[i].getTypeForEVT(Ctx)};
      setArgFlags(CurArgInfo, AttributeList::ReturnIndex, DL, F);
      splitToValueTypes(CurArgInfo, 0, RetInfos, OrigArgIndices);
    }

    SmallVector<ISD::OutputArg, 8> Outs;
    subTargetRegTypeForCallingConv(F, RetInfos, OrigArgIndices, Outs);

    SmallVector<CCValAssign, 16> ArgLocs;
    MipsCCState CCInfo(F.getCallingConv(), F.isVarArg(), MF, ArgLocs,
                       F.getContext());
    CCInfo.AnalyzeReturn(Outs, TLI.CCAssignFnForReturn());
    setLocInfo(ArgLocs, Outs);

    OutgoingValueHandler RetHandler(MIRBuilder, MF.getRegInfo(), Ret);
    if (!RetHandler.handle(ArgLocs, RetInfos)) {
      return false;
    }
  }
  MIRBuilder.insertInstr(Ret);
  return true;
}

bool MipsCallLowering::lowerFormalArguments(MachineIRBuilder &MIRBuilder,
                                            const Function &F,
                                            ArrayRef<unsigned> VRegs) const {

  // Quick exit if there aren't any args.
  if (F.arg_empty())
    return true;

  if (F.isVarArg()) {
    return false;
  }

  for (auto &Arg : F.args()) {
    if (!isSupportedType(Arg.getType()))
      return false;
  }

  MachineFunction &MF = MIRBuilder.getMF();
  const DataLayout &DL = MF.getDataLayout();
  const MipsTargetLowering &TLI = *getTLI<MipsTargetLowering>();

  SmallVector<ArgInfo, 8> ArgInfos;
  SmallVector<unsigned, 8> OrigArgIndices;
  unsigned i = 0;
  for (auto &Arg : F.args()) {
    ArgInfo AInfo(VRegs[i], Arg.getType());
    setArgFlags(AInfo, i + AttributeList::FirstArgIndex, DL, F);
    splitToValueTypes(AInfo, i, ArgInfos, OrigArgIndices);
    ++i;
  }

  SmallVector<ISD::InputArg, 8> Ins;
  subTargetRegTypeForCallingConv(F, ArgInfos, OrigArgIndices, Ins);

  SmallVector<CCValAssign, 16> ArgLocs;
  MipsCCState CCInfo(F.getCallingConv(), F.isVarArg(), MF, ArgLocs,
                     F.getContext());

  const MipsTargetMachine &TM =
      static_cast<const MipsTargetMachine &>(MF.getTarget());
  const MipsABIInfo &ABI = TM.getABI();
  CCInfo.AllocateStack(ABI.GetCalleeAllocdArgSizeInBytes(F.getCallingConv()),
                       1);
  CCInfo.AnalyzeFormalArguments(Ins, TLI.CCAssignFnForCall());
  setLocInfo(ArgLocs, Ins);

  IncomingValueHandler Handler(MIRBuilder, MF.getRegInfo());
  if (!Handler.handle(ArgLocs, ArgInfos))
    return false;

  return true;
}

bool MipsCallLowering::lowerCall(MachineIRBuilder &MIRBuilder,
                                 CallingConv::ID CallConv,
                                 const MachineOperand &Callee,
                                 const ArgInfo &OrigRet,
                                 ArrayRef<ArgInfo> OrigArgs) const {

  if (CallConv != CallingConv::C)
    return false;

  for (auto &Arg : OrigArgs) {
    if (!isSupportedType(Arg.Ty))
      return false;
    if (Arg.Flags.isByVal() || Arg.Flags.isSRet())
      return false;
  }
  if (OrigRet.Reg && !isSupportedType(OrigRet.Ty))
    return false;

  MachineFunction &MF = MIRBuilder.getMF();
  const Function &F = MF.getFunction();
  const MipsTargetLowering &TLI = *getTLI<MipsTargetLowering>();
  const MipsTargetMachine &TM =
      static_cast<const MipsTargetMachine &>(MF.getTarget());
  const MipsABIInfo &ABI = TM.getABI();

  MachineInstrBuilder CallSeqStart =
      MIRBuilder.buildInstr(Mips::ADJCALLSTACKDOWN);

  // FIXME: Add support for pic calling sequences, long call sequences for O32,
  //       N32 and N64. First handle the case when Callee.isReg().
  if (Callee.isReg())
    return false;

  MachineInstrBuilder MIB = MIRBuilder.buildInstrNoInsert(Mips::JAL);
  MIB.addDef(Mips::SP, RegState::Implicit);
  MIB.add(Callee);
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  MIB.addRegMask(TRI->getCallPreservedMask(MF, F.getCallingConv()));

  TargetLowering::ArgListTy FuncOrigArgs;
  FuncOrigArgs.reserve(OrigArgs.size());

  SmallVector<ArgInfo, 8> ArgInfos;
  SmallVector<unsigned, 8> OrigArgIndices;
  unsigned i = 0;
  for (auto &Arg : OrigArgs) {

    TargetLowering::ArgListEntry Entry;
    Entry.Ty = Arg.Ty;
    FuncOrigArgs.push_back(Entry);

    splitToValueTypes(Arg, i, ArgInfos, OrigArgIndices);
    ++i;
  }

  SmallVector<ISD::OutputArg, 8> Outs;
  subTargetRegTypeForCallingConv(F, ArgInfos, OrigArgIndices, Outs);

  SmallVector<CCValAssign, 8> ArgLocs;
  MipsCCState CCInfo(F.getCallingConv(), F.isVarArg(), MF, ArgLocs,
                     F.getContext());

  CCInfo.AllocateStack(ABI.GetCalleeAllocdArgSizeInBytes(CallConv), 1);
  const char *Call = Callee.isSymbol() ? Callee.getSymbolName() : nullptr;
  CCInfo.AnalyzeCallOperands(Outs, TLI.CCAssignFnForCall(), FuncOrigArgs, Call);
  setLocInfo(ArgLocs, Outs);

  OutgoingValueHandler RetHandler(MIRBuilder, MF.getRegInfo(), MIB);
  if (!RetHandler.handle(ArgLocs, ArgInfos)) {
    return false;
  }

  unsigned NextStackOffset = CCInfo.getNextStackOffset();
  const TargetFrameLowering *TFL = MF.getSubtarget().getFrameLowering();
  unsigned StackAlignment = TFL->getStackAlignment();
  NextStackOffset = alignTo(NextStackOffset, StackAlignment);
  CallSeqStart.addImm(NextStackOffset).addImm(0);

  MIRBuilder.insertInstr(MIB);

  if (OrigRet.Reg) {

    ArgInfos.clear();
    SmallVector<unsigned, 8> OrigRetIndices;

    splitToValueTypes(OrigRet, 0, ArgInfos, OrigRetIndices);

    SmallVector<ISD::InputArg, 8> Ins;
    subTargetRegTypeForCallingConv(F, ArgInfos, OrigRetIndices, Ins);

    SmallVector<CCValAssign, 8> ArgLocs;
    MipsCCState CCInfo(F.getCallingConv(), F.isVarArg(), MF, ArgLocs,
                       F.getContext());

    CCInfo.AnalyzeCallResult(Ins, TLI.CCAssignFnForReturn(), OrigRet.Ty, Call);
    setLocInfo(ArgLocs, Ins);

    CallReturnHandler Handler(MIRBuilder, MF.getRegInfo(), MIB);
    if (!Handler.handle(ArgLocs, ArgInfos))
      return false;
  }

  MIRBuilder.buildInstr(Mips::ADJCALLSTACKUP).addImm(NextStackOffset).addImm(0);

  return true;
}

template <typename T>
void MipsCallLowering::subTargetRegTypeForCallingConv(
    const Function &F, ArrayRef<ArgInfo> Args,
    ArrayRef<unsigned> OrigArgIndices, SmallVectorImpl<T> &ISDArgs) const {
  const DataLayout &DL = F.getParent()->getDataLayout();
  const MipsTargetLowering &TLI = *getTLI<MipsTargetLowering>();

  unsigned ArgNo = 0;
  for (auto &Arg : Args) {

    EVT VT = TLI.getValueType(DL, Arg.Ty);
    MVT RegisterVT = TLI.getRegisterTypeForCallingConv(F.getContext(),
                                                       F.getCallingConv(), VT);
    unsigned NumRegs = TLI.getNumRegistersForCallingConv(
        F.getContext(), F.getCallingConv(), VT);

    for (unsigned i = 0; i < NumRegs; ++i) {
      ISD::ArgFlagsTy Flags = Arg.Flags;

      if (i == 0)
        Flags.setOrigAlign(TLI.getABIAlignmentForCallingConv(Arg.Ty, DL));
      else
        Flags.setOrigAlign(1);

      ISDArgs.emplace_back(Flags, RegisterVT, VT, true, OrigArgIndices[ArgNo],
                           0);
    }
    ++ArgNo;
  }
}

void MipsCallLowering::splitToValueTypes(
    const ArgInfo &OrigArg, unsigned OriginalIndex,
    SmallVectorImpl<ArgInfo> &SplitArgs,
    SmallVectorImpl<unsigned> &SplitArgsOrigIndices) const {

  // TODO : perform structure and array split. For now we only deal with
  // types that pass isSupportedType check.
  SplitArgs.push_back(OrigArg);
  SplitArgsOrigIndices.push_back(OriginalIndex);
}
