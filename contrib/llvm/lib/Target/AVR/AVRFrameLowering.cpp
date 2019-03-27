//===-- AVRFrameLowering.cpp - AVR Frame Information ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the AVR implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "AVRFrameLowering.h"

#include "AVR.h"
#include "AVRInstrInfo.h"
#include "AVRMachineFunctionInfo.h"
#include "AVRTargetMachine.h"
#include "MCTargetDesc/AVRMCTargetDesc.h"

#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"

#include <vector>

namespace llvm {

AVRFrameLowering::AVRFrameLowering()
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, 1, -2) {}

bool AVRFrameLowering::canSimplifyCallFramePseudos(
    const MachineFunction &MF) const {
  // Always simplify call frame pseudo instructions, even when
  // hasReservedCallFrame is false.
  return true;
}

bool AVRFrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  // Reserve call frame memory in function prologue under the following
  // conditions:
  // - Y pointer is reserved to be the frame pointer.
  // - The function does not contain variable sized objects.

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return hasFP(MF) && !MFI.hasVarSizedObjects();
}

void AVRFrameLowering::emitPrologue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.begin();
  CallingConv::ID CallConv = MF.getFunction().getCallingConv();
  DebugLoc DL = (MBBI != MBB.end()) ? MBBI->getDebugLoc() : DebugLoc();
  const AVRSubtarget &STI = MF.getSubtarget<AVRSubtarget>();
  const AVRInstrInfo &TII = *STI.getInstrInfo();
  bool HasFP = hasFP(MF);

  // Interrupt handlers re-enable interrupts in function entry.
  if (CallConv == CallingConv::AVR_INTR) {
    BuildMI(MBB, MBBI, DL, TII.get(AVR::BSETs))
        .addImm(0x07)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // Save the frame pointer if we have one.
  if (HasFP) {
    BuildMI(MBB, MBBI, DL, TII.get(AVR::PUSHWRr))
        .addReg(AVR::R29R28, RegState::Kill)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // Emit special prologue code to save R1, R0 and SREG in interrupt/signal
  // handlers before saving any other registers.
  if (CallConv == CallingConv::AVR_INTR ||
      CallConv == CallingConv::AVR_SIGNAL) {
    BuildMI(MBB, MBBI, DL, TII.get(AVR::PUSHWRr))
        .addReg(AVR::R1R0, RegState::Kill)
        .setMIFlag(MachineInstr::FrameSetup);

    BuildMI(MBB, MBBI, DL, TII.get(AVR::INRdA), AVR::R0)
        .addImm(0x3f)
        .setMIFlag(MachineInstr::FrameSetup);
    BuildMI(MBB, MBBI, DL, TII.get(AVR::PUSHRr))
        .addReg(AVR::R0, RegState::Kill)
        .setMIFlag(MachineInstr::FrameSetup);
    BuildMI(MBB, MBBI, DL, TII.get(AVR::EORRdRr))
        .addReg(AVR::R0, RegState::Define)
        .addReg(AVR::R0, RegState::Kill)
        .addReg(AVR::R0, RegState::Kill)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // Early exit if the frame pointer is not needed in this function.
  if (!HasFP) {
    return;
  }

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const AVRMachineFunctionInfo *AFI = MF.getInfo<AVRMachineFunctionInfo>();
  unsigned FrameSize = MFI.getStackSize() - AFI->getCalleeSavedFrameSize();

  // Skip the callee-saved push instructions.
  while (
      (MBBI != MBB.end()) && MBBI->getFlag(MachineInstr::FrameSetup) &&
      (MBBI->getOpcode() == AVR::PUSHRr || MBBI->getOpcode() == AVR::PUSHWRr)) {
    ++MBBI;
  }

  // Update Y with the new base value.
  BuildMI(MBB, MBBI, DL, TII.get(AVR::SPREAD), AVR::R29R28)
      .addReg(AVR::SP)
      .setMIFlag(MachineInstr::FrameSetup);

  // Mark the FramePtr as live-in in every block except the entry.
  for (MachineFunction::iterator I = std::next(MF.begin()), E = MF.end();
       I != E; ++I) {
    I->addLiveIn(AVR::R29R28);
  }

  if (!FrameSize) {
    return;
  }

  // Reserve the necessary frame memory by doing FP -= <size>.
  unsigned Opcode = (isUInt<6>(FrameSize)) ? AVR::SBIWRdK : AVR::SUBIWRdK;

  MachineInstr *MI = BuildMI(MBB, MBBI, DL, TII.get(Opcode), AVR::R29R28)
                         .addReg(AVR::R29R28, RegState::Kill)
                         .addImm(FrameSize)
                         .setMIFlag(MachineInstr::FrameSetup);
  // The SREG implicit def is dead.
  MI->getOperand(3).setIsDead();

  // Write back R29R28 to SP and temporarily disable interrupts.
  BuildMI(MBB, MBBI, DL, TII.get(AVR::SPWRITE), AVR::SP)
      .addReg(AVR::R29R28)
      .setMIFlag(MachineInstr::FrameSetup);
}

void AVRFrameLowering::emitEpilogue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  CallingConv::ID CallConv = MF.getFunction().getCallingConv();
  bool isHandler = (CallConv == CallingConv::AVR_INTR ||
                    CallConv == CallingConv::AVR_SIGNAL);

  // Early exit if the frame pointer is not needed in this function except for
  // signal/interrupt handlers where special code generation is required.
  if (!hasFP(MF) && !isHandler) {
    return;
  }

  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  assert(MBBI->getDesc().isReturn() &&
         "Can only insert epilog into returning blocks");

  DebugLoc DL = MBBI->getDebugLoc();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const AVRMachineFunctionInfo *AFI = MF.getInfo<AVRMachineFunctionInfo>();
  unsigned FrameSize = MFI.getStackSize() - AFI->getCalleeSavedFrameSize();
  const AVRSubtarget &STI = MF.getSubtarget<AVRSubtarget>();
  const AVRInstrInfo &TII = *STI.getInstrInfo();

  // Emit special epilogue code to restore R1, R0 and SREG in interrupt/signal
  // handlers at the very end of the function, just before reti.
  if (isHandler) {
    BuildMI(MBB, MBBI, DL, TII.get(AVR::POPRd), AVR::R0);
    BuildMI(MBB, MBBI, DL, TII.get(AVR::OUTARr))
        .addImm(0x3f)
        .addReg(AVR::R0, RegState::Kill);
    BuildMI(MBB, MBBI, DL, TII.get(AVR::POPWRd), AVR::R1R0);
  }

  if (hasFP(MF))
    BuildMI(MBB, MBBI, DL, TII.get(AVR::POPWRd), AVR::R29R28);

  // Early exit if there is no need to restore the frame pointer.
  if (!FrameSize) {
    return;
  }

  // Skip the callee-saved pop instructions.
  while (MBBI != MBB.begin()) {
    MachineBasicBlock::iterator PI = std::prev(MBBI);
    int Opc = PI->getOpcode();

    if (Opc != AVR::POPRd && Opc != AVR::POPWRd && !PI->isTerminator()) {
      break;
    }

    --MBBI;
  }

  unsigned Opcode;

  // Select the optimal opcode depending on how big it is.
  if (isUInt<6>(FrameSize)) {
    Opcode = AVR::ADIWRdK;
  } else {
    Opcode = AVR::SUBIWRdK;
    FrameSize = -FrameSize;
  }

  // Restore the frame pointer by doing FP += <size>.
  MachineInstr *MI = BuildMI(MBB, MBBI, DL, TII.get(Opcode), AVR::R29R28)
                         .addReg(AVR::R29R28, RegState::Kill)
                         .addImm(FrameSize);
  // The SREG implicit def is dead.
  MI->getOperand(3).setIsDead();

  // Write back R29R28 to SP and temporarily disable interrupts.
  BuildMI(MBB, MBBI, DL, TII.get(AVR::SPWRITE), AVR::SP)
      .addReg(AVR::R29R28, RegState::Kill);
}

// Return true if the specified function should have a dedicated frame
// pointer register. This is true if the function meets any of the following
// conditions:
//  - a register has been spilled
//  - has allocas
//  - input arguments are passed using the stack
//
// Notice that strictly this is not a frame pointer because it contains SP after
// frame allocation instead of having the original SP in function entry.
bool AVRFrameLowering::hasFP(const MachineFunction &MF) const {
  const AVRMachineFunctionInfo *FuncInfo = MF.getInfo<AVRMachineFunctionInfo>();

  return (FuncInfo->getHasSpills() || FuncInfo->getHasAllocas() ||
          FuncInfo->getHasStackArgs());
}

bool AVRFrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    const std::vector<CalleeSavedInfo> &CSI,
    const TargetRegisterInfo *TRI) const {
  if (CSI.empty()) {
    return false;
  }

  unsigned CalleeFrameSize = 0;
  DebugLoc DL = MBB.findDebugLoc(MI);
  MachineFunction &MF = *MBB.getParent();
  const AVRSubtarget &STI = MF.getSubtarget<AVRSubtarget>();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  AVRMachineFunctionInfo *AVRFI = MF.getInfo<AVRMachineFunctionInfo>();

  for (unsigned i = CSI.size(); i != 0; --i) {
    unsigned Reg = CSI[i - 1].getReg();
    bool IsNotLiveIn = !MBB.isLiveIn(Reg);

    assert(TRI->getRegSizeInBits(*TRI->getMinimalPhysRegClass(Reg)) == 8 &&
           "Invalid register size");

    // Add the callee-saved register as live-in only if it is not already a
    // live-in register, this usually happens with arguments that are passed
    // through callee-saved registers.
    if (IsNotLiveIn) {
      MBB.addLiveIn(Reg);
    }

    // Do not kill the register when it is an input argument.
    BuildMI(MBB, MI, DL, TII.get(AVR::PUSHRr))
        .addReg(Reg, getKillRegState(IsNotLiveIn))
        .setMIFlag(MachineInstr::FrameSetup);
    ++CalleeFrameSize;
  }

  AVRFI->setCalleeSavedFrameSize(CalleeFrameSize);

  return true;
}

bool AVRFrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    std::vector<CalleeSavedInfo> &CSI,
    const TargetRegisterInfo *TRI) const {
  if (CSI.empty()) {
    return false;
  }

  DebugLoc DL = MBB.findDebugLoc(MI);
  const MachineFunction &MF = *MBB.getParent();
  const AVRSubtarget &STI = MF.getSubtarget<AVRSubtarget>();
  const TargetInstrInfo &TII = *STI.getInstrInfo();

  for (const CalleeSavedInfo &CCSI : CSI) {
    unsigned Reg = CCSI.getReg();

    assert(TRI->getRegSizeInBits(*TRI->getMinimalPhysRegClass(Reg)) == 8 &&
           "Invalid register size");

    BuildMI(MBB, MI, DL, TII.get(AVR::POPRd), Reg);
  }

  return true;
}

/// Replace pseudo store instructions that pass arguments through the stack with
/// real instructions. If insertPushes is true then all instructions are
/// replaced with push instructions, otherwise regular std instructions are
/// inserted.
static void fixStackStores(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MI,
                           const TargetInstrInfo &TII, bool insertPushes) {
  const AVRSubtarget &STI = MBB.getParent()->getSubtarget<AVRSubtarget>();
  const TargetRegisterInfo &TRI = *STI.getRegisterInfo();

  // Iterate through the BB until we hit a call instruction or we reach the end.
  for (auto I = MI, E = MBB.end(); I != E && !I->isCall();) {
    MachineBasicBlock::iterator NextMI = std::next(I);
    MachineInstr &MI = *I;
    unsigned Opcode = I->getOpcode();

    // Only care of pseudo store instructions where SP is the base pointer.
    if (Opcode != AVR::STDSPQRr && Opcode != AVR::STDWSPQRr) {
      I = NextMI;
      continue;
    }

    assert(MI.getOperand(0).getReg() == AVR::SP &&
           "Invalid register, should be SP!");
    if (insertPushes) {
      // Replace this instruction with a push.
      unsigned SrcReg = MI.getOperand(2).getReg();
      bool SrcIsKill = MI.getOperand(2).isKill();

      // We can't use PUSHWRr here because when expanded the order of the new
      // instructions are reversed from what we need. Perform the expansion now.
      if (Opcode == AVR::STDWSPQRr) {
        BuildMI(MBB, I, MI.getDebugLoc(), TII.get(AVR::PUSHRr))
            .addReg(TRI.getSubReg(SrcReg, AVR::sub_hi),
                    getKillRegState(SrcIsKill));
        BuildMI(MBB, I, MI.getDebugLoc(), TII.get(AVR::PUSHRr))
            .addReg(TRI.getSubReg(SrcReg, AVR::sub_lo),
                    getKillRegState(SrcIsKill));
      } else {
        BuildMI(MBB, I, MI.getDebugLoc(), TII.get(AVR::PUSHRr))
            .addReg(SrcReg, getKillRegState(SrcIsKill));
      }

      MI.eraseFromParent();
      I = NextMI;
      continue;
    }

    // Replace this instruction with a regular store. Use Y as the base
    // pointer since it is guaranteed to contain a copy of SP.
    unsigned STOpc =
        (Opcode == AVR::STDWSPQRr) ? AVR::STDWPtrQRr : AVR::STDPtrQRr;

    MI.setDesc(TII.get(STOpc));
    MI.getOperand(0).setReg(AVR::R29R28);

    I = NextMI;
  }
}

MachineBasicBlock::iterator AVRFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MI) const {
  const AVRSubtarget &STI = MF.getSubtarget<AVRSubtarget>();
  const TargetFrameLowering &TFI = *STI.getFrameLowering();
  const AVRInstrInfo &TII = *STI.getInstrInfo();

  // There is nothing to insert when the call frame memory is allocated during
  // function entry. Delete the call frame pseudo and replace all pseudo stores
  // with real store instructions.
  if (TFI.hasReservedCallFrame(MF)) {
    fixStackStores(MBB, MI, TII, false);
    return MBB.erase(MI);
  }

  DebugLoc DL = MI->getDebugLoc();
  unsigned int Opcode = MI->getOpcode();
  int Amount = TII.getFrameSize(*MI);

  // Adjcallstackup does not need to allocate stack space for the call, instead
  // we insert push instructions that will allocate the necessary stack.
  // For adjcallstackdown we convert it into an 'adiw reg, <amt>' handling
  // the read and write of SP in I/O space.
  if (Amount != 0) {
    assert(TFI.getStackAlignment() == 1 && "Unsupported stack alignment");

    if (Opcode == TII.getCallFrameSetupOpcode()) {
      fixStackStores(MBB, MI, TII, true);
    } else {
      assert(Opcode == TII.getCallFrameDestroyOpcode());

      // Select the best opcode to adjust SP based on the offset size.
      unsigned addOpcode;
      if (isUInt<6>(Amount)) {
        addOpcode = AVR::ADIWRdK;
      } else {
        addOpcode = AVR::SUBIWRdK;
        Amount = -Amount;
      }

      // Build the instruction sequence.
      BuildMI(MBB, MI, DL, TII.get(AVR::SPREAD), AVR::R31R30).addReg(AVR::SP);

      MachineInstr *New = BuildMI(MBB, MI, DL, TII.get(addOpcode), AVR::R31R30)
                              .addReg(AVR::R31R30, RegState::Kill)
                              .addImm(Amount);
      New->getOperand(3).setIsDead();

      BuildMI(MBB, MI, DL, TII.get(AVR::SPWRITE), AVR::SP)
          .addReg(AVR::R31R30, RegState::Kill);
    }
  }

  return MBB.erase(MI);
}

void AVRFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                            BitVector &SavedRegs,
                                            RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);

  // If we have a frame pointer, the Y register needs to be saved as well.
  // We don't do that here however - the prologue and epilogue generation
  // code will handle it specially.
}
/// The frame analyzer pass.
///
/// Scans the function for allocas and used arguments
/// that are passed through the stack.
struct AVRFrameAnalyzer : public MachineFunctionPass {
  static char ID;
  AVRFrameAnalyzer() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) {
    const MachineFrameInfo &MFI = MF.getFrameInfo();
    AVRMachineFunctionInfo *FuncInfo = MF.getInfo<AVRMachineFunctionInfo>();

    // If there are no fixed frame indexes during this stage it means there
    // are allocas present in the function.
    if (MFI.getNumObjects() != MFI.getNumFixedObjects()) {
      // Check for the type of allocas present in the function. We only care
      // about fixed size allocas so do not give false positives if only
      // variable sized allocas are present.
      for (unsigned i = 0, e = MFI.getObjectIndexEnd(); i != e; ++i) {
        // Variable sized objects have size 0.
        if (MFI.getObjectSize(i)) {
          FuncInfo->setHasAllocas(true);
          break;
        }
      }
    }

    // If there are fixed frame indexes present, scan the function to see if
    // they are really being used.
    if (MFI.getNumFixedObjects() == 0) {
      return false;
    }

    // Ok fixed frame indexes present, now scan the function to see if they
    // are really being used, otherwise we can ignore them.
    for (const MachineBasicBlock &BB : MF) {
      for (const MachineInstr &MI : BB) {
        int Opcode = MI.getOpcode();

        if ((Opcode != AVR::LDDRdPtrQ) && (Opcode != AVR::LDDWRdPtrQ) &&
            (Opcode != AVR::STDPtrQRr) && (Opcode != AVR::STDWPtrQRr)) {
          continue;
        }

        for (const MachineOperand &MO : MI.operands()) {
          if (!MO.isFI()) {
            continue;
          }

          if (MFI.isFixedObjectIndex(MO.getIndex())) {
            FuncInfo->setHasStackArgs(true);
            return false;
          }
        }
      }
    }

    return false;
  }

  StringRef getPassName() const { return "AVR Frame Analyzer"; }
};

char AVRFrameAnalyzer::ID = 0;

/// Creates instance of the frame analyzer pass.
FunctionPass *createAVRFrameAnalyzerPass() { return new AVRFrameAnalyzer(); }

/// Create the Dynalloca Stack Pointer Save/Restore pass.
/// Insert a copy of SP before allocating the dynamic stack memory and restore
/// it in function exit to restore the original SP state. This avoids the need
/// of reserving a register pair for a frame pointer.
struct AVRDynAllocaSR : public MachineFunctionPass {
  static char ID;
  AVRDynAllocaSR() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) {
    // Early exit when there are no variable sized objects in the function.
    if (!MF.getFrameInfo().hasVarSizedObjects()) {
      return false;
    }

    const AVRSubtarget &STI = MF.getSubtarget<AVRSubtarget>();
    const TargetInstrInfo &TII = *STI.getInstrInfo();
    MachineBasicBlock &EntryMBB = MF.front();
    MachineBasicBlock::iterator MBBI = EntryMBB.begin();
    DebugLoc DL = EntryMBB.findDebugLoc(MBBI);

    unsigned SPCopy =
        MF.getRegInfo().createVirtualRegister(&AVR::DREGSRegClass);

    // Create a copy of SP in function entry before any dynallocas are
    // inserted.
    BuildMI(EntryMBB, MBBI, DL, TII.get(AVR::COPY), SPCopy).addReg(AVR::SP);

    // Restore SP in all exit basic blocks.
    for (MachineBasicBlock &MBB : MF) {
      // If last instruction is a return instruction, add a restore copy.
      if (!MBB.empty() && MBB.back().isReturn()) {
        MBBI = MBB.getLastNonDebugInstr();
        DL = MBBI->getDebugLoc();
        BuildMI(MBB, MBBI, DL, TII.get(AVR::COPY), AVR::SP)
            .addReg(SPCopy, RegState::Kill);
      }
    }

    return true;
  }

  StringRef getPassName() const {
    return "AVR dynalloca stack pointer save/restore";
  }
};

char AVRDynAllocaSR::ID = 0;

/// createAVRDynAllocaSRPass - returns an instance of the dynalloca stack
/// pointer save/restore pass.
FunctionPass *createAVRDynAllocaSRPass() { return new AVRDynAllocaSR(); }

} // end of namespace llvm

