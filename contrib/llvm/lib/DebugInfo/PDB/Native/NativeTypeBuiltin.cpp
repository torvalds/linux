//===- NativeTypeBuiltin.cpp -------------------------------------- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NativeTypeBuiltin.h"
#include "llvm/Support/FormatVariadic.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

NativeTypeBuiltin::NativeTypeBuiltin(NativeSession &PDBSession, SymIndexId Id,
                                     ModifierOptions Mods, PDB_BuiltinType T,
                                     uint64_t L)
    : NativeRawSymbol(PDBSession, PDB_SymType::BuiltinType, Id),
      Session(PDBSession), Mods(Mods), Type(T), Length(L) {}

NativeTypeBuiltin::~NativeTypeBuiltin() {}

void NativeTypeBuiltin::dump(raw_ostream &OS, int Indent,
                             PdbSymbolIdField ShowIdFields,
                             PdbSymbolIdField RecurseIdFields) const {}

PDB_SymType NativeTypeBuiltin::getSymTag() const {
  return PDB_SymType::BuiltinType;
}

PDB_BuiltinType NativeTypeBuiltin::getBuiltinType() const { return Type; }

bool NativeTypeBuiltin::isConstType() const {
  return (Mods & ModifierOptions::Const) != ModifierOptions::None;
}

uint64_t NativeTypeBuiltin::getLength() const { return Length; }

bool NativeTypeBuiltin::isUnalignedType() const {
  return (Mods & ModifierOptions::Unaligned) != ModifierOptions::None;
}

bool NativeTypeBuiltin::isVolatileType() const {
  return (Mods & ModifierOptions::Volatile) != ModifierOptions::None;
}
