//===- PDBSymbolTypeBaseClass.h - base class type information ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEBASECLASS_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEBASECLASS_H

#include "PDBSymbol.h"
#include "PDBTypes.h"

#include "llvm/DebugInfo/PDB/PDBSymbolTypeVTable.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeVTableShape.h"

namespace llvm {

class raw_ostream;
namespace pdb {

class PDBSymbolTypeBaseClass : public PDBSymbol {
  DECLARE_PDB_SYMBOL_CONCRETE_TYPE(PDB_SymType::BaseClass)
public:
  void dump(PDBSymDumper &Dumper) const override;

  FORWARD_SYMBOL_METHOD(getAccess)
  FORWARD_SYMBOL_ID_METHOD(getClassParent)
  FORWARD_SYMBOL_METHOD(hasConstructor)
  FORWARD_SYMBOL_METHOD(isConstType)
  FORWARD_SYMBOL_METHOD(hasAssignmentOperator)
  FORWARD_SYMBOL_METHOD(hasCastOperator)
  FORWARD_SYMBOL_METHOD(hasNestedTypes)
  FORWARD_SYMBOL_METHOD(isIndirectVirtualBaseClass)
  FORWARD_SYMBOL_METHOD(getLength)
  FORWARD_SYMBOL_ID_METHOD(getLexicalParent)
  FORWARD_SYMBOL_METHOD(getName)
  FORWARD_SYMBOL_METHOD(isNested)
  FORWARD_SYMBOL_METHOD(getOffset)
  FORWARD_SYMBOL_METHOD(hasOverloadedOperator)
  FORWARD_SYMBOL_METHOD(isPacked)
  FORWARD_SYMBOL_METHOD(isScoped)
  FORWARD_SYMBOL_ID_METHOD(getType)
  FORWARD_SYMBOL_METHOD(getUdtKind)
  FORWARD_SYMBOL_METHOD(isUnalignedType)

  FORWARD_SYMBOL_METHOD(isVirtualBaseClass)
  FORWARD_SYMBOL_METHOD(getVirtualBaseDispIndex)
  FORWARD_SYMBOL_METHOD(getVirtualBasePointerOffset)
  // FORWARD_SYMBOL_METHOD(getVirtualBaseTableType)
  FORWARD_SYMBOL_ID_METHOD(getVirtualTableShape)
  FORWARD_SYMBOL_METHOD(isVolatileType)
};

} // namespace llvm
}

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEBASECLASS_H
