//===-- AllocaHoisting.h - Hosist allocas to the entry block ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Hoist the alloca instructions in the non-entry blocks to the entry blocks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_NVPTXALLOCAHOISTING_H
#define LLVM_LIB_TARGET_NVPTX_NVPTXALLOCAHOISTING_H

namespace llvm {
class FunctionPass;

extern FunctionPass *createAllocaHoisting();
} // end namespace llvm

#endif
