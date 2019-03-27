//===- PrettyClassLayoutGraphicalDumper.h -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PrettyClassLayoutGraphicalDumper.h"

#include "LinePrinter.h"
#include "PrettyClassDefinitionDumper.h"
#include "PrettyEnumDumper.h"
#include "PrettyFunctionDumper.h"
#include "PrettyTypedefDumper.h"
#include "PrettyVariableDumper.h"
#include "PrettyVariableDumper.h"
#include "llvm-pdbutil.h"

#include "llvm/DebugInfo/PDB/PDBSymbolData.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeBaseClass.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeUDT.h"
#include "llvm/DebugInfo/PDB/UDTLayout.h"
#include "llvm/Support/Format.h"

using namespace llvm;
using namespace llvm::pdb;

PrettyClassLayoutGraphicalDumper::PrettyClassLayoutGraphicalDumper(
    LinePrinter &P, uint32_t RecurseLevel, uint32_t InitialOffset)
    : PDBSymDumper(true), Printer(P), RecursionLevel(RecurseLevel),
      ClassOffsetZero(InitialOffset), CurrentAbsoluteOffset(InitialOffset) {}

bool PrettyClassLayoutGraphicalDumper::start(const UDTLayoutBase &Layout) {

  if (RecursionLevel == 1 &&
      opts::pretty::ClassFormat == opts::pretty::ClassDefinitionFormat::All) {
    for (auto &Other : Layout.other_items())
      Other->dump(*this);
    for (auto &Func : Layout.funcs())
      Func->dump(*this);
  }

  const BitVector &UseMap = Layout.usedBytes();
  int NextPaddingByte = UseMap.find_first_unset();

  for (auto &Item : Layout.layout_items()) {
    // Calculate the absolute offset of the first byte of the next field.
    uint32_t RelativeOffset = Item->getOffsetInParent();
    CurrentAbsoluteOffset = ClassOffsetZero + RelativeOffset;

    // This might be an empty base, in which case it could extend outside the
    // bounds of the parent class.
    if (RelativeOffset < UseMap.size() && (Item->getSize() > 0)) {
      // If there is any remaining padding in this class, and the offset of the
      // new item is after the padding, then we must have just jumped over some
      // padding.  Print a padding row and then look for where the next block
      // of padding begins.
      if ((NextPaddingByte >= 0) &&
          (RelativeOffset > uint32_t(NextPaddingByte))) {
        printPaddingRow(RelativeOffset - NextPaddingByte);
        NextPaddingByte = UseMap.find_next_unset(RelativeOffset);
      }
    }

    CurrentItem = Item;
    if (Item->isVBPtr()) {
      VTableLayoutItem &Layout = static_cast<VTableLayoutItem &>(*CurrentItem);

      VariableDumper VarDumper(Printer);
      VarDumper.startVbptr(CurrentAbsoluteOffset, Layout.getSize());
    } else {
      if (auto Sym = Item->getSymbol())
        Sym->dump(*this);
    }

    if (Item->getLayoutSize() > 0) {
      uint32_t Prev = RelativeOffset + Item->getLayoutSize() - 1;
      if (Prev < UseMap.size())
        NextPaddingByte = UseMap.find_next_unset(Prev);
    }
  }

  auto TailPadding = Layout.tailPadding();
  if (TailPadding > 0) {
    if (TailPadding != 1 || Layout.getSize() != 1) {
      Printer.NewLine();
      WithColor(Printer, PDB_ColorItem::Padding).get()
          << "<padding> (" << TailPadding << " bytes)";
      DumpedAnything = true;
    }
  }

  return DumpedAnything;
}

void PrettyClassLayoutGraphicalDumper::printPaddingRow(uint32_t Amount) {
  if (Amount == 0)
    return;

  Printer.NewLine();
  WithColor(Printer, PDB_ColorItem::Padding).get() << "<padding> (" << Amount
                                                   << " bytes)";
  DumpedAnything = true;
}

void PrettyClassLayoutGraphicalDumper::dump(
    const PDBSymbolTypeBaseClass &Symbol) {
  assert(CurrentItem != nullptr);

  Printer.NewLine();
  BaseClassLayout &Layout = static_cast<BaseClassLayout &>(*CurrentItem);

  std::string Label = "base";
  if (Layout.isVirtualBase()) {
    Label.insert(Label.begin(), 'v');
    if (Layout.getBase().isIndirectVirtualBaseClass())
      Label.insert(Label.begin(), 'i');
  }
  Printer << Label << " ";

  uint32_t Size = Layout.isEmptyBase() ? 1 : Layout.getLayoutSize();

  WithColor(Printer, PDB_ColorItem::Offset).get()
      << "+" << format_hex(CurrentAbsoluteOffset, 4) << " [sizeof=" << Size
      << "] ";

  WithColor(Printer, PDB_ColorItem::Identifier).get() << Layout.getName();

  if (shouldRecurse()) {
    Printer.Indent();
    uint32_t ChildOffsetZero = ClassOffsetZero + Layout.getOffsetInParent();
    PrettyClassLayoutGraphicalDumper BaseDumper(Printer, RecursionLevel + 1,
                                                ChildOffsetZero);
    DumpedAnything |= BaseDumper.start(Layout);
    Printer.Unindent();
  }

  DumpedAnything = true;
}

bool PrettyClassLayoutGraphicalDumper::shouldRecurse() const {
  uint32_t Limit = opts::pretty::ClassRecursionDepth;
  if (Limit == 0)
    return true;
  return RecursionLevel < Limit;
}

void PrettyClassLayoutGraphicalDumper::dump(const PDBSymbolData &Symbol) {
  VariableDumper VarDumper(Printer);
  VarDumper.start(Symbol, ClassOffsetZero);

  if (CurrentItem != nullptr) {
    DataMemberLayoutItem &Layout =
        static_cast<DataMemberLayoutItem &>(*CurrentItem);

    if (Layout.hasUDTLayout() && shouldRecurse()) {
      uint32_t ChildOffsetZero = ClassOffsetZero + Layout.getOffsetInParent();
      Printer.Indent();
      PrettyClassLayoutGraphicalDumper TypeDumper(Printer, RecursionLevel + 1,
                                                  ChildOffsetZero);
      TypeDumper.start(Layout.getUDTLayout());
      Printer.Unindent();
    }
  }

  DumpedAnything = true;
}

void PrettyClassLayoutGraphicalDumper::dump(const PDBSymbolTypeVTable &Symbol) {
  assert(CurrentItem != nullptr);

  VariableDumper VarDumper(Printer);
  VarDumper.start(Symbol, ClassOffsetZero);

  DumpedAnything = true;
}

void PrettyClassLayoutGraphicalDumper::dump(const PDBSymbolTypeEnum &Symbol) {
  DumpedAnything = true;
  Printer.NewLine();
  EnumDumper Dumper(Printer);
  Dumper.start(Symbol);
}

void PrettyClassLayoutGraphicalDumper::dump(
    const PDBSymbolTypeTypedef &Symbol) {
  DumpedAnything = true;
  Printer.NewLine();
  TypedefDumper Dumper(Printer);
  Dumper.start(Symbol);
}

void PrettyClassLayoutGraphicalDumper::dump(
    const PDBSymbolTypeBuiltin &Symbol) {}

void PrettyClassLayoutGraphicalDumper::dump(const PDBSymbolTypeUDT &Symbol) {}

void PrettyClassLayoutGraphicalDumper::dump(const PDBSymbolFunc &Symbol) {
  if (Printer.IsSymbolExcluded(Symbol.getName()))
    return;
  if (Symbol.isCompilerGenerated() && opts::pretty::ExcludeCompilerGenerated)
    return;
  if (Symbol.getLength() == 0 && !Symbol.isPureVirtual() &&
      !Symbol.isIntroVirtualFunction())
    return;

  DumpedAnything = true;
  Printer.NewLine();
  FunctionDumper Dumper(Printer);
  Dumper.start(Symbol, FunctionDumper::PointerType::None);
}
