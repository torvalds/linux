//===- WasmAsmParser.cpp - Wasm Assembly Parser -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// --
//
// Note, this is for wasm, the binary format (analogous to ELF), not wasm,
// the instruction set (analogous to x86), for which parsing code lives in
// WebAssemblyAsmParser.
//
// This file contains processing for generic directives implemented using
// MCTargetStreamer, the ones that depend on WebAssemblyTargetStreamer are in
// WebAssemblyAsmParser.
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCAsmParserExtension.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolWasm.h"
#include "llvm/Support/MachineValueType.h"

using namespace llvm;

namespace {

class WasmAsmParser : public MCAsmParserExtension {
  MCAsmParser *Parser;
  MCAsmLexer *Lexer;

  template<bool (WasmAsmParser::*HandlerMethod)(StringRef, SMLoc)>
  void addDirectiveHandler(StringRef Directive) {
    MCAsmParser::ExtensionDirectiveHandler Handler = std::make_pair(
        this, HandleDirective<WasmAsmParser, HandlerMethod>);

    getParser().addDirectiveHandler(Directive, Handler);
  }

public:
  WasmAsmParser() : Parser(nullptr), Lexer(nullptr) {
    BracketExpressionsSupported = true;
  }

  void Initialize(MCAsmParser &P) override {
    Parser = &P;
    Lexer = &Parser->getLexer();
    // Call the base implementation.
    this->MCAsmParserExtension::Initialize(*Parser);

    addDirectiveHandler<&WasmAsmParser::parseSectionDirectiveText>(".text");
    addDirectiveHandler<&WasmAsmParser::parseSectionDirective>(".section");
    addDirectiveHandler<&WasmAsmParser::parseDirectiveSize>(".size");
    addDirectiveHandler<&WasmAsmParser::parseDirectiveType>(".type");
  }

  bool Error(const StringRef &msg, const AsmToken &tok) {
    return Parser->Error(tok.getLoc(), msg + tok.getString());
  }

  bool IsNext(AsmToken::TokenKind Kind) {
    auto ok = Lexer->is(Kind);
    if (ok) Lex();
    return ok;
  }

  bool Expect(AsmToken::TokenKind Kind, const char *KindName) {
    if (!IsNext(Kind))
      return Error(std::string("Expected ") + KindName + ", instead got: ",
                   Lexer->getTok());
    return false;
  }

  bool parseSectionDirectiveText(StringRef, SMLoc) {
    // FIXME: .text currently no-op.
    return false;
  }

  bool parseSectionDirective(StringRef, SMLoc) {
    // FIXME: .section currently no-op.
    while (Lexer->isNot(AsmToken::EndOfStatement)) Parser->Lex();
    return false;
  }

  // TODO: This function is almost the same as ELFAsmParser::ParseDirectiveSize
  // so maybe could be shared somehow.
  bool parseDirectiveSize(StringRef, SMLoc) {
    StringRef Name;
    if (Parser->parseIdentifier(Name))
      return TokError("expected identifier in directive");
    auto Sym = getContext().getOrCreateSymbol(Name);
    if (Lexer->isNot(AsmToken::Comma))
      return TokError("unexpected token in directive");
    Lex();
    const MCExpr *Expr;
    if (Parser->parseExpression(Expr))
      return true;
    if (Lexer->isNot(AsmToken::EndOfStatement))
      return TokError("unexpected token in directive");
    Lex();
    // MCWasmStreamer implements this.
    getStreamer().emitELFSize(Sym, Expr);
    return false;
  }

  bool parseDirectiveType(StringRef, SMLoc) {
    // This could be the start of a function, check if followed by
    // "label,@function"
    if (!Lexer->is(AsmToken::Identifier))
      return Error("Expected label after .type directive, got: ",
                   Lexer->getTok());
    auto WasmSym = cast<MCSymbolWasm>(
                     getStreamer().getContext().getOrCreateSymbol(
                       Lexer->getTok().getString()));
    Lex();
    if (!(IsNext(AsmToken::Comma) && IsNext(AsmToken::At) &&
          Lexer->is(AsmToken::Identifier)))
      return Error("Expected label,@type declaration, got: ", Lexer->getTok());
    auto TypeName = Lexer->getTok().getString();
    if (TypeName == "function")
      WasmSym->setType(wasm::WASM_SYMBOL_TYPE_FUNCTION);
    else if (TypeName == "global")
      WasmSym->setType(wasm::WASM_SYMBOL_TYPE_GLOBAL);
    else
      return Error("Unknown WASM symbol type: ", Lexer->getTok());
    Lex();
    return Expect(AsmToken::EndOfStatement, "EOL");
  }
};

} // end anonymous namespace

namespace llvm {

MCAsmParserExtension *createWasmAsmParser() {
  return new WasmAsmParser;
}

} // end namespace llvm
