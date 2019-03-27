//===- AArch64FrameLowering.cpp - AArch64 Frame Lowering -------*- C++ -*-====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the AArch64 implementation of TargetFrameLowering class.
//
// On AArch64, stack frames are structured as follows:
//
// The stack grows downward.
//
// All of the individual frame areas on the frame below are optional, i.e. it's
// possible to create a function so that the particular area isn't present
// in the frame.
//
// At function entry, the "frame" looks as follows:
//
// |                                   | Higher address
// |-----------------------------------|
// |                                   |
// | arguments passed on the stack     |
// |                                   |
// |-----------------------------------| <- sp
// |                                   | Lower address
//
//
// After the prologue has run, the frame has the following general structure.
// Note that this doesn't depict the case where a red-zone is used. Also,
// technically the last frame area (VLAs) doesn't get created until in the
// main function body, after the prologue is run. However, it's depicted here
// for completeness.
//
// |                                   | Higher address
// |-----------------------------------|
// |                                   |
// | arguments passed on the stack     |
// |                                   |
// |-----------------------------------|
// |                                   |
// | (Win64 only) varargs from reg     |
// |                                   |
// |-----------------------------------|
// |                                   |
// | prev_fp, prev_lr                  |
// | (a.k.a. "frame record")           |
// |-----------------------------------| <- fp(=x29)
// |                                   |
// | other callee-saved registers      |
// |                                   |
// |-----------------------------------|
// |.empty.space.to.make.part.below....|
// |.aligned.in.case.it.needs.more.than| (size of this area is unknown at
// |.the.standard.16-byte.alignment....|  compile time; if present)
// |-----------------------------------|
// |                                   |
// | local variables of fixed size     |
// | including spill slots             |
// |-----------------------------------| <- bp(not defined by ABI,
// |.variable-sized.local.variables....|       LLVM chooses X19)
// |.(VLAs)............................| (size of this area is unknown at
// |...................................|  compile time)
// |-----------------------------------| <- sp
// |                                   | Lower address
//
//
// To access the data in a frame, at-compile time, a constant offset must be
// computable from one of the pointers (fp, bp, sp) to access it. The size
// of the areas with a dotted background cannot be computed at compile-time
// if they are present, making it required to have all three of fp, bp and
// sp to be set up to be able to access all contents in the frame areas,
// assuming all of the frame areas are non-empty.
//
// For most functions, some of the frame areas are empty. For those functions,
// it may not be necessary to set up fp or bp:
// * A base pointer is definitely needed when there are both VLAs and local
//   variables with more-than-default alignment requirements.
// * A frame pointer is definitely needed when there are local variables with
//   more-than-default alignment requirements.
//
// In some cases when a base pointer is not strictly needed, it is generated
// anyway when offsets from the frame pointer to access local variables become
// so large that the offset can't be encoded in the immediate fields of loads
// or stores.
//
// FIXME: also explain the redzone concept.
// FIXME: also explain the concept of reserved call frames.
//
//===----------------------------------------------------------------------===//

#include "AArch64FrameLowering.h"
#include "AArch64InstrInfo.h"
#include "AArch64MachineFunctionInfo.h"
#include "AArch64RegisterInfo.h"
#include "AArch64Subtarget.h"
#include "AArch64TargetMachine.h"
#include "MCTargetDesc/AArch64AddressingModes.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/WinEHFuncInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <cassert>
#include <cstdint>
#include <iterator>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "frame-info"

static cl::opt<bool> EnableRedZone("aarch64-redzone",
                                   cl::desc("enable use of redzone on AArch64"),
                                   cl::init(false), cl::Hidden);

static cl::opt<bool>
    ReverseCSRRestoreSeq("reverse-csr-restore-seq",
                         cl::desc("reverse the CSR restore sequence"),
                         cl::init(false), cl::Hidden);

STATISTIC(NumRedZoneFunctions, "Number of functions using red zone");

/// This is the biggest offset to the stack pointer we can encode in aarch64
/// instructions (without using a separate calculation and a temp register).
/// Note that the exception here are vector stores/loads which cannot encode any
/// displacements (see estimateRSStackSizeLimit(), isAArch64FrameOffsetLegal()).
static const unsigned DefaultSafeSPDisplacement = 255;

/// Look at each instruction that references stack frames and return the stack
/// size limit beyond which some of these instructions will require a scratch
/// register during their expansion later.
static unsigned estimateRSStackSizeLimit(MachineFunction &MF) {
  // FIXME: For now, just conservatively guestimate based on unscaled indexing
  // range. We'll end up allocating an unnecessary spill slot a lot, but
  // realistically that's not a big deal at this stage of the game.
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr() || MI.isPseudo() ||
          MI.getOpcode() == AArch64::ADDXri ||
          MI.getOpcode() == AArch64::ADDSXri)
        continue;

      for (const MachineOperand &MO : MI.operands()) {
        if (!MO.isFI())
          continue;

        int Offset = 0;
        if (isAArch64FrameOffsetLegal(MI, Offset, nullptr, nullptr, nullptr) ==
            AArch64FrameOffsetCannotUpdate)
          return 0;
      }
    }
  }
  return DefaultSafeSPDisplacement;
}

bool AArch64FrameLowering::canUseRedZone(const MachineFunction &MF) const {
  if (!EnableRedZone)
    return false;
  // Don't use the red zone if the function explicitly asks us not to.
  // This is typically used for kernel code.
  if (MF.getFunction().hasFnAttribute(Attribute::NoRedZone))
    return false;

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const AArch64FunctionInfo *AFI = MF.getInfo<AArch64FunctionInfo>();
  unsigned NumBytes = AFI->getLocalStackSize();

  return !(MFI.hasCalls() || hasFP(MF) || NumBytes > 128);
}

/// hasFP - Return true if the specified function should have a dedicated frame
/// pointer register.
bool AArch64FrameLowering::hasFP(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterInfo *RegInfo = MF.getSubtarget().getRegisterInfo();
  // Win64 EH requires a frame pointer if funclets are present, as the locals
  // are accessed off the frame pointer in both the parent function and the
  // funclets.
  if (MF.hasEHFunclets())
    return true;
  // Retain behavior of always omitting the FP for leaf functions when possible.
  if (MFI.hasCalls() && MF.getTarget().Options.DisableFramePointerElim(MF))
    return true;
  if (MFI.hasVarSizedObjects() || MFI.isFrameAddressTaken() ||
      MFI.hasStackMap() || MFI.hasPatchPoint() ||
      RegInfo->needsStackRealignment(MF))
    return true;
  // With large callframes around we may need to use FP to access the scavenging
  // emergency spillslot.
  //
  // Unfortunately some calls to hasFP() like machine verifier ->
  // getReservedReg() -> hasFP in the middle of global isel are too early
  // to know the max call frame size. Hopefully conservatively returning "true"
  // in those cases is fine.
  // DefaultSafeSPDisplacement is fine as we only emergency spill GP regs.
  if (!MFI.isMaxCallFrameSizeComputed() ||
      MFI.getMaxCallFrameSize() > DefaultSafeSPDisplacement)
    return true;

  // Win64 SEH requires frame pointer if funclets are present.
  if (MF.hasLocalEscape())
    return true;

  return false;
}

/// hasReservedCallFrame - Under normal circumstances, when a frame pointer is
/// not required, we reserve argument space for call sites in the function
/// immediately on entry to the current function.  This eliminates the need for
/// add/sub sp brackets around call sites.  Returns true if the call frame is
/// included as part of the stack frame.
bool
AArch64FrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  return !MF.getFrameInfo().hasVarSizedObjects();
}

MachineBasicBlock::iterator AArch64FrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  const AArch64InstrInfo *TII =
      static_cast<const AArch64InstrInfo *>(MF.getSubtarget().getInstrInfo());
  DebugLoc DL = I->getDebugLoc();
  unsigned Opc = I->getOpcode();
  bool IsDestroy = Opc == TII->getCallFrameDestroyOpcode();
  uint64_t CalleePopAmount = IsDestroy ? I->getOperand(1).getImm() : 0;

  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  if (!TFI->hasReservedCallFrame(MF)) {
    unsigned Align = getStackAlignment();

    int64_t Amount = I->getOperand(0).getImm();
    Amount = alignTo(Amount, Align);
    if (!IsDestroy)
      Amount = -Amount;

    // N.b. if CalleePopAmount is valid but zero (i.e. callee would pop, but it
    // doesn't have to pop anything), then the first operand will be zero too so
    // this adjustment is a no-op.
    if (CalleePopAmount == 0) {
      // FIXME: in-function stack adjustment for calls is limited to 24-bits
      // because there's no guaranteed temporary register available.
      //
      // ADD/SUB (immediate) has only LSL #0 and LSL #12 available.
      // 1) For offset <= 12-bit, we use LSL #0
      // 2) For 12-bit <= offset <= 24-bit, we use two instructions. One uses
      // LSL #0, and the other uses LSL #12.
      //
      // Most call frames will be allocated at the start of a function so
      // this is OK, but it is a limitation that needs dealing with.
      assert(Amount > -0xffffff && Amount < 0xffffff && "call frame too large");
      emitFrameOffset(MBB, I, DL, AArch64::SP, AArch64::SP, Amount, TII);
    }
  } else if (CalleePopAmount != 0) {
    // If the calling convention demands that the callee pops arguments from the
    // stack, we want to add it back if we have a reserved call frame.
    assert(CalleePopAmount < 0xffffff && "call frame too large");
    emitFrameOffset(MBB, I, DL, AArch64::SP, AArch64::SP, -CalleePopAmount,
                    TII);
  }
  return MBB.erase(I);
}

static bool ShouldSignReturnAddress(MachineFunction &MF) {
  // The function should be signed in the following situations:
  // - sign-return-address=all
  // - sign-return-address=non-leaf and the functions spills the LR

  const Function &F = MF.getFunction();
  if (!F.hasFnAttribute("sign-return-address"))
    return false;

  StringRef Scope = F.getFnAttribute("sign-return-address").getValueAsString();
  if (Scope.equals("none"))
    return false;

  if (Scope.equals("all"))
    return true;

  assert(Scope.equals("non-leaf") && "Expected all, none or non-leaf");

  for (const auto &Info : MF.getFrameInfo().getCalleeSavedInfo())
    if (Info.getReg() == AArch64::LR)
      return true;

  return false;
}

void AArch64FrameLowering::emitCalleeSavedFrameMoves(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI) const {
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetSubtargetInfo &STI = MF.getSubtarget();
  const MCRegisterInfo *MRI = STI.getRegisterInfo();
  const TargetInstrInfo *TII = STI.getInstrInfo();
  DebugLoc DL = MBB.findDebugLoc(MBBI);

  // Add callee saved registers to move list.
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
  if (CSI.empty())
    return;

  for (const auto &Info : CSI) {
    unsigned Reg = Info.getReg();
    int64_t Offset =
        MFI.getObjectOffset(Info.getFrameIdx()) - getOffsetOfLocalArea();
    unsigned DwarfReg = MRI->getDwarfRegNum(Reg, true);
    unsigned CFIIndex = MF.addFrameInst(
        MCCFIInstruction::createOffset(nullptr, DwarfReg, Offset));
    BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlags(MachineInstr::FrameSetup);
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
static unsigned findScratchNonCalleeSaveRegister(MachineBasicBlock *MBB) {
  MachineFunction *MF = MBB->getParent();

  // If MBB is an entry block, use X9 as the scratch register
  if (&MF->front() == MBB)
    return AArch64::X9;

  const AArch64Subtarget &Subtarget = MF->getSubtarget<AArch64Subtarget>();
  const AArch64RegisterInfo &TRI = *Subtarget.getRegisterInfo();
  LivePhysRegs LiveRegs(TRI);
  LiveRegs.addLiveIns(*MBB);

  // Mark callee saved registers as used so we will not choose them.
  const MCPhysReg *CSRegs = MF->getRegInfo().getCalleeSavedRegs();
  for (unsigned i = 0; CSRegs[i]; ++i)
    LiveRegs.addReg(CSRegs[i]);

  // Prefer X9 since it was historically used for the prologue scratch reg.
  const MachineRegisterInfo &MRI = MF->getRegInfo();
  if (LiveRegs.available(MRI, AArch64::X9))
    return AArch64::X9;

  for (unsigned Reg : AArch64::GPR64RegClass) {
    if (LiveRegs.available(MRI, Reg))
      return Reg;
  }
  return AArch64::NoRegister;
}

bool AArch64FrameLowering::canUseAsPrologue(
    const MachineBasicBlock &MBB) const {
  const MachineFunction *MF = MBB.getParent();
  MachineBasicBlock *TmpMBB = const_cast<MachineBasicBlock *>(&MBB);
  const AArch64Subtarget &Subtarget = MF->getSubtarget<AArch64Subtarget>();
  const AArch64RegisterInfo *RegInfo = Subtarget.getRegisterInfo();

  // Don't need a scratch register if we're not going to re-align the stack.
  if (!RegInfo->needsStackRealignment(*MF))
    return true;
  // Otherwise, we can use any block as long as it has a scratch register
  // available.
  return findScratchNonCalleeSaveRegister(TmpMBB) != AArch64::NoRegister;
}

static bool windowsRequiresStackProbe(MachineFunction &MF,
                                      unsigned StackSizeInBytes) {
  const AArch64Subtarget &Subtarget = MF.getSubtarget<AArch64Subtarget>();
  if (!Subtarget.isTargetWindows())
    return false;
  const Function &F = MF.getFunction();
  // TODO: When implementing stack protectors, take that into account
  // for the probe threshold.
  unsigned StackProbeSize = 4096;
  if (F.hasFnAttribute("stack-probe-size"))
    F.getFnAttribute("stack-probe-size")
        .getValueAsString()
        .getAsInteger(0, StackProbeSize);
  return (StackSizeInBytes >= StackProbeSize) &&
         !F.hasFnAttribute("no-stack-arg-probe");
}

bool AArch64FrameLowering::shouldCombineCSRLocalStackBump(
    MachineFunction &MF, unsigned StackBumpBytes) const {
  AArch64FunctionInfo *AFI = MF.getInfo<AArch64FunctionInfo>();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const AArch64Subtarget &Subtarget = MF.getSubtarget<AArch64Subtarget>();
  const AArch64RegisterInfo *RegInfo = Subtarget.getRegisterInfo();

  if (AFI->getLocalStackSize() == 0)
    return false;

  // 512 is the maximum immediate for stp/ldp that will be used for
  // callee-save save/restores
  if (StackBumpBytes >= 512 || windowsRequiresStackProbe(MF, StackBumpBytes))
    return false;

  if (MFI.hasVarSizedObjects())
    return false;

  if (RegInfo->needsStackRealignment(MF))
    return false;

  // This isn't strictly necessary, but it simplifies things a bit since the
  // current RedZone handling code assumes the SP is adjusted by the
  // callee-save save/restore code.
  if (canUseRedZone(MF))
    return false;

  return true;
}

// Given a load or a store instruction, generate an appropriate unwinding SEH
// code on Windows.
static MachineBasicBlock::iterator InsertSEH(MachineBasicBlock::iterator MBBI,
                                             const TargetInstrInfo &TII,
                                             MachineInstr::MIFlag Flag) {
  unsigned Opc = MBBI->getOpcode();
  MachineBasicBlock *MBB = MBBI->getParent();
  MachineFunction &MF = *MBB->getParent();
  DebugLoc DL = MBBI->getDebugLoc();
  unsigned ImmIdx = MBBI->getNumOperands() - 1;
  int Imm = MBBI->getOperand(ImmIdx).getImm();
  MachineInstrBuilder MIB;
  const AArch64Subtarget &Subtarget = MF.getSubtarget<AArch64Subtarget>();
  const AArch64RegisterInfo *RegInfo = Subtarget.getRegisterInfo();

  switch (Opc) {
  default:
    llvm_unreachable("No SEH Opcode for this instruction");
  case AArch64::LDPDpost:
    Imm = -Imm;
    LLVM_FALLTHROUGH;
  case AArch64::STPDpre: {
    unsigned Reg0 = RegInfo->getSEHRegNum(MBBI->getOperand(1).getReg());
    unsigned Reg1 = RegInfo->getSEHRegNum(MBBI->getOperand(2).getReg());
    MIB = BuildMI(MF, DL, TII.get(AArch64::SEH_SaveFRegP_X))
              .addImm(Reg0)
              .addImm(Reg1)
              .addImm(Imm * 8)
              .setMIFlag(Flag);
    break;
  }
  case AArch64::LDPXpost:
    Imm = -Imm;
    LLVM_FALLTHROUGH;
  case AArch64::STPXpre: {
    unsigned Reg0 = MBBI->getOperand(1).getReg();
    unsigned Reg1 = MBBI->getOperand(2).getReg();
    if (Reg0 == AArch64::FP && Reg1 == AArch64::LR)
      MIB = BuildMI(MF, DL, TII.get(AArch64::SEH_SaveFPLR_X))
                .addImm(Imm * 8)
                .setMIFlag(Flag);
    else
      MIB = BuildMI(MF, DL, TII.get(AArch64::SEH_SaveRegP_X))
                .addImm(RegInfo->getSEHRegNum(Reg0))
                .addImm(RegInfo->getSEHRegNum(Reg1))
                .addImm(Imm * 8)
                .setMIFlag(Flag);
    break;
  }
  case AArch64::LDRDpost:
    Imm = -Imm;
    LLVM_FALLTHROUGH;
  case AArch64::STRDpre: {
    unsigned Reg = RegInfo->getSEHRegNum(MBBI->getOperand(1).getReg());
    MIB = BuildMI(MF, DL, TII.get(AArch64::SEH_SaveFReg_X))
              .addImm(Reg)
              .addImm(Imm)
              .setMIFlag(Flag);
    break;
  }
  case AArch64::LDRXpost:
    Imm = -Imm;
    LLVM_FALLTHROUGH;
  case AArch64::STRXpre: {
    unsigned Reg =  RegInfo->getSEHRegNum(MBBI->getOperand(1).getReg());
    MIB = BuildMI(MF, DL, TII.get(AArch64::SEH_SaveReg_X))
              .addImm(Reg)
              .addImm(Imm)
              .setMIFlag(Flag);
    break;
  }
  case AArch64::STPDi:
  case AArch64::LDPDi: {
    unsigned Reg0 =  RegInfo->getSEHRegNum(MBBI->getOperand(0).getReg());
    unsigned Reg1 =  RegInfo->getSEHRegNum(MBBI->getOperand(1).getReg());
    MIB = BuildMI(MF, DL, TII.get(AArch64::SEH_SaveFRegP))
              .addImm(Reg0)
              .addImm(Reg1)
              .addImm(Imm * 8)
              .setMIFlag(Flag);
    break;
  }
  case AArch64::STPXi:
  case AArch64::LDPXi: {
    unsigned Reg0 = MBBI->getOperand(0).getReg();
    unsigned Reg1 = MBBI->getOperand(1).getReg();
    if (Reg0 == AArch64::FP && Reg1 == AArch64::LR)
      MIB = BuildMI(MF, DL, TII.get(AArch64::SEH_SaveFPLR))
                .addImm(Imm * 8)
                .setMIFlag(Flag);
    else
      MIB = BuildMI(MF, DL, TII.get(AArch64::SEH_SaveRegP))
                .addImm(RegInfo->getSEHRegNum(Reg0))
                .addImm(RegInfo->getSEHRegNum(Reg1))
                .addImm(Imm * 8)
                .setMIFlag(Flag);
    break;
  }
  case AArch64::STRXui:
  case AArch64::LDRXui: {
    int Reg = RegInfo->getSEHRegNum(MBBI->getOperand(0).getReg());
    MIB = BuildMI(MF, DL, TII.get(AArch64::SEH_SaveReg))
              .addImm(Reg)
              .addImm(Imm * 8)
              .setMIFlag(Flag);
    break;
  }
  case AArch64::STRDui:
  case AArch64::LDRDui: {
    unsigned Reg = RegInfo->getSEHRegNum(MBBI->getOperand(0).getReg());
    MIB = BuildMI(MF, DL, TII.get(AArch64::SEH_SaveFReg))
              .addImm(Reg)
              .addImm(Imm * 8)
              .setMIFlag(Flag);
    break;
  }
  }
  auto I = MBB->insertAfter(MBBI, MIB);
  return I;
}

// Fix up the SEH opcode associated with the save/restore instruction.
static void fixupSEHOpcode(MachineBasicBlock::iterator MBBI,
                           unsigned LocalStackSize) {
  MachineOperand *ImmOpnd = nullptr;
  unsigned ImmIdx = MBBI->getNumOperands() - 1;
  switch (MBBI->getOpcode()) {
  default:
    llvm_unreachable("Fix the offset in the SEH instruction");
  case AArch64::SEH_SaveFPLR:
  case AArch64::SEH_SaveRegP:
  case AArch64::SEH_SaveReg:
  case AArch64::SEH_SaveFRegP:
  case AArch64::SEH_SaveFReg:
    ImmOpnd = &MBBI->getOperand(ImmIdx);
    break;
  }
  if (ImmOpnd)
    ImmOpnd->setImm(ImmOpnd->getImm() + LocalStackSize);
}

// Convert callee-save register save/restore instruction to do stack pointer
// decrement/increment to allocate/deallocate the callee-save stack area by
// converting store/load to use pre/post increment version.
static MachineBasicBlock::iterator convertCalleeSaveRestoreToSPPrePostIncDec(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    const DebugLoc &DL, const TargetInstrInfo *TII, int CSStackSizeInc,
    bool NeedsWinCFI, bool InProlog = true) {
  // Ignore instructions that do not operate on SP, i.e. shadow call stack
  // instructions and associated CFI instruction.
  while (MBBI->getOpcode() == AArch64::STRXpost ||
         MBBI->getOpcode() == AArch64::LDRXpre ||
         MBBI->getOpcode() == AArch64::CFI_INSTRUCTION) {
    if (MBBI->getOpcode() != AArch64::CFI_INSTRUCTION)
      assert(MBBI->getOperand(0).getReg() != AArch64::SP);
    ++MBBI;
  }
  unsigned NewOpc;
  int Scale = 1;
  switch (MBBI->getOpcode()) {
  default:
    llvm_unreachable("Unexpected callee-save save/restore opcode!");
  case AArch64::STPXi:
    NewOpc = AArch64::STPXpre;
    Scale = 8;
    break;
  case AArch64::STPDi:
    NewOpc = AArch64::STPDpre;
    Scale = 8;
    break;
  case AArch64::STPQi:
    NewOpc = AArch64::STPQpre;
    Scale = 16;
    break;
  case AArch64::STRXui:
    NewOpc = AArch64::STRXpre;
    break;
  case AArch64::STRDui:
    NewOpc = AArch64::STRDpre;
    break;
  case AArch64::STRQui:
    NewOpc = AArch64::STRQpre;
    break;
  case AArch64::LDPXi:
    NewOpc = AArch64::LDPXpost;
    Scale = 8;
    break;
  case AArch64::LDPDi:
    NewOpc = AArch64::LDPDpost;
    Scale = 8;
    break;
  case AArch64::LDPQi:
    NewOpc = AArch64::LDPQpost;
    Scale = 16;
    break;
  case AArch64::LDRXui:
    NewOpc = AArch64::LDRXpost;
    break;
  case AArch64::LDRDui:
    NewOpc = AArch64::LDRDpost;
    break;
  case AArch64::LDRQui:
    NewOpc = AArch64::LDRQpost;
    break;
  }
  // Get rid of the SEH code associated with the old instruction.
  if (NeedsWinCFI) {
    auto SEH = std::next(MBBI);
    if (AArch64InstrInfo::isSEHInstruction(*SEH))
      SEH->eraseFromParent();
  }

  MachineInstrBuilder MIB = BuildMI(MBB, MBBI, DL, TII->get(NewOpc));
  MIB.addReg(AArch64::SP, RegState::Define);

  // Copy all operands other than the immediate offset.
  unsigned OpndIdx = 0;
  for (unsigned OpndEnd = MBBI->getNumOperands() - 1; OpndIdx < OpndEnd;
       ++OpndIdx)
    MIB.add(MBBI->getOperand(OpndIdx));

  assert(MBBI->getOperand(OpndIdx).getImm() == 0 &&
         "Unexpected immediate offset in first/last callee-save save/restore "
         "instruction!");
  assert(MBBI->getOperand(OpndIdx - 1).getReg() == AArch64::SP &&
         "Unexpected base register in callee-save save/restore instruction!");
  assert(CSStackSizeInc % Scale == 0);
  MIB.addImm(CSStackSizeInc / Scale);

  MIB.setMIFlags(MBBI->getFlags());
  MIB.setMemRefs(MBBI->memoperands());

  // Generate a new SEH code that corresponds to the new instruction.
  if (NeedsWinCFI)
    InsertSEH(*MIB, *TII,
              InProlog ? MachineInstr::FrameSetup : MachineInstr::FrameDestroy);

  return std::prev(MBB.erase(MBBI));
}

// Fixup callee-save register save/restore instructions to take into account
// combined SP bump by adding the local stack size to the stack offsets.
static void fixupCalleeSaveRestoreStackOffset(MachineInstr &MI,
                                              unsigned LocalStackSize,
                                              bool NeedsWinCFI) {
  if (AArch64InstrInfo::isSEHInstruction(MI))
    return;

  unsigned Opc = MI.getOpcode();

  // Ignore instructions that do not operate on SP, i.e. shadow call stack
  // instructions and associated CFI instruction.
  if (Opc == AArch64::STRXpost || Opc == AArch64::LDRXpre ||
      Opc == AArch64::CFI_INSTRUCTION) {
    if (Opc != AArch64::CFI_INSTRUCTION)
      assert(MI.getOperand(0).getReg() != AArch64::SP);
    return;
  }

  unsigned Scale;
  switch (Opc) {
  case AArch64::STPXi:
  case AArch64::STRXui:
  case AArch64::STPDi:
  case AArch64::STRDui:
  case AArch64::LDPXi:
  case AArch64::LDRXui:
  case AArch64::LDPDi:
  case AArch64::LDRDui:
    Scale = 8;
    break;
  case AArch64::STPQi:
  case AArch64::STRQui:
  case AArch64::LDPQi:
  case AArch64::LDRQui:
    Scale = 16;
    break;
  default:
    llvm_unreachable("Unexpected callee-save save/restore opcode!");
  }

  unsigned OffsetIdx = MI.getNumExplicitOperands() - 1;
  assert(MI.getOperand(OffsetIdx - 1).getReg() == AArch64::SP &&
         "Unexpected base register in callee-save save/restore instruction!");
  // Last operand is immediate offset that needs fixing.
  MachineOperand &OffsetOpnd = MI.getOperand(OffsetIdx);
  // All generated opcodes have scaled offsets.
  assert(LocalStackSize % Scale == 0);
  OffsetOpnd.setImm(OffsetOpnd.getImm() + LocalStackSize / Scale);

  if (NeedsWinCFI) {
    auto MBBI = std::next(MachineBasicBlock::iterator(MI));
    assert(MBBI != MI.getParent()->end() && "Expecting a valid instruction");
    assert(AArch64InstrInfo::isSEHInstruction(*MBBI) &&
           "Expecting a SEH instruction");
    fixupSEHOpcode(MBBI, LocalStackSize);
  }
}

static void adaptForLdStOpt(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator FirstSPPopI,
                            MachineBasicBlock::iterator LastPopI) {
  // Sometimes (when we restore in the same order as we save), we can end up
  // with code like this:
  //
  // ldp      x26, x25, [sp]
  // ldp      x24, x23, [sp, #16]
  // ldp      x22, x21, [sp, #32]
  // ldp      x20, x19, [sp, #48]
  // add      sp, sp, #64
  //
  // In this case, it is always better to put the first ldp at the end, so
  // that the load-store optimizer can run and merge the ldp and the add into
  // a post-index ldp.
  // If we managed to grab the first pop instruction, move it to the end.
  if (ReverseCSRRestoreSeq)
    MBB.splice(FirstSPPopI, &MBB, LastPopI);
  // We should end up with something like this now:
  //
  // ldp      x24, x23, [sp, #16]
  // ldp      x22, x21, [sp, #32]
  // ldp      x20, x19, [sp, #48]
  // ldp      x26, x25, [sp]
  // add      sp, sp, #64
  //
  // and the load-store optimizer can merge the last two instructions into:
  //
  // ldp      x26, x25, [sp], #64
  //
}

static bool ShouldSignWithAKey(MachineFunction &MF) {
  const Function &F = MF.getFunction();
  if (!F.hasFnAttribute("sign-return-address-key"))
    return true;

  const StringRef Key =
      F.getFnAttribute("sign-return-address-key").getValueAsString();
  assert(Key.equals_lower("a_key") || Key.equals_lower("b_key"));
  return Key.equals_lower("a_key");
}

static bool needsWinCFI(const MachineFunction &MF) {
  const Function &F = MF.getFunction();
  return MF.getTarget().getMCAsmInfo()->usesWindowsCFI() &&
         F.needsUnwindTableEntry();
}

void AArch64FrameLowering::emitPrologue(MachineFunction &MF,
                                        MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.begin();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const Function &F = MF.getFunction();
  const AArch64Subtarget &Subtarget = MF.getSubtarget<AArch64Subtarget>();
  const AArch64RegisterInfo *RegInfo = Subtarget.getRegisterInfo();
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  MachineModuleInfo &MMI = MF.getMMI();
  AArch64FunctionInfo *AFI = MF.getInfo<AArch64FunctionInfo>();
  bool needsFrameMoves = (MMI.hasDebugInfo() || F.needsUnwindTableEntry()) &&
                         !MF.getTarget().getMCAsmInfo()->usesWindowsCFI();
  bool HasFP = hasFP(MF);
  bool NeedsWinCFI = needsWinCFI(MF);
  MF.setHasWinCFI(NeedsWinCFI);
  bool IsFunclet = MBB.isEHFuncletEntry();

  // At this point, we're going to decide whether or not the function uses a
  // redzone. In most cases, the function doesn't have a redzone so let's
  // assume that's false and set it to true in the case that there's a redzone.
  AFI->setHasRedZone(false);

  // Debug location must be unknown since the first debug location is used
  // to determine the end of the prologue.
  DebugLoc DL;

  if (ShouldSignReturnAddress(MF)) {
    if (ShouldSignWithAKey(MF))
      BuildMI(MBB, MBBI, DL, TII->get(AArch64::PACIASP))
          .setMIFlag(MachineInstr::FrameSetup);
    else {
      BuildMI(MBB, MBBI, DL, TII->get(AArch64::EMITBKEY))
          .setMIFlag(MachineInstr::FrameSetup);
      BuildMI(MBB, MBBI, DL, TII->get(AArch64::PACIBSP))
          .setMIFlag(MachineInstr::FrameSetup);
    }

    unsigned CFIIndex =
        MF.addFrameInst(MCCFIInstruction::createNegateRAState(nullptr));
    BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlags(MachineInstr::FrameSetup);
  }

  // All calls are tail calls in GHC calling conv, and functions have no
  // prologue/epilogue.
  if (MF.getFunction().getCallingConv() == CallingConv::GHC)
    return;

  // getStackSize() includes all the locals in its size calculation. We don't
  // include these locals when computing the stack size of a funclet, as they
  // are allocated in the parent's stack frame and accessed via the frame
  // pointer from the funclet.  We only save the callee saved registers in the
  // funclet, which are really the callee saved registers of the parent
  // function, including the funclet.
  int NumBytes = IsFunclet ? (int)getWinEHFuncletFrameSize(MF)
                           : (int)MFI.getStackSize();
  if (!AFI->hasStackFrame() && !windowsRequiresStackProbe(MF, NumBytes)) {
    assert(!HasFP && "unexpected function without stack frame but with FP");
    // All of the stack allocation is for locals.
    AFI->setLocalStackSize(NumBytes);
    if (!NumBytes)
      return;
    // REDZONE: If the stack size is less than 128 bytes, we don't need
    // to actually allocate.
    if (canUseRedZone(MF)) {
      AFI->setHasRedZone(true);
      ++NumRedZoneFunctions;
    } else {
      emitFrameOffset(MBB, MBBI, DL, AArch64::SP, AArch64::SP, -NumBytes, TII,
                      MachineInstr::FrameSetup, false, NeedsWinCFI);
      if (!NeedsWinCFI) {
        // Label used to tie together the PROLOG_LABEL and the MachineMoves.
        MCSymbol *FrameLabel = MMI.getContext().createTempSymbol();
        // Encode the stack size of the leaf function.
        unsigned CFIIndex = MF.addFrameInst(
            MCCFIInstruction::createDefCfaOffset(FrameLabel, -NumBytes));
        BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(CFIIndex)
            .setMIFlags(MachineInstr::FrameSetup);
      }
    }

    if (NeedsWinCFI)
      BuildMI(MBB, MBBI, DL, TII->get(AArch64::SEH_PrologEnd))
          .setMIFlag(MachineInstr::FrameSetup);

    return;
  }

  bool IsWin64 =
      Subtarget.isCallingConvWin64(MF.getFunction().getCallingConv());
  // Var args are accounted for in the containing function, so don't
  // include them for funclets.
  unsigned FixedObject = (IsWin64 && !IsFunclet) ?
                         alignTo(AFI->getVarArgsGPRSize(), 16) : 0;

  auto PrologueSaveSize = AFI->getCalleeSavedStackSize() + FixedObject;
  // All of the remaining stack allocations are for locals.
  AFI->setLocalStackSize(NumBytes - PrologueSaveSize);
  bool CombineSPBump = shouldCombineCSRLocalStackBump(MF, NumBytes);
  if (CombineSPBump) {
    emitFrameOffset(MBB, MBBI, DL, AArch64::SP, AArch64::SP, -NumBytes, TII,
                    MachineInstr::FrameSetup, false, NeedsWinCFI);
    NumBytes = 0;
  } else if (PrologueSaveSize != 0) {
    MBBI = convertCalleeSaveRestoreToSPPrePostIncDec(
        MBB, MBBI, DL, TII, -PrologueSaveSize, NeedsWinCFI);
    NumBytes -= PrologueSaveSize;
  }
  assert(NumBytes >= 0 && "Negative stack allocation size!?");

  // Move past the saves of the callee-saved registers, fixing up the offsets
  // and pre-inc if we decided to combine the callee-save and local stack
  // pointer bump above.
  MachineBasicBlock::iterator End = MBB.end();
  while (MBBI != End && MBBI->getFlag(MachineInstr::FrameSetup)) {
    if (CombineSPBump)
      fixupCalleeSaveRestoreStackOffset(*MBBI, AFI->getLocalStackSize(),
                                        NeedsWinCFI);
    ++MBBI;
  }

  // The code below is not applicable to funclets. We have emitted all the SEH
  // opcodes that we needed to emit.  The FP and BP belong to the containing
  // function.
  if (IsFunclet) {
    if (NeedsWinCFI)
      BuildMI(MBB, MBBI, DL, TII->get(AArch64::SEH_PrologEnd))
          .setMIFlag(MachineInstr::FrameSetup);
    return;
  }

  if (HasFP) {
    // Only set up FP if we actually need to. Frame pointer is fp =
    // sp - fixedobject - 16.
    int FPOffset = AFI->getCalleeSavedStackSize() - 16;
    if (CombineSPBump)
      FPOffset += AFI->getLocalStackSize();

    // Issue    sub fp, sp, FPOffset or
    //          mov fp,sp          when FPOffset is zero.
    // Note: All stores of callee-saved registers are marked as "FrameSetup".
    // This code marks the instruction(s) that set the FP also.
    emitFrameOffset(MBB, MBBI, DL, AArch64::FP, AArch64::SP, FPOffset, TII,
                    MachineInstr::FrameSetup, false, NeedsWinCFI);
  }

  if (windowsRequiresStackProbe(MF, NumBytes)) {
    uint32_t NumWords = NumBytes >> 4;
    if (NeedsWinCFI) {
      // alloc_l can hold at most 256MB, so assume that NumBytes doesn't
      // exceed this amount.  We need to move at most 2^24 - 1 into x15.
      // This is at most two instructions, MOVZ follwed by MOVK.
      // TODO: Fix to use multiple stack alloc unwind codes for stacks
      // exceeding 256MB in size.
      if (NumBytes >= (1 << 28))
        report_fatal_error("Stack size cannot exceed 256MB for stack "
                            "unwinding purposes");

      uint32_t LowNumWords = NumWords & 0xFFFF;
      BuildMI(MBB, MBBI, DL, TII->get(AArch64::MOVZXi), AArch64::X15)
            .addImm(LowNumWords)
            .addImm(AArch64_AM::getShifterImm(AArch64_AM::LSL, 0))
            .setMIFlag(MachineInstr::FrameSetup);
      BuildMI(MBB, MBBI, DL, TII->get(AArch64::SEH_Nop))
            .setMIFlag(MachineInstr::FrameSetup);
      if ((NumWords & 0xFFFF0000) != 0) {
          BuildMI(MBB, MBBI, DL, TII->get(AArch64::MOVKXi), AArch64::X15)
              .addReg(AArch64::X15)
              .addImm((NumWords & 0xFFFF0000) >> 16) // High half
              .addImm(AArch64_AM::getShifterImm(AArch64_AM::LSL, 16))
              .setMIFlag(MachineInstr::FrameSetup);
          BuildMI(MBB, MBBI, DL, TII->get(AArch64::SEH_Nop))
            .setMIFlag(MachineInstr::FrameSetup);
      }
    } else {
      BuildMI(MBB, MBBI, DL, TII->get(AArch64::MOVi64imm), AArch64::X15)
          .addImm(NumWords)
          .setMIFlags(MachineInstr::FrameSetup);
    }

    switch (MF.getTarget().getCodeModel()) {
    case CodeModel::Tiny:
    case CodeModel::Small:
    case CodeModel::Medium:
    case CodeModel::Kernel:
      BuildMI(MBB, MBBI, DL, TII->get(AArch64::BL))
          .addExternalSymbol("__chkstk")
          .addReg(AArch64::X15, RegState::Implicit)
          .addReg(AArch64::X16, RegState::Implicit | RegState::Define | RegState::Dead)
          .addReg(AArch64::X17, RegState::Implicit | RegState::Define | RegState::Dead)
          .addReg(AArch64::NZCV, RegState::Implicit | RegState::Define | RegState::Dead)
          .setMIFlags(MachineInstr::FrameSetup);
      if (NeedsWinCFI)
        BuildMI(MBB, MBBI, DL, TII->get(AArch64::SEH_Nop))
            .setMIFlag(MachineInstr::FrameSetup);
      break;
    case CodeModel::Large:
      BuildMI(MBB, MBBI, DL, TII->get(AArch64::MOVaddrEXT))
          .addReg(AArch64::X16, RegState::Define)
          .addExternalSymbol("__chkstk")
          .addExternalSymbol("__chkstk")
          .setMIFlags(MachineInstr::FrameSetup);
      if (NeedsWinCFI)
        BuildMI(MBB, MBBI, DL, TII->get(AArch64::SEH_Nop))
            .setMIFlag(MachineInstr::FrameSetup);

      BuildMI(MBB, MBBI, DL, TII->get(AArch64::BLR))
          .addReg(AArch64::X16, RegState::Kill)
          .addReg(AArch64::X15, RegState::Implicit | RegState::Define)
          .addReg(AArch64::X16, RegState::Implicit | RegState::Define | RegState::Dead)
          .addReg(AArch64::X17, RegState::Implicit | RegState::Define | RegState::Dead)
          .addReg(AArch64::NZCV, RegState::Implicit | RegState::Define | RegState::Dead)
          .setMIFlags(MachineInstr::FrameSetup);
      if (NeedsWinCFI)
        BuildMI(MBB, MBBI, DL, TII->get(AArch64::SEH_Nop))
            .setMIFlag(MachineInstr::FrameSetup);
      break;
    }

    BuildMI(MBB, MBBI, DL, TII->get(AArch64::SUBXrx64), AArch64::SP)
        .addReg(AArch64::SP, RegState::Kill)
        .addReg(AArch64::X15, RegState::Kill)
        .addImm(AArch64_AM::getArithExtendImm(AArch64_AM::UXTX, 4))
        .setMIFlags(MachineInstr::FrameSetup);
    if (NeedsWinCFI)
       BuildMI(MBB, MBBI, DL, TII->get(AArch64::SEH_StackAlloc))
            .addImm(NumBytes)
            .setMIFlag(MachineInstr::FrameSetup);
    NumBytes = 0;
  }

  // Allocate space for the rest of the frame.
  if (NumBytes) {
    const bool NeedsRealignment = RegInfo->needsStackRealignment(MF);
    unsigned scratchSPReg = AArch64::SP;

    if (NeedsRealignment) {
      scratchSPReg = findScratchNonCalleeSaveRegister(&MBB);
      assert(scratchSPReg != AArch64::NoRegister);
    }

    // If we're a leaf function, try using the red zone.
    if (!canUseRedZone(MF))
      // FIXME: in the case of dynamic re-alignment, NumBytes doesn't have
      // the correct value here, as NumBytes also includes padding bytes,
      // which shouldn't be counted here.
      emitFrameOffset(MBB, MBBI, DL, scratchSPReg, AArch64::SP, -NumBytes, TII,
                      MachineInstr::FrameSetup, false, NeedsWinCFI);

    if (NeedsRealignment) {
      const unsigned Alignment = MFI.getMaxAlignment();
      const unsigned NrBitsToZero = countTrailingZeros(Alignment);
      assert(NrBitsToZero > 1);
      assert(scratchSPReg != AArch64::SP);

      // SUB X9, SP, NumBytes
      //   -- X9 is temporary register, so shouldn't contain any live data here,
      //   -- free to use. This is already produced by emitFrameOffset above.
      // AND SP, X9, 0b11111...0000
      // The logical immediates have a non-trivial encoding. The following
      // formula computes the encoded immediate with all ones but
      // NrBitsToZero zero bits as least significant bits.
      uint32_t andMaskEncoded = (1 << 12)                         // = N
                                | ((64 - NrBitsToZero) << 6)      // immr
                                | ((64 - NrBitsToZero - 1) << 0); // imms

      BuildMI(MBB, MBBI, DL, TII->get(AArch64::ANDXri), AArch64::SP)
          .addReg(scratchSPReg, RegState::Kill)
          .addImm(andMaskEncoded);
      AFI->setStackRealigned(true);
      if (NeedsWinCFI)
        BuildMI(MBB, MBBI, DL, TII->get(AArch64::SEH_StackAlloc))
            .addImm(NumBytes & andMaskEncoded)
            .setMIFlag(MachineInstr::FrameSetup);
    }
  }

  // If we need a base pointer, set it up here. It's whatever the value of the
  // stack pointer is at this point. Any variable size objects will be allocated
  // after this, so we can still use the base pointer to reference locals.
  //
  // FIXME: Clarify FrameSetup flags here.
  // Note: Use emitFrameOffset() like above for FP if the FrameSetup flag is
  // needed.
  if (RegInfo->hasBasePointer(MF)) {
    TII->copyPhysReg(MBB, MBBI, DL, RegInfo->getBaseRegister(), AArch64::SP,
                     false);
    if (NeedsWinCFI)
      BuildMI(MBB, MBBI, DL, TII->get(AArch64::SEH_Nop))
          .setMIFlag(MachineInstr::FrameSetup);
  }

  // The very last FrameSetup instruction indicates the end of prologue. Emit a
  // SEH opcode indicating the prologue end.
  if (NeedsWinCFI)
    BuildMI(MBB, MBBI, DL, TII->get(AArch64::SEH_PrologEnd))
        .setMIFlag(MachineInstr::FrameSetup);

  if (needsFrameMoves) {
    const DataLayout &TD = MF.getDataLayout();
    const int StackGrowth = -TD.getPointerSize(0);
    unsigned FramePtr = RegInfo->getFrameRegister(MF);
    // An example of the prologue:
    //
    //     .globl __foo
    //     .align 2
    //  __foo:
    // Ltmp0:
    //     .cfi_startproc
    //     .cfi_personality 155, ___gxx_personality_v0
    // Leh_func_begin:
    //     .cfi_lsda 16, Lexception33
    //
    //     stp  xa,bx, [sp, -#offset]!
    //     ...
    //     stp  x28, x27, [sp, #offset-32]
    //     stp  fp, lr, [sp, #offset-16]
    //     add  fp, sp, #offset - 16
    //     sub  sp, sp, #1360
    //
    // The Stack:
    //       +-------------------------------------------+
    // 10000 | ........ | ........ | ........ | ........ |
    // 10004 | ........ | ........ | ........ | ........ |
    //       +-------------------------------------------+
    // 10008 | ........ | ........ | ........ | ........ |
    // 1000c | ........ | ........ | ........ | ........ |
    //       +===========================================+
    // 10010 |                X28 Register               |
    // 10014 |                X28 Register               |
    //       +-------------------------------------------+
    // 10018 |                X27 Register               |
    // 1001c |                X27 Register               |
    //       +===========================================+
    // 10020 |                Frame Pointer              |
    // 10024 |                Frame Pointer              |
    //       +-------------------------------------------+
    // 10028 |                Link Register              |
    // 1002c |                Link Register              |
    //       +===========================================+
    // 10030 | ........ | ........ | ........ | ........ |
    // 10034 | ........ | ........ | ........ | ........ |
    //       +-------------------------------------------+
    // 10038 | ........ | ........ | ........ | ........ |
    // 1003c | ........ | ........ | ........ | ........ |
    //       +-------------------------------------------+
    //
    //     [sp] = 10030        ::    >>initial value<<
    //     sp = 10020          ::  stp fp, lr, [sp, #-16]!
    //     fp = sp == 10020    ::  mov fp, sp
    //     [sp] == 10020       ::  stp x28, x27, [sp, #-16]!
    //     sp == 10010         ::    >>final value<<
    //
    // The frame pointer (w29) points to address 10020. If we use an offset of
    // '16' from 'w29', we get the CFI offsets of -8 for w30, -16 for w29, -24
    // for w27, and -32 for w28:
    //
    //  Ltmp1:
    //     .cfi_def_cfa w29, 16
    //  Ltmp2:
    //     .cfi_offset w30, -8
    //  Ltmp3:
    //     .cfi_offset w29, -16
    //  Ltmp4:
    //     .cfi_offset w27, -24
    //  Ltmp5:
    //     .cfi_offset w28, -32

    if (HasFP) {
      // Define the current CFA rule to use the provided FP.
      unsigned Reg = RegInfo->getDwarfRegNum(FramePtr, true);
      unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createDefCfa(
          nullptr, Reg, 2 * StackGrowth - FixedObject));
      BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlags(MachineInstr::FrameSetup);
    } else {
      // Encode the stack size of the leaf function.
      unsigned CFIIndex = MF.addFrameInst(
          MCCFIInstruction::createDefCfaOffset(nullptr, -MFI.getStackSize()));
      BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlags(MachineInstr::FrameSetup);
    }

    // Now emit the moves for whatever callee saved regs we have (including FP,
    // LR if those are saved).
    emitCalleeSavedFrameMoves(MBB, MBBI);
  }
}

static void InsertReturnAddressAuth(MachineFunction &MF,
                                    MachineBasicBlock &MBB) {
  if (!ShouldSignReturnAddress(MF))
    return;
  const AArch64Subtarget &Subtarget = MF.getSubtarget<AArch64Subtarget>();
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();

  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  DebugLoc DL;
  if (MBBI != MBB.end())
    DL = MBBI->getDebugLoc();

  // The AUTIASP instruction assembles to a hint instruction before v8.3a so
  // this instruction can safely used for any v8a architecture.
  // From v8.3a onwards there are optimised authenticate LR and return
  // instructions, namely RETA{A,B}, that can be used instead.
  if (Subtarget.hasV8_3aOps() && MBBI != MBB.end() &&
      MBBI->getOpcode() == AArch64::RET_ReallyLR) {
    BuildMI(MBB, MBBI, DL,
            TII->get(ShouldSignWithAKey(MF) ? AArch64::RETAA : AArch64::RETAB))
        .copyImplicitOps(*MBBI);
    MBB.erase(MBBI);
  } else {
    BuildMI(
        MBB, MBBI, DL,
        TII->get(ShouldSignWithAKey(MF) ? AArch64::AUTIASP : AArch64::AUTIBSP))
        .setMIFlag(MachineInstr::FrameDestroy);
  }
}

static bool isFuncletReturnInstr(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case AArch64::CATCHRET:
  case AArch64::CLEANUPRET:
    return true;
  }
}

void AArch64FrameLowering::emitEpilogue(MachineFunction &MF,
                                        MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const AArch64Subtarget &Subtarget = MF.getSubtarget<AArch64Subtarget>();
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  DebugLoc DL;
  bool IsTailCallReturn = false;
  bool NeedsWinCFI = needsWinCFI(MF);
  bool IsFunclet = false;

  if (MBB.end() != MBBI) {
    DL = MBBI->getDebugLoc();
    unsigned RetOpcode = MBBI->getOpcode();
    IsTailCallReturn = RetOpcode == AArch64::TCRETURNdi ||
                       RetOpcode == AArch64::TCRETURNri ||
                       RetOpcode == AArch64::TCRETURNriBTI;
    IsFunclet = isFuncletReturnInstr(*MBBI);
  }

  int NumBytes = IsFunclet ? (int)getWinEHFuncletFrameSize(MF)
                           : MFI.getStackSize();
  AArch64FunctionInfo *AFI = MF.getInfo<AArch64FunctionInfo>();

  // All calls are tail calls in GHC calling conv, and functions have no
  // prologue/epilogue.
  if (MF.getFunction().getCallingConv() == CallingConv::GHC)
    return;

  // Initial and residual are named for consistency with the prologue. Note that
  // in the epilogue, the residual adjustment is executed first.
  uint64_t ArgumentPopSize = 0;
  if (IsTailCallReturn) {
    MachineOperand &StackAdjust = MBBI->getOperand(1);

    // For a tail-call in a callee-pops-arguments environment, some or all of
    // the stack may actually be in use for the call's arguments, this is
    // calculated during LowerCall and consumed here...
    ArgumentPopSize = StackAdjust.getImm();
  } else {
    // ... otherwise the amount to pop is *all* of the argument space,
    // conveniently stored in the MachineFunctionInfo by
    // LowerFormalArguments. This will, of course, be zero for the C calling
    // convention.
    ArgumentPopSize = AFI->getArgumentStackToRestore();
  }

  // The stack frame should be like below,
  //
  //      ----------------------                     ---
  //      |                    |                      |
  //      | BytesInStackArgArea|              CalleeArgStackSize
  //      | (NumReusableBytes) |                (of tail call)
  //      |                    |                     ---
  //      |                    |                      |
  //      ---------------------|        ---           |
  //      |                    |         |            |
  //      |   CalleeSavedReg   |         |            |
  //      | (CalleeSavedStackSize)|      |            |
  //      |                    |         |            |
  //      ---------------------|         |         NumBytes
  //      |                    |     StackSize  (StackAdjustUp)
  //      |   LocalStackSize   |         |            |
  //      | (covering callee   |         |            |
  //      |       args)        |         |            |
  //      |                    |         |            |
  //      ----------------------        ---          ---
  //
  // So NumBytes = StackSize + BytesInStackArgArea - CalleeArgStackSize
  //             = StackSize + ArgumentPopSize
  //
  // AArch64TargetLowering::LowerCall figures out ArgumentPopSize and keeps
  // it as the 2nd argument of AArch64ISD::TC_RETURN.

  auto Cleanup = make_scope_exit([&] { InsertReturnAddressAuth(MF, MBB); });

  bool IsWin64 =
      Subtarget.isCallingConvWin64(MF.getFunction().getCallingConv());
  // Var args are accounted for in the containing function, so don't
  // include them for funclets.
  unsigned FixedObject =
      (IsWin64 && !IsFunclet) ? alignTo(AFI->getVarArgsGPRSize(), 16) : 0;

  uint64_t AfterCSRPopSize = ArgumentPopSize;
  auto PrologueSaveSize = AFI->getCalleeSavedStackSize() + FixedObject;
  // We cannot rely on the local stack size set in emitPrologue if the function
  // has funclets, as funclets have different local stack size requirements, and
  // the current value set in emitPrologue may be that of the containing
  // function.
  if (MF.hasEHFunclets())
    AFI->setLocalStackSize(NumBytes - PrologueSaveSize);
  bool CombineSPBump = shouldCombineCSRLocalStackBump(MF, NumBytes);
  // Assume we can't combine the last pop with the sp restore.

  if (!CombineSPBump && PrologueSaveSize != 0) {
    MachineBasicBlock::iterator Pop = std::prev(MBB.getFirstTerminator());
    while (AArch64InstrInfo::isSEHInstruction(*Pop))
      Pop = std::prev(Pop);
    // Converting the last ldp to a post-index ldp is valid only if the last
    // ldp's offset is 0.
    const MachineOperand &OffsetOp = Pop->getOperand(Pop->getNumOperands() - 1);
    // If the offset is 0, convert it to a post-index ldp.
    if (OffsetOp.getImm() == 0)
      convertCalleeSaveRestoreToSPPrePostIncDec(
          MBB, Pop, DL, TII, PrologueSaveSize, NeedsWinCFI, false);
    else {
      // If not, make sure to emit an add after the last ldp.
      // We're doing this by transfering the size to be restored from the
      // adjustment *before* the CSR pops to the adjustment *after* the CSR
      // pops.
      AfterCSRPopSize += PrologueSaveSize;
    }
  }

  // Move past the restores of the callee-saved registers.
  // If we plan on combining the sp bump of the local stack size and the callee
  // save stack size, we might need to adjust the CSR save and restore offsets.
  MachineBasicBlock::iterator LastPopI = MBB.getFirstTerminator();
  MachineBasicBlock::iterator Begin = MBB.begin();
  while (LastPopI != Begin) {
    --LastPopI;
    if (!LastPopI->getFlag(MachineInstr::FrameDestroy)) {
      ++LastPopI;
      break;
    } else if (CombineSPBump)
      fixupCalleeSaveRestoreStackOffset(*LastPopI, AFI->getLocalStackSize(),
                                        NeedsWinCFI);
  }

  if (NeedsWinCFI)
    BuildMI(MBB, LastPopI, DL, TII->get(AArch64::SEH_EpilogStart))
        .setMIFlag(MachineInstr::FrameDestroy);

  // If there is a single SP update, insert it before the ret and we're done.
  if (CombineSPBump) {
    emitFrameOffset(MBB, MBB.getFirstTerminator(), DL, AArch64::SP, AArch64::SP,
                    NumBytes + AfterCSRPopSize, TII, MachineInstr::FrameDestroy,
                    false, NeedsWinCFI);
    if (NeedsWinCFI)
      BuildMI(MBB, MBB.getFirstTerminator(), DL,
              TII->get(AArch64::SEH_EpilogEnd))
          .setMIFlag(MachineInstr::FrameDestroy);
    return;
  }

  NumBytes -= PrologueSaveSize;
  assert(NumBytes >= 0 && "Negative stack allocation size!?");

  if (!hasFP(MF)) {
    bool RedZone = canUseRedZone(MF);
    // If this was a redzone leaf function, we don't need to restore the
    // stack pointer (but we may need to pop stack args for fastcc).
    if (RedZone && AfterCSRPopSize == 0)
      return;

    bool NoCalleeSaveRestore = PrologueSaveSize == 0;
    int StackRestoreBytes = RedZone ? 0 : NumBytes;
    if (NoCalleeSaveRestore)
      StackRestoreBytes += AfterCSRPopSize;

    // If we were able to combine the local stack pop with the argument pop,
    // then we're done.
    bool Done = NoCalleeSaveRestore || AfterCSRPopSize == 0;

    // If we're done after this, make sure to help the load store optimizer.
    if (Done)
      adaptForLdStOpt(MBB, MBB.getFirstTerminator(), LastPopI);

    emitFrameOffset(MBB, LastPopI, DL, AArch64::SP, AArch64::SP,
                    StackRestoreBytes, TII, MachineInstr::FrameDestroy, false,
                    NeedsWinCFI);
    if (Done) {
      if (NeedsWinCFI)
        BuildMI(MBB, MBB.getFirstTerminator(), DL,
                TII->get(AArch64::SEH_EpilogEnd))
            .setMIFlag(MachineInstr::FrameDestroy);
      return;
    }

    NumBytes = 0;
  }

  // Restore the original stack pointer.
  // FIXME: Rather than doing the math here, we should instead just use
  // non-post-indexed loads for the restores if we aren't actually going to
  // be able to save any instructions.
  if (!IsFunclet && (MFI.hasVarSizedObjects() || AFI->isStackRealigned()))
    emitFrameOffset(MBB, LastPopI, DL, AArch64::SP, AArch64::FP,
                    -AFI->getCalleeSavedStackSize() + 16, TII,
                    MachineInstr::FrameDestroy, false, NeedsWinCFI);
  else if (NumBytes)
    emitFrameOffset(MBB, LastPopI, DL, AArch64::SP, AArch64::SP, NumBytes, TII,
                    MachineInstr::FrameDestroy, false, NeedsWinCFI);

  // This must be placed after the callee-save restore code because that code
  // assumes the SP is at the same location as it was after the callee-save save
  // code in the prologue.
  if (AfterCSRPopSize) {
    // Find an insertion point for the first ldp so that it goes before the
    // shadow call stack epilog instruction. This ensures that the restore of
    // lr from x18 is placed after the restore from sp.
    auto FirstSPPopI = MBB.getFirstTerminator();
    while (FirstSPPopI != Begin) {
      auto Prev = std::prev(FirstSPPopI);
      if (Prev->getOpcode() != AArch64::LDRXpre ||
          Prev->getOperand(0).getReg() == AArch64::SP)
        break;
      FirstSPPopI = Prev;
    }

    adaptForLdStOpt(MBB, FirstSPPopI, LastPopI);

    emitFrameOffset(MBB, FirstSPPopI, DL, AArch64::SP, AArch64::SP,
                    AfterCSRPopSize, TII, MachineInstr::FrameDestroy, false,
                    NeedsWinCFI);
  }
  if (NeedsWinCFI)
    BuildMI(MBB, MBB.getFirstTerminator(), DL, TII->get(AArch64::SEH_EpilogEnd))
        .setMIFlag(MachineInstr::FrameDestroy);
}

/// getFrameIndexReference - Provide a base+offset reference to an FI slot for
/// debug info.  It's the same as what we use for resolving the code-gen
/// references for now.  FIXME: This can go wrong when references are
/// SP-relative and simple call frames aren't used.
int AArch64FrameLowering::getFrameIndexReference(const MachineFunction &MF,
                                                 int FI,
                                                 unsigned &FrameReg) const {
  return resolveFrameIndexReference(MF, FI, FrameReg);
}

int AArch64FrameLowering::resolveFrameIndexReference(const MachineFunction &MF,
                                                     int FI, unsigned &FrameReg,
                                                     bool PreferFP) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const AArch64RegisterInfo *RegInfo = static_cast<const AArch64RegisterInfo *>(
      MF.getSubtarget().getRegisterInfo());
  const AArch64FunctionInfo *AFI = MF.getInfo<AArch64FunctionInfo>();
  const AArch64Subtarget &Subtarget = MF.getSubtarget<AArch64Subtarget>();
  bool IsWin64 =
      Subtarget.isCallingConvWin64(MF.getFunction().getCallingConv());
  unsigned FixedObject = IsWin64 ? alignTo(AFI->getVarArgsGPRSize(), 16) : 0;
  int FPOffset = MFI.getObjectOffset(FI) + FixedObject + 16;
  int Offset = MFI.getObjectOffset(FI) + MFI.getStackSize();
  bool isFixed = MFI.isFixedObjectIndex(FI);
  bool isCSR = !isFixed && MFI.getObjectOffset(FI) >=
                               -((int)AFI->getCalleeSavedStackSize());

  // Use frame pointer to reference fixed objects. Use it for locals if
  // there are VLAs or a dynamically realigned SP (and thus the SP isn't
  // reliable as a base). Make sure useFPForScavengingIndex() does the
  // right thing for the emergency spill slot.
  bool UseFP = false;
  if (AFI->hasStackFrame()) {
    // Note: Keeping the following as multiple 'if' statements rather than
    // merging to a single expression for readability.
    //
    // Argument access should always use the FP.
    if (isFixed) {
      UseFP = hasFP(MF);
    } else if (isCSR && RegInfo->needsStackRealignment(MF)) {
      // References to the CSR area must use FP if we're re-aligning the stack
      // since the dynamically-sized alignment padding is between the SP/BP and
      // the CSR area.
      assert(hasFP(MF) && "Re-aligned stack must have frame pointer");
      UseFP = true;
    } else if (hasFP(MF) && !RegInfo->needsStackRealignment(MF)) {
      // If the FPOffset is negative, we have to keep in mind that the
      // available offset range for negative offsets is smaller than for
      // positive ones. If an offset is
      // available via the FP and the SP, use whichever is closest.
      bool FPOffsetFits = FPOffset >= -256;
      PreferFP |= Offset > -FPOffset;

      if (MFI.hasVarSizedObjects()) {
        // If we have variable sized objects, we can use either FP or BP, as the
        // SP offset is unknown. We can use the base pointer if we have one and
        // FP is not preferred. If not, we're stuck with using FP.
        bool CanUseBP = RegInfo->hasBasePointer(MF);
        if (FPOffsetFits && CanUseBP) // Both are ok. Pick the best.
          UseFP = PreferFP;
        else if (!CanUseBP) // Can't use BP. Forced to use FP.
          UseFP = true;
        // else we can use BP and FP, but the offset from FP won't fit.
        // That will make us scavenge registers which we can probably avoid by
        // using BP. If it won't fit for BP either, we'll scavenge anyway.
      } else if (FPOffset >= 0) {
        // Use SP or FP, whichever gives us the best chance of the offset
        // being in range for direct access. If the FPOffset is positive,
        // that'll always be best, as the SP will be even further away.
        UseFP = true;
      } else if (MF.hasEHFunclets() && !RegInfo->hasBasePointer(MF)) {
        // Funclets access the locals contained in the parent's stack frame
        // via the frame pointer, so we have to use the FP in the parent
        // function.
        assert(
            Subtarget.isCallingConvWin64(MF.getFunction().getCallingConv()) &&
            "Funclets should only be present on Win64");
        UseFP = true;
      } else {
        // We have the choice between FP and (SP or BP).
        if (FPOffsetFits && PreferFP) // If FP is the best fit, use it.
          UseFP = true;
      }
    }
  }

  assert(((isFixed || isCSR) || !RegInfo->needsStackRealignment(MF) || !UseFP) &&
         "In the presence of dynamic stack pointer realignment, "
         "non-argument/CSR objects cannot be accessed through the frame pointer");

  if (UseFP) {
    FrameReg = RegInfo->getFrameRegister(MF);
    return FPOffset;
  }

  // Use the base pointer if we have one.
  if (RegInfo->hasBasePointer(MF))
    FrameReg = RegInfo->getBaseRegister();
  else {
    assert(!MFI.hasVarSizedObjects() &&
           "Can't use SP when we have var sized objects.");
    FrameReg = AArch64::SP;
    // If we're using the red zone for this function, the SP won't actually
    // be adjusted, so the offsets will be negative. They're also all
    // within range of the signed 9-bit immediate instructions.
    if (canUseRedZone(MF))
      Offset -= AFI->getLocalStackSize();
  }

  return Offset;
}

static unsigned getPrologueDeath(MachineFunction &MF, unsigned Reg) {
  // Do not set a kill flag on values that are also marked as live-in. This
  // happens with the @llvm-returnaddress intrinsic and with arguments passed in
  // callee saved registers.
  // Omitting the kill flags is conservatively correct even if the live-in
  // is not used after all.
  bool IsLiveIn = MF.getRegInfo().isLiveIn(Reg);
  return getKillRegState(!IsLiveIn);
}

static bool produceCompactUnwindFrame(MachineFunction &MF) {
  const AArch64Subtarget &Subtarget = MF.getSubtarget<AArch64Subtarget>();
  AttributeList Attrs = MF.getFunction().getAttributes();
  return Subtarget.isTargetMachO() &&
         !(Subtarget.getTargetLowering()->supportSwiftError() &&
           Attrs.hasAttrSomewhere(Attribute::SwiftError));
}

static bool invalidateWindowsRegisterPairing(unsigned Reg1, unsigned Reg2,
                                             bool NeedsWinCFI) {
  // If we are generating register pairs for a Windows function that requires
  // EH support, then pair consecutive registers only.  There are no unwind
  // opcodes for saves/restores of non-consectuve register pairs.
  // The unwind opcodes are save_regp, save_regp_x, save_fregp, save_frepg_x.
  // https://docs.microsoft.com/en-us/cpp/build/arm64-exception-handling

  // TODO: LR can be paired with any register.  We don't support this yet in
  // the MCLayer.  We need to add support for the save_lrpair unwind code.
  if (!NeedsWinCFI)
    return false;
  if (Reg2 == Reg1 + 1)
    return false;
  return true;
}

namespace {

struct RegPairInfo {
  unsigned Reg1 = AArch64::NoRegister;
  unsigned Reg2 = AArch64::NoRegister;
  int FrameIdx;
  int Offset;
  enum RegType { GPR, FPR64, FPR128 } Type;

  RegPairInfo() = default;

  bool isPaired() const { return Reg2 != AArch64::NoRegister; }
};

} // end anonymous namespace

static void computeCalleeSaveRegisterPairs(
    MachineFunction &MF, const std::vector<CalleeSavedInfo> &CSI,
    const TargetRegisterInfo *TRI, SmallVectorImpl<RegPairInfo> &RegPairs,
    bool &NeedShadowCallStackProlog) {

  if (CSI.empty())
    return;

  bool NeedsWinCFI = needsWinCFI(MF);
  AArch64FunctionInfo *AFI = MF.getInfo<AArch64FunctionInfo>();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  CallingConv::ID CC = MF.getFunction().getCallingConv();
  unsigned Count = CSI.size();
  (void)CC;
  // MachO's compact unwind format relies on all registers being stored in
  // pairs.
  assert((!produceCompactUnwindFrame(MF) ||
          CC == CallingConv::PreserveMost ||
          (Count & 1) == 0) &&
         "Odd number of callee-saved regs to spill!");
  int Offset = AFI->getCalleeSavedStackSize();
  // On Linux, we will have either one or zero non-paired register.  On Windows
  // with CFI, we can have multiple unpaired registers in order to utilize the
  // available unwind codes.  This flag assures that the alignment fixup is done
  // only once, as intened.
  bool FixupDone = false;
  for (unsigned i = 0; i < Count; ++i) {
    RegPairInfo RPI;
    RPI.Reg1 = CSI[i].getReg();

    if (AArch64::GPR64RegClass.contains(RPI.Reg1))
      RPI.Type = RegPairInfo::GPR;
    else if (AArch64::FPR64RegClass.contains(RPI.Reg1))
      RPI.Type = RegPairInfo::FPR64;
    else if (AArch64::FPR128RegClass.contains(RPI.Reg1))
      RPI.Type = RegPairInfo::FPR128;
    else
      llvm_unreachable("Unsupported register class.");

    // Add the next reg to the pair if it is in the same register class.
    if (i + 1 < Count) {
      unsigned NextReg = CSI[i + 1].getReg();
      switch (RPI.Type) {
      case RegPairInfo::GPR:
        if (AArch64::GPR64RegClass.contains(NextReg) &&
            !invalidateWindowsRegisterPairing(RPI.Reg1, NextReg, NeedsWinCFI))
          RPI.Reg2 = NextReg;
        break;
      case RegPairInfo::FPR64:
        if (AArch64::FPR64RegClass.contains(NextReg) &&
            !invalidateWindowsRegisterPairing(RPI.Reg1, NextReg, NeedsWinCFI))
          RPI.Reg2 = NextReg;
        break;
      case RegPairInfo::FPR128:
        if (AArch64::FPR128RegClass.contains(NextReg))
          RPI.Reg2 = NextReg;
        break;
      }
    }

    // If either of the registers to be saved is the lr register, it means that
    // we also need to save lr in the shadow call stack.
    if ((RPI.Reg1 == AArch64::LR || RPI.Reg2 == AArch64::LR) &&
        MF.getFunction().hasFnAttribute(Attribute::ShadowCallStack)) {
      if (!MF.getSubtarget<AArch64Subtarget>().isXRegisterReserved(18))
        report_fatal_error("Must reserve x18 to use shadow call stack");
      NeedShadowCallStackProlog = true;
    }

    // GPRs and FPRs are saved in pairs of 64-bit regs. We expect the CSI
    // list to come in sorted by frame index so that we can issue the store
    // pair instructions directly. Assert if we see anything otherwise.
    //
    // The order of the registers in the list is controlled by
    // getCalleeSavedRegs(), so they will always be in-order, as well.
    assert((!RPI.isPaired() ||
            (CSI[i].getFrameIdx() + 1 == CSI[i + 1].getFrameIdx())) &&
           "Out of order callee saved regs!");

    // MachO's compact unwind format relies on all registers being stored in
    // adjacent register pairs.
    assert((!produceCompactUnwindFrame(MF) ||
            CC == CallingConv::PreserveMost ||
            (RPI.isPaired() &&
             ((RPI.Reg1 == AArch64::LR && RPI.Reg2 == AArch64::FP) ||
              RPI.Reg1 + 1 == RPI.Reg2))) &&
           "Callee-save registers not saved as adjacent register pair!");

    RPI.FrameIdx = CSI[i].getFrameIdx();

    int Scale = RPI.Type == RegPairInfo::FPR128 ? 16 : 8;
    Offset -= RPI.isPaired() ? 2 * Scale : Scale;

    // Round up size of non-pair to pair size if we need to pad the
    // callee-save area to ensure 16-byte alignment.
    if (AFI->hasCalleeSaveStackFreeSpace() && !FixupDone &&
        RPI.Type != RegPairInfo::FPR128 && !RPI.isPaired()) {
      FixupDone = true;
      Offset -= 8;
      assert(Offset % 16 == 0);
      assert(MFI.getObjectAlignment(RPI.FrameIdx) <= 16);
      MFI.setObjectAlignment(RPI.FrameIdx, 16);
    }

    assert(Offset % Scale == 0);
    RPI.Offset = Offset / Scale;
    assert((RPI.Offset >= -64 && RPI.Offset <= 63) &&
           "Offset out of bounds for LDP/STP immediate");

    RegPairs.push_back(RPI);
    if (RPI.isPaired())
      ++i;
  }
}

bool AArch64FrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    const std::vector<CalleeSavedInfo> &CSI,
    const TargetRegisterInfo *TRI) const {
  MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  bool NeedsWinCFI = needsWinCFI(MF);
  DebugLoc DL;
  SmallVector<RegPairInfo, 8> RegPairs;

  bool NeedShadowCallStackProlog = false;
  computeCalleeSaveRegisterPairs(MF, CSI, TRI, RegPairs,
                                 NeedShadowCallStackProlog);
  const MachineRegisterInfo &MRI = MF.getRegInfo();

  if (NeedShadowCallStackProlog) {
    // Shadow call stack prolog: str x30, [x18], #8
    BuildMI(MBB, MI, DL, TII.get(AArch64::STRXpost))
        .addReg(AArch64::X18, RegState::Define)
        .addReg(AArch64::LR)
        .addReg(AArch64::X18)
        .addImm(8)
        .setMIFlag(MachineInstr::FrameSetup);

    if (NeedsWinCFI)
      BuildMI(MBB, MI, DL, TII.get(AArch64::SEH_Nop))
          .setMIFlag(MachineInstr::FrameSetup);

    if (!MF.getFunction().hasFnAttribute(Attribute::NoUnwind)) {
      // Emit a CFI instruction that causes 8 to be subtracted from the value of
      // x18 when unwinding past this frame.
      static const char CFIInst[] = {
          dwarf::DW_CFA_val_expression,
          18, // register
          2,  // length
          static_cast<char>(unsigned(dwarf::DW_OP_breg18)),
          static_cast<char>(-8) & 0x7f, // addend (sleb128)
      };
      unsigned CFIIndex =
          MF.addFrameInst(MCCFIInstruction::createEscape(nullptr, CFIInst));
      BuildMI(MBB, MI, DL, TII.get(AArch64::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlag(MachineInstr::FrameSetup);
    }

    // This instruction also makes x18 live-in to the entry block.
    MBB.addLiveIn(AArch64::X18);
  }

  for (auto RPII = RegPairs.rbegin(), RPIE = RegPairs.rend(); RPII != RPIE;
       ++RPII) {
    RegPairInfo RPI = *RPII;
    unsigned Reg1 = RPI.Reg1;
    unsigned Reg2 = RPI.Reg2;
    unsigned StrOpc;

    // Issue sequence of spills for cs regs.  The first spill may be converted
    // to a pre-decrement store later by emitPrologue if the callee-save stack
    // area allocation can't be combined with the local stack area allocation.
    // For example:
    //    stp     x22, x21, [sp, #0]     // addImm(+0)
    //    stp     x20, x19, [sp, #16]    // addImm(+2)
    //    stp     fp, lr, [sp, #32]      // addImm(+4)
    // Rationale: This sequence saves uop updates compared to a sequence of
    // pre-increment spills like stp xi,xj,[sp,#-16]!
    // Note: Similar rationale and sequence for restores in epilog.
    unsigned Size, Align;
    switch (RPI.Type) {
    case RegPairInfo::GPR:
       StrOpc = RPI.isPaired() ? AArch64::STPXi : AArch64::STRXui;
       Size = 8;
       Align = 8;
       break;
    case RegPairInfo::FPR64:
       StrOpc = RPI.isPaired() ? AArch64::STPDi : AArch64::STRDui;
       Size = 8;
       Align = 8;
       break;
    case RegPairInfo::FPR128:
       StrOpc = RPI.isPaired() ? AArch64::STPQi : AArch64::STRQui;
       Size = 16;
       Align = 16;
       break;
    }
    LLVM_DEBUG(dbgs() << "CSR spill: (" << printReg(Reg1, TRI);
               if (RPI.isPaired()) dbgs() << ", " << printReg(Reg2, TRI);
               dbgs() << ") -> fi#(" << RPI.FrameIdx;
               if (RPI.isPaired()) dbgs() << ", " << RPI.FrameIdx + 1;
               dbgs() << ")\n");

    assert((!NeedsWinCFI || !(Reg1 == AArch64::LR && Reg2 == AArch64::FP)) &&
           "Windows unwdinding requires a consecutive (FP,LR) pair");
    // Windows unwind codes require consecutive registers if registers are
    // paired.  Make the switch here, so that the code below will save (x,x+1)
    // and not (x+1,x).
    unsigned FrameIdxReg1 = RPI.FrameIdx;
    unsigned FrameIdxReg2 = RPI.FrameIdx + 1;
    if (NeedsWinCFI && RPI.isPaired()) {
      std::swap(Reg1, Reg2);
      std::swap(FrameIdxReg1, FrameIdxReg2);
    }
    MachineInstrBuilder MIB = BuildMI(MBB, MI, DL, TII.get(StrOpc));
    if (!MRI.isReserved(Reg1))
      MBB.addLiveIn(Reg1);
    if (RPI.isPaired()) {
      if (!MRI.isReserved(Reg2))
        MBB.addLiveIn(Reg2);
      MIB.addReg(Reg2, getPrologueDeath(MF, Reg2));
      MIB.addMemOperand(MF.getMachineMemOperand(
          MachinePointerInfo::getFixedStack(MF, FrameIdxReg2),
          MachineMemOperand::MOStore, Size, Align));
    }
    MIB.addReg(Reg1, getPrologueDeath(MF, Reg1))
        .addReg(AArch64::SP)
        .addImm(RPI.Offset) // [sp, #offset*scale],
                            // where factor*scale is implicit
        .setMIFlag(MachineInstr::FrameSetup);
    MIB.addMemOperand(MF.getMachineMemOperand(
        MachinePointerInfo::getFixedStack(MF,FrameIdxReg1),
        MachineMemOperand::MOStore, Size, Align));
    if (NeedsWinCFI)
      InsertSEH(MIB, TII, MachineInstr::FrameSetup);

  }
  return true;
}

bool AArch64FrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    std::vector<CalleeSavedInfo> &CSI,
    const TargetRegisterInfo *TRI) const {
  MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  DebugLoc DL;
  SmallVector<RegPairInfo, 8> RegPairs;
  bool NeedsWinCFI = needsWinCFI(MF);

  if (MI != MBB.end())
    DL = MI->getDebugLoc();

  bool NeedShadowCallStackProlog = false;
  computeCalleeSaveRegisterPairs(MF, CSI, TRI, RegPairs,
                                 NeedShadowCallStackProlog);

  auto EmitMI = [&](const RegPairInfo &RPI) {
    unsigned Reg1 = RPI.Reg1;
    unsigned Reg2 = RPI.Reg2;

    // Issue sequence of restores for cs regs. The last restore may be converted
    // to a post-increment load later by emitEpilogue if the callee-save stack
    // area allocation can't be combined with the local stack area allocation.
    // For example:
    //    ldp     fp, lr, [sp, #32]       // addImm(+4)
    //    ldp     x20, x19, [sp, #16]     // addImm(+2)
    //    ldp     x22, x21, [sp, #0]      // addImm(+0)
    // Note: see comment in spillCalleeSavedRegisters()
    unsigned LdrOpc;
    unsigned Size, Align;
    switch (RPI.Type) {
    case RegPairInfo::GPR:
       LdrOpc = RPI.isPaired() ? AArch64::LDPXi : AArch64::LDRXui;
       Size = 8;
       Align = 8;
       break;
    case RegPairInfo::FPR64:
       LdrOpc = RPI.isPaired() ? AArch64::LDPDi : AArch64::LDRDui;
       Size = 8;
       Align = 8;
       break;
    case RegPairInfo::FPR128:
       LdrOpc = RPI.isPaired() ? AArch64::LDPQi : AArch64::LDRQui;
       Size = 16;
       Align = 16;
       break;
    }
    LLVM_DEBUG(dbgs() << "CSR restore: (" << printReg(Reg1, TRI);
               if (RPI.isPaired()) dbgs() << ", " << printReg(Reg2, TRI);
               dbgs() << ") -> fi#(" << RPI.FrameIdx;
               if (RPI.isPaired()) dbgs() << ", " << RPI.FrameIdx + 1;
               dbgs() << ")\n");

    // Windows unwind codes require consecutive registers if registers are
    // paired.  Make the switch here, so that the code below will save (x,x+1)
    // and not (x+1,x).
    unsigned FrameIdxReg1 = RPI.FrameIdx;
    unsigned FrameIdxReg2 = RPI.FrameIdx + 1;
    if (NeedsWinCFI && RPI.isPaired()) {
      std::swap(Reg1, Reg2);
      std::swap(FrameIdxReg1, FrameIdxReg2);
    }
    MachineInstrBuilder MIB = BuildMI(MBB, MI, DL, TII.get(LdrOpc));
    if (RPI.isPaired()) {
      MIB.addReg(Reg2, getDefRegState(true));
      MIB.addMemOperand(MF.getMachineMemOperand(
          MachinePointerInfo::getFixedStack(MF, FrameIdxReg2),
          MachineMemOperand::MOLoad, Size, Align));
    }
    MIB.addReg(Reg1, getDefRegState(true))
        .addReg(AArch64::SP)
        .addImm(RPI.Offset) // [sp, #offset*scale]
                            // where factor*scale is implicit
        .setMIFlag(MachineInstr::FrameDestroy);
    MIB.addMemOperand(MF.getMachineMemOperand(
        MachinePointerInfo::getFixedStack(MF, FrameIdxReg1),
        MachineMemOperand::MOLoad, Size, Align));
    if (NeedsWinCFI)
      InsertSEH(MIB, TII, MachineInstr::FrameDestroy);
  };
  if (ReverseCSRRestoreSeq)
    for (const RegPairInfo &RPI : reverse(RegPairs))
      EmitMI(RPI);
  else
    for (const RegPairInfo &RPI : RegPairs)
      EmitMI(RPI);

  if (NeedShadowCallStackProlog) {
    // Shadow call stack epilog: ldr x30, [x18, #-8]!
    BuildMI(MBB, MI, DL, TII.get(AArch64::LDRXpre))
        .addReg(AArch64::X18, RegState::Define)
        .addReg(AArch64::LR, RegState::Define)
        .addReg(AArch64::X18)
        .addImm(-8)
        .setMIFlag(MachineInstr::FrameDestroy);
  }

  return true;
}

void AArch64FrameLowering::determineCalleeSaves(MachineFunction &MF,
                                                BitVector &SavedRegs,
                                                RegScavenger *RS) const {
  // All calls are tail calls in GHC calling conv, and functions have no
  // prologue/epilogue.
  if (MF.getFunction().getCallingConv() == CallingConv::GHC)
    return;

  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
  const AArch64RegisterInfo *RegInfo = static_cast<const AArch64RegisterInfo *>(
      MF.getSubtarget().getRegisterInfo());
  AArch64FunctionInfo *AFI = MF.getInfo<AArch64FunctionInfo>();
  unsigned UnspilledCSGPR = AArch64::NoRegister;
  unsigned UnspilledCSGPRPaired = AArch64::NoRegister;

  MachineFrameInfo &MFI = MF.getFrameInfo();
  const MCPhysReg *CSRegs = MF.getRegInfo().getCalleeSavedRegs();

  unsigned BasePointerReg = RegInfo->hasBasePointer(MF)
                                ? RegInfo->getBaseRegister()
                                : (unsigned)AArch64::NoRegister;

  unsigned ExtraCSSpill = 0;
  // Figure out which callee-saved registers to save/restore.
  for (unsigned i = 0; CSRegs[i]; ++i) {
    const unsigned Reg = CSRegs[i];

    // Add the base pointer register to SavedRegs if it is callee-save.
    if (Reg == BasePointerReg)
      SavedRegs.set(Reg);

    bool RegUsed = SavedRegs.test(Reg);
    unsigned PairedReg = CSRegs[i ^ 1];
    if (!RegUsed) {
      if (AArch64::GPR64RegClass.contains(Reg) &&
          !RegInfo->isReservedReg(MF, Reg)) {
        UnspilledCSGPR = Reg;
        UnspilledCSGPRPaired = PairedReg;
      }
      continue;
    }

    // MachO's compact unwind format relies on all registers being stored in
    // pairs.
    // FIXME: the usual format is actually better if unwinding isn't needed.
    if (produceCompactUnwindFrame(MF) && PairedReg != AArch64::NoRegister &&
        !SavedRegs.test(PairedReg)) {
      SavedRegs.set(PairedReg);
      if (AArch64::GPR64RegClass.contains(PairedReg) &&
          !RegInfo->isReservedReg(MF, PairedReg))
        ExtraCSSpill = PairedReg;
    }
  }

  // Calculates the callee saved stack size.
  unsigned CSStackSize = 0;
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  for (unsigned Reg : SavedRegs.set_bits())
    CSStackSize += TRI->getRegSizeInBits(Reg, MRI) / 8;

  // Save number of saved regs, so we can easily update CSStackSize later.
  unsigned NumSavedRegs = SavedRegs.count();

  // The frame record needs to be created by saving the appropriate registers
  unsigned EstimatedStackSize = MFI.estimateStackSize(MF);
  if (hasFP(MF) ||
      windowsRequiresStackProbe(MF, EstimatedStackSize + CSStackSize + 16)) {
    SavedRegs.set(AArch64::FP);
    SavedRegs.set(AArch64::LR);
  }

  LLVM_DEBUG(dbgs() << "*** determineCalleeSaves\nUsed CSRs:";
             for (unsigned Reg
                  : SavedRegs.set_bits()) dbgs()
             << ' ' << printReg(Reg, RegInfo);
             dbgs() << "\n";);

  // If any callee-saved registers are used, the frame cannot be eliminated.
  bool CanEliminateFrame = SavedRegs.count() == 0;

  // The CSR spill slots have not been allocated yet, so estimateStackSize
  // won't include them.
  unsigned EstimatedStackSizeLimit = estimateRSStackSizeLimit(MF);
  bool BigStack = (EstimatedStackSize + CSStackSize) > EstimatedStackSizeLimit;
  if (BigStack || !CanEliminateFrame || RegInfo->cannotEliminateFrame(MF))
    AFI->setHasStackFrame(true);

  // Estimate if we might need to scavenge a register at some point in order
  // to materialize a stack offset. If so, either spill one additional
  // callee-saved register or reserve a special spill slot to facilitate
  // register scavenging. If we already spilled an extra callee-saved register
  // above to keep the number of spills even, we don't need to do anything else
  // here.
  if (BigStack) {
    if (!ExtraCSSpill && UnspilledCSGPR != AArch64::NoRegister) {
      LLVM_DEBUG(dbgs() << "Spilling " << printReg(UnspilledCSGPR, RegInfo)
                        << " to get a scratch register.\n");
      SavedRegs.set(UnspilledCSGPR);
      // MachO's compact unwind format relies on all registers being stored in
      // pairs, so if we need to spill one extra for BigStack, then we need to
      // store the pair.
      if (produceCompactUnwindFrame(MF))
        SavedRegs.set(UnspilledCSGPRPaired);
      ExtraCSSpill = UnspilledCSGPRPaired;
    }

    // If we didn't find an extra callee-saved register to spill, create
    // an emergency spill slot.
    if (!ExtraCSSpill || MF.getRegInfo().isPhysRegUsed(ExtraCSSpill)) {
      const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
      const TargetRegisterClass &RC = AArch64::GPR64RegClass;
      unsigned Size = TRI->getSpillSize(RC);
      unsigned Align = TRI->getSpillAlignment(RC);
      int FI = MFI.CreateStackObject(Size, Align, false);
      RS->addScavengingFrameIndex(FI);
      LLVM_DEBUG(dbgs() << "No available CS registers, allocated fi#" << FI
                        << " as the emergency spill slot.\n");
    }
  }

  // Adding the size of additional 64bit GPR saves.
  CSStackSize += 8 * (SavedRegs.count() - NumSavedRegs);
  unsigned AlignedCSStackSize = alignTo(CSStackSize, 16);
  LLVM_DEBUG(dbgs() << "Estimated stack frame size: "
               << EstimatedStackSize + AlignedCSStackSize
               << " bytes.\n");

  // Round up to register pair alignment to avoid additional SP adjustment
  // instructions.
  AFI->setCalleeSavedStackSize(AlignedCSStackSize);
  AFI->setCalleeSaveStackHasFreeSpace(AlignedCSStackSize != CSStackSize);
}

bool AArch64FrameLowering::enableStackSlotScavenging(
    const MachineFunction &MF) const {
  const AArch64FunctionInfo *AFI = MF.getInfo<AArch64FunctionInfo>();
  return AFI->hasCalleeSaveStackFreeSpace();
}

void AArch64FrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *RS) const {
  // If this function isn't doing Win64-style C++ EH, we don't need to do
  // anything.
  if (!MF.hasEHFunclets())
    return;
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  WinEHFuncInfo &EHInfo = *MF.getWinEHFuncInfo();

  MachineBasicBlock &MBB = MF.front();
  auto MBBI = MBB.begin();
  while (MBBI != MBB.end() && MBBI->getFlag(MachineInstr::FrameSetup))
    ++MBBI;

  // Create an UnwindHelp object.
  int UnwindHelpFI =
      MFI.CreateStackObject(/*size*/8, /*alignment*/16, false);
  EHInfo.UnwindHelpFrameIdx = UnwindHelpFI;
  // We need to store -2 into the UnwindHelp object at the start of the
  // function.
  DebugLoc DL;
  RS->enterBasicBlockEnd(MBB);
  RS->backward(std::prev(MBBI));
  unsigned DstReg = RS->FindUnusedReg(&AArch64::GPR64commonRegClass);
  assert(DstReg && "There must be a free register after frame setup");
  BuildMI(MBB, MBBI, DL, TII.get(AArch64::MOVi64imm), DstReg).addImm(-2);
  BuildMI(MBB, MBBI, DL, TII.get(AArch64::STURXi))
      .addReg(DstReg, getKillRegState(true))
      .addFrameIndex(UnwindHelpFI)
      .addImm(0);
}

/// For Win64 AArch64 EH, the offset to the Unwind object is from the SP before
/// the update.  This is easily retrieved as it is exactly the offset that is set
/// in processFunctionBeforeFrameFinalized.
int AArch64FrameLowering::getFrameIndexReferencePreferSP(
    const MachineFunction &MF, int FI, unsigned &FrameReg,
    bool IgnoreSPUpdates) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  LLVM_DEBUG(dbgs() << "Offset from the SP for " << FI << " is "
                    << MFI.getObjectOffset(FI) << "\n");
  FrameReg = AArch64::SP;
  return MFI.getObjectOffset(FI);
}

/// The parent frame offset (aka dispFrame) is only used on X86_64 to retrieve
/// the parent's frame pointer
unsigned AArch64FrameLowering::getWinEHParentFrameOffset(
    const MachineFunction &MF) const {
  return 0;
}

/// Funclets only need to account for space for the callee saved registers,
/// as the locals are accounted for in the parent's stack frame.
unsigned AArch64FrameLowering::getWinEHFuncletFrameSize(
    const MachineFunction &MF) const {
  // This is the size of the pushed CSRs.
  unsigned CSSize =
      MF.getInfo<AArch64FunctionInfo>()->getCalleeSavedStackSize();
  // This is the amount of stack a funclet needs to allocate.
  return alignTo(CSSize + MF.getFrameInfo().getMaxCallFrameSize(),
                 getStackAlignment());
}
