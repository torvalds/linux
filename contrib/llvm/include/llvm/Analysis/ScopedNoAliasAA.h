//===- ScopedNoAliasAA.h - Scoped No-Alias Alias Analysis -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This is the interface for a metadata-based scoped no-alias analysis.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_SCOPEDNOALIASAA_H
#define LLVM_ANALYSIS_SCOPEDNOALIASAA_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include <memory>

namespace llvm {

class Function;
class MDNode;
class MemoryLocation;

/// A simple AA result which uses scoped-noalias metadata to answer queries.
class ScopedNoAliasAAResult : public AAResultBase<ScopedNoAliasAAResult> {
  friend AAResultBase<ScopedNoAliasAAResult>;

public:
  /// Handle invalidation events from the new pass manager.
  ///
  /// By definition, this result is stateless and so remains valid.
  bool invalidate(Function &, const PreservedAnalyses &,
                  FunctionAnalysisManager::Invalidator &) {
    return false;
  }

  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB);
  ModRefInfo getModRefInfo(const CallBase *Call, const MemoryLocation &Loc);
  ModRefInfo getModRefInfo(const CallBase *Call1, const CallBase *Call2);

private:
  bool mayAliasInScopes(const MDNode *Scopes, const MDNode *NoAlias) const;
};

/// Analysis pass providing a never-invalidated alias analysis result.
class ScopedNoAliasAA : public AnalysisInfoMixin<ScopedNoAliasAA> {
  friend AnalysisInfoMixin<ScopedNoAliasAA>;

  static AnalysisKey Key;

public:
  using Result = ScopedNoAliasAAResult;

  ScopedNoAliasAAResult run(Function &F, FunctionAnalysisManager &AM);
};

/// Legacy wrapper pass to provide the ScopedNoAliasAAResult object.
class ScopedNoAliasAAWrapperPass : public ImmutablePass {
  std::unique_ptr<ScopedNoAliasAAResult> Result;

public:
  static char ID;

  ScopedNoAliasAAWrapperPass();

  ScopedNoAliasAAResult &getResult() { return *Result; }
  const ScopedNoAliasAAResult &getResult() const { return *Result; }

  bool doInitialization(Module &M) override;
  bool doFinalization(Module &M) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

//===--------------------------------------------------------------------===//
//
// createScopedNoAliasAAWrapperPass - This pass implements metadata-based
// scoped noalias analysis.
//
ImmutablePass *createScopedNoAliasAAWrapperPass();

} // end namespace llvm

#endif // LLVM_ANALYSIS_SCOPEDNOALIASAA_H
