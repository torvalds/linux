//===-- ARMTargetInfo.cpp - ARM Target Implementation ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/ARMMCTargetDesc.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

Target &llvm::getTheARMLETarget() {
  static Target TheARMLETarget;
  return TheARMLETarget;
}
Target &llvm::getTheARMBETarget() {
  static Target TheARMBETarget;
  return TheARMBETarget;
}
Target &llvm::getTheThumbLETarget() {
  static Target TheThumbLETarget;
  return TheThumbLETarget;
}
Target &llvm::getTheThumbBETarget() {
  static Target TheThumbBETarget;
  return TheThumbBETarget;
}

extern "C" void LLVMInitializeARMTargetInfo() {
  RegisterTarget<Triple::arm, /*HasJIT=*/true> X(getTheARMLETarget(), "arm",
                                                 "ARM", "ARM");
  RegisterTarget<Triple::armeb, /*HasJIT=*/true> Y(getTheARMBETarget(), "armeb",
                                                   "ARM (big endian)", "ARM");

  RegisterTarget<Triple::thumb, /*HasJIT=*/true> A(getTheThumbLETarget(),
                                                   "thumb", "Thumb", "ARM");
  RegisterTarget<Triple::thumbeb, /*HasJIT=*/true> B(
      getTheThumbBETarget(), "thumbeb", "Thumb (big endian)", "ARM");
}
