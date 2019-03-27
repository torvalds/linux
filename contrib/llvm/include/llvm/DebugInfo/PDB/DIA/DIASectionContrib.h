//===- DIASectionContrib.h - DIA Impl. of IPDBSectionContrib ------ C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_DIA_DIASECTIONCONTRIB_H
#define LLVM_DEBUGINFO_PDB_DIA_DIASECTIONCONTRIB_H

#include "DIASupport.h"
#include "llvm/DebugInfo/PDB/IPDBSectionContrib.h"

namespace llvm {
namespace pdb {
class DIASession;

class DIASectionContrib : public IPDBSectionContrib {
public:
  explicit DIASectionContrib(const DIASession &PDBSession,
                             CComPtr<IDiaSectionContrib> DiaSection);

  std::unique_ptr<PDBSymbolCompiland> getCompiland() const override;
  uint32_t getAddressSection() const override;
  uint32_t getAddressOffset() const override;
  uint32_t getRelativeVirtualAddress() const override;
  uint64_t getVirtualAddress() const override;
  uint32_t getLength() const override;
  bool isNotPaged() const override;
  bool hasCode() const override;
  bool hasCode16Bit() const override;
  bool hasInitializedData() const override;
  bool hasUninitializedData() const override;
  bool isRemoved() const override;
  bool hasComdat() const override;
  bool isDiscardable() const override;
  bool isNotCached() const override;
  bool isShared() const override;
  bool isExecutable() const override;
  bool isReadable() const override;
  bool isWritable() const override;
  uint32_t getDataCrc32() const override;
  uint32_t getRelocationsCrc32() const override;
  uint32_t getCompilandId() const override;

private:
  const DIASession &Session;
  CComPtr<IDiaSectionContrib> Section;
};
} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_DIA_DIASECTIONCONTRIB_H
