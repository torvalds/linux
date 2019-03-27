//== llvm/CodeGen/GlobalISel/InstructionSelect.h -----------------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file This file describes the interface of the MachineFunctionPass
/// responsible for selecting (possibly generic) machine instructions to
/// target-specific instructions.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_INSTRUCTIONSELECT_H
#define LLVM_CODEGEN_GLOBALISEL_INSTRUCTIONSELECT_H

#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {
/// This pass is responsible for selecting generic machine instructions to
/// target-specific instructions.  It relies on the InstructionSelector provided
/// by the target.
/// Selection is done by examining blocks in post-order, and instructions in
/// reverse order.
///
/// \post for all inst in MF: not isPreISelGenericOpcode(inst.opcode)
class InstructionSelect : public MachineFunctionPass {
public:
  static char ID;
  StringRef getPassName() const override { return "InstructionSelect"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties()
        .set(MachineFunctionProperties::Property::IsSSA)
        .set(MachineFunctionProperties::Property::Legalized)
        .set(MachineFunctionProperties::Property::RegBankSelected);
  }

  MachineFunctionProperties getSetProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::Selected);
  }

  InstructionSelect();

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // End namespace llvm.

#endif
