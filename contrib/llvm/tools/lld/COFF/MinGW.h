//===- MinGW.h --------------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COFF_MINGW_H
#define LLD_COFF_MINGW_H

#include "Config.h"
#include "Symbols.h"
#include "lld/Common/LLVM.h"

namespace lld {
namespace coff {

// Logic for deciding what symbols to export, when exporting all
// symbols for MinGW.
class AutoExporter {
public:
  AutoExporter();

  void initSymbolExcludes();

  void addWholeArchive(StringRef Path);

  llvm::StringSet<> ExcludeSymbols;
  llvm::StringSet<> ExcludeSymbolPrefixes;
  llvm::StringSet<> ExcludeSymbolSuffixes;
  llvm::StringSet<> ExcludeLibs;
  llvm::StringSet<> ExcludeObjects;

  bool shouldExport(Defined *Sym) const;
};

void writeDefFile(StringRef Name);

} // namespace coff
} // namespace lld

#endif
