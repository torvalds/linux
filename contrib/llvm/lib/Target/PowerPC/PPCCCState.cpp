//===---- PPCCCState.cpp - CCState with PowerPC specific extensions ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PPCCCState.h"
#include "PPCSubtarget.h"
#include "llvm/IR/Module.h"
using namespace llvm;

// Identify lowered values that originated from ppcf128 arguments and record
// this.
void PPCCCState::PreAnalyzeCallOperands(
    const SmallVectorImpl<ISD::OutputArg> &Outs) {
  for (const auto &I : Outs) {
    if (I.ArgVT == llvm::MVT::ppcf128)
      OriginalArgWasPPCF128.push_back(true);
    else
      OriginalArgWasPPCF128.push_back(false);
  }
}

void PPCCCState::PreAnalyzeFormalArguments(
    const SmallVectorImpl<ISD::InputArg> &Ins) {
  for (const auto &I : Ins) {
    if (I.ArgVT == llvm::MVT::ppcf128) {
      OriginalArgWasPPCF128.push_back(true);
    } else {
      OriginalArgWasPPCF128.push_back(false);
    }
  }
}