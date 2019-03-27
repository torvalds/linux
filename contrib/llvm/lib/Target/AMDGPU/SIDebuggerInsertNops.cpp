//===--- SIDebuggerInsertNops.cpp - Inserts nops for debugger usage -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Inserts one nop instruction for each high level source statement for
/// debugger usage.
///
/// Tools, such as a debugger, need to pause execution based on user input (i.e.
/// breakpoint). In order to do this, one nop instruction is inserted before the
/// first isa instruction of each high level source statement. Further, the
/// debugger may replace nop instructions with trap instructions based on user
/// input.
//
//===----------------------------------------------------------------------===//

#include "AMDGPUSubtarget.h"
#include "SIInstrInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
using namespace llvm;

#define DEBUG_TYPE "si-debugger-insert-nops"
#define PASS_NAME "SI Debugger Insert Nops"

namespace {

class SIDebuggerInsertNops : public MachineFunctionPass {
public:
  static char ID;

  SIDebuggerInsertNops() : MachineFunctionPass(ID) { }
  StringRef getPassName() const override { return PASS_NAME; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

} // anonymous namespace

INITIALIZE_PASS(SIDebuggerInsertNops, DEBUG_TYPE, PASS_NAME, false, false)

char SIDebuggerInsertNops::ID = 0;
char &llvm::SIDebuggerInsertNopsID = SIDebuggerInsertNops::ID;

FunctionPass *llvm::createSIDebuggerInsertNopsPass() {
  return new SIDebuggerInsertNops();
}

bool SIDebuggerInsertNops::runOnMachineFunction(MachineFunction &MF) {
  // Skip this pass if "amdgpu-debugger-insert-nops" attribute was not
  // specified.
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  if (!ST.debuggerInsertNops())
    return false;

  // Skip machine functions without debug info.
  if (!MF.getMMI().hasDebugInfo())
    return false;

  // Target instruction info.
  const SIInstrInfo *TII = ST.getInstrInfo();

  // Set containing line numbers that have nop inserted.
  DenseSet<unsigned> NopInserted;

  for (auto &MBB : MF) {
    for (auto MI = MBB.begin(); MI != MBB.end(); ++MI) {
      // Skip debug instructions and instructions without location.
      if (MI->isDebugInstr() || !MI->getDebugLoc())
        continue;

      // Insert nop instruction if line number does not have nop inserted.
      auto DL = MI->getDebugLoc();
      if (NopInserted.find(DL.getLine()) == NopInserted.end()) {
        BuildMI(MBB, *MI, DL, TII->get(AMDGPU::S_NOP))
          .addImm(0);
        NopInserted.insert(DL.getLine());
      }
    }
  }

  return true;
}
