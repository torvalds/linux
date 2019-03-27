//==- CFLAndersAliasAnalysis.h - Unification-based Alias Analysis -*- C++-*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This is the interface for LLVM's inclusion-based alias analysis
/// implemented with CFL graph reachability.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CFLANDERSALIASANALYSIS_H
#define LLVM_ANALYSIS_CFLANDERSALIASANALYSIS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CFLAliasAnalysisUtils.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include <forward_list>
#include <memory>

namespace llvm {

class Function;
class MemoryLocation;
class TargetLibraryInfo;

namespace cflaa {

struct AliasSummary;

} // end namespace cflaa

class CFLAndersAAResult : public AAResultBase<CFLAndersAAResult> {
  friend AAResultBase<CFLAndersAAResult>;

  class FunctionInfo;

public:
  explicit CFLAndersAAResult(const TargetLibraryInfo &TLI);
  CFLAndersAAResult(CFLAndersAAResult &&RHS);
  ~CFLAndersAAResult();

  /// Handle invalidation events from the new pass manager.
  /// By definition, this result is stateless and so remains valid.
  bool invalidate(Function &, const PreservedAnalyses &,
                  FunctionAnalysisManager::Invalidator &) {
    return false;
  }

  /// Evict the given function from cache
  void evict(const Function *Fn);

  /// Get the alias summary for the given function
  /// Return nullptr if the summary is not found or not available
  const cflaa::AliasSummary *getAliasSummary(const Function &);

  AliasResult query(const MemoryLocation &, const MemoryLocation &);
  AliasResult alias(const MemoryLocation &, const MemoryLocation &);

private:
  /// Ensures that the given function is available in the cache.
  /// Returns the appropriate entry from the cache.
  const Optional<FunctionInfo> &ensureCached(const Function &);

  /// Inserts the given Function into the cache.
  void scan(const Function &);

  /// Build summary for a given function
  FunctionInfo buildInfoFrom(const Function &);

  const TargetLibraryInfo &TLI;

  /// Cached mapping of Functions to their StratifiedSets.
  /// If a function's sets are currently being built, it is marked
  /// in the cache as an Optional without a value. This way, if we
  /// have any kind of recursion, it is discernable from a function
  /// that simply has empty sets.
  DenseMap<const Function *, Optional<FunctionInfo>> Cache;

  std::forward_list<cflaa::FunctionHandle<CFLAndersAAResult>> Handles;
};

/// Analysis pass providing a never-invalidated alias analysis result.
///
/// FIXME: We really should refactor CFL to use the analysis more heavily, and
/// in particular to leverage invalidation to trigger re-computation.
class CFLAndersAA : public AnalysisInfoMixin<CFLAndersAA> {
  friend AnalysisInfoMixin<CFLAndersAA>;

  static AnalysisKey Key;

public:
  using Result = CFLAndersAAResult;

  CFLAndersAAResult run(Function &F, FunctionAnalysisManager &AM);
};

/// Legacy wrapper pass to provide the CFLAndersAAResult object.
class CFLAndersAAWrapperPass : public ImmutablePass {
  std::unique_ptr<CFLAndersAAResult> Result;

public:
  static char ID;

  CFLAndersAAWrapperPass();

  CFLAndersAAResult &getResult() { return *Result; }
  const CFLAndersAAResult &getResult() const { return *Result; }

  void initializePass() override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

// createCFLAndersAAWrapperPass - This pass implements a set-based approach to
// alias analysis.
ImmutablePass *createCFLAndersAAWrapperPass();

} // end namespace llvm

#endif // LLVM_ANALYSIS_CFLANDERSALIASANALYSIS_H
