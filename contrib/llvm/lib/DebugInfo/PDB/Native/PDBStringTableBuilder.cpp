//===- PDBStringTableBuilder.cpp - PDB String Table -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/PDBStringTableBuilder.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/DebugInfo/PDB/Native/Hash.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Endian.h"

#include <map>

using namespace llvm;
using namespace llvm::msf;
using namespace llvm::support;
using namespace llvm::support::endian;
using namespace llvm::pdb;

StringTableHashTraits::StringTableHashTraits(PDBStringTableBuilder &Table)
    : Table(&Table) {}

uint32_t StringTableHashTraits::hashLookupKey(StringRef S) const {
  return Table->getIdForString(S);
}

StringRef StringTableHashTraits::storageKeyToLookupKey(uint32_t Offset) const {
  return Table->getStringForId(Offset);
}

uint32_t StringTableHashTraits::lookupKeyToStorageKey(StringRef S) {
  return Table->insert(S);
}

uint32_t PDBStringTableBuilder::insert(StringRef S) {
  return Strings.insert(S);
}

uint32_t PDBStringTableBuilder::getIdForString(StringRef S) const {
  return Strings.getIdForString(S);
}

StringRef PDBStringTableBuilder::getStringForId(uint32_t Id) const {
  return Strings.getStringForId(Id);
}

// This is a precomputed list of Buckets given the specified number of
// strings.  Matching the reference algorithm exactly is not strictly
// necessary for correctness, but it helps when comparing LLD's PDBs with
// Microsoft's PDBs so as to eliminate superfluous differences.
static std::map<uint32_t, uint32_t> StringsToBuckets = {
    {1, 2},
    {2, 4},
    {4, 7},
    {6, 11},
    {9, 17},
    {13, 26},
    {20, 40},
    {31, 61},
    {46, 92},
    {70, 139},
    {105, 209},
    {157, 314},
    {236, 472},
    {355, 709},
    {532, 1064},
    {799, 1597},
    {1198, 2396},
    {1798, 3595},
    {2697, 5393},
    {4045, 8090},
    {6068, 12136},
    {9103, 18205},
    {13654, 27308},
    {20482, 40963},
    {30723, 61445},
    {46084, 92168},
    {69127, 138253},
    {103690, 207380},
    {155536, 311071},
    {233304, 466607},
    {349956, 699911},
    {524934, 1049867},
    {787401, 1574801},
    {1181101, 2362202},
    {1771652, 3543304},
    {2657479, 5314957},
    {3986218, 7972436},
    {5979328, 11958655},
    {8968992, 17937983},
    {13453488, 26906975},
    {20180232, 40360463},
    {30270348, 60540695},
    {45405522, 90811043},
    {68108283, 136216565},
    {102162424, 204324848},
    {153243637, 306487273},
    {229865455, 459730910},
    {344798183, 689596366},
    {517197275, 1034394550},
    {775795913, 1551591826}};

static uint32_t computeBucketCount(uint32_t NumStrings) {
  auto Entry = StringsToBuckets.lower_bound(NumStrings);
  assert(Entry != StringsToBuckets.end());
  return Entry->second;
}

uint32_t PDBStringTableBuilder::calculateHashTableSize() const {
  uint32_t Size = sizeof(uint32_t); // Hash table begins with 4-byte size field.
  Size += sizeof(uint32_t) * computeBucketCount(Strings.size());

  return Size;
}

uint32_t PDBStringTableBuilder::calculateSerializedSize() const {
  uint32_t Size = 0;
  Size += sizeof(PDBStringTableHeader);
  Size += Strings.calculateSerializedSize();
  Size += calculateHashTableSize();
  Size += sizeof(uint32_t); // The /names stream ends with the string count.
  return Size;
}

void PDBStringTableBuilder::setStrings(
    const codeview::DebugStringTableSubsection &Strings) {
  this->Strings = Strings;
}

Error PDBStringTableBuilder::writeHeader(BinaryStreamWriter &Writer) const {
  // Write a header
  PDBStringTableHeader H;
  H.Signature = PDBStringTableSignature;
  H.HashVersion = 1;
  H.ByteSize = Strings.calculateSerializedSize();
  if (auto EC = Writer.writeObject(H))
    return EC;
  assert(Writer.bytesRemaining() == 0);
  return Error::success();
}

Error PDBStringTableBuilder::writeStrings(BinaryStreamWriter &Writer) const {
  if (auto EC = Strings.commit(Writer))
    return EC;

  assert(Writer.bytesRemaining() == 0);
  return Error::success();
}

Error PDBStringTableBuilder::writeHashTable(BinaryStreamWriter &Writer) const {
  // Write a hash table.
  uint32_t BucketCount = computeBucketCount(Strings.size());
  if (auto EC = Writer.writeInteger(BucketCount))
    return EC;
  std::vector<ulittle32_t> Buckets(BucketCount);

  for (auto &Pair : Strings) {
    StringRef S = Pair.getKey();
    uint32_t Offset = Pair.getValue();
    uint32_t Hash = hashStringV1(S);

    for (uint32_t I = 0; I != BucketCount; ++I) {
      uint32_t Slot = (Hash + I) % BucketCount;
      if (Buckets[Slot] != 0)
        continue;
      Buckets[Slot] = Offset;
      break;
    }
  }

  if (auto EC = Writer.writeArray(ArrayRef<ulittle32_t>(Buckets)))
    return EC;

  assert(Writer.bytesRemaining() == 0);
  return Error::success();
}

Error PDBStringTableBuilder::writeEpilogue(BinaryStreamWriter &Writer) const {
  if (auto EC = Writer.writeInteger<uint32_t>(Strings.size()))
    return EC;
  assert(Writer.bytesRemaining() == 0);
  return Error::success();
}

Error PDBStringTableBuilder::commit(BinaryStreamWriter &Writer) const {
  BinaryStreamWriter SectionWriter;

  std::tie(SectionWriter, Writer) = Writer.split(sizeof(PDBStringTableHeader));
  if (auto EC = writeHeader(SectionWriter))
    return EC;

  std::tie(SectionWriter, Writer) =
      Writer.split(Strings.calculateSerializedSize());
  if (auto EC = writeStrings(SectionWriter))
    return EC;

  std::tie(SectionWriter, Writer) = Writer.split(calculateHashTableSize());
  if (auto EC = writeHashTable(SectionWriter))
    return EC;

  std::tie(SectionWriter, Writer) = Writer.split(sizeof(uint32_t));
  if (auto EC = writeEpilogue(SectionWriter))
    return EC;

  return Error::success();
}
