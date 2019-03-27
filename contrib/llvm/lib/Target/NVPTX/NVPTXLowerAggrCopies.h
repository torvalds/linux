//===-- llvm/lib/Target/NVPTX/NVPTXLowerAggrCopies.h ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the NVIDIA specific lowering of
// aggregate copies
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_NVPTXLOWERAGGRCOPIES_H
#define LLVM_LIB_TARGET_NVPTX_NVPTXLOWERAGGRCOPIES_H

namespace llvm {
class FunctionPass;

FunctionPass *createLowerAggrCopies();
}

#endif
