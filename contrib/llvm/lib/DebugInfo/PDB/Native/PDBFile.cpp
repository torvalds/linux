//===- PDBFile.cpp - Low level interface to a PDB file ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/DebugInfo/MSF/MSFCommon.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/DbiStream.h"
#include "llvm/DebugInfo/PDB/Native/GlobalsStream.h"
#include "llvm/DebugInfo/PDB/Native/InfoStream.h"
#include "llvm/DebugInfo/PDB/Native/PDBStringTable.h"
#include "llvm/DebugInfo/PDB/Native/PublicsStream.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/DebugInfo/PDB/Native/SymbolStream.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/Support/BinaryStream.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Path.h"
#include <algorithm>
#include <cassert>
#include <cstdint>

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::msf;
using namespace llvm::pdb;

namespace {
typedef FixedStreamArray<support::ulittle32_t> ulittle_array;
} // end anonymous namespace

PDBFile::PDBFile(StringRef Path, std::unique_ptr<BinaryStream> PdbFileBuffer,
                 BumpPtrAllocator &Allocator)
    : FilePath(Path), Allocator(Allocator), Buffer(std::move(PdbFileBuffer)) {}

PDBFile::~PDBFile() = default;

StringRef PDBFile::getFilePath() const { return FilePath; }

StringRef PDBFile::getFileDirectory() const {
  return sys::path::parent_path(FilePath);
}

uint32_t PDBFile::getBlockSize() const { return ContainerLayout.SB->BlockSize; }

uint32_t PDBFile::getFreeBlockMapBlock() const {
  return ContainerLayout.SB->FreeBlockMapBlock;
}

uint32_t PDBFile::getBlockCount() const {
  return ContainerLayout.SB->NumBlocks;
}

uint32_t PDBFile::getNumDirectoryBytes() const {
  return ContainerLayout.SB->NumDirectoryBytes;
}

uint32_t PDBFile::getBlockMapIndex() const {
  return ContainerLayout.SB->BlockMapAddr;
}

uint32_t PDBFile::getUnknown1() const { return ContainerLayout.SB->Unknown1; }

uint32_t PDBFile::getNumDirectoryBlocks() const {
  return msf::bytesToBlocks(ContainerLayout.SB->NumDirectoryBytes,
                            ContainerLayout.SB->BlockSize);
}

uint64_t PDBFile::getBlockMapOffset() const {
  return (uint64_t)ContainerLayout.SB->BlockMapAddr *
         ContainerLayout.SB->BlockSize;
}

uint32_t PDBFile::getNumStreams() const {
  return ContainerLayout.StreamSizes.size();
}

uint32_t PDBFile::getMaxStreamSize() const {
  return *std::max_element(ContainerLayout.StreamSizes.begin(),
                           ContainerLayout.StreamSizes.end());
}

uint32_t PDBFile::getStreamByteSize(uint32_t StreamIndex) const {
  return ContainerLayout.StreamSizes[StreamIndex];
}

ArrayRef<support::ulittle32_t>
PDBFile::getStreamBlockList(uint32_t StreamIndex) const {
  return ContainerLayout.StreamMap[StreamIndex];
}

uint32_t PDBFile::getFileSize() const { return Buffer->getLength(); }

Expected<ArrayRef<uint8_t>> PDBFile::getBlockData(uint32_t BlockIndex,
                                                  uint32_t NumBytes) const {
  uint64_t StreamBlockOffset = msf::blockToOffset(BlockIndex, getBlockSize());

  ArrayRef<uint8_t> Result;
  if (auto EC = Buffer->readBytes(StreamBlockOffset, NumBytes, Result))
    return std::move(EC);
  return Result;
}

Error PDBFile::setBlockData(uint32_t BlockIndex, uint32_t Offset,
                            ArrayRef<uint8_t> Data) const {
  return make_error<RawError>(raw_error_code::not_writable,
                              "PDBFile is immutable");
}

Error PDBFile::parseFileHeaders() {
  BinaryStreamReader Reader(*Buffer);

  // Initialize SB.
  const msf::SuperBlock *SB = nullptr;
  if (auto EC = Reader.readObject(SB)) {
    consumeError(std::move(EC));
    return make_error<RawError>(raw_error_code::corrupt_file,
                                "MSF superblock is missing");
  }

  if (auto EC = msf::validateSuperBlock(*SB))
    return EC;

  if (Buffer->getLength() % SB->BlockSize != 0)
    return make_error<RawError>(raw_error_code::corrupt_file,
                                "File size is not a multiple of block size");
  ContainerLayout.SB = SB;

  // Initialize Free Page Map.
  ContainerLayout.FreePageMap.resize(SB->NumBlocks);
  // The Fpm exists either at block 1 or block 2 of the MSF.  However, this
  // allows for a maximum of getBlockSize() * 8 blocks bits in the Fpm, and
  // thusly an equal number of total blocks in the file.  For a block size
  // of 4KiB (very common), this would yield 32KiB total blocks in file, for a
  // maximum file size of 32KiB * 4KiB = 128MiB.  Obviously this won't do, so
  // the Fpm is split across the file at `getBlockSize()` intervals.  As a
  // result, every block whose index is of the form |{1,2} + getBlockSize() * k|
  // for any non-negative integer k is an Fpm block.  In theory, we only really
  // need to reserve blocks of the form |{1,2} + getBlockSize() * 8 * k|, but
  // current versions of the MSF format already expect the Fpm to be arranged
  // at getBlockSize() intervals, so we have to be compatible.
  // See the function fpmPn() for more information:
  // https://github.com/Microsoft/microsoft-pdb/blob/master/PDB/msf/msf.cpp#L489
  auto FpmStream =
      MappedBlockStream::createFpmStream(ContainerLayout, *Buffer, Allocator);
  BinaryStreamReader FpmReader(*FpmStream);
  ArrayRef<uint8_t> FpmBytes;
  if (auto EC = FpmReader.readBytes(FpmBytes, FpmReader.bytesRemaining()))
    return EC;
  uint32_t BlocksRemaining = getBlockCount();
  uint32_t BI = 0;
  for (auto Byte : FpmBytes) {
    uint32_t BlocksThisByte = std::min(BlocksRemaining, 8U);
    for (uint32_t I = 0; I < BlocksThisByte; ++I) {
      if (Byte & (1 << I))
        ContainerLayout.FreePageMap[BI] = true;
      --BlocksRemaining;
      ++BI;
    }
  }

  Reader.setOffset(getBlockMapOffset());
  if (auto EC = Reader.readArray(ContainerLayout.DirectoryBlocks,
                                 getNumDirectoryBlocks()))
    return EC;

  return Error::success();
}

Error PDBFile::parseStreamData() {
  assert(ContainerLayout.SB);
  if (DirectoryStream)
    return Error::success();

  uint32_t NumStreams = 0;

  // Normally you can't use a MappedBlockStream without having fully parsed the
  // PDB file, because it accesses the directory and various other things, which
  // is exactly what we are attempting to parse.  By specifying a custom
  // subclass of IPDBStreamData which only accesses the fields that have already
  // been parsed, we can avoid this and reuse MappedBlockStream.
  auto DS = MappedBlockStream::createDirectoryStream(ContainerLayout, *Buffer,
                                                     Allocator);
  BinaryStreamReader Reader(*DS);
  if (auto EC = Reader.readInteger(NumStreams))
    return EC;

  if (auto EC = Reader.readArray(ContainerLayout.StreamSizes, NumStreams))
    return EC;
  for (uint32_t I = 0; I < NumStreams; ++I) {
    uint32_t StreamSize = getStreamByteSize(I);
    // FIXME: What does StreamSize ~0U mean?
    uint64_t NumExpectedStreamBlocks =
        StreamSize == UINT32_MAX
            ? 0
            : msf::bytesToBlocks(StreamSize, ContainerLayout.SB->BlockSize);

    // For convenience, we store the block array contiguously.  This is because
    // if someone calls setStreamMap(), it is more convenient to be able to call
    // it with an ArrayRef instead of setting up a StreamRef.  Since the
    // DirectoryStream is cached in the class and thus lives for the life of the
    // class, we can be guaranteed that readArray() will return a stable
    // reference, even if it has to allocate from its internal pool.
    ArrayRef<support::ulittle32_t> Blocks;
    if (auto EC = Reader.readArray(Blocks, NumExpectedStreamBlocks))
      return EC;
    for (uint32_t Block : Blocks) {
      uint64_t BlockEndOffset =
          (uint64_t)(Block + 1) * ContainerLayout.SB->BlockSize;
      if (BlockEndOffset > getFileSize())
        return make_error<RawError>(raw_error_code::corrupt_file,
                                    "Stream block map is corrupt.");
    }
    ContainerLayout.StreamMap.push_back(Blocks);
  }

  // We should have read exactly SB->NumDirectoryBytes bytes.
  assert(Reader.bytesRemaining() == 0);
  DirectoryStream = std::move(DS);
  return Error::success();
}

ArrayRef<support::ulittle32_t> PDBFile::getDirectoryBlockArray() const {
  return ContainerLayout.DirectoryBlocks;
}

std::unique_ptr<MappedBlockStream> PDBFile::createIndexedStream(uint16_t SN) {
  if (SN == kInvalidStreamIndex)
    return nullptr;
  return MappedBlockStream::createIndexedStream(ContainerLayout, *Buffer, SN,
                                                Allocator);
}

MSFStreamLayout PDBFile::getStreamLayout(uint32_t StreamIdx) const {
  MSFStreamLayout Result;
  auto Blocks = getStreamBlockList(StreamIdx);
  Result.Blocks.assign(Blocks.begin(), Blocks.end());
  Result.Length = getStreamByteSize(StreamIdx);
  return Result;
}

msf::MSFStreamLayout PDBFile::getFpmStreamLayout() const {
  return msf::getFpmStreamLayout(ContainerLayout);
}

Expected<GlobalsStream &> PDBFile::getPDBGlobalsStream() {
  if (!Globals) {
    auto DbiS = getPDBDbiStream();
    if (!DbiS)
      return DbiS.takeError();

    auto GlobalS = safelyCreateIndexedStream(
        ContainerLayout, *Buffer, DbiS->getGlobalSymbolStreamIndex());
    if (!GlobalS)
      return GlobalS.takeError();
    auto TempGlobals = llvm::make_unique<GlobalsStream>(std::move(*GlobalS));
    if (auto EC = TempGlobals->reload())
      return std::move(EC);
    Globals = std::move(TempGlobals);
  }
  return *Globals;
}

Expected<InfoStream &> PDBFile::getPDBInfoStream() {
  if (!Info) {
    auto InfoS = safelyCreateIndexedStream(ContainerLayout, *Buffer, StreamPDB);
    if (!InfoS)
      return InfoS.takeError();
    auto TempInfo = llvm::make_unique<InfoStream>(std::move(*InfoS));
    if (auto EC = TempInfo->reload())
      return std::move(EC);
    Info = std::move(TempInfo);
  }
  return *Info;
}

Expected<DbiStream &> PDBFile::getPDBDbiStream() {
  if (!Dbi) {
    auto DbiS = safelyCreateIndexedStream(ContainerLayout, *Buffer, StreamDBI);
    if (!DbiS)
      return DbiS.takeError();
    auto TempDbi = llvm::make_unique<DbiStream>(std::move(*DbiS));
    if (auto EC = TempDbi->reload(this))
      return std::move(EC);
    Dbi = std::move(TempDbi);
  }
  return *Dbi;
}

Expected<TpiStream &> PDBFile::getPDBTpiStream() {
  if (!Tpi) {
    auto TpiS = safelyCreateIndexedStream(ContainerLayout, *Buffer, StreamTPI);
    if (!TpiS)
      return TpiS.takeError();
    auto TempTpi = llvm::make_unique<TpiStream>(*this, std::move(*TpiS));
    if (auto EC = TempTpi->reload())
      return std::move(EC);
    Tpi = std::move(TempTpi);
  }
  return *Tpi;
}

Expected<TpiStream &> PDBFile::getPDBIpiStream() {
  if (!Ipi) {
    if (!hasPDBIpiStream())
      return make_error<RawError>(raw_error_code::no_stream);

    auto IpiS = safelyCreateIndexedStream(ContainerLayout, *Buffer, StreamIPI);
    if (!IpiS)
      return IpiS.takeError();
    auto TempIpi = llvm::make_unique<TpiStream>(*this, std::move(*IpiS));
    if (auto EC = TempIpi->reload())
      return std::move(EC);
    Ipi = std::move(TempIpi);
  }
  return *Ipi;
}

Expected<PublicsStream &> PDBFile::getPDBPublicsStream() {
  if (!Publics) {
    auto DbiS = getPDBDbiStream();
    if (!DbiS)
      return DbiS.takeError();

    auto PublicS = safelyCreateIndexedStream(
        ContainerLayout, *Buffer, DbiS->getPublicSymbolStreamIndex());
    if (!PublicS)
      return PublicS.takeError();
    auto TempPublics = llvm::make_unique<PublicsStream>(std::move(*PublicS));
    if (auto EC = TempPublics->reload())
      return std::move(EC);
    Publics = std::move(TempPublics);
  }
  return *Publics;
}

Expected<SymbolStream &> PDBFile::getPDBSymbolStream() {
  if (!Symbols) {
    auto DbiS = getPDBDbiStream();
    if (!DbiS)
      return DbiS.takeError();

    uint32_t SymbolStreamNum = DbiS->getSymRecordStreamIndex();
    auto SymbolS =
        safelyCreateIndexedStream(ContainerLayout, *Buffer, SymbolStreamNum);
    if (!SymbolS)
      return SymbolS.takeError();

    auto TempSymbols = llvm::make_unique<SymbolStream>(std::move(*SymbolS));
    if (auto EC = TempSymbols->reload())
      return std::move(EC);
    Symbols = std::move(TempSymbols);
  }
  return *Symbols;
}

Expected<PDBStringTable &> PDBFile::getStringTable() {
  if (!Strings) {
    auto IS = getPDBInfoStream();
    if (!IS)
      return IS.takeError();

    Expected<uint32_t> ExpectedNSI = IS->getNamedStreamIndex("/names");
    if (!ExpectedNSI)
      return ExpectedNSI.takeError();
    uint32_t NameStreamIndex = *ExpectedNSI;

    auto NS =
        safelyCreateIndexedStream(ContainerLayout, *Buffer, NameStreamIndex);
    if (!NS)
      return NS.takeError();

    auto N = llvm::make_unique<PDBStringTable>();
    BinaryStreamReader Reader(**NS);
    if (auto EC = N->reload(Reader))
      return std::move(EC);
    assert(Reader.bytesRemaining() == 0);
    StringTableStream = std::move(*NS);
    Strings = std::move(N);
  }
  return *Strings;
}

uint32_t PDBFile::getPointerSize() {
  auto DbiS = getPDBDbiStream();
  if (!DbiS)
    return 0;
  PDB_Machine Machine = DbiS->getMachineType();
  if (Machine == PDB_Machine::Amd64)
    return 8;
  return 4;
}

bool PDBFile::hasPDBDbiStream() const {
  return StreamDBI < getNumStreams() && getStreamByteSize(StreamDBI) > 0;
}

bool PDBFile::hasPDBGlobalsStream() {
  auto DbiS = getPDBDbiStream();
  if (!DbiS) {
    consumeError(DbiS.takeError());
    return false;
  }

  return DbiS->getGlobalSymbolStreamIndex() < getNumStreams();
}

bool PDBFile::hasPDBInfoStream() const { return StreamPDB < getNumStreams(); }

bool PDBFile::hasPDBIpiStream() const {
  if (!hasPDBInfoStream())
    return false;

  if (StreamIPI >= getNumStreams())
    return false;

  auto &InfoStream = cantFail(const_cast<PDBFile *>(this)->getPDBInfoStream());
  return InfoStream.containsIdStream();
}

bool PDBFile::hasPDBPublicsStream() {
  auto DbiS = getPDBDbiStream();
  if (!DbiS) {
    consumeError(DbiS.takeError());
    return false;
  }
  return DbiS->getPublicSymbolStreamIndex() < getNumStreams();
}

bool PDBFile::hasPDBSymbolStream() {
  auto DbiS = getPDBDbiStream();
  if (!DbiS)
    return false;
  return DbiS->getSymRecordStreamIndex() < getNumStreams();
}

bool PDBFile::hasPDBTpiStream() const { return StreamTPI < getNumStreams(); }

bool PDBFile::hasPDBStringTable() {
  auto IS = getPDBInfoStream();
  if (!IS)
    return false;
  Expected<uint32_t> ExpectedNSI = IS->getNamedStreamIndex("/names");
  if (!ExpectedNSI) {
    consumeError(ExpectedNSI.takeError());
    return false;
  }
  assert(*ExpectedNSI < getNumStreams());
  return true;
}

/// Wrapper around MappedBlockStream::createIndexedStream() that checks if a
/// stream with that index actually exists.  If it does not, the return value
/// will have an MSFError with code msf_error_code::no_stream.  Else, the return
/// value will contain the stream returned by createIndexedStream().
Expected<std::unique_ptr<MappedBlockStream>>
PDBFile::safelyCreateIndexedStream(const MSFLayout &Layout,
                                   BinaryStreamRef MsfData,
                                   uint32_t StreamIndex) const {
  if (StreamIndex >= getNumStreams())
    return make_error<RawError>(raw_error_code::no_stream);
  return MappedBlockStream::createIndexedStream(Layout, MsfData, StreamIndex,
                                                Allocator);
}
