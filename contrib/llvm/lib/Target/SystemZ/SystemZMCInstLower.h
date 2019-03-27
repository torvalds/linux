//===-- SystemZMCInstLower.h - Lower MachineInstr to MCInst ----*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZMCINSTLOWER_H
#define LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZMCINSTLOWER_H

#include "llvm/MC/MCExpr.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"

namespace llvm {
class MCInst;
class MCOperand;
class MachineInstr;
class MachineOperand;
class Mangler;
class SystemZAsmPrinter;

class LLVM_LIBRARY_VISIBILITY SystemZMCInstLower {
  MCContext &Ctx;
  SystemZAsmPrinter &AsmPrinter;

public:
  SystemZMCInstLower(MCContext &ctx, SystemZAsmPrinter &asmPrinter);

  // Lower MachineInstr MI to MCInst OutMI.
  void lower(const MachineInstr *MI, MCInst &OutMI) const;

  // Return an MCOperand for MO.
  MCOperand lowerOperand(const MachineOperand& MO) const;

  // Return an MCExpr for symbolic operand MO with variant kind Kind.
  const MCExpr *getExpr(const MachineOperand &MO,
                        MCSymbolRefExpr::VariantKind Kind) const;
};
} // end namespace llvm

#endif
