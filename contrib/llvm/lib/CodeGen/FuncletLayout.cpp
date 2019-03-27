//===-- FuncletLayout.cpp - Contiguously lay out funclets -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements basic block placement transformations which result in
// funclets being contiguous.
//
//===----------------------------------------------------------------------===//
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/Passes.h"
using namespace llvm;

#define DEBUG_TYPE "funclet-layout"

namespace {
class FuncletLayout : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  FuncletLayout() : MachineFunctionPass(ID) {
    initializeFuncletLayoutPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &F) override;
  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }
};
}

char FuncletLayout::ID = 0;
char &llvm::FuncletLayoutID = FuncletLayout::ID;
INITIALIZE_PASS(FuncletLayout, DEBUG_TYPE,
                "Contiguously Lay Out Funclets", false, false)

bool FuncletLayout::runOnMachineFunction(MachineFunction &F) {
  // Even though this gets information from getEHScopeMembership(), this pass is
  // only necessary for funclet-based EH personalities, in which these EH scopes
  // are outlined at the end.
  DenseMap<const MachineBasicBlock *, int> FuncletMembership =
      getEHScopeMembership(F);
  if (FuncletMembership.empty())
    return false;

  F.sort([&](MachineBasicBlock &X, MachineBasicBlock &Y) {
    auto FuncletX = FuncletMembership.find(&X);
    auto FuncletY = FuncletMembership.find(&Y);
    assert(FuncletX != FuncletMembership.end());
    assert(FuncletY != FuncletMembership.end());
    return FuncletX->second < FuncletY->second;
  });

  // Conservatively assume we changed something.
  return true;
}
