//===- CVSymbolVisitor.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/CVSymbolVisitor.h"

#include "llvm/DebugInfo/CodeView/CodeViewError.h"
#include "llvm/DebugInfo/CodeView/SymbolVisitorCallbacks.h"

using namespace llvm;
using namespace llvm::codeview;

CVSymbolVisitor::CVSymbolVisitor(SymbolVisitorCallbacks &Callbacks)
    : Callbacks(Callbacks) {}

template <typename T>
static Error visitKnownRecord(CVSymbol &Record,
                              SymbolVisitorCallbacks &Callbacks) {
  SymbolRecordKind RK = static_cast<SymbolRecordKind>(Record.Type);
  T KnownRecord(RK);
  if (auto EC = Callbacks.visitKnownRecord(Record, KnownRecord))
    return EC;
  return Error::success();
}

static Error finishVisitation(CVSymbol &Record,
                              SymbolVisitorCallbacks &Callbacks) {
  switch (Record.Type) {
  default:
    if (auto EC = Callbacks.visitUnknownSymbol(Record))
      return EC;
    break;
#define SYMBOL_RECORD(EnumName, EnumVal, Name)                                 \
  case EnumName: {                                                             \
    if (auto EC = visitKnownRecord<Name>(Record, Callbacks))                   \
      return EC;                                                               \
    break;                                                                     \
  }
#define SYMBOL_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)                \
  SYMBOL_RECORD(EnumVal, EnumVal, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewSymbols.def"
  }

  if (auto EC = Callbacks.visitSymbolEnd(Record))
    return EC;

  return Error::success();
}

Error CVSymbolVisitor::visitSymbolRecord(CVSymbol &Record) {
  if (auto EC = Callbacks.visitSymbolBegin(Record))
    return EC;
  return finishVisitation(Record, Callbacks);
}

Error CVSymbolVisitor::visitSymbolRecord(CVSymbol &Record, uint32_t Offset) {
  if (auto EC = Callbacks.visitSymbolBegin(Record, Offset))
    return EC;
  return finishVisitation(Record, Callbacks);
}

Error CVSymbolVisitor::visitSymbolStream(const CVSymbolArray &Symbols) {
  for (auto I : Symbols) {
    if (auto EC = visitSymbolRecord(I))
      return EC;
  }
  return Error::success();
}

Error CVSymbolVisitor::visitSymbolStream(const CVSymbolArray &Symbols,
                                         uint32_t InitialOffset) {
  for (auto I : Symbols) {
    if (auto EC = visitSymbolRecord(I, InitialOffset + Symbols.skew()))
      return EC;
    InitialOffset += I.length();
  }
  return Error::success();
}
