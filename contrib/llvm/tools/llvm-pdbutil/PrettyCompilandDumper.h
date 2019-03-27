//===- PrettyCompilandDumper.h - llvm-pdbutil compiland dumper -*- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_PRETTYCOMPILANDDUMPER_H
#define LLVM_TOOLS_LLVMPDBDUMP_PRETTYCOMPILANDDUMPER_H

#include "llvm/DebugInfo/PDB/PDBSymDumper.h"

namespace llvm {
namespace pdb {

class LinePrinter;

typedef int CompilandDumpFlags;
class CompilandDumper : public PDBSymDumper {
public:
  enum Flags { None = 0x0, Children = 0x1, Symbols = 0x2, Lines = 0x4 };

  CompilandDumper(LinePrinter &P);

  void start(const PDBSymbolCompiland &Symbol, CompilandDumpFlags flags);

  void dump(const PDBSymbolCompilandDetails &Symbol) override;
  void dump(const PDBSymbolCompilandEnv &Symbol) override;
  void dump(const PDBSymbolData &Symbol) override;
  void dump(const PDBSymbolFunc &Symbol) override;
  void dump(const PDBSymbolLabel &Symbol) override;
  void dump(const PDBSymbolThunk &Symbol) override;
  void dump(const PDBSymbolTypeTypedef &Symbol) override;
  void dump(const PDBSymbolUnknown &Symbol) override;
  void dump(const PDBSymbolUsingNamespace &Symbol) override;

private:
  LinePrinter &Printer;
};
}
}

#endif
