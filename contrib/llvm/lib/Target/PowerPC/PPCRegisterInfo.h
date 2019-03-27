//===-- PPCRegisterInfo.h - PowerPC Register Information Impl ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the PowerPC implementation of the TargetRegisterInfo
// class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_POWERPC_PPCREGISTERINFO_H
#define LLVM_LIB_TARGET_POWERPC_PPCREGISTERINFO_H

#include "PPC.h"
#include "llvm/ADT/DenseMap.h"

#define GET_REGINFO_HEADER
#include "PPCGenRegisterInfo.inc"

namespace llvm {

inline static unsigned getCRFromCRBit(unsigned SrcReg) {
  unsigned Reg = 0;
  if (SrcReg == PPC::CR0LT || SrcReg == PPC::CR0GT ||
      SrcReg == PPC::CR0EQ || SrcReg == PPC::CR0UN)
    Reg = PPC::CR0;
  else if (SrcReg == PPC::CR1LT || SrcReg == PPC::CR1GT ||
           SrcReg == PPC::CR1EQ || SrcReg == PPC::CR1UN)
    Reg = PPC::CR1;
  else if (SrcReg == PPC::CR2LT || SrcReg == PPC::CR2GT ||
           SrcReg == PPC::CR2EQ || SrcReg == PPC::CR2UN)
    Reg = PPC::CR2;
  else if (SrcReg == PPC::CR3LT || SrcReg == PPC::CR3GT ||
           SrcReg == PPC::CR3EQ || SrcReg == PPC::CR3UN)
    Reg = PPC::CR3;
  else if (SrcReg == PPC::CR4LT || SrcReg == PPC::CR4GT ||
           SrcReg == PPC::CR4EQ || SrcReg == PPC::CR4UN)
    Reg = PPC::CR4;
  else if (SrcReg == PPC::CR5LT || SrcReg == PPC::CR5GT ||
           SrcReg == PPC::CR5EQ || SrcReg == PPC::CR5UN)
    Reg = PPC::CR5;
  else if (SrcReg == PPC::CR6LT || SrcReg == PPC::CR6GT ||
           SrcReg == PPC::CR6EQ || SrcReg == PPC::CR6UN)
    Reg = PPC::CR6;
  else if (SrcReg == PPC::CR7LT || SrcReg == PPC::CR7GT ||
           SrcReg == PPC::CR7EQ || SrcReg == PPC::CR7UN)
    Reg = PPC::CR7;

  assert(Reg != 0 && "Invalid CR bit register");
  return Reg;
}

class PPCRegisterInfo : public PPCGenRegisterInfo {
  DenseMap<unsigned, unsigned> ImmToIdxMap;
  const PPCTargetMachine &TM;

public:
  PPCRegisterInfo(const PPCTargetMachine &TM);

  /// getPointerRegClass - Return the register class to use to hold pointers.
  /// This is used for addressing modes.
  const TargetRegisterClass *
  getPointerRegClass(const MachineFunction &MF, unsigned Kind=0) const override;

  unsigned getRegPressureLimit(const TargetRegisterClass *RC,
                               MachineFunction &MF) const override;

  const TargetRegisterClass *
  getLargestLegalSuperClass(const TargetRegisterClass *RC,
                            const MachineFunction &MF) const override;

  /// Code Generation virtual methods...
  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;
  const MCPhysReg *getCalleeSavedRegsViaCopy(const MachineFunction *MF) const;
  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID CC) const override;
  const uint32_t *getNoPreservedMask() const override;

  void adjustStackMapLiveOutMask(uint32_t *Mask) const override;

  BitVector getReservedRegs(const MachineFunction &MF) const override;
  bool isCallerPreservedPhysReg(unsigned PhysReg, const MachineFunction &MF) const override;

  /// We require the register scavenger.
  bool requiresRegisterScavenging(const MachineFunction &MF) const override {
    return true;
  }

  bool requiresFrameIndexScavenging(const MachineFunction &MF) const override {
    return true;
  }

  bool trackLivenessAfterRegAlloc(const MachineFunction &MF) const override {
    return true;
  }

  bool requiresVirtualBaseRegisters(const MachineFunction &MF) const override {
    return true;
  }

  void lowerDynamicAlloc(MachineBasicBlock::iterator II) const;
  void lowerDynamicAreaOffset(MachineBasicBlock::iterator II) const;
  void lowerCRSpilling(MachineBasicBlock::iterator II,
                       unsigned FrameIndex) const;
  void lowerCRRestore(MachineBasicBlock::iterator II,
                      unsigned FrameIndex) const;
  void lowerCRBitSpilling(MachineBasicBlock::iterator II,
                          unsigned FrameIndex) const;
  void lowerCRBitRestore(MachineBasicBlock::iterator II,
                         unsigned FrameIndex) const;
  void lowerVRSAVESpilling(MachineBasicBlock::iterator II,
                           unsigned FrameIndex) const;
  void lowerVRSAVERestore(MachineBasicBlock::iterator II,
                          unsigned FrameIndex) const;

  bool hasReservedSpillSlot(const MachineFunction &MF, unsigned Reg,
                            int &FrameIdx) const override;
  void eliminateFrameIndex(MachineBasicBlock::iterator II, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;

  // Support for virtual base registers.
  bool needsFrameBaseReg(MachineInstr *MI, int64_t Offset) const override;
  void materializeFrameBaseRegister(MachineBasicBlock *MBB,
                                    unsigned BaseReg, int FrameIdx,
                                    int64_t Offset) const override;
  void resolveFrameIndex(MachineInstr &MI, unsigned BaseReg,
                         int64_t Offset) const override;
  bool isFrameOffsetLegal(const MachineInstr *MI, unsigned BaseReg,
                          int64_t Offset) const override;

  // Debug information queries.
  unsigned getFrameRegister(const MachineFunction &MF) const override;

  // Base pointer (stack realignment) support.
  unsigned getBaseRegister(const MachineFunction &MF) const;
  bool hasBasePointer(const MachineFunction &MF) const;

  /// stripRegisterPrefix - This method strips the character prefix from a
  /// register name so that only the number is left.  Used by for linux asm.
  static const char *stripRegisterPrefix(const char *RegName) {
    switch (RegName[0]) {
      case 'r':
      case 'f':
      case 'q': // for QPX
      case 'v':
        if (RegName[1] == 's')
          return RegName + 2;
        return RegName + 1;
      case 'c': if (RegName[1] == 'r') return RegName + 2;
    }

    return RegName;
  }
};

} // end namespace llvm

#endif
