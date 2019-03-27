//===- Transforms/Instrumentation/CGProfile.h -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the interface for LLVM's Call Graph Profile pass.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_CGPROFILE_H
#define LLVM_TRANSFORMS_CGPROFILE_H

#include "llvm/ADT/MapVector.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class CGProfilePass : public PassInfoMixin<CGProfilePass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

private:
  void addModuleFlags(
      Module &M,
      MapVector<std::pair<Function *, Function *>, uint64_t> &Counts) const;
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_CGPROFILE_H
