//===- NativeTypeArray.h ------------------------------------------ C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEARRAY_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEARRAY_H

#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"

#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {
namespace pdb {

class NativeSession;

class NativeTypeArray : public NativeRawSymbol {
public:
  NativeTypeArray(NativeSession &Session, SymIndexId Id, codeview::TypeIndex TI,
                  codeview::ArrayRecord Record);
  ~NativeTypeArray() override;

  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  SymIndexId getArrayIndexTypeId() const override;

  bool isConstType() const override;
  bool isUnalignedType() const override;
  bool isVolatileType() const override;

  uint32_t getCount() const override;
  SymIndexId getTypeId() const override;
  uint64_t getLength() const override;

protected:
  codeview::ArrayRecord Record;
  codeview::TypeIndex Index;
};

} // namespace pdb
} // namespace llvm

#endif
