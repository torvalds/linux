//===- LoopAccessAnalysisPrinter.cpp - Loop Access Analysis Printer --------==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LoopAccessAnalysisPrinter.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
using namespace llvm;

#define DEBUG_TYPE "loop-accesses"

PreservedAnalyses
LoopAccessInfoPrinterPass::run(Loop &L, LoopAnalysisManager &AM,
                               LoopStandardAnalysisResults &AR, LPMUpdater &) {
  Function &F = *L.getHeader()->getParent();
  auto &LAI = AM.getResult<LoopAccessAnalysis>(L, AR);
  OS << "Loop access info in function '" << F.getName() << "':\n";
  OS.indent(2) << L.getHeader()->getName() << ":\n";
  LAI.print(OS, 4);
  return PreservedAnalyses::all();
}
