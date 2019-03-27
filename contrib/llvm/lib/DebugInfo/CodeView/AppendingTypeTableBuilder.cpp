//===- AppendingTypeTableBuilder.cpp --------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/AppendingTypeTableBuilder.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/ContinuationRecordBuilder.h"
#include "llvm/DebugInfo/CodeView/RecordSerialization.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>

using namespace llvm;
using namespace llvm::codeview;

TypeIndex AppendingTypeTableBuilder::nextTypeIndex() const {
  return TypeIndex::fromArrayIndex(SeenRecords.size());
}

AppendingTypeTableBuilder::AppendingTypeTableBuilder(BumpPtrAllocator &Storage)
    : RecordStorage(Storage) {}

AppendingTypeTableBuilder::~AppendingTypeTableBuilder() = default;

Optional<TypeIndex> AppendingTypeTableBuilder::getFirst() {
  if (empty())
    return None;

  return TypeIndex(TypeIndex::FirstNonSimpleIndex);
}

Optional<TypeIndex> AppendingTypeTableBuilder::getNext(TypeIndex Prev) {
  if (++Prev == nextTypeIndex())
    return None;
  return Prev;
}

CVType AppendingTypeTableBuilder::getType(TypeIndex Index) {
  CVType Type;
  Type.RecordData = SeenRecords[Index.toArrayIndex()];
  const RecordPrefix *P =
      reinterpret_cast<const RecordPrefix *>(Type.RecordData.data());
  Type.Type = static_cast<TypeLeafKind>(uint16_t(P->RecordKind));
  return Type;
}

StringRef AppendingTypeTableBuilder::getTypeName(TypeIndex Index) {
  llvm_unreachable("Method not implemented");
}

bool AppendingTypeTableBuilder::contains(TypeIndex Index) {
  if (Index.isSimple() || Index.isNoneType())
    return false;

  return Index.toArrayIndex() < SeenRecords.size();
}

uint32_t AppendingTypeTableBuilder::size() { return SeenRecords.size(); }

uint32_t AppendingTypeTableBuilder::capacity() { return SeenRecords.size(); }

ArrayRef<ArrayRef<uint8_t>> AppendingTypeTableBuilder::records() const {
  return SeenRecords;
}

void AppendingTypeTableBuilder::reset() { SeenRecords.clear(); }

TypeIndex
AppendingTypeTableBuilder::insertRecordBytes(ArrayRef<uint8_t> &Record) {
  TypeIndex NewTI = nextTypeIndex();
  uint8_t *Stable = RecordStorage.Allocate<uint8_t>(Record.size());
  memcpy(Stable, Record.data(), Record.size());
  Record = ArrayRef<uint8_t>(Stable, Record.size());
  SeenRecords.push_back(Record);
  return NewTI;
}

TypeIndex
AppendingTypeTableBuilder::insertRecord(ContinuationRecordBuilder &Builder) {
  TypeIndex TI;
  auto Fragments = Builder.end(nextTypeIndex());
  assert(!Fragments.empty());
  for (auto C : Fragments)
    TI = insertRecordBytes(C.RecordData);
  return TI;
}
