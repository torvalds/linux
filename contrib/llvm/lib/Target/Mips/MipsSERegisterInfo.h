//===-- MipsSERegisterInfo.h - Mips32/64 Register Information ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Mips32/64 implementation of the TargetRegisterInfo
// class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPSSEREGISTERINFO_H
#define LLVM_LIB_TARGET_MIPS_MIPSSEREGISTERINFO_H

#include "MipsRegisterInfo.h"

namespace llvm {
class MipsSEInstrInfo;

class MipsSERegisterInfo : public MipsRegisterInfo {
public:
  MipsSERegisterInfo();

  bool requiresRegisterScavenging(const MachineFunction &MF) const override;

  bool requiresFrameIndexScavenging(const MachineFunction &MF) const override;

  const TargetRegisterClass *intRegClass(unsigned Size) const override;

private:
  void eliminateFI(MachineBasicBlock::iterator II, unsigned OpNo,
                   int FrameIndex, uint64_t StackSize,
                   int64_t SPOffset) const override;
};

} // end namespace llvm

#endif
