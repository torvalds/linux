//===- SCCP.h - Sparse Conditional Constant Propagation ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass implements  interprocedural sparse conditional constant
// propagation and merging.
//
// Specifically, this:
//   * Assumes values are constant unless proven otherwise
//   * Assumes BasicBlocks are dead unless proven otherwise
//   * Proves values to be constant, and replaces them with constants
//   * Proves conditional branches to be unconditional
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_SCCP_H
#define LLVM_TRANSFORMS_IPO_SCCP_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

/// Pass to perform interprocedural constant propagation.
class IPSCCPPass : public PassInfoMixin<IPSCCPPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_SCCP_H
