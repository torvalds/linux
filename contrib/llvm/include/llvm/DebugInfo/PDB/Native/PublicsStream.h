//===- PublicsStream.h - PDB Public Symbol Stream -------- ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_RAW_PUBLICSSTREAM_H
#define LLVM_DEBUGINFO_PDB_RAW_PUBLICSSTREAM_H

#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/GlobalsStream.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace pdb {
class DbiStream;
struct GSIHashHeader;
class PDBFile;

class PublicsStream {
public:
  PublicsStream(std::unique_ptr<msf::MappedBlockStream> Stream);
  ~PublicsStream();
  Error reload();

  uint32_t getSymHash() const;
  uint16_t getThunkTableSection() const;
  uint32_t getThunkTableOffset() const;
  const GSIHashTable &getPublicsTable() const { return PublicsTable; }
  FixedStreamArray<support::ulittle32_t> getAddressMap() const {
    return AddressMap;
  }
  FixedStreamArray<support::ulittle32_t> getThunkMap() const {
    return ThunkMap;
  }
  FixedStreamArray<SectionOffset> getSectionOffsets() const {
    return SectionOffsets;
  }

private:
  std::unique_ptr<msf::MappedBlockStream> Stream;
  GSIHashTable PublicsTable;
  FixedStreamArray<support::ulittle32_t> AddressMap;
  FixedStreamArray<support::ulittle32_t> ThunkMap;
  FixedStreamArray<SectionOffset> SectionOffsets;

  const PublicsStreamHeader *Header;
};
}
}

#endif
