//===- PDBSymDumper.h - base interface for PDB symbol dumper *- C++ -----*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMDUMPER_H
#define LLVM_DEBUGINFO_PDB_PDBSYMDUMPER_H

#include "PDBTypes.h"

namespace llvm {

class raw_ostream;
namespace pdb {

class PDBSymDumper {
public:
  PDBSymDumper(bool ShouldRequireImpl);
  virtual ~PDBSymDumper();

  virtual void dump(const PDBSymbolAnnotation &Symbol);
  virtual void dump(const PDBSymbolBlock &Symbol);
  virtual void dump(const PDBSymbolCompiland &Symbol);
  virtual void dump(const PDBSymbolCompilandDetails &Symbol);
  virtual void dump(const PDBSymbolCompilandEnv &Symbol);
  virtual void dump(const PDBSymbolCustom &Symbol);
  virtual void dump(const PDBSymbolData &Symbol);
  virtual void dump(const PDBSymbolExe &Symbol);
  virtual void dump(const PDBSymbolFunc &Symbol);
  virtual void dump(const PDBSymbolFuncDebugEnd &Symbol);
  virtual void dump(const PDBSymbolFuncDebugStart &Symbol);
  virtual void dump(const PDBSymbolLabel &Symbol);
  virtual void dump(const PDBSymbolPublicSymbol &Symbol);
  virtual void dump(const PDBSymbolThunk &Symbol);
  virtual void dump(const PDBSymbolTypeArray &Symbol);
  virtual void dump(const PDBSymbolTypeBaseClass &Symbol);
  virtual void dump(const PDBSymbolTypeBuiltin &Symbol);
  virtual void dump(const PDBSymbolTypeCustom &Symbol);
  virtual void dump(const PDBSymbolTypeDimension &Symbol);
  virtual void dump(const PDBSymbolTypeEnum &Symbol);
  virtual void dump(const PDBSymbolTypeFriend &Symbol);
  virtual void dump(const PDBSymbolTypeFunctionArg &Symbol);
  virtual void dump(const PDBSymbolTypeFunctionSig &Symbol);
  virtual void dump(const PDBSymbolTypeManaged &Symbol);
  virtual void dump(const PDBSymbolTypePointer &Symbol);
  virtual void dump(const PDBSymbolTypeTypedef &Symbol);
  virtual void dump(const PDBSymbolTypeUDT &Symbol);
  virtual void dump(const PDBSymbolTypeVTable &Symbol);
  virtual void dump(const PDBSymbolTypeVTableShape &Symbol);
  virtual void dump(const PDBSymbolUnknown &Symbol);
  virtual void dump(const PDBSymbolUsingNamespace &Symbol);

  virtual void dumpRight(const PDBSymbolTypeArray &Symbol) {}
  virtual void dumpRight(const PDBSymbolTypeBaseClass &Symbol) {}
  virtual void dumpRight(const PDBSymbolTypeBuiltin &Symbol) {}
  virtual void dumpRight(const PDBSymbolTypeCustom &Symbol) {}
  virtual void dumpRight(const PDBSymbolTypeDimension &Symbol) {}
  virtual void dumpRight(const PDBSymbolTypeEnum &Symbol) {}
  virtual void dumpRight(const PDBSymbolTypeFriend &Symbol) {}
  virtual void dumpRight(const PDBSymbolTypeFunctionArg &Symbol) {}
  virtual void dumpRight(const PDBSymbolTypeFunctionSig &Symbol) {}
  virtual void dumpRight(const PDBSymbolTypeManaged &Symbol) {}
  virtual void dumpRight(const PDBSymbolTypePointer &Symbol) {}
  virtual void dumpRight(const PDBSymbolTypeTypedef &Symbol) {}
  virtual void dumpRight(const PDBSymbolTypeUDT &Symbol) {}
  virtual void dumpRight(const PDBSymbolTypeVTable &Symbol) {}
  virtual void dumpRight(const PDBSymbolTypeVTableShape &Symbol) {}

private:
  bool RequireImpl;
};
}
}

#endif
