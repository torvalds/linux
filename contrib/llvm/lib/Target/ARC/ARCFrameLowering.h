//===- ARCFrameLowering.h - Define frame lowering for ARC -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class implements the ARC specific frame lowering.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARC_ARCFRAMELOWERING_H
#define LLVM_LIB_TARGET_ARC_ARCFRAMELOWERING_H

#include "ARC.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {

class MachineFunction;
class ARCSubtarget;
class ARCInstrInfo;

class ARCFrameLowering : public TargetFrameLowering {
public:
  ARCFrameLowering(const ARCSubtarget &st)
      : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, 4, 0), ST(st) {
  }

  /// Insert Prologue into the function.
  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  /// Insert Epilogue into the function.
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  /// Add explicit callee save registers.
  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                            RegScavenger *RS) const override;

  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 const std::vector<CalleeSavedInfo> &CSI,
                                 const TargetRegisterInfo *TRI) const override;

  bool
  restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI,
                              std::vector<CalleeSavedInfo> &CSI,
                              const TargetRegisterInfo *TRI) const override;

  void processFunctionBeforeFrameFinalized(MachineFunction &MF,
                                           RegScavenger *RS) const override;

  bool hasFP(const MachineFunction &MF) const override;

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I) const override;

  bool assignCalleeSavedSpillSlots(
      llvm::MachineFunction &, const llvm::TargetRegisterInfo *,
      std::vector<llvm::CalleeSavedInfo> &) const override;

private:
  void adjustStackToMatchRecords(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 bool allocate) const;

  const ARCSubtarget &ST;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARC_ARCFRAMELOWERING_H
