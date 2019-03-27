//===- PDBFileBuilder.cpp - PDB File Creation -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/PDBFileBuilder.h"

#include "llvm/ADT/BitVector.h"

#include "llvm/DebugInfo/MSF/MSFBuilder.h"
#include "llvm/DebugInfo/PDB/Native/DbiStream.h"
#include "llvm/DebugInfo/PDB/Native/DbiStreamBuilder.h"
#include "llvm/DebugInfo/PDB/Native/GSIStreamBuilder.h"
#include "llvm/DebugInfo/PDB/Native/InfoStream.h"
#include "llvm/DebugInfo/PDB/Native/InfoStreamBuilder.h"
#include "llvm/DebugInfo/PDB/Native/PDBStringTableBuilder.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/DebugInfo/PDB/Native/TpiStreamBuilder.h"
#include "llvm/Support/BinaryStream.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/JamCRC.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/xxhash.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::msf;
using namespace llvm::pdb;
using namespace llvm::support;

PDBFileBuilder::PDBFileBuilder(BumpPtrAllocator &Allocator)
    : Allocator(Allocator), InjectedSourceHashTraits(Strings),
      InjectedSourceTable(2, InjectedSourceHashTraits) {}

PDBFileBuilder::~PDBFileBuilder() {}

Error PDBFileBuilder::initialize(uint32_t BlockSize) {
  auto ExpectedMsf = MSFBuilder::create(Allocator, BlockSize);
  if (!ExpectedMsf)
    return ExpectedMsf.takeError();
  Msf = llvm::make_unique<MSFBuilder>(std::move(*ExpectedMsf));
  return Error::success();
}

MSFBuilder &PDBFileBuilder::getMsfBuilder() { return *Msf; }

InfoStreamBuilder &PDBFileBuilder::getInfoBuilder() {
  if (!Info)
    Info = llvm::make_unique<InfoStreamBuilder>(*Msf, NamedStreams);
  return *Info;
}

DbiStreamBuilder &PDBFileBuilder::getDbiBuilder() {
  if (!Dbi)
    Dbi = llvm::make_unique<DbiStreamBuilder>(*Msf);
  return *Dbi;
}

TpiStreamBuilder &PDBFileBuilder::getTpiBuilder() {
  if (!Tpi)
    Tpi = llvm::make_unique<TpiStreamBuilder>(*Msf, StreamTPI);
  return *Tpi;
}

TpiStreamBuilder &PDBFileBuilder::getIpiBuilder() {
  if (!Ipi)
    Ipi = llvm::make_unique<TpiStreamBuilder>(*Msf, StreamIPI);
  return *Ipi;
}

PDBStringTableBuilder &PDBFileBuilder::getStringTableBuilder() {
  return Strings;
}

GSIStreamBuilder &PDBFileBuilder::getGsiBuilder() {
  if (!Gsi)
    Gsi = llvm::make_unique<GSIStreamBuilder>(*Msf);
  return *Gsi;
}

Expected<uint32_t> PDBFileBuilder::allocateNamedStream(StringRef Name,
                                                       uint32_t Size) {
  auto ExpectedStream = Msf->addStream(Size);
  if (ExpectedStream)
    NamedStreams.set(Name, *ExpectedStream);
  return ExpectedStream;
}

Error PDBFileBuilder::addNamedStream(StringRef Name, StringRef Data) {
  Expected<uint32_t> ExpectedIndex = allocateNamedStream(Name, Data.size());
  if (!ExpectedIndex)
    return ExpectedIndex.takeError();
  assert(NamedStreamData.count(*ExpectedIndex) == 0);
  NamedStreamData[*ExpectedIndex] = Data;
  return Error::success();
}

void PDBFileBuilder::addInjectedSource(StringRef Name,
                                       std::unique_ptr<MemoryBuffer> Buffer) {
  // Stream names must be exact matches, since they get looked up in a hash
  // table and the hash value is dependent on the exact contents of the string.
  // link.exe lowercases a path and converts / to \, so we must do the same.
  SmallString<64> VName;
  sys::path::native(Name.lower(), VName);

  uint32_t NI = getStringTableBuilder().insert(Name);
  uint32_t VNI = getStringTableBuilder().insert(VName);

  InjectedSourceDescriptor Desc;
  Desc.Content = std::move(Buffer);
  Desc.NameIndex = NI;
  Desc.VNameIndex = VNI;
  Desc.StreamName = "/src/files/";

  Desc.StreamName += VName;

  InjectedSources.push_back(std::move(Desc));
}

Error PDBFileBuilder::finalizeMsfLayout() {

  if (Ipi && Ipi->getRecordCount() > 0) {
    // In theory newer PDBs always have an ID stream, but by saying that we're
    // only going to *really* have an ID stream if there is at least one ID
    // record, we leave open the opportunity to test older PDBs such as those
    // that don't have an ID stream.
    auto &Info = getInfoBuilder();
    Info.addFeature(PdbRaw_FeatureSig::VC140);
  }

  uint32_t StringsLen = Strings.calculateSerializedSize();

  Expected<uint32_t> SN = allocateNamedStream("/LinkInfo", 0);
  if (!SN)
    return SN.takeError();

  if (Gsi) {
    if (auto EC = Gsi->finalizeMsfLayout())
      return EC;
    if (Dbi) {
      Dbi->setPublicsStreamIndex(Gsi->getPublicsStreamIndex());
      Dbi->setGlobalsStreamIndex(Gsi->getGlobalsStreamIndex());
      Dbi->setSymbolRecordStreamIndex(Gsi->getRecordStreamIdx());
    }
  }
  if (Tpi) {
    if (auto EC = Tpi->finalizeMsfLayout())
      return EC;
  }
  if (Dbi) {
    if (auto EC = Dbi->finalizeMsfLayout())
      return EC;
  }
  SN = allocateNamedStream("/names", StringsLen);
  if (!SN)
    return SN.takeError();

  if (Ipi) {
    if (auto EC = Ipi->finalizeMsfLayout())
      return EC;
  }

  // Do this last, since it relies on the named stream map being complete, and
  // that can be updated by previous steps in the finalization.
  if (Info) {
    if (auto EC = Info->finalizeMsfLayout())
      return EC;
  }

  if (!InjectedSources.empty()) {
    for (const auto &IS : InjectedSources) {
      JamCRC CRC(0);
      CRC.update(makeArrayRef(IS.Content->getBufferStart(),
                              IS.Content->getBufferSize()));

      SrcHeaderBlockEntry Entry;
      ::memset(&Entry, 0, sizeof(SrcHeaderBlockEntry));
      Entry.Size = sizeof(SrcHeaderBlockEntry);
      Entry.FileSize = IS.Content->getBufferSize();
      Entry.FileNI = IS.NameIndex;
      Entry.VFileNI = IS.VNameIndex;
      Entry.ObjNI = 1;
      Entry.IsVirtual = 0;
      Entry.Version =
          static_cast<uint32_t>(PdbRaw_SrcHeaderBlockVer::SrcVerOne);
      Entry.CRC = CRC.getCRC();
      StringRef VName = getStringTableBuilder().getStringForId(IS.VNameIndex);
      InjectedSourceTable.set_as(VName, std::move(Entry));
    }

    uint32_t SrcHeaderBlockSize =
        sizeof(SrcHeaderBlockHeader) +
        InjectedSourceTable.calculateSerializedLength();
    SN = allocateNamedStream("/src/headerblock", SrcHeaderBlockSize);
    if (!SN)
      return SN.takeError();
    for (const auto &IS : InjectedSources) {
      SN = allocateNamedStream(IS.StreamName, IS.Content->getBufferSize());
      if (!SN)
        return SN.takeError();
    }
  }

  // Do this last, since it relies on the named stream map being complete, and
  // that can be updated by previous steps in the finalization.
  if (Info) {
    if (auto EC = Info->finalizeMsfLayout())
      return EC;
  }

  return Error::success();
}

Expected<uint32_t> PDBFileBuilder::getNamedStreamIndex(StringRef Name) const {
  uint32_t SN = 0;
  if (!NamedStreams.get(Name, SN))
    return llvm::make_error<pdb::RawError>(raw_error_code::no_stream);
  return SN;
}

void PDBFileBuilder::commitSrcHeaderBlock(WritableBinaryStream &MsfBuffer,
                                          const msf::MSFLayout &Layout) {
  assert(!InjectedSourceTable.empty());

  uint32_t SN = cantFail(getNamedStreamIndex("/src/headerblock"));
  auto Stream = WritableMappedBlockStream::createIndexedStream(
      Layout, MsfBuffer, SN, Allocator);
  BinaryStreamWriter Writer(*Stream);

  SrcHeaderBlockHeader Header;
  ::memset(&Header, 0, sizeof(Header));
  Header.Version = static_cast<uint32_t>(PdbRaw_SrcHeaderBlockVer::SrcVerOne);
  Header.Size = Writer.bytesRemaining();

  cantFail(Writer.writeObject(Header));
  cantFail(InjectedSourceTable.commit(Writer));

  assert(Writer.bytesRemaining() == 0);
}

void PDBFileBuilder::commitInjectedSources(WritableBinaryStream &MsfBuffer,
                                           const msf::MSFLayout &Layout) {
  if (InjectedSourceTable.empty())
    return;

  commitSrcHeaderBlock(MsfBuffer, Layout);

  for (const auto &IS : InjectedSources) {
    uint32_t SN = cantFail(getNamedStreamIndex(IS.StreamName));

    auto SourceStream = WritableMappedBlockStream::createIndexedStream(
        Layout, MsfBuffer, SN, Allocator);
    BinaryStreamWriter SourceWriter(*SourceStream);
    assert(SourceWriter.bytesRemaining() == IS.Content->getBufferSize());
    cantFail(SourceWriter.writeBytes(
        arrayRefFromStringRef(IS.Content->getBuffer())));
  }
}

Error PDBFileBuilder::commit(StringRef Filename, codeview::GUID *Guid) {
  assert(!Filename.empty());
  if (auto EC = finalizeMsfLayout())
    return EC;

  MSFLayout Layout;
  Expected<FileBufferByteStream> ExpectedMsfBuffer =
      Msf->commit(Filename, Layout);
  if (!ExpectedMsfBuffer)
    return ExpectedMsfBuffer.takeError();
  FileBufferByteStream Buffer = std::move(*ExpectedMsfBuffer);

  auto ExpectedSN = getNamedStreamIndex("/names");
  if (!ExpectedSN)
    return ExpectedSN.takeError();

  auto NS = WritableMappedBlockStream::createIndexedStream(
      Layout, Buffer, *ExpectedSN, Allocator);
  BinaryStreamWriter NSWriter(*NS);
  if (auto EC = Strings.commit(NSWriter))
    return EC;

  for (const auto &NSE : NamedStreamData) {
    if (NSE.second.empty())
      continue;

    auto NS = WritableMappedBlockStream::createIndexedStream(
        Layout, Buffer, NSE.first, Allocator);
    BinaryStreamWriter NSW(*NS);
    if (auto EC = NSW.writeBytes(arrayRefFromStringRef(NSE.second)))
      return EC;
  }

  if (Info) {
    if (auto EC = Info->commit(Layout, Buffer))
      return EC;
  }

  if (Dbi) {
    if (auto EC = Dbi->commit(Layout, Buffer))
      return EC;
  }

  if (Tpi) {
    if (auto EC = Tpi->commit(Layout, Buffer))
      return EC;
  }

  if (Ipi) {
    if (auto EC = Ipi->commit(Layout, Buffer))
      return EC;
  }

  if (Gsi) {
    if (auto EC = Gsi->commit(Layout, Buffer))
      return EC;
  }

  auto InfoStreamBlocks = Layout.StreamMap[StreamPDB];
  assert(!InfoStreamBlocks.empty());
  uint64_t InfoStreamFileOffset =
      blockToOffset(InfoStreamBlocks.front(), Layout.SB->BlockSize);
  InfoStreamHeader *H = reinterpret_cast<InfoStreamHeader *>(
      Buffer.getBufferStart() + InfoStreamFileOffset);

  commitInjectedSources(Buffer, Layout);

  // Set the build id at the very end, after every other byte of the PDB
  // has been written.
  if (Info->hashPDBContentsToGUID()) {
    // Compute a hash of all sections of the output file.
    uint64_t Digest =
        xxHash64({Buffer.getBufferStart(), Buffer.getBufferEnd()});

    H->Age = 1;

    memcpy(H->Guid.Guid, &Digest, 8);
    // xxhash only gives us 8 bytes, so put some fixed data in the other half.
    memcpy(H->Guid.Guid + 8, "LLD PDB.", 8);

    // Put the hash in the Signature field too.
    H->Signature = static_cast<uint32_t>(Digest);

    // Return GUID to caller.
    memcpy(Guid, H->Guid.Guid, 16);
  } else {
    H->Age = Info->getAge();
    H->Guid = Info->getGuid();
    Optional<uint32_t> Sig = Info->getSignature();
    H->Signature = Sig.hasValue() ? *Sig : time(nullptr);
  }

  return Buffer.commit();
}
