//===- ARCMCInstLower.h - Lower MachineInstr to MCInst ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARC_ARCMCINSTLOWER_H
#define LLVM_LIB_TARGET_ARC_ARCMCINSTLOWER_H

#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class MCContext;
class MCInst;
class MCOperand;
class MachineInstr;
class MachineFunction;
class Mangler;
class AsmPrinter;

/// This class is used to lower an MachineInstr into an MCInst.
class LLVM_LIBRARY_VISIBILITY ARCMCInstLower {
  using MachineOperandType = MachineOperand::MachineOperandType;
  MCContext *Ctx;
  AsmPrinter &Printer;

public:
  ARCMCInstLower(MCContext *C, AsmPrinter &asmprinter);
  void Lower(const MachineInstr *MI, MCInst &OutMI) const;
  MCOperand LowerOperand(const MachineOperand &MO, unsigned offset = 0) const;

private:
  MCOperand LowerSymbolOperand(const MachineOperand &MO,
                               MachineOperandType MOTy, unsigned Offset) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARC_ARCMCINSTLOWER_H
