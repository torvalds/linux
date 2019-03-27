//===- Dominators.h - Dominator Info Calculation ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the DominatorTree class, which provides fast and efficient
// dominance queries.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DOMINATORS_H
#define LLVM_IR_DOMINATORS_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/GenericDomTree.h"
#include <utility>

namespace llvm {

class Function;
class Instruction;
class Module;
class raw_ostream;

extern template class DomTreeNodeBase<BasicBlock>;
extern template class DominatorTreeBase<BasicBlock, false>; // DomTree
extern template class DominatorTreeBase<BasicBlock, true>; // PostDomTree

extern template class cfg::Update<BasicBlock *>;

namespace DomTreeBuilder {
using BBDomTree = DomTreeBase<BasicBlock>;
using BBPostDomTree = PostDomTreeBase<BasicBlock>;

using BBUpdates = ArrayRef<llvm::cfg::Update<BasicBlock *>>;

extern template void Calculate<BBDomTree>(BBDomTree &DT);
extern template void CalculateWithUpdates<BBDomTree>(BBDomTree &DT,
                                                     BBUpdates U);

extern template void Calculate<BBPostDomTree>(BBPostDomTree &DT);

extern template void InsertEdge<BBDomTree>(BBDomTree &DT, BasicBlock *From,
                                           BasicBlock *To);
extern template void InsertEdge<BBPostDomTree>(BBPostDomTree &DT,
                                               BasicBlock *From,
                                               BasicBlock *To);

extern template void DeleteEdge<BBDomTree>(BBDomTree &DT, BasicBlock *From,
                                           BasicBlock *To);
extern template void DeleteEdge<BBPostDomTree>(BBPostDomTree &DT,
                                               BasicBlock *From,
                                               BasicBlock *To);

extern template void ApplyUpdates<BBDomTree>(BBDomTree &DT, BBUpdates);
extern template void ApplyUpdates<BBPostDomTree>(BBPostDomTree &DT, BBUpdates);

extern template bool Verify<BBDomTree>(const BBDomTree &DT,
                                       BBDomTree::VerificationLevel VL);
extern template bool Verify<BBPostDomTree>(const BBPostDomTree &DT,
                                           BBPostDomTree::VerificationLevel VL);
}  // namespace DomTreeBuilder

using DomTreeNode = DomTreeNodeBase<BasicBlock>;

class BasicBlockEdge {
  const BasicBlock *Start;
  const BasicBlock *End;

public:
  BasicBlockEdge(const BasicBlock *Start_, const BasicBlock *End_) :
    Start(Start_), End(End_) {}

  BasicBlockEdge(const std::pair<BasicBlock *, BasicBlock *> &Pair)
      : Start(Pair.first), End(Pair.second) {}

  BasicBlockEdge(const std::pair<const BasicBlock *, const BasicBlock *> &Pair)
      : Start(Pair.first), End(Pair.second) {}

  const BasicBlock *getStart() const {
    return Start;
  }

  const BasicBlock *getEnd() const {
    return End;
  }

  /// Check if this is the only edge between Start and End.
  bool isSingleEdge() const;
};

template <> struct DenseMapInfo<BasicBlockEdge> {
  using BBInfo = DenseMapInfo<const BasicBlock *>;

  static unsigned getHashValue(const BasicBlockEdge *V);

  static inline BasicBlockEdge getEmptyKey() {
    return BasicBlockEdge(BBInfo::getEmptyKey(), BBInfo::getEmptyKey());
  }

  static inline BasicBlockEdge getTombstoneKey() {
    return BasicBlockEdge(BBInfo::getTombstoneKey(), BBInfo::getTombstoneKey());
  }

  static unsigned getHashValue(const BasicBlockEdge &Edge) {
    return hash_combine(BBInfo::getHashValue(Edge.getStart()),
                        BBInfo::getHashValue(Edge.getEnd()));
  }

  static bool isEqual(const BasicBlockEdge &LHS, const BasicBlockEdge &RHS) {
    return BBInfo::isEqual(LHS.getStart(), RHS.getStart()) &&
           BBInfo::isEqual(LHS.getEnd(), RHS.getEnd());
  }
};

/// Concrete subclass of DominatorTreeBase that is used to compute a
/// normal dominator tree.
///
/// Definition: A block is said to be forward statically reachable if there is
/// a path from the entry of the function to the block.  A statically reachable
/// block may become statically unreachable during optimization.
///
/// A forward unreachable block may appear in the dominator tree, or it may
/// not.  If it does, dominance queries will return results as if all reachable
/// blocks dominate it.  When asking for a Node corresponding to a potentially
/// unreachable block, calling code must handle the case where the block was
/// unreachable and the result of getNode() is nullptr.
///
/// Generally, a block known to be unreachable when the dominator tree is
/// constructed will not be in the tree.  One which becomes unreachable after
/// the dominator tree is initially constructed may still exist in the tree,
/// even if the tree is properly updated. Calling code should not rely on the
/// preceding statements; this is stated only to assist human understanding.
class DominatorTree : public DominatorTreeBase<BasicBlock, false> {
 public:
  using Base = DominatorTreeBase<BasicBlock, false>;

  DominatorTree() = default;
  explicit DominatorTree(Function &F) { recalculate(F); }
  explicit DominatorTree(DominatorTree &DT, DomTreeBuilder::BBUpdates U) {
    recalculate(*DT.Parent, U);
  }

  /// Handle invalidation explicitly.
  bool invalidate(Function &F, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &);

  // Ensure base-class overloads are visible.
  using Base::dominates;

  /// Return true if Def dominates a use in User.
  ///
  /// This performs the special checks necessary if Def and User are in the same
  /// basic block. Note that Def doesn't dominate a use in Def itself!
  bool dominates(const Instruction *Def, const Use &U) const;
  bool dominates(const Instruction *Def, const Instruction *User) const;
  bool dominates(const Instruction *Def, const BasicBlock *BB) const;

  /// Return true if an edge dominates a use.
  ///
  /// If BBE is not a unique edge between start and end of the edge, it can
  /// never dominate the use.
  bool dominates(const BasicBlockEdge &BBE, const Use &U) const;
  bool dominates(const BasicBlockEdge &BBE, const BasicBlock *BB) const;

  // Ensure base class overloads are visible.
  using Base::isReachableFromEntry;

  /// Provide an overload for a Use.
  bool isReachableFromEntry(const Use &U) const;

  // Pop up a GraphViz/gv window with the Dominator Tree rendered using `dot`.
  void viewGraph(const Twine &Name, const Twine &Title);
  void viewGraph();
};

//===-------------------------------------
// DominatorTree GraphTraits specializations so the DominatorTree can be
// iterable by generic graph iterators.

template <class Node, class ChildIterator> struct DomTreeGraphTraitsBase {
  using NodeRef = Node *;
  using ChildIteratorType = ChildIterator;
  using nodes_iterator = df_iterator<Node *, df_iterator_default_set<Node*>>;

  static NodeRef getEntryNode(NodeRef N) { return N; }
  static ChildIteratorType child_begin(NodeRef N) { return N->begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->end(); }

  static nodes_iterator nodes_begin(NodeRef N) {
    return df_begin(getEntryNode(N));
  }

  static nodes_iterator nodes_end(NodeRef N) { return df_end(getEntryNode(N)); }
};

template <>
struct GraphTraits<DomTreeNode *>
    : public DomTreeGraphTraitsBase<DomTreeNode, DomTreeNode::iterator> {};

template <>
struct GraphTraits<const DomTreeNode *>
    : public DomTreeGraphTraitsBase<const DomTreeNode,
                                    DomTreeNode::const_iterator> {};

template <> struct GraphTraits<DominatorTree*>
  : public GraphTraits<DomTreeNode*> {
  static NodeRef getEntryNode(DominatorTree *DT) { return DT->getRootNode(); }

  static nodes_iterator nodes_begin(DominatorTree *N) {
    return df_begin(getEntryNode(N));
  }

  static nodes_iterator nodes_end(DominatorTree *N) {
    return df_end(getEntryNode(N));
  }
};

/// Analysis pass which computes a \c DominatorTree.
class DominatorTreeAnalysis : public AnalysisInfoMixin<DominatorTreeAnalysis> {
  friend AnalysisInfoMixin<DominatorTreeAnalysis>;
  static AnalysisKey Key;

public:
  /// Provide the result typedef for this analysis pass.
  using Result = DominatorTree;

  /// Run the analysis pass over a function and produce a dominator tree.
  DominatorTree run(Function &F, FunctionAnalysisManager &);
};

/// Printer pass for the \c DominatorTree.
class DominatorTreePrinterPass
    : public PassInfoMixin<DominatorTreePrinterPass> {
  raw_ostream &OS;

public:
  explicit DominatorTreePrinterPass(raw_ostream &OS);

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Verifier pass for the \c DominatorTree.
struct DominatorTreeVerifierPass : PassInfoMixin<DominatorTreeVerifierPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Legacy analysis pass which computes a \c DominatorTree.
class DominatorTreeWrapperPass : public FunctionPass {
  DominatorTree DT;

public:
  static char ID;

  DominatorTreeWrapperPass() : FunctionPass(ID) {
    initializeDominatorTreeWrapperPassPass(*PassRegistry::getPassRegistry());
  }

  DominatorTree &getDomTree() { return DT; }
  const DominatorTree &getDomTree() const { return DT; }

  bool runOnFunction(Function &F) override;

  void verifyAnalysis() const override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  void releaseMemory() override { DT.releaseMemory(); }

  void print(raw_ostream &OS, const Module *M = nullptr) const override;
};
} // end namespace llvm

#endif // LLVM_IR_DOMINATORS_H
