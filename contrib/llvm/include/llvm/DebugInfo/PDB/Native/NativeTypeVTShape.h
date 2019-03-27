//===- NativeTypeVTShape.h - info about virtual table shape ------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEVTSHAPE_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEVTSHAPE_H

#include "llvm/ADT/Optional.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"

namespace llvm {
namespace pdb {

class NativeTypeVTShape : public NativeRawSymbol {
public:
  // Create a pointer record for a non-simple type.
  NativeTypeVTShape(NativeSession &Session, SymIndexId Id,
                    codeview::TypeIndex TI, codeview::VFTableShapeRecord SR);

  ~NativeTypeVTShape() override;

  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  bool isConstType() const override;
  bool isVolatileType() const override;
  bool isUnalignedType() const override;
  uint32_t getCount() const override;

protected:
  codeview::TypeIndex TI;
  codeview::VFTableShapeRecord Record;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEVTSHAPE_H