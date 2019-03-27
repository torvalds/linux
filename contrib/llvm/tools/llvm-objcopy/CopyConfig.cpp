//===- CopyConfig.cpp -----------------------------------------------------===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CopyConfig.h"
#include "llvm-objcopy.h"

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>
#include <string>

namespace llvm {
namespace objcopy {

namespace {
enum ObjcopyID {
  OBJCOPY_INVALID = 0, // This is not an option ID.
#define OPTION(PREFIX, NAME, ID, KIND, GROUP, ALIAS, ALIASARGS, FLAGS, PARAM,  \
               HELPTEXT, METAVAR, VALUES)                                      \
  OBJCOPY_##ID,
#include "ObjcopyOpts.inc"
#undef OPTION
};

#define PREFIX(NAME, VALUE) const char *const OBJCOPY_##NAME[] = VALUE;
#include "ObjcopyOpts.inc"
#undef PREFIX

static const opt::OptTable::Info ObjcopyInfoTable[] = {
#define OPTION(PREFIX, NAME, ID, KIND, GROUP, ALIAS, ALIASARGS, FLAGS, PARAM,  \
               HELPTEXT, METAVAR, VALUES)                                      \
  {OBJCOPY_##PREFIX,                                                           \
   NAME,                                                                       \
   HELPTEXT,                                                                   \
   METAVAR,                                                                    \
   OBJCOPY_##ID,                                                               \
   opt::Option::KIND##Class,                                                   \
   PARAM,                                                                      \
   FLAGS,                                                                      \
   OBJCOPY_##GROUP,                                                            \
   OBJCOPY_##ALIAS,                                                            \
   ALIASARGS,                                                                  \
   VALUES},
#include "ObjcopyOpts.inc"
#undef OPTION
};

class ObjcopyOptTable : public opt::OptTable {
public:
  ObjcopyOptTable() : OptTable(ObjcopyInfoTable) {}
};

enum StripID {
  STRIP_INVALID = 0, // This is not an option ID.
#define OPTION(PREFIX, NAME, ID, KIND, GROUP, ALIAS, ALIASARGS, FLAGS, PARAM,  \
               HELPTEXT, METAVAR, VALUES)                                      \
  STRIP_##ID,
#include "StripOpts.inc"
#undef OPTION
};

#define PREFIX(NAME, VALUE) const char *const STRIP_##NAME[] = VALUE;
#include "StripOpts.inc"
#undef PREFIX

static const opt::OptTable::Info StripInfoTable[] = {
#define OPTION(PREFIX, NAME, ID, KIND, GROUP, ALIAS, ALIASARGS, FLAGS, PARAM,  \
               HELPTEXT, METAVAR, VALUES)                                      \
  {STRIP_##PREFIX, NAME,       HELPTEXT,                                       \
   METAVAR,        STRIP_##ID, opt::Option::KIND##Class,                       \
   PARAM,          FLAGS,      STRIP_##GROUP,                                  \
   STRIP_##ALIAS,  ALIASARGS,  VALUES},
#include "StripOpts.inc"
#undef OPTION
};

class StripOptTable : public opt::OptTable {
public:
  StripOptTable() : OptTable(StripInfoTable) {}
};

enum SectionFlag {
  SecNone = 0,
  SecAlloc = 1 << 0,
  SecLoad = 1 << 1,
  SecNoload = 1 << 2,
  SecReadonly = 1 << 3,
  SecDebug = 1 << 4,
  SecCode = 1 << 5,
  SecData = 1 << 6,
  SecRom = 1 << 7,
  SecMerge = 1 << 8,
  SecStrings = 1 << 9,
  SecContents = 1 << 10,
  SecShare = 1 << 11,
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ SecShare)
};

} // namespace

static SectionFlag parseSectionRenameFlag(StringRef SectionName) {
  return llvm::StringSwitch<SectionFlag>(SectionName)
      .Case("alloc", SectionFlag::SecAlloc)
      .Case("load", SectionFlag::SecLoad)
      .Case("noload", SectionFlag::SecNoload)
      .Case("readonly", SectionFlag::SecReadonly)
      .Case("debug", SectionFlag::SecDebug)
      .Case("code", SectionFlag::SecCode)
      .Case("data", SectionFlag::SecData)
      .Case("rom", SectionFlag::SecRom)
      .Case("merge", SectionFlag::SecMerge)
      .Case("strings", SectionFlag::SecStrings)
      .Case("contents", SectionFlag::SecContents)
      .Case("share", SectionFlag::SecShare)
      .Default(SectionFlag::SecNone);
}

static SectionRename parseRenameSectionValue(StringRef FlagValue) {
  if (!FlagValue.contains('='))
    error("Bad format for --rename-section: missing '='");

  // Initial split: ".foo" = ".bar,f1,f2,..."
  auto Old2New = FlagValue.split('=');
  SectionRename SR;
  SR.OriginalName = Old2New.first;

  // Flags split: ".bar" "f1" "f2" ...
  SmallVector<StringRef, 6> NameAndFlags;
  Old2New.second.split(NameAndFlags, ',');
  SR.NewName = NameAndFlags[0];

  if (NameAndFlags.size() > 1) {
    SectionFlag Flags = SectionFlag::SecNone;
    for (size_t I = 1, Size = NameAndFlags.size(); I < Size; ++I) {
      SectionFlag Flag = parseSectionRenameFlag(NameAndFlags[I]);
      if (Flag == SectionFlag::SecNone)
        error("Unrecognized section flag '" + NameAndFlags[I] +
              "'. Flags supported for GNU compatibility: alloc, load, noload, "
              "readonly, debug, code, data, rom, share, contents, merge, "
              "strings.");
      Flags |= Flag;
    }

    SR.NewFlags = 0;
    if (Flags & SectionFlag::SecAlloc)
      *SR.NewFlags |= ELF::SHF_ALLOC;
    if (!(Flags & SectionFlag::SecReadonly))
      *SR.NewFlags |= ELF::SHF_WRITE;
    if (Flags & SectionFlag::SecCode)
      *SR.NewFlags |= ELF::SHF_EXECINSTR;
    if (Flags & SectionFlag::SecMerge)
      *SR.NewFlags |= ELF::SHF_MERGE;
    if (Flags & SectionFlag::SecStrings)
      *SR.NewFlags |= ELF::SHF_STRINGS;
  }

  return SR;
}

static const StringMap<MachineInfo> ArchMap{
    // Name, {EMachine, 64bit, LittleEndian}
    {"aarch64", {ELF::EM_AARCH64, true, true}},
    {"arm", {ELF::EM_ARM, false, true}},
    {"i386", {ELF::EM_386, false, true}},
    {"i386:x86-64", {ELF::EM_X86_64, true, true}},
    {"powerpc:common64", {ELF::EM_PPC64, true, true}},
    {"sparc", {ELF::EM_SPARC, false, true}},
    {"x86-64", {ELF::EM_X86_64, true, true}},
};

static const MachineInfo &getMachineInfo(StringRef Arch) {
  auto Iter = ArchMap.find(Arch);
  if (Iter == std::end(ArchMap))
    error("Invalid architecture: '" + Arch + "'");
  return Iter->getValue();
}

static const StringMap<MachineInfo> OutputFormatMap{
    // Name, {EMachine, 64bit, LittleEndian}
    {"elf32-i386", {ELF::EM_386, false, true}},
    {"elf32-powerpcle", {ELF::EM_PPC, false, true}},
    {"elf32-x86-64", {ELF::EM_X86_64, false, true}},
    {"elf64-powerpcle", {ELF::EM_PPC64, true, true}},
    {"elf64-x86-64", {ELF::EM_X86_64, true, true}},
};

static const MachineInfo &getOutputFormatMachineInfo(StringRef Format) {
  auto Iter = OutputFormatMap.find(Format);
  if (Iter == std::end(OutputFormatMap))
    error("Invalid output format: '" + Format + "'");
  return Iter->getValue();
}

static void addGlobalSymbolsFromFile(std::vector<std::string> &Symbols,
                                     StringRef Filename) {
  SmallVector<StringRef, 16> Lines;
  auto BufOrErr = MemoryBuffer::getFile(Filename);
  if (!BufOrErr)
    reportError(Filename, BufOrErr.getError());

  BufOrErr.get()->getBuffer().split(Lines, '\n');
  for (StringRef Line : Lines) {
    // Ignore everything after '#', trim whitespace, and only add the symbol if
    // it's not empty.
    auto TrimmedLine = Line.split('#').first.trim();
    if (!TrimmedLine.empty())
      Symbols.push_back(TrimmedLine.str());
  }
}

// ParseObjcopyOptions returns the config and sets the input arguments. If a
// help flag is set then ParseObjcopyOptions will print the help messege and
// exit.
DriverConfig parseObjcopyOptions(ArrayRef<const char *> ArgsArr) {
  ObjcopyOptTable T;
  unsigned MissingArgumentIndex, MissingArgumentCount;
  llvm::opt::InputArgList InputArgs =
      T.ParseArgs(ArgsArr, MissingArgumentIndex, MissingArgumentCount);

  if (InputArgs.size() == 0) {
    T.PrintHelp(errs(), "llvm-objcopy input [output]", "objcopy tool");
    exit(1);
  }

  if (InputArgs.hasArg(OBJCOPY_help)) {
    T.PrintHelp(outs(), "llvm-objcopy input [output]", "objcopy tool");
    exit(0);
  }

  if (InputArgs.hasArg(OBJCOPY_version)) {
    outs() << "llvm-objcopy, compatible with GNU objcopy\n";
    cl::PrintVersionMessage();
    exit(0);
  }

  SmallVector<const char *, 2> Positional;

  for (auto Arg : InputArgs.filtered(OBJCOPY_UNKNOWN))
    error("unknown argument '" + Arg->getAsString(InputArgs) + "'");

  for (auto Arg : InputArgs.filtered(OBJCOPY_INPUT))
    Positional.push_back(Arg->getValue());

  if (Positional.empty())
    error("No input file specified");

  if (Positional.size() > 2)
    error("Too many positional arguments");

  CopyConfig Config;
  Config.InputFilename = Positional[0];
  Config.OutputFilename = Positional[Positional.size() == 1 ? 0 : 1];
  if (InputArgs.hasArg(OBJCOPY_target) &&
      (InputArgs.hasArg(OBJCOPY_input_target) ||
       InputArgs.hasArg(OBJCOPY_output_target)))
    error("--target cannot be used with --input-target or --output-target");

  if (InputArgs.hasArg(OBJCOPY_target)) {
    Config.InputFormat = InputArgs.getLastArgValue(OBJCOPY_target);
    Config.OutputFormat = InputArgs.getLastArgValue(OBJCOPY_target);
  } else {
    Config.InputFormat = InputArgs.getLastArgValue(OBJCOPY_input_target);
    Config.OutputFormat = InputArgs.getLastArgValue(OBJCOPY_output_target);
  }
  if (Config.InputFormat == "binary") {
    auto BinaryArch = InputArgs.getLastArgValue(OBJCOPY_binary_architecture);
    if (BinaryArch.empty())
      error("Specified binary input without specifiying an architecture");
    Config.BinaryArch = getMachineInfo(BinaryArch);
  }
  if (!Config.OutputFormat.empty() && Config.OutputFormat != "binary")
    Config.OutputArch = getOutputFormatMachineInfo(Config.OutputFormat);

  if (auto Arg = InputArgs.getLastArg(OBJCOPY_compress_debug_sections,
                                      OBJCOPY_compress_debug_sections_eq)) {
    Config.CompressionType = DebugCompressionType::Z;

    if (Arg->getOption().getID() == OBJCOPY_compress_debug_sections_eq) {
      Config.CompressionType =
          StringSwitch<DebugCompressionType>(
              InputArgs.getLastArgValue(OBJCOPY_compress_debug_sections_eq))
              .Case("zlib-gnu", DebugCompressionType::GNU)
              .Case("zlib", DebugCompressionType::Z)
              .Default(DebugCompressionType::None);
      if (Config.CompressionType == DebugCompressionType::None)
        error("Invalid or unsupported --compress-debug-sections format: " +
              InputArgs.getLastArgValue(OBJCOPY_compress_debug_sections_eq));
      if (!zlib::isAvailable())
        error("LLVM was not compiled with LLVM_ENABLE_ZLIB: can not compress.");
    }
  }

  Config.AddGnuDebugLink = InputArgs.getLastArgValue(OBJCOPY_add_gnu_debuglink);
  Config.BuildIdLinkDir = InputArgs.getLastArgValue(OBJCOPY_build_id_link_dir);
  if (InputArgs.hasArg(OBJCOPY_build_id_link_input))
    Config.BuildIdLinkInput =
        InputArgs.getLastArgValue(OBJCOPY_build_id_link_input);
  if (InputArgs.hasArg(OBJCOPY_build_id_link_output))
    Config.BuildIdLinkOutput =
        InputArgs.getLastArgValue(OBJCOPY_build_id_link_output);
  Config.SplitDWO = InputArgs.getLastArgValue(OBJCOPY_split_dwo);
  Config.SymbolsPrefix = InputArgs.getLastArgValue(OBJCOPY_prefix_symbols);

  for (auto Arg : InputArgs.filtered(OBJCOPY_redefine_symbol)) {
    if (!StringRef(Arg->getValue()).contains('='))
      error("Bad format for --redefine-sym");
    auto Old2New = StringRef(Arg->getValue()).split('=');
    if (!Config.SymbolsToRename.insert(Old2New).second)
      error("Multiple redefinition of symbol " + Old2New.first);
  }

  for (auto Arg : InputArgs.filtered(OBJCOPY_rename_section)) {
    SectionRename SR = parseRenameSectionValue(StringRef(Arg->getValue()));
    if (!Config.SectionsToRename.try_emplace(SR.OriginalName, SR).second)
      error("Multiple renames of section " + SR.OriginalName);
  }

  for (auto Arg : InputArgs.filtered(OBJCOPY_remove_section))
    Config.ToRemove.push_back(Arg->getValue());
  for (auto Arg : InputArgs.filtered(OBJCOPY_keep_section))
    Config.KeepSection.push_back(Arg->getValue());
  for (auto Arg : InputArgs.filtered(OBJCOPY_only_section))
    Config.OnlySection.push_back(Arg->getValue());
  for (auto Arg : InputArgs.filtered(OBJCOPY_add_section))
    Config.AddSection.push_back(Arg->getValue());
  for (auto Arg : InputArgs.filtered(OBJCOPY_dump_section))
    Config.DumpSection.push_back(Arg->getValue());
  Config.StripAll = InputArgs.hasArg(OBJCOPY_strip_all);
  Config.StripAllGNU = InputArgs.hasArg(OBJCOPY_strip_all_gnu);
  Config.StripDebug = InputArgs.hasArg(OBJCOPY_strip_debug);
  Config.StripDWO = InputArgs.hasArg(OBJCOPY_strip_dwo);
  Config.StripSections = InputArgs.hasArg(OBJCOPY_strip_sections);
  Config.StripNonAlloc = InputArgs.hasArg(OBJCOPY_strip_non_alloc);
  Config.StripUnneeded = InputArgs.hasArg(OBJCOPY_strip_unneeded);
  Config.ExtractDWO = InputArgs.hasArg(OBJCOPY_extract_dwo);
  Config.LocalizeHidden = InputArgs.hasArg(OBJCOPY_localize_hidden);
  Config.Weaken = InputArgs.hasArg(OBJCOPY_weaken);
  Config.DiscardAll = InputArgs.hasArg(OBJCOPY_discard_all);
  Config.OnlyKeepDebug = InputArgs.hasArg(OBJCOPY_only_keep_debug);
  Config.KeepFileSymbols = InputArgs.hasArg(OBJCOPY_keep_file_symbols);
  Config.DecompressDebugSections =
      InputArgs.hasArg(OBJCOPY_decompress_debug_sections);
  for (auto Arg : InputArgs.filtered(OBJCOPY_localize_symbol))
    Config.SymbolsToLocalize.push_back(Arg->getValue());
  for (auto Arg : InputArgs.filtered(OBJCOPY_keep_global_symbol))
    Config.SymbolsToKeepGlobal.push_back(Arg->getValue());
  for (auto Arg : InputArgs.filtered(OBJCOPY_keep_global_symbols))
    addGlobalSymbolsFromFile(Config.SymbolsToKeepGlobal, Arg->getValue());
  for (auto Arg : InputArgs.filtered(OBJCOPY_globalize_symbol))
    Config.SymbolsToGlobalize.push_back(Arg->getValue());
  for (auto Arg : InputArgs.filtered(OBJCOPY_weaken_symbol))
    Config.SymbolsToWeaken.push_back(Arg->getValue());
  for (auto Arg : InputArgs.filtered(OBJCOPY_strip_symbol))
    Config.SymbolsToRemove.push_back(Arg->getValue());
  for (auto Arg : InputArgs.filtered(OBJCOPY_keep_symbol))
    Config.SymbolsToKeep.push_back(Arg->getValue());

  Config.DeterministicArchives = InputArgs.hasFlag(
      OBJCOPY_enable_deterministic_archives,
      OBJCOPY_disable_deterministic_archives, /*default=*/true);

  Config.PreserveDates = InputArgs.hasArg(OBJCOPY_preserve_dates);

  if (Config.DecompressDebugSections &&
      Config.CompressionType != DebugCompressionType::None) {
    error("Cannot specify --compress-debug-sections at the same time as "
          "--decompress-debug-sections at the same time");
  }

  if (Config.DecompressDebugSections && !zlib::isAvailable())
    error("LLVM was not compiled with LLVM_ENABLE_ZLIB: cannot decompress.");

  DriverConfig DC;
  DC.CopyConfigs.push_back(std::move(Config));
  return DC;
}

// ParseStripOptions returns the config and sets the input arguments. If a
// help flag is set then ParseStripOptions will print the help messege and
// exit.
DriverConfig parseStripOptions(ArrayRef<const char *> ArgsArr) {
  StripOptTable T;
  unsigned MissingArgumentIndex, MissingArgumentCount;
  llvm::opt::InputArgList InputArgs =
      T.ParseArgs(ArgsArr, MissingArgumentIndex, MissingArgumentCount);

  if (InputArgs.size() == 0) {
    T.PrintHelp(errs(), "llvm-strip [options] file...", "strip tool");
    exit(1);
  }

  if (InputArgs.hasArg(STRIP_help)) {
    T.PrintHelp(outs(), "llvm-strip [options] file...", "strip tool");
    exit(0);
  }

  if (InputArgs.hasArg(STRIP_version)) {
    outs() << "llvm-strip, compatible with GNU strip\n";
    cl::PrintVersionMessage();
    exit(0);
  }

  SmallVector<const char *, 2> Positional;
  for (auto Arg : InputArgs.filtered(STRIP_UNKNOWN))
    error("unknown argument '" + Arg->getAsString(InputArgs) + "'");
  for (auto Arg : InputArgs.filtered(STRIP_INPUT))
    Positional.push_back(Arg->getValue());

  if (Positional.empty())
    error("No input file specified");

  if (Positional.size() > 1 && InputArgs.hasArg(STRIP_output))
    error("Multiple input files cannot be used in combination with -o");

  CopyConfig Config;
  Config.StripDebug = InputArgs.hasArg(STRIP_strip_debug);

  Config.DiscardAll = InputArgs.hasArg(STRIP_discard_all);
  Config.StripUnneeded = InputArgs.hasArg(STRIP_strip_unneeded);
  Config.StripAll = InputArgs.hasArg(STRIP_strip_all);
  Config.StripAllGNU = InputArgs.hasArg(STRIP_strip_all_gnu);

  if (!Config.StripDebug && !Config.StripUnneeded && !Config.DiscardAll &&
      !Config.StripAllGNU)
    Config.StripAll = true;

  for (auto Arg : InputArgs.filtered(STRIP_keep_section))
    Config.KeepSection.push_back(Arg->getValue());

  for (auto Arg : InputArgs.filtered(STRIP_remove_section))
    Config.ToRemove.push_back(Arg->getValue());

  for (auto Arg : InputArgs.filtered(STRIP_keep_symbol))
    Config.SymbolsToKeep.push_back(Arg->getValue());

  Config.DeterministicArchives =
      InputArgs.hasFlag(STRIP_enable_deterministic_archives,
                        STRIP_disable_deterministic_archives, /*default=*/true);

  Config.PreserveDates = InputArgs.hasArg(STRIP_preserve_dates);

  DriverConfig DC;
  if (Positional.size() == 1) {
    Config.InputFilename = Positional[0];
    Config.OutputFilename =
        InputArgs.getLastArgValue(STRIP_output, Positional[0]);
    DC.CopyConfigs.push_back(std::move(Config));
  } else {
    for (const char *Filename : Positional) {
      Config.InputFilename = Filename;
      Config.OutputFilename = Filename;
      DC.CopyConfigs.push_back(Config);
    }
  }

  return DC;
}

} // namespace objcopy
} // namespace llvm
