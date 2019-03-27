//==-- AArch64FrameLowering.h - TargetFrameLowering for AArch64 --*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64FRAMELOWERING_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64FRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {

class AArch64FrameLowering : public TargetFrameLowering {
public:
  explicit AArch64FrameLowering()
      : TargetFrameLowering(StackGrowsDown, 16, 0, 16,
                            true /*StackRealignable*/) {}

  void emitCalleeSavedFrameMoves(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MBBI) const;

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I) const override;

  /// emitProlog/emitEpilog - These methods insert prolog and epilog code into
  /// the function.
  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  bool canUseAsPrologue(const MachineBasicBlock &MBB) const override;

  int getFrameIndexReference(const MachineFunction &MF, int FI,
                             unsigned &FrameReg) const override;
  int resolveFrameIndexReference(const MachineFunction &MF, int FI,
                                 unsigned &FrameReg,
                                 bool PreferFP = false) const;
  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 const std::vector<CalleeSavedInfo> &CSI,
                                 const TargetRegisterInfo *TRI) const override;

  bool restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator MI,
                                  std::vector<CalleeSavedInfo> &CSI,
                                  const TargetRegisterInfo *TRI) const override;

  /// Can this function use the red zone for local allocations.
  bool canUseRedZone(const MachineFunction &MF) const;

  bool hasFP(const MachineFunction &MF) const override;
  bool hasReservedCallFrame(const MachineFunction &MF) const override;

  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                            RegScavenger *RS) const override;

  /// Returns true if the target will correctly handle shrink wrapping.
  bool enableShrinkWrapping(const MachineFunction &MF) const override {
    return true;
  }

  bool enableStackSlotScavenging(const MachineFunction &MF) const override;

  void processFunctionBeforeFrameFinalized(MachineFunction &MF,
                                             RegScavenger *RS) const override;

  unsigned getWinEHParentFrameOffset(const MachineFunction &MF) const override;

  unsigned getWinEHFuncletFrameSize(const MachineFunction &MF) const;

  int getFrameIndexReferencePreferSP(const MachineFunction &MF, int FI,
                                     unsigned &FrameReg,
                                     bool IgnoreSPUpdates) const override;

private:
  bool shouldCombineCSRLocalStackBump(MachineFunction &MF,
                                      unsigned StackBumpBytes) const;
};

} // End llvm namespace

#endif
