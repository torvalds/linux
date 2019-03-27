//===-- llvm/CodeGen/GlobalISel/GlobalIsel.cpp --- GlobalISel ----*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
// This file implements the common initialization routines for the
// GlobalISel library.
//===----------------------------------------------------------------------===//

#include "llvm/InitializePasses.h"
#include "llvm/PassRegistry.h"

using namespace llvm;

void llvm::initializeGlobalISel(PassRegistry &Registry) {
  initializeIRTranslatorPass(Registry);
  initializeLegalizerPass(Registry);
  initializeLocalizerPass(Registry);
  initializeRegBankSelectPass(Registry);
  initializeInstructionSelectPass(Registry);
}
