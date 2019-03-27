//===- InfoStreamBuilder.cpp - PDB Info Stream Creation ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/InfoStreamBuilder.h"

#include "llvm/DebugInfo/MSF/MSFBuilder.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/InfoStream.h"
#include "llvm/DebugInfo/PDB/Native/NamedStreamMap.h"
#include "llvm/DebugInfo/PDB/Native/PDBFileBuilder.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/Support/BinaryStreamWriter.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::msf;
using namespace llvm::pdb;

InfoStreamBuilder::InfoStreamBuilder(msf::MSFBuilder &Msf,
                                     NamedStreamMap &NamedStreams)
    : Msf(Msf), Ver(PdbRaw_ImplVer::PdbImplVC70), Age(0),
      NamedStreams(NamedStreams) {
  ::memset(&Guid, 0, sizeof(Guid));
}

void InfoStreamBuilder::setVersion(PdbRaw_ImplVer V) { Ver = V; }

void InfoStreamBuilder::addFeature(PdbRaw_FeatureSig Sig) {
  Features.push_back(Sig);
}

void InfoStreamBuilder::setHashPDBContentsToGUID(bool B) {
  HashPDBContentsToGUID = B;
}

void InfoStreamBuilder::setAge(uint32_t A) { Age = A; }

void InfoStreamBuilder::setSignature(uint32_t S) { Signature = S; }

void InfoStreamBuilder::setGuid(GUID G) { Guid = G; }


Error InfoStreamBuilder::finalizeMsfLayout() {
  uint32_t Length = sizeof(InfoStreamHeader) +
                    NamedStreams.calculateSerializedLength() +
                    (Features.size() + 1) * sizeof(uint32_t);
  if (auto EC = Msf.setStreamSize(StreamPDB, Length))
    return EC;
  return Error::success();
}

Error InfoStreamBuilder::commit(const msf::MSFLayout &Layout,
                                WritableBinaryStreamRef Buffer) const {
  auto InfoS = WritableMappedBlockStream::createIndexedStream(
      Layout, Buffer, StreamPDB, Msf.getAllocator());
  BinaryStreamWriter Writer(*InfoS);

  InfoStreamHeader H;
  // Leave the build id fields 0 so they can be set as the last step before
  // committing the file to disk.
  ::memset(&H, 0, sizeof(H));
  H.Version = Ver;
  if (auto EC = Writer.writeObject(H))
    return EC;

  if (auto EC = NamedStreams.commit(Writer))
    return EC;
  if (auto EC = Writer.writeInteger(0))
    return EC;
  for (auto E : Features) {
    if (auto EC = Writer.writeEnum(E))
      return EC;
  }
  assert(Writer.bytesRemaining() == 0);
  return Error::success();
}
