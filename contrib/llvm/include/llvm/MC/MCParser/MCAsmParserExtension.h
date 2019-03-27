//===- llvm/MC/MCAsmParserExtension.h - Asm Parser Hooks --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCPARSER_MCASMPARSEREXTENSION_H
#define LLVM_MC_MCPARSER_MCASMPARSEREXTENSION_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/Support/SMLoc.h"

namespace llvm {

class Twine;

/// Generic interface for extending the MCAsmParser,
/// which is implemented by target and object file assembly parser
/// implementations.
class MCAsmParserExtension {
  MCAsmParser *Parser;

protected:
  MCAsmParserExtension();

  // Helper template for implementing static dispatch functions.
  template<typename T, bool (T::*Handler)(StringRef, SMLoc)>
  static bool HandleDirective(MCAsmParserExtension *Target,
                              StringRef Directive,
                              SMLoc DirectiveLoc) {
    T *Obj = static_cast<T*>(Target);
    return (Obj->*Handler)(Directive, DirectiveLoc);
  }

  bool BracketExpressionsSupported = false;

public:
  MCAsmParserExtension(const MCAsmParserExtension &) = delete;
  MCAsmParserExtension &operator=(const MCAsmParserExtension &) = delete;
  virtual ~MCAsmParserExtension();

  /// Initialize the extension for parsing using the given \p Parser.
  /// The extension should use the AsmParser interfaces to register its
  /// parsing routines.
  virtual void Initialize(MCAsmParser &Parser);

  /// \name MCAsmParser Proxy Interfaces
  /// @{

  MCContext &getContext() { return getParser().getContext(); }

  MCAsmLexer &getLexer() { return getParser().getLexer(); }
  const MCAsmLexer &getLexer() const {
    return const_cast<MCAsmParserExtension *>(this)->getLexer();
  }

  MCAsmParser &getParser() { return *Parser; }
  const MCAsmParser &getParser() const {
    return const_cast<MCAsmParserExtension*>(this)->getParser();
  }

  SourceMgr &getSourceManager() { return getParser().getSourceManager(); }
  MCStreamer &getStreamer() { return getParser().getStreamer(); }

  bool Warning(SMLoc L, const Twine &Msg) {
    return getParser().Warning(L, Msg);
  }

  bool Error(SMLoc L, const Twine &Msg, SMRange Range = SMRange()) {
    return getParser().Error(L, Msg, Range);
  }

  void Note(SMLoc L, const Twine &Msg) {
    getParser().Note(L, Msg);
  }

  bool TokError(const Twine &Msg) {
    return getParser().TokError(Msg);
  }

  const AsmToken &Lex() { return getParser().Lex(); }
  const AsmToken &getTok() { return getParser().getTok(); }
  bool parseToken(AsmToken::TokenKind T,
                  const Twine &Msg = "unexpected token") {
    return getParser().parseToken(T, Msg);
  }

  bool parseMany(function_ref<bool()> parseOne, bool hasComma = true) {
    return getParser().parseMany(parseOne, hasComma);
  }

  bool parseOptionalToken(AsmToken::TokenKind T) {
    return getParser().parseOptionalToken(T);
  }

  bool check(bool P, const Twine &Msg) {
    return getParser().check(P, Msg);
  }

  bool check(bool P, SMLoc Loc, const Twine &Msg) {
    return getParser().check(P, Loc, Msg);
  }

  bool addErrorSuffix(const Twine &Suffix) {
    return getParser().addErrorSuffix(Suffix);
  }

  bool HasBracketExpressions() const { return BracketExpressionsSupported; }

  /// @}
};

} // end namespace llvm

#endif // LLVM_MC_MCPARSER_MCASMPARSEREXTENSION_H
