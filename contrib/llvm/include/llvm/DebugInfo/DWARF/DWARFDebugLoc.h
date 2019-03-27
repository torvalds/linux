//===- DWARFDebugLoc.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDEBUGLOC_H
#define LLVM_DEBUGINFO_DWARF_DWARFDEBUGLOC_H

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/DebugInfo/DWARF/DWARFRelocMap.h"
#include <cstdint>

namespace llvm {
class DWARFUnit;
class MCRegisterInfo;
class raw_ostream;

class DWARFDebugLoc {
public:
  /// A single location within a location list.
  struct Entry {
    /// The beginning address of the instruction range.
    uint64_t Begin;
    /// The ending address of the instruction range.
    uint64_t End;
    /// The location of the variable within the specified range.
    SmallVector<char, 4> Loc;
  };

  /// A list of locations that contain one variable.
  struct LocationList {
    /// The beginning offset where this location list is stored in the debug_loc
    /// section.
    unsigned Offset;
    /// All the locations in which the variable is stored.
    SmallVector<Entry, 2> Entries;
    /// Dump this list on OS.
    void dump(raw_ostream &OS, bool IsLittleEndian, unsigned AddressSize,
              const MCRegisterInfo *MRI, uint64_t BaseAddress,
              unsigned Indent) const;
  };

private:
  using LocationLists = SmallVector<LocationList, 4>;

  /// A list of all the variables in the debug_loc section, each one describing
  /// the locations in which the variable is stored.
  LocationLists Locations;

  unsigned AddressSize;

  bool IsLittleEndian;

public:
  /// Print the location lists found within the debug_loc section.
  void dump(raw_ostream &OS, const MCRegisterInfo *RegInfo,
            Optional<uint64_t> Offset) const;

  /// Parse the debug_loc section accessible via the 'data' parameter using the
  /// address size also given in 'data' to interpret the address ranges.
  void parse(const DWARFDataExtractor &data);

  /// Return the location list at the given offset or nullptr.
  LocationList const *getLocationListAtOffset(uint64_t Offset) const;

  Optional<LocationList> parseOneLocationList(DWARFDataExtractor Data,
                                              uint32_t *Offset);
};

class DWARFDebugLoclists {
public:
  struct Entry {
    uint8_t Kind;
    uint64_t Value0;
    uint64_t Value1;
    SmallVector<char, 4> Loc;
  };

  struct LocationList {
    unsigned Offset;
    SmallVector<Entry, 2> Entries;
    void dump(raw_ostream &OS, uint64_t BaseAddr, bool IsLittleEndian,
              unsigned AddressSize, const MCRegisterInfo *RegInfo,
              unsigned Indent) const;
  };

private:
  using LocationLists = SmallVector<LocationList, 4>;

  LocationLists Locations;

  unsigned AddressSize;

  bool IsLittleEndian;

public:
  void parse(DataExtractor data, unsigned Version);
  void dump(raw_ostream &OS, uint64_t BaseAddr, const MCRegisterInfo *RegInfo,
            Optional<uint64_t> Offset) const;

  /// Return the location list at the given offset or nullptr.
  LocationList const *getLocationListAtOffset(uint64_t Offset) const;

  static Optional<LocationList>
  parseOneLocationList(DataExtractor Data, unsigned *Offset, unsigned Version);
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDEBUGLOC_H
