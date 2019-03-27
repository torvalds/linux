//===- DWARFDebugAddr.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARFDEBUGADDR_H
#define LLVM_DEBUGINFO_DWARFDEBUGADDR_H

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <map>
#include <vector>

namespace llvm {

class Error;
class raw_ostream;

/// A class representing an address table as specified in DWARF v5.
/// The table consists of a header followed by an array of address values from
/// .debug_addr section.
class DWARFDebugAddrTable {
public:
  struct Header {
    /// The total length of the entries for this table, not including the length
    /// field itself.
    uint32_t Length = 0;
    /// The DWARF version number.
    uint16_t Version = 5;
    /// The size in bytes of an address on the target architecture. For
    /// segmented addressing, this is the size of the offset portion of the
    /// address.
    uint8_t AddrSize;
    /// The size in bytes of a segment selector on the target architecture.
    /// If the target system uses a flat address space, this value is 0.
    uint8_t SegSize = 0;
  };

private:
  dwarf::DwarfFormat Format;
  uint32_t HeaderOffset;
  Header HeaderData;
  uint32_t DataSize = 0;
  std::vector<uint64_t> Addrs;

public:
  void clear();

  /// Extract an entire table, including all addresses.
  Error extract(DWARFDataExtractor Data, uint32_t *OffsetPtr,
                uint16_t Version, uint8_t AddrSize,
                std::function<void(Error)> WarnCallback);

  uint32_t getHeaderOffset() const { return HeaderOffset; }
  uint8_t getAddrSize() const { return HeaderData.AddrSize; }
  void dump(raw_ostream &OS, DIDumpOptions DumpOpts = {}) const;

  /// Return the address based on a given index.
  Expected<uint64_t> getAddrEntry(uint32_t Index) const;

  /// Return the size of the table header including the length
  /// but not including the addresses.
  uint8_t getHeaderSize() const {
    switch (Format) {
    case dwarf::DwarfFormat::DWARF32:
      return 8; // 4 + 2 + 1 + 1
    case dwarf::DwarfFormat::DWARF64:
      return 16; // 12 + 2 + 1 + 1
    }
    llvm_unreachable("Invalid DWARF format (expected DWARF32 or DWARF64)");
  }

  /// Returns the length of this table, including the length field, or 0 if the
  /// length has not been determined (e.g. because the table has not yet been
  /// parsed, or there was a problem in parsing).
  uint32_t getLength() const;

  /// Verify that the given length is valid for this table.
  bool hasValidLength() const { return getLength() != 0; }

  /// Invalidate Length field to stop further processing.
  void invalidateLength() { HeaderData.Length = 0; }

  /// Returns the length of the array of addresses.
  uint32_t getDataSize() const;
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARFDEBUGADDR_H
