//===-- X86TargetInfo.cpp - X86 Target Implementation ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/X86MCTargetDesc.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

Target &llvm::getTheX86_32Target() {
  static Target TheX86_32Target;
  return TheX86_32Target;
}
Target &llvm::getTheX86_64Target() {
  static Target TheX86_64Target;
  return TheX86_64Target;
}

extern "C" void LLVMInitializeX86TargetInfo() {
  RegisterTarget<Triple::x86, /*HasJIT=*/true> X(
      getTheX86_32Target(), "x86", "32-bit X86: Pentium-Pro and above", "X86");

  RegisterTarget<Triple::x86_64, /*HasJIT=*/true> Y(
      getTheX86_64Target(), "x86-64", "64-bit X86: EM64T and AMD64", "X86");
}
