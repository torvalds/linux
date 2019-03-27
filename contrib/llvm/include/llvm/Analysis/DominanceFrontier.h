//===- llvm/Analysis/DominanceFrontier.h - Dominator Frontiers --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the DominanceFrontier class, which calculate and holds the
// dominance frontier for a function.
//
// This should be considered deprecated, don't add any more uses of this data
// structure.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DOMINANCEFRONTIER_H
#define LLVM_ANALYSIS_DOMINANCEFRONTIER_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/GenericDomTree.h"
#include <cassert>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace llvm {

class Function;
class raw_ostream;

//===----------------------------------------------------------------------===//
/// DominanceFrontierBase - Common base class for computing forward and inverse
/// dominance frontiers for a function.
///
template <class BlockT, bool IsPostDom>
class DominanceFrontierBase {
public:
  using DomSetType = std::set<BlockT *>;                // Dom set for a bb
  using DomSetMapType = std::map<BlockT *, DomSetType>; // Dom set map

protected:
  using BlockTraits = GraphTraits<BlockT *>;

  DomSetMapType Frontiers;
  // Postdominators can have multiple roots.
  SmallVector<BlockT *, IsPostDom ? 4 : 1> Roots;
  static constexpr bool IsPostDominators = IsPostDom;

public:
  DominanceFrontierBase() = default;

  /// getRoots - Return the root blocks of the current CFG.  This may include
  /// multiple blocks if we are computing post dominators.  For forward
  /// dominators, this will always be a single block (the entry node).
  const SmallVectorImpl<BlockT *> &getRoots() const { return Roots; }

  BlockT *getRoot() const {
    assert(Roots.size() == 1 && "Should always have entry node!");
    return Roots[0];
  }

  /// isPostDominator - Returns true if analysis based of postdoms
  bool isPostDominator() const {
    return IsPostDominators;
  }

  void releaseMemory() {
    Frontiers.clear();
  }

  // Accessor interface:
  using iterator = typename DomSetMapType::iterator;
  using const_iterator = typename DomSetMapType::const_iterator;

  iterator begin() { return Frontiers.begin(); }
  const_iterator begin() const { return Frontiers.begin(); }
  iterator end() { return Frontiers.end(); }
  const_iterator end() const { return Frontiers.end(); }
  iterator find(BlockT *B) { return Frontiers.find(B); }
  const_iterator find(BlockT *B) const { return Frontiers.find(B); }

  iterator addBasicBlock(BlockT *BB, const DomSetType &frontier) {
    assert(find(BB) == end() && "Block already in DominanceFrontier!");
    return Frontiers.insert(std::make_pair(BB, frontier)).first;
  }

  /// removeBlock - Remove basic block BB's frontier.
  void removeBlock(BlockT *BB);

  void addToFrontier(iterator I, BlockT *Node);

  void removeFromFrontier(iterator I, BlockT *Node);

  /// compareDomSet - Return false if two domsets match. Otherwise
  /// return true;
  bool compareDomSet(DomSetType &DS1, const DomSetType &DS2) const;

  /// compare - Return true if the other dominance frontier base matches
  /// this dominance frontier base. Otherwise return false.
  bool compare(DominanceFrontierBase &Other) const;

  /// print - Convert to human readable form
  ///
  void print(raw_ostream &OS) const;

  /// dump - Dump the dominance frontier to dbgs().
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() const;
#endif
};

//===-------------------------------------
/// DominanceFrontier Class - Concrete subclass of DominanceFrontierBase that is
/// used to compute a forward dominator frontiers.
///
template <class BlockT>
class ForwardDominanceFrontierBase
    : public DominanceFrontierBase<BlockT, false> {
private:
  using BlockTraits = GraphTraits<BlockT *>;

public:
  using DomTreeT = DomTreeBase<BlockT>;
  using DomTreeNodeT = DomTreeNodeBase<BlockT>;
  using DomSetType = typename DominanceFrontierBase<BlockT, false>::DomSetType;

  void analyze(DomTreeT &DT) {
    assert(DT.getRoots().size() == 1 &&
           "Only one entry block for forward domfronts!");
    this->Roots = {DT.getRoot()};
    calculate(DT, DT[this->Roots[0]]);
  }

  const DomSetType &calculate(const DomTreeT &DT, const DomTreeNodeT *Node);
};

class DominanceFrontier : public ForwardDominanceFrontierBase<BasicBlock> {
public:
  using DomTreeT = DomTreeBase<BasicBlock>;
  using DomTreeNodeT = DomTreeNodeBase<BasicBlock>;
  using DomSetType = DominanceFrontierBase<BasicBlock, false>::DomSetType;
  using iterator = DominanceFrontierBase<BasicBlock, false>::iterator;
  using const_iterator =
      DominanceFrontierBase<BasicBlock, false>::const_iterator;

  /// Handle invalidation explicitly.
  bool invalidate(Function &F, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &);
};

class DominanceFrontierWrapperPass : public FunctionPass {
  DominanceFrontier DF;

public:
  static char ID; // Pass ID, replacement for typeid

  DominanceFrontierWrapperPass();

  DominanceFrontier &getDominanceFrontier() { return DF; }
  const DominanceFrontier &getDominanceFrontier() const { return DF;  }

  void releaseMemory() override;

  bool runOnFunction(Function &) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  void print(raw_ostream &OS, const Module * = nullptr) const override;

  void dump() const;
};

extern template class DominanceFrontierBase<BasicBlock, false>;
extern template class DominanceFrontierBase<BasicBlock, true>;
extern template class ForwardDominanceFrontierBase<BasicBlock>;

/// Analysis pass which computes a \c DominanceFrontier.
class DominanceFrontierAnalysis
    : public AnalysisInfoMixin<DominanceFrontierAnalysis> {
  friend AnalysisInfoMixin<DominanceFrontierAnalysis>;

  static AnalysisKey Key;

public:
  /// Provide the result type for this analysis pass.
  using Result = DominanceFrontier;

  /// Run the analysis pass over a function and produce a dominator tree.
  DominanceFrontier run(Function &F, FunctionAnalysisManager &AM);
};

/// Printer pass for the \c DominanceFrontier.
class DominanceFrontierPrinterPass
    : public PassInfoMixin<DominanceFrontierPrinterPass> {
  raw_ostream &OS;

public:
  explicit DominanceFrontierPrinterPass(raw_ostream &OS);

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_DOMINANCEFRONTIER_H
