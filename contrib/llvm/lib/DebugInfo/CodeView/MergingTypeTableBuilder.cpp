//===- MergingTypeTableBuilder.cpp ----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/MergingTypeTableBuilder.h"
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

TypeIndex MergingTypeTableBuilder::nextTypeIndex() const {
  return TypeIndex::fromArrayIndex(SeenRecords.size());
}

MergingTypeTableBuilder::MergingTypeTableBuilder(BumpPtrAllocator &Storage)
    : RecordStorage(Storage) {
  SeenRecords.reserve(4096);
}

MergingTypeTableBuilder::~MergingTypeTableBuilder() = default;

Optional<TypeIndex> MergingTypeTableBuilder::getFirst() {
  if (empty())
    return None;

  return TypeIndex(TypeIndex::FirstNonSimpleIndex);
}

Optional<TypeIndex> MergingTypeTableBuilder::getNext(TypeIndex Prev) {
  if (++Prev == nextTypeIndex())
    return None;
  return Prev;
}

CVType MergingTypeTableBuilder::getType(TypeIndex Index) {
  CVType Type;
  Type.RecordData = SeenRecords[Index.toArrayIndex()];
  const RecordPrefix *P =
      reinterpret_cast<const RecordPrefix *>(Type.RecordData.data());
  Type.Type = static_cast<TypeLeafKind>(uint16_t(P->RecordKind));
  return Type;
}

StringRef MergingTypeTableBuilder::getTypeName(TypeIndex Index) {
  llvm_unreachable("Method not implemented");
}

bool MergingTypeTableBuilder::contains(TypeIndex Index) {
  if (Index.isSimple() || Index.isNoneType())
    return false;

  return Index.toArrayIndex() < SeenRecords.size();
}

uint32_t MergingTypeTableBuilder::size() { return SeenRecords.size(); }

uint32_t MergingTypeTableBuilder::capacity() { return SeenRecords.size(); }

ArrayRef<ArrayRef<uint8_t>> MergingTypeTableBuilder::records() const {
  return SeenRecords;
}

void MergingTypeTableBuilder::reset() {
  HashedRecords.clear();
  SeenRecords.clear();
}

static inline ArrayRef<uint8_t> stabilize(BumpPtrAllocator &Alloc,
                                          ArrayRef<uint8_t> Data) {
  uint8_t *Stable = Alloc.Allocate<uint8_t>(Data.size());
  memcpy(Stable, Data.data(), Data.size());
  return makeArrayRef(Stable, Data.size());
}

TypeIndex MergingTypeTableBuilder::insertRecordAs(hash_code Hash,
                                                  ArrayRef<uint8_t> &Record) {
  assert(Record.size() < UINT32_MAX && "Record too big");
  assert(Record.size() % 4 == 0 && "Record is not aligned to 4 bytes!");

  LocallyHashedType WeakHash{Hash, Record};
  auto Result = HashedRecords.try_emplace(WeakHash, nextTypeIndex());

  if (Result.second) {
    ArrayRef<uint8_t> RecordData = stabilize(RecordStorage, Record);
    Result.first->first.RecordData = RecordData;
    SeenRecords.push_back(RecordData);
  }

  // Update the caller's copy of Record to point a stable copy.
  TypeIndex ActualTI = Result.first->second;
  Record = SeenRecords[ActualTI.toArrayIndex()];
  return ActualTI;
}

TypeIndex
MergingTypeTableBuilder::insertRecordBytes(ArrayRef<uint8_t> &Record) {
  return insertRecordAs(hash_value(Record), Record);
}

TypeIndex
MergingTypeTableBuilder::insertRecord(ContinuationRecordBuilder &Builder) {
  TypeIndex TI;
  auto Fragments = Builder.end(nextTypeIndex());
  assert(!Fragments.empty());
  for (auto C : Fragments)
    TI = insertRecordBytes(C.RecordData);
  return TI;
}
