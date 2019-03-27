//===- SimplifyCFG.h - Simplify and canonicalize the CFG --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the interface for the pass responsible for both
/// simplifying and canonicalizing the CFG.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_SIMPLIFYCFG_H
#define LLVM_TRANSFORMS_SCALAR_SIMPLIFYCFG_H

#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

/// A pass to simplify and canonicalize the CFG of a function.
///
/// This pass iteratively simplifies the entire CFG of a function. It may change
/// or remove control flow to put the CFG into a canonical form expected by
/// other passes of the mid-level optimizer. Depending on the specified options,
/// it may further optimize control-flow to create non-canonical forms.
class SimplifyCFGPass : public PassInfoMixin<SimplifyCFGPass> {
  SimplifyCFGOptions Options;

public:
  /// The default constructor sets the pass options to create canonical IR,
  /// rather than optimal IR. That is, by default we bypass transformations that
  /// are likely to improve performance but make analysis for other passes more
  /// difficult.
  SimplifyCFGPass()
      : SimplifyCFGPass(SimplifyCFGOptions()
                            .forwardSwitchCondToPhi(false)
                            .convertSwitchToLookupTable(false)
                            .needCanonicalLoops(true)
                            .sinkCommonInsts(false)) {}


  /// Construct a pass with optional optimizations.
  SimplifyCFGPass(const SimplifyCFGOptions &PassOptions);

  /// Run the pass over the function.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

}

#endif
