//===- DLL.h ----------------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COFF_DLL_H
#define LLD_COFF_DLL_H

#include "Chunks.h"
#include "Symbols.h"

namespace lld {
namespace coff {

// Windows-specific.
// IdataContents creates all chunks for the DLL import table.
// You are supposed to call add() to add symbols and then
// call create() to populate the chunk vectors.
class IdataContents {
public:
  void add(DefinedImportData *Sym) { Imports.push_back(Sym); }
  bool empty() { return Imports.empty(); }

  void create();

  std::vector<DefinedImportData *> Imports;
  std::vector<Chunk *> Dirs;
  std::vector<Chunk *> Lookups;
  std::vector<Chunk *> Addresses;
  std::vector<Chunk *> Hints;
  std::vector<Chunk *> DLLNames;
};

// Windows-specific.
// DelayLoadContents creates all chunks for the delay-load DLL import table.
class DelayLoadContents {
public:
  void add(DefinedImportData *Sym) { Imports.push_back(Sym); }
  bool empty() { return Imports.empty(); }
  void create(Defined *Helper);
  std::vector<Chunk *> getChunks();
  std::vector<Chunk *> getDataChunks();
  ArrayRef<Chunk *> getCodeChunks() { return Thunks; }

  uint64_t getDirRVA() { return Dirs[0]->getRVA(); }
  uint64_t getDirSize();

private:
  Chunk *newThunkChunk(DefinedImportData *S, Chunk *Dir);

  Defined *Helper;
  std::vector<DefinedImportData *> Imports;
  std::vector<Chunk *> Dirs;
  std::vector<Chunk *> ModuleHandles;
  std::vector<Chunk *> Addresses;
  std::vector<Chunk *> Names;
  std::vector<Chunk *> HintNames;
  std::vector<Chunk *> Thunks;
  std::vector<Chunk *> DLLNames;
};

// Windows-specific.
// EdataContents creates all chunks for the DLL export table.
class EdataContents {
public:
  EdataContents();
  std::vector<Chunk *> Chunks;

  uint64_t getRVA() { return Chunks[0]->getRVA(); }
  uint64_t getSize() {
    return Chunks.back()->getRVA() + Chunks.back()->getSize() - getRVA();
  }
};

} // namespace coff
} // namespace lld

#endif
