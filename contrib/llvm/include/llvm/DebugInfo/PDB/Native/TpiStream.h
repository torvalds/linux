//===- TpiStream.cpp - PDB Type Info (TPI) Stream 2 Access ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_RAW_PDBTPISTREAM_H
#define LLVM_DEBUGINFO_PDB_RAW_PDBTPISTREAM_H

#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/Native/HashTable.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Support/Error.h"

namespace llvm {
namespace codeview {
class LazyRandomTypeCollection;
}
namespace msf {
class MappedBlockStream;
}
namespace pdb {
class PDBFile;

class TpiStream {
  friend class TpiStreamBuilder;

public:
  TpiStream(PDBFile &File, std::unique_ptr<msf::MappedBlockStream> Stream);
  ~TpiStream();
  Error reload();

  PdbRaw_TpiVer getTpiVersion() const;

  uint32_t TypeIndexBegin() const;
  uint32_t TypeIndexEnd() const;
  uint32_t getNumTypeRecords() const;
  uint16_t getTypeHashStreamIndex() const;
  uint16_t getTypeHashStreamAuxIndex() const;

  uint32_t getHashKeySize() const;
  uint32_t getNumHashBuckets() const;
  FixedStreamArray<support::ulittle32_t> getHashValues() const;
  FixedStreamArray<codeview::TypeIndexOffset> getTypeIndexOffsets() const;
  HashTable<support::ulittle32_t> &getHashAdjusters();

  codeview::CVTypeRange types(bool *HadError) const;
  const codeview::CVTypeArray &typeArray() const { return TypeRecords; }

  codeview::LazyRandomTypeCollection &typeCollection() { return *Types; }

  Expected<codeview::TypeIndex>
  findFullDeclForForwardRef(codeview::TypeIndex ForwardRefTI) const;

  std::vector<codeview::TypeIndex> findRecordsByName(StringRef Name) const;

  codeview::CVType getType(codeview::TypeIndex Index);

  BinarySubstreamRef getTypeRecordsSubstream() const;

  Error commit();

  void buildHashMap();

  bool supportsTypeLookup() const;

private:
  PDBFile &Pdb;
  std::unique_ptr<msf::MappedBlockStream> Stream;

  std::unique_ptr<codeview::LazyRandomTypeCollection> Types;

  BinarySubstreamRef TypeRecordsSubstream;

  codeview::CVTypeArray TypeRecords;

  std::unique_ptr<BinaryStream> HashStream;
  FixedStreamArray<support::ulittle32_t> HashValues;
  FixedStreamArray<codeview::TypeIndexOffset> TypeIndexOffsets;
  HashTable<support::ulittle32_t> HashAdjusters;

  std::vector<std::vector<codeview::TypeIndex>> HashMap;

  const TpiStreamHeader *Header;
};
}
}

#endif
