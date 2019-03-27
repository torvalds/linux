//==- DIAEnumInjectedSources.h - DIA Injected Sources Enumerator -*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_DIA_DIAENUMINJECTEDSOURCES_H
#define LLVM_DEBUGINFO_PDB_DIA_DIAENUMINJECTEDSOURCES_H

#include "DIASupport.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/IPDBInjectedSource.h"

namespace llvm {
namespace pdb {

class DIAEnumInjectedSources : public IPDBEnumChildren<IPDBInjectedSource> {
public:
  explicit DIAEnumInjectedSources(
      CComPtr<IDiaEnumInjectedSources> DiaEnumerator);

  uint32_t getChildCount() const override;
  ChildTypePtr getChildAtIndex(uint32_t Index) const override;
  ChildTypePtr getNext() override;
  void reset() override;

private:
  CComPtr<IDiaEnumInjectedSources> Enumerator;
};
} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_DIA_DIAENUMINJECTEDSOURCES_H
