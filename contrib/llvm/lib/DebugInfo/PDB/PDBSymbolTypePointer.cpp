//===- PDBSymbolTypePointer.cpp -----------------------------------*- C++ -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/PDBSymbolTypePointer.h"

#include "llvm/DebugInfo/PDB/IPDBSession.h"
#include "llvm/DebugInfo/PDB/PDBSymDumper.h"

#include <utility>

using namespace llvm;
using namespace llvm::pdb;

void PDBSymbolTypePointer::dump(PDBSymDumper &Dumper) const {
  Dumper.dump(*this);
}

void PDBSymbolTypePointer::dumpRight(PDBSymDumper &Dumper) const {
  Dumper.dumpRight(*this);
}
