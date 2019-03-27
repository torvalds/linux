//===- ARC.h - Top-level interface for ARC representation -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the LLVM
// ARC back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARC_ARC_H
#define LLVM_LIB_TARGET_ARC_ARC_H

#include "MCTargetDesc/ARCMCTargetDesc.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

class FunctionPass;
class ARCTargetMachine;

FunctionPass *createARCISelDag(ARCTargetMachine &TM,
                               CodeGenOpt::Level OptLevel);
FunctionPass *createARCExpandPseudosPass();
FunctionPass *createARCBranchFinalizePass();

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARC_ARC_H
