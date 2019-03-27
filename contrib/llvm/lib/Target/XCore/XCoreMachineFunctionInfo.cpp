//===-- XCoreMachineFunctionInfo.cpp - XCore machine function info --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "XCoreMachineFunctionInfo.h"
#include "XCoreInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Function.h"

using namespace llvm;

void XCoreFunctionInfo::anchor() { }

bool XCoreFunctionInfo::isLargeFrame(const MachineFunction &MF) const {
  if (CachedEStackSize == -1) {
    CachedEStackSize = MF.getFrameInfo().estimateStackSize(MF);
  }
  // isLargeFrame() is used when deciding if spill slots should be added to
  // allow eliminateFrameIndex() to scavenge registers.
  // This is only required when there is no FP and offsets are greater than
  // ~256KB (~64Kwords). Thus only for code run on the emulator!
  //
  // The arbitrary value of 0xf000 allows frames of up to ~240KB before spill
  // slots are added for the use of eliminateFrameIndex() register scavenging.
  // For frames less than 240KB, it is assumed that there will be less than
  // 16KB of function arguments.
  return CachedEStackSize > 0xf000;
}

int XCoreFunctionInfo::createLRSpillSlot(MachineFunction &MF) {
  if (LRSpillSlotSet) {
    return LRSpillSlot;
  }
  const TargetRegisterClass &RC = XCore::GRRegsRegClass;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  if (! MF.getFunction().isVarArg()) {
    // A fixed offset of 0 allows us to save / restore LR using entsp / retsp.
    LRSpillSlot = MFI.CreateFixedObject(TRI.getSpillSize(RC), 0, true);
  } else {
    LRSpillSlot = MFI.CreateStackObject(TRI.getSpillSize(RC),
                                        TRI.getSpillAlignment(RC), true);
  }
  LRSpillSlotSet = true;
  return LRSpillSlot;
}

int XCoreFunctionInfo::createFPSpillSlot(MachineFunction &MF) {
  if (FPSpillSlotSet) {
    return FPSpillSlot;
  }
  const TargetRegisterClass &RC = XCore::GRRegsRegClass;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  FPSpillSlot = MFI.CreateStackObject(TRI.getSpillSize(RC),
                                      TRI.getSpillAlignment(RC), true);
  FPSpillSlotSet = true;
  return FPSpillSlot;
}

const int* XCoreFunctionInfo::createEHSpillSlot(MachineFunction &MF) {
  if (EHSpillSlotSet) {
    return EHSpillSlot;
  }
  const TargetRegisterClass &RC = XCore::GRRegsRegClass;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  unsigned Size = TRI.getSpillSize(RC);
  unsigned Align = TRI.getSpillAlignment(RC);
  EHSpillSlot[0] = MFI.CreateStackObject(Size, Align, true);
  EHSpillSlot[1] = MFI.CreateStackObject(Size, Align, true);
  EHSpillSlotSet = true;
  return EHSpillSlot;
}

