//===- DIAInjectedSource.cpp - DIA impl for IPDBInjectedSource --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/DIA/DIAInjectedSource.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/DebugInfo/PDB/ConcreteSymbolEnumerator.h"
#include "llvm/DebugInfo/PDB/DIA/DIASession.h"
#include "llvm/DebugInfo/PDB/DIA/DIAUtils.h"

using namespace llvm;
using namespace llvm::pdb;

DIAInjectedSource::DIAInjectedSource(CComPtr<IDiaInjectedSource> DiaSourceFile)
    : SourceFile(DiaSourceFile) {}

uint32_t DIAInjectedSource::getCrc32() const {
  DWORD Crc;
  return (S_OK == SourceFile->get_crc(&Crc)) ? Crc : 0;
}

uint64_t DIAInjectedSource::getCodeByteSize() const {
  ULONGLONG Size;
  return (S_OK == SourceFile->get_length(&Size)) ? Size : 0;
}

std::string DIAInjectedSource::getFileName() const {
  return invokeBstrMethod(*SourceFile, &IDiaInjectedSource::get_filename);
}

std::string DIAInjectedSource::getObjectFileName() const {
  return invokeBstrMethod(*SourceFile, &IDiaInjectedSource::get_objectFilename);
}

std::string DIAInjectedSource::getVirtualFileName() const {
  return invokeBstrMethod(*SourceFile,
                          &IDiaInjectedSource::get_virtualFilename);
}

PDB_SourceCompression DIAInjectedSource::getCompression() const {
  DWORD Compression = 0;
  if (S_OK != SourceFile->get_sourceCompression(&Compression))
    return PDB_SourceCompression::None;
  return static_cast<PDB_SourceCompression>(Compression);
}

std::string DIAInjectedSource::getCode() const {
  DWORD DataSize;
  if (S_OK != SourceFile->get_source(0, &DataSize, nullptr))
    return "";

  std::vector<uint8_t> Buffer(DataSize);
  if (S_OK != SourceFile->get_source(DataSize, &DataSize, Buffer.data()))
    return "";
  assert(Buffer.size() == DataSize);
  return std::string(reinterpret_cast<const char *>(Buffer.data()),
                     Buffer.size());
}
