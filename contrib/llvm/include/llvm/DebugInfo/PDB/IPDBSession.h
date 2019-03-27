//===- IPDBSession.h - base interface for a PDB symbol context --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_IPDBSESSION_H
#define LLVM_DEBUGINFO_PDB_IPDBSESSION_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include <memory>

namespace llvm {
namespace pdb {
class PDBSymbolCompiland;
class PDBSymbolExe;

/// IPDBSession defines an interface used to provide a context for querying
/// debug information from a debug data source (for example, a PDB).
class IPDBSession {
public:
  virtual ~IPDBSession();

  virtual uint64_t getLoadAddress() const = 0;
  virtual bool setLoadAddress(uint64_t Address) = 0;
  virtual std::unique_ptr<PDBSymbolExe> getGlobalScope() = 0;
  virtual std::unique_ptr<PDBSymbol>
  getSymbolById(SymIndexId SymbolId) const = 0;

  virtual bool addressForVA(uint64_t VA, uint32_t &Section,
                            uint32_t &Offset) const = 0;
  virtual bool addressForRVA(uint32_t RVA, uint32_t &Section,
                             uint32_t &Offset) const = 0;

  template <typename T>
  std::unique_ptr<T> getConcreteSymbolById(SymIndexId SymbolId) const {
    return unique_dyn_cast_or_null<T>(getSymbolById(SymbolId));
  }

  virtual std::unique_ptr<PDBSymbol>
  findSymbolByAddress(uint64_t Address, PDB_SymType Type) const = 0;
  virtual std::unique_ptr<PDBSymbol>
  findSymbolByRVA(uint32_t RVA, PDB_SymType Type) const = 0;
  virtual std::unique_ptr<PDBSymbol>
  findSymbolBySectOffset(uint32_t Sect, uint32_t Offset,
                         PDB_SymType Type) const = 0;

  virtual std::unique_ptr<IPDBEnumLineNumbers>
  findLineNumbers(const PDBSymbolCompiland &Compiland,
                  const IPDBSourceFile &File) const = 0;
  virtual std::unique_ptr<IPDBEnumLineNumbers>
  findLineNumbersByAddress(uint64_t Address, uint32_t Length) const = 0;
  virtual std::unique_ptr<IPDBEnumLineNumbers>
  findLineNumbersByRVA(uint32_t RVA, uint32_t Length) const = 0;
  virtual std::unique_ptr<IPDBEnumLineNumbers>
  findLineNumbersBySectOffset(uint32_t Section, uint32_t Offset,
                              uint32_t Length) const = 0;

  virtual std::unique_ptr<IPDBEnumSourceFiles>
  findSourceFiles(const PDBSymbolCompiland *Compiland, llvm::StringRef Pattern,
                  PDB_NameSearchFlags Flags) const = 0;
  virtual std::unique_ptr<IPDBSourceFile>
  findOneSourceFile(const PDBSymbolCompiland *Compiland,
                    llvm::StringRef Pattern,
                    PDB_NameSearchFlags Flags) const = 0;
  virtual std::unique_ptr<IPDBEnumChildren<PDBSymbolCompiland>>
  findCompilandsForSourceFile(llvm::StringRef Pattern,
                              PDB_NameSearchFlags Flags) const = 0;
  virtual std::unique_ptr<PDBSymbolCompiland>
  findOneCompilandForSourceFile(llvm::StringRef Pattern,
                                PDB_NameSearchFlags Flags) const = 0;

  virtual std::unique_ptr<IPDBEnumSourceFiles> getAllSourceFiles() const = 0;
  virtual std::unique_ptr<IPDBEnumSourceFiles>
  getSourceFilesForCompiland(const PDBSymbolCompiland &Compiland) const = 0;
  virtual std::unique_ptr<IPDBSourceFile>
  getSourceFileById(uint32_t FileId) const = 0;

  virtual std::unique_ptr<IPDBEnumDataStreams> getDebugStreams() const = 0;

  virtual std::unique_ptr<IPDBEnumTables> getEnumTables() const = 0;

  virtual std::unique_ptr<IPDBEnumInjectedSources>
  getInjectedSources() const = 0;

  virtual std::unique_ptr<IPDBEnumSectionContribs>
  getSectionContribs() const = 0;

  virtual std::unique_ptr<IPDBEnumFrameData>
  getFrameData() const = 0;
};
} // namespace pdb
} // namespace llvm

#endif
