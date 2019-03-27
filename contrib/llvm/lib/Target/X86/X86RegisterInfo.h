//===-- X86RegisterInfo.h - X86 Register Information Impl -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the X86 implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86REGISTERINFO_H
#define LLVM_LIB_TARGET_X86_X86REGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "X86GenRegisterInfo.inc"

namespace llvm {
  class Triple;

class X86RegisterInfo final : public X86GenRegisterInfo {
private:
  /// Is64Bit - Is the target 64-bits.
  ///
  bool Is64Bit;

  /// IsWin64 - Is the target on of win64 flavours
  ///
  bool IsWin64;

  /// SlotSize - Stack slot size in bytes.
  ///
  unsigned SlotSize;

  /// StackPtr - X86 physical register used as stack ptr.
  ///
  unsigned StackPtr;

  /// FramePtr - X86 physical register used as frame ptr.
  ///
  unsigned FramePtr;

  /// BasePtr - X86 physical register used as a base ptr in complex stack
  /// frames. I.e., when we need a 3rd base, not just SP and FP, due to
  /// variable size stack objects.
  unsigned BasePtr;

public:
  X86RegisterInfo(const Triple &TT);

  // FIXME: This should be tablegen'd like getDwarfRegNum is
  int getSEHRegNum(unsigned i) const;

  /// Code Generation virtual methods...
  ///
  bool trackLivenessAfterRegAlloc(const MachineFunction &MF) const override;

  /// getMatchingSuperRegClass - Return a subclass of the specified register
  /// class A so that each register in it has a sub-register of the
  /// specified sub-register index which is in the specified register class B.
  const TargetRegisterClass *
  getMatchingSuperRegClass(const TargetRegisterClass *A,
                           const TargetRegisterClass *B,
                           unsigned Idx) const override;

  const TargetRegisterClass *
  getSubClassWithSubReg(const TargetRegisterClass *RC,
                        unsigned Idx) const override;

  const TargetRegisterClass *
  getLargestLegalSuperClass(const TargetRegisterClass *RC,
                            const MachineFunction &MF) const override;

  /// getPointerRegClass - Returns a TargetRegisterClass used for pointer
  /// values.
  const TargetRegisterClass *
  getPointerRegClass(const MachineFunction &MF,
                     unsigned Kind = 0) const override;

  /// getCrossCopyRegClass - Returns a legal register class to copy a register
  /// in the specified class to or from. Returns NULL if it is possible to copy
  /// between a two registers of the specified class.
  const TargetRegisterClass *
  getCrossCopyRegClass(const TargetRegisterClass *RC) const override;

  /// getGPRsForTailCall - Returns a register class with registers that can be
  /// used in forming tail calls.
  const TargetRegisterClass *
  getGPRsForTailCall(const MachineFunction &MF) const;

  unsigned getRegPressureLimit(const TargetRegisterClass *RC,
                               MachineFunction &MF) const override;

  /// getCalleeSavedRegs - Return a null-terminated list of all of the
  /// callee-save registers on this target.
  const MCPhysReg *
  getCalleeSavedRegs(const MachineFunction* MF) const override;
  const MCPhysReg *
  getCalleeSavedRegsViaCopy(const MachineFunction *MF) const;
  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID) const override;
  const uint32_t *getNoPreservedMask() const override;

  // Calls involved in thread-local variable lookup save more registers than
  // normal calls, so they need a different mask to represent this.
  const uint32_t *getDarwinTLSCallPreservedMask() const;

  /// getReservedRegs - Returns a bitset indexed by physical register number
  /// indicating if a register is a special register that has particular uses and
  /// should be considered unavailable at all times, e.g. SP, RA. This is used by
  /// register scavenger to determine what registers are free.
  BitVector getReservedRegs(const MachineFunction &MF) const override;

  void adjustStackMapLiveOutMask(uint32_t *Mask) const override;

  bool hasBasePointer(const MachineFunction &MF) const;

  bool canRealignStack(const MachineFunction &MF) const override;

  bool hasReservedSpillSlot(const MachineFunction &MF, unsigned Reg,
                            int &FrameIdx) const override;

  void eliminateFrameIndex(MachineBasicBlock::iterator MI,
                           int SPAdj, unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;

  // Debug information queries.
  unsigned getFrameRegister(const MachineFunction &MF) const override;
  unsigned getPtrSizedFrameRegister(const MachineFunction &MF) const;
  unsigned getStackRegister() const { return StackPtr; }
  unsigned getBaseRegister() const { return BasePtr; }
  /// Returns physical register used as frame pointer.
  /// This will always returns the frame pointer register, contrary to
  /// getFrameRegister() which returns the "base pointer" in situations
  /// involving a stack, frame and base pointer.
  unsigned getFramePtr() const { return FramePtr; }
  // FIXME: Move to FrameInfok
  unsigned getSlotSize() const { return SlotSize; }
};

} // End llvm namespace

#endif
