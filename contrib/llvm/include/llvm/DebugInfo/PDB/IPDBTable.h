//===- IPDBTable.h - Base Interface for a PDB Symbol Context ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_IPDBTABLE_H
#define LLVM_DEBUGINFO_PDB_IPDBTABLE_H

#include "PDBTypes.h"

namespace llvm {
namespace pdb {
class IPDBTable {
public:
  virtual ~IPDBTable();

  virtual std::string getName() const = 0;
  virtual uint32_t getItemCount() const = 0;
  virtual PDB_TableType getTableType() const = 0;
};
}
}

#endif // LLVM_DEBUGINFO_PDB_IPDBTABLE_H
