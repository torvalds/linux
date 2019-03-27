//===- DWARFDebugPubTable.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDEBUGPUBTABLE_H
#define LLVM_DEBUGINFO_DWARF_DWARFDEBUGPUBTABLE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFObject.h"
#include <cstdint>
#include <vector>

namespace llvm {

class raw_ostream;

/// Represents structure for holding and parsing .debug_pub* tables.
class DWARFDebugPubTable {
public:
  struct Entry {
    /// Section offset from the beginning of the compilation unit.
    uint32_t SecOffset;

    /// An entry of the various gnu_pub* debug sections.
    dwarf::PubIndexEntryDescriptor Descriptor;

    /// The name of the object as given by the DW_AT_name attribute of the
    /// referenced DIE.
    StringRef Name;
  };

  /// Each table consists of sets of variable length entries. Each set describes
  /// the names of global objects and functions, or global types, respectively,
  /// whose definitions are represented by debugging information entries owned
  /// by a single compilation unit.
  struct Set {
    /// The total length of the entries for that set, not including the length
    /// field itself.
    uint32_t Length;

    /// This number is specific to the name lookup table and is independent of
    /// the DWARF version number.
    uint16_t Version;

    /// The offset from the beginning of the .debug_info section of the
    /// compilation unit header referenced by the set.
    uint32_t Offset;

    /// The size in bytes of the contents of the .debug_info section generated
    /// to represent that compilation unit.
    uint32_t Size;

    std::vector<Entry> Entries;
  };

private:
  std::vector<Set> Sets;

  /// gnu styled tables contains additional information.
  /// This flag determines whether or not section we parse is debug_gnu* table.
  bool GnuStyle;

public:
  DWARFDebugPubTable(const DWARFObject &Obj, const DWARFSection &Sec,
                     bool LittleEndian, bool GnuStyle);

  void dump(raw_ostream &OS) const;

  ArrayRef<Set> getData() { return Sets; }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDEBUGPUBTABLE_H
