//===-- WebAssemblyMCInstLower.h - Lower MachineInstr to MCInst -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the class to lower WebAssembly MachineInstrs to
/// their corresponding MCInst records.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYMCINSTLOWER_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYMCINSTLOWER_H

#include "llvm/MC/MCInst.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class WebAssemblyAsmPrinter;
class MCContext;
class MCSymbol;
class MachineInstr;
class MachineOperand;

/// This class is used to lower an MachineInstr into an MCInst.
class LLVM_LIBRARY_VISIBILITY WebAssemblyMCInstLower {
  MCContext &Ctx;
  WebAssemblyAsmPrinter &Printer;

  MCSymbol *GetGlobalAddressSymbol(const MachineOperand &MO) const;
  MCSymbol *GetExternalSymbolSymbol(const MachineOperand &MO) const;
  MCOperand LowerSymbolOperand(MCSymbol *Sym, int64_t Offset, bool IsFunc,
                               bool IsGlob, bool IsEvent) const;

public:
  WebAssemblyMCInstLower(MCContext &ctx, WebAssemblyAsmPrinter &printer)
      : Ctx(ctx), Printer(printer) {}
  void Lower(const MachineInstr *MI, MCInst &OutMI) const;
};
} // end namespace llvm

#endif
