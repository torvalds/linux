//===- YAMLOutputStyle.cpp ------------------------------------ *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "YAMLOutputStyle.h"

#include "PdbYaml.h"
#include "llvm-pdbutil.h"

#include "llvm/DebugInfo/CodeView/DebugChecksumsSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugUnknownSubsection.h"
#include "llvm/DebugInfo/CodeView/StringsAndChecksums.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/DbiStream.h"
#include "llvm/DebugInfo/PDB/Native/GlobalsStream.h"
#include "llvm/DebugInfo/PDB/Native/InfoStream.h"
#include "llvm/DebugInfo/PDB/Native/ModuleDebugStream.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/PublicsStream.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/Native/SymbolStream.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

static bool checkModuleSubsection(opts::ModuleSubsection MS) {
  return any_of(opts::pdb2yaml::DumpModuleSubsections,
                [=](opts::ModuleSubsection M) {
                  return M == MS || M == opts::ModuleSubsection::All;
                });
}

YAMLOutputStyle::YAMLOutputStyle(PDBFile &File)
    : File(File), Out(outs()), Obj(File.getAllocator()) {
  Out.setWriteDefaultValues(!opts::pdb2yaml::Minimal);
}

Error YAMLOutputStyle::dump() {
  if (opts::pdb2yaml::StreamDirectory)
    opts::pdb2yaml::StreamMetadata = true;

  if (auto EC = dumpFileHeaders())
    return EC;

  if (auto EC = dumpStreamMetadata())
    return EC;

  if (auto EC = dumpStreamDirectory())
    return EC;

  if (auto EC = dumpStringTable())
    return EC;

  if (auto EC = dumpPDBStream())
    return EC;

  if (auto EC = dumpDbiStream())
    return EC;

  if (auto EC = dumpTpiStream())
    return EC;

  if (auto EC = dumpIpiStream())
    return EC;

  if (auto EC = dumpPublics())
    return EC;

  flush();
  return Error::success();
}


Error YAMLOutputStyle::dumpFileHeaders() {
  if (opts::pdb2yaml::NoFileHeaders)
    return Error::success();

  yaml::MSFHeaders Headers;
  Obj.Headers.emplace();
  Obj.Headers->SuperBlock.NumBlocks = File.getBlockCount();
  Obj.Headers->SuperBlock.BlockMapAddr = File.getBlockMapIndex();
  Obj.Headers->SuperBlock.BlockSize = File.getBlockSize();
  auto Blocks = File.getDirectoryBlockArray();
  Obj.Headers->DirectoryBlocks.assign(Blocks.begin(), Blocks.end());
  Obj.Headers->NumDirectoryBlocks = File.getNumDirectoryBlocks();
  Obj.Headers->SuperBlock.NumDirectoryBytes = File.getNumDirectoryBytes();
  Obj.Headers->NumStreams =
      opts::pdb2yaml::StreamMetadata ? File.getNumStreams() : 0;
  Obj.Headers->SuperBlock.FreeBlockMapBlock = File.getFreeBlockMapBlock();
  Obj.Headers->SuperBlock.Unknown1 = File.getUnknown1();
  Obj.Headers->FileSize = File.getFileSize();

  return Error::success();
}

Error YAMLOutputStyle::dumpStringTable() {
  bool RequiresStringTable = opts::pdb2yaml::DumpModuleFiles ||
                             !opts::pdb2yaml::DumpModuleSubsections.empty();
  bool RequestedStringTable = opts::pdb2yaml::StringTable;
  if (!RequiresStringTable && !RequestedStringTable)
    return Error::success();

  auto ExpectedST = File.getStringTable();
  if (!ExpectedST)
    return ExpectedST.takeError();

  Obj.StringTable.emplace();
  const auto &ST = ExpectedST.get();
  for (auto ID : ST.name_ids()) {
    auto S = ST.getStringForID(ID);
    if (!S)
      return S.takeError();
    if (S->empty())
      continue;
    Obj.StringTable->push_back(*S);
  }
  return Error::success();
}

Error YAMLOutputStyle::dumpStreamMetadata() {
  if (!opts::pdb2yaml::StreamMetadata)
    return Error::success();

  Obj.StreamSizes.emplace();
  Obj.StreamSizes->assign(File.getStreamSizes().begin(),
                          File.getStreamSizes().end());
  return Error::success();
}

Error YAMLOutputStyle::dumpStreamDirectory() {
  if (!opts::pdb2yaml::StreamDirectory)
    return Error::success();

  auto StreamMap = File.getStreamMap();
  Obj.StreamMap.emplace();
  for (auto &Stream : StreamMap) {
    pdb::yaml::StreamBlockList BlockList;
    BlockList.Blocks.assign(Stream.begin(), Stream.end());
    Obj.StreamMap->push_back(BlockList);
  }

  return Error::success();
}

Error YAMLOutputStyle::dumpPDBStream() {
  if (!opts::pdb2yaml::PdbStream)
    return Error::success();

  auto IS = File.getPDBInfoStream();
  if (!IS)
    return IS.takeError();

  auto &InfoS = IS.get();
  Obj.PdbStream.emplace();
  Obj.PdbStream->Age = InfoS.getAge();
  Obj.PdbStream->Guid = InfoS.getGuid();
  Obj.PdbStream->Signature = InfoS.getSignature();
  Obj.PdbStream->Version = InfoS.getVersion();
  Obj.PdbStream->Features = InfoS.getFeatureSignatures();

  return Error::success();
}

static opts::ModuleSubsection convertSubsectionKind(DebugSubsectionKind K) {
  switch (K) {
  case DebugSubsectionKind::CrossScopeExports:
    return opts::ModuleSubsection::CrossScopeExports;
  case DebugSubsectionKind::CrossScopeImports:
    return opts::ModuleSubsection::CrossScopeImports;
  case DebugSubsectionKind::FileChecksums:
    return opts::ModuleSubsection::FileChecksums;
  case DebugSubsectionKind::InlineeLines:
    return opts::ModuleSubsection::InlineeLines;
  case DebugSubsectionKind::Lines:
    return opts::ModuleSubsection::Lines;
  case DebugSubsectionKind::Symbols:
    return opts::ModuleSubsection::Symbols;
  case DebugSubsectionKind::StringTable:
    return opts::ModuleSubsection::StringTable;
  case DebugSubsectionKind::FrameData:
    return opts::ModuleSubsection::FrameData;
  default:
    return opts::ModuleSubsection::Unknown;
  }
  llvm_unreachable("Unreachable!");
}

Error YAMLOutputStyle::dumpDbiStream() {
  if (!opts::pdb2yaml::DbiStream)
    return Error::success();

  if (!File.hasPDBDbiStream())
    return Error::success();

  auto DbiS = File.getPDBDbiStream();
  if (!DbiS)
    return DbiS.takeError();

  auto &DS = DbiS.get();
  Obj.DbiStream.emplace();
  Obj.DbiStream->Age = DS.getAge();
  Obj.DbiStream->BuildNumber = DS.getBuildNumber();
  Obj.DbiStream->Flags = DS.getFlags();
  Obj.DbiStream->MachineType = DS.getMachineType();
  Obj.DbiStream->PdbDllRbld = DS.getPdbDllRbld();
  Obj.DbiStream->PdbDllVersion = DS.getPdbDllVersion();
  Obj.DbiStream->VerHeader = DS.getDbiVersion();
  if (opts::pdb2yaml::DumpModules) {
    const auto &Modules = DS.modules();
    for (uint32_t I = 0; I < Modules.getModuleCount(); ++I) {
      DbiModuleDescriptor MI = Modules.getModuleDescriptor(I);

      Obj.DbiStream->ModInfos.emplace_back();
      yaml::PdbDbiModuleInfo &DMI = Obj.DbiStream->ModInfos.back();

      DMI.Mod = MI.getModuleName();
      DMI.Obj = MI.getObjFileName();
      if (opts::pdb2yaml::DumpModuleFiles) {
        auto Files = Modules.source_files(I);
        DMI.SourceFiles.assign(Files.begin(), Files.end());
      }

      uint16_t ModiStream = MI.getModuleStreamIndex();
      if (ModiStream == kInvalidStreamIndex)
        continue;

      auto ModStreamData = msf::MappedBlockStream::createIndexedStream(
          File.getMsfLayout(), File.getMsfBuffer(), ModiStream,
          File.getAllocator());

      pdb::ModuleDebugStreamRef ModS(MI, std::move(ModStreamData));
      if (auto EC = ModS.reload())
        return EC;

      auto ExpectedST = File.getStringTable();
      if (!ExpectedST)
        return ExpectedST.takeError();
      if (!opts::pdb2yaml::DumpModuleSubsections.empty() &&
          ModS.hasDebugSubsections()) {
        auto ExpectedChecksums = ModS.findChecksumsSubsection();
        if (!ExpectedChecksums)
          return ExpectedChecksums.takeError();

        StringsAndChecksumsRef SC(ExpectedST->getStringTable(),
                                  *ExpectedChecksums);

        for (const auto &SS : ModS.subsections()) {
          opts::ModuleSubsection OptionKind = convertSubsectionKind(SS.kind());
          if (!checkModuleSubsection(OptionKind))
            continue;

          auto Converted =
              CodeViewYAML::YAMLDebugSubsection::fromCodeViewSubection(SC, SS);
          if (!Converted)
            return Converted.takeError();
          DMI.Subsections.push_back(*Converted);
        }
      }

      if (opts::pdb2yaml::DumpModuleSyms) {
        DMI.Modi.emplace();

        DMI.Modi->Signature = ModS.signature();
        bool HadError = false;
        for (auto &Sym : ModS.symbols(&HadError)) {
          auto ES = CodeViewYAML::SymbolRecord::fromCodeViewSymbol(Sym);
          if (!ES)
            return ES.takeError();

          DMI.Modi->Symbols.push_back(*ES);
        }
      }
    }
  }
  return Error::success();
}

Error YAMLOutputStyle::dumpTpiStream() {
  if (!opts::pdb2yaml::TpiStream)
    return Error::success();

  auto TpiS = File.getPDBTpiStream();
  if (!TpiS)
    return TpiS.takeError();

  auto &TS = TpiS.get();
  Obj.TpiStream.emplace();
  Obj.TpiStream->Version = TS.getTpiVersion();
  for (auto &Record : TS.types(nullptr)) {
    auto ExpectedRecord = CodeViewYAML::LeafRecord::fromCodeViewRecord(Record);
    if (!ExpectedRecord)
      return ExpectedRecord.takeError();
    Obj.TpiStream->Records.push_back(*ExpectedRecord);
  }

  return Error::success();
}

Error YAMLOutputStyle::dumpIpiStream() {
  if (!opts::pdb2yaml::IpiStream)
    return Error::success();

  auto InfoS = File.getPDBInfoStream();
  if (!InfoS)
    return InfoS.takeError();
  if (!InfoS->containsIdStream())
    return Error::success();

  auto IpiS = File.getPDBIpiStream();
  if (!IpiS)
    return IpiS.takeError();

  auto &IS = IpiS.get();
  Obj.IpiStream.emplace();
  Obj.IpiStream->Version = IS.getTpiVersion();
  for (auto &Record : IS.types(nullptr)) {
    auto ExpectedRecord = CodeViewYAML::LeafRecord::fromCodeViewRecord(Record);
    if (!ExpectedRecord)
      return ExpectedRecord.takeError();

    Obj.IpiStream->Records.push_back(*ExpectedRecord);
  }

  return Error::success();
}

Error YAMLOutputStyle::dumpPublics() {
  if (!opts::pdb2yaml::PublicsStream)
    return Error::success();

  Obj.PublicsStream.emplace();
  auto ExpectedPublics = File.getPDBPublicsStream();
  if (!ExpectedPublics) {
    llvm::consumeError(ExpectedPublics.takeError());
    return Error::success();
  }

  PublicsStream &Publics = *ExpectedPublics;
  const GSIHashTable &PublicsTable = Publics.getPublicsTable();

  auto ExpectedSyms = File.getPDBSymbolStream();
  if (!ExpectedSyms) {
    llvm::consumeError(ExpectedSyms.takeError());
    return Error::success();
  }

  BinaryStreamRef SymStream =
      ExpectedSyms->getSymbolArray().getUnderlyingStream();
  for (uint32_t PubSymOff : PublicsTable) {
    Expected<CVSymbol> Sym = readSymbolFromStream(SymStream, PubSymOff);
    if (!Sym)
      return Sym.takeError();
    auto ES = CodeViewYAML::SymbolRecord::fromCodeViewSymbol(*Sym);
    if (!ES)
      return ES.takeError();

    Obj.PublicsStream->PubSyms.push_back(*ES);
  }

  return Error::success();
}

void YAMLOutputStyle::flush() {
  Out << Obj;
  outs().flush();
}
