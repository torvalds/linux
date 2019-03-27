//===- DWARFDataExtractor.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARFDATAEXTRACTOR_H
#define LLVM_DEBUGINFO_DWARFDATAEXTRACTOR_H

#include "llvm/DebugInfo/DWARF/DWARFSection.h"
#include "llvm/Support/DataExtractor.h"

namespace llvm {
class DWARFObject;

/// A DataExtractor (typically for an in-memory copy of an object-file section)
/// plus a relocation map for that section, if there is one.
class DWARFDataExtractor : public DataExtractor {
  const DWARFObject *Obj = nullptr;
  const DWARFSection *Section = nullptr;

public:
  /// Constructor for the normal case of extracting data from a DWARF section.
  /// The DWARFSection's lifetime must be at least as long as the extractor's.
  DWARFDataExtractor(const DWARFObject &Obj, const DWARFSection &Section,
                     bool IsLittleEndian, uint8_t AddressSize)
      : DataExtractor(Section.Data, IsLittleEndian, AddressSize), Obj(&Obj),
        Section(&Section) {}

  /// Constructor for cases when there are no relocations.
  DWARFDataExtractor(StringRef Data, bool IsLittleEndian, uint8_t AddressSize)
    : DataExtractor(Data, IsLittleEndian, AddressSize) {}

  /// Extracts a value and applies a relocation to the result if
  /// one exists for the given offset.
  uint64_t getRelocatedValue(uint32_t Size, uint32_t *Off,
                             uint64_t *SectionIndex = nullptr) const;

  /// Extracts an address-sized value and applies a relocation to the result if
  /// one exists for the given offset.
  uint64_t getRelocatedAddress(uint32_t *Off, uint64_t *SecIx = nullptr) const {
    return getRelocatedValue(getAddressSize(), Off, SecIx);
  }

  /// Extracts a DWARF-encoded pointer in \p Offset using \p Encoding.
  /// There is a DWARF encoding that uses a PC-relative adjustment.
  /// For these values, \p AbsPosOffset is used to fix them, which should
  /// reflect the absolute address of this pointer.
  Optional<uint64_t> getEncodedPointer(uint32_t *Offset, uint8_t Encoding,
                                       uint64_t AbsPosOffset = 0) const;

  size_t size() const { return Section == nullptr ? 0 : Section->Data.size(); }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARFDATAEXTRACTOR_H
