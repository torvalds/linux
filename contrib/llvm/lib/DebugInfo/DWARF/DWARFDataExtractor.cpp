//===- DWARFDataExtractor.cpp ---------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"

using namespace llvm;

uint64_t DWARFDataExtractor::getRelocatedValue(uint32_t Size, uint32_t *Off,
                                               uint64_t *SecNdx) const {
  if (SecNdx)
    *SecNdx = -1ULL;
  if (!Section)
    return getUnsigned(Off, Size);
  Optional<RelocAddrEntry> Rel = Obj->find(*Section, *Off);
  if (!Rel)
    return getUnsigned(Off, Size);
  if (SecNdx)
    *SecNdx = Rel->SectionIndex;
  return getUnsigned(Off, Size) + Rel->Value;
}

Optional<uint64_t>
DWARFDataExtractor::getEncodedPointer(uint32_t *Offset, uint8_t Encoding,
                                      uint64_t PCRelOffset) const {
  if (Encoding == dwarf::DW_EH_PE_omit)
    return None;

  uint64_t Result = 0;
  uint32_t OldOffset = *Offset;
  // First get value
  switch (Encoding & 0x0F) {
  case dwarf::DW_EH_PE_absptr:
    switch (getAddressSize()) {
    case 2:
    case 4:
    case 8:
      Result = getUnsigned(Offset, getAddressSize());
      break;
    default:
      return None;
    }
    break;
  case dwarf::DW_EH_PE_uleb128:
    Result = getULEB128(Offset);
    break;
  case dwarf::DW_EH_PE_sleb128:
    Result = getSLEB128(Offset);
    break;
  case dwarf::DW_EH_PE_udata2:
    Result = getUnsigned(Offset, 2);
    break;
  case dwarf::DW_EH_PE_udata4:
    Result = getUnsigned(Offset, 4);
    break;
  case dwarf::DW_EH_PE_udata8:
    Result = getUnsigned(Offset, 8);
    break;
  case dwarf::DW_EH_PE_sdata2:
    Result = getSigned(Offset, 2);
    break;
  case dwarf::DW_EH_PE_sdata4:
    Result = getSigned(Offset, 4);
    break;
  case dwarf::DW_EH_PE_sdata8:
    Result = getSigned(Offset, 8);
    break;
  default:
    return None;
  }
  // Then add relative offset, if required
  switch (Encoding & 0x70) {
  case dwarf::DW_EH_PE_absptr:
    // do nothing
    break;
  case dwarf::DW_EH_PE_pcrel:
    Result += PCRelOffset;
    break;
  case dwarf::DW_EH_PE_datarel:
  case dwarf::DW_EH_PE_textrel:
  case dwarf::DW_EH_PE_funcrel:
  case dwarf::DW_EH_PE_aligned:
  default:
    *Offset = OldOffset;
    return None;
  }

  return Result;
}
