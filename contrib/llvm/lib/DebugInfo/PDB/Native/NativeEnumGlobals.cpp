//==- NativeEnumGlobals.cpp - Native Global Enumerator impl ------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NativeEnumGlobals.h"

#include "llvm/DebugInfo/CodeView/SymbolDeserializer.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/Native/GlobalsStream.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/SymbolStream.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

NativeEnumGlobals::NativeEnumGlobals(NativeSession &PDBSession,
                                     std::vector<codeview::SymbolKind> Kinds)
    : Index(0), Session(PDBSession) {
  GlobalsStream &GS = cantFail(Session.getPDBFile().getPDBGlobalsStream());
  SymbolStream &SS = cantFail(Session.getPDBFile().getPDBSymbolStream());
  for (uint32_t Off : GS.getGlobalsTable()) {
    CVSymbol S = SS.readRecord(Off);
    if (!llvm::is_contained(Kinds, S.kind()))
      continue;
    MatchOffsets.push_back(Off);
  }
}

uint32_t NativeEnumGlobals::getChildCount() const {
  return static_cast<uint32_t>(MatchOffsets.size());
}

std::unique_ptr<PDBSymbol>
NativeEnumGlobals::getChildAtIndex(uint32_t N) const {
  if (N >= MatchOffsets.size())
    return nullptr;

  SymIndexId Id =
      Session.getSymbolCache().getOrCreateGlobalSymbolByOffset(MatchOffsets[N]);
  return Session.getSymbolCache().getSymbolById(Id);
}

std::unique_ptr<PDBSymbol> NativeEnumGlobals::getNext() {
  return getChildAtIndex(Index++);
}

void NativeEnumGlobals::reset() { Index = 0; }
