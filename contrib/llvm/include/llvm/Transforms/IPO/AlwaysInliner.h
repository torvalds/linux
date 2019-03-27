//===-- AlwaysInliner.h - Pass to inline "always_inline" functions --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Provides passes to inlining "always_inline" functions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_ALWAYSINLINER_H
#define LLVM_TRANSFORMS_IPO_ALWAYSINLINER_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Inlines functions marked as "always_inline".
///
/// Note that this does not inline call sites marked as always_inline and does
/// not delete the functions even when all users are inlined. The normal
/// inliner should be used to handle call site inlining, this pass's goal is to
/// be the simplest possible pass to remove always_inline function definitions'
/// uses by inlining them. The \c GlobalDCE pass can be used to remove these
/// functions once all users are gone.
class AlwaysInlinerPass : public PassInfoMixin<AlwaysInlinerPass> {
  bool InsertLifetime;

public:
  AlwaysInlinerPass(bool InsertLifetime = true)
      : InsertLifetime(InsertLifetime) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);
};

/// Create a legacy pass manager instance of a pass to inline and remove
/// functions marked as "always_inline".
Pass *createAlwaysInlinerLegacyPass(bool InsertLifetime = true);

}

#endif // LLVM_TRANSFORMS_IPO_ALWAYSINLINER_H
