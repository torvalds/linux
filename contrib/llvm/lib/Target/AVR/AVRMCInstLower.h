//===-- AVRMCInstLower.h - Lower MachineInstr to MCInst ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_AVR_MCINST_LOWER_H
#define LLVM_AVR_MCINST_LOWER_H

#include "llvm/Support/Compiler.h"

namespace llvm {

class AsmPrinter;
class MachineInstr;
class MachineOperand;
class MCContext;
class MCInst;
class MCOperand;
class MCSymbol;

/// Lowers `MachineInstr` objects into `MCInst` objects.
class AVRMCInstLower {
public:
  AVRMCInstLower(MCContext &Ctx, AsmPrinter &Printer)
      : Ctx(Ctx), Printer(Printer) {}

  /// Lowers a `MachineInstr` into a `MCInst`.
  void lowerInstruction(const MachineInstr &MI, MCInst &OutMI) const;
  MCOperand lowerSymbolOperand(const MachineOperand &MO, MCSymbol *Sym) const;

private:
  MCContext &Ctx;
  AsmPrinter &Printer;
};

} // end namespace llvm

#endif // LLVM_AVR_MCINST_LOWER_H

