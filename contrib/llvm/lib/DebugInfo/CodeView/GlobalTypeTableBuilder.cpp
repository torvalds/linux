//===- GlobalTypeTableBuilder.cpp -----------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/GlobalTypeTableBuilder.h"
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

TypeIndex GlobalTypeTableBuilder::nextTypeIndex() const {
  return TypeIndex::fromArrayIndex(SeenRecords.size());
}

GlobalTypeTableBuilder::GlobalTypeTableBuilder(BumpPtrAllocator &Storage)
    : RecordStorage(Storage) {
  SeenRecords.reserve(4096);
}

GlobalTypeTableBuilder::~GlobalTypeTableBuilder() = default;

Optional<TypeIndex> GlobalTypeTableBuilder::getFirst() {
  if (empty())
    return None;

  return TypeIndex(TypeIndex::FirstNonSimpleIndex);
}

Optional<TypeIndex> GlobalTypeTableBuilder::getNext(TypeIndex Prev) {
  if (++Prev == nextTypeIndex())
    return None;
  return Prev;
}

CVType GlobalTypeTableBuilder::getType(TypeIndex Index) {
  CVType Type;
  Type.RecordData = SeenRecords[Index.toArrayIndex()];
  if (!Type.RecordData.empty()) {
    assert(Type.RecordData.size() >= sizeof(RecordPrefix));
    const RecordPrefix *P =
        reinterpret_cast<const RecordPrefix *>(Type.RecordData.data());
    Type.Type = static_cast<TypeLeafKind>(uint16_t(P->RecordKind));
  }
  return Type;
}

StringRef GlobalTypeTableBuilder::getTypeName(TypeIndex Index) {
  llvm_unreachable("Method not implemented");
}

bool GlobalTypeTableBuilder::contains(TypeIndex Index) {
  if (Index.isSimple() || Index.isNoneType())
    return false;

  return Index.toArrayIndex() < SeenRecords.size();
}

uint32_t GlobalTypeTableBuilder::size() { return SeenRecords.size(); }

uint32_t GlobalTypeTableBuilder::capacity() { return SeenRecords.size(); }

ArrayRef<ArrayRef<uint8_t>> GlobalTypeTableBuilder::records() const {
  return SeenRecords;
}

ArrayRef<GloballyHashedType> GlobalTypeTableBuilder::hashes() const {
  return SeenHashes;
}

void GlobalTypeTableBuilder::reset() {
  HashedRecords.clear();
  SeenRecords.clear();
}

TypeIndex GlobalTypeTableBuilder::insertRecordBytes(ArrayRef<uint8_t> Record) {
  GloballyHashedType GHT =
      GloballyHashedType::hashType(Record, SeenHashes, SeenHashes);
  return insertRecordAs(GHT, Record.size(),
                        [Record](MutableArrayRef<uint8_t> Data) {
                          assert(Data.size() == Record.size());
                          ::memcpy(Data.data(), Record.data(), Record.size());
                          return Data;
                        });
}

TypeIndex
GlobalTypeTableBuilder::insertRecord(ContinuationRecordBuilder &Builder) {
  TypeIndex TI;
  auto Fragments = Builder.end(nextTypeIndex());
  assert(!Fragments.empty());
  for (auto C : Fragments)
    TI = insertRecordBytes(C.RecordData);
  return TI;
}
