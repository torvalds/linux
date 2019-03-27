//===- FormatUtil.cpp ----------------------------------------- *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "FormatUtil.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/FormatVariadic.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

std::string llvm::pdb::truncateStringBack(StringRef S, uint32_t MaxLen) {
  if (MaxLen == 0 || S.size() <= MaxLen || S.size() <= 3)
    return S;

  assert(MaxLen >= 3);
  uint32_t FinalLen = std::min<size_t>(S.size(), MaxLen - 3);
  S = S.take_front(FinalLen);
  return std::string(S) + std::string("...");
}

std::string llvm::pdb::truncateStringMiddle(StringRef S, uint32_t MaxLen) {
  if (MaxLen == 0 || S.size() <= MaxLen || S.size() <= 3)
    return S;

  assert(MaxLen >= 3);
  uint32_t FinalLen = std::min<size_t>(S.size(), MaxLen - 3);
  StringRef Front = S.take_front(FinalLen / 2);
  StringRef Back = S.take_back(Front.size());
  return std::string(Front) + std::string("...") + std::string(Back);
}

std::string llvm::pdb::truncateStringFront(StringRef S, uint32_t MaxLen) {
  if (MaxLen == 0 || S.size() <= MaxLen || S.size() <= 3)
    return S;

  assert(MaxLen >= 3);
  S = S.take_back(MaxLen - 3);
  return std::string("...") + std::string(S);
}

std::string llvm::pdb::truncateQuotedNameFront(StringRef Label, StringRef Name,
                                               uint32_t MaxLen) {
  uint32_t RequiredExtraChars = Label.size() + 1 + 2;
  if (MaxLen == 0 || RequiredExtraChars + Name.size() <= MaxLen)
    return formatv("{0} \"{1}\"", Label, Name).str();

  assert(MaxLen >= RequiredExtraChars);
  std::string TN = truncateStringFront(Name, MaxLen - RequiredExtraChars);
  return formatv("{0} \"{1}\"", Label, TN).str();
}

std::string llvm::pdb::truncateQuotedNameBack(StringRef Label, StringRef Name,
                                              uint32_t MaxLen) {
  uint32_t RequiredExtraChars = Label.size() + 1 + 2;
  if (MaxLen == 0 || RequiredExtraChars + Name.size() <= MaxLen)
    return formatv("{0} \"{1}\"", Label, Name).str();

  assert(MaxLen >= RequiredExtraChars);
  std::string TN = truncateStringBack(Name, MaxLen - RequiredExtraChars);
  return formatv("{0} \"{1}\"", Label, TN).str();
}

std::string llvm::pdb::typesetItemList(ArrayRef<std::string> Opts,
                                       uint32_t IndentLevel, uint32_t GroupSize,
                                       StringRef Sep) {
  std::string Result;
  while (!Opts.empty()) {
    ArrayRef<std::string> ThisGroup;
    ThisGroup = Opts.take_front(GroupSize);
    Opts = Opts.drop_front(ThisGroup.size());
    Result += join(ThisGroup, Sep);
    if (!Opts.empty()) {
      Result += Sep;
      Result += "\n";
      Result += formatv("{0}", fmt_repeat(' ', IndentLevel));
    }
  }
  return Result;
}

std::string llvm::pdb::typesetStringList(uint32_t IndentLevel,
                                         ArrayRef<StringRef> Strings) {
  std::string Result = "[";
  for (const auto &S : Strings) {
    Result += formatv("\n{0}{1}", fmt_repeat(' ', IndentLevel), S);
  }
  Result += "]";
  return Result;
}

std::string llvm::pdb::formatChunkKind(DebugSubsectionKind Kind,
                                       bool Friendly) {
  if (Friendly) {
    switch (Kind) {
      RETURN_CASE(DebugSubsectionKind, None, "none");
      RETURN_CASE(DebugSubsectionKind, Symbols, "symbols");
      RETURN_CASE(DebugSubsectionKind, Lines, "lines");
      RETURN_CASE(DebugSubsectionKind, StringTable, "strings");
      RETURN_CASE(DebugSubsectionKind, FileChecksums, "checksums");
      RETURN_CASE(DebugSubsectionKind, FrameData, "frames");
      RETURN_CASE(DebugSubsectionKind, InlineeLines, "inlinee lines");
      RETURN_CASE(DebugSubsectionKind, CrossScopeImports, "xmi");
      RETURN_CASE(DebugSubsectionKind, CrossScopeExports, "xme");
      RETURN_CASE(DebugSubsectionKind, ILLines, "il lines");
      RETURN_CASE(DebugSubsectionKind, FuncMDTokenMap, "func md token map");
      RETURN_CASE(DebugSubsectionKind, TypeMDTokenMap, "type md token map");
      RETURN_CASE(DebugSubsectionKind, MergedAssemblyInput,
                  "merged assembly input");
      RETURN_CASE(DebugSubsectionKind, CoffSymbolRVA, "coff symbol rva");
    }
  } else {
    switch (Kind) {
      RETURN_CASE(DebugSubsectionKind, None, "none");
      RETURN_CASE(DebugSubsectionKind, Symbols, "DEBUG_S_SYMBOLS");
      RETURN_CASE(DebugSubsectionKind, Lines, "DEBUG_S_LINES");
      RETURN_CASE(DebugSubsectionKind, StringTable, "DEBUG_S_STRINGTABLE");
      RETURN_CASE(DebugSubsectionKind, FileChecksums, "DEBUG_S_FILECHKSMS");
      RETURN_CASE(DebugSubsectionKind, FrameData, "DEBUG_S_FRAMEDATA");
      RETURN_CASE(DebugSubsectionKind, InlineeLines, "DEBUG_S_INLINEELINES");
      RETURN_CASE(DebugSubsectionKind, CrossScopeImports,
                  "DEBUG_S_CROSSSCOPEIMPORTS");
      RETURN_CASE(DebugSubsectionKind, CrossScopeExports,
                  "DEBUG_S_CROSSSCOPEEXPORTS");
      RETURN_CASE(DebugSubsectionKind, ILLines, "DEBUG_S_IL_LINES");
      RETURN_CASE(DebugSubsectionKind, FuncMDTokenMap,
                  "DEBUG_S_FUNC_MDTOKEN_MAP");
      RETURN_CASE(DebugSubsectionKind, TypeMDTokenMap,
                  "DEBUG_S_TYPE_MDTOKEN_MAP");
      RETURN_CASE(DebugSubsectionKind, MergedAssemblyInput,
                  "DEBUG_S_MERGED_ASSEMBLYINPUT");
      RETURN_CASE(DebugSubsectionKind, CoffSymbolRVA,
                  "DEBUG_S_COFF_SYMBOL_RVA");
    }
  }
  return formatUnknownEnum(Kind);
}

std::string llvm::pdb::formatSymbolKind(SymbolKind K) {
  switch (uint32_t(K)) {
#define SYMBOL_RECORD(EnumName, value, name)                                   \
  case EnumName:                                                               \
    return #EnumName;
#define CV_SYMBOL(EnumName, value) SYMBOL_RECORD(EnumName, value, EnumName)
#include "llvm/DebugInfo/CodeView/CodeViewSymbols.def"
  }
  return formatUnknownEnum(K);
}

StringRef llvm::pdb::formatTypeLeafKind(TypeLeafKind K) {
  switch (K) {
#define TYPE_RECORD(EnumName, value, name)                                     \
  case EnumName:                                                               \
    return #EnumName;
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"
  default:
    llvm_unreachable("Unknown type leaf kind!");
  }
  return "";
}

std::string llvm::pdb::formatSegmentOffset(uint16_t Segment, uint32_t Offset) {
  return formatv("{0:4}:{1:4}", Segment, Offset);
}

#define PUSH_CHARACTERISTIC_FLAG(Enum, TheOpt, Value, Style, Descriptive)      \
  PUSH_FLAG(Enum, TheOpt, Value,                                               \
            ((Style == CharacteristicStyle::HeaderDefinition) ? #TheOpt        \
                                                              : Descriptive))

#define PUSH_MASKED_CHARACTERISTIC_FLAG(Enum, Mask, TheOpt, Value, Style,      \
                                        Descriptive)                           \
  PUSH_MASKED_FLAG(Enum, Mask, TheOpt, Value,                                  \
                   ((Style == CharacteristicStyle::HeaderDefinition)           \
                        ? #TheOpt                                              \
                        : Descriptive))

std::string llvm::pdb::formatSectionCharacteristics(uint32_t IndentLevel,
                                                    uint32_t C,
                                                    uint32_t FlagsPerLine,
                                                    StringRef Separator,
                                                    CharacteristicStyle Style) {
  using SC = COFF::SectionCharacteristics;
  std::vector<std::string> Opts;
  if (C == COFF::SC_Invalid)
    return "invalid";
  if (C == 0)
    return "none";
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_TYPE_NOLOAD, C, Style, "noload");
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_TYPE_NO_PAD, C, Style, "no padding");
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_CNT_CODE, C, Style, "code");
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_CNT_INITIALIZED_DATA, C, Style,
                           "initialized data");
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_CNT_UNINITIALIZED_DATA, C, Style,
                           "uninitialized data");
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_LNK_OTHER, C, Style, "other");
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_LNK_INFO, C, Style, "info");
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_LNK_REMOVE, C, Style, "remove");
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_LNK_COMDAT, C, Style, "comdat");
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_GPREL, C, Style, "gp rel");
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_MEM_PURGEABLE, C, Style, "purgeable");
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_MEM_16BIT, C, Style, "16-bit");
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_MEM_LOCKED, C, Style, "locked");
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_MEM_PRELOAD, C, Style, "preload");
  PUSH_MASKED_CHARACTERISTIC_FLAG(SC, 0xF00000, IMAGE_SCN_ALIGN_1BYTES, C,
                                  Style, "1 byte align");
  PUSH_MASKED_CHARACTERISTIC_FLAG(SC, 0xF00000, IMAGE_SCN_ALIGN_2BYTES, C,
                                  Style, "2 byte align");
  PUSH_MASKED_CHARACTERISTIC_FLAG(SC, 0xF00000, IMAGE_SCN_ALIGN_4BYTES, C,
                                  Style, "4 byte align");
  PUSH_MASKED_CHARACTERISTIC_FLAG(SC, 0xF00000, IMAGE_SCN_ALIGN_8BYTES, C,
                                  Style, "8 byte align");
  PUSH_MASKED_CHARACTERISTIC_FLAG(SC, 0xF00000, IMAGE_SCN_ALIGN_16BYTES, C,
                                  Style, "16 byte align");
  PUSH_MASKED_CHARACTERISTIC_FLAG(SC, 0xF00000, IMAGE_SCN_ALIGN_32BYTES, C,
                                  Style, "32 byte align");
  PUSH_MASKED_CHARACTERISTIC_FLAG(SC, 0xF00000, IMAGE_SCN_ALIGN_64BYTES, C,
                                  Style, "64 byte align");
  PUSH_MASKED_CHARACTERISTIC_FLAG(SC, 0xF00000, IMAGE_SCN_ALIGN_128BYTES, C,
                                  Style, "128 byte align");
  PUSH_MASKED_CHARACTERISTIC_FLAG(SC, 0xF00000, IMAGE_SCN_ALIGN_256BYTES, C,
                                  Style, "256 byte align");
  PUSH_MASKED_CHARACTERISTIC_FLAG(SC, 0xF00000, IMAGE_SCN_ALIGN_512BYTES, C,
                                  Style, "512 byte align");
  PUSH_MASKED_CHARACTERISTIC_FLAG(SC, 0xF00000, IMAGE_SCN_ALIGN_1024BYTES, C,
                                  Style, "1024 byte align");
  PUSH_MASKED_CHARACTERISTIC_FLAG(SC, 0xF00000, IMAGE_SCN_ALIGN_2048BYTES, C,
                                  Style, "2048 byte align");
  PUSH_MASKED_CHARACTERISTIC_FLAG(SC, 0xF00000, IMAGE_SCN_ALIGN_4096BYTES, C,
                                  Style, "4096 byte align");
  PUSH_MASKED_CHARACTERISTIC_FLAG(SC, 0xF00000, IMAGE_SCN_ALIGN_8192BYTES, C,
                                  Style, "8192 byte align");
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_LNK_NRELOC_OVFL, C, Style,
                           "noreloc overflow");
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_MEM_DISCARDABLE, C, Style,
                           "discardable");
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_MEM_NOT_CACHED, C, Style,
                           "not cached");
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_MEM_NOT_PAGED, C, Style, "not paged");
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_MEM_SHARED, C, Style, "shared");
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_MEM_EXECUTE, C, Style,
                           "execute permissions");
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_MEM_READ, C, Style,
                           "read permissions");
  PUSH_CHARACTERISTIC_FLAG(SC, IMAGE_SCN_MEM_WRITE, C, Style,
                           "write permissions");
  return typesetItemList(Opts, IndentLevel, FlagsPerLine, Separator);
}
