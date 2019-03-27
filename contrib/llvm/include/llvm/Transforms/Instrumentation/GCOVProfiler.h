//===- Transforms/Instrumentation/GCOVProfiler.h ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the interface for the GCOV style profiler  pass.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_GCOVPROFILER_H
#define LLVM_TRANSFORMS_GCOVPROFILER_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Instrumentation.h"

namespace llvm {
/// The gcov-style instrumentation pass
class GCOVProfilerPass : public PassInfoMixin<GCOVProfilerPass> {
public:
  GCOVProfilerPass(const GCOVOptions &Options = GCOVOptions::getDefault()) : GCOVOpts(Options) { }
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

private:
  GCOVOptions GCOVOpts;
};

} // End llvm namespace
#endif
