//===- NVPTXRegisterInfo.h - NVPTX Register Information Impl ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the NVPTX implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_NVPTXREGISTERINFO_H
#define LLVM_LIB_TARGET_NVPTX_NVPTXREGISTERINFO_H

#include "ManagedStringPool.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include <sstream>

#define GET_REGINFO_HEADER
#include "NVPTXGenRegisterInfo.inc"

namespace llvm {
class NVPTXRegisterInfo : public NVPTXGenRegisterInfo {
private:
  // Hold Strings that can be free'd all together with NVPTXRegisterInfo
  ManagedStringPool ManagedStrPool;

public:
  NVPTXRegisterInfo();

  //------------------------------------------------------
  // Pure virtual functions from TargetRegisterInfo
  //------------------------------------------------------

  // NVPTX callee saved registers
  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;

  BitVector getReservedRegs(const MachineFunction &MF) const override;

  void eliminateFrameIndex(MachineBasicBlock::iterator MI, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;

  unsigned getFrameRegister(const MachineFunction &MF) const override;

  ManagedStringPool *getStrPool() const {
    return const_cast<ManagedStringPool *>(&ManagedStrPool);
  }

  const char *getName(unsigned RegNo) const {
    std::stringstream O;
    O << "reg" << RegNo;
    return getStrPool()->getManagedString(O.str().c_str())->c_str();
  }

};

std::string getNVPTXRegClassName(const TargetRegisterClass *RC);
std::string getNVPTXRegClassStr(const TargetRegisterClass *RC);

} // end namespace llvm

#endif
