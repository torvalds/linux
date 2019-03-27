//===- PrettyCompilandDumper.cpp - llvm-pdbutil compiland dumper -*- C++ *-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PrettyCompilandDumper.h"

#include "LinePrinter.h"
#include "PrettyFunctionDumper.h"
#include "llvm-pdbutil.h"

#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/IPDBLineNumber.h"
#include "llvm/DebugInfo/PDB/IPDBSession.h"
#include "llvm/DebugInfo/PDB/IPDBSourceFile.h"
#include "llvm/DebugInfo/PDB/PDBExtras.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"
#include "llvm/DebugInfo/PDB/PDBSymbolCompiland.h"
#include "llvm/DebugInfo/PDB/PDBSymbolData.h"
#include "llvm/DebugInfo/PDB/PDBSymbolFunc.h"
#include "llvm/DebugInfo/PDB/PDBSymbolFuncDebugEnd.h"
#include "llvm/DebugInfo/PDB/PDBSymbolFuncDebugStart.h"
#include "llvm/DebugInfo/PDB/PDBSymbolLabel.h"
#include "llvm/DebugInfo/PDB/PDBSymbolThunk.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeFunctionSig.h"
#include "llvm/DebugInfo/PDB/PDBSymbolUnknown.h"
#include "llvm/DebugInfo/PDB/PDBSymbolUsingNamespace.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <utility>

using namespace llvm;
using namespace llvm::pdb;

CompilandDumper::CompilandDumper(LinePrinter &P)
    : PDBSymDumper(true), Printer(P) {}

void CompilandDumper::dump(const PDBSymbolCompilandDetails &Symbol) {}

void CompilandDumper::dump(const PDBSymbolCompilandEnv &Symbol) {}

void CompilandDumper::start(const PDBSymbolCompiland &Symbol,
                            CompilandDumpFlags opts) {
  std::string FullName = Symbol.getName();
  if (Printer.IsCompilandExcluded(FullName))
    return;

  Printer.NewLine();
  WithColor(Printer, PDB_ColorItem::Path).get() << FullName;

  if (opts & Flags::Lines) {
    const IPDBSession &Session = Symbol.getSession();
    if (auto Files = Session.getSourceFilesForCompiland(Symbol)) {
      Printer.Indent();
      while (auto File = Files->getNext()) {
        Printer.NewLine();
        WithColor(Printer, PDB_ColorItem::Path).get() << File->getFileName();
        if (File->getChecksumType() != PDB_Checksum::None) {
          auto ChecksumType = File->getChecksumType();
          auto ChecksumHexString = toHex(File->getChecksum());
          WithColor(Printer, PDB_ColorItem::Comment).get()
              << " (" << ChecksumType << ": " << ChecksumHexString << ")";
        }

        auto Lines = Session.findLineNumbers(Symbol, *File);
        if (!Lines)
          continue;

        Printer.Indent();
        while (auto Line = Lines->getNext()) {
          Printer.NewLine();
          uint32_t LineStart = Line->getLineNumber();
          uint32_t LineEnd = Line->getLineNumberEnd();

          Printer << "Line ";
          PDB_ColorItem StatementColor = Line->isStatement()
            ? PDB_ColorItem::Keyword
            : PDB_ColorItem::LiteralValue;
          WithColor(Printer, StatementColor).get() << LineStart;
          if (LineStart != LineEnd)
            WithColor(Printer, StatementColor).get() << " - " << LineEnd;

          uint32_t ColumnStart = Line->getColumnNumber();
          uint32_t ColumnEnd = Line->getColumnNumberEnd();
          if (ColumnStart != 0 || ColumnEnd != 0) {
            Printer << ", Column: ";
            WithColor(Printer, StatementColor).get() << ColumnStart;
            if (ColumnEnd != ColumnStart)
              WithColor(Printer, StatementColor).get() << " - " << ColumnEnd;
          }

          Printer << ", Address: ";
          if (Line->getLength() > 0) {
            uint64_t AddrStart = Line->getVirtualAddress();
            uint64_t AddrEnd = AddrStart + Line->getLength() - 1;
            WithColor(Printer, PDB_ColorItem::Address).get()
              << "[" << format_hex(AddrStart, 10) << " - "
              << format_hex(AddrEnd, 10) << "]";
            Printer << " (" << Line->getLength() << " bytes)";
          } else {
            uint64_t AddrStart = Line->getVirtualAddress();
            WithColor(Printer, PDB_ColorItem::Address).get()
              << "[" << format_hex(AddrStart, 10) << "] ";
            Printer << "(0 bytes)";
          }
        }
        Printer.Unindent();
      }
      Printer.Unindent();
    }
  }

  if (opts & Flags::Children) {
    if (auto ChildrenEnum = Symbol.findAllChildren()) {
      Printer.Indent();
      while (auto Child = ChildrenEnum->getNext())
        Child->dump(*this);
      Printer.Unindent();
    }
  }
}

void CompilandDumper::dump(const PDBSymbolData &Symbol) {
  if (!shouldDumpSymLevel(opts::pretty::SymLevel::Data))
    return;
  if (Printer.IsSymbolExcluded(Symbol.getName()))
    return;

  Printer.NewLine();

  switch (auto LocType = Symbol.getLocationType()) {
  case PDB_LocType::Static:
    Printer << "data: ";
    WithColor(Printer, PDB_ColorItem::Address).get()
        << "[" << format_hex(Symbol.getVirtualAddress(), 10) << "]";

    WithColor(Printer, PDB_ColorItem::Comment).get()
        << " [sizeof = " << getTypeLength(Symbol) << "]";

    break;
  case PDB_LocType::Constant:
    Printer << "constant: ";
    WithColor(Printer, PDB_ColorItem::LiteralValue).get()
        << "[" << Symbol.getValue() << "]";
    WithColor(Printer, PDB_ColorItem::Comment).get()
        << " [sizeof = " << getTypeLength(Symbol) << "]";
    break;
  default:
    Printer << "data(unexpected type=" << LocType << ")";
  }

  Printer << " ";
  WithColor(Printer, PDB_ColorItem::Identifier).get() << Symbol.getName();
}

void CompilandDumper::dump(const PDBSymbolFunc &Symbol) {
  if (!shouldDumpSymLevel(opts::pretty::SymLevel::Functions))
    return;
  if (Symbol.getLength() == 0)
    return;
  if (Printer.IsSymbolExcluded(Symbol.getName()))
    return;

  Printer.NewLine();
  FunctionDumper Dumper(Printer);
  Dumper.start(Symbol, FunctionDumper::PointerType::None);
}

void CompilandDumper::dump(const PDBSymbolLabel &Symbol) {
  if (Printer.IsSymbolExcluded(Symbol.getName()))
    return;

  Printer.NewLine();
  Printer << "label ";
  WithColor(Printer, PDB_ColorItem::Address).get()
      << "[" << format_hex(Symbol.getVirtualAddress(), 10) << "] ";
  WithColor(Printer, PDB_ColorItem::Identifier).get() << Symbol.getName();
}

void CompilandDumper::dump(const PDBSymbolThunk &Symbol) {
  if (!shouldDumpSymLevel(opts::pretty::SymLevel::Thunks))
    return;
  if (Printer.IsSymbolExcluded(Symbol.getName()))
    return;

  Printer.NewLine();
  Printer << "thunk ";
  codeview::ThunkOrdinal Ordinal = Symbol.getThunkOrdinal();
  uint64_t VA = Symbol.getVirtualAddress();
  if (Ordinal == codeview::ThunkOrdinal::TrampIncremental) {
    uint64_t Target = Symbol.getTargetVirtualAddress();
    WithColor(Printer, PDB_ColorItem::Address).get() << format_hex(VA, 10);
    Printer << " -> ";
    WithColor(Printer, PDB_ColorItem::Address).get() << format_hex(Target, 10);
  } else {
    WithColor(Printer, PDB_ColorItem::Address).get()
        << "[" << format_hex(VA, 10) << " - "
        << format_hex(VA + Symbol.getLength(), 10) << "]";
  }
  Printer << " (";
  WithColor(Printer, PDB_ColorItem::Register).get() << Ordinal;
  Printer << ") ";
  std::string Name = Symbol.getName();
  if (!Name.empty())
    WithColor(Printer, PDB_ColorItem::Identifier).get() << Name;
}

void CompilandDumper::dump(const PDBSymbolTypeTypedef &Symbol) {}

void CompilandDumper::dump(const PDBSymbolUnknown &Symbol) {
  Printer.NewLine();
  Printer << "unknown (" << Symbol.getSymTag() << ")";
}

void CompilandDumper::dump(const PDBSymbolUsingNamespace &Symbol) {
  if (Printer.IsSymbolExcluded(Symbol.getName()))
    return;

  Printer.NewLine();
  Printer << "using namespace ";
  std::string Name = Symbol.getName();
  WithColor(Printer, PDB_ColorItem::Identifier).get() << Name;
}
