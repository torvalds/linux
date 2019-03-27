//===-- RISCVTargetInfo.cpp - RISCV Target Implementation -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

namespace llvm {
Target &getTheRISCV32Target() {
  static Target TheRISCV32Target;
  return TheRISCV32Target;
}

Target &getTheRISCV64Target() {
  static Target TheRISCV64Target;
  return TheRISCV64Target;
}
}

extern "C" void LLVMInitializeRISCVTargetInfo() {
  RegisterTarget<Triple::riscv32> X(getTheRISCV32Target(), "riscv32",
                                    "32-bit RISC-V", "RISCV");
  RegisterTarget<Triple::riscv64> Y(getTheRISCV64Target(), "riscv64",
                                    "64-bit RISC-V", "RISCV");
}
