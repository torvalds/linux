//===- NativeSymbolEnumerator.cpp - info about enumerators ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NativeSymbolEnumerator.h"

#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeBuiltin.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeEnum.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

NativeSymbolEnumerator::NativeSymbolEnumerator(
    NativeSession &Session, SymIndexId Id, const NativeTypeEnum &Parent,
    codeview::EnumeratorRecord Record)
    : NativeRawSymbol(Session, PDB_SymType::Data, Id), Parent(Parent),
      Record(std::move(Record)) {}

NativeSymbolEnumerator::~NativeSymbolEnumerator() {}

void NativeSymbolEnumerator::dump(raw_ostream &OS, int Indent,
                                  PdbSymbolIdField ShowIdFields,
                                  PdbSymbolIdField RecurseIdFields) const {
  NativeRawSymbol::dump(OS, Indent, ShowIdFields, RecurseIdFields);
  dumpSymbolIdField(OS, "classParentId", getClassParentId(), Indent, Session,
                    PdbSymbolIdField::ClassParent, ShowIdFields,
                    RecurseIdFields);
  dumpSymbolIdField(OS, "lexicalParentId", getLexicalParentId(), Indent,
                    Session, PdbSymbolIdField::LexicalParent, ShowIdFields,
                    RecurseIdFields);
  dumpSymbolField(OS, "name", getName(), Indent);
  dumpSymbolIdField(OS, "typeId", getTypeId(), Indent, Session,
                    PdbSymbolIdField::Type, ShowIdFields, RecurseIdFields);
  dumpSymbolField(OS, "dataKind", getDataKind(), Indent);
  dumpSymbolField(OS, "locationType", getLocationType(), Indent);
  dumpSymbolField(OS, "constType", isConstType(), Indent);
  dumpSymbolField(OS, "unalignedType", isUnalignedType(), Indent);
  dumpSymbolField(OS, "volatileType", isVolatileType(), Indent);
  dumpSymbolField(OS, "value", getValue(), Indent);
}

SymIndexId NativeSymbolEnumerator::getClassParentId() const {
  return Parent.getSymIndexId();
}

SymIndexId NativeSymbolEnumerator::getLexicalParentId() const { return 0; }

std::string NativeSymbolEnumerator::getName() const { return Record.Name; }

SymIndexId NativeSymbolEnumerator::getTypeId() const {
  return Parent.getTypeId();
}

PDB_DataKind NativeSymbolEnumerator::getDataKind() const {
  return PDB_DataKind::Constant;
}

PDB_LocType NativeSymbolEnumerator::getLocationType() const {
  return PDB_LocType::Constant;
}

bool NativeSymbolEnumerator::isConstType() const { return false; }

bool NativeSymbolEnumerator::isVolatileType() const { return false; }

bool NativeSymbolEnumerator::isUnalignedType() const { return false; }

Variant NativeSymbolEnumerator::getValue() const {
  const NativeTypeBuiltin &BT = Parent.getUnderlyingBuiltinType();

  switch (BT.getBuiltinType()) {
  case PDB_BuiltinType::Int:
  case PDB_BuiltinType::Long:
  case PDB_BuiltinType::Char: {
    assert(Record.Value.isSignedIntN(BT.getLength() * 8));
    int64_t N = Record.Value.getSExtValue();
    switch (BT.getLength()) {
    case 1:
      return Variant{static_cast<int8_t>(N)};
    case 2:
      return Variant{static_cast<int16_t>(N)};
    case 4:
      return Variant{static_cast<int32_t>(N)};
    case 8:
      return Variant{static_cast<int64_t>(N)};
    }
    break;
  }
  case PDB_BuiltinType::UInt:
  case PDB_BuiltinType::ULong: {
    assert(Record.Value.isIntN(BT.getLength() * 8));
    uint64_t U = Record.Value.getZExtValue();
    switch (BT.getLength()) {
    case 1:
      return Variant{static_cast<uint8_t>(U)};
    case 2:
      return Variant{static_cast<uint16_t>(U)};
    case 4:
      return Variant{static_cast<uint32_t>(U)};
    case 8:
      return Variant{static_cast<uint64_t>(U)};
    }
    break;
  }
  case PDB_BuiltinType::Bool: {
    assert(Record.Value.isIntN(BT.getLength() * 8));
    uint64_t U = Record.Value.getZExtValue();
    return Variant{static_cast<bool>(U)};
  }
  default:
    assert(false && "Invalid enumeration type");
    break;
  }

  return Variant{Record.Value.getSExtValue()};
}
