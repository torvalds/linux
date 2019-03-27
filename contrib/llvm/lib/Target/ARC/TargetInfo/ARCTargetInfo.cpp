//===- ARCTargetInfo.cpp - ARC Target Implementation ----------- *- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ARC.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

Target &llvm::getTheARCTarget() {
  static Target TheARCTarget;
  return TheARCTarget;
}

extern "C" void LLVMInitializeARCTargetInfo() {
  RegisterTarget<Triple::arc> X(getTheARCTarget(), "arc", "ARC", "ARC");
}
