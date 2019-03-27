//===- InfoStream.h - PDB Info Stream (Stream 1) Access ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_RAW_PDBINFOSTREAM_H
#define LLVM_DEBUGINFO_PDB_RAW_PDBINFOSTREAM_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/DebugInfo/CodeView/GUID.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/NamedStreamMap.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace pdb {
class InfoStreamBuilder;
class PDBFile;

class InfoStream {
  friend class InfoStreamBuilder;

public:
  InfoStream(std::unique_ptr<BinaryStream> Stream);

  Error reload();

  uint32_t getStreamSize() const;

  const InfoStreamHeader *getHeader() const { return Header; }

  bool containsIdStream() const;
  PdbRaw_ImplVer getVersion() const;
  uint32_t getSignature() const;
  uint32_t getAge() const;
  codeview::GUID getGuid() const;
  uint32_t getNamedStreamMapByteSize() const;

  PdbRaw_Features getFeatures() const;
  ArrayRef<PdbRaw_FeatureSig> getFeatureSignatures() const;

  const NamedStreamMap &getNamedStreams() const;

  BinarySubstreamRef getNamedStreamsBuffer() const;

  Expected<uint32_t> getNamedStreamIndex(llvm::StringRef Name) const;
  StringMap<uint32_t> named_streams() const;

private:
  std::unique_ptr<BinaryStream> Stream;

  const InfoStreamHeader *Header;

  BinarySubstreamRef SubNamedStreams;

  std::vector<PdbRaw_FeatureSig> FeatureSignatures;
  PdbRaw_Features Features = PdbFeatureNone;

  uint32_t NamedStreamMapByteSize = 0;

  NamedStreamMap NamedStreams;
};
}
}

#endif
