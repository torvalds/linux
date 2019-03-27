//===-------- LoopDataPrefetch.h - Loop Data Prefetching Pass ---*- C++ -*-===//
//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the interface for LLVM's Loop Data Prefetching Pass.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPDATAPREFETCH_H
#define LLVM_TRANSFORMS_SCALAR_LOOPDATAPREFETCH_H

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

/// An optimization pass inserting data prefetches in loops.
class LoopDataPrefetchPass : public PassInfoMixin<LoopDataPrefetchPass> {
public:
  LoopDataPrefetchPass() = default;

  /// Run the pass over the function.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LOOPDATAPREFETCH_H
