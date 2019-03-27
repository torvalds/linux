//===-- SystemZTargetInfo.cpp - SystemZ target implementation -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SystemZ.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

Target &llvm::getTheSystemZTarget() {
  static Target TheSystemZTarget;
  return TheSystemZTarget;
}

extern "C" void LLVMInitializeSystemZTargetInfo() {
  RegisterTarget<Triple::systemz, /*HasJIT=*/true> X(
      getTheSystemZTarget(), "systemz", "SystemZ", "SystemZ");
}
