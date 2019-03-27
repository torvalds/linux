//===-- MSP430TargetInfo.cpp - MSP430 Target Implementation ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MSP430.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

Target &llvm::getTheMSP430Target() {
  static Target TheMSP430Target;
  return TheMSP430Target;
}

extern "C" void LLVMInitializeMSP430TargetInfo() {
  RegisterTarget<Triple::msp430> X(getTheMSP430Target(), "msp430",
                                   "MSP430 [experimental]", "MSP430");
}
