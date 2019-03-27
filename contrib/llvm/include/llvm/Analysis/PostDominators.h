//=- llvm/Analysis/PostDominators.h - Post Dominator Calculation --*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file exposes interfaces to post dominance information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_POSTDOMINATORS_H
#define LLVM_ANALYSIS_POSTDOMINATORS_H

#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

namespace llvm {

class Function;
class raw_ostream;

/// PostDominatorTree Class - Concrete subclass of DominatorTree that is used to
/// compute the post-dominator tree.
class PostDominatorTree : public PostDomTreeBase<BasicBlock> {
public:
  using Base = PostDomTreeBase<BasicBlock>;

  PostDominatorTree() = default;
  explicit PostDominatorTree(Function &F) { recalculate(F); }
  /// Handle invalidation explicitly.
  bool invalidate(Function &F, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &);
};

/// Analysis pass which computes a \c PostDominatorTree.
class PostDominatorTreeAnalysis
    : public AnalysisInfoMixin<PostDominatorTreeAnalysis> {
  friend AnalysisInfoMixin<PostDominatorTreeAnalysis>;

  static AnalysisKey Key;

public:
  /// Provide the result type for this analysis pass.
  using Result = PostDominatorTree;

  /// Run the analysis pass over a function and produce a post dominator
  ///        tree.
  PostDominatorTree run(Function &F, FunctionAnalysisManager &);
};

/// Printer pass for the \c PostDominatorTree.
class PostDominatorTreePrinterPass
    : public PassInfoMixin<PostDominatorTreePrinterPass> {
  raw_ostream &OS;

public:
  explicit PostDominatorTreePrinterPass(raw_ostream &OS);

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

struct PostDominatorTreeWrapperPass : public FunctionPass {
  static char ID; // Pass identification, replacement for typeid

  PostDominatorTree DT;

  PostDominatorTreeWrapperPass() : FunctionPass(ID) {
    initializePostDominatorTreeWrapperPassPass(*PassRegistry::getPassRegistry());
  }

  PostDominatorTree &getPostDomTree() { return DT; }
  const PostDominatorTree &getPostDomTree() const { return DT; }

  bool runOnFunction(Function &F) override;

  void verifyAnalysis() const override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  void releaseMemory() override {
    DT.releaseMemory();
  }

  void print(raw_ostream &OS, const Module*) const override;
};

FunctionPass* createPostDomTree();

template <> struct GraphTraits<PostDominatorTree*>
  : public GraphTraits<DomTreeNode*> {
  static NodeRef getEntryNode(PostDominatorTree *DT) {
    return DT->getRootNode();
  }

  static nodes_iterator nodes_begin(PostDominatorTree *N) {
    if (getEntryNode(N))
      return df_begin(getEntryNode(N));
    else
      return df_end(getEntryNode(N));
  }

  static nodes_iterator nodes_end(PostDominatorTree *N) {
    return df_end(getEntryNode(N));
  }
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_POSTDOMINATORS_H
