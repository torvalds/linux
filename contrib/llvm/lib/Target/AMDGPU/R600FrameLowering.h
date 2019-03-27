//===--------------------- R600FrameLowering.h ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_R600FRAMELOWERING_H
#define LLVM_LIB_TARGET_AMDGPU_R600FRAMELOWERING_H

#include "AMDGPUFrameLowering.h"

namespace llvm {

class R600FrameLowering : public AMDGPUFrameLowering {
public:
  R600FrameLowering(StackDirection D, unsigned StackAl, int LAO,
                    unsigned TransAl = 1) :
    AMDGPUFrameLowering(D, StackAl, LAO, TransAl) {}
  ~R600FrameLowering() override;

  void emitPrologue(MachineFunction &MF,
                    MachineBasicBlock &MBB) const override {}
  void emitEpilogue(MachineFunction &MF,
                    MachineBasicBlock &MBB) const override {}
  int getFrameIndexReference(const MachineFunction &MF, int FI,
                             unsigned &FrameReg) const override;

  bool hasFP(const MachineFunction &MF) const override {
    return false;
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_R600FRAMELOWERING_H
