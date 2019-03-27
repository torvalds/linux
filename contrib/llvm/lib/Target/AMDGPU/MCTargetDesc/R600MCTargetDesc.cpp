//===-- R600MCTargetDesc.cpp - R600 Target Descriptions -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// \brief This file provides R600 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "AMDGPUMCTargetDesc.h"
#include "llvm/MC/MCInstrInfo.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#include "R600GenInstrInfo.inc"

MCInstrInfo *llvm::createR600MCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitR600MCInstrInfo(X);
  return X;
}
