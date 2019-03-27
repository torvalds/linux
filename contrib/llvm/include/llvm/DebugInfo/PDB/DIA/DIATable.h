//===- DIATable.h - DIA implementation of IPDBTable -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_DIA_DIATABLE_H
#define LLVM_DEBUGINFO_PDB_DIA_DIATABLE_H

#include "DIASupport.h"
#include "llvm/DebugInfo/PDB/IPDBTable.h"

namespace llvm {
namespace pdb {
class DIATable : public IPDBTable {
public:
  explicit DIATable(CComPtr<IDiaTable> DiaTable);

  uint32_t getItemCount() const override;
  std::string getName() const override;
  PDB_TableType getTableType() const override;

private:
  CComPtr<IDiaTable> Table;
};
}
}

#endif // LLVM_DEBUGINFO_PDB_DIA_DIATABLE_H
