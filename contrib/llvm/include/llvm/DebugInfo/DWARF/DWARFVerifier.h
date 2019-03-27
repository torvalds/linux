//===- DWARFVerifier.h ----------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFVERIFIER_H
#define LLVM_DEBUGINFO_DWARF_DWARFVERIFIER_H

#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFAcceleratorTable.h"
#include "llvm/DebugInfo/DWARF/DWARFAddressRange.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/DebugInfo/DWARF/DWARFUnitIndex.h"

#include <cstdint>
#include <map>
#include <set>

namespace llvm {
class raw_ostream;
struct DWARFAttribute;
class DWARFContext;
class DWARFDie;
class DWARFUnit;
class DWARFCompileUnit;
class DWARFDataExtractor;
class DWARFDebugAbbrev;
class DataExtractor;
struct DWARFSection;

/// A class that verifies DWARF debug information given a DWARF Context.
class DWARFVerifier {
public:
  /// A class that keeps the address range information for a single DIE.
  struct DieRangeInfo {
    DWARFDie Die;

    /// Sorted DWARFAddressRanges.
    std::vector<DWARFAddressRange> Ranges;

    /// Sorted DWARFAddressRangeInfo.
    std::set<DieRangeInfo> Children;

    DieRangeInfo() = default;
    DieRangeInfo(DWARFDie Die) : Die(Die) {}

    /// Used for unit testing.
    DieRangeInfo(std::vector<DWARFAddressRange> Ranges)
        : Ranges(std::move(Ranges)) {}

    typedef std::vector<DWARFAddressRange>::const_iterator
        address_range_iterator;
    typedef std::set<DieRangeInfo>::const_iterator die_range_info_iterator;

    /// Inserts the address range. If the range overlaps with an existing
    /// range, the range is *not* added and an iterator to the overlapping
    /// range is returned.
    ///
    /// This is used for finding overlapping ranges within the same DIE.
    address_range_iterator insert(const DWARFAddressRange &R);

    /// Finds an address range in the sorted vector of ranges.
    address_range_iterator findRange(const DWARFAddressRange &R) const {
      auto Begin = Ranges.begin();
      auto End = Ranges.end();
      auto Iter = std::upper_bound(Begin, End, R);
      if (Iter != Begin)
        --Iter;
      return Iter;
    }

    /// Inserts the address range info. If any of its ranges overlaps with a
    /// range in an existing range info, the range info is *not* added and an
    /// iterator to the overlapping range info.
    ///
    /// This is used for finding overlapping children of the same DIE.
    die_range_info_iterator insert(const DieRangeInfo &RI);

    /// Return true if ranges in this object contains all ranges within RHS.
    bool contains(const DieRangeInfo &RHS) const;

    /// Return true if any range in this object intersects with any range in
    /// RHS.
    bool intersects(const DieRangeInfo &RHS) const;
  };

private:
  raw_ostream &OS;
  DWARFContext &DCtx;
  DIDumpOptions DumpOpts;
  /// A map that tracks all references (converted absolute references) so we
  /// can verify each reference points to a valid DIE and not an offset that
  /// lies between to valid DIEs.
  std::map<uint64_t, std::set<uint32_t>> ReferenceToDIEOffsets;
  uint32_t NumDebugLineErrors = 0;
  // Used to relax some checks that do not currently work portably
  bool IsObjectFile;
  bool IsMachOObject;

  raw_ostream &error() const;
  raw_ostream &warn() const;
  raw_ostream &note() const;
  raw_ostream &dump(const DWARFDie &Die, unsigned indent = 0) const;

  /// Verifies the abbreviations section.
  ///
  /// This function currently checks that:
  /// --No abbreviation declaration has more than one attributes with the same
  /// name.
  ///
  /// \param Abbrev Pointer to the abbreviations section we are verifying
  /// Abbrev can be a pointer to either .debug_abbrev or debug_abbrev.dwo.
  ///
  /// \returns The number of errors that occurred during verification.
  unsigned verifyAbbrevSection(const DWARFDebugAbbrev *Abbrev);

  /// Verifies the header of a unit in a .debug_info or .debug_types section.
  ///
  /// This function currently checks for:
  /// - Unit is in 32-bit DWARF format. The function can be modified to
  /// support 64-bit format.
  /// - The DWARF version is valid
  /// - The unit type is valid (if unit is in version >=5)
  /// - The unit doesn't extend beyond the containing section
  /// - The address size is valid
  /// - The offset in the .debug_abbrev section is valid
  ///
  /// \param DebugInfoData The section data
  /// \param Offset A reference to the offset start of the unit. The offset will
  /// be updated to point to the next unit in the section
  /// \param UnitIndex The index of the unit to be verified
  /// \param UnitType A reference to the type of the unit
  /// \param isUnitDWARF64 A reference to a flag that shows whether the unit is
  /// in 64-bit format.
  ///
  /// \returns true if the header is verified successfully, false otherwise.
  bool verifyUnitHeader(const DWARFDataExtractor DebugInfoData,
                        uint32_t *Offset, unsigned UnitIndex, uint8_t &UnitType,
                        bool &isUnitDWARF64);

  /// Verifies the header of a unit in a .debug_info or .debug_types section.
  ///
  /// This function currently verifies:
  ///  - The debug info attributes.
  ///  - The debug info form=s.
  ///  - The presence of a root DIE.
  ///  - That the root DIE is a unit DIE.
  ///  - If a unit type is provided, that the unit DIE matches the unit type.
  ///  - The DIE ranges.
  ///  - That call site entries are only nested within subprograms with a
  ///    DW_AT_call attribute.
  ///
  /// \param Unit      The DWARF Unit to verify.
  ///
  /// \returns The number of errors that occurred during verification.
  unsigned verifyUnitContents(DWARFUnit &Unit);

  /// Verifies the unit headers and contents in a .debug_info or .debug_types
  /// section.
  ///
  /// \param S           The DWARF Section to verify.
  /// \param SectionKind The object-file section kind that S comes from.
  ///
  /// \returns The number of errors that occurred during verification.
  unsigned verifyUnitSection(const DWARFSection &S,
                             DWARFSectionKind SectionKind);

  /// Verifies that a call site entry is nested within a subprogram with a
  /// DW_AT_call attribute.
  ///
  /// \returns Number of errors that occurred during verification.
  unsigned verifyDebugInfoCallSite(const DWARFDie &Die);

  /// Verify that all Die ranges are valid.
  ///
  /// This function currently checks for:
  /// - cases in which lowPC >= highPC
  ///
  /// \returns Number of errors that occurred during verification.
  unsigned verifyDieRanges(const DWARFDie &Die, DieRangeInfo &ParentRI);

  /// Verifies the attribute's DWARF attribute and its value.
  ///
  /// This function currently checks for:
  /// - DW_AT_ranges values is a valid .debug_ranges offset
  /// - DW_AT_stmt_list is a valid .debug_line offset
  ///
  /// \param Die          The DWARF DIE that owns the attribute value
  /// \param AttrValue    The DWARF attribute value to check
  ///
  /// \returns NumErrors The number of errors occurred during verification of
  /// attributes' values in a unit
  unsigned verifyDebugInfoAttribute(const DWARFDie &Die,
                                    DWARFAttribute &AttrValue);

  /// Verifies the attribute's DWARF form.
  ///
  /// This function currently checks for:
  /// - All DW_FORM_ref values that are CU relative have valid CU offsets
  /// - All DW_FORM_ref_addr values have valid section offsets
  /// - All DW_FORM_strp values have valid .debug_str offsets
  ///
  /// \param Die          The DWARF DIE that owns the attribute value
  /// \param AttrValue    The DWARF attribute value to check
  ///
  /// \returns NumErrors The number of errors occurred during verification of
  /// attributes' forms in a unit
  unsigned verifyDebugInfoForm(const DWARFDie &Die, DWARFAttribute &AttrValue);

  /// Verifies the all valid references that were found when iterating through
  /// all of the DIE attributes.
  ///
  /// This function will verify that all references point to DIEs whose DIE
  /// offset matches. This helps to ensure if a DWARF link phase moved things
  /// around, that it doesn't create invalid references by failing to relocate
  /// CU relative and absolute references.
  ///
  /// \returns NumErrors The number of errors occurred during verification of
  /// references for the .debug_info and .debug_types sections
  unsigned verifyDebugInfoReferences();

  /// Verify the DW_AT_stmt_list encoding and value and ensure that no
  /// compile units that have the same DW_AT_stmt_list value.
  void verifyDebugLineStmtOffsets();

  /// Verify that all of the rows in the line table are valid.
  ///
  /// This function currently checks for:
  /// - addresses within a sequence that decrease in value
  /// - invalid file indexes
  void verifyDebugLineRows();

  /// Verify that an Apple-style accelerator table is valid.
  ///
  /// This function currently checks that:
  /// - The fixed part of the header fits in the section
  /// - The size of the section is as large as what the header describes
  /// - There is at least one atom
  /// - The form for each atom is valid
  /// - The tag for each DIE in the table is valid
  /// - The buckets have a valid index, or they are empty
  /// - Each hashdata offset is valid
  /// - Each DIE is valid
  ///
  /// \param AccelSection pointer to the section containing the acceleration table
  /// \param StrData pointer to the string section
  /// \param SectionName the name of the table we're verifying
  ///
  /// \returns The number of errors occurred during verification
  unsigned verifyAppleAccelTable(const DWARFSection *AccelSection,
                                 DataExtractor *StrData,
                                 const char *SectionName);

  unsigned verifyDebugNamesCULists(const DWARFDebugNames &AccelTable);
  unsigned verifyNameIndexBuckets(const DWARFDebugNames::NameIndex &NI,
                                  const DataExtractor &StrData);
  unsigned verifyNameIndexAbbrevs(const DWARFDebugNames::NameIndex &NI);
  unsigned verifyNameIndexAttribute(const DWARFDebugNames::NameIndex &NI,
                                    const DWARFDebugNames::Abbrev &Abbr,
                                    DWARFDebugNames::AttributeEncoding AttrEnc);
  unsigned verifyNameIndexEntries(const DWARFDebugNames::NameIndex &NI,
                                  const DWARFDebugNames::NameTableEntry &NTE);
  unsigned verifyNameIndexCompleteness(const DWARFDie &Die,
                                       const DWARFDebugNames::NameIndex &NI);

  /// Verify that the DWARF v5 accelerator table is valid.
  ///
  /// This function currently checks that:
  /// - Headers individual Name Indices fit into the section and can be parsed.
  /// - Abbreviation tables can be parsed and contain valid index attributes
  ///   with correct form encodings.
  /// - The CU lists reference existing compile units.
  /// - The buckets have a valid index, or they are empty.
  /// - All names are reachable via the hash table (they have the correct hash,
  ///   and the hash is in the correct bucket).
  /// - Information in the index entries is complete (all required entries are
  ///   present) and consistent with the debug_info section DIEs.
  ///
  /// \param AccelSection section containing the acceleration table
  /// \param StrData string section
  ///
  /// \returns The number of errors occurred during verification
  unsigned verifyDebugNames(const DWARFSection &AccelSection,
                            const DataExtractor &StrData);

public:
  DWARFVerifier(raw_ostream &S, DWARFContext &D,
                DIDumpOptions DumpOpts = DIDumpOptions::getForSingleDIE());

  /// Verify the information in any of the following sections, if available:
  /// .debug_abbrev, debug_abbrev.dwo
  ///
  /// Any errors are reported to the stream that was this object was
  /// constructed with.
  ///
  /// \returns true if .debug_abbrev and .debug_abbrev.dwo verify successfully,
  /// false otherwise.
  bool handleDebugAbbrev();

  /// Verify the information in the .debug_info and .debug_types sections.
  ///
  /// Any errors are reported to the stream that this object was
  /// constructed with.
  ///
  /// \returns true if all sections verify successfully, false otherwise.
  bool handleDebugInfo();

  /// Verify the information in the .debug_line section.
  ///
  /// Any errors are reported to the stream that was this object was
  /// constructed with.
  ///
  /// \returns true if the .debug_line verifies successfully, false otherwise.
  bool handleDebugLine();

  /// Verify the information in accelerator tables, if they exist.
  ///
  /// Any errors are reported to the stream that was this object was
  /// constructed with.
  ///
  /// \returns true if the existing Apple-style accelerator tables verify
  /// successfully, false otherwise.
  bool handleAccelTables();
};

static inline bool operator<(const DWARFVerifier::DieRangeInfo &LHS,
                             const DWARFVerifier::DieRangeInfo &RHS) {
  return std::tie(LHS.Ranges, LHS.Die) < std::tie(RHS.Ranges, RHS.Die);
}

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFCONTEXT_H
