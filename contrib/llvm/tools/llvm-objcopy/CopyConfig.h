//===- CopyConfig.h -------------------------------------------------------===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_OBJCOPY_COPY_CONFIG_H
#define LLVM_TOOLS_LLVM_OBJCOPY_COPY_CONFIG_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
// Necessary for llvm::DebugCompressionType::None
#include "llvm/Target/TargetOptions.h"
#include <string>
#include <vector>

namespace llvm {
namespace objcopy {

// This type keeps track of the machine info for various architectures. This
// lets us map architecture names to ELF types and the e_machine value of the
// ELF file.
struct MachineInfo {
  uint16_t EMachine;
  bool Is64Bit;
  bool IsLittleEndian;
};

struct SectionRename {
  StringRef OriginalName;
  StringRef NewName;
  Optional<uint64_t> NewFlags;
};

// Configuration for copying/stripping a single file.
struct CopyConfig {
  // Main input/output options
  StringRef InputFilename;
  StringRef InputFormat;
  StringRef OutputFilename;
  StringRef OutputFormat;

  // Only applicable for --input-format=binary
  MachineInfo BinaryArch;
  // Only applicable when --output-format!=binary (e.g. elf64-x86-64).
  Optional<MachineInfo> OutputArch;

  // Advanced options
  StringRef AddGnuDebugLink;
  StringRef BuildIdLinkDir;
  Optional<StringRef> BuildIdLinkInput;
  Optional<StringRef> BuildIdLinkOutput;
  StringRef SplitDWO;
  StringRef SymbolsPrefix;

  // Repeated options
  std::vector<StringRef> AddSection;
  std::vector<StringRef> DumpSection;
  std::vector<StringRef> KeepSection;
  std::vector<StringRef> OnlySection;
  std::vector<StringRef> SymbolsToGlobalize;
  std::vector<StringRef> SymbolsToKeep;
  std::vector<StringRef> SymbolsToLocalize;
  std::vector<StringRef> SymbolsToRemove;
  std::vector<StringRef> SymbolsToWeaken;
  std::vector<StringRef> ToRemove;
  std::vector<std::string> SymbolsToKeepGlobal;

  // Map options
  StringMap<SectionRename> SectionsToRename;
  StringMap<StringRef> SymbolsToRename;

  // Boolean options
  bool DeterministicArchives = true;
  bool DiscardAll = false;
  bool ExtractDWO = false;
  bool KeepFileSymbols = false;
  bool LocalizeHidden = false;
  bool OnlyKeepDebug = false;
  bool PreserveDates = false;
  bool StripAll = false;
  bool StripAllGNU = false;
  bool StripDWO = false;
  bool StripDebug = false;
  bool StripNonAlloc = false;
  bool StripSections = false;
  bool StripUnneeded = false;
  bool Weaken = false;
  bool DecompressDebugSections = false;
  DebugCompressionType CompressionType = DebugCompressionType::None;
};

// Configuration for the overall invocation of this tool. When invoked as
// objcopy, will always contain exactly one CopyConfig. When invoked as strip,
// will contain one or more CopyConfigs.
struct DriverConfig {
  SmallVector<CopyConfig, 1> CopyConfigs;
};

// ParseObjcopyOptions returns the config and sets the input arguments. If a
// help flag is set then ParseObjcopyOptions will print the help messege and
// exit.
DriverConfig parseObjcopyOptions(ArrayRef<const char *> ArgsArr);

// ParseStripOptions returns the config and sets the input arguments. If a
// help flag is set then ParseStripOptions will print the help messege and
// exit.
DriverConfig parseStripOptions(ArrayRef<const char *> ArgsArr);

} // namespace objcopy
} // namespace llvm

#endif
