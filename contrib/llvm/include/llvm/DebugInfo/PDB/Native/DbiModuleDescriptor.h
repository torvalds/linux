//===- DbiModuleDescriptor.h - PDB module information -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_RAW_DBIMODULEDESCRIPTOR_H
#define LLVM_DEBUGINFO_PDB_RAW_DBIMODULEDESCRIPTOR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <vector>

namespace llvm {

namespace pdb {

class DbiModuleDescriptor {
  friend class DbiStreamBuilder;

public:
  DbiModuleDescriptor();
  DbiModuleDescriptor(const DbiModuleDescriptor &Info);
  ~DbiModuleDescriptor();

  static Error initialize(BinaryStreamRef Stream, DbiModuleDescriptor &Info);

  bool hasECInfo() const;
  uint16_t getTypeServerIndex() const;
  uint16_t getModuleStreamIndex() const;
  uint32_t getSymbolDebugInfoByteSize() const;
  uint32_t getC11LineInfoByteSize() const;
  uint32_t getC13LineInfoByteSize() const;
  uint32_t getNumberOfFiles() const;
  uint32_t getSourceFileNameIndex() const;
  uint32_t getPdbFilePathNameIndex() const;

  StringRef getModuleName() const;
  StringRef getObjFileName() const;

  uint32_t getRecordLength() const;

  const SectionContrib &getSectionContrib() const;

private:
  StringRef ModuleName;
  StringRef ObjFileName;
  const ModuleInfoHeader *Layout = nullptr;
};

} // end namespace pdb

template <> struct VarStreamArrayExtractor<pdb::DbiModuleDescriptor> {
  Error operator()(BinaryStreamRef Stream, uint32_t &Length,
                   pdb::DbiModuleDescriptor &Info) {
    if (auto EC = pdb::DbiModuleDescriptor::initialize(Stream, Info))
      return EC;
    Length = Info.getRecordLength();
    return Error::success();
  }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_RAW_DBIMODULEDESCRIPTOR_H
