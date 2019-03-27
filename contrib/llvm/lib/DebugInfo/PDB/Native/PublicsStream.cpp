//===- PublicsStream.cpp - PDB Public Symbol Stream -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The data structures defined in this file are based on the reference
// implementation which is available at
// https://github.com/Microsoft/microsoft-pdb/blob/master/PDB/dbi/gsi.h
//
// When you are reading the reference source code, you'd find the
// information below useful.
//
//  - ppdb1->m_fMinimalDbgInfo seems to be always true.
//  - SMALLBUCKETS macro is defined.
//
// The reference doesn't compile, so I learned just by reading code.
// It's not guaranteed to be correct.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/PublicsStream.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstdint>

using namespace llvm;
using namespace llvm::msf;
using namespace llvm::support;
using namespace llvm::pdb;

PublicsStream::PublicsStream(std::unique_ptr<MappedBlockStream> Stream)
    : Stream(std::move(Stream)) {}

PublicsStream::~PublicsStream() = default;

uint32_t PublicsStream::getSymHash() const { return Header->SymHash; }
uint16_t PublicsStream::getThunkTableSection() const {
  return Header->ISectThunkTable;
}
uint32_t PublicsStream::getThunkTableOffset() const {
  return Header->OffThunkTable;
}

// Publics stream contains fixed-size headers and a serialized hash table.
// This implementation is not complete yet. It reads till the end of the
// stream so that we verify the stream is at least not corrupted. However,
// we skip over the hash table which we believe contains information about
// public symbols.
Error PublicsStream::reload() {
  BinaryStreamReader Reader(*Stream);

  // Check stream size.
  if (Reader.bytesRemaining() <
      sizeof(PublicsStreamHeader) + sizeof(GSIHashHeader))
    return make_error<RawError>(raw_error_code::corrupt_file,
                                "Publics Stream does not contain a header.");

  // Read PSGSIHDR struct.
  if (Reader.readObject(Header))
    return make_error<RawError>(raw_error_code::corrupt_file,
                                "Publics Stream does not contain a header.");

  // Read the hash table.
  if (auto E = PublicsTable.read(Reader))
    return E;

  // Something called "address map" follows.
  uint32_t NumAddressMapEntries = Header->AddrMap / sizeof(uint32_t);
  if (auto EC = Reader.readArray(AddressMap, NumAddressMapEntries))
    return joinErrors(std::move(EC),
                      make_error<RawError>(raw_error_code::corrupt_file,
                                           "Could not read an address map."));

  // Something called "thunk map" follows.
  if (auto EC = Reader.readArray(ThunkMap, Header->NumThunks))
    return joinErrors(std::move(EC),
                      make_error<RawError>(raw_error_code::corrupt_file,
                                           "Could not read a thunk map."));

  // Something called "section map" follows.
  if (Reader.bytesRemaining() > 0) {
    if (auto EC = Reader.readArray(SectionOffsets, Header->NumSections))
      return joinErrors(std::move(EC),
                        make_error<RawError>(raw_error_code::corrupt_file,
                                             "Could not read a section map."));
  }

  if (Reader.bytesRemaining() > 0)
    return make_error<RawError>(raw_error_code::corrupt_file,
                                "Corrupted publics stream.");
  return Error::success();
}
