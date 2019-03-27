//===----------------------- SIFrameLowering.cpp --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//==-----------------------------------------------------------------------===//

#include "SIFrameLowering.h"
#include "AMDGPUSubtarget.h"
#include "SIInstrInfo.h"
#include "SIMachineFunctionInfo.h"
#include "SIRegisterInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"

#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"

using namespace llvm;


static ArrayRef<MCPhysReg> getAllSGPR128(const GCNSubtarget &ST,
                                         const MachineFunction &MF) {
  return makeArrayRef(AMDGPU::SGPR_128RegClass.begin(),
                      ST.getMaxNumSGPRs(MF) / 4);
}

static ArrayRef<MCPhysReg> getAllSGPRs(const GCNSubtarget &ST,
                                       const MachineFunction &MF) {
  return makeArrayRef(AMDGPU::SGPR_32RegClass.begin(),
                      ST.getMaxNumSGPRs(MF));
}

void SIFrameLowering::emitFlatScratchInit(const GCNSubtarget &ST,
                                          MachineFunction &MF,
                                          MachineBasicBlock &MBB) const {
  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo* TRI = &TII->getRegisterInfo();
  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();

  // We don't need this if we only have spills since there is no user facing
  // scratch.

  // TODO: If we know we don't have flat instructions earlier, we can omit
  // this from the input registers.
  //
  // TODO: We only need to know if we access scratch space through a flat
  // pointer. Because we only detect if flat instructions are used at all,
  // this will be used more often than necessary on VI.

  // Debug location must be unknown since the first debug location is used to
  // determine the end of the prologue.
  DebugLoc DL;
  MachineBasicBlock::iterator I = MBB.begin();

  unsigned FlatScratchInitReg
    = MFI->getPreloadedReg(AMDGPUFunctionArgInfo::FLAT_SCRATCH_INIT);

  MachineRegisterInfo &MRI = MF.getRegInfo();
  MRI.addLiveIn(FlatScratchInitReg);
  MBB.addLiveIn(FlatScratchInitReg);

  unsigned FlatScrInitLo = TRI->getSubReg(FlatScratchInitReg, AMDGPU::sub0);
  unsigned FlatScrInitHi = TRI->getSubReg(FlatScratchInitReg, AMDGPU::sub1);

  unsigned ScratchWaveOffsetReg = MFI->getScratchWaveOffsetReg();

  // Do a 64-bit pointer add.
  if (ST.flatScratchIsPointer()) {
    BuildMI(MBB, I, DL, TII->get(AMDGPU::S_ADD_U32), AMDGPU::FLAT_SCR_LO)
      .addReg(FlatScrInitLo)
      .addReg(ScratchWaveOffsetReg);
    BuildMI(MBB, I, DL, TII->get(AMDGPU::S_ADDC_U32), AMDGPU::FLAT_SCR_HI)
      .addReg(FlatScrInitHi)
      .addImm(0);

    return;
  }

  // Copy the size in bytes.
  BuildMI(MBB, I, DL, TII->get(AMDGPU::COPY), AMDGPU::FLAT_SCR_LO)
    .addReg(FlatScrInitHi, RegState::Kill);

  // Add wave offset in bytes to private base offset.
  // See comment in AMDKernelCodeT.h for enable_sgpr_flat_scratch_init.
  BuildMI(MBB, I, DL, TII->get(AMDGPU::S_ADD_U32), FlatScrInitLo)
    .addReg(FlatScrInitLo)
    .addReg(ScratchWaveOffsetReg);

  // Convert offset to 256-byte units.
  BuildMI(MBB, I, DL, TII->get(AMDGPU::S_LSHR_B32), AMDGPU::FLAT_SCR_HI)
    .addReg(FlatScrInitLo, RegState::Kill)
    .addImm(8);
}

unsigned SIFrameLowering::getReservedPrivateSegmentBufferReg(
  const GCNSubtarget &ST,
  const SIInstrInfo *TII,
  const SIRegisterInfo *TRI,
  SIMachineFunctionInfo *MFI,
  MachineFunction &MF) const {
  MachineRegisterInfo &MRI = MF.getRegInfo();

  // We need to insert initialization of the scratch resource descriptor.
  unsigned ScratchRsrcReg = MFI->getScratchRSrcReg();
  if (ScratchRsrcReg == AMDGPU::NoRegister ||
      !MRI.isPhysRegUsed(ScratchRsrcReg))
    return AMDGPU::NoRegister;

  if (ST.hasSGPRInitBug() ||
      ScratchRsrcReg != TRI->reservedPrivateSegmentBufferReg(MF))
    return ScratchRsrcReg;

  // We reserved the last registers for this. Shift it down to the end of those
  // which were actually used.
  //
  // FIXME: It might be safer to use a pseudoregister before replacement.

  // FIXME: We should be able to eliminate unused input registers. We only
  // cannot do this for the resources required for scratch access. For now we
  // skip over user SGPRs and may leave unused holes.

  // We find the resource first because it has an alignment requirement.

  unsigned NumPreloaded = (MFI->getNumPreloadedSGPRs() + 3) / 4;
  ArrayRef<MCPhysReg> AllSGPR128s = getAllSGPR128(ST, MF);
  AllSGPR128s = AllSGPR128s.slice(std::min(static_cast<unsigned>(AllSGPR128s.size()), NumPreloaded));

  // Skip the last N reserved elements because they should have already been
  // reserved for VCC etc.
  for (MCPhysReg Reg : AllSGPR128s) {
    // Pick the first unallocated one. Make sure we don't clobber the other
    // reserved input we needed.
    if (!MRI.isPhysRegUsed(Reg) && MRI.isAllocatable(Reg)) {
      MRI.replaceRegWith(ScratchRsrcReg, Reg);
      MFI->setScratchRSrcReg(Reg);
      return Reg;
    }
  }

  return ScratchRsrcReg;
}

// Shift down registers reserved for the scratch wave offset and stack pointer
// SGPRs.
std::pair<unsigned, unsigned>
SIFrameLowering::getReservedPrivateSegmentWaveByteOffsetReg(
  const GCNSubtarget &ST,
  const SIInstrInfo *TII,
  const SIRegisterInfo *TRI,
  SIMachineFunctionInfo *MFI,
  MachineFunction &MF) const {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  unsigned ScratchWaveOffsetReg = MFI->getScratchWaveOffsetReg();

  // No replacement necessary.
  if (ScratchWaveOffsetReg == AMDGPU::NoRegister ||
      !MRI.isPhysRegUsed(ScratchWaveOffsetReg)) {
    assert(MFI->getStackPtrOffsetReg() == AMDGPU::SP_REG);
    return std::make_pair(AMDGPU::NoRegister, AMDGPU::NoRegister);
  }

  unsigned SPReg = MFI->getStackPtrOffsetReg();
  if (ST.hasSGPRInitBug())
    return std::make_pair(ScratchWaveOffsetReg, SPReg);

  unsigned NumPreloaded = MFI->getNumPreloadedSGPRs();

  ArrayRef<MCPhysReg> AllSGPRs = getAllSGPRs(ST, MF);
  if (NumPreloaded > AllSGPRs.size())
    return std::make_pair(ScratchWaveOffsetReg, SPReg);

  AllSGPRs = AllSGPRs.slice(NumPreloaded);

  // We need to drop register from the end of the list that we cannot use
  // for the scratch wave offset.
  // + 2 s102 and s103 do not exist on VI.
  // + 2 for vcc
  // + 2 for xnack_mask
  // + 2 for flat_scratch
  // + 4 for registers reserved for scratch resource register
  // + 1 for register reserved for scratch wave offset.  (By exluding this
  //     register from the list to consider, it means that when this
  //     register is being used for the scratch wave offset and there
  //     are no other free SGPRs, then the value will stay in this register.
  // + 1 if stack pointer is used.
  // ----
  //  13 (+1)
  unsigned ReservedRegCount = 13;

  if (AllSGPRs.size() < ReservedRegCount)
    return std::make_pair(ScratchWaveOffsetReg, SPReg);

  bool HandledScratchWaveOffsetReg =
    ScratchWaveOffsetReg != TRI->reservedPrivateSegmentWaveByteOffsetReg(MF);

  for (MCPhysReg Reg : AllSGPRs.drop_back(ReservedRegCount)) {
    // Pick the first unallocated SGPR. Be careful not to pick an alias of the
    // scratch descriptor, since we haven’t added its uses yet.
    if (!MRI.isPhysRegUsed(Reg) && MRI.isAllocatable(Reg)) {
      if (!HandledScratchWaveOffsetReg) {
        HandledScratchWaveOffsetReg = true;

        MRI.replaceRegWith(ScratchWaveOffsetReg, Reg);
        MFI->setScratchWaveOffsetReg(Reg);
        ScratchWaveOffsetReg = Reg;
        break;
      }
    }
  }

  return std::make_pair(ScratchWaveOffsetReg, SPReg);
}

void SIFrameLowering::emitEntryFunctionPrologue(MachineFunction &MF,
                                                MachineBasicBlock &MBB) const {
  // Emit debugger prologue if "amdgpu-debugger-emit-prologue" attribute was
  // specified.
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  if (ST.debuggerEmitPrologue())
    emitDebuggerPrologue(MF, MBB);

  assert(&MF.front() == &MBB && "Shrink-wrapping not yet supported");

  SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();

  // If we only have SGPR spills, we won't actually be using scratch memory
  // since these spill to VGPRs.
  //
  // FIXME: We should be cleaning up these unused SGPR spill frame indices
  // somewhere.

  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo *TRI = &TII->getRegisterInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const Function &F = MF.getFunction();

  // We need to do the replacement of the private segment buffer and wave offset
  // register even if there are no stack objects. There could be stores to undef
  // or a constant without an associated object.

  // FIXME: We still have implicit uses on SGPR spill instructions in case they
  // need to spill to vector memory. It's likely that will not happen, but at
  // this point it appears we need the setup. This part of the prolog should be
  // emitted after frame indices are eliminated.

  if (MFI->hasFlatScratchInit())
    emitFlatScratchInit(ST, MF, MBB);

  unsigned SPReg = MFI->getStackPtrOffsetReg();
  if (SPReg != AMDGPU::SP_REG) {
    assert(MRI.isReserved(SPReg) && "SPReg used but not reserved");

    DebugLoc DL;
    const MachineFrameInfo &FrameInfo = MF.getFrameInfo();
    int64_t StackSize = FrameInfo.getStackSize();

    if (StackSize == 0) {
      BuildMI(MBB, MBB.begin(), DL, TII->get(AMDGPU::COPY), SPReg)
        .addReg(MFI->getScratchWaveOffsetReg());
    } else {
      BuildMI(MBB, MBB.begin(), DL, TII->get(AMDGPU::S_ADD_U32), SPReg)
        .addReg(MFI->getScratchWaveOffsetReg())
        .addImm(StackSize * ST.getWavefrontSize());
    }
  }

  unsigned ScratchRsrcReg
    = getReservedPrivateSegmentBufferReg(ST, TII, TRI, MFI, MF);

  unsigned ScratchWaveOffsetReg;
  std::tie(ScratchWaveOffsetReg, SPReg)
    = getReservedPrivateSegmentWaveByteOffsetReg(ST, TII, TRI, MFI, MF);

  // It's possible to have uses of only ScratchWaveOffsetReg without
  // ScratchRsrcReg if it's only used for the initialization of flat_scratch,
  // but the inverse is not true.
  if (ScratchWaveOffsetReg == AMDGPU::NoRegister) {
    assert(ScratchRsrcReg == AMDGPU::NoRegister);
    return;
  }

  // We need to insert initialization of the scratch resource descriptor.
  unsigned PreloadedScratchWaveOffsetReg = MFI->getPreloadedReg(
    AMDGPUFunctionArgInfo::PRIVATE_SEGMENT_WAVE_BYTE_OFFSET);

  unsigned PreloadedPrivateBufferReg = AMDGPU::NoRegister;
  if (ST.isAmdHsaOrMesa(F)) {
    PreloadedPrivateBufferReg = MFI->getPreloadedReg(
      AMDGPUFunctionArgInfo::PRIVATE_SEGMENT_BUFFER);
  }

  bool OffsetRegUsed = MRI.isPhysRegUsed(ScratchWaveOffsetReg);
  bool ResourceRegUsed = ScratchRsrcReg != AMDGPU::NoRegister &&
                         MRI.isPhysRegUsed(ScratchRsrcReg);

  // We added live-ins during argument lowering, but since they were not used
  // they were deleted. We're adding the uses now, so add them back.
  if (OffsetRegUsed) {
    assert(PreloadedScratchWaveOffsetReg != AMDGPU::NoRegister &&
           "scratch wave offset input is required");
    MRI.addLiveIn(PreloadedScratchWaveOffsetReg);
    MBB.addLiveIn(PreloadedScratchWaveOffsetReg);
  }

  if (ResourceRegUsed && PreloadedPrivateBufferReg != AMDGPU::NoRegister) {
    assert(ST.isAmdHsaOrMesa(F) || ST.isMesaGfxShader(F));
    MRI.addLiveIn(PreloadedPrivateBufferReg);
    MBB.addLiveIn(PreloadedPrivateBufferReg);
  }

  // Make the register selected live throughout the function.
  for (MachineBasicBlock &OtherBB : MF) {
    if (&OtherBB == &MBB)
      continue;

    if (OffsetRegUsed)
      OtherBB.addLiveIn(ScratchWaveOffsetReg);

    if (ResourceRegUsed)
      OtherBB.addLiveIn(ScratchRsrcReg);
  }

  DebugLoc DL;
  MachineBasicBlock::iterator I = MBB.begin();

  // If we reserved the original input registers, we don't need to copy to the
  // reserved registers.

  bool CopyBuffer = ResourceRegUsed &&
    PreloadedPrivateBufferReg != AMDGPU::NoRegister &&
    ST.isAmdHsaOrMesa(F) &&
    ScratchRsrcReg != PreloadedPrivateBufferReg;

  // This needs to be careful of the copying order to avoid overwriting one of
  // the input registers before it's been copied to it's final
  // destination. Usually the offset should be copied first.
  bool CopyBufferFirst = TRI->isSubRegisterEq(PreloadedPrivateBufferReg,
                                              ScratchWaveOffsetReg);
  if (CopyBuffer && CopyBufferFirst) {
    BuildMI(MBB, I, DL, TII->get(AMDGPU::COPY), ScratchRsrcReg)
      .addReg(PreloadedPrivateBufferReg, RegState::Kill);
  }

  if (OffsetRegUsed &&
      PreloadedScratchWaveOffsetReg != ScratchWaveOffsetReg) {
    BuildMI(MBB, I, DL, TII->get(AMDGPU::COPY), ScratchWaveOffsetReg)
      .addReg(PreloadedScratchWaveOffsetReg,
              MRI.isPhysRegUsed(ScratchWaveOffsetReg) ? 0 : RegState::Kill);
  }

  if (CopyBuffer && !CopyBufferFirst) {
    BuildMI(MBB, I, DL, TII->get(AMDGPU::COPY), ScratchRsrcReg)
      .addReg(PreloadedPrivateBufferReg, RegState::Kill);
  }

  if (ResourceRegUsed)
    emitEntryFunctionScratchSetup(ST, MF, MBB, MFI, I,
        PreloadedPrivateBufferReg, ScratchRsrcReg);
}

// Emit scratch setup code for AMDPAL or Mesa, assuming ResourceRegUsed is set.
void SIFrameLowering::emitEntryFunctionScratchSetup(const GCNSubtarget &ST,
      MachineFunction &MF, MachineBasicBlock &MBB, SIMachineFunctionInfo *MFI,
      MachineBasicBlock::iterator I, unsigned PreloadedPrivateBufferReg,
      unsigned ScratchRsrcReg) const {

  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo *TRI = &TII->getRegisterInfo();
  const Function &Fn = MF.getFunction();
  DebugLoc DL;

  if (ST.isAmdPalOS()) {
    // The pointer to the GIT is formed from the offset passed in and either
    // the amdgpu-git-ptr-high function attribute or the top part of the PC
    unsigned RsrcLo = TRI->getSubReg(ScratchRsrcReg, AMDGPU::sub0);
    unsigned RsrcHi = TRI->getSubReg(ScratchRsrcReg, AMDGPU::sub1);
    unsigned Rsrc01 = TRI->getSubReg(ScratchRsrcReg, AMDGPU::sub0_sub1);

    const MCInstrDesc &SMovB32 = TII->get(AMDGPU::S_MOV_B32);

    if (MFI->getGITPtrHigh() != 0xffffffff) {
      BuildMI(MBB, I, DL, SMovB32, RsrcHi)
        .addImm(MFI->getGITPtrHigh())
        .addReg(ScratchRsrcReg, RegState::ImplicitDefine);
    } else {
      const MCInstrDesc &GetPC64 = TII->get(AMDGPU::S_GETPC_B64);
      BuildMI(MBB, I, DL, GetPC64, Rsrc01);
    }
    auto GitPtrLo = AMDGPU::SGPR0; // Low GIT address passed in
    if (ST.hasMergedShaders()) {
      switch (MF.getFunction().getCallingConv()) {
        case CallingConv::AMDGPU_HS:
        case CallingConv::AMDGPU_GS:
          // Low GIT address is passed in s8 rather than s0 for an LS+HS or
          // ES+GS merged shader on gfx9+.
          GitPtrLo = AMDGPU::SGPR8;
          break;
        default:
          break;
      }
    }
    MF.getRegInfo().addLiveIn(GitPtrLo);
    MF.front().addLiveIn(GitPtrLo);
    BuildMI(MBB, I, DL, SMovB32, RsrcLo)
      .addReg(GitPtrLo)
      .addReg(ScratchRsrcReg, RegState::ImplicitDefine);

    // We now have the GIT ptr - now get the scratch descriptor from the entry
    // at offset 0 (or offset 16 for a compute shader).
    PointerType *PtrTy =
      PointerType::get(Type::getInt64Ty(MF.getFunction().getContext()),
                       AMDGPUAS::CONSTANT_ADDRESS);
    MachinePointerInfo PtrInfo(UndefValue::get(PtrTy));
    const MCInstrDesc &LoadDwordX4 = TII->get(AMDGPU::S_LOAD_DWORDX4_IMM);
    auto MMO = MF.getMachineMemOperand(PtrInfo,
                                       MachineMemOperand::MOLoad |
                                       MachineMemOperand::MOInvariant |
                                       MachineMemOperand::MODereferenceable,
                                       0, 0);
    unsigned Offset = Fn.getCallingConv() == CallingConv::AMDGPU_CS ? 16 : 0;
    BuildMI(MBB, I, DL, LoadDwordX4, ScratchRsrcReg)
      .addReg(Rsrc01)
      .addImm(Offset) // offset
      .addImm(0) // glc
      .addReg(ScratchRsrcReg, RegState::ImplicitDefine)
      .addMemOperand(MMO);
    return;
  }
  if (ST.isMesaGfxShader(Fn)
      || (PreloadedPrivateBufferReg == AMDGPU::NoRegister)) {
    assert(!ST.isAmdHsaOrMesa(Fn));
    const MCInstrDesc &SMovB32 = TII->get(AMDGPU::S_MOV_B32);

    unsigned Rsrc2 = TRI->getSubReg(ScratchRsrcReg, AMDGPU::sub2);
    unsigned Rsrc3 = TRI->getSubReg(ScratchRsrcReg, AMDGPU::sub3);

    // Use relocations to get the pointer, and setup the other bits manually.
    uint64_t Rsrc23 = TII->getScratchRsrcWords23();

    if (MFI->hasImplicitBufferPtr()) {
      unsigned Rsrc01 = TRI->getSubReg(ScratchRsrcReg, AMDGPU::sub0_sub1);

      if (AMDGPU::isCompute(MF.getFunction().getCallingConv())) {
        const MCInstrDesc &Mov64 = TII->get(AMDGPU::S_MOV_B64);

        BuildMI(MBB, I, DL, Mov64, Rsrc01)
          .addReg(MFI->getImplicitBufferPtrUserSGPR())
          .addReg(ScratchRsrcReg, RegState::ImplicitDefine);
      } else {
        const MCInstrDesc &LoadDwordX2 = TII->get(AMDGPU::S_LOAD_DWORDX2_IMM);

        PointerType *PtrTy =
          PointerType::get(Type::getInt64Ty(MF.getFunction().getContext()),
                           AMDGPUAS::CONSTANT_ADDRESS);
        MachinePointerInfo PtrInfo(UndefValue::get(PtrTy));
        auto MMO = MF.getMachineMemOperand(PtrInfo,
                                           MachineMemOperand::MOLoad |
                                           MachineMemOperand::MOInvariant |
                                           MachineMemOperand::MODereferenceable,
                                           0, 0);
        BuildMI(MBB, I, DL, LoadDwordX2, Rsrc01)
          .addReg(MFI->getImplicitBufferPtrUserSGPR())
          .addImm(0) // offset
          .addImm(0) // glc
          .addMemOperand(MMO)
          .addReg(ScratchRsrcReg, RegState::ImplicitDefine);
      }
    } else {
      unsigned Rsrc0 = TRI->getSubReg(ScratchRsrcReg, AMDGPU::sub0);
      unsigned Rsrc1 = TRI->getSubReg(ScratchRsrcReg, AMDGPU::sub1);

      BuildMI(MBB, I, DL, SMovB32, Rsrc0)
        .addExternalSymbol("SCRATCH_RSRC_DWORD0")
        .addReg(ScratchRsrcReg, RegState::ImplicitDefine);

      BuildMI(MBB, I, DL, SMovB32, Rsrc1)
        .addExternalSymbol("SCRATCH_RSRC_DWORD1")
        .addReg(ScratchRsrcReg, RegState::ImplicitDefine);

    }

    BuildMI(MBB, I, DL, SMovB32, Rsrc2)
      .addImm(Rsrc23 & 0xffffffff)
      .addReg(ScratchRsrcReg, RegState::ImplicitDefine);

    BuildMI(MBB, I, DL, SMovB32, Rsrc3)
      .addImm(Rsrc23 >> 32)
      .addReg(ScratchRsrcReg, RegState::ImplicitDefine);
  }
}

// Find a scratch register that we can use at the start of the prologue to
// re-align the stack pointer.  We avoid using callee-save registers since they
// may appear to be free when this is called from canUseAsPrologue (during
// shrink wrapping), but then no longer be free when this is called from
// emitPrologue.
//
// FIXME: This is a bit conservative, since in the above case we could use one
// of the callee-save registers as a scratch temp to re-align the stack pointer,
// but we would then have to make sure that we were in fact saving at least one
// callee-save register in the prologue, which is additional complexity that
// doesn't seem worth the benefit.
static unsigned findScratchNonCalleeSaveRegister(MachineBasicBlock &MBB) {
  MachineFunction *MF = MBB.getParent();

  const GCNSubtarget &Subtarget = MF->getSubtarget<GCNSubtarget>();
  const SIRegisterInfo &TRI = *Subtarget.getRegisterInfo();
  LivePhysRegs LiveRegs(TRI);
  LiveRegs.addLiveIns(MBB);

  // Mark callee saved registers as used so we will not choose them.
  const MCPhysReg *CSRegs = TRI.getCalleeSavedRegs(MF);
  for (unsigned i = 0; CSRegs[i]; ++i)
    LiveRegs.addReg(CSRegs[i]);

  MachineRegisterInfo &MRI = MF->getRegInfo();

  for (unsigned Reg : AMDGPU::SReg_32_XM0RegClass) {
    if (LiveRegs.available(MRI, Reg))
      return Reg;
  }

  return AMDGPU::NoRegister;
}

void SIFrameLowering::emitPrologue(MachineFunction &MF,
                                   MachineBasicBlock &MBB) const {
  SIMachineFunctionInfo *FuncInfo = MF.getInfo<SIMachineFunctionInfo>();
  if (FuncInfo->isEntryFunction()) {
    emitEntryFunctionPrologue(MF, MBB);
    return;
  }

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo &TRI = TII->getRegisterInfo();

  unsigned StackPtrReg = FuncInfo->getStackPtrOffsetReg();
  unsigned FramePtrReg = FuncInfo->getFrameOffsetReg();

  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL;

  // XXX - Is this the right predicate?

  bool NeedFP = hasFP(MF);
  uint32_t NumBytes = MFI.getStackSize();
  uint32_t RoundedSize = NumBytes;
  const bool NeedsRealignment = TRI.needsStackRealignment(MF);

  if (NeedsRealignment) {
    assert(NeedFP);
    const unsigned Alignment = MFI.getMaxAlignment();

    RoundedSize += Alignment;

    unsigned ScratchSPReg = findScratchNonCalleeSaveRegister(MBB);
    assert(ScratchSPReg != AMDGPU::NoRegister);

    // s_add_u32 tmp_reg, s32, NumBytes
    // s_and_b32 s32, tmp_reg, 0b111...0000
    BuildMI(MBB, MBBI, DL, TII->get(AMDGPU::S_ADD_U32), ScratchSPReg)
      .addReg(StackPtrReg)
      .addImm((Alignment - 1) * ST.getWavefrontSize())
      .setMIFlag(MachineInstr::FrameSetup);
    BuildMI(MBB, MBBI, DL, TII->get(AMDGPU::S_AND_B32), FramePtrReg)
      .addReg(ScratchSPReg, RegState::Kill)
      .addImm(-Alignment * ST.getWavefrontSize())
      .setMIFlag(MachineInstr::FrameSetup);
    FuncInfo->setIsStackRealigned(true);
  } else if (NeedFP) {
    // If we need a base pointer, set it up here. It's whatever the value of
    // the stack pointer is at this point. Any variable size objects will be
    // allocated after this, so we can still use the base pointer to reference
    // locals.
    BuildMI(MBB, MBBI, DL, TII->get(AMDGPU::COPY), FramePtrReg)
      .addReg(StackPtrReg)
      .setMIFlag(MachineInstr::FrameSetup);
  }

  if (RoundedSize != 0 && hasSP(MF)) {
    BuildMI(MBB, MBBI, DL, TII->get(AMDGPU::S_ADD_U32), StackPtrReg)
      .addReg(StackPtrReg)
      .addImm(RoundedSize * ST.getWavefrontSize())
      .setMIFlag(MachineInstr::FrameSetup);
  }

  for (const SIMachineFunctionInfo::SGPRSpillVGPRCSR &Reg
         : FuncInfo->getSGPRSpillVGPRs()) {
    if (!Reg.FI.hasValue())
      continue;
    TII->storeRegToStackSlot(MBB, MBBI, Reg.VGPR, true,
                             Reg.FI.getValue(), &AMDGPU::VGPR_32RegClass,
                             &TII->getRegisterInfo());
  }
}

void SIFrameLowering::emitEpilogue(MachineFunction &MF,
                                   MachineBasicBlock &MBB) const {
  const SIMachineFunctionInfo *FuncInfo = MF.getInfo<SIMachineFunctionInfo>();
  if (FuncInfo->isEntryFunction())
    return;

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();

  for (const SIMachineFunctionInfo::SGPRSpillVGPRCSR &Reg
         : FuncInfo->getSGPRSpillVGPRs()) {
    if (!Reg.FI.hasValue())
      continue;
    TII->loadRegFromStackSlot(MBB, MBBI, Reg.VGPR,
                              Reg.FI.getValue(), &AMDGPU::VGPR_32RegClass,
                              &TII->getRegisterInfo());
  }

  unsigned StackPtrReg = FuncInfo->getStackPtrOffsetReg();
  if (StackPtrReg == AMDGPU::NoRegister)
    return;

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  uint32_t NumBytes = MFI.getStackSize();

  DebugLoc DL;

  // FIXME: Clarify distinction between no set SP and SP. For callee functions,
  // it's really whether we need SP to be accurate or not.

  if (NumBytes != 0 && hasSP(MF)) {
    uint32_t RoundedSize = FuncInfo->isStackRealigned() ?
      NumBytes + MFI.getMaxAlignment() : NumBytes;

    BuildMI(MBB, MBBI, DL, TII->get(AMDGPU::S_SUB_U32), StackPtrReg)
      .addReg(StackPtrReg)
      .addImm(RoundedSize * ST.getWavefrontSize());
  }
}

static bool allStackObjectsAreDead(const MachineFrameInfo &MFI) {
  for (int I = MFI.getObjectIndexBegin(), E = MFI.getObjectIndexEnd();
       I != E; ++I) {
    if (!MFI.isDeadObjectIndex(I))
      return false;
  }

  return true;
}

int SIFrameLowering::getFrameIndexReference(const MachineFunction &MF, int FI,
                                            unsigned &FrameReg) const {
  const SIRegisterInfo *RI = MF.getSubtarget<GCNSubtarget>().getRegisterInfo();

  FrameReg = RI->getFrameRegister(MF);
  return MF.getFrameInfo().getObjectOffset(FI);
}

void SIFrameLowering::processFunctionBeforeFrameFinalized(
  MachineFunction &MF,
  RegScavenger *RS) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();

  if (!MFI.hasStackObjects())
    return;

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo &TRI = TII->getRegisterInfo();
  SIMachineFunctionInfo *FuncInfo = MF.getInfo<SIMachineFunctionInfo>();
  bool AllSGPRSpilledToVGPRs = false;

  if (TRI.spillSGPRToVGPR() && FuncInfo->hasSpilledSGPRs()) {
    AllSGPRSpilledToVGPRs = true;

    // Process all SGPR spills before frame offsets are finalized. Ideally SGPRs
    // are spilled to VGPRs, in which case we can eliminate the stack usage.
    //
    // XXX - This operates under the assumption that only other SGPR spills are
    // users of the frame index. I'm not 100% sure this is correct. The
    // StackColoring pass has a comment saying a future improvement would be to
    // merging of allocas with spill slots, but for now according to
    // MachineFrameInfo isSpillSlot can't alias any other object.
    for (MachineBasicBlock &MBB : MF) {
      MachineBasicBlock::iterator Next;
      for (auto I = MBB.begin(), E = MBB.end(); I != E; I = Next) {
        MachineInstr &MI = *I;
        Next = std::next(I);

        if (TII->isSGPRSpill(MI)) {
          int FI = TII->getNamedOperand(MI, AMDGPU::OpName::addr)->getIndex();
          assert(MFI.getStackID(FI) == SIStackID::SGPR_SPILL);
          if (FuncInfo->allocateSGPRSpillToVGPR(MF, FI)) {
            bool Spilled = TRI.eliminateSGPRToVGPRSpillFrameIndex(MI, FI, RS);
            (void)Spilled;
            assert(Spilled && "failed to spill SGPR to VGPR when allocated");
          } else
            AllSGPRSpilledToVGPRs = false;
        }
      }
    }

    FuncInfo->removeSGPRToVGPRFrameIndices(MFI);
  }

  // FIXME: The other checks should be redundant with allStackObjectsAreDead,
  // but currently hasNonSpillStackObjects is set only from source
  // allocas. Stack temps produced from legalization are not counted currently.
  if (FuncInfo->hasNonSpillStackObjects() || FuncInfo->hasSpilledVGPRs() ||
      !AllSGPRSpilledToVGPRs || !allStackObjectsAreDead(MFI)) {
    assert(RS && "RegScavenger required if spilling");

    // We force this to be at offset 0 so no user object ever has 0 as an
    // address, so we may use 0 as an invalid pointer value. This is because
    // LLVM assumes 0 is an invalid pointer in address space 0. Because alloca
    // is required to be address space 0, we are forced to accept this for
    // now. Ideally we could have the stack in another address space with 0 as a
    // valid pointer, and -1 as the null value.
    //
    // This will also waste additional space when user stack objects require > 4
    // byte alignment.
    //
    // The main cost here is losing the offset for addressing modes. However
    // this also ensures we shouldn't need a register for the offset when
    // emergency scavenging.
    int ScavengeFI = MFI.CreateFixedObject(
      TRI.getSpillSize(AMDGPU::SGPR_32RegClass), 0, false);
    RS->addScavengingFrameIndex(ScavengeFI);
  }
}

void SIFrameLowering::determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                                           RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();

  // The SP is specifically managed and we don't want extra spills of it.
  SavedRegs.reset(MFI->getStackPtrOffsetReg());
}

MachineBasicBlock::iterator SIFrameLowering::eliminateCallFramePseudoInstr(
  MachineFunction &MF,
  MachineBasicBlock &MBB,
  MachineBasicBlock::iterator I) const {
  int64_t Amount = I->getOperand(0).getImm();
  if (Amount == 0)
    return MBB.erase(I);

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  const DebugLoc &DL = I->getDebugLoc();
  unsigned Opc = I->getOpcode();
  bool IsDestroy = Opc == TII->getCallFrameDestroyOpcode();
  uint64_t CalleePopAmount = IsDestroy ? I->getOperand(1).getImm() : 0;

  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  if (!TFI->hasReservedCallFrame(MF)) {
    unsigned Align = getStackAlignment();

    Amount = alignTo(Amount, Align);
    assert(isUInt<32>(Amount) && "exceeded stack address space size");
    const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
    unsigned SPReg = MFI->getStackPtrOffsetReg();

    unsigned Op = IsDestroy ? AMDGPU::S_SUB_U32 : AMDGPU::S_ADD_U32;
    BuildMI(MBB, I, DL, TII->get(Op), SPReg)
      .addReg(SPReg)
      .addImm(Amount * ST.getWavefrontSize());
  } else if (CalleePopAmount != 0) {
    llvm_unreachable("is this used?");
  }

  return MBB.erase(I);
}

void SIFrameLowering::emitDebuggerPrologue(MachineFunction &MF,
                                           MachineBasicBlock &MBB) const {
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo *TRI = &TII->getRegisterInfo();
  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();

  MachineBasicBlock::iterator I = MBB.begin();
  DebugLoc DL;

  // For each dimension:
  for (unsigned i = 0; i < 3; ++i) {
    // Get work group ID SGPR, and make it live-in again.
    unsigned WorkGroupIDSGPR = MFI->getWorkGroupIDSGPR(i);
    MF.getRegInfo().addLiveIn(WorkGroupIDSGPR);
    MBB.addLiveIn(WorkGroupIDSGPR);

    // Since SGPRs are spilled into VGPRs, copy work group ID SGPR to VGPR in
    // order to spill it to scratch.
    unsigned WorkGroupIDVGPR =
      MF.getRegInfo().createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    BuildMI(MBB, I, DL, TII->get(AMDGPU::V_MOV_B32_e32), WorkGroupIDVGPR)
      .addReg(WorkGroupIDSGPR);

    // Spill work group ID.
    int WorkGroupIDObjectIdx = MFI->getDebuggerWorkGroupIDStackObjectIndex(i);
    TII->storeRegToStackSlot(MBB, I, WorkGroupIDVGPR, false,
      WorkGroupIDObjectIdx, &AMDGPU::VGPR_32RegClass, TRI);

    // Get work item ID VGPR, and make it live-in again.
    unsigned WorkItemIDVGPR = MFI->getWorkItemIDVGPR(i);
    MF.getRegInfo().addLiveIn(WorkItemIDVGPR);
    MBB.addLiveIn(WorkItemIDVGPR);

    // Spill work item ID.
    int WorkItemIDObjectIdx = MFI->getDebuggerWorkItemIDStackObjectIndex(i);
    TII->storeRegToStackSlot(MBB, I, WorkItemIDVGPR, false,
      WorkItemIDObjectIdx, &AMDGPU::VGPR_32RegClass, TRI);
  }
}

bool SIFrameLowering::hasFP(const MachineFunction &MF) const {
  // All stack operations are relative to the frame offset SGPR.
  // TODO: Still want to eliminate sometimes.
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  // XXX - Is this only called after frame is finalized? Should be able to check
  // frame size.
  return MFI.hasStackObjects() && !allStackObjectsAreDead(MFI);
}

bool SIFrameLowering::hasSP(const MachineFunction &MF) const {
  const SIRegisterInfo *TRI = MF.getSubtarget<GCNSubtarget>().getRegisterInfo();
  // All stack operations are relative to the frame offset SGPR.
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MFI.hasCalls() || MFI.hasVarSizedObjects() || TRI->needsStackRealignment(MF);
}
