//===-- RuntimeDyldCheckerImpl.h -- RuntimeDyld test framework --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_RUNTIMEDYLDCHECKERIMPL_H
#define LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_RUNTIMEDYLDCHECKERIMPL_H

#include "RuntimeDyldImpl.h"

namespace llvm {

class RuntimeDyldCheckerImpl {
  friend class RuntimeDyldChecker;
  friend class RuntimeDyldImpl;
  friend class RuntimeDyldCheckerExprEval;
  friend class RuntimeDyldELF;

public:
  RuntimeDyldCheckerImpl(RuntimeDyld &RTDyld, MCDisassembler *Disassembler,
                         MCInstPrinter *InstPrinter,
                         llvm::raw_ostream &ErrStream);

  bool check(StringRef CheckExpr) const;
  bool checkAllRulesInBuffer(StringRef RulePrefix, MemoryBuffer *MemBuf) const;

private:

  // StubMap typedefs.
  typedef std::map<std::string, uint64_t> StubOffsetsMap;
  struct SectionAddressInfo {
    uint64_t SectionID;
    StubOffsetsMap StubOffsets;
  };
  typedef std::map<std::string, SectionAddressInfo> SectionMap;
  typedef std::map<std::string, SectionMap> StubMap;

  RuntimeDyldImpl &getRTDyld() const { return *RTDyld.Dyld; }

  Expected<JITSymbolResolver::LookupResult>
  lookup(const JITSymbolResolver::LookupSet &Symbols) const;

  bool isSymbolValid(StringRef Symbol) const;
  uint64_t getSymbolLocalAddr(StringRef Symbol) const;
  uint64_t getSymbolRemoteAddr(StringRef Symbol) const;
  uint64_t readMemoryAtAddr(uint64_t Addr, unsigned Size) const;

  std::pair<const SectionAddressInfo*, std::string> findSectionAddrInfo(
                                                   StringRef FileName,
                                                   StringRef SectionName) const;

  std::pair<uint64_t, std::string> getSectionAddr(StringRef FileName,
                                                  StringRef SectionName,
                                                  bool IsInsideLoad) const;

  std::pair<uint64_t, std::string> getStubAddrFor(StringRef FileName,
                                                  StringRef SectionName,
                                                  StringRef Symbol,
                                                  bool IsInsideLoad) const;
  StringRef getSubsectionStartingAt(StringRef Name) const;

  Optional<uint64_t> getSectionLoadAddress(void *LocalAddr) const;

  void registerSection(StringRef FilePath, unsigned SectionID);
  void registerStubMap(StringRef FilePath, unsigned SectionID,
                       const RuntimeDyldImpl::StubMap &RTDyldStubs);

  RuntimeDyld &RTDyld;
  MCDisassembler *Disassembler;
  MCInstPrinter *InstPrinter;
  llvm::raw_ostream &ErrStream;

  StubMap Stubs;
};
}

#endif
