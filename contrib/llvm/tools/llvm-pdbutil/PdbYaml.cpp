//===- PdbYAML.cpp -------------------------------------------- *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PdbYaml.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/DebugInfo/CodeView/CVTypeVisitor.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/DebugInfo/PDB/Native/TpiHashing.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/ObjectYAML/CodeViewYAMLDebugSections.h"
#include "llvm/ObjectYAML/CodeViewYAMLTypes.h"

using namespace llvm;
using namespace llvm::pdb;
using namespace llvm::pdb::yaml;
using namespace llvm::yaml;

LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::pdb::yaml::NamedStreamMapping)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::pdb::yaml::PdbDbiModuleInfo)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::pdb::yaml::StreamBlockList)
LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(llvm::pdb::PdbRaw_FeatureSig)

namespace llvm {
namespace yaml {

template <> struct ScalarEnumerationTraits<llvm::pdb::PDB_Machine> {
  static void enumeration(IO &io, llvm::pdb::PDB_Machine &Value) {
    io.enumCase(Value, "Invalid", PDB_Machine::Invalid);
    io.enumCase(Value, "Am33", PDB_Machine::Am33);
    io.enumCase(Value, "Amd64", PDB_Machine::Amd64);
    io.enumCase(Value, "Arm", PDB_Machine::Arm);
    io.enumCase(Value, "ArmNT", PDB_Machine::ArmNT);
    io.enumCase(Value, "Ebc", PDB_Machine::Ebc);
    io.enumCase(Value, "x86", PDB_Machine::x86);
    io.enumCase(Value, "Ia64", PDB_Machine::Ia64);
    io.enumCase(Value, "M32R", PDB_Machine::M32R);
    io.enumCase(Value, "Mips16", PDB_Machine::Mips16);
    io.enumCase(Value, "MipsFpu", PDB_Machine::MipsFpu);
    io.enumCase(Value, "MipsFpu16", PDB_Machine::MipsFpu16);
    io.enumCase(Value, "PowerPCFP", PDB_Machine::PowerPCFP);
    io.enumCase(Value, "R4000", PDB_Machine::R4000);
    io.enumCase(Value, "SH3", PDB_Machine::SH3);
    io.enumCase(Value, "SH3DSP", PDB_Machine::SH3DSP);
    io.enumCase(Value, "Thumb", PDB_Machine::Thumb);
    io.enumCase(Value, "WceMipsV2", PDB_Machine::WceMipsV2);
  }
};

template <> struct ScalarEnumerationTraits<llvm::pdb::PdbRaw_DbiVer> {
  static void enumeration(IO &io, llvm::pdb::PdbRaw_DbiVer &Value) {
    io.enumCase(Value, "V41", llvm::pdb::PdbRaw_DbiVer::PdbDbiVC41);
    io.enumCase(Value, "V50", llvm::pdb::PdbRaw_DbiVer::PdbDbiV50);
    io.enumCase(Value, "V60", llvm::pdb::PdbRaw_DbiVer::PdbDbiV60);
    io.enumCase(Value, "V70", llvm::pdb::PdbRaw_DbiVer::PdbDbiV70);
    io.enumCase(Value, "V110", llvm::pdb::PdbRaw_DbiVer::PdbDbiV110);
  }
};

template <> struct ScalarEnumerationTraits<llvm::pdb::PdbRaw_ImplVer> {
  static void enumeration(IO &io, llvm::pdb::PdbRaw_ImplVer &Value) {
    io.enumCase(Value, "VC2", llvm::pdb::PdbRaw_ImplVer::PdbImplVC2);
    io.enumCase(Value, "VC4", llvm::pdb::PdbRaw_ImplVer::PdbImplVC4);
    io.enumCase(Value, "VC41", llvm::pdb::PdbRaw_ImplVer::PdbImplVC41);
    io.enumCase(Value, "VC50", llvm::pdb::PdbRaw_ImplVer::PdbImplVC50);
    io.enumCase(Value, "VC98", llvm::pdb::PdbRaw_ImplVer::PdbImplVC98);
    io.enumCase(Value, "VC70Dep", llvm::pdb::PdbRaw_ImplVer::PdbImplVC70Dep);
    io.enumCase(Value, "VC70", llvm::pdb::PdbRaw_ImplVer::PdbImplVC70);
    io.enumCase(Value, "VC80", llvm::pdb::PdbRaw_ImplVer::PdbImplVC80);
    io.enumCase(Value, "VC110", llvm::pdb::PdbRaw_ImplVer::PdbImplVC110);
    io.enumCase(Value, "VC140", llvm::pdb::PdbRaw_ImplVer::PdbImplVC140);
  }
};

template <> struct ScalarEnumerationTraits<llvm::pdb::PdbRaw_TpiVer> {
  static void enumeration(IO &io, llvm::pdb::PdbRaw_TpiVer &Value) {
    io.enumCase(Value, "VC40", llvm::pdb::PdbRaw_TpiVer::PdbTpiV40);
    io.enumCase(Value, "VC41", llvm::pdb::PdbRaw_TpiVer::PdbTpiV41);
    io.enumCase(Value, "VC50", llvm::pdb::PdbRaw_TpiVer::PdbTpiV50);
    io.enumCase(Value, "VC70", llvm::pdb::PdbRaw_TpiVer::PdbTpiV70);
    io.enumCase(Value, "VC80", llvm::pdb::PdbRaw_TpiVer::PdbTpiV80);
  }
};

template <> struct ScalarEnumerationTraits<llvm::pdb::PdbRaw_FeatureSig> {
  static void enumeration(IO &io, PdbRaw_FeatureSig &Features) {
    io.enumCase(Features, "MinimalDebugInfo",
                PdbRaw_FeatureSig::MinimalDebugInfo);
    io.enumCase(Features, "NoTypeMerge", PdbRaw_FeatureSig::NoTypeMerge);
    io.enumCase(Features, "VC110", PdbRaw_FeatureSig::VC110);
    io.enumCase(Features, "VC140", PdbRaw_FeatureSig::VC140);
  }
};
}
}

void MappingTraits<PdbObject>::mapping(IO &IO, PdbObject &Obj) {
  IO.mapOptional("MSF", Obj.Headers);
  IO.mapOptional("StreamSizes", Obj.StreamSizes);
  IO.mapOptional("StreamMap", Obj.StreamMap);
  IO.mapOptional("StringTable", Obj.StringTable);
  IO.mapOptional("PdbStream", Obj.PdbStream);
  IO.mapOptional("DbiStream", Obj.DbiStream);
  IO.mapOptional("TpiStream", Obj.TpiStream);
  IO.mapOptional("IpiStream", Obj.IpiStream);
  IO.mapOptional("PublicsStream", Obj.PublicsStream);
}

void MappingTraits<MSFHeaders>::mapping(IO &IO, MSFHeaders &Obj) {
  IO.mapOptional("SuperBlock", Obj.SuperBlock);
  IO.mapOptional("NumDirectoryBlocks", Obj.NumDirectoryBlocks);
  IO.mapOptional("DirectoryBlocks", Obj.DirectoryBlocks);
  IO.mapOptional("NumStreams", Obj.NumStreams);
  IO.mapOptional("FileSize", Obj.FileSize);
}

void MappingTraits<msf::SuperBlock>::mapping(IO &IO, msf::SuperBlock &SB) {
  if (!IO.outputting()) {
    ::memcpy(SB.MagicBytes, msf::Magic, sizeof(msf::Magic));
  }

  using u32 = support::ulittle32_t;
  IO.mapOptional("BlockSize", SB.BlockSize, u32(4096U));
  IO.mapOptional("FreeBlockMap", SB.FreeBlockMapBlock, u32(0U));
  IO.mapOptional("NumBlocks", SB.NumBlocks, u32(0U));
  IO.mapOptional("NumDirectoryBytes", SB.NumDirectoryBytes, u32(0U));
  IO.mapOptional("Unknown1", SB.Unknown1, u32(0U));
  IO.mapOptional("BlockMapAddr", SB.BlockMapAddr, u32(0U));
}

void MappingTraits<StreamBlockList>::mapping(IO &IO, StreamBlockList &SB) {
  IO.mapRequired("Stream", SB.Blocks);
}

void MappingTraits<PdbInfoStream>::mapping(IO &IO, PdbInfoStream &Obj) {
  IO.mapOptional("Age", Obj.Age, 1U);
  IO.mapOptional("Guid", Obj.Guid);
  IO.mapOptional("Signature", Obj.Signature, 0U);
  IO.mapOptional("Features", Obj.Features);
  IO.mapOptional("Version", Obj.Version, PdbImplVC70);
}

void MappingTraits<PdbDbiStream>::mapping(IO &IO, PdbDbiStream &Obj) {
  IO.mapOptional("VerHeader", Obj.VerHeader, PdbDbiV70);
  IO.mapOptional("Age", Obj.Age, 1U);
  IO.mapOptional("BuildNumber", Obj.BuildNumber, uint16_t(0U));
  IO.mapOptional("PdbDllVersion", Obj.PdbDllVersion, 0U);
  IO.mapOptional("PdbDllRbld", Obj.PdbDllRbld, uint16_t(0U));
  IO.mapOptional("Flags", Obj.Flags, uint16_t(1U));
  IO.mapOptional("MachineType", Obj.MachineType, PDB_Machine::x86);
  IO.mapOptional("Modules", Obj.ModInfos);
}

void MappingTraits<PdbTpiStream>::mapping(IO &IO,
                                          pdb::yaml::PdbTpiStream &Obj) {
  IO.mapOptional("Version", Obj.Version, PdbTpiV80);
  IO.mapRequired("Records", Obj.Records);
}

void MappingTraits<PdbPublicsStream>::mapping(
    IO &IO, pdb::yaml::PdbPublicsStream &Obj) {
  IO.mapRequired("Records", Obj.PubSyms);
}

void MappingTraits<NamedStreamMapping>::mapping(IO &IO,
                                                NamedStreamMapping &Obj) {
  IO.mapRequired("Name", Obj.StreamName);
  IO.mapRequired("StreamNum", Obj.StreamNumber);
}

void MappingTraits<PdbModiStream>::mapping(IO &IO, PdbModiStream &Obj) {
  IO.mapOptional("Signature", Obj.Signature, 4U);
  IO.mapRequired("Records", Obj.Symbols);
}

void MappingTraits<PdbDbiModuleInfo>::mapping(IO &IO, PdbDbiModuleInfo &Obj) {
  IO.mapRequired("Module", Obj.Mod);
  IO.mapOptional("ObjFile", Obj.Obj, Obj.Mod);
  IO.mapOptional("SourceFiles", Obj.SourceFiles);
  IO.mapOptional("Subsections", Obj.Subsections);
  IO.mapOptional("Modi", Obj.Modi);
}
