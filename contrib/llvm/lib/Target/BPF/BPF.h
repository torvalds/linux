//===-- BPF.h - Top-level interface for BPF representation ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BPF_BPF_H
#define LLVM_LIB_TARGET_BPF_BPF_H

#include "MCTargetDesc/BPFMCTargetDesc.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
class BPFTargetMachine;

FunctionPass *createBPFISelDag(BPFTargetMachine &TM);
FunctionPass *createBPFMIPeepholePass();
FunctionPass *createBPFMIPreEmitPeepholePass();
FunctionPass *createBPFMIPreEmitCheckingPass();

void initializeBPFMIPeepholePass(PassRegistry&);
void initializeBPFMIPreEmitPeepholePass(PassRegistry&);
void initializeBPFMIPreEmitCheckingPass(PassRegistry&);
}

#endif
