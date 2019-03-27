//==- DIAEnumDebugStreams.cpp - DIA Debug Stream Enumerator impl -*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/DIA/DIAEnumDebugStreams.h"
#include "llvm/DebugInfo/PDB/DIA/DIADataStream.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"

using namespace llvm;
using namespace llvm::pdb;

DIAEnumDebugStreams::DIAEnumDebugStreams(
    CComPtr<IDiaEnumDebugStreams> DiaEnumerator)
    : Enumerator(DiaEnumerator) {}

uint32_t DIAEnumDebugStreams::getChildCount() const {
  LONG Count = 0;
  return (S_OK == Enumerator->get_Count(&Count)) ? Count : 0;
}

std::unique_ptr<IPDBDataStream>
DIAEnumDebugStreams::getChildAtIndex(uint32_t Index) const {
  CComPtr<IDiaEnumDebugStreamData> Item;
  VARIANT VarIndex;
  VarIndex.vt = VT_I4;
  VarIndex.lVal = Index;
  if (S_OK != Enumerator->Item(VarIndex, &Item))
    return nullptr;

  return std::unique_ptr<IPDBDataStream>(new DIADataStream(Item));
}

std::unique_ptr<IPDBDataStream> DIAEnumDebugStreams::getNext() {
  CComPtr<IDiaEnumDebugStreamData> Item;
  ULONG NumFetched = 0;
  if (S_OK != Enumerator->Next(1, &Item, &NumFetched))
    return nullptr;

  return std::unique_ptr<IPDBDataStream>(new DIADataStream(Item));
}

void DIAEnumDebugStreams::reset() { Enumerator->Reset(); }
