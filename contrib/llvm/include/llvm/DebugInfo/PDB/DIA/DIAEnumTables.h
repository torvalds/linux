//===- DIAEnumTables.h - DIA Tables Enumerator Impl -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_DIA_DIAENUMTABLES_H
#define LLVM_DEBUGINFO_PDB_DIA_DIAENUMTABLES_H

#include "DIASupport.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/IPDBTable.h"

namespace llvm {
namespace pdb {
class IPDBTable;

class DIAEnumTables : public IPDBEnumChildren<IPDBTable> {
public:
  explicit DIAEnumTables(CComPtr<IDiaEnumTables> DiaEnumerator);

  uint32_t getChildCount() const override;
  std::unique_ptr<IPDBTable> getChildAtIndex(uint32_t Index) const override;
  std::unique_ptr<IPDBTable> getNext() override;
  void reset() override;

private:
  CComPtr<IDiaEnumTables> Enumerator;
};
}
}

#endif // LLVM_DEBUGINFO_PDB_DIA_DIAENUMTABLES_H
