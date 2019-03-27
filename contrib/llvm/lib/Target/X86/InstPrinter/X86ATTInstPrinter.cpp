//===-- X86ATTInstPrinter.cpp - AT&T assembly instruction printing --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file includes code for rendering MCInst instances as AT&T-style
// assembly.
//
//===----------------------------------------------------------------------===//

#include "X86ATTInstPrinter.h"
#include "MCTargetDesc/X86BaseInfo.h"
#include "X86InstComments.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cinttypes>
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

// Include the auto-generated portion of the assembly writer.
#define PRINT_ALIAS_INSTR
#include "X86GenAsmWriter.inc"

void X86ATTInstPrinter::printRegName(raw_ostream &OS, unsigned RegNo) const {
  OS << markup("<reg:") << '%' << getRegisterName(RegNo) << markup(">");
}

void X86ATTInstPrinter::printInst(const MCInst *MI, raw_ostream &OS,
                                  StringRef Annot, const MCSubtargetInfo &STI) {
  // If verbose assembly is enabled, we can print some informative comments.
  if (CommentStream)
    HasCustomInstComment = EmitAnyX86InstComments(MI, *CommentStream, MII);

  printInstFlags(MI, OS);

  // Output CALLpcrel32 as "callq" in 64-bit mode.
  // In Intel annotation it's always emitted as "call".
  //
  // TODO: Probably this hack should be redesigned via InstAlias in
  // InstrInfo.td as soon as Requires clause is supported properly
  // for InstAlias.
  if (MI->getOpcode() == X86::CALLpcrel32 &&
      (STI.getFeatureBits()[X86::Mode64Bit])) {
    OS << "\tcallq\t";
    printPCRelImm(MI, 0, OS);
  }
  // data16 and data32 both have the same encoding of 0x66. While data32 is
  // valid only in 16 bit systems, data16 is valid in the rest.
  // There seems to be some lack of support of the Requires clause that causes
  // 0x66 to be interpreted as "data16" by the asm printer.
  // Thus we add an adjustment here in order to print the "right" instruction.
  else if (MI->getOpcode() == X86::DATA16_PREFIX &&
           STI.getFeatureBits()[X86::Mode16Bit]) {
   OS << "\tdata32";
  }
  // Try to print any aliases first.
  else if (!printAliasInstr(MI, OS))
    printInstruction(MI, OS);

  // Next always print the annotation.
  printAnnotation(OS, Annot);
}

void X86ATTInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                     raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg()) {
    printRegName(O, Op.getReg());
  } else if (Op.isImm()) {
    // Print immediates as signed values.
    int64_t Imm = Op.getImm();
    O << markup("<imm:") << '$' << formatImm(Imm) << markup(">");

    // TODO: This should be in a helper function in the base class, so it can
    // be used by other printers.

    // If there are no instruction-specific comments, add a comment clarifying
    // the hex value of the immediate operand when it isn't in the range
    // [-256,255].
    if (CommentStream && !HasCustomInstComment && (Imm > 255 || Imm < -256)) {
      // Don't print unnecessary hex sign bits.
      if (Imm == (int16_t)(Imm))
        *CommentStream << format("imm = 0x%" PRIX16 "\n", (uint16_t)Imm);
      else if (Imm == (int32_t)(Imm))
        *CommentStream << format("imm = 0x%" PRIX32 "\n", (uint32_t)Imm);
      else
        *CommentStream << format("imm = 0x%" PRIX64 "\n", (uint64_t)Imm);
    }
  } else {
    assert(Op.isExpr() && "unknown operand kind in printOperand");
    O << markup("<imm:") << '$';
    Op.getExpr()->print(O, &MAI);
    O << markup(">");
  }
}

void X86ATTInstPrinter::printMemReference(const MCInst *MI, unsigned Op,
                                          raw_ostream &O) {
  const MCOperand &BaseReg = MI->getOperand(Op + X86::AddrBaseReg);
  const MCOperand &IndexReg = MI->getOperand(Op + X86::AddrIndexReg);
  const MCOperand &DispSpec = MI->getOperand(Op + X86::AddrDisp);

  O << markup("<mem:");

  // If this has a segment register, print it.
  printOptionalSegReg(MI, Op + X86::AddrSegmentReg, O);

  if (DispSpec.isImm()) {
    int64_t DispVal = DispSpec.getImm();
    if (DispVal || (!IndexReg.getReg() && !BaseReg.getReg()))
      O << formatImm(DispVal);
  } else {
    assert(DispSpec.isExpr() && "non-immediate displacement for LEA?");
    DispSpec.getExpr()->print(O, &MAI);
  }

  if (IndexReg.getReg() || BaseReg.getReg()) {
    O << '(';
    if (BaseReg.getReg())
      printOperand(MI, Op + X86::AddrBaseReg, O);

    if (IndexReg.getReg()) {
      O << ',';
      printOperand(MI, Op + X86::AddrIndexReg, O);
      unsigned ScaleVal = MI->getOperand(Op + X86::AddrScaleAmt).getImm();
      if (ScaleVal != 1) {
        O << ',' << markup("<imm:") << ScaleVal // never printed in hex.
          << markup(">");
      }
    }
    O << ')';
  }

  O << markup(">");
}

void X86ATTInstPrinter::printSrcIdx(const MCInst *MI, unsigned Op,
                                    raw_ostream &O) {
  O << markup("<mem:");

  // If this has a segment register, print it.
  printOptionalSegReg(MI, Op + 1, O);

  O << "(";
  printOperand(MI, Op, O);
  O << ")";

  O << markup(">");
}

void X86ATTInstPrinter::printDstIdx(const MCInst *MI, unsigned Op,
                                    raw_ostream &O) {
  O << markup("<mem:");

  O << "%es:(";
  printOperand(MI, Op, O);
  O << ")";

  O << markup(">");
}

void X86ATTInstPrinter::printMemOffset(const MCInst *MI, unsigned Op,
                                       raw_ostream &O) {
  const MCOperand &DispSpec = MI->getOperand(Op);

  O << markup("<mem:");

  // If this has a segment register, print it.
  printOptionalSegReg(MI, Op + 1, O);

  if (DispSpec.isImm()) {
    O << formatImm(DispSpec.getImm());
  } else {
    assert(DispSpec.isExpr() && "non-immediate displacement?");
    DispSpec.getExpr()->print(O, &MAI);
  }

  O << markup(">");
}

void X86ATTInstPrinter::printU8Imm(const MCInst *MI, unsigned Op,
                                   raw_ostream &O) {
  if (MI->getOperand(Op).isExpr())
    return printOperand(MI, Op, O);

  O << markup("<imm:") << '$' << formatImm(MI->getOperand(Op).getImm() & 0xff)
    << markup(">");
}

void X86ATTInstPrinter::printSTiRegOperand(const MCInst *MI, unsigned OpNo,
                                           raw_ostream &OS) {
  const MCOperand &Op = MI->getOperand(OpNo);
  unsigned Reg = Op.getReg();
  // Override the default printing to print st(0) instead st.
  if (Reg == X86::ST0)
    OS << markup("<reg:") << "%st(0)" << markup(">");
  else
    printRegName(OS, Reg);
}
