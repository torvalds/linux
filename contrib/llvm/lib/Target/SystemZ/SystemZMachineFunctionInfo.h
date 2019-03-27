//=== SystemZMachineFunctionInfo.h - SystemZ machine function info -*- C++ -*-//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZMACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

class SystemZMachineFunctionInfo : public MachineFunctionInfo {
  virtual void anchor();
  unsigned LowSavedGPR;
  unsigned HighSavedGPR;
  unsigned VarArgsFirstGPR;
  unsigned VarArgsFirstFPR;
  unsigned VarArgsFrameIndex;
  unsigned RegSaveFrameIndex;
  int FramePointerSaveIndex;
  bool ManipulatesSP;
  unsigned NumLocalDynamics;

public:
  explicit SystemZMachineFunctionInfo(MachineFunction &MF)
    : LowSavedGPR(0), HighSavedGPR(0), VarArgsFirstGPR(0), VarArgsFirstFPR(0),
      VarArgsFrameIndex(0), RegSaveFrameIndex(0), FramePointerSaveIndex(0),
      ManipulatesSP(false), NumLocalDynamics(0) {}

  // Get and set the first call-saved GPR that should be saved and restored
  // by this function.  This is 0 if no GPRs need to be saved or restored.
  unsigned getLowSavedGPR() const { return LowSavedGPR; }
  void setLowSavedGPR(unsigned Reg) { LowSavedGPR = Reg; }

  // Get and set the last call-saved GPR that should be saved and restored
  // by this function.
  unsigned getHighSavedGPR() const { return HighSavedGPR; }
  void setHighSavedGPR(unsigned Reg) { HighSavedGPR = Reg; }

  // Get and set the number of fixed (as opposed to variable) arguments
  // that are passed in GPRs to this function.
  unsigned getVarArgsFirstGPR() const { return VarArgsFirstGPR; }
  void setVarArgsFirstGPR(unsigned GPR) { VarArgsFirstGPR = GPR; }

  // Likewise FPRs.
  unsigned getVarArgsFirstFPR() const { return VarArgsFirstFPR; }
  void setVarArgsFirstFPR(unsigned FPR) { VarArgsFirstFPR = FPR; }

  // Get and set the frame index of the first stack vararg.
  unsigned getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(unsigned FI) { VarArgsFrameIndex = FI; }

  // Get and set the frame index of the register save area
  // (i.e. the incoming stack pointer).
  unsigned getRegSaveFrameIndex() const { return RegSaveFrameIndex; }
  void setRegSaveFrameIndex(unsigned FI) { RegSaveFrameIndex = FI; }

  // Get and set the frame index of where the old frame pointer is stored.
  int getFramePointerSaveIndex() const { return FramePointerSaveIndex; }
  void setFramePointerSaveIndex(int Idx) { FramePointerSaveIndex = Idx; }

  // Get and set whether the function directly manipulates the stack pointer,
  // e.g. through STACKSAVE or STACKRESTORE.
  bool getManipulatesSP() const { return ManipulatesSP; }
  void setManipulatesSP(bool MSP) { ManipulatesSP = MSP; }

  // Count number of local-dynamic TLS symbols used.
  unsigned getNumLocalDynamicTLSAccesses() const { return NumLocalDynamics; }
  void incNumLocalDynamicTLSAccesses() { ++NumLocalDynamics; }
};

} // end namespace llvm

#endif
