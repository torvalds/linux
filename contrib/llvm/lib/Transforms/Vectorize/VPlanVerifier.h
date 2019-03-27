//===-- VPlanVerifier.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the class VPlanVerifier, which contains utility functions
/// to check the consistency of a VPlan. This includes the following kinds of
/// invariants:
///
/// 1. Region/Block invariants:
///   - Region's entry/exit block must have no predecessors/successors,
///     respectively.
///   - Block's parent must be the region immediately containing the block.
///   - Linked blocks must have a bi-directional link (successor/predecessor).
///   - All predecessors/successors of a block must belong to the same region.
///   - Blocks must have no duplicated successor/predecessor.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_VPLANVERIFIER_H
#define LLVM_TRANSFORMS_VECTORIZE_VPLANVERIFIER_H

#include "VPlan.h"

namespace llvm {

/// Class with utility functions that can be used to check the consistency and
/// invariants of a VPlan, including the components of its H-CFG.
class VPlanVerifier {
public:
  /// Verify the invariants of the H-CFG starting from \p TopRegion. The
  /// verification process comprises the following steps:
  /// 1. Region/Block verification: Check the Region/Block verification
  /// invariants for every region in the H-CFG.
  void verifyHierarchicalCFG(const VPRegionBlock *TopRegion) const;
};
} // namespace llvm

#endif //LLVM_TRANSFORMS_VECTORIZE_VPLANVERIFIER_H
