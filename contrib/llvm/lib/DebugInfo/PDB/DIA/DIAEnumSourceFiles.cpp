//==- DIAEnumSourceFiles.cpp - DIA Source File Enumerator impl ---*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/DIA/DIAEnumSourceFiles.h"
#include "llvm/DebugInfo/PDB/DIA/DIASourceFile.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"

using namespace llvm;
using namespace llvm::pdb;

DIAEnumSourceFiles::DIAEnumSourceFiles(
    const DIASession &PDBSession, CComPtr<IDiaEnumSourceFiles> DiaEnumerator)
    : Session(PDBSession), Enumerator(DiaEnumerator) {}

uint32_t DIAEnumSourceFiles::getChildCount() const {
  LONG Count = 0;
  return (S_OK == Enumerator->get_Count(&Count)) ? Count : 0;
}

std::unique_ptr<IPDBSourceFile>
DIAEnumSourceFiles::getChildAtIndex(uint32_t Index) const {
  CComPtr<IDiaSourceFile> Item;
  if (S_OK != Enumerator->Item(Index, &Item))
    return nullptr;

  return std::unique_ptr<IPDBSourceFile>(new DIASourceFile(Session, Item));
}

std::unique_ptr<IPDBSourceFile> DIAEnumSourceFiles::getNext() {
  CComPtr<IDiaSourceFile> Item;
  ULONG NumFetched = 0;
  if (S_OK != Enumerator->Next(1, &Item, &NumFetched))
    return nullptr;

  return std::unique_ptr<IPDBSourceFile>(new DIASourceFile(Session, Item));
}

void DIAEnumSourceFiles::reset() { Enumerator->Reset(); }
