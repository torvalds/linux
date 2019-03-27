//===- PrettyExternalSymbolDumper.cpp -------------------------- *- C++ *-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PrettyExternalSymbolDumper.h"
#include "LinePrinter.h"

#include "llvm/DebugInfo/PDB/PDBSymbolExe.h"
#include "llvm/DebugInfo/PDB/PDBSymbolPublicSymbol.h"
#include "llvm/Support/Format.h"

using namespace llvm;
using namespace llvm::pdb;

ExternalSymbolDumper::ExternalSymbolDumper(LinePrinter &P)
    : PDBSymDumper(true), Printer(P) {}

void ExternalSymbolDumper::start(const PDBSymbolExe &Symbol) {
  if (auto Vars = Symbol.findAllChildren<PDBSymbolPublicSymbol>()) {
    while (auto Var = Vars->getNext())
      Var->dump(*this);
  }
}

void ExternalSymbolDumper::dump(const PDBSymbolPublicSymbol &Symbol) {
  std::string LinkageName = Symbol.getName();
  if (Printer.IsSymbolExcluded(LinkageName))
    return;

  Printer.NewLine();
  uint64_t Addr = Symbol.getVirtualAddress();

  Printer << "public [";
  WithColor(Printer, PDB_ColorItem::Address).get() << format_hex(Addr, 10);
  Printer << "] ";
  WithColor(Printer, PDB_ColorItem::Identifier).get() << LinkageName;
}
