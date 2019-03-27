//===-- MCTargetAsmParser.cpp - Target Assembly Parser --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCContext.h"

using namespace llvm;

MCTargetAsmParser::MCTargetAsmParser(MCTargetOptions const &MCOptions,
                                     const MCSubtargetInfo &STI,
                                     const MCInstrInfo &MII)
    : MCOptions(MCOptions), STI(&STI), MII(MII) {}

MCTargetAsmParser::~MCTargetAsmParser() = default;

MCSubtargetInfo &MCTargetAsmParser::copySTI() {
  MCSubtargetInfo &STICopy = getContext().getSubtargetCopy(getSTI());
  STI = &STICopy;
  return STICopy;
}

const MCSubtargetInfo &MCTargetAsmParser::getSTI() const {
  return *STI;
}
