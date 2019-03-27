//===- DWARFDebugRnglists.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARFDEBUGRNGLISTS_H
#define LLVM_DEBUGINFO_DWARFDEBUGRNGLISTS_H

#include "llvm/ADT/Optional.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugRangeList.h"
#include "llvm/DebugInfo/DWARF/DWARFListTable.h"
#include <cstdint>
#include <map>
#include <vector>

namespace llvm {

class Error;
class raw_ostream;
class DWARFUnit;

/// A class representing a single range list entry.
struct RangeListEntry : public DWARFListEntryBase {
  /// The values making up the range list entry. Most represent a range with
  /// a start and end address or a start address and a length. Others are
  /// single value base addresses or end-of-list with no values. The unneeded
  /// values are semantically undefined, but initialized to 0.
  uint64_t Value0;
  uint64_t Value1;

  Error extract(DWARFDataExtractor Data, uint32_t End, uint32_t *OffsetPtr);
  void dump(raw_ostream &OS, uint8_t AddrSize, uint8_t MaxEncodingStringLength,
            uint64_t &CurrentBase, DIDumpOptions DumpOpts,
            llvm::function_ref<Optional<SectionedAddress>(uint32_t)>
                LookupPooledAddress) const;
  bool isSentinel() const { return EntryKind == dwarf::DW_RLE_end_of_list; }
};

/// A class representing a single rangelist.
class DWARFDebugRnglist : public DWARFListType<RangeListEntry> {
public:
  /// Build a DWARFAddressRangesVector from a rangelist.
  DWARFAddressRangesVector
  getAbsoluteRanges(llvm::Optional<SectionedAddress> BaseAddr,
                    DWARFUnit &U) const;
};

class DWARFDebugRnglistTable : public DWARFListTableBase<DWARFDebugRnglist> {
public:
  DWARFDebugRnglistTable()
      : DWARFListTableBase(/* SectionName    = */ ".debug_rnglists",
                           /* HeaderString   = */ "ranges:",
                           /* ListTypeString = */ "range") {}
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARFDEBUGRNGLISTS_H
