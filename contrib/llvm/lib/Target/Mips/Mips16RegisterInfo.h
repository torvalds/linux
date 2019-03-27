//===-- Mips16RegisterInfo.h - Mips16 Register Information ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Mips16 implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPS16REGISTERINFO_H
#define LLVM_LIB_TARGET_MIPS_MIPS16REGISTERINFO_H

#include "MipsRegisterInfo.h"

namespace llvm {
class Mips16InstrInfo;

class Mips16RegisterInfo : public MipsRegisterInfo {
public:
  Mips16RegisterInfo();

  bool requiresRegisterScavenging(const MachineFunction &MF) const override;

  bool requiresFrameIndexScavenging(const MachineFunction &MF) const override;

  bool useFPForScavengingIndex(const MachineFunction &MF) const override;

  bool saveScavengerRegister(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator I,
                                     MachineBasicBlock::iterator &UseMI,
                                     const TargetRegisterClass *RC,
                                     unsigned Reg) const override;

  const TargetRegisterClass *intRegClass(unsigned Size) const override;

private:
  void eliminateFI(MachineBasicBlock::iterator II, unsigned OpNo,
                   int FrameIndex, uint64_t StackSize,
                   int64_t SPOffset) const override;
};

} // end namespace llvm

#endif
