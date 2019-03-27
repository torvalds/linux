//==- IPDBSectionContrib.h - Interfaces for PDB SectionContribs --*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_IPDBSECTIONCONTRIB_H
#define LLVM_DEBUGINFO_PDB_IPDBSECTIONCONTRIB_H

#include "PDBTypes.h"

namespace llvm {
namespace pdb {

/// IPDBSectionContrib defines an interface used to represent section
/// contributions whose information are stored in the PDB.
class IPDBSectionContrib {
public:
  virtual ~IPDBSectionContrib();

  virtual std::unique_ptr<PDBSymbolCompiland> getCompiland() const = 0;
  virtual uint32_t getAddressSection() const = 0;
  virtual uint32_t getAddressOffset() const = 0;
  virtual uint32_t getRelativeVirtualAddress() const = 0;
  virtual uint64_t getVirtualAddress() const  = 0;
  virtual uint32_t getLength() const = 0;
  virtual bool isNotPaged() const = 0;
  virtual bool hasCode() const = 0;
  virtual bool hasCode16Bit() const = 0;
  virtual bool hasInitializedData() const = 0;
  virtual bool hasUninitializedData() const = 0;
  virtual bool isRemoved() const = 0;
  virtual bool hasComdat() const = 0;
  virtual bool isDiscardable() const = 0;
  virtual bool isNotCached() const = 0;
  virtual bool isShared() const = 0;
  virtual bool isExecutable() const = 0;
  virtual bool isReadable() const = 0;
  virtual bool isWritable() const = 0;
  virtual uint32_t getDataCrc32() const = 0;
  virtual uint32_t getRelocationsCrc32() const = 0;
  virtual uint32_t getCompilandId() const = 0;
};
}
}

#endif // LLVM_DEBUGINFO_PDB_IPDBSECTIONCONTRIB_H
