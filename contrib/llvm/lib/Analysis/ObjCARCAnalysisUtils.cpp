//===- ObjCARCAnalysisUtils.cpp -------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements common infrastructure for libLLVMObjCARCOpts.a, which
// implements several scalar transformations over the LLVM intermediate
// representation, including the C bindings for that library.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ObjCARCAnalysisUtils.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;
using namespace llvm::objcarc;

/// A handy option to enable/disable all ARC Optimizations.
bool llvm::objcarc::EnableARCOpts;
static cl::opt<bool, true> EnableARCOptimizations(
    "enable-objc-arc-opts", cl::desc("enable/disable all ARC Optimizations"),
    cl::location(EnableARCOpts), cl::init(true), cl::Hidden);
