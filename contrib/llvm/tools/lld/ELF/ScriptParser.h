//===- ScriptParser.h -------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_SCRIPT_PARSER_H
#define LLD_ELF_SCRIPT_PARSER_H

#include "lld/Common/LLVM.h"
#include "llvm/Support/MemoryBuffer.h"

namespace lld {
namespace elf {

// Parses a linker script. Calling this function updates
// Config and ScriptConfig.
void readLinkerScript(MemoryBufferRef MB);

// Parses a version script.
void readVersionScript(MemoryBufferRef MB);

void readDynamicList(MemoryBufferRef MB);

// Parses the defsym expression.
void readDefsym(StringRef Name, MemoryBufferRef MB);

} // namespace elf
} // namespace lld

#endif
