//===- COFFAsmParser.cpp - COFF Assembly Parser ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParserExtension.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSectionCOFF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Support/SMLoc.h"
#include <cassert>
#include <cstdint>
#include <limits>
#include <utility>

using namespace llvm;

namespace {

class COFFAsmParser : public MCAsmParserExtension {
  template<bool (COFFAsmParser::*HandlerMethod)(StringRef, SMLoc)>
  void addDirectiveHandler(StringRef Directive) {
    MCAsmParser::ExtensionDirectiveHandler Handler = std::make_pair(
        this, HandleDirective<COFFAsmParser, HandlerMethod>);
    getParser().addDirectiveHandler(Directive, Handler);
  }

  bool ParseSectionSwitch(StringRef Section,
                          unsigned Characteristics,
                          SectionKind Kind);

  bool ParseSectionSwitch(StringRef Section, unsigned Characteristics,
                          SectionKind Kind, StringRef COMDATSymName,
                          COFF::COMDATType Type);

  bool ParseSectionName(StringRef &SectionName);
  bool ParseSectionFlags(StringRef SectionName, StringRef FlagsString,
                         unsigned *Flags);

  void Initialize(MCAsmParser &Parser) override {
    // Call the base implementation.
    MCAsmParserExtension::Initialize(Parser);

    addDirectiveHandler<&COFFAsmParser::ParseSectionDirectiveText>(".text");
    addDirectiveHandler<&COFFAsmParser::ParseSectionDirectiveData>(".data");
    addDirectiveHandler<&COFFAsmParser::ParseSectionDirectiveBSS>(".bss");
    addDirectiveHandler<&COFFAsmParser::ParseDirectiveSection>(".section");
    addDirectiveHandler<&COFFAsmParser::ParseDirectiveDef>(".def");
    addDirectiveHandler<&COFFAsmParser::ParseDirectiveScl>(".scl");
    addDirectiveHandler<&COFFAsmParser::ParseDirectiveType>(".type");
    addDirectiveHandler<&COFFAsmParser::ParseDirectiveEndef>(".endef");
    addDirectiveHandler<&COFFAsmParser::ParseDirectiveSecRel32>(".secrel32");
    addDirectiveHandler<&COFFAsmParser::ParseDirectiveSymIdx>(".symidx");
    addDirectiveHandler<&COFFAsmParser::ParseDirectiveSafeSEH>(".safeseh");
    addDirectiveHandler<&COFFAsmParser::ParseDirectiveSecIdx>(".secidx");
    addDirectiveHandler<&COFFAsmParser::ParseDirectiveLinkOnce>(".linkonce");
    addDirectiveHandler<&COFFAsmParser::ParseDirectiveRVA>(".rva");

    // Win64 EH directives.
    addDirectiveHandler<&COFFAsmParser::ParseSEHDirectiveStartProc>(
                                                                   ".seh_proc");
    addDirectiveHandler<&COFFAsmParser::ParseSEHDirectiveEndProc>(
                                                                ".seh_endproc");
    addDirectiveHandler<&COFFAsmParser::ParseSEHDirectiveStartChained>(
                                                           ".seh_startchained");
    addDirectiveHandler<&COFFAsmParser::ParseSEHDirectiveEndChained>(
                                                             ".seh_endchained");
    addDirectiveHandler<&COFFAsmParser::ParseSEHDirectiveHandler>(
                                                                ".seh_handler");
    addDirectiveHandler<&COFFAsmParser::ParseSEHDirectiveHandlerData>(
                                                            ".seh_handlerdata");
    addDirectiveHandler<&COFFAsmParser::ParseSEHDirectivePushReg>(
                                                                ".seh_pushreg");
    addDirectiveHandler<&COFFAsmParser::ParseSEHDirectiveSetFrame>(
                                                               ".seh_setframe");
    addDirectiveHandler<&COFFAsmParser::ParseSEHDirectiveAllocStack>(
                                                             ".seh_stackalloc");
    addDirectiveHandler<&COFFAsmParser::ParseSEHDirectiveSaveReg>(
                                                                ".seh_savereg");
    addDirectiveHandler<&COFFAsmParser::ParseSEHDirectiveSaveXMM>(
                                                                ".seh_savexmm");
    addDirectiveHandler<&COFFAsmParser::ParseSEHDirectivePushFrame>(
                                                              ".seh_pushframe");
    addDirectiveHandler<&COFFAsmParser::ParseSEHDirectiveEndProlog>(
                                                            ".seh_endprologue");
    addDirectiveHandler<&COFFAsmParser::ParseDirectiveSymbolAttribute>(".weak");
  }

  bool ParseSectionDirectiveText(StringRef, SMLoc) {
    return ParseSectionSwitch(".text",
                              COFF::IMAGE_SCN_CNT_CODE
                            | COFF::IMAGE_SCN_MEM_EXECUTE
                            | COFF::IMAGE_SCN_MEM_READ,
                              SectionKind::getText());
  }

  bool ParseSectionDirectiveData(StringRef, SMLoc) {
    return ParseSectionSwitch(".data", COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                           COFF::IMAGE_SCN_MEM_READ |
                                           COFF::IMAGE_SCN_MEM_WRITE,
                              SectionKind::getData());
  }

  bool ParseSectionDirectiveBSS(StringRef, SMLoc) {
    return ParseSectionSwitch(".bss",
                              COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA
                            | COFF::IMAGE_SCN_MEM_READ
                            | COFF::IMAGE_SCN_MEM_WRITE,
                              SectionKind::getBSS());
  }

  bool ParseDirectiveSection(StringRef, SMLoc);
  bool ParseDirectiveDef(StringRef, SMLoc);
  bool ParseDirectiveScl(StringRef, SMLoc);
  bool ParseDirectiveType(StringRef, SMLoc);
  bool ParseDirectiveEndef(StringRef, SMLoc);
  bool ParseDirectiveSecRel32(StringRef, SMLoc);
  bool ParseDirectiveSecIdx(StringRef, SMLoc);
  bool ParseDirectiveSafeSEH(StringRef, SMLoc);
  bool ParseDirectiveSymIdx(StringRef, SMLoc);
  bool parseCOMDATType(COFF::COMDATType &Type);
  bool ParseDirectiveLinkOnce(StringRef, SMLoc);
  bool ParseDirectiveRVA(StringRef, SMLoc);

  // Win64 EH directives.
  bool ParseSEHDirectiveStartProc(StringRef, SMLoc);
  bool ParseSEHDirectiveEndProc(StringRef, SMLoc);
  bool ParseSEHDirectiveStartChained(StringRef, SMLoc);
  bool ParseSEHDirectiveEndChained(StringRef, SMLoc);
  bool ParseSEHDirectiveHandler(StringRef, SMLoc);
  bool ParseSEHDirectiveHandlerData(StringRef, SMLoc);
  bool ParseSEHDirectivePushReg(StringRef, SMLoc);
  bool ParseSEHDirectiveSetFrame(StringRef, SMLoc);
  bool ParseSEHDirectiveAllocStack(StringRef, SMLoc);
  bool ParseSEHDirectiveSaveReg(StringRef, SMLoc);
  bool ParseSEHDirectiveSaveXMM(StringRef, SMLoc);
  bool ParseSEHDirectivePushFrame(StringRef, SMLoc);
  bool ParseSEHDirectiveEndProlog(StringRef, SMLoc);

  bool ParseAtUnwindOrAtExcept(bool &unwind, bool &except);
  bool ParseSEHRegisterNumber(unsigned &RegNo);
  bool ParseDirectiveSymbolAttribute(StringRef Directive, SMLoc);

public:
  COFFAsmParser() = default;
};

} // end annonomous namespace.

static SectionKind computeSectionKind(unsigned Flags) {
  if (Flags & COFF::IMAGE_SCN_MEM_EXECUTE)
    return SectionKind::getText();
  if (Flags & COFF::IMAGE_SCN_MEM_READ &&
      (Flags & COFF::IMAGE_SCN_MEM_WRITE) == 0)
    return SectionKind::getReadOnly();
  return SectionKind::getData();
}

bool COFFAsmParser::ParseSectionFlags(StringRef SectionName,
                                      StringRef FlagsString, unsigned *Flags) {
  enum {
    None        = 0,
    Alloc       = 1 << 0,
    Code        = 1 << 1,
    Load        = 1 << 2,
    InitData    = 1 << 3,
    Shared      = 1 << 4,
    NoLoad      = 1 << 5,
    NoRead      = 1 << 6,
    NoWrite     = 1 << 7,
    Discardable = 1 << 8,
  };

  bool ReadOnlyRemoved = false;
  unsigned SecFlags = None;

  for (char FlagChar : FlagsString) {
    switch (FlagChar) {
    case 'a':
      // Ignored.
      break;

    case 'b': // bss section
      SecFlags |= Alloc;
      if (SecFlags & InitData)
        return TokError("conflicting section flags 'b' and 'd'.");
      SecFlags &= ~Load;
      break;

    case 'd': // data section
      SecFlags |= InitData;
      if (SecFlags & Alloc)
        return TokError("conflicting section flags 'b' and 'd'.");
      SecFlags &= ~NoWrite;
      if ((SecFlags & NoLoad) == 0)
        SecFlags |= Load;
      break;

    case 'n': // section is not loaded
      SecFlags |= NoLoad;
      SecFlags &= ~Load;
      break;

    case 'D': // discardable
      SecFlags |= Discardable;
      break;

    case 'r': // read-only
      ReadOnlyRemoved = false;
      SecFlags |= NoWrite;
      if ((SecFlags & Code) == 0)
        SecFlags |= InitData;
      if ((SecFlags & NoLoad) == 0)
        SecFlags |= Load;
      break;

    case 's': // shared section
      SecFlags |= Shared | InitData;
      SecFlags &= ~NoWrite;
      if ((SecFlags & NoLoad) == 0)
        SecFlags |= Load;
      break;

    case 'w': // writable
      SecFlags &= ~NoWrite;
      ReadOnlyRemoved = true;
      break;

    case 'x': // executable section
      SecFlags |= Code;
      if ((SecFlags & NoLoad) == 0)
        SecFlags |= Load;
      if (!ReadOnlyRemoved)
        SecFlags |= NoWrite;
      break;

    case 'y': // not readable
      SecFlags |= NoRead | NoWrite;
      break;

    default:
      return TokError("unknown flag");
    }
  }

  *Flags = 0;

  if (SecFlags == None)
    SecFlags = InitData;

  if (SecFlags & Code)
    *Flags |= COFF::IMAGE_SCN_CNT_CODE | COFF::IMAGE_SCN_MEM_EXECUTE;
  if (SecFlags & InitData)
    *Flags |= COFF::IMAGE_SCN_CNT_INITIALIZED_DATA;
  if ((SecFlags & Alloc) && (SecFlags & Load) == 0)
    *Flags |= COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA;
  if (SecFlags & NoLoad)
    *Flags |= COFF::IMAGE_SCN_LNK_REMOVE;
  if ((SecFlags & Discardable) ||
      MCSectionCOFF::isImplicitlyDiscardable(SectionName))
    *Flags |= COFF::IMAGE_SCN_MEM_DISCARDABLE;
  if ((SecFlags & NoRead) == 0)
    *Flags |= COFF::IMAGE_SCN_MEM_READ;
  if ((SecFlags & NoWrite) == 0)
    *Flags |= COFF::IMAGE_SCN_MEM_WRITE;
  if (SecFlags & Shared)
    *Flags |= COFF::IMAGE_SCN_MEM_SHARED;

  return false;
}

/// ParseDirectiveSymbolAttribute
///  ::= { ".weak", ... } [ identifier ( , identifier )* ]
bool COFFAsmParser::ParseDirectiveSymbolAttribute(StringRef Directive, SMLoc) {
  MCSymbolAttr Attr = StringSwitch<MCSymbolAttr>(Directive)
    .Case(".weak", MCSA_Weak)
    .Default(MCSA_Invalid);
  assert(Attr != MCSA_Invalid && "unexpected symbol attribute directive!");
  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    while (true) {
      StringRef Name;

      if (getParser().parseIdentifier(Name))
        return TokError("expected identifier in directive");

      MCSymbol *Sym = getContext().getOrCreateSymbol(Name);

      getStreamer().EmitSymbolAttribute(Sym, Attr);

      if (getLexer().is(AsmToken::EndOfStatement))
        break;

      if (getLexer().isNot(AsmToken::Comma))
        return TokError("unexpected token in directive");
      Lex();
    }
  }

  Lex();
  return false;
}

bool COFFAsmParser::ParseSectionSwitch(StringRef Section,
                                       unsigned Characteristics,
                                       SectionKind Kind) {
  return ParseSectionSwitch(Section, Characteristics, Kind, "", (COFF::COMDATType)0);
}

bool COFFAsmParser::ParseSectionSwitch(StringRef Section,
                                       unsigned Characteristics,
                                       SectionKind Kind,
                                       StringRef COMDATSymName,
                                       COFF::COMDATType Type) {
  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in section switching directive");
  Lex();

  getStreamer().SwitchSection(getContext().getCOFFSection(
      Section, Characteristics, Kind, COMDATSymName, Type));

  return false;
}

bool COFFAsmParser::ParseSectionName(StringRef &SectionName) {
  if (!getLexer().is(AsmToken::Identifier))
    return true;

  SectionName = getTok().getIdentifier();
  Lex();
  return false;
}

// .section name [, "flags"] [, identifier [ identifier ], identifier]
//
// Supported flags:
//   a: Ignored.
//   b: BSS section (uninitialized data)
//   d: data section (initialized data)
//   n: "noload" section (removed by linker)
//   D: Discardable section
//   r: Readable section
//   s: Shared section
//   w: Writable section
//   x: Executable section
//   y: Not-readable section (clears 'r')
//
// Subsections are not supported.
bool COFFAsmParser::ParseDirectiveSection(StringRef, SMLoc) {
  StringRef SectionName;

  if (ParseSectionName(SectionName))
    return TokError("expected identifier in directive");

  unsigned Flags = COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                   COFF::IMAGE_SCN_MEM_READ |
                   COFF::IMAGE_SCN_MEM_WRITE;

  if (getLexer().is(AsmToken::Comma)) {
    Lex();

    if (getLexer().isNot(AsmToken::String))
      return TokError("expected string in directive");

    StringRef FlagsStr = getTok().getStringContents();
    Lex();

    if (ParseSectionFlags(SectionName, FlagsStr, &Flags))
      return true;
  }

  COFF::COMDATType Type = (COFF::COMDATType)0;
  StringRef COMDATSymName;
  if (getLexer().is(AsmToken::Comma)) {
    Type = COFF::IMAGE_COMDAT_SELECT_ANY;
    Lex();

    Flags |= COFF::IMAGE_SCN_LNK_COMDAT;

    if (!getLexer().is(AsmToken::Identifier))
      return TokError("expected comdat type such as 'discard' or 'largest' "
                      "after protection bits");

    if (parseCOMDATType(Type))
      return true;

    if (getLexer().isNot(AsmToken::Comma))
      return TokError("expected comma in directive");
    Lex();

    if (getParser().parseIdentifier(COMDATSymName))
      return TokError("expected identifier in directive");
  }

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in directive");

  SectionKind Kind = computeSectionKind(Flags);
  if (Kind.isText()) {
    const Triple &T = getContext().getObjectFileInfo()->getTargetTriple();
    if (T.getArch() == Triple::arm || T.getArch() == Triple::thumb)
      Flags |= COFF::IMAGE_SCN_MEM_16BIT;
  }
  ParseSectionSwitch(SectionName, Flags, Kind, COMDATSymName, Type);
  return false;
}

bool COFFAsmParser::ParseDirectiveDef(StringRef, SMLoc) {
  StringRef SymbolName;

  if (getParser().parseIdentifier(SymbolName))
    return TokError("expected identifier in directive");

  MCSymbol *Sym = getContext().getOrCreateSymbol(SymbolName);

  getStreamer().BeginCOFFSymbolDef(Sym);

  Lex();
  return false;
}

bool COFFAsmParser::ParseDirectiveScl(StringRef, SMLoc) {
  int64_t SymbolStorageClass;
  if (getParser().parseAbsoluteExpression(SymbolStorageClass))
    return true;

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in directive");

  Lex();
  getStreamer().EmitCOFFSymbolStorageClass(SymbolStorageClass);
  return false;
}

bool COFFAsmParser::ParseDirectiveType(StringRef, SMLoc) {
  int64_t Type;
  if (getParser().parseAbsoluteExpression(Type))
    return true;

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in directive");

  Lex();
  getStreamer().EmitCOFFSymbolType(Type);
  return false;
}

bool COFFAsmParser::ParseDirectiveEndef(StringRef, SMLoc) {
  Lex();
  getStreamer().EndCOFFSymbolDef();
  return false;
}

bool COFFAsmParser::ParseDirectiveSecRel32(StringRef, SMLoc) {
  StringRef SymbolID;
  if (getParser().parseIdentifier(SymbolID))
    return TokError("expected identifier in directive");

  int64_t Offset = 0;
  SMLoc OffsetLoc;
  if (getLexer().is(AsmToken::Plus)) {
    OffsetLoc = getLexer().getLoc();
    if (getParser().parseAbsoluteExpression(Offset))
      return true;
  }

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in directive");

  if (Offset < 0 || Offset > std::numeric_limits<uint32_t>::max())
    return Error(
        OffsetLoc,
        "invalid '.secrel32' directive offset, can't be less "
        "than zero or greater than std::numeric_limits<uint32_t>::max()");

  MCSymbol *Symbol = getContext().getOrCreateSymbol(SymbolID);

  Lex();
  getStreamer().EmitCOFFSecRel32(Symbol, Offset);
  return false;
}

bool COFFAsmParser::ParseDirectiveRVA(StringRef, SMLoc) {
  auto parseOp = [&]() -> bool {
    StringRef SymbolID;
    if (getParser().parseIdentifier(SymbolID))
      return TokError("expected identifier in directive");

    int64_t Offset = 0;
    SMLoc OffsetLoc;
    if (getLexer().is(AsmToken::Plus) || getLexer().is(AsmToken::Minus)) {
      OffsetLoc = getLexer().getLoc();
      if (getParser().parseAbsoluteExpression(Offset))
        return true;
    }

    if (Offset < std::numeric_limits<int32_t>::min() ||
        Offset > std::numeric_limits<int32_t>::max())
      return Error(OffsetLoc, "invalid '.rva' directive offset, can't be less "
                              "than -2147483648 or greater than "
                              "2147483647");

    MCSymbol *Symbol = getContext().getOrCreateSymbol(SymbolID);

    getStreamer().EmitCOFFImgRel32(Symbol, Offset);
    return false;
  };

  if (getParser().parseMany(parseOp))
    return addErrorSuffix(" in directive");
  return false;
}

bool COFFAsmParser::ParseDirectiveSafeSEH(StringRef, SMLoc) {
  StringRef SymbolID;
  if (getParser().parseIdentifier(SymbolID))
    return TokError("expected identifier in directive");

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in directive");

  MCSymbol *Symbol = getContext().getOrCreateSymbol(SymbolID);

  Lex();
  getStreamer().EmitCOFFSafeSEH(Symbol);
  return false;
}

bool COFFAsmParser::ParseDirectiveSecIdx(StringRef, SMLoc) {
  StringRef SymbolID;
  if (getParser().parseIdentifier(SymbolID))
    return TokError("expected identifier in directive");

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in directive");

  MCSymbol *Symbol = getContext().getOrCreateSymbol(SymbolID);

  Lex();
  getStreamer().EmitCOFFSectionIndex(Symbol);
  return false;
}

bool COFFAsmParser::ParseDirectiveSymIdx(StringRef, SMLoc) {
  StringRef SymbolID;
  if (getParser().parseIdentifier(SymbolID))
    return TokError("expected identifier in directive");

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in directive");

  MCSymbol *Symbol = getContext().getOrCreateSymbol(SymbolID);

  Lex();
  getStreamer().EmitCOFFSymbolIndex(Symbol);
  return false;
}

/// ::= [ identifier ]
bool COFFAsmParser::parseCOMDATType(COFF::COMDATType &Type) {
  StringRef TypeId = getTok().getIdentifier();

  Type = StringSwitch<COFF::COMDATType>(TypeId)
    .Case("one_only", COFF::IMAGE_COMDAT_SELECT_NODUPLICATES)
    .Case("discard", COFF::IMAGE_COMDAT_SELECT_ANY)
    .Case("same_size", COFF::IMAGE_COMDAT_SELECT_SAME_SIZE)
    .Case("same_contents", COFF::IMAGE_COMDAT_SELECT_EXACT_MATCH)
    .Case("associative", COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE)
    .Case("largest", COFF::IMAGE_COMDAT_SELECT_LARGEST)
    .Case("newest", COFF::IMAGE_COMDAT_SELECT_NEWEST)
    .Default((COFF::COMDATType)0);

  if (Type == 0)
    return TokError(Twine("unrecognized COMDAT type '" + TypeId + "'"));

  Lex();

  return false;
}

/// ParseDirectiveLinkOnce
///  ::= .linkonce [ identifier ]
bool COFFAsmParser::ParseDirectiveLinkOnce(StringRef, SMLoc Loc) {
  COFF::COMDATType Type = COFF::IMAGE_COMDAT_SELECT_ANY;
  if (getLexer().is(AsmToken::Identifier))
    if (parseCOMDATType(Type))
      return true;

  const MCSectionCOFF *Current =
      static_cast<const MCSectionCOFF *>(getStreamer().getCurrentSectionOnly());

  if (Type == COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE)
    return Error(Loc, "cannot make section associative with .linkonce");

  if (Current->getCharacteristics() & COFF::IMAGE_SCN_LNK_COMDAT)
    return Error(Loc, Twine("section '") + Current->getSectionName() +
                                                       "' is already linkonce");

  Current->setSelection(Type);

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in directive");

  return false;
}

bool COFFAsmParser::ParseSEHDirectiveStartProc(StringRef, SMLoc Loc) {
  StringRef SymbolID;
  if (getParser().parseIdentifier(SymbolID))
    return true;

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in directive");

  MCSymbol *Symbol = getContext().getOrCreateSymbol(SymbolID);

  Lex();
  getStreamer().EmitWinCFIStartProc(Symbol, Loc);
  return false;
}

bool COFFAsmParser::ParseSEHDirectiveEndProc(StringRef, SMLoc Loc) {
  Lex();
  getStreamer().EmitWinCFIEndProc(Loc);
  return false;
}

bool COFFAsmParser::ParseSEHDirectiveStartChained(StringRef, SMLoc Loc) {
  Lex();
  getStreamer().EmitWinCFIStartChained(Loc);
  return false;
}

bool COFFAsmParser::ParseSEHDirectiveEndChained(StringRef, SMLoc Loc) {
  Lex();
  getStreamer().EmitWinCFIEndChained(Loc);
  return false;
}

bool COFFAsmParser::ParseSEHDirectiveHandler(StringRef, SMLoc Loc) {
  StringRef SymbolID;
  if (getParser().parseIdentifier(SymbolID))
    return true;

  if (getLexer().isNot(AsmToken::Comma))
    return TokError("you must specify one or both of @unwind or @except");
  Lex();
  bool unwind = false, except = false;
  if (ParseAtUnwindOrAtExcept(unwind, except))
    return true;
  if (getLexer().is(AsmToken::Comma)) {
    Lex();
    if (ParseAtUnwindOrAtExcept(unwind, except))
      return true;
  }
  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in directive");

  MCSymbol *handler = getContext().getOrCreateSymbol(SymbolID);

  Lex();
  getStreamer().EmitWinEHHandler(handler, unwind, except, Loc);
  return false;
}

bool COFFAsmParser::ParseSEHDirectiveHandlerData(StringRef, SMLoc Loc) {
  Lex();
  getStreamer().EmitWinEHHandlerData();
  return false;
}

bool COFFAsmParser::ParseSEHDirectivePushReg(StringRef, SMLoc Loc) {
  unsigned Reg = 0;
  if (ParseSEHRegisterNumber(Reg))
    return true;

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in directive");

  Lex();
  getStreamer().EmitWinCFIPushReg(Reg, Loc);
  return false;
}

bool COFFAsmParser::ParseSEHDirectiveSetFrame(StringRef, SMLoc Loc) {
  unsigned Reg = 0;
  int64_t Off;
  if (ParseSEHRegisterNumber(Reg))
    return true;
  if (getLexer().isNot(AsmToken::Comma))
    return TokError("you must specify a stack pointer offset");

  Lex();
  if (getParser().parseAbsoluteExpression(Off))
    return true;

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in directive");

  Lex();
  getStreamer().EmitWinCFISetFrame(Reg, Off, Loc);
  return false;
}

bool COFFAsmParser::ParseSEHDirectiveAllocStack(StringRef, SMLoc Loc) {
  int64_t Size;
  if (getParser().parseAbsoluteExpression(Size))
    return true;

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in directive");

  Lex();
  getStreamer().EmitWinCFIAllocStack(Size, Loc);
  return false;
}

bool COFFAsmParser::ParseSEHDirectiveSaveReg(StringRef, SMLoc Loc) {
  unsigned Reg = 0;
  int64_t Off;
  if (ParseSEHRegisterNumber(Reg))
    return true;
  if (getLexer().isNot(AsmToken::Comma))
    return TokError("you must specify an offset on the stack");

  Lex();
  if (getParser().parseAbsoluteExpression(Off))
    return true;

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in directive");

  Lex();
  // FIXME: Err on %xmm* registers
  getStreamer().EmitWinCFISaveReg(Reg, Off, Loc);
  return false;
}

// FIXME: This method is inherently x86-specific. It should really be in the
// x86 backend.
bool COFFAsmParser::ParseSEHDirectiveSaveXMM(StringRef, SMLoc Loc) {
  unsigned Reg = 0;
  int64_t Off;
  if (ParseSEHRegisterNumber(Reg))
    return true;
  if (getLexer().isNot(AsmToken::Comma))
    return TokError("you must specify an offset on the stack");

  Lex();
  if (getParser().parseAbsoluteExpression(Off))
    return true;

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in directive");

  Lex();
  // FIXME: Err on non-%xmm* registers
  getStreamer().EmitWinCFISaveXMM(Reg, Off, Loc);
  return false;
}

bool COFFAsmParser::ParseSEHDirectivePushFrame(StringRef, SMLoc Loc) {
  bool Code = false;
  StringRef CodeID;
  if (getLexer().is(AsmToken::At)) {
    SMLoc startLoc = getLexer().getLoc();
    Lex();
    if (!getParser().parseIdentifier(CodeID)) {
      if (CodeID != "code")
        return Error(startLoc, "expected @code");
      Code = true;
    }
  }

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return TokError("unexpected token in directive");

  Lex();
  getStreamer().EmitWinCFIPushFrame(Code, Loc);
  return false;
}

bool COFFAsmParser::ParseSEHDirectiveEndProlog(StringRef, SMLoc Loc) {
  Lex();
  getStreamer().EmitWinCFIEndProlog(Loc);
  return false;
}

bool COFFAsmParser::ParseAtUnwindOrAtExcept(bool &unwind, bool &except) {
  StringRef identifier;
  if (getLexer().isNot(AsmToken::At))
    return TokError("a handler attribute must begin with '@'");
  SMLoc startLoc = getLexer().getLoc();
  Lex();
  if (getParser().parseIdentifier(identifier))
    return Error(startLoc, "expected @unwind or @except");
  if (identifier == "unwind")
    unwind = true;
  else if (identifier == "except")
    except = true;
  else
    return Error(startLoc, "expected @unwind or @except");
  return false;
}

bool COFFAsmParser::ParseSEHRegisterNumber(unsigned &RegNo) {
  SMLoc startLoc = getLexer().getLoc();
  if (getLexer().is(AsmToken::Percent)) {
    const MCRegisterInfo *MRI = getContext().getRegisterInfo();
    SMLoc endLoc;
    unsigned LLVMRegNo;
    if (getParser().getTargetParser().ParseRegister(LLVMRegNo,startLoc,endLoc))
      return true;

#if 0
    // FIXME: TargetAsmInfo::getCalleeSavedRegs() commits a serious layering
    // violation so this validation code is disabled.

    // Check that this is a non-volatile register.
    const unsigned *NVRegs = TAI.getCalleeSavedRegs();
    unsigned i;
    for (i = 0; NVRegs[i] != 0; ++i)
      if (NVRegs[i] == LLVMRegNo)
        break;
    if (NVRegs[i] == 0)
      return Error(startLoc, "expected non-volatile register");
#endif

    int SEHRegNo = MRI->getSEHRegNum(LLVMRegNo);
    if (SEHRegNo < 0)
      return Error(startLoc,"register can't be represented in SEH unwind info");
    RegNo = SEHRegNo;
  }
  else {
    int64_t n;
    if (getParser().parseAbsoluteExpression(n))
      return true;
    if (n > 15)
      return Error(startLoc, "register number is too high");
    RegNo = n;
  }

  return false;
}

namespace llvm {

MCAsmParserExtension *createCOFFAsmParser() {
  return new COFFAsmParser;
}

} // end namespace llvm
