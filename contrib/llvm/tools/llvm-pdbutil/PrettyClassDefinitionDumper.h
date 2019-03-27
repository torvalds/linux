//===- PrettyClassDefinitionDumper.h ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_PRETTYCLASSDEFINITIONDUMPER_H
#define LLVM_TOOLS_LLVMPDBDUMP_PRETTYCLASSDEFINITIONDUMPER_H

#include "llvm/ADT/BitVector.h"

#include "llvm/DebugInfo/PDB/PDBSymDumper.h"
#include "llvm/DebugInfo/PDB/PDBSymbolData.h"
#include "llvm/DebugInfo/PDB/PDBSymbolFunc.h"

#include <list>
#include <memory>
#include <unordered_map>

namespace llvm {
class BitVector;

namespace pdb {

class ClassLayout;
class LinePrinter;

class ClassDefinitionDumper : public PDBSymDumper {
public:
  ClassDefinitionDumper(LinePrinter &P);

  void start(const PDBSymbolTypeUDT &Class);
  void start(const ClassLayout &Class);

private:
  void prettyPrintClassIntro(const ClassLayout &Class);
  void prettyPrintClassOutro(const ClassLayout &Class);

  LinePrinter &Printer;
  bool DumpedAnything = false;
};
}
}
#endif
