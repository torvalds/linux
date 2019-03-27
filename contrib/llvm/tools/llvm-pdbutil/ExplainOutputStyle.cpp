//===- ExplainOutputStyle.cpp --------------------------------- *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ExplainOutputStyle.h"

#include "FormatUtil.h"
#include "InputFile.h"
#include "StreamUtil.h"
#include "llvm-pdbutil.h"

#include "llvm/DebugInfo/CodeView/Formatters.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/DbiStream.h"
#include "llvm/DebugInfo/PDB/Native/InfoStream.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/Error.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::msf;
using namespace llvm::pdb;

ExplainOutputStyle::ExplainOutputStyle(InputFile &File, uint64_t FileOffset)
    : File(File), FileOffset(FileOffset), P(2, false, outs()) {}

Error ExplainOutputStyle::dump() {
  P.formatLine("Explaining file offset {0} of file '{1}'.", FileOffset,
               File.getFilePath());

  if (File.isPdb())
    return explainPdbFile();

  return explainBinaryFile();
}

Error ExplainOutputStyle::explainPdbFile() {
  bool IsAllocated = explainPdbBlockStatus();
  if (!IsAllocated)
    return Error::success();

  AutoIndent Indent(P);
  if (isPdbSuperBlock())
    explainPdbSuperBlockOffset();
  else if (isPdbFpmBlock())
    explainPdbFpmBlockOffset();
  else if (isPdbBlockMapBlock())
    explainPdbBlockMapOffset();
  else if (isPdbStreamDirectoryBlock())
    explainPdbStreamDirectoryOffset();
  else if (auto Index = getPdbBlockStreamIndex())
    explainPdbStreamOffset(*Index);
  else
    explainPdbUnknownBlock();
  return Error::success();
}

Error ExplainOutputStyle::explainBinaryFile() {
  std::unique_ptr<BinaryByteStream> Stream =
      llvm::make_unique<BinaryByteStream>(File.unknown().getBuffer(),
                                          llvm::support::little);
  switch (opts::explain::InputType) {
  case opts::explain::InputFileType::DBIStream: {
    DbiStream Dbi(std::move(Stream));
    if (auto EC = Dbi.reload(nullptr))
      return EC;
    explainStreamOffset(Dbi, FileOffset);
    break;
  }
  case opts::explain::InputFileType::PDBStream: {
    InfoStream Info(std::move(Stream));
    if (auto EC = Info.reload())
      return EC;
    explainStreamOffset(Info, FileOffset);
    break;
  }
  default:
    llvm_unreachable("Invalid input file type!");
  }
  return Error::success();
}

uint32_t ExplainOutputStyle::pdbBlockIndex() const {
  return FileOffset / File.pdb().getBlockSize();
}

uint32_t ExplainOutputStyle::pdbBlockOffset() const {
  uint64_t BlockStart = pdbBlockIndex() * File.pdb().getBlockSize();
  assert(FileOffset >= BlockStart);
  return FileOffset - BlockStart;
}

bool ExplainOutputStyle::isPdbSuperBlock() const {
  return pdbBlockIndex() == 0;
}

bool ExplainOutputStyle::isPdbFpm1() const {
  return ((pdbBlockIndex() - 1) % File.pdb().getBlockSize() == 0);
}
bool ExplainOutputStyle::isPdbFpm2() const {
  return ((pdbBlockIndex() - 2) % File.pdb().getBlockSize() == 0);
}

bool ExplainOutputStyle::isPdbFpmBlock() const {
  return isPdbFpm1() || isPdbFpm2();
}

bool ExplainOutputStyle::isPdbBlockMapBlock() const {
  return pdbBlockIndex() == File.pdb().getBlockMapIndex();
}

bool ExplainOutputStyle::isPdbStreamDirectoryBlock() const {
  const auto &Layout = File.pdb().getMsfLayout();
  return llvm::is_contained(Layout.DirectoryBlocks, pdbBlockIndex());
}

Optional<uint32_t> ExplainOutputStyle::getPdbBlockStreamIndex() const {
  const auto &Layout = File.pdb().getMsfLayout();
  for (const auto &Entry : enumerate(Layout.StreamMap)) {
    if (!llvm::is_contained(Entry.value(), pdbBlockIndex()))
      continue;
    return Entry.index();
  }
  return None;
}

bool ExplainOutputStyle::explainPdbBlockStatus() {
  if (FileOffset >= File.pdb().getFileSize()) {
    P.formatLine("Address {0} is not in the file (file size = {1}).",
                 FileOffset, File.pdb().getFileSize());
    return false;
  }
  P.formatLine("Block:Offset = {2:X-}:{1:X-4}.", FileOffset, pdbBlockOffset(),
               pdbBlockIndex());

  bool IsFree = File.pdb().getMsfLayout().FreePageMap[pdbBlockIndex()];
  P.formatLine("Address is in block {0} ({1}allocated).", pdbBlockIndex(),
               IsFree ? "un" : "");
  return !IsFree;
}

#define endof(Class, Field) (offsetof(Class, Field) + sizeof(Class::Field))

void ExplainOutputStyle::explainPdbSuperBlockOffset() {
  P.formatLine("This corresponds to offset {0} of the MSF super block, ",
               pdbBlockOffset());
  if (pdbBlockOffset() < endof(SuperBlock, MagicBytes))
    P.printLine("which is part of the MSF file magic.");
  else if (pdbBlockOffset() < endof(SuperBlock, BlockSize)) {
    P.printLine("which contains the block size of the file.");
    P.formatLine("The current value is {0}.",
                 uint32_t(File.pdb().getMsfLayout().SB->BlockSize));
  } else if (pdbBlockOffset() < endof(SuperBlock, FreeBlockMapBlock)) {
    P.printLine("which contains the index of the FPM block (e.g. 1 or 2).");
    P.formatLine("The current value is {0}.",
                 uint32_t(File.pdb().getMsfLayout().SB->FreeBlockMapBlock));
  } else if (pdbBlockOffset() < endof(SuperBlock, NumBlocks)) {
    P.printLine("which contains the number of blocks in the file.");
    P.formatLine("The current value is {0}.",
                 uint32_t(File.pdb().getMsfLayout().SB->NumBlocks));
  } else if (pdbBlockOffset() < endof(SuperBlock, NumDirectoryBytes)) {
    P.printLine("which contains the number of bytes in the stream directory.");
    P.formatLine("The current value is {0}.",
                 uint32_t(File.pdb().getMsfLayout().SB->NumDirectoryBytes));
  } else if (pdbBlockOffset() < endof(SuperBlock, Unknown1)) {
    P.printLine("whose purpose is unknown.");
    P.formatLine("The current value is {0}.",
                 uint32_t(File.pdb().getMsfLayout().SB->Unknown1));
  } else if (pdbBlockOffset() < endof(SuperBlock, BlockMapAddr)) {
    P.printLine("which contains the file offset of the block map.");
    P.formatLine("The current value is {0}.",
                 uint32_t(File.pdb().getMsfLayout().SB->BlockMapAddr));
  } else {
    assert(pdbBlockOffset() > sizeof(SuperBlock));
    P.printLine(
        "which is outside the range of valid data for the super block.");
  }
}

static std::string toBinaryString(uint8_t Byte) {
  char Result[9] = {0};
  for (int I = 0; I < 8; ++I) {
    char C = (Byte & 1) ? '1' : '0';
    Result[I] = C;
    Byte >>= 1;
  }
  return std::string(Result);
}

void ExplainOutputStyle::explainPdbFpmBlockOffset() {
  const MSFLayout &Layout = File.pdb().getMsfLayout();
  uint32_t MainFpm = Layout.mainFpmBlock();
  uint32_t AltFpm = Layout.alternateFpmBlock();

  assert(isPdbFpmBlock());
  uint32_t Fpm = isPdbFpm1() ? 1 : 2;
  uint32_t FpmChunk = pdbBlockIndex() / File.pdb().getBlockSize();
  assert((Fpm == MainFpm) || (Fpm == AltFpm));
  (void)AltFpm;
  bool IsMain = (Fpm == MainFpm);
  P.formatLine("Address is in FPM{0} ({1} FPM)", Fpm, IsMain ? "Main" : "Alt");
  uint32_t DescribedBlockStart =
      8 * (FpmChunk * File.pdb().getBlockSize() + pdbBlockOffset());
  if (DescribedBlockStart > File.pdb().getBlockCount()) {
    P.printLine("Address is in extraneous FPM space.");
    return;
  }

  P.formatLine("Address describes the allocation status of blocks [{0},{1})",
               DescribedBlockStart, DescribedBlockStart + 8);
  ArrayRef<uint8_t> Bytes;
  cantFail(File.pdb().getMsfBuffer().readBytes(FileOffset, 1, Bytes));
  P.formatLine("Status = {0} (Note: 0 = allocated, 1 = free)",
               toBinaryString(Bytes[0]));
}

void ExplainOutputStyle::explainPdbBlockMapOffset() {
  uint64_t BlockMapOffset = File.pdb().getBlockMapOffset();
  uint32_t OffsetInBlock = FileOffset - BlockMapOffset;
  P.formatLine("Address is at offset {0} of the directory block list",
               OffsetInBlock);
}

static uint32_t getOffsetInStream(ArrayRef<support::ulittle32_t> StreamBlocks,
                                  uint64_t FileOffset, uint32_t BlockSize) {
  uint32_t BlockIndex = FileOffset / BlockSize;
  uint32_t OffsetInBlock = FileOffset - BlockIndex * BlockSize;

  auto Iter = llvm::find(StreamBlocks, BlockIndex);
  assert(Iter != StreamBlocks.end());
  uint32_t StreamBlockIndex = std::distance(StreamBlocks.begin(), Iter);
  return StreamBlockIndex * BlockSize + OffsetInBlock;
}

void ExplainOutputStyle::explainPdbStreamOffset(uint32_t Stream) {
  SmallVector<StreamInfo, 12> Streams;
  discoverStreamPurposes(File.pdb(), Streams);

  assert(Stream <= Streams.size());
  const StreamInfo &S = Streams[Stream];
  const auto &Layout = File.pdb().getStreamLayout(Stream);
  uint32_t StreamOff =
      getOffsetInStream(Layout.Blocks, FileOffset, File.pdb().getBlockSize());
  P.formatLine("Address is at offset {0}/{1} of Stream {2} ({3}){4}.",
               StreamOff, Layout.Length, Stream, S.getLongName(),
               (StreamOff > Layout.Length) ? " in unused space" : "");
  switch (S.getPurpose()) {
  case StreamPurpose::DBI: {
    DbiStream &Dbi = cantFail(File.pdb().getPDBDbiStream());
    explainStreamOffset(Dbi, StreamOff);
    break;
  }
  case StreamPurpose::PDB: {
    InfoStream &Info = cantFail(File.pdb().getPDBInfoStream());
    explainStreamOffset(Info, StreamOff);
    break;
  }
  case StreamPurpose::IPI:
  case StreamPurpose::TPI:
  case StreamPurpose::ModuleStream:
  case StreamPurpose::NamedStream:
  default:
    break;
  }
}

void ExplainOutputStyle::explainPdbStreamDirectoryOffset() {
  auto DirectoryBlocks = File.pdb().getDirectoryBlockArray();
  const auto &Layout = File.pdb().getMsfLayout();
  uint32_t StreamOff =
      getOffsetInStream(DirectoryBlocks, FileOffset, File.pdb().getBlockSize());
  P.formatLine("Address is at offset {0}/{1} of Stream Directory{2}.",
               StreamOff, uint32_t(Layout.SB->NumDirectoryBytes),
               uint32_t(StreamOff > Layout.SB->NumDirectoryBytes)
                   ? " in unused space"
                   : "");
}

void ExplainOutputStyle::explainPdbUnknownBlock() {
  P.formatLine("Address has unknown purpose.");
}

template <typename T>
static void printStructField(LinePrinter &P, StringRef Label, T Value) {
  P.formatLine("which contains {0}.", Label);
  P.formatLine("The current value is {0}.", Value);
}

static void explainDbiHeaderOffset(LinePrinter &P, DbiStream &Dbi,
                                   uint32_t Offset) {
  const DbiStreamHeader *Header = Dbi.getHeader();
  assert(Header != nullptr);

  if (Offset < endof(DbiStreamHeader, VersionSignature))
    printStructField(P, "the DBI Stream Version Signature",
                     int32_t(Header->VersionSignature));
  else if (Offset < endof(DbiStreamHeader, VersionHeader))
    printStructField(P, "the DBI Stream Version Header",
                     uint32_t(Header->VersionHeader));
  else if (Offset < endof(DbiStreamHeader, Age))
    printStructField(P, "the age of the DBI Stream", uint32_t(Header->Age));
  else if (Offset < endof(DbiStreamHeader, GlobalSymbolStreamIndex))
    printStructField(P, "the index of the Global Symbol Stream",
                     uint16_t(Header->GlobalSymbolStreamIndex));
  else if (Offset < endof(DbiStreamHeader, BuildNumber))
    printStructField(P, "the build number", uint16_t(Header->BuildNumber));
  else if (Offset < endof(DbiStreamHeader, PublicSymbolStreamIndex))
    printStructField(P, "the index of the Public Symbol Stream",
                     uint16_t(Header->PublicSymbolStreamIndex));
  else if (Offset < endof(DbiStreamHeader, PdbDllVersion))
    printStructField(P, "the version of mspdb.dll",
                     uint16_t(Header->PdbDllVersion));
  else if (Offset < endof(DbiStreamHeader, SymRecordStreamIndex))
    printStructField(P, "the index of the Symbol Record Stream",
                     uint16_t(Header->SymRecordStreamIndex));
  else if (Offset < endof(DbiStreamHeader, PdbDllRbld))
    printStructField(P, "the rbld of mspdb.dll", uint16_t(Header->PdbDllRbld));
  else if (Offset < endof(DbiStreamHeader, ModiSubstreamSize))
    printStructField(P, "the size of the Module Info Substream",
                     int32_t(Header->ModiSubstreamSize));
  else if (Offset < endof(DbiStreamHeader, SecContrSubstreamSize))
    printStructField(P, "the size of the Section Contribution Substream",
                     int32_t(Header->SecContrSubstreamSize));
  else if (Offset < endof(DbiStreamHeader, SectionMapSize))
    printStructField(P, "the size of the Section Map Substream",
                     int32_t(Header->SectionMapSize));
  else if (Offset < endof(DbiStreamHeader, FileInfoSize))
    printStructField(P, "the size of the File Info Substream",
                     int32_t(Header->FileInfoSize));
  else if (Offset < endof(DbiStreamHeader, TypeServerSize))
    printStructField(P, "the size of the Type Server Map",
                     int32_t(Header->TypeServerSize));
  else if (Offset < endof(DbiStreamHeader, MFCTypeServerIndex))
    printStructField(P, "the index of the MFC Type Server stream",
                     uint32_t(Header->MFCTypeServerIndex));
  else if (Offset < endof(DbiStreamHeader, OptionalDbgHdrSize))
    printStructField(P, "the size of the Optional Debug Stream array",
                     int32_t(Header->OptionalDbgHdrSize));
  else if (Offset < endof(DbiStreamHeader, ECSubstreamSize))
    printStructField(P, "the size of the Edit & Continue Substream",
                     int32_t(Header->ECSubstreamSize));
  else if (Offset < endof(DbiStreamHeader, Flags))
    printStructField(P, "the DBI Stream flags", uint16_t(Header->Flags));
  else if (Offset < endof(DbiStreamHeader, MachineType))
    printStructField(P, "the machine type", uint16_t(Header->MachineType));
  else if (Offset < endof(DbiStreamHeader, Reserved))
    printStructField(P, "reserved data", uint32_t(Header->Reserved));
}

static void explainDbiModiSubstreamOffset(LinePrinter &P, DbiStream &Dbi,
                                          uint32_t Offset) {
  VarStreamArray<DbiModuleDescriptor> ModuleDescriptors;
  BinaryStreamRef ModiSubstreamData = Dbi.getModiSubstreamData().StreamData;
  BinaryStreamReader Reader(ModiSubstreamData);

  cantFail(Reader.readArray(ModuleDescriptors, ModiSubstreamData.getLength()));
  auto Prev = ModuleDescriptors.begin();
  assert(Prev.offset() == 0);
  auto Current = Prev;
  uint32_t Index = 0;
  while (true) {
    Prev = Current;
    ++Current;
    if (Current == ModuleDescriptors.end() || Offset < Current.offset())
      break;
    ++Index;
  }

  DbiModuleDescriptor &Descriptor = *Prev;
  P.formatLine("which contains the descriptor for module {0} ({1}).", Index,
               Descriptor.getModuleName());
}

template <typename T>
static void dontExplain(LinePrinter &Printer, T &Stream, uint32_t Offset) {}

template <typename T, typename SubstreamRangeT>
static void explainSubstreamOffset(LinePrinter &P, uint32_t OffsetInStream,
                                   T &Stream,
                                   const SubstreamRangeT &Substreams) {
  uint32_t SubOffset = OffsetInStream;
  for (const auto &Entry : Substreams) {
    if (Entry.Size <= 0)
      continue;
    uint32_t S = static_cast<uint32_t>(Entry.Size);
    if (SubOffset < S) {
      P.formatLine("address is at offset {0}/{1} of the {2}.", SubOffset, S,
                   Entry.Label);
      Entry.Explain(P, Stream, SubOffset);
      return;
    }
    SubOffset -= S;
  }
}

void ExplainOutputStyle::explainStreamOffset(DbiStream &Dbi,
                                             uint32_t OffsetInStream) {
  P.printLine("Within the DBI stream:");
  AutoIndent Indent(P);
  const DbiStreamHeader *Header = Dbi.getHeader();
  assert(Header != nullptr);

  struct SubstreamInfo {
    int32_t Size;
    StringRef Label;
    void (*Explain)(LinePrinter &, DbiStream &, uint32_t);
  } Substreams[] = {
      {sizeof(DbiStreamHeader), "DBI Stream Header", explainDbiHeaderOffset},
      {int32_t(Header->ModiSubstreamSize), "Module Info Substream",
       explainDbiModiSubstreamOffset},
      {int32_t(Header->SecContrSubstreamSize), "Section Contribution Substream",
       dontExplain<DbiStream>},
      {int32_t(Header->SectionMapSize), "Section Map", dontExplain<DbiStream>},
      {int32_t(Header->FileInfoSize), "File Info Substream",
       dontExplain<DbiStream>},
      {int32_t(Header->TypeServerSize), "Type Server Map Substream",
       dontExplain<DbiStream>},
      {int32_t(Header->ECSubstreamSize), "Edit & Continue Substream",
       dontExplain<DbiStream>},
      {int32_t(Header->OptionalDbgHdrSize), "Optional Debug Stream Array",
       dontExplain<DbiStream>},
  };

  explainSubstreamOffset(P, OffsetInStream, Dbi, Substreams);
}

static void explainPdbStreamHeaderOffset(LinePrinter &P, InfoStream &Info,
                                         uint32_t Offset) {
  const InfoStreamHeader *Header = Info.getHeader();
  assert(Header != nullptr);

  if (Offset < endof(InfoStreamHeader, Version))
    printStructField(P, "the PDB Stream Version Signature",
                     uint32_t(Header->Version));
  else if (Offset < endof(InfoStreamHeader, Signature))
    printStructField(P, "the signature of the PDB Stream",
                     uint32_t(Header->Signature));
  else if (Offset < endof(InfoStreamHeader, Age))
    printStructField(P, "the age of the PDB", uint32_t(Header->Age));
  else if (Offset < endof(InfoStreamHeader, Guid))
    printStructField(P, "the guid of the PDB", fmt_guid(Header->Guid.Guid));
}

void ExplainOutputStyle::explainStreamOffset(InfoStream &Info,
                                             uint32_t OffsetInStream) {
  P.printLine("Within the PDB stream:");
  AutoIndent Indent(P);

  struct SubstreamInfo {
    uint32_t Size;
    StringRef Label;
    void (*Explain)(LinePrinter &, InfoStream &, uint32_t);
  } Substreams[] = {{sizeof(InfoStreamHeader), "PDB Stream Header",
                     explainPdbStreamHeaderOffset},
                    {Info.getNamedStreamMapByteSize(), "Named Stream Map",
                     dontExplain<InfoStream>},
                    {Info.getStreamSize(), "PDB Feature Signatures",
                     dontExplain<InfoStream>}};

  explainSubstreamOffset(P, OffsetInStream, Info, Substreams);
}
