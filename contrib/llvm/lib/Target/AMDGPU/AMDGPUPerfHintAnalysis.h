//===- AMDGPUPerfHintAnalysis.h - analysis of functions memory traffic ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// \brief Analyzes if a function potentially memory bound and if a kernel
/// kernel may benefit from limiting number of waves to reduce cache thrashing.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_MDGPUPERFHINTANALYSIS_H
#define LLVM_LIB_TARGET_AMDGPU_MDGPUPERFHINTANALYSIS_H
#include "llvm/IR/ValueMap.h"
#include "llvm/Pass.h"

namespace llvm {

struct AMDGPUPerfHintAnalysis : public FunctionPass {
  static char ID;

public:
  AMDGPUPerfHintAnalysis() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  bool isMemoryBound(const Function *F) const;

  bool needsWaveLimiter(const Function *F) const;

  struct FuncInfo {
    unsigned MemInstCount;
    unsigned InstCount;
    unsigned IAMInstCount; // Indirect access memory instruction count
    unsigned LSMInstCount; // Large stride memory instruction count
    FuncInfo() : MemInstCount(0), InstCount(0), IAMInstCount(0),
                 LSMInstCount(0) {}
  };

  typedef ValueMap<const Function*, FuncInfo> FuncInfoMap;

private:

  FuncInfoMap FIM;
};
} // namespace llvm
#endif // LLVM_LIB_TARGET_AMDGPU_MDGPUPERFHINTANALYSIS_H
