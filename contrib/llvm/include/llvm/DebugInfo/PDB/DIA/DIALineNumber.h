//===- DIALineNumber.h - DIA implementation of IPDBLineNumber ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_DIA_DIALINENUMBER_H
#define LLVM_DEBUGINFO_PDB_DIA_DIALINENUMBER_H

#include "DIASupport.h"
#include "llvm/DebugInfo/PDB/IPDBLineNumber.h"

namespace llvm {
namespace pdb {
class DIALineNumber : public IPDBLineNumber {
public:
  explicit DIALineNumber(CComPtr<IDiaLineNumber> DiaLineNumber);

  uint32_t getLineNumber() const override;
  uint32_t getLineNumberEnd() const override;
  uint32_t getColumnNumber() const override;
  uint32_t getColumnNumberEnd() const override;
  uint32_t getAddressSection() const override;
  uint32_t getAddressOffset() const override;
  uint32_t getRelativeVirtualAddress() const override;
  uint64_t getVirtualAddress() const override;
  uint32_t getLength() const override;
  uint32_t getSourceFileId() const override;
  uint32_t getCompilandId() const override;
  bool isStatement() const override;

private:
  CComPtr<IDiaLineNumber> LineNumber;
};
}
}
#endif
