//=- PHIEliminationUtils.h - Helper functions for PHI elimination -*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_PHIELIMINATIONUTILS_H
#define LLVM_LIB_CODEGEN_PHIELIMINATIONUTILS_H

#include "llvm/CodeGen/MachineBasicBlock.h"

namespace llvm {
    /// findPHICopyInsertPoint - Find a safe place in MBB to insert a copy from
    /// SrcReg when following the CFG edge to SuccMBB. This needs to be after
    /// any def of SrcReg, but before any subsequent point where control flow
    /// might jump out of the basic block.
    MachineBasicBlock::iterator
    findPHICopyInsertPoint(MachineBasicBlock* MBB, MachineBasicBlock* SuccMBB,
                           unsigned SrcReg);
}

#endif
