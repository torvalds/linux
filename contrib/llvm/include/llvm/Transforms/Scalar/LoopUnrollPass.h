//===- LoopUnrollPass.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPUNROLLPASS_H
#define LLVM_TRANSFORMS_SCALAR_LOOPUNROLLPASS_H

#include "llvm/ADT/Optional.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;
class Loop;
class LPMUpdater;

/// Loop unroll pass that only does full loop unrolling.
class LoopFullUnrollPass : public PassInfoMixin<LoopFullUnrollPass> {
  const int OptLevel;

  /// If false, use a cost model to determine whether unrolling of a loop is
  /// profitable. If true, only loops that explicitly request unrolling via
  /// metadata are considered. All other loops are skipped.
  const bool OnlyWhenForced;

public:
  explicit LoopFullUnrollPass(int OptLevel = 2, bool OnlyWhenForced = false)
      : OptLevel(OptLevel), OnlyWhenForced(OnlyWhenForced) {}

  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);
};

/// A set of parameters used to control various transforms performed by the
/// LoopUnroll pass. Each of the boolean parameters can be set to:
///      true - enabling the transformation.
///      false - disabling the transformation.
///      None - relying on a global default.
///
/// There is also OptLevel parameter, which is used for additional loop unroll
/// tuning.
///
/// Intended use is to create a default object, modify parameters with
/// additional setters and then pass it to LoopUnrollPass.
///
struct LoopUnrollOptions {
  Optional<bool> AllowPartial;
  Optional<bool> AllowPeeling;
  Optional<bool> AllowRuntime;
  Optional<bool> AllowUpperBound;
  int OptLevel;

  /// If false, use a cost model to determine whether unrolling of a loop is
  /// profitable. If true, only loops that explicitly request unrolling via
  /// metadata are considered. All other loops are skipped.
  bool OnlyWhenForced;

  LoopUnrollOptions(int OptLevel = 2, bool OnlyWhenForced = false)
      : OptLevel(OptLevel), OnlyWhenForced(OnlyWhenForced) {}

  /// Enables or disables partial unrolling. When disabled only full unrolling
  /// is allowed.
  LoopUnrollOptions &setPartial(bool Partial) {
    AllowPartial = Partial;
    return *this;
  }

  /// Enables or disables unrolling of loops with runtime trip count.
  LoopUnrollOptions &setRuntime(bool Runtime) {
    AllowRuntime = Runtime;
    return *this;
  }

  /// Enables or disables loop peeling.
  LoopUnrollOptions &setPeeling(bool Peeling) {
    AllowPeeling = Peeling;
    return *this;
  }

  /// Enables or disables the use of trip count upper bound
  /// in loop unrolling.
  LoopUnrollOptions &setUpperBound(bool UpperBound) {
    AllowUpperBound = UpperBound;
    return *this;
  }

  // Sets "optimization level" tuning parameter for loop unrolling.
  LoopUnrollOptions &setOptLevel(int O) {
    OptLevel = O;
    return *this;
  }
};

/// Loop unroll pass that will support both full and partial unrolling.
/// It is a function pass to have access to function and module analyses.
/// It will also put loops into canonical form (simplified and LCSSA).
class LoopUnrollPass : public PassInfoMixin<LoopUnrollPass> {
  LoopUnrollOptions UnrollOpts;

public:
  /// This uses the target information (or flags) to control the thresholds for
  /// different unrolling stategies but supports all of them.
  explicit LoopUnrollPass(LoopUnrollOptions UnrollOpts = {})
      : UnrollOpts(UnrollOpts) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LOOPUNROLLPASS_H
