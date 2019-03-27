//===-- SIRegisterInfo.cpp - SI Register Information ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// SI implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "SIRegisterInfo.h"
#include "AMDGPURegisterBankInfo.h"
#include "AMDGPUSubtarget.h"
#include "SIInstrInfo.h"
#include "SIMachineFunctionInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"

using namespace llvm;

static bool hasPressureSet(const int *PSets, unsigned PSetID) {
  for (unsigned i = 0; PSets[i] != -1; ++i) {
    if (PSets[i] == (int)PSetID)
      return true;
  }
  return false;
}

void SIRegisterInfo::classifyPressureSet(unsigned PSetID, unsigned Reg,
                                         BitVector &PressureSets) const {
  for (MCRegUnitIterator U(Reg, this); U.isValid(); ++U) {
    const int *PSets = getRegUnitPressureSets(*U);
    if (hasPressureSet(PSets, PSetID)) {
      PressureSets.set(PSetID);
      break;
    }
  }
}

static cl::opt<bool> EnableSpillSGPRToSMEM(
  "amdgpu-spill-sgpr-to-smem",
  cl::desc("Use scalar stores to spill SGPRs if supported by subtarget"),
  cl::init(false));

static cl::opt<bool> EnableSpillSGPRToVGPR(
  "amdgpu-spill-sgpr-to-vgpr",
  cl::desc("Enable spilling VGPRs to SGPRs"),
  cl::ReallyHidden,
  cl::init(true));

SIRegisterInfo::SIRegisterInfo(const GCNSubtarget &ST) :
  AMDGPURegisterInfo(),
  SGPRPressureSets(getNumRegPressureSets()),
  VGPRPressureSets(getNumRegPressureSets()),
  SpillSGPRToVGPR(false),
  SpillSGPRToSMEM(false) {
  if (EnableSpillSGPRToSMEM && ST.hasScalarStores())
    SpillSGPRToSMEM = true;
  else if (EnableSpillSGPRToVGPR)
    SpillSGPRToVGPR = true;

  unsigned NumRegPressureSets = getNumRegPressureSets();

  SGPRSetID = NumRegPressureSets;
  VGPRSetID = NumRegPressureSets;

  for (unsigned i = 0; i < NumRegPressureSets; ++i) {
    classifyPressureSet(i, AMDGPU::SGPR0, SGPRPressureSets);
    classifyPressureSet(i, AMDGPU::VGPR0, VGPRPressureSets);
  }

  // Determine the number of reg units for each pressure set.
  std::vector<unsigned> PressureSetRegUnits(NumRegPressureSets, 0);
  for (unsigned i = 0, e = getNumRegUnits(); i != e; ++i) {
    const int *PSets = getRegUnitPressureSets(i);
    for (unsigned j = 0; PSets[j] != -1; ++j) {
      ++PressureSetRegUnits[PSets[j]];
    }
  }

  unsigned VGPRMax = 0, SGPRMax = 0;
  for (unsigned i = 0; i < NumRegPressureSets; ++i) {
    if (isVGPRPressureSet(i) && PressureSetRegUnits[i] > VGPRMax) {
      VGPRSetID = i;
      VGPRMax = PressureSetRegUnits[i];
      continue;
    }
    if (isSGPRPressureSet(i) && PressureSetRegUnits[i] > SGPRMax) {
      SGPRSetID = i;
      SGPRMax = PressureSetRegUnits[i];
    }
  }

  assert(SGPRSetID < NumRegPressureSets &&
         VGPRSetID < NumRegPressureSets);
}

unsigned SIRegisterInfo::reservedPrivateSegmentBufferReg(
  const MachineFunction &MF) const {

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  unsigned BaseIdx = alignDown(ST.getMaxNumSGPRs(MF), 4) - 4;
  unsigned BaseReg(AMDGPU::SGPR_32RegClass.getRegister(BaseIdx));
  return getMatchingSuperReg(BaseReg, AMDGPU::sub0, &AMDGPU::SReg_128RegClass);
}

static unsigned findPrivateSegmentWaveByteOffsetRegIndex(unsigned RegCount) {
  unsigned Reg;

  // Try to place it in a hole after PrivateSegmentBufferReg.
  if (RegCount & 3) {
    // We cannot put the segment buffer in (Idx - 4) ... (Idx - 1) due to
    // alignment constraints, so we have a hole where can put the wave offset.
    Reg = RegCount - 1;
  } else {
    // We can put the segment buffer in (Idx - 4) ... (Idx - 1) and put the
    // wave offset before it.
    Reg = RegCount - 5;
  }

  return Reg;
}

unsigned SIRegisterInfo::reservedPrivateSegmentWaveByteOffsetReg(
  const MachineFunction &MF) const {
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  unsigned Reg = findPrivateSegmentWaveByteOffsetRegIndex(ST.getMaxNumSGPRs(MF));
  return AMDGPU::SGPR_32RegClass.getRegister(Reg);
}

unsigned SIRegisterInfo::reservedStackPtrOffsetReg(
  const MachineFunction &MF) const {
  return AMDGPU::SGPR32;
}

BitVector SIRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());

  // EXEC_LO and EXEC_HI could be allocated and used as regular register, but
  // this seems likely to result in bugs, so I'm marking them as reserved.
  reserveRegisterTuples(Reserved, AMDGPU::EXEC);
  reserveRegisterTuples(Reserved, AMDGPU::FLAT_SCR);

  // M0 has to be reserved so that llvm accepts it as a live-in into a block.
  reserveRegisterTuples(Reserved, AMDGPU::M0);

  // Reserve the memory aperture registers.
  reserveRegisterTuples(Reserved, AMDGPU::SRC_SHARED_BASE);
  reserveRegisterTuples(Reserved, AMDGPU::SRC_SHARED_LIMIT);
  reserveRegisterTuples(Reserved, AMDGPU::SRC_PRIVATE_BASE);
  reserveRegisterTuples(Reserved, AMDGPU::SRC_PRIVATE_LIMIT);

  // Reserve xnack_mask registers - support is not implemented in Codegen.
  reserveRegisterTuples(Reserved, AMDGPU::XNACK_MASK);

  // Reserve Trap Handler registers - support is not implemented in Codegen.
  reserveRegisterTuples(Reserved, AMDGPU::TBA);
  reserveRegisterTuples(Reserved, AMDGPU::TMA);
  reserveRegisterTuples(Reserved, AMDGPU::TTMP0_TTMP1);
  reserveRegisterTuples(Reserved, AMDGPU::TTMP2_TTMP3);
  reserveRegisterTuples(Reserved, AMDGPU::TTMP4_TTMP5);
  reserveRegisterTuples(Reserved, AMDGPU::TTMP6_TTMP7);
  reserveRegisterTuples(Reserved, AMDGPU::TTMP8_TTMP9);
  reserveRegisterTuples(Reserved, AMDGPU::TTMP10_TTMP11);
  reserveRegisterTuples(Reserved, AMDGPU::TTMP12_TTMP13);
  reserveRegisterTuples(Reserved, AMDGPU::TTMP14_TTMP15);

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();

  unsigned MaxNumSGPRs = ST.getMaxNumSGPRs(MF);
  unsigned TotalNumSGPRs = AMDGPU::SGPR_32RegClass.getNumRegs();
  for (unsigned i = MaxNumSGPRs; i < TotalNumSGPRs; ++i) {
    unsigned Reg = AMDGPU::SGPR_32RegClass.getRegister(i);
    reserveRegisterTuples(Reserved, Reg);
  }

  unsigned MaxNumVGPRs = ST.getMaxNumVGPRs(MF);
  unsigned TotalNumVGPRs = AMDGPU::VGPR_32RegClass.getNumRegs();
  for (unsigned i = MaxNumVGPRs; i < TotalNumVGPRs; ++i) {
    unsigned Reg = AMDGPU::VGPR_32RegClass.getRegister(i);
    reserveRegisterTuples(Reserved, Reg);
  }

  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();

  unsigned ScratchWaveOffsetReg = MFI->getScratchWaveOffsetReg();
  if (ScratchWaveOffsetReg != AMDGPU::NoRegister) {
    // Reserve 1 SGPR for scratch wave offset in case we need to spill.
    reserveRegisterTuples(Reserved, ScratchWaveOffsetReg);
  }

  unsigned ScratchRSrcReg = MFI->getScratchRSrcReg();
  if (ScratchRSrcReg != AMDGPU::NoRegister) {
    // Reserve 4 SGPRs for the scratch buffer resource descriptor in case we need
    // to spill.
    // TODO: May need to reserve a VGPR if doing LDS spilling.
    reserveRegisterTuples(Reserved, ScratchRSrcReg);
    assert(!isSubRegister(ScratchRSrcReg, ScratchWaveOffsetReg));
  }

  // We have to assume the SP is needed in case there are calls in the function,
  // which is detected after the function is lowered. If we aren't really going
  // to need SP, don't bother reserving it.
  unsigned StackPtrReg = MFI->getStackPtrOffsetReg();

  if (StackPtrReg != AMDGPU::NoRegister) {
    reserveRegisterTuples(Reserved, StackPtrReg);
    assert(!isSubRegister(ScratchRSrcReg, StackPtrReg));
  }

  unsigned FrameReg = MFI->getFrameOffsetReg();
  if (FrameReg != AMDGPU::NoRegister) {
    reserveRegisterTuples(Reserved, FrameReg);
    assert(!isSubRegister(ScratchRSrcReg, FrameReg));
  }

  return Reserved;
}

bool SIRegisterInfo::requiresRegisterScavenging(const MachineFunction &Fn) const {
  const SIMachineFunctionInfo *Info = Fn.getInfo<SIMachineFunctionInfo>();
  if (Info->isEntryFunction()) {
    const MachineFrameInfo &MFI = Fn.getFrameInfo();
    return MFI.hasStackObjects() || MFI.hasCalls();
  }

  // May need scavenger for dealing with callee saved registers.
  return true;
}

bool SIRegisterInfo::requiresFrameIndexScavenging(
  const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  if (MFI.hasStackObjects())
    return true;

  // May need to deal with callee saved registers.
  const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();
  return !Info->isEntryFunction();
}

bool SIRegisterInfo::requiresFrameIndexReplacementScavenging(
  const MachineFunction &MF) const {
  // m0 is needed for the scalar store offset. m0 is unallocatable, so we can't
  // create a virtual register for it during frame index elimination, so the
  // scavenger is directly needed.
  return MF.getFrameInfo().hasStackObjects() &&
         MF.getSubtarget<GCNSubtarget>().hasScalarStores() &&
         MF.getInfo<SIMachineFunctionInfo>()->hasSpilledSGPRs();
}

bool SIRegisterInfo::requiresVirtualBaseRegisters(
  const MachineFunction &) const {
  // There are no special dedicated stack or frame pointers.
  return true;
}

bool SIRegisterInfo::trackLivenessAfterRegAlloc(const MachineFunction &MF) const {
  // This helps catch bugs as verifier errors.
  return true;
}

int64_t SIRegisterInfo::getMUBUFInstrOffset(const MachineInstr *MI) const {
  assert(SIInstrInfo::isMUBUF(*MI));

  int OffIdx = AMDGPU::getNamedOperandIdx(MI->getOpcode(),
                                          AMDGPU::OpName::offset);
  return MI->getOperand(OffIdx).getImm();
}

int64_t SIRegisterInfo::getFrameIndexInstrOffset(const MachineInstr *MI,
                                                 int Idx) const {
  if (!SIInstrInfo::isMUBUF(*MI))
    return 0;

  assert(Idx == AMDGPU::getNamedOperandIdx(MI->getOpcode(),
                                           AMDGPU::OpName::vaddr) &&
         "Should never see frame index on non-address operand");

  return getMUBUFInstrOffset(MI);
}

bool SIRegisterInfo::needsFrameBaseReg(MachineInstr *MI, int64_t Offset) const {
  if (!MI->mayLoadOrStore())
    return false;

  int64_t FullOffset = Offset + getMUBUFInstrOffset(MI);

  return !isUInt<12>(FullOffset);
}

void SIRegisterInfo::materializeFrameBaseRegister(MachineBasicBlock *MBB,
                                                  unsigned BaseReg,
                                                  int FrameIdx,
                                                  int64_t Offset) const {
  MachineBasicBlock::iterator Ins = MBB->begin();
  DebugLoc DL; // Defaults to "unknown"

  if (Ins != MBB->end())
    DL = Ins->getDebugLoc();

  MachineFunction *MF = MBB->getParent();
  const GCNSubtarget &Subtarget = MF->getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = Subtarget.getInstrInfo();

  if (Offset == 0) {
    BuildMI(*MBB, Ins, DL, TII->get(AMDGPU::V_MOV_B32_e32), BaseReg)
      .addFrameIndex(FrameIdx);
    return;
  }

  MachineRegisterInfo &MRI = MF->getRegInfo();
  unsigned OffsetReg = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);

  unsigned FIReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);

  BuildMI(*MBB, Ins, DL, TII->get(AMDGPU::S_MOV_B32), OffsetReg)
    .addImm(Offset);
  BuildMI(*MBB, Ins, DL, TII->get(AMDGPU::V_MOV_B32_e32), FIReg)
    .addFrameIndex(FrameIdx);

  TII->getAddNoCarry(*MBB, Ins, DL, BaseReg)
    .addReg(OffsetReg, RegState::Kill)
    .addReg(FIReg);
}

void SIRegisterInfo::resolveFrameIndex(MachineInstr &MI, unsigned BaseReg,
                                       int64_t Offset) const {

  MachineBasicBlock *MBB = MI.getParent();
  MachineFunction *MF = MBB->getParent();
  const GCNSubtarget &Subtarget = MF->getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = Subtarget.getInstrInfo();

#ifndef NDEBUG
  // FIXME: Is it possible to be storing a frame index to itself?
  bool SeenFI = false;
  for (const MachineOperand &MO: MI.operands()) {
    if (MO.isFI()) {
      if (SeenFI)
        llvm_unreachable("should not see multiple frame indices");

      SeenFI = true;
    }
  }
#endif

  MachineOperand *FIOp = TII->getNamedOperand(MI, AMDGPU::OpName::vaddr);
  assert(FIOp && FIOp->isFI() && "frame index must be address operand");
  assert(TII->isMUBUF(MI));
  assert(TII->getNamedOperand(MI, AMDGPU::OpName::soffset)->getReg() ==
         MF->getInfo<SIMachineFunctionInfo>()->getFrameOffsetReg() &&
         "should only be seeing frame offset relative FrameIndex");


  MachineOperand *OffsetOp = TII->getNamedOperand(MI, AMDGPU::OpName::offset);
  int64_t NewOffset = OffsetOp->getImm() + Offset;
  assert(isUInt<12>(NewOffset) && "offset should be legal");

  FIOp->ChangeToRegister(BaseReg, false);
  OffsetOp->setImm(NewOffset);
}

bool SIRegisterInfo::isFrameOffsetLegal(const MachineInstr *MI,
                                        unsigned BaseReg,
                                        int64_t Offset) const {
  if (!SIInstrInfo::isMUBUF(*MI))
    return false;

  int64_t NewOffset = Offset + getMUBUFInstrOffset(MI);

  return isUInt<12>(NewOffset);
}

const TargetRegisterClass *SIRegisterInfo::getPointerRegClass(
  const MachineFunction &MF, unsigned Kind) const {
  // This is inaccurate. It depends on the instruction and address space. The
  // only place where we should hit this is for dealing with frame indexes /
  // private accesses, so this is correct in that case.
  return &AMDGPU::VGPR_32RegClass;
}

static unsigned getNumSubRegsForSpillOp(unsigned Op) {

  switch (Op) {
  case AMDGPU::SI_SPILL_S512_SAVE:
  case AMDGPU::SI_SPILL_S512_RESTORE:
  case AMDGPU::SI_SPILL_V512_SAVE:
  case AMDGPU::SI_SPILL_V512_RESTORE:
    return 16;
  case AMDGPU::SI_SPILL_S256_SAVE:
  case AMDGPU::SI_SPILL_S256_RESTORE:
  case AMDGPU::SI_SPILL_V256_SAVE:
  case AMDGPU::SI_SPILL_V256_RESTORE:
    return 8;
  case AMDGPU::SI_SPILL_S128_SAVE:
  case AMDGPU::SI_SPILL_S128_RESTORE:
  case AMDGPU::SI_SPILL_V128_SAVE:
  case AMDGPU::SI_SPILL_V128_RESTORE:
    return 4;
  case AMDGPU::SI_SPILL_V96_SAVE:
  case AMDGPU::SI_SPILL_V96_RESTORE:
    return 3;
  case AMDGPU::SI_SPILL_S64_SAVE:
  case AMDGPU::SI_SPILL_S64_RESTORE:
  case AMDGPU::SI_SPILL_V64_SAVE:
  case AMDGPU::SI_SPILL_V64_RESTORE:
    return 2;
  case AMDGPU::SI_SPILL_S32_SAVE:
  case AMDGPU::SI_SPILL_S32_RESTORE:
  case AMDGPU::SI_SPILL_V32_SAVE:
  case AMDGPU::SI_SPILL_V32_RESTORE:
    return 1;
  default: llvm_unreachable("Invalid spill opcode");
  }
}

static int getOffsetMUBUFStore(unsigned Opc) {
  switch (Opc) {
  case AMDGPU::BUFFER_STORE_DWORD_OFFEN:
    return AMDGPU::BUFFER_STORE_DWORD_OFFSET;
  case AMDGPU::BUFFER_STORE_BYTE_OFFEN:
    return AMDGPU::BUFFER_STORE_BYTE_OFFSET;
  case AMDGPU::BUFFER_STORE_SHORT_OFFEN:
    return AMDGPU::BUFFER_STORE_SHORT_OFFSET;
  case AMDGPU::BUFFER_STORE_DWORDX2_OFFEN:
    return AMDGPU::BUFFER_STORE_DWORDX2_OFFSET;
  case AMDGPU::BUFFER_STORE_DWORDX4_OFFEN:
    return AMDGPU::BUFFER_STORE_DWORDX4_OFFSET;
  case AMDGPU::BUFFER_STORE_SHORT_D16_HI_OFFEN:
    return AMDGPU::BUFFER_STORE_SHORT_D16_HI_OFFSET;
  case AMDGPU::BUFFER_STORE_BYTE_D16_HI_OFFEN:
    return AMDGPU::BUFFER_STORE_BYTE_D16_HI_OFFSET;
  default:
    return -1;
  }
}

static int getOffsetMUBUFLoad(unsigned Opc) {
  switch (Opc) {
  case AMDGPU::BUFFER_LOAD_DWORD_OFFEN:
    return AMDGPU::BUFFER_LOAD_DWORD_OFFSET;
  case AMDGPU::BUFFER_LOAD_UBYTE_OFFEN:
    return AMDGPU::BUFFER_LOAD_UBYTE_OFFSET;
  case AMDGPU::BUFFER_LOAD_SBYTE_OFFEN:
    return AMDGPU::BUFFER_LOAD_SBYTE_OFFSET;
  case AMDGPU::BUFFER_LOAD_USHORT_OFFEN:
    return AMDGPU::BUFFER_LOAD_USHORT_OFFSET;
  case AMDGPU::BUFFER_LOAD_SSHORT_OFFEN:
    return AMDGPU::BUFFER_LOAD_SSHORT_OFFSET;
  case AMDGPU::BUFFER_LOAD_DWORDX2_OFFEN:
    return AMDGPU::BUFFER_LOAD_DWORDX2_OFFSET;
  case AMDGPU::BUFFER_LOAD_DWORDX4_OFFEN:
    return AMDGPU::BUFFER_LOAD_DWORDX4_OFFSET;
  case AMDGPU::BUFFER_LOAD_UBYTE_D16_OFFEN:
    return AMDGPU::BUFFER_LOAD_UBYTE_D16_OFFSET;
  case AMDGPU::BUFFER_LOAD_UBYTE_D16_HI_OFFEN:
    return AMDGPU::BUFFER_LOAD_UBYTE_D16_HI_OFFSET;
  case AMDGPU::BUFFER_LOAD_SBYTE_D16_OFFEN:
    return AMDGPU::BUFFER_LOAD_SBYTE_D16_OFFSET;
  case AMDGPU::BUFFER_LOAD_SBYTE_D16_HI_OFFEN:
    return AMDGPU::BUFFER_LOAD_SBYTE_D16_HI_OFFSET;
  case AMDGPU::BUFFER_LOAD_SHORT_D16_OFFEN:
    return AMDGPU::BUFFER_LOAD_SHORT_D16_OFFSET;
  case AMDGPU::BUFFER_LOAD_SHORT_D16_HI_OFFEN:
    return AMDGPU::BUFFER_LOAD_SHORT_D16_HI_OFFSET;
  default:
    return -1;
  }
}

// This differs from buildSpillLoadStore by only scavenging a VGPR. It does not
// need to handle the case where an SGPR may need to be spilled while spilling.
static bool buildMUBUFOffsetLoadStore(const SIInstrInfo *TII,
                                      MachineFrameInfo &MFI,
                                      MachineBasicBlock::iterator MI,
                                      int Index,
                                      int64_t Offset) {
  MachineBasicBlock *MBB = MI->getParent();
  const DebugLoc &DL = MI->getDebugLoc();
  bool IsStore = MI->mayStore();

  unsigned Opc = MI->getOpcode();
  int LoadStoreOp = IsStore ?
    getOffsetMUBUFStore(Opc) : getOffsetMUBUFLoad(Opc);
  if (LoadStoreOp == -1)
    return false;

  const MachineOperand *Reg = TII->getNamedOperand(*MI, AMDGPU::OpName::vdata);
  MachineInstrBuilder NewMI =
      BuildMI(*MBB, MI, DL, TII->get(LoadStoreOp))
          .add(*Reg)
          .add(*TII->getNamedOperand(*MI, AMDGPU::OpName::srsrc))
          .add(*TII->getNamedOperand(*MI, AMDGPU::OpName::soffset))
          .addImm(Offset)
          .addImm(0) // glc
          .addImm(0) // slc
          .addImm(0) // tfe
          .cloneMemRefs(*MI);

  const MachineOperand *VDataIn = TII->getNamedOperand(*MI,
                                                       AMDGPU::OpName::vdata_in);
  if (VDataIn)
    NewMI.add(*VDataIn);
  return true;
}

void SIRegisterInfo::buildSpillLoadStore(MachineBasicBlock::iterator MI,
                                         unsigned LoadStoreOp,
                                         int Index,
                                         unsigned ValueReg,
                                         bool IsKill,
                                         unsigned ScratchRsrcReg,
                                         unsigned ScratchOffsetReg,
                                         int64_t InstOffset,
                                         MachineMemOperand *MMO,
                                         RegScavenger *RS) const {
  MachineBasicBlock *MBB = MI->getParent();
  MachineFunction *MF = MI->getParent()->getParent();
  const GCNSubtarget &ST =  MF->getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  const MachineFrameInfo &MFI = MF->getFrameInfo();

  const MCInstrDesc &Desc = TII->get(LoadStoreOp);
  const DebugLoc &DL = MI->getDebugLoc();
  bool IsStore = Desc.mayStore();

  bool Scavenged = false;
  unsigned SOffset = ScratchOffsetReg;

  const unsigned EltSize = 4;
  const TargetRegisterClass *RC = getRegClassForReg(MF->getRegInfo(), ValueReg);
  unsigned NumSubRegs = AMDGPU::getRegBitWidth(RC->getID()) / (EltSize * CHAR_BIT);
  unsigned Size = NumSubRegs * EltSize;
  int64_t Offset = InstOffset + MFI.getObjectOffset(Index);
  int64_t ScratchOffsetRegDelta = 0;

  unsigned Align = MFI.getObjectAlignment(Index);
  const MachinePointerInfo &BasePtrInfo = MMO->getPointerInfo();

  assert((Offset % EltSize) == 0 && "unexpected VGPR spill offset");

  if (!isUInt<12>(Offset + Size - EltSize)) {
    SOffset = AMDGPU::NoRegister;

    // We currently only support spilling VGPRs to EltSize boundaries, meaning
    // we can simplify the adjustment of Offset here to just scale with
    // WavefrontSize.
    Offset *= ST.getWavefrontSize();

    // We don't have access to the register scavenger if this function is called
    // during  PEI::scavengeFrameVirtualRegs().
    if (RS)
      SOffset = RS->FindUnusedReg(&AMDGPU::SGPR_32RegClass);

    if (SOffset == AMDGPU::NoRegister) {
      // There are no free SGPRs, and since we are in the process of spilling
      // VGPRs too.  Since we need a VGPR in order to spill SGPRs (this is true
      // on SI/CI and on VI it is true until we implement spilling using scalar
      // stores), we have no way to free up an SGPR.  Our solution here is to
      // add the offset directly to the ScratchOffset register, and then
      // subtract the offset after the spill to return ScratchOffset to it's
      // original value.
      SOffset = ScratchOffsetReg;
      ScratchOffsetRegDelta = Offset;
    } else {
      Scavenged = true;
    }

    BuildMI(*MBB, MI, DL, TII->get(AMDGPU::S_ADD_U32), SOffset)
      .addReg(ScratchOffsetReg)
      .addImm(Offset);

    Offset = 0;
  }

  for (unsigned i = 0, e = NumSubRegs; i != e; ++i, Offset += EltSize) {
    unsigned SubReg = NumSubRegs == 1 ?
      ValueReg : getSubReg(ValueReg, getSubRegFromChannel(i));

    unsigned SOffsetRegState = 0;
    unsigned SrcDstRegState = getDefRegState(!IsStore);
    if (i + 1 == e) {
      SOffsetRegState |= getKillRegState(Scavenged);
      // The last implicit use carries the "Kill" flag.
      SrcDstRegState |= getKillRegState(IsKill);
    }

    MachinePointerInfo PInfo = BasePtrInfo.getWithOffset(EltSize * i);
    MachineMemOperand *NewMMO
      = MF->getMachineMemOperand(PInfo, MMO->getFlags(),
                                 EltSize, MinAlign(Align, EltSize * i));

    auto MIB = BuildMI(*MBB, MI, DL, Desc)
      .addReg(SubReg, getDefRegState(!IsStore) | getKillRegState(IsKill))
      .addReg(ScratchRsrcReg)
      .addReg(SOffset, SOffsetRegState)
      .addImm(Offset)
      .addImm(0) // glc
      .addImm(0) // slc
      .addImm(0) // tfe
      .addMemOperand(NewMMO);

    if (NumSubRegs > 1)
      MIB.addReg(ValueReg, RegState::Implicit | SrcDstRegState);
  }

  if (ScratchOffsetRegDelta != 0) {
    // Subtract the offset we added to the ScratchOffset register.
    BuildMI(*MBB, MI, DL, TII->get(AMDGPU::S_SUB_U32), ScratchOffsetReg)
        .addReg(ScratchOffsetReg)
        .addImm(ScratchOffsetRegDelta);
  }
}

static std::pair<unsigned, unsigned> getSpillEltSize(unsigned SuperRegSize,
                                                     bool Store) {
  if (SuperRegSize % 16 == 0) {
    return { 16, Store ? AMDGPU::S_BUFFER_STORE_DWORDX4_SGPR :
                         AMDGPU::S_BUFFER_LOAD_DWORDX4_SGPR };
  }

  if (SuperRegSize % 8 == 0) {
    return { 8, Store ? AMDGPU::S_BUFFER_STORE_DWORDX2_SGPR :
                        AMDGPU::S_BUFFER_LOAD_DWORDX2_SGPR };
  }

  return { 4, Store ? AMDGPU::S_BUFFER_STORE_DWORD_SGPR :
                      AMDGPU::S_BUFFER_LOAD_DWORD_SGPR};
}

bool SIRegisterInfo::spillSGPR(MachineBasicBlock::iterator MI,
                               int Index,
                               RegScavenger *RS,
                               bool OnlyToVGPR) const {
  MachineBasicBlock *MBB = MI->getParent();
  MachineFunction *MF = MBB->getParent();
  SIMachineFunctionInfo *MFI = MF->getInfo<SIMachineFunctionInfo>();
  DenseSet<unsigned> SGPRSpillVGPRDefinedSet;

  ArrayRef<SIMachineFunctionInfo::SpilledReg> VGPRSpills
    = MFI->getSGPRToVGPRSpills(Index);
  bool SpillToVGPR = !VGPRSpills.empty();
  if (OnlyToVGPR && !SpillToVGPR)
    return false;

  MachineRegisterInfo &MRI = MF->getRegInfo();
  const GCNSubtarget &ST =  MF->getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();

  unsigned SuperReg = MI->getOperand(0).getReg();
  bool IsKill = MI->getOperand(0).isKill();
  const DebugLoc &DL = MI->getDebugLoc();

  MachineFrameInfo &FrameInfo = MF->getFrameInfo();

  bool SpillToSMEM = spillSGPRToSMEM();
  if (SpillToSMEM && OnlyToVGPR)
    return false;

  assert(SpillToVGPR || (SuperReg != MFI->getStackPtrOffsetReg() &&
                         SuperReg != MFI->getFrameOffsetReg() &&
                         SuperReg != MFI->getScratchWaveOffsetReg()));

  assert(SuperReg != AMDGPU::M0 && "m0 should never spill");

  unsigned OffsetReg = AMDGPU::M0;
  unsigned M0CopyReg = AMDGPU::NoRegister;

  if (SpillToSMEM) {
    if (RS->isRegUsed(AMDGPU::M0)) {
      M0CopyReg = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);
      BuildMI(*MBB, MI, DL, TII->get(AMDGPU::COPY), M0CopyReg)
        .addReg(AMDGPU::M0);
    }
  }

  unsigned ScalarStoreOp;
  unsigned EltSize = 4;
  const TargetRegisterClass *RC = getPhysRegClass(SuperReg);
  if (SpillToSMEM && isSGPRClass(RC)) {
    // XXX - if private_element_size is larger than 4 it might be useful to be
    // able to spill wider vmem spills.
    std::tie(EltSize, ScalarStoreOp) =
          getSpillEltSize(getRegSizeInBits(*RC) / 8, true);
  }

  ArrayRef<int16_t> SplitParts = getRegSplitParts(RC, EltSize);
  unsigned NumSubRegs = SplitParts.empty() ? 1 : SplitParts.size();

  // SubReg carries the "Kill" flag when SubReg == SuperReg.
  unsigned SubKillState = getKillRegState((NumSubRegs == 1) && IsKill);
  for (unsigned i = 0, e = NumSubRegs; i < e; ++i) {
    unsigned SubReg = NumSubRegs == 1 ?
      SuperReg : getSubReg(SuperReg, SplitParts[i]);

    if (SpillToSMEM) {
      int64_t FrOffset = FrameInfo.getObjectOffset(Index);

      // The allocated memory size is really the wavefront size * the frame
      // index size. The widest register class is 64 bytes, so a 4-byte scratch
      // allocation is enough to spill this in a single stack object.
      //
      // FIXME: Frame size/offsets are computed earlier than this, so the extra
      // space is still unnecessarily allocated.

      unsigned Align = FrameInfo.getObjectAlignment(Index);
      MachinePointerInfo PtrInfo
        = MachinePointerInfo::getFixedStack(*MF, Index, EltSize * i);
      MachineMemOperand *MMO
        = MF->getMachineMemOperand(PtrInfo, MachineMemOperand::MOStore,
                                   EltSize, MinAlign(Align, EltSize * i));

      // SMEM instructions only support a single offset, so increment the wave
      // offset.

      int64_t Offset = (ST.getWavefrontSize() * FrOffset) + (EltSize * i);
      if (Offset != 0) {
        BuildMI(*MBB, MI, DL, TII->get(AMDGPU::S_ADD_U32), OffsetReg)
          .addReg(MFI->getFrameOffsetReg())
          .addImm(Offset);
      } else {
        BuildMI(*MBB, MI, DL, TII->get(AMDGPU::S_MOV_B32), OffsetReg)
          .addReg(MFI->getFrameOffsetReg());
      }

      BuildMI(*MBB, MI, DL, TII->get(ScalarStoreOp))
        .addReg(SubReg, getKillRegState(IsKill)) // sdata
        .addReg(MFI->getScratchRSrcReg())        // sbase
        .addReg(OffsetReg, RegState::Kill)       // soff
        .addImm(0)                               // glc
        .addMemOperand(MMO);

      continue;
    }

    if (SpillToVGPR) {
      SIMachineFunctionInfo::SpilledReg Spill = VGPRSpills[i];

      // During SGPR spilling to VGPR, determine if the VGPR is defined. The
      // only circumstance in which we say it is undefined is when it is the
      // first spill to this VGPR in the first basic block.
      bool VGPRDefined = true;
      if (MBB == &MF->front())
        VGPRDefined = !SGPRSpillVGPRDefinedSet.insert(Spill.VGPR).second;

      // Mark the "old value of vgpr" input undef only if this is the first sgpr
      // spill to this specific vgpr in the first basic block.
      BuildMI(*MBB, MI, DL,
              TII->getMCOpcodeFromPseudo(AMDGPU::V_WRITELANE_B32),
              Spill.VGPR)
        .addReg(SubReg, getKillRegState(IsKill))
        .addImm(Spill.Lane)
        .addReg(Spill.VGPR, VGPRDefined ? 0 : RegState::Undef);

      // FIXME: Since this spills to another register instead of an actual
      // frame index, we should delete the frame index when all references to
      // it are fixed.
    } else {
      // XXX - Can to VGPR spill fail for some subregisters but not others?
      if (OnlyToVGPR)
        return false;

      // Spill SGPR to a frame index.
      // TODO: Should VI try to spill to VGPR and then spill to SMEM?
      unsigned TmpReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
      // TODO: Should VI try to spill to VGPR and then spill to SMEM?

      MachineInstrBuilder Mov
        = BuildMI(*MBB, MI, DL, TII->get(AMDGPU::V_MOV_B32_e32), TmpReg)
        .addReg(SubReg, SubKillState);


      // There could be undef components of a spilled super register.
      // TODO: Can we detect this and skip the spill?
      if (NumSubRegs > 1) {
        // The last implicit use of the SuperReg carries the "Kill" flag.
        unsigned SuperKillState = 0;
        if (i + 1 == e)
          SuperKillState |= getKillRegState(IsKill);
        Mov.addReg(SuperReg, RegState::Implicit | SuperKillState);
      }

      unsigned Align = FrameInfo.getObjectAlignment(Index);
      MachinePointerInfo PtrInfo
        = MachinePointerInfo::getFixedStack(*MF, Index, EltSize * i);
      MachineMemOperand *MMO
        = MF->getMachineMemOperand(PtrInfo, MachineMemOperand::MOStore,
                                   EltSize, MinAlign(Align, EltSize * i));
      BuildMI(*MBB, MI, DL, TII->get(AMDGPU::SI_SPILL_V32_SAVE))
        .addReg(TmpReg, RegState::Kill)    // src
        .addFrameIndex(Index)              // vaddr
        .addReg(MFI->getScratchRSrcReg())  // srrsrc
        .addReg(MFI->getFrameOffsetReg())  // soffset
        .addImm(i * 4)                     // offset
        .addMemOperand(MMO);
    }
  }

  if (M0CopyReg != AMDGPU::NoRegister) {
    BuildMI(*MBB, MI, DL, TII->get(AMDGPU::COPY), AMDGPU::M0)
      .addReg(M0CopyReg, RegState::Kill);
  }

  MI->eraseFromParent();
  MFI->addToSpilledSGPRs(NumSubRegs);
  return true;
}

bool SIRegisterInfo::restoreSGPR(MachineBasicBlock::iterator MI,
                                 int Index,
                                 RegScavenger *RS,
                                 bool OnlyToVGPR) const {
  MachineFunction *MF = MI->getParent()->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  MachineBasicBlock *MBB = MI->getParent();
  SIMachineFunctionInfo *MFI = MF->getInfo<SIMachineFunctionInfo>();

  ArrayRef<SIMachineFunctionInfo::SpilledReg> VGPRSpills
    = MFI->getSGPRToVGPRSpills(Index);
  bool SpillToVGPR = !VGPRSpills.empty();
  if (OnlyToVGPR && !SpillToVGPR)
    return false;

  MachineFrameInfo &FrameInfo = MF->getFrameInfo();
  const GCNSubtarget &ST =  MF->getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  const DebugLoc &DL = MI->getDebugLoc();

  unsigned SuperReg = MI->getOperand(0).getReg();
  bool SpillToSMEM = spillSGPRToSMEM();
  if (SpillToSMEM && OnlyToVGPR)
    return false;

  assert(SuperReg != AMDGPU::M0 && "m0 should never spill");

  unsigned OffsetReg = AMDGPU::M0;
  unsigned M0CopyReg = AMDGPU::NoRegister;

  if (SpillToSMEM) {
    if (RS->isRegUsed(AMDGPU::M0)) {
      M0CopyReg = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);
      BuildMI(*MBB, MI, DL, TII->get(AMDGPU::COPY), M0CopyReg)
        .addReg(AMDGPU::M0);
    }
  }

  unsigned EltSize = 4;
  unsigned ScalarLoadOp;

  const TargetRegisterClass *RC = getPhysRegClass(SuperReg);
  if (SpillToSMEM && isSGPRClass(RC)) {
    // XXX - if private_element_size is larger than 4 it might be useful to be
    // able to spill wider vmem spills.
    std::tie(EltSize, ScalarLoadOp) =
          getSpillEltSize(getRegSizeInBits(*RC) / 8, false);
  }

  ArrayRef<int16_t> SplitParts = getRegSplitParts(RC, EltSize);
  unsigned NumSubRegs = SplitParts.empty() ? 1 : SplitParts.size();

  // SubReg carries the "Kill" flag when SubReg == SuperReg.
  int64_t FrOffset = FrameInfo.getObjectOffset(Index);

  for (unsigned i = 0, e = NumSubRegs; i < e; ++i) {
    unsigned SubReg = NumSubRegs == 1 ?
      SuperReg : getSubReg(SuperReg, SplitParts[i]);

    if (SpillToSMEM) {
      // FIXME: Size may be > 4 but extra bytes wasted.
      unsigned Align = FrameInfo.getObjectAlignment(Index);
      MachinePointerInfo PtrInfo
        = MachinePointerInfo::getFixedStack(*MF, Index, EltSize * i);
      MachineMemOperand *MMO
        = MF->getMachineMemOperand(PtrInfo, MachineMemOperand::MOLoad,
                                   EltSize, MinAlign(Align, EltSize * i));

      // Add i * 4 offset
      int64_t Offset = (ST.getWavefrontSize() * FrOffset) + (EltSize * i);
      if (Offset != 0) {
        BuildMI(*MBB, MI, DL, TII->get(AMDGPU::S_ADD_U32), OffsetReg)
          .addReg(MFI->getFrameOffsetReg())
          .addImm(Offset);
      } else {
        BuildMI(*MBB, MI, DL, TII->get(AMDGPU::S_MOV_B32), OffsetReg)
          .addReg(MFI->getFrameOffsetReg());
      }

      auto MIB =
        BuildMI(*MBB, MI, DL, TII->get(ScalarLoadOp), SubReg)
        .addReg(MFI->getScratchRSrcReg()) // sbase
        .addReg(OffsetReg, RegState::Kill)                // soff
        .addImm(0)                        // glc
        .addMemOperand(MMO);

      if (NumSubRegs > 1 && i == 0)
        MIB.addReg(SuperReg, RegState::ImplicitDefine);

      continue;
    }

    if (SpillToVGPR) {
      SIMachineFunctionInfo::SpilledReg Spill = VGPRSpills[i];
      auto MIB =
        BuildMI(*MBB, MI, DL, TII->getMCOpcodeFromPseudo(AMDGPU::V_READLANE_B32),
                SubReg)
        .addReg(Spill.VGPR)
        .addImm(Spill.Lane);

      if (NumSubRegs > 1 && i == 0)
        MIB.addReg(SuperReg, RegState::ImplicitDefine);
    } else {
      if (OnlyToVGPR)
        return false;

      // Restore SGPR from a stack slot.
      // FIXME: We should use S_LOAD_DWORD here for VI.
      unsigned TmpReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
      unsigned Align = FrameInfo.getObjectAlignment(Index);

      MachinePointerInfo PtrInfo
        = MachinePointerInfo::getFixedStack(*MF, Index, EltSize * i);

      MachineMemOperand *MMO = MF->getMachineMemOperand(PtrInfo,
        MachineMemOperand::MOLoad, EltSize,
        MinAlign(Align, EltSize * i));

      BuildMI(*MBB, MI, DL, TII->get(AMDGPU::SI_SPILL_V32_RESTORE), TmpReg)
        .addFrameIndex(Index)              // vaddr
        .addReg(MFI->getScratchRSrcReg())  // srsrc
        .addReg(MFI->getFrameOffsetReg())  // soffset
        .addImm(i * 4)                     // offset
        .addMemOperand(MMO);

      auto MIB =
        BuildMI(*MBB, MI, DL, TII->get(AMDGPU::V_READFIRSTLANE_B32), SubReg)
        .addReg(TmpReg, RegState::Kill);

      if (NumSubRegs > 1)
        MIB.addReg(MI->getOperand(0).getReg(), RegState::ImplicitDefine);
    }
  }

  if (M0CopyReg != AMDGPU::NoRegister) {
    BuildMI(*MBB, MI, DL, TII->get(AMDGPU::COPY), AMDGPU::M0)
      .addReg(M0CopyReg, RegState::Kill);
  }

  MI->eraseFromParent();
  return true;
}

/// Special case of eliminateFrameIndex. Returns true if the SGPR was spilled to
/// a VGPR and the stack slot can be safely eliminated when all other users are
/// handled.
bool SIRegisterInfo::eliminateSGPRToVGPRSpillFrameIndex(
  MachineBasicBlock::iterator MI,
  int FI,
  RegScavenger *RS) const {
  switch (MI->getOpcode()) {
  case AMDGPU::SI_SPILL_S512_SAVE:
  case AMDGPU::SI_SPILL_S256_SAVE:
  case AMDGPU::SI_SPILL_S128_SAVE:
  case AMDGPU::SI_SPILL_S64_SAVE:
  case AMDGPU::SI_SPILL_S32_SAVE:
    return spillSGPR(MI, FI, RS, true);
  case AMDGPU::SI_SPILL_S512_RESTORE:
  case AMDGPU::SI_SPILL_S256_RESTORE:
  case AMDGPU::SI_SPILL_S128_RESTORE:
  case AMDGPU::SI_SPILL_S64_RESTORE:
  case AMDGPU::SI_SPILL_S32_RESTORE:
    return restoreSGPR(MI, FI, RS, true);
  default:
    llvm_unreachable("not an SGPR spill instruction");
  }
}

void SIRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator MI,
                                        int SPAdj, unsigned FIOperandNum,
                                        RegScavenger *RS) const {
  MachineFunction *MF = MI->getParent()->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  MachineBasicBlock *MBB = MI->getParent();
  SIMachineFunctionInfo *MFI = MF->getInfo<SIMachineFunctionInfo>();
  MachineFrameInfo &FrameInfo = MF->getFrameInfo();
  const GCNSubtarget &ST =  MF->getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  DebugLoc DL = MI->getDebugLoc();

  MachineOperand &FIOp = MI->getOperand(FIOperandNum);
  int Index = MI->getOperand(FIOperandNum).getIndex();

  switch (MI->getOpcode()) {
    // SGPR register spill
    case AMDGPU::SI_SPILL_S512_SAVE:
    case AMDGPU::SI_SPILL_S256_SAVE:
    case AMDGPU::SI_SPILL_S128_SAVE:
    case AMDGPU::SI_SPILL_S64_SAVE:
    case AMDGPU::SI_SPILL_S32_SAVE: {
      spillSGPR(MI, Index, RS);
      break;
    }

    // SGPR register restore
    case AMDGPU::SI_SPILL_S512_RESTORE:
    case AMDGPU::SI_SPILL_S256_RESTORE:
    case AMDGPU::SI_SPILL_S128_RESTORE:
    case AMDGPU::SI_SPILL_S64_RESTORE:
    case AMDGPU::SI_SPILL_S32_RESTORE: {
      restoreSGPR(MI, Index, RS);
      break;
    }

    // VGPR register spill
    case AMDGPU::SI_SPILL_V512_SAVE:
    case AMDGPU::SI_SPILL_V256_SAVE:
    case AMDGPU::SI_SPILL_V128_SAVE:
    case AMDGPU::SI_SPILL_V96_SAVE:
    case AMDGPU::SI_SPILL_V64_SAVE:
    case AMDGPU::SI_SPILL_V32_SAVE: {
      const MachineOperand *VData = TII->getNamedOperand(*MI,
                                                         AMDGPU::OpName::vdata);
      buildSpillLoadStore(MI, AMDGPU::BUFFER_STORE_DWORD_OFFSET,
            Index,
            VData->getReg(), VData->isKill(),
            TII->getNamedOperand(*MI, AMDGPU::OpName::srsrc)->getReg(),
            TII->getNamedOperand(*MI, AMDGPU::OpName::soffset)->getReg(),
            TII->getNamedOperand(*MI, AMDGPU::OpName::offset)->getImm(),
            *MI->memoperands_begin(),
            RS);
      MFI->addToSpilledVGPRs(getNumSubRegsForSpillOp(MI->getOpcode()));
      MI->eraseFromParent();
      break;
    }
    case AMDGPU::SI_SPILL_V32_RESTORE:
    case AMDGPU::SI_SPILL_V64_RESTORE:
    case AMDGPU::SI_SPILL_V96_RESTORE:
    case AMDGPU::SI_SPILL_V128_RESTORE:
    case AMDGPU::SI_SPILL_V256_RESTORE:
    case AMDGPU::SI_SPILL_V512_RESTORE: {
      const MachineOperand *VData = TII->getNamedOperand(*MI,
                                                         AMDGPU::OpName::vdata);

      buildSpillLoadStore(MI, AMDGPU::BUFFER_LOAD_DWORD_OFFSET,
            Index,
            VData->getReg(), VData->isKill(),
            TII->getNamedOperand(*MI, AMDGPU::OpName::srsrc)->getReg(),
            TII->getNamedOperand(*MI, AMDGPU::OpName::soffset)->getReg(),
            TII->getNamedOperand(*MI, AMDGPU::OpName::offset)->getImm(),
            *MI->memoperands_begin(),
            RS);
      MI->eraseFromParent();
      break;
    }

    default: {
      const DebugLoc &DL = MI->getDebugLoc();
      bool IsMUBUF = TII->isMUBUF(*MI);

      if (!IsMUBUF &&
          MFI->getFrameOffsetReg() != MFI->getScratchWaveOffsetReg()) {
        // Convert to an absolute stack address by finding the offset from the
        // scratch wave base and scaling by the wave size.
        //
        // In an entry function/kernel the stack address is already the
        // absolute address relative to the scratch wave offset.

        unsigned DiffReg
          = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);

        bool IsCopy = MI->getOpcode() == AMDGPU::V_MOV_B32_e32;
        unsigned ResultReg = IsCopy ?
          MI->getOperand(0).getReg() :
          MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);

        BuildMI(*MBB, MI, DL, TII->get(AMDGPU::S_SUB_U32), DiffReg)
          .addReg(MFI->getFrameOffsetReg())
          .addReg(MFI->getScratchWaveOffsetReg());

        int64_t Offset = FrameInfo.getObjectOffset(Index);
        if (Offset == 0) {
          // XXX - This never happens because of emergency scavenging slot at 0?
          BuildMI(*MBB, MI, DL, TII->get(AMDGPU::V_LSHRREV_B32_e64), ResultReg)
            .addImm(Log2_32(ST.getWavefrontSize()))
            .addReg(DiffReg);
        } else {
          unsigned ScaledReg
            = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);

          BuildMI(*MBB, MI, DL, TII->get(AMDGPU::V_LSHRREV_B32_e64), ScaledReg)
            .addImm(Log2_32(ST.getWavefrontSize()))
            .addReg(DiffReg, RegState::Kill);

          // TODO: Fold if use instruction is another add of a constant.
          if (AMDGPU::isInlinableLiteral32(Offset, ST.hasInv2PiInlineImm())) {
            TII->getAddNoCarry(*MBB, MI, DL, ResultReg)
              .addImm(Offset)
              .addReg(ScaledReg, RegState::Kill);
          } else {
            unsigned ConstOffsetReg
              = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);

            BuildMI(*MBB, MI, DL, TII->get(AMDGPU::S_MOV_B32), ConstOffsetReg)
              .addImm(Offset);
            TII->getAddNoCarry(*MBB, MI, DL, ResultReg)
              .addReg(ConstOffsetReg, RegState::Kill)
              .addReg(ScaledReg, RegState::Kill);
          }
        }

        // Don't introduce an extra copy if we're just materializing in a mov.
        if (IsCopy)
          MI->eraseFromParent();
        else
          FIOp.ChangeToRegister(ResultReg, false, false, true);
        return;
      }

      if (IsMUBUF) {
        // Disable offen so we don't need a 0 vgpr base.
        assert(static_cast<int>(FIOperandNum) ==
               AMDGPU::getNamedOperandIdx(MI->getOpcode(),
                                          AMDGPU::OpName::vaddr));

        assert(TII->getNamedOperand(*MI, AMDGPU::OpName::soffset)->getReg()
               == MFI->getFrameOffsetReg());

        int64_t Offset = FrameInfo.getObjectOffset(Index);
        int64_t OldImm
          = TII->getNamedOperand(*MI, AMDGPU::OpName::offset)->getImm();
        int64_t NewOffset = OldImm + Offset;

        if (isUInt<12>(NewOffset) &&
            buildMUBUFOffsetLoadStore(TII, FrameInfo, MI, Index, NewOffset)) {
          MI->eraseFromParent();
          return;
        }
      }

      // If the offset is simply too big, don't convert to a scratch wave offset
      // relative index.

      int64_t Offset = FrameInfo.getObjectOffset(Index);
      FIOp.ChangeToImmediate(Offset);
      if (!TII->isImmOperandLegal(*MI, FIOperandNum, FIOp)) {
        unsigned TmpReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
        BuildMI(*MBB, MI, DL, TII->get(AMDGPU::V_MOV_B32_e32), TmpReg)
          .addImm(Offset);
        FIOp.ChangeToRegister(TmpReg, false, false, true);
      }
    }
  }
}

StringRef SIRegisterInfo::getRegAsmName(unsigned Reg) const {
  #define AMDGPU_REG_ASM_NAMES
  #include "AMDGPURegAsmNames.inc.cpp"

  #define REG_RANGE(BeginReg, EndReg, RegTable)            \
    if (Reg >= BeginReg && Reg <= EndReg) {                \
      unsigned Index = Reg - BeginReg;                     \
      assert(Index < array_lengthof(RegTable));            \
      return RegTable[Index];                              \
    }

  REG_RANGE(AMDGPU::VGPR0, AMDGPU::VGPR255, VGPR32RegNames);
  REG_RANGE(AMDGPU::SGPR0, AMDGPU::SGPR103, SGPR32RegNames);
  REG_RANGE(AMDGPU::VGPR0_VGPR1, AMDGPU::VGPR254_VGPR255, VGPR64RegNames);
  REG_RANGE(AMDGPU::SGPR0_SGPR1, AMDGPU::SGPR102_SGPR103, SGPR64RegNames);
  REG_RANGE(AMDGPU::VGPR0_VGPR1_VGPR2, AMDGPU::VGPR253_VGPR254_VGPR255,
            VGPR96RegNames);

  REG_RANGE(AMDGPU::VGPR0_VGPR1_VGPR2_VGPR3,
            AMDGPU::VGPR252_VGPR253_VGPR254_VGPR255,
            VGPR128RegNames);
  REG_RANGE(AMDGPU::SGPR0_SGPR1_SGPR2_SGPR3,
            AMDGPU::SGPR100_SGPR101_SGPR102_SGPR103,
            SGPR128RegNames);

  REG_RANGE(AMDGPU::VGPR0_VGPR1_VGPR2_VGPR3_VGPR4_VGPR5_VGPR6_VGPR7,
            AMDGPU::VGPR248_VGPR249_VGPR250_VGPR251_VGPR252_VGPR253_VGPR254_VGPR255,
            VGPR256RegNames);

  REG_RANGE(
    AMDGPU::VGPR0_VGPR1_VGPR2_VGPR3_VGPR4_VGPR5_VGPR6_VGPR7_VGPR8_VGPR9_VGPR10_VGPR11_VGPR12_VGPR13_VGPR14_VGPR15,
    AMDGPU::VGPR240_VGPR241_VGPR242_VGPR243_VGPR244_VGPR245_VGPR246_VGPR247_VGPR248_VGPR249_VGPR250_VGPR251_VGPR252_VGPR253_VGPR254_VGPR255,
    VGPR512RegNames);

  REG_RANGE(AMDGPU::SGPR0_SGPR1_SGPR2_SGPR3_SGPR4_SGPR5_SGPR6_SGPR7,
            AMDGPU::SGPR96_SGPR97_SGPR98_SGPR99_SGPR100_SGPR101_SGPR102_SGPR103,
            SGPR256RegNames);

  REG_RANGE(
    AMDGPU::SGPR0_SGPR1_SGPR2_SGPR3_SGPR4_SGPR5_SGPR6_SGPR7_SGPR8_SGPR9_SGPR10_SGPR11_SGPR12_SGPR13_SGPR14_SGPR15,
    AMDGPU::SGPR88_SGPR89_SGPR90_SGPR91_SGPR92_SGPR93_SGPR94_SGPR95_SGPR96_SGPR97_SGPR98_SGPR99_SGPR100_SGPR101_SGPR102_SGPR103,
    SGPR512RegNames
  );

#undef REG_RANGE

  // FIXME: Rename flat_scr so we don't need to special case this.
  switch (Reg) {
  case AMDGPU::FLAT_SCR:
    return "flat_scratch";
  case AMDGPU::FLAT_SCR_LO:
    return "flat_scratch_lo";
  case AMDGPU::FLAT_SCR_HI:
    return "flat_scratch_hi";
  default:
    // For the special named registers the default is fine.
    return TargetRegisterInfo::getRegAsmName(Reg);
  }
}

// FIXME: This is very slow. It might be worth creating a map from physreg to
// register class.
const TargetRegisterClass *SIRegisterInfo::getPhysRegClass(unsigned Reg) const {
  assert(!TargetRegisterInfo::isVirtualRegister(Reg));

  static const TargetRegisterClass *const BaseClasses[] = {
    &AMDGPU::VGPR_32RegClass,
    &AMDGPU::SReg_32RegClass,
    &AMDGPU::VReg_64RegClass,
    &AMDGPU::SReg_64RegClass,
    &AMDGPU::VReg_96RegClass,
    &AMDGPU::VReg_128RegClass,
    &AMDGPU::SReg_128RegClass,
    &AMDGPU::VReg_256RegClass,
    &AMDGPU::SReg_256RegClass,
    &AMDGPU::VReg_512RegClass,
    &AMDGPU::SReg_512RegClass,
    &AMDGPU::SCC_CLASSRegClass,
    &AMDGPU::Pseudo_SReg_32RegClass,
    &AMDGPU::Pseudo_SReg_128RegClass,
  };

  for (const TargetRegisterClass *BaseClass : BaseClasses) {
    if (BaseClass->contains(Reg)) {
      return BaseClass;
    }
  }
  return nullptr;
}

// TODO: It might be helpful to have some target specific flags in
// TargetRegisterClass to mark which classes are VGPRs to make this trivial.
bool SIRegisterInfo::hasVGPRs(const TargetRegisterClass *RC) const {
  unsigned Size = getRegSizeInBits(*RC);
  if (Size < 32)
    return false;
  switch (Size) {
  case 32:
    return getCommonSubClass(&AMDGPU::VGPR_32RegClass, RC) != nullptr;
  case 64:
    return getCommonSubClass(&AMDGPU::VReg_64RegClass, RC) != nullptr;
  case 96:
    return getCommonSubClass(&AMDGPU::VReg_96RegClass, RC) != nullptr;
  case 128:
    return getCommonSubClass(&AMDGPU::VReg_128RegClass, RC) != nullptr;
  case 256:
    return getCommonSubClass(&AMDGPU::VReg_256RegClass, RC) != nullptr;
  case 512:
    return getCommonSubClass(&AMDGPU::VReg_512RegClass, RC) != nullptr;
  default:
    llvm_unreachable("Invalid register class size");
  }
}

const TargetRegisterClass *SIRegisterInfo::getEquivalentVGPRClass(
                                         const TargetRegisterClass *SRC) const {
  switch (getRegSizeInBits(*SRC)) {
  case 32:
    return &AMDGPU::VGPR_32RegClass;
  case 64:
    return &AMDGPU::VReg_64RegClass;
  case 96:
    return &AMDGPU::VReg_96RegClass;
  case 128:
    return &AMDGPU::VReg_128RegClass;
  case 256:
    return &AMDGPU::VReg_256RegClass;
  case 512:
    return &AMDGPU::VReg_512RegClass;
  default:
    llvm_unreachable("Invalid register class size");
  }
}

const TargetRegisterClass *SIRegisterInfo::getEquivalentSGPRClass(
                                         const TargetRegisterClass *VRC) const {
  switch (getRegSizeInBits(*VRC)) {
  case 32:
    return &AMDGPU::SGPR_32RegClass;
  case 64:
    return &AMDGPU::SReg_64RegClass;
  case 128:
    return &AMDGPU::SReg_128RegClass;
  case 256:
    return &AMDGPU::SReg_256RegClass;
  case 512:
    return &AMDGPU::SReg_512RegClass;
  default:
    llvm_unreachable("Invalid register class size");
  }
}

const TargetRegisterClass *SIRegisterInfo::getSubRegClass(
                         const TargetRegisterClass *RC, unsigned SubIdx) const {
  if (SubIdx == AMDGPU::NoSubRegister)
    return RC;

  // We can assume that each lane corresponds to one 32-bit register.
  unsigned Count = getSubRegIndexLaneMask(SubIdx).getNumLanes();
  if (isSGPRClass(RC)) {
    switch (Count) {
    case 1:
      return &AMDGPU::SGPR_32RegClass;
    case 2:
      return &AMDGPU::SReg_64RegClass;
    case 4:
      return &AMDGPU::SReg_128RegClass;
    case 8:
      return &AMDGPU::SReg_256RegClass;
    case 16: /* fall-through */
    default:
      llvm_unreachable("Invalid sub-register class size");
    }
  } else {
    switch (Count) {
    case 1:
      return &AMDGPU::VGPR_32RegClass;
    case 2:
      return &AMDGPU::VReg_64RegClass;
    case 3:
      return &AMDGPU::VReg_96RegClass;
    case 4:
      return &AMDGPU::VReg_128RegClass;
    case 8:
      return &AMDGPU::VReg_256RegClass;
    case 16: /* fall-through */
    default:
      llvm_unreachable("Invalid sub-register class size");
    }
  }
}

bool SIRegisterInfo::shouldRewriteCopySrc(
  const TargetRegisterClass *DefRC,
  unsigned DefSubReg,
  const TargetRegisterClass *SrcRC,
  unsigned SrcSubReg) const {
  // We want to prefer the smallest register class possible, so we don't want to
  // stop and rewrite on anything that looks like a subregister
  // extract. Operations mostly don't care about the super register class, so we
  // only want to stop on the most basic of copies between the same register
  // class.
  //
  // e.g. if we have something like
  // %0 = ...
  // %1 = ...
  // %2 = REG_SEQUENCE %0, sub0, %1, sub1, %2, sub2
  // %3 = COPY %2, sub0
  //
  // We want to look through the COPY to find:
  //  => %3 = COPY %0

  // Plain copy.
  return getCommonSubClass(DefRC, SrcRC) != nullptr;
}

/// Returns a register that is not used at any point in the function.
///        If all registers are used, then this function will return
//         AMDGPU::NoRegister.
unsigned
SIRegisterInfo::findUnusedRegister(const MachineRegisterInfo &MRI,
                                   const TargetRegisterClass *RC,
                                   const MachineFunction &MF) const {

  for (unsigned Reg : *RC)
    if (MRI.isAllocatable(Reg) && !MRI.isPhysRegUsed(Reg))
      return Reg;
  return AMDGPU::NoRegister;
}

ArrayRef<int16_t> SIRegisterInfo::getRegSplitParts(const TargetRegisterClass *RC,
                                                   unsigned EltSize) const {
  if (EltSize == 4) {
    static const int16_t Sub0_15[] = {
      AMDGPU::sub0, AMDGPU::sub1, AMDGPU::sub2, AMDGPU::sub3,
      AMDGPU::sub4, AMDGPU::sub5, AMDGPU::sub6, AMDGPU::sub7,
      AMDGPU::sub8, AMDGPU::sub9, AMDGPU::sub10, AMDGPU::sub11,
      AMDGPU::sub12, AMDGPU::sub13, AMDGPU::sub14, AMDGPU::sub15,
    };

    static const int16_t Sub0_7[] = {
      AMDGPU::sub0, AMDGPU::sub1, AMDGPU::sub2, AMDGPU::sub3,
      AMDGPU::sub4, AMDGPU::sub5, AMDGPU::sub6, AMDGPU::sub7,
    };

    static const int16_t Sub0_3[] = {
      AMDGPU::sub0, AMDGPU::sub1, AMDGPU::sub2, AMDGPU::sub3,
    };

    static const int16_t Sub0_2[] = {
      AMDGPU::sub0, AMDGPU::sub1, AMDGPU::sub2,
    };

    static const int16_t Sub0_1[] = {
      AMDGPU::sub0, AMDGPU::sub1,
    };

    switch (AMDGPU::getRegBitWidth(*RC->MC)) {
    case 32:
      return {};
    case 64:
      return makeArrayRef(Sub0_1);
    case 96:
      return makeArrayRef(Sub0_2);
    case 128:
      return makeArrayRef(Sub0_3);
    case 256:
      return makeArrayRef(Sub0_7);
    case 512:
      return makeArrayRef(Sub0_15);
    default:
      llvm_unreachable("unhandled register size");
    }
  }

  if (EltSize == 8) {
    static const int16_t Sub0_15_64[] = {
      AMDGPU::sub0_sub1, AMDGPU::sub2_sub3,
      AMDGPU::sub4_sub5, AMDGPU::sub6_sub7,
      AMDGPU::sub8_sub9, AMDGPU::sub10_sub11,
      AMDGPU::sub12_sub13, AMDGPU::sub14_sub15
    };

    static const int16_t Sub0_7_64[] = {
      AMDGPU::sub0_sub1, AMDGPU::sub2_sub3,
      AMDGPU::sub4_sub5, AMDGPU::sub6_sub7
    };


    static const int16_t Sub0_3_64[] = {
      AMDGPU::sub0_sub1, AMDGPU::sub2_sub3
    };

    switch (AMDGPU::getRegBitWidth(*RC->MC)) {
    case 64:
      return {};
    case 128:
      return makeArrayRef(Sub0_3_64);
    case 256:
      return makeArrayRef(Sub0_7_64);
    case 512:
      return makeArrayRef(Sub0_15_64);
    default:
      llvm_unreachable("unhandled register size");
    }
  }

  assert(EltSize == 16 && "unhandled register spill split size");

  static const int16_t Sub0_15_128[] = {
    AMDGPU::sub0_sub1_sub2_sub3,
    AMDGPU::sub4_sub5_sub6_sub7,
    AMDGPU::sub8_sub9_sub10_sub11,
    AMDGPU::sub12_sub13_sub14_sub15
  };

  static const int16_t Sub0_7_128[] = {
    AMDGPU::sub0_sub1_sub2_sub3,
    AMDGPU::sub4_sub5_sub6_sub7
  };

  switch (AMDGPU::getRegBitWidth(*RC->MC)) {
  case 128:
    return {};
  case 256:
    return makeArrayRef(Sub0_7_128);
  case 512:
    return makeArrayRef(Sub0_15_128);
  default:
    llvm_unreachable("unhandled register size");
  }
}

const TargetRegisterClass*
SIRegisterInfo::getRegClassForReg(const MachineRegisterInfo &MRI,
                                  unsigned Reg) const {
  if (TargetRegisterInfo::isVirtualRegister(Reg))
    return  MRI.getRegClass(Reg);

  return getPhysRegClass(Reg);
}

bool SIRegisterInfo::isVGPR(const MachineRegisterInfo &MRI,
                            unsigned Reg) const {
  const TargetRegisterClass * RC = getRegClassForReg(MRI, Reg);
  assert(RC && "Register class for the reg not found");
  return hasVGPRs(RC);
}

bool SIRegisterInfo::shouldCoalesce(MachineInstr *MI,
                                    const TargetRegisterClass *SrcRC,
                                    unsigned SubReg,
                                    const TargetRegisterClass *DstRC,
                                    unsigned DstSubReg,
                                    const TargetRegisterClass *NewRC,
                                    LiveIntervals &LIS) const {
  unsigned SrcSize = getRegSizeInBits(*SrcRC);
  unsigned DstSize = getRegSizeInBits(*DstRC);
  unsigned NewSize = getRegSizeInBits(*NewRC);

  // Do not increase size of registers beyond dword, we would need to allocate
  // adjacent registers and constraint regalloc more than needed.

  // Always allow dword coalescing.
  if (SrcSize <= 32 || DstSize <= 32)
    return true;

  return NewSize <= DstSize || NewSize <= SrcSize;
}

unsigned SIRegisterInfo::getRegPressureLimit(const TargetRegisterClass *RC,
                                             MachineFunction &MF) const {

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();

  unsigned Occupancy = ST.getOccupancyWithLocalMemSize(MFI->getLDSSize(),
                                                       MF.getFunction());
  switch (RC->getID()) {
  default:
    return AMDGPURegisterInfo::getRegPressureLimit(RC, MF);
  case AMDGPU::VGPR_32RegClassID:
    return std::min(ST.getMaxNumVGPRs(Occupancy), ST.getMaxNumVGPRs(MF));
  case AMDGPU::SGPR_32RegClassID:
    return std::min(ST.getMaxNumSGPRs(Occupancy, true), ST.getMaxNumSGPRs(MF));
  }
}

unsigned SIRegisterInfo::getRegPressureSetLimit(const MachineFunction &MF,
                                                unsigned Idx) const {
  if (Idx == getVGPRPressureSet())
    return getRegPressureLimit(&AMDGPU::VGPR_32RegClass,
                               const_cast<MachineFunction &>(MF));

  if (Idx == getSGPRPressureSet())
    return getRegPressureLimit(&AMDGPU::SGPR_32RegClass,
                               const_cast<MachineFunction &>(MF));

  return AMDGPURegisterInfo::getRegPressureSetLimit(MF, Idx);
}

const int *SIRegisterInfo::getRegUnitPressureSets(unsigned RegUnit) const {
  static const int Empty[] = { -1 };

  if (hasRegUnit(AMDGPU::M0, RegUnit))
    return Empty;
  return AMDGPURegisterInfo::getRegUnitPressureSets(RegUnit);
}

unsigned SIRegisterInfo::getReturnAddressReg(const MachineFunction &MF) const {
  // Not a callee saved register.
  return AMDGPU::SGPR30_SGPR31;
}

const TargetRegisterClass *
SIRegisterInfo::getConstrainedRegClassForOperand(const MachineOperand &MO,
                                         const MachineRegisterInfo &MRI) const {
  unsigned Size = getRegSizeInBits(MO.getReg(), MRI);
  const RegisterBank *RB = MRI.getRegBankOrNull(MO.getReg());
  if (!RB)
    return nullptr;

  switch (Size) {
  case 32:
    return RB->getID() == AMDGPU::VGPRRegBankID ? &AMDGPU::VGPR_32RegClass :
                                                  &AMDGPU::SReg_32_XM0RegClass;
  case 64:
    return RB->getID() == AMDGPU::VGPRRegBankID ? &AMDGPU::VReg_64RegClass :
                                                   &AMDGPU::SReg_64_XEXECRegClass;
  case 96:
    return RB->getID() == AMDGPU::VGPRRegBankID ? &AMDGPU::VReg_96RegClass :
                                                  nullptr;
  case 128:
    return RB->getID() == AMDGPU::VGPRRegBankID ? &AMDGPU::VReg_128RegClass :
                                                  &AMDGPU::SReg_128RegClass;
  default:
    llvm_unreachable("not implemented");
  }
}

// Find reaching register definition
MachineInstr *SIRegisterInfo::findReachingDef(unsigned Reg, unsigned SubReg,
                                              MachineInstr &Use,
                                              MachineRegisterInfo &MRI,
                                              LiveIntervals *LIS) const {
  auto &MDT = LIS->getAnalysis<MachineDominatorTree>();
  SlotIndex UseIdx = LIS->getInstructionIndex(Use);
  SlotIndex DefIdx;

  if (TargetRegisterInfo::isVirtualRegister(Reg)) {
    if (!LIS->hasInterval(Reg))
      return nullptr;
    LiveInterval &LI = LIS->getInterval(Reg);
    LaneBitmask SubLanes = SubReg ? getSubRegIndexLaneMask(SubReg)
                                  : MRI.getMaxLaneMaskForVReg(Reg);
    VNInfo *V = nullptr;
    if (LI.hasSubRanges()) {
      for (auto &S : LI.subranges()) {
        if ((S.LaneMask & SubLanes) == SubLanes) {
          V = S.getVNInfoAt(UseIdx);
          break;
        }
      }
    } else {
      V = LI.getVNInfoAt(UseIdx);
    }
    if (!V)
      return nullptr;
    DefIdx = V->def;
  } else {
    // Find last def.
    for (MCRegUnitIterator Units(Reg, this); Units.isValid(); ++Units) {
      LiveRange &LR = LIS->getRegUnit(*Units);
      if (VNInfo *V = LR.getVNInfoAt(UseIdx)) {
        if (!DefIdx.isValid() ||
            MDT.dominates(LIS->getInstructionFromIndex(DefIdx),
                          LIS->getInstructionFromIndex(V->def)))
          DefIdx = V->def;
      } else {
        return nullptr;
      }
    }
  }

  MachineInstr *Def = LIS->getInstructionFromIndex(DefIdx);

  if (!Def || !MDT.dominates(Def, &Use))
    return nullptr;

  assert(Def->modifiesRegister(Reg, this));

  return Def;
}
