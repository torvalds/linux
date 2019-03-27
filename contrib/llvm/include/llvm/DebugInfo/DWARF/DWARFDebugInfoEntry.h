//===- DWARFDebugInfoEntry.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARFDEBUGINFOENTRY_H
#define LLVM_DEBUGINFO_DWARFDEBUGINFOENTRY_H

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFAbbreviationDeclaration.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include <cstdint>

namespace llvm {

class DataExtractor;
class DWARFUnit;

/// DWARFDebugInfoEntry - A DIE with only the minimum required data.
class DWARFDebugInfoEntry {
  /// Offset within the .debug_info of the start of this entry.
  uint32_t Offset = 0;

  /// The integer depth of this DIE within the compile unit DIEs where the
  /// compile/type unit DIE has a depth of zero.
  uint32_t Depth = 0;

  const DWARFAbbreviationDeclaration *AbbrevDecl = nullptr;

public:
  DWARFDebugInfoEntry() = default;

  /// Extracts a debug info entry, which is a child of a given unit,
  /// starting at a given offset. If DIE can't be extracted, returns false and
  /// doesn't change OffsetPtr.
  bool extractFast(const DWARFUnit &U, uint32_t *OffsetPtr);

  /// High performance extraction should use this call.
  bool extractFast(const DWARFUnit &U, uint32_t *OffsetPtr,
                   const DWARFDataExtractor &DebugInfoData, uint32_t UEndOffset,
                   uint32_t Depth);

  uint32_t getOffset() const { return Offset; }
  uint32_t getDepth() const { return Depth; }

  dwarf::Tag getTag() const {
    return AbbrevDecl ? AbbrevDecl->getTag() : dwarf::DW_TAG_null;
  }

  bool hasChildren() const { return AbbrevDecl && AbbrevDecl->hasChildren(); }

  const DWARFAbbreviationDeclaration *getAbbreviationDeclarationPtr() const {
    return AbbrevDecl;
  }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARFDEBUGINFOENTRY_H
