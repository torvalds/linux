//===- IVUsersPrinter.h - Induction Variable Users Printing -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_IVUSERSPRINTER_H
#define LLVM_TRANSFORMS_SCALAR_IVUSERSPRINTER_H

#include "llvm/Analysis/IVUsers.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"

namespace llvm {

/// Printer pass for the \c IVUsers for a loop.
class IVUsersPrinterPass : public PassInfoMixin<IVUsersPrinterPass> {
  raw_ostream &OS;

public:
  explicit IVUsersPrinterPass(raw_ostream &OS) : OS(OS) {}
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);
};
}

#endif
