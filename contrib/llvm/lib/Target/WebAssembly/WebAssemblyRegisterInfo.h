// WebAssemblyRegisterInfo.h - WebAssembly Register Information Impl -*- C++ -*-
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the WebAssembly implementation of the
/// WebAssemblyRegisterInfo class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYREGISTERINFO_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYREGISTERINFO_H

#define GET_REGINFO_HEADER
#include "WebAssemblyGenRegisterInfo.inc"

namespace llvm {

class MachineFunction;
class RegScavenger;
class TargetRegisterClass;
class Triple;

class WebAssemblyRegisterInfo final : public WebAssemblyGenRegisterInfo {
  const Triple &TT;

public:
  explicit WebAssemblyRegisterInfo(const Triple &TT);

  // Code Generation virtual methods.
  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;
  BitVector getReservedRegs(const MachineFunction &MF) const override;
  void eliminateFrameIndex(MachineBasicBlock::iterator MI, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;

  // Debug information queries.
  unsigned getFrameRegister(const MachineFunction &MF) const override;

  const TargetRegisterClass *
  getPointerRegClass(const MachineFunction &MF,
                     unsigned Kind = 0) const override;
  // This does not apply to wasm.
  const uint32_t *getNoPreservedMask() const override { return nullptr; }
};

} // end namespace llvm

#endif
