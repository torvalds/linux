//===- EntryExitInstrumenter.h - Function Entry/Exit Instrumentation ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// EntryExitInstrumenter pass - Instrument function entry/exit with calls to
// mcount(), @__cyg_profile_func_{enter,exit} and the like. There are two
// variants, intended to run pre- and post-inlining, respectively.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_ENTRYEXITINSTRUMENTER_H
#define LLVM_TRANSFORMS_UTILS_ENTRYEXITINSTRUMENTER_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

struct EntryExitInstrumenterPass
    : public PassInfoMixin<EntryExitInstrumenterPass> {
  EntryExitInstrumenterPass(bool PostInlining) : PostInlining(PostInlining) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  bool PostInlining;
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_ENTRYEXITINSTRUMENTER_H
