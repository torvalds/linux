//==-- MSP430.h - Top-level interface for MSP430 representation --*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in
// the LLVM MSP430 backend.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MSP430_MSP430_H
#define LLVM_LIB_TARGET_MSP430_MSP430_H

#include "MCTargetDesc/MSP430MCTargetDesc.h"
#include "llvm/Target/TargetMachine.h"

namespace MSP430CC {
  // MSP430 specific condition code.
  enum CondCodes {
    COND_E  = 0,  // aka COND_Z
    COND_NE = 1,  // aka COND_NZ
    COND_HS = 2,  // aka COND_C
    COND_LO = 3,  // aka COND_NC
    COND_GE = 4,
    COND_L  = 5,
    COND_N  = 6,  // jump if negative
    COND_NONE,    // unconditional

    COND_INVALID = -1
  };
}

namespace llvm {
  class MSP430TargetMachine;
  class FunctionPass;
  class formatted_raw_ostream;

  FunctionPass *createMSP430ISelDag(MSP430TargetMachine &TM,
                                    CodeGenOpt::Level OptLevel);

  FunctionPass *createMSP430BranchSelectionPass();

} // end namespace llvm;

#endif
