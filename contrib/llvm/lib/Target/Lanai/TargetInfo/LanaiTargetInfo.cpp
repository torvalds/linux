//===-- LanaiTargetInfo.cpp - Lanai Target Implementation -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Module.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

namespace llvm {
Target &getTheLanaiTarget() {
  static Target TheLanaiTarget;
  return TheLanaiTarget;
}
} // namespace llvm

extern "C" void LLVMInitializeLanaiTargetInfo() {
  RegisterTarget<Triple::lanai> X(getTheLanaiTarget(), "lanai", "Lanai",
                                  "Lanai");
}
