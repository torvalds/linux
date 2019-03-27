//===- TypeRecordMapping.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPERECORDMAPPING_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPERECORDMAPPING_H

#include "llvm/ADT/Optional.h"
#include "llvm/DebugInfo/CodeView/CodeViewRecordIO.h"
#include "llvm/DebugInfo/CodeView/TypeVisitorCallbacks.h"
#include "llvm/Support/Error.h"

namespace llvm {
class BinaryStreamReader;
class BinaryStreamWriter;

namespace codeview {
class TypeRecordMapping : public TypeVisitorCallbacks {
public:
  explicit TypeRecordMapping(BinaryStreamReader &Reader) : IO(Reader) {}
  explicit TypeRecordMapping(BinaryStreamWriter &Writer) : IO(Writer) {}

  using TypeVisitorCallbacks::visitTypeBegin;
  Error visitTypeBegin(CVType &Record) override;
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
  Optional<TypeLeafKind> TypeKind;
  Optional<TypeLeafKind> MemberKind;

  CodeViewRecordIO IO;
};
}
}

#endif
