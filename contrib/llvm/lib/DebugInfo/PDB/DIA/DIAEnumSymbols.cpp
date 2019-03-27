//==- DIAEnumSymbols.cpp - DIA Symbol Enumerator impl ------------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/DIA/DIAEnumSymbols.h"
#include "llvm/DebugInfo/PDB/DIA/DIARawSymbol.h"
#include "llvm/DebugInfo/PDB/DIA/DIASession.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"

using namespace llvm;
using namespace llvm::pdb;

DIAEnumSymbols::DIAEnumSymbols(const DIASession &PDBSession,
                               CComPtr<IDiaEnumSymbols> DiaEnumerator)
    : Session(PDBSession), Enumerator(DiaEnumerator) {}

uint32_t DIAEnumSymbols::getChildCount() const {
  LONG Count = 0;
  return (S_OK == Enumerator->get_Count(&Count)) ? Count : 0;
}

std::unique_ptr<PDBSymbol>
DIAEnumSymbols::getChildAtIndex(uint32_t Index) const {
  CComPtr<IDiaSymbol> Item;
  if (S_OK != Enumerator->Item(Index, &Item))
    return nullptr;

  std::unique_ptr<DIARawSymbol> RawSymbol(new DIARawSymbol(Session, Item));
  return std::unique_ptr<PDBSymbol>(PDBSymbol::create(Session, std::move(RawSymbol)));
}

std::unique_ptr<PDBSymbol> DIAEnumSymbols::getNext() {
  CComPtr<IDiaSymbol> Item;
  ULONG NumFetched = 0;
  if (S_OK != Enumerator->Next(1, &Item, &NumFetched))
    return nullptr;

  std::unique_ptr<DIARawSymbol> RawSymbol(new DIARawSymbol(Session, Item));
  return std::unique_ptr<PDBSymbol>(
      PDBSymbol::create(Session, std::move(RawSymbol)));
}

void DIAEnumSymbols::reset() { Enumerator->Reset(); }
