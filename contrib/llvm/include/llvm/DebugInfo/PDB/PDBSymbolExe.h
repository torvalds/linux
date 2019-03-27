//===- PDBSymbolExe.h - Accessors for querying executables in a PDB ----*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLEXE_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLEXE_H

#include "PDBSymbol.h"
#include "PDBTypes.h"

namespace llvm {

class raw_ostream;

namespace pdb {

class PDBSymbolExe : public PDBSymbol {
  DECLARE_PDB_SYMBOL_CONCRETE_TYPE(PDB_SymType::Exe)
public:
  void dump(PDBSymDumper &Dumper) const override;

  FORWARD_SYMBOL_METHOD(getAge)
  FORWARD_SYMBOL_METHOD(getGuid)
  FORWARD_SYMBOL_METHOD(hasCTypes)
  FORWARD_SYMBOL_METHOD(hasPrivateSymbols)
  FORWARD_SYMBOL_METHOD(getMachineType)
  FORWARD_SYMBOL_METHOD(getName)
  FORWARD_SYMBOL_METHOD(getSignature)
  FORWARD_SYMBOL_METHOD(getSymbolsFileName)

  uint32_t getPointerByteSize() const;

private:
  void dumpChildren(raw_ostream &OS, StringRef Label, PDB_SymType ChildType,
                    int Indent) const;
};
} // namespace llvm
}

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLEXE_H
