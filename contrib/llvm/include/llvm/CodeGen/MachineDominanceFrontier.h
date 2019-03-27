//===- llvm/CodeGen/MachineDominanceFrontier.h ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEDOMINANCEFRONTIER_H
#define LLVM_CODEGEN_MACHINEDOMINANCEFRONTIER_H

#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/DominanceFrontierImpl.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/Support/GenericDomTree.h"
#include <vector>

namespace llvm {

class MachineDominanceFrontier : public MachineFunctionPass {
  ForwardDominanceFrontierBase<MachineBasicBlock> Base;

public:
 using DomTreeT = DomTreeBase<MachineBasicBlock>;
 using DomTreeNodeT = DomTreeNodeBase<MachineBasicBlock>;
 using DomSetType = DominanceFrontierBase<MachineBasicBlock, false>::DomSetType;
 using iterator = DominanceFrontierBase<MachineBasicBlock, false>::iterator;
 using const_iterator =
     DominanceFrontierBase<MachineBasicBlock, false>::const_iterator;

 MachineDominanceFrontier(const MachineDominanceFrontier &) = delete;
 MachineDominanceFrontier &operator=(const MachineDominanceFrontier &) = delete;

 static char ID;

 MachineDominanceFrontier();

 ForwardDominanceFrontierBase<MachineBasicBlock> &getBase() { return Base; }

 const SmallVectorImpl<MachineBasicBlock *> &getRoots() const {
   return Base.getRoots();
  }

  MachineBasicBlock *getRoot() const {
    return Base.getRoot();
  }

  bool isPostDominator() const {
    return Base.isPostDominator();
  }

  iterator begin() {
    return Base.begin();
  }

  const_iterator begin() const {
    return Base.begin();
  }

  iterator end() {
    return Base.end();
  }

  const_iterator end() const {
    return Base.end();
  }

  iterator find(MachineBasicBlock *B) {
    return Base.find(B);
  }

  const_iterator find(MachineBasicBlock *B) const {
    return Base.find(B);
  }

  iterator addBasicBlock(MachineBasicBlock *BB, const DomSetType &frontier) {
    return Base.addBasicBlock(BB, frontier);
  }

  void removeBlock(MachineBasicBlock *BB) {
    return Base.removeBlock(BB);
  }

  void addToFrontier(iterator I, MachineBasicBlock *Node) {
    return Base.addToFrontier(I, Node);
  }

  void removeFromFrontier(iterator I, MachineBasicBlock *Node) {
    return Base.removeFromFrontier(I, Node);
  }

  bool compareDomSet(DomSetType &DS1, const DomSetType &DS2) const {
    return Base.compareDomSet(DS1, DS2);
  }

  bool compare(DominanceFrontierBase<MachineBasicBlock, false> &Other) const {
    return Base.compare(Other);
  }

  bool runOnMachineFunction(MachineFunction &F) override;

  void releaseMemory() override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEDOMINANCEFRONTIER_H
