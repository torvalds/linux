//===- TypeRecordMapping.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/TypeRecordMapping.h"

using namespace llvm;
using namespace llvm::codeview;

#define error(X)                                                               \
  if (auto EC = X)                                                             \
    return EC;

namespace {
struct MapOneMethodRecord {
  explicit MapOneMethodRecord(bool IsFromOverloadList)
      : IsFromOverloadList(IsFromOverloadList) {}

  Error operator()(CodeViewRecordIO &IO, OneMethodRecord &Method) const {
    error(IO.mapInteger(Method.Attrs.Attrs));
    if (IsFromOverloadList) {
      uint16_t Padding = 0;
      error(IO.mapInteger(Padding));
    }
    error(IO.mapInteger(Method.Type));
    if (Method.isIntroducingVirtual()) {
      error(IO.mapInteger(Method.VFTableOffset));
    } else if (!IO.isWriting())
      Method.VFTableOffset = -1;

    if (!IsFromOverloadList)
      error(IO.mapStringZ(Method.Name));

    return Error::success();
  }

private:
  bool IsFromOverloadList;
};
}

static Error mapNameAndUniqueName(CodeViewRecordIO &IO, StringRef &Name,
                                  StringRef &UniqueName, bool HasUniqueName) {
  if (IO.isWriting()) {
    // Try to be smart about what we write here.  We can't write anything too
    // large, so if we're going to go over the limit, truncate both the name
    // and unique name by the same amount.
    size_t BytesLeft = IO.maxFieldLength();
    if (HasUniqueName) {
      size_t BytesNeeded = Name.size() + UniqueName.size() + 2;
      StringRef N = Name;
      StringRef U = UniqueName;
      if (BytesNeeded > BytesLeft) {
        size_t BytesToDrop = (BytesNeeded - BytesLeft);
        size_t DropN = std::min(N.size(), BytesToDrop / 2);
        size_t DropU = std::min(U.size(), BytesToDrop - DropN);

        N = N.drop_back(DropN);
        U = U.drop_back(DropU);
      }

      error(IO.mapStringZ(N));
      error(IO.mapStringZ(U));
    } else {
      // Cap the length of the string at however many bytes we have available,
      // plus one for the required null terminator.
      auto N = StringRef(Name).take_front(BytesLeft - 1);
      error(IO.mapStringZ(N));
    }
  } else {
    error(IO.mapStringZ(Name));
    if (HasUniqueName)
      error(IO.mapStringZ(UniqueName));
  }

  return Error::success();
}

Error TypeRecordMapping::visitTypeBegin(CVType &CVR) {
  assert(!TypeKind.hasValue() && "Already in a type mapping!");
  assert(!MemberKind.hasValue() && "Already in a member mapping!");

  // FieldList and MethodList records can be any length because they can be
  // split with continuation records.  All other record types cannot be
  // longer than the maximum record length.
  Optional<uint32_t> MaxLen;
  if (CVR.Type != TypeLeafKind::LF_FIELDLIST &&
      CVR.Type != TypeLeafKind::LF_METHODLIST)
    MaxLen = MaxRecordLength - sizeof(RecordPrefix);
  error(IO.beginRecord(MaxLen));
  TypeKind = CVR.Type;
  return Error::success();
}

Error TypeRecordMapping::visitTypeEnd(CVType &Record) {
  assert(TypeKind.hasValue() && "Not in a type mapping!");
  assert(!MemberKind.hasValue() && "Still in a member mapping!");

  error(IO.endRecord());

  TypeKind.reset();
  return Error::success();
}

Error TypeRecordMapping::visitMemberBegin(CVMemberRecord &Record) {
  assert(TypeKind.hasValue() && "Not in a type mapping!");
  assert(!MemberKind.hasValue() && "Already in a member mapping!");

  // The largest possible subrecord is one in which there is a record prefix,
  // followed by the subrecord, followed by a continuation, and that entire
  // sequence spaws `MaxRecordLength` bytes.  So the record's length is
  // calculated as follows.
  constexpr uint32_t ContinuationLength = 8;
  error(IO.beginRecord(MaxRecordLength - sizeof(RecordPrefix) -
                       ContinuationLength));

  MemberKind = Record.Kind;
  return Error::success();
}

Error TypeRecordMapping::visitMemberEnd(CVMemberRecord &Record) {
  assert(TypeKind.hasValue() && "Not in a type mapping!");
  assert(MemberKind.hasValue() && "Not in a member mapping!");

  if (!IO.isWriting()) {
    if (auto EC = IO.skipPadding())
      return EC;
  }

  MemberKind.reset();
  error(IO.endRecord());
  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, ModifierRecord &Record) {
  error(IO.mapInteger(Record.ModifiedType));
  error(IO.mapEnum(Record.Modifiers));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          ProcedureRecord &Record) {
  error(IO.mapInteger(Record.ReturnType));
  error(IO.mapEnum(Record.CallConv));
  error(IO.mapEnum(Record.Options));
  error(IO.mapInteger(Record.ParameterCount));
  error(IO.mapInteger(Record.ArgumentList));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          MemberFunctionRecord &Record) {
  error(IO.mapInteger(Record.ReturnType));
  error(IO.mapInteger(Record.ClassType));
  error(IO.mapInteger(Record.ThisType));
  error(IO.mapEnum(Record.CallConv));
  error(IO.mapEnum(Record.Options));
  error(IO.mapInteger(Record.ParameterCount));
  error(IO.mapInteger(Record.ArgumentList));
  error(IO.mapInteger(Record.ThisPointerAdjustment));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, ArgListRecord &Record) {
  error(IO.mapVectorN<uint32_t>(
      Record.ArgIndices,
      [](CodeViewRecordIO &IO, TypeIndex &N) { return IO.mapInteger(N); }));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          StringListRecord &Record) {
  error(IO.mapVectorN<uint32_t>(
      Record.StringIndices,
      [](CodeViewRecordIO &IO, TypeIndex &N) { return IO.mapInteger(N); }));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, PointerRecord &Record) {
  error(IO.mapInteger(Record.ReferentType));
  error(IO.mapInteger(Record.Attrs));

  if (Record.isPointerToMember()) {
    if (!IO.isWriting())
      Record.MemberInfo.emplace();

    MemberPointerInfo &M = *Record.MemberInfo;
    error(IO.mapInteger(M.ContainingType));
    error(IO.mapEnum(M.Representation));
  }

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, ArrayRecord &Record) {
  error(IO.mapInteger(Record.ElementType));
  error(IO.mapInteger(Record.IndexType));
  error(IO.mapEncodedInteger(Record.Size));
  error(IO.mapStringZ(Record.Name));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, ClassRecord &Record) {
  assert((CVR.Type == TypeLeafKind::LF_STRUCTURE) ||
         (CVR.Type == TypeLeafKind::LF_CLASS) ||
         (CVR.Type == TypeLeafKind::LF_INTERFACE));

  error(IO.mapInteger(Record.MemberCount));
  error(IO.mapEnum(Record.Options));
  error(IO.mapInteger(Record.FieldList));
  error(IO.mapInteger(Record.DerivationList));
  error(IO.mapInteger(Record.VTableShape));
  error(IO.mapEncodedInteger(Record.Size));
  error(mapNameAndUniqueName(IO, Record.Name, Record.UniqueName,
                             Record.hasUniqueName()));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, UnionRecord &Record) {
  error(IO.mapInteger(Record.MemberCount));
  error(IO.mapEnum(Record.Options));
  error(IO.mapInteger(Record.FieldList));
  error(IO.mapEncodedInteger(Record.Size));
  error(mapNameAndUniqueName(IO, Record.Name, Record.UniqueName,
                             Record.hasUniqueName()));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, EnumRecord &Record) {
  error(IO.mapInteger(Record.MemberCount));
  error(IO.mapEnum(Record.Options));
  error(IO.mapInteger(Record.UnderlyingType));
  error(IO.mapInteger(Record.FieldList));
  error(mapNameAndUniqueName(IO, Record.Name, Record.UniqueName,
                             Record.hasUniqueName()));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, BitFieldRecord &Record) {
  error(IO.mapInteger(Record.Type));
  error(IO.mapInteger(Record.BitSize));
  error(IO.mapInteger(Record.BitOffset));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          VFTableShapeRecord &Record) {
  uint16_t Size;
  if (IO.isWriting()) {
    ArrayRef<VFTableSlotKind> Slots = Record.getSlots();
    Size = Slots.size();
    error(IO.mapInteger(Size));

    for (size_t SlotIndex = 0; SlotIndex < Slots.size(); SlotIndex += 2) {
      uint8_t Byte = static_cast<uint8_t>(Slots[SlotIndex]) << 4;
      if ((SlotIndex + 1) < Slots.size()) {
        Byte |= static_cast<uint8_t>(Slots[SlotIndex + 1]);
      }
      error(IO.mapInteger(Byte));
    }
  } else {
    error(IO.mapInteger(Size));
    for (uint16_t I = 0; I < Size; I += 2) {
      uint8_t Byte;
      error(IO.mapInteger(Byte));
      Record.Slots.push_back(static_cast<VFTableSlotKind>(Byte & 0xF));
      if ((I + 1) < Size)
        Record.Slots.push_back(static_cast<VFTableSlotKind>(Byte >> 4));
    }
  }

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, VFTableRecord &Record) {
  error(IO.mapInteger(Record.CompleteClass));
  error(IO.mapInteger(Record.OverriddenVFTable));
  error(IO.mapInteger(Record.VFPtrOffset));
  uint32_t NamesLen = 0;
  if (IO.isWriting()) {
    for (auto Name : Record.MethodNames)
      NamesLen += Name.size() + 1;
  }
  error(IO.mapInteger(NamesLen));
  error(IO.mapVectorTail(
      Record.MethodNames,
      [](CodeViewRecordIO &IO, StringRef &S) { return IO.mapStringZ(S); }));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, StringIdRecord &Record) {
  error(IO.mapInteger(Record.Id));
  error(IO.mapStringZ(Record.String));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          UdtSourceLineRecord &Record) {
  error(IO.mapInteger(Record.UDT));
  error(IO.mapInteger(Record.SourceFile));
  error(IO.mapInteger(Record.LineNumber));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          UdtModSourceLineRecord &Record) {
  error(IO.mapInteger(Record.UDT));
  error(IO.mapInteger(Record.SourceFile));
  error(IO.mapInteger(Record.LineNumber));
  error(IO.mapInteger(Record.Module));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, FuncIdRecord &Record) {
  error(IO.mapInteger(Record.ParentScope));
  error(IO.mapInteger(Record.FunctionType));
  error(IO.mapStringZ(Record.Name));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          MemberFuncIdRecord &Record) {
  error(IO.mapInteger(Record.ClassType));
  error(IO.mapInteger(Record.FunctionType));
  error(IO.mapStringZ(Record.Name));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          BuildInfoRecord &Record) {
  error(IO.mapVectorN<uint16_t>(
      Record.ArgIndices,
      [](CodeViewRecordIO &IO, TypeIndex &N) { return IO.mapInteger(N); }));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          MethodOverloadListRecord &Record) {
  // TODO: Split the list into multiple records if it's longer than 64KB, using
  // a subrecord of TypeRecordKind::Index to chain the records together.
  error(IO.mapVectorTail(Record.Methods, MapOneMethodRecord(true)));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          FieldListRecord &Record) {
  error(IO.mapByteVectorTail(Record.Data));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          TypeServer2Record &Record) {
  error(IO.mapGuid(Record.Guid));
  error(IO.mapInteger(Record.Age));
  error(IO.mapStringZ(Record.Name));
  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR, LabelRecord &Record) {
  error(IO.mapEnum(Record.Mode));
  return Error::success();
}

Error TypeRecordMapping::visitKnownMember(CVMemberRecord &CVR,
                                          BaseClassRecord &Record) {
  error(IO.mapInteger(Record.Attrs.Attrs));
  error(IO.mapInteger(Record.Type));
  error(IO.mapEncodedInteger(Record.Offset));

  return Error::success();
}

Error TypeRecordMapping::visitKnownMember(CVMemberRecord &CVR,
                                          EnumeratorRecord &Record) {
  error(IO.mapInteger(Record.Attrs.Attrs));

  // FIXME: Handle full APInt such as __int128.
  error(IO.mapEncodedInteger(Record.Value));
  error(IO.mapStringZ(Record.Name));

  return Error::success();
}

Error TypeRecordMapping::visitKnownMember(CVMemberRecord &CVR,
                                          DataMemberRecord &Record) {
  error(IO.mapInteger(Record.Attrs.Attrs));
  error(IO.mapInteger(Record.Type));
  error(IO.mapEncodedInteger(Record.FieldOffset));
  error(IO.mapStringZ(Record.Name));

  return Error::success();
}

Error TypeRecordMapping::visitKnownMember(CVMemberRecord &CVR,
                                          OverloadedMethodRecord &Record) {
  error(IO.mapInteger(Record.NumOverloads));
  error(IO.mapInteger(Record.MethodList));
  error(IO.mapStringZ(Record.Name));

  return Error::success();
}

Error TypeRecordMapping::visitKnownMember(CVMemberRecord &CVR,
                                          OneMethodRecord &Record) {
  const bool IsFromOverloadList = (TypeKind == LF_METHODLIST);
  MapOneMethodRecord Mapper(IsFromOverloadList);
  return Mapper(IO, Record);
}

Error TypeRecordMapping::visitKnownMember(CVMemberRecord &CVR,
                                          NestedTypeRecord &Record) {
  uint16_t Padding = 0;
  error(IO.mapInteger(Padding));
  error(IO.mapInteger(Record.Type));
  error(IO.mapStringZ(Record.Name));

  return Error::success();
}

Error TypeRecordMapping::visitKnownMember(CVMemberRecord &CVR,
                                          StaticDataMemberRecord &Record) {

  error(IO.mapInteger(Record.Attrs.Attrs));
  error(IO.mapInteger(Record.Type));
  error(IO.mapStringZ(Record.Name));

  return Error::success();
}

Error TypeRecordMapping::visitKnownMember(CVMemberRecord &CVR,
                                          VirtualBaseClassRecord &Record) {

  error(IO.mapInteger(Record.Attrs.Attrs));
  error(IO.mapInteger(Record.BaseType));
  error(IO.mapInteger(Record.VBPtrType));
  error(IO.mapEncodedInteger(Record.VBPtrOffset));
  error(IO.mapEncodedInteger(Record.VTableIndex));

  return Error::success();
}

Error TypeRecordMapping::visitKnownMember(CVMemberRecord &CVR,
                                          VFPtrRecord &Record) {
  uint16_t Padding = 0;
  error(IO.mapInteger(Padding));
  error(IO.mapInteger(Record.Type));

  return Error::success();
}

Error TypeRecordMapping::visitKnownMember(CVMemberRecord &CVR,
                                          ListContinuationRecord &Record) {
  uint16_t Padding = 0;
  error(IO.mapInteger(Padding));
  error(IO.mapInteger(Record.ContinuationIndex));

  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          PrecompRecord &Precomp) {
  error(IO.mapInteger(Precomp.StartTypeIndex));
  error(IO.mapInteger(Precomp.TypesCount));
  error(IO.mapInteger(Precomp.Signature));
  error(IO.mapStringZ(Precomp.PrecompFilePath));
  return Error::success();
}

Error TypeRecordMapping::visitKnownRecord(CVType &CVR,
                                          EndPrecompRecord &EndPrecomp) {
  error(IO.mapInteger(EndPrecomp.Signature));
  return Error::success();
}
