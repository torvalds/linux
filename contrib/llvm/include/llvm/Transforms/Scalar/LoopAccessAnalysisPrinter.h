//===- llvm/Analysis/LoopAccessAnalysisPrinter.h ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPACCESSANALYSISPRINTER_H
#define LLVM_TRANSFORMS_SCALAR_LOOPACCESSANALYSISPRINTER_H

#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"

namespace llvm {

/// Printer pass for the \c LoopAccessInfo results.
class LoopAccessInfoPrinterPass
    : public PassInfoMixin<LoopAccessInfoPrinterPass> {
  raw_ostream &OS;

public:
  explicit LoopAccessInfoPrinterPass(raw_ostream &OS) : OS(OS) {}
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);
};

} // End llvm namespace

#endif
