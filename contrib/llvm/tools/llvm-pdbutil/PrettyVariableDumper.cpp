//===- PrettyVariableDumper.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PrettyVariableDumper.h"

#include "LinePrinter.h"
#include "PrettyBuiltinDumper.h"
#include "PrettyFunctionDumper.h"
#include "llvm-pdbutil.h"

#include "llvm/DebugInfo/PDB/IPDBSession.h"
#include "llvm/DebugInfo/PDB/PDBSymbolData.h"
#include "llvm/DebugInfo/PDB/PDBSymbolFunc.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeArray.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeEnum.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeEnum.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeFunctionSig.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypePointer.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeTypedef.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeUDT.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

#include "llvm/Support/Format.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

VariableDumper::VariableDumper(LinePrinter &P)
    : PDBSymDumper(true), Printer(P) {}

void VariableDumper::start(const PDBSymbolData &Var, uint32_t Offset) {
  if (Var.isCompilerGenerated() && opts::pretty::ExcludeCompilerGenerated)
    return;
  if (Printer.IsSymbolExcluded(Var.getName()))
    return;

  auto VarType = Var.getType();

  uint64_t Length = VarType->getRawSymbol().getLength();

  switch (auto LocType = Var.getLocationType()) {
  case PDB_LocType::Static:
    Printer.NewLine();
    Printer << "data [";
    WithColor(Printer, PDB_ColorItem::Address).get()
        << format_hex(Var.getVirtualAddress(), 10);
    Printer << ", sizeof=" << Length << "] ";
    WithColor(Printer, PDB_ColorItem::Keyword).get() << "static ";
    dumpSymbolTypeAndName(*VarType, Var.getName());
    break;
  case PDB_LocType::Constant:
    if (isa<PDBSymbolTypeEnum>(*VarType))
      break;
    Printer.NewLine();
    Printer << "data [sizeof=" << Length << "] ";
    dumpSymbolTypeAndName(*VarType, Var.getName());
    Printer << " = ";
    WithColor(Printer, PDB_ColorItem::LiteralValue).get() << Var.getValue();
    break;
  case PDB_LocType::ThisRel:
    Printer.NewLine();
    Printer << "data ";
    WithColor(Printer, PDB_ColorItem::Offset).get()
        << "+" << format_hex(Offset + Var.getOffset(), 4)
        << " [sizeof=" << Length << "] ";
    dumpSymbolTypeAndName(*VarType, Var.getName());
    break;
  case PDB_LocType::BitField:
    Printer.NewLine();
    Printer << "data ";
    WithColor(Printer, PDB_ColorItem::Offset).get()
        << "+" << format_hex(Offset + Var.getOffset(), 4)
        << " [sizeof=" << Length << "] ";
    dumpSymbolTypeAndName(*VarType, Var.getName());
    Printer << " : ";
    WithColor(Printer, PDB_ColorItem::LiteralValue).get() << Var.getLength();
    break;
  default:
    Printer.NewLine();
    Printer << "data [sizeof=" << Length << "] ";
    Printer << "unknown(" << LocType << ") ";
    WithColor(Printer, PDB_ColorItem::Identifier).get() << Var.getName();
    break;
  }
}

void VariableDumper::startVbptr(uint32_t Offset, uint32_t Size) {
  Printer.NewLine();
  Printer << "vbptr ";

  WithColor(Printer, PDB_ColorItem::Offset).get()
      << "+" << format_hex(Offset, 4) << " [sizeof=" << Size << "] ";
}

void VariableDumper::start(const PDBSymbolTypeVTable &Var, uint32_t Offset) {
  Printer.NewLine();
  Printer << "vfptr ";
  auto VTableType = cast<PDBSymbolTypePointer>(Var.getType());
  uint32_t PointerSize = VTableType->getLength();

  WithColor(Printer, PDB_ColorItem::Offset).get()
      << "+" << format_hex(Offset + Var.getOffset(), 4)
      << " [sizeof=" << PointerSize << "] ";
}

void VariableDumper::dump(const PDBSymbolTypeArray &Symbol) {
  auto ElementType = Symbol.getElementType();
  assert(ElementType);
  if (!ElementType)
    return;
  ElementType->dump(*this);
}

void VariableDumper::dumpRight(const PDBSymbolTypeArray &Symbol) {
  auto ElementType = Symbol.getElementType();
  assert(ElementType);
  if (!ElementType)
    return;
  Printer << '[' << Symbol.getCount() << ']';
  ElementType->dumpRight(*this);
}

void VariableDumper::dump(const PDBSymbolTypeBuiltin &Symbol) {
  BuiltinDumper Dumper(Printer);
  Dumper.start(Symbol);
}

void VariableDumper::dump(const PDBSymbolTypeEnum &Symbol) {
  WithColor(Printer, PDB_ColorItem::Type).get() << Symbol.getName();
}

void VariableDumper::dump(const PDBSymbolTypeFunctionSig &Symbol) {
  auto ReturnType = Symbol.getReturnType();
  ReturnType->dump(*this);
  Printer << " ";

  uint32_t ClassParentId = Symbol.getClassParentId();
  auto ClassParent =
      Symbol.getSession().getConcreteSymbolById<PDBSymbolTypeUDT>(
          ClassParentId);

  if (ClassParent) {
    WithColor(Printer, PDB_ColorItem::Identifier).get()
      << ClassParent->getName();
    Printer << "::";
  }
}

void VariableDumper::dumpRight(const PDBSymbolTypeFunctionSig &Symbol) {
  Printer << "(";
  if (auto Arguments = Symbol.getArguments()) {
    uint32_t Index = 0;
    while (auto Arg = Arguments->getNext()) {
      Arg->dump(*this);
      if (++Index < Arguments->getChildCount())
        Printer << ", ";
    }
  }
  Printer << ")";

  if (Symbol.isConstType())
    WithColor(Printer, PDB_ColorItem::Keyword).get() << " const";
  if (Symbol.isVolatileType())
    WithColor(Printer, PDB_ColorItem::Keyword).get() << " volatile";

  if (Symbol.getRawSymbol().isRestrictedType())
    WithColor(Printer, PDB_ColorItem::Keyword).get() << " __restrict";
}

void VariableDumper::dump(const PDBSymbolTypePointer &Symbol) {
  auto PointeeType = Symbol.getPointeeType();
  if (!PointeeType)
    return;
  PointeeType->dump(*this);
  if (auto FuncSig = unique_dyn_cast<PDBSymbolTypeFunctionSig>(PointeeType)) {
    // A hack to get the calling convention in the right spot.
    Printer << " (";
    PDB_CallingConv CC = FuncSig->getCallingConvention();
    WithColor(Printer, PDB_ColorItem::Keyword).get() << CC << " ";
  } else if (isa<PDBSymbolTypeArray>(PointeeType)) {
    Printer << " (";
  }
  Printer << (Symbol.isReference() ? "&" : "*");
  if (Symbol.isConstType())
    WithColor(Printer, PDB_ColorItem::Keyword).get() << " const ";
  if (Symbol.isVolatileType())
    WithColor(Printer, PDB_ColorItem::Keyword).get() << " volatile ";

  if (Symbol.getRawSymbol().isRestrictedType())
    WithColor(Printer, PDB_ColorItem::Keyword).get() << " __restrict ";
}

void VariableDumper::dumpRight(const PDBSymbolTypePointer &Symbol) {
  auto PointeeType = Symbol.getPointeeType();
  assert(PointeeType);
  if (!PointeeType)
    return;
  if (isa<PDBSymbolTypeFunctionSig>(PointeeType) ||
      isa<PDBSymbolTypeArray>(PointeeType)) {
    Printer << ")";
  }
  PointeeType->dumpRight(*this);
}

void VariableDumper::dump(const PDBSymbolTypeTypedef &Symbol) {
  WithColor(Printer, PDB_ColorItem::Keyword).get() << "typedef ";
  WithColor(Printer, PDB_ColorItem::Type).get() << Symbol.getName();
}

void VariableDumper::dump(const PDBSymbolTypeUDT &Symbol) {
  WithColor(Printer, PDB_ColorItem::Type).get() << Symbol.getName();
}

void VariableDumper::dumpSymbolTypeAndName(const PDBSymbol &Type,
                                           StringRef Name) {
  Type.dump(*this);
  WithColor(Printer, PDB_ColorItem::Identifier).get() << " " << Name;
  Type.dumpRight(*this);
}
