//===- NativeSession.cpp - Native implementation of IPDBSession -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NativeSession.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/IPDBSourceFile.h"
#include "llvm/DebugInfo/PDB/Native/NativeCompilandSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeEnumTypes.h"
#include "llvm/DebugInfo/PDB/Native/NativeExeSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeBuiltin.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeEnum.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/DebugInfo/PDB/Native/SymbolCache.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/DebugInfo/PDB/PDBSymbolCompiland.h"
#include "llvm/DebugInfo/PDB/PDBSymbolExe.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeEnum.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBuffer.h"

#include <algorithm>
#include <cassert>
#include <memory>
#include <utility>

using namespace llvm;
using namespace llvm::msf;
using namespace llvm::pdb;

static DbiStream *getDbiStreamPtr(PDBFile &File) {
  Expected<DbiStream &> DbiS = File.getPDBDbiStream();
  if (DbiS)
    return &DbiS.get();

  consumeError(DbiS.takeError());
  return nullptr;
}

NativeSession::NativeSession(std::unique_ptr<PDBFile> PdbFile,
                             std::unique_ptr<BumpPtrAllocator> Allocator)
    : Pdb(std::move(PdbFile)), Allocator(std::move(Allocator)),
      Cache(*this, getDbiStreamPtr(*Pdb)) {}

NativeSession::~NativeSession() = default;

Error NativeSession::createFromPdb(std::unique_ptr<MemoryBuffer> Buffer,
                                   std::unique_ptr<IPDBSession> &Session) {
  StringRef Path = Buffer->getBufferIdentifier();
  auto Stream = llvm::make_unique<MemoryBufferByteStream>(
      std::move(Buffer), llvm::support::little);

  auto Allocator = llvm::make_unique<BumpPtrAllocator>();
  auto File = llvm::make_unique<PDBFile>(Path, std::move(Stream), *Allocator);
  if (auto EC = File->parseFileHeaders())
    return EC;
  if (auto EC = File->parseStreamData())
    return EC;

  Session =
      llvm::make_unique<NativeSession>(std::move(File), std::move(Allocator));

  return Error::success();
}

Error NativeSession::createFromExe(StringRef Path,
                                   std::unique_ptr<IPDBSession> &Session) {
  return make_error<RawError>(raw_error_code::feature_unsupported);
}

uint64_t NativeSession::getLoadAddress() const { return 0; }

bool NativeSession::setLoadAddress(uint64_t Address) { return false; }

std::unique_ptr<PDBSymbolExe> NativeSession::getGlobalScope() {
  return PDBSymbol::createAs<PDBSymbolExe>(*this, getNativeGlobalScope());
}

std::unique_ptr<PDBSymbol>
NativeSession::getSymbolById(SymIndexId SymbolId) const {
  return Cache.getSymbolById(SymbolId);
}

bool NativeSession::addressForVA(uint64_t VA, uint32_t &Section,
                                 uint32_t &Offset) const {
  return false;
}

bool NativeSession::addressForRVA(uint32_t VA, uint32_t &Section,
                                  uint32_t &Offset) const {
  return false;
}

std::unique_ptr<PDBSymbol>
NativeSession::findSymbolByAddress(uint64_t Address, PDB_SymType Type) const {
  return nullptr;
}

std::unique_ptr<PDBSymbol>
NativeSession::findSymbolByRVA(uint32_t RVA, PDB_SymType Type) const {
  return nullptr;
}

std::unique_ptr<PDBSymbol>
NativeSession::findSymbolBySectOffset(uint32_t Sect, uint32_t Offset,
                                      PDB_SymType Type) const {
  return nullptr;
}

std::unique_ptr<IPDBEnumLineNumbers>
NativeSession::findLineNumbers(const PDBSymbolCompiland &Compiland,
                               const IPDBSourceFile &File) const {
  return nullptr;
}

std::unique_ptr<IPDBEnumLineNumbers>
NativeSession::findLineNumbersByAddress(uint64_t Address,
                                        uint32_t Length) const {
  return nullptr;
}

std::unique_ptr<IPDBEnumLineNumbers>
NativeSession::findLineNumbersByRVA(uint32_t RVA, uint32_t Length) const {
  return nullptr;
}

std::unique_ptr<IPDBEnumLineNumbers>
NativeSession::findLineNumbersBySectOffset(uint32_t Section, uint32_t Offset,
                                           uint32_t Length) const {
  return nullptr;
}

std::unique_ptr<IPDBEnumSourceFiles>
NativeSession::findSourceFiles(const PDBSymbolCompiland *Compiland,
                               StringRef Pattern,
                               PDB_NameSearchFlags Flags) const {
  return nullptr;
}

std::unique_ptr<IPDBSourceFile>
NativeSession::findOneSourceFile(const PDBSymbolCompiland *Compiland,
                                 StringRef Pattern,
                                 PDB_NameSearchFlags Flags) const {
  return nullptr;
}

std::unique_ptr<IPDBEnumChildren<PDBSymbolCompiland>>
NativeSession::findCompilandsForSourceFile(StringRef Pattern,
                                           PDB_NameSearchFlags Flags) const {
  return nullptr;
}

std::unique_ptr<PDBSymbolCompiland>
NativeSession::findOneCompilandForSourceFile(StringRef Pattern,
                                             PDB_NameSearchFlags Flags) const {
  return nullptr;
}

std::unique_ptr<IPDBEnumSourceFiles> NativeSession::getAllSourceFiles() const {
  return nullptr;
}

std::unique_ptr<IPDBEnumSourceFiles> NativeSession::getSourceFilesForCompiland(
    const PDBSymbolCompiland &Compiland) const {
  return nullptr;
}

std::unique_ptr<IPDBSourceFile>
NativeSession::getSourceFileById(uint32_t FileId) const {
  return nullptr;
}

std::unique_ptr<IPDBEnumDataStreams> NativeSession::getDebugStreams() const {
  return nullptr;
}

std::unique_ptr<IPDBEnumTables> NativeSession::getEnumTables() const {
  return nullptr;
}

std::unique_ptr<IPDBEnumInjectedSources>
NativeSession::getInjectedSources() const {
  return nullptr;
}

std::unique_ptr<IPDBEnumSectionContribs>
NativeSession::getSectionContribs() const {
  return nullptr;
}

std::unique_ptr<IPDBEnumFrameData>
NativeSession::getFrameData() const {
  return nullptr;
}

void NativeSession::initializeExeSymbol() {
  if (ExeSymbol == 0)
    ExeSymbol = Cache.createSymbol<NativeExeSymbol>();
}

NativeExeSymbol &NativeSession::getNativeGlobalScope() const {
  const_cast<NativeSession &>(*this).initializeExeSymbol();

  return Cache.getNativeSymbolById<NativeExeSymbol>(ExeSymbol);
}
