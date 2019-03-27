//==- NativeEnumModules.cpp - Native Symbol Enumerator impl ------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NativeEnumModules.h"

#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/Native/NativeCompilandSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeExeSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"
#include "llvm/DebugInfo/PDB/PDBSymbolCompiland.h"
#include "llvm/DebugInfo/PDB/PDBSymbolExe.h"

namespace llvm {
namespace pdb {

NativeEnumModules::NativeEnumModules(NativeSession &PDBSession, uint32_t Index)
    : Session(PDBSession), Index(Index) {}

uint32_t NativeEnumModules::getChildCount() const {
  return Session.getSymbolCache().getNumCompilands();
}

std::unique_ptr<PDBSymbol>
NativeEnumModules::getChildAtIndex(uint32_t N) const {
  return Session.getSymbolCache().getOrCreateCompiland(N);
}

std::unique_ptr<PDBSymbol> NativeEnumModules::getNext() {
  if (Index >= getChildCount())
    return nullptr;
  return getChildAtIndex(Index++);
}

void NativeEnumModules::reset() { Index = 0; }

}
}
