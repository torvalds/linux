//===- AppendingTypeTableBuilder.h -------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_APPENDINGTYPETABLEBUILDER_H
#define LLVM_DEBUGINFO_CODEVIEW_APPENDINGTYPETABLEBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/SimpleTypeSerializer.h"
#include "llvm/DebugInfo/CodeView/TypeCollection.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/Support/Allocator.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

namespace llvm {
namespace codeview {

class ContinuationRecordBuilder;

class AppendingTypeTableBuilder : public TypeCollection {

  BumpPtrAllocator &RecordStorage;
  SimpleTypeSerializer SimpleSerializer;

  /// Contains a list of all records indexed by TypeIndex.toArrayIndex().
  SmallVector<ArrayRef<uint8_t>, 2> SeenRecords;

public:
  explicit AppendingTypeTableBuilder(BumpPtrAllocator &Storage);
  ~AppendingTypeTableBuilder();

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
  TypeIndex insertRecordBytes(ArrayRef<uint8_t> &Record);
  TypeIndex insertRecord(ContinuationRecordBuilder &Builder);

  template <typename T> TypeIndex writeLeafType(T &Record) {
    ArrayRef<uint8_t> Data = SimpleSerializer.serialize(Record);
    return insertRecordBytes(Data);
  }
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_TYPETABLEBUILDER_H
