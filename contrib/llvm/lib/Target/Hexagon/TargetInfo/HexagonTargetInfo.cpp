//===-- HexagonTargetInfo.cpp - Hexagon Target Implementation ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Hexagon.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

Target &llvm::getTheHexagonTarget() {
  static Target TheHexagonTarget;
  return TheHexagonTarget;
}

extern "C" void LLVMInitializeHexagonTargetInfo() {
  RegisterTarget<Triple::hexagon, /*HasJIT=*/true> X(
      getTheHexagonTarget(), "hexagon", "Hexagon", "Hexagon");
}
