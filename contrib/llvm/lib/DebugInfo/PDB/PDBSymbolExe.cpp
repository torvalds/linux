//===- PDBSymbolExe.cpp - ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/PDBSymbolExe.h"

#include "llvm/DebugInfo/PDB/PDBSymDumper.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypePointer.h"

#include <utility>

using namespace llvm;
using namespace llvm::pdb;

void PDBSymbolExe::dump(PDBSymDumper &Dumper) const { Dumper.dump(*this); }

uint32_t PDBSymbolExe::getPointerByteSize() const {
  auto Pointer = findOneChild<PDBSymbolTypePointer>();
  if (Pointer)
    return Pointer->getLength();

  if (getMachineType() == PDB_Machine::x86)
    return 4;
  return 8;
}
