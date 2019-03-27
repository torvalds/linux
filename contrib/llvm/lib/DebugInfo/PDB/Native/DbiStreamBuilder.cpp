//===- DbiStreamBuilder.cpp - PDB Dbi Stream Creation -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/DbiStreamBuilder.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/DebugInfo/CodeView/DebugFrameDataSubsection.h"
#include "llvm/DebugInfo/MSF/MSFBuilder.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/DbiModuleDescriptorBuilder.h"
#include "llvm/DebugInfo/PDB/Native/DbiStream.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/BinaryStreamWriter.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::msf;
using namespace llvm::pdb;

DbiStreamBuilder::DbiStreamBuilder(msf::MSFBuilder &Msf)
    : Msf(Msf), Allocator(Msf.getAllocator()), Age(1), BuildNumber(0),
      PdbDllVersion(0), PdbDllRbld(0), Flags(0), MachineType(PDB_Machine::x86),
      Header(nullptr) {}

DbiStreamBuilder::~DbiStreamBuilder() {}

void DbiStreamBuilder::setVersionHeader(PdbRaw_DbiVer V) { VerHeader = V; }

void DbiStreamBuilder::setAge(uint32_t A) { Age = A; }

void DbiStreamBuilder::setBuildNumber(uint16_t B) { BuildNumber = B; }

void DbiStreamBuilder::setBuildNumber(uint8_t Major, uint8_t Minor) {
  BuildNumber = (uint16_t(Major) << DbiBuildNo::BuildMajorShift) &
                DbiBuildNo::BuildMajorMask;
  BuildNumber |= (uint16_t(Minor) << DbiBuildNo::BuildMinorShift) &
                 DbiBuildNo::BuildMinorMask;
  BuildNumber |= DbiBuildNo::NewVersionFormatMask;
}

void DbiStreamBuilder::setPdbDllVersion(uint16_t V) { PdbDllVersion = V; }

void DbiStreamBuilder::setPdbDllRbld(uint16_t R) { PdbDllRbld = R; }

void DbiStreamBuilder::setFlags(uint16_t F) { Flags = F; }

void DbiStreamBuilder::setMachineType(PDB_Machine M) { MachineType = M; }

void DbiStreamBuilder::setMachineType(COFF::MachineTypes M) {
  // These enums are mirrors of each other, so we can just cast the value.
  MachineType = static_cast<pdb::PDB_Machine>(static_cast<unsigned>(M));
}

void DbiStreamBuilder::setSectionMap(ArrayRef<SecMapEntry> SecMap) {
  SectionMap = SecMap;
}

void DbiStreamBuilder::setGlobalsStreamIndex(uint32_t Index) {
  GlobalsStreamIndex = Index;
}

void DbiStreamBuilder::setSymbolRecordStreamIndex(uint32_t Index) {
  SymRecordStreamIndex = Index;
}

void DbiStreamBuilder::setPublicsStreamIndex(uint32_t Index) {
  PublicsStreamIndex = Index;
}

void DbiStreamBuilder::addNewFpoData(const codeview::FrameData &FD) {
  if (!NewFpoData.hasValue())
    NewFpoData.emplace(false);

  NewFpoData->addFrameData(FD);
}

void DbiStreamBuilder::addOldFpoData(const object::FpoData &FD) {
  OldFpoData.push_back(FD);
}

Error DbiStreamBuilder::addDbgStream(pdb::DbgHeaderType Type,
                                     ArrayRef<uint8_t> Data) {
  assert(Type != DbgHeaderType::NewFPO &&
         "NewFPO data should be written via addFrameData()!");

  DbgStreams[(int)Type].emplace();
  DbgStreams[(int)Type]->Size = Data.size();
  DbgStreams[(int)Type]->WriteFn = [Data](BinaryStreamWriter &Writer) {
    return Writer.writeArray(Data);
  };
  return Error::success();
}

uint32_t DbiStreamBuilder::addECName(StringRef Name) {
  return ECNamesBuilder.insert(Name);
}

uint32_t DbiStreamBuilder::calculateSerializedLength() const {
  // For now we only support serializing the header.
  return sizeof(DbiStreamHeader) + calculateFileInfoSubstreamSize() +
         calculateModiSubstreamSize() + calculateSectionContribsStreamSize() +
         calculateSectionMapStreamSize() + calculateDbgStreamsSize() +
         ECNamesBuilder.calculateSerializedSize();
}

Expected<DbiModuleDescriptorBuilder &>
DbiStreamBuilder::addModuleInfo(StringRef ModuleName) {
  uint32_t Index = ModiList.size();
  ModiList.push_back(
      llvm::make_unique<DbiModuleDescriptorBuilder>(ModuleName, Index, Msf));
  return *ModiList.back();
}

Error DbiStreamBuilder::addModuleSourceFile(DbiModuleDescriptorBuilder &Module,
                                            StringRef File) {
  uint32_t Index = SourceFileNames.size();
  SourceFileNames.insert(std::make_pair(File, Index));
  Module.addSourceFile(File);
  return Error::success();
}

Expected<uint32_t> DbiStreamBuilder::getSourceFileNameIndex(StringRef File) {
  auto NameIter = SourceFileNames.find(File);
  if (NameIter == SourceFileNames.end())
    return make_error<RawError>(raw_error_code::no_entry,
                                "The specified source file was not found");
  return NameIter->getValue();
}

uint32_t DbiStreamBuilder::calculateModiSubstreamSize() const {
  uint32_t Size = 0;
  for (const auto &M : ModiList)
    Size += M->calculateSerializedLength();
  return Size;
}

uint32_t DbiStreamBuilder::calculateSectionContribsStreamSize() const {
  if (SectionContribs.empty())
    return 0;
  return sizeof(enum PdbRaw_DbiSecContribVer) +
         sizeof(SectionContribs[0]) * SectionContribs.size();
}

uint32_t DbiStreamBuilder::calculateSectionMapStreamSize() const {
  if (SectionMap.empty())
    return 0;
  return sizeof(SecMapHeader) + sizeof(SecMapEntry) * SectionMap.size();
}

uint32_t DbiStreamBuilder::calculateNamesOffset() const {
  uint32_t Offset = 0;
  Offset += sizeof(ulittle16_t);                         // NumModules
  Offset += sizeof(ulittle16_t);                         // NumSourceFiles
  Offset += ModiList.size() * sizeof(ulittle16_t);       // ModIndices
  Offset += ModiList.size() * sizeof(ulittle16_t);       // ModFileCounts
  uint32_t NumFileInfos = 0;
  for (const auto &M : ModiList)
    NumFileInfos += M->source_files().size();
  Offset += NumFileInfos * sizeof(ulittle32_t); // FileNameOffsets
  return Offset;
}

uint32_t DbiStreamBuilder::calculateFileInfoSubstreamSize() const {
  uint32_t Size = calculateNamesOffset();
  Size += calculateNamesBufferSize();
  return alignTo(Size, sizeof(uint32_t));
}

uint32_t DbiStreamBuilder::calculateNamesBufferSize() const {
  uint32_t Size = 0;
  for (const auto &F : SourceFileNames) {
    Size += F.getKeyLength() + 1; // Names[I];
  }
  return Size;
}

uint32_t DbiStreamBuilder::calculateDbgStreamsSize() const {
  return DbgStreams.size() * sizeof(uint16_t);
}

Error DbiStreamBuilder::generateFileInfoSubstream() {
  uint32_t Size = calculateFileInfoSubstreamSize();
  auto Data = Allocator.Allocate<uint8_t>(Size);
  uint32_t NamesOffset = calculateNamesOffset();

  FileInfoBuffer = MutableBinaryByteStream(MutableArrayRef<uint8_t>(Data, Size),
                                           llvm::support::little);

  WritableBinaryStreamRef MetadataBuffer =
      WritableBinaryStreamRef(FileInfoBuffer).keep_front(NamesOffset);
  BinaryStreamWriter MetadataWriter(MetadataBuffer);

  uint16_t ModiCount = std::min<uint32_t>(UINT16_MAX, ModiList.size());
  uint16_t FileCount = std::min<uint32_t>(UINT16_MAX, SourceFileNames.size());
  if (auto EC = MetadataWriter.writeInteger(ModiCount)) // NumModules
    return EC;
  if (auto EC = MetadataWriter.writeInteger(FileCount)) // NumSourceFiles
    return EC;
  for (uint16_t I = 0; I < ModiCount; ++I) {
    if (auto EC = MetadataWriter.writeInteger(I)) // Mod Indices
      return EC;
  }
  for (const auto &MI : ModiList) {
    FileCount = static_cast<uint16_t>(MI->source_files().size());
    if (auto EC = MetadataWriter.writeInteger(FileCount)) // Mod File Counts
      return EC;
  }

  // Before writing the FileNameOffsets array, write the NamesBuffer array.
  // A side effect of this is that this will actually compute the various
  // file name offsets, so we can then go back and write the FileNameOffsets
  // array to the other substream.
  NamesBuffer = WritableBinaryStreamRef(FileInfoBuffer).drop_front(NamesOffset);
  BinaryStreamWriter NameBufferWriter(NamesBuffer);
  for (auto &Name : SourceFileNames) {
    Name.second = NameBufferWriter.getOffset();
    if (auto EC = NameBufferWriter.writeCString(Name.getKey()))
      return EC;
  }

  for (const auto &MI : ModiList) {
    for (StringRef Name : MI->source_files()) {
      auto Result = SourceFileNames.find(Name);
      if (Result == SourceFileNames.end())
        return make_error<RawError>(raw_error_code::no_entry,
                                    "The source file was not found.");
      if (auto EC = MetadataWriter.writeInteger(Result->second))
        return EC;
    }
  }

  if (auto EC = NameBufferWriter.padToAlignment(sizeof(uint32_t)))
    return EC;

  if (NameBufferWriter.bytesRemaining() > 0)
    return make_error<RawError>(raw_error_code::invalid_format,
                                "The names buffer contained unexpected data.");

  if (MetadataWriter.bytesRemaining() > sizeof(uint32_t))
    return make_error<RawError>(
        raw_error_code::invalid_format,
        "The metadata buffer contained unexpected data.");

  return Error::success();
}

Error DbiStreamBuilder::finalize() {
  if (Header)
    return Error::success();

  for (auto &MI : ModiList)
    MI->finalize();

  if (auto EC = generateFileInfoSubstream())
    return EC;

  DbiStreamHeader *H = Allocator.Allocate<DbiStreamHeader>();
  ::memset(H, 0, sizeof(DbiStreamHeader));
  H->VersionHeader = *VerHeader;
  H->VersionSignature = -1;
  H->Age = Age;
  H->BuildNumber = BuildNumber;
  H->Flags = Flags;
  H->PdbDllRbld = PdbDllRbld;
  H->PdbDllVersion = PdbDllVersion;
  H->MachineType = static_cast<uint16_t>(MachineType);

  H->ECSubstreamSize = ECNamesBuilder.calculateSerializedSize();
  H->FileInfoSize = FileInfoBuffer.getLength();
  H->ModiSubstreamSize = calculateModiSubstreamSize();
  H->OptionalDbgHdrSize = DbgStreams.size() * sizeof(uint16_t);
  H->SecContrSubstreamSize = calculateSectionContribsStreamSize();
  H->SectionMapSize = calculateSectionMapStreamSize();
  H->TypeServerSize = 0;
  H->SymRecordStreamIndex = SymRecordStreamIndex;
  H->PublicSymbolStreamIndex = PublicsStreamIndex;
  H->MFCTypeServerIndex = 0; // Not sure what this is, but link.exe writes 0.
  H->GlobalSymbolStreamIndex = GlobalsStreamIndex;

  Header = H;
  return Error::success();
}

Error DbiStreamBuilder::finalizeMsfLayout() {
  if (NewFpoData.hasValue()) {
    DbgStreams[(int)DbgHeaderType::NewFPO].emplace();
    DbgStreams[(int)DbgHeaderType::NewFPO]->Size =
        NewFpoData->calculateSerializedSize();
    DbgStreams[(int)DbgHeaderType::NewFPO]->WriteFn =
        [this](BinaryStreamWriter &Writer) {
          return NewFpoData->commit(Writer);
        };
  }

  if (!OldFpoData.empty()) {
    DbgStreams[(int)DbgHeaderType::FPO].emplace();
    DbgStreams[(int)DbgHeaderType::FPO]->Size =
        sizeof(object::FpoData) * OldFpoData.size();
    DbgStreams[(int)DbgHeaderType::FPO]->WriteFn =
        [this](BinaryStreamWriter &Writer) {
          return Writer.writeArray(makeArrayRef(OldFpoData));
        };
  }

  for (auto &S : DbgStreams) {
    if (!S.hasValue())
      continue;
    auto ExpectedIndex = Msf.addStream(S->Size);
    if (!ExpectedIndex)
      return ExpectedIndex.takeError();
    S->StreamNumber = *ExpectedIndex;
  }

  for (auto &MI : ModiList) {
    if (auto EC = MI->finalizeMsfLayout())
      return EC;
  }

  uint32_t Length = calculateSerializedLength();
  if (auto EC = Msf.setStreamSize(StreamDBI, Length))
    return EC;
  return Error::success();
}

static uint16_t toSecMapFlags(uint32_t Flags) {
  uint16_t Ret = 0;
  if (Flags & COFF::IMAGE_SCN_MEM_READ)
    Ret |= static_cast<uint16_t>(OMFSegDescFlags::Read);
  if (Flags & COFF::IMAGE_SCN_MEM_WRITE)
    Ret |= static_cast<uint16_t>(OMFSegDescFlags::Write);
  if (Flags & COFF::IMAGE_SCN_MEM_EXECUTE)
    Ret |= static_cast<uint16_t>(OMFSegDescFlags::Execute);
  if (Flags & COFF::IMAGE_SCN_MEM_EXECUTE)
    Ret |= static_cast<uint16_t>(OMFSegDescFlags::Execute);
  if (!(Flags & COFF::IMAGE_SCN_MEM_16BIT))
    Ret |= static_cast<uint16_t>(OMFSegDescFlags::AddressIs32Bit);

  // This seems always 1.
  Ret |= static_cast<uint16_t>(OMFSegDescFlags::IsSelector);

  return Ret;
}

// A utility function to create a Section Map for a given list of COFF sections.
//
// A Section Map seem to be a copy of a COFF section list in other format.
// I don't know why a PDB file contains both a COFF section header and
// a Section Map, but it seems it must be present in a PDB.
std::vector<SecMapEntry> DbiStreamBuilder::createSectionMap(
    ArrayRef<llvm::object::coff_section> SecHdrs) {
  std::vector<SecMapEntry> Ret;
  int Idx = 0;

  auto Add = [&]() -> SecMapEntry & {
    Ret.emplace_back();
    auto &Entry = Ret.back();
    memset(&Entry, 0, sizeof(Entry));

    Entry.Frame = Idx + 1;

    // We don't know the meaning of these fields yet.
    Entry.SecName = UINT16_MAX;
    Entry.ClassName = UINT16_MAX;

    return Entry;
  };

  for (auto &Hdr : SecHdrs) {
    auto &Entry = Add();
    Entry.Flags = toSecMapFlags(Hdr.Characteristics);
    Entry.SecByteLength = Hdr.VirtualSize;
    ++Idx;
  }

  // The last entry is for absolute symbols.
  auto &Entry = Add();
  Entry.Flags = static_cast<uint16_t>(OMFSegDescFlags::AddressIs32Bit) |
                static_cast<uint16_t>(OMFSegDescFlags::IsAbsoluteAddress);
  Entry.SecByteLength = UINT32_MAX;

  return Ret;
}

Error DbiStreamBuilder::commit(const msf::MSFLayout &Layout,
                               WritableBinaryStreamRef MsfBuffer) {
  if (auto EC = finalize())
    return EC;

  auto DbiS = WritableMappedBlockStream::createIndexedStream(
      Layout, MsfBuffer, StreamDBI, Allocator);

  BinaryStreamWriter Writer(*DbiS);
  if (auto EC = Writer.writeObject(*Header))
    return EC;

  for (auto &M : ModiList) {
    if (auto EC = M->commit(Writer, Layout, MsfBuffer))
      return EC;
  }

  if (!SectionContribs.empty()) {
    if (auto EC = Writer.writeEnum(DbiSecContribVer60))
      return EC;
    if (auto EC = Writer.writeArray(makeArrayRef(SectionContribs)))
      return EC;
  }

  if (!SectionMap.empty()) {
    ulittle16_t Size = static_cast<ulittle16_t>(SectionMap.size());
    SecMapHeader SMHeader = {Size, Size};
    if (auto EC = Writer.writeObject(SMHeader))
      return EC;
    if (auto EC = Writer.writeArray(SectionMap))
      return EC;
  }

  if (auto EC = Writer.writeStreamRef(FileInfoBuffer))
    return EC;

  if (auto EC = ECNamesBuilder.commit(Writer))
    return EC;

  for (auto &Stream : DbgStreams) {
    uint16_t StreamNumber = kInvalidStreamIndex;
    if (Stream.hasValue())
      StreamNumber = Stream->StreamNumber;
    if (auto EC = Writer.writeInteger(StreamNumber))
      return EC;
  }

  for (auto &Stream : DbgStreams) {
    if (!Stream.hasValue())
      continue;
    assert(Stream->StreamNumber != kInvalidStreamIndex);

    auto WritableStream = WritableMappedBlockStream::createIndexedStream(
        Layout, MsfBuffer, Stream->StreamNumber, Allocator);
    BinaryStreamWriter DbgStreamWriter(*WritableStream);

    if (auto EC = Stream->WriteFn(DbgStreamWriter))
      return EC;
  }

  if (Writer.bytesRemaining() > 0)
    return make_error<RawError>(raw_error_code::invalid_format,
                                "Unexpected bytes found in DBI Stream");
  return Error::success();
}
