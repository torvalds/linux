//===- CVTypeVisitor.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/CVTypeVisitor.h"

#include "llvm/DebugInfo/CodeView/CodeViewError.h"
#include "llvm/DebugInfo/CodeView/TypeCollection.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeRecordMapping.h"
#include "llvm/DebugInfo/CodeView/TypeVisitorCallbackPipeline.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/BinaryStreamReader.h"

using namespace llvm;
using namespace llvm::codeview;


template <typename T>
static Error visitKnownRecord(CVType &Record, TypeVisitorCallbacks &Callbacks) {
  TypeRecordKind RK = static_cast<TypeRecordKind>(Record.Type);
  T KnownRecord(RK);
  if (auto EC = Callbacks.visitKnownRecord(Record, KnownRecord))
    return EC;
  return Error::success();
}

template <typename T>
static Error visitKnownMember(CVMemberRecord &Record,
                              TypeVisitorCallbacks &Callbacks) {
  TypeRecordKind RK = static_cast<TypeRecordKind>(Record.Kind);
  T KnownRecord(RK);
  if (auto EC = Callbacks.visitKnownMember(Record, KnownRecord))
    return EC;
  return Error::success();
}

static Error visitMemberRecord(CVMemberRecord &Record,
                               TypeVisitorCallbacks &Callbacks) {
  if (auto EC = Callbacks.visitMemberBegin(Record))
    return EC;

  switch (Record.Kind) {
  default:
    if (auto EC = Callbacks.visitUnknownMember(Record))
      return EC;
    break;
#define MEMBER_RECORD(EnumName, EnumVal, Name)                                 \
  case EnumName: {                                                             \
    if (auto EC = visitKnownMember<Name##Record>(Record, Callbacks))           \
      return EC;                                                               \
    break;                                                                     \
  }
#define MEMBER_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)                \
  MEMBER_RECORD(EnumVal, EnumVal, AliasName)
#define TYPE_RECORD(EnumName, EnumVal, Name)
#define TYPE_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"
  }

  if (auto EC = Callbacks.visitMemberEnd(Record))
    return EC;

  return Error::success();
}

namespace {

class CVTypeVisitor {
public:
  explicit CVTypeVisitor(TypeVisitorCallbacks &Callbacks);

  Error visitTypeRecord(CVType &Record, TypeIndex Index);
  Error visitTypeRecord(CVType &Record);

  /// Visits the type records in Data. Sets the error flag on parse failures.
  Error visitTypeStream(const CVTypeArray &Types);
  Error visitTypeStream(CVTypeRange Types);
  Error visitTypeStream(TypeCollection &Types);

  Error visitMemberRecord(CVMemberRecord Record);
  Error visitFieldListMemberStream(BinaryStreamReader &Stream);

private:
  Error finishVisitation(CVType &Record);

  /// The interface to the class that gets notified of each visitation.
  TypeVisitorCallbacks &Callbacks;
};

CVTypeVisitor::CVTypeVisitor(TypeVisitorCallbacks &Callbacks)
    : Callbacks(Callbacks) {}

Error CVTypeVisitor::finishVisitation(CVType &Record) {
  switch (Record.Type) {
  default:
    if (auto EC = Callbacks.visitUnknownType(Record))
      return EC;
    break;
#define TYPE_RECORD(EnumName, EnumVal, Name)                                   \
  case EnumName: {                                                             \
    if (auto EC = visitKnownRecord<Name##Record>(Record, Callbacks))           \
      return EC;                                                               \
    break;                                                                     \
  }
#define TYPE_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)                  \
  TYPE_RECORD(EnumVal, EnumVal, AliasName)
#define MEMBER_RECORD(EnumName, EnumVal, Name)
#define MEMBER_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"
  }

  if (auto EC = Callbacks.visitTypeEnd(Record))
    return EC;

  return Error::success();
}

Error CVTypeVisitor::visitTypeRecord(CVType &Record, TypeIndex Index) {
  if (auto EC = Callbacks.visitTypeBegin(Record, Index))
    return EC;

  return finishVisitation(Record);
}

Error CVTypeVisitor::visitTypeRecord(CVType &Record) {
  if (auto EC = Callbacks.visitTypeBegin(Record))
    return EC;

  return finishVisitation(Record);
}

Error CVTypeVisitor::visitMemberRecord(CVMemberRecord Record) {
  return ::visitMemberRecord(Record, Callbacks);
}

/// Visits the type records in Data. Sets the error flag on parse failures.
Error CVTypeVisitor::visitTypeStream(const CVTypeArray &Types) {
  for (auto I : Types) {
    if (auto EC = visitTypeRecord(I))
      return EC;
  }
  return Error::success();
}

Error CVTypeVisitor::visitTypeStream(CVTypeRange Types) {
  for (auto I : Types) {
    if (auto EC = visitTypeRecord(I))
      return EC;
  }
  return Error::success();
}

Error CVTypeVisitor::visitTypeStream(TypeCollection &Types) {
  Optional<TypeIndex> I = Types.getFirst();
  while (I) {
    CVType Type = Types.getType(*I);
    if (auto EC = visitTypeRecord(Type, *I))
      return EC;
    I = Types.getNext(*I);
  }
  return Error::success();
}

Error CVTypeVisitor::visitFieldListMemberStream(BinaryStreamReader &Reader) {
  TypeLeafKind Leaf;
  while (!Reader.empty()) {
    if (auto EC = Reader.readEnum(Leaf))
      return EC;

    CVMemberRecord Record;
    Record.Kind = Leaf;
    if (auto EC = ::visitMemberRecord(Record, Callbacks))
      return EC;
  }

  return Error::success();
}

struct FieldListVisitHelper {
  FieldListVisitHelper(TypeVisitorCallbacks &Callbacks, ArrayRef<uint8_t> Data,
                       VisitorDataSource Source)
      : Stream(Data, llvm::support::little), Reader(Stream),
        Deserializer(Reader),
        Visitor((Source == VDS_BytesPresent) ? Pipeline : Callbacks) {
    if (Source == VDS_BytesPresent) {
      Pipeline.addCallbackToPipeline(Deserializer);
      Pipeline.addCallbackToPipeline(Callbacks);
    }
  }

  BinaryByteStream Stream;
  BinaryStreamReader Reader;
  FieldListDeserializer Deserializer;
  TypeVisitorCallbackPipeline Pipeline;
  CVTypeVisitor Visitor;
};

struct VisitHelper {
  VisitHelper(TypeVisitorCallbacks &Callbacks, VisitorDataSource Source)
      : Visitor((Source == VDS_BytesPresent) ? Pipeline : Callbacks) {
    if (Source == VDS_BytesPresent) {
      Pipeline.addCallbackToPipeline(Deserializer);
      Pipeline.addCallbackToPipeline(Callbacks);
    }
  }

  TypeDeserializer Deserializer;
  TypeVisitorCallbackPipeline Pipeline;
  CVTypeVisitor Visitor;
};
}

Error llvm::codeview::visitTypeRecord(CVType &Record, TypeIndex Index,
                                      TypeVisitorCallbacks &Callbacks,
                                      VisitorDataSource Source) {
  VisitHelper V(Callbacks, Source);
  return V.Visitor.visitTypeRecord(Record, Index);
}

Error llvm::codeview::visitTypeRecord(CVType &Record,
                                      TypeVisitorCallbacks &Callbacks,
                                      VisitorDataSource Source) {
  VisitHelper V(Callbacks, Source);
  return V.Visitor.visitTypeRecord(Record);
}

Error llvm::codeview::visitTypeStream(const CVTypeArray &Types,
                                      TypeVisitorCallbacks &Callbacks,
                                      VisitorDataSource Source) {
  VisitHelper V(Callbacks, Source);
  return V.Visitor.visitTypeStream(Types);
}

Error llvm::codeview::visitTypeStream(CVTypeRange Types,
                                      TypeVisitorCallbacks &Callbacks) {
  VisitHelper V(Callbacks, VDS_BytesPresent);
  return V.Visitor.visitTypeStream(Types);
}

Error llvm::codeview::visitTypeStream(TypeCollection &Types,
                                      TypeVisitorCallbacks &Callbacks) {
  // When the internal visitor calls Types.getType(Index) the interface is
  // required to return a CVType with the bytes filled out.  So we can assume
  // that the bytes will be present when individual records are visited.
  VisitHelper V(Callbacks, VDS_BytesPresent);
  return V.Visitor.visitTypeStream(Types);
}

Error llvm::codeview::visitMemberRecord(CVMemberRecord Record,
                                        TypeVisitorCallbacks &Callbacks,
                                        VisitorDataSource Source) {
  FieldListVisitHelper V(Callbacks, Record.Data, Source);
  return V.Visitor.visitMemberRecord(Record);
}

Error llvm::codeview::visitMemberRecord(TypeLeafKind Kind,
                                        ArrayRef<uint8_t> Record,
                                        TypeVisitorCallbacks &Callbacks) {
  CVMemberRecord R;
  R.Data = Record;
  R.Kind = Kind;
  return visitMemberRecord(R, Callbacks, VDS_BytesPresent);
}

Error llvm::codeview::visitMemberRecordStream(ArrayRef<uint8_t> FieldList,
                                              TypeVisitorCallbacks &Callbacks) {
  FieldListVisitHelper V(Callbacks, FieldList, VDS_BytesPresent);
  return V.Visitor.visitFieldListMemberStream(V.Reader);
}
