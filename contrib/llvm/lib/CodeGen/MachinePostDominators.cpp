//===- MachinePostDominators.cpp -Machine Post Dominator Calculation ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements simple dominator construction algorithms for finding
// post dominators on machine functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachinePostDominators.h"

using namespace llvm;

namespace llvm {
template class DominatorTreeBase<MachineBasicBlock, true>; // PostDomTreeBase
}

char MachinePostDominatorTree::ID = 0;

//declare initializeMachinePostDominatorTreePass
INITIALIZE_PASS(MachinePostDominatorTree, "machinepostdomtree",
                "MachinePostDominator Tree Construction", true, true)

MachinePostDominatorTree::MachinePostDominatorTree() : MachineFunctionPass(ID) {
  initializeMachinePostDominatorTreePass(*PassRegistry::getPassRegistry());
  DT = new PostDomTreeBase<MachineBasicBlock>();
}

FunctionPass *
MachinePostDominatorTree::createMachinePostDominatorTreePass() {
  return new MachinePostDominatorTree();
}

bool
MachinePostDominatorTree::runOnMachineFunction(MachineFunction &F) {
  DT->recalculate(F);
  return false;
}

MachinePostDominatorTree::~MachinePostDominatorTree() {
  delete DT;
}

void
MachinePostDominatorTree::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  MachineFunctionPass::getAnalysisUsage(AU);
}

void
MachinePostDominatorTree::print(llvm::raw_ostream &OS, const Module *M) const {
  DT->print(OS);
}
