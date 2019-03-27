//===- MachineDominanceFrontier.cpp ---------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineDominanceFrontier.h"
#include "llvm/Analysis/DominanceFrontierImpl.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/Passes.h"

using namespace llvm;

namespace llvm {
template class DominanceFrontierBase<MachineBasicBlock, false>;
template class DominanceFrontierBase<MachineBasicBlock, true>;
template class ForwardDominanceFrontierBase<MachineBasicBlock>;
}


char MachineDominanceFrontier::ID = 0;

INITIALIZE_PASS_BEGIN(MachineDominanceFrontier, "machine-domfrontier",
                "Machine Dominance Frontier Construction", true, true)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_END(MachineDominanceFrontier, "machine-domfrontier",
                "Machine Dominance Frontier Construction", true, true)

MachineDominanceFrontier::MachineDominanceFrontier()
  : MachineFunctionPass(ID),
    Base() {
  initializeMachineDominanceFrontierPass(*PassRegistry::getPassRegistry());
}

char &llvm::MachineDominanceFrontierID = MachineDominanceFrontier::ID;

bool MachineDominanceFrontier::runOnMachineFunction(MachineFunction &) {
  releaseMemory();
  Base.analyze(getAnalysis<MachineDominatorTree>().getBase());
  return false;
}

void MachineDominanceFrontier::releaseMemory() {
  Base.releaseMemory();
}

void MachineDominanceFrontier::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<MachineDominatorTree>();
  MachineFunctionPass::getAnalysisUsage(AU);
}
