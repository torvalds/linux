//===- SparcMCAsmInfo.cpp - Sparc asm properties --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the SparcMCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "SparcMCAsmInfo.h"
#include "SparcMCExpr.h"
#include "llvm/ADT/Triple.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCTargetOptions.h"

using namespace llvm;

void SparcELFMCAsmInfo::anchor() {}

SparcELFMCAsmInfo::SparcELFMCAsmInfo(const Triple &TheTriple) {
  bool isV9 = (TheTriple.getArch() == Triple::sparcv9);
  IsLittleEndian = (TheTriple.getArch() == Triple::sparcel);

  if (isV9) {
    CodePointerSize = CalleeSaveStackSlotSize = 8;
  }

  Data16bitsDirective = "\t.half\t";
  Data32bitsDirective = "\t.word\t";
  // .xword is only supported by V9.
  Data64bitsDirective = (isV9) ? "\t.xword\t" : nullptr;
  ZeroDirective = "\t.skip\t";
  CommentString = "!";
  SupportsDebugInformation = true;

  ExceptionsType = ExceptionHandling::DwarfCFI;

  SunStyleELFSectionSwitchSyntax = true;
  UsesELFSectionDirectiveForBSS = true;

  UseIntegratedAssembler = true;
}

const MCExpr*
SparcELFMCAsmInfo::getExprForPersonalitySymbol(const MCSymbol *Sym,
                                               unsigned Encoding,
                                               MCStreamer &Streamer) const {
  if (Encoding & dwarf::DW_EH_PE_pcrel) {
    MCContext &Ctx = Streamer.getContext();
    return SparcMCExpr::create(SparcMCExpr::VK_Sparc_R_DISP32,
                               MCSymbolRefExpr::create(Sym, Ctx), Ctx);
  }

  return MCAsmInfo::getExprForPersonalitySymbol(Sym, Encoding, Streamer);
}

const MCExpr*
SparcELFMCAsmInfo::getExprForFDESymbol(const MCSymbol *Sym,
                                       unsigned Encoding,
                                       MCStreamer &Streamer) const {
  if (Encoding & dwarf::DW_EH_PE_pcrel) {
    MCContext &Ctx = Streamer.getContext();
    return SparcMCExpr::create(SparcMCExpr::VK_Sparc_R_DISP32,
                               MCSymbolRefExpr::create(Sym, Ctx), Ctx);
  }
  return MCAsmInfo::getExprForFDESymbol(Sym, Encoding, Streamer);
}
