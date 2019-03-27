//===- MCAsmInfo.cpp - Asm Info -------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines target asm properties related what form asm statements
// should take.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCAsmInfo.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

enum DefaultOnOff { Default, Enable, Disable };
static cl::opt<DefaultOnOff> DwarfExtendedLoc(
    "dwarf-extended-loc", cl::Hidden,
    cl::desc("Disable emission of the extended flags in .loc directives."),
    cl::values(clEnumVal(Default, "Default for platform"),
               clEnumVal(Enable, "Enabled"), clEnumVal(Disable, "Disabled")),
    cl::init(Default));

MCAsmInfo::MCAsmInfo() {
  SeparatorString = ";";
  CommentString = "#";
  LabelSuffix = ":";
  PrivateGlobalPrefix = "L";
  PrivateLabelPrefix = PrivateGlobalPrefix;
  LinkerPrivateGlobalPrefix = "";
  InlineAsmStart = "APP";
  InlineAsmEnd = "NO_APP";
  Code16Directive = ".code16";
  Code32Directive = ".code32";
  Code64Directive = ".code64";
  ZeroDirective = "\t.zero\t";
  AsciiDirective = "\t.ascii\t";
  AscizDirective = "\t.asciz\t";
  Data8bitsDirective = "\t.byte\t";
  Data16bitsDirective = "\t.short\t";
  Data32bitsDirective = "\t.long\t";
  Data64bitsDirective = "\t.quad\t";
  GlobalDirective = "\t.globl\t";
  WeakDirective = "\t.weak\t";
  if (DwarfExtendedLoc != Default)
    SupportsExtendedDwarfLocDirective = DwarfExtendedLoc == Enable;

  // FIXME: Clang's logic should be synced with the logic used to initialize
  //        this member and the two implementations should be merged.
  // For reference:
  // - Solaris always enables the integrated assembler by default
  //   - SparcELFMCAsmInfo and X86ELFMCAsmInfo are handling this case
  // - Windows always enables the integrated assembler by default
  //   - MCAsmInfoCOFF is handling this case, should it be MCAsmInfoMicrosoft?
  // - MachO targets always enables the integrated assembler by default
  //   - MCAsmInfoDarwin is handling this case
  // - Generic_GCC toolchains enable the integrated assembler on a per
  //   architecture basis.
  //   - The target subclasses for AArch64, ARM, and X86 handle these cases
  UseIntegratedAssembler = false;
  PreserveAsmComments = true;
}

MCAsmInfo::~MCAsmInfo() = default;

bool MCAsmInfo::isSectionAtomizableBySymbols(const MCSection &Section) const {
  return false;
}

const MCExpr *
MCAsmInfo::getExprForPersonalitySymbol(const MCSymbol *Sym,
                                       unsigned Encoding,
                                       MCStreamer &Streamer) const {
  return getExprForFDESymbol(Sym, Encoding, Streamer);
}

const MCExpr *
MCAsmInfo::getExprForFDESymbol(const MCSymbol *Sym,
                               unsigned Encoding,
                               MCStreamer &Streamer) const {
  if (!(Encoding & dwarf::DW_EH_PE_pcrel))
    return MCSymbolRefExpr::create(Sym, Streamer.getContext());

  MCContext &Context = Streamer.getContext();
  const MCExpr *Res = MCSymbolRefExpr::create(Sym, Context);
  MCSymbol *PCSym = Context.createTempSymbol();
  Streamer.EmitLabel(PCSym);
  const MCExpr *PC = MCSymbolRefExpr::create(PCSym, Context);
  return MCBinaryExpr::createSub(Res, PC, Context);
}

static bool isAcceptableChar(char C) {
  return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
         (C >= '0' && C <= '9') || C == '_' || C == '$' || C == '.' || C == '@';
}

bool MCAsmInfo::isValidUnquotedName(StringRef Name) const {
  if (Name.empty())
    return false;

  // If any of the characters in the string is an unacceptable character, force
  // quotes.
  for (char C : Name) {
    if (!isAcceptableChar(C))
      return false;
  }

  return true;
}

bool MCAsmInfo::shouldOmitSectionDirective(StringRef SectionName) const {
  // FIXME: Does .section .bss/.data/.text work everywhere??
  return SectionName == ".text" || SectionName == ".data" ||
        (SectionName == ".bss" && !usesELFSectionDirectiveForBSS());
}
