//===-- SparcTargetInfo.cpp - Sparc Target Implementation -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Sparc.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

Target &llvm::getTheSparcTarget() {
  static Target TheSparcTarget;
  return TheSparcTarget;
}
Target &llvm::getTheSparcV9Target() {
  static Target TheSparcV9Target;
  return TheSparcV9Target;
}
Target &llvm::getTheSparcelTarget() {
  static Target TheSparcelTarget;
  return TheSparcelTarget;
}

extern "C" void LLVMInitializeSparcTargetInfo() {
  RegisterTarget<Triple::sparc, /*HasJIT=*/true> X(getTheSparcTarget(), "sparc",
                                                   "Sparc", "Sparc");
  RegisterTarget<Triple::sparcv9, /*HasJIT=*/true> Y(
      getTheSparcV9Target(), "sparcv9", "Sparc V9", "Sparc");
  RegisterTarget<Triple::sparcel, /*HasJIT=*/true> Z(
      getTheSparcelTarget(), "sparcel", "Sparc LE", "Sparc");
}
