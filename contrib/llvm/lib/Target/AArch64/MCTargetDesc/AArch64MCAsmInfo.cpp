//===-- AArch64MCAsmInfo.cpp - AArch64 asm properties ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the AArch64MCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "AArch64MCAsmInfo.h"
#include "llvm/ADT/Triple.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/CommandLine.h"
using namespace llvm;

enum AsmWriterVariantTy {
  Default = -1,
  Generic = 0,
  Apple = 1
};

static cl::opt<AsmWriterVariantTy> AsmWriterVariant(
    "aarch64-neon-syntax", cl::init(Default),
    cl::desc("Choose style of NEON code to emit from AArch64 backend:"),
    cl::values(clEnumValN(Generic, "generic", "Emit generic NEON assembly"),
               clEnumValN(Apple, "apple", "Emit Apple-style NEON assembly")));

AArch64MCAsmInfoDarwin::AArch64MCAsmInfoDarwin() {
  // We prefer NEON instructions to be printed in the short, Apple-specific
  // form when targeting Darwin.
  AssemblerDialect = AsmWriterVariant == Default ? Apple : AsmWriterVariant;

  PrivateGlobalPrefix = "L";
  PrivateLabelPrefix = "L";
  SeparatorString = "%%";
  CommentString = ";";
  CodePointerSize = CalleeSaveStackSlotSize = 8;

  AlignmentIsInBytes = false;
  UsesELFSectionDirectiveForBSS = true;
  SupportsDebugInformation = true;
  UseDataRegionDirectives = true;

  ExceptionsType = ExceptionHandling::DwarfCFI;
}

const MCExpr *AArch64MCAsmInfoDarwin::getExprForPersonalitySymbol(
    const MCSymbol *Sym, unsigned Encoding, MCStreamer &Streamer) const {
  // On Darwin, we can reference dwarf symbols with foo@GOT-., which
  // is an indirect pc-relative reference. The default implementation
  // won't reference using the GOT, so we need this target-specific
  // version.
  MCContext &Context = Streamer.getContext();
  const MCExpr *Res =
      MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_GOT, Context);
  MCSymbol *PCSym = Context.createTempSymbol();
  Streamer.EmitLabel(PCSym);
  const MCExpr *PC = MCSymbolRefExpr::create(PCSym, Context);
  return MCBinaryExpr::createSub(Res, PC, Context);
}

AArch64MCAsmInfoELF::AArch64MCAsmInfoELF(const Triple &T) {
  if (T.getArch() == Triple::aarch64_be)
    IsLittleEndian = false;

  // We prefer NEON instructions to be printed in the generic form when
  // targeting ELF.
  AssemblerDialect = AsmWriterVariant == Default ? Generic : AsmWriterVariant;

  CodePointerSize = 8;

  // ".comm align is in bytes but .align is pow-2."
  AlignmentIsInBytes = false;

  CommentString = "//";
  PrivateGlobalPrefix = ".L";
  PrivateLabelPrefix = ".L";
  Code32Directive = ".code\t32";

  Data16bitsDirective = "\t.hword\t";
  Data32bitsDirective = "\t.word\t";
  Data64bitsDirective = "\t.xword\t";

  UseDataRegionDirectives = false;

  WeakRefDirective = "\t.weak\t";

  SupportsDebugInformation = true;

  // Exceptions handling
  ExceptionsType = ExceptionHandling::DwarfCFI;

  UseIntegratedAssembler = true;

  HasIdentDirective = true;
}

AArch64MCAsmInfoMicrosoftCOFF::AArch64MCAsmInfoMicrosoftCOFF() {
  PrivateGlobalPrefix = ".L";
  PrivateLabelPrefix = ".L";

  Data16bitsDirective = "\t.hword\t";
  Data32bitsDirective = "\t.word\t";
  Data64bitsDirective = "\t.xword\t";

  AlignmentIsInBytes = false;
  SupportsDebugInformation = true;
  CodePointerSize = 8;

  CommentString = ";";
  ExceptionsType = ExceptionHandling::WinEH;
  WinEHEncodingType = WinEH::EncodingType::Itanium;
}

AArch64MCAsmInfoGNUCOFF::AArch64MCAsmInfoGNUCOFF() {
  PrivateGlobalPrefix = ".L";
  PrivateLabelPrefix = ".L";

  Data16bitsDirective = "\t.hword\t";
  Data32bitsDirective = "\t.word\t";
  Data64bitsDirective = "\t.xword\t";

  AlignmentIsInBytes = false;
  SupportsDebugInformation = true;
  CodePointerSize = 8;

  CommentString = "//";
  ExceptionsType = ExceptionHandling::DwarfCFI;
  // The default is dwarf, but WinEH can be enabled optionally, which requires
  // WinEHEncodingType to be set.
  WinEHEncodingType = WinEH::EncodingType::Itanium;
}
