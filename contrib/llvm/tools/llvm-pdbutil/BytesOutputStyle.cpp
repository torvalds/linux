//===- BytesOutputStyle.cpp ----------------------------------- *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "BytesOutputStyle.h"

#include "FormatUtil.h"
#include "StreamUtil.h"
#include "llvm-pdbutil.h"

#include "llvm/DebugInfo/CodeView/Formatters.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/MSF/MSFCommon.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/DbiStream.h"
#include "llvm/DebugInfo/PDB/Native/InfoStream.h"
#include "llvm/DebugInfo/PDB/Native/ModuleDebugStream.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/FormatVariadic.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::msf;
using namespace llvm::pdb;

namespace {
struct StreamSpec {
  uint32_t SI = 0;
  uint32_t Begin = 0;
  uint32_t Size = 0;
};
} // namespace

static Expected<StreamSpec> parseStreamSpec(StringRef Str) {
  StreamSpec Result;
  if (Str.consumeInteger(0, Result.SI))
    return make_error<RawError>(raw_error_code::invalid_format,
                                "Invalid Stream Specification");
  if (Str.consume_front(":")) {
    if (Str.consumeInteger(0, Result.Begin))
      return make_error<RawError>(raw_error_code::invalid_format,
                                  "Invalid Stream Specification");
  }
  if (Str.consume_front("@")) {
    if (Str.consumeInteger(0, Result.Size))
      return make_error<RawError>(raw_error_code::invalid_format,
                                  "Invalid Stream Specification");
  }

  if (!Str.empty())
    return make_error<RawError>(raw_error_code::invalid_format,
                                "Invalid Stream Specification");
  return Result;
}

static SmallVector<StreamSpec, 2> parseStreamSpecs(LinePrinter &P) {
  SmallVector<StreamSpec, 2> Result;

  for (auto &Str : opts::bytes::DumpStreamData) {
    auto ESS = parseStreamSpec(Str);
    if (!ESS) {
      P.formatLine("Error parsing stream spec {0}: {1}", Str,
                   toString(ESS.takeError()));
      continue;
    }
    Result.push_back(*ESS);
  }
  return Result;
}

static void printHeader(LinePrinter &P, const Twine &S) {
  P.NewLine();
  P.formatLine("{0,=60}", S);
  P.formatLine("{0}", fmt_repeat('=', 60));
}

BytesOutputStyle::BytesOutputStyle(PDBFile &File)
    : File(File), P(2, false, outs()) {}

Error BytesOutputStyle::dump() {

  if (opts::bytes::DumpBlockRange.hasValue()) {
    auto &R = *opts::bytes::DumpBlockRange;
    uint32_t Max = R.Max.getValueOr(R.Min);

    if (Max < R.Min)
      return make_error<StringError>(
          "Invalid block range specified.  Max < Min",
          inconvertibleErrorCode());
    if (Max >= File.getBlockCount())
      return make_error<StringError>(
          "Invalid block range specified.  Requested block out of bounds",
          inconvertibleErrorCode());

    dumpBlockRanges(R.Min, Max);
    P.NewLine();
  }

  if (opts::bytes::DumpByteRange.hasValue()) {
    auto &R = *opts::bytes::DumpByteRange;
    uint32_t Max = R.Max.getValueOr(File.getFileSize());

    if (Max < R.Min)
      return make_error<StringError>("Invalid byte range specified.  Max < Min",
                                     inconvertibleErrorCode());
    if (Max >= File.getFileSize())
      return make_error<StringError>(
          "Invalid byte range specified.  Requested byte larger than file size",
          inconvertibleErrorCode());

    dumpByteRanges(R.Min, Max);
    P.NewLine();
  }

  if (opts::bytes::Fpm) {
    dumpFpm();
    P.NewLine();
  }

  if (!opts::bytes::DumpStreamData.empty()) {
    dumpStreamBytes();
    P.NewLine();
  }

  if (opts::bytes::NameMap) {
    dumpNameMap();
    P.NewLine();
  }

  if (opts::bytes::SectionContributions) {
    dumpSectionContributions();
    P.NewLine();
  }

  if (opts::bytes::SectionMap) {
    dumpSectionMap();
    P.NewLine();
  }

  if (opts::bytes::ModuleInfos) {
    dumpModuleInfos();
    P.NewLine();
  }

  if (opts::bytes::FileInfo) {
    dumpFileInfo();
    P.NewLine();
  }

  if (opts::bytes::TypeServerMap) {
    dumpTypeServerMap();
    P.NewLine();
  }

  if (opts::bytes::ECData) {
    dumpECData();
    P.NewLine();
  }

  if (!opts::bytes::TypeIndex.empty()) {
    dumpTypeIndex(StreamTPI, opts::bytes::TypeIndex);
    P.NewLine();
  }

  if (!opts::bytes::IdIndex.empty()) {
    dumpTypeIndex(StreamIPI, opts::bytes::IdIndex);
    P.NewLine();
  }

  if (opts::bytes::ModuleSyms) {
    dumpModuleSyms();
    P.NewLine();
  }

  if (opts::bytes::ModuleC11) {
    dumpModuleC11();
    P.NewLine();
  }

  if (opts::bytes::ModuleC13) {
    dumpModuleC13();
    P.NewLine();
  }

  return Error::success();
}

void BytesOutputStyle::dumpNameMap() {
  printHeader(P, "Named Stream Map");

  AutoIndent Indent(P);

  auto &InfoS = Err(File.getPDBInfoStream());
  BinarySubstreamRef NS = InfoS.getNamedStreamsBuffer();
  auto Layout = File.getStreamLayout(StreamPDB);
  P.formatMsfStreamData("Named Stream Map", File, Layout, NS);
}

void BytesOutputStyle::dumpBlockRanges(uint32_t Min, uint32_t Max) {
  printHeader(P, "MSF Blocks");

  AutoIndent Indent(P);
  for (uint32_t I = Min; I <= Max; ++I) {
    uint64_t Base = I;
    Base *= File.getBlockSize();

    auto ExpectedData = File.getBlockData(I, File.getBlockSize());
    if (!ExpectedData) {
      P.formatLine("Could not get block {0}.  Reason = {1}", I,
                   toString(ExpectedData.takeError()));
      continue;
    }
    std::string Label = formatv("Block {0}", I).str();
    P.formatBinary(Label, *ExpectedData, Base, 0);
  }
}

void BytesOutputStyle::dumpSectionContributions() {
  printHeader(P, "Section Contributions");

  AutoIndent Indent(P);

  auto &DbiS = Err(File.getPDBDbiStream());
  BinarySubstreamRef NS = DbiS.getSectionContributionData();
  auto Layout = File.getStreamLayout(StreamDBI);
  P.formatMsfStreamData("Section Contributions", File, Layout, NS);
}

void BytesOutputStyle::dumpSectionMap() {
  printHeader(P, "Section Map");

  AutoIndent Indent(P);

  auto &DbiS = Err(File.getPDBDbiStream());
  BinarySubstreamRef NS = DbiS.getSecMapSubstreamData();
  auto Layout = File.getStreamLayout(StreamDBI);
  P.formatMsfStreamData("Section Map", File, Layout, NS);
}

void BytesOutputStyle::dumpModuleInfos() {
  printHeader(P, "Module Infos");

  AutoIndent Indent(P);

  auto &DbiS = Err(File.getPDBDbiStream());
  BinarySubstreamRef NS = DbiS.getModiSubstreamData();
  auto Layout = File.getStreamLayout(StreamDBI);
  P.formatMsfStreamData("Module Infos", File, Layout, NS);
}

void BytesOutputStyle::dumpFileInfo() {
  printHeader(P, "File Info");

  AutoIndent Indent(P);

  auto &DbiS = Err(File.getPDBDbiStream());
  BinarySubstreamRef NS = DbiS.getFileInfoSubstreamData();
  auto Layout = File.getStreamLayout(StreamDBI);
  P.formatMsfStreamData("File Info", File, Layout, NS);
}

void BytesOutputStyle::dumpTypeServerMap() {
  printHeader(P, "Type Server Map");

  AutoIndent Indent(P);

  auto &DbiS = Err(File.getPDBDbiStream());
  BinarySubstreamRef NS = DbiS.getTypeServerMapSubstreamData();
  auto Layout = File.getStreamLayout(StreamDBI);
  P.formatMsfStreamData("Type Server Map", File, Layout, NS);
}

void BytesOutputStyle::dumpECData() {
  printHeader(P, "Edit and Continue Data");

  AutoIndent Indent(P);

  auto &DbiS = Err(File.getPDBDbiStream());
  BinarySubstreamRef NS = DbiS.getECSubstreamData();
  auto Layout = File.getStreamLayout(StreamDBI);
  P.formatMsfStreamData("Edit and Continue Data", File, Layout, NS);
}

void BytesOutputStyle::dumpTypeIndex(uint32_t StreamIdx,
                                     ArrayRef<uint32_t> Indices) {
  assert(StreamIdx == StreamTPI || StreamIdx == StreamIPI);
  assert(!Indices.empty());

  bool IsTpi = (StreamIdx == StreamTPI);

  StringRef Label = IsTpi ? "Type (TPI) Records" : "Index (IPI) Records";
  printHeader(P, Label);
  auto &Stream = Err(IsTpi ? File.getPDBTpiStream() : File.getPDBIpiStream());

  AutoIndent Indent(P);

  auto Substream = Stream.getTypeRecordsSubstream();
  auto &Types = Err(initializeTypes(StreamIdx));
  auto Layout = File.getStreamLayout(StreamIdx);
  for (const auto &Id : Indices) {
    TypeIndex TI(Id);
    if (TI.toArrayIndex() >= Types.capacity()) {
      P.formatLine("Error: TypeIndex {0} does not exist", TI);
      continue;
    }

    auto Type = Types.getType(TI);
    uint32_t Offset = Types.getOffsetOfType(TI);
    auto OneType = Substream.slice(Offset, Type.length());
    P.formatMsfStreamData(formatv("Type {0}", TI).str(), File, Layout, OneType);
  }
}

template <typename CallbackT>
static void iterateOneModule(PDBFile &File, LinePrinter &P,
                             const DbiModuleList &Modules, uint32_t I,
                             uint32_t Digits, uint32_t IndentLevel,
                             CallbackT Callback) {
  if (I >= Modules.getModuleCount()) {
    P.formatLine("Mod {0:4} | Invalid module index ",
                 fmt_align(I, AlignStyle::Right, std::max(Digits, 4U)));
    return;
  }

  auto Modi = Modules.getModuleDescriptor(I);
  P.formatLine("Mod {0:4} | `{1}`: ",
               fmt_align(I, AlignStyle::Right, std::max(Digits, 4U)),
               Modi.getModuleName());

  uint16_t ModiStream = Modi.getModuleStreamIndex();
  AutoIndent Indent2(P, IndentLevel);
  if (ModiStream == kInvalidStreamIndex)
    return;

  auto ModStreamData = MappedBlockStream::createIndexedStream(
      File.getMsfLayout(), File.getMsfBuffer(), ModiStream,
      File.getAllocator());
  ModuleDebugStreamRef ModStream(Modi, std::move(ModStreamData));
  if (auto EC = ModStream.reload()) {
    P.formatLine("Could not parse debug information.");
    return;
  }
  auto Layout = File.getStreamLayout(ModiStream);
  Callback(I, ModStream, Layout);
}

template <typename CallbackT>
static void iterateModules(PDBFile &File, LinePrinter &P, uint32_t IndentLevel,
                           CallbackT Callback) {
  AutoIndent Indent(P);
  if (!File.hasPDBDbiStream()) {
    P.formatLine("DBI Stream not present");
    return;
  }

  ExitOnError Err("Unexpected error processing modules");

  auto &Stream = Err(File.getPDBDbiStream());

  const DbiModuleList &Modules = Stream.modules();

  if (opts::bytes::ModuleIndex.getNumOccurrences() > 0) {
    iterateOneModule(File, P, Modules, opts::bytes::ModuleIndex, 1, IndentLevel,
                     Callback);
  } else {
    uint32_t Count = Modules.getModuleCount();
    uint32_t Digits = NumDigits(Count);
    for (uint32_t I = 0; I < Count; ++I) {
      iterateOneModule(File, P, Modules, I, Digits, IndentLevel, Callback);
    }
  }
}

void BytesOutputStyle::dumpModuleSyms() {
  printHeader(P, "Module Symbols");

  AutoIndent Indent(P);

  iterateModules(File, P, 2,
                 [this](uint32_t Modi, const ModuleDebugStreamRef &Stream,
                        const MSFStreamLayout &Layout) {
                   auto Symbols = Stream.getSymbolsSubstream();
                   P.formatMsfStreamData("Symbols", File, Layout, Symbols);
                 });
}

void BytesOutputStyle::dumpModuleC11() {
  printHeader(P, "C11 Debug Chunks");

  AutoIndent Indent(P);

  iterateModules(File, P, 2,
                 [this](uint32_t Modi, const ModuleDebugStreamRef &Stream,
                        const MSFStreamLayout &Layout) {
                   auto Chunks = Stream.getC11LinesSubstream();
                   P.formatMsfStreamData("C11 Debug Chunks", File, Layout,
                                         Chunks);
                 });
}

void BytesOutputStyle::dumpModuleC13() {
  printHeader(P, "Debug Chunks");

  AutoIndent Indent(P);

  iterateModules(
      File, P, 2,
      [this](uint32_t Modi, const ModuleDebugStreamRef &Stream,
             const MSFStreamLayout &Layout) {
        auto Chunks = Stream.getC13LinesSubstream();
        if (opts::bytes::SplitChunks) {
          for (const auto &SS : Stream.subsections()) {
            BinarySubstreamRef ThisChunk;
            std::tie(ThisChunk, Chunks) = Chunks.split(SS.getRecordLength());
            P.formatMsfStreamData(formatChunkKind(SS.kind()), File, Layout,
                                  ThisChunk);
          }
        } else {
          P.formatMsfStreamData("Debug Chunks", File, Layout, Chunks);
        }
      });
}

void BytesOutputStyle::dumpByteRanges(uint32_t Min, uint32_t Max) {
  printHeader(P, "MSF Bytes");

  AutoIndent Indent(P);

  BinaryStreamReader Reader(File.getMsfBuffer());
  ArrayRef<uint8_t> Data;
  consumeError(Reader.skip(Min));
  uint32_t Size = Max - Min + 1;
  auto EC = Reader.readBytes(Data, Size);
  assert(!EC);
  consumeError(std::move(EC));
  P.formatBinary("Bytes", Data, Min);
}

Expected<codeview::LazyRandomTypeCollection &>
BytesOutputStyle::initializeTypes(uint32_t StreamIdx) {
  auto &TypeCollection = (StreamIdx == StreamTPI) ? TpiTypes : IpiTypes;
  if (TypeCollection)
    return *TypeCollection;

  auto Tpi = (StreamIdx == StreamTPI) ? File.getPDBTpiStream()
                                      : File.getPDBIpiStream();
  if (!Tpi)
    return Tpi.takeError();

  auto &Types = Tpi->typeArray();
  uint32_t Count = Tpi->getNumTypeRecords();
  auto Offsets = Tpi->getTypeIndexOffsets();
  TypeCollection =
      llvm::make_unique<LazyRandomTypeCollection>(Types, Count, Offsets);

  return *TypeCollection;
}

void BytesOutputStyle::dumpFpm() {
  printHeader(P, "Free Page Map");

  msf::MSFStreamLayout FpmLayout = File.getFpmStreamLayout();
  P.formatMsfStreamBlocks(File, FpmLayout);
}

void BytesOutputStyle::dumpStreamBytes() {
  if (StreamPurposes.empty())
    discoverStreamPurposes(File, StreamPurposes);

  printHeader(P, "Stream Data");
  ExitOnError Err("Unexpected error reading stream data");

  auto Specs = parseStreamSpecs(P);

  for (const auto &Spec : Specs) {
    AutoIndent Indent(P);
    if (Spec.SI >= StreamPurposes.size()) {
      P.formatLine("Stream {0}: Not present", Spec.SI);
      continue;
    }
    P.formatMsfStreamData("Data", File, Spec.SI,
                          StreamPurposes[Spec.SI].getShortName(), Spec.Begin,
                          Spec.Size);
  }
}
