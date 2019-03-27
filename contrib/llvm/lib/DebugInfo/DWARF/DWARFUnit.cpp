//===- DWARFUnit.cpp ------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/DWARF/DWARFAbbreviationDeclaration.h"
#include "llvm/DebugInfo/DWARF/DWARFCompileUnit.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugAbbrev.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugInfoEntry.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugRnglists.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/DebugInfo/DWARF/DWARFTypeUnit.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/WithColor.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

using namespace llvm;
using namespace dwarf;

void DWARFUnitVector::addUnitsForSection(DWARFContext &C,
                                         const DWARFSection &Section,
                                         DWARFSectionKind SectionKind) {
  const DWARFObject &D = C.getDWARFObj();
  addUnitsImpl(C, D, Section, C.getDebugAbbrev(), &D.getRangeSection(),
               &D.getLocSection(), D.getStringSection(),
               D.getStringOffsetSection(), &D.getAddrSection(),
               D.getLineSection(), D.isLittleEndian(), false, false,
               SectionKind);
}

void DWARFUnitVector::addUnitsForDWOSection(DWARFContext &C,
                                            const DWARFSection &DWOSection,
                                            DWARFSectionKind SectionKind,
                                            bool Lazy) {
  const DWARFObject &D = C.getDWARFObj();
  addUnitsImpl(C, D, DWOSection, C.getDebugAbbrevDWO(), &D.getRangeDWOSection(),
               &D.getLocDWOSection(), D.getStringDWOSection(),
               D.getStringOffsetDWOSection(), &D.getAddrSection(),
               D.getLineDWOSection(), C.isLittleEndian(), true, Lazy,
               SectionKind);
}

void DWARFUnitVector::addUnitsImpl(
    DWARFContext &Context, const DWARFObject &Obj, const DWARFSection &Section,
    const DWARFDebugAbbrev *DA, const DWARFSection *RS,
    const DWARFSection *LocSection, StringRef SS, const DWARFSection &SOS,
    const DWARFSection *AOS, const DWARFSection &LS, bool LE, bool IsDWO,
    bool Lazy, DWARFSectionKind SectionKind) {
  DWARFDataExtractor Data(Obj, Section, LE, 0);
  // Lazy initialization of Parser, now that we have all section info.
  if (!Parser) {
    Parser = [=, &Context, &Obj, &Section, &SOS,
              &LS](uint32_t Offset, DWARFSectionKind SectionKind,
                   const DWARFSection *CurSection,
                   const DWARFUnitIndex::Entry *IndexEntry)
        -> std::unique_ptr<DWARFUnit> {
      const DWARFSection &InfoSection = CurSection ? *CurSection : Section;
      DWARFDataExtractor Data(Obj, InfoSection, LE, 0);
      if (!Data.isValidOffset(Offset))
        return nullptr;
      const DWARFUnitIndex *Index = nullptr;
      if (IsDWO)
        Index = &getDWARFUnitIndex(Context, SectionKind);
      DWARFUnitHeader Header;
      if (!Header.extract(Context, Data, &Offset, SectionKind, Index,
                          IndexEntry))
        return nullptr;
      std::unique_ptr<DWARFUnit> U;
      if (Header.isTypeUnit())
        U = llvm::make_unique<DWARFTypeUnit>(Context, InfoSection, Header, DA,
                                             RS, LocSection, SS, SOS, AOS, LS,
                                             LE, IsDWO, *this);
      else
        U = llvm::make_unique<DWARFCompileUnit>(Context, InfoSection, Header,
                                                DA, RS, LocSection, SS, SOS,
                                                AOS, LS, LE, IsDWO, *this);
      return U;
    };
  }
  if (Lazy)
    return;
  // Find a reasonable insertion point within the vector.  We skip over
  // (a) units from a different section, (b) units from the same section
  // but with lower offset-within-section.  This keeps units in order
  // within a section, although not necessarily within the object file,
  // even if we do lazy parsing.
  auto I = this->begin();
  uint32_t Offset = 0;
  while (Data.isValidOffset(Offset)) {
    if (I != this->end() &&
        (&(*I)->getInfoSection() != &Section || (*I)->getOffset() == Offset)) {
      ++I;
      continue;
    }
    auto U = Parser(Offset, SectionKind, &Section, nullptr);
    // If parsing failed, we're done with this section.
    if (!U)
      break;
    Offset = U->getNextUnitOffset();
    I = std::next(this->insert(I, std::move(U)));
  }
}

DWARFUnit *DWARFUnitVector::addUnit(std::unique_ptr<DWARFUnit> Unit) {
  auto I = std::upper_bound(begin(), end(), Unit,
                            [](const std::unique_ptr<DWARFUnit> &LHS,
                               const std::unique_ptr<DWARFUnit> &RHS) {
                              return LHS->getOffset() < RHS->getOffset();
                            });
  return this->insert(I, std::move(Unit))->get();
}

DWARFUnit *DWARFUnitVector::getUnitForOffset(uint32_t Offset) const {
  auto end = begin() + getNumInfoUnits();
  auto *CU =
      std::upper_bound(begin(), end, Offset,
                       [](uint32_t LHS, const std::unique_ptr<DWARFUnit> &RHS) {
                         return LHS < RHS->getNextUnitOffset();
                       });
  if (CU != end && (*CU)->getOffset() <= Offset)
    return CU->get();
  return nullptr;
}

DWARFUnit *
DWARFUnitVector::getUnitForIndexEntry(const DWARFUnitIndex::Entry &E) {
  const auto *CUOff = E.getOffset(DW_SECT_INFO);
  if (!CUOff)
    return nullptr;

  auto Offset = CUOff->Offset;
  auto end = begin() + getNumInfoUnits();

  auto *CU =
      std::upper_bound(begin(), end, CUOff->Offset,
                       [](uint32_t LHS, const std::unique_ptr<DWARFUnit> &RHS) {
                         return LHS < RHS->getNextUnitOffset();
                       });
  if (CU != end && (*CU)->getOffset() <= Offset)
    return CU->get();

  if (!Parser)
    return nullptr;

  auto U = Parser(Offset, DW_SECT_INFO, nullptr, &E);
  if (!U)
    U = nullptr;

  auto *NewCU = U.get();
  this->insert(CU, std::move(U));
  ++NumInfoUnits;
  return NewCU;
}

DWARFUnit::DWARFUnit(DWARFContext &DC, const DWARFSection &Section,
                     const DWARFUnitHeader &Header, const DWARFDebugAbbrev *DA,
                     const DWARFSection *RS, const DWARFSection *LocSection,
                     StringRef SS, const DWARFSection &SOS,
                     const DWARFSection *AOS, const DWARFSection &LS, bool LE,
                     bool IsDWO, const DWARFUnitVector &UnitVector)
    : Context(DC), InfoSection(Section), Header(Header), Abbrev(DA),
      RangeSection(RS), LocSection(LocSection), LineSection(LS),
      StringSection(SS), StringOffsetSection(SOS), AddrOffsetSection(AOS),
      isLittleEndian(LE), IsDWO(IsDWO), UnitVector(UnitVector) {
  clear();
  // For split DWARF we only need to keep track of the location list section's
  // data (no relocations), and if we are reading a package file, we need to
  // adjust the location list data based on the index entries.
  if (IsDWO) {
    LocSectionData = LocSection->Data;
    if (auto *IndexEntry = Header.getIndexEntry())
      if (const auto *C = IndexEntry->getOffset(DW_SECT_LOC))
        LocSectionData = LocSectionData.substr(C->Offset, C->Length);
  }
}

DWARFUnit::~DWARFUnit() = default;

DWARFDataExtractor DWARFUnit::getDebugInfoExtractor() const {
  return DWARFDataExtractor(Context.getDWARFObj(), InfoSection, isLittleEndian,
                            getAddressByteSize());
}

Optional<SectionedAddress>
DWARFUnit::getAddrOffsetSectionItem(uint32_t Index) const {
  if (IsDWO) {
    auto R = Context.info_section_units();
    auto I = R.begin();
    // Surprising if a DWO file has more than one skeleton unit in it - this
    // probably shouldn't be valid, but if a use case is found, here's where to
    // support it (probably have to linearly search for the matching skeleton CU
    // here)
    if (I != R.end() && std::next(I) == R.end())
      return (*I)->getAddrOffsetSectionItem(Index);
  }
  uint32_t Offset = AddrOffsetSectionBase + Index * getAddressByteSize();
  if (AddrOffsetSection->Data.size() < Offset + getAddressByteSize())
    return None;
  DWARFDataExtractor DA(Context.getDWARFObj(), *AddrOffsetSection,
                        isLittleEndian, getAddressByteSize());
  uint64_t Section;
  uint64_t Address = DA.getRelocatedAddress(&Offset, &Section);
  return {{Address, Section}};
}

Optional<uint64_t> DWARFUnit::getStringOffsetSectionItem(uint32_t Index) const {
  if (!StringOffsetsTableContribution)
    return None;
  unsigned ItemSize = getDwarfStringOffsetsByteSize();
  uint32_t Offset = getStringOffsetsBase() + Index * ItemSize;
  if (StringOffsetSection.Data.size() < Offset + ItemSize)
    return None;
  DWARFDataExtractor DA(Context.getDWARFObj(), StringOffsetSection,
                        isLittleEndian, 0);
  return DA.getRelocatedValue(ItemSize, &Offset);
}

bool DWARFUnitHeader::extract(DWARFContext &Context,
                              const DWARFDataExtractor &debug_info,
                              uint32_t *offset_ptr,
                              DWARFSectionKind SectionKind,
                              const DWARFUnitIndex *Index,
                              const DWARFUnitIndex::Entry *Entry) {
  Offset = *offset_ptr;
  IndexEntry = Entry;
  if (!IndexEntry && Index)
    IndexEntry = Index->getFromOffset(*offset_ptr);
  Length = debug_info.getU32(offset_ptr);
  // FIXME: Support DWARF64.
  unsigned SizeOfLength = 4;
  FormParams.Format = DWARF32;
  FormParams.Version = debug_info.getU16(offset_ptr);
  if (FormParams.Version >= 5) {
    UnitType = debug_info.getU8(offset_ptr);
    FormParams.AddrSize = debug_info.getU8(offset_ptr);
    AbbrOffset = debug_info.getU32(offset_ptr);
  } else {
    AbbrOffset = debug_info.getRelocatedValue(4, offset_ptr);
    FormParams.AddrSize = debug_info.getU8(offset_ptr);
    // Fake a unit type based on the section type.  This isn't perfect,
    // but distinguishing compile and type units is generally enough.
    if (SectionKind == DW_SECT_TYPES)
      UnitType = DW_UT_type;
    else
      UnitType = DW_UT_compile;
  }
  if (IndexEntry) {
    if (AbbrOffset)
      return false;
    auto *UnitContrib = IndexEntry->getOffset();
    if (!UnitContrib || UnitContrib->Length != (Length + 4))
      return false;
    auto *AbbrEntry = IndexEntry->getOffset(DW_SECT_ABBREV);
    if (!AbbrEntry)
      return false;
    AbbrOffset = AbbrEntry->Offset;
  }
  if (isTypeUnit()) {
    TypeHash = debug_info.getU64(offset_ptr);
    TypeOffset = debug_info.getU32(offset_ptr);
  } else if (UnitType == DW_UT_split_compile || UnitType == DW_UT_skeleton)
    DWOId = debug_info.getU64(offset_ptr);

  // Header fields all parsed, capture the size of this unit header.
  assert(*offset_ptr - Offset <= 255 && "unexpected header size");
  Size = uint8_t(*offset_ptr - Offset);

  // Type offset is unit-relative; should be after the header and before
  // the end of the current unit.
  bool TypeOffsetOK =
      !isTypeUnit()
          ? true
          : TypeOffset >= Size && TypeOffset < getLength() + SizeOfLength;
  bool LengthOK = debug_info.isValidOffset(getNextUnitOffset() - 1);
  bool VersionOK = DWARFContext::isSupportedVersion(getVersion());
  bool AddrSizeOK = getAddressByteSize() == 4 || getAddressByteSize() == 8;

  if (!LengthOK || !VersionOK || !AddrSizeOK || !TypeOffsetOK)
    return false;

  // Keep track of the highest DWARF version we encounter across all units.
  Context.setMaxVersionIfGreater(getVersion());
  return true;
}

// Parse the rangelist table header, including the optional array of offsets
// following it (DWARF v5 and later).
static Expected<DWARFDebugRnglistTable>
parseRngListTableHeader(DWARFDataExtractor &DA, uint32_t Offset) {
  // TODO: Support DWARF64
  // We are expected to be called with Offset 0 or pointing just past the table
  // header, which is 12 bytes long for DWARF32.
  if (Offset > 0) {
    if (Offset < 12U)
      return createStringError(errc::invalid_argument, "Did not detect a valid"
                               " range list table with base = 0x%" PRIu32,
                               Offset);
    Offset -= 12U;
  }
  llvm::DWARFDebugRnglistTable Table;
  if (Error E = Table.extractHeaderAndOffsets(DA, &Offset))
    return std::move(E);
  return Table;
}

Error DWARFUnit::extractRangeList(uint32_t RangeListOffset,
                                  DWARFDebugRangeList &RangeList) const {
  // Require that compile unit is extracted.
  assert(!DieArray.empty());
  DWARFDataExtractor RangesData(Context.getDWARFObj(), *RangeSection,
                                isLittleEndian, getAddressByteSize());
  uint32_t ActualRangeListOffset = RangeSectionBase + RangeListOffset;
  return RangeList.extract(RangesData, &ActualRangeListOffset);
}

void DWARFUnit::clear() {
  Abbrevs = nullptr;
  BaseAddr.reset();
  RangeSectionBase = 0;
  AddrOffsetSectionBase = 0;
  clearDIEs(false);
  DWO.reset();
}

const char *DWARFUnit::getCompilationDir() {
  return dwarf::toString(getUnitDIE().find(DW_AT_comp_dir), nullptr);
}

void DWARFUnit::extractDIEsToVector(
    bool AppendCUDie, bool AppendNonCUDies,
    std::vector<DWARFDebugInfoEntry> &Dies) const {
  if (!AppendCUDie && !AppendNonCUDies)
    return;

  // Set the offset to that of the first DIE and calculate the start of the
  // next compilation unit header.
  uint32_t DIEOffset = getOffset() + getHeaderSize();
  uint32_t NextCUOffset = getNextUnitOffset();
  DWARFDebugInfoEntry DIE;
  DWARFDataExtractor DebugInfoData = getDebugInfoExtractor();
  uint32_t Depth = 0;
  bool IsCUDie = true;

  while (DIE.extractFast(*this, &DIEOffset, DebugInfoData, NextCUOffset,
                         Depth)) {
    if (IsCUDie) {
      if (AppendCUDie)
        Dies.push_back(DIE);
      if (!AppendNonCUDies)
        break;
      // The average bytes per DIE entry has been seen to be
      // around 14-20 so let's pre-reserve the needed memory for
      // our DIE entries accordingly.
      Dies.reserve(Dies.size() + getDebugInfoSize() / 14);
      IsCUDie = false;
    } else {
      Dies.push_back(DIE);
    }

    if (const DWARFAbbreviationDeclaration *AbbrDecl =
            DIE.getAbbreviationDeclarationPtr()) {
      // Normal DIE
      if (AbbrDecl->hasChildren())
        ++Depth;
    } else {
      // NULL DIE.
      if (Depth > 0)
        --Depth;
      if (Depth == 0)
        break;  // We are done with this compile unit!
    }
  }

  // Give a little bit of info if we encounter corrupt DWARF (our offset
  // should always terminate at or before the start of the next compilation
  // unit header).
  if (DIEOffset > NextCUOffset)
    WithColor::warning() << format("DWARF compile unit extends beyond its "
                                   "bounds cu 0x%8.8x at 0x%8.8x\n",
                                   getOffset(), DIEOffset);
}

size_t DWARFUnit::extractDIEsIfNeeded(bool CUDieOnly) {
  if ((CUDieOnly && !DieArray.empty()) ||
      DieArray.size() > 1)
    return 0; // Already parsed.

  bool HasCUDie = !DieArray.empty();
  extractDIEsToVector(!HasCUDie, !CUDieOnly, DieArray);

  if (DieArray.empty())
    return 0;

  // If CU DIE was just parsed, copy several attribute values from it.
  if (!HasCUDie) {
    DWARFDie UnitDie = getUnitDIE();
    if (Optional<uint64_t> DWOId = toUnsigned(UnitDie.find(DW_AT_GNU_dwo_id)))
      Header.setDWOId(*DWOId);
    if (!IsDWO) {
      assert(AddrOffsetSectionBase == 0);
      assert(RangeSectionBase == 0);
      AddrOffsetSectionBase = toSectionOffset(UnitDie.find(DW_AT_addr_base), 0);
      if (!AddrOffsetSectionBase)
        AddrOffsetSectionBase =
            toSectionOffset(UnitDie.find(DW_AT_GNU_addr_base), 0);
      RangeSectionBase = toSectionOffset(UnitDie.find(DW_AT_rnglists_base), 0);
    }

    // In general, in DWARF v5 and beyond we derive the start of the unit's
    // contribution to the string offsets table from the unit DIE's
    // DW_AT_str_offsets_base attribute. Split DWARF units do not use this
    // attribute, so we assume that there is a contribution to the string
    // offsets table starting at offset 0 of the debug_str_offsets.dwo section.
    // In both cases we need to determine the format of the contribution,
    // which may differ from the unit's format.
    DWARFDataExtractor DA(Context.getDWARFObj(), StringOffsetSection,
                          isLittleEndian, 0);
    if (IsDWO)
      StringOffsetsTableContribution =
          determineStringOffsetsTableContributionDWO(DA);
    else if (getVersion() >= 5)
      StringOffsetsTableContribution =
          determineStringOffsetsTableContribution(DA);

    // DWARF v5 uses the .debug_rnglists and .debug_rnglists.dwo sections to
    // describe address ranges.
    if (getVersion() >= 5) {
      if (IsDWO)
        setRangesSection(&Context.getDWARFObj().getRnglistsDWOSection(), 0);
      else
        setRangesSection(&Context.getDWARFObj().getRnglistsSection(),
                         toSectionOffset(UnitDie.find(DW_AT_rnglists_base), 0));
      if (RangeSection->Data.size()) {
        // Parse the range list table header. Individual range lists are
        // extracted lazily.
        DWARFDataExtractor RangesDA(Context.getDWARFObj(), *RangeSection,
                                    isLittleEndian, 0);
        if (auto TableOrError =
                parseRngListTableHeader(RangesDA, RangeSectionBase))
          RngListTable = TableOrError.get();
        else
          WithColor::error() << "parsing a range list table: "
                             << toString(TableOrError.takeError())
                             << '\n';

        // In a split dwarf unit, there is no DW_AT_rnglists_base attribute.
        // Adjust RangeSectionBase to point past the table header.
        if (IsDWO && RngListTable)
          RangeSectionBase = RngListTable->getHeaderSize();
      }
    }

    // Don't fall back to DW_AT_GNU_ranges_base: it should be ignored for
    // skeleton CU DIE, so that DWARF users not aware of it are not broken.
    }

  return DieArray.size();
}

bool DWARFUnit::parseDWO() {
  if (IsDWO)
    return false;
  if (DWO.get())
    return false;
  DWARFDie UnitDie = getUnitDIE();
  if (!UnitDie)
    return false;
  auto DWOFileName = dwarf::toString(UnitDie.find(DW_AT_GNU_dwo_name));
  if (!DWOFileName)
    return false;
  auto CompilationDir = dwarf::toString(UnitDie.find(DW_AT_comp_dir));
  SmallString<16> AbsolutePath;
  if (sys::path::is_relative(*DWOFileName) && CompilationDir &&
      *CompilationDir) {
    sys::path::append(AbsolutePath, *CompilationDir);
  }
  sys::path::append(AbsolutePath, *DWOFileName);
  auto DWOId = getDWOId();
  if (!DWOId)
    return false;
  auto DWOContext = Context.getDWOContext(AbsolutePath);
  if (!DWOContext)
    return false;

  DWARFCompileUnit *DWOCU = DWOContext->getDWOCompileUnitForHash(*DWOId);
  if (!DWOCU)
    return false;
  DWO = std::shared_ptr<DWARFCompileUnit>(std::move(DWOContext), DWOCU);
  // Share .debug_addr and .debug_ranges section with compile unit in .dwo
  DWO->setAddrOffsetSection(AddrOffsetSection, AddrOffsetSectionBase);
  if (getVersion() >= 5) {
    DWO->setRangesSection(&Context.getDWARFObj().getRnglistsDWOSection(), 0);
    DWARFDataExtractor RangesDA(Context.getDWARFObj(), *RangeSection,
                                isLittleEndian, 0);
    if (auto TableOrError = parseRngListTableHeader(RangesDA, RangeSectionBase))
      DWO->RngListTable = TableOrError.get();
    else
      WithColor::error() << "parsing a range list table: "
                         << toString(TableOrError.takeError())
                         << '\n';
    if (DWO->RngListTable)
      DWO->RangeSectionBase = DWO->RngListTable->getHeaderSize();
  } else {
    auto DWORangesBase = UnitDie.getRangesBaseAttribute();
    DWO->setRangesSection(RangeSection, DWORangesBase ? *DWORangesBase : 0);
  }

  return true;
}

void DWARFUnit::clearDIEs(bool KeepCUDie) {
  if (DieArray.size() > (unsigned)KeepCUDie) {
    DieArray.resize((unsigned)KeepCUDie);
    DieArray.shrink_to_fit();
  }
}

Expected<DWARFAddressRangesVector>
DWARFUnit::findRnglistFromOffset(uint32_t Offset) {
  if (getVersion() <= 4) {
    DWARFDebugRangeList RangeList;
    if (Error E = extractRangeList(Offset, RangeList))
      return std::move(E);
    return RangeList.getAbsoluteRanges(getBaseAddress());
  }
  if (RngListTable) {
    DWARFDataExtractor RangesData(Context.getDWARFObj(), *RangeSection,
                                  isLittleEndian, RngListTable->getAddrSize());
    auto RangeListOrError = RngListTable->findList(RangesData, Offset);
    if (RangeListOrError)
      return RangeListOrError.get().getAbsoluteRanges(getBaseAddress(), *this);
    return RangeListOrError.takeError();
  }

  return createStringError(errc::invalid_argument,
                           "missing or invalid range list table");
}

Expected<DWARFAddressRangesVector>
DWARFUnit::findRnglistFromIndex(uint32_t Index) {
  if (auto Offset = getRnglistOffset(Index))
    return findRnglistFromOffset(*Offset + RangeSectionBase);

  if (RngListTable)
    return createStringError(errc::invalid_argument,
                             "invalid range list table index %d", Index);
  else
    return createStringError(errc::invalid_argument,
                             "missing or invalid range list table");
}

Expected<DWARFAddressRangesVector> DWARFUnit::collectAddressRanges() {
  DWARFDie UnitDie = getUnitDIE();
  if (!UnitDie)
    return createStringError(errc::invalid_argument, "No unit DIE");

  // First, check if unit DIE describes address ranges for the whole unit.
  auto CUDIERangesOrError = UnitDie.getAddressRanges();
  if (!CUDIERangesOrError)
    return createStringError(errc::invalid_argument,
                             "decoding address ranges: %s",
                             toString(CUDIERangesOrError.takeError()).c_str());
  return *CUDIERangesOrError;
}

void DWARFUnit::updateAddressDieMap(DWARFDie Die) {
  if (Die.isSubroutineDIE()) {
    auto DIERangesOrError = Die.getAddressRanges();
    if (DIERangesOrError) {
      for (const auto &R : DIERangesOrError.get()) {
        // Ignore 0-sized ranges.
        if (R.LowPC == R.HighPC)
          continue;
        auto B = AddrDieMap.upper_bound(R.LowPC);
        if (B != AddrDieMap.begin() && R.LowPC < (--B)->second.first) {
          // The range is a sub-range of existing ranges, we need to split the
          // existing range.
          if (R.HighPC < B->second.first)
            AddrDieMap[R.HighPC] = B->second;
          if (R.LowPC > B->first)
            AddrDieMap[B->first].first = R.LowPC;
        }
        AddrDieMap[R.LowPC] = std::make_pair(R.HighPC, Die);
      }
    } else
      llvm::consumeError(DIERangesOrError.takeError());
  }
  // Parent DIEs are added to the AddrDieMap prior to the Children DIEs to
  // simplify the logic to update AddrDieMap. The child's range will always
  // be equal or smaller than the parent's range. With this assumption, when
  // adding one range into the map, it will at most split a range into 3
  // sub-ranges.
  for (DWARFDie Child = Die.getFirstChild(); Child; Child = Child.getSibling())
    updateAddressDieMap(Child);
}

DWARFDie DWARFUnit::getSubroutineForAddress(uint64_t Address) {
  extractDIEsIfNeeded(false);
  if (AddrDieMap.empty())
    updateAddressDieMap(getUnitDIE());
  auto R = AddrDieMap.upper_bound(Address);
  if (R == AddrDieMap.begin())
    return DWARFDie();
  // upper_bound's previous item contains Address.
  --R;
  if (Address >= R->second.first)
    return DWARFDie();
  return R->second.second;
}

void
DWARFUnit::getInlinedChainForAddress(uint64_t Address,
                                     SmallVectorImpl<DWARFDie> &InlinedChain) {
  assert(InlinedChain.empty());
  // Try to look for subprogram DIEs in the DWO file.
  parseDWO();
  // First, find the subroutine that contains the given address (the leaf
  // of inlined chain).
  DWARFDie SubroutineDIE =
      (DWO ? DWO.get() : this)->getSubroutineForAddress(Address);

  if (!SubroutineDIE)
    return;

  while (!SubroutineDIE.isSubprogramDIE()) {
    if (SubroutineDIE.getTag() == DW_TAG_inlined_subroutine)
      InlinedChain.push_back(SubroutineDIE);
    SubroutineDIE  = SubroutineDIE.getParent();
  }
  InlinedChain.push_back(SubroutineDIE);
}

const DWARFUnitIndex &llvm::getDWARFUnitIndex(DWARFContext &Context,
                                              DWARFSectionKind Kind) {
  if (Kind == DW_SECT_INFO)
    return Context.getCUIndex();
  assert(Kind == DW_SECT_TYPES);
  return Context.getTUIndex();
}

DWARFDie DWARFUnit::getParent(const DWARFDebugInfoEntry *Die) {
  if (!Die)
    return DWARFDie();
  const uint32_t Depth = Die->getDepth();
  // Unit DIEs always have a depth of zero and never have parents.
  if (Depth == 0)
    return DWARFDie();
  // Depth of 1 always means parent is the compile/type unit.
  if (Depth == 1)
    return getUnitDIE();
  // Look for previous DIE with a depth that is one less than the Die's depth.
  const uint32_t ParentDepth = Depth - 1;
  for (uint32_t I = getDIEIndex(Die) - 1; I > 0; --I) {
    if (DieArray[I].getDepth() == ParentDepth)
      return DWARFDie(this, &DieArray[I]);
  }
  return DWARFDie();
}

DWARFDie DWARFUnit::getSibling(const DWARFDebugInfoEntry *Die) {
  if (!Die)
    return DWARFDie();
  uint32_t Depth = Die->getDepth();
  // Unit DIEs always have a depth of zero and never have siblings.
  if (Depth == 0)
    return DWARFDie();
  // NULL DIEs don't have siblings.
  if (Die->getAbbreviationDeclarationPtr() == nullptr)
    return DWARFDie();

  // Find the next DIE whose depth is the same as the Die's depth.
  for (size_t I = getDIEIndex(Die) + 1, EndIdx = DieArray.size(); I < EndIdx;
       ++I) {
    if (DieArray[I].getDepth() == Depth)
      return DWARFDie(this, &DieArray[I]);
  }
  return DWARFDie();
}

DWARFDie DWARFUnit::getPreviousSibling(const DWARFDebugInfoEntry *Die) {
  if (!Die)
    return DWARFDie();
  uint32_t Depth = Die->getDepth();
  // Unit DIEs always have a depth of zero and never have siblings.
  if (Depth == 0)
    return DWARFDie();

  // Find the previous DIE whose depth is the same as the Die's depth.
  for (size_t I = getDIEIndex(Die); I > 0;) {
    --I;
    if (DieArray[I].getDepth() == Depth - 1)
      return DWARFDie();
    if (DieArray[I].getDepth() == Depth)
      return DWARFDie(this, &DieArray[I]);
  }
  return DWARFDie();
}

DWARFDie DWARFUnit::getFirstChild(const DWARFDebugInfoEntry *Die) {
  if (!Die->hasChildren())
    return DWARFDie();

  // We do not want access out of bounds when parsing corrupted debug data.
  size_t I = getDIEIndex(Die) + 1;
  if (I >= DieArray.size())
    return DWARFDie();
  return DWARFDie(this, &DieArray[I]);
}

DWARFDie DWARFUnit::getLastChild(const DWARFDebugInfoEntry *Die) {
  if (!Die->hasChildren())
    return DWARFDie();

  uint32_t Depth = Die->getDepth();
  for (size_t I = getDIEIndex(Die) + 1, EndIdx = DieArray.size(); I < EndIdx;
       ++I) {
    if (DieArray[I].getDepth() == Depth + 1 &&
        DieArray[I].getTag() == dwarf::DW_TAG_null)
      return DWARFDie(this, &DieArray[I]);
    assert(DieArray[I].getDepth() > Depth && "Not processing children?");
  }
  return DWARFDie();
}

const DWARFAbbreviationDeclarationSet *DWARFUnit::getAbbreviations() const {
  if (!Abbrevs)
    Abbrevs = Abbrev->getAbbreviationDeclarationSet(Header.getAbbrOffset());
  return Abbrevs;
}

llvm::Optional<SectionedAddress> DWARFUnit::getBaseAddress() {
  if (BaseAddr)
    return BaseAddr;

  DWARFDie UnitDie = getUnitDIE();
  Optional<DWARFFormValue> PC = UnitDie.find({DW_AT_low_pc, DW_AT_entry_pc});
  BaseAddr = toSectionedAddress(PC);
  return BaseAddr;
}

Optional<StrOffsetsContributionDescriptor>
StrOffsetsContributionDescriptor::validateContributionSize(
    DWARFDataExtractor &DA) {
  uint8_t EntrySize = getDwarfOffsetByteSize();
  // In order to ensure that we don't read a partial record at the end of
  // the section we validate for a multiple of the entry size.
  uint64_t ValidationSize = alignTo(Size, EntrySize);
  // Guard against overflow.
  if (ValidationSize >= Size)
    if (DA.isValidOffsetForDataOfSize((uint32_t)Base, ValidationSize))
      return *this;
  return None;
}

// Look for a DWARF64-formatted contribution to the string offsets table
// starting at a given offset and record it in a descriptor.
static Optional<StrOffsetsContributionDescriptor>
parseDWARF64StringOffsetsTableHeader(DWARFDataExtractor &DA, uint32_t Offset) {
  if (!DA.isValidOffsetForDataOfSize(Offset, 16))
    return None;

  if (DA.getU32(&Offset) != 0xffffffff)
    return None;

  uint64_t Size = DA.getU64(&Offset);
  uint8_t Version = DA.getU16(&Offset);
  (void)DA.getU16(&Offset); // padding
  // The encoded length includes the 2-byte version field and the 2-byte
  // padding, so we need to subtract them out when we populate the descriptor.
  return {{Offset, Size - 4, Version, DWARF64}};
}

// Look for a DWARF32-formatted contribution to the string offsets table
// starting at a given offset and record it in a descriptor.
static Optional<StrOffsetsContributionDescriptor>
parseDWARF32StringOffsetsTableHeader(DWARFDataExtractor &DA, uint32_t Offset) {
  if (!DA.isValidOffsetForDataOfSize(Offset, 8))
    return None;
  uint32_t ContributionSize = DA.getU32(&Offset);
  if (ContributionSize >= 0xfffffff0)
    return None;
  uint8_t Version = DA.getU16(&Offset);
  (void)DA.getU16(&Offset); // padding
  // The encoded length includes the 2-byte version field and the 2-byte
  // padding, so we need to subtract them out when we populate the descriptor.
  return {{Offset, ContributionSize - 4, Version, DWARF32}};
}

Optional<StrOffsetsContributionDescriptor>
DWARFUnit::determineStringOffsetsTableContribution(DWARFDataExtractor &DA) {
  auto Offset = toSectionOffset(getUnitDIE().find(DW_AT_str_offsets_base), 0);
  Optional<StrOffsetsContributionDescriptor> Descriptor;
  // Attempt to find a DWARF64 contribution 16 bytes before the base.
  if (Offset >= 16)
    Descriptor =
        parseDWARF64StringOffsetsTableHeader(DA, (uint32_t)Offset - 16);
  // Try to find a DWARF32 contribution 8 bytes before the base.
  if (!Descriptor && Offset >= 8)
    Descriptor = parseDWARF32StringOffsetsTableHeader(DA, (uint32_t)Offset - 8);
  return Descriptor ? Descriptor->validateContributionSize(DA) : Descriptor;
}

Optional<StrOffsetsContributionDescriptor>
DWARFUnit::determineStringOffsetsTableContributionDWO(DWARFDataExtractor & DA) {
  uint64_t Offset = 0;
  auto IndexEntry = Header.getIndexEntry();
  const auto *C =
      IndexEntry ? IndexEntry->getOffset(DW_SECT_STR_OFFSETS) : nullptr;
  if (C)
    Offset = C->Offset;
  if (getVersion() >= 5) {
    // Look for a valid contribution at the given offset.
    auto Descriptor =
        parseDWARF64StringOffsetsTableHeader(DA, (uint32_t)Offset);
    if (!Descriptor)
      Descriptor = parseDWARF32StringOffsetsTableHeader(DA, (uint32_t)Offset);
    return Descriptor ? Descriptor->validateContributionSize(DA) : Descriptor;
  }
  // Prior to DWARF v5, we derive the contribution size from the
  // index table (in a package file). In a .dwo file it is simply
  // the length of the string offsets section.
  if (!IndexEntry)
    return {{0, StringOffsetSection.Data.size(), 4, DWARF32}};
  if (C)
    return {{C->Offset, C->Length, 4, DWARF32}};
  return None;
}
