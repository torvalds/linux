//===-- ARMBaseRegisterInfo.h - ARM Register Information Impl ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the base ARM implementation of TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARMBASEREGISTERINFO_H
#define LLVM_LIB_TARGET_ARM_ARMBASEREGISTERINFO_H

#include "MCTargetDesc/ARMBaseInfo.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/MC/MCRegisterInfo.h"
#include <cstdint>

#define GET_REGINFO_HEADER
#include "ARMGenRegisterInfo.inc"

namespace llvm {

class LiveIntervals;

/// Register allocation hints.
namespace ARMRI {

  enum {
    RegPairOdd  = 1,
    RegPairEven = 2
  };

} // end namespace ARMRI

/// isARMArea1Register - Returns true if the register is a low register (r0-r7)
/// or a stack/pc register that we should push/pop.
static inline bool isARMArea1Register(unsigned Reg, bool isIOS) {
  using namespace ARM;

  switch (Reg) {
    case R0:  case R1:  case R2:  case R3:
    case R4:  case R5:  case R6:  case R7:
    case LR:  case SP:  case PC:
      return true;
    case R8:  case R9:  case R10: case R11: case R12:
      // For iOS we want r7 and lr to be next to each other.
      return !isIOS;
    default:
      return false;
  }
}

static inline bool isARMArea2Register(unsigned Reg, bool isIOS) {
  using namespace ARM;

  switch (Reg) {
    case R8: case R9: case R10: case R11: case R12:
      // iOS has this second area.
      return isIOS;
    default:
      return false;
  }
}

static inline bool isARMArea3Register(unsigned Reg, bool isIOS) {
  using namespace ARM;

  switch (Reg) {
    case D15: case D14: case D13: case D12:
    case D11: case D10: case D9:  case D8:
    case D7:  case D6:  case D5:  case D4:
    case D3:  case D2:  case D1:  case D0:
    case D31: case D30: case D29: case D28:
    case D27: case D26: case D25: case D24:
    case D23: case D22: case D21: case D20:
    case D19: case D18: case D17: case D16:
      return true;
    default:
      return false;
  }
}

static inline bool isCalleeSavedRegister(unsigned Reg,
                                         const MCPhysReg *CSRegs) {
  for (unsigned i = 0; CSRegs[i]; ++i)
    if (Reg == CSRegs[i])
      return true;
  return false;
}

class ARMBaseRegisterInfo : public ARMGenRegisterInfo {
protected:
  /// BasePtr - ARM physical register used as a base ptr in complex stack
  /// frames. I.e., when we need a 3rd base, not just SP and FP, due to
  /// variable size stack objects.
  unsigned BasePtr = ARM::R6;

  // Can be only subclassed.
  explicit ARMBaseRegisterInfo();

  // Return the opcode that implements 'Op', or 0 if no opcode
  unsigned getOpcode(int Op) const;

public:
  /// Code Generation virtual methods...
  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;
  const MCPhysReg *
  getCalleeSavedRegsViaCopy(const MachineFunction *MF) const;
  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID) const override;
  const uint32_t *getNoPreservedMask() const override;
  const uint32_t *getTLSCallPreservedMask(const MachineFunction &MF) const;
  const uint32_t *getSjLjDispatchPreservedMask(const MachineFunction &MF) const;

  /// getThisReturnPreservedMask - Returns a call preserved mask specific to the
  /// case that 'returned' is on an i32 first argument if the calling convention
  /// is one that can (partially) model this attribute with a preserved mask
  /// (i.e. it is a calling convention that uses the same register for the first
  /// i32 argument and an i32 return value)
  ///
  /// Should return NULL in the case that the calling convention does not have
  /// this property
  const uint32_t *getThisReturnPreservedMask(const MachineFunction &MF,
                                             CallingConv::ID) const;

  BitVector getReservedRegs(const MachineFunction &MF) const override;
  bool isAsmClobberable(const MachineFunction &MF,
                       unsigned PhysReg) const override;

  const TargetRegisterClass *
  getPointerRegClass(const MachineFunction &MF,
                     unsigned Kind = 0) const override;
  const TargetRegisterClass *
  getCrossCopyRegClass(const TargetRegisterClass *RC) const override;

  const TargetRegisterClass *
  getLargestLegalSuperClass(const TargetRegisterClass *RC,
                            const MachineFunction &MF) const override;

  unsigned getRegPressureLimit(const TargetRegisterClass *RC,
                               MachineFunction &MF) const override;

  bool getRegAllocationHints(unsigned VirtReg,
                             ArrayRef<MCPhysReg> Order,
                             SmallVectorImpl<MCPhysReg> &Hints,
                             const MachineFunction &MF,
                             const VirtRegMap *VRM,
                             const LiveRegMatrix *Matrix) const override;

  void updateRegAllocHint(unsigned Reg, unsigned NewReg,
                          MachineFunction &MF) const override;

  bool hasBasePointer(const MachineFunction &MF) const;

  bool canRealignStack(const MachineFunction &MF) const override;
  int64_t getFrameIndexInstrOffset(const MachineInstr *MI,
                                   int Idx) const override;
  bool needsFrameBaseReg(MachineInstr *MI, int64_t Offset) const override;
  void materializeFrameBaseRegister(MachineBasicBlock *MBB,
                                    unsigned BaseReg, int FrameIdx,
                                    int64_t Offset) const override;
  void resolveFrameIndex(MachineInstr &MI, unsigned BaseReg,
                         int64_t Offset) const override;
  bool isFrameOffsetLegal(const MachineInstr *MI, unsigned BaseReg,
                          int64_t Offset) const override;

  bool cannotEliminateFrame(const MachineFunction &MF) const;

  // Debug information queries.
  unsigned getFrameRegister(const MachineFunction &MF) const override;
  unsigned getBaseRegister() const { return BasePtr; }

  bool isLowRegister(unsigned Reg) const;


  /// emitLoadConstPool - Emits a load from constpool to materialize the
  /// specified immediate.
  virtual void
  emitLoadConstPool(MachineBasicBlock &MBB, MachineBasicBlock::iterator &MBBI,
                    const DebugLoc &dl, unsigned DestReg, unsigned SubIdx,
                    int Val, ARMCC::CondCodes Pred = ARMCC::AL,
                    unsigned PredReg = 0,
                    unsigned MIFlags = MachineInstr::NoFlags) const;

  /// Code Generation virtual methods...
  bool requiresRegisterScavenging(const MachineFunction &MF) const override;

  bool trackLivenessAfterRegAlloc(const MachineFunction &MF) const override;

  bool requiresFrameIndexScavenging(const MachineFunction &MF) const override;

  bool requiresVirtualBaseRegisters(const MachineFunction &MF) const override;

  void eliminateFrameIndex(MachineBasicBlock::iterator II,
                           int SPAdj, unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;

  /// SrcRC and DstRC will be morphed into NewRC if this returns true
  bool shouldCoalesce(MachineInstr *MI,
                      const TargetRegisterClass *SrcRC,
                      unsigned SubReg,
                      const TargetRegisterClass *DstRC,
                      unsigned DstSubReg,
                      const TargetRegisterClass *NewRC,
                      LiveIntervals &LIS) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARM_ARMBASEREGISTERINFO_H
