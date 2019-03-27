//===- WebAssemblyDisassemblerEmitter.h - Disassembler tables ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is part of the WebAssembly Disassembler Emitter.
// It contains the interface of the disassembler tables.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_WEBASSEMBLYDISASSEMBLEREMITTER_H
#define LLVM_UTILS_TABLEGEN_WEBASSEMBLYDISASSEMBLEREMITTER_H

#include "CodeGenInstruction.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

void emitWebAssemblyDisassemblerTables(
    raw_ostream &OS,
    const ArrayRef<const CodeGenInstruction *> &NumberedInstructions);

} // namespace llvm

#endif
