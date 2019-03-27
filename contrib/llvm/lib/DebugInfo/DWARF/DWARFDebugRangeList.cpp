//===- DWARFDebugRangesList.cpp -------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFDebugRangeList.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cinttypes>
#include <cstdint>

using namespace llvm;

void DWARFDebugRangeList::clear() {
  Offset = -1U;
  AddressSize = 0;
  Entries.clear();
}

Error DWARFDebugRangeList::extract(const DWARFDataExtractor &data,
                                   uint32_t *offset_ptr) {
  clear();
  if (!data.isValidOffset(*offset_ptr))
    return createStringError(errc::invalid_argument,
                       "invalid range list offset 0x%" PRIx32, *offset_ptr);

  AddressSize = data.getAddressSize();
  if (AddressSize != 4 && AddressSize != 8)
    return createStringError(errc::invalid_argument,
                       "invalid address size: %" PRIu8, AddressSize);
  Offset = *offset_ptr;
  while (true) {
    RangeListEntry Entry;
    Entry.SectionIndex = -1ULL;

    uint32_t prev_offset = *offset_ptr;
    Entry.StartAddress = data.getRelocatedAddress(offset_ptr);
    Entry.EndAddress =
        data.getRelocatedAddress(offset_ptr, &Entry.SectionIndex);

    // Check that both values were extracted correctly.
    if (*offset_ptr != prev_offset + 2 * AddressSize) {
      clear();
      return createStringError(errc::invalid_argument,
                         "invalid range list entry at offset 0x%" PRIx32,
                         prev_offset);
    }
    if (Entry.isEndOfListEntry())
      break;
    Entries.push_back(Entry);
  }
  return Error::success();
}

void DWARFDebugRangeList::dump(raw_ostream &OS) const {
  for (const RangeListEntry &RLE : Entries) {
    const char *format_str = (AddressSize == 4
                              ? "%08x %08"  PRIx64 " %08"  PRIx64 "\n"
                              : "%08x %016" PRIx64 " %016" PRIx64 "\n");
    OS << format(format_str, Offset, RLE.StartAddress, RLE.EndAddress);
  }
  OS << format("%08x <End of list>\n", Offset);
}

DWARFAddressRangesVector DWARFDebugRangeList::getAbsoluteRanges(
    llvm::Optional<SectionedAddress> BaseAddr) const {
  DWARFAddressRangesVector Res;
  for (const RangeListEntry &RLE : Entries) {
    if (RLE.isBaseAddressSelectionEntry(AddressSize)) {
      BaseAddr = {RLE.EndAddress, RLE.SectionIndex};
      continue;
    }

    DWARFAddressRange E;
    E.LowPC = RLE.StartAddress;
    E.HighPC = RLE.EndAddress;
    E.SectionIndex = RLE.SectionIndex;
    // Base address of a range list entry is determined by the closest preceding
    // base address selection entry in the same range list. It defaults to the
    // base address of the compilation unit if there is no such entry.
    if (BaseAddr) {
      E.LowPC += BaseAddr->Address;
      E.HighPC += BaseAddr->Address;
      if (E.SectionIndex == -1ULL)
        E.SectionIndex = BaseAddr->SectionIndex;
    }
    Res.push_back(E);
  }
  return Res;
}
