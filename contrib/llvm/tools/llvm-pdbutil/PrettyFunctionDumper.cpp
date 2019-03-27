//===- PrettyFunctionDumper.cpp --------------------------------- *- C++ *-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PrettyFunctionDumper.h"
#include "LinePrinter.h"
#include "PrettyBuiltinDumper.h"

#include "llvm/DebugInfo/PDB/IPDBSession.h"
#include "llvm/DebugInfo/PDB/PDBExtras.h"
#include "llvm/DebugInfo/PDB/PDBSymbolData.h"
#include "llvm/DebugInfo/PDB/PDBSymbolFunc.h"
#include "llvm/DebugInfo/PDB/PDBSymbolFuncDebugEnd.h"
#include "llvm/DebugInfo/PDB/PDBSymbolFuncDebugStart.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeArray.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeEnum.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeFunctionArg.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeFunctionSig.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypePointer.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeTypedef.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeUDT.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

namespace {
template <class T>
void dumpClassParentWithScopeOperator(const T &Symbol, LinePrinter &Printer,
                                      FunctionDumper &Dumper) {
  uint32_t ClassParentId = Symbol.getClassParentId();
  auto ClassParent =
      Symbol.getSession().template getConcreteSymbolById<PDBSymbolTypeUDT>(
          ClassParentId);
  if (!ClassParent)
    return;

  WithColor(Printer, PDB_ColorItem::Type).get() << ClassParent->getName();
  Printer << "::";
}
}

FunctionDumper::FunctionDumper(LinePrinter &P)
    : PDBSymDumper(true), Printer(P) {}

void FunctionDumper::start(const PDBSymbolTypeFunctionSig &Symbol,
                           const char *Name, PointerType Pointer) {
  auto ReturnType = Symbol.getReturnType();
  if (!ReturnType)
    Printer << "<unknown-type>";
  else
    ReturnType->dump(*this);
  Printer << " ";
  uint32_t ClassParentId = Symbol.getClassParentId();
  auto ClassParent =
      Symbol.getSession().getConcreteSymbolById<PDBSymbolTypeUDT>(
          ClassParentId);

  PDB_CallingConv CC = Symbol.getCallingConvention();
  bool ShouldDumpCallingConvention = true;
  if ((ClassParent && CC == CallingConvention::ThisCall) ||
      (!ClassParent && CC == CallingConvention::NearStdCall)) {
    ShouldDumpCallingConvention = false;
  }

  if (Pointer == PointerType::None) {
    if (ShouldDumpCallingConvention)
      WithColor(Printer, PDB_ColorItem::Keyword).get() << CC << " ";
    if (ClassParent) {
      Printer << "(";
      WithColor(Printer, PDB_ColorItem::Identifier).get()
          << ClassParent->getName();
      Printer << "::)";
    }
  } else {
    Printer << "(";
    if (ShouldDumpCallingConvention)
      WithColor(Printer, PDB_ColorItem::Keyword).get() << CC << " ";
    if (ClassParent) {
      WithColor(Printer, PDB_ColorItem::Identifier).get()
          << ClassParent->getName();
      Printer << "::";
    }
    if (Pointer == PointerType::Reference)
      Printer << "&";
    else
      Printer << "*";
    if (Name)
      WithColor(Printer, PDB_ColorItem::Identifier).get() << Name;
    Printer << ")";
  }

  Printer << "(";
  if (auto ChildEnum = Symbol.getArguments()) {
    uint32_t Index = 0;
    while (auto Arg = ChildEnum->getNext()) {
      Arg->dump(*this);
      if (++Index < ChildEnum->getChildCount())
        Printer << ", ";
    }
  }
  Printer << ")";

  if (Symbol.isConstType())
    WithColor(Printer, PDB_ColorItem::Keyword).get() << " const";
  if (Symbol.isVolatileType())
    WithColor(Printer, PDB_ColorItem::Keyword).get() << " volatile";
}

void FunctionDumper::start(const PDBSymbolFunc &Symbol, PointerType Pointer) {
  uint64_t FuncStart = Symbol.getVirtualAddress();
  uint64_t FuncEnd = FuncStart + Symbol.getLength();

  Printer << "func [";
  WithColor(Printer, PDB_ColorItem::Address).get() << format_hex(FuncStart, 10);
  if (auto DebugStart = Symbol.findOneChild<PDBSymbolFuncDebugStart>()) {
    uint64_t Prologue = DebugStart->getVirtualAddress() - FuncStart;
    WithColor(Printer, PDB_ColorItem::Offset).get()
        << formatv("+{0,2}", Prologue);
  }
  Printer << " - ";
  WithColor(Printer, PDB_ColorItem::Address).get() << format_hex(FuncEnd, 10);
  if (auto DebugEnd = Symbol.findOneChild<PDBSymbolFuncDebugEnd>()) {
    uint64_t Epilogue = FuncEnd - DebugEnd->getVirtualAddress();
    WithColor(Printer, PDB_ColorItem::Offset).get()
        << formatv("-{0,2}", Epilogue);
  }

  WithColor(Printer, PDB_ColorItem::Comment).get()
      << formatv(" | sizeof={0,3}", Symbol.getLength());
  Printer << "] (";

  if (Symbol.hasFramePointer()) {
    WithColor(Printer, PDB_ColorItem::Register).get()
        << Symbol.getLocalBasePointerRegisterId();
  } else {
    WithColor(Printer, PDB_ColorItem::Register).get() << "FPO";
  }
  Printer << ") ";

  if (Symbol.isVirtual() || Symbol.isPureVirtual())
    WithColor(Printer, PDB_ColorItem::Keyword).get() << "virtual ";

  auto Signature = Symbol.getSignature();
  if (!Signature) {
    WithColor(Printer, PDB_ColorItem::Identifier).get() << Symbol.getName();
    if (Pointer == PointerType::Pointer)
      Printer << "*";
    else if (Pointer == FunctionDumper::PointerType::Reference)
      Printer << "&";
    return;
  }

  auto ReturnType = Signature->getReturnType();
  ReturnType->dump(*this);
  Printer << " ";

  auto ClassParent = Symbol.getClassParent();
  CallingConvention CC = Signature->getCallingConvention();
  if (Pointer != FunctionDumper::PointerType::None)
    Printer << "(";

  if ((ClassParent && CC != CallingConvention::ThisCall) ||
      (!ClassParent && CC != CallingConvention::NearStdCall)) {
    WithColor(Printer, PDB_ColorItem::Keyword).get()
        << Signature->getCallingConvention() << " ";
  }
  WithColor(Printer, PDB_ColorItem::Identifier).get() << Symbol.getName();
  if (Pointer != FunctionDumper::PointerType::None) {
    if (Pointer == PointerType::Pointer)
      Printer << "*";
    else if (Pointer == FunctionDumper::PointerType::Reference)
      Printer << "&";
    Printer << ")";
  }

  Printer << "(";
  if (auto Arguments = Symbol.getArguments()) {
    uint32_t Index = 0;
    while (auto Arg = Arguments->getNext()) {
      auto ArgType = Arg->getType();
      ArgType->dump(*this);
      WithColor(Printer, PDB_ColorItem::Identifier).get() << " "
                                                          << Arg->getName();
      if (++Index < Arguments->getChildCount())
        Printer << ", ";
    }
    if (Signature->isCVarArgs())
      Printer << ", ...";
  }
  Printer << ")";
  if (Symbol.isConstType())
    WithColor(Printer, PDB_ColorItem::Keyword).get() << " const";
  if (Symbol.isVolatileType())
    WithColor(Printer, PDB_ColorItem::Keyword).get() << " volatile";
  if (Symbol.isPureVirtual())
    Printer << " = 0";
}

void FunctionDumper::dump(const PDBSymbolTypeArray &Symbol) {
  auto ElementType = Symbol.getElementType();

  ElementType->dump(*this);
  Printer << "[";
  WithColor(Printer, PDB_ColorItem::LiteralValue).get() << Symbol.getLength();
  Printer << "]";
}

void FunctionDumper::dump(const PDBSymbolTypeBuiltin &Symbol) {
  BuiltinDumper Dumper(Printer);
  Dumper.start(Symbol);
}

void FunctionDumper::dump(const PDBSymbolTypeEnum &Symbol) {
  dumpClassParentWithScopeOperator(Symbol, Printer, *this);
  WithColor(Printer, PDB_ColorItem::Type).get() << Symbol.getName();
}

void FunctionDumper::dump(const PDBSymbolTypeFunctionArg &Symbol) {
  // PDBSymbolTypeFunctionArg is just a shim over the real argument.  Just drill
  // through to the real thing and dump it.
  uint32_t TypeId = Symbol.getTypeId();
  auto Type = Symbol.getSession().getSymbolById(TypeId);
  if (Type)
    Printer << "<unknown-type>";
  else
    Type->dump(*this);
}

void FunctionDumper::dump(const PDBSymbolTypeTypedef &Symbol) {
  dumpClassParentWithScopeOperator(Symbol, Printer, *this);
  WithColor(Printer, PDB_ColorItem::Type).get() << Symbol.getName();
}

void FunctionDumper::dump(const PDBSymbolTypePointer &Symbol) {
  auto PointeeType = Symbol.getPointeeType();
  if (!PointeeType)
    return;

  if (auto FuncSig = unique_dyn_cast<PDBSymbolTypeFunctionSig>(PointeeType)) {
    FunctionDumper NestedDumper(Printer);
    PointerType Pointer =
        Symbol.isReference() ? PointerType::Reference : PointerType::Pointer;
    NestedDumper.start(*FuncSig, nullptr, Pointer);
  } else {
    if (Symbol.isConstType())
      WithColor(Printer, PDB_ColorItem::Keyword).get() << "const ";
    if (Symbol.isVolatileType())
      WithColor(Printer, PDB_ColorItem::Keyword).get() << "volatile ";
    PointeeType->dump(*this);
    Printer << (Symbol.isReference() ? "&" : "*");

    if (Symbol.getRawSymbol().isRestrictedType())
      WithColor(Printer, PDB_ColorItem::Keyword).get() << " __restrict";
  }
}

void FunctionDumper::dump(const PDBSymbolTypeUDT &Symbol) {
  WithColor(Printer, PDB_ColorItem::Type).get() << Symbol.getName();
}
