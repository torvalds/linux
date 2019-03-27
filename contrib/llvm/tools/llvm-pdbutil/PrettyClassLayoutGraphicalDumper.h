//===- PrettyClassLayoutGraphicalDumper.h -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_PRETTYCLASSLAYOUTGRAPHICALDUMPER_H
#define LLVM_TOOLS_LLVMPDBDUMP_PRETTYCLASSLAYOUTGRAPHICALDUMPER_H

#include "llvm/ADT/BitVector.h"

#include "llvm/DebugInfo/PDB/PDBSymDumper.h"

namespace llvm {

namespace pdb {

class UDTLayoutBase;
class LayoutItemBase;
class LinePrinter;

class PrettyClassLayoutGraphicalDumper : public PDBSymDumper {
public:
  PrettyClassLayoutGraphicalDumper(LinePrinter &P, uint32_t RecurseLevel,
                                   uint32_t InitialOffset);

  bool start(const UDTLayoutBase &Layout);

  // Layout based symbol types.
  void dump(const PDBSymbolTypeBaseClass &Symbol) override;
  void dump(const PDBSymbolData &Symbol) override;
  void dump(const PDBSymbolTypeVTable &Symbol) override;

  // Non layout-based symbol types.
  void dump(const PDBSymbolTypeEnum &Symbol) override;
  void dump(const PDBSymbolFunc &Symbol) override;
  void dump(const PDBSymbolTypeTypedef &Symbol) override;
  void dump(const PDBSymbolTypeUDT &Symbol) override;
  void dump(const PDBSymbolTypeBuiltin &Symbol) override;

private:
  bool shouldRecurse() const;
  void printPaddingRow(uint32_t Amount);

  LinePrinter &Printer;

  LayoutItemBase *CurrentItem = nullptr;
  uint32_t RecursionLevel = 0;
  uint32_t ClassOffsetZero = 0;
  uint32_t CurrentAbsoluteOffset = 0;
  bool DumpedAnything = false;
};
}
}
#endif
