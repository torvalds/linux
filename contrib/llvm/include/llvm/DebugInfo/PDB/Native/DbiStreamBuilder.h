//===- DbiStreamBuilder.h - PDB Dbi Stream Creation -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_RAW_PDBDBISTREAMBUILDER_H
#define LLVM_DEBUGINFO_PDB_RAW_PDBDBISTREAMBUILDER_H

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Support/Error.h"

#include "llvm/DebugInfo/CodeView/DebugFrameDataSubsection.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/PDBStringTableBuilder.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Endian.h"

namespace llvm {
namespace codeview {
struct FrameData;
}
namespace msf {
class MSFBuilder;
}
namespace object {
struct coff_section;
struct FpoData;
}
namespace pdb {
class DbiStream;
struct DbiStreamHeader;
class DbiModuleDescriptorBuilder;
class PDBFile;

class DbiStreamBuilder {
public:
  DbiStreamBuilder(msf::MSFBuilder &Msf);
  ~DbiStreamBuilder();

  DbiStreamBuilder(const DbiStreamBuilder &) = delete;
  DbiStreamBuilder &operator=(const DbiStreamBuilder &) = delete;

  void setVersionHeader(PdbRaw_DbiVer V);
  void setAge(uint32_t A);
  void setBuildNumber(uint16_t B);
  void setBuildNumber(uint8_t Major, uint8_t Minor);
  void setPdbDllVersion(uint16_t V);
  void setPdbDllRbld(uint16_t R);
  void setFlags(uint16_t F);
  void setMachineType(PDB_Machine M);
  void setMachineType(COFF::MachineTypes M);
  void setSectionMap(ArrayRef<SecMapEntry> SecMap);

  // Add given bytes as a new stream.
  Error addDbgStream(pdb::DbgHeaderType Type, ArrayRef<uint8_t> Data);

  uint32_t addECName(StringRef Name);

  uint32_t calculateSerializedLength() const;

  void setGlobalsStreamIndex(uint32_t Index);
  void setPublicsStreamIndex(uint32_t Index);
  void setSymbolRecordStreamIndex(uint32_t Index);
  void addNewFpoData(const codeview::FrameData &FD);
  void addOldFpoData(const object::FpoData &Fpo);

  Expected<DbiModuleDescriptorBuilder &> addModuleInfo(StringRef ModuleName);
  Error addModuleSourceFile(DbiModuleDescriptorBuilder &Module, StringRef File);
  Expected<uint32_t> getSourceFileNameIndex(StringRef FileName);

  Error finalizeMsfLayout();

  Error commit(const msf::MSFLayout &Layout, WritableBinaryStreamRef MsfBuffer);

  void addSectionContrib(const SectionContrib &SC) {
    SectionContribs.emplace_back(SC);
  }

  // A helper function to create a Section Map from a COFF section header.
  static std::vector<SecMapEntry>
  createSectionMap(ArrayRef<llvm::object::coff_section> SecHdrs);

private:
  struct DebugStream {
    std::function<Error(BinaryStreamWriter &)> WriteFn;
    uint32_t Size = 0;
    uint16_t StreamNumber = kInvalidStreamIndex;
  };

  Error finalize();
  uint32_t calculateModiSubstreamSize() const;
  uint32_t calculateNamesOffset() const;
  uint32_t calculateSectionContribsStreamSize() const;
  uint32_t calculateSectionMapStreamSize() const;
  uint32_t calculateFileInfoSubstreamSize() const;
  uint32_t calculateNamesBufferSize() const;
  uint32_t calculateDbgStreamsSize() const;

  Error generateFileInfoSubstream();

  msf::MSFBuilder &Msf;
  BumpPtrAllocator &Allocator;

  Optional<PdbRaw_DbiVer> VerHeader;
  uint32_t Age;
  uint16_t BuildNumber;
  uint16_t PdbDllVersion;
  uint16_t PdbDllRbld;
  uint16_t Flags;
  PDB_Machine MachineType;
  uint32_t GlobalsStreamIndex = kInvalidStreamIndex;
  uint32_t PublicsStreamIndex = kInvalidStreamIndex;
  uint32_t SymRecordStreamIndex = kInvalidStreamIndex;

  const DbiStreamHeader *Header;

  std::vector<std::unique_ptr<DbiModuleDescriptorBuilder>> ModiList;

  Optional<codeview::DebugFrameDataSubsection> NewFpoData;
  std::vector<object::FpoData> OldFpoData;

  StringMap<uint32_t> SourceFileNames;

  PDBStringTableBuilder ECNamesBuilder;
  WritableBinaryStreamRef NamesBuffer;
  MutableBinaryByteStream FileInfoBuffer;
  std::vector<SectionContrib> SectionContribs;
  ArrayRef<SecMapEntry> SectionMap;
  std::array<Optional<DebugStream>, (int)DbgHeaderType::Max> DbgStreams;
};
}
}

#endif
