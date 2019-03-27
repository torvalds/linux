//===- GlobalsStream.h - PDB Index of Symbols by Name -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_RAW_GLOBALS_STREAM_H
#define LLVM_DEBUGINFO_PDB_RAW_GLOBALS_STREAM_H

#include "llvm/ADT/iterator.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace pdb {
class DbiStream;
class PDBFile;
class SymbolStream;

/// Iterator over hash records producing symbol record offsets. Abstracts away
/// the fact that symbol record offsets on disk are off-by-one.
class GSIHashIterator
    : public iterator_adaptor_base<
          GSIHashIterator, FixedStreamArrayIterator<PSHashRecord>,
          std::random_access_iterator_tag, const uint32_t> {
public:
  template <typename T>
  GSIHashIterator(T &&v)
      : GSIHashIterator::iterator_adaptor_base(std::forward<T &&>(v)) {}

  uint32_t operator*() const {
    uint32_t Off = this->I->Off;
    return --Off;
  }
};

/// From https://github.com/Microsoft/microsoft-pdb/blob/master/PDB/dbi/gsi.cpp
enum : unsigned { IPHR_HASH = 4096 };

/// A readonly view of a hash table used in the globals and publics streams.
/// Most clients will only want to iterate this to get symbol record offsets
/// into the PDB symbol stream.
class GSIHashTable {
public:
  const GSIHashHeader *HashHdr;
  FixedStreamArray<PSHashRecord> HashRecords;
  FixedStreamArray<support::ulittle32_t> HashBitmap;
  FixedStreamArray<support::ulittle32_t> HashBuckets;
  std::array<int32_t, IPHR_HASH + 1> BucketMap;

  Error read(BinaryStreamReader &Reader);

  uint32_t getVerSignature() const { return HashHdr->VerSignature; }
  uint32_t getVerHeader() const { return HashHdr->VerHdr; }
  uint32_t getHashRecordSize() const { return HashHdr->HrSize; }
  uint32_t getNumBuckets() const { return HashHdr->NumBuckets; }

  typedef GSIHashHeader iterator;
  GSIHashIterator begin() const { return GSIHashIterator(HashRecords.begin()); }
  GSIHashIterator end() const { return GSIHashIterator(HashRecords.end()); }
};

class GlobalsStream {
public:
  explicit GlobalsStream(std::unique_ptr<msf::MappedBlockStream> Stream);
  ~GlobalsStream();
  const GSIHashTable &getGlobalsTable() const { return GlobalsTable; }
  Error reload();

  std::vector<std::pair<uint32_t, codeview::CVSymbol>>
  findRecordsByName(StringRef Name, const SymbolStream &Symbols) const;

private:
  GSIHashTable GlobalsTable;
  std::unique_ptr<msf::MappedBlockStream> Stream;
};
}
}

#endif
