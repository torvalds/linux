//===-- TypeDumpVisitor.h - CodeView type info dumper -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPEDUMPVISITOR_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPEDUMPVISITOR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/CodeView/TypeVisitorCallbacks.h"

namespace llvm {
class ScopedPrinter;

namespace codeview {

class TypeCollection;

/// Dumper for CodeView type streams found in COFF object files and PDB files.
class TypeDumpVisitor : public TypeVisitorCallbacks {
public:
  TypeDumpVisitor(TypeCollection &TpiTypes, ScopedPrinter *W,
                  bool PrintRecordBytes)
      : W(W), PrintRecordBytes(PrintRecordBytes), TpiTypes(TpiTypes) {}

  /// When dumping types from an IPI stream in a PDB, a type index may refer to
  /// a type or an item ID. The dumper will lookup the "name" of the index in
  /// the item database if appropriate. If ItemDB is null, it will use TypeDB,
  /// which is correct when dumping types from an object file (/Z7).
  void setIpiTypes(TypeCollection &Types) { IpiTypes = &Types; }

  void printTypeIndex(StringRef FieldName, TypeIndex TI) const;

  void printItemIndex(StringRef FieldName, TypeIndex TI) const;

  /// Action to take on unknown types. By default, they are ignored.
  Error visitUnknownType(CVType &Record) override;
  Error visitUnknownMember(CVMemberRecord &Record) override;

  /// Paired begin/end actions for all types. Receives all record data,
  /// including the fixed-length record prefix.
  Error visitTypeBegin(CVType &Record) override;
  Error visitTypeBegin(CVType &Record, TypeIndex Index) override;
  Error visitTypeEnd(CVType &Record) override;
  Error visitMemberBegin(CVMemberRecord &Record) override;
  Error visitMemberEnd(CVMemberRecord &Record) override;

#define TYPE_RECORD(EnumName, EnumVal, Name)                                   \
  Error visitKnownRecord(CVType &CVR, Name##Record &Record) override;
#define MEMBER_RECORD(EnumName, EnumVal, Name)                                 \
  Error visitKnownMember(CVMemberRecord &CVR, Name##Record &Record) override;
#define TYPE_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#define MEMBER_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"

private:
  void printMemberAttributes(MemberAttributes Attrs);
  void printMemberAttributes(MemberAccess Access, MethodKind Kind,
                             MethodOptions Options);

  /// Get the database of indices for the stream that we are dumping. If ItemDB
  /// is set, then we must be dumping an item (IPI) stream. This will also
  /// always get the appropriate DB for printing item names.
  TypeCollection &getSourceTypes() const {
    return IpiTypes ? *IpiTypes : TpiTypes;
  }

  ScopedPrinter *W;

  bool PrintRecordBytes = false;

  TypeCollection &TpiTypes;
  TypeCollection *IpiTypes = nullptr;
};

} // end namespace codeview
} // end namespace llvm

#endif
