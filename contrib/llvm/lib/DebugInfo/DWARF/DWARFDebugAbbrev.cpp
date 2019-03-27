//===- DWARFDebugAbbrev.cpp -----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFDebugAbbrev.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cinttypes>
#include <cstdint>

using namespace llvm;

DWARFAbbreviationDeclarationSet::DWARFAbbreviationDeclarationSet() {
  clear();
}

void DWARFAbbreviationDeclarationSet::clear() {
  Offset = 0;
  FirstAbbrCode = 0;
  Decls.clear();
}

bool DWARFAbbreviationDeclarationSet::extract(DataExtractor Data,
                                              uint32_t *OffsetPtr) {
  clear();
  const uint32_t BeginOffset = *OffsetPtr;
  Offset = BeginOffset;
  DWARFAbbreviationDeclaration AbbrDecl;
  uint32_t PrevAbbrCode = 0;
  while (AbbrDecl.extract(Data, OffsetPtr)) {
    if (FirstAbbrCode == 0) {
      FirstAbbrCode = AbbrDecl.getCode();
    } else {
      if (PrevAbbrCode + 1 != AbbrDecl.getCode()) {
        // Codes are not consecutive, can't do O(1) lookups.
        FirstAbbrCode = UINT32_MAX;
      }
    }
    PrevAbbrCode = AbbrDecl.getCode();
    Decls.push_back(std::move(AbbrDecl));
  }
  return BeginOffset != *OffsetPtr;
}

void DWARFAbbreviationDeclarationSet::dump(raw_ostream &OS) const {
  for (const auto &Decl : Decls)
    Decl.dump(OS);
}

const DWARFAbbreviationDeclaration *
DWARFAbbreviationDeclarationSet::getAbbreviationDeclaration(
    uint32_t AbbrCode) const {
  if (FirstAbbrCode == UINT32_MAX) {
    for (const auto &Decl : Decls) {
      if (Decl.getCode() == AbbrCode)
        return &Decl;
    }
    return nullptr;
  }
  if (AbbrCode < FirstAbbrCode || AbbrCode >= FirstAbbrCode + Decls.size())
    return nullptr;
  return &Decls[AbbrCode - FirstAbbrCode];
}

DWARFDebugAbbrev::DWARFDebugAbbrev() { clear(); }

void DWARFDebugAbbrev::clear() {
  AbbrDeclSets.clear();
  PrevAbbrOffsetPos = AbbrDeclSets.end();
}

void DWARFDebugAbbrev::extract(DataExtractor Data) {
  clear();
  this->Data = Data;
}

void DWARFDebugAbbrev::parse() const {
  if (!Data)
    return;
  uint32_t Offset = 0;
  DWARFAbbreviationDeclarationSet AbbrDecls;
  auto I = AbbrDeclSets.begin();
  while (Data->isValidOffset(Offset)) {
    while (I != AbbrDeclSets.end() && I->first < Offset)
      ++I;
    uint32_t CUAbbrOffset = Offset;
    if (!AbbrDecls.extract(*Data, &Offset))
      break;
    AbbrDeclSets.insert(I, std::make_pair(CUAbbrOffset, std::move(AbbrDecls)));
  }
  Data = None;
}

void DWARFDebugAbbrev::dump(raw_ostream &OS) const {
  parse();

  if (AbbrDeclSets.empty()) {
    OS << "< EMPTY >\n";
    return;
  }

  for (const auto &I : AbbrDeclSets) {
    OS << format("Abbrev table for offset: 0x%8.8" PRIx64 "\n", I.first);
    I.second.dump(OS);
  }
}

const DWARFAbbreviationDeclarationSet*
DWARFDebugAbbrev::getAbbreviationDeclarationSet(uint64_t CUAbbrOffset) const {
  const auto End = AbbrDeclSets.end();
  if (PrevAbbrOffsetPos != End && PrevAbbrOffsetPos->first == CUAbbrOffset) {
    return &(PrevAbbrOffsetPos->second);
  }

  const auto Pos = AbbrDeclSets.find(CUAbbrOffset);
  if (Pos != End) {
    PrevAbbrOffsetPos = Pos;
    return &(Pos->second);
  }

  if (Data && CUAbbrOffset < Data->getData().size()) {
    uint32_t Offset = CUAbbrOffset;
    DWARFAbbreviationDeclarationSet AbbrDecls;
    if (!AbbrDecls.extract(*Data, &Offset))
      return nullptr;
    PrevAbbrOffsetPos =
        AbbrDeclSets.insert(std::make_pair(CUAbbrOffset, std::move(AbbrDecls)))
            .first;
    return &PrevAbbrOffsetPos->second;
  }

  return nullptr;
}
