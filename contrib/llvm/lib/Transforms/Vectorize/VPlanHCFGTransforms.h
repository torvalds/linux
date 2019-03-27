//===- VPlanHCFGTransforms.h - Utility VPlan to VPlan transforms ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides utility VPlan to VPlan transformations.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_VPLANHCFGTRANSFORMS_H
#define LLVM_TRANSFORMS_VECTORIZE_VPLANHCFGTRANSFORMS_H

#include "VPlan.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Transforms/Vectorize/LoopVectorizationLegality.h"

namespace llvm {

class VPlanHCFGTransforms {

public:
  /// Replaces the VPInstructions in \p Plan with corresponding
  /// widen recipes.
  static void VPInstructionsToVPRecipes(
      VPlanPtr &Plan,
      LoopVectorizationLegality::InductionList *Inductions,
      SmallPtrSetImpl<Instruction *> &DeadInstructions);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_VECTORIZE_VPLANHCFGTRANSFORMS_H
