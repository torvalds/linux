//===- DWARFUnitIndex.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFUNITINDEX_H
#define LLVM_DEBUGINFO_DWARF_DWARFUNITINDEX_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DataExtractor.h"
#include <cstdint>
#include <memory>

namespace llvm {

class raw_ostream;

enum DWARFSectionKind {
  DW_SECT_INFO = 1,
  DW_SECT_TYPES,
  DW_SECT_ABBREV,
  DW_SECT_LINE,
  DW_SECT_LOC,
  DW_SECT_STR_OFFSETS,
  DW_SECT_MACINFO,
  DW_SECT_MACRO,
};

class DWARFUnitIndex {
  struct Header {
    uint32_t Version;
    uint32_t NumColumns;
    uint32_t NumUnits;
    uint32_t NumBuckets = 0;

    bool parse(DataExtractor IndexData, uint32_t *OffsetPtr);
    void dump(raw_ostream &OS) const;
  };

public:
  class Entry {
  public:
    struct SectionContribution {
      uint32_t Offset;
      uint32_t Length;
    };

  private:
    const DWARFUnitIndex *Index;
    uint64_t Signature;
    std::unique_ptr<SectionContribution[]> Contributions;
    friend class DWARFUnitIndex;

  public:
    const SectionContribution *getOffset(DWARFSectionKind Sec) const;
    const SectionContribution *getOffset() const;

    const SectionContribution *getOffsets() const {
      return Contributions.get();
    }

    uint64_t getSignature() const { return Signature; }
  };

private:
  struct Header Header;

  DWARFSectionKind InfoColumnKind;
  int InfoColumn = -1;
  std::unique_ptr<DWARFSectionKind[]> ColumnKinds;
  std::unique_ptr<Entry[]> Rows;
  mutable std::vector<Entry *> OffsetLookup;

  static StringRef getColumnHeader(DWARFSectionKind DS);

  bool parseImpl(DataExtractor IndexData);

public:
  DWARFUnitIndex(DWARFSectionKind InfoColumnKind)
      : InfoColumnKind(InfoColumnKind) {}

  explicit operator bool() const { return Header.NumBuckets; }

  bool parse(DataExtractor IndexData);
  void dump(raw_ostream &OS) const;

  const Entry *getFromOffset(uint32_t Offset) const;
  const Entry *getFromHash(uint64_t Offset) const;

  ArrayRef<DWARFSectionKind> getColumnKinds() const {
    return makeArrayRef(ColumnKinds.get(), Header.NumColumns);
  }

  ArrayRef<Entry> getRows() const {
    return makeArrayRef(Rows.get(), Header.NumBuckets);
  }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFUNITINDEX_H
