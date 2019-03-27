//===- ModuleSummaryAnalysis.h - Module summary index builder ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This is the interface to build a ModuleSummaryIndex for a module.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MODULESUMMARYANALYSIS_H
#define LLVM_ANALYSIS_MODULESUMMARYANALYSIS_H

#include "llvm/ADT/Optional.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include <functional>

namespace llvm {

class BlockFrequencyInfo;
class Function;
class Module;
class ProfileSummaryInfo;

/// Direct function to compute a \c ModuleSummaryIndex from a given module.
///
/// If operating within a pass manager which has defined ways to compute the \c
/// BlockFrequencyInfo for a given function, that can be provided via
/// a std::function callback. Otherwise, this routine will manually construct
/// that information.
ModuleSummaryIndex buildModuleSummaryIndex(
    const Module &M,
    std::function<BlockFrequencyInfo *(const Function &F)> GetBFICallback,
    ProfileSummaryInfo *PSI);

/// Analysis pass to provide the ModuleSummaryIndex object.
class ModuleSummaryIndexAnalysis
    : public AnalysisInfoMixin<ModuleSummaryIndexAnalysis> {
  friend AnalysisInfoMixin<ModuleSummaryIndexAnalysis>;

  static AnalysisKey Key;

public:
  using Result = ModuleSummaryIndex;

  Result run(Module &M, ModuleAnalysisManager &AM);
};

/// Legacy wrapper pass to provide the ModuleSummaryIndex object.
class ModuleSummaryIndexWrapperPass : public ModulePass {
  Optional<ModuleSummaryIndex> Index;

public:
  static char ID;

  ModuleSummaryIndexWrapperPass();

  /// Get the index built by pass
  ModuleSummaryIndex &getIndex() { return *Index; }
  const ModuleSummaryIndex &getIndex() const { return *Index; }

  bool runOnModule(Module &M) override;
  bool doFinalization(Module &M) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

//===--------------------------------------------------------------------===//
//
// createModuleSummaryIndexWrapperPass - This pass builds a ModuleSummaryIndex
// object for the module, to be written to bitcode or LLVM assembly.
//
ModulePass *createModuleSummaryIndexWrapperPass();

} // end namespace llvm

#endif // LLVM_ANALYSIS_MODULESUMMARYANALYSIS_H
