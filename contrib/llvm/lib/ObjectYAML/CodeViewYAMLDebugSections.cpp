//===- CodeViewYAMLDebugSections.cpp - CodeView YAMLIO debug sections -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines classes for handling the YAML representation of CodeView
// Debug Info.
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjectYAML/CodeViewYAMLDebugSections.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/CodeViewError.h"
#include "llvm/DebugInfo/CodeView/DebugChecksumsSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugCrossExSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugCrossImpSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugFrameDataSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugInlineeLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugStringTableSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugSubsectionVisitor.h"
#include "llvm/DebugInfo/CodeView/DebugSymbolRVASubsection.h"
#include "llvm/DebugInfo/CodeView/DebugSymbolsSubsection.h"
#include "llvm/DebugInfo/CodeView/Line.h"
#include "llvm/DebugInfo/CodeView/StringsAndChecksums.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/ObjectYAML/CodeViewYAMLSymbols.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::CodeViewYAML;
using namespace llvm::CodeViewYAML::detail;
using namespace llvm::yaml;

LLVM_YAML_IS_SEQUENCE_VECTOR(SourceFileChecksumEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(SourceLineEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(SourceColumnEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(SourceLineBlock)
LLVM_YAML_IS_SEQUENCE_VECTOR(SourceLineInfo)
LLVM_YAML_IS_SEQUENCE_VECTOR(InlineeSite)
LLVM_YAML_IS_SEQUENCE_VECTOR(InlineeInfo)
LLVM_YAML_IS_SEQUENCE_VECTOR(CrossModuleExport)
LLVM_YAML_IS_SEQUENCE_VECTOR(YAMLCrossModuleImport)
LLVM_YAML_IS_SEQUENCE_VECTOR(YAMLFrameData)

LLVM_YAML_DECLARE_SCALAR_TRAITS(HexFormattedString, QuotingType::None)
LLVM_YAML_DECLARE_ENUM_TRAITS(DebugSubsectionKind)
LLVM_YAML_DECLARE_ENUM_TRAITS(FileChecksumKind)
LLVM_YAML_DECLARE_BITSET_TRAITS(LineFlags)

LLVM_YAML_DECLARE_MAPPING_TRAITS(CrossModuleExport)
LLVM_YAML_DECLARE_MAPPING_TRAITS(YAMLFrameData)
LLVM_YAML_DECLARE_MAPPING_TRAITS(YAMLCrossModuleImport)
LLVM_YAML_DECLARE_MAPPING_TRAITS(CrossModuleImportItem)
LLVM_YAML_DECLARE_MAPPING_TRAITS(SourceLineEntry)
LLVM_YAML_DECLARE_MAPPING_TRAITS(SourceColumnEntry)
LLVM_YAML_DECLARE_MAPPING_TRAITS(SourceFileChecksumEntry)
LLVM_YAML_DECLARE_MAPPING_TRAITS(SourceLineBlock)
LLVM_YAML_DECLARE_MAPPING_TRAITS(InlineeSite)

namespace llvm {
namespace CodeViewYAML {
namespace detail {

struct YAMLSubsectionBase {
  explicit YAMLSubsectionBase(DebugSubsectionKind Kind) : Kind(Kind) {}
  virtual ~YAMLSubsectionBase() = default;

  virtual void map(IO &IO) = 0;
  virtual std::shared_ptr<DebugSubsection>
  toCodeViewSubsection(BumpPtrAllocator &Allocator,
                       const codeview::StringsAndChecksums &SC) const = 0;

  DebugSubsectionKind Kind;
};

} // end namespace detail
} // end namespace CodeViewYAML
} // end namespace llvm

namespace {

struct YAMLChecksumsSubsection : public YAMLSubsectionBase {
  YAMLChecksumsSubsection()
      : YAMLSubsectionBase(DebugSubsectionKind::FileChecksums) {}

  void map(IO &IO) override;
  std::shared_ptr<DebugSubsection>
  toCodeViewSubsection(BumpPtrAllocator &Allocator,
                       const codeview::StringsAndChecksums &SC) const override;
  static Expected<std::shared_ptr<YAMLChecksumsSubsection>>
  fromCodeViewSubsection(const DebugStringTableSubsectionRef &Strings,
                         const DebugChecksumsSubsectionRef &FC);

  std::vector<SourceFileChecksumEntry> Checksums;
};

struct YAMLLinesSubsection : public YAMLSubsectionBase {
  YAMLLinesSubsection() : YAMLSubsectionBase(DebugSubsectionKind::Lines) {}

  void map(IO &IO) override;
  std::shared_ptr<DebugSubsection>
  toCodeViewSubsection(BumpPtrAllocator &Allocator,
                       const codeview::StringsAndChecksums &SC) const override;
  static Expected<std::shared_ptr<YAMLLinesSubsection>>
  fromCodeViewSubsection(const DebugStringTableSubsectionRef &Strings,
                         const DebugChecksumsSubsectionRef &Checksums,
                         const DebugLinesSubsectionRef &Lines);

  SourceLineInfo Lines;
};

struct YAMLInlineeLinesSubsection : public YAMLSubsectionBase {
  YAMLInlineeLinesSubsection()
      : YAMLSubsectionBase(DebugSubsectionKind::InlineeLines) {}

  void map(IO &IO) override;
  std::shared_ptr<DebugSubsection>
  toCodeViewSubsection(BumpPtrAllocator &Allocator,
                       const codeview::StringsAndChecksums &SC) const override;
  static Expected<std::shared_ptr<YAMLInlineeLinesSubsection>>
  fromCodeViewSubsection(const DebugStringTableSubsectionRef &Strings,
                         const DebugChecksumsSubsectionRef &Checksums,
                         const DebugInlineeLinesSubsectionRef &Lines);

  InlineeInfo InlineeLines;
};

struct YAMLCrossModuleExportsSubsection : public YAMLSubsectionBase {
  YAMLCrossModuleExportsSubsection()
      : YAMLSubsectionBase(DebugSubsectionKind::CrossScopeExports) {}

  void map(IO &IO) override;
  std::shared_ptr<DebugSubsection>
  toCodeViewSubsection(BumpPtrAllocator &Allocator,
                       const codeview::StringsAndChecksums &SC) const override;
  static Expected<std::shared_ptr<YAMLCrossModuleExportsSubsection>>
  fromCodeViewSubsection(const DebugCrossModuleExportsSubsectionRef &Exports);

  std::vector<CrossModuleExport> Exports;
};

struct YAMLCrossModuleImportsSubsection : public YAMLSubsectionBase {
  YAMLCrossModuleImportsSubsection()
      : YAMLSubsectionBase(DebugSubsectionKind::CrossScopeImports) {}

  void map(IO &IO) override;
  std::shared_ptr<DebugSubsection>
  toCodeViewSubsection(BumpPtrAllocator &Allocator,
                       const codeview::StringsAndChecksums &SC) const override;
  static Expected<std::shared_ptr<YAMLCrossModuleImportsSubsection>>
  fromCodeViewSubsection(const DebugStringTableSubsectionRef &Strings,
                         const DebugCrossModuleImportsSubsectionRef &Imports);

  std::vector<YAMLCrossModuleImport> Imports;
};

struct YAMLSymbolsSubsection : public YAMLSubsectionBase {
  YAMLSymbolsSubsection() : YAMLSubsectionBase(DebugSubsectionKind::Symbols) {}

  void map(IO &IO) override;
  std::shared_ptr<DebugSubsection>
  toCodeViewSubsection(BumpPtrAllocator &Allocator,
                       const codeview::StringsAndChecksums &SC) const override;
  static Expected<std::shared_ptr<YAMLSymbolsSubsection>>
  fromCodeViewSubsection(const DebugSymbolsSubsectionRef &Symbols);

  std::vector<CodeViewYAML::SymbolRecord> Symbols;
};

struct YAMLStringTableSubsection : public YAMLSubsectionBase {
  YAMLStringTableSubsection()
      : YAMLSubsectionBase(DebugSubsectionKind::StringTable) {}

  void map(IO &IO) override;
  std::shared_ptr<DebugSubsection>
  toCodeViewSubsection(BumpPtrAllocator &Allocator,
                       const codeview::StringsAndChecksums &SC) const override;
  static Expected<std::shared_ptr<YAMLStringTableSubsection>>
  fromCodeViewSubsection(const DebugStringTableSubsectionRef &Strings);

  std::vector<StringRef> Strings;
};

struct YAMLFrameDataSubsection : public YAMLSubsectionBase {
  YAMLFrameDataSubsection()
      : YAMLSubsectionBase(DebugSubsectionKind::FrameData) {}

  void map(IO &IO) override;
  std::shared_ptr<DebugSubsection>
  toCodeViewSubsection(BumpPtrAllocator &Allocator,
                       const codeview::StringsAndChecksums &SC) const override;
  static Expected<std::shared_ptr<YAMLFrameDataSubsection>>
  fromCodeViewSubsection(const DebugStringTableSubsectionRef &Strings,
                         const DebugFrameDataSubsectionRef &Frames);

  std::vector<YAMLFrameData> Frames;
};

struct YAMLCoffSymbolRVASubsection : public YAMLSubsectionBase {
  YAMLCoffSymbolRVASubsection()
      : YAMLSubsectionBase(DebugSubsectionKind::CoffSymbolRVA) {}

  void map(IO &IO) override;
  std::shared_ptr<DebugSubsection>
  toCodeViewSubsection(BumpPtrAllocator &Allocator,
                       const codeview::StringsAndChecksums &SC) const override;
  static Expected<std::shared_ptr<YAMLCoffSymbolRVASubsection>>
  fromCodeViewSubsection(const DebugSymbolRVASubsectionRef &RVAs);

  std::vector<uint32_t> RVAs;
};

} // end anonymous namespace

void ScalarBitSetTraits<LineFlags>::bitset(IO &io, LineFlags &Flags) {
  io.bitSetCase(Flags, "HasColumnInfo", LF_HaveColumns);
  io.enumFallback<Hex16>(Flags);
}

void ScalarEnumerationTraits<FileChecksumKind>::enumeration(
    IO &io, FileChecksumKind &Kind) {
  io.enumCase(Kind, "None", FileChecksumKind::None);
  io.enumCase(Kind, "MD5", FileChecksumKind::MD5);
  io.enumCase(Kind, "SHA1", FileChecksumKind::SHA1);
  io.enumCase(Kind, "SHA256", FileChecksumKind::SHA256);
}

void ScalarTraits<HexFormattedString>::output(const HexFormattedString &Value,
                                              void *ctx, raw_ostream &Out) {
  StringRef Bytes(reinterpret_cast<const char *>(Value.Bytes.data()),
                  Value.Bytes.size());
  Out << toHex(Bytes);
}

StringRef ScalarTraits<HexFormattedString>::input(StringRef Scalar, void *ctxt,
                                                  HexFormattedString &Value) {
  std::string H = fromHex(Scalar);
  Value.Bytes.assign(H.begin(), H.end());
  return StringRef();
}

void MappingTraits<SourceLineEntry>::mapping(IO &IO, SourceLineEntry &Obj) {
  IO.mapRequired("Offset", Obj.Offset);
  IO.mapRequired("LineStart", Obj.LineStart);
  IO.mapRequired("IsStatement", Obj.IsStatement);
  IO.mapRequired("EndDelta", Obj.EndDelta);
}

void MappingTraits<SourceColumnEntry>::mapping(IO &IO, SourceColumnEntry &Obj) {
  IO.mapRequired("StartColumn", Obj.StartColumn);
  IO.mapRequired("EndColumn", Obj.EndColumn);
}

void MappingTraits<SourceLineBlock>::mapping(IO &IO, SourceLineBlock &Obj) {
  IO.mapRequired("FileName", Obj.FileName);
  IO.mapRequired("Lines", Obj.Lines);
  IO.mapRequired("Columns", Obj.Columns);
}

void MappingTraits<CrossModuleExport>::mapping(IO &IO, CrossModuleExport &Obj) {
  IO.mapRequired("LocalId", Obj.Local);
  IO.mapRequired("GlobalId", Obj.Global);
}

void MappingTraits<YAMLCrossModuleImport>::mapping(IO &IO,
                                                   YAMLCrossModuleImport &Obj) {
  IO.mapRequired("Module", Obj.ModuleName);
  IO.mapRequired("Imports", Obj.ImportIds);
}

void MappingTraits<SourceFileChecksumEntry>::mapping(
    IO &IO, SourceFileChecksumEntry &Obj) {
  IO.mapRequired("FileName", Obj.FileName);
  IO.mapRequired("Kind", Obj.Kind);
  IO.mapRequired("Checksum", Obj.ChecksumBytes);
}

void MappingTraits<InlineeSite>::mapping(IO &IO, InlineeSite &Obj) {
  IO.mapRequired("FileName", Obj.FileName);
  IO.mapRequired("LineNum", Obj.SourceLineNum);
  IO.mapRequired("Inlinee", Obj.Inlinee);
  IO.mapOptional("ExtraFiles", Obj.ExtraFiles);
}

void MappingTraits<YAMLFrameData>::mapping(IO &IO, YAMLFrameData &Obj) {
  IO.mapRequired("CodeSize", Obj.CodeSize);
  IO.mapRequired("FrameFunc", Obj.FrameFunc);
  IO.mapRequired("LocalSize", Obj.LocalSize);
  IO.mapOptional("MaxStackSize", Obj.MaxStackSize);
  IO.mapOptional("ParamsSize", Obj.ParamsSize);
  IO.mapOptional("PrologSize", Obj.PrologSize);
  IO.mapOptional("RvaStart", Obj.RvaStart);
  IO.mapOptional("SavedRegsSize", Obj.SavedRegsSize);
}

void YAMLChecksumsSubsection::map(IO &IO) {
  IO.mapTag("!FileChecksums", true);
  IO.mapRequired("Checksums", Checksums);
}

void YAMLLinesSubsection::map(IO &IO) {
  IO.mapTag("!Lines", true);
  IO.mapRequired("CodeSize", Lines.CodeSize);

  IO.mapRequired("Flags", Lines.Flags);
  IO.mapRequired("RelocOffset", Lines.RelocOffset);
  IO.mapRequired("RelocSegment", Lines.RelocSegment);
  IO.mapRequired("Blocks", Lines.Blocks);
}

void YAMLInlineeLinesSubsection::map(IO &IO) {
  IO.mapTag("!InlineeLines", true);
  IO.mapRequired("HasExtraFiles", InlineeLines.HasExtraFiles);
  IO.mapRequired("Sites", InlineeLines.Sites);
}

void YAMLCrossModuleExportsSubsection::map(IO &IO) {
  IO.mapTag("!CrossModuleExports", true);
  IO.mapOptional("Exports", Exports);
}

void YAMLCrossModuleImportsSubsection::map(IO &IO) {
  IO.mapTag("!CrossModuleImports", true);
  IO.mapOptional("Imports", Imports);
}

void YAMLSymbolsSubsection::map(IO &IO) {
  IO.mapTag("!Symbols", true);
  IO.mapRequired("Records", Symbols);
}

void YAMLStringTableSubsection::map(IO &IO) {
  IO.mapTag("!StringTable", true);
  IO.mapRequired("Strings", Strings);
}

void YAMLFrameDataSubsection::map(IO &IO) {
  IO.mapTag("!FrameData", true);
  IO.mapRequired("Frames", Frames);
}

void YAMLCoffSymbolRVASubsection::map(IO &IO) {
  IO.mapTag("!COFFSymbolRVAs", true);
  IO.mapRequired("RVAs", RVAs);
}

void MappingTraits<YAMLDebugSubsection>::mapping(
    IO &IO, YAMLDebugSubsection &Subsection) {
  if (!IO.outputting()) {
    if (IO.mapTag("!FileChecksums")) {
      auto SS = std::make_shared<YAMLChecksumsSubsection>();
      Subsection.Subsection = SS;
    } else if (IO.mapTag("!Lines")) {
      Subsection.Subsection = std::make_shared<YAMLLinesSubsection>();
    } else if (IO.mapTag("!InlineeLines")) {
      Subsection.Subsection = std::make_shared<YAMLInlineeLinesSubsection>();
    } else if (IO.mapTag("!CrossModuleExports")) {
      Subsection.Subsection =
          std::make_shared<YAMLCrossModuleExportsSubsection>();
    } else if (IO.mapTag("!CrossModuleImports")) {
      Subsection.Subsection =
          std::make_shared<YAMLCrossModuleImportsSubsection>();
    } else if (IO.mapTag("!Symbols")) {
      Subsection.Subsection = std::make_shared<YAMLSymbolsSubsection>();
    } else if (IO.mapTag("!StringTable")) {
      Subsection.Subsection = std::make_shared<YAMLStringTableSubsection>();
    } else if (IO.mapTag("!FrameData")) {
      Subsection.Subsection = std::make_shared<YAMLFrameDataSubsection>();
    } else if (IO.mapTag("!COFFSymbolRVAs")) {
      Subsection.Subsection = std::make_shared<YAMLCoffSymbolRVASubsection>();
    } else {
      llvm_unreachable("Unexpected subsection tag!");
    }
  }
  Subsection.Subsection->map(IO);
}

std::shared_ptr<DebugSubsection> YAMLChecksumsSubsection::toCodeViewSubsection(
    BumpPtrAllocator &Allocator,
    const codeview::StringsAndChecksums &SC) const {
  assert(SC.hasStrings());
  auto Result = std::make_shared<DebugChecksumsSubsection>(*SC.strings());
  for (const auto &CS : Checksums) {
    Result->addChecksum(CS.FileName, CS.Kind, CS.ChecksumBytes.Bytes);
  }
  return Result;
}

std::shared_ptr<DebugSubsection> YAMLLinesSubsection::toCodeViewSubsection(
    BumpPtrAllocator &Allocator,
    const codeview::StringsAndChecksums &SC) const {
  assert(SC.hasStrings() && SC.hasChecksums());
  auto Result =
      std::make_shared<DebugLinesSubsection>(*SC.checksums(), *SC.strings());
  Result->setCodeSize(Lines.CodeSize);
  Result->setRelocationAddress(Lines.RelocSegment, Lines.RelocOffset);
  Result->setFlags(Lines.Flags);
  for (const auto &LC : Lines.Blocks) {
    Result->createBlock(LC.FileName);
    if (Result->hasColumnInfo()) {
      for (const auto &Item : zip(LC.Lines, LC.Columns)) {
        auto &L = std::get<0>(Item);
        auto &C = std::get<1>(Item);
        uint32_t LE = L.LineStart + L.EndDelta;
        Result->addLineAndColumnInfo(L.Offset,
                                     LineInfo(L.LineStart, LE, L.IsStatement),
                                     C.StartColumn, C.EndColumn);
      }
    } else {
      for (const auto &L : LC.Lines) {
        uint32_t LE = L.LineStart + L.EndDelta;
        Result->addLineInfo(L.Offset, LineInfo(L.LineStart, LE, L.IsStatement));
      }
    }
  }
  return Result;
}

std::shared_ptr<DebugSubsection>
YAMLInlineeLinesSubsection::toCodeViewSubsection(
    BumpPtrAllocator &Allocator,
    const codeview::StringsAndChecksums &SC) const {
  assert(SC.hasChecksums());
  auto Result = std::make_shared<DebugInlineeLinesSubsection>(
      *SC.checksums(), InlineeLines.HasExtraFiles);

  for (const auto &Site : InlineeLines.Sites) {
    Result->addInlineSite(TypeIndex(Site.Inlinee), Site.FileName,
                          Site.SourceLineNum);
    if (!InlineeLines.HasExtraFiles)
      continue;

    for (auto EF : Site.ExtraFiles) {
      Result->addExtraFile(EF);
    }
  }
  return Result;
}

std::shared_ptr<DebugSubsection>
YAMLCrossModuleExportsSubsection::toCodeViewSubsection(
    BumpPtrAllocator &Allocator,
    const codeview::StringsAndChecksums &SC) const {
  auto Result = std::make_shared<DebugCrossModuleExportsSubsection>();
  for (const auto &M : Exports)
    Result->addMapping(M.Local, M.Global);
  return Result;
}

std::shared_ptr<DebugSubsection>
YAMLCrossModuleImportsSubsection::toCodeViewSubsection(
    BumpPtrAllocator &Allocator,
    const codeview::StringsAndChecksums &SC) const {
  assert(SC.hasStrings());

  auto Result =
      std::make_shared<DebugCrossModuleImportsSubsection>(*SC.strings());
  for (const auto &M : Imports) {
    for (const auto Id : M.ImportIds)
      Result->addImport(M.ModuleName, Id);
  }
  return Result;
}

std::shared_ptr<DebugSubsection> YAMLSymbolsSubsection::toCodeViewSubsection(
    BumpPtrAllocator &Allocator,
    const codeview::StringsAndChecksums &SC) const {
  auto Result = std::make_shared<DebugSymbolsSubsection>();
  for (const auto &Sym : Symbols)
    Result->addSymbol(
        Sym.toCodeViewSymbol(Allocator, CodeViewContainer::ObjectFile));
  return Result;
}

std::shared_ptr<DebugSubsection>
YAMLStringTableSubsection::toCodeViewSubsection(
    BumpPtrAllocator &Allocator,
    const codeview::StringsAndChecksums &SC) const {
  auto Result = std::make_shared<DebugStringTableSubsection>();
  for (const auto &Str : this->Strings)
    Result->insert(Str);
  return Result;
}

std::shared_ptr<DebugSubsection> YAMLFrameDataSubsection::toCodeViewSubsection(
    BumpPtrAllocator &Allocator,
    const codeview::StringsAndChecksums &SC) const {
  assert(SC.hasStrings());

  auto Result = std::make_shared<DebugFrameDataSubsection>(true);
  for (const auto &YF : Frames) {
    codeview::FrameData F;
    F.CodeSize = YF.CodeSize;
    F.Flags = YF.Flags;
    F.LocalSize = YF.LocalSize;
    F.MaxStackSize = YF.MaxStackSize;
    F.ParamsSize = YF.ParamsSize;
    F.PrologSize = YF.PrologSize;
    F.RvaStart = YF.RvaStart;
    F.SavedRegsSize = YF.SavedRegsSize;
    F.FrameFunc = SC.strings()->insert(YF.FrameFunc);
    Result->addFrameData(F);
  }
  return Result;
}

std::shared_ptr<DebugSubsection>
YAMLCoffSymbolRVASubsection::toCodeViewSubsection(
    BumpPtrAllocator &Allocator,
    const codeview::StringsAndChecksums &SC) const {
  auto Result = std::make_shared<DebugSymbolRVASubsection>();
  for (const auto &RVA : RVAs)
    Result->addRVA(RVA);
  return Result;
}

static Expected<SourceFileChecksumEntry>
convertOneChecksum(const DebugStringTableSubsectionRef &Strings,
                   const FileChecksumEntry &CS) {
  auto ExpectedString = Strings.getString(CS.FileNameOffset);
  if (!ExpectedString)
    return ExpectedString.takeError();

  SourceFileChecksumEntry Result;
  Result.ChecksumBytes.Bytes = CS.Checksum;
  Result.Kind = CS.Kind;
  Result.FileName = *ExpectedString;
  return Result;
}

static Expected<StringRef>
getFileName(const DebugStringTableSubsectionRef &Strings,
            const DebugChecksumsSubsectionRef &Checksums, uint32_t FileID) {
  auto Iter = Checksums.getArray().at(FileID);
  if (Iter == Checksums.getArray().end())
    return make_error<CodeViewError>(cv_error_code::no_records);
  uint32_t Offset = Iter->FileNameOffset;
  return Strings.getString(Offset);
}

Expected<std::shared_ptr<YAMLChecksumsSubsection>>
YAMLChecksumsSubsection::fromCodeViewSubsection(
    const DebugStringTableSubsectionRef &Strings,
    const DebugChecksumsSubsectionRef &FC) {
  auto Result = std::make_shared<YAMLChecksumsSubsection>();

  for (const auto &CS : FC) {
    auto ConvertedCS = convertOneChecksum(Strings, CS);
    if (!ConvertedCS)
      return ConvertedCS.takeError();
    Result->Checksums.push_back(*ConvertedCS);
  }
  return Result;
}

Expected<std::shared_ptr<YAMLLinesSubsection>>
YAMLLinesSubsection::fromCodeViewSubsection(
    const DebugStringTableSubsectionRef &Strings,
    const DebugChecksumsSubsectionRef &Checksums,
    const DebugLinesSubsectionRef &Lines) {
  auto Result = std::make_shared<YAMLLinesSubsection>();
  Result->Lines.CodeSize = Lines.header()->CodeSize;
  Result->Lines.RelocOffset = Lines.header()->RelocOffset;
  Result->Lines.RelocSegment = Lines.header()->RelocSegment;
  Result->Lines.Flags = static_cast<LineFlags>(uint16_t(Lines.header()->Flags));
  for (const auto &L : Lines) {
    SourceLineBlock Block;
    auto EF = getFileName(Strings, Checksums, L.NameIndex);
    if (!EF)
      return EF.takeError();
    Block.FileName = *EF;
    if (Lines.hasColumnInfo()) {
      for (const auto &C : L.Columns) {
        SourceColumnEntry SCE;
        SCE.EndColumn = C.EndColumn;
        SCE.StartColumn = C.StartColumn;
        Block.Columns.push_back(SCE);
      }
    }
    for (const auto &LN : L.LineNumbers) {
      SourceLineEntry SLE;
      LineInfo LI(LN.Flags);
      SLE.Offset = LN.Offset;
      SLE.LineStart = LI.getStartLine();
      SLE.EndDelta = LI.getLineDelta();
      SLE.IsStatement = LI.isStatement();
      Block.Lines.push_back(SLE);
    }
    Result->Lines.Blocks.push_back(Block);
  }
  return Result;
}

Expected<std::shared_ptr<YAMLInlineeLinesSubsection>>
YAMLInlineeLinesSubsection::fromCodeViewSubsection(
    const DebugStringTableSubsectionRef &Strings,
    const DebugChecksumsSubsectionRef &Checksums,
    const DebugInlineeLinesSubsectionRef &Lines) {
  auto Result = std::make_shared<YAMLInlineeLinesSubsection>();

  Result->InlineeLines.HasExtraFiles = Lines.hasExtraFiles();
  for (const auto &IL : Lines) {
    InlineeSite Site;
    auto ExpF = getFileName(Strings, Checksums, IL.Header->FileID);
    if (!ExpF)
      return ExpF.takeError();
    Site.FileName = *ExpF;
    Site.Inlinee = IL.Header->Inlinee.getIndex();
    Site.SourceLineNum = IL.Header->SourceLineNum;
    if (Lines.hasExtraFiles()) {
      for (const auto EF : IL.ExtraFiles) {
        auto ExpF2 = getFileName(Strings, Checksums, EF);
        if (!ExpF2)
          return ExpF2.takeError();
        Site.ExtraFiles.push_back(*ExpF2);
      }
    }
    Result->InlineeLines.Sites.push_back(Site);
  }
  return Result;
}

Expected<std::shared_ptr<YAMLCrossModuleExportsSubsection>>
YAMLCrossModuleExportsSubsection::fromCodeViewSubsection(
    const DebugCrossModuleExportsSubsectionRef &Exports) {
  auto Result = std::make_shared<YAMLCrossModuleExportsSubsection>();
  Result->Exports.assign(Exports.begin(), Exports.end());
  return Result;
}

Expected<std::shared_ptr<YAMLCrossModuleImportsSubsection>>
YAMLCrossModuleImportsSubsection::fromCodeViewSubsection(
    const DebugStringTableSubsectionRef &Strings,
    const DebugCrossModuleImportsSubsectionRef &Imports) {
  auto Result = std::make_shared<YAMLCrossModuleImportsSubsection>();
  for (const auto &CMI : Imports) {
    YAMLCrossModuleImport YCMI;
    auto ExpectedStr = Strings.getString(CMI.Header->ModuleNameOffset);
    if (!ExpectedStr)
      return ExpectedStr.takeError();
    YCMI.ModuleName = *ExpectedStr;
    YCMI.ImportIds.assign(CMI.Imports.begin(), CMI.Imports.end());
    Result->Imports.push_back(YCMI);
  }
  return Result;
}

Expected<std::shared_ptr<YAMLSymbolsSubsection>>
YAMLSymbolsSubsection::fromCodeViewSubsection(
    const DebugSymbolsSubsectionRef &Symbols) {
  auto Result = std::make_shared<YAMLSymbolsSubsection>();
  for (const auto &Sym : Symbols) {
    auto S = CodeViewYAML::SymbolRecord::fromCodeViewSymbol(Sym);
    if (!S)
      return joinErrors(make_error<CodeViewError>(
                            cv_error_code::corrupt_record,
                            "Invalid CodeView Symbol Record in SymbolRecord "
                            "subsection of .debug$S while converting to YAML!"),
                        S.takeError());

    Result->Symbols.push_back(*S);
  }
  return Result;
}

Expected<std::shared_ptr<YAMLStringTableSubsection>>
YAMLStringTableSubsection::fromCodeViewSubsection(
    const DebugStringTableSubsectionRef &Strings) {
  auto Result = std::make_shared<YAMLStringTableSubsection>();
  BinaryStreamReader Reader(Strings.getBuffer());
  StringRef S;
  // First item is a single null string, skip it.
  if (auto EC = Reader.readCString(S))
    return std::move(EC);
  assert(S.empty());
  while (Reader.bytesRemaining() > 0) {
    if (auto EC = Reader.readCString(S))
      return std::move(EC);
    Result->Strings.push_back(S);
  }
  return Result;
}

Expected<std::shared_ptr<YAMLFrameDataSubsection>>
YAMLFrameDataSubsection::fromCodeViewSubsection(
    const DebugStringTableSubsectionRef &Strings,
    const DebugFrameDataSubsectionRef &Frames) {
  auto Result = std::make_shared<YAMLFrameDataSubsection>();
  for (const auto &F : Frames) {
    YAMLFrameData YF;
    YF.CodeSize = F.CodeSize;
    YF.Flags = F.Flags;
    YF.LocalSize = F.LocalSize;
    YF.MaxStackSize = F.MaxStackSize;
    YF.ParamsSize = F.ParamsSize;
    YF.PrologSize = F.PrologSize;
    YF.RvaStart = F.RvaStart;
    YF.SavedRegsSize = F.SavedRegsSize;

    auto ES = Strings.getString(F.FrameFunc);
    if (!ES)
      return joinErrors(
          make_error<CodeViewError>(
              cv_error_code::no_records,
              "Could not find string for string id while mapping FrameData!"),
          ES.takeError());
    YF.FrameFunc = *ES;
    Result->Frames.push_back(YF);
  }
  return Result;
}

Expected<std::shared_ptr<YAMLCoffSymbolRVASubsection>>
YAMLCoffSymbolRVASubsection::fromCodeViewSubsection(
    const DebugSymbolRVASubsectionRef &Section) {
  auto Result = std::make_shared<YAMLCoffSymbolRVASubsection>();
  for (const auto &RVA : Section) {
    Result->RVAs.push_back(RVA);
  }
  return Result;
}

Expected<std::vector<std::shared_ptr<DebugSubsection>>>
llvm::CodeViewYAML::toCodeViewSubsectionList(
    BumpPtrAllocator &Allocator, ArrayRef<YAMLDebugSubsection> Subsections,
    const codeview::StringsAndChecksums &SC) {
  std::vector<std::shared_ptr<DebugSubsection>> Result;
  if (Subsections.empty())
    return std::move(Result);

  for (const auto &SS : Subsections) {
    std::shared_ptr<DebugSubsection> CVS;
    CVS = SS.Subsection->toCodeViewSubsection(Allocator, SC);
    assert(CVS != nullptr);
    Result.push_back(std::move(CVS));
  }
  return std::move(Result);
}

namespace {

struct SubsectionConversionVisitor : public DebugSubsectionVisitor {
  SubsectionConversionVisitor() = default;

  Error visitUnknown(DebugUnknownSubsectionRef &Unknown) override;
  Error visitLines(DebugLinesSubsectionRef &Lines,
                   const StringsAndChecksumsRef &State) override;
  Error visitFileChecksums(DebugChecksumsSubsectionRef &Checksums,
                           const StringsAndChecksumsRef &State) override;
  Error visitInlineeLines(DebugInlineeLinesSubsectionRef &Inlinees,
                          const StringsAndChecksumsRef &State) override;
  Error visitCrossModuleExports(DebugCrossModuleExportsSubsectionRef &Checksums,
                                const StringsAndChecksumsRef &State) override;
  Error visitCrossModuleImports(DebugCrossModuleImportsSubsectionRef &Inlinees,
                                const StringsAndChecksumsRef &State) override;
  Error visitStringTable(DebugStringTableSubsectionRef &ST,
                         const StringsAndChecksumsRef &State) override;
  Error visitSymbols(DebugSymbolsSubsectionRef &Symbols,
                     const StringsAndChecksumsRef &State) override;
  Error visitFrameData(DebugFrameDataSubsectionRef &Symbols,
                       const StringsAndChecksumsRef &State) override;
  Error visitCOFFSymbolRVAs(DebugSymbolRVASubsectionRef &Symbols,
                            const StringsAndChecksumsRef &State) override;

  YAMLDebugSubsection Subsection;
};

} // end anonymous namespace

Error SubsectionConversionVisitor::visitUnknown(
    DebugUnknownSubsectionRef &Unknown) {
  return make_error<CodeViewError>(cv_error_code::operation_unsupported);
}

Error SubsectionConversionVisitor::visitLines(
    DebugLinesSubsectionRef &Lines, const StringsAndChecksumsRef &State) {
  auto Result = YAMLLinesSubsection::fromCodeViewSubsection(
      State.strings(), State.checksums(), Lines);
  if (!Result)
    return Result.takeError();
  Subsection.Subsection = *Result;
  return Error::success();
}

Error SubsectionConversionVisitor::visitFileChecksums(
    DebugChecksumsSubsectionRef &Checksums,
    const StringsAndChecksumsRef &State) {
  auto Result = YAMLChecksumsSubsection::fromCodeViewSubsection(State.strings(),
                                                                Checksums);
  if (!Result)
    return Result.takeError();
  Subsection.Subsection = *Result;
  return Error::success();
}

Error SubsectionConversionVisitor::visitInlineeLines(
    DebugInlineeLinesSubsectionRef &Inlinees,
    const StringsAndChecksumsRef &State) {
  auto Result = YAMLInlineeLinesSubsection::fromCodeViewSubsection(
      State.strings(), State.checksums(), Inlinees);
  if (!Result)
    return Result.takeError();
  Subsection.Subsection = *Result;
  return Error::success();
}

Error SubsectionConversionVisitor::visitCrossModuleExports(
    DebugCrossModuleExportsSubsectionRef &Exports,
    const StringsAndChecksumsRef &State) {
  auto Result =
      YAMLCrossModuleExportsSubsection::fromCodeViewSubsection(Exports);
  if (!Result)
    return Result.takeError();
  Subsection.Subsection = *Result;
  return Error::success();
}

Error SubsectionConversionVisitor::visitCrossModuleImports(
    DebugCrossModuleImportsSubsectionRef &Imports,
    const StringsAndChecksumsRef &State) {
  auto Result = YAMLCrossModuleImportsSubsection::fromCodeViewSubsection(
      State.strings(), Imports);
  if (!Result)
    return Result.takeError();
  Subsection.Subsection = *Result;
  return Error::success();
}

Error SubsectionConversionVisitor::visitStringTable(
    DebugStringTableSubsectionRef &Strings,
    const StringsAndChecksumsRef &State) {
  auto Result = YAMLStringTableSubsection::fromCodeViewSubsection(Strings);
  if (!Result)
    return Result.takeError();
  Subsection.Subsection = *Result;
  return Error::success();
}

Error SubsectionConversionVisitor::visitSymbols(
    DebugSymbolsSubsectionRef &Symbols, const StringsAndChecksumsRef &State) {
  auto Result = YAMLSymbolsSubsection::fromCodeViewSubsection(Symbols);
  if (!Result)
    return Result.takeError();
  Subsection.Subsection = *Result;
  return Error::success();
}

Error SubsectionConversionVisitor::visitFrameData(
    DebugFrameDataSubsectionRef &Frames, const StringsAndChecksumsRef &State) {
  auto Result =
      YAMLFrameDataSubsection::fromCodeViewSubsection(State.strings(), Frames);
  if (!Result)
    return Result.takeError();
  Subsection.Subsection = *Result;
  return Error::success();
}

Error SubsectionConversionVisitor::visitCOFFSymbolRVAs(
    DebugSymbolRVASubsectionRef &RVAs, const StringsAndChecksumsRef &State) {
  auto Result = YAMLCoffSymbolRVASubsection::fromCodeViewSubsection(RVAs);
  if (!Result)
    return Result.takeError();
  Subsection.Subsection = *Result;
  return Error::success();
}

Expected<YAMLDebugSubsection>
YAMLDebugSubsection::fromCodeViewSubection(const StringsAndChecksumsRef &SC,
                                           const DebugSubsectionRecord &SS) {
  SubsectionConversionVisitor V;
  if (auto EC = visitDebugSubsection(SS, V, SC))
    return std::move(EC);

  return V.Subsection;
}

std::vector<YAMLDebugSubsection>
llvm::CodeViewYAML::fromDebugS(ArrayRef<uint8_t> Data,
                               const StringsAndChecksumsRef &SC) {
  BinaryStreamReader Reader(Data, support::little);
  uint32_t Magic;

  ExitOnError Err("Invalid .debug$S section!");
  Err(Reader.readInteger(Magic));
  assert(Magic == COFF::DEBUG_SECTION_MAGIC && "Invalid .debug$S section!");

  DebugSubsectionArray Subsections;
  Err(Reader.readArray(Subsections, Reader.bytesRemaining()));

  std::vector<YAMLDebugSubsection> Result;

  for (const auto &SS : Subsections) {
    auto YamlSS = Err(YAMLDebugSubsection::fromCodeViewSubection(SC, SS));
    Result.push_back(YamlSS);
  }
  return Result;
}

void llvm::CodeViewYAML::initializeStringsAndChecksums(
    ArrayRef<YAMLDebugSubsection> Sections, codeview::StringsAndChecksums &SC) {
  // String Table and Checksums subsections don't use the allocator.
  BumpPtrAllocator Allocator;

  // It's possible for checksums and strings to even appear in different debug$S
  // sections, so we have to make this a stateful function that can build up
  // the strings and checksums field over multiple iterations.

  // File Checksums require the string table, but may become before it, so we
  // have to scan for strings first, then scan for checksums again from the
  // beginning.
  if (!SC.hasStrings()) {
    for (const auto &SS : Sections) {
      if (SS.Subsection->Kind != DebugSubsectionKind::StringTable)
        continue;

      auto Result = SS.Subsection->toCodeViewSubsection(Allocator, SC);
      SC.setStrings(
          std::static_pointer_cast<DebugStringTableSubsection>(Result));
      break;
    }
  }

  if (SC.hasStrings() && !SC.hasChecksums()) {
    for (const auto &SS : Sections) {
      if (SS.Subsection->Kind != DebugSubsectionKind::FileChecksums)
        continue;

      auto Result = SS.Subsection->toCodeViewSubsection(Allocator, SC);
      SC.setChecksums(
          std::static_pointer_cast<DebugChecksumsSubsection>(Result));
      break;
    }
  }
}
