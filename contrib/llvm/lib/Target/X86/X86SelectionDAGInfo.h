//===-- X86SelectionDAGInfo.h - X86 SelectionDAG Info -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the X86 subclass for SelectionDAGTargetInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86SELECTIONDAGINFO_H
#define LLVM_LIB_TARGET_X86_X86SELECTIONDAGINFO_H

#include "llvm/CodeGen/SelectionDAGTargetInfo.h"
#include "llvm/MC/MCRegisterInfo.h"

namespace llvm {

class X86TargetLowering;
class X86TargetMachine;
class X86Subtarget;

class X86SelectionDAGInfo : public SelectionDAGTargetInfo {
  /// Returns true if it is possible for the base register to conflict with the
  /// given set of clobbers for a memory intrinsic.
  bool isBaseRegConflictPossible(SelectionDAG &DAG,
                                 ArrayRef<MCPhysReg> ClobberSet) const;

public:
  explicit X86SelectionDAGInfo() = default;

  SDValue EmitTargetCodeForMemset(SelectionDAG &DAG, const SDLoc &dl,
                                  SDValue Chain, SDValue Dst, SDValue Src,
                                  SDValue Size, unsigned Align, bool isVolatile,
                                  MachinePointerInfo DstPtrInfo) const override;

  SDValue EmitTargetCodeForMemcpy(SelectionDAG &DAG, const SDLoc &dl,
                                  SDValue Chain, SDValue Dst, SDValue Src,
                                  SDValue Size, unsigned Align, bool isVolatile,
                                  bool AlwaysInline,
                                  MachinePointerInfo DstPtrInfo,
                                  MachinePointerInfo SrcPtrInfo) const override;
};

}

#endif
