//===-- HexagonRegisterInfo.cpp - Hexagon Register Information ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Hexagon implementation of the TargetRegisterInfo
// class.
//
//===----------------------------------------------------------------------===//

#include "HexagonRegisterInfo.h"
#include "Hexagon.h"
#include "HexagonMachineFunctionInfo.h"
#include "HexagonSubtarget.h"
#include "HexagonTargetMachine.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/MC/MachineLocation.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

#define GET_REGINFO_TARGET_DESC
#include "HexagonGenRegisterInfo.inc"

using namespace llvm;

HexagonRegisterInfo::HexagonRegisterInfo(unsigned HwMode)
    : HexagonGenRegisterInfo(Hexagon::R31, 0/*DwarfFlavor*/, 0/*EHFlavor*/,
                             0/*PC*/, HwMode) {}


bool HexagonRegisterInfo::isEHReturnCalleeSaveReg(unsigned R) const {
  return R == Hexagon::R0 || R == Hexagon::R1 || R == Hexagon::R2 ||
         R == Hexagon::R3 || R == Hexagon::D0 || R == Hexagon::D1;
}

const MCPhysReg *
HexagonRegisterInfo::getCallerSavedRegs(const MachineFunction *MF,
      const TargetRegisterClass *RC) const {
  using namespace Hexagon;

  static const MCPhysReg Int32[] = {
    R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, 0
  };
  static const MCPhysReg Int64[] = {
    D0, D1, D2, D3, D4, D5, D6, D7, 0
  };
  static const MCPhysReg Pred[] = {
    P0, P1, P2, P3, 0
  };
  static const MCPhysReg VecSgl[] = {
     V0,  V1,  V2,  V3,  V4,  V5,  V6,  V7,  V8,  V9, V10, V11, V12, V13,
    V14, V15, V16, V17, V18, V19, V20, V21, V22, V23, V24, V25, V26, V27,
    V28, V29, V30, V31,   0
  };
  static const MCPhysReg VecDbl[] = {
    W0, W1, W2, W3, W4, W5, W6, W7, W8, W9, W10, W11, W12, W13, W14, W15, 0
  };

  switch (RC->getID()) {
    case IntRegsRegClassID:
      return Int32;
    case DoubleRegsRegClassID:
      return Int64;
    case PredRegsRegClassID:
      return Pred;
    case HvxVRRegClassID:
      return VecSgl;
    case HvxWRRegClassID:
      return VecDbl;
    default:
      break;
  }

  static const MCPhysReg Empty[] = { 0 };
#ifndef NDEBUG
  dbgs() << "Register class: " << getRegClassName(RC) << "\n";
#endif
  llvm_unreachable("Unexpected register class");
  return Empty;
}


const MCPhysReg *
HexagonRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  static const MCPhysReg CalleeSavedRegsV3[] = {
    Hexagon::R16,   Hexagon::R17,   Hexagon::R18,   Hexagon::R19,
    Hexagon::R20,   Hexagon::R21,   Hexagon::R22,   Hexagon::R23,
    Hexagon::R24,   Hexagon::R25,   Hexagon::R26,   Hexagon::R27, 0
  };

  // Functions that contain a call to __builtin_eh_return also save the first 4
  // parameter registers.
  static const MCPhysReg CalleeSavedRegsV3EHReturn[] = {
    Hexagon::R0,    Hexagon::R1,    Hexagon::R2,    Hexagon::R3,
    Hexagon::R16,   Hexagon::R17,   Hexagon::R18,   Hexagon::R19,
    Hexagon::R20,   Hexagon::R21,   Hexagon::R22,   Hexagon::R23,
    Hexagon::R24,   Hexagon::R25,   Hexagon::R26,   Hexagon::R27, 0
  };

  bool HasEHReturn = MF->getInfo<HexagonMachineFunctionInfo>()->hasEHReturn();

  return HasEHReturn ? CalleeSavedRegsV3EHReturn : CalleeSavedRegsV3;
}


const uint32_t *HexagonRegisterInfo::getCallPreservedMask(
      const MachineFunction &MF, CallingConv::ID) const {
  return HexagonCSR_RegMask;
}


BitVector HexagonRegisterInfo::getReservedRegs(const MachineFunction &MF)
  const {
  BitVector Reserved(getNumRegs());
  Reserved.set(Hexagon::R29);
  Reserved.set(Hexagon::R30);
  Reserved.set(Hexagon::R31);
  Reserved.set(Hexagon::VTMP);

  // Guest registers.
  Reserved.set(Hexagon::GELR);        // G0
  Reserved.set(Hexagon::GSR);         // G1
  Reserved.set(Hexagon::GOSP);        // G2
  Reserved.set(Hexagon::G3);          // G3

  // Control registers.
  Reserved.set(Hexagon::SA0);         // C0
  Reserved.set(Hexagon::LC0);         // C1
  Reserved.set(Hexagon::SA1);         // C2
  Reserved.set(Hexagon::LC1);         // C3
  Reserved.set(Hexagon::P3_0);        // C4
  Reserved.set(Hexagon::USR);         // C8
  Reserved.set(Hexagon::PC);          // C9
  Reserved.set(Hexagon::UGP);         // C10
  Reserved.set(Hexagon::GP);          // C11
  Reserved.set(Hexagon::CS0);         // C12
  Reserved.set(Hexagon::CS1);         // C13
  Reserved.set(Hexagon::UPCYCLELO);   // C14
  Reserved.set(Hexagon::UPCYCLEHI);   // C15
  Reserved.set(Hexagon::FRAMELIMIT);  // C16
  Reserved.set(Hexagon::FRAMEKEY);    // C17
  Reserved.set(Hexagon::PKTCOUNTLO);  // C18
  Reserved.set(Hexagon::PKTCOUNTHI);  // C19
  Reserved.set(Hexagon::UTIMERLO);    // C30
  Reserved.set(Hexagon::UTIMERHI);    // C31
  // Out of the control registers, only C8 is explicitly defined in
  // HexagonRegisterInfo.td. If others are defined, make sure to add
  // them here as well.
  Reserved.set(Hexagon::C8);
  Reserved.set(Hexagon::USR_OVF);

  if (MF.getSubtarget<HexagonSubtarget>().hasReservedR19())
    Reserved.set(Hexagon::R19);

  for (int x = Reserved.find_first(); x >= 0; x = Reserved.find_next(x))
    markSuperRegs(Reserved, x);

  return Reserved;
}


void HexagonRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                              int SPAdj, unsigned FIOp,
                                              RegScavenger *RS) const {
  //
  // Hexagon_TODO: Do we need to enforce this for Hexagon?
  assert(SPAdj == 0 && "Unexpected");

  MachineInstr &MI = *II;
  MachineBasicBlock &MB = *MI.getParent();
  MachineFunction &MF = *MB.getParent();
  auto &HST = MF.getSubtarget<HexagonSubtarget>();
  auto &HII = *HST.getInstrInfo();
  auto &HFI = *HST.getFrameLowering();

  unsigned BP = 0;
  int FI = MI.getOperand(FIOp).getIndex();
  // Select the base pointer (BP) and calculate the actual offset from BP
  // to the beginning of the object at index FI.
  int Offset = HFI.getFrameIndexReference(MF, FI, BP);
  // Add the offset from the instruction.
  int RealOffset = Offset + MI.getOperand(FIOp+1).getImm();
  bool IsKill = false;

  unsigned Opc = MI.getOpcode();
  switch (Opc) {
    case Hexagon::PS_fia:
      MI.setDesc(HII.get(Hexagon::A2_addi));
      MI.getOperand(FIOp).ChangeToImmediate(RealOffset);
      MI.RemoveOperand(FIOp+1);
      return;
    case Hexagon::PS_fi:
      // Set up the instruction for updating below.
      MI.setDesc(HII.get(Hexagon::A2_addi));
      break;
  }

  if (!HII.isValidOffset(Opc, RealOffset, this)) {
    // If the offset is not valid, calculate the address in a temporary
    // register and use it with offset 0.
    auto &MRI = MF.getRegInfo();
    unsigned TmpR = MRI.createVirtualRegister(&Hexagon::IntRegsRegClass);
    const DebugLoc &DL = MI.getDebugLoc();
    BuildMI(MB, II, DL, HII.get(Hexagon::A2_addi), TmpR)
      .addReg(BP)
      .addImm(RealOffset);
    BP = TmpR;
    RealOffset = 0;
    IsKill = true;
  }

  MI.getOperand(FIOp).ChangeToRegister(BP, false, false, IsKill);
  MI.getOperand(FIOp+1).ChangeToImmediate(RealOffset);
}


bool HexagonRegisterInfo::shouldCoalesce(MachineInstr *MI,
      const TargetRegisterClass *SrcRC, unsigned SubReg,
      const TargetRegisterClass *DstRC, unsigned DstSubReg,
      const TargetRegisterClass *NewRC, LiveIntervals &LIS) const {
  // Coalescing will extend the live interval of the destination register.
  // If the destination register is a vector pair, avoid introducing function
  // calls into the interval, since it could result in a spilling of a pair
  // instead of a single vector.
  MachineFunction &MF = *MI->getParent()->getParent();
  const HexagonSubtarget &HST = MF.getSubtarget<HexagonSubtarget>();
  if (!HST.useHVXOps() || NewRC->getID() != Hexagon::HvxWRRegClass.getID())
    return true;
  bool SmallSrc = SrcRC->getID() == Hexagon::HvxVRRegClass.getID();
  bool SmallDst = DstRC->getID() == Hexagon::HvxVRRegClass.getID();
  if (!SmallSrc && !SmallDst)
    return true;

  unsigned DstReg = MI->getOperand(0).getReg();
  unsigned SrcReg = MI->getOperand(1).getReg();
  const SlotIndexes &Indexes = *LIS.getSlotIndexes();
  auto HasCall = [&Indexes] (const LiveInterval::Segment &S) {
    for (SlotIndex I = S.start.getBaseIndex(), E = S.end.getBaseIndex();
         I != E; I = I.getNextIndex()) {
      if (const MachineInstr *MI = Indexes.getInstructionFromIndex(I))
        if (MI->isCall())
          return true;
    }
    return false;
  };

  if (SmallSrc == SmallDst) {
    // Both must be true, because the case for both being false was
    // checked earlier. Both registers will be coalesced into a register
    // of a wider class (HvxWR), and we don't want its live range to
    // span over calls.
    return !any_of(LIS.getInterval(DstReg), HasCall) &&
           !any_of(LIS.getInterval(SrcReg), HasCall);
  }

  // If one register is large (HvxWR) and the other is small (HvxVR), then
  // coalescing is ok if the large is already live across a function call,
  // or if the small one is not.
  unsigned SmallReg = SmallSrc ? SrcReg : DstReg;
  unsigned LargeReg = SmallSrc ? DstReg : SrcReg;
  return  any_of(LIS.getInterval(LargeReg), HasCall) ||
         !any_of(LIS.getInterval(SmallReg), HasCall);
}


unsigned HexagonRegisterInfo::getRARegister() const {
  return Hexagon::R31;
}


unsigned HexagonRegisterInfo::getFrameRegister(const MachineFunction
                                               &MF) const {
  const HexagonFrameLowering *TFI = getFrameLowering(MF);
  if (TFI->hasFP(MF))
    return getFrameRegister();
  return getStackRegister();
}


unsigned HexagonRegisterInfo::getFrameRegister() const {
  return Hexagon::R30;
}


unsigned HexagonRegisterInfo::getStackRegister() const {
  return Hexagon::R29;
}


unsigned HexagonRegisterInfo::getHexagonSubRegIndex(
      const TargetRegisterClass &RC, unsigned GenIdx) const {
  assert(GenIdx == Hexagon::ps_sub_lo || GenIdx == Hexagon::ps_sub_hi);

  static const unsigned ISub[] = { Hexagon::isub_lo, Hexagon::isub_hi };
  static const unsigned VSub[] = { Hexagon::vsub_lo, Hexagon::vsub_hi };
  static const unsigned WSub[] = { Hexagon::wsub_lo, Hexagon::wsub_hi };

  switch (RC.getID()) {
    case Hexagon::CtrRegs64RegClassID:
    case Hexagon::DoubleRegsRegClassID:
      return ISub[GenIdx];
    case Hexagon::HvxWRRegClassID:
      return VSub[GenIdx];
    case Hexagon::HvxVQRRegClassID:
      return WSub[GenIdx];
  }

  if (const TargetRegisterClass *SuperRC = *RC.getSuperClasses())
    return getHexagonSubRegIndex(*SuperRC, GenIdx);

  llvm_unreachable("Invalid register class");
}

bool HexagonRegisterInfo::useFPForScavengingIndex(const MachineFunction &MF)
      const {
  return MF.getSubtarget<HexagonSubtarget>().getFrameLowering()->hasFP(MF);
}

const TargetRegisterClass *
HexagonRegisterInfo::getPointerRegClass(const MachineFunction &MF,
                                        unsigned Kind) const {
  return &Hexagon::IntRegsRegClass;
}

unsigned HexagonRegisterInfo::getFirstCallerSavedNonParamReg() const {
  return Hexagon::R6;
}

