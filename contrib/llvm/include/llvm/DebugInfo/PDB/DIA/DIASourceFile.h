//===- DIASourceFile.h - DIA implementation of IPDBSourceFile ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_DIA_DIASOURCEFILE_H
#define LLVM_DEBUGINFO_PDB_DIA_DIASOURCEFILE_H

#include "DIASupport.h"
#include "llvm/DebugInfo/PDB/IPDBSourceFile.h"

namespace llvm {
namespace pdb {
class DIASession;

class DIASourceFile : public IPDBSourceFile {
public:
  explicit DIASourceFile(const DIASession &Session,
                         CComPtr<IDiaSourceFile> DiaSourceFile);

  std::string getFileName() const override;
  uint32_t getUniqueId() const override;
  std::string getChecksum() const override;
  PDB_Checksum getChecksumType() const override;
  std::unique_ptr<IPDBEnumChildren<PDBSymbolCompiland>>
  getCompilands() const override;

  CComPtr<IDiaSourceFile> getDiaFile() const { return SourceFile; }

private:
  const DIASession &Session;
  CComPtr<IDiaSourceFile> SourceFile;
};
}
}

#endif
