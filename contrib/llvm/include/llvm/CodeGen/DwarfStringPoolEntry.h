//===- llvm/CodeGen/DwarfStringPoolEntry.h - String pool entry --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_DWARFSTRINGPOOLENTRY_H
#define LLVM_CODEGEN_DWARFSTRINGPOOLENTRY_H

#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/StringMap.h"

namespace llvm {

class MCSymbol;

/// Data for a string pool entry.
struct DwarfStringPoolEntry {
  static constexpr unsigned NotIndexed = -1;

  MCSymbol *Symbol;
  unsigned Offset;
  unsigned Index;

  bool isIndexed() const { return Index != NotIndexed; }
};

/// String pool entry reference.
class DwarfStringPoolEntryRef {
  PointerIntPair<const StringMapEntry<DwarfStringPoolEntry> *, 1, bool>
      MapEntryAndIndexed;

  const StringMapEntry<DwarfStringPoolEntry> *getMapEntry() const {
    return MapEntryAndIndexed.getPointer();
  }

public:
  DwarfStringPoolEntryRef() = default;
  DwarfStringPoolEntryRef(const StringMapEntry<DwarfStringPoolEntry> &Entry,
                          bool Indexed)
      : MapEntryAndIndexed(&Entry, Indexed) {}

  explicit operator bool() const { return getMapEntry(); }
  MCSymbol *getSymbol() const {
    assert(getMapEntry()->second.Symbol && "No symbol available!");
    return getMapEntry()->second.Symbol;
  }
  unsigned getOffset() const { return getMapEntry()->second.Offset; }
  bool isIndexed() const { return MapEntryAndIndexed.getInt(); }
  unsigned getIndex() const {
    assert(isIndexed());
    assert(getMapEntry()->getValue().isIndexed());
    return getMapEntry()->second.Index;
  }
  StringRef getString() const { return getMapEntry()->first(); }
  /// Return the entire string pool entry for convenience.
  DwarfStringPoolEntry getEntry() const { return getMapEntry()->getValue(); }

  bool operator==(const DwarfStringPoolEntryRef &X) const {
    return getMapEntry() == X.getMapEntry();
  }
  bool operator!=(const DwarfStringPoolEntryRef &X) const {
    return getMapEntry() != X.getMapEntry();
  }
};

} // end namespace llvm

#endif
