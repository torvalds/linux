//===- PDBFile.h - Low level interface to a PDB file ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_RAW_PDBFILE_H
#define LLVM_DEBUGINFO_PDB_RAW_PDBFILE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/DebugInfo/MSF/IMSFFile.h"
#include "llvm/DebugInfo/MSF/MSFCommon.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"

#include <memory>

namespace llvm {

class BinaryStream;

namespace msf {
class MappedBlockStream;
}

namespace pdb {
class DbiStream;
class GlobalsStream;
class InfoStream;
class PDBStringTable;
class PDBFileBuilder;
class PublicsStream;
class SymbolStream;
class TpiStream;

class PDBFile : public msf::IMSFFile {
  friend PDBFileBuilder;

public:
  PDBFile(StringRef Path, std::unique_ptr<BinaryStream> PdbFileBuffer,
          BumpPtrAllocator &Allocator);
  ~PDBFile() override;

  StringRef getFileDirectory() const;
  StringRef getFilePath() const;

  uint32_t getFreeBlockMapBlock() const;
  uint32_t getUnknown1() const;

  uint32_t getBlockSize() const override;
  uint32_t getBlockCount() const override;
  uint32_t getNumDirectoryBytes() const;
  uint32_t getBlockMapIndex() const;
  uint32_t getNumDirectoryBlocks() const;
  uint64_t getBlockMapOffset() const;

  uint32_t getNumStreams() const override;
  uint32_t getMaxStreamSize() const;
  uint32_t getStreamByteSize(uint32_t StreamIndex) const override;
  ArrayRef<support::ulittle32_t>
  getStreamBlockList(uint32_t StreamIndex) const override;
  uint32_t getFileSize() const;

  Expected<ArrayRef<uint8_t>> getBlockData(uint32_t BlockIndex,
                                           uint32_t NumBytes) const override;
  Error setBlockData(uint32_t BlockIndex, uint32_t Offset,
                     ArrayRef<uint8_t> Data) const override;

  ArrayRef<support::ulittle32_t> getStreamSizes() const {
    return ContainerLayout.StreamSizes;
  }
  ArrayRef<ArrayRef<support::ulittle32_t>> getStreamMap() const {
    return ContainerLayout.StreamMap;
  }

  const msf::MSFLayout &getMsfLayout() const { return ContainerLayout; }
  BinaryStreamRef getMsfBuffer() const { return *Buffer; }

  ArrayRef<support::ulittle32_t> getDirectoryBlockArray() const;

  std::unique_ptr<msf::MappedBlockStream> createIndexedStream(uint16_t SN);

  msf::MSFStreamLayout getStreamLayout(uint32_t StreamIdx) const;
  msf::MSFStreamLayout getFpmStreamLayout() const;

  Error parseFileHeaders();
  Error parseStreamData();

  Expected<InfoStream &> getPDBInfoStream();
  Expected<DbiStream &> getPDBDbiStream();
  Expected<GlobalsStream &> getPDBGlobalsStream();
  Expected<TpiStream &> getPDBTpiStream();
  Expected<TpiStream &> getPDBIpiStream();
  Expected<PublicsStream &> getPDBPublicsStream();
  Expected<SymbolStream &> getPDBSymbolStream();
  Expected<PDBStringTable &> getStringTable();

  BumpPtrAllocator &getAllocator() { return Allocator; }

  bool hasPDBDbiStream() const;
  bool hasPDBGlobalsStream();
  bool hasPDBInfoStream() const;
  bool hasPDBIpiStream() const;
  bool hasPDBPublicsStream();
  bool hasPDBSymbolStream();
  bool hasPDBTpiStream() const;
  bool hasPDBStringTable();

  uint32_t getPointerSize();

private:
  Expected<std::unique_ptr<msf::MappedBlockStream>>
  safelyCreateIndexedStream(const msf::MSFLayout &Layout,
                            BinaryStreamRef MsfData,
                            uint32_t StreamIndex) const;

  std::string FilePath;
  BumpPtrAllocator &Allocator;

  std::unique_ptr<BinaryStream> Buffer;

  msf::MSFLayout ContainerLayout;

  std::unique_ptr<GlobalsStream> Globals;
  std::unique_ptr<InfoStream> Info;
  std::unique_ptr<DbiStream> Dbi;
  std::unique_ptr<TpiStream> Tpi;
  std::unique_ptr<TpiStream> Ipi;
  std::unique_ptr<PublicsStream> Publics;
  std::unique_ptr<SymbolStream> Symbols;
  std::unique_ptr<msf::MappedBlockStream> DirectoryStream;
  std::unique_ptr<msf::MappedBlockStream> StringTableStream;
  std::unique_ptr<PDBStringTable> Strings;
};
}
}

#endif
