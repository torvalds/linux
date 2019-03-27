//===-- ObjCARC.cpp -------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements common infrastructure for libLLVMObjCARCOpts.a, which
// implements several scalar transformations over the LLVM intermediate
// representation, including the C bindings for that library.
//
//===----------------------------------------------------------------------===//

#include "ObjCARC.h"
#include "llvm-c/Initialization.h"
#include "llvm/InitializePasses.h"

namespace llvm {
  class PassRegistry;
}

using namespace llvm;
using namespace llvm::objcarc;

/// initializeObjCARCOptsPasses - Initialize all passes linked into the
/// ObjCARCOpts library.
void llvm::initializeObjCARCOpts(PassRegistry &Registry) {
  initializeObjCARCAAWrapperPassPass(Registry);
  initializeObjCARCAPElimPass(Registry);
  initializeObjCARCExpandPass(Registry);
  initializeObjCARCContractPass(Registry);
  initializeObjCARCOptPass(Registry);
  initializePAEvalPass(Registry);
}

void LLVMInitializeObjCARCOpts(LLVMPassRegistryRef R) {
  initializeObjCARCOpts(*unwrap(R));
}
