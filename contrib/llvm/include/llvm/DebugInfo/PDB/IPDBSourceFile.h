//===- IPDBSourceFile.h - base interface for a PDB source file --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_IPDBSOURCEFILE_H
#define LLVM_DEBUGINFO_PDB_IPDBSOURCEFILE_H

#include "PDBTypes.h"
#include <memory>
#include <string>

namespace llvm {
class raw_ostream;

namespace pdb {

/// IPDBSourceFile defines an interface used to represent source files whose
/// information are stored in the PDB.
class IPDBSourceFile {
public:
  virtual ~IPDBSourceFile();

  void dump(raw_ostream &OS, int Indent) const;

  virtual std::string getFileName() const = 0;
  virtual uint32_t getUniqueId() const = 0;
  virtual std::string getChecksum() const = 0;
  virtual PDB_Checksum getChecksumType() const = 0;
  virtual std::unique_ptr<IPDBEnumChildren<PDBSymbolCompiland>>
  getCompilands() const = 0;
};
}
}

#endif
