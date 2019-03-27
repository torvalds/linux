//===-- NVPTXTargetInfo.cpp - NVPTX Target Implementation -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "NVPTX.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

Target &llvm::getTheNVPTXTarget32() {
  static Target TheNVPTXTarget32;
  return TheNVPTXTarget32;
}
Target &llvm::getTheNVPTXTarget64() {
  static Target TheNVPTXTarget64;
  return TheNVPTXTarget64;
}

extern "C" void LLVMInitializeNVPTXTargetInfo() {
  RegisterTarget<Triple::nvptx> X(getTheNVPTXTarget32(), "nvptx",
                                  "NVIDIA PTX 32-bit", "NVPTX");
  RegisterTarget<Triple::nvptx64> Y(getTheNVPTXTarget64(), "nvptx64",
                                    "NVIDIA PTX 64-bit", "NVPTX");
}
