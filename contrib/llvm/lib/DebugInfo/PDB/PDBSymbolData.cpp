//===- PDBSymbolData.cpp - PDB data (e.g. variable) accessors ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/PDBSymbolData.h"
#include "llvm/DebugInfo/PDB/IPDBSectionContrib.h"
#include "llvm/DebugInfo/PDB/IPDBSession.h"
#include "llvm/DebugInfo/PDB/PDBSymDumper.h"

#include <utility>

using namespace llvm;
using namespace llvm::pdb;

void PDBSymbolData::dump(PDBSymDumper &Dumper) const { Dumper.dump(*this); }

std::unique_ptr<IPDBEnumLineNumbers> PDBSymbolData::getLineNumbers() const {
  auto Len = RawSymbol->getLength();
  Len = Len ? Len : 1;
  if (auto RVA = RawSymbol->getRelativeVirtualAddress())
    return Session.findLineNumbersByRVA(RVA, Len);

  if (auto Section = RawSymbol->getAddressSection())
    return Session.findLineNumbersBySectOffset(
        Section, RawSymbol->getAddressOffset(), Len);

  return nullptr;
}

uint32_t PDBSymbolData::getCompilandId() const {
  if (auto Lines = getLineNumbers()) {
    if (auto FirstLine = Lines->getNext())
      return FirstLine->getCompilandId();
  }

  uint32_t DataSection = RawSymbol->getAddressSection();
  uint32_t DataOffset = RawSymbol->getAddressOffset();
  if (DataSection == 0) {
    if (auto RVA = RawSymbol->getRelativeVirtualAddress())
      Session.addressForRVA(RVA, DataSection, DataOffset);
  }

  if (DataSection) {
    if (auto SecContribs = Session.getSectionContribs()) {
      while (auto Section = SecContribs->getNext()) {
        if (Section->getAddressSection() == DataSection &&
            Section->getAddressOffset() <= DataOffset &&
            (Section->getAddressOffset() + Section->getLength()) > DataOffset)
          return Section->getCompilandId();
      }
    }
  } else {
    auto LexParentId = RawSymbol->getLexicalParentId();
    while (auto LexParent = Session.getSymbolById(LexParentId)) {
      if (LexParent->getSymTag() == PDB_SymType::Exe)
        break;
      if (LexParent->getSymTag() == PDB_SymType::Compiland)
        return LexParentId;
      LexParentId = LexParent->getRawSymbol().getLexicalParentId();
    }
  }

  return 0;
}
