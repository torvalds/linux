//===- llvm/Analysis/LegacyDivergenceAnalysis.h - KernelDivergence Analysis -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The kernel divergence analysis is an LLVM pass which can be used to find out
// if a branch instruction in a GPU program (kernel) is divergent or not. It can help
// branch optimizations such as jump threading and loop unswitching to make
// better decisions.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_ANALYSIS_LEGACY_DIVERGENCE_ANALYSIS_H
#define LLVM_ANALYSIS_LEGACY_DIVERGENCE_ANALYSIS_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/DivergenceAnalysis.h"

namespace llvm {
class Value;
class GPUDivergenceAnalysis;
class LegacyDivergenceAnalysis : public FunctionPass {
public:
  static char ID;

  LegacyDivergenceAnalysis() : FunctionPass(ID) {
    initializeLegacyDivergenceAnalysisPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  bool runOnFunction(Function &F) override;

  // Print all divergent branches in the function.
  void print(raw_ostream &OS, const Module *) const override;

  // Returns true if V is divergent at its definition.
  //
  // Even if this function returns false, V may still be divergent when used
  // in a different basic block.
  bool isDivergent(const Value *V) const;

  // Returns true if V is uniform/non-divergent.
  //
  // Even if this function returns true, V may still be divergent when used
  // in a different basic block.
  bool isUniform(const Value *V) const { return !isDivergent(V); }

  // Keep the analysis results uptodate by removing an erased value.
  void removeValue(const Value *V) { DivergentValues.erase(V); }

private:
  // Whether analysis should be performed by GPUDivergenceAnalysis.
  bool shouldUseGPUDivergenceAnalysis(const Function &F) const;

  // (optional) handle to new DivergenceAnalysis
  std::unique_ptr<GPUDivergenceAnalysis> gpuDA;

  // Stores all divergent values.
  DenseSet<const Value *> DivergentValues;
};
} // End llvm namespace

#endif //LLVM_ANALYSIS_LEGACY_DIVERGENCE_ANALYSIS_H
