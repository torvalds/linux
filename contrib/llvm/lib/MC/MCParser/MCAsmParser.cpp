//===-- MCAsmParser.cpp - Abstract Asm Parser Interface -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;

MCAsmParser::MCAsmParser() {}

MCAsmParser::~MCAsmParser() = default;

void MCAsmParser::setTargetParser(MCTargetAsmParser &P) {
  assert(!TargetParser && "Target parser is already initialized!");
  TargetParser = &P;
  TargetParser->Initialize(*this);
}

const AsmToken &MCAsmParser::getTok() const {
  return getLexer().getTok();
}

bool MCAsmParser::parseTokenLoc(SMLoc &Loc) {
  Loc = getTok().getLoc();
  return false;
}

bool MCAsmParser::parseEOL(const Twine &Msg) {
  if (getTok().getKind() != AsmToken::EndOfStatement)
    return Error(getTok().getLoc(), Msg);
  Lex();
  return false;
}

bool MCAsmParser::parseToken(AsmToken::TokenKind T, const Twine &Msg) {
  if (T == AsmToken::EndOfStatement)
    return parseEOL(Msg);
  if (getTok().getKind() != T)
    return Error(getTok().getLoc(), Msg);
  Lex();
  return false;
}

bool MCAsmParser::parseIntToken(int64_t &V, const Twine &Msg) {
  if (getTok().getKind() != AsmToken::Integer)
    return TokError(Msg);
  V = getTok().getIntVal();
  Lex();
  return false;
}

bool MCAsmParser::parseOptionalToken(AsmToken::TokenKind T) {
  bool Present = (getTok().getKind() == T);
  if (Present)
    parseToken(T);
  return Present;
}

bool MCAsmParser::check(bool P, const Twine &Msg) {
  return check(P, getTok().getLoc(), Msg);
}

bool MCAsmParser::check(bool P, SMLoc Loc, const Twine &Msg) {
  if (P)
    return Error(Loc, Msg);
  return false;
}

bool MCAsmParser::TokError(const Twine &Msg, SMRange Range) {
  return Error(getLexer().getLoc(), Msg, Range);
}

bool MCAsmParser::Error(SMLoc L, const Twine &Msg, SMRange Range) {

  MCPendingError PErr;
  PErr.Loc = L;
  Msg.toVector(PErr.Msg);
  PErr.Range = Range;
  PendingErrors.push_back(PErr);

  // If we threw this parsing error after a lexing error, this should
  // supercede the lexing error and so we remove it from the Lexer
  // before it can propagate
  if (getTok().is(AsmToken::Error))
    getLexer().Lex();
  return true;
}

bool MCAsmParser::addErrorSuffix(const Twine &Suffix) {
  // Make sure lexing errors have propagated to the parser.
  if (getTok().is(AsmToken::Error))
    Lex();
  for (auto &PErr : PendingErrors)
    Suffix.toVector(PErr.Msg);
  return true;
}

bool MCAsmParser::parseMany(function_ref<bool()> parseOne, bool hasComma) {
  if (parseOptionalToken(AsmToken::EndOfStatement))
    return false;
  while (true) {
    if (parseOne())
      return true;
    if (parseOptionalToken(AsmToken::EndOfStatement))
      return false;
    if (hasComma && parseToken(AsmToken::Comma))
      return true;
  }
  return false;
}

bool MCAsmParser::parseExpression(const MCExpr *&Res) {
  SMLoc L;
  return parseExpression(Res, L);
}

void MCParsedAsmOperand::dump() const {
  // Cannot completely remove virtual function even in release mode.
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  dbgs() << "  " << *this;
#endif
}
