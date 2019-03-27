//===-- PPCFrameLowering.cpp - PPC Frame Information ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the PPC implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "PPCFrameLowering.h"
#include "PPCInstrBuilder.h"
#include "PPCInstrInfo.h"
#include "PPCMachineFunctionInfo.h"
#include "PPCSubtarget.h"
#include "PPCTargetMachine.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/IR/Function.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

#define DEBUG_TYPE "framelowering"
STATISTIC(NumNoNeedForFrame, "Number of functions without frames");
STATISTIC(NumPESpillVSR, "Number of spills to vector in prologue");
STATISTIC(NumPEReloadVSR, "Number of reloads from vector in epilogue");

static cl::opt<bool>
EnablePEVectorSpills("ppc-enable-pe-vector-spills",
                     cl::desc("Enable spills in prologue to vector registers."),
                     cl::init(false), cl::Hidden);

/// VRRegNo - Map from a numbered VR register to its enum value.
///
static const MCPhysReg VRRegNo[] = {
 PPC::V0 , PPC::V1 , PPC::V2 , PPC::V3 , PPC::V4 , PPC::V5 , PPC::V6 , PPC::V7 ,
 PPC::V8 , PPC::V9 , PPC::V10, PPC::V11, PPC::V12, PPC::V13, PPC::V14, PPC::V15,
 PPC::V16, PPC::V17, PPC::V18, PPC::V19, PPC::V20, PPC::V21, PPC::V22, PPC::V23,
 PPC::V24, PPC::V25, PPC::V26, PPC::V27, PPC::V28, PPC::V29, PPC::V30, PPC::V31
};

static unsigned computeReturnSaveOffset(const PPCSubtarget &STI) {
  if (STI.isDarwinABI())
    return STI.isPPC64() ? 16 : 8;
  // SVR4 ABI:
  return STI.isPPC64() ? 16 : 4;
}

static unsigned computeTOCSaveOffset(const PPCSubtarget &STI) {
  return STI.isELFv2ABI() ? 24 : 40;
}

static unsigned computeFramePointerSaveOffset(const PPCSubtarget &STI) {
  // For the Darwin ABI:
  // We cannot use the TOC save slot (offset +20) in the PowerPC linkage area
  // for saving the frame pointer (if needed.)  While the published ABI has
  // not used this slot since at least MacOSX 10.2, there is older code
  // around that does use it, and that needs to continue to work.
  if (STI.isDarwinABI())
    return STI.isPPC64() ? -8U : -4U;

  // SVR4 ABI: First slot in the general register save area.
  return STI.isPPC64() ? -8U : -4U;
}

static unsigned computeLinkageSize(const PPCSubtarget &STI) {
  if (STI.isDarwinABI() || STI.isPPC64())
    return (STI.isELFv2ABI() ? 4 : 6) * (STI.isPPC64() ? 8 : 4);

  // SVR4 ABI:
  return 8;
}

static unsigned computeBasePointerSaveOffset(const PPCSubtarget &STI) {
  if (STI.isDarwinABI())
    return STI.isPPC64() ? -16U : -8U;

  // SVR4 ABI: First slot in the general register save area.
  return STI.isPPC64()
             ? -16U
             : STI.getTargetMachine().isPositionIndependent() ? -12U : -8U;
}

PPCFrameLowering::PPCFrameLowering(const PPCSubtarget &STI)
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown,
                          STI.getPlatformStackAlignment(), 0),
      Subtarget(STI), ReturnSaveOffset(computeReturnSaveOffset(Subtarget)),
      TOCSaveOffset(computeTOCSaveOffset(Subtarget)),
      FramePointerSaveOffset(computeFramePointerSaveOffset(Subtarget)),
      LinkageSize(computeLinkageSize(Subtarget)),
      BasePointerSaveOffset(computeBasePointerSaveOffset(STI)) {}

// With the SVR4 ABI, callee-saved registers have fixed offsets on the stack.
const PPCFrameLowering::SpillSlot *PPCFrameLowering::getCalleeSavedSpillSlots(
    unsigned &NumEntries) const {
  if (Subtarget.isDarwinABI()) {
    NumEntries = 1;
    if (Subtarget.isPPC64()) {
      static const SpillSlot darwin64Offsets = {PPC::X31, -8};
      return &darwin64Offsets;
    } else {
      static const SpillSlot darwinOffsets = {PPC::R31, -4};
      return &darwinOffsets;
    }
  }

  // Early exit if not using the SVR4 ABI.
  if (!Subtarget.isSVR4ABI()) {
    NumEntries = 0;
    return nullptr;
  }

  // Note that the offsets here overlap, but this is fixed up in
  // processFunctionBeforeFrameFinalized.

  static const SpillSlot Offsets[] = {
      // Floating-point register save area offsets.
      {PPC::F31, -8},
      {PPC::F30, -16},
      {PPC::F29, -24},
      {PPC::F28, -32},
      {PPC::F27, -40},
      {PPC::F26, -48},
      {PPC::F25, -56},
      {PPC::F24, -64},
      {PPC::F23, -72},
      {PPC::F22, -80},
      {PPC::F21, -88},
      {PPC::F20, -96},
      {PPC::F19, -104},
      {PPC::F18, -112},
      {PPC::F17, -120},
      {PPC::F16, -128},
      {PPC::F15, -136},
      {PPC::F14, -144},

      // General register save area offsets.
      {PPC::R31, -4},
      {PPC::R30, -8},
      {PPC::R29, -12},
      {PPC::R28, -16},
      {PPC::R27, -20},
      {PPC::R26, -24},
      {PPC::R25, -28},
      {PPC::R24, -32},
      {PPC::R23, -36},
      {PPC::R22, -40},
      {PPC::R21, -44},
      {PPC::R20, -48},
      {PPC::R19, -52},
      {PPC::R18, -56},
      {PPC::R17, -60},
      {PPC::R16, -64},
      {PPC::R15, -68},
      {PPC::R14, -72},

      // CR save area offset.  We map each of the nonvolatile CR fields
      // to the slot for CR2, which is the first of the nonvolatile CR
      // fields to be assigned, so that we only allocate one save slot.
      // See PPCRegisterInfo::hasReservedSpillSlot() for more information.
      {PPC::CR2, -4},

      // VRSAVE save area offset.
      {PPC::VRSAVE, -4},

      // Vector register save area
      {PPC::V31, -16},
      {PPC::V30, -32},
      {PPC::V29, -48},
      {PPC::V28, -64},
      {PPC::V27, -80},
      {PPC::V26, -96},
      {PPC::V25, -112},
      {PPC::V24, -128},
      {PPC::V23, -144},
      {PPC::V22, -160},
      {PPC::V21, -176},
      {PPC::V20, -192},

      // SPE register save area (overlaps Vector save area).
      {PPC::S31, -8},
      {PPC::S30, -16},
      {PPC::S29, -24},
      {PPC::S28, -32},
      {PPC::S27, -40},
      {PPC::S26, -48},
      {PPC::S25, -56},
      {PPC::S24, -64},
      {PPC::S23, -72},
      {PPC::S22, -80},
      {PPC::S21, -88},
      {PPC::S20, -96},
      {PPC::S19, -104},
      {PPC::S18, -112},
      {PPC::S17, -120},
      {PPC::S16, -128},
      {PPC::S15, -136},
      {PPC::S14, -144}};

  static const SpillSlot Offsets64[] = {
      // Floating-point register save area offsets.
      {PPC::F31, -8},
      {PPC::F30, -16},
      {PPC::F29, -24},
      {PPC::F28, -32},
      {PPC::F27, -40},
      {PPC::F26, -48},
      {PPC::F25, -56},
      {PPC::F24, -64},
      {PPC::F23, -72},
      {PPC::F22, -80},
      {PPC::F21, -88},
      {PPC::F20, -96},
      {PPC::F19, -104},
      {PPC::F18, -112},
      {PPC::F17, -120},
      {PPC::F16, -128},
      {PPC::F15, -136},
      {PPC::F14, -144},

      // General register save area offsets.
      {PPC::X31, -8},
      {PPC::X30, -16},
      {PPC::X29, -24},
      {PPC::X28, -32},
      {PPC::X27, -40},
      {PPC::X26, -48},
      {PPC::X25, -56},
      {PPC::X24, -64},
      {PPC::X23, -72},
      {PPC::X22, -80},
      {PPC::X21, -88},
      {PPC::X20, -96},
      {PPC::X19, -104},
      {PPC::X18, -112},
      {PPC::X17, -120},
      {PPC::X16, -128},
      {PPC::X15, -136},
      {PPC::X14, -144},

      // VRSAVE save area offset.
      {PPC::VRSAVE, -4},

      // Vector register save area
      {PPC::V31, -16},
      {PPC::V30, -32},
      {PPC::V29, -48},
      {PPC::V28, -64},
      {PPC::V27, -80},
      {PPC::V26, -96},
      {PPC::V25, -112},
      {PPC::V24, -128},
      {PPC::V23, -144},
      {PPC::V22, -160},
      {PPC::V21, -176},
      {PPC::V20, -192}};

  if (Subtarget.isPPC64()) {
    NumEntries = array_lengthof(Offsets64);

    return Offsets64;
  } else {
    NumEntries = array_lengthof(Offsets);

    return Offsets;
  }
}

/// RemoveVRSaveCode - We have found that this function does not need any code
/// to manipulate the VRSAVE register, even though it uses vector registers.
/// This can happen when the only registers used are known to be live in or out
/// of the function.  Remove all of the VRSAVE related code from the function.
/// FIXME: The removal of the code results in a compile failure at -O0 when the
/// function contains a function call, as the GPR containing original VRSAVE
/// contents is spilled and reloaded around the call.  Without the prolog code,
/// the spill instruction refers to an undefined register.  This code needs
/// to account for all uses of that GPR.
static void RemoveVRSaveCode(MachineInstr &MI) {
  MachineBasicBlock *Entry = MI.getParent();
  MachineFunction *MF = Entry->getParent();

  // We know that the MTVRSAVE instruction immediately follows MI.  Remove it.
  MachineBasicBlock::iterator MBBI = MI;
  ++MBBI;
  assert(MBBI != Entry->end() && MBBI->getOpcode() == PPC::MTVRSAVE);
  MBBI->eraseFromParent();

  bool RemovedAllMTVRSAVEs = true;
  // See if we can find and remove the MTVRSAVE instruction from all of the
  // epilog blocks.
  for (MachineFunction::iterator I = MF->begin(), E = MF->end(); I != E; ++I) {
    // If last instruction is a return instruction, add an epilogue
    if (I->isReturnBlock()) {
      bool FoundIt = false;
      for (MBBI = I->end(); MBBI != I->begin(); ) {
        --MBBI;
        if (MBBI->getOpcode() == PPC::MTVRSAVE) {
          MBBI->eraseFromParent();  // remove it.
          FoundIt = true;
          break;
        }
      }
      RemovedAllMTVRSAVEs &= FoundIt;
    }
  }

  // If we found and removed all MTVRSAVE instructions, remove the read of
  // VRSAVE as well.
  if (RemovedAllMTVRSAVEs) {
    MBBI = MI;
    assert(MBBI != Entry->begin() && "UPDATE_VRSAVE is first instr in block?");
    --MBBI;
    assert(MBBI->getOpcode() == PPC::MFVRSAVE && "VRSAVE instrs wandered?");
    MBBI->eraseFromParent();
  }

  // Finally, nuke the UPDATE_VRSAVE.
  MI.eraseFromParent();
}

// HandleVRSaveUpdate - MI is the UPDATE_VRSAVE instruction introduced by the
// instruction selector.  Based on the vector registers that have been used,
// transform this into the appropriate ORI instruction.
static void HandleVRSaveUpdate(MachineInstr &MI, const TargetInstrInfo &TII) {
  MachineFunction *MF = MI.getParent()->getParent();
  const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
  DebugLoc dl = MI.getDebugLoc();

  const MachineRegisterInfo &MRI = MF->getRegInfo();
  unsigned UsedRegMask = 0;
  for (unsigned i = 0; i != 32; ++i)
    if (MRI.isPhysRegModified(VRRegNo[i]))
      UsedRegMask |= 1 << (31-i);

  // Live in and live out values already must be in the mask, so don't bother
  // marking them.
  for (std::pair<unsigned, unsigned> LI : MF->getRegInfo().liveins()) {
    unsigned RegNo = TRI->getEncodingValue(LI.first);
    if (VRRegNo[RegNo] == LI.first)        // If this really is a vector reg.
      UsedRegMask &= ~(1 << (31-RegNo));   // Doesn't need to be marked.
  }

  // Live out registers appear as use operands on return instructions.
  for (MachineFunction::const_iterator BI = MF->begin(), BE = MF->end();
       UsedRegMask != 0 && BI != BE; ++BI) {
    const MachineBasicBlock &MBB = *BI;
    if (!MBB.isReturnBlock())
      continue;
    const MachineInstr &Ret = MBB.back();
    for (unsigned I = 0, E = Ret.getNumOperands(); I != E; ++I) {
      const MachineOperand &MO = Ret.getOperand(I);
      if (!MO.isReg() || !PPC::VRRCRegClass.contains(MO.getReg()))
        continue;
      unsigned RegNo = TRI->getEncodingValue(MO.getReg());
      UsedRegMask &= ~(1 << (31-RegNo));
    }
  }

  // If no registers are used, turn this into a copy.
  if (UsedRegMask == 0) {
    // Remove all VRSAVE code.
    RemoveVRSaveCode(MI);
    return;
  }

  unsigned SrcReg = MI.getOperand(1).getReg();
  unsigned DstReg = MI.getOperand(0).getReg();

  if ((UsedRegMask & 0xFFFF) == UsedRegMask) {
    if (DstReg != SrcReg)
      BuildMI(*MI.getParent(), MI, dl, TII.get(PPC::ORI), DstReg)
          .addReg(SrcReg)
          .addImm(UsedRegMask);
    else
      BuildMI(*MI.getParent(), MI, dl, TII.get(PPC::ORI), DstReg)
          .addReg(SrcReg, RegState::Kill)
          .addImm(UsedRegMask);
  } else if ((UsedRegMask & 0xFFFF0000) == UsedRegMask) {
    if (DstReg != SrcReg)
      BuildMI(*MI.getParent(), MI, dl, TII.get(PPC::ORIS), DstReg)
          .addReg(SrcReg)
          .addImm(UsedRegMask >> 16);
    else
      BuildMI(*MI.getParent(), MI, dl, TII.get(PPC::ORIS), DstReg)
          .addReg(SrcReg, RegState::Kill)
          .addImm(UsedRegMask >> 16);
  } else {
    if (DstReg != SrcReg)
      BuildMI(*MI.getParent(), MI, dl, TII.get(PPC::ORIS), DstReg)
          .addReg(SrcReg)
          .addImm(UsedRegMask >> 16);
    else
      BuildMI(*MI.getParent(), MI, dl, TII.get(PPC::ORIS), DstReg)
          .addReg(SrcReg, RegState::Kill)
          .addImm(UsedRegMask >> 16);

    BuildMI(*MI.getParent(), MI, dl, TII.get(PPC::ORI), DstReg)
        .addReg(DstReg, RegState::Kill)
        .addImm(UsedRegMask & 0xFFFF);
  }

  // Remove the old UPDATE_VRSAVE instruction.
  MI.eraseFromParent();
}

static bool spillsCR(const MachineFunction &MF) {
  const PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  return FuncInfo->isCRSpilled();
}

static bool spillsVRSAVE(const MachineFunction &MF) {
  const PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  return FuncInfo->isVRSAVESpilled();
}

static bool hasSpills(const MachineFunction &MF) {
  const PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  return FuncInfo->hasSpills();
}

static bool hasNonRISpills(const MachineFunction &MF) {
  const PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  return FuncInfo->hasNonRISpills();
}

/// MustSaveLR - Return true if this function requires that we save the LR
/// register onto the stack in the prolog and restore it in the epilog of the
/// function.
static bool MustSaveLR(const MachineFunction &MF, unsigned LR) {
  const PPCFunctionInfo *MFI = MF.getInfo<PPCFunctionInfo>();

  // We need a save/restore of LR if there is any def of LR (which is
  // defined by calls, including the PIC setup sequence), or if there is
  // some use of the LR stack slot (e.g. for builtin_return_address).
  // (LR comes in 32 and 64 bit versions.)
  MachineRegisterInfo::def_iterator RI = MF.getRegInfo().def_begin(LR);
  return RI !=MF.getRegInfo().def_end() || MFI->isLRStoreRequired();
}

/// determineFrameLayout - Determine the size of the frame and maximum call
/// frame size.
unsigned PPCFrameLowering::determineFrameLayout(MachineFunction &MF,
                                                bool UpdateMF,
                                                bool UseEstimate) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();

  // Get the number of bytes to allocate from the FrameInfo
  unsigned FrameSize =
    UseEstimate ? MFI.estimateStackSize(MF) : MFI.getStackSize();

  // Get stack alignments. The frame must be aligned to the greatest of these:
  unsigned TargetAlign = getStackAlignment(); // alignment required per the ABI
  unsigned MaxAlign = MFI.getMaxAlignment(); // algmt required by data in frame
  unsigned AlignMask = std::max(MaxAlign, TargetAlign) - 1;

  const PPCRegisterInfo *RegInfo = Subtarget.getRegisterInfo();

  unsigned LR = RegInfo->getRARegister();
  bool DisableRedZone = MF.getFunction().hasFnAttribute(Attribute::NoRedZone);
  bool CanUseRedZone = !MFI.hasVarSizedObjects() && // No dynamic alloca.
                       !MFI.adjustsStack() &&       // No calls.
                       !MustSaveLR(MF, LR) &&       // No need to save LR.
                       !RegInfo->hasBasePointer(MF); // No special alignment.

  // Note: for PPC32 SVR4ABI (Non-DarwinABI), we can still generate stackless
  // code if all local vars are reg-allocated.
  bool FitsInRedZone = FrameSize <= Subtarget.getRedZoneSize();

  // Check whether we can skip adjusting the stack pointer (by using red zone)
  if (!DisableRedZone && CanUseRedZone && FitsInRedZone) {
    NumNoNeedForFrame++;
    // No need for frame
    if (UpdateMF)
      MFI.setStackSize(0);
    return 0;
  }

  // Get the maximum call frame size of all the calls.
  unsigned maxCallFrameSize = MFI.getMaxCallFrameSize();

  // Maximum call frame needs to be at least big enough for linkage area.
  unsigned minCallFrameSize = getLinkageSize();
  maxCallFrameSize = std::max(maxCallFrameSize, minCallFrameSize);

  // If we have dynamic alloca then maxCallFrameSize needs to be aligned so
  // that allocations will be aligned.
  if (MFI.hasVarSizedObjects())
    maxCallFrameSize = (maxCallFrameSize + AlignMask) & ~AlignMask;

  // Update maximum call frame size.
  if (UpdateMF)
    MFI.setMaxCallFrameSize(maxCallFrameSize);

  // Include call frame size in total.
  FrameSize += maxCallFrameSize;

  // Make sure the frame is aligned.
  FrameSize = (FrameSize + AlignMask) & ~AlignMask;

  // Update frame info.
  if (UpdateMF)
    MFI.setStackSize(FrameSize);

  return FrameSize;
}

// hasFP - Return true if the specified function actually has a dedicated frame
// pointer register.
bool PPCFrameLowering::hasFP(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  // FIXME: This is pretty much broken by design: hasFP() might be called really
  // early, before the stack layout was calculated and thus hasFP() might return
  // true or false here depending on the time of call.
  return (MFI.getStackSize()) && needsFP(MF);
}

// needsFP - Return true if the specified function should have a dedicated frame
// pointer register.  This is true if the function has variable sized allocas or
// if frame pointer elimination is disabled.
bool PPCFrameLowering::needsFP(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  // Naked functions have no stack frame pushed, so we don't have a frame
  // pointer.
  if (MF.getFunction().hasFnAttribute(Attribute::Naked))
    return false;

  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
    MFI.hasVarSizedObjects() || MFI.hasStackMap() || MFI.hasPatchPoint() ||
    (MF.getTarget().Options.GuaranteedTailCallOpt &&
     MF.getInfo<PPCFunctionInfo>()->hasFastCall());
}

void PPCFrameLowering::replaceFPWithRealFP(MachineFunction &MF) const {
  bool is31 = needsFP(MF);
  unsigned FPReg  = is31 ? PPC::R31 : PPC::R1;
  unsigned FP8Reg = is31 ? PPC::X31 : PPC::X1;

  const PPCRegisterInfo *RegInfo = Subtarget.getRegisterInfo();
  bool HasBP = RegInfo->hasBasePointer(MF);
  unsigned BPReg  = HasBP ? (unsigned) RegInfo->getBaseRegister(MF) : FPReg;
  unsigned BP8Reg = HasBP ? (unsigned) PPC::X30 : FP8Reg;

  for (MachineFunction::iterator BI = MF.begin(), BE = MF.end();
       BI != BE; ++BI)
    for (MachineBasicBlock::iterator MBBI = BI->end(); MBBI != BI->begin(); ) {
      --MBBI;
      for (unsigned I = 0, E = MBBI->getNumOperands(); I != E; ++I) {
        MachineOperand &MO = MBBI->getOperand(I);
        if (!MO.isReg())
          continue;

        switch (MO.getReg()) {
        case PPC::FP:
          MO.setReg(FPReg);
          break;
        case PPC::FP8:
          MO.setReg(FP8Reg);
          break;
        case PPC::BP:
          MO.setReg(BPReg);
          break;
        case PPC::BP8:
          MO.setReg(BP8Reg);
          break;

        }
      }
    }
}

/*  This function will do the following:
    - If MBB is an entry or exit block, set SR1 and SR2 to R0 and R12
      respectively (defaults recommended by the ABI) and return true
    - If MBB is not an entry block, initialize the register scavenger and look
      for available registers.
    - If the defaults (R0/R12) are available, return true
    - If TwoUniqueRegsRequired is set to true, it looks for two unique
      registers. Otherwise, look for a single available register.
      - If the required registers are found, set SR1 and SR2 and return true.
      - If the required registers are not found, set SR2 or both SR1 and SR2 to
        PPC::NoRegister and return false.

    Note that if both SR1 and SR2 are valid parameters and TwoUniqueRegsRequired
    is not set, this function will attempt to find two different registers, but
    still return true if only one register is available (and set SR1 == SR2).
*/
bool
PPCFrameLowering::findScratchRegister(MachineBasicBlock *MBB,
                                      bool UseAtEnd,
                                      bool TwoUniqueRegsRequired,
                                      unsigned *SR1,
                                      unsigned *SR2) const {
  RegScavenger RS;
  unsigned R0 =  Subtarget.isPPC64() ? PPC::X0 : PPC::R0;
  unsigned R12 = Subtarget.isPPC64() ? PPC::X12 : PPC::R12;

  // Set the defaults for the two scratch registers.
  if (SR1)
    *SR1 = R0;

  if (SR2) {
    assert (SR1 && "Asking for the second scratch register but not the first?");
    *SR2 = R12;
  }

  // If MBB is an entry or exit block, use R0 and R12 as the scratch registers.
  if ((UseAtEnd && MBB->isReturnBlock()) ||
      (!UseAtEnd && (&MBB->getParent()->front() == MBB)))
    return true;

  RS.enterBasicBlock(*MBB);

  if (UseAtEnd && !MBB->empty()) {
    // The scratch register will be used at the end of the block, so must
    // consider all registers used within the block

    MachineBasicBlock::iterator MBBI = MBB->getFirstTerminator();
    // If no terminator, back iterator up to previous instruction.
    if (MBBI == MBB->end())
      MBBI = std::prev(MBBI);

    if (MBBI != MBB->begin())
      RS.forward(MBBI);
  }

  // If the two registers are available, we're all good.
  // Note that we only return here if both R0 and R12 are available because
  // although the function may not require two unique registers, it may benefit
  // from having two so we should try to provide them.
  if (!RS.isRegUsed(R0) && !RS.isRegUsed(R12))
    return true;

  // Get the list of callee-saved registers for the target.
  const PPCRegisterInfo *RegInfo = Subtarget.getRegisterInfo();
  const MCPhysReg *CSRegs = RegInfo->getCalleeSavedRegs(MBB->getParent());

  // Get all the available registers in the block.
  BitVector BV = RS.getRegsAvailable(Subtarget.isPPC64() ? &PPC::G8RCRegClass :
                                     &PPC::GPRCRegClass);

  // We shouldn't use callee-saved registers as scratch registers as they may be
  // available when looking for a candidate block for shrink wrapping but not
  // available when the actual prologue/epilogue is being emitted because they
  // were added as live-in to the prologue block by PrologueEpilogueInserter.
  for (int i = 0; CSRegs[i]; ++i)
    BV.reset(CSRegs[i]);

  // Set the first scratch register to the first available one.
  if (SR1) {
    int FirstScratchReg = BV.find_first();
    *SR1 = FirstScratchReg == -1 ? (unsigned)PPC::NoRegister : FirstScratchReg;
  }

  // If there is another one available, set the second scratch register to that.
  // Otherwise, set it to either PPC::NoRegister if this function requires two
  // or to whatever SR1 is set to if this function doesn't require two.
  if (SR2) {
    int SecondScratchReg = BV.find_next(*SR1);
    if (SecondScratchReg != -1)
      *SR2 = SecondScratchReg;
    else
      *SR2 = TwoUniqueRegsRequired ? (unsigned)PPC::NoRegister : *SR1;
  }

  // Now that we've done our best to provide both registers, double check
  // whether we were unable to provide enough.
  if (BV.count() < (TwoUniqueRegsRequired ? 2U : 1U))
    return false;

  return true;
}

// We need a scratch register for spilling LR and for spilling CR. By default,
// we use two scratch registers to hide latency. However, if only one scratch
// register is available, we can adjust for that by not overlapping the spill
// code. However, if we need to realign the stack (i.e. have a base pointer)
// and the stack frame is large, we need two scratch registers.
bool
PPCFrameLowering::twoUniqueScratchRegsRequired(MachineBasicBlock *MBB) const {
  const PPCRegisterInfo *RegInfo = Subtarget.getRegisterInfo();
  MachineFunction &MF = *(MBB->getParent());
  bool HasBP = RegInfo->hasBasePointer(MF);
  unsigned FrameSize = determineFrameLayout(MF, false);
  int NegFrameSize = -FrameSize;
  bool IsLargeFrame = !isInt<16>(NegFrameSize);
  MachineFrameInfo &MFI = MF.getFrameInfo();
  unsigned MaxAlign = MFI.getMaxAlignment();
  bool HasRedZone = Subtarget.isPPC64() || !Subtarget.isSVR4ABI();

  return (IsLargeFrame || !HasRedZone) && HasBP && MaxAlign > 1;
}

bool PPCFrameLowering::canUseAsPrologue(const MachineBasicBlock &MBB) const {
  MachineBasicBlock *TmpMBB = const_cast<MachineBasicBlock *>(&MBB);

  return findScratchRegister(TmpMBB, false,
                             twoUniqueScratchRegsRequired(TmpMBB));
}

bool PPCFrameLowering::canUseAsEpilogue(const MachineBasicBlock &MBB) const {
  MachineBasicBlock *TmpMBB = const_cast<MachineBasicBlock *>(&MBB);

  return findScratchRegister(TmpMBB, true);
}

void PPCFrameLowering::emitPrologue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.begin();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const PPCInstrInfo &TII = *Subtarget.getInstrInfo();
  const PPCRegisterInfo *RegInfo = Subtarget.getRegisterInfo();

  MachineModuleInfo &MMI = MF.getMMI();
  const MCRegisterInfo *MRI = MMI.getContext().getRegisterInfo();
  DebugLoc dl;
  bool needsCFI = MMI.hasDebugInfo() ||
    MF.getFunction().needsUnwindTableEntry();

  // Get processor type.
  bool isPPC64 = Subtarget.isPPC64();
  // Get the ABI.
  bool isSVR4ABI = Subtarget.isSVR4ABI();
  bool isELFv2ABI = Subtarget.isELFv2ABI();
  assert((Subtarget.isDarwinABI() || isSVR4ABI) &&
         "Currently only Darwin and SVR4 ABIs are supported for PowerPC.");

  // Scan the prolog, looking for an UPDATE_VRSAVE instruction.  If we find it,
  // process it.
  if (!isSVR4ABI)
    for (unsigned i = 0; MBBI != MBB.end(); ++i, ++MBBI) {
      if (MBBI->getOpcode() == PPC::UPDATE_VRSAVE) {
        HandleVRSaveUpdate(*MBBI, TII);
        break;
      }
    }

  // Move MBBI back to the beginning of the prologue block.
  MBBI = MBB.begin();

  // Work out frame sizes.
  unsigned FrameSize = determineFrameLayout(MF);
  int NegFrameSize = -FrameSize;
  if (!isInt<32>(NegFrameSize))
    llvm_unreachable("Unhandled stack size!");

  if (MFI.isFrameAddressTaken())
    replaceFPWithRealFP(MF);

  // Check if the link register (LR) must be saved.
  PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();
  bool MustSaveLR = FI->mustSaveLR();
  const SmallVectorImpl<unsigned> &MustSaveCRs = FI->getMustSaveCRs();
  bool MustSaveCR = !MustSaveCRs.empty();
  // Do we have a frame pointer and/or base pointer for this function?
  bool HasFP = hasFP(MF);
  bool HasBP = RegInfo->hasBasePointer(MF);
  bool HasRedZone = isPPC64 || !isSVR4ABI;

  unsigned SPReg       = isPPC64 ? PPC::X1  : PPC::R1;
  unsigned BPReg       = RegInfo->getBaseRegister(MF);
  unsigned FPReg       = isPPC64 ? PPC::X31 : PPC::R31;
  unsigned LRReg       = isPPC64 ? PPC::LR8 : PPC::LR;
  unsigned ScratchReg  = 0;
  unsigned TempReg     = isPPC64 ? PPC::X12 : PPC::R12; // another scratch reg
  //  ...(R12/X12 is volatile in both Darwin & SVR4, & can't be a function arg.)
  const MCInstrDesc& MFLRInst = TII.get(isPPC64 ? PPC::MFLR8
                                                : PPC::MFLR );
  const MCInstrDesc& StoreInst = TII.get(isPPC64 ? PPC::STD
                                                 : PPC::STW );
  const MCInstrDesc& StoreUpdtInst = TII.get(isPPC64 ? PPC::STDU
                                                     : PPC::STWU );
  const MCInstrDesc& StoreUpdtIdxInst = TII.get(isPPC64 ? PPC::STDUX
                                                        : PPC::STWUX);
  const MCInstrDesc& LoadImmShiftedInst = TII.get(isPPC64 ? PPC::LIS8
                                                          : PPC::LIS );
  const MCInstrDesc& OrImmInst = TII.get(isPPC64 ? PPC::ORI8
                                                 : PPC::ORI );
  const MCInstrDesc& OrInst = TII.get(isPPC64 ? PPC::OR8
                                              : PPC::OR );
  const MCInstrDesc& SubtractCarryingInst = TII.get(isPPC64 ? PPC::SUBFC8
                                                            : PPC::SUBFC);
  const MCInstrDesc& SubtractImmCarryingInst = TII.get(isPPC64 ? PPC::SUBFIC8
                                                               : PPC::SUBFIC);

  // Regarding this assert: Even though LR is saved in the caller's frame (i.e.,
  // LROffset is positive), that slot is callee-owned. Because PPC32 SVR4 has no
  // Red Zone, an asynchronous event (a form of "callee") could claim a frame &
  // overwrite it, so PPC32 SVR4 must claim at least a minimal frame to save LR.
  assert((isPPC64 || !isSVR4ABI || !(!FrameSize && (MustSaveLR || HasFP))) &&
         "FrameSize must be >0 to save/restore the FP or LR for 32-bit SVR4.");

  // Using the same bool variable as below to suppress compiler warnings.
  bool SingleScratchReg =
    findScratchRegister(&MBB, false, twoUniqueScratchRegsRequired(&MBB),
                        &ScratchReg, &TempReg);
  assert(SingleScratchReg &&
         "Required number of registers not available in this block");

  SingleScratchReg = ScratchReg == TempReg;

  int LROffset = getReturnSaveOffset();

  int FPOffset = 0;
  if (HasFP) {
    if (isSVR4ABI) {
      MachineFrameInfo &MFI = MF.getFrameInfo();
      int FPIndex = FI->getFramePointerSaveIndex();
      assert(FPIndex && "No Frame Pointer Save Slot!");
      FPOffset = MFI.getObjectOffset(FPIndex);
    } else {
      FPOffset = getFramePointerSaveOffset();
    }
  }

  int BPOffset = 0;
  if (HasBP) {
    if (isSVR4ABI) {
      MachineFrameInfo &MFI = MF.getFrameInfo();
      int BPIndex = FI->getBasePointerSaveIndex();
      assert(BPIndex && "No Base Pointer Save Slot!");
      BPOffset = MFI.getObjectOffset(BPIndex);
    } else {
      BPOffset = getBasePointerSaveOffset();
    }
  }

  int PBPOffset = 0;
  if (FI->usesPICBase()) {
    MachineFrameInfo &MFI = MF.getFrameInfo();
    int PBPIndex = FI->getPICBasePointerSaveIndex();
    assert(PBPIndex && "No PIC Base Pointer Save Slot!");
    PBPOffset = MFI.getObjectOffset(PBPIndex);
  }

  // Get stack alignments.
  unsigned MaxAlign = MFI.getMaxAlignment();
  if (HasBP && MaxAlign > 1)
    assert(isPowerOf2_32(MaxAlign) && isInt<16>(MaxAlign) &&
           "Invalid alignment!");

  // Frames of 32KB & larger require special handling because they cannot be
  // indexed into with a simple STDU/STWU/STD/STW immediate offset operand.
  bool isLargeFrame = !isInt<16>(NegFrameSize);

  assert((isPPC64 || !MustSaveCR) &&
         "Prologue CR saving supported only in 64-bit mode");

  // If we need to spill the CR and the LR but we don't have two separate
  // registers available, we must spill them one at a time
  if (MustSaveCR && SingleScratchReg && MustSaveLR) {
    // In the ELFv2 ABI, we are not required to save all CR fields.
    // If only one or two CR fields are clobbered, it is more efficient to use
    // mfocrf to selectively save just those fields, because mfocrf has short
    // latency compares to mfcr.
    unsigned MfcrOpcode = PPC::MFCR8;
    unsigned CrState = RegState::ImplicitKill;
    if (isELFv2ABI && MustSaveCRs.size() == 1) {
      MfcrOpcode = PPC::MFOCRF8;
      CrState = RegState::Kill;
    }
    MachineInstrBuilder MIB =
      BuildMI(MBB, MBBI, dl, TII.get(MfcrOpcode), TempReg);
    for (unsigned i = 0, e = MustSaveCRs.size(); i != e; ++i)
      MIB.addReg(MustSaveCRs[i], CrState);
    BuildMI(MBB, MBBI, dl, TII.get(PPC::STW8))
      .addReg(TempReg, getKillRegState(true))
      .addImm(8)
      .addReg(SPReg);
  }

  if (MustSaveLR)
    BuildMI(MBB, MBBI, dl, MFLRInst, ScratchReg);

  if (MustSaveCR &&
      !(SingleScratchReg && MustSaveLR)) { // will only occur for PPC64
    // In the ELFv2 ABI, we are not required to save all CR fields.
    // If only one or two CR fields are clobbered, it is more efficient to use
    // mfocrf to selectively save just those fields, because mfocrf has short
    // latency compares to mfcr.
    unsigned MfcrOpcode = PPC::MFCR8;
    unsigned CrState = RegState::ImplicitKill;
    if (isELFv2ABI && MustSaveCRs.size() == 1) {
      MfcrOpcode = PPC::MFOCRF8;
      CrState = RegState::Kill;
    }
    MachineInstrBuilder MIB =
      BuildMI(MBB, MBBI, dl, TII.get(MfcrOpcode), TempReg);
    for (unsigned i = 0, e = MustSaveCRs.size(); i != e; ++i)
      MIB.addReg(MustSaveCRs[i], CrState);
  }

  if (HasRedZone) {
    if (HasFP)
      BuildMI(MBB, MBBI, dl, StoreInst)
        .addReg(FPReg)
        .addImm(FPOffset)
        .addReg(SPReg);
    if (FI->usesPICBase())
      BuildMI(MBB, MBBI, dl, StoreInst)
        .addReg(PPC::R30)
        .addImm(PBPOffset)
        .addReg(SPReg);
    if (HasBP)
      BuildMI(MBB, MBBI, dl, StoreInst)
        .addReg(BPReg)
        .addImm(BPOffset)
        .addReg(SPReg);
  }

  if (MustSaveLR)
    BuildMI(MBB, MBBI, dl, StoreInst)
      .addReg(ScratchReg, getKillRegState(true))
      .addImm(LROffset)
      .addReg(SPReg);

  if (MustSaveCR &&
      !(SingleScratchReg && MustSaveLR)) { // will only occur for PPC64
    assert(HasRedZone && "A red zone is always available on PPC64");
    BuildMI(MBB, MBBI, dl, TII.get(PPC::STW8))
      .addReg(TempReg, getKillRegState(true))
      .addImm(8)
      .addReg(SPReg);
  }

  // Skip the rest if this is a leaf function & all spills fit in the Red Zone.
  if (!FrameSize)
    return;

  // Adjust stack pointer: r1 += NegFrameSize.
  // If there is a preferred stack alignment, align R1 now

  if (HasBP && HasRedZone) {
    // Save a copy of r1 as the base pointer.
    BuildMI(MBB, MBBI, dl, OrInst, BPReg)
      .addReg(SPReg)
      .addReg(SPReg);
  }

  // Have we generated a STUX instruction to claim stack frame? If so,
  // the negated frame size will be placed in ScratchReg.
  bool HasSTUX = false;

  // This condition must be kept in sync with canUseAsPrologue.
  if (HasBP && MaxAlign > 1) {
    if (isPPC64)
      BuildMI(MBB, MBBI, dl, TII.get(PPC::RLDICL), ScratchReg)
        .addReg(SPReg)
        .addImm(0)
        .addImm(64 - Log2_32(MaxAlign));
    else // PPC32...
      BuildMI(MBB, MBBI, dl, TII.get(PPC::RLWINM), ScratchReg)
        .addReg(SPReg)
        .addImm(0)
        .addImm(32 - Log2_32(MaxAlign))
        .addImm(31);
    if (!isLargeFrame) {
      BuildMI(MBB, MBBI, dl, SubtractImmCarryingInst, ScratchReg)
        .addReg(ScratchReg, RegState::Kill)
        .addImm(NegFrameSize);
    } else {
      assert(!SingleScratchReg && "Only a single scratch reg available");
      BuildMI(MBB, MBBI, dl, LoadImmShiftedInst, TempReg)
        .addImm(NegFrameSize >> 16);
      BuildMI(MBB, MBBI, dl, OrImmInst, TempReg)
        .addReg(TempReg, RegState::Kill)
        .addImm(NegFrameSize & 0xFFFF);
      BuildMI(MBB, MBBI, dl, SubtractCarryingInst, ScratchReg)
        .addReg(ScratchReg, RegState::Kill)
        .addReg(TempReg, RegState::Kill);
    }

    BuildMI(MBB, MBBI, dl, StoreUpdtIdxInst, SPReg)
      .addReg(SPReg, RegState::Kill)
      .addReg(SPReg)
      .addReg(ScratchReg);
    HasSTUX = true;

  } else if (!isLargeFrame) {
    BuildMI(MBB, MBBI, dl, StoreUpdtInst, SPReg)
      .addReg(SPReg)
      .addImm(NegFrameSize)
      .addReg(SPReg);

  } else {
    BuildMI(MBB, MBBI, dl, LoadImmShiftedInst, ScratchReg)
      .addImm(NegFrameSize >> 16);
    BuildMI(MBB, MBBI, dl, OrImmInst, ScratchReg)
      .addReg(ScratchReg, RegState::Kill)
      .addImm(NegFrameSize & 0xFFFF);
    BuildMI(MBB, MBBI, dl, StoreUpdtIdxInst, SPReg)
      .addReg(SPReg, RegState::Kill)
      .addReg(SPReg)
      .addReg(ScratchReg);
    HasSTUX = true;
  }

  if (!HasRedZone) {
    assert(!isPPC64 && "A red zone is always available on PPC64");
    if (HasSTUX) {
      // The negated frame size is in ScratchReg, and the SPReg has been
      // decremented by the frame size: SPReg = old SPReg + ScratchReg.
      // Since FPOffset, PBPOffset, etc. are relative to the beginning of
      // the stack frame (i.e. the old SP), ideally, we would put the old
      // SP into a register and use it as the base for the stores. The
      // problem is that the only available register may be ScratchReg,
      // which could be R0, and R0 cannot be used as a base address.

      // First, set ScratchReg to the old SP. This may need to be modified
      // later.
      BuildMI(MBB, MBBI, dl, TII.get(PPC::SUBF), ScratchReg)
        .addReg(ScratchReg, RegState::Kill)
        .addReg(SPReg);

      if (ScratchReg == PPC::R0) {
        // R0 cannot be used as a base register, but it can be used as an
        // index in a store-indexed.
        int LastOffset = 0;
        if (HasFP)  {
          // R0 += (FPOffset-LastOffset).
          // Need addic, since addi treats R0 as 0.
          BuildMI(MBB, MBBI, dl, TII.get(PPC::ADDIC), ScratchReg)
            .addReg(ScratchReg)
            .addImm(FPOffset-LastOffset);
          LastOffset = FPOffset;
          // Store FP into *R0.
          BuildMI(MBB, MBBI, dl, TII.get(PPC::STWX))
            .addReg(FPReg, RegState::Kill)  // Save FP.
            .addReg(PPC::ZERO)
            .addReg(ScratchReg);  // This will be the index (R0 is ok here).
        }
        if (FI->usesPICBase()) {
          // R0 += (PBPOffset-LastOffset).
          BuildMI(MBB, MBBI, dl, TII.get(PPC::ADDIC), ScratchReg)
            .addReg(ScratchReg)
            .addImm(PBPOffset-LastOffset);
          LastOffset = PBPOffset;
          BuildMI(MBB, MBBI, dl, TII.get(PPC::STWX))
            .addReg(PPC::R30, RegState::Kill)  // Save PIC base pointer.
            .addReg(PPC::ZERO)
            .addReg(ScratchReg);  // This will be the index (R0 is ok here).
        }
        if (HasBP) {
          // R0 += (BPOffset-LastOffset).
          BuildMI(MBB, MBBI, dl, TII.get(PPC::ADDIC), ScratchReg)
            .addReg(ScratchReg)
            .addImm(BPOffset-LastOffset);
          LastOffset = BPOffset;
          BuildMI(MBB, MBBI, dl, TII.get(PPC::STWX))
            .addReg(BPReg, RegState::Kill)  // Save BP.
            .addReg(PPC::ZERO)
            .addReg(ScratchReg);  // This will be the index (R0 is ok here).
          // BP = R0-LastOffset
          BuildMI(MBB, MBBI, dl, TII.get(PPC::ADDIC), BPReg)
            .addReg(ScratchReg, RegState::Kill)
            .addImm(-LastOffset);
        }
      } else {
        // ScratchReg is not R0, so use it as the base register. It is
        // already set to the old SP, so we can use the offsets directly.

        // Now that the stack frame has been allocated, save all the necessary
        // registers using ScratchReg as the base address.
        if (HasFP)
          BuildMI(MBB, MBBI, dl, StoreInst)
            .addReg(FPReg)
            .addImm(FPOffset)
            .addReg(ScratchReg);
        if (FI->usesPICBase())
          BuildMI(MBB, MBBI, dl, StoreInst)
            .addReg(PPC::R30)
            .addImm(PBPOffset)
            .addReg(ScratchReg);
        if (HasBP) {
          BuildMI(MBB, MBBI, dl, StoreInst)
            .addReg(BPReg)
            .addImm(BPOffset)
            .addReg(ScratchReg);
          BuildMI(MBB, MBBI, dl, OrInst, BPReg)
            .addReg(ScratchReg, RegState::Kill)
            .addReg(ScratchReg);
        }
      }
    } else {
      // The frame size is a known 16-bit constant (fitting in the immediate
      // field of STWU). To be here we have to be compiling for PPC32.
      // Since the SPReg has been decreased by FrameSize, add it back to each
      // offset.
      if (HasFP)
        BuildMI(MBB, MBBI, dl, StoreInst)
          .addReg(FPReg)
          .addImm(FrameSize + FPOffset)
          .addReg(SPReg);
      if (FI->usesPICBase())
        BuildMI(MBB, MBBI, dl, StoreInst)
          .addReg(PPC::R30)
          .addImm(FrameSize + PBPOffset)
          .addReg(SPReg);
      if (HasBP) {
        BuildMI(MBB, MBBI, dl, StoreInst)
          .addReg(BPReg)
          .addImm(FrameSize + BPOffset)
          .addReg(SPReg);
        BuildMI(MBB, MBBI, dl, TII.get(PPC::ADDI), BPReg)
          .addReg(SPReg)
          .addImm(FrameSize);
      }
    }
  }

  // Add Call Frame Information for the instructions we generated above.
  if (needsCFI) {
    unsigned CFIIndex;

    if (HasBP) {
      // Define CFA in terms of BP. Do this in preference to using FP/SP,
      // because if the stack needed aligning then CFA won't be at a fixed
      // offset from FP/SP.
      unsigned Reg = MRI->getDwarfRegNum(BPReg, true);
      CFIIndex = MF.addFrameInst(
          MCCFIInstruction::createDefCfaRegister(nullptr, Reg));
    } else {
      // Adjust the definition of CFA to account for the change in SP.
      assert(NegFrameSize);
      CFIIndex = MF.addFrameInst(
          MCCFIInstruction::createDefCfaOffset(nullptr, NegFrameSize));
    }
    BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);

    if (HasFP) {
      // Describe where FP was saved, at a fixed offset from CFA.
      unsigned Reg = MRI->getDwarfRegNum(FPReg, true);
      CFIIndex = MF.addFrameInst(
          MCCFIInstruction::createOffset(nullptr, Reg, FPOffset));
      BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
    }

    if (FI->usesPICBase()) {
      // Describe where FP was saved, at a fixed offset from CFA.
      unsigned Reg = MRI->getDwarfRegNum(PPC::R30, true);
      CFIIndex = MF.addFrameInst(
          MCCFIInstruction::createOffset(nullptr, Reg, PBPOffset));
      BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
    }

    if (HasBP) {
      // Describe where BP was saved, at a fixed offset from CFA.
      unsigned Reg = MRI->getDwarfRegNum(BPReg, true);
      CFIIndex = MF.addFrameInst(
          MCCFIInstruction::createOffset(nullptr, Reg, BPOffset));
      BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
    }

    if (MustSaveLR) {
      // Describe where LR was saved, at a fixed offset from CFA.
      unsigned Reg = MRI->getDwarfRegNum(LRReg, true);
      CFIIndex = MF.addFrameInst(
          MCCFIInstruction::createOffset(nullptr, Reg, LROffset));
      BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
    }
  }

  // If there is a frame pointer, copy R1 into R31
  if (HasFP) {
    BuildMI(MBB, MBBI, dl, OrInst, FPReg)
      .addReg(SPReg)
      .addReg(SPReg);

    if (!HasBP && needsCFI) {
      // Change the definition of CFA from SP+offset to FP+offset, because SP
      // will change at every alloca.
      unsigned Reg = MRI->getDwarfRegNum(FPReg, true);
      unsigned CFIIndex = MF.addFrameInst(
          MCCFIInstruction::createDefCfaRegister(nullptr, Reg));

      BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
    }
  }

  if (needsCFI) {
    // Describe where callee saved registers were saved, at fixed offsets from
    // CFA.
    const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
    for (unsigned I = 0, E = CSI.size(); I != E; ++I) {
      unsigned Reg = CSI[I].getReg();
      if (Reg == PPC::LR || Reg == PPC::LR8 || Reg == PPC::RM) continue;

      // This is a bit of a hack: CR2LT, CR2GT, CR2EQ and CR2UN are just
      // subregisters of CR2. We just need to emit a move of CR2.
      if (PPC::CRBITRCRegClass.contains(Reg))
        continue;

      // For SVR4, don't emit a move for the CR spill slot if we haven't
      // spilled CRs.
      if (isSVR4ABI && (PPC::CR2 <= Reg && Reg <= PPC::CR4)
          && !MustSaveCR)
        continue;

      // For 64-bit SVR4 when we have spilled CRs, the spill location
      // is SP+8, not a frame-relative slot.
      if (isSVR4ABI && isPPC64 && (PPC::CR2 <= Reg && Reg <= PPC::CR4)) {
        // In the ELFv1 ABI, only CR2 is noted in CFI and stands in for
        // the whole CR word.  In the ELFv2 ABI, every CR that was
        // actually saved gets its own CFI record.
        unsigned CRReg = isELFv2ABI? Reg : (unsigned) PPC::CR2;
        unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
            nullptr, MRI->getDwarfRegNum(CRReg, true), 8));
        BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(CFIIndex);
        continue;
      }

      if (CSI[I].isSpilledToReg()) {
        unsigned SpilledReg = CSI[I].getDstReg();
        unsigned CFIRegister = MF.addFrameInst(MCCFIInstruction::createRegister(
            nullptr, MRI->getDwarfRegNum(Reg, true),
            MRI->getDwarfRegNum(SpilledReg, true)));
        BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIRegister);
      } else {
        int Offset = MFI.getObjectOffset(CSI[I].getFrameIdx());
        unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
            nullptr, MRI->getDwarfRegNum(Reg, true), Offset));
        BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(CFIIndex);
      }
    }
  }
}

void PPCFrameLowering::emitEpilogue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  DebugLoc dl;

  if (MBBI != MBB.end())
    dl = MBBI->getDebugLoc();

  const PPCInstrInfo &TII = *Subtarget.getInstrInfo();
  const PPCRegisterInfo *RegInfo = Subtarget.getRegisterInfo();

  // Get alignment info so we know how to restore the SP.
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  // Get the number of bytes allocated from the FrameInfo.
  int FrameSize = MFI.getStackSize();

  // Get processor type.
  bool isPPC64 = Subtarget.isPPC64();
  // Get the ABI.
  bool isSVR4ABI = Subtarget.isSVR4ABI();

  // Check if the link register (LR) has been saved.
  PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();
  bool MustSaveLR = FI->mustSaveLR();
  const SmallVectorImpl<unsigned> &MustSaveCRs = FI->getMustSaveCRs();
  bool MustSaveCR = !MustSaveCRs.empty();
  // Do we have a frame pointer and/or base pointer for this function?
  bool HasFP = hasFP(MF);
  bool HasBP = RegInfo->hasBasePointer(MF);
  bool HasRedZone = Subtarget.isPPC64() || !Subtarget.isSVR4ABI();

  unsigned SPReg      = isPPC64 ? PPC::X1  : PPC::R1;
  unsigned BPReg      = RegInfo->getBaseRegister(MF);
  unsigned FPReg      = isPPC64 ? PPC::X31 : PPC::R31;
  unsigned ScratchReg = 0;
  unsigned TempReg     = isPPC64 ? PPC::X12 : PPC::R12; // another scratch reg
  const MCInstrDesc& MTLRInst = TII.get( isPPC64 ? PPC::MTLR8
                                                 : PPC::MTLR );
  const MCInstrDesc& LoadInst = TII.get( isPPC64 ? PPC::LD
                                                 : PPC::LWZ );
  const MCInstrDesc& LoadImmShiftedInst = TII.get( isPPC64 ? PPC::LIS8
                                                           : PPC::LIS );
  const MCInstrDesc& OrInst = TII.get(isPPC64 ? PPC::OR8
                                              : PPC::OR );
  const MCInstrDesc& OrImmInst = TII.get( isPPC64 ? PPC::ORI8
                                                  : PPC::ORI );
  const MCInstrDesc& AddImmInst = TII.get( isPPC64 ? PPC::ADDI8
                                                   : PPC::ADDI );
  const MCInstrDesc& AddInst = TII.get( isPPC64 ? PPC::ADD8
                                                : PPC::ADD4 );

  int LROffset = getReturnSaveOffset();

  int FPOffset = 0;

  // Using the same bool variable as below to suppress compiler warnings.
  bool SingleScratchReg = findScratchRegister(&MBB, true, false, &ScratchReg,
                                              &TempReg);
  assert(SingleScratchReg &&
         "Could not find an available scratch register");

  SingleScratchReg = ScratchReg == TempReg;

  if (HasFP) {
    if (isSVR4ABI) {
      int FPIndex = FI->getFramePointerSaveIndex();
      assert(FPIndex && "No Frame Pointer Save Slot!");
      FPOffset = MFI.getObjectOffset(FPIndex);
    } else {
      FPOffset = getFramePointerSaveOffset();
    }
  }

  int BPOffset = 0;
  if (HasBP) {
    if (isSVR4ABI) {
      int BPIndex = FI->getBasePointerSaveIndex();
      assert(BPIndex && "No Base Pointer Save Slot!");
      BPOffset = MFI.getObjectOffset(BPIndex);
    } else {
      BPOffset = getBasePointerSaveOffset();
    }
  }

  int PBPOffset = 0;
  if (FI->usesPICBase()) {
    int PBPIndex = FI->getPICBasePointerSaveIndex();
    assert(PBPIndex && "No PIC Base Pointer Save Slot!");
    PBPOffset = MFI.getObjectOffset(PBPIndex);
  }

  bool IsReturnBlock = (MBBI != MBB.end() && MBBI->isReturn());

  if (IsReturnBlock) {
    unsigned RetOpcode = MBBI->getOpcode();
    bool UsesTCRet =  RetOpcode == PPC::TCRETURNri ||
                      RetOpcode == PPC::TCRETURNdi ||
                      RetOpcode == PPC::TCRETURNai ||
                      RetOpcode == PPC::TCRETURNri8 ||
                      RetOpcode == PPC::TCRETURNdi8 ||
                      RetOpcode == PPC::TCRETURNai8;

    if (UsesTCRet) {
      int MaxTCRetDelta = FI->getTailCallSPDelta();
      MachineOperand &StackAdjust = MBBI->getOperand(1);
      assert(StackAdjust.isImm() && "Expecting immediate value.");
      // Adjust stack pointer.
      int StackAdj = StackAdjust.getImm();
      int Delta = StackAdj - MaxTCRetDelta;
      assert((Delta >= 0) && "Delta must be positive");
      if (MaxTCRetDelta>0)
        FrameSize += (StackAdj +Delta);
      else
        FrameSize += StackAdj;
    }
  }

  // Frames of 32KB & larger require special handling because they cannot be
  // indexed into with a simple LD/LWZ immediate offset operand.
  bool isLargeFrame = !isInt<16>(FrameSize);

  // On targets without red zone, the SP needs to be restored last, so that
  // all live contents of the stack frame are upwards of the SP. This means
  // that we cannot restore SP just now, since there may be more registers
  // to restore from the stack frame (e.g. R31). If the frame size is not
  // a simple immediate value, we will need a spare register to hold the
  // restored SP. If the frame size is known and small, we can simply adjust
  // the offsets of the registers to be restored, and still use SP to restore
  // them. In such case, the final update of SP will be to add the frame
  // size to it.
  // To simplify the code, set RBReg to the base register used to restore
  // values from the stack, and set SPAdd to the value that needs to be added
  // to the SP at the end. The default values are as if red zone was present.
  unsigned RBReg = SPReg;
  unsigned SPAdd = 0;

  if (FrameSize) {
    // In the prologue, the loaded (or persistent) stack pointer value is
    // offset by the STDU/STDUX/STWU/STWUX instruction. For targets with red
    // zone add this offset back now.

    // If this function contained a fastcc call and GuaranteedTailCallOpt is
    // enabled (=> hasFastCall()==true) the fastcc call might contain a tail
    // call which invalidates the stack pointer value in SP(0). So we use the
    // value of R31 in this case.
    if (FI->hasFastCall()) {
      assert(HasFP && "Expecting a valid frame pointer.");
      if (!HasRedZone)
        RBReg = FPReg;
      if (!isLargeFrame) {
        BuildMI(MBB, MBBI, dl, AddImmInst, RBReg)
          .addReg(FPReg).addImm(FrameSize);
      } else {
        BuildMI(MBB, MBBI, dl, LoadImmShiftedInst, ScratchReg)
          .addImm(FrameSize >> 16);
        BuildMI(MBB, MBBI, dl, OrImmInst, ScratchReg)
          .addReg(ScratchReg, RegState::Kill)
          .addImm(FrameSize & 0xFFFF);
        BuildMI(MBB, MBBI, dl, AddInst)
          .addReg(RBReg)
          .addReg(FPReg)
          .addReg(ScratchReg);
      }
    } else if (!isLargeFrame && !HasBP && !MFI.hasVarSizedObjects()) {
      if (HasRedZone) {
        BuildMI(MBB, MBBI, dl, AddImmInst, SPReg)
          .addReg(SPReg)
          .addImm(FrameSize);
      } else {
        // Make sure that adding FrameSize will not overflow the max offset
        // size.
        assert(FPOffset <= 0 && BPOffset <= 0 && PBPOffset <= 0 &&
               "Local offsets should be negative");
        SPAdd = FrameSize;
        FPOffset += FrameSize;
        BPOffset += FrameSize;
        PBPOffset += FrameSize;
      }
    } else {
      // We don't want to use ScratchReg as a base register, because it
      // could happen to be R0. Use FP instead, but make sure to preserve it.
      if (!HasRedZone) {
        // If FP is not saved, copy it to ScratchReg.
        if (!HasFP)
          BuildMI(MBB, MBBI, dl, OrInst, ScratchReg)
            .addReg(FPReg)
            .addReg(FPReg);
        RBReg = FPReg;
      }
      BuildMI(MBB, MBBI, dl, LoadInst, RBReg)
        .addImm(0)
        .addReg(SPReg);
    }
  }
  assert(RBReg != ScratchReg && "Should have avoided ScratchReg");
  // If there is no red zone, ScratchReg may be needed for holding a useful
  // value (although not the base register). Make sure it is not overwritten
  // too early.

  assert((isPPC64 || !MustSaveCR) &&
         "Epilogue CR restoring supported only in 64-bit mode");

  // If we need to restore both the LR and the CR and we only have one
  // available scratch register, we must do them one at a time.
  if (MustSaveCR && SingleScratchReg && MustSaveLR) {
    // Here TempReg == ScratchReg, and in the absence of red zone ScratchReg
    // is live here.
    assert(HasRedZone && "Expecting red zone");
    BuildMI(MBB, MBBI, dl, TII.get(PPC::LWZ8), TempReg)
      .addImm(8)
      .addReg(SPReg);
    for (unsigned i = 0, e = MustSaveCRs.size(); i != e; ++i)
      BuildMI(MBB, MBBI, dl, TII.get(PPC::MTOCRF8), MustSaveCRs[i])
        .addReg(TempReg, getKillRegState(i == e-1));
  }

  // Delay restoring of the LR if ScratchReg is needed. This is ok, since
  // LR is stored in the caller's stack frame. ScratchReg will be needed
  // if RBReg is anything other than SP. We shouldn't use ScratchReg as
  // a base register anyway, because it may happen to be R0.
  bool LoadedLR = false;
  if (MustSaveLR && RBReg == SPReg && isInt<16>(LROffset+SPAdd)) {
    BuildMI(MBB, MBBI, dl, LoadInst, ScratchReg)
      .addImm(LROffset+SPAdd)
      .addReg(RBReg);
    LoadedLR = true;
  }

  if (MustSaveCR && !(SingleScratchReg && MustSaveLR)) {
    // This will only occur for PPC64.
    assert(isPPC64 && "Expecting 64-bit mode");
    assert(RBReg == SPReg && "Should be using SP as a base register");
    BuildMI(MBB, MBBI, dl, TII.get(PPC::LWZ8), TempReg)
      .addImm(8)
      .addReg(RBReg);
  }

  if (HasFP) {
    // If there is red zone, restore FP directly, since SP has already been
    // restored. Otherwise, restore the value of FP into ScratchReg.
    if (HasRedZone || RBReg == SPReg)
      BuildMI(MBB, MBBI, dl, LoadInst, FPReg)
        .addImm(FPOffset)
        .addReg(SPReg);
    else
      BuildMI(MBB, MBBI, dl, LoadInst, ScratchReg)
        .addImm(FPOffset)
        .addReg(RBReg);
  }

  if (FI->usesPICBase())
    BuildMI(MBB, MBBI, dl, LoadInst, PPC::R30)
      .addImm(PBPOffset)
      .addReg(RBReg);

  if (HasBP)
    BuildMI(MBB, MBBI, dl, LoadInst, BPReg)
      .addImm(BPOffset)
      .addReg(RBReg);

  // There is nothing more to be loaded from the stack, so now we can
  // restore SP: SP = RBReg + SPAdd.
  if (RBReg != SPReg || SPAdd != 0) {
    assert(!HasRedZone && "This should not happen with red zone");
    // If SPAdd is 0, generate a copy.
    if (SPAdd == 0)
      BuildMI(MBB, MBBI, dl, OrInst, SPReg)
        .addReg(RBReg)
        .addReg(RBReg);
    else
      BuildMI(MBB, MBBI, dl, AddImmInst, SPReg)
        .addReg(RBReg)
        .addImm(SPAdd);

    assert(RBReg != ScratchReg && "Should be using FP or SP as base register");
    if (RBReg == FPReg)
      BuildMI(MBB, MBBI, dl, OrInst, FPReg)
        .addReg(ScratchReg)
        .addReg(ScratchReg);

    // Now load the LR from the caller's stack frame.
    if (MustSaveLR && !LoadedLR)
      BuildMI(MBB, MBBI, dl, LoadInst, ScratchReg)
        .addImm(LROffset)
        .addReg(SPReg);
  }

  if (MustSaveCR &&
      !(SingleScratchReg && MustSaveLR)) // will only occur for PPC64
    for (unsigned i = 0, e = MustSaveCRs.size(); i != e; ++i)
      BuildMI(MBB, MBBI, dl, TII.get(PPC::MTOCRF8), MustSaveCRs[i])
        .addReg(TempReg, getKillRegState(i == e-1));

  if (MustSaveLR)
    BuildMI(MBB, MBBI, dl, MTLRInst).addReg(ScratchReg);

  // Callee pop calling convention. Pop parameter/linkage area. Used for tail
  // call optimization
  if (IsReturnBlock) {
    unsigned RetOpcode = MBBI->getOpcode();
    if (MF.getTarget().Options.GuaranteedTailCallOpt &&
        (RetOpcode == PPC::BLR || RetOpcode == PPC::BLR8) &&
        MF.getFunction().getCallingConv() == CallingConv::Fast) {
      PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();
      unsigned CallerAllocatedAmt = FI->getMinReservedArea();

      if (CallerAllocatedAmt && isInt<16>(CallerAllocatedAmt)) {
        BuildMI(MBB, MBBI, dl, AddImmInst, SPReg)
          .addReg(SPReg).addImm(CallerAllocatedAmt);
      } else {
        BuildMI(MBB, MBBI, dl, LoadImmShiftedInst, ScratchReg)
          .addImm(CallerAllocatedAmt >> 16);
        BuildMI(MBB, MBBI, dl, OrImmInst, ScratchReg)
          .addReg(ScratchReg, RegState::Kill)
          .addImm(CallerAllocatedAmt & 0xFFFF);
        BuildMI(MBB, MBBI, dl, AddInst)
          .addReg(SPReg)
          .addReg(FPReg)
          .addReg(ScratchReg);
      }
    } else {
      createTailCallBranchInstr(MBB);
    }
  }
}

void PPCFrameLowering::createTailCallBranchInstr(MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();

  // If we got this far a first terminator should exist.
  assert(MBBI != MBB.end() && "Failed to find the first terminator.");

  DebugLoc dl = MBBI->getDebugLoc();
  const PPCInstrInfo &TII = *Subtarget.getInstrInfo();

  // Create branch instruction for pseudo tail call return instruction
  unsigned RetOpcode = MBBI->getOpcode();
  if (RetOpcode == PPC::TCRETURNdi) {
    MBBI = MBB.getLastNonDebugInstr();
    MachineOperand &JumpTarget = MBBI->getOperand(0);
    BuildMI(MBB, MBBI, dl, TII.get(PPC::TAILB)).
      addGlobalAddress(JumpTarget.getGlobal(), JumpTarget.getOffset());
  } else if (RetOpcode == PPC::TCRETURNri) {
    MBBI = MBB.getLastNonDebugInstr();
    assert(MBBI->getOperand(0).isReg() && "Expecting register operand.");
    BuildMI(MBB, MBBI, dl, TII.get(PPC::TAILBCTR));
  } else if (RetOpcode == PPC::TCRETURNai) {
    MBBI = MBB.getLastNonDebugInstr();
    MachineOperand &JumpTarget = MBBI->getOperand(0);
    BuildMI(MBB, MBBI, dl, TII.get(PPC::TAILBA)).addImm(JumpTarget.getImm());
  } else if (RetOpcode == PPC::TCRETURNdi8) {
    MBBI = MBB.getLastNonDebugInstr();
    MachineOperand &JumpTarget = MBBI->getOperand(0);
    BuildMI(MBB, MBBI, dl, TII.get(PPC::TAILB8)).
      addGlobalAddress(JumpTarget.getGlobal(), JumpTarget.getOffset());
  } else if (RetOpcode == PPC::TCRETURNri8) {
    MBBI = MBB.getLastNonDebugInstr();
    assert(MBBI->getOperand(0).isReg() && "Expecting register operand.");
    BuildMI(MBB, MBBI, dl, TII.get(PPC::TAILBCTR8));
  } else if (RetOpcode == PPC::TCRETURNai8) {
    MBBI = MBB.getLastNonDebugInstr();
    MachineOperand &JumpTarget = MBBI->getOperand(0);
    BuildMI(MBB, MBBI, dl, TII.get(PPC::TAILBA8)).addImm(JumpTarget.getImm());
  }
}

void PPCFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                            BitVector &SavedRegs,
                                            RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);

  const PPCRegisterInfo *RegInfo = Subtarget.getRegisterInfo();

  //  Save and clear the LR state.
  PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();
  unsigned LR = RegInfo->getRARegister();
  FI->setMustSaveLR(MustSaveLR(MF, LR));
  SavedRegs.reset(LR);

  //  Save R31 if necessary
  int FPSI = FI->getFramePointerSaveIndex();
  bool isPPC64 = Subtarget.isPPC64();
  bool isDarwinABI  = Subtarget.isDarwinABI();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  // If the frame pointer save index hasn't been defined yet.
  if (!FPSI && needsFP(MF)) {
    // Find out what the fix offset of the frame pointer save area.
    int FPOffset = getFramePointerSaveOffset();
    // Allocate the frame index for frame pointer save area.
    FPSI = MFI.CreateFixedObject(isPPC64? 8 : 4, FPOffset, true);
    // Save the result.
    FI->setFramePointerSaveIndex(FPSI);
  }

  int BPSI = FI->getBasePointerSaveIndex();
  if (!BPSI && RegInfo->hasBasePointer(MF)) {
    int BPOffset = getBasePointerSaveOffset();
    // Allocate the frame index for the base pointer save area.
    BPSI = MFI.CreateFixedObject(isPPC64? 8 : 4, BPOffset, true);
    // Save the result.
    FI->setBasePointerSaveIndex(BPSI);
  }

  // Reserve stack space for the PIC Base register (R30).
  // Only used in SVR4 32-bit.
  if (FI->usesPICBase()) {
    int PBPSI = MFI.CreateFixedObject(4, -8, true);
    FI->setPICBasePointerSaveIndex(PBPSI);
  }

  // Make sure we don't explicitly spill r31, because, for example, we have
  // some inline asm which explicitly clobbers it, when we otherwise have a
  // frame pointer and are using r31's spill slot for the prologue/epilogue
  // code. Same goes for the base pointer and the PIC base register.
  if (needsFP(MF))
    SavedRegs.reset(isPPC64 ? PPC::X31 : PPC::R31);
  if (RegInfo->hasBasePointer(MF))
    SavedRegs.reset(RegInfo->getBaseRegister(MF));
  if (FI->usesPICBase())
    SavedRegs.reset(PPC::R30);

  // Reserve stack space to move the linkage area to in case of a tail call.
  int TCSPDelta = 0;
  if (MF.getTarget().Options.GuaranteedTailCallOpt &&
      (TCSPDelta = FI->getTailCallSPDelta()) < 0) {
    MFI.CreateFixedObject(-1 * TCSPDelta, TCSPDelta, true);
  }

  // For 32-bit SVR4, allocate the nonvolatile CR spill slot iff the
  // function uses CR 2, 3, or 4.
  if (!isPPC64 && !isDarwinABI &&
      (SavedRegs.test(PPC::CR2) ||
       SavedRegs.test(PPC::CR3) ||
       SavedRegs.test(PPC::CR4))) {
    int FrameIdx = MFI.CreateFixedObject((uint64_t)4, (int64_t)-4, true);
    FI->setCRSpillFrameIndex(FrameIdx);
  }
}

void PPCFrameLowering::processFunctionBeforeFrameFinalized(MachineFunction &MF,
                                                       RegScavenger *RS) const {
  // Early exit if not using the SVR4 ABI.
  if (!Subtarget.isSVR4ABI()) {
    addScavengingSpillSlot(MF, RS);
    return;
  }

  // Get callee saved register information.
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();

  // If the function is shrink-wrapped, and if the function has a tail call, the
  // tail call might not be in the new RestoreBlock, so real branch instruction
  // won't be generated by emitEpilogue(), because shrink-wrap has chosen new
  // RestoreBlock. So we handle this case here.
  if (MFI.getSavePoint() && MFI.hasTailCall()) {
    MachineBasicBlock *RestoreBlock = MFI.getRestorePoint();
    for (MachineBasicBlock &MBB : MF) {
      if (MBB.isReturnBlock() && (&MBB) != RestoreBlock)
        createTailCallBranchInstr(MBB);
    }
  }

  // Early exit if no callee saved registers are modified!
  if (CSI.empty() && !needsFP(MF)) {
    addScavengingSpillSlot(MF, RS);
    return;
  }

  unsigned MinGPR = PPC::R31;
  unsigned MinG8R = PPC::X31;
  unsigned MinFPR = PPC::F31;
  unsigned MinVR = Subtarget.hasSPE() ? PPC::S31 : PPC::V31;

  bool HasGPSaveArea = false;
  bool HasG8SaveArea = false;
  bool HasFPSaveArea = false;
  bool HasVRSAVESaveArea = false;
  bool HasVRSaveArea = false;

  SmallVector<CalleeSavedInfo, 18> GPRegs;
  SmallVector<CalleeSavedInfo, 18> G8Regs;
  SmallVector<CalleeSavedInfo, 18> FPRegs;
  SmallVector<CalleeSavedInfo, 18> VRegs;

  for (unsigned i = 0, e = CSI.size(); i != e; ++i) {
    unsigned Reg = CSI[i].getReg();
    if (PPC::GPRCRegClass.contains(Reg) ||
        PPC::SPE4RCRegClass.contains(Reg)) {
      HasGPSaveArea = true;

      GPRegs.push_back(CSI[i]);

      if (Reg < MinGPR) {
        MinGPR = Reg;
      }
    } else if (PPC::G8RCRegClass.contains(Reg)) {
      HasG8SaveArea = true;

      G8Regs.push_back(CSI[i]);

      if (Reg < MinG8R) {
        MinG8R = Reg;
      }
    } else if (PPC::F8RCRegClass.contains(Reg)) {
      HasFPSaveArea = true;

      FPRegs.push_back(CSI[i]);

      if (Reg < MinFPR) {
        MinFPR = Reg;
      }
    } else if (PPC::CRBITRCRegClass.contains(Reg) ||
               PPC::CRRCRegClass.contains(Reg)) {
      ; // do nothing, as we already know whether CRs are spilled
    } else if (PPC::VRSAVERCRegClass.contains(Reg)) {
      HasVRSAVESaveArea = true;
    } else if (PPC::VRRCRegClass.contains(Reg) ||
               PPC::SPERCRegClass.contains(Reg)) {
      // Altivec and SPE are mutually exclusive, but have the same stack
      // alignment requirements, so overload the save area for both cases.
      HasVRSaveArea = true;

      VRegs.push_back(CSI[i]);

      if (Reg < MinVR) {
        MinVR = Reg;
      }
    } else {
      llvm_unreachable("Unknown RegisterClass!");
    }
  }

  PPCFunctionInfo *PFI = MF.getInfo<PPCFunctionInfo>();
  const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();

  int64_t LowerBound = 0;

  // Take into account stack space reserved for tail calls.
  int TCSPDelta = 0;
  if (MF.getTarget().Options.GuaranteedTailCallOpt &&
      (TCSPDelta = PFI->getTailCallSPDelta()) < 0) {
    LowerBound = TCSPDelta;
  }

  // The Floating-point register save area is right below the back chain word
  // of the previous stack frame.
  if (HasFPSaveArea) {
    for (unsigned i = 0, e = FPRegs.size(); i != e; ++i) {
      int FI = FPRegs[i].getFrameIdx();

      MFI.setObjectOffset(FI, LowerBound + MFI.getObjectOffset(FI));
    }

    LowerBound -= (31 - TRI->getEncodingValue(MinFPR) + 1) * 8;
  }

  // Check whether the frame pointer register is allocated. If so, make sure it
  // is spilled to the correct offset.
  if (needsFP(MF)) {
    int FI = PFI->getFramePointerSaveIndex();
    assert(FI && "No Frame Pointer Save Slot!");
    MFI.setObjectOffset(FI, LowerBound + MFI.getObjectOffset(FI));
    // FP is R31/X31, so no need to update MinGPR/MinG8R.
    HasGPSaveArea = true;
  }

  if (PFI->usesPICBase()) {
    int FI = PFI->getPICBasePointerSaveIndex();
    assert(FI && "No PIC Base Pointer Save Slot!");
    MFI.setObjectOffset(FI, LowerBound + MFI.getObjectOffset(FI));

    MinGPR = std::min<unsigned>(MinGPR, PPC::R30);
    HasGPSaveArea = true;
  }

  const PPCRegisterInfo *RegInfo = Subtarget.getRegisterInfo();
  if (RegInfo->hasBasePointer(MF)) {
    int FI = PFI->getBasePointerSaveIndex();
    assert(FI && "No Base Pointer Save Slot!");
    MFI.setObjectOffset(FI, LowerBound + MFI.getObjectOffset(FI));

    unsigned BP = RegInfo->getBaseRegister(MF);
    if (PPC::G8RCRegClass.contains(BP)) {
      MinG8R = std::min<unsigned>(MinG8R, BP);
      HasG8SaveArea = true;
    } else if (PPC::GPRCRegClass.contains(BP)) {
      MinGPR = std::min<unsigned>(MinGPR, BP);
      HasGPSaveArea = true;
    }
  }

  // General register save area starts right below the Floating-point
  // register save area.
  if (HasGPSaveArea || HasG8SaveArea) {
    // Move general register save area spill slots down, taking into account
    // the size of the Floating-point register save area.
    for (unsigned i = 0, e = GPRegs.size(); i != e; ++i) {
      if (!GPRegs[i].isSpilledToReg()) {
        int FI = GPRegs[i].getFrameIdx();
        MFI.setObjectOffset(FI, LowerBound + MFI.getObjectOffset(FI));
      }
    }

    // Move general register save area spill slots down, taking into account
    // the size of the Floating-point register save area.
    for (unsigned i = 0, e = G8Regs.size(); i != e; ++i) {
      if (!G8Regs[i].isSpilledToReg()) {
        int FI = G8Regs[i].getFrameIdx();
        MFI.setObjectOffset(FI, LowerBound + MFI.getObjectOffset(FI));
      }
    }

    unsigned MinReg =
      std::min<unsigned>(TRI->getEncodingValue(MinGPR),
                         TRI->getEncodingValue(MinG8R));

    if (Subtarget.isPPC64()) {
      LowerBound -= (31 - MinReg + 1) * 8;
    } else {
      LowerBound -= (31 - MinReg + 1) * 4;
    }
  }

  // For 32-bit only, the CR save area is below the general register
  // save area.  For 64-bit SVR4, the CR save area is addressed relative
  // to the stack pointer and hence does not need an adjustment here.
  // Only CR2 (the first nonvolatile spilled) has an associated frame
  // index so that we have a single uniform save area.
  if (spillsCR(MF) && !(Subtarget.isPPC64() && Subtarget.isSVR4ABI())) {
    // Adjust the frame index of the CR spill slot.
    for (unsigned i = 0, e = CSI.size(); i != e; ++i) {
      unsigned Reg = CSI[i].getReg();

      if ((Subtarget.isSVR4ABI() && Reg == PPC::CR2)
          // Leave Darwin logic as-is.
          || (!Subtarget.isSVR4ABI() &&
              (PPC::CRBITRCRegClass.contains(Reg) ||
               PPC::CRRCRegClass.contains(Reg)))) {
        int FI = CSI[i].getFrameIdx();

        MFI.setObjectOffset(FI, LowerBound + MFI.getObjectOffset(FI));
      }
    }

    LowerBound -= 4; // The CR save area is always 4 bytes long.
  }

  if (HasVRSAVESaveArea) {
    // FIXME SVR4: Is it actually possible to have multiple elements in CSI
    //             which have the VRSAVE register class?
    // Adjust the frame index of the VRSAVE spill slot.
    for (unsigned i = 0, e = CSI.size(); i != e; ++i) {
      unsigned Reg = CSI[i].getReg();

      if (PPC::VRSAVERCRegClass.contains(Reg)) {
        int FI = CSI[i].getFrameIdx();

        MFI.setObjectOffset(FI, LowerBound + MFI.getObjectOffset(FI));
      }
    }

    LowerBound -= 4; // The VRSAVE save area is always 4 bytes long.
  }

  // Both Altivec and SPE have the same alignment and padding requirements
  // within the stack frame.
  if (HasVRSaveArea) {
    // Insert alignment padding, we need 16-byte alignment. Note: for positive
    // number the alignment formula is : y = (x + (n-1)) & (~(n-1)). But since
    // we are using negative number here (the stack grows downward). We should
    // use formula : y = x & (~(n-1)). Where x is the size before aligning, n
    // is the alignment size ( n = 16 here) and y is the size after aligning.
    assert(LowerBound <= 0 && "Expect LowerBound have a non-positive value!");
    LowerBound &= ~(15);

    for (unsigned i = 0, e = VRegs.size(); i != e; ++i) {
      int FI = VRegs[i].getFrameIdx();

      MFI.setObjectOffset(FI, LowerBound + MFI.getObjectOffset(FI));
    }
  }

  addScavengingSpillSlot(MF, RS);
}

void
PPCFrameLowering::addScavengingSpillSlot(MachineFunction &MF,
                                         RegScavenger *RS) const {
  // Reserve a slot closest to SP or frame pointer if we have a dynalloc or
  // a large stack, which will require scavenging a register to materialize a
  // large offset.

  // We need to have a scavenger spill slot for spills if the frame size is
  // large. In case there is no free register for large-offset addressing,
  // this slot is used for the necessary emergency spill. Also, we need the
  // slot for dynamic stack allocations.

  // The scavenger might be invoked if the frame offset does not fit into
  // the 16-bit immediate. We don't know the complete frame size here
  // because we've not yet computed callee-saved register spills or the
  // needed alignment padding.
  unsigned StackSize = determineFrameLayout(MF, false, true);
  MachineFrameInfo &MFI = MF.getFrameInfo();
  if (MFI.hasVarSizedObjects() || spillsCR(MF) || spillsVRSAVE(MF) ||
      hasNonRISpills(MF) || (hasSpills(MF) && !isInt<16>(StackSize))) {
    const TargetRegisterClass &GPRC = PPC::GPRCRegClass;
    const TargetRegisterClass &G8RC = PPC::G8RCRegClass;
    const TargetRegisterClass &RC = Subtarget.isPPC64() ? G8RC : GPRC;
    const TargetRegisterInfo &TRI = *Subtarget.getRegisterInfo();
    unsigned Size = TRI.getSpillSize(RC);
    unsigned Align = TRI.getSpillAlignment(RC);
    RS->addScavengingFrameIndex(MFI.CreateStackObject(Size, Align, false));

    // Might we have over-aligned allocas?
    bool HasAlVars = MFI.hasVarSizedObjects() &&
                     MFI.getMaxAlignment() > getStackAlignment();

    // These kinds of spills might need two registers.
    if (spillsCR(MF) || spillsVRSAVE(MF) || HasAlVars)
      RS->addScavengingFrameIndex(MFI.CreateStackObject(Size, Align, false));

  }
}

// This function checks if a callee saved gpr can be spilled to a volatile
// vector register. This occurs for leaf functions when the option
// ppc-enable-pe-vector-spills is enabled. If there are any remaining registers
// which were not spilled to vectors, return false so the target independent
// code can handle them by assigning a FrameIdx to a stack slot.
bool PPCFrameLowering::assignCalleeSavedSpillSlots(
    MachineFunction &MF, const TargetRegisterInfo *TRI,
    std::vector<CalleeSavedInfo> &CSI) const {

  if (CSI.empty())
    return true; // Early exit if no callee saved registers are modified!

  // Early exit if cannot spill gprs to volatile vector registers.
  MachineFrameInfo &MFI = MF.getFrameInfo();
  if (!EnablePEVectorSpills || MFI.hasCalls() || !Subtarget.hasP9Vector())
    return false;

  // Build a BitVector of VSRs that can be used for spilling GPRs.
  BitVector BVAllocatable = TRI->getAllocatableSet(MF);
  BitVector BVCalleeSaved(TRI->getNumRegs());
  const PPCRegisterInfo *RegInfo = Subtarget.getRegisterInfo();
  const MCPhysReg *CSRegs = RegInfo->getCalleeSavedRegs(&MF);
  for (unsigned i = 0; CSRegs[i]; ++i)
    BVCalleeSaved.set(CSRegs[i]);

  for (unsigned Reg : BVAllocatable.set_bits()) {
    // Set to 0 if the register is not a volatile VF/F8 register, or if it is
    // used in the function.
    if (BVCalleeSaved[Reg] ||
        (!PPC::F8RCRegClass.contains(Reg) &&
         !PPC::VFRCRegClass.contains(Reg)) ||
        (MF.getRegInfo().isPhysRegUsed(Reg)))
      BVAllocatable.reset(Reg);
  }

  bool AllSpilledToReg = true;
  for (auto &CS : CSI) {
    if (BVAllocatable.none())
      return false;

    unsigned Reg = CS.getReg();
    if (!PPC::G8RCRegClass.contains(Reg) && !PPC::GPRCRegClass.contains(Reg)) {
      AllSpilledToReg = false;
      continue;
    }

    unsigned VolatileVFReg = BVAllocatable.find_first();
    if (VolatileVFReg < BVAllocatable.size()) {
      CS.setDstReg(VolatileVFReg);
      BVAllocatable.reset(VolatileVFReg);
    } else {
      AllSpilledToReg = false;
    }
  }
  return AllSpilledToReg;
}


bool
PPCFrameLowering::spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator MI,
                                     const std::vector<CalleeSavedInfo> &CSI,
                                     const TargetRegisterInfo *TRI) const {

  // Currently, this function only handles SVR4 32- and 64-bit ABIs.
  // Return false otherwise to maintain pre-existing behavior.
  if (!Subtarget.isSVR4ABI())
    return false;

  MachineFunction *MF = MBB.getParent();
  const PPCInstrInfo &TII = *Subtarget.getInstrInfo();
  DebugLoc DL;
  bool CRSpilled = false;
  MachineInstrBuilder CRMIB;

  for (unsigned i = 0, e = CSI.size(); i != e; ++i) {
    unsigned Reg = CSI[i].getReg();
    // Only Darwin actually uses the VRSAVE register, but it can still appear
    // here if, for example, @llvm.eh.unwind.init() is used.  If we're not on
    // Darwin, ignore it.
    if (Reg == PPC::VRSAVE && !Subtarget.isDarwinABI())
      continue;

    // CR2 through CR4 are the nonvolatile CR fields.
    bool IsCRField = PPC::CR2 <= Reg && Reg <= PPC::CR4;

    // Add the callee-saved register as live-in; it's killed at the spill.
    // Do not do this for callee-saved registers that are live-in to the
    // function because they will already be marked live-in and this will be
    // adding it for a second time. It is an error to add the same register
    // to the set more than once.
    const MachineRegisterInfo &MRI = MF->getRegInfo();
    bool IsLiveIn = MRI.isLiveIn(Reg);
    if (!IsLiveIn)
       MBB.addLiveIn(Reg);

    if (CRSpilled && IsCRField) {
      CRMIB.addReg(Reg, RegState::ImplicitKill);
      continue;
    }

    // Insert the spill to the stack frame.
    if (IsCRField) {
      PPCFunctionInfo *FuncInfo = MF->getInfo<PPCFunctionInfo>();
      if (Subtarget.isPPC64()) {
        // The actual spill will happen at the start of the prologue.
        FuncInfo->addMustSaveCR(Reg);
      } else {
        CRSpilled = true;
        FuncInfo->setSpillsCR();

        // 32-bit:  FP-relative.  Note that we made sure CR2-CR4 all have
        // the same frame index in PPCRegisterInfo::hasReservedSpillSlot.
        CRMIB = BuildMI(*MF, DL, TII.get(PPC::MFCR), PPC::R12)
                  .addReg(Reg, RegState::ImplicitKill);

        MBB.insert(MI, CRMIB);
        MBB.insert(MI, addFrameReference(BuildMI(*MF, DL, TII.get(PPC::STW))
                                         .addReg(PPC::R12,
                                                 getKillRegState(true)),
                                         CSI[i].getFrameIdx()));
      }
    } else {
      if (CSI[i].isSpilledToReg()) {
        NumPESpillVSR++;
        BuildMI(MBB, MI, DL, TII.get(PPC::MTVSRD), CSI[i].getDstReg())
          .addReg(Reg, getKillRegState(true));
      } else {
        const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
        // Use !IsLiveIn for the kill flag.
        // We do not want to kill registers that are live in this function
        // before their use because they will become undefined registers.
        TII.storeRegToStackSlot(MBB, MI, Reg, !IsLiveIn,
                                CSI[i].getFrameIdx(), RC, TRI);
      }
    }
  }
  return true;
}

static void
restoreCRs(bool isPPC64, bool is31,
           bool CR2Spilled, bool CR3Spilled, bool CR4Spilled,
           MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
           const std::vector<CalleeSavedInfo> &CSI, unsigned CSIIndex) {

  MachineFunction *MF = MBB.getParent();
  const PPCInstrInfo &TII = *MF->getSubtarget<PPCSubtarget>().getInstrInfo();
  DebugLoc DL;
  unsigned RestoreOp, MoveReg;

  if (isPPC64)
    // This is handled during epilogue generation.
    return;
  else {
    // 32-bit:  FP-relative
    MBB.insert(MI, addFrameReference(BuildMI(*MF, DL, TII.get(PPC::LWZ),
                                             PPC::R12),
                                     CSI[CSIIndex].getFrameIdx()));
    RestoreOp = PPC::MTOCRF;
    MoveReg = PPC::R12;
  }

  if (CR2Spilled)
    MBB.insert(MI, BuildMI(*MF, DL, TII.get(RestoreOp), PPC::CR2)
               .addReg(MoveReg, getKillRegState(!CR3Spilled && !CR4Spilled)));

  if (CR3Spilled)
    MBB.insert(MI, BuildMI(*MF, DL, TII.get(RestoreOp), PPC::CR3)
               .addReg(MoveReg, getKillRegState(!CR4Spilled)));

  if (CR4Spilled)
    MBB.insert(MI, BuildMI(*MF, DL, TII.get(RestoreOp), PPC::CR4)
               .addReg(MoveReg, getKillRegState(true)));
}

MachineBasicBlock::iterator PPCFrameLowering::
eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I) const {
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  if (MF.getTarget().Options.GuaranteedTailCallOpt &&
      I->getOpcode() == PPC::ADJCALLSTACKUP) {
    // Add (actually subtract) back the amount the callee popped on return.
    if (int CalleeAmt =  I->getOperand(1).getImm()) {
      bool is64Bit = Subtarget.isPPC64();
      CalleeAmt *= -1;
      unsigned StackReg = is64Bit ? PPC::X1 : PPC::R1;
      unsigned TmpReg = is64Bit ? PPC::X0 : PPC::R0;
      unsigned ADDIInstr = is64Bit ? PPC::ADDI8 : PPC::ADDI;
      unsigned ADDInstr = is64Bit ? PPC::ADD8 : PPC::ADD4;
      unsigned LISInstr = is64Bit ? PPC::LIS8 : PPC::LIS;
      unsigned ORIInstr = is64Bit ? PPC::ORI8 : PPC::ORI;
      const DebugLoc &dl = I->getDebugLoc();

      if (isInt<16>(CalleeAmt)) {
        BuildMI(MBB, I, dl, TII.get(ADDIInstr), StackReg)
          .addReg(StackReg, RegState::Kill)
          .addImm(CalleeAmt);
      } else {
        MachineBasicBlock::iterator MBBI = I;
        BuildMI(MBB, MBBI, dl, TII.get(LISInstr), TmpReg)
          .addImm(CalleeAmt >> 16);
        BuildMI(MBB, MBBI, dl, TII.get(ORIInstr), TmpReg)
          .addReg(TmpReg, RegState::Kill)
          .addImm(CalleeAmt & 0xFFFF);
        BuildMI(MBB, MBBI, dl, TII.get(ADDInstr), StackReg)
          .addReg(StackReg, RegState::Kill)
          .addReg(TmpReg);
      }
    }
  }
  // Simply discard ADJCALLSTACKDOWN, ADJCALLSTACKUP instructions.
  return MBB.erase(I);
}

bool
PPCFrameLowering::restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MI,
                                        std::vector<CalleeSavedInfo> &CSI,
                                        const TargetRegisterInfo *TRI) const {

  // Currently, this function only handles SVR4 32- and 64-bit ABIs.
  // Return false otherwise to maintain pre-existing behavior.
  if (!Subtarget.isSVR4ABI())
    return false;

  MachineFunction *MF = MBB.getParent();
  const PPCInstrInfo &TII = *Subtarget.getInstrInfo();
  bool CR2Spilled = false;
  bool CR3Spilled = false;
  bool CR4Spilled = false;
  unsigned CSIIndex = 0;

  // Initialize insertion-point logic; we will be restoring in reverse
  // order of spill.
  MachineBasicBlock::iterator I = MI, BeforeI = I;
  bool AtStart = I == MBB.begin();

  if (!AtStart)
    --BeforeI;

  for (unsigned i = 0, e = CSI.size(); i != e; ++i) {
    unsigned Reg = CSI[i].getReg();

    // Only Darwin actually uses the VRSAVE register, but it can still appear
    // here if, for example, @llvm.eh.unwind.init() is used.  If we're not on
    // Darwin, ignore it.
    if (Reg == PPC::VRSAVE && !Subtarget.isDarwinABI())
      continue;

    if (Reg == PPC::CR2) {
      CR2Spilled = true;
      // The spill slot is associated only with CR2, which is the
      // first nonvolatile spilled.  Save it here.
      CSIIndex = i;
      continue;
    } else if (Reg == PPC::CR3) {
      CR3Spilled = true;
      continue;
    } else if (Reg == PPC::CR4) {
      CR4Spilled = true;
      continue;
    } else {
      // When we first encounter a non-CR register after seeing at
      // least one CR register, restore all spilled CRs together.
      if ((CR2Spilled || CR3Spilled || CR4Spilled)
          && !(PPC::CR2 <= Reg && Reg <= PPC::CR4)) {
        bool is31 = needsFP(*MF);
        restoreCRs(Subtarget.isPPC64(), is31,
                   CR2Spilled, CR3Spilled, CR4Spilled,
                   MBB, I, CSI, CSIIndex);
        CR2Spilled = CR3Spilled = CR4Spilled = false;
      }

      if (CSI[i].isSpilledToReg()) {
        DebugLoc DL;
        NumPEReloadVSR++;
        BuildMI(MBB, I, DL, TII.get(PPC::MFVSRD), Reg)
            .addReg(CSI[i].getDstReg(), getKillRegState(true));
      } else {
       // Default behavior for non-CR saves.
        const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
        TII.loadRegFromStackSlot(MBB, I, Reg, CSI[i].getFrameIdx(), RC, TRI);
        assert(I != MBB.begin() &&
               "loadRegFromStackSlot didn't insert any code!");
      }
    }

    // Insert in reverse order.
    if (AtStart)
      I = MBB.begin();
    else {
      I = BeforeI;
      ++I;
    }
  }

  // If we haven't yet spilled the CRs, do so now.
  if (CR2Spilled || CR3Spilled || CR4Spilled) {
    bool is31 = needsFP(*MF);
    restoreCRs(Subtarget.isPPC64(), is31, CR2Spilled, CR3Spilled, CR4Spilled,
               MBB, I, CSI, CSIIndex);
  }

  return true;
}

bool PPCFrameLowering::enableShrinkWrapping(const MachineFunction &MF) const {
  if (MF.getInfo<PPCFunctionInfo>()->shrinkWrapDisabled())
    return false;
  return (MF.getSubtarget<PPCSubtarget>().isSVR4ABI() &&
          MF.getSubtarget<PPCSubtarget>().isPPC64());
}
