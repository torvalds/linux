//===- AArch64CallLowering.h - Call lowering --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file describes how to lower LLVM calls to machine code calls.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64CALLLOWERING_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64CALLLOWERING_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/IR/CallingConv.h"
#include <cstdint>
#include <functional>

namespace llvm {

class AArch64TargetLowering;
class CCValAssign;
class DataLayout;
class MachineIRBuilder;
class MachineRegisterInfo;
class Type;

class AArch64CallLowering: public CallLowering {
public:
  AArch64CallLowering(const AArch64TargetLowering &TLI);

  bool lowerReturn(MachineIRBuilder &MIRBuilder, const Value *Val,
                   ArrayRef<unsigned> VRegs) const override;

  bool lowerFormalArguments(MachineIRBuilder &MIRBuilder, const Function &F,
                            ArrayRef<unsigned> VRegs) const override;

  bool lowerCall(MachineIRBuilder &MIRBuilder, CallingConv::ID CallConv,
                 const MachineOperand &Callee, const ArgInfo &OrigRet,
                 ArrayRef<ArgInfo> OrigArgs) const override;

private:
  using RegHandler = std::function<void(MachineIRBuilder &, Type *, unsigned,
                                        CCValAssign &)>;

  using MemHandler =
      std::function<void(MachineIRBuilder &, int, CCValAssign &)>;

  using SplitArgTy = std::function<void(unsigned, uint64_t)>;

  void splitToValueTypes(const ArgInfo &OrigArgInfo,
                         SmallVectorImpl<ArgInfo> &SplitArgs,
                         const DataLayout &DL, MachineRegisterInfo &MRI,
                         CallingConv::ID CallConv,
                         const SplitArgTy &SplitArg) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AARCH64_AARCH64CALLLOWERING_H
