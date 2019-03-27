//===- InfoStreamBuilder.h - PDB Info Stream Creation -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_RAW_PDBINFOSTREAMBUILDER_H
#define LLVM_DEBUGINFO_PDB_RAW_PDBINFOSTREAMBUILDER_H

#include "llvm/ADT/Optional.h"
#include "llvm/Support/Error.h"

#include "llvm/DebugInfo/PDB/Native/NamedStreamMap.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {
class WritableBinaryStreamRef;

namespace msf {
class MSFBuilder;
}
namespace pdb {
class PDBFile;
class NamedStreamMap;

class InfoStreamBuilder {
public:
  InfoStreamBuilder(msf::MSFBuilder &Msf, NamedStreamMap &NamedStreams);
  InfoStreamBuilder(const InfoStreamBuilder &) = delete;
  InfoStreamBuilder &operator=(const InfoStreamBuilder &) = delete;

  void setVersion(PdbRaw_ImplVer V);
  void addFeature(PdbRaw_FeatureSig Sig);

  // If this is true, the PDB contents are hashed and this hash is used as
  // PDB GUID and as Signature. The age is always 1.
  void setHashPDBContentsToGUID(bool B);

  // These only have an effect if hashPDBContentsToGUID() is false.
  void setSignature(uint32_t S);
  void setAge(uint32_t A);
  void setGuid(codeview::GUID G);

  bool hashPDBContentsToGUID() const { return HashPDBContentsToGUID; }
  uint32_t getAge() const { return Age; }
  codeview::GUID getGuid() const { return Guid; }
  Optional<uint32_t> getSignature() const { return Signature; }

  uint32_t finalize();

  Error finalizeMsfLayout();

  Error commit(const msf::MSFLayout &Layout,
               WritableBinaryStreamRef Buffer) const;

private:
  msf::MSFBuilder &Msf;

  std::vector<PdbRaw_FeatureSig> Features;
  PdbRaw_ImplVer Ver;
  uint32_t Age;
  Optional<uint32_t> Signature;
  codeview::GUID Guid;

  bool HashPDBContentsToGUID = false;

  NamedStreamMap &NamedStreams;
};
}
}

#endif
