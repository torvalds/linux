//===- DWARFDebugRnglists.cpp ---------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFDebugRnglists.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

Error RangeListEntry::extract(DWARFDataExtractor Data, uint32_t End,
                              uint32_t *OffsetPtr) {
  Offset = *OffsetPtr;
  SectionIndex = -1ULL;
  // The caller should guarantee that we have at least 1 byte available, so
  // we just assert instead of revalidate.
  assert(*OffsetPtr < End &&
         "not enough space to extract a rangelist encoding");
  uint8_t Encoding = Data.getU8(OffsetPtr);

  switch (Encoding) {
  case dwarf::DW_RLE_end_of_list:
    Value0 = Value1 = 0;
    break;
  // TODO: Support other encodings.
  case dwarf::DW_RLE_base_addressx: {
    uint32_t PreviousOffset = *OffsetPtr - 1;
    Value0 = Data.getULEB128(OffsetPtr);
    if (End < *OffsetPtr)
      return createStringError(
          errc::invalid_argument,
          "read past end of table when reading "
          "DW_RLE_base_addressx encoding at offset 0x%" PRIx32,
          PreviousOffset);
    break;
  }
  case dwarf::DW_RLE_startx_endx:
    return createStringError(errc::not_supported,
                       "unsupported rnglists encoding DW_RLE_startx_endx at "
                       "offset 0x%" PRIx32,
                       *OffsetPtr - 1);
  case dwarf::DW_RLE_startx_length: {
    uint32_t PreviousOffset = *OffsetPtr - 1;
    Value0 = Data.getULEB128(OffsetPtr);
    Value1 = Data.getULEB128(OffsetPtr);
    if (End < *OffsetPtr)
      return createStringError(
          errc::invalid_argument,
          "read past end of table when reading "
          "DW_RLE_startx_length encoding at offset 0x%" PRIx32,
          PreviousOffset);
    break;
  }
  case dwarf::DW_RLE_offset_pair: {
    uint32_t PreviousOffset = *OffsetPtr - 1;
    Value0 = Data.getULEB128(OffsetPtr);
    Value1 = Data.getULEB128(OffsetPtr);
    if (End < *OffsetPtr)
      return createStringError(errc::invalid_argument,
                         "read past end of table when reading "
                         "DW_RLE_offset_pair encoding at offset 0x%" PRIx32,
                         PreviousOffset);
    break;
  }
  case dwarf::DW_RLE_base_address: {
    if ((End - *OffsetPtr) < Data.getAddressSize())
      return createStringError(errc::invalid_argument,
                         "insufficient space remaining in table for "
                         "DW_RLE_base_address encoding at offset 0x%" PRIx32,
                         *OffsetPtr - 1);
    Value0 = Data.getRelocatedAddress(OffsetPtr, &SectionIndex);
    break;
  }
  case dwarf::DW_RLE_start_end: {
    if ((End - *OffsetPtr) < unsigned(Data.getAddressSize() * 2))
      return createStringError(errc::invalid_argument,
                         "insufficient space remaining in table for "
                         "DW_RLE_start_end encoding "
                         "at offset 0x%" PRIx32,
                         *OffsetPtr - 1);
    Value0 = Data.getRelocatedAddress(OffsetPtr, &SectionIndex);
    Value1 = Data.getRelocatedAddress(OffsetPtr);
    break;
  }
  case dwarf::DW_RLE_start_length: {
    uint32_t PreviousOffset = *OffsetPtr - 1;
    Value0 = Data.getRelocatedAddress(OffsetPtr, &SectionIndex);
    Value1 = Data.getULEB128(OffsetPtr);
    if (End < *OffsetPtr)
      return createStringError(errc::invalid_argument,
                         "read past end of table when reading "
                         "DW_RLE_start_length encoding at offset 0x%" PRIx32,
                         PreviousOffset);
    break;
  }
  default:
    return createStringError(errc::not_supported,
                       "unknown rnglists encoding 0x%" PRIx32
                       " at offset 0x%" PRIx32,
                       uint32_t(Encoding), *OffsetPtr - 1);
  }

  EntryKind = Encoding;
  return Error::success();
}

DWARFAddressRangesVector
DWARFDebugRnglist::getAbsoluteRanges(llvm::Optional<SectionedAddress> BaseAddr,
                                     DWARFUnit &U) const {
  DWARFAddressRangesVector Res;
  for (const RangeListEntry &RLE : Entries) {
    if (RLE.EntryKind == dwarf::DW_RLE_end_of_list)
      break;
    if (RLE.EntryKind == dwarf::DW_RLE_base_addressx) {
      BaseAddr = U.getAddrOffsetSectionItem(RLE.Value0);
      if (!BaseAddr)
        BaseAddr = {RLE.Value0, -1ULL};
      continue;
    }
    if (RLE.EntryKind == dwarf::DW_RLE_base_address) {
      BaseAddr = {RLE.Value0, RLE.SectionIndex};
      continue;
    }

    DWARFAddressRange E;
    E.SectionIndex = RLE.SectionIndex;
    if (BaseAddr && E.SectionIndex == -1ULL)
      E.SectionIndex = BaseAddr->SectionIndex;

    switch (RLE.EntryKind) {
    case dwarf::DW_RLE_offset_pair:
      E.LowPC = RLE.Value0;
      E.HighPC = RLE.Value1;
      if (BaseAddr) {
        E.LowPC += BaseAddr->Address;
        E.HighPC += BaseAddr->Address;
      }
      break;
    case dwarf::DW_RLE_start_end:
      E.LowPC = RLE.Value0;
      E.HighPC = RLE.Value1;
      break;
    case dwarf::DW_RLE_start_length:
      E.LowPC = RLE.Value0;
      E.HighPC = E.LowPC + RLE.Value1;
      break;
    case dwarf::DW_RLE_startx_length: {
      auto Start = U.getAddrOffsetSectionItem(RLE.Value0);
      if (!Start)
        Start = {0, -1ULL};
      E.SectionIndex = Start->SectionIndex;
      E.LowPC = Start->Address;
      E.HighPC = E.LowPC + RLE.Value1;
      break;
    }
    default:
      // Unsupported encodings should have been reported during extraction,
      // so we should not run into any here.
      llvm_unreachable("Unsupported range list encoding");
    }
    Res.push_back(E);
  }
  return Res;
}

void RangeListEntry::dump(
    raw_ostream &OS, uint8_t AddrSize, uint8_t MaxEncodingStringLength,
    uint64_t &CurrentBase, DIDumpOptions DumpOpts,
    llvm::function_ref<Optional<SectionedAddress>(uint32_t)>
        LookupPooledAddress) const {
  auto PrintRawEntry = [](raw_ostream &OS, const RangeListEntry &Entry,
                          uint8_t AddrSize, DIDumpOptions DumpOpts) {
    if (DumpOpts.Verbose) {
      DumpOpts.DisplayRawContents = true;
      DWARFAddressRange(Entry.Value0, Entry.Value1)
          .dump(OS, AddrSize, DumpOpts);
      OS << " => ";
    }
  };

  if (DumpOpts.Verbose) {
    // Print the section offset in verbose mode.
    OS << format("0x%8.8" PRIx32 ":", Offset);
    auto EncodingString = dwarf::RangeListEncodingString(EntryKind);
    // Unsupported encodings should have been reported during parsing.
    assert(!EncodingString.empty() && "Unknown range entry encoding");
    OS << format(" [%s%*c", EncodingString.data(),
                 MaxEncodingStringLength - EncodingString.size() + 1, ']');
    if (EntryKind != dwarf::DW_RLE_end_of_list)
      OS << ": ";
  }

  switch (EntryKind) {
  case dwarf::DW_RLE_end_of_list:
    OS << (DumpOpts.Verbose ? "" : "<End of list>");
    break;
    //  case dwarf::DW_RLE_base_addressx:
  case dwarf::DW_RLE_base_addressx: {
    if (auto SA = LookupPooledAddress(Value0))
      CurrentBase = SA->Address;
    else
      CurrentBase = Value0;
    if (!DumpOpts.Verbose)
      return;
    OS << format(" 0x%*.*" PRIx64, AddrSize * 2, AddrSize * 2, Value0);
    break;
  }
  case dwarf::DW_RLE_base_address:
    // In non-verbose mode we do not print anything for this entry.
    CurrentBase = Value0;
    if (!DumpOpts.Verbose)
      return;
    OS << format(" 0x%*.*" PRIx64, AddrSize * 2, AddrSize * 2, Value0);
    break;
  case dwarf::DW_RLE_start_length:
    PrintRawEntry(OS, *this, AddrSize, DumpOpts);
    DWARFAddressRange(Value0, Value0 + Value1).dump(OS, AddrSize, DumpOpts);
    break;
  case dwarf::DW_RLE_offset_pair:
    PrintRawEntry(OS, *this, AddrSize, DumpOpts);
    DWARFAddressRange(Value0 + CurrentBase, Value1 + CurrentBase)
        .dump(OS, AddrSize, DumpOpts);
    break;
  case dwarf::DW_RLE_start_end:
    DWARFAddressRange(Value0, Value1).dump(OS, AddrSize, DumpOpts);
    break;
  case dwarf::DW_RLE_startx_length: {
    PrintRawEntry(OS, *this, AddrSize, DumpOpts);
    uint64_t Start = 0;
    if (auto SA = LookupPooledAddress(Value0))
      Start = SA->Address;
    DWARFAddressRange(Start, Start + Value1).dump(OS, AddrSize, DumpOpts);
    break;
  } break;
  default:
    llvm_unreachable("Unsupported range list encoding");
  }
  OS << "\n";
}
