//===- SystemZInstPrinter.cpp - Convert SystemZ MCInst to assembly syntax -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SystemZInstPrinter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

#include "SystemZGenAsmWriter.inc"

void SystemZInstPrinter::printAddress(unsigned Base, int64_t Disp,
                                      unsigned Index, raw_ostream &O) {
  O << Disp;
  if (Base || Index) {
    O << '(';
    if (Index) {
      O << '%' << getRegisterName(Index);
      if (Base)
        O << ',';
    }
    if (Base)
      O << '%' << getRegisterName(Base);
    O << ')';
  }
}

void SystemZInstPrinter::printOperand(const MCOperand &MO, const MCAsmInfo *MAI,
                                      raw_ostream &O) {
  if (MO.isReg())
    O << '%' << getRegisterName(MO.getReg());
  else if (MO.isImm())
    O << MO.getImm();
  else if (MO.isExpr())
    MO.getExpr()->print(O, MAI);
  else
    llvm_unreachable("Invalid operand");
}

void SystemZInstPrinter::printInst(const MCInst *MI, raw_ostream &O,
                                   StringRef Annot,
                                   const MCSubtargetInfo &STI) {
  printInstruction(MI, O);
  printAnnotation(O, Annot);
}

void SystemZInstPrinter::printRegName(raw_ostream &O, unsigned RegNo) const {
  O << '%' << getRegisterName(RegNo);
}

template <unsigned N>
static void printUImmOperand(const MCInst *MI, int OpNum, raw_ostream &O) {
  int64_t Value = MI->getOperand(OpNum).getImm();
  assert(isUInt<N>(Value) && "Invalid uimm argument");
  O << Value;
}

template <unsigned N>
static void printSImmOperand(const MCInst *MI, int OpNum, raw_ostream &O) {
  int64_t Value = MI->getOperand(OpNum).getImm();
  assert(isInt<N>(Value) && "Invalid simm argument");
  O << Value;
}

void SystemZInstPrinter::printU1ImmOperand(const MCInst *MI, int OpNum,
                                           raw_ostream &O) {
  printUImmOperand<1>(MI, OpNum, O);
}

void SystemZInstPrinter::printU2ImmOperand(const MCInst *MI, int OpNum,
                                           raw_ostream &O) {
  printUImmOperand<2>(MI, OpNum, O);
}

void SystemZInstPrinter::printU3ImmOperand(const MCInst *MI, int OpNum,
                                           raw_ostream &O) {
  printUImmOperand<3>(MI, OpNum, O);
}

void SystemZInstPrinter::printU4ImmOperand(const MCInst *MI, int OpNum,
                                           raw_ostream &O) {
  printUImmOperand<4>(MI, OpNum, O);
}

void SystemZInstPrinter::printU6ImmOperand(const MCInst *MI, int OpNum,
                                           raw_ostream &O) {
  printUImmOperand<6>(MI, OpNum, O);
}

void SystemZInstPrinter::printS8ImmOperand(const MCInst *MI, int OpNum,
                                           raw_ostream &O) {
  printSImmOperand<8>(MI, OpNum, O);
}

void SystemZInstPrinter::printU8ImmOperand(const MCInst *MI, int OpNum,
                                           raw_ostream &O) {
  printUImmOperand<8>(MI, OpNum, O);
}

void SystemZInstPrinter::printU12ImmOperand(const MCInst *MI, int OpNum,
                                            raw_ostream &O) {
  printUImmOperand<12>(MI, OpNum, O);
}

void SystemZInstPrinter::printS16ImmOperand(const MCInst *MI, int OpNum,
                                            raw_ostream &O) {
  printSImmOperand<16>(MI, OpNum, O);
}

void SystemZInstPrinter::printU16ImmOperand(const MCInst *MI, int OpNum,
                                            raw_ostream &O) {
  printUImmOperand<16>(MI, OpNum, O);
}

void SystemZInstPrinter::printS32ImmOperand(const MCInst *MI, int OpNum,
                                            raw_ostream &O) {
  printSImmOperand<32>(MI, OpNum, O);
}

void SystemZInstPrinter::printU32ImmOperand(const MCInst *MI, int OpNum,
                                            raw_ostream &O) {
  printUImmOperand<32>(MI, OpNum, O);
}

void SystemZInstPrinter::printU48ImmOperand(const MCInst *MI, int OpNum,
                                            raw_ostream &O) {
  printUImmOperand<48>(MI, OpNum, O);
}

void SystemZInstPrinter::printPCRelOperand(const MCInst *MI, int OpNum,
                                           raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNum);
  if (MO.isImm()) {
    O << "0x";
    O.write_hex(MO.getImm());
  } else
    MO.getExpr()->print(O, &MAI);
}

void SystemZInstPrinter::printPCRelTLSOperand(const MCInst *MI, int OpNum,
                                              raw_ostream &O) {
  // Output the PC-relative operand.
  printPCRelOperand(MI, OpNum, O);

  // Output the TLS marker if present.
  if ((unsigned)OpNum + 1 < MI->getNumOperands()) {
    const MCOperand &MO = MI->getOperand(OpNum + 1);
    const MCSymbolRefExpr &refExp = cast<MCSymbolRefExpr>(*MO.getExpr());
    switch (refExp.getKind()) {
      case MCSymbolRefExpr::VK_TLSGD:
        O << ":tls_gdcall:";
        break;
      case MCSymbolRefExpr::VK_TLSLDM:
        O << ":tls_ldcall:";
        break;
      default:
        llvm_unreachable("Unexpected symbol kind");
    }
    O << refExp.getSymbol().getName();
  }
}

void SystemZInstPrinter::printOperand(const MCInst *MI, int OpNum,
                                      raw_ostream &O) {
  printOperand(MI->getOperand(OpNum), &MAI, O);
}

void SystemZInstPrinter::printBDAddrOperand(const MCInst *MI, int OpNum,
                                            raw_ostream &O) {
  printAddress(MI->getOperand(OpNum).getReg(),
               MI->getOperand(OpNum + 1).getImm(), 0, O);
}

void SystemZInstPrinter::printBDXAddrOperand(const MCInst *MI, int OpNum,
                                             raw_ostream &O) {
  printAddress(MI->getOperand(OpNum).getReg(),
               MI->getOperand(OpNum + 1).getImm(),
               MI->getOperand(OpNum + 2).getReg(), O);
}

void SystemZInstPrinter::printBDLAddrOperand(const MCInst *MI, int OpNum,
                                             raw_ostream &O) {
  unsigned Base = MI->getOperand(OpNum).getReg();
  uint64_t Disp = MI->getOperand(OpNum + 1).getImm();
  uint64_t Length = MI->getOperand(OpNum + 2).getImm();
  O << Disp << '(' << Length;
  if (Base)
    O << ",%" << getRegisterName(Base);
  O << ')';
}

void SystemZInstPrinter::printBDRAddrOperand(const MCInst *MI, int OpNum,
                                             raw_ostream &O) {
  unsigned Base = MI->getOperand(OpNum).getReg();
  uint64_t Disp = MI->getOperand(OpNum + 1).getImm();
  unsigned Length = MI->getOperand(OpNum + 2).getReg();
  O << Disp << "(%" << getRegisterName(Length);
  if (Base)
    O << ",%" << getRegisterName(Base);
  O << ')';
}

void SystemZInstPrinter::printBDVAddrOperand(const MCInst *MI, int OpNum,
                                             raw_ostream &O) {
  printAddress(MI->getOperand(OpNum).getReg(),
               MI->getOperand(OpNum + 1).getImm(),
               MI->getOperand(OpNum + 2).getReg(), O);
}

void SystemZInstPrinter::printCond4Operand(const MCInst *MI, int OpNum,
                                           raw_ostream &O) {
  static const char *const CondNames[] = {
    "o", "h", "nle", "l", "nhe", "lh", "ne",
    "e", "nlh", "he", "nl", "le", "nh", "no"
  };
  uint64_t Imm = MI->getOperand(OpNum).getImm();
  assert(Imm > 0 && Imm < 15 && "Invalid condition");
  O << CondNames[Imm - 1];
}
