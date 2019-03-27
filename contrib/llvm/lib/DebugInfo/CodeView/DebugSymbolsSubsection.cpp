//===- DebugSymbolsSubsection.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/DebugSymbolsSubsection.h"

using namespace llvm;
using namespace llvm::codeview;

Error DebugSymbolsSubsectionRef::initialize(BinaryStreamReader Reader) {
  return Reader.readArray(Records, Reader.getLength());
}

uint32_t DebugSymbolsSubsection::calculateSerializedSize() const {
  return Length;
}

Error DebugSymbolsSubsection::commit(BinaryStreamWriter &Writer) const {
  for (const auto &Record : Records) {
    if (auto EC = Writer.writeBytes(Record.RecordData))
      return EC;
  }
  return Error::success();
}

void DebugSymbolsSubsection::addSymbol(CVSymbol Symbol) {
  Records.push_back(Symbol);
  Length += Symbol.length();
}