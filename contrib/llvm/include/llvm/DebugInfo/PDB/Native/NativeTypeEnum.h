//===- NativeTypeEnum.h - info about enum type ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEENUM_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEENUM_H

#include "llvm/ADT/Optional.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/TypeVisitorCallbacks.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"

namespace llvm {
namespace pdb {

class NativeTypeBuiltin;

class NativeTypeEnum : public NativeRawSymbol {
public:
  NativeTypeEnum(NativeSession &Session, SymIndexId Id, codeview::TypeIndex TI,
                 codeview::EnumRecord Record);

  NativeTypeEnum(NativeSession &Session, SymIndexId Id,
                 NativeTypeEnum &UnmodifiedType,
                 codeview::ModifierRecord Modifier);
  ~NativeTypeEnum() override;

  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  std::unique_ptr<IPDBEnumSymbols>
  findChildren(PDB_SymType Type) const override;

  PDB_BuiltinType getBuiltinType() const override;
  PDB_SymType getSymTag() const override;
  SymIndexId getUnmodifiedTypeId() const override;
  bool hasConstructor() const override;
  bool hasAssignmentOperator() const override;
  bool hasCastOperator() const override;
  uint64_t getLength() const override;
  std::string getName() const override;
  bool isConstType() const override;
  bool isVolatileType() const override;
  bool isUnalignedType() const override;
  bool isNested() const override;
  bool hasOverloadedOperator() const override;
  bool hasNestedTypes() const override;
  bool isIntrinsic() const override;
  bool isPacked() const override;
  bool isScoped() const override;
  SymIndexId getTypeId() const override;
  bool isRefUdt() const override;
  bool isValueUdt() const override;
  bool isInterfaceUdt() const override;

  const NativeTypeBuiltin &getUnderlyingBuiltinType() const;
  const codeview::EnumRecord &getEnumRecord() const { return *Record; }

protected:
  codeview::TypeIndex Index;
  Optional<codeview::EnumRecord> Record;
  NativeTypeEnum *UnmodifiedType = nullptr;
  Optional<codeview::ModifierRecord> Modifiers;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEENUM_H
