//===-- X86IntelInstPrinter.cpp - Intel assembly instruction printing -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file includes code for rendering MCInst instances as Intel-style
// assembly.
//
//===----------------------------------------------------------------------===//

#include "X86IntelInstPrinter.h"
#include "MCTargetDesc/X86BaseInfo.h"
#include "X86InstComments.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

#include "X86GenAsmWriter1.inc"

void X86IntelInstPrinter::printRegName(raw_ostream &OS, unsigned RegNo) const {
  OS << getRegisterName(RegNo);
}

void X86IntelInstPrinter::printInst(const MCInst *MI, raw_ostream &OS,
                                    StringRef Annot,
                                    const MCSubtargetInfo &STI) {
  printInstFlags(MI, OS);

  // In 16-bit mode, print data16 as data32.
  if (MI->getOpcode() == X86::DATA16_PREFIX &&
      STI.getFeatureBits()[X86::Mode16Bit]) {
    OS << "\tdata32";
  } else
    printInstruction(MI, OS);

  // Next always print the annotation.
  printAnnotation(OS, Annot);

  // If verbose assembly is enabled, we can print some informative comments.
  if (CommentStream)
    EmitAnyX86InstComments(MI, *CommentStream, MII);
}

void X86IntelInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                       raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg()) {
    printRegName(O, Op.getReg());
  } else if (Op.isImm()) {
    O << formatImm((int64_t)Op.getImm());
  } else {
    assert(Op.isExpr() && "unknown operand kind in printOperand");
    O << "offset ";
    Op.getExpr()->print(O, &MAI);
  }
}

void X86IntelInstPrinter::printMemReference(const MCInst *MI, unsigned Op,
                                            raw_ostream &O) {
  const MCOperand &BaseReg  = MI->getOperand(Op+X86::AddrBaseReg);
  unsigned ScaleVal         = MI->getOperand(Op+X86::AddrScaleAmt).getImm();
  const MCOperand &IndexReg = MI->getOperand(Op+X86::AddrIndexReg);
  const MCOperand &DispSpec = MI->getOperand(Op+X86::AddrDisp);

  // If this has a segment register, print it.
  printOptionalSegReg(MI, Op + X86::AddrSegmentReg, O);

  O << '[';

  bool NeedPlus = false;
  if (BaseReg.getReg()) {
    printOperand(MI, Op+X86::AddrBaseReg, O);
    NeedPlus = true;
  }

  if (IndexReg.getReg()) {
    if (NeedPlus) O << " + ";
    if (ScaleVal != 1)
      O << ScaleVal << '*';
    printOperand(MI, Op+X86::AddrIndexReg, O);
    NeedPlus = true;
  }

  if (!DispSpec.isImm()) {
    if (NeedPlus) O << " + ";
    assert(DispSpec.isExpr() && "non-immediate displacement for LEA?");
    DispSpec.getExpr()->print(O, &MAI);
  } else {
    int64_t DispVal = DispSpec.getImm();
    if (DispVal || (!IndexReg.getReg() && !BaseReg.getReg())) {
      if (NeedPlus) {
        if (DispVal > 0)
          O << " + ";
        else {
          O << " - ";
          DispVal = -DispVal;
        }
      }
      O << formatImm(DispVal);
    }
  }

  O << ']';
}

void X86IntelInstPrinter::printSrcIdx(const MCInst *MI, unsigned Op,
                                      raw_ostream &O) {
  // If this has a segment register, print it.
  printOptionalSegReg(MI, Op + 1, O);
  O << '[';
  printOperand(MI, Op, O);
  O << ']';
}

void X86IntelInstPrinter::printDstIdx(const MCInst *MI, unsigned Op,
                                      raw_ostream &O) {
  // DI accesses are always ES-based.
  O << "es:[";
  printOperand(MI, Op, O);
  O << ']';
}

void X86IntelInstPrinter::printMemOffset(const MCInst *MI, unsigned Op,
                                         raw_ostream &O) {
  const MCOperand &DispSpec = MI->getOperand(Op);

  // If this has a segment register, print it.
  printOptionalSegReg(MI, Op + 1, O);

  O << '[';

  if (DispSpec.isImm()) {
    O << formatImm(DispSpec.getImm());
  } else {
    assert(DispSpec.isExpr() && "non-immediate displacement?");
    DispSpec.getExpr()->print(O, &MAI);
  }

  O << ']';
}

void X86IntelInstPrinter::printU8Imm(const MCInst *MI, unsigned Op,
                                     raw_ostream &O) {
  if (MI->getOperand(Op).isExpr())
    return MI->getOperand(Op).getExpr()->print(O, &MAI);

  O << formatImm(MI->getOperand(Op).getImm() & 0xff);
}

void X86IntelInstPrinter::printSTiRegOperand(const MCInst *MI, unsigned OpNo,
                                            raw_ostream &OS) {
  const MCOperand &Op = MI->getOperand(OpNo);
  unsigned Reg = Op.getReg();
  // Override the default printing to print st(0) instead st.
  if (Reg == X86::ST0)
    OS << "st(0)";
  else
    printRegName(OS, Reg);
}
