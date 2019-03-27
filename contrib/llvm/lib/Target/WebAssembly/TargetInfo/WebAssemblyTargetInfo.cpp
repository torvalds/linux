//===-- WebAssemblyTargetInfo.cpp - WebAssembly Target Implementation -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file registers the WebAssembly target.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-target-info"

Target &llvm::getTheWebAssemblyTarget32() {
  static Target TheWebAssemblyTarget32;
  return TheWebAssemblyTarget32;
}
Target &llvm::getTheWebAssemblyTarget64() {
  static Target TheWebAssemblyTarget64;
  return TheWebAssemblyTarget64;
}

extern "C" void LLVMInitializeWebAssemblyTargetInfo() {
  RegisterTarget<Triple::wasm32> X(getTheWebAssemblyTarget32(), "wasm32",
                                   "WebAssembly 32-bit", "WebAssembly");
  RegisterTarget<Triple::wasm64> Y(getTheWebAssemblyTarget64(), "wasm64",
                                   "WebAssembly 64-bit", "WebAssembly");
}
