//===-- llvm/MC/MCInstBuilder.h - Simplify creation of MCInsts --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the MCInstBuilder class for convenient creation of
// MCInsts.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCINSTBUILDER_H
#define LLVM_MC_MCINSTBUILDER_H

#include "llvm/MC/MCInst.h"

namespace llvm {

class MCInstBuilder {
  MCInst Inst;

public:
  /// Create a new MCInstBuilder for an MCInst with a specific opcode.
  MCInstBuilder(unsigned Opcode) {
    Inst.setOpcode(Opcode);
  }

  /// Add a new register operand.
  MCInstBuilder &addReg(unsigned Reg) {
    Inst.addOperand(MCOperand::createReg(Reg));
    return *this;
  }

  /// Add a new integer immediate operand.
  MCInstBuilder &addImm(int64_t Val) {
    Inst.addOperand(MCOperand::createImm(Val));
    return *this;
  }

  /// Add a new floating point immediate operand.
  MCInstBuilder &addFPImm(double Val) {
    Inst.addOperand(MCOperand::createFPImm(Val));
    return *this;
  }

  /// Add a new MCExpr operand.
  MCInstBuilder &addExpr(const MCExpr *Val) {
    Inst.addOperand(MCOperand::createExpr(Val));
    return *this;
  }

  /// Add a new MCInst operand.
  MCInstBuilder &addInst(const MCInst *Val) {
    Inst.addOperand(MCOperand::createInst(Val));
    return *this;
  }

  /// Add an operand.
  MCInstBuilder &addOperand(const MCOperand &Op) {
    Inst.addOperand(Op);
    return *this;
  }

  operator MCInst&() {
    return Inst;
  }
};

} // end namespace llvm

#endif
