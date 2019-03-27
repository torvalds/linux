//===- NativeTypePointer.h - info about pointer type -------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEPOINTER_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEPOINTER_H

#include "llvm/ADT/Optional.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"

namespace llvm {
namespace pdb {

class NativeTypePointer : public NativeRawSymbol {
public:
  // Create a pointer record for a simple type.
  NativeTypePointer(NativeSession &Session, SymIndexId Id,
                    codeview::TypeIndex TI);

  // Create a pointer record for a non-simple type.
  NativeTypePointer(NativeSession &Session, SymIndexId Id,
                    codeview::TypeIndex TI, codeview::PointerRecord PR);
  ~NativeTypePointer() override;

  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  SymIndexId getClassParentId() const override;
  bool isConstType() const override;
  uint64_t getLength() const override;
  bool isReference() const override;
  bool isRValueReference() const override;
  bool isPointerToDataMember() const override;
  bool isPointerToMemberFunction() const override;
  SymIndexId getTypeId() const override;
  bool isRestrictedType() const override;
  bool isVolatileType() const override;
  bool isUnalignedType() const override;

  bool isSingleInheritance() const override;
  bool isMultipleInheritance() const override;
  bool isVirtualInheritance() const override;

protected:
  bool isMemberPointer() const;
  codeview::TypeIndex TI;
  Optional<codeview::PointerRecord> Record;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEPOINTER_H