//===- DWARFAttribute.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARFATTRIBUTE_H
#define LLVM_DEBUGINFO_DWARFATTRIBUTE_H

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include <cstdint>

namespace llvm {

//===----------------------------------------------------------------------===//
/// Encapsulates a DWARF attribute value and all of the data required to
/// describe the attribute value.
///
/// This class is designed to be used by clients that want to iterate across all
/// attributes in a DWARFDie.
struct DWARFAttribute {
  /// The debug info/types offset for this attribute.
  uint32_t Offset = 0;
  /// The debug info/types section byte size of the data for this attribute.
  uint32_t ByteSize = 0;
  /// The attribute enumeration of this attribute.
  dwarf::Attribute Attr;
  /// The form and value for this attribute.
  DWARFFormValue Value;

  DWARFAttribute(uint32_t O, dwarf::Attribute A = dwarf::Attribute(0),
                 dwarf::Form F = dwarf::Form(0)) : Attr(A), Value(F) {}

  bool isValid() const {
    return Offset != 0 && Attr != dwarf::Attribute(0);
  }

  explicit operator bool() const {
    return isValid();
  }

  void clear() {
    Offset = 0;
    ByteSize = 0;
    Attr = dwarf::Attribute(0);
    Value = DWARFFormValue();
  }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARFATTRIBUTE_H
