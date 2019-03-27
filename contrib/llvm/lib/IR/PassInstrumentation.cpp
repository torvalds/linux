//===- PassInstrumentation.cpp - Pass Instrumentation interface -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides the implementation of PassInstrumentation class.
///
//===----------------------------------------------------------------------===//

#include "llvm/IR/PassInstrumentation.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

AnalysisKey PassInstrumentationAnalysis::Key;

} // namespace llvm
