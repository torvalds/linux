//===- IVUsersPrinter.cpp - Induction Variable Users Printer ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/IVUsersPrinter.h"
#include "llvm/Analysis/IVUsers.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

#define DEBUG_TYPE "iv-users"

PreservedAnalyses IVUsersPrinterPass::run(Loop &L, LoopAnalysisManager &AM,
                                          LoopStandardAnalysisResults &AR,
                                          LPMUpdater &U) {
  AM.getResult<IVUsersAnalysis>(L, AR).print(OS);
  return PreservedAnalyses::all();
}
