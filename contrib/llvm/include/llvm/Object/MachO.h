//===- MachO.h - MachO object file implementation ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the MachOObjectFile class, which implement the ObjectFile
// interface for MachO files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_MACHO_H
#define LLVM_OBJECT_MACHO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <memory>
#include <string>
#include <system_error>

namespace llvm {
namespace object {

/// DiceRef - This is a value type class that represents a single
/// data in code entry in the table in a Mach-O object file.
class DiceRef {
  DataRefImpl DicePimpl;
  const ObjectFile *OwningObject = nullptr;

public:
  DiceRef() = default;
  DiceRef(DataRefImpl DiceP, const ObjectFile *Owner);

  bool operator==(const DiceRef &Other) const;
  bool operator<(const DiceRef &Other) const;

  void moveNext();

  std::error_code getOffset(uint32_t &Result) const;
  std::error_code getLength(uint16_t &Result) const;
  std::error_code getKind(uint16_t &Result) const;

  DataRefImpl getRawDataRefImpl() const;
  const ObjectFile *getObjectFile() const;
};
using dice_iterator = content_iterator<DiceRef>;

/// ExportEntry encapsulates the current-state-of-the-walk used when doing a
/// non-recursive walk of the trie data structure.  This allows you to iterate
/// across all exported symbols using:
///      Error Err;
///      for (const llvm::object::ExportEntry &AnExport : Obj->exports(&Err)) {
///      }
///      if (Err) { report error ...
class ExportEntry {
public:
  ExportEntry(Error *Err, const MachOObjectFile *O, ArrayRef<uint8_t> Trie);

  StringRef name() const;
  uint64_t flags() const;
  uint64_t address() const;
  uint64_t other() const;
  StringRef otherName() const;
  uint32_t nodeOffset() const;

  bool operator==(const ExportEntry &) const;

  void moveNext();

private:
  friend class MachOObjectFile;

  void moveToFirst();
  void moveToEnd();
  uint64_t readULEB128(const uint8_t *&p, const char **error);
  void pushDownUntilBottom();
  void pushNode(uint64_t Offset);

  // Represents a node in the mach-o exports trie.
  struct NodeState {
    NodeState(const uint8_t *Ptr);

    const uint8_t *Start;
    const uint8_t *Current;
    uint64_t Flags = 0;
    uint64_t Address = 0;
    uint64_t Other = 0;
    const char *ImportName = nullptr;
    unsigned ChildCount = 0;
    unsigned NextChildIndex = 0;
    unsigned ParentStringLength = 0;
    bool IsExportNode = false;
  };
  using NodeList = SmallVector<NodeState, 16>;
  using node_iterator = NodeList::const_iterator;

  Error *E;
  const MachOObjectFile *O;
  ArrayRef<uint8_t> Trie;
  SmallString<256> CumulativeString;
  NodeList Stack;
  bool Done = false;

  iterator_range<node_iterator> nodes() const {
    return make_range(Stack.begin(), Stack.end());
  }
};
using export_iterator = content_iterator<ExportEntry>;

// Segment info so SegIndex/SegOffset pairs in a Mach-O Bind or Rebase entry
// can be checked and translated.  Only the SegIndex/SegOffset pairs from
// checked entries are to be used with the segmentName(), sectionName() and
// address() methods below.
class BindRebaseSegInfo {
public:
  BindRebaseSegInfo(const MachOObjectFile *Obj);

  // Used to check a Mach-O Bind or Rebase entry for errors when iterating.
  const char *checkSegAndOffset(int32_t SegIndex, uint64_t SegOffset,
                                bool endInvalid);
  const char *checkCountAndSkip(uint32_t Count, uint32_t Skip,
                                uint8_t PointerSize, int32_t SegIndex,
                                uint64_t SegOffset);
  // Used with valid SegIndex/SegOffset values from checked entries.
  StringRef segmentName(int32_t SegIndex);
  StringRef sectionName(int32_t SegIndex, uint64_t SegOffset);
  uint64_t address(uint32_t SegIndex, uint64_t SegOffset);

private:
  struct SectionInfo {
    uint64_t Address;
    uint64_t Size;
    StringRef SectionName;
    StringRef SegmentName;
    uint64_t OffsetInSegment;
    uint64_t SegmentStartAddress;
    int32_t SegmentIndex;
  };
  const SectionInfo &findSection(int32_t SegIndex, uint64_t SegOffset);

  SmallVector<SectionInfo, 32> Sections;
  int32_t MaxSegIndex;
};

/// MachORebaseEntry encapsulates the current state in the decompression of
/// rebasing opcodes. This allows you to iterate through the compressed table of
/// rebasing using:
///    Error Err;
///    for (const llvm::object::MachORebaseEntry &Entry : Obj->rebaseTable(&Err)) {
///    }
///    if (Err) { report error ...
class MachORebaseEntry {
public:
  MachORebaseEntry(Error *Err, const MachOObjectFile *O,
                   ArrayRef<uint8_t> opcodes, bool is64Bit);

  int32_t segmentIndex() const;
  uint64_t segmentOffset() const;
  StringRef typeName() const;
  StringRef segmentName() const;
  StringRef sectionName() const;
  uint64_t address() const;

  bool operator==(const MachORebaseEntry &) const;

  void moveNext();

private:
  friend class MachOObjectFile;

  void moveToFirst();
  void moveToEnd();
  uint64_t readULEB128(const char **error);

  Error *E;
  const MachOObjectFile *O;
  ArrayRef<uint8_t> Opcodes;
  const uint8_t *Ptr;
  uint64_t SegmentOffset = 0;
  int32_t SegmentIndex = -1;
  uint64_t RemainingLoopCount = 0;
  uint64_t AdvanceAmount = 0;
  uint8_t  RebaseType = 0;
  uint8_t  PointerSize;
  bool     Done = false;
};
using rebase_iterator = content_iterator<MachORebaseEntry>;

/// MachOBindEntry encapsulates the current state in the decompression of
/// binding opcodes. This allows you to iterate through the compressed table of
/// bindings using:
///    Error Err;
///    for (const llvm::object::MachOBindEntry &Entry : Obj->bindTable(&Err)) {
///    }
///    if (Err) { report error ...
class MachOBindEntry {
public:
  enum class Kind { Regular, Lazy, Weak };

  MachOBindEntry(Error *Err, const MachOObjectFile *O,
                 ArrayRef<uint8_t> Opcodes, bool is64Bit, MachOBindEntry::Kind);

  int32_t segmentIndex() const;
  uint64_t segmentOffset() const;
  StringRef typeName() const;
  StringRef symbolName() const;
  uint32_t flags() const;
  int64_t addend() const;
  int ordinal() const;

  StringRef segmentName() const;
  StringRef sectionName() const;
  uint64_t address() const;

  bool operator==(const MachOBindEntry &) const;

  void moveNext();

private:
  friend class MachOObjectFile;

  void moveToFirst();
  void moveToEnd();
  uint64_t readULEB128(const char **error);
  int64_t readSLEB128(const char **error);

  Error *E;
  const MachOObjectFile *O;
  ArrayRef<uint8_t> Opcodes;
  const uint8_t *Ptr;
  uint64_t SegmentOffset = 0;
  int32_t  SegmentIndex = -1;
  StringRef SymbolName;
  bool     LibraryOrdinalSet = false;
  int      Ordinal = 0;
  uint32_t Flags = 0;
  int64_t  Addend = 0;
  uint64_t RemainingLoopCount = 0;
  uint64_t AdvanceAmount = 0;
  uint8_t  BindType = 0;
  uint8_t  PointerSize;
  Kind     TableKind;
  bool     Done = false;
};
using bind_iterator = content_iterator<MachOBindEntry>;

class MachOObjectFile : public ObjectFile {
public:
  struct LoadCommandInfo {
    const char *Ptr;      // Where in memory the load command is.
    MachO::load_command C; // The command itself.
  };
  using LoadCommandList = SmallVector<LoadCommandInfo, 4>;
  using load_command_iterator = LoadCommandList::const_iterator;

  static Expected<std::unique_ptr<MachOObjectFile>>
  create(MemoryBufferRef Object, bool IsLittleEndian, bool Is64Bits,
         uint32_t UniversalCputype = 0, uint32_t UniversalIndex = 0);

  void moveSymbolNext(DataRefImpl &Symb) const override;

  uint64_t getNValue(DataRefImpl Sym) const;
  Expected<StringRef> getSymbolName(DataRefImpl Symb) const override;

  // MachO specific.
  Error checkSymbolTable() const;

  std::error_code getIndirectName(DataRefImpl Symb, StringRef &Res) const;
  unsigned getSectionType(SectionRef Sec) const;

  Expected<uint64_t> getSymbolAddress(DataRefImpl Symb) const override;
  uint32_t getSymbolAlignment(DataRefImpl Symb) const override;
  uint64_t getCommonSymbolSizeImpl(DataRefImpl Symb) const override;
  Expected<SymbolRef::Type> getSymbolType(DataRefImpl Symb) const override;
  uint32_t getSymbolFlags(DataRefImpl Symb) const override;
  Expected<section_iterator> getSymbolSection(DataRefImpl Symb) const override;
  unsigned getSymbolSectionID(SymbolRef Symb) const;
  unsigned getSectionID(SectionRef Sec) const;

  void moveSectionNext(DataRefImpl &Sec) const override;
  std::error_code getSectionName(DataRefImpl Sec,
                                 StringRef &Res) const override;
  uint64_t getSectionAddress(DataRefImpl Sec) const override;
  uint64_t getSectionIndex(DataRefImpl Sec) const override;
  uint64_t getSectionSize(DataRefImpl Sec) const override;
  std::error_code getSectionContents(DataRefImpl Sec,
                                     StringRef &Res) const override;
  uint64_t getSectionAlignment(DataRefImpl Sec) const override;
  Expected<SectionRef> getSection(unsigned SectionIndex) const;
  Expected<SectionRef> getSection(StringRef SectionName) const;
  bool isSectionCompressed(DataRefImpl Sec) const override;
  bool isSectionText(DataRefImpl Sec) const override;
  bool isSectionData(DataRefImpl Sec) const override;
  bool isSectionBSS(DataRefImpl Sec) const override;
  bool isSectionVirtual(DataRefImpl Sec) const override;
  bool isSectionBitcode(DataRefImpl Sec) const override;

  /// When dsymutil generates the companion file, it strips all unnecessary
  /// sections (e.g. everything in the _TEXT segment) by omitting their body
  /// and setting the offset in their corresponding load command to zero.
  ///
  /// While the load command itself is valid, reading the section corresponds
  /// to reading the number of bytes specified in the load command, starting
  /// from offset 0 (i.e. the Mach-O header at the beginning of the file).
  bool isSectionStripped(DataRefImpl Sec) const override;

  relocation_iterator section_rel_begin(DataRefImpl Sec) const override;
  relocation_iterator section_rel_end(DataRefImpl Sec) const override;

  relocation_iterator extrel_begin() const;
  relocation_iterator extrel_end() const;
  iterator_range<relocation_iterator> external_relocations() const {
    return make_range(extrel_begin(), extrel_end());
  }

  relocation_iterator locrel_begin() const;
  relocation_iterator locrel_end() const;

  void moveRelocationNext(DataRefImpl &Rel) const override;
  uint64_t getRelocationOffset(DataRefImpl Rel) const override;
  symbol_iterator getRelocationSymbol(DataRefImpl Rel) const override;
  section_iterator getRelocationSection(DataRefImpl Rel) const;
  uint64_t getRelocationType(DataRefImpl Rel) const override;
  void getRelocationTypeName(DataRefImpl Rel,
                             SmallVectorImpl<char> &Result) const override;
  uint8_t getRelocationLength(DataRefImpl Rel) const;

  // MachO specific.
  std::error_code getLibraryShortNameByIndex(unsigned Index, StringRef &) const;
  uint32_t getLibraryCount() const;

  section_iterator getRelocationRelocatedSection(relocation_iterator Rel) const;

  // TODO: Would be useful to have an iterator based version
  // of the load command interface too.

  basic_symbol_iterator symbol_begin() const override;
  basic_symbol_iterator symbol_end() const override;

  // MachO specific.
  symbol_iterator getSymbolByIndex(unsigned Index) const;
  uint64_t getSymbolIndex(DataRefImpl Symb) const;

  section_iterator section_begin() const override;
  section_iterator section_end() const override;

  uint8_t getBytesInAddress() const override;

  StringRef getFileFormatName() const override;
  Triple::ArchType getArch() const override;
  SubtargetFeatures getFeatures() const override { return SubtargetFeatures(); }
  Triple getArchTriple(const char **McpuDefault = nullptr) const;

  relocation_iterator section_rel_begin(unsigned Index) const;
  relocation_iterator section_rel_end(unsigned Index) const;

  dice_iterator begin_dices() const;
  dice_iterator end_dices() const;

  load_command_iterator begin_load_commands() const;
  load_command_iterator end_load_commands() const;
  iterator_range<load_command_iterator> load_commands() const;

  /// For use iterating over all exported symbols.
  iterator_range<export_iterator> exports(Error &Err) const;

  /// For use examining a trie not in a MachOObjectFile.
  static iterator_range<export_iterator> exports(Error &Err,
                                                 ArrayRef<uint8_t> Trie,
                                                 const MachOObjectFile *O =
                                                                      nullptr);

  /// For use iterating over all rebase table entries.
  iterator_range<rebase_iterator> rebaseTable(Error &Err);

  /// For use examining rebase opcodes in a MachOObjectFile.
  static iterator_range<rebase_iterator> rebaseTable(Error &Err,
                                                     MachOObjectFile *O,
                                                     ArrayRef<uint8_t> Opcodes,
                                                     bool is64);

  /// For use iterating over all bind table entries.
  iterator_range<bind_iterator> bindTable(Error &Err);

  /// For use iterating over all lazy bind table entries.
  iterator_range<bind_iterator> lazyBindTable(Error &Err);

  /// For use iterating over all weak bind table entries.
  iterator_range<bind_iterator> weakBindTable(Error &Err);

  /// For use examining bind opcodes in a MachOObjectFile.
  static iterator_range<bind_iterator> bindTable(Error &Err,
                                                 MachOObjectFile *O,
                                                 ArrayRef<uint8_t> Opcodes,
                                                 bool is64,
                                                 MachOBindEntry::Kind);

  /// For use with a SegIndex,SegOffset pair in MachOBindEntry::moveNext() to
  /// validate a MachOBindEntry.
  const char *BindEntryCheckSegAndOffset(int32_t SegIndex, uint64_t SegOffset,
                                         bool endInvalid) const {
    return BindRebaseSectionTable->checkSegAndOffset(SegIndex, SegOffset,
                                                     endInvalid);
  }
  /// For use in MachOBindEntry::moveNext() to validate a MachOBindEntry for
  /// the BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB opcode.
  const char *BindEntryCheckCountAndSkip(uint32_t Count, uint32_t Skip,
                                         uint8_t PointerSize, int32_t SegIndex,
                                         uint64_t SegOffset) const {
    return BindRebaseSectionTable->checkCountAndSkip(Count, Skip, PointerSize,
                                                     SegIndex, SegOffset);
  }

  /// For use with a SegIndex,SegOffset pair in MachORebaseEntry::moveNext() to
  /// validate a MachORebaseEntry.
  const char *RebaseEntryCheckSegAndOffset(int32_t SegIndex, uint64_t SegOffset,
                                           bool endInvalid) const {
    return BindRebaseSectionTable->checkSegAndOffset(SegIndex, SegOffset,
                                                     endInvalid);
  }
  /// For use in MachORebaseEntry::moveNext() to validate a MachORebaseEntry for
  /// the REBASE_OPCODE_DO_*_TIMES* opcodes.
  const char *RebaseEntryCheckCountAndSkip(uint32_t Count, uint32_t Skip,
                                         uint8_t PointerSize, int32_t SegIndex,
                                         uint64_t SegOffset) const {
    return BindRebaseSectionTable->checkCountAndSkip(Count, Skip, PointerSize,
                                                     SegIndex, SegOffset);
  }

  /// For use with the SegIndex of a checked Mach-O Bind or Rebase entry to
  /// get the segment name.
  StringRef BindRebaseSegmentName(int32_t SegIndex) const {
    return BindRebaseSectionTable->segmentName(SegIndex);
  }

  /// For use with a SegIndex,SegOffset pair from a checked Mach-O Bind or
  /// Rebase entry to get the section name.
  StringRef BindRebaseSectionName(uint32_t SegIndex, uint64_t SegOffset) const {
    return BindRebaseSectionTable->sectionName(SegIndex, SegOffset);
  }

  /// For use with a SegIndex,SegOffset pair from a checked Mach-O Bind or
  /// Rebase entry to get the address.
  uint64_t BindRebaseAddress(uint32_t SegIndex, uint64_t SegOffset) const {
    return BindRebaseSectionTable->address(SegIndex, SegOffset);
  }

  // In a MachO file, sections have a segment name. This is used in the .o
  // files. They have a single segment, but this field specifies which segment
  // a section should be put in the final object.
  StringRef getSectionFinalSegmentName(DataRefImpl Sec) const;

  // Names are stored as 16 bytes. These returns the raw 16 bytes without
  // interpreting them as a C string.
  ArrayRef<char> getSectionRawName(DataRefImpl Sec) const;
  ArrayRef<char> getSectionRawFinalSegmentName(DataRefImpl Sec) const;

  // MachO specific Info about relocations.
  bool isRelocationScattered(const MachO::any_relocation_info &RE) const;
  unsigned getPlainRelocationSymbolNum(
                                    const MachO::any_relocation_info &RE) const;
  bool getPlainRelocationExternal(const MachO::any_relocation_info &RE) const;
  bool getScatteredRelocationScattered(
                                    const MachO::any_relocation_info &RE) const;
  uint32_t getScatteredRelocationValue(
                                    const MachO::any_relocation_info &RE) const;
  uint32_t getScatteredRelocationType(
                                    const MachO::any_relocation_info &RE) const;
  unsigned getAnyRelocationAddress(const MachO::any_relocation_info &RE) const;
  unsigned getAnyRelocationPCRel(const MachO::any_relocation_info &RE) const;
  unsigned getAnyRelocationLength(const MachO::any_relocation_info &RE) const;
  unsigned getAnyRelocationType(const MachO::any_relocation_info &RE) const;
  SectionRef getAnyRelocationSection(const MachO::any_relocation_info &RE) const;

  // MachO specific structures.
  MachO::section getSection(DataRefImpl DRI) const;
  MachO::section_64 getSection64(DataRefImpl DRI) const;
  MachO::section getSection(const LoadCommandInfo &L, unsigned Index) const;
  MachO::section_64 getSection64(const LoadCommandInfo &L,unsigned Index) const;
  MachO::nlist getSymbolTableEntry(DataRefImpl DRI) const;
  MachO::nlist_64 getSymbol64TableEntry(DataRefImpl DRI) const;

  MachO::linkedit_data_command
  getLinkeditDataLoadCommand(const LoadCommandInfo &L) const;
  MachO::segment_command
  getSegmentLoadCommand(const LoadCommandInfo &L) const;
  MachO::segment_command_64
  getSegment64LoadCommand(const LoadCommandInfo &L) const;
  MachO::linker_option_command
  getLinkerOptionLoadCommand(const LoadCommandInfo &L) const;
  MachO::version_min_command
  getVersionMinLoadCommand(const LoadCommandInfo &L) const;
  MachO::note_command
  getNoteLoadCommand(const LoadCommandInfo &L) const;
  MachO::build_version_command
  getBuildVersionLoadCommand(const LoadCommandInfo &L) const;
  MachO::build_tool_version
  getBuildToolVersion(unsigned index) const;
  MachO::dylib_command
  getDylibIDLoadCommand(const LoadCommandInfo &L) const;
  MachO::dyld_info_command
  getDyldInfoLoadCommand(const LoadCommandInfo &L) const;
  MachO::dylinker_command
  getDylinkerCommand(const LoadCommandInfo &L) const;
  MachO::uuid_command
  getUuidCommand(const LoadCommandInfo &L) const;
  MachO::rpath_command
  getRpathCommand(const LoadCommandInfo &L) const;
  MachO::source_version_command
  getSourceVersionCommand(const LoadCommandInfo &L) const;
  MachO::entry_point_command
  getEntryPointCommand(const LoadCommandInfo &L) const;
  MachO::encryption_info_command
  getEncryptionInfoCommand(const LoadCommandInfo &L) const;
  MachO::encryption_info_command_64
  getEncryptionInfoCommand64(const LoadCommandInfo &L) const;
  MachO::sub_framework_command
  getSubFrameworkCommand(const LoadCommandInfo &L) const;
  MachO::sub_umbrella_command
  getSubUmbrellaCommand(const LoadCommandInfo &L) const;
  MachO::sub_library_command
  getSubLibraryCommand(const LoadCommandInfo &L) const;
  MachO::sub_client_command
  getSubClientCommand(const LoadCommandInfo &L) const;
  MachO::routines_command
  getRoutinesCommand(const LoadCommandInfo &L) const;
  MachO::routines_command_64
  getRoutinesCommand64(const LoadCommandInfo &L) const;
  MachO::thread_command
  getThreadCommand(const LoadCommandInfo &L) const;

  MachO::any_relocation_info getRelocation(DataRefImpl Rel) const;
  MachO::data_in_code_entry getDice(DataRefImpl Rel) const;
  const MachO::mach_header &getHeader() const;
  const MachO::mach_header_64 &getHeader64() const;
  uint32_t
  getIndirectSymbolTableEntry(const MachO::dysymtab_command &DLC,
                              unsigned Index) const;
  MachO::data_in_code_entry getDataInCodeTableEntry(uint32_t DataOffset,
                                                    unsigned Index) const;
  MachO::symtab_command getSymtabLoadCommand() const;
  MachO::dysymtab_command getDysymtabLoadCommand() const;
  MachO::linkedit_data_command getDataInCodeLoadCommand() const;
  MachO::linkedit_data_command getLinkOptHintsLoadCommand() const;
  ArrayRef<uint8_t> getDyldInfoRebaseOpcodes() const;
  ArrayRef<uint8_t> getDyldInfoBindOpcodes() const;
  ArrayRef<uint8_t> getDyldInfoWeakBindOpcodes() const;
  ArrayRef<uint8_t> getDyldInfoLazyBindOpcodes() const;
  ArrayRef<uint8_t> getDyldInfoExportsTrie() const;
  ArrayRef<uint8_t> getUuid() const;

  StringRef getStringTableData() const;
  bool is64Bit() const;
  void ReadULEB128s(uint64_t Index, SmallVectorImpl<uint64_t> &Out) const;

  static StringRef guessLibraryShortName(StringRef Name, bool &isFramework,
                                         StringRef &Suffix);

  static Triple::ArchType getArch(uint32_t CPUType);
  static Triple getArchTriple(uint32_t CPUType, uint32_t CPUSubType,
                              const char **McpuDefault = nullptr,
                              const char **ArchFlag = nullptr);
  static bool isValidArch(StringRef ArchFlag);
  static Triple getHostArch();

  bool isRelocatableObject() const override;

  StringRef mapDebugSectionName(StringRef Name) const override;

  bool hasPageZeroSegment() const { return HasPageZeroSegment; }

  static bool classof(const Binary *v) {
    return v->isMachO();
  }

  static uint32_t
  getVersionMinMajor(MachO::version_min_command &C, bool SDK) {
    uint32_t VersionOrSDK = (SDK) ? C.sdk : C.version;
    return (VersionOrSDK >> 16) & 0xffff;
  }

  static uint32_t
  getVersionMinMinor(MachO::version_min_command &C, bool SDK) {
    uint32_t VersionOrSDK = (SDK) ? C.sdk : C.version;
    return (VersionOrSDK >> 8) & 0xff;
  }

  static uint32_t
  getVersionMinUpdate(MachO::version_min_command &C, bool SDK) {
    uint32_t VersionOrSDK = (SDK) ? C.sdk : C.version;
    return VersionOrSDK & 0xff;
  }

  static std::string getBuildPlatform(uint32_t platform) {
    switch (platform) {
    case MachO::PLATFORM_MACOS: return "macos";
    case MachO::PLATFORM_IOS: return "ios";
    case MachO::PLATFORM_TVOS: return "tvos";
    case MachO::PLATFORM_WATCHOS: return "watchos";
    case MachO::PLATFORM_BRIDGEOS: return "bridgeos";
    case MachO::PLATFORM_IOSSIMULATOR: return "iossimulator";
    case MachO::PLATFORM_TVOSSIMULATOR: return "tvossimulator";
    case MachO::PLATFORM_WATCHOSSIMULATOR: return "watchossimulator";
    default:
      std::string ret;
      raw_string_ostream ss(ret);
      ss << format_hex(platform, 8, true);
      return ss.str();
    }
  }

  static std::string getBuildTool(uint32_t tools) {
    switch (tools) {
    case MachO::TOOL_CLANG: return "clang";
    case MachO::TOOL_SWIFT: return "swift";
    case MachO::TOOL_LD: return "ld";
    default:
      std::string ret;
      raw_string_ostream ss(ret);
      ss << format_hex(tools, 8, true);
      return ss.str();
    }
  }

  static std::string getVersionString(uint32_t version) {
    uint32_t major = (version >> 16) & 0xffff;
    uint32_t minor = (version >> 8) & 0xff;
    uint32_t update = version & 0xff;

    SmallString<32> Version;
    Version = utostr(major) + "." + utostr(minor);
    if (update != 0)
      Version += "." + utostr(update);
    return Version.str();
  }

private:
  MachOObjectFile(MemoryBufferRef Object, bool IsLittleEndian, bool Is64Bits,
                  Error &Err, uint32_t UniversalCputype = 0,
                  uint32_t UniversalIndex = 0);

  uint64_t getSymbolValueImpl(DataRefImpl Symb) const override;

  union {
    MachO::mach_header_64 Header64;
    MachO::mach_header Header;
  };
  using SectionList = SmallVector<const char*, 1>;
  SectionList Sections;
  using LibraryList = SmallVector<const char*, 1>;
  LibraryList Libraries;
  LoadCommandList LoadCommands;
  using LibraryShortName = SmallVector<StringRef, 1>;
  using BuildToolList = SmallVector<const char*, 1>;
  BuildToolList BuildTools;
  mutable LibraryShortName LibrariesShortNames;
  std::unique_ptr<BindRebaseSegInfo> BindRebaseSectionTable;
  const char *SymtabLoadCmd = nullptr;
  const char *DysymtabLoadCmd = nullptr;
  const char *DataInCodeLoadCmd = nullptr;
  const char *LinkOptHintsLoadCmd = nullptr;
  const char *DyldInfoLoadCmd = nullptr;
  const char *UuidLoadCmd = nullptr;
  bool HasPageZeroSegment = false;
};

/// DiceRef
inline DiceRef::DiceRef(DataRefImpl DiceP, const ObjectFile *Owner)
  : DicePimpl(DiceP) , OwningObject(Owner) {}

inline bool DiceRef::operator==(const DiceRef &Other) const {
  return DicePimpl == Other.DicePimpl;
}

inline bool DiceRef::operator<(const DiceRef &Other) const {
  return DicePimpl < Other.DicePimpl;
}

inline void DiceRef::moveNext() {
  const MachO::data_in_code_entry *P =
    reinterpret_cast<const MachO::data_in_code_entry *>(DicePimpl.p);
  DicePimpl.p = reinterpret_cast<uintptr_t>(P + 1);
}

// Since a Mach-O data in code reference, a DiceRef, can only be created when
// the OwningObject ObjectFile is a MachOObjectFile a static_cast<> is used for
// the methods that get the values of the fields of the reference.

inline std::error_code DiceRef::getOffset(uint32_t &Result) const {
  const MachOObjectFile *MachOOF =
    static_cast<const MachOObjectFile *>(OwningObject);
  MachO::data_in_code_entry Dice = MachOOF->getDice(DicePimpl);
  Result = Dice.offset;
  return std::error_code();
}

inline std::error_code DiceRef::getLength(uint16_t &Result) const {
  const MachOObjectFile *MachOOF =
    static_cast<const MachOObjectFile *>(OwningObject);
  MachO::data_in_code_entry Dice = MachOOF->getDice(DicePimpl);
  Result = Dice.length;
  return std::error_code();
}

inline std::error_code DiceRef::getKind(uint16_t &Result) const {
  const MachOObjectFile *MachOOF =
    static_cast<const MachOObjectFile *>(OwningObject);
  MachO::data_in_code_entry Dice = MachOOF->getDice(DicePimpl);
  Result = Dice.kind;
  return std::error_code();
}

inline DataRefImpl DiceRef::getRawDataRefImpl() const {
  return DicePimpl;
}

inline const ObjectFile *DiceRef::getObjectFile() const {
  return OwningObject;
}

} // end namespace object
} // end namespace llvm

#endif // LLVM_OBJECT_MACHO_H
