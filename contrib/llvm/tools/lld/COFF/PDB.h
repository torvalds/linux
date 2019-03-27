//===- PDB.h ----------------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COFF_PDB_H
#define LLD_COFF_PDB_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {
namespace codeview {
union DebugInfo;
}
}

namespace lld {
namespace coff {
class OutputSection;
class SectionChunk;
class SymbolTable;

void createPDB(SymbolTable *Symtab,
               llvm::ArrayRef<OutputSection *> OutputSections,
               llvm::ArrayRef<uint8_t> SectionTable,
               llvm::codeview::DebugInfo *BuildId);

std::pair<llvm::StringRef, uint32_t> getFileLine(const SectionChunk *C,
                                                 uint32_t Addr);
}
}

#endif
