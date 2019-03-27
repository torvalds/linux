//===-- llvm/lib/Target/AMDGPU/AMDGPUCallLowering.cpp - Call lowering -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the lowering of LLVM calls to machine code calls for
/// GlobalISel.
///
//===----------------------------------------------------------------------===//

#include "AMDGPUCallLowering.h"
#include "AMDGPU.h"
#include "AMDGPUISelLowering.h"
#include "AMDGPUSubtarget.h"
#include "SIISelLowering.h"
#include "SIMachineFunctionInfo.h"
#include "SIRegisterInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

using namespace llvm;

AMDGPUCallLowering::AMDGPUCallLowering(const AMDGPUTargetLowering &TLI)
  : CallLowering(&TLI) {
}

bool AMDGPUCallLowering::lowerReturn(MachineIRBuilder &MIRBuilder,
                                     const Value *Val,
                                     ArrayRef<unsigned> VRegs) const {
  // FIXME: Add support for non-void returns.
  if (Val)
    return false;

  MIRBuilder.buildInstr(AMDGPU::S_ENDPGM);
  return true;
}

unsigned AMDGPUCallLowering::lowerParameterPtr(MachineIRBuilder &MIRBuilder,
                                               Type *ParamTy,
                                               uint64_t Offset) const {

  MachineFunction &MF = MIRBuilder.getMF();
  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const Function &F = MF.getFunction();
  const DataLayout &DL = F.getParent()->getDataLayout();
  PointerType *PtrTy = PointerType::get(ParamTy, AMDGPUAS::CONSTANT_ADDRESS);
  LLT PtrType = getLLTForType(*PtrTy, DL);
  unsigned DstReg = MRI.createGenericVirtualRegister(PtrType);
  unsigned KernArgSegmentPtr =
    MFI->getPreloadedReg(AMDGPUFunctionArgInfo::KERNARG_SEGMENT_PTR);
  unsigned KernArgSegmentVReg = MRI.getLiveInVirtReg(KernArgSegmentPtr);

  unsigned OffsetReg = MRI.createGenericVirtualRegister(LLT::scalar(64));
  MIRBuilder.buildConstant(OffsetReg, Offset);

  MIRBuilder.buildGEP(DstReg, KernArgSegmentVReg, OffsetReg);

  return DstReg;
}

void AMDGPUCallLowering::lowerParameter(MachineIRBuilder &MIRBuilder,
                                        Type *ParamTy, uint64_t Offset,
                                        unsigned Align,
                                        unsigned DstReg) const {
  MachineFunction &MF = MIRBuilder.getMF();
  const Function &F = MF.getFunction();
  const DataLayout &DL = F.getParent()->getDataLayout();
  PointerType *PtrTy = PointerType::get(ParamTy, AMDGPUAS::CONSTANT_ADDRESS);
  MachinePointerInfo PtrInfo(UndefValue::get(PtrTy));
  unsigned TypeSize = DL.getTypeStoreSize(ParamTy);
  unsigned PtrReg = lowerParameterPtr(MIRBuilder, ParamTy, Offset);

  MachineMemOperand *MMO =
      MF.getMachineMemOperand(PtrInfo, MachineMemOperand::MOLoad |
                                       MachineMemOperand::MONonTemporal |
                                       MachineMemOperand::MOInvariant,
                                       TypeSize, Align);

  MIRBuilder.buildLoad(DstReg, PtrReg, *MMO);
}

bool AMDGPUCallLowering::lowerFormalArguments(MachineIRBuilder &MIRBuilder,
                                              const Function &F,
                                              ArrayRef<unsigned> VRegs) const {
  // AMDGPU_GS and AMDGP_HS are not supported yet.
  if (F.getCallingConv() == CallingConv::AMDGPU_GS ||
      F.getCallingConv() == CallingConv::AMDGPU_HS)
    return false;

  MachineFunction &MF = MIRBuilder.getMF();
  const GCNSubtarget *Subtarget = &MF.getSubtarget<GCNSubtarget>();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();
  const SIRegisterInfo *TRI = MF.getSubtarget<GCNSubtarget>().getRegisterInfo();
  const DataLayout &DL = F.getParent()->getDataLayout();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(F.getCallingConv(), F.isVarArg(), MF, ArgLocs, F.getContext());

  // FIXME: How should these inputs interact with inreg / custom SGPR inputs?
  if (Info->hasPrivateSegmentBuffer()) {
    unsigned PrivateSegmentBufferReg = Info->addPrivateSegmentBuffer(*TRI);
    MF.addLiveIn(PrivateSegmentBufferReg, &AMDGPU::SReg_128RegClass);
    CCInfo.AllocateReg(PrivateSegmentBufferReg);
  }

  if (Info->hasDispatchPtr()) {
    unsigned DispatchPtrReg = Info->addDispatchPtr(*TRI);
    // FIXME: Need to add reg as live-in
    CCInfo.AllocateReg(DispatchPtrReg);
  }

  if (Info->hasQueuePtr()) {
    unsigned QueuePtrReg = Info->addQueuePtr(*TRI);
    // FIXME: Need to add reg as live-in
    CCInfo.AllocateReg(QueuePtrReg);
  }

  if (Info->hasKernargSegmentPtr()) {
    unsigned InputPtrReg = Info->addKernargSegmentPtr(*TRI);
    const LLT P2 = LLT::pointer(AMDGPUAS::CONSTANT_ADDRESS, 64);
    unsigned VReg = MRI.createGenericVirtualRegister(P2);
    MRI.addLiveIn(InputPtrReg, VReg);
    MIRBuilder.getMBB().addLiveIn(InputPtrReg);
    MIRBuilder.buildCopy(VReg, InputPtrReg);
    CCInfo.AllocateReg(InputPtrReg);
  }

  if (Info->hasDispatchID()) {
    unsigned DispatchIDReg = Info->addDispatchID(*TRI);
    // FIXME: Need to add reg as live-in
    CCInfo.AllocateReg(DispatchIDReg);
  }

  if (Info->hasFlatScratchInit()) {
    unsigned FlatScratchInitReg = Info->addFlatScratchInit(*TRI);
    // FIXME: Need to add reg as live-in
    CCInfo.AllocateReg(FlatScratchInitReg);
  }

  // The infrastructure for normal calling convention lowering is essentially
  // useless for kernels. We want to avoid any kind of legalization or argument
  // splitting.
  if (F.getCallingConv() == CallingConv::AMDGPU_KERNEL) {
    unsigned i = 0;
    const unsigned KernArgBaseAlign = 16;
    const unsigned BaseOffset = Subtarget->getExplicitKernelArgOffset(F);
    uint64_t ExplicitArgOffset = 0;

    // TODO: Align down to dword alignment and extract bits for extending loads.
    for (auto &Arg : F.args()) {
      Type *ArgTy = Arg.getType();
      unsigned AllocSize = DL.getTypeAllocSize(ArgTy);
      if (AllocSize == 0)
        continue;

      unsigned ABIAlign = DL.getABITypeAlignment(ArgTy);

      uint64_t ArgOffset = alignTo(ExplicitArgOffset, ABIAlign) + BaseOffset;
      ExplicitArgOffset = alignTo(ExplicitArgOffset, ABIAlign) + AllocSize;

      unsigned Align = MinAlign(KernArgBaseAlign, ArgOffset);
      ArgOffset = alignTo(ArgOffset, DL.getABITypeAlignment(ArgTy));
      lowerParameter(MIRBuilder, ArgTy, ArgOffset, Align, VRegs[i]);
      ++i;
    }

    return true;
  }

  unsigned NumArgs = F.arg_size();
  Function::const_arg_iterator CurOrigArg = F.arg_begin();
  const AMDGPUTargetLowering &TLI = *getTLI<AMDGPUTargetLowering>();
  unsigned PSInputNum = 0;
  BitVector Skipped(NumArgs);
  for (unsigned i = 0; i != NumArgs; ++i, ++CurOrigArg) {
    EVT ValEVT = TLI.getValueType(DL, CurOrigArg->getType());

    // We can only hanlde simple value types at the moment.
    ISD::ArgFlagsTy Flags;
    ArgInfo OrigArg{VRegs[i], CurOrigArg->getType()};
    setArgFlags(OrigArg, i + 1, DL, F);
    Flags.setOrigAlign(DL.getABITypeAlignment(CurOrigArg->getType()));

    if (F.getCallingConv() == CallingConv::AMDGPU_PS &&
        !OrigArg.Flags.isInReg() && !OrigArg.Flags.isByVal() &&
        PSInputNum <= 15) {
      if (CurOrigArg->use_empty() && !Info->isPSInputAllocated(PSInputNum)) {
        Skipped.set(i);
        ++PSInputNum;
        continue;
      }

      Info->markPSInputAllocated(PSInputNum);
      if (!CurOrigArg->use_empty())
        Info->markPSInputEnabled(PSInputNum);

      ++PSInputNum;
    }

    CCAssignFn *AssignFn = CCAssignFnForCall(F.getCallingConv(),
                                             /*IsVarArg=*/false);

    if (ValEVT.isVector()) {
      EVT ElemVT = ValEVT.getVectorElementType();
      if (!ValEVT.isSimple())
        return false;
      MVT ValVT = ElemVT.getSimpleVT();
      bool Res = AssignFn(i, ValVT, ValVT, CCValAssign::Full,
                          OrigArg.Flags, CCInfo);
      if (!Res)
        return false;
    } else {
      MVT ValVT = ValEVT.getSimpleVT();
      if (!ValEVT.isSimple())
        return false;
      bool Res =
          AssignFn(i, ValVT, ValVT, CCValAssign::Full, OrigArg.Flags, CCInfo);

      // Fail if we don't know how to handle this type.
      if (Res)
        return false;
    }
  }

  Function::const_arg_iterator Arg = F.arg_begin();

  if (F.getCallingConv() == CallingConv::AMDGPU_VS ||
      F.getCallingConv() == CallingConv::AMDGPU_PS) {
    for (unsigned i = 0, OrigArgIdx = 0;
         OrigArgIdx != NumArgs && i != ArgLocs.size(); ++Arg, ++OrigArgIdx) {
       if (Skipped.test(OrigArgIdx))
          continue;
      CCValAssign &VA = ArgLocs[i++];
      MRI.addLiveIn(VA.getLocReg(), VRegs[OrigArgIdx]);
      MIRBuilder.getMBB().addLiveIn(VA.getLocReg());
      MIRBuilder.buildCopy(VRegs[OrigArgIdx], VA.getLocReg());
    }
    return true;
  }

  return false;
}
