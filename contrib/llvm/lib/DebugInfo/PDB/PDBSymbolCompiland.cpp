//===- PDBSymbolCompiland.cpp - compiland details ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/IPDBSession.h"
#include "llvm/DebugInfo/PDB/IPDBSourceFile.h"

#include "llvm/DebugInfo/PDB/PDBSymbolCompiland.h"
#include "llvm/DebugInfo/PDB/PDBSymbolCompilandDetails.h"
#include "llvm/DebugInfo/PDB/PDBSymbolCompilandEnv.h"
#include "llvm/DebugInfo/PDB/PDBSymDumper.h"

#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Path.h"
#include <utility>

using namespace llvm;
using namespace llvm::pdb;

void PDBSymbolCompiland::dump(PDBSymDumper &Dumper) const {
  Dumper.dump(*this);
}

std::string PDBSymbolCompiland::getSourceFileName() const {
  return sys::path::filename(getSourceFileFullPath()).str();
}

std::string PDBSymbolCompiland::getSourceFileFullPath() const {
  std::string SourceFileFullPath;

  // RecordedResult could be the basename, relative path or full path of the
  // source file. Usually it is retrieved and recorded from the command that
  // compiles this compiland.
  //
  //  cmd FileName          -> RecordedResult = .\\FileName
  //  cmd (Path)\\FileName  -> RecordedResult = (Path)\\FileName
  //
  std::string RecordedResult = RawSymbol->getSourceFileName();

  if (RecordedResult.empty()) {
    if (auto Envs = findAllChildren<PDBSymbolCompilandEnv>()) {
      std::string EnvWorkingDir, EnvSrc;

      while (auto Env = Envs->getNext()) {
        std::string Var = Env->getName();
        if (Var == "cwd") {
          EnvWorkingDir = Env->getValue();
          continue;
        }
        if (Var == "src") {
          EnvSrc = Env->getValue();
          if (sys::path::is_absolute(EnvSrc))
            return EnvSrc;
          RecordedResult = EnvSrc;
          continue;
        }
      }
      if (!EnvWorkingDir.empty() && !EnvSrc.empty()) {
        auto Len = EnvWorkingDir.length();
        if (EnvWorkingDir[Len - 1] != '/' && EnvWorkingDir[Len - 1] != '\\') {
          std::string Path = EnvWorkingDir + "\\" + EnvSrc;
          std::replace(Path.begin(), Path.end(), '/', '\\');
          // We will return it as full path if we can't find a better one.
          if (sys::path::is_absolute(Path))
            SourceFileFullPath = Path;
        }
      }
    }
  }

  if (!RecordedResult.empty()) {
    if (sys::path::is_absolute(RecordedResult))
      return RecordedResult;

    // This searches name that has same basename as the one in RecordedResult.
    auto OneSrcFile = Session.findOneSourceFile(
        this, RecordedResult, PDB_NameSearchFlags::NS_CaseInsensitive);
    if (OneSrcFile)
      return OneSrcFile->getFileName();
  }

  // At this point, we have to walk through all source files of this compiland,
  // and determine the right source file if any that is used to generate this
  // compiland based on language indicated in compilanddetails language field.
  auto Details = findOneChild<PDBSymbolCompilandDetails>();
  PDB_Lang Lang = Details ? Details->getLanguage() : PDB_Lang::Cpp;
  auto SrcFiles = Session.getSourceFilesForCompiland(*this);
  if (SrcFiles) {
    bool LangC = (Lang == PDB_Lang::Cpp || Lang == PDB_Lang::C);
    while (auto File = SrcFiles->getNext()) {
      std::string FileName = File->getFileName();
      auto file_extension = sys::path::extension(FileName);
      if (StringSwitch<bool>(file_extension.lower())
              .Case(".cpp", LangC)
              .Case(".c", LangC)
              .Case(".cc", LangC)
              .Case(".cxx", LangC)
              .Case(".asm", Lang == PDB_Lang::Masm)
              .Default(false))
        return File->getFileName();
    }
  }

  return SourceFileFullPath;
}
