//===- NativeTypeUDT.h - info about class/struct type ------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEUDT_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEUDT_H

#include "llvm/ADT/Optional.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"

namespace llvm {
namespace pdb {

class NativeTypeUDT : public NativeRawSymbol {
public:
  NativeTypeUDT(NativeSession &Session, SymIndexId Id, codeview::TypeIndex TI,
                codeview::ClassRecord Class);

  NativeTypeUDT(NativeSession &Session, SymIndexId Id, codeview::TypeIndex TI,
                codeview::UnionRecord Union);

  NativeTypeUDT(NativeSession &Session, SymIndexId Id,
                NativeTypeUDT &UnmodifiedType,
                codeview::ModifierRecord Modifier);

  ~NativeTypeUDT() override;

  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  std::string getName() const override;
  SymIndexId getLexicalParentId() const override;
  SymIndexId getUnmodifiedTypeId() const override;
  SymIndexId getVirtualTableShapeId() const override;
  uint64_t getLength() const override;
  PDB_UdtType getUdtKind() const override;
  bool hasConstructor() const override;
  bool isConstType() const override;
  bool hasAssignmentOperator() const override;
  bool hasCastOperator() const override;
  bool hasNestedTypes() const override;
  bool hasOverloadedOperator() const override;
  bool isInterfaceUdt() const override;
  bool isIntrinsic() const override;
  bool isNested() const override;
  bool isPacked() const override;
  bool isRefUdt() const override;
  bool isScoped() const override;
  bool isValueUdt() const override;
  bool isUnalignedType() const override;
  bool isVolatileType() const override;

protected:
  codeview::TypeIndex Index;

  Optional<codeview::ClassRecord> Class;
  Optional<codeview::UnionRecord> Union;
  NativeTypeUDT *UnmodifiedType = nullptr;
  codeview::TagRecord *Tag = nullptr;
  Optional<codeview::ModifierRecord> Modifiers;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEUDT_H