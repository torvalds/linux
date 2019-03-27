//===- TpiStreamBuilder.cpp -   -------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/TpiStreamBuilder.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/MSF/MSFBuilder.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstdint>

using namespace llvm;
using namespace llvm::msf;
using namespace llvm::pdb;
using namespace llvm::support;

TpiStreamBuilder::TpiStreamBuilder(MSFBuilder &Msf, uint32_t StreamIdx)
    : Msf(Msf), Allocator(Msf.getAllocator()), Header(nullptr), Idx(StreamIdx) {
}

TpiStreamBuilder::~TpiStreamBuilder() = default;

void TpiStreamBuilder::setVersionHeader(PdbRaw_TpiVer Version) {
  VerHeader = Version;
}

void TpiStreamBuilder::addTypeRecord(ArrayRef<uint8_t> Record,
                                     Optional<uint32_t> Hash) {
  // If we just crossed an 8KB threshold, add a type index offset.
  size_t NewSize = TypeRecordBytes + Record.size();
  constexpr size_t EightKB = 8 * 1024;
  if (NewSize / EightKB > TypeRecordBytes / EightKB || TypeRecords.empty()) {
    TypeIndexOffsets.push_back(
        {codeview::TypeIndex(codeview::TypeIndex::FirstNonSimpleIndex +
                             TypeRecords.size()),
         ulittle32_t(TypeRecordBytes)});
  }
  TypeRecordBytes = NewSize;

  TypeRecords.push_back(Record);
  if (Hash)
    TypeHashes.push_back(*Hash);
}

Error TpiStreamBuilder::finalize() {
  if (Header)
    return Error::success();

  TpiStreamHeader *H = Allocator.Allocate<TpiStreamHeader>();

  uint32_t Count = TypeRecords.size();

  H->Version = VerHeader;
  H->HeaderSize = sizeof(TpiStreamHeader);
  H->TypeIndexBegin = codeview::TypeIndex::FirstNonSimpleIndex;
  H->TypeIndexEnd = H->TypeIndexBegin + Count;
  H->TypeRecordBytes = TypeRecordBytes;

  H->HashStreamIndex = HashStreamIndex;
  H->HashAuxStreamIndex = kInvalidStreamIndex;
  H->HashKeySize = sizeof(ulittle32_t);
  H->NumHashBuckets = MinTpiHashBuckets;

  // Recall that hash values go into a completely different stream identified by
  // the `HashStreamIndex` field of the `TpiStreamHeader`.  Therefore, the data
  // begins at offset 0 of this independent stream.
  H->HashValueBuffer.Off = 0;
  H->HashValueBuffer.Length = calculateHashBufferSize();

  // We never write any adjustments into our PDBs, so this is usually some
  // offset with zero length.
  H->HashAdjBuffer.Off = H->HashValueBuffer.Off + H->HashValueBuffer.Length;
  H->HashAdjBuffer.Length = 0;

  H->IndexOffsetBuffer.Off = H->HashAdjBuffer.Off + H->HashAdjBuffer.Length;
  H->IndexOffsetBuffer.Length = calculateIndexOffsetSize();

  Header = H;
  return Error::success();
}

uint32_t TpiStreamBuilder::calculateSerializedLength() {
  return sizeof(TpiStreamHeader) + TypeRecordBytes;
}

uint32_t TpiStreamBuilder::calculateHashBufferSize() const {
  assert((TypeRecords.size() == TypeHashes.size() || TypeHashes.empty()) &&
         "either all or no type records should have hashes");
  return TypeHashes.size() * sizeof(ulittle32_t);
}

uint32_t TpiStreamBuilder::calculateIndexOffsetSize() const {
  return TypeIndexOffsets.size() * sizeof(codeview::TypeIndexOffset);
}

Error TpiStreamBuilder::finalizeMsfLayout() {
  uint32_t Length = calculateSerializedLength();
  if (auto EC = Msf.setStreamSize(Idx, Length))
    return EC;

  uint32_t HashStreamSize =
      calculateHashBufferSize() + calculateIndexOffsetSize();

  if (HashStreamSize == 0)
    return Error::success();

  auto ExpectedIndex = Msf.addStream(HashStreamSize);
  if (!ExpectedIndex)
    return ExpectedIndex.takeError();
  HashStreamIndex = *ExpectedIndex;
  if (!TypeHashes.empty()) {
    ulittle32_t *H = Allocator.Allocate<ulittle32_t>(TypeHashes.size());
    MutableArrayRef<ulittle32_t> HashBuffer(H, TypeHashes.size());
    for (uint32_t I = 0; I < TypeHashes.size(); ++I) {
      HashBuffer[I] = TypeHashes[I] % MinTpiHashBuckets;
    }
    ArrayRef<uint8_t> Bytes(
        reinterpret_cast<const uint8_t *>(HashBuffer.data()),
        calculateHashBufferSize());
    HashValueStream =
        llvm::make_unique<BinaryByteStream>(Bytes, llvm::support::little);
  }
  return Error::success();
}

Error TpiStreamBuilder::commit(const msf::MSFLayout &Layout,
                               WritableBinaryStreamRef Buffer) {
  if (auto EC = finalize())
    return EC;

  auto InfoS = WritableMappedBlockStream::createIndexedStream(Layout, Buffer,
                                                              Idx, Allocator);

  BinaryStreamWriter Writer(*InfoS);
  if (auto EC = Writer.writeObject(*Header))
    return EC;

  for (auto Rec : TypeRecords)
    if (auto EC = Writer.writeBytes(Rec))
      return EC;

  if (HashStreamIndex != kInvalidStreamIndex) {
    auto HVS = WritableMappedBlockStream::createIndexedStream(
        Layout, Buffer, HashStreamIndex, Allocator);
    BinaryStreamWriter HW(*HVS);
    if (HashValueStream) {
      if (auto EC = HW.writeStreamRef(*HashValueStream))
        return EC;
    }

    for (auto &IndexOffset : TypeIndexOffsets) {
      if (auto EC = HW.writeObject(IndexOffset))
        return EC;
    }
  }

  return Error::success();
}
