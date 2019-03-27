//==- DIAEnumSourceFiles.cpp - DIA Source File Enumerator impl ---*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/DIA/DIAEnumInjectedSources.h"
#include "llvm/DebugInfo/PDB/DIA/DIAInjectedSource.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"

using namespace llvm;
using namespace llvm::pdb;

DIAEnumInjectedSources::DIAEnumInjectedSources(
    CComPtr<IDiaEnumInjectedSources> DiaEnumerator)
    : Enumerator(DiaEnumerator) {}

uint32_t DIAEnumInjectedSources::getChildCount() const {
  LONG Count = 0;
  return (S_OK == Enumerator->get_Count(&Count)) ? Count : 0;
}

std::unique_ptr<IPDBInjectedSource>
DIAEnumInjectedSources::getChildAtIndex(uint32_t Index) const {
  CComPtr<IDiaInjectedSource> Item;
  if (S_OK != Enumerator->Item(Index, &Item))
    return nullptr;

  return std::unique_ptr<IPDBInjectedSource>(new DIAInjectedSource(Item));
}

std::unique_ptr<IPDBInjectedSource> DIAEnumInjectedSources::getNext() {
  CComPtr<IDiaInjectedSource> Item;
  ULONG NumFetched = 0;
  if (S_OK != Enumerator->Next(1, &Item, &NumFetched))
    return nullptr;

  return std::unique_ptr<IPDBInjectedSource>(new DIAInjectedSource(Item));
}

void DIAEnumInjectedSources::reset() { Enumerator->Reset(); }
