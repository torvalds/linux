//== llvm/CodeGen/LowLevelType.h ------------------------------- -*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// Implement a low-level type suitable for MachineInstr level instruction
/// selection.
///
/// This provides the CodeGen aspects of LowLevelType, such as Type conversion.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LOWLEVELTYPE_H
#define LLVM_CODEGEN_LOWLEVELTYPE_H

#include "llvm/Support/LowLevelTypeImpl.h"

namespace llvm {

class DataLayout;
class Type;

/// Construct a low-level type based on an LLVM type.
LLT getLLTForType(Type &Ty, const DataLayout &DL);

}

#endif // LLVM_CODEGEN_LOWLEVELTYPE_H
