//===- BranchProbabilityInfo.h - Branch Probability Analysis ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass is used to evaluate branch probabilties.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_BRANCHPROBABILITYINFO_H
#define LLVM_ANALYSIS_BRANCHPROBABILITYINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Casting.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <utility>

namespace llvm {

class Function;
class LoopInfo;
class raw_ostream;
class TargetLibraryInfo;
class Value;

/// Analysis providing branch probability information.
///
/// This is a function analysis which provides information on the relative
/// probabilities of each "edge" in the function's CFG where such an edge is
/// defined by a pair (PredBlock and an index in the successors). The
/// probability of an edge from one block is always relative to the
/// probabilities of other edges from the block. The probabilites of all edges
/// from a block sum to exactly one (100%).
/// We use a pair (PredBlock and an index in the successors) to uniquely
/// identify an edge, since we can have multiple edges from Src to Dst.
/// As an example, we can have a switch which jumps to Dst with value 0 and
/// value 10.
class BranchProbabilityInfo {
public:
  BranchProbabilityInfo() = default;

  BranchProbabilityInfo(const Function &F, const LoopInfo &LI,
                        const TargetLibraryInfo *TLI = nullptr) {
    calculate(F, LI, TLI);
  }

  BranchProbabilityInfo(BranchProbabilityInfo &&Arg)
      : Probs(std::move(Arg.Probs)), LastF(Arg.LastF),
        PostDominatedByUnreachable(std::move(Arg.PostDominatedByUnreachable)),
        PostDominatedByColdCall(std::move(Arg.PostDominatedByColdCall)) {}

  BranchProbabilityInfo(const BranchProbabilityInfo &) = delete;
  BranchProbabilityInfo &operator=(const BranchProbabilityInfo &) = delete;

  BranchProbabilityInfo &operator=(BranchProbabilityInfo &&RHS) {
    releaseMemory();
    Probs = std::move(RHS.Probs);
    PostDominatedByColdCall = std::move(RHS.PostDominatedByColdCall);
    PostDominatedByUnreachable = std::move(RHS.PostDominatedByUnreachable);
    return *this;
  }

  void releaseMemory();

  void print(raw_ostream &OS) const;

  /// Get an edge's probability, relative to other out-edges of the Src.
  ///
  /// This routine provides access to the fractional probability between zero
  /// (0%) and one (100%) of this edge executing, relative to other edges
  /// leaving the 'Src' block. The returned probability is never zero, and can
  /// only be one if the source block has only one successor.
  BranchProbability getEdgeProbability(const BasicBlock *Src,
                                       unsigned IndexInSuccessors) const;

  /// Get the probability of going from Src to Dst.
  ///
  /// It returns the sum of all probabilities for edges from Src to Dst.
  BranchProbability getEdgeProbability(const BasicBlock *Src,
                                       const BasicBlock *Dst) const;

  BranchProbability getEdgeProbability(const BasicBlock *Src,
                                       succ_const_iterator Dst) const;

  /// Test if an edge is hot relative to other out-edges of the Src.
  ///
  /// Check whether this edge out of the source block is 'hot'. We define hot
  /// as having a relative probability >= 80%.
  bool isEdgeHot(const BasicBlock *Src, const BasicBlock *Dst) const;

  /// Retrieve the hot successor of a block if one exists.
  ///
  /// Given a basic block, look through its successors and if one exists for
  /// which \see isEdgeHot would return true, return that successor block.
  const BasicBlock *getHotSucc(const BasicBlock *BB) const;

  /// Print an edge's probability.
  ///
  /// Retrieves an edge's probability similarly to \see getEdgeProbability, but
  /// then prints that probability to the provided stream. That stream is then
  /// returned.
  raw_ostream &printEdgeProbability(raw_ostream &OS, const BasicBlock *Src,
                                    const BasicBlock *Dst) const;

  /// Set the raw edge probability for the given edge.
  ///
  /// This allows a pass to explicitly set the edge probability for an edge. It
  /// can be used when updating the CFG to update and preserve the branch
  /// probability information. Read the implementation of how these edge
  /// probabilities are calculated carefully before using!
  void setEdgeProbability(const BasicBlock *Src, unsigned IndexInSuccessors,
                          BranchProbability Prob);

  static BranchProbability getBranchProbStackProtector(bool IsLikely) {
    static const BranchProbability LikelyProb((1u << 20) - 1, 1u << 20);
    return IsLikely ? LikelyProb : LikelyProb.getCompl();
  }

  void calculate(const Function &F, const LoopInfo &LI,
                 const TargetLibraryInfo *TLI = nullptr);

  /// Forget analysis results for the given basic block.
  void eraseBlock(const BasicBlock *BB);

  // Use to track SCCs for handling irreducible loops.
  using SccMap = DenseMap<const BasicBlock *, int>;
  using SccHeaderMap = DenseMap<const BasicBlock *, bool>;
  using SccHeaderMaps = std::vector<SccHeaderMap>;
  struct SccInfo {
    SccMap SccNums;
    SccHeaderMaps SccHeaders;
  };

private:
  // We need to store CallbackVH's in order to correctly handle basic block
  // removal.
  class BasicBlockCallbackVH final : public CallbackVH {
    BranchProbabilityInfo *BPI;

    void deleted() override {
      assert(BPI != nullptr);
      BPI->eraseBlock(cast<BasicBlock>(getValPtr()));
      BPI->Handles.erase(*this);
    }

  public:
    BasicBlockCallbackVH(const Value *V, BranchProbabilityInfo *BPI = nullptr)
        : CallbackVH(const_cast<Value *>(V)), BPI(BPI) {}
  };

  DenseSet<BasicBlockCallbackVH, DenseMapInfo<Value*>> Handles;

  // Since we allow duplicate edges from one basic block to another, we use
  // a pair (PredBlock and an index in the successors) to specify an edge.
  using Edge = std::pair<const BasicBlock *, unsigned>;

  // Default weight value. Used when we don't have information about the edge.
  // TODO: DEFAULT_WEIGHT makes sense during static predication, when none of
  // the successors have a weight yet. But it doesn't make sense when providing
  // weight to an edge that may have siblings with non-zero weights. This can
  // be handled various ways, but it's probably fine for an edge with unknown
  // weight to just "inherit" the non-zero weight of an adjacent successor.
  static const uint32_t DEFAULT_WEIGHT = 16;

  DenseMap<Edge, BranchProbability> Probs;

  /// Track the last function we run over for printing.
  const Function *LastF;

  /// Track the set of blocks directly succeeded by a returning block.
  SmallPtrSet<const BasicBlock *, 16> PostDominatedByUnreachable;

  /// Track the set of blocks that always lead to a cold call.
  SmallPtrSet<const BasicBlock *, 16> PostDominatedByColdCall;

  void updatePostDominatedByUnreachable(const BasicBlock *BB);
  void updatePostDominatedByColdCall(const BasicBlock *BB);
  bool calcUnreachableHeuristics(const BasicBlock *BB);
  bool calcMetadataWeights(const BasicBlock *BB);
  bool calcColdCallHeuristics(const BasicBlock *BB);
  bool calcPointerHeuristics(const BasicBlock *BB);
  bool calcLoopBranchHeuristics(const BasicBlock *BB, const LoopInfo &LI,
                                SccInfo &SccI);
  bool calcZeroHeuristics(const BasicBlock *BB, const TargetLibraryInfo *TLI);
  bool calcFloatingPointHeuristics(const BasicBlock *BB);
  bool calcInvokeHeuristics(const BasicBlock *BB);
};

/// Analysis pass which computes \c BranchProbabilityInfo.
class BranchProbabilityAnalysis
    : public AnalysisInfoMixin<BranchProbabilityAnalysis> {
  friend AnalysisInfoMixin<BranchProbabilityAnalysis>;

  static AnalysisKey Key;

public:
  /// Provide the result type for this analysis pass.
  using Result = BranchProbabilityInfo;

  /// Run the analysis pass over a function and produce BPI.
  BranchProbabilityInfo run(Function &F, FunctionAnalysisManager &AM);
};

/// Printer pass for the \c BranchProbabilityAnalysis results.
class BranchProbabilityPrinterPass
    : public PassInfoMixin<BranchProbabilityPrinterPass> {
  raw_ostream &OS;

public:
  explicit BranchProbabilityPrinterPass(raw_ostream &OS) : OS(OS) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Legacy analysis pass which computes \c BranchProbabilityInfo.
class BranchProbabilityInfoWrapperPass : public FunctionPass {
  BranchProbabilityInfo BPI;

public:
  static char ID;

  BranchProbabilityInfoWrapperPass() : FunctionPass(ID) {
    initializeBranchProbabilityInfoWrapperPassPass(
        *PassRegistry::getPassRegistry());
  }

  BranchProbabilityInfo &getBPI() { return BPI; }
  const BranchProbabilityInfo &getBPI() const { return BPI; }

  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnFunction(Function &F) override;
  void releaseMemory() override;
  void print(raw_ostream &OS, const Module *M = nullptr) const override;
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_BRANCHPROBABILITYINFO_H
