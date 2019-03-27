//===- GSIStreamBuilder.h - PDB Publics/Globals Stream Creation -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_RAW_GSISTREAMBUILDER_H
#define LLVM_DEBUGINFO_PDB_RAW_GSISTREAMBUILDER_H

#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/PDB/Native/GlobalsStream.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/BinaryItemStream.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"

namespace llvm {

template <> struct BinaryItemTraits<codeview::CVSymbol> {
  static size_t length(const codeview::CVSymbol &Item) {
    return Item.RecordData.size();
  }
  static ArrayRef<uint8_t> bytes(const codeview::CVSymbol &Item) {
    return Item.RecordData;
  }
};

namespace msf {
class MSFBuilder;
struct MSFLayout;
} // namespace msf
namespace pdb {
struct GSIHashStreamBuilder;

class GSIStreamBuilder {

public:
  explicit GSIStreamBuilder(msf::MSFBuilder &Msf);
  ~GSIStreamBuilder();

  GSIStreamBuilder(const GSIStreamBuilder &) = delete;
  GSIStreamBuilder &operator=(const GSIStreamBuilder &) = delete;

  Error finalizeMsfLayout();

  Error commit(const msf::MSFLayout &Layout, WritableBinaryStreamRef Buffer);

  uint32_t getPublicsStreamIndex() const;
  uint32_t getGlobalsStreamIndex() const;
  uint32_t getRecordStreamIdx() const { return RecordStreamIdx; }

  void addPublicSymbol(const codeview::PublicSym32 &Pub);

  void addGlobalSymbol(const codeview::ProcRefSym &Sym);
  void addGlobalSymbol(const codeview::DataSym &Sym);
  void addGlobalSymbol(const codeview::ConstantSym &Sym);
  void addGlobalSymbol(const codeview::CVSymbol &Sym);

private:
  uint32_t calculatePublicsHashStreamSize() const;
  uint32_t calculateGlobalsHashStreamSize() const;
  Error commitSymbolRecordStream(WritableBinaryStreamRef Stream);
  Error commitPublicsHashStream(WritableBinaryStreamRef Stream);
  Error commitGlobalsHashStream(WritableBinaryStreamRef Stream);

  uint32_t RecordStreamIdx = kInvalidStreamIndex;
  msf::MSFBuilder &Msf;
  std::unique_ptr<GSIHashStreamBuilder> PSH;
  std::unique_ptr<GSIHashStreamBuilder> GSH;
};
} // namespace pdb
} // namespace llvm

#endif
