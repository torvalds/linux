//===- TypeHashing.cpp -------------------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/TypeHashing.h"

#include "llvm/DebugInfo/CodeView/TypeIndexDiscovery.h"
#include "llvm/Support/SHA1.h"

using namespace llvm;
using namespace llvm::codeview;

LocallyHashedType DenseMapInfo<LocallyHashedType>::Empty{0, {}};
LocallyHashedType DenseMapInfo<LocallyHashedType>::Tombstone{hash_code(-1), {}};

static std::array<uint8_t, 8> EmptyHash = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
static std::array<uint8_t, 8> TombstoneHash = {
    {0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

GloballyHashedType DenseMapInfo<GloballyHashedType>::Empty{EmptyHash};
GloballyHashedType DenseMapInfo<GloballyHashedType>::Tombstone{TombstoneHash};

LocallyHashedType LocallyHashedType::hashType(ArrayRef<uint8_t> RecordData) {
  return {llvm::hash_value(RecordData), RecordData};
}

GloballyHashedType
GloballyHashedType::hashType(ArrayRef<uint8_t> RecordData,
                             ArrayRef<GloballyHashedType> PreviousTypes,
                             ArrayRef<GloballyHashedType> PreviousIds) {
  SmallVector<TiReference, 4> Refs;
  discoverTypeIndices(RecordData, Refs);
  SHA1 S;
  S.init();
  uint32_t Off = 0;
  S.update(RecordData.take_front(sizeof(RecordPrefix)));
  RecordData = RecordData.drop_front(sizeof(RecordPrefix));
  for (const auto &Ref : Refs) {
    // Hash any data that comes before this TiRef.
    uint32_t PreLen = Ref.Offset - Off;
    ArrayRef<uint8_t> PreData = RecordData.slice(Off, PreLen);
    S.update(PreData);
    auto Prev = (Ref.Kind == TiRefKind::IndexRef) ? PreviousIds : PreviousTypes;

    auto RefData = RecordData.slice(Ref.Offset, Ref.Count * sizeof(TypeIndex));
    // For each type index referenced, add in the previously computed hash
    // value of that type.
    ArrayRef<TypeIndex> Indices(
        reinterpret_cast<const TypeIndex *>(RefData.data()), Ref.Count);
    for (TypeIndex TI : Indices) {
      ArrayRef<uint8_t> BytesToHash;
      if (TI.isSimple() || TI.isNoneType() || TI.toArrayIndex() >= Prev.size()) {
        const uint8_t *IndexBytes = reinterpret_cast<const uint8_t *>(&TI);
        BytesToHash = makeArrayRef(IndexBytes, sizeof(TypeIndex));
      } else {
        BytesToHash = Prev[TI.toArrayIndex()].Hash;
      }
      S.update(BytesToHash);
    }

    Off = Ref.Offset + Ref.Count * sizeof(TypeIndex);
  }

  // Don't forget to add in any trailing bytes.
  auto TrailingBytes = RecordData.drop_front(Off);
  S.update(TrailingBytes);

  return {S.final().take_back(8)};
}
