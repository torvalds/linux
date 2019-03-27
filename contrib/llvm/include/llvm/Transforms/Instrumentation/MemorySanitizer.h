//===- Transforms/Instrumentation/MemorySanitizer.h - MSan Pass -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the memoy sanitizer pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_MEMORYSANITIZER_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_MEMORYSANITIZER_H

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

namespace llvm {

// Insert MemorySanitizer instrumentation (detection of uninitialized reads)
FunctionPass *createMemorySanitizerLegacyPassPass(int TrackOrigins = 0,
                                        bool Recover = false,
                                        bool EnableKmsan = false);

/// A function pass for msan instrumentation.
///
/// Instruments functions to detect unitialized reads. This function pass
/// inserts calls to runtime library functions. If the functions aren't declared
/// yet, the pass inserts the declarations. Otherwise the existing globals are
/// used.
struct MemorySanitizerPass : public PassInfoMixin<MemorySanitizerPass> {
  MemorySanitizerPass(int TrackOrigins = 0, bool Recover = false,
                      bool EnableKmsan = false)
      : TrackOrigins(TrackOrigins), Recover(Recover), EnableKmsan(EnableKmsan) {
  }

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);

private:
  int TrackOrigins;
  bool Recover;
  bool EnableKmsan;
};
}

#endif /* LLVM_TRANSFORMS_INSTRUMENTATION_MEMORYSANITIZER_H */
