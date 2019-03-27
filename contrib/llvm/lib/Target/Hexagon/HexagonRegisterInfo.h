//==- HexagonRegisterInfo.h - Hexagon Register Information Impl --*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Hexagon implementation of the TargetRegisterInfo
// class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONREGISTERINFO_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONREGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "HexagonGenRegisterInfo.inc"

namespace llvm {

namespace Hexagon {
  // Generic (pseudo) subreg indices for use with getHexagonSubRegIndex.
  enum { ps_sub_lo = 0, ps_sub_hi = 1 };
}

class HexagonRegisterInfo : public HexagonGenRegisterInfo {
public:
  HexagonRegisterInfo(unsigned HwMode);

  /// Code Generation virtual methods...
  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF)
        const override;
  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
        CallingConv::ID) const override;

  BitVector getReservedRegs(const MachineFunction &MF) const override;

  void eliminateFrameIndex(MachineBasicBlock::iterator II, int SPAdj,
        unsigned FIOperandNum, RegScavenger *RS = nullptr) const override;

  /// Returns true since we may need scavenging for a temporary register
  /// when generating hardware loop instructions.
  bool requiresRegisterScavenging(const MachineFunction &MF) const override {
    return true;
  }

  /// Returns true. Spill code for predicate registers might need an extra
  /// register.
  bool requiresFrameIndexScavenging(const MachineFunction &MF) const override {
    return true;
  }

  /// Returns true if the frame pointer is valid.
  bool useFPForScavengingIndex(const MachineFunction &MF) const override;

  bool trackLivenessAfterRegAlloc(const MachineFunction &MF) const override {
    return true;
  }

  bool shouldCoalesce(MachineInstr *MI, const TargetRegisterClass *SrcRC,
        unsigned SubReg, const TargetRegisterClass *DstRC, unsigned DstSubReg,
        const TargetRegisterClass *NewRC, LiveIntervals &LIS) const override;

  // Debug information queries.
  unsigned getRARegister() const;
  unsigned getFrameRegister(const MachineFunction &MF) const override;
  unsigned getFrameRegister() const;
  unsigned getStackRegister() const;

  unsigned getHexagonSubRegIndex(const TargetRegisterClass &RC,
        unsigned GenIdx) const;

  const MCPhysReg *getCallerSavedRegs(const MachineFunction *MF,
        const TargetRegisterClass *RC) const;

  unsigned getFirstCallerSavedNonParamReg() const;

  const TargetRegisterClass *
  getPointerRegClass(const MachineFunction &MF,
                     unsigned Kind = 0) const override;

  bool isEHReturnCalleeSaveReg(unsigned Reg) const;
};

} // end namespace llvm

#endif
