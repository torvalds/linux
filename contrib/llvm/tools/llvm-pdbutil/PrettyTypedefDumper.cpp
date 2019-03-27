//===- PrettyTypedefDumper.cpp - PDBSymDumper impl for typedefs -- * C++ *-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PrettyTypedefDumper.h"

#include "LinePrinter.h"
#include "PrettyBuiltinDumper.h"
#include "PrettyFunctionDumper.h"
#include "PrettyTypeDumper.h"

#include "llvm/DebugInfo/PDB/IPDBSession.h"
#include "llvm/DebugInfo/PDB/PDBExtras.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeEnum.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeFunctionSig.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypePointer.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeTypedef.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeUDT.h"

using namespace llvm;
using namespace llvm::pdb;

TypedefDumper::TypedefDumper(LinePrinter &P) : PDBSymDumper(true), Printer(P) {}

void TypedefDumper::start(const PDBSymbolTypeTypedef &Symbol) {
  WithColor(Printer, PDB_ColorItem::Keyword).get() << "typedef ";
  uint32_t TargetId = Symbol.getTypeId();
  if (auto TypeSymbol = Symbol.getSession().getSymbolById(TargetId))
    TypeSymbol->dump(*this);
  WithColor(Printer, PDB_ColorItem::Identifier).get() << " "
                                                      << Symbol.getName();
}

void TypedefDumper::dump(const PDBSymbolTypeArray &Symbol) {
  TypeDumper Dumper(Printer);
  Dumper.dump(Symbol);
}

void TypedefDumper::dump(const PDBSymbolTypeBuiltin &Symbol) {
  BuiltinDumper Dumper(Printer);
  Dumper.start(Symbol);
}

void TypedefDumper::dump(const PDBSymbolTypeEnum &Symbol) {
  WithColor(Printer, PDB_ColorItem::Keyword).get() << "enum ";
  WithColor(Printer, PDB_ColorItem::Type).get() << " " << Symbol.getName();
}

void TypedefDumper::dump(const PDBSymbolTypePointer &Symbol) {
  if (Symbol.isConstType())
    WithColor(Printer, PDB_ColorItem::Keyword).get() << "const ";
  if (Symbol.isVolatileType())
    WithColor(Printer, PDB_ColorItem::Keyword).get() << "volatile ";
  auto PointeeType = Symbol.getPointeeType();
  if (auto FuncSig = unique_dyn_cast<PDBSymbolTypeFunctionSig>(PointeeType)) {
    FunctionDumper::PointerType Pointer = FunctionDumper::PointerType::Pointer;
    if (Symbol.isReference())
      Pointer = FunctionDumper::PointerType::Reference;
    FunctionDumper NestedDumper(Printer);
    NestedDumper.start(*FuncSig, nullptr, Pointer);
  } else {
    PointeeType->dump(*this);
    Printer << ((Symbol.isReference()) ? "&" : "*");
  }

  if (Symbol.getRawSymbol().isRestrictedType())
    WithColor(Printer, PDB_ColorItem::Keyword).get() << " __restrict";
}

void TypedefDumper::dump(const PDBSymbolTypeFunctionSig &Symbol) {
  FunctionDumper Dumper(Printer);
  Dumper.start(Symbol, nullptr, FunctionDumper::PointerType::None);
}

void TypedefDumper::dump(const PDBSymbolTypeUDT &Symbol) {
  WithColor(Printer, PDB_ColorItem::Keyword).get() << "class ";
  WithColor(Printer, PDB_ColorItem::Type).get() << Symbol.getName();
}
