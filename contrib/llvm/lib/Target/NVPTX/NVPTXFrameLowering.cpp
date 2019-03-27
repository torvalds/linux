//=======- NVPTXFrameLowering.cpp - NVPTX Frame Information ---*- C++ -*-=====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the NVPTX implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "NVPTXFrameLowering.h"
#include "NVPTX.h"
#include "NVPTXRegisterInfo.h"
#include "NVPTXSubtarget.h"
#include "NVPTXTargetMachine.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/MC/MachineLocation.h"

using namespace llvm;

NVPTXFrameLowering::NVPTXFrameLowering()
    : TargetFrameLowering(TargetFrameLowering::StackGrowsUp, 8, 0) {}

bool NVPTXFrameLowering::hasFP(const MachineFunction &MF) const { return true; }

void NVPTXFrameLowering::emitPrologue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  if (MF.getFrameInfo().hasStackObjects()) {
    assert(&MF.front() == &MBB && "Shrink-wrapping not yet supported");
    MachineInstr *MI = &MBB.front();
    MachineRegisterInfo &MR = MF.getRegInfo();

    // This instruction really occurs before first instruction
    // in the BB, so giving it no debug location.
    DebugLoc dl = DebugLoc();

    // Emits
    //   mov %SPL, %depot;
    //   cvta.local %SP, %SPL;
    // for local address accesses in MF.
    bool Is64Bit =
        static_cast<const NVPTXTargetMachine &>(MF.getTarget()).is64Bit();
    unsigned CvtaLocalOpcode =
        (Is64Bit ? NVPTX::cvta_local_yes_64 : NVPTX::cvta_local_yes);
    unsigned MovDepotOpcode =
        (Is64Bit ? NVPTX::MOV_DEPOT_ADDR_64 : NVPTX::MOV_DEPOT_ADDR);
    if (!MR.use_empty(NVPTX::VRFrame)) {
      // If %SP is not used, do not bother emitting "cvta.local %SP, %SPL".
      MI = BuildMI(MBB, MI, dl,
                   MF.getSubtarget().getInstrInfo()->get(CvtaLocalOpcode),
                   NVPTX::VRFrame)
               .addReg(NVPTX::VRFrameLocal);
    }
    BuildMI(MBB, MI, dl, MF.getSubtarget().getInstrInfo()->get(MovDepotOpcode),
            NVPTX::VRFrameLocal)
        .addImm(MF.getFunctionNumber());
  }
}

int NVPTXFrameLowering::getFrameIndexReference(const MachineFunction &MF,
                                               int FI,
                                               unsigned &FrameReg) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  FrameReg = NVPTX::VRDepot;
  return MFI.getObjectOffset(FI) - getOffsetOfLocalArea();
}

void NVPTXFrameLowering::emitEpilogue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {}

// This function eliminates ADJCALLSTACKDOWN,
// ADJCALLSTACKUP pseudo instructions
MachineBasicBlock::iterator NVPTXFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  // Simply discard ADJCALLSTACKDOWN,
  // ADJCALLSTACKUP instructions.
  return MBB.erase(I);
}
