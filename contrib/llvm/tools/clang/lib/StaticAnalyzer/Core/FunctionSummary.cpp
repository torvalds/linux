//===- FunctionSummary.cpp - Stores summaries of functions. ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a summary of a function gathered/used by static analysis.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/FunctionSummary.h"

using namespace clang;
using namespace ento;

unsigned FunctionSummariesTy::getTotalNumBasicBlocks() {
  unsigned Total = 0;
  for (const auto &I : Map)
    Total += I.second.TotalBasicBlocks;
  return Total;
}

unsigned FunctionSummariesTy::getTotalNumVisitedBasicBlocks() {
  unsigned Total = 0;
  for (const auto &I : Map)
    Total += I.second.VisitedBasicBlocks.count();
  return Total;
}
