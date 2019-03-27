//===- MipsSEFrameLowering.h - Mips32/64 frame lowering ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPSSEFRAMELOWERING_H
#define LLVM_LIB_TARGET_MIPS_MIPSSEFRAMELOWERING_H

#include "MipsFrameLowering.h"
#include <vector>

namespace llvm {

class MachineBasicBlock;
class MachineFunction;
class MipsSubtarget;

class MipsSEFrameLowering : public MipsFrameLowering {
public:
  explicit MipsSEFrameLowering(const MipsSubtarget &STI);

  /// emitProlog/emitEpilog - These methods insert prolog and epilog code into
  /// the function.
  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  int getFrameIndexReference(const MachineFunction &MF, int FI,
                             unsigned &FrameReg) const override;

  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 const std::vector<CalleeSavedInfo> &CSI,
                                 const TargetRegisterInfo *TRI) const override;

  bool hasReservedCallFrame(const MachineFunction &MF) const override;

  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                            RegScavenger *RS) const override;

private:
  void emitInterruptEpilogueStub(MachineFunction &MF,
                                 MachineBasicBlock &MBB) const;
  void emitInterruptPrologueStub(MachineFunction &MF,
                                 MachineBasicBlock &MBB) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_MIPS_MIPSSEFRAMELOWERING_H
