//===- GlobalTypeTableBuilder.h ----------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_GLOBALTYPETABLEBUILDER_H
#define LLVM_DEBUGINFO_CODEVIEW_GLOBALTYPETABLEBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/SimpleTypeSerializer.h"
#include "llvm/DebugInfo/CodeView/TypeCollection.h"
#include "llvm/DebugInfo/CodeView/TypeHashing.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/Support/Allocator.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

namespace llvm {
namespace codeview {

class ContinuationRecordBuilder;

class GlobalTypeTableBuilder : public TypeCollection {
  /// Storage for records.  These need to outlive the TypeTableBuilder.
  BumpPtrAllocator &RecordStorage;

  /// A serializer that can write non-continuation leaf types.  Only used as
  /// a convenience function so that we can provide an interface method to
  /// write an unserialized record.
  SimpleTypeSerializer SimpleSerializer;

  /// Hash table.
  DenseMap<GloballyHashedType, TypeIndex> HashedRecords;

  /// Contains a list of all records indexed by TypeIndex.toArrayIndex().
  SmallVector<ArrayRef<uint8_t>, 2> SeenRecords;

  /// Contains a list of all hash values inexed by TypeIndex.toArrayIndex().
  SmallVector<GloballyHashedType, 2> SeenHashes;

public:
  explicit GlobalTypeTableBuilder(BumpPtrAllocator &Storage);
  ~GlobalTypeTableBuilder();

  // TypeTableCollection overrides
  Optional<TypeIndex> getFirst() override;
  Optional<TypeIndex> getNext(TypeIndex Prev) override;
  CVType getType(TypeIndex Index) override;
  StringRef getTypeName(TypeIndex Index) override;
  bool contains(TypeIndex Index) override;
  uint32_t size() override;
  uint32_t capacity() override;

  // public interface
  void reset();
  TypeIndex nextTypeIndex() const;

  BumpPtrAllocator &getAllocator() { return RecordStorage; }

  ArrayRef<ArrayRef<uint8_t>> records() const;
  ArrayRef<GloballyHashedType> hashes() const;

  template <typename CreateFunc>
  TypeIndex insertRecordAs(GloballyHashedType Hash, size_t RecordSize,
                           CreateFunc Create) {
    auto Result = HashedRecords.try_emplace(Hash, nextTypeIndex());

    if (LLVM_UNLIKELY(Result.second)) {
      uint8_t *Stable = RecordStorage.Allocate<uint8_t>(RecordSize);
      MutableArrayRef<uint8_t> Data(Stable, RecordSize);
      SeenRecords.push_back(Create(Data));
      SeenHashes.push_back(Hash);
    }

    // Update the caller's copy of Record to point a stable copy.
    return Result.first->second;
  }

  TypeIndex insertRecordBytes(ArrayRef<uint8_t> Data);
  TypeIndex insertRecord(ContinuationRecordBuilder &Builder);

  template <typename T> TypeIndex writeLeafType(T &Record) {
    ArrayRef<uint8_t> Data = SimpleSerializer.serialize(Record);
    return insertRecordBytes(Data);
  }
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_MERGINGTYPETABLEBUILDER_H
