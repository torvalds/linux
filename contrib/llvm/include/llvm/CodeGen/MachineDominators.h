//==- llvm/CodeGen/MachineDominators.h - Machine Dom Calculation -*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines classes mirroring those in llvm/Analysis/Dominators.h,
// but for target-specific code rather than target-independent IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEDOMINATORS_H
#define LLVM_CODEGEN_MACHINEDOMINATORS_H

#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/Support/GenericDomTree.h"
#include "llvm/Support/GenericDomTreeConstruction.h"
#include <cassert>
#include <memory>
#include <vector>

namespace llvm {

template <>
inline void DominatorTreeBase<MachineBasicBlock, false>::addRoot(
    MachineBasicBlock *MBB) {
  this->Roots.push_back(MBB);
}

extern template class DomTreeNodeBase<MachineBasicBlock>;
extern template class DominatorTreeBase<MachineBasicBlock, false>; // DomTree
extern template class DominatorTreeBase<MachineBasicBlock, true>; // PostDomTree

using MachineDomTreeNode = DomTreeNodeBase<MachineBasicBlock>;

//===-------------------------------------
/// DominatorTree Class - Concrete subclass of DominatorTreeBase that is used to
/// compute a normal dominator tree.
///
class MachineDominatorTree : public MachineFunctionPass {
  /// Helper structure used to hold all the basic blocks
  /// involved in the split of a critical edge.
  struct CriticalEdge {
    MachineBasicBlock *FromBB;
    MachineBasicBlock *ToBB;
    MachineBasicBlock *NewBB;
  };

  /// Pile up all the critical edges to be split.
  /// The splitting of a critical edge is local and thus, it is possible
  /// to apply several of those changes at the same time.
  mutable SmallVector<CriticalEdge, 32> CriticalEdgesToSplit;

  /// Remember all the basic blocks that are inserted during
  /// edge splitting.
  /// Invariant: NewBBs == all the basic blocks contained in the NewBB
  /// field of all the elements of CriticalEdgesToSplit.
  /// I.e., forall elt in CriticalEdgesToSplit, it exists BB in NewBBs
  /// such as BB == elt.NewBB.
  mutable SmallSet<MachineBasicBlock *, 32> NewBBs;

  /// The DominatorTreeBase that is used to compute a normal dominator tree
  std::unique_ptr<DomTreeBase<MachineBasicBlock>> DT;

  /// Apply all the recorded critical edges to the DT.
  /// This updates the underlying DT information in a way that uses
  /// the fast query path of DT as much as possible.
  ///
  /// \post CriticalEdgesToSplit.empty().
  void applySplitCriticalEdges() const;

public:
  static char ID; // Pass ID, replacement for typeid

  MachineDominatorTree();

  DomTreeBase<MachineBasicBlock> &getBase() {
    if (!DT) DT.reset(new DomTreeBase<MachineBasicBlock>());
    applySplitCriticalEdges();
    return *DT;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// getRoots -  Return the root blocks of the current CFG.  This may include
  /// multiple blocks if we are computing post dominators.  For forward
  /// dominators, this will always be a single block (the entry node).
  ///
  inline const SmallVectorImpl<MachineBasicBlock*> &getRoots() const {
    applySplitCriticalEdges();
    return DT->getRoots();
  }

  inline MachineBasicBlock *getRoot() const {
    applySplitCriticalEdges();
    return DT->getRoot();
  }

  inline MachineDomTreeNode *getRootNode() const {
    applySplitCriticalEdges();
    return DT->getRootNode();
  }

  bool runOnMachineFunction(MachineFunction &F) override;

  inline bool dominates(const MachineDomTreeNode* A,
                        const MachineDomTreeNode* B) const {
    applySplitCriticalEdges();
    return DT->dominates(A, B);
  }

  inline bool dominates(const MachineBasicBlock* A,
                        const MachineBasicBlock* B) const {
    applySplitCriticalEdges();
    return DT->dominates(A, B);
  }

  // dominates - Return true if A dominates B. This performs the
  // special checks necessary if A and B are in the same basic block.
  bool dominates(const MachineInstr *A, const MachineInstr *B) const {
    applySplitCriticalEdges();
    const MachineBasicBlock *BBA = A->getParent(), *BBB = B->getParent();
    if (BBA != BBB) return DT->dominates(BBA, BBB);

    // Loop through the basic block until we find A or B.
    MachineBasicBlock::const_iterator I = BBA->begin();
    for (; &*I != A && &*I != B; ++I)
      /*empty*/ ;

    //if(!DT.IsPostDominators) {
      // A dominates B if it is found first in the basic block.
      return &*I == A;
    //} else {
    //  // A post-dominates B if B is found first in the basic block.
    //  return &*I == B;
    //}
  }

  inline bool properlyDominates(const MachineDomTreeNode* A,
                                const MachineDomTreeNode* B) const {
    applySplitCriticalEdges();
    return DT->properlyDominates(A, B);
  }

  inline bool properlyDominates(const MachineBasicBlock* A,
                                const MachineBasicBlock* B) const {
    applySplitCriticalEdges();
    return DT->properlyDominates(A, B);
  }

  /// findNearestCommonDominator - Find nearest common dominator basic block
  /// for basic block A and B. If there is no such block then return NULL.
  inline MachineBasicBlock *findNearestCommonDominator(MachineBasicBlock *A,
                                                       MachineBasicBlock *B) {
    applySplitCriticalEdges();
    return DT->findNearestCommonDominator(A, B);
  }

  inline MachineDomTreeNode *operator[](MachineBasicBlock *BB) const {
    applySplitCriticalEdges();
    return DT->getNode(BB);
  }

  /// getNode - return the (Post)DominatorTree node for the specified basic
  /// block.  This is the same as using operator[] on this class.
  ///
  inline MachineDomTreeNode *getNode(MachineBasicBlock *BB) const {
    applySplitCriticalEdges();
    return DT->getNode(BB);
  }

  /// addNewBlock - Add a new node to the dominator tree information.  This
  /// creates a new node as a child of DomBB dominator node,linking it into
  /// the children list of the immediate dominator.
  inline MachineDomTreeNode *addNewBlock(MachineBasicBlock *BB,
                                         MachineBasicBlock *DomBB) {
    applySplitCriticalEdges();
    return DT->addNewBlock(BB, DomBB);
  }

  /// changeImmediateDominator - This method is used to update the dominator
  /// tree information when a node's immediate dominator changes.
  ///
  inline void changeImmediateDominator(MachineBasicBlock *N,
                                       MachineBasicBlock* NewIDom) {
    applySplitCriticalEdges();
    DT->changeImmediateDominator(N, NewIDom);
  }

  inline void changeImmediateDominator(MachineDomTreeNode *N,
                                       MachineDomTreeNode* NewIDom) {
    applySplitCriticalEdges();
    DT->changeImmediateDominator(N, NewIDom);
  }

  /// eraseNode - Removes a node from  the dominator tree. Block must not
  /// dominate any other blocks. Removes node from its immediate dominator's
  /// children list. Deletes dominator node associated with basic block BB.
  inline void eraseNode(MachineBasicBlock *BB) {
    applySplitCriticalEdges();
    DT->eraseNode(BB);
  }

  /// splitBlock - BB is split and now it has one successor. Update dominator
  /// tree to reflect this change.
  inline void splitBlock(MachineBasicBlock* NewBB) {
    applySplitCriticalEdges();
    DT->splitBlock(NewBB);
  }

  /// isReachableFromEntry - Return true if A is dominated by the entry
  /// block of the function containing it.
  bool isReachableFromEntry(const MachineBasicBlock *A) {
    applySplitCriticalEdges();
    return DT->isReachableFromEntry(A);
  }

  void releaseMemory() override;

  void verifyAnalysis() const override;

  void print(raw_ostream &OS, const Module*) const override;

  /// Record that the critical edge (FromBB, ToBB) has been
  /// split with NewBB.
  /// This is best to use this method instead of directly update the
  /// underlying information, because this helps mitigating the
  /// number of time the DT information is invalidated.
  ///
  /// \note Do not use this method with regular edges.
  ///
  /// \note To benefit from the compile time improvement incurred by this
  /// method, the users of this method have to limit the queries to the DT
  /// interface between two edges splitting. In other words, they have to
  /// pack the splitting of critical edges as much as possible.
  void recordSplitCriticalEdge(MachineBasicBlock *FromBB,
                              MachineBasicBlock *ToBB,
                              MachineBasicBlock *NewBB) {
    bool Inserted = NewBBs.insert(NewBB).second;
    (void)Inserted;
    assert(Inserted &&
           "A basic block inserted via edge splitting cannot appear twice");
    CriticalEdgesToSplit.push_back({FromBB, ToBB, NewBB});
  }
};

//===-------------------------------------
/// DominatorTree GraphTraits specialization so the DominatorTree can be
/// iterable by generic graph iterators.
///

template <class Node, class ChildIterator>
struct MachineDomTreeGraphTraitsBase {
  using NodeRef = Node *;
  using ChildIteratorType = ChildIterator;

  static NodeRef getEntryNode(NodeRef N) { return N; }
  static ChildIteratorType child_begin(NodeRef N) { return N->begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->end(); }
};

template <class T> struct GraphTraits;

template <>
struct GraphTraits<MachineDomTreeNode *>
    : public MachineDomTreeGraphTraitsBase<MachineDomTreeNode,
                                           MachineDomTreeNode::iterator> {};

template <>
struct GraphTraits<const MachineDomTreeNode *>
    : public MachineDomTreeGraphTraitsBase<const MachineDomTreeNode,
                                           MachineDomTreeNode::const_iterator> {
};

template <> struct GraphTraits<MachineDominatorTree*>
  : public GraphTraits<MachineDomTreeNode *> {
  static NodeRef getEntryNode(MachineDominatorTree *DT) {
    return DT->getRootNode();
  }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEDOMINATORS_H
