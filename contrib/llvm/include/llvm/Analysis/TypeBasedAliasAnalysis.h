//===- TypeBasedAliasAnalysis.h - Type-Based Alias Analysis -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This is the interface for a metadata-based TBAA. See the source file for
/// details on the algorithm.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_TYPEBASEDALIASANALYSIS_H
#define LLVM_ANALYSIS_TYPEBASEDALIASANALYSIS_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include <memory>

namespace llvm {

class Function;
class MDNode;
class MemoryLocation;

/// A simple AA result that uses TBAA metadata to answer queries.
class TypeBasedAAResult : public AAResultBase<TypeBasedAAResult> {
  friend AAResultBase<TypeBasedAAResult>;

public:
  /// Handle invalidation events from the new pass manager.
  ///
  /// By definition, this result is stateless and so remains valid.
  bool invalidate(Function &, const PreservedAnalyses &,
                  FunctionAnalysisManager::Invalidator &) {
    return false;
  }

  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB);
  bool pointsToConstantMemory(const MemoryLocation &Loc, bool OrLocal);
  FunctionModRefBehavior getModRefBehavior(const CallBase *Call);
  FunctionModRefBehavior getModRefBehavior(const Function *F);
  ModRefInfo getModRefInfo(const CallBase *Call, const MemoryLocation &Loc);
  ModRefInfo getModRefInfo(const CallBase *Call1, const CallBase *Call2);

private:
  bool Aliases(const MDNode *A, const MDNode *B) const;
  bool PathAliases(const MDNode *A, const MDNode *B) const;
};

/// Analysis pass providing a never-invalidated alias analysis result.
class TypeBasedAA : public AnalysisInfoMixin<TypeBasedAA> {
  friend AnalysisInfoMixin<TypeBasedAA>;

  static AnalysisKey Key;

public:
  using Result = TypeBasedAAResult;

  TypeBasedAAResult run(Function &F, FunctionAnalysisManager &AM);
};

/// Legacy wrapper pass to provide the TypeBasedAAResult object.
class TypeBasedAAWrapperPass : public ImmutablePass {
  std::unique_ptr<TypeBasedAAResult> Result;

public:
  static char ID;

  TypeBasedAAWrapperPass();

  TypeBasedAAResult &getResult() { return *Result; }
  const TypeBasedAAResult &getResult() const { return *Result; }

  bool doInitialization(Module &M) override;
  bool doFinalization(Module &M) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

//===--------------------------------------------------------------------===//
//
// createTypeBasedAAWrapperPass - This pass implements metadata-based
// type-based alias analysis.
//
ImmutablePass *createTypeBasedAAWrapperPass();

} // end namespace llvm

#endif // LLVM_ANALYSIS_TYPEBASEDALIASANALYSIS_H
