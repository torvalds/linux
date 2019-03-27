//=- WebAssemblyInstrInfo.h - WebAssembly Instruction Information -*- C++ -*-=//
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
/// TargetInstrInfo class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYINSTRINFO_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYINSTRINFO_H

#include "WebAssemblyRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "WebAssemblyGenInstrInfo.inc"

namespace llvm {

class WebAssemblySubtarget;

class WebAssemblyInstrInfo final : public WebAssemblyGenInstrInfo {
  const WebAssemblyRegisterInfo RI;

public:
  explicit WebAssemblyInstrInfo(const WebAssemblySubtarget &STI);

  const WebAssemblyRegisterInfo &getRegisterInfo() const { return RI; }

  bool isReallyTriviallyReMaterializable(const MachineInstr &MI,
                                         AliasAnalysis *AA) const override;

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                   const DebugLoc &DL, unsigned DestReg, unsigned SrcReg,
                   bool KillSrc) const override;
  MachineInstr *commuteInstructionImpl(MachineInstr &MI, bool NewMI,
                                       unsigned OpIdx1,
                                       unsigned OpIdx2) const override;

  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify = false) const override;
  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;
  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;
  bool
  reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;
};

} // end namespace llvm

#endif
