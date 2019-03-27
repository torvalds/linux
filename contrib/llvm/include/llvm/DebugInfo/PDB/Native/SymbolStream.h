//===- SymbolStream.cpp - PDB Symbol Stream Access --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_RAW_PDBSYMBOLSTREAM_H
#define LLVM_DEBUGINFO_PDB_RAW_PDBSYMBOLSTREAM_H

#include "llvm/DebugInfo/CodeView/SymbolRecord.h"

#include "llvm/Support/Error.h"

namespace llvm {
namespace msf {
class MappedBlockStream;
}
namespace pdb {
class PDBFile;

class SymbolStream {
public:
  SymbolStream(std::unique_ptr<msf::MappedBlockStream> Stream);
  ~SymbolStream();
  Error reload();

  const codeview::CVSymbolArray &getSymbolArray() const {
    return SymbolRecords;
  }

  codeview::CVSymbol readRecord(uint32_t Offset) const;

  iterator_range<codeview::CVSymbolArray::Iterator>
  getSymbols(bool *HadError) const;

  Error commit();

private:
  codeview::CVSymbolArray SymbolRecords;
  std::unique_ptr<msf::MappedBlockStream> Stream;
};
}
}

#endif
