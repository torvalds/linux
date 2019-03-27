//===- llvm/Analysis/DivergenceAnalysis.h - Divergence Analysis -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// \file
// The divergence analysis determines which instructions and branches are
// divergent given a set of divergent source instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DIVERGENCE_ANALYSIS_H
#define LLVM_ANALYSIS_DIVERGENCE_ANALYSIS_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/Analysis/SyncDependenceAnalysis.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include <vector>

namespace llvm {
class Module;
class Value;
class Instruction;
class Loop;
class raw_ostream;
class TargetTransformInfo;

/// \brief Generic divergence analysis for reducible CFGs.
///
/// This analysis propagates divergence in a data-parallel context from sources
/// of divergence to all users. It requires reducible CFGs. All assignments
/// should be in SSA form.
class DivergenceAnalysis {
public:
  /// \brief This instance will analyze the whole function \p F or the loop \p
  /// RegionLoop.
  ///
  /// \param RegionLoop if non-null the analysis is restricted to \p RegionLoop.
  /// Otherwise the whole function is analyzed.
  /// \param IsLCSSAForm whether the analysis may assume that the IR in the
  /// region in in LCSSA form.
  DivergenceAnalysis(const Function &F, const Loop *RegionLoop,
                     const DominatorTree &DT, const LoopInfo &LI,
                     SyncDependenceAnalysis &SDA, bool IsLCSSAForm);

  /// \brief The loop that defines the analyzed region (if any).
  const Loop *getRegionLoop() const { return RegionLoop; }
  const Function &getFunction() const { return F; }

  /// \brief Whether \p BB is part of the region.
  bool inRegion(const BasicBlock &BB) const;
  /// \brief Whether \p I is part of the region.
  bool inRegion(const Instruction &I) const;

  /// \brief Mark \p UniVal as a value that is always uniform.
  void addUniformOverride(const Value &UniVal);

  /// \brief Mark \p DivVal as a value that is always divergent.
  void markDivergent(const Value &DivVal);

  /// \brief Propagate divergence to all instructions in the region.
  /// Divergence is seeded by calls to \p markDivergent.
  void compute();

  /// \brief Whether any value was marked or analyzed to be divergent.
  bool hasDetectedDivergence() const { return !DivergentValues.empty(); }

  /// \brief Whether \p Val will always return a uniform value regardless of its
  /// operands
  bool isAlwaysUniform(const Value &Val) const;

  /// \brief Whether \p Val is a divergent value
  bool isDivergent(const Value &Val) const;

  void print(raw_ostream &OS, const Module *) const;

private:
  bool updateTerminator(const Instruction &Term) const;
  bool updatePHINode(const PHINode &Phi) const;

  /// \brief Computes whether \p Inst is divergent based on the
  /// divergence of its operands.
  ///
  /// \returns Whether \p Inst is divergent.
  ///
  /// This should only be called for non-phi, non-terminator instructions.
  bool updateNormalInstruction(const Instruction &Inst) const;

  /// \brief Mark users of live-out users as divergent.
  ///
  /// \param LoopHeader the header of the divergent loop.
  ///
  /// Marks all users of live-out values of the loop headed by \p LoopHeader
  /// as divergent and puts them on the worklist.
  void taintLoopLiveOuts(const BasicBlock &LoopHeader);

  /// \brief Push all users of \p Val (in the region) to the worklist
  void pushUsers(const Value &I);

  /// \brief Push all phi nodes in @block to the worklist
  void pushPHINodes(const BasicBlock &Block);

  /// \brief Mark \p Block as join divergent
  ///
  /// A block is join divergent if two threads may reach it from different
  /// incoming blocks at the same time.
  void markBlockJoinDivergent(const BasicBlock &Block) {
    DivergentJoinBlocks.insert(&Block);
  }

  /// \brief Whether \p Val is divergent when read in \p ObservingBlock.
  bool isTemporalDivergent(const BasicBlock &ObservingBlock,
                           const Value &Val) const;

  /// \brief Whether \p Block is join divergent
  ///
  /// (see markBlockJoinDivergent).
  bool isJoinDivergent(const BasicBlock &Block) const {
    return DivergentJoinBlocks.find(&Block) != DivergentJoinBlocks.end();
  }

  /// \brief Propagate control-induced divergence to users (phi nodes and
  /// instructions).
  //
  // \param JoinBlock is a divergent loop exit or join point of two disjoint
  // paths.
  // \returns Whether \p JoinBlock is a divergent loop exit of \p TermLoop.
  bool propagateJoinDivergence(const BasicBlock &JoinBlock,
                               const Loop *TermLoop);

  /// \brief Propagate induced value divergence due to control divergence in \p
  /// Term.
  void propagateBranchDivergence(const Instruction &Term);

  /// \brief Propagate divergent caused by a divergent loop exit.
  ///
  /// \param ExitingLoop is a divergent loop.
  void propagateLoopDivergence(const Loop &ExitingLoop);

private:
  const Function &F;
  // If regionLoop != nullptr, analysis is only performed within \p RegionLoop.
  // Otw, analyze the whole function
  const Loop *RegionLoop;

  const DominatorTree &DT;
  const LoopInfo &LI;

  // Recognized divergent loops
  DenseSet<const Loop *> DivergentLoops;

  // The SDA links divergent branches to divergent control-flow joins.
  SyncDependenceAnalysis &SDA;

  // Use simplified code path for LCSSA form.
  bool IsLCSSAForm;

  // Set of known-uniform values.
  DenseSet<const Value *> UniformOverrides;

  // Blocks with joining divergent control from different predecessors.
  DenseSet<const BasicBlock *> DivergentJoinBlocks;

  // Detected/marked divergent values.
  DenseSet<const Value *> DivergentValues;

  // Internal worklist for divergence propagation.
  std::vector<const Instruction *> Worklist;
};

/// \brief Divergence analysis frontend for GPU kernels.
class GPUDivergenceAnalysis {
  SyncDependenceAnalysis SDA;
  DivergenceAnalysis DA;

public:
  /// Runs the divergence analysis on @F, a GPU kernel
  GPUDivergenceAnalysis(Function &F, const DominatorTree &DT,
                        const PostDominatorTree &PDT, const LoopInfo &LI,
                        const TargetTransformInfo &TTI);

  /// Whether any divergence was detected.
  bool hasDivergence() const { return DA.hasDetectedDivergence(); }

  /// The GPU kernel this analysis result is for
  const Function &getFunction() const { return DA.getFunction(); }

  /// Whether \p V is divergent.
  bool isDivergent(const Value &V) const;

  /// Whether \p V is uniform/non-divergent
  bool isUniform(const Value &V) const { return !isDivergent(V); }

  /// Print all divergent values in the kernel.
  void print(raw_ostream &OS, const Module *) const;
};

} // namespace llvm

#endif // LLVM_ANALYSIS_DIVERGENCE_ANALYSIS_H
