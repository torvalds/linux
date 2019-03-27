//===- DbiStreamBuilder.cpp - PDB Dbi Stream Creation -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/GSIStreamBuilder.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/DebugInfo/CodeView/RecordName.h"
#include "llvm/DebugInfo/CodeView/SymbolDeserializer.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/SymbolSerializer.h"
#include "llvm/DebugInfo/MSF/MSFBuilder.h"
#include "llvm/DebugInfo/MSF/MSFCommon.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/GlobalsStream.h"
#include "llvm/DebugInfo/PDB/Native/Hash.h"
#include "llvm/Support/BinaryItemStream.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/xxhash.h"
#include <algorithm>
#include <vector>

using namespace llvm;
using namespace llvm::msf;
using namespace llvm::pdb;
using namespace llvm::codeview;

struct llvm::pdb::GSIHashStreamBuilder {
  struct UdtDenseMapInfo {
    static inline CVSymbol getEmptyKey() {
      static CVSymbol Empty;
      return Empty;
    }
    static inline CVSymbol getTombstoneKey() {
      static CVSymbol Tombstone(static_cast<SymbolKind>(-1),
                                ArrayRef<uint8_t>());
      return Tombstone;
    }
    static unsigned getHashValue(const CVSymbol &Val) {
      return xxHash64(Val.RecordData);
    }
    static bool isEqual(const CVSymbol &LHS, const CVSymbol &RHS) {
      return LHS.RecordData == RHS.RecordData;
    }
  };

  std::vector<CVSymbol> Records;
  uint32_t StreamIndex;
  llvm::DenseSet<CVSymbol, UdtDenseMapInfo> UdtHashes;
  std::vector<PSHashRecord> HashRecords;
  std::array<support::ulittle32_t, (IPHR_HASH + 32) / 32> HashBitmap;
  std::vector<support::ulittle32_t> HashBuckets;

  uint32_t calculateSerializedLength() const;
  uint32_t calculateRecordByteSize() const;
  Error commit(BinaryStreamWriter &Writer);
  void finalizeBuckets(uint32_t RecordZeroOffset);

  template <typename T> void addSymbol(const T &Symbol, MSFBuilder &Msf) {
    T Copy(Symbol);
    addSymbol(SymbolSerializer::writeOneSymbol(Copy, Msf.getAllocator(),
                                               CodeViewContainer::Pdb));
  }
  void addSymbol(const CVSymbol &Symbol) {
    if (Symbol.kind() == S_UDT) {
      auto Iter = UdtHashes.insert(Symbol);
      if (!Iter.second)
        return;
    }

    Records.push_back(Symbol);
  }
};

uint32_t GSIHashStreamBuilder::calculateSerializedLength() const {
  uint32_t Size = sizeof(GSIHashHeader);
  Size += HashRecords.size() * sizeof(PSHashRecord);
  Size += HashBitmap.size() * sizeof(uint32_t);
  Size += HashBuckets.size() * sizeof(uint32_t);
  return Size;
}

uint32_t GSIHashStreamBuilder::calculateRecordByteSize() const {
  uint32_t Size = 0;
  for (const auto &Sym : Records)
    Size += Sym.length();
  return Size;
}

Error GSIHashStreamBuilder::commit(BinaryStreamWriter &Writer) {
  GSIHashHeader Header;
  Header.VerSignature = GSIHashHeader::HdrSignature;
  Header.VerHdr = GSIHashHeader::HdrVersion;
  Header.HrSize = HashRecords.size() * sizeof(PSHashRecord);
  Header.NumBuckets = HashBitmap.size() * 4 + HashBuckets.size() * 4;

  if (auto EC = Writer.writeObject(Header))
    return EC;

  if (auto EC = Writer.writeArray(makeArrayRef(HashRecords)))
    return EC;
  if (auto EC = Writer.writeArray(makeArrayRef(HashBitmap)))
    return EC;
  if (auto EC = Writer.writeArray(makeArrayRef(HashBuckets)))
    return EC;
  return Error::success();
}

static bool isAsciiString(StringRef S) {
  return llvm::all_of(S, [](char C) { return unsigned(C) < 0x80; });
}

// See `caseInsensitiveComparePchPchCchCch` in gsi.cpp
static bool gsiRecordLess(StringRef S1, StringRef S2) {
  size_t LS = S1.size();
  size_t RS = S2.size();
  // Shorter strings always compare less than longer strings.
  if (LS != RS)
    return LS < RS;

  // If either string contains non ascii characters, memcmp them.
  if (LLVM_UNLIKELY(!isAsciiString(S1) || !isAsciiString(S2)))
    return memcmp(S1.data(), S2.data(), LS) < 0;

  // Both strings are ascii, perform a case-insenstive comparison.
  return S1.compare_lower(S2.data()) < 0;
}

void GSIHashStreamBuilder::finalizeBuckets(uint32_t RecordZeroOffset) {
  std::array<std::vector<std::pair<StringRef, PSHashRecord>>, IPHR_HASH + 1>
      TmpBuckets;
  uint32_t SymOffset = RecordZeroOffset;
  for (const CVSymbol &Sym : Records) {
    PSHashRecord HR;
    // Add one when writing symbol offsets to disk. See GSI1::fixSymRecs.
    HR.Off = SymOffset + 1;
    HR.CRef = 1; // Always use a refcount of 1.

    // Hash the name to figure out which bucket this goes into.
    StringRef Name = getSymbolName(Sym);
    size_t BucketIdx = hashStringV1(Name) % IPHR_HASH;
    TmpBuckets[BucketIdx].push_back(std::make_pair(Name, HR));
    SymOffset += Sym.length();
  }

  // Compute the three tables: the hash records in bucket and chain order, the
  // bucket presence bitmap, and the bucket chain start offsets.
  HashRecords.reserve(Records.size());
  for (ulittle32_t &Word : HashBitmap)
    Word = 0;
  for (size_t BucketIdx = 0; BucketIdx < IPHR_HASH + 1; ++BucketIdx) {
    auto &Bucket = TmpBuckets[BucketIdx];
    if (Bucket.empty())
      continue;
    HashBitmap[BucketIdx / 32] |= 1U << (BucketIdx % 32);

    // Calculate what the offset of the first hash record in the chain would
    // be if it were inflated to contain 32-bit pointers. On a 32-bit system,
    // each record would be 12 bytes. See HROffsetCalc in gsi.h.
    const int SizeOfHROffsetCalc = 12;
    ulittle32_t ChainStartOff =
        ulittle32_t(HashRecords.size() * SizeOfHROffsetCalc);
    HashBuckets.push_back(ChainStartOff);

    // Sort each bucket by memcmp of the symbol's name.  It's important that
    // we use the same sorting algorithm as is used by the reference
    // implementation to ensure that the search for a record within a bucket
    // can properly early-out when it detects the record won't be found.  The
    // algorithm used here corredsponds to the function
    // caseInsensitiveComparePchPchCchCch in the reference implementation.
    llvm::sort(Bucket, [](const std::pair<StringRef, PSHashRecord> &Left,
                          const std::pair<StringRef, PSHashRecord> &Right) {
      return gsiRecordLess(Left.first, Right.first);
    });

    for (const auto &Entry : Bucket)
      HashRecords.push_back(Entry.second);
  }
}

GSIStreamBuilder::GSIStreamBuilder(msf::MSFBuilder &Msf)
    : Msf(Msf), PSH(llvm::make_unique<GSIHashStreamBuilder>()),
      GSH(llvm::make_unique<GSIHashStreamBuilder>()) {}

GSIStreamBuilder::~GSIStreamBuilder() {}

uint32_t GSIStreamBuilder::calculatePublicsHashStreamSize() const {
  uint32_t Size = 0;
  Size += sizeof(PublicsStreamHeader);
  Size += PSH->calculateSerializedLength();
  Size += PSH->Records.size() * sizeof(uint32_t); // AddrMap
  // FIXME: Add thunk map and section offsets for incremental linking.

  return Size;
}

uint32_t GSIStreamBuilder::calculateGlobalsHashStreamSize() const {
  return GSH->calculateSerializedLength();
}

Error GSIStreamBuilder::finalizeMsfLayout() {
  // First we write public symbol records, then we write global symbol records.
  uint32_t PSHZero = 0;
  uint32_t GSHZero = PSH->calculateRecordByteSize();

  PSH->finalizeBuckets(PSHZero);
  GSH->finalizeBuckets(GSHZero);

  Expected<uint32_t> Idx = Msf.addStream(calculateGlobalsHashStreamSize());
  if (!Idx)
    return Idx.takeError();
  GSH->StreamIndex = *Idx;
  Idx = Msf.addStream(calculatePublicsHashStreamSize());
  if (!Idx)
    return Idx.takeError();
  PSH->StreamIndex = *Idx;

  uint32_t RecordBytes =
      GSH->calculateRecordByteSize() + PSH->calculateRecordByteSize();

  Idx = Msf.addStream(RecordBytes);
  if (!Idx)
    return Idx.takeError();
  RecordStreamIdx = *Idx;
  return Error::success();
}

static bool comparePubSymByAddrAndName(
    const std::pair<const CVSymbol *, const PublicSym32 *> &LS,
    const std::pair<const CVSymbol *, const PublicSym32 *> &RS) {
  if (LS.second->Segment != RS.second->Segment)
    return LS.second->Segment < RS.second->Segment;
  if (LS.second->Offset != RS.second->Offset)
    return LS.second->Offset < RS.second->Offset;

  return LS.second->Name < RS.second->Name;
}

/// Compute the address map. The address map is an array of symbol offsets
/// sorted so that it can be binary searched by address.
static std::vector<ulittle32_t> computeAddrMap(ArrayRef<CVSymbol> Records) {
  // Make a vector of pointers to the symbols so we can sort it by address.
  // Also gather the symbol offsets while we're at it.

  std::vector<PublicSym32> DeserializedPublics;
  std::vector<std::pair<const CVSymbol *, const PublicSym32 *>> PublicsByAddr;
  std::vector<uint32_t> SymOffsets;
  DeserializedPublics.reserve(Records.size());
  PublicsByAddr.reserve(Records.size());
  SymOffsets.reserve(Records.size());

  uint32_t SymOffset = 0;
  for (const CVSymbol &Sym : Records) {
    assert(Sym.kind() == SymbolKind::S_PUB32);
    DeserializedPublics.push_back(
        cantFail(SymbolDeserializer::deserializeAs<PublicSym32>(Sym)));
    PublicsByAddr.emplace_back(&Sym, &DeserializedPublics.back());
    SymOffsets.push_back(SymOffset);
    SymOffset += Sym.length();
  }
  std::stable_sort(PublicsByAddr.begin(), PublicsByAddr.end(),
                   comparePubSymByAddrAndName);

  // Fill in the symbol offsets in the appropriate order.
  std::vector<ulittle32_t> AddrMap;
  AddrMap.reserve(Records.size());
  for (auto &Sym : PublicsByAddr) {
    ptrdiff_t Idx = std::distance(Records.data(), Sym.first);
    assert(Idx >= 0 && size_t(Idx) < Records.size());
    AddrMap.push_back(ulittle32_t(SymOffsets[Idx]));
  }
  return AddrMap;
}

uint32_t GSIStreamBuilder::getPublicsStreamIndex() const {
  return PSH->StreamIndex;
}

uint32_t GSIStreamBuilder::getGlobalsStreamIndex() const {
  return GSH->StreamIndex;
}

void GSIStreamBuilder::addPublicSymbol(const PublicSym32 &Pub) {
  PSH->addSymbol(Pub, Msf);
}

void GSIStreamBuilder::addGlobalSymbol(const ProcRefSym &Sym) {
  GSH->addSymbol(Sym, Msf);
}

void GSIStreamBuilder::addGlobalSymbol(const DataSym &Sym) {
  GSH->addSymbol(Sym, Msf);
}

void GSIStreamBuilder::addGlobalSymbol(const ConstantSym &Sym) {
  GSH->addSymbol(Sym, Msf);
}

void GSIStreamBuilder::addGlobalSymbol(const codeview::CVSymbol &Sym) {
  GSH->addSymbol(Sym);
}

static Error writeRecords(BinaryStreamWriter &Writer,
                          ArrayRef<CVSymbol> Records) {
  BinaryItemStream<CVSymbol> ItemStream(support::endianness::little);
  ItemStream.setItems(Records);
  BinaryStreamRef RecordsRef(ItemStream);
  return Writer.writeStreamRef(RecordsRef);
}

Error GSIStreamBuilder::commitSymbolRecordStream(
    WritableBinaryStreamRef Stream) {
  BinaryStreamWriter Writer(Stream);

  // Write public symbol records first, followed by global symbol records.  This
  // must match the order that we assume in finalizeMsfLayout when computing
  // PSHZero and GSHZero.
  if (auto EC = writeRecords(Writer, PSH->Records))
    return EC;
  if (auto EC = writeRecords(Writer, GSH->Records))
    return EC;

  return Error::success();
}

Error GSIStreamBuilder::commitPublicsHashStream(
    WritableBinaryStreamRef Stream) {
  BinaryStreamWriter Writer(Stream);
  PublicsStreamHeader Header;

  // FIXME: Fill these in. They are for incremental linking.
  Header.SymHash = PSH->calculateSerializedLength();
  Header.AddrMap = PSH->Records.size() * 4;
  Header.NumThunks = 0;
  Header.SizeOfThunk = 0;
  Header.ISectThunkTable = 0;
  memset(Header.Padding, 0, sizeof(Header.Padding));
  Header.OffThunkTable = 0;
  Header.NumSections = 0;
  if (auto EC = Writer.writeObject(Header))
    return EC;

  if (auto EC = PSH->commit(Writer))
    return EC;

  std::vector<ulittle32_t> AddrMap = computeAddrMap(PSH->Records);
  if (auto EC = Writer.writeArray(makeArrayRef(AddrMap)))
    return EC;

  return Error::success();
}

Error GSIStreamBuilder::commitGlobalsHashStream(
    WritableBinaryStreamRef Stream) {
  BinaryStreamWriter Writer(Stream);
  return GSH->commit(Writer);
}

Error GSIStreamBuilder::commit(const msf::MSFLayout &Layout,
                               WritableBinaryStreamRef Buffer) {
  auto GS = WritableMappedBlockStream::createIndexedStream(
      Layout, Buffer, getGlobalsStreamIndex(), Msf.getAllocator());
  auto PS = WritableMappedBlockStream::createIndexedStream(
      Layout, Buffer, getPublicsStreamIndex(), Msf.getAllocator());
  auto PRS = WritableMappedBlockStream::createIndexedStream(
      Layout, Buffer, getRecordStreamIdx(), Msf.getAllocator());

  if (auto EC = commitSymbolRecordStream(*PRS))
    return EC;
  if (auto EC = commitGlobalsHashStream(*GS))
    return EC;
  if (auto EC = commitPublicsHashStream(*PS))
    return EC;
  return Error::success();
}
