//===- VPRecipeBuilder.h - Helper class to build recipes --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_VPRECIPEBUILDER_H
#define LLVM_TRANSFORMS_VECTORIZE_VPRECIPEBUILDER_H

#include "LoopVectorizationPlanner.h"
#include "VPlan.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/IRBuilder.h"

namespace llvm {

class LoopVectorizationLegality;
class LoopVectorizationCostModel;
class TargetTransformInfo;
class TargetLibraryInfo;

/// Helper class to create VPRecipies from IR instructions.
class VPRecipeBuilder {
  /// The loop that we evaluate.
  Loop *OrigLoop;

  /// Target Library Info.
  const TargetLibraryInfo *TLI;

  /// Target Transform Info.
  const TargetTransformInfo *TTI;

  /// The legality analysis.
  LoopVectorizationLegality *Legal;

  /// The profitablity analysis.
  LoopVectorizationCostModel &CM;

  VPBuilder &Builder;

  /// When we if-convert we need to create edge masks. We have to cache values
  /// so that we don't end up with exponential recursion/IR. Note that
  /// if-conversion currently takes place during VPlan-construction, so these
  /// caches are only used at that stage.
  using EdgeMaskCacheTy =
      DenseMap<std::pair<BasicBlock *, BasicBlock *>, VPValue *>;
  using BlockMaskCacheTy = DenseMap<BasicBlock *, VPValue *>;
  EdgeMaskCacheTy EdgeMaskCache;
  BlockMaskCacheTy BlockMaskCache;

public:
  /// A helper function that computes the predicate of the block BB, assuming
  /// that the header block of the loop is set to True. It returns the *entry*
  /// mask for the block BB.
  VPValue *createBlockInMask(BasicBlock *BB, VPlanPtr &Plan);

  /// A helper function that computes the predicate of the edge between SRC
  /// and DST.
  VPValue *createEdgeMask(BasicBlock *Src, BasicBlock *Dst, VPlanPtr &Plan);

  /// Check if \I belongs to an Interleave Group within the given VF \p Range,
  /// \return true in the first returned value if so and false otherwise.
  /// Build a new VPInterleaveGroup Recipe if \I is the primary member of an IG
  /// for \p Range.Start, and provide it as the second returned value.
  /// Note that if \I is an adjunct member of an IG for \p Range.Start, the
  /// \return value is <true, nullptr>, as it is handled by another recipe.
  /// \p Range.End may be decreased to ensure same decision from \p Range.Start
  /// to \p Range.End.
  VPInterleaveRecipe *tryToInterleaveMemory(Instruction *I, VFRange &Range,
                                            VPlanPtr &Plan);

  /// Check if \I is a memory instruction to be widened for \p Range.Start and
  /// potentially masked. Such instructions are handled by a recipe that takes
  /// an additional VPInstruction for the mask.
  VPWidenMemoryInstructionRecipe *
  tryToWidenMemory(Instruction *I, VFRange &Range, VPlanPtr &Plan);

  /// Check if an induction recipe should be constructed for \I within the given
  /// VF \p Range. If so build and return it. If not, return null. \p Range.End
  /// may be decreased to ensure same decision from \p Range.Start to
  /// \p Range.End.
  VPWidenIntOrFpInductionRecipe *tryToOptimizeInduction(Instruction *I,
                                                        VFRange &Range);

  /// Handle non-loop phi nodes. Currently all such phi nodes are turned into
  /// a sequence of select instructions as the vectorizer currently performs
  /// full if-conversion.
  VPBlendRecipe *tryToBlend(Instruction *I, VPlanPtr &Plan);

  /// Check if \p I can be widened within the given VF \p Range. If \p I can be
  /// widened for \p Range.Start, check if the last recipe of \p VPBB can be
  /// extended to include \p I or else build a new VPWidenRecipe for it and
  /// append it to \p VPBB. Return true if \p I can be widened for Range.Start,
  /// false otherwise. Range.End may be decreased to ensure same decision from
  /// \p Range.Start to \p Range.End.
  bool tryToWiden(Instruction *I, VPBasicBlock *VPBB, VFRange &Range);

  /// Create a replicating region for instruction \p I that requires
  /// predication. \p PredRecipe is a VPReplicateRecipe holding \p I.
  VPRegionBlock *createReplicateRegion(Instruction *I, VPRecipeBase *PredRecipe,
                                       VPlanPtr &Plan);

public:
  VPRecipeBuilder(Loop *OrigLoop, const TargetLibraryInfo *TLI,
                  const TargetTransformInfo *TTI,
                  LoopVectorizationLegality *Legal,
                  LoopVectorizationCostModel &CM, VPBuilder &Builder)
      : OrigLoop(OrigLoop), TLI(TLI), TTI(TTI), Legal(Legal), CM(CM),
        Builder(Builder) {}

  /// Check if a recipe can be create for \p I withing the given VF \p Range.
  /// If a recipe can be created, it adds it to \p VPBB.
  bool tryToCreateRecipe(Instruction *Instr, VFRange &Range, VPlanPtr &Plan,
                         VPBasicBlock *VPBB);

  /// Build a VPReplicationRecipe for \p I and enclose it within a Region if it
  /// is predicated. \return \p VPBB augmented with this new recipe if \p I is
  /// not predicated, otherwise \return a new VPBasicBlock that succeeds the new
  /// Region. Update the packing decision of predicated instructions if they
  /// feed \p I. Range.End may be decreased to ensure same recipe behavior from
  /// \p Range.Start to \p Range.End.
  VPBasicBlock *handleReplication(
      Instruction *I, VFRange &Range, VPBasicBlock *VPBB,
      DenseMap<Instruction *, VPReplicateRecipe *> &PredInst2Recipe,
      VPlanPtr &Plan);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_VECTORIZE_VPRECIPEBUILDER_H
