//===- DWARFContext.cpp ---------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFAcceleratorTable.h"
#include "llvm/DebugInfo/DWARF/DWARFCompileUnit.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugAbbrev.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugAddr.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugArangeSet.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugAranges.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugFrame.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLine.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLoc.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugMacro.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugPubTable.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugRangeList.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugRnglists.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/DebugInfo/DWARF/DWARFGdbIndex.h"
#include "llvm/DebugInfo/DWARF/DWARFSection.h"
#include "llvm/DebugInfo/DWARF/DWARFUnitIndex.h"
#include "llvm/DebugInfo/DWARF/DWARFVerifier.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Object/Decompressor.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/RelocVisitor.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
using namespace dwarf;
using namespace object;

#define DEBUG_TYPE "dwarf"

using DWARFLineTable = DWARFDebugLine::LineTable;
using FileLineInfoKind = DILineInfoSpecifier::FileLineInfoKind;
using FunctionNameKind = DILineInfoSpecifier::FunctionNameKind;

DWARFContext::DWARFContext(std::unique_ptr<const DWARFObject> DObj,
                           std::string DWPName)
    : DIContext(CK_DWARF), DWPName(std::move(DWPName)), DObj(std::move(DObj)) {}

DWARFContext::~DWARFContext() = default;

/// Dump the UUID load command.
static void dumpUUID(raw_ostream &OS, const ObjectFile &Obj) {
  auto *MachO = dyn_cast<MachOObjectFile>(&Obj);
  if (!MachO)
    return;
  for (auto LC : MachO->load_commands()) {
    raw_ostream::uuid_t UUID;
    if (LC.C.cmd == MachO::LC_UUID) {
      if (LC.C.cmdsize < sizeof(UUID) + sizeof(LC.C)) {
        OS << "error: UUID load command is too short.\n";
        return;
      }
      OS << "UUID: ";
      memcpy(&UUID, LC.Ptr+sizeof(LC.C), sizeof(UUID));
      OS.write_uuid(UUID);
      Triple T = MachO->getArchTriple();
      OS << " (" << T.getArchName() << ')';
      OS << ' ' << MachO->getFileName() << '\n';
    }
  }
}

using ContributionCollection =
    std::vector<Optional<StrOffsetsContributionDescriptor>>;

// Collect all the contributions to the string offsets table from all units,
// sort them by their starting offsets and remove duplicates.
static ContributionCollection
collectContributionData(DWARFContext::unit_iterator_range Units) {
  ContributionCollection Contributions;
  for (const auto &U : Units)
    Contributions.push_back(U->getStringOffsetsTableContribution());
  // Sort the contributions so that any invalid ones are placed at
  // the start of the contributions vector. This way they are reported
  // first.
  llvm::sort(Contributions,
             [](const Optional<StrOffsetsContributionDescriptor> &L,
                const Optional<StrOffsetsContributionDescriptor> &R) {
               if (L && R)
                 return L->Base < R->Base;
               return R.hasValue();
             });

  // Uniquify contributions, as it is possible that units (specifically
  // type units in dwo or dwp files) share contributions. We don't want
  // to report them more than once.
  Contributions.erase(
      std::unique(Contributions.begin(), Contributions.end(),
                  [](const Optional<StrOffsetsContributionDescriptor> &L,
                     const Optional<StrOffsetsContributionDescriptor> &R) {
                    if (L && R)
                      return L->Base == R->Base && L->Size == R->Size;
                    return false;
                  }),
      Contributions.end());
  return Contributions;
}

static void dumpDWARFv5StringOffsetsSection(
    raw_ostream &OS, StringRef SectionName, const DWARFObject &Obj,
    const DWARFSection &StringOffsetsSection, StringRef StringSection,
    DWARFContext::unit_iterator_range Units, bool LittleEndian) {
  auto Contributions = collectContributionData(Units);
  DWARFDataExtractor StrOffsetExt(Obj, StringOffsetsSection, LittleEndian, 0);
  DataExtractor StrData(StringSection, LittleEndian, 0);
  uint64_t SectionSize = StringOffsetsSection.Data.size();
  uint32_t Offset = 0;
  for (auto &Contribution : Contributions) {
    // Report an ill-formed contribution.
    if (!Contribution) {
      OS << "error: invalid contribution to string offsets table in section ."
         << SectionName << ".\n";
      return;
    }

    dwarf::DwarfFormat Format = Contribution->getFormat();
    uint16_t Version = Contribution->getVersion();
    uint64_t ContributionHeader = Contribution->Base;
    // In DWARF v5 there is a contribution header that immediately precedes
    // the string offsets base (the location we have previously retrieved from
    // the CU DIE's DW_AT_str_offsets attribute). The header is located either
    // 8 or 16 bytes before the base, depending on the contribution's format.
    if (Version >= 5)
      ContributionHeader -= Format == DWARF32 ? 8 : 16;

    // Detect overlapping contributions.
    if (Offset > ContributionHeader) {
      OS << "error: overlapping contributions to string offsets table in "
            "section ."
         << SectionName << ".\n";
      return;
    }
    // Report a gap in the table.
    if (Offset < ContributionHeader) {
      OS << format("0x%8.8x: Gap, length = ", Offset);
      OS << (ContributionHeader - Offset) << "\n";
    }
    OS << format("0x%8.8x: ", (uint32_t)ContributionHeader);
    // In DWARF v5 the contribution size in the descriptor does not equal
    // the originally encoded length (it does not contain the length of the
    // version field and the padding, a total of 4 bytes). Add them back in
    // for reporting.
    OS << "Contribution size = " << (Contribution->Size + (Version < 5 ? 0 : 4))
       << ", Format = " << (Format == DWARF32 ? "DWARF32" : "DWARF64")
       << ", Version = " << Version << "\n";

    Offset = Contribution->Base;
    unsigned EntrySize = Contribution->getDwarfOffsetByteSize();
    while (Offset - Contribution->Base < Contribution->Size) {
      OS << format("0x%8.8x: ", Offset);
      // FIXME: We can only extract strings if the offset fits in 32 bits.
      uint64_t StringOffset =
          StrOffsetExt.getRelocatedValue(EntrySize, &Offset);
      // Extract the string if we can and display it. Otherwise just report
      // the offset.
      if (StringOffset <= std::numeric_limits<uint32_t>::max()) {
        uint32_t StringOffset32 = (uint32_t)StringOffset;
        OS << format("%8.8x ", StringOffset32);
        const char *S = StrData.getCStr(&StringOffset32);
        if (S)
          OS << format("\"%s\"", S);
      } else
        OS << format("%16.16" PRIx64 " ", StringOffset);
      OS << "\n";
    }
  }
  // Report a gap at the end of the table.
  if (Offset < SectionSize) {
    OS << format("0x%8.8x: Gap, length = ", Offset);
    OS << (SectionSize - Offset) << "\n";
  }
}

// Dump a DWARF string offsets section. This may be a DWARF v5 formatted
// string offsets section, where each compile or type unit contributes a
// number of entries (string offsets), with each contribution preceded by
// a header containing size and version number. Alternatively, it may be a
// monolithic series of string offsets, as generated by the pre-DWARF v5
// implementation of split DWARF.
static void dumpStringOffsetsSection(raw_ostream &OS, StringRef SectionName,
                                     const DWARFObject &Obj,
                                     const DWARFSection &StringOffsetsSection,
                                     StringRef StringSection,
                                     DWARFContext::unit_iterator_range Units,
                                     bool LittleEndian, unsigned MaxVersion) {
  // If we have at least one (compile or type) unit with DWARF v5 or greater,
  // we assume that the section is formatted like a DWARF v5 string offsets
  // section.
  if (MaxVersion >= 5)
    dumpDWARFv5StringOffsetsSection(OS, SectionName, Obj, StringOffsetsSection,
                                    StringSection, Units, LittleEndian);
  else {
    DataExtractor strOffsetExt(StringOffsetsSection.Data, LittleEndian, 0);
    uint32_t offset = 0;
    uint64_t size = StringOffsetsSection.Data.size();
    // Ensure that size is a multiple of the size of an entry.
    if (size & ((uint64_t)(sizeof(uint32_t) - 1))) {
      OS << "error: size of ." << SectionName << " is not a multiple of "
         << sizeof(uint32_t) << ".\n";
      size &= -(uint64_t)sizeof(uint32_t);
    }
    DataExtractor StrData(StringSection, LittleEndian, 0);
    while (offset < size) {
      OS << format("0x%8.8x: ", offset);
      uint32_t StringOffset = strOffsetExt.getU32(&offset);
      OS << format("%8.8x  ", StringOffset);
      const char *S = StrData.getCStr(&StringOffset);
      if (S)
        OS << format("\"%s\"", S);
      OS << "\n";
    }
  }
}

// Dump the .debug_addr section.
static void dumpAddrSection(raw_ostream &OS, DWARFDataExtractor &AddrData,
                            DIDumpOptions DumpOpts, uint16_t Version,
                            uint8_t AddrSize) {
  uint32_t Offset = 0;
  while (AddrData.isValidOffset(Offset)) {
    DWARFDebugAddrTable AddrTable;
    uint32_t TableOffset = Offset;
    if (Error Err = AddrTable.extract(AddrData, &Offset, Version, AddrSize,
                                      DWARFContext::dumpWarning)) {
      WithColor::error() << toString(std::move(Err)) << '\n';
      // Keep going after an error, if we can, assuming that the length field
      // could be read. If it couldn't, stop reading the section.
      if (!AddrTable.hasValidLength())
        break;
      uint64_t Length = AddrTable.getLength();
      Offset = TableOffset + Length;
    } else {
      AddrTable.dump(OS, DumpOpts);
    }
  }
}

// Dump the .debug_rnglists or .debug_rnglists.dwo section (DWARF v5).
static void
dumpRnglistsSection(raw_ostream &OS, DWARFDataExtractor &rnglistData,
                    llvm::function_ref<Optional<SectionedAddress>(uint32_t)>
                        LookupPooledAddress,
                    DIDumpOptions DumpOpts) {
  uint32_t Offset = 0;
  while (rnglistData.isValidOffset(Offset)) {
    llvm::DWARFDebugRnglistTable Rnglists;
    uint32_t TableOffset = Offset;
    if (Error Err = Rnglists.extract(rnglistData, &Offset)) {
      WithColor::error() << toString(std::move(Err)) << '\n';
      uint64_t Length = Rnglists.length();
      // Keep going after an error, if we can, assuming that the length field
      // could be read. If it couldn't, stop reading the section.
      if (Length == 0)
        break;
      Offset = TableOffset + Length;
    } else {
      Rnglists.dump(OS, LookupPooledAddress, DumpOpts);
    }
  }
}

static void dumpLoclistsSection(raw_ostream &OS, DIDumpOptions DumpOpts,
                                DWARFDataExtractor Data,
                                const MCRegisterInfo *MRI,
                                Optional<uint64_t> DumpOffset) {
  uint32_t Offset = 0;
  DWARFDebugLoclists Loclists;

  DWARFListTableHeader Header(".debug_loclists", "locations");
  if (Error E = Header.extract(Data, &Offset)) {
    WithColor::error() << toString(std::move(E)) << '\n';
    return;
  }

  Header.dump(OS, DumpOpts);
  DataExtractor LocData(Data.getData().drop_front(Offset),
                        Data.isLittleEndian(), Header.getAddrSize());

  Loclists.parse(LocData, Header.getVersion());
  Loclists.dump(OS, 0, MRI, DumpOffset);
}

void DWARFContext::dump(
    raw_ostream &OS, DIDumpOptions DumpOpts,
    std::array<Optional<uint64_t>, DIDT_ID_Count> DumpOffsets) {

  uint64_t DumpType = DumpOpts.DumpType;

  StringRef Extension = sys::path::extension(DObj->getFileName());
  bool IsDWO = (Extension == ".dwo") || (Extension == ".dwp");

  // Print UUID header.
  const auto *ObjFile = DObj->getFile();
  if (DumpType & DIDT_UUID)
    dumpUUID(OS, *ObjFile);

  // Print a header for each explicitly-requested section.
  // Otherwise just print one for non-empty sections.
  // Only print empty .dwo section headers when dumping a .dwo file.
  bool Explicit = DumpType != DIDT_All && !IsDWO;
  bool ExplicitDWO = Explicit && IsDWO;
  auto shouldDump = [&](bool Explicit, const char *Name, unsigned ID,
                        StringRef Section) -> Optional<uint64_t> * {
    unsigned Mask = 1U << ID;
    bool Should = (DumpType & Mask) && (Explicit || !Section.empty());
    if (!Should)
      return nullptr;
    OS << "\n" << Name << " contents:\n";
    return &DumpOffsets[ID];
  };

  // Dump individual sections.
  if (shouldDump(Explicit, ".debug_abbrev", DIDT_ID_DebugAbbrev,
                 DObj->getAbbrevSection()))
    getDebugAbbrev()->dump(OS);
  if (shouldDump(ExplicitDWO, ".debug_abbrev.dwo", DIDT_ID_DebugAbbrev,
                 DObj->getAbbrevDWOSection()))
    getDebugAbbrevDWO()->dump(OS);

  auto dumpDebugInfo = [&](const char *Name, unit_iterator_range Units) {
    OS << '\n' << Name << " contents:\n";
    if (auto DumpOffset = DumpOffsets[DIDT_ID_DebugInfo])
      for (const auto &U : Units)
        U->getDIEForOffset(DumpOffset.getValue())
            .dump(OS, 0, DumpOpts.noImplicitRecursion());
    else
      for (const auto &U : Units)
        U->dump(OS, DumpOpts);
  };
  if ((DumpType & DIDT_DebugInfo)) {
    if (Explicit || getNumCompileUnits())
      dumpDebugInfo(".debug_info", info_section_units());
    if (ExplicitDWO || getNumDWOCompileUnits())
      dumpDebugInfo(".debug_info.dwo", dwo_info_section_units());
  }

  auto dumpDebugType = [&](const char *Name, unit_iterator_range Units) {
    OS << '\n' << Name << " contents:\n";
    for (const auto &U : Units)
      if (auto DumpOffset = DumpOffsets[DIDT_ID_DebugTypes])
        U->getDIEForOffset(*DumpOffset)
            .dump(OS, 0, DumpOpts.noImplicitRecursion());
      else
        U->dump(OS, DumpOpts);
  };
  if ((DumpType & DIDT_DebugTypes)) {
    if (Explicit || getNumTypeUnits())
      dumpDebugType(".debug_types", types_section_units());
    if (ExplicitDWO || getNumDWOTypeUnits())
      dumpDebugType(".debug_types.dwo", dwo_types_section_units());
  }

  if (const auto *Off = shouldDump(Explicit, ".debug_loc", DIDT_ID_DebugLoc,
                                   DObj->getLocSection().Data)) {
    getDebugLoc()->dump(OS, getRegisterInfo(), *Off);
  }
  if (const auto *Off =
          shouldDump(Explicit, ".debug_loclists", DIDT_ID_DebugLoclists,
                     DObj->getLoclistsSection().Data)) {
    DWARFDataExtractor Data(*DObj, DObj->getLoclistsSection(), isLittleEndian(),
                            0);
    dumpLoclistsSection(OS, DumpOpts, Data, getRegisterInfo(), *Off);
  }
  if (const auto *Off =
          shouldDump(ExplicitDWO, ".debug_loc.dwo", DIDT_ID_DebugLoc,
                     DObj->getLocDWOSection().Data)) {
    getDebugLocDWO()->dump(OS, 0, getRegisterInfo(), *Off);
  }

  if (const auto *Off = shouldDump(Explicit, ".debug_frame", DIDT_ID_DebugFrame,
                                   DObj->getDebugFrameSection()))
    getDebugFrame()->dump(OS, getRegisterInfo(), *Off);

  if (const auto *Off = shouldDump(Explicit, ".eh_frame", DIDT_ID_DebugFrame,
                                   DObj->getEHFrameSection()))
    getEHFrame()->dump(OS, getRegisterInfo(), *Off);

  if (DumpType & DIDT_DebugMacro) {
    if (Explicit || !getDebugMacro()->empty()) {
      OS << "\n.debug_macinfo contents:\n";
      getDebugMacro()->dump(OS);
    }
  }

  if (shouldDump(Explicit, ".debug_aranges", DIDT_ID_DebugAranges,
                 DObj->getARangeSection())) {
    uint32_t offset = 0;
    DataExtractor arangesData(DObj->getARangeSection(), isLittleEndian(), 0);
    DWARFDebugArangeSet set;
    while (set.extract(arangesData, &offset))
      set.dump(OS);
  }

  auto DumpLineSection = [&](DWARFDebugLine::SectionParser Parser,
                             DIDumpOptions DumpOpts,
                             Optional<uint64_t> DumpOffset) {
    while (!Parser.done()) {
      if (DumpOffset && Parser.getOffset() != *DumpOffset) {
        Parser.skip(dumpWarning);
        continue;
      }
      OS << "debug_line[" << format("0x%8.8x", Parser.getOffset()) << "]\n";
      if (DumpOpts.Verbose) {
        Parser.parseNext(dumpWarning, dumpWarning, &OS);
      } else {
        DWARFDebugLine::LineTable LineTable =
            Parser.parseNext(dumpWarning, dumpWarning);
        LineTable.dump(OS, DumpOpts);
      }
    }
  };

  if (const auto *Off = shouldDump(Explicit, ".debug_line", DIDT_ID_DebugLine,
                                   DObj->getLineSection().Data)) {
    DWARFDataExtractor LineData(*DObj, DObj->getLineSection(), isLittleEndian(),
                                0);
    DWARFDebugLine::SectionParser Parser(LineData, *this, compile_units(),
                                         type_units());
    DumpLineSection(Parser, DumpOpts, *Off);
  }

  if (const auto *Off =
          shouldDump(ExplicitDWO, ".debug_line.dwo", DIDT_ID_DebugLine,
                     DObj->getLineDWOSection().Data)) {
    DWARFDataExtractor LineData(*DObj, DObj->getLineDWOSection(),
                                isLittleEndian(), 0);
    DWARFDebugLine::SectionParser Parser(LineData, *this, dwo_compile_units(),
                                         dwo_type_units());
    DumpLineSection(Parser, DumpOpts, *Off);
  }

  if (shouldDump(Explicit, ".debug_cu_index", DIDT_ID_DebugCUIndex,
                 DObj->getCUIndexSection())) {
    getCUIndex().dump(OS);
  }

  if (shouldDump(Explicit, ".debug_tu_index", DIDT_ID_DebugTUIndex,
                 DObj->getTUIndexSection())) {
    getTUIndex().dump(OS);
  }

  if (shouldDump(Explicit, ".debug_str", DIDT_ID_DebugStr,
                 DObj->getStringSection())) {
    DataExtractor strData(DObj->getStringSection(), isLittleEndian(), 0);
    uint32_t offset = 0;
    uint32_t strOffset = 0;
    while (const char *s = strData.getCStr(&offset)) {
      OS << format("0x%8.8x: \"%s\"\n", strOffset, s);
      strOffset = offset;
    }
  }
  if (shouldDump(ExplicitDWO, ".debug_str.dwo", DIDT_ID_DebugStr,
                 DObj->getStringDWOSection())) {
    DataExtractor strDWOData(DObj->getStringDWOSection(), isLittleEndian(), 0);
    uint32_t offset = 0;
    uint32_t strDWOOffset = 0;
    while (const char *s = strDWOData.getCStr(&offset)) {
      OS << format("0x%8.8x: \"%s\"\n", strDWOOffset, s);
      strDWOOffset = offset;
    }
  }
  if (shouldDump(Explicit, ".debug_line_str", DIDT_ID_DebugLineStr,
                 DObj->getLineStringSection())) {
    DataExtractor strData(DObj->getLineStringSection(), isLittleEndian(), 0);
    uint32_t offset = 0;
    uint32_t strOffset = 0;
    while (const char *s = strData.getCStr(&offset)) {
      OS << format("0x%8.8x: \"", strOffset);
      OS.write_escaped(s);
      OS << "\"\n";
      strOffset = offset;
    }
  }

  if (shouldDump(Explicit, ".debug_addr", DIDT_ID_DebugAddr,
                 DObj->getAddrSection().Data)) {
    DWARFDataExtractor AddrData(*DObj, DObj->getAddrSection(),
                                   isLittleEndian(), 0);
    dumpAddrSection(OS, AddrData, DumpOpts, getMaxVersion(), getCUAddrSize());
  }

  if (shouldDump(Explicit, ".debug_ranges", DIDT_ID_DebugRanges,
                 DObj->getRangeSection().Data)) {
    uint8_t savedAddressByteSize = getCUAddrSize();
    DWARFDataExtractor rangesData(*DObj, DObj->getRangeSection(),
                                  isLittleEndian(), savedAddressByteSize);
    uint32_t offset = 0;
    DWARFDebugRangeList rangeList;
    while (rangesData.isValidOffset(offset)) {
      if (Error E = rangeList.extract(rangesData, &offset)) {
        WithColor::error() << toString(std::move(E)) << '\n';
        break;
      }
      rangeList.dump(OS);
    }
  }

  auto LookupPooledAddress = [&](uint32_t Index) -> Optional<SectionedAddress> {
    const auto &CUs = compile_units();
    auto I = CUs.begin();
    if (I == CUs.end())
      return None;
    return (*I)->getAddrOffsetSectionItem(Index);
  };

  if (shouldDump(Explicit, ".debug_rnglists", DIDT_ID_DebugRnglists,
                 DObj->getRnglistsSection().Data)) {
    DWARFDataExtractor RnglistData(*DObj, DObj->getRnglistsSection(),
                                   isLittleEndian(), 0);
    dumpRnglistsSection(OS, RnglistData, LookupPooledAddress, DumpOpts);
  }

  if (shouldDump(ExplicitDWO, ".debug_rnglists.dwo", DIDT_ID_DebugRnglists,
                 DObj->getRnglistsDWOSection().Data)) {
    DWARFDataExtractor RnglistData(*DObj, DObj->getRnglistsDWOSection(),
                                   isLittleEndian(), 0);
    dumpRnglistsSection(OS, RnglistData, LookupPooledAddress, DumpOpts);
  }

  if (shouldDump(Explicit, ".debug_pubnames", DIDT_ID_DebugPubnames,
                 DObj->getPubNamesSection().Data))
    DWARFDebugPubTable(*DObj, DObj->getPubNamesSection(), isLittleEndian(), false)
        .dump(OS);

  if (shouldDump(Explicit, ".debug_pubtypes", DIDT_ID_DebugPubtypes,
                 DObj->getPubTypesSection().Data))
    DWARFDebugPubTable(*DObj, DObj->getPubTypesSection(), isLittleEndian(), false)
        .dump(OS);

  if (shouldDump(Explicit, ".debug_gnu_pubnames", DIDT_ID_DebugGnuPubnames,
                 DObj->getGnuPubNamesSection().Data))
    DWARFDebugPubTable(*DObj, DObj->getGnuPubNamesSection(), isLittleEndian(),
                       true /* GnuStyle */)
        .dump(OS);

  if (shouldDump(Explicit, ".debug_gnu_pubtypes", DIDT_ID_DebugGnuPubtypes,
                 DObj->getGnuPubTypesSection().Data))
    DWARFDebugPubTable(*DObj, DObj->getGnuPubTypesSection(), isLittleEndian(),
                       true /* GnuStyle */)
        .dump(OS);

  if (shouldDump(Explicit, ".debug_str_offsets", DIDT_ID_DebugStrOffsets,
                 DObj->getStringOffsetSection().Data))
    dumpStringOffsetsSection(OS, "debug_str_offsets", *DObj,
                             DObj->getStringOffsetSection(),
                             DObj->getStringSection(), normal_units(),
                             isLittleEndian(), getMaxVersion());
  if (shouldDump(ExplicitDWO, ".debug_str_offsets.dwo", DIDT_ID_DebugStrOffsets,
                 DObj->getStringOffsetDWOSection().Data))
    dumpStringOffsetsSection(OS, "debug_str_offsets.dwo", *DObj,
                             DObj->getStringOffsetDWOSection(),
                             DObj->getStringDWOSection(), dwo_units(),
                             isLittleEndian(), getMaxDWOVersion());

  if (shouldDump(Explicit, ".gdb_index", DIDT_ID_GdbIndex,
                 DObj->getGdbIndexSection())) {
    getGdbIndex().dump(OS);
  }

  if (shouldDump(Explicit, ".apple_names", DIDT_ID_AppleNames,
                 DObj->getAppleNamesSection().Data))
    getAppleNames().dump(OS);

  if (shouldDump(Explicit, ".apple_types", DIDT_ID_AppleTypes,
                 DObj->getAppleTypesSection().Data))
    getAppleTypes().dump(OS);

  if (shouldDump(Explicit, ".apple_namespaces", DIDT_ID_AppleNamespaces,
                 DObj->getAppleNamespacesSection().Data))
    getAppleNamespaces().dump(OS);

  if (shouldDump(Explicit, ".apple_objc", DIDT_ID_AppleObjC,
                 DObj->getAppleObjCSection().Data))
    getAppleObjC().dump(OS);
  if (shouldDump(Explicit, ".debug_names", DIDT_ID_DebugNames,
                 DObj->getDebugNamesSection().Data))
    getDebugNames().dump(OS);
}

DWARFCompileUnit *DWARFContext::getDWOCompileUnitForHash(uint64_t Hash) {
  parseDWOUnits(LazyParse);

  if (const auto &CUI = getCUIndex()) {
    if (const auto *R = CUI.getFromHash(Hash))
      return dyn_cast_or_null<DWARFCompileUnit>(
          DWOUnits.getUnitForIndexEntry(*R));
    return nullptr;
  }

  // If there's no index, just search through the CUs in the DWO - there's
  // probably only one unless this is something like LTO - though an in-process
  // built/cached lookup table could be used in that case to improve repeated
  // lookups of different CUs in the DWO.
  for (const auto &DWOCU : dwo_compile_units()) {
    // Might not have parsed DWO ID yet.
    if (!DWOCU->getDWOId()) {
      if (Optional<uint64_t> DWOId =
          toUnsigned(DWOCU->getUnitDIE().find(DW_AT_GNU_dwo_id)))
        DWOCU->setDWOId(*DWOId);
      else
        // No DWO ID?
        continue;
    }
    if (DWOCU->getDWOId() == Hash)
      return dyn_cast<DWARFCompileUnit>(DWOCU.get());
  }
  return nullptr;
}

DWARFDie DWARFContext::getDIEForOffset(uint32_t Offset) {
  parseNormalUnits();
  if (auto *CU = NormalUnits.getUnitForOffset(Offset))
    return CU->getDIEForOffset(Offset);
  return DWARFDie();
}

bool DWARFContext::verify(raw_ostream &OS, DIDumpOptions DumpOpts) {
  bool Success = true;
  DWARFVerifier verifier(OS, *this, DumpOpts);

  Success &= verifier.handleDebugAbbrev();
  if (DumpOpts.DumpType & DIDT_DebugInfo)
    Success &= verifier.handleDebugInfo();
  if (DumpOpts.DumpType & DIDT_DebugLine)
    Success &= verifier.handleDebugLine();
  Success &= verifier.handleAccelTables();
  return Success;
}

const DWARFUnitIndex &DWARFContext::getCUIndex() {
  if (CUIndex)
    return *CUIndex;

  DataExtractor CUIndexData(DObj->getCUIndexSection(), isLittleEndian(), 0);

  CUIndex = llvm::make_unique<DWARFUnitIndex>(DW_SECT_INFO);
  CUIndex->parse(CUIndexData);
  return *CUIndex;
}

const DWARFUnitIndex &DWARFContext::getTUIndex() {
  if (TUIndex)
    return *TUIndex;

  DataExtractor TUIndexData(DObj->getTUIndexSection(), isLittleEndian(), 0);

  TUIndex = llvm::make_unique<DWARFUnitIndex>(DW_SECT_TYPES);
  TUIndex->parse(TUIndexData);
  return *TUIndex;
}

DWARFGdbIndex &DWARFContext::getGdbIndex() {
  if (GdbIndex)
    return *GdbIndex;

  DataExtractor GdbIndexData(DObj->getGdbIndexSection(), true /*LE*/, 0);
  GdbIndex = llvm::make_unique<DWARFGdbIndex>();
  GdbIndex->parse(GdbIndexData);
  return *GdbIndex;
}

const DWARFDebugAbbrev *DWARFContext::getDebugAbbrev() {
  if (Abbrev)
    return Abbrev.get();

  DataExtractor abbrData(DObj->getAbbrevSection(), isLittleEndian(), 0);

  Abbrev.reset(new DWARFDebugAbbrev());
  Abbrev->extract(abbrData);
  return Abbrev.get();
}

const DWARFDebugAbbrev *DWARFContext::getDebugAbbrevDWO() {
  if (AbbrevDWO)
    return AbbrevDWO.get();

  DataExtractor abbrData(DObj->getAbbrevDWOSection(), isLittleEndian(), 0);
  AbbrevDWO.reset(new DWARFDebugAbbrev());
  AbbrevDWO->extract(abbrData);
  return AbbrevDWO.get();
}

const DWARFDebugLoc *DWARFContext::getDebugLoc() {
  if (Loc)
    return Loc.get();

  Loc.reset(new DWARFDebugLoc);
  // Assume all units have the same address byte size.
  if (getNumCompileUnits()) {
    DWARFDataExtractor LocData(*DObj, DObj->getLocSection(), isLittleEndian(),
                               getUnitAtIndex(0)->getAddressByteSize());
    Loc->parse(LocData);
  }
  return Loc.get();
}

const DWARFDebugLoclists *DWARFContext::getDebugLocDWO() {
  if (LocDWO)
    return LocDWO.get();

  LocDWO.reset(new DWARFDebugLoclists());
  // Assume all compile units have the same address byte size.
  // FIXME: We don't need AddressSize for split DWARF since relocatable
  // addresses cannot appear there. At the moment DWARFExpression requires it.
  DataExtractor LocData(DObj->getLocDWOSection().Data, isLittleEndian(), 4);
  // Use version 4. DWO does not support the DWARF v5 .debug_loclists yet and
  // that means we are parsing the new style .debug_loc (pre-standatized version
  // of the .debug_loclists).
  LocDWO->parse(LocData, 4 /* Version */);
  return LocDWO.get();
}

const DWARFDebugAranges *DWARFContext::getDebugAranges() {
  if (Aranges)
    return Aranges.get();

  Aranges.reset(new DWARFDebugAranges());
  Aranges->generate(this);
  return Aranges.get();
}

const DWARFDebugFrame *DWARFContext::getDebugFrame() {
  if (DebugFrame)
    return DebugFrame.get();

  // There's a "bug" in the DWARFv3 standard with respect to the target address
  // size within debug frame sections. While DWARF is supposed to be independent
  // of its container, FDEs have fields with size being "target address size",
  // which isn't specified in DWARF in general. It's only specified for CUs, but
  // .eh_frame can appear without a .debug_info section. Follow the example of
  // other tools (libdwarf) and extract this from the container (ObjectFile
  // provides this information). This problem is fixed in DWARFv4
  // See this dwarf-discuss discussion for more details:
  // http://lists.dwarfstd.org/htdig.cgi/dwarf-discuss-dwarfstd.org/2011-December/001173.html
  DWARFDataExtractor debugFrameData(DObj->getDebugFrameSection(),
                                    isLittleEndian(), DObj->getAddressSize());
  DebugFrame.reset(new DWARFDebugFrame(getArch(), false /* IsEH */));
  DebugFrame->parse(debugFrameData);
  return DebugFrame.get();
}

const DWARFDebugFrame *DWARFContext::getEHFrame() {
  if (EHFrame)
    return EHFrame.get();

  DWARFDataExtractor debugFrameData(DObj->getEHFrameSection(), isLittleEndian(),
                                    DObj->getAddressSize());
  DebugFrame.reset(new DWARFDebugFrame(getArch(), true /* IsEH */));
  DebugFrame->parse(debugFrameData);
  return DebugFrame.get();
}

const DWARFDebugMacro *DWARFContext::getDebugMacro() {
  if (Macro)
    return Macro.get();

  DataExtractor MacinfoData(DObj->getMacinfoSection(), isLittleEndian(), 0);
  Macro.reset(new DWARFDebugMacro());
  Macro->parse(MacinfoData);
  return Macro.get();
}

template <typename T>
static T &getAccelTable(std::unique_ptr<T> &Cache, const DWARFObject &Obj,
                        const DWARFSection &Section, StringRef StringSection,
                        bool IsLittleEndian) {
  if (Cache)
    return *Cache;
  DWARFDataExtractor AccelSection(Obj, Section, IsLittleEndian, 0);
  DataExtractor StrData(StringSection, IsLittleEndian, 0);
  Cache.reset(new T(AccelSection, StrData));
  if (Error E = Cache->extract())
    llvm::consumeError(std::move(E));
  return *Cache;
}

const DWARFDebugNames &DWARFContext::getDebugNames() {
  return getAccelTable(Names, *DObj, DObj->getDebugNamesSection(),
                       DObj->getStringSection(), isLittleEndian());
}

const AppleAcceleratorTable &DWARFContext::getAppleNames() {
  return getAccelTable(AppleNames, *DObj, DObj->getAppleNamesSection(),
                       DObj->getStringSection(), isLittleEndian());
}

const AppleAcceleratorTable &DWARFContext::getAppleTypes() {
  return getAccelTable(AppleTypes, *DObj, DObj->getAppleTypesSection(),
                       DObj->getStringSection(), isLittleEndian());
}

const AppleAcceleratorTable &DWARFContext::getAppleNamespaces() {
  return getAccelTable(AppleNamespaces, *DObj,
                       DObj->getAppleNamespacesSection(),
                       DObj->getStringSection(), isLittleEndian());
}

const AppleAcceleratorTable &DWARFContext::getAppleObjC() {
  return getAccelTable(AppleObjC, *DObj, DObj->getAppleObjCSection(),
                       DObj->getStringSection(), isLittleEndian());
}

const DWARFDebugLine::LineTable *
DWARFContext::getLineTableForUnit(DWARFUnit *U) {
  Expected<const DWARFDebugLine::LineTable *> ExpectedLineTable =
      getLineTableForUnit(U, dumpWarning);
  if (!ExpectedLineTable) {
    dumpWarning(ExpectedLineTable.takeError());
    return nullptr;
  }
  return *ExpectedLineTable;
}

Expected<const DWARFDebugLine::LineTable *> DWARFContext::getLineTableForUnit(
    DWARFUnit *U, std::function<void(Error)> RecoverableErrorCallback) {
  if (!Line)
    Line.reset(new DWARFDebugLine);

  auto UnitDIE = U->getUnitDIE();
  if (!UnitDIE)
    return nullptr;

  auto Offset = toSectionOffset(UnitDIE.find(DW_AT_stmt_list));
  if (!Offset)
    return nullptr; // No line table for this compile unit.

  uint32_t stmtOffset = *Offset + U->getLineTableOffset();
  // See if the line table is cached.
  if (const DWARFLineTable *lt = Line->getLineTable(stmtOffset))
    return lt;

  // Make sure the offset is good before we try to parse.
  if (stmtOffset >= U->getLineSection().Data.size())
    return nullptr;

  // We have to parse it first.
  DWARFDataExtractor lineData(*DObj, U->getLineSection(), isLittleEndian(),
                              U->getAddressByteSize());
  return Line->getOrParseLineTable(lineData, stmtOffset, *this, U,
                                   RecoverableErrorCallback);
}

void DWARFContext::parseNormalUnits() {
  if (!NormalUnits.empty())
    return;
  DObj->forEachInfoSections([&](const DWARFSection &S) {
    NormalUnits.addUnitsForSection(*this, S, DW_SECT_INFO);
  });
  NormalUnits.finishedInfoUnits();
  DObj->forEachTypesSections([&](const DWARFSection &S) {
    NormalUnits.addUnitsForSection(*this, S, DW_SECT_TYPES);
  });
}

void DWARFContext::parseDWOUnits(bool Lazy) {
  if (!DWOUnits.empty())
    return;
  DObj->forEachInfoDWOSections([&](const DWARFSection &S) {
    DWOUnits.addUnitsForDWOSection(*this, S, DW_SECT_INFO, Lazy);
  });
  DWOUnits.finishedInfoUnits();
  DObj->forEachTypesDWOSections([&](const DWARFSection &S) {
    DWOUnits.addUnitsForDWOSection(*this, S, DW_SECT_TYPES, Lazy);
  });
}

DWARFCompileUnit *DWARFContext::getCompileUnitForOffset(uint32_t Offset) {
  parseNormalUnits();
  return dyn_cast_or_null<DWARFCompileUnit>(
      NormalUnits.getUnitForOffset(Offset));
}

DWARFCompileUnit *DWARFContext::getCompileUnitForAddress(uint64_t Address) {
  // First, get the offset of the compile unit.
  uint32_t CUOffset = getDebugAranges()->findAddress(Address);
  // Retrieve the compile unit.
  return getCompileUnitForOffset(CUOffset);
}

DWARFContext::DIEsForAddress DWARFContext::getDIEsForAddress(uint64_t Address) {
  DIEsForAddress Result;

  DWARFCompileUnit *CU = getCompileUnitForAddress(Address);
  if (!CU)
    return Result;

  Result.CompileUnit = CU;
  Result.FunctionDIE = CU->getSubroutineForAddress(Address);

  std::vector<DWARFDie> Worklist;
  Worklist.push_back(Result.FunctionDIE);
  while (!Worklist.empty()) {
    DWARFDie DIE = Worklist.back();
    Worklist.pop_back();

    if (DIE.getTag() == DW_TAG_lexical_block &&
        DIE.addressRangeContainsAddress(Address)) {
      Result.BlockDIE = DIE;
      break;
    }

    for (auto Child : DIE)
      Worklist.push_back(Child);
  }

  return Result;
}

static bool getFunctionNameAndStartLineForAddress(DWARFCompileUnit *CU,
                                                  uint64_t Address,
                                                  FunctionNameKind Kind,
                                                  std::string &FunctionName,
                                                  uint32_t &StartLine) {
  // The address may correspond to instruction in some inlined function,
  // so we have to build the chain of inlined functions and take the
  // name of the topmost function in it.
  SmallVector<DWARFDie, 4> InlinedChain;
  CU->getInlinedChainForAddress(Address, InlinedChain);
  if (InlinedChain.empty())
    return false;

  const DWARFDie &DIE = InlinedChain[0];
  bool FoundResult = false;
  const char *Name = nullptr;
  if (Kind != FunctionNameKind::None && (Name = DIE.getSubroutineName(Kind))) {
    FunctionName = Name;
    FoundResult = true;
  }
  if (auto DeclLineResult = DIE.getDeclLine()) {
    StartLine = DeclLineResult;
    FoundResult = true;
  }

  return FoundResult;
}

DILineInfo DWARFContext::getLineInfoForAddress(uint64_t Address,
                                               DILineInfoSpecifier Spec) {
  DILineInfo Result;

  DWARFCompileUnit *CU = getCompileUnitForAddress(Address);
  if (!CU)
    return Result;
  getFunctionNameAndStartLineForAddress(CU, Address, Spec.FNKind,
                                        Result.FunctionName,
                                        Result.StartLine);
  if (Spec.FLIKind != FileLineInfoKind::None) {
    if (const DWARFLineTable *LineTable = getLineTableForUnit(CU))
      LineTable->getFileLineInfoForAddress(Address, CU->getCompilationDir(),
                                           Spec.FLIKind, Result);
  }
  return Result;
}

DILineInfoTable
DWARFContext::getLineInfoForAddressRange(uint64_t Address, uint64_t Size,
                                         DILineInfoSpecifier Spec) {
  DILineInfoTable  Lines;
  DWARFCompileUnit *CU = getCompileUnitForAddress(Address);
  if (!CU)
    return Lines;

  std::string FunctionName = "<invalid>";
  uint32_t StartLine = 0;
  getFunctionNameAndStartLineForAddress(CU, Address, Spec.FNKind, FunctionName,
                                        StartLine);

  // If the Specifier says we don't need FileLineInfo, just
  // return the top-most function at the starting address.
  if (Spec.FLIKind == FileLineInfoKind::None) {
    DILineInfo Result;
    Result.FunctionName = FunctionName;
    Result.StartLine = StartLine;
    Lines.push_back(std::make_pair(Address, Result));
    return Lines;
  }

  const DWARFLineTable *LineTable = getLineTableForUnit(CU);

  // Get the index of row we're looking for in the line table.
  std::vector<uint32_t> RowVector;
  if (!LineTable->lookupAddressRange(Address, Size, RowVector))
    return Lines;

  for (uint32_t RowIndex : RowVector) {
    // Take file number and line/column from the row.
    const DWARFDebugLine::Row &Row = LineTable->Rows[RowIndex];
    DILineInfo Result;
    LineTable->getFileNameByIndex(Row.File, CU->getCompilationDir(),
                                  Spec.FLIKind, Result.FileName);
    Result.FunctionName = FunctionName;
    Result.Line = Row.Line;
    Result.Column = Row.Column;
    Result.StartLine = StartLine;
    Lines.push_back(std::make_pair(Row.Address, Result));
  }

  return Lines;
}

DIInliningInfo
DWARFContext::getInliningInfoForAddress(uint64_t Address,
                                        DILineInfoSpecifier Spec) {
  DIInliningInfo InliningInfo;

  DWARFCompileUnit *CU = getCompileUnitForAddress(Address);
  if (!CU)
    return InliningInfo;

  const DWARFLineTable *LineTable = nullptr;
  SmallVector<DWARFDie, 4> InlinedChain;
  CU->getInlinedChainForAddress(Address, InlinedChain);
  if (InlinedChain.size() == 0) {
    // If there is no DIE for address (e.g. it is in unavailable .dwo file),
    // try to at least get file/line info from symbol table.
    if (Spec.FLIKind != FileLineInfoKind::None) {
      DILineInfo Frame;
      LineTable = getLineTableForUnit(CU);
      if (LineTable &&
          LineTable->getFileLineInfoForAddress(Address, CU->getCompilationDir(),
                                               Spec.FLIKind, Frame))
        InliningInfo.addFrame(Frame);
    }
    return InliningInfo;
  }

  uint32_t CallFile = 0, CallLine = 0, CallColumn = 0, CallDiscriminator = 0;
  for (uint32_t i = 0, n = InlinedChain.size(); i != n; i++) {
    DWARFDie &FunctionDIE = InlinedChain[i];
    DILineInfo Frame;
    // Get function name if necessary.
    if (const char *Name = FunctionDIE.getSubroutineName(Spec.FNKind))
      Frame.FunctionName = Name;
    if (auto DeclLineResult = FunctionDIE.getDeclLine())
      Frame.StartLine = DeclLineResult;
    if (Spec.FLIKind != FileLineInfoKind::None) {
      if (i == 0) {
        // For the topmost frame, initialize the line table of this
        // compile unit and fetch file/line info from it.
        LineTable = getLineTableForUnit(CU);
        // For the topmost routine, get file/line info from line table.
        if (LineTable)
          LineTable->getFileLineInfoForAddress(Address, CU->getCompilationDir(),
                                               Spec.FLIKind, Frame);
      } else {
        // Otherwise, use call file, call line and call column from
        // previous DIE in inlined chain.
        if (LineTable)
          LineTable->getFileNameByIndex(CallFile, CU->getCompilationDir(),
                                        Spec.FLIKind, Frame.FileName);
        Frame.Line = CallLine;
        Frame.Column = CallColumn;
        Frame.Discriminator = CallDiscriminator;
      }
      // Get call file/line/column of a current DIE.
      if (i + 1 < n) {
        FunctionDIE.getCallerFrame(CallFile, CallLine, CallColumn,
                                   CallDiscriminator);
      }
    }
    InliningInfo.addFrame(Frame);
  }
  return InliningInfo;
}

std::shared_ptr<DWARFContext>
DWARFContext::getDWOContext(StringRef AbsolutePath) {
  if (auto S = DWP.lock()) {
    DWARFContext *Ctxt = S->Context.get();
    return std::shared_ptr<DWARFContext>(std::move(S), Ctxt);
  }

  std::weak_ptr<DWOFile> *Entry = &DWOFiles[AbsolutePath];

  if (auto S = Entry->lock()) {
    DWARFContext *Ctxt = S->Context.get();
    return std::shared_ptr<DWARFContext>(std::move(S), Ctxt);
  }

  Expected<OwningBinary<ObjectFile>> Obj = [&] {
    if (!CheckedForDWP) {
      SmallString<128> DWPName;
      auto Obj = object::ObjectFile::createObjectFile(
          this->DWPName.empty()
              ? (DObj->getFileName() + ".dwp").toStringRef(DWPName)
              : StringRef(this->DWPName));
      if (Obj) {
        Entry = &DWP;
        return Obj;
      } else {
        CheckedForDWP = true;
        // TODO: Should this error be handled (maybe in a high verbosity mode)
        // before falling back to .dwo files?
        consumeError(Obj.takeError());
      }
    }

    return object::ObjectFile::createObjectFile(AbsolutePath);
  }();

  if (!Obj) {
    // TODO: Actually report errors helpfully.
    consumeError(Obj.takeError());
    return nullptr;
  }

  auto S = std::make_shared<DWOFile>();
  S->File = std::move(Obj.get());
  S->Context = DWARFContext::create(*S->File.getBinary());
  *Entry = S;
  auto *Ctxt = S->Context.get();
  return std::shared_ptr<DWARFContext>(std::move(S), Ctxt);
}

static Error createError(const Twine &Reason, llvm::Error E) {
  return make_error<StringError>(Reason + toString(std::move(E)),
                                 inconvertibleErrorCode());
}

/// SymInfo contains information about symbol: it's address
/// and section index which is -1LL for absolute symbols.
struct SymInfo {
  uint64_t Address;
  uint64_t SectionIndex;
};

/// Returns the address of symbol relocation used against and a section index.
/// Used for futher relocations computation. Symbol's section load address is
static Expected<SymInfo> getSymbolInfo(const object::ObjectFile &Obj,
                                       const RelocationRef &Reloc,
                                       const LoadedObjectInfo *L,
                                       std::map<SymbolRef, SymInfo> &Cache) {
  SymInfo Ret = {0, (uint64_t)-1LL};
  object::section_iterator RSec = Obj.section_end();
  object::symbol_iterator Sym = Reloc.getSymbol();

  std::map<SymbolRef, SymInfo>::iterator CacheIt = Cache.end();
  // First calculate the address of the symbol or section as it appears
  // in the object file
  if (Sym != Obj.symbol_end()) {
    bool New;
    std::tie(CacheIt, New) = Cache.insert({*Sym, {0, 0}});
    if (!New)
      return CacheIt->second;

    Expected<uint64_t> SymAddrOrErr = Sym->getAddress();
    if (!SymAddrOrErr)
      return createError("failed to compute symbol address: ",
                         SymAddrOrErr.takeError());

    // Also remember what section this symbol is in for later
    auto SectOrErr = Sym->getSection();
    if (!SectOrErr)
      return createError("failed to get symbol section: ",
                         SectOrErr.takeError());

    RSec = *SectOrErr;
    Ret.Address = *SymAddrOrErr;
  } else if (auto *MObj = dyn_cast<MachOObjectFile>(&Obj)) {
    RSec = MObj->getRelocationSection(Reloc.getRawDataRefImpl());
    Ret.Address = RSec->getAddress();
  }

  if (RSec != Obj.section_end())
    Ret.SectionIndex = RSec->getIndex();

  // If we are given load addresses for the sections, we need to adjust:
  // SymAddr = (Address of Symbol Or Section in File) -
  //           (Address of Section in File) +
  //           (Load Address of Section)
  // RSec is now either the section being targeted or the section
  // containing the symbol being targeted. In either case,
  // we need to perform the same computation.
  if (L && RSec != Obj.section_end())
    if (uint64_t SectionLoadAddress = L->getSectionLoadAddress(*RSec))
      Ret.Address += SectionLoadAddress - RSec->getAddress();

  if (CacheIt != Cache.end())
    CacheIt->second = Ret;

  return Ret;
}

static bool isRelocScattered(const object::ObjectFile &Obj,
                             const RelocationRef &Reloc) {
  const MachOObjectFile *MachObj = dyn_cast<MachOObjectFile>(&Obj);
  if (!MachObj)
    return false;
  // MachO also has relocations that point to sections and
  // scattered relocations.
  auto RelocInfo = MachObj->getRelocation(Reloc.getRawDataRefImpl());
  return MachObj->isRelocationScattered(RelocInfo);
}

ErrorPolicy DWARFContext::defaultErrorHandler(Error E) {
  WithColor::error() << toString(std::move(E)) << '\n';
  return ErrorPolicy::Continue;
}

namespace {
struct DWARFSectionMap final : public DWARFSection {
  RelocAddrMap Relocs;
};

class DWARFObjInMemory final : public DWARFObject {
  bool IsLittleEndian;
  uint8_t AddressSize;
  StringRef FileName;
  const object::ObjectFile *Obj = nullptr;
  std::vector<SectionName> SectionNames;

  using InfoSectionMap = MapVector<object::SectionRef, DWARFSectionMap,
                                   std::map<object::SectionRef, unsigned>>;

  InfoSectionMap InfoSections;
  InfoSectionMap TypesSections;
  InfoSectionMap InfoDWOSections;
  InfoSectionMap TypesDWOSections;

  DWARFSectionMap LocSection;
  DWARFSectionMap LocListsSection;
  DWARFSectionMap LineSection;
  DWARFSectionMap RangeSection;
  DWARFSectionMap RnglistsSection;
  DWARFSectionMap StringOffsetSection;
  DWARFSectionMap LineDWOSection;
  DWARFSectionMap LocDWOSection;
  DWARFSectionMap StringOffsetDWOSection;
  DWARFSectionMap RangeDWOSection;
  DWARFSectionMap RnglistsDWOSection;
  DWARFSectionMap AddrSection;
  DWARFSectionMap AppleNamesSection;
  DWARFSectionMap AppleTypesSection;
  DWARFSectionMap AppleNamespacesSection;
  DWARFSectionMap AppleObjCSection;
  DWARFSectionMap DebugNamesSection;
  DWARFSectionMap PubNamesSection;
  DWARFSectionMap PubTypesSection;
  DWARFSectionMap GnuPubNamesSection;
  DWARFSectionMap GnuPubTypesSection;

  DWARFSectionMap *mapNameToDWARFSection(StringRef Name) {
    return StringSwitch<DWARFSectionMap *>(Name)
        .Case("debug_loc", &LocSection)
        .Case("debug_loclists", &LocListsSection)
        .Case("debug_line", &LineSection)
        .Case("debug_str_offsets", &StringOffsetSection)
        .Case("debug_ranges", &RangeSection)
        .Case("debug_rnglists", &RnglistsSection)
        .Case("debug_loc.dwo", &LocDWOSection)
        .Case("debug_line.dwo", &LineDWOSection)
        .Case("debug_names", &DebugNamesSection)
        .Case("debug_rnglists.dwo", &RnglistsDWOSection)
        .Case("debug_str_offsets.dwo", &StringOffsetDWOSection)
        .Case("debug_addr", &AddrSection)
        .Case("apple_names", &AppleNamesSection)
        .Case("debug_pubnames", &PubNamesSection)
        .Case("debug_pubtypes", &PubTypesSection)
        .Case("debug_gnu_pubnames", &GnuPubNamesSection)
        .Case("debug_gnu_pubtypes", &GnuPubTypesSection)
        .Case("apple_types", &AppleTypesSection)
        .Case("apple_namespaces", &AppleNamespacesSection)
        .Case("apple_namespac", &AppleNamespacesSection)
        .Case("apple_objc", &AppleObjCSection)
        .Default(nullptr);
  }

  StringRef AbbrevSection;
  StringRef ARangeSection;
  StringRef DebugFrameSection;
  StringRef EHFrameSection;
  StringRef StringSection;
  StringRef MacinfoSection;
  StringRef AbbrevDWOSection;
  StringRef StringDWOSection;
  StringRef CUIndexSection;
  StringRef GdbIndexSection;
  StringRef TUIndexSection;
  StringRef LineStringSection;

  // A deque holding section data whose iterators are not invalidated when
  // new decompressed sections are inserted at the end.
  std::deque<SmallString<0>> UncompressedSections;

  StringRef *mapSectionToMember(StringRef Name) {
    if (DWARFSection *Sec = mapNameToDWARFSection(Name))
      return &Sec->Data;
    return StringSwitch<StringRef *>(Name)
        .Case("debug_abbrev", &AbbrevSection)
        .Case("debug_aranges", &ARangeSection)
        .Case("debug_frame", &DebugFrameSection)
        .Case("eh_frame", &EHFrameSection)
        .Case("debug_str", &StringSection)
        .Case("debug_macinfo", &MacinfoSection)
        .Case("debug_abbrev.dwo", &AbbrevDWOSection)
        .Case("debug_str.dwo", &StringDWOSection)
        .Case("debug_cu_index", &CUIndexSection)
        .Case("debug_tu_index", &TUIndexSection)
        .Case("gdb_index", &GdbIndexSection)
        .Case("debug_line_str", &LineStringSection)
        // Any more debug info sections go here.
        .Default(nullptr);
  }

  /// If Sec is compressed section, decompresses and updates its contents
  /// provided by Data. Otherwise leaves it unchanged.
  Error maybeDecompress(const object::SectionRef &Sec, StringRef Name,
                        StringRef &Data) {
    if (!Decompressor::isCompressed(Sec))
      return Error::success();

    Expected<Decompressor> Decompressor =
        Decompressor::create(Name, Data, IsLittleEndian, AddressSize == 8);
    if (!Decompressor)
      return Decompressor.takeError();

    SmallString<0> Out;
    if (auto Err = Decompressor->resizeAndDecompress(Out))
      return Err;

    UncompressedSections.push_back(std::move(Out));
    Data = UncompressedSections.back();

    return Error::success();
  }

public:
  DWARFObjInMemory(const StringMap<std::unique_ptr<MemoryBuffer>> &Sections,
                   uint8_t AddrSize, bool IsLittleEndian)
      : IsLittleEndian(IsLittleEndian) {
    for (const auto &SecIt : Sections) {
      if (StringRef *SectionData = mapSectionToMember(SecIt.first()))
        *SectionData = SecIt.second->getBuffer();
      else if (SecIt.first() == "debug_info")
        // Find debug_info and debug_types data by section rather than name as
        // there are multiple, comdat grouped, of these sections.
        InfoSections[SectionRef()].Data = SecIt.second->getBuffer();
      else if (SecIt.first() == "debug_info.dwo")
        InfoDWOSections[SectionRef()].Data = SecIt.second->getBuffer();
      else if (SecIt.first() == "debug_types")
        TypesSections[SectionRef()].Data = SecIt.second->getBuffer();
      else if (SecIt.first() == "debug_types.dwo")
        TypesDWOSections[SectionRef()].Data = SecIt.second->getBuffer();
    }
  }
  DWARFObjInMemory(const object::ObjectFile &Obj, const LoadedObjectInfo *L,
                   function_ref<ErrorPolicy(Error)> HandleError)
      : IsLittleEndian(Obj.isLittleEndian()),
        AddressSize(Obj.getBytesInAddress()), FileName(Obj.getFileName()),
        Obj(&Obj) {

    StringMap<unsigned> SectionAmountMap;
    for (const SectionRef &Section : Obj.sections()) {
      StringRef Name;
      Section.getName(Name);
      ++SectionAmountMap[Name];
      SectionNames.push_back({ Name, true });

      // Skip BSS and Virtual sections, they aren't interesting.
      if (Section.isBSS() || Section.isVirtual())
        continue;

      // Skip sections stripped by dsymutil.
      if (Section.isStripped())
        continue;

      StringRef Data;
      section_iterator RelocatedSection = Section.getRelocatedSection();
      // Try to obtain an already relocated version of this section.
      // Else use the unrelocated section from the object file. We'll have to
      // apply relocations ourselves later.
      if (!L || !L->getLoadedSectionContents(*RelocatedSection, Data))
        Section.getContents(Data);

      if (auto Err = maybeDecompress(Section, Name, Data)) {
        ErrorPolicy EP = HandleError(createError(
            "failed to decompress '" + Name + "', ", std::move(Err)));
        if (EP == ErrorPolicy::Halt)
          return;
        continue;
      }

      // Compressed sections names in GNU style starts from ".z",
      // at this point section is decompressed and we drop compression prefix.
      Name = Name.substr(
          Name.find_first_not_of("._z")); // Skip ".", "z" and "_" prefixes.

      // Map platform specific debug section names to DWARF standard section
      // names.
      Name = Obj.mapDebugSectionName(Name);

      if (StringRef *SectionData = mapSectionToMember(Name)) {
        *SectionData = Data;
        if (Name == "debug_ranges") {
          // FIXME: Use the other dwo range section when we emit it.
          RangeDWOSection.Data = Data;
        }
      } else if (Name == "debug_info") {
        // Find debug_info and debug_types data by section rather than name as
        // there are multiple, comdat grouped, of these sections.
        InfoSections[Section].Data = Data;
      } else if (Name == "debug_info.dwo") {
        InfoDWOSections[Section].Data = Data;
      } else if (Name == "debug_types") {
        TypesSections[Section].Data = Data;
      } else if (Name == "debug_types.dwo") {
        TypesDWOSections[Section].Data = Data;
      }

      if (RelocatedSection == Obj.section_end())
        continue;

      StringRef RelSecName;
      StringRef RelSecData;
      RelocatedSection->getName(RelSecName);

      // If the section we're relocating was relocated already by the JIT,
      // then we used the relocated version above, so we do not need to process
      // relocations for it now.
      if (L && L->getLoadedSectionContents(*RelocatedSection, RelSecData))
        continue;

      // In Mach-o files, the relocations do not need to be applied if
      // there is no load offset to apply. The value read at the
      // relocation point already factors in the section address
      // (actually applying the relocations will produce wrong results
      // as the section address will be added twice).
      if (!L && isa<MachOObjectFile>(&Obj))
        continue;

      RelSecName = RelSecName.substr(
          RelSecName.find_first_not_of("._z")); // Skip . and _ prefixes.

      // TODO: Add support for relocations in other sections as needed.
      // Record relocations for the debug_info and debug_line sections.
      DWARFSectionMap *Sec = mapNameToDWARFSection(RelSecName);
      RelocAddrMap *Map = Sec ? &Sec->Relocs : nullptr;
      if (!Map) {
        // Find debug_info and debug_types relocs by section rather than name
        // as there are multiple, comdat grouped, of these sections.
        if (RelSecName == "debug_info")
          Map = &static_cast<DWARFSectionMap &>(InfoSections[*RelocatedSection])
                     .Relocs;
        else if (RelSecName == "debug_info.dwo")
          Map = &static_cast<DWARFSectionMap &>(
                     InfoDWOSections[*RelocatedSection])
                     .Relocs;
        else if (RelSecName == "debug_types")
          Map =
              &static_cast<DWARFSectionMap &>(TypesSections[*RelocatedSection])
                   .Relocs;
        else if (RelSecName == "debug_types.dwo")
          Map = &static_cast<DWARFSectionMap &>(
                     TypesDWOSections[*RelocatedSection])
                     .Relocs;
        else
          continue;
      }

      if (Section.relocation_begin() == Section.relocation_end())
        continue;

      // Symbol to [address, section index] cache mapping.
      std::map<SymbolRef, SymInfo> AddrCache;
      for (const RelocationRef &Reloc : Section.relocations()) {
        // FIXME: it's not clear how to correctly handle scattered
        // relocations.
        if (isRelocScattered(Obj, Reloc))
          continue;

        Expected<SymInfo> SymInfoOrErr =
            getSymbolInfo(Obj, Reloc, L, AddrCache);
        if (!SymInfoOrErr) {
          if (HandleError(SymInfoOrErr.takeError()) == ErrorPolicy::Halt)
            return;
          continue;
        }

        object::RelocVisitor V(Obj);
        uint64_t Val = V.visit(Reloc.getType(), Reloc, SymInfoOrErr->Address);
        if (V.error()) {
          SmallString<32> Type;
          Reloc.getTypeName(Type);
          ErrorPolicy EP = HandleError(
              createError("failed to compute relocation: " + Type + ", ",
                          errorCodeToError(object_error::parse_failed)));
          if (EP == ErrorPolicy::Halt)
            return;
          continue;
        }
        RelocAddrEntry Rel = {SymInfoOrErr->SectionIndex, Val};
        Map->insert({Reloc.getOffset(), Rel});
      }
    }

    for (SectionName &S : SectionNames)
      if (SectionAmountMap[S.Name] > 1)
        S.IsNameUnique = false;
  }

  Optional<RelocAddrEntry> find(const DWARFSection &S,
                                uint64_t Pos) const override {
    auto &Sec = static_cast<const DWARFSectionMap &>(S);
    RelocAddrMap::const_iterator AI = Sec.Relocs.find(Pos);
    if (AI == Sec.Relocs.end())
      return None;
    return AI->second;
  }

  const object::ObjectFile *getFile() const override { return Obj; }

  ArrayRef<SectionName> getSectionNames() const override {
    return SectionNames;
  }

  bool isLittleEndian() const override { return IsLittleEndian; }
  StringRef getAbbrevDWOSection() const override { return AbbrevDWOSection; }
  const DWARFSection &getLineDWOSection() const override {
    return LineDWOSection;
  }
  const DWARFSection &getLocDWOSection() const override {
    return LocDWOSection;
  }
  StringRef getStringDWOSection() const override { return StringDWOSection; }
  const DWARFSection &getStringOffsetDWOSection() const override {
    return StringOffsetDWOSection;
  }
  const DWARFSection &getRangeDWOSection() const override {
    return RangeDWOSection;
  }
  const DWARFSection &getRnglistsDWOSection() const override {
    return RnglistsDWOSection;
  }
  const DWARFSection &getAddrSection() const override { return AddrSection; }
  StringRef getCUIndexSection() const override { return CUIndexSection; }
  StringRef getGdbIndexSection() const override { return GdbIndexSection; }
  StringRef getTUIndexSection() const override { return TUIndexSection; }

  // DWARF v5
  const DWARFSection &getStringOffsetSection() const override {
    return StringOffsetSection;
  }
  StringRef getLineStringSection() const override { return LineStringSection; }

  // Sections for DWARF5 split dwarf proposal.
  void forEachInfoDWOSections(
      function_ref<void(const DWARFSection &)> F) const override {
    for (auto &P : InfoDWOSections)
      F(P.second);
  }
  void forEachTypesDWOSections(
      function_ref<void(const DWARFSection &)> F) const override {
    for (auto &P : TypesDWOSections)
      F(P.second);
  }

  StringRef getAbbrevSection() const override { return AbbrevSection; }
  const DWARFSection &getLocSection() const override { return LocSection; }
  const DWARFSection &getLoclistsSection() const override { return LocListsSection; }
  StringRef getARangeSection() const override { return ARangeSection; }
  StringRef getDebugFrameSection() const override { return DebugFrameSection; }
  StringRef getEHFrameSection() const override { return EHFrameSection; }
  const DWARFSection &getLineSection() const override { return LineSection; }
  StringRef getStringSection() const override { return StringSection; }
  const DWARFSection &getRangeSection() const override { return RangeSection; }
  const DWARFSection &getRnglistsSection() const override {
    return RnglistsSection;
  }
  StringRef getMacinfoSection() const override { return MacinfoSection; }
  const DWARFSection &getPubNamesSection() const override { return PubNamesSection; }
  const DWARFSection &getPubTypesSection() const override { return PubTypesSection; }
  const DWARFSection &getGnuPubNamesSection() const override {
    return GnuPubNamesSection;
  }
  const DWARFSection &getGnuPubTypesSection() const override {
    return GnuPubTypesSection;
  }
  const DWARFSection &getAppleNamesSection() const override {
    return AppleNamesSection;
  }
  const DWARFSection &getAppleTypesSection() const override {
    return AppleTypesSection;
  }
  const DWARFSection &getAppleNamespacesSection() const override {
    return AppleNamespacesSection;
  }
  const DWARFSection &getAppleObjCSection() const override {
    return AppleObjCSection;
  }
  const DWARFSection &getDebugNamesSection() const override {
    return DebugNamesSection;
  }

  StringRef getFileName() const override { return FileName; }
  uint8_t getAddressSize() const override { return AddressSize; }
  void forEachInfoSections(
      function_ref<void(const DWARFSection &)> F) const override {
    for (auto &P : InfoSections)
      F(P.second);
  }
  void forEachTypesSections(
      function_ref<void(const DWARFSection &)> F) const override {
    for (auto &P : TypesSections)
      F(P.second);
  }
};
} // namespace

std::unique_ptr<DWARFContext>
DWARFContext::create(const object::ObjectFile &Obj, const LoadedObjectInfo *L,
                     function_ref<ErrorPolicy(Error)> HandleError,
                     std::string DWPName) {
  auto DObj = llvm::make_unique<DWARFObjInMemory>(Obj, L, HandleError);
  return llvm::make_unique<DWARFContext>(std::move(DObj), std::move(DWPName));
}

std::unique_ptr<DWARFContext>
DWARFContext::create(const StringMap<std::unique_ptr<MemoryBuffer>> &Sections,
                     uint8_t AddrSize, bool isLittleEndian) {
  auto DObj =
      llvm::make_unique<DWARFObjInMemory>(Sections, AddrSize, isLittleEndian);
  return llvm::make_unique<DWARFContext>(std::move(DObj), "");
}

Error DWARFContext::loadRegisterInfo(const object::ObjectFile &Obj) {
  // Detect the architecture from the object file. We usually don't need OS
  // info to lookup a target and create register info.
  Triple TT;
  TT.setArch(Triple::ArchType(Obj.getArch()));
  TT.setVendor(Triple::UnknownVendor);
  TT.setOS(Triple::UnknownOS);
  std::string TargetLookupError;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(TT.str(), TargetLookupError);
  if (!TargetLookupError.empty())
    return createStringError(errc::invalid_argument,
                             TargetLookupError.c_str());
  RegInfo.reset(TheTarget->createMCRegInfo(TT.str()));
  return Error::success();
}

uint8_t DWARFContext::getCUAddrSize() {
  // In theory, different compile units may have different address byte
  // sizes, but for simplicity we just use the address byte size of the
  // last compile unit. In practice the address size field is repeated across
  // various DWARF headers (at least in version 5) to make it easier to dump
  // them independently, not to enable varying the address size.
  uint8_t Addr = 0;
  for (const auto &CU : compile_units()) {
    Addr = CU->getAddressByteSize();
    break;
  }
  return Addr;
}

void DWARFContext::dumpWarning(Error Warning) {
  handleAllErrors(std::move(Warning), [](ErrorInfoBase &Info) {
      WithColor::warning() << Info.message() << '\n';
  });
}
