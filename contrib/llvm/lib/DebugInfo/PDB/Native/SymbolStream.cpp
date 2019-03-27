//===- SymbolStream.cpp - PDB Symbol Stream Access ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/SymbolStream.h"

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::msf;
using namespace llvm::support;
using namespace llvm::pdb;

SymbolStream::SymbolStream(std::unique_ptr<MappedBlockStream> Stream)
    : Stream(std::move(Stream)) {}

SymbolStream::~SymbolStream() {}

Error SymbolStream::reload() {
  BinaryStreamReader Reader(*Stream);

  if (auto EC = Reader.readArray(SymbolRecords, Stream->getLength()))
    return EC;

  return Error::success();
}

iterator_range<codeview::CVSymbolArray::Iterator>
SymbolStream::getSymbols(bool *HadError) const {
  return llvm::make_range(SymbolRecords.begin(HadError), SymbolRecords.end());
}

Error SymbolStream::commit() { return Error::success(); }

codeview::CVSymbol SymbolStream::readRecord(uint32_t Offset) const {
  return *SymbolRecords.at(Offset);
}
