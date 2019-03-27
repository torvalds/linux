//===- MergedLoadStoreMotion.h - merge and hoist/sink load/stores ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//! \file
//! This pass performs merges of loads and stores on both sides of a
//  diamond (hammock). It hoists the loads and sinks the stores.
//
// The algorithm iteratively hoists two loads to the same address out of a
// diamond (hammock) and merges them into a single load in the header. Similar
// it sinks and merges two stores to the tail block (footer). The algorithm
// iterates over the instructions of one side of the diamond and attempts to
// find a matching load/store on the other side. It hoists / sinks when it
// thinks it safe to do so.  This optimization helps with eg. hiding load
// latencies, triggering if-conversion, and reducing static code size.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_MERGEDLOADSTOREMOTION_H
#define LLVM_TRANSFORMS_SCALAR_MERGEDLOADSTOREMOTION_H

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class MergedLoadStoreMotionPass
    : public PassInfoMixin<MergedLoadStoreMotionPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

}

#endif // LLVM_TRANSFORMS_SCALAR_MERGEDLOADSTOREMOTION_H
