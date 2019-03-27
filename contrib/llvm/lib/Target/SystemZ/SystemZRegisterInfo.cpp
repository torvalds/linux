//===-- SystemZRegisterInfo.cpp - SystemZ register information ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SystemZRegisterInfo.h"
#include "SystemZInstrInfo.h"
#include "SystemZSubtarget.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/VirtRegMap.h"

using namespace llvm;

#define GET_REGINFO_TARGET_DESC
#include "SystemZGenRegisterInfo.inc"

SystemZRegisterInfo::SystemZRegisterInfo()
    : SystemZGenRegisterInfo(SystemZ::R14D) {}

// Given that MO is a GRX32 operand, return either GR32 or GRH32 if MO
// somehow belongs in it. Otherwise, return GRX32.
static const TargetRegisterClass *getRC32(MachineOperand &MO,
                                          const VirtRegMap *VRM,
                                          const MachineRegisterInfo *MRI) {
  const TargetRegisterClass *RC = MRI->getRegClass(MO.getReg());

  if (SystemZ::GR32BitRegClass.hasSubClassEq(RC) ||
      MO.getSubReg() == SystemZ::subreg_l32 ||
      MO.getSubReg() == SystemZ::subreg_hl32)
    return &SystemZ::GR32BitRegClass;
  if (SystemZ::GRH32BitRegClass.hasSubClassEq(RC) ||
      MO.getSubReg() == SystemZ::subreg_h32 ||
      MO.getSubReg() == SystemZ::subreg_hh32)
    return &SystemZ::GRH32BitRegClass;

  if (VRM && VRM->hasPhys(MO.getReg())) {
    unsigned PhysReg = VRM->getPhys(MO.getReg());
    if (SystemZ::GR32BitRegClass.contains(PhysReg))
      return &SystemZ::GR32BitRegClass;
    assert (SystemZ::GRH32BitRegClass.contains(PhysReg) &&
            "Phys reg not in GR32 or GRH32?");
    return &SystemZ::GRH32BitRegClass;
  }

  assert (RC == &SystemZ::GRX32BitRegClass);
  return RC;
}

bool
SystemZRegisterInfo::getRegAllocationHints(unsigned VirtReg,
                                           ArrayRef<MCPhysReg> Order,
                                           SmallVectorImpl<MCPhysReg> &Hints,
                                           const MachineFunction &MF,
                                           const VirtRegMap *VRM,
                                           const LiveRegMatrix *Matrix) const {
  const MachineRegisterInfo *MRI = &MF.getRegInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  bool BaseImplRetVal = TargetRegisterInfo::getRegAllocationHints(
      VirtReg, Order, Hints, MF, VRM, Matrix);

  if (MRI->getRegClass(VirtReg) == &SystemZ::GRX32BitRegClass) {
    SmallVector<unsigned, 8> Worklist;
    SmallSet<unsigned, 4> DoneRegs;
    Worklist.push_back(VirtReg);
    while (Worklist.size()) {
      unsigned Reg = Worklist.pop_back_val();
      if (!DoneRegs.insert(Reg).second)
        continue;

      for (auto &Use : MRI->use_instructions(Reg))
        // For LOCRMux, see if the other operand is already a high or low
        // register, and in that case give the correpsonding hints for
        // VirtReg. LOCR instructions need both operands in either high or
        // low parts.
        if (Use.getOpcode() == SystemZ::LOCRMux) {
          MachineOperand &TrueMO = Use.getOperand(1);
          MachineOperand &FalseMO = Use.getOperand(2);
          const TargetRegisterClass *RC =
            TRI->getCommonSubClass(getRC32(FalseMO, VRM, MRI),
                                   getRC32(TrueMO, VRM, MRI));
          if (RC && RC != &SystemZ::GRX32BitRegClass) {
            // Pass the registers of RC as hints while making sure that if
            // any of these registers are copy hints, hint them first.
            SmallSet<unsigned, 4> CopyHints;
            CopyHints.insert(Hints.begin(), Hints.end());
            Hints.clear();
            for (MCPhysReg Reg : Order)
              if (CopyHints.count(Reg) &&
                  RC->contains(Reg) && !MRI->isReserved(Reg))
                Hints.push_back(Reg);
            for (MCPhysReg Reg : Order)
              if (!CopyHints.count(Reg) &&
                  RC->contains(Reg) && !MRI->isReserved(Reg))
                Hints.push_back(Reg);
            // Return true to make these hints the only regs available to
            // RA. This may mean extra spilling but since the alternative is
            // a jump sequence expansion of the LOCRMux, it is preferred.
            return true;
          }

          // Add the other operand of the LOCRMux to the worklist.
          unsigned OtherReg =
            (TrueMO.getReg() == Reg ? FalseMO.getReg() : TrueMO.getReg());
          if (MRI->getRegClass(OtherReg) == &SystemZ::GRX32BitRegClass)
            Worklist.push_back(OtherReg);
        }
    }
  }

  return BaseImplRetVal;
}

const MCPhysReg *
SystemZRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  const SystemZSubtarget &Subtarget = MF->getSubtarget<SystemZSubtarget>();
  if (MF->getFunction().getCallingConv() == CallingConv::AnyReg)
    return Subtarget.hasVector()? CSR_SystemZ_AllRegs_Vector_SaveList
                                : CSR_SystemZ_AllRegs_SaveList;
  if (MF->getSubtarget().getTargetLowering()->supportSwiftError() &&
      MF->getFunction().getAttributes().hasAttrSomewhere(
          Attribute::SwiftError))
    return CSR_SystemZ_SwiftError_SaveList;
  return CSR_SystemZ_SaveList;
}

const uint32_t *
SystemZRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                          CallingConv::ID CC) const {
  const SystemZSubtarget &Subtarget = MF.getSubtarget<SystemZSubtarget>();
  if (CC == CallingConv::AnyReg)
    return Subtarget.hasVector()? CSR_SystemZ_AllRegs_Vector_RegMask
                                : CSR_SystemZ_AllRegs_RegMask;
  if (MF.getSubtarget().getTargetLowering()->supportSwiftError() &&
      MF.getFunction().getAttributes().hasAttrSomewhere(
          Attribute::SwiftError))
    return CSR_SystemZ_SwiftError_RegMask;
  return CSR_SystemZ_RegMask;
}

BitVector
SystemZRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  const SystemZFrameLowering *TFI = getFrameLowering(MF);

  if (TFI->hasFP(MF)) {
    // R11D is the frame pointer.  Reserve all aliases.
    Reserved.set(SystemZ::R11D);
    Reserved.set(SystemZ::R11L);
    Reserved.set(SystemZ::R11H);
    Reserved.set(SystemZ::R10Q);
  }

  // R15D is the stack pointer.  Reserve all aliases.
  Reserved.set(SystemZ::R15D);
  Reserved.set(SystemZ::R15L);
  Reserved.set(SystemZ::R15H);
  Reserved.set(SystemZ::R14Q);

  // A0 and A1 hold the thread pointer.
  Reserved.set(SystemZ::A0);
  Reserved.set(SystemZ::A1);

  return Reserved;
}

void
SystemZRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator MI,
                                         int SPAdj, unsigned FIOperandNum,
                                         RegScavenger *RS) const {
  assert(SPAdj == 0 && "Outgoing arguments should be part of the frame");

  MachineBasicBlock &MBB = *MI->getParent();
  MachineFunction &MF = *MBB.getParent();
  auto *TII =
      static_cast<const SystemZInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const SystemZFrameLowering *TFI = getFrameLowering(MF);
  DebugLoc DL = MI->getDebugLoc();

  // Decompose the frame index into a base and offset.
  int FrameIndex = MI->getOperand(FIOperandNum).getIndex();
  unsigned BasePtr;
  int64_t Offset = (TFI->getFrameIndexReference(MF, FrameIndex, BasePtr) +
                    MI->getOperand(FIOperandNum + 1).getImm());

  // Special handling of dbg_value instructions.
  if (MI->isDebugValue()) {
    MI->getOperand(FIOperandNum).ChangeToRegister(BasePtr, /*isDef*/ false);
    MI->getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
    return;
  }

  // See if the offset is in range, or if an equivalent instruction that
  // accepts the offset exists.
  unsigned Opcode = MI->getOpcode();
  unsigned OpcodeForOffset = TII->getOpcodeForOffset(Opcode, Offset);
  if (OpcodeForOffset) {
    if (OpcodeForOffset == SystemZ::LE &&
        MF.getSubtarget<SystemZSubtarget>().hasVector()) {
      // If LE is ok for offset, use LDE instead on z13.
      OpcodeForOffset = SystemZ::LDE32;
    }
    MI->getOperand(FIOperandNum).ChangeToRegister(BasePtr, false);
  }
  else {
    // Create an anchor point that is in range.  Start at 0xffff so that
    // can use LLILH to load the immediate.
    int64_t OldOffset = Offset;
    int64_t Mask = 0xffff;
    do {
      Offset = OldOffset & Mask;
      OpcodeForOffset = TII->getOpcodeForOffset(Opcode, Offset);
      Mask >>= 1;
      assert(Mask && "One offset must be OK");
    } while (!OpcodeForOffset);

    unsigned ScratchReg =
      MF.getRegInfo().createVirtualRegister(&SystemZ::ADDR64BitRegClass);
    int64_t HighOffset = OldOffset - Offset;

    if (MI->getDesc().TSFlags & SystemZII::HasIndex
        && MI->getOperand(FIOperandNum + 2).getReg() == 0) {
      // Load the offset into the scratch register and use it as an index.
      // The scratch register then dies here.
      TII->loadImmediate(MBB, MI, ScratchReg, HighOffset);
      MI->getOperand(FIOperandNum).ChangeToRegister(BasePtr, false);
      MI->getOperand(FIOperandNum + 2).ChangeToRegister(ScratchReg,
                                                        false, false, true);
    } else {
      // Load the anchor address into a scratch register.
      unsigned LAOpcode = TII->getOpcodeForOffset(SystemZ::LA, HighOffset);
      if (LAOpcode)
        BuildMI(MBB, MI, DL, TII->get(LAOpcode),ScratchReg)
          .addReg(BasePtr).addImm(HighOffset).addReg(0);
      else {
        // Load the high offset into the scratch register and use it as
        // an index.
        TII->loadImmediate(MBB, MI, ScratchReg, HighOffset);
        BuildMI(MBB, MI, DL, TII->get(SystemZ::AGR),ScratchReg)
          .addReg(ScratchReg, RegState::Kill).addReg(BasePtr);
      }

      // Use the scratch register as the base.  It then dies here.
      MI->getOperand(FIOperandNum).ChangeToRegister(ScratchReg,
                                                    false, false, true);
    }
  }
  MI->setDesc(TII->get(OpcodeForOffset));
  MI->getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
}

bool SystemZRegisterInfo::shouldCoalesce(MachineInstr *MI,
                                  const TargetRegisterClass *SrcRC,
                                  unsigned SubReg,
                                  const TargetRegisterClass *DstRC,
                                  unsigned DstSubReg,
                                  const TargetRegisterClass *NewRC,
                                  LiveIntervals &LIS) const {
  assert (MI->isCopy() && "Only expecting COPY instructions");

  // Coalesce anything which is not a COPY involving a subreg to/from GR128.
  if (!(NewRC->hasSuperClassEq(&SystemZ::GR128BitRegClass) &&
        (getRegSizeInBits(*SrcRC) <= 64 || getRegSizeInBits(*DstRC) <= 64)))
    return true;

  // Allow coalescing of a GR128 subreg COPY only if the live ranges are small
  // and local to one MBB with not too much interferring registers. Otherwise
  // regalloc may run out of registers.

  unsigned WideOpNo = (getRegSizeInBits(*SrcRC) == 128 ? 1 : 0);
  unsigned GR128Reg = MI->getOperand(WideOpNo).getReg();
  unsigned GRNarReg = MI->getOperand((WideOpNo == 1) ? 0 : 1).getReg();
  LiveInterval &IntGR128 = LIS.getInterval(GR128Reg);
  LiveInterval &IntGRNar = LIS.getInterval(GRNarReg);

  // Check that the two virtual registers are local to MBB.
  MachineBasicBlock *MBB = MI->getParent();
  MachineInstr *FirstMI_GR128 =
    LIS.getInstructionFromIndex(IntGR128.beginIndex());
  MachineInstr *FirstMI_GRNar =
    LIS.getInstructionFromIndex(IntGRNar.beginIndex());
  MachineInstr *LastMI_GR128 = LIS.getInstructionFromIndex(IntGR128.endIndex());
  MachineInstr *LastMI_GRNar = LIS.getInstructionFromIndex(IntGRNar.endIndex());
  if ((!FirstMI_GR128 || FirstMI_GR128->getParent() != MBB) ||
      (!FirstMI_GRNar || FirstMI_GRNar->getParent() != MBB) ||
      (!LastMI_GR128 || LastMI_GR128->getParent() != MBB) ||
      (!LastMI_GRNar || LastMI_GRNar->getParent() != MBB))
    return false;

  MachineBasicBlock::iterator MII = nullptr, MEE = nullptr;
  if (WideOpNo == 1) {
    MII = FirstMI_GR128;
    MEE = LastMI_GRNar;
  } else {
    MII = FirstMI_GRNar;
    MEE = LastMI_GR128;
  }

  // Check if coalescing seems safe by finding the set of clobbered physreg
  // pairs in the region.
  BitVector PhysClobbered(getNumRegs());
  MEE++;
  for (; MII != MEE; ++MII) {
    for (const MachineOperand &MO : MII->operands())
      if (MO.isReg() && isPhysicalRegister(MO.getReg())) {
        for (MCSuperRegIterator SI(MO.getReg(), this, true/*IncludeSelf*/);
             SI.isValid(); ++SI)
          if (NewRC->contains(*SI)) {
            PhysClobbered.set(*SI);
            break;
          }
      }
  }

  // Demand an arbitrary margin of free regs.
  unsigned const DemandedFreeGR128 = 3;
  if (PhysClobbered.count() > (NewRC->getNumRegs() - DemandedFreeGR128))
    return false;

  return true;
}

unsigned
SystemZRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const SystemZFrameLowering *TFI = getFrameLowering(MF);
  return TFI->hasFP(MF) ? SystemZ::R11D : SystemZ::R15D;
}

const TargetRegisterClass *
SystemZRegisterInfo::getCrossCopyRegClass(const TargetRegisterClass *RC) const {
  if (RC == &SystemZ::CCRRegClass)
    return &SystemZ::GR32BitRegClass;
  return RC;
}

