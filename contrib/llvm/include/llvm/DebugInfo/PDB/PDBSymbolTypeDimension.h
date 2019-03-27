//===- PDBSymbolTypeDimension.h - array dimension type info -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEDIMENSION_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEDIMENSION_H

#include "PDBSymbol.h"
#include "PDBTypes.h"

namespace llvm {

class raw_ostream;
namespace pdb {

class PDBSymbolTypeDimension : public PDBSymbol {
  DECLARE_PDB_SYMBOL_CONCRETE_TYPE(PDB_SymType::Dimension)
public:
  void dump(PDBSymDumper &Dumper) const override;

  FORWARD_SYMBOL_METHOD(getLowerBoundId)
  FORWARD_SYMBOL_METHOD(getUpperBoundId)
};

} // namespace llvm
}

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEDIMENSION_H
