//== llvm/CodeGen/GlobalISel/LegalizePass.h ------------- -*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file A pass to convert the target-illegal operations created by IR -> MIR
/// translation into ones the target expects to be able to select. This may
/// occur in multiple phases, for example G_ADD <2 x i8> -> G_ADD <2 x i16> ->
/// G_ADD <4 x i16>.
///
/// The LegalizeHelper class is where most of the work happens, and is designed
/// to be callable from other passes that find themselves with an illegal
/// instruction.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_LEGALIZEMACHINEIRPASS_H
#define LLVM_CODEGEN_GLOBALISEL_LEGALIZEMACHINEIRPASS_H

#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

class MachineRegisterInfo;

class Legalizer : public MachineFunctionPass {
public:
  static char ID;

private:

  /// Initialize the field members using \p MF.
  void init(MachineFunction &MF);

public:
  // Ctor, nothing fancy.
  Legalizer();

  StringRef getPassName() const override { return "Legalizer"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::IsSSA);
  }

  MachineFunctionProperties getSetProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::Legalized);
  }

  bool combineExtracts(MachineInstr &MI, MachineRegisterInfo &MRI,
                       const TargetInstrInfo &TII);

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // End namespace llvm.

#endif
