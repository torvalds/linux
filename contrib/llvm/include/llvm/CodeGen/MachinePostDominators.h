//=- llvm/CodeGen/MachineDominators.h ----------------------------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file exposes interfaces to post dominance information for
// target-specific code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEPOSTDOMINATORS_H
#define LLVM_CODEGEN_MACHINEPOSTDOMINATORS_H

#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

namespace llvm {

///
/// PostDominatorTree Class - Concrete subclass of DominatorTree that is used
/// to compute the post-dominator tree.
///
struct MachinePostDominatorTree : public MachineFunctionPass {
private:
 PostDomTreeBase<MachineBasicBlock> *DT;

public:
  static char ID;

  MachinePostDominatorTree();

  ~MachinePostDominatorTree() override;

  FunctionPass *createMachinePostDominatorTreePass();

  const SmallVectorImpl<MachineBasicBlock *> &getRoots() const {
    return DT->getRoots();
  }

  MachineDomTreeNode *getRootNode() const {
    return DT->getRootNode();
  }

  MachineDomTreeNode *operator[](MachineBasicBlock *BB) const {
    return DT->getNode(BB);
  }

  MachineDomTreeNode *getNode(MachineBasicBlock *BB) const {
    return DT->getNode(BB);
  }

  bool dominates(const MachineDomTreeNode *A,
                 const MachineDomTreeNode *B) const {
    return DT->dominates(A, B);
  }

  bool dominates(const MachineBasicBlock *A, const MachineBasicBlock *B) const {
    return DT->dominates(A, B);
  }

  bool properlyDominates(const MachineDomTreeNode *A,
                         const MachineDomTreeNode *B) const {
    return DT->properlyDominates(A, B);
  }

  bool properlyDominates(const MachineBasicBlock *A,
                         const MachineBasicBlock *B) const {
    return DT->properlyDominates(A, B);
  }

  MachineBasicBlock *findNearestCommonDominator(MachineBasicBlock *A,
                                                MachineBasicBlock *B) {
    return DT->findNearestCommonDominator(A, B);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  void print(llvm::raw_ostream &OS, const Module *M = nullptr) const override;
};
} //end of namespace llvm

#endif
