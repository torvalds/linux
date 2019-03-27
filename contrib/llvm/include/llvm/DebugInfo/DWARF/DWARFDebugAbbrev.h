//===- DWARFDebugAbbrev.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARFDEBUGABBREV_H
#define LLVM_DEBUGINFO_DWARFDEBUGABBREV_H

#include "llvm/DebugInfo/DWARF/DWARFAbbreviationDeclaration.h"
#include "llvm/Support/DataExtractor.h"
#include <cstdint>
#include <map>
#include <vector>

namespace llvm {

class raw_ostream;

class DWARFAbbreviationDeclarationSet {
  uint32_t Offset;
  /// Code of the first abbreviation, if all abbreviations in the set have
  /// consecutive codes. UINT32_MAX otherwise.
  uint32_t FirstAbbrCode;
  std::vector<DWARFAbbreviationDeclaration> Decls;

  using const_iterator =
      std::vector<DWARFAbbreviationDeclaration>::const_iterator;

public:
  DWARFAbbreviationDeclarationSet();

  uint32_t getOffset() const { return Offset; }
  void dump(raw_ostream &OS) const;
  bool extract(DataExtractor Data, uint32_t *OffsetPtr);

  const DWARFAbbreviationDeclaration *
  getAbbreviationDeclaration(uint32_t AbbrCode) const;

  const_iterator begin() const {
    return Decls.begin();
  }

  const_iterator end() const {
    return Decls.end();
  }

private:
  void clear();
};

class DWARFDebugAbbrev {
  using DWARFAbbreviationDeclarationSetMap =
      std::map<uint64_t, DWARFAbbreviationDeclarationSet>;

  mutable DWARFAbbreviationDeclarationSetMap AbbrDeclSets;
  mutable DWARFAbbreviationDeclarationSetMap::const_iterator PrevAbbrOffsetPos;
  mutable Optional<DataExtractor> Data;

public:
  DWARFDebugAbbrev();

  const DWARFAbbreviationDeclarationSet *
  getAbbreviationDeclarationSet(uint64_t CUAbbrOffset) const;

  void dump(raw_ostream &OS) const;
  void parse() const;
  void extract(DataExtractor Data);

  DWARFAbbreviationDeclarationSetMap::const_iterator begin() const {
    parse();
    return AbbrDeclSets.begin();
  }

  DWARFAbbreviationDeclarationSetMap::const_iterator end() const {
    return AbbrDeclSets.end();
  }

private:
  void clear();
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARFDEBUGABBREV_H
