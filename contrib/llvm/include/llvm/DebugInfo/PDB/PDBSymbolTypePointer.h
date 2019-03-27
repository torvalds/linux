//===- PDBSymbolTypePointer.h - pointer type info ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEPOINTER_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEPOINTER_H

#include "PDBSymbol.h"
#include "PDBTypes.h"

namespace llvm {

class raw_ostream;
namespace pdb {

class PDBSymbolTypePointer : public PDBSymbol {
  DECLARE_PDB_SYMBOL_CONCRETE_TYPE(PDB_SymType::PointerType)
public:
  void dump(PDBSymDumper &Dumper) const override;
  void dumpRight(PDBSymDumper &Dumper) const override;

  FORWARD_SYMBOL_METHOD(isConstType)
  FORWARD_SYMBOL_ID_METHOD(getClassParent)
  FORWARD_SYMBOL_METHOD(getLength)
  FORWARD_SYMBOL_ID_METHOD(getLexicalParent)
  FORWARD_SYMBOL_METHOD(isReference)
  FORWARD_SYMBOL_METHOD(isRValueReference)
  FORWARD_SYMBOL_METHOD(isPointerToDataMember)
  FORWARD_SYMBOL_METHOD(isPointerToMemberFunction)
  FORWARD_SYMBOL_ID_METHOD_WITH_NAME(getType, getPointeeType)
  FORWARD_SYMBOL_METHOD(isRestrictedType)
  FORWARD_SYMBOL_METHOD(isUnalignedType)
  FORWARD_SYMBOL_METHOD(isVolatileType)
};

} // namespace llvm
}

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEPOINTER_H
