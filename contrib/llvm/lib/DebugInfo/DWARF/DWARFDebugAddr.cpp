//===- DWARFDebugAddr.cpp -------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFDebugAddr.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"

using namespace llvm;

void DWARFDebugAddrTable::clear() {
  HeaderData = {};
  Addrs.clear();
  invalidateLength();
}

Error DWARFDebugAddrTable::extract(DWARFDataExtractor Data,
                                   uint32_t *OffsetPtr,
                                   uint16_t Version,
                                   uint8_t AddrSize,
                                   std::function<void(Error)> WarnCallback) {
  clear();
  HeaderOffset = *OffsetPtr;
  // Read and verify the length field.
  if (!Data.isValidOffsetForDataOfSize(*OffsetPtr, sizeof(uint32_t)))
    return createStringError(errc::invalid_argument,
                       "section is not large enough to contain a "
                       ".debug_addr table length at offset 0x%"
                       PRIx32, *OffsetPtr);
  uint16_t UnitVersion;
  if (Version == 0) {
    WarnCallback(createStringError(errc::invalid_argument,
                       "DWARF version is not defined in CU,"
                       " assuming version 5"));
    UnitVersion = 5;
  } else {
    UnitVersion = Version;
  }
  // TODO: Add support for DWARF64.
  Format = dwarf::DwarfFormat::DWARF32;
  if (UnitVersion >= 5) {
    HeaderData.Length = Data.getU32(OffsetPtr);
    if (HeaderData.Length == 0xffffffffu) {
      invalidateLength();
      return createStringError(errc::not_supported,
          "DWARF64 is not supported in .debug_addr at offset 0x%" PRIx32,
          HeaderOffset);
    }
    if (HeaderData.Length + sizeof(uint32_t) < sizeof(Header)) {
      uint32_t TmpLength = getLength();
      invalidateLength();
      return createStringError(errc::invalid_argument,
                         ".debug_addr table at offset 0x%" PRIx32
                         " has too small length (0x%" PRIx32
                         ") to contain a complete header",
                         HeaderOffset, TmpLength);
    }
    uint32_t End = HeaderOffset + getLength();
    if (!Data.isValidOffsetForDataOfSize(HeaderOffset, End - HeaderOffset)) {
      uint32_t TmpLength = getLength();
      invalidateLength();
      return createStringError(errc::invalid_argument,
          "section is not large enough to contain a .debug_addr table "
          "of length 0x%" PRIx32 " at offset 0x%" PRIx32,
          TmpLength, HeaderOffset);
    }

    HeaderData.Version = Data.getU16(OffsetPtr);
    HeaderData.AddrSize = Data.getU8(OffsetPtr);
    HeaderData.SegSize = Data.getU8(OffsetPtr);
    DataSize = getDataSize();
  } else {
    HeaderData.Version = UnitVersion;
    HeaderData.AddrSize = AddrSize;
    // TODO: Support for non-zero SegSize.
    HeaderData.SegSize = 0;
    DataSize = Data.size();
  }

  // Perform basic validation of the remaining header fields.

  // We support DWARF version 5 for now as well as pre-DWARF5
  // implementations of .debug_addr table, which doesn't contain a header
  // and consists only of a series of addresses.
  if (HeaderData.Version > 5) {
    return createStringError(errc::not_supported, "version %" PRIu16
        " of .debug_addr section at offset 0x%" PRIx32 " is not supported",
        HeaderData.Version, HeaderOffset);
  }
  // FIXME: For now we just treat version mismatch as an error,
  // however the correct way to associate a .debug_addr table
  // with a .debug_info table is to look at the DW_AT_addr_base
  // attribute in the info table.
  if (HeaderData.Version != UnitVersion)
    return createStringError(errc::invalid_argument,
                       ".debug_addr table at offset 0x%" PRIx32
                       " has version %" PRIu16
                       " which is different from the version suggested"
                       " by the DWARF unit header: %" PRIu16,
                       HeaderOffset, HeaderData.Version, UnitVersion);
  if (HeaderData.AddrSize != 4 && HeaderData.AddrSize != 8)
    return createStringError(errc::not_supported,
                       ".debug_addr table at offset 0x%" PRIx32
                       " has unsupported address size %" PRIu8,
                       HeaderOffset, HeaderData.AddrSize);
  if (HeaderData.AddrSize != AddrSize && AddrSize != 0)
    return createStringError(errc::invalid_argument,
                       ".debug_addr table at offset 0x%" PRIx32
                       " has address size %" PRIu8
                       " which is different from CU address size %" PRIu8,
                       HeaderOffset, HeaderData.AddrSize, AddrSize);

  // TODO: add support for non-zero segment selector size.
  if (HeaderData.SegSize != 0)
    return createStringError(errc::not_supported,
                       ".debug_addr table at offset 0x%" PRIx32
                       " has unsupported segment selector size %" PRIu8,
                       HeaderOffset, HeaderData.SegSize);
  if (DataSize % HeaderData.AddrSize != 0) {
    invalidateLength();
    return createStringError(errc::invalid_argument,
                       ".debug_addr table at offset 0x%" PRIx32
                       " contains data of size %" PRIu32
                       " which is not a multiple of addr size %" PRIu8,
                       HeaderOffset, DataSize, HeaderData.AddrSize);
  }
  Data.setAddressSize(HeaderData.AddrSize);
  uint32_t AddrCount = DataSize / HeaderData.AddrSize;
  for (uint32_t I = 0; I < AddrCount; ++I)
    if (HeaderData.AddrSize == 4)
      Addrs.push_back(Data.getU32(OffsetPtr));
    else
      Addrs.push_back(Data.getU64(OffsetPtr));
  return Error::success();
}

void DWARFDebugAddrTable::dump(raw_ostream &OS, DIDumpOptions DumpOpts) const {
  if (DumpOpts.Verbose)
    OS << format("0x%8.8" PRIx32 ": ", HeaderOffset);
  OS << format("Addr Section: length = 0x%8.8" PRIx32
               ", version = 0x%4.4" PRIx16 ", "
               "addr_size = 0x%2.2" PRIx8 ", seg_size = 0x%2.2" PRIx8 "\n",
               HeaderData.Length, HeaderData.Version, HeaderData.AddrSize,
               HeaderData.SegSize);

  static const char *Fmt32 = "0x%8.8" PRIx64;
  static const char *Fmt64 = "0x%16.16" PRIx64;
  std::string AddrFmt = "\n";
  std::string AddrFmtVerbose = " => ";
  if (HeaderData.AddrSize == 4) {
    AddrFmt.append(Fmt32);
    AddrFmtVerbose.append(Fmt32);
  }
  else {
    AddrFmt.append(Fmt64);
    AddrFmtVerbose.append(Fmt64);
  }

  if (Addrs.size() > 0) {
    OS << "Addrs: [";
    for (uint64_t Addr : Addrs) {
      OS << format(AddrFmt.c_str(), Addr);
      if (DumpOpts.Verbose)
        OS << format(AddrFmtVerbose.c_str(),
                     Addr + HeaderOffset + sizeof(HeaderData));
    }
    OS << "\n]\n";
  }
}

Expected<uint64_t> DWARFDebugAddrTable::getAddrEntry(uint32_t Index) const {
  if (Index < Addrs.size())
    return Addrs[Index];
  return createStringError(errc::invalid_argument,
                           "Index %" PRIu32 " is out of range of the "
                           ".debug_addr table at offset 0x%" PRIx32,
                           Index, HeaderOffset);
}

uint32_t DWARFDebugAddrTable::getLength() const {
  if (HeaderData.Length == 0)
    return 0;
  // TODO: DWARF64 support.
  return HeaderData.Length + sizeof(uint32_t);
}

uint32_t DWARFDebugAddrTable::getDataSize() const {
  if (DataSize != 0)
    return DataSize;
  if (getLength() == 0)
    return 0;
  return getLength() - getHeaderSize();
}
