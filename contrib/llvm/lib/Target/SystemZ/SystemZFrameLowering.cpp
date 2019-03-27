//===-- SystemZFrameLowering.cpp - Frame lowering for SystemZ -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SystemZFrameLowering.h"
#include "SystemZCallingConv.h"
#include "SystemZInstrBuilder.h"
#include "SystemZInstrInfo.h"
#include "SystemZMachineFunctionInfo.h"
#include "SystemZRegisterInfo.h"
#include "SystemZSubtarget.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/IR/Function.h"

using namespace llvm;

namespace {
// The ABI-defined register save slots, relative to the incoming stack
// pointer.
static const TargetFrameLowering::SpillSlot SpillOffsetTable[] = {
  { SystemZ::R2D,  0x10 },
  { SystemZ::R3D,  0x18 },
  { SystemZ::R4D,  0x20 },
  { SystemZ::R5D,  0x28 },
  { SystemZ::R6D,  0x30 },
  { SystemZ::R7D,  0x38 },
  { SystemZ::R8D,  0x40 },
  { SystemZ::R9D,  0x48 },
  { SystemZ::R10D, 0x50 },
  { SystemZ::R11D, 0x58 },
  { SystemZ::R12D, 0x60 },
  { SystemZ::R13D, 0x68 },
  { SystemZ::R14D, 0x70 },
  { SystemZ::R15D, 0x78 },
  { SystemZ::F0D,  0x80 },
  { SystemZ::F2D,  0x88 },
  { SystemZ::F4D,  0x90 },
  { SystemZ::F6D,  0x98 }
};
} // end anonymous namespace

SystemZFrameLowering::SystemZFrameLowering()
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, 8,
                          -SystemZMC::CallFrameSize, 8,
                          false /* StackRealignable */) {
  // Create a mapping from register number to save slot offset.
  RegSpillOffsets.grow(SystemZ::NUM_TARGET_REGS);
  for (unsigned I = 0, E = array_lengthof(SpillOffsetTable); I != E; ++I)
    RegSpillOffsets[SpillOffsetTable[I].Reg] = SpillOffsetTable[I].Offset;
}

const TargetFrameLowering::SpillSlot *
SystemZFrameLowering::getCalleeSavedSpillSlots(unsigned &NumEntries) const {
  NumEntries = array_lengthof(SpillOffsetTable);
  return SpillOffsetTable;
}

void SystemZFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                                BitVector &SavedRegs,
                                                RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);

  MachineFrameInfo &MFFrame = MF.getFrameInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  bool HasFP = hasFP(MF);
  SystemZMachineFunctionInfo *MFI = MF.getInfo<SystemZMachineFunctionInfo>();
  bool IsVarArg = MF.getFunction().isVarArg();

  // va_start stores incoming FPR varargs in the normal way, but delegates
  // the saving of incoming GPR varargs to spillCalleeSavedRegisters().
  // Record these pending uses, which typically include the call-saved
  // argument register R6D.
  if (IsVarArg)
    for (unsigned I = MFI->getVarArgsFirstGPR(); I < SystemZ::NumArgGPRs; ++I)
      SavedRegs.set(SystemZ::ArgGPRs[I]);

  // If there are any landing pads, entering them will modify r6/r7.
  if (!MF.getLandingPads().empty()) {
    SavedRegs.set(SystemZ::R6D);
    SavedRegs.set(SystemZ::R7D);
  }

  // If the function requires a frame pointer, record that the hard
  // frame pointer will be clobbered.
  if (HasFP)
    SavedRegs.set(SystemZ::R11D);

  // If the function calls other functions, record that the return
  // address register will be clobbered.
  if (MFFrame.hasCalls())
    SavedRegs.set(SystemZ::R14D);

  // If we are saving GPRs other than the stack pointer, we might as well
  // save and restore the stack pointer at the same time, via STMG and LMG.
  // This allows the deallocation to be done by the LMG, rather than needing
  // a separate %r15 addition.
  const MCPhysReg *CSRegs = TRI->getCalleeSavedRegs(&MF);
  for (unsigned I = 0; CSRegs[I]; ++I) {
    unsigned Reg = CSRegs[I];
    if (SystemZ::GR64BitRegClass.contains(Reg) && SavedRegs.test(Reg)) {
      SavedRegs.set(SystemZ::R15D);
      break;
    }
  }
}

// Add GPR64 to the save instruction being built by MIB, which is in basic
// block MBB.  IsImplicit says whether this is an explicit operand to the
// instruction, or an implicit one that comes between the explicit start
// and end registers.
static void addSavedGPR(MachineBasicBlock &MBB, MachineInstrBuilder &MIB,
                        unsigned GPR64, bool IsImplicit) {
  const TargetRegisterInfo *RI =
      MBB.getParent()->getSubtarget().getRegisterInfo();
  unsigned GPR32 = RI->getSubReg(GPR64, SystemZ::subreg_l32);
  bool IsLive = MBB.isLiveIn(GPR64) || MBB.isLiveIn(GPR32);
  if (!IsLive || !IsImplicit) {
    MIB.addReg(GPR64, getImplRegState(IsImplicit) | getKillRegState(!IsLive));
    if (!IsLive)
      MBB.addLiveIn(GPR64);
  }
}

bool SystemZFrameLowering::
spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator MBBI,
                          const std::vector<CalleeSavedInfo> &CSI,
                          const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return false;

  MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  SystemZMachineFunctionInfo *ZFI = MF.getInfo<SystemZMachineFunctionInfo>();
  bool IsVarArg = MF.getFunction().isVarArg();
  DebugLoc DL;

  // Scan the call-saved GPRs and find the bounds of the register spill area.
  unsigned LowGPR = 0;
  unsigned HighGPR = SystemZ::R15D;
  unsigned StartOffset = -1U;
  for (unsigned I = 0, E = CSI.size(); I != E; ++I) {
    unsigned Reg = CSI[I].getReg();
    if (SystemZ::GR64BitRegClass.contains(Reg)) {
      unsigned Offset = RegSpillOffsets[Reg];
      assert(Offset && "Unexpected GPR save");
      if (StartOffset > Offset) {
        LowGPR = Reg;
        StartOffset = Offset;
      }
    }
  }

  // Save the range of call-saved registers, for use by the epilogue inserter.
  ZFI->setLowSavedGPR(LowGPR);
  ZFI->setHighSavedGPR(HighGPR);

  // Include the GPR varargs, if any.  R6D is call-saved, so would
  // be included by the loop above, but we also need to handle the
  // call-clobbered argument registers.
  if (IsVarArg) {
    unsigned FirstGPR = ZFI->getVarArgsFirstGPR();
    if (FirstGPR < SystemZ::NumArgGPRs) {
      unsigned Reg = SystemZ::ArgGPRs[FirstGPR];
      unsigned Offset = RegSpillOffsets[Reg];
      if (StartOffset > Offset) {
        LowGPR = Reg; StartOffset = Offset;
      }
    }
  }

  // Save GPRs
  if (LowGPR) {
    assert(LowGPR != HighGPR && "Should be saving %r15 and something else");

    // Build an STMG instruction.
    MachineInstrBuilder MIB = BuildMI(MBB, MBBI, DL, TII->get(SystemZ::STMG));

    // Add the explicit register operands.
    addSavedGPR(MBB, MIB, LowGPR, false);
    addSavedGPR(MBB, MIB, HighGPR, false);

    // Add the address.
    MIB.addReg(SystemZ::R15D).addImm(StartOffset);

    // Make sure all call-saved GPRs are included as operands and are
    // marked as live on entry.
    for (unsigned I = 0, E = CSI.size(); I != E; ++I) {
      unsigned Reg = CSI[I].getReg();
      if (SystemZ::GR64BitRegClass.contains(Reg))
        addSavedGPR(MBB, MIB, Reg, true);
    }

    // ...likewise GPR varargs.
    if (IsVarArg)
      for (unsigned I = ZFI->getVarArgsFirstGPR(); I < SystemZ::NumArgGPRs; ++I)
        addSavedGPR(MBB, MIB, SystemZ::ArgGPRs[I], true);
  }

  // Save FPRs/VRs in the normal TargetInstrInfo way.
  for (unsigned I = 0, E = CSI.size(); I != E; ++I) {
    unsigned Reg = CSI[I].getReg();
    if (SystemZ::FP64BitRegClass.contains(Reg)) {
      MBB.addLiveIn(Reg);
      TII->storeRegToStackSlot(MBB, MBBI, Reg, true, CSI[I].getFrameIdx(),
                               &SystemZ::FP64BitRegClass, TRI);
    }
    if (SystemZ::VR128BitRegClass.contains(Reg)) {
      MBB.addLiveIn(Reg);
      TII->storeRegToStackSlot(MBB, MBBI, Reg, true, CSI[I].getFrameIdx(),
                               &SystemZ::VR128BitRegClass, TRI);
    }
  }

  return true;
}

bool SystemZFrameLowering::
restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI,
                            std::vector<CalleeSavedInfo> &CSI,
                            const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return false;

  MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  SystemZMachineFunctionInfo *ZFI = MF.getInfo<SystemZMachineFunctionInfo>();
  bool HasFP = hasFP(MF);
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  // Restore FPRs/VRs in the normal TargetInstrInfo way.
  for (unsigned I = 0, E = CSI.size(); I != E; ++I) {
    unsigned Reg = CSI[I].getReg();
    if (SystemZ::FP64BitRegClass.contains(Reg))
      TII->loadRegFromStackSlot(MBB, MBBI, Reg, CSI[I].getFrameIdx(),
                                &SystemZ::FP64BitRegClass, TRI);
    if (SystemZ::VR128BitRegClass.contains(Reg))
      TII->loadRegFromStackSlot(MBB, MBBI, Reg, CSI[I].getFrameIdx(),
                                &SystemZ::VR128BitRegClass, TRI);
  }

  // Restore call-saved GPRs (but not call-clobbered varargs, which at
  // this point might hold return values).
  unsigned LowGPR = ZFI->getLowSavedGPR();
  unsigned HighGPR = ZFI->getHighSavedGPR();
  unsigned StartOffset = RegSpillOffsets[LowGPR];
  if (LowGPR) {
    // If we saved any of %r2-%r5 as varargs, we should also be saving
    // and restoring %r6.  If we're saving %r6 or above, we should be
    // restoring it too.
    assert(LowGPR != HighGPR && "Should be loading %r15 and something else");

    // Build an LMG instruction.
    MachineInstrBuilder MIB = BuildMI(MBB, MBBI, DL, TII->get(SystemZ::LMG));

    // Add the explicit register operands.
    MIB.addReg(LowGPR, RegState::Define);
    MIB.addReg(HighGPR, RegState::Define);

    // Add the address.
    MIB.addReg(HasFP ? SystemZ::R11D : SystemZ::R15D);
    MIB.addImm(StartOffset);

    // Do a second scan adding regs as being defined by instruction
    for (unsigned I = 0, E = CSI.size(); I != E; ++I) {
      unsigned Reg = CSI[I].getReg();
      if (Reg != LowGPR && Reg != HighGPR &&
          SystemZ::GR64BitRegClass.contains(Reg))
        MIB.addReg(Reg, RegState::ImplicitDefine);
    }
  }

  return true;
}

void SystemZFrameLowering::
processFunctionBeforeFrameFinalized(MachineFunction &MF,
                                    RegScavenger *RS) const {
  MachineFrameInfo &MFFrame = MF.getFrameInfo();
  // Get the size of our stack frame to be allocated ...
  uint64_t StackSize = (MFFrame.estimateStackSize(MF) +
                        SystemZMC::CallFrameSize);
  // ... and the maximum offset we may need to reach into the
  // caller's frame to access the save area or stack arguments.
  int64_t MaxArgOffset = SystemZMC::CallFrameSize;
  for (int I = MFFrame.getObjectIndexBegin(); I != 0; ++I)
    if (MFFrame.getObjectOffset(I) >= 0) {
      int64_t ArgOffset = SystemZMC::CallFrameSize +
                          MFFrame.getObjectOffset(I) +
                          MFFrame.getObjectSize(I);
      MaxArgOffset = std::max(MaxArgOffset, ArgOffset);
    }

  uint64_t MaxReach = StackSize + MaxArgOffset;
  if (!isUInt<12>(MaxReach)) {
    // We may need register scavenging slots if some parts of the frame
    // are outside the reach of an unsigned 12-bit displacement.
    // Create 2 for the case where both addresses in an MVC are
    // out of range.
    RS->addScavengingFrameIndex(MFFrame.CreateStackObject(8, 8, false));
    RS->addScavengingFrameIndex(MFFrame.CreateStackObject(8, 8, false));
  }
}

// Emit instructions before MBBI (in MBB) to add NumBytes to Reg.
static void emitIncrement(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator &MBBI,
                          const DebugLoc &DL,
                          unsigned Reg, int64_t NumBytes,
                          const TargetInstrInfo *TII) {
  while (NumBytes) {
    unsigned Opcode;
    int64_t ThisVal = NumBytes;
    if (isInt<16>(NumBytes))
      Opcode = SystemZ::AGHI;
    else {
      Opcode = SystemZ::AGFI;
      // Make sure we maintain 8-byte stack alignment.
      int64_t MinVal = -uint64_t(1) << 31;
      int64_t MaxVal = (int64_t(1) << 31) - 8;
      if (ThisVal < MinVal)
        ThisVal = MinVal;
      else if (ThisVal > MaxVal)
        ThisVal = MaxVal;
    }
    MachineInstr *MI = BuildMI(MBB, MBBI, DL, TII->get(Opcode), Reg)
      .addReg(Reg).addImm(ThisVal);
    // The CC implicit def is dead.
    MI->getOperand(3).setIsDead();
    NumBytes -= ThisVal;
  }
}

void SystemZFrameLowering::emitPrologue(MachineFunction &MF,
                                        MachineBasicBlock &MBB) const {
  assert(&MF.front() == &MBB && "Shrink-wrapping not yet supported");
  MachineFrameInfo &MFFrame = MF.getFrameInfo();
  auto *ZII =
      static_cast<const SystemZInstrInfo *>(MF.getSubtarget().getInstrInfo());
  SystemZMachineFunctionInfo *ZFI = MF.getInfo<SystemZMachineFunctionInfo>();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  MachineModuleInfo &MMI = MF.getMMI();
  const MCRegisterInfo *MRI = MMI.getContext().getRegisterInfo();
  const std::vector<CalleeSavedInfo> &CSI = MFFrame.getCalleeSavedInfo();
  bool HasFP = hasFP(MF);

  // Debug location must be unknown since the first debug location is used
  // to determine the end of the prologue.
  DebugLoc DL;

  // The current offset of the stack pointer from the CFA.
  int64_t SPOffsetFromCFA = -SystemZMC::CFAOffsetFromInitialSP;

  if (ZFI->getLowSavedGPR()) {
    // Skip over the GPR saves.
    if (MBBI != MBB.end() && MBBI->getOpcode() == SystemZ::STMG)
      ++MBBI;
    else
      llvm_unreachable("Couldn't skip over GPR saves");

    // Add CFI for the GPR saves.
    for (auto &Save : CSI) {
      unsigned Reg = Save.getReg();
      if (SystemZ::GR64BitRegClass.contains(Reg)) {
        int64_t Offset = SPOffsetFromCFA + RegSpillOffsets[Reg];
        unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
            nullptr, MRI->getDwarfRegNum(Reg, true), Offset));
        BuildMI(MBB, MBBI, DL, ZII->get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(CFIIndex);
      }
    }
  }

  uint64_t StackSize = MFFrame.getStackSize();
  // We need to allocate the ABI-defined 160-byte base area whenever
  // we allocate stack space for our own use and whenever we call another
  // function.
  if (StackSize || MFFrame.hasVarSizedObjects() || MFFrame.hasCalls()) {
    StackSize += SystemZMC::CallFrameSize;
    MFFrame.setStackSize(StackSize);
  }

  if (StackSize) {
    // Determine if we want to store a backchain.
    bool StoreBackchain = MF.getFunction().hasFnAttribute("backchain");

    // If we need backchain, save current stack pointer.  R1 is free at this
    // point.
    if (StoreBackchain)
      BuildMI(MBB, MBBI, DL, ZII->get(SystemZ::LGR))
        .addReg(SystemZ::R1D, RegState::Define).addReg(SystemZ::R15D);

    // Allocate StackSize bytes.
    int64_t Delta = -int64_t(StackSize);
    emitIncrement(MBB, MBBI, DL, SystemZ::R15D, Delta, ZII);

    // Add CFI for the allocation.
    unsigned CFIIndex = MF.addFrameInst(
        MCCFIInstruction::createDefCfaOffset(nullptr, SPOffsetFromCFA + Delta));
    BuildMI(MBB, MBBI, DL, ZII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);
    SPOffsetFromCFA += Delta;

    if (StoreBackchain)
      BuildMI(MBB, MBBI, DL, ZII->get(SystemZ::STG))
        .addReg(SystemZ::R1D, RegState::Kill).addReg(SystemZ::R15D).addImm(0).addReg(0);
  }

  if (HasFP) {
    // Copy the base of the frame to R11.
    BuildMI(MBB, MBBI, DL, ZII->get(SystemZ::LGR), SystemZ::R11D)
      .addReg(SystemZ::R15D);

    // Add CFI for the new frame location.
    unsigned HardFP = MRI->getDwarfRegNum(SystemZ::R11D, true);
    unsigned CFIIndex = MF.addFrameInst(
        MCCFIInstruction::createDefCfaRegister(nullptr, HardFP));
    BuildMI(MBB, MBBI, DL, ZII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);

    // Mark the FramePtr as live at the beginning of every block except
    // the entry block.  (We'll have marked R11 as live on entry when
    // saving the GPRs.)
    for (auto I = std::next(MF.begin()), E = MF.end(); I != E; ++I)
      I->addLiveIn(SystemZ::R11D);
  }

  // Skip over the FPR/VR saves.
  SmallVector<unsigned, 8> CFIIndexes;
  for (auto &Save : CSI) {
    unsigned Reg = Save.getReg();
    if (SystemZ::FP64BitRegClass.contains(Reg)) {
      if (MBBI != MBB.end() &&
          (MBBI->getOpcode() == SystemZ::STD ||
           MBBI->getOpcode() == SystemZ::STDY))
        ++MBBI;
      else
        llvm_unreachable("Couldn't skip over FPR save");
    } else if (SystemZ::VR128BitRegClass.contains(Reg)) {
      if (MBBI != MBB.end() &&
          MBBI->getOpcode() == SystemZ::VST)
        ++MBBI;
      else
        llvm_unreachable("Couldn't skip over VR save");
    } else
      continue;

    // Add CFI for the this save.
    unsigned DwarfReg = MRI->getDwarfRegNum(Reg, true);
    unsigned IgnoredFrameReg;
    int64_t Offset =
        getFrameIndexReference(MF, Save.getFrameIdx(), IgnoredFrameReg);

    unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
          nullptr, DwarfReg, SPOffsetFromCFA + Offset));
    CFIIndexes.push_back(CFIIndex);
  }
  // Complete the CFI for the FPR/VR saves, modelling them as taking effect
  // after the last save.
  for (auto CFIIndex : CFIIndexes) {
    BuildMI(MBB, MBBI, DL, ZII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);
  }
}

void SystemZFrameLowering::emitEpilogue(MachineFunction &MF,
                                        MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  auto *ZII =
      static_cast<const SystemZInstrInfo *>(MF.getSubtarget().getInstrInfo());
  SystemZMachineFunctionInfo *ZFI = MF.getInfo<SystemZMachineFunctionInfo>();
  MachineFrameInfo &MFFrame = MF.getFrameInfo();

  // Skip the return instruction.
  assert(MBBI->isReturn() && "Can only insert epilogue into returning blocks");

  uint64_t StackSize = MFFrame.getStackSize();
  if (ZFI->getLowSavedGPR()) {
    --MBBI;
    unsigned Opcode = MBBI->getOpcode();
    if (Opcode != SystemZ::LMG)
      llvm_unreachable("Expected to see callee-save register restore code");

    unsigned AddrOpNo = 2;
    DebugLoc DL = MBBI->getDebugLoc();
    uint64_t Offset = StackSize + MBBI->getOperand(AddrOpNo + 1).getImm();
    unsigned NewOpcode = ZII->getOpcodeForOffset(Opcode, Offset);

    // If the offset is too large, use the largest stack-aligned offset
    // and add the rest to the base register (the stack or frame pointer).
    if (!NewOpcode) {
      uint64_t NumBytes = Offset - 0x7fff8;
      emitIncrement(MBB, MBBI, DL, MBBI->getOperand(AddrOpNo).getReg(),
                    NumBytes, ZII);
      Offset -= NumBytes;
      NewOpcode = ZII->getOpcodeForOffset(Opcode, Offset);
      assert(NewOpcode && "No restore instruction available");
    }

    MBBI->setDesc(ZII->get(NewOpcode));
    MBBI->getOperand(AddrOpNo + 1).ChangeToImmediate(Offset);
  } else if (StackSize) {
    DebugLoc DL = MBBI->getDebugLoc();
    emitIncrement(MBB, MBBI, DL, SystemZ::R15D, StackSize, ZII);
  }
}

bool SystemZFrameLowering::hasFP(const MachineFunction &MF) const {
  return (MF.getTarget().Options.DisableFramePointerElim(MF) ||
          MF.getFrameInfo().hasVarSizedObjects() ||
          MF.getInfo<SystemZMachineFunctionInfo>()->getManipulatesSP());
}

bool
SystemZFrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  // The ABI requires us to allocate 160 bytes of stack space for the callee,
  // with any outgoing stack arguments being placed above that.  It seems
  // better to make that area a permanent feature of the frame even if
  // we're using a frame pointer.
  return true;
}

MachineBasicBlock::iterator SystemZFrameLowering::
eliminateCallFramePseudoInstr(MachineFunction &MF,
                              MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI) const {
  switch (MI->getOpcode()) {
  case SystemZ::ADJCALLSTACKDOWN:
  case SystemZ::ADJCALLSTACKUP:
    assert(hasReservedCallFrame(MF) &&
           "ADJSTACKDOWN and ADJSTACKUP should be no-ops");
    return MBB.erase(MI);
    break;

  default:
    llvm_unreachable("Unexpected call frame instruction");
  }
}
