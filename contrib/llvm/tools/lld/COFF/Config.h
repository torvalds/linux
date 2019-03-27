//===- Config.h -------------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COFF_CONFIG_H
#define LLD_COFF_CONFIG_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/CachePruning.h"
#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace lld {
namespace coff {

using llvm::COFF::IMAGE_FILE_MACHINE_UNKNOWN;
using llvm::COFF::WindowsSubsystem;
using llvm::StringRef;
class DefinedAbsolute;
class DefinedRelative;
class StringChunk;
class Symbol;

// Short aliases.
static const auto AMD64 = llvm::COFF::IMAGE_FILE_MACHINE_AMD64;
static const auto ARM64 = llvm::COFF::IMAGE_FILE_MACHINE_ARM64;
static const auto ARMNT = llvm::COFF::IMAGE_FILE_MACHINE_ARMNT;
static const auto I386 = llvm::COFF::IMAGE_FILE_MACHINE_I386;

// Represents an /export option.
struct Export {
  StringRef Name;       // N in /export:N or /export:E=N
  StringRef ExtName;    // E in /export:E=N
  Symbol *Sym = nullptr;
  uint16_t Ordinal = 0;
  bool Noname = false;
  bool Data = false;
  bool Private = false;
  bool Constant = false;

  // If an export is a form of /export:foo=dllname.bar, that means
  // that foo should be exported as an alias to bar in the DLL.
  // ForwardTo is set to "dllname.bar" part. Usually empty.
  StringRef ForwardTo;
  StringChunk *ForwardChunk = nullptr;

  // True if this /export option was in .drectves section.
  bool Directives = false;
  StringRef SymbolName;
  StringRef ExportName; // Name in DLL

  bool operator==(const Export &E) {
    return (Name == E.Name && ExtName == E.ExtName &&
            Ordinal == E.Ordinal && Noname == E.Noname &&
            Data == E.Data && Private == E.Private);
  }
};

enum class DebugType {
  None  = 0x0,
  CV    = 0x1,  /// CodeView
  PData = 0x2,  /// Procedure Data
  Fixup = 0x4,  /// Relocation Table
};

enum class GuardCFLevel {
  Off,
  NoLongJmp, // Emit gfids but no longjmp tables
  Full,      // Enable all protections.
};

// Global configuration.
struct Configuration {
  enum ManifestKind { SideBySide, Embed, No };
  bool is64() { return Machine == AMD64 || Machine == ARM64; }

  llvm::COFF::MachineTypes Machine = IMAGE_FILE_MACHINE_UNKNOWN;
  size_t Wordsize;
  bool Verbose = false;
  WindowsSubsystem Subsystem = llvm::COFF::IMAGE_SUBSYSTEM_UNKNOWN;
  Symbol *Entry = nullptr;
  bool NoEntry = false;
  std::string OutputFile;
  std::string ImportName;
  bool DoGC = true;
  bool DoICF = true;
  bool TailMerge;
  bool Relocatable = true;
  bool ForceMultiple = false;
  bool ForceUnresolved = false;
  bool Debug = false;
  bool DebugDwarf = false;
  bool DebugGHashes = false;
  bool DebugSymtab = false;
  bool ShowTiming = false;
  unsigned DebugTypes = static_cast<unsigned>(DebugType::None);
  std::vector<std::string> NatvisFiles;
  llvm::SmallString<128> PDBAltPath;
  llvm::SmallString<128> PDBPath;
  llvm::SmallString<128> PDBSourcePath;
  std::vector<llvm::StringRef> Argv;

  // Symbols in this set are considered as live by the garbage collector.
  std::vector<Symbol *> GCRoot;

  std::set<StringRef> NoDefaultLibs;
  bool NoDefaultLibAll = false;

  // True if we are creating a DLL.
  bool DLL = false;
  StringRef Implib;
  std::vector<Export> Exports;
  std::set<std::string> DelayLoads;
  std::map<std::string, int> DLLOrder;
  Symbol *DelayLoadHelper = nullptr;

  bool SaveTemps = false;

  // /guard:cf
  GuardCFLevel GuardCF = GuardCFLevel::Off;

  // Used for SafeSEH.
  Symbol *SEHTable = nullptr;
  Symbol *SEHCount = nullptr;

  // Used for /opt:lldlto=N
  unsigned LTOO = 2;

  // Used for /opt:lldltojobs=N
  unsigned ThinLTOJobs = 0;
  // Used for /opt:lldltopartitions=N
  unsigned LTOPartitions = 1;

  // Used for /opt:lldltocache=path
  StringRef LTOCache;
  // Used for /opt:lldltocachepolicy=policy
  llvm::CachePruningPolicy LTOCachePolicy;

  // Used for /merge:from=to (e.g. /merge:.rdata=.text)
  std::map<StringRef, StringRef> Merge;

  // Used for /section=.name,{DEKPRSW} to set section attributes.
  std::map<StringRef, uint32_t> Section;

  // Options for manifest files.
  ManifestKind Manifest = No;
  int ManifestID = 1;
  StringRef ManifestDependency;
  bool ManifestUAC = true;
  std::vector<std::string> ManifestInput;
  StringRef ManifestLevel = "'asInvoker'";
  StringRef ManifestUIAccess = "'false'";
  StringRef ManifestFile;

  // Used for /aligncomm.
  std::map<std::string, int> AlignComm;

  // Used for /failifmismatch.
  std::map<StringRef, StringRef> MustMatch;

  // Used for /alternatename.
  std::map<StringRef, StringRef> AlternateNames;

  // Used for /order.
  llvm::StringMap<int> Order;

  // Used for /lldmap.
  std::string MapFile;

  uint64_t ImageBase = -1;
  uint64_t StackReserve = 1024 * 1024;
  uint64_t StackCommit = 4096;
  uint64_t HeapReserve = 1024 * 1024;
  uint64_t HeapCommit = 4096;
  uint32_t MajorImageVersion = 0;
  uint32_t MinorImageVersion = 0;
  uint32_t MajorOSVersion = 6;
  uint32_t MinorOSVersion = 0;
  uint32_t Timestamp = 0;
  bool DynamicBase = true;
  bool AllowBind = true;
  bool NxCompat = true;
  bool AllowIsolation = true;
  bool TerminalServerAware = true;
  bool LargeAddressAware = false;
  bool HighEntropyVA = false;
  bool AppContainer = false;
  bool MinGW = false;
  bool WarnMissingOrderSymbol = true;
  bool WarnLocallyDefinedImported = true;
  bool WarnDebugInfoUnusable = true;
  bool Incremental = true;
  bool IntegrityCheck = false;
  bool KillAt = false;
  bool Repro = false;
};

extern Configuration *Config;

} // namespace coff
} // namespace lld

#endif
