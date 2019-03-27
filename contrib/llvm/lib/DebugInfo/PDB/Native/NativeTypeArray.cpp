//===- NativeTypeArray.cpp - info about arrays ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NativeTypeArray.h"

#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeBuiltin.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeEnum.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

NativeTypeArray::NativeTypeArray(NativeSession &Session, SymIndexId Id,
                                 codeview::TypeIndex TI,
                                 codeview::ArrayRecord Record)
    : NativeRawSymbol(Session, PDB_SymType::ArrayType, Id), Record(Record),
      Index(TI) {}
NativeTypeArray::~NativeTypeArray() {}

void NativeTypeArray::dump(raw_ostream &OS, int Indent,
                           PdbSymbolIdField ShowIdFields,
                           PdbSymbolIdField RecurseIdFields) const {
  NativeRawSymbol::dump(OS, Indent, ShowIdFields, RecurseIdFields);

  dumpSymbolField(OS, "arrayIndexTypeId", getArrayIndexTypeId(), Indent);
  dumpSymbolIdField(OS, "elementTypeId", getTypeId(), Indent, Session,
                    PdbSymbolIdField::Type, ShowIdFields, RecurseIdFields);

  dumpSymbolIdField(OS, "lexicalParentId", 0, Indent, Session,
                    PdbSymbolIdField::LexicalParent, ShowIdFields,
                    RecurseIdFields);
  dumpSymbolField(OS, "length", getLength(), Indent);
  dumpSymbolField(OS, "count", getCount(), Indent);
  dumpSymbolField(OS, "constType", isConstType(), Indent);
  dumpSymbolField(OS, "unalignedType", isUnalignedType(), Indent);
  dumpSymbolField(OS, "volatileType", isVolatileType(), Indent);
}

SymIndexId NativeTypeArray::getArrayIndexTypeId() const {
  return Session.getSymbolCache().findSymbolByTypeIndex(Record.getIndexType());
}

bool NativeTypeArray::isConstType() const { return false; }

bool NativeTypeArray::isUnalignedType() const { return false; }

bool NativeTypeArray::isVolatileType() const { return false; }

uint32_t NativeTypeArray::getCount() const {
  NativeRawSymbol &Element =
      Session.getSymbolCache().getNativeSymbolById(getTypeId());
  return getLength() / Element.getLength();
}

SymIndexId NativeTypeArray::getTypeId() const {
  return Session.getSymbolCache().findSymbolByTypeIndex(
      Record.getElementType());
}

uint64_t NativeTypeArray::getLength() const { return Record.Size; }