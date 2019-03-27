//===- PDBSymbolUnknown.h - unknown symbol type -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLUNKNOWN_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLUNKNOWN_H

#include "PDBSymbol.h"

namespace llvm {

class raw_ostream;
namespace pdb {

class PDBSymbolUnknown : public PDBSymbol {
  DECLARE_PDB_SYMBOL_CUSTOM_TYPE(S->getSymTag() == PDB_SymType::None ||
                                 S->getSymTag() >= PDB_SymType::Max)

public:
  void dump(PDBSymDumper &Dumper) const override;
};

} // namespace llvm
}

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLUNKNOWN_H
