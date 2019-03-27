//===- NativeSymbolEnumerator.h - info about enumerator values --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVESYMBOLENUMERATOR_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVESYMBOLENUMERATOR_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"

namespace llvm {
namespace pdb {
class NativeTypeEnum;

class NativeSymbolEnumerator : public NativeRawSymbol {
public:
  NativeSymbolEnumerator(NativeSession &Session, SymIndexId Id,
                         const NativeTypeEnum &Parent,
                         codeview::EnumeratorRecord Record);

  ~NativeSymbolEnumerator() override;

  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  SymIndexId getClassParentId() const override;
  SymIndexId getLexicalParentId() const override;
  std::string getName() const override;
  SymIndexId getTypeId() const override;
  PDB_DataKind getDataKind() const override;
  PDB_LocType getLocationType() const override;
  bool isConstType() const override;
  bool isVolatileType() const override;
  bool isUnalignedType() const override;
  Variant getValue() const override;

protected:
  const NativeTypeEnum &Parent;
  codeview::EnumeratorRecord Record;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEENUM_H
