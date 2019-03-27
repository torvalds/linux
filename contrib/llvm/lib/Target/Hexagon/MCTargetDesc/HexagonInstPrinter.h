//===-- HexagonInstPrinter.h - Convert Hexagon MCInst to assembly syntax --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_INSTPRINTER_HEXAGONINSTPRINTER_H
#define LLVM_LIB_TARGET_HEXAGON_INSTPRINTER_HEXAGONINSTPRINTER_H

#include "llvm/MC/MCInstPrinter.h"

namespace llvm {
/// Prints bundles as a newline separated list of individual instructions
/// Duplexes are separated by a vertical tab \v character
/// A trailing line includes bundle properties such as endloop0/1
///
/// r0 = add(r1, r2)
/// r0 = #0 \v jump 0x0
/// :endloop0 :endloop1
class HexagonInstPrinter : public MCInstPrinter {
public:
  explicit HexagonInstPrinter(MCAsmInfo const &MAI, MCInstrInfo const &MII,
                              MCRegisterInfo const &MRI)
    : MCInstPrinter(MAI, MII, MRI), MII(MII) {}

  void printInst(MCInst const *MI, raw_ostream &O, StringRef Annot,
                 const MCSubtargetInfo &STI) override;
  void printRegName(raw_ostream &O, unsigned RegNo) const override;

  static char const *getRegisterName(unsigned RegNo);

  void printInstruction(MCInst const *MI, raw_ostream &O);
  void printOperand(MCInst const *MI, unsigned OpNo, raw_ostream &O) const;
  void printBrtarget(MCInst const *MI, unsigned OpNo, raw_ostream &O) const;

  MCAsmInfo const &getMAI() const { return MAI; }
  MCInstrInfo const &getMII() const { return MII; }

private:
  MCInstrInfo const &MII;
  bool HasExtender = false;
};

} // end namespace llvm

#endif
