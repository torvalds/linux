//===- TypeTableCollection.cpp -------------------------------- *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/TypeTableCollection.h"

#include "llvm/DebugInfo/CodeView/CVTypeVisitor.h"
#include "llvm/DebugInfo/CodeView/RecordName.h"
#include "llvm/Support/BinaryStreamReader.h"

using namespace llvm;
using namespace llvm::codeview;

TypeTableCollection::TypeTableCollection(ArrayRef<ArrayRef<uint8_t>> Records)
    : NameStorage(Allocator), Records(Records) {
  Names.resize(Records.size());
}

Optional<TypeIndex> TypeTableCollection::getFirst() {
  if (empty())
    return None;
  return TypeIndex::fromArrayIndex(0);
}

Optional<TypeIndex> TypeTableCollection::getNext(TypeIndex Prev) {
  assert(contains(Prev));
  ++Prev;
  if (Prev.toArrayIndex() == size())
    return None;
  return Prev;
}

CVType TypeTableCollection::getType(TypeIndex Index) {
  assert(Index.toArrayIndex() < Records.size());
  ArrayRef<uint8_t> Bytes = Records[Index.toArrayIndex()];
  const RecordPrefix *Prefix =
      reinterpret_cast<const RecordPrefix *>(Bytes.data());
  TypeLeafKind Kind = static_cast<TypeLeafKind>(uint16_t(Prefix->RecordKind));
  return CVType(Kind, Bytes);
}

StringRef TypeTableCollection::getTypeName(TypeIndex Index) {
  if (Index.isNoneType() || Index.isSimple())
    return TypeIndex::simpleTypeName(Index);

  uint32_t I = Index.toArrayIndex();
  if (Names[I].data() == nullptr) {
    StringRef Result = NameStorage.save(computeTypeName(*this, Index));
    Names[I] = Result;
  }
  return Names[I];
}

bool TypeTableCollection::contains(TypeIndex Index) {
  return Index.toArrayIndex() <= size();
}

uint32_t TypeTableCollection::size() { return Records.size(); }

uint32_t TypeTableCollection::capacity() { return Records.size(); }
