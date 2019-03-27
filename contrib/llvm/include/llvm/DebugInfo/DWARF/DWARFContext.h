//===- DWARFContext.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===/

#ifndef LLVM_DEBUGINFO_DWARF_DWARFCONTEXT_H
#define LLVM_DEBUGINFO_DWARF_DWARFCONTEXT_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFAcceleratorTable.h"
#include "llvm/DebugInfo/DWARF/DWARFCompileUnit.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugAbbrev.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugAranges.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugFrame.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLine.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLoc.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugMacro.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/DebugInfo/DWARF/DWARFGdbIndex.h"
#include "llvm/DebugInfo/DWARF/DWARFObject.h"
#include "llvm/DebugInfo/DWARF/DWARFSection.h"
#include "llvm/DebugInfo/DWARF/DWARFTypeUnit.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/DebugInfo/DWARF/DWARFUnitIndex.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Host.h"
#include <cstdint>
#include <deque>
#include <map>
#include <memory>

namespace llvm {

class MCRegisterInfo;
class MemoryBuffer;
class raw_ostream;

/// Used as a return value for a error callback passed to DWARF context.
/// Callback should return Halt if client application wants to stop
/// object parsing, or should return Continue otherwise.
enum class ErrorPolicy { Halt, Continue };

/// DWARFContext
/// This data structure is the top level entity that deals with dwarf debug
/// information parsing. The actual data is supplied through DWARFObj.
class DWARFContext : public DIContext {
  DWARFUnitVector NormalUnits;
  std::unique_ptr<DWARFUnitIndex> CUIndex;
  std::unique_ptr<DWARFGdbIndex> GdbIndex;
  std::unique_ptr<DWARFUnitIndex> TUIndex;
  std::unique_ptr<DWARFDebugAbbrev> Abbrev;
  std::unique_ptr<DWARFDebugLoc> Loc;
  std::unique_ptr<DWARFDebugAranges> Aranges;
  std::unique_ptr<DWARFDebugLine> Line;
  std::unique_ptr<DWARFDebugFrame> DebugFrame;
  std::unique_ptr<DWARFDebugFrame> EHFrame;
  std::unique_ptr<DWARFDebugMacro> Macro;
  std::unique_ptr<DWARFDebugNames> Names;
  std::unique_ptr<AppleAcceleratorTable> AppleNames;
  std::unique_ptr<AppleAcceleratorTable> AppleTypes;
  std::unique_ptr<AppleAcceleratorTable> AppleNamespaces;
  std::unique_ptr<AppleAcceleratorTable> AppleObjC;

  DWARFUnitVector DWOUnits;
  std::unique_ptr<DWARFDebugAbbrev> AbbrevDWO;
  std::unique_ptr<DWARFDebugLoclists> LocDWO;

  /// The maximum DWARF version of all units.
  unsigned MaxVersion = 0;

  struct DWOFile {
    object::OwningBinary<object::ObjectFile> File;
    std::unique_ptr<DWARFContext> Context;
  };
  StringMap<std::weak_ptr<DWOFile>> DWOFiles;
  std::weak_ptr<DWOFile> DWP;
  bool CheckedForDWP = false;
  std::string DWPName;

  std::unique_ptr<MCRegisterInfo> RegInfo;

  /// Read compile units from the debug_info section (if necessary)
  /// and type units from the debug_types sections (if necessary)
  /// and store them in NormalUnits.
  void parseNormalUnits();

  /// Read compile units from the debug_info.dwo section (if necessary)
  /// and type units from the debug_types.dwo section (if necessary)
  /// and store them in DWOUnits.
  /// If \p Lazy is true, set up to parse but don't actually parse them.
  enum { EagerParse = false, LazyParse = true };
  void parseDWOUnits(bool Lazy = false);

  std::unique_ptr<const DWARFObject> DObj;

public:
  DWARFContext(std::unique_ptr<const DWARFObject> DObj,
               std::string DWPName = "");
  ~DWARFContext();

  DWARFContext(DWARFContext &) = delete;
  DWARFContext &operator=(DWARFContext &) = delete;

  const DWARFObject &getDWARFObj() const { return *DObj; }

  static bool classof(const DIContext *DICtx) {
    return DICtx->getKind() == CK_DWARF;
  }

  /// Dump a textual representation to \p OS. If any \p DumpOffsets are present,
  /// dump only the record at the specified offset.
  void dump(raw_ostream &OS, DIDumpOptions DumpOpts,
            std::array<Optional<uint64_t>, DIDT_ID_Count> DumpOffsets);

  void dump(raw_ostream &OS, DIDumpOptions DumpOpts) override {
    std::array<Optional<uint64_t>, DIDT_ID_Count> DumpOffsets;
    dump(OS, DumpOpts, DumpOffsets);
  }

  bool verify(raw_ostream &OS, DIDumpOptions DumpOpts = {}) override;

  using unit_iterator_range = DWARFUnitVector::iterator_range;

  /// Get units from .debug_info in this context.
  unit_iterator_range info_section_units() {
    parseNormalUnits();
    return unit_iterator_range(NormalUnits.begin(),
                               NormalUnits.begin() +
                                   NormalUnits.getNumInfoUnits());
  }

  /// Get units from .debug_types in this context.
  unit_iterator_range types_section_units() {
    parseNormalUnits();
    return unit_iterator_range(
        NormalUnits.begin() + NormalUnits.getNumInfoUnits(), NormalUnits.end());
  }

  /// Get compile units in this context.
  unit_iterator_range compile_units() { return info_section_units(); }

  /// Get type units in this context.
  unit_iterator_range type_units() { return types_section_units(); }

  /// Get all normal compile/type units in this context.
  unit_iterator_range normal_units() {
    parseNormalUnits();
    return unit_iterator_range(NormalUnits.begin(), NormalUnits.end());
  }

  /// Get units from .debug_info..dwo in the DWO context.
  unit_iterator_range dwo_info_section_units() {
    parseDWOUnits();
    return unit_iterator_range(DWOUnits.begin(),
                               DWOUnits.begin() + DWOUnits.getNumInfoUnits());
  }

  /// Get units from .debug_types.dwo in the DWO context.
  unit_iterator_range dwo_types_section_units() {
    parseDWOUnits();
    return unit_iterator_range(DWOUnits.begin() + DWOUnits.getNumInfoUnits(),
                               DWOUnits.end());
  }

  /// Get compile units in the DWO context.
  unit_iterator_range dwo_compile_units() { return dwo_info_section_units(); }

  /// Get type units in the DWO context.
  unit_iterator_range dwo_type_units() { return dwo_types_section_units(); }

  /// Get all units in the DWO context.
  unit_iterator_range dwo_units() {
    parseDWOUnits();
    return unit_iterator_range(DWOUnits.begin(), DWOUnits.end());
  }

  /// Get the number of compile units in this context.
  unsigned getNumCompileUnits() {
    parseNormalUnits();
    return NormalUnits.getNumInfoUnits();
  }

  /// Get the number of type units in this context.
  unsigned getNumTypeUnits() {
    parseNormalUnits();
    return NormalUnits.getNumTypesUnits();
  }

  /// Get the number of compile units in the DWO context.
  unsigned getNumDWOCompileUnits() {
    parseDWOUnits();
    return DWOUnits.getNumInfoUnits();
  }

  /// Get the number of type units in the DWO context.
  unsigned getNumDWOTypeUnits() {
    parseDWOUnits();
    return DWOUnits.getNumTypesUnits();
  }

  /// Get the unit at the specified index.
  DWARFUnit *getUnitAtIndex(unsigned index) {
    parseNormalUnits();
    return NormalUnits[index].get();
  }

  /// Get the unit at the specified index for the DWO units.
  DWARFUnit *getDWOUnitAtIndex(unsigned index) {
    parseDWOUnits();
    return DWOUnits[index].get();
  }

  DWARFCompileUnit *getDWOCompileUnitForHash(uint64_t Hash);

  /// Return the compile unit that includes an offset (relative to .debug_info).
  DWARFCompileUnit *getCompileUnitForOffset(uint32_t Offset);

  /// Get a DIE given an exact offset.
  DWARFDie getDIEForOffset(uint32_t Offset);

  unsigned getMaxVersion() {
    // Ensure info units have been parsed to discover MaxVersion
    info_section_units();
    return MaxVersion;
  }

  unsigned getMaxDWOVersion() {
    // Ensure DWO info units have been parsed to discover MaxVersion
    dwo_info_section_units();
    return MaxVersion;
  }

  void setMaxVersionIfGreater(unsigned Version) {
    if (Version > MaxVersion)
      MaxVersion = Version;
  }

  const DWARFUnitIndex &getCUIndex();
  DWARFGdbIndex &getGdbIndex();
  const DWARFUnitIndex &getTUIndex();

  /// Get a pointer to the parsed DebugAbbrev object.
  const DWARFDebugAbbrev *getDebugAbbrev();

  /// Get a pointer to the parsed DebugLoc object.
  const DWARFDebugLoc *getDebugLoc();

  /// Get a pointer to the parsed dwo abbreviations object.
  const DWARFDebugAbbrev *getDebugAbbrevDWO();

  /// Get a pointer to the parsed DebugLoc object.
  const DWARFDebugLoclists *getDebugLocDWO();

  /// Get a pointer to the parsed DebugAranges object.
  const DWARFDebugAranges *getDebugAranges();

  /// Get a pointer to the parsed frame information object.
  const DWARFDebugFrame *getDebugFrame();

  /// Get a pointer to the parsed eh frame information object.
  const DWARFDebugFrame *getEHFrame();

  /// Get a pointer to the parsed DebugMacro object.
  const DWARFDebugMacro *getDebugMacro();

  /// Get a reference to the parsed accelerator table object.
  const DWARFDebugNames &getDebugNames();

  /// Get a reference to the parsed accelerator table object.
  const AppleAcceleratorTable &getAppleNames();

  /// Get a reference to the parsed accelerator table object.
  const AppleAcceleratorTable &getAppleTypes();

  /// Get a reference to the parsed accelerator table object.
  const AppleAcceleratorTable &getAppleNamespaces();

  /// Get a reference to the parsed accelerator table object.
  const AppleAcceleratorTable &getAppleObjC();

  /// Get a pointer to a parsed line table corresponding to a compile unit.
  /// Report any parsing issues as warnings on stderr.
  const DWARFDebugLine::LineTable *getLineTableForUnit(DWARFUnit *U);

  /// Get a pointer to a parsed line table corresponding to a compile unit.
  /// Report any recoverable parsing problems using the callback.
  Expected<const DWARFDebugLine::LineTable *>
  getLineTableForUnit(DWARFUnit *U,
                      std::function<void(Error)> RecoverableErrorCallback);

  DataExtractor getStringExtractor() const {
    return DataExtractor(DObj->getStringSection(), false, 0);
  }
  DataExtractor getLineStringExtractor() const {
    return DataExtractor(DObj->getLineStringSection(), false, 0);
  }

  /// Wraps the returned DIEs for a given address.
  struct DIEsForAddress {
    DWARFCompileUnit *CompileUnit = nullptr;
    DWARFDie FunctionDIE;
    DWARFDie BlockDIE;
    explicit operator bool() const { return CompileUnit != nullptr; }
  };

  /// Get the compilation unit, the function DIE and lexical block DIE for the
  /// given address where applicable.
  DIEsForAddress getDIEsForAddress(uint64_t Address);

  DILineInfo getLineInfoForAddress(uint64_t Address,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;
  DILineInfoTable getLineInfoForAddressRange(uint64_t Address, uint64_t Size,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;
  DIInliningInfo getInliningInfoForAddress(uint64_t Address,
      DILineInfoSpecifier Specifier = DILineInfoSpecifier()) override;

  bool isLittleEndian() const { return DObj->isLittleEndian(); }
  static bool isSupportedVersion(unsigned version) {
    return version == 2 || version == 3 || version == 4 || version == 5;
  }

  std::shared_ptr<DWARFContext> getDWOContext(StringRef AbsolutePath);

  const MCRegisterInfo *getRegisterInfo() const { return RegInfo.get(); }

  /// Function used to handle default error reporting policy. Prints a error
  /// message and returns Continue, so DWARF context ignores the error.
  static ErrorPolicy defaultErrorHandler(Error E);
  static std::unique_ptr<DWARFContext>
  create(const object::ObjectFile &Obj, const LoadedObjectInfo *L = nullptr,
         function_ref<ErrorPolicy(Error)> HandleError = defaultErrorHandler,
         std::string DWPName = "");

  static std::unique_ptr<DWARFContext>
  create(const StringMap<std::unique_ptr<MemoryBuffer>> &Sections,
         uint8_t AddrSize, bool isLittleEndian = sys::IsLittleEndianHost);

  /// Loads register info for the architecture of the provided object file.
  /// Improves readability of dumped DWARF expressions. Requires the caller to
  /// have initialized the relevant target descriptions.
  Error loadRegisterInfo(const object::ObjectFile &Obj);

  /// Get address size from CUs.
  /// TODO: refactor compile_units() to make this const.
  uint8_t getCUAddrSize();

  /// Dump Error as warning message to stderr.
  static void dumpWarning(Error Warning);

  Triple::ArchType getArch() const {
    return getDWARFObj().getFile()->getArch();
  }

private:
  /// Return the compile unit which contains instruction with provided
  /// address.
  DWARFCompileUnit *getCompileUnitForAddress(uint64_t Address);
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFCONTEXT_H
