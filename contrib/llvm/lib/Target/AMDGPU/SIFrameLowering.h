//===--------------------- SIFrameLowering.h --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_SIFRAMELOWERING_H
#define LLVM_LIB_TARGET_AMDGPU_SIFRAMELOWERING_H

#include "AMDGPUFrameLowering.h"

namespace llvm {

class SIInstrInfo;
class SIMachineFunctionInfo;
class SIRegisterInfo;
class GCNSubtarget;

class SIFrameLowering final : public AMDGPUFrameLowering {
public:
  SIFrameLowering(StackDirection D, unsigned StackAl, int LAO,
                  unsigned TransAl = 1) :
    AMDGPUFrameLowering(D, StackAl, LAO, TransAl) {}
  ~SIFrameLowering() override = default;

  void emitEntryFunctionPrologue(MachineFunction &MF,
                                 MachineBasicBlock &MBB) const;
  void emitPrologue(MachineFunction &MF,
                    MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF,
                    MachineBasicBlock &MBB) const override;
  int getFrameIndexReference(const MachineFunction &MF, int FI,
                             unsigned &FrameReg) const override;

  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                            RegScavenger *RS = nullptr) const override;

  void processFunctionBeforeFrameFinalized(
    MachineFunction &MF,
    RegScavenger *RS = nullptr) const override;

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF,
                                MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI) const override;

private:
  void emitFlatScratchInit(const GCNSubtarget &ST,
                           MachineFunction &MF,
                           MachineBasicBlock &MBB) const;

  unsigned getReservedPrivateSegmentBufferReg(
    const GCNSubtarget &ST,
    const SIInstrInfo *TII,
    const SIRegisterInfo *TRI,
    SIMachineFunctionInfo *MFI,
    MachineFunction &MF) const;

  std::pair<unsigned, unsigned> getReservedPrivateSegmentWaveByteOffsetReg(
    const GCNSubtarget &ST,
    const SIInstrInfo *TII,
    const SIRegisterInfo *TRI,
    SIMachineFunctionInfo *MFI,
    MachineFunction &MF) const;

  /// Emits debugger prologue.
  void emitDebuggerPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const;

  // Emit scratch setup code for AMDPAL or Mesa, assuming ResourceRegUsed is set.
  void emitEntryFunctionScratchSetup(const GCNSubtarget &ST, MachineFunction &MF,
      MachineBasicBlock &MBB, SIMachineFunctionInfo *MFI,
      MachineBasicBlock::iterator I, unsigned PreloadedPrivateBufferReg,
      unsigned ScratchRsrcReg) const;

public:
  bool hasFP(const MachineFunction &MF) const override;
  bool hasSP(const MachineFunction &MF) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_SIFRAMELOWERING_H
