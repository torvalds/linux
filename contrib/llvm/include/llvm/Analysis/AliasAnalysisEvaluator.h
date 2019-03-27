//===- AliasAnalysisEvaluator.h - Alias Analysis Accuracy Evaluator -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements a simple N^2 alias analysis accuracy evaluator. The
/// analysis result is a set of statistics of how many times the AA
/// infrastructure provides each kind of alias result and mod/ref result when
/// queried with all pairs of pointers in the function.
///
/// It can be used to evaluate a change in an alias analysis implementation,
/// algorithm, or the AA pipeline infrastructure itself. It acts like a stable
/// and easily tested consumer of all AA information exposed.
///
/// This is inspired and adapted from code by: Naveen Neelakantam, Francesco
/// Spadini, and Wojciech Stryjewski.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_ALIASANALYSISEVALUATOR_H
#define LLVM_ANALYSIS_ALIASANALYSISEVALUATOR_H

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class AAResults;

class AAEvaluator : public PassInfoMixin<AAEvaluator> {
  int64_t FunctionCount;
  int64_t NoAliasCount, MayAliasCount, PartialAliasCount, MustAliasCount;
  int64_t NoModRefCount, ModCount, RefCount, ModRefCount;
  int64_t MustCount, MustRefCount, MustModCount, MustModRefCount;

public:
  AAEvaluator()
      : FunctionCount(), NoAliasCount(), MayAliasCount(), PartialAliasCount(),
        MustAliasCount(), NoModRefCount(), ModCount(), RefCount(),
        ModRefCount(), MustCount(), MustRefCount(), MustModCount(),
        MustModRefCount() {}
  AAEvaluator(AAEvaluator &&Arg)
      : FunctionCount(Arg.FunctionCount), NoAliasCount(Arg.NoAliasCount),
        MayAliasCount(Arg.MayAliasCount),
        PartialAliasCount(Arg.PartialAliasCount),
        MustAliasCount(Arg.MustAliasCount), NoModRefCount(Arg.NoModRefCount),
        ModCount(Arg.ModCount), RefCount(Arg.RefCount),
        ModRefCount(Arg.ModRefCount), MustCount(Arg.MustCount),
        MustRefCount(Arg.MustRefCount), MustModCount(Arg.MustModCount),
        MustModRefCount(Arg.MustModRefCount) {
    Arg.FunctionCount = 0;
  }
  ~AAEvaluator();

  /// Run the pass over the function.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

private:
  // Allow the legacy pass to run this using an internal API.
  friend class AAEvalLegacyPass;

  void runInternal(Function &F, AAResults &AA);
};

/// Create a wrapper of the above for the legacy pass manager.
FunctionPass *createAAEvalPass();

}

#endif
