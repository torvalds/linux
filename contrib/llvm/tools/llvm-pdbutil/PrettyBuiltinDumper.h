//===- PrettyBuiltinDumper.h ---------------------------------- *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_PRETTYBUILTINDUMPER_H
#define LLVM_TOOLS_LLVMPDBDUMP_PRETTYBUILTINDUMPER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/PDB/PDBSymDumper.h"

namespace llvm {
namespace pdb {

class LinePrinter;

class BuiltinDumper : public PDBSymDumper {
public:
  BuiltinDumper(LinePrinter &P);

  void start(const PDBSymbolTypeBuiltin &Symbol);

private:
  StringRef getTypeName(const PDBSymbolTypeBuiltin &Symbol);

  LinePrinter &Printer;
};
}
}

#endif
