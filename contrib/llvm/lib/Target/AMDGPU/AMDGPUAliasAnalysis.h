//===- AMDGPUAliasAnalysis --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This is the AMGPU address space based alias analysis pass.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUALIASANALYSIS_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUALIASANALYSIS_H

#include "AMDGPU.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include <algorithm>
#include <memory>

namespace llvm {

class DataLayout;
class MDNode;
class MemoryLocation;

/// A simple AA result that uses TBAA metadata to answer queries.
class AMDGPUAAResult : public AAResultBase<AMDGPUAAResult> {
  friend AAResultBase<AMDGPUAAResult>;

  const DataLayout &DL;

public:
  explicit AMDGPUAAResult(const DataLayout &DL, Triple T) : AAResultBase(),
    DL(DL) {}
  AMDGPUAAResult(AMDGPUAAResult &&Arg)
      : AAResultBase(std::move(Arg)), DL(Arg.DL) {}

  /// Handle invalidation events from the new pass manager.
  ///
  /// By definition, this result is stateless and so remains valid.
  bool invalidate(Function &, const PreservedAnalyses &) { return false; }

  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB);
  bool pointsToConstantMemory(const MemoryLocation &Loc, bool OrLocal);

private:
  bool Aliases(const MDNode *A, const MDNode *B) const;
  bool PathAliases(const MDNode *A, const MDNode *B) const;
};

/// Analysis pass providing a never-invalidated alias analysis result.
class AMDGPUAA : public AnalysisInfoMixin<AMDGPUAA> {
  friend AnalysisInfoMixin<AMDGPUAA>;

  static char PassID;

public:
  using Result = AMDGPUAAResult;

  AMDGPUAAResult run(Function &F, AnalysisManager<Function> &AM) {
    return AMDGPUAAResult(F.getParent()->getDataLayout(),
        Triple(F.getParent()->getTargetTriple()));
  }
};

/// Legacy wrapper pass to provide the AMDGPUAAResult object.
class AMDGPUAAWrapperPass : public ImmutablePass {
  std::unique_ptr<AMDGPUAAResult> Result;

public:
  static char ID;

  AMDGPUAAWrapperPass() : ImmutablePass(ID) {
    initializeAMDGPUAAWrapperPassPass(*PassRegistry::getPassRegistry());
  }

  AMDGPUAAResult &getResult() { return *Result; }
  const AMDGPUAAResult &getResult() const { return *Result; }

  bool doInitialization(Module &M) override {
    Result.reset(new AMDGPUAAResult(M.getDataLayout(),
        Triple(M.getTargetTriple())));
    return false;
  }

  bool doFinalization(Module &M) override {
    Result.reset();
    return false;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

// Wrapper around ExternalAAWrapperPass so that the default constructor gets the
// callback.
class AMDGPUExternalAAWrapper : public ExternalAAWrapperPass {
public:
  static char ID;

  AMDGPUExternalAAWrapper() : ExternalAAWrapperPass(
    [](Pass &P, Function &, AAResults &AAR) {
      if (auto *WrapperPass = P.getAnalysisIfAvailable<AMDGPUAAWrapperPass>())
        AAR.addAAResult(WrapperPass->getResult());
    }) {}
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_AMDGPUALIASANALYSIS_H
