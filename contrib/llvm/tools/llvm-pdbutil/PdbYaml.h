//===- PdbYAML.h ---------------------------------------------- *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_PDBYAML_H
#define LLVM_TOOLS_LLVMPDBDUMP_PDBYAML_H

#include "OutputStyle.h"

#include "llvm/ADT/Optional.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/MSF/MSFCommon.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/ObjectYAML/CodeViewYAMLDebugSections.h"
#include "llvm/ObjectYAML/CodeViewYAMLSymbols.h"
#include "llvm/ObjectYAML/CodeViewYAMLTypes.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/YAMLTraits.h"

#include <vector>

namespace llvm {
namespace codeview {
class DebugStringTableSubsection;
}
namespace pdb {

namespace yaml {
struct SerializationContext;

struct MSFHeaders {
  msf::SuperBlock SuperBlock;
  uint32_t NumDirectoryBlocks = 0;
  std::vector<uint32_t> DirectoryBlocks;
  uint32_t NumStreams = 0;
  uint32_t FileSize = 0;
};

struct StreamBlockList {
  std::vector<uint32_t> Blocks;
};

struct NamedStreamMapping {
  StringRef StreamName;
  uint32_t StreamNumber;
};

struct PdbInfoStream {
  PdbRaw_ImplVer Version = PdbImplVC70;
  uint32_t Signature = 0;
  uint32_t Age = 1;
  codeview::GUID Guid;
  std::vector<PdbRaw_FeatureSig> Features;
  std::vector<NamedStreamMapping> NamedStreams;
};

struct PdbModiStream {
  uint32_t Signature;
  std::vector<CodeViewYAML::SymbolRecord> Symbols;
};

struct PdbDbiModuleInfo {
  StringRef Obj;
  StringRef Mod;
  std::vector<StringRef> SourceFiles;
  std::vector<CodeViewYAML::YAMLDebugSubsection> Subsections;
  Optional<PdbModiStream> Modi;
};

struct PdbDbiStream {
  PdbRaw_DbiVer VerHeader = PdbDbiV70;
  uint32_t Age = 1;
  uint16_t BuildNumber = 0;
  uint32_t PdbDllVersion = 0;
  uint16_t PdbDllRbld = 0;
  uint16_t Flags = 1;
  PDB_Machine MachineType = PDB_Machine::x86;

  std::vector<PdbDbiModuleInfo> ModInfos;
};

struct PdbTpiStream {
  PdbRaw_TpiVer Version = PdbTpiV80;
  std::vector<CodeViewYAML::LeafRecord> Records;
};

struct PdbPublicsStream {
  std::vector<CodeViewYAML::SymbolRecord> PubSyms;
};

struct PdbObject {
  explicit PdbObject(BumpPtrAllocator &Allocator) : Allocator(Allocator) {}

  Optional<MSFHeaders> Headers;
  Optional<std::vector<uint32_t>> StreamSizes;
  Optional<std::vector<StreamBlockList>> StreamMap;
  Optional<PdbInfoStream> PdbStream;
  Optional<PdbDbiStream> DbiStream;
  Optional<PdbTpiStream> TpiStream;
  Optional<PdbTpiStream> IpiStream;
  Optional<PdbPublicsStream> PublicsStream;

  Optional<std::vector<StringRef>> StringTable;

  BumpPtrAllocator &Allocator;
};
}
}
}

LLVM_YAML_DECLARE_MAPPING_TRAITS(pdb::yaml::PdbObject)
LLVM_YAML_DECLARE_MAPPING_TRAITS(pdb::yaml::MSFHeaders)
LLVM_YAML_DECLARE_MAPPING_TRAITS(msf::SuperBlock)
LLVM_YAML_DECLARE_MAPPING_TRAITS(pdb::yaml::StreamBlockList)
LLVM_YAML_DECLARE_MAPPING_TRAITS(pdb::yaml::PdbInfoStream)
LLVM_YAML_DECLARE_MAPPING_TRAITS(pdb::yaml::PdbDbiStream)
LLVM_YAML_DECLARE_MAPPING_TRAITS(pdb::yaml::PdbTpiStream)
LLVM_YAML_DECLARE_MAPPING_TRAITS(pdb::yaml::PdbPublicsStream)
LLVM_YAML_DECLARE_MAPPING_TRAITS(pdb::yaml::NamedStreamMapping)
LLVM_YAML_DECLARE_MAPPING_TRAITS(pdb::yaml::PdbModiStream)
LLVM_YAML_DECLARE_MAPPING_TRAITS(pdb::yaml::PdbDbiModuleInfo)

#endif // LLVM_TOOLS_LLVMPDBDUMP_PDBYAML_H
