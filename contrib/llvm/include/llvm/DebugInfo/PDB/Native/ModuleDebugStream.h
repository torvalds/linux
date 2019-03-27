//===- ModuleDebugStream.h - PDB Module Info Stream Access ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_MODULEDEBUGSTREAM_H
#define LLVM_DEBUGINFO_PDB_NATIVE_MODULEDEBUGSTREAM_H

#include "llvm/ADT/iterator_range.h"
#include "llvm/DebugInfo/CodeView/DebugChecksumsSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugSubsectionRecord.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/DbiModuleDescriptor.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>

namespace llvm {
namespace pdb {

class DbiModuleDescriptor;

class ModuleDebugStreamRef {
  using DebugSubsectionIterator = codeview::DebugSubsectionArray::Iterator;

public:
  ModuleDebugStreamRef(const DbiModuleDescriptor &Module,
                       std::unique_ptr<msf::MappedBlockStream> Stream);
  ModuleDebugStreamRef(ModuleDebugStreamRef &&Other) = default;
  ModuleDebugStreamRef(const ModuleDebugStreamRef &Other) = default;
  ~ModuleDebugStreamRef();

  Error reload();

  uint32_t signature() const { return Signature; }

  iterator_range<codeview::CVSymbolArray::Iterator>
  symbols(bool *HadError) const;

  const codeview::CVSymbolArray &getSymbolArray() const { return SymbolArray; }
  const codeview::CVSymbolArray
  getSymbolArrayForScope(uint32_t ScopeBegin) const;

  BinarySubstreamRef getSymbolsSubstream() const;
  BinarySubstreamRef getC11LinesSubstream() const;
  BinarySubstreamRef getC13LinesSubstream() const;
  BinarySubstreamRef getGlobalRefsSubstream() const;

  ModuleDebugStreamRef &operator=(ModuleDebugStreamRef &&Other) = delete;

  codeview::CVSymbol readSymbolAtOffset(uint32_t Offset) const;

  iterator_range<DebugSubsectionIterator> subsections() const;
  codeview::DebugSubsectionArray getSubsectionsArray() const {
    return Subsections;
  }

  bool hasDebugSubsections() const;

  Error commit();

  Expected<codeview::DebugChecksumsSubsectionRef>
  findChecksumsSubsection() const;

private:
  DbiModuleDescriptor Mod;

  uint32_t Signature;

  std::shared_ptr<msf::MappedBlockStream> Stream;

  codeview::CVSymbolArray SymbolArray;

  BinarySubstreamRef SymbolsSubstream;
  BinarySubstreamRef C11LinesSubstream;
  BinarySubstreamRef C13LinesSubstream;
  BinarySubstreamRef GlobalRefsSubstream;

  codeview::DebugSubsectionArray Subsections;
};

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_MODULEDEBUGSTREAM_H
