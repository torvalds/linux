//===- IPDBLineNumber.h - base interface for PDB line no. info ---*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_IPDBLINENUMBER_H
#define LLVM_DEBUGINFO_PDB_IPDBLINENUMBER_H

#include "PDBTypes.h"

namespace llvm {
namespace pdb {
class IPDBLineNumber {
public:
  virtual ~IPDBLineNumber();

  virtual uint32_t getLineNumber() const = 0;
  virtual uint32_t getLineNumberEnd() const = 0;
  virtual uint32_t getColumnNumber() const = 0;
  virtual uint32_t getColumnNumberEnd() const = 0;
  virtual uint32_t getAddressSection() const = 0;
  virtual uint32_t getAddressOffset() const = 0;
  virtual uint32_t getRelativeVirtualAddress() const = 0;
  virtual uint64_t getVirtualAddress() const = 0;
  virtual uint32_t getLength() const = 0;
  virtual uint32_t getSourceFileId() const = 0;
  virtual uint32_t getCompilandId() const = 0;
  virtual bool isStatement() const = 0;
};
}
}

#endif
