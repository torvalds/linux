//===-- X86InstPrinterCommon.cpp - X86 assembly instruction printing ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file includes code common for rendering MCInst instances as AT&T-style
// and Intel-style assembly.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_INSTPRINTER_X86INSTPRINTERCOMMON_H
#define LLVM_LIB_TARGET_X86_INSTPRINTER_X86INSTPRINTERCOMMON_H

#include "llvm/MC/MCInstPrinter.h"

namespace llvm {

class X86InstPrinterCommon : public MCInstPrinter {
public:
  using MCInstPrinter::MCInstPrinter;

  virtual void printOperand(const MCInst *MI, unsigned OpNo, raw_ostream &O) = 0;
  void printSSEAVXCC(const MCInst *MI, unsigned Op, raw_ostream &OS);
  void printXOPCC(const MCInst *MI, unsigned Op, raw_ostream &OS);
  void printRoundingControl(const MCInst *MI, unsigned Op, raw_ostream &O);
  void printPCRelImm(const MCInst *MI, unsigned OpNo, raw_ostream &O);
protected:
  void printInstFlags(const MCInst *MI, raw_ostream &O);
  void printOptionalSegReg(const MCInst *MI, unsigned OpNo, raw_ostream &O);
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_X86_INSTPRINTER_X86ATTINSTPRINTER_H
