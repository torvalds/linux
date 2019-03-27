//===- MipsCallLowering.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file describes how to lower LLVM calls to machine code calls.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPSCALLLOWERING_H
#define LLVM_LIB_TARGET_MIPS_MIPSCALLLOWERING_H

#include "llvm/CodeGen/GlobalISel/CallLowering.h"

namespace llvm {

class MipsTargetLowering;

class MipsCallLowering : public CallLowering {

public:
  class MipsHandler {
  public:
    MipsHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI)
        : MIRBuilder(MIRBuilder), MRI(MRI) {}

    virtual ~MipsHandler() = default;

    bool handle(ArrayRef<CCValAssign> ArgLocs,
                ArrayRef<CallLowering::ArgInfo> Args);

  protected:
    bool assignVRegs(ArrayRef<unsigned> VRegs, ArrayRef<CCValAssign> ArgLocs,
                     unsigned Index);

    void setLeastSignificantFirst(SmallVectorImpl<unsigned> &VRegs);

    MachineIRBuilder &MIRBuilder;
    MachineRegisterInfo &MRI;

  private:
    bool assign(unsigned VReg, const CCValAssign &VA);

    virtual unsigned getStackAddress(const CCValAssign &VA,
                                     MachineMemOperand *&MMO) = 0;

    virtual void assignValueToReg(unsigned ValVReg, const CCValAssign &VA) = 0;

    virtual void assignValueToAddress(unsigned ValVReg,
                                      const CCValAssign &VA) = 0;

    virtual bool handleSplit(SmallVectorImpl<unsigned> &VRegs,
                             ArrayRef<CCValAssign> ArgLocs,
                             unsigned ArgLocsStartIndex, unsigned ArgsReg) = 0;
  };

  MipsCallLowering(const MipsTargetLowering &TLI);

  bool lowerReturn(MachineIRBuilder &MIRBuilder, const Value *Val,
                   ArrayRef<unsigned> VRegs) const override;

  bool lowerFormalArguments(MachineIRBuilder &MIRBuilder, const Function &F,
                            ArrayRef<unsigned> VRegs) const override;

  bool lowerCall(MachineIRBuilder &MIRBuilder, CallingConv::ID CallConv,
                 const MachineOperand &Callee, const ArgInfo &OrigRet,
                 ArrayRef<ArgInfo> OrigArgs) const override;

private:
  /// Based on registers available on target machine split or extend
  /// type if needed, also change pointer type to appropriate integer
  /// type.
  template <typename T>
  void subTargetRegTypeForCallingConv(const Function &F, ArrayRef<ArgInfo> Args,
                                      ArrayRef<unsigned> OrigArgIndices,
                                      SmallVectorImpl<T> &ISDArgs) const;

  /// Split structures and arrays, save original argument indices since
  /// Mips calling convention needs info about original argument type.
  void splitToValueTypes(const ArgInfo &OrigArg, unsigned OriginalIndex,
                         SmallVectorImpl<ArgInfo> &SplitArgs,
                         SmallVectorImpl<unsigned> &SplitArgsOrigIndices) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_MIPS_MIPSCALLLOWERING_H
