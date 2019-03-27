//===- NativeTypeBuiltin.h ---------------------------------------- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEBUILTIN_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEBUILTIN_H

#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"

#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {
namespace pdb {

class NativeSession;

class NativeTypeBuiltin : public NativeRawSymbol {
public:
  NativeTypeBuiltin(NativeSession &PDBSession, SymIndexId Id,
                    codeview::ModifierOptions Mods, PDB_BuiltinType T,
                    uint64_t L);
  ~NativeTypeBuiltin() override;

  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  PDB_SymType getSymTag() const override;

  PDB_BuiltinType getBuiltinType() const override;
  bool isConstType() const override;
  uint64_t getLength() const override;
  bool isUnalignedType() const override;
  bool isVolatileType() const override;

protected:
  NativeSession &Session;
  codeview::ModifierOptions Mods;
  PDB_BuiltinType Type;
  uint64_t Length;
};

} // namespace pdb
} // namespace llvm

#endif
