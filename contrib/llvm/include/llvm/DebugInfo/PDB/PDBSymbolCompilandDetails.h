//===- PDBSymbolCompilandDetails.h - PDB compiland details ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLCOMPILANDDETAILS_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLCOMPILANDDETAILS_H

#include "PDBSymbol.h"
#include "PDBTypes.h"

namespace llvm {

class raw_ostream;
namespace pdb {

class PDBSymbolCompilandDetails : public PDBSymbol {
  DECLARE_PDB_SYMBOL_CONCRETE_TYPE(PDB_SymType::CompilandDetails)
public:
  void dump(PDBSymDumper &Dumper) const override;

  void getFrontEndVersion(VersionInfo &Version) const {
    RawSymbol->getFrontEndVersion(Version);
  }

  void getBackEndVersion(VersionInfo &Version) const {
    RawSymbol->getBackEndVersion(Version);
  }

  FORWARD_SYMBOL_METHOD(getCompilerName)
  FORWARD_SYMBOL_METHOD(isEditAndContinueEnabled)
  FORWARD_SYMBOL_METHOD(hasDebugInfo)
  FORWARD_SYMBOL_METHOD(hasManagedCode)
  FORWARD_SYMBOL_METHOD(hasSecurityChecks)
  FORWARD_SYMBOL_METHOD(isCVTCIL)
  FORWARD_SYMBOL_METHOD(isDataAligned)
  FORWARD_SYMBOL_METHOD(isHotpatchable)
  FORWARD_SYMBOL_METHOD(isLTCG)
  FORWARD_SYMBOL_METHOD(isMSILNetmodule)
  FORWARD_SYMBOL_METHOD(getLanguage)
  FORWARD_SYMBOL_ID_METHOD(getLexicalParent)
  FORWARD_SYMBOL_METHOD(getPlatform)
  FORWARD_SYMBOL_METHOD(getSourceFileName)
};

} // namespace llvm
}

#endif // LLVM_DEBUGINFO_PDB_PDBFUNCTION_H
