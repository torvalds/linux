//===----------------------- R600FrameLowering.cpp ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//==-----------------------------------------------------------------------===//

#include "R600FrameLowering.h"
#include "AMDGPUSubtarget.h"
#include "R600RegisterInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

R600FrameLowering::~R600FrameLowering() = default;

/// \returns The number of registers allocated for \p FI.
int R600FrameLowering::getFrameIndexReference(const MachineFunction &MF,
                                              int FI,
                                              unsigned &FrameReg) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const R600RegisterInfo *RI
    = MF.getSubtarget<R600Subtarget>().getRegisterInfo();

  // Fill in FrameReg output argument.
  FrameReg = RI->getFrameRegister(MF);

  // Start the offset at 2 so we don't overwrite work group information.
  // FIXME: We should only do this when the shader actually uses this
  // information.
  unsigned OffsetBytes = 2 * (getStackWidth(MF) * 4);
  int UpperBound = FI == -1 ? MFI.getNumObjects() : FI;

  for (int i = MFI.getObjectIndexBegin(); i < UpperBound; ++i) {
    OffsetBytes = alignTo(OffsetBytes, MFI.getObjectAlignment(i));
    OffsetBytes += MFI.getObjectSize(i);
    // Each register holds 4 bytes, so we must always align the offset to at
    // least 4 bytes, so that 2 frame objects won't share the same register.
    OffsetBytes = alignTo(OffsetBytes, 4);
  }

  if (FI != -1)
    OffsetBytes = alignTo(OffsetBytes, MFI.getObjectAlignment(FI));

  return OffsetBytes / (getStackWidth(MF) * 4);
}
