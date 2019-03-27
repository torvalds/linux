//==- WebAssemblyAsmParser.cpp - Assembler for WebAssembly -*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file is part of the WebAssembly Assembler.
///
/// It contains code to translate a parsed .s file into MCInsts.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "MCTargetDesc/WebAssemblyTargetStreamer.h"
#include "WebAssembly.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolWasm.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "wasm-asm-parser"

namespace {

/// WebAssemblyOperand - Instances of this class represent the operands in a
/// parsed WASM machine instruction.
struct WebAssemblyOperand : public MCParsedAsmOperand {
  enum KindTy { Token, Integer, Float, Symbol, BrList } Kind;

  SMLoc StartLoc, EndLoc;

  struct TokOp {
    StringRef Tok;
  };

  struct IntOp {
    int64_t Val;
  };

  struct FltOp {
    double Val;
  };

  struct SymOp {
    const MCExpr *Exp;
  };

  struct BrLOp {
    std::vector<unsigned> List;
  };

  union {
    struct TokOp Tok;
    struct IntOp Int;
    struct FltOp Flt;
    struct SymOp Sym;
    struct BrLOp BrL;
  };

  WebAssemblyOperand(KindTy K, SMLoc Start, SMLoc End, TokOp T)
      : Kind(K), StartLoc(Start), EndLoc(End), Tok(T) {}
  WebAssemblyOperand(KindTy K, SMLoc Start, SMLoc End, IntOp I)
      : Kind(K), StartLoc(Start), EndLoc(End), Int(I) {}
  WebAssemblyOperand(KindTy K, SMLoc Start, SMLoc End, FltOp F)
      : Kind(K), StartLoc(Start), EndLoc(End), Flt(F) {}
  WebAssemblyOperand(KindTy K, SMLoc Start, SMLoc End, SymOp S)
      : Kind(K), StartLoc(Start), EndLoc(End), Sym(S) {}
  WebAssemblyOperand(KindTy K, SMLoc Start, SMLoc End)
      : Kind(K), StartLoc(Start), EndLoc(End), BrL() {}

  ~WebAssemblyOperand() {
    if (isBrList())
      BrL.~BrLOp();
  }

  bool isToken() const override { return Kind == Token; }
  bool isImm() const override {
    return Kind == Integer || Kind == Float || Kind == Symbol;
  }
  bool isMem() const override { return false; }
  bool isReg() const override { return false; }
  bool isBrList() const { return Kind == BrList; }

  unsigned getReg() const override {
    llvm_unreachable("Assembly inspects a register operand");
    return 0;
  }

  StringRef getToken() const {
    assert(isToken());
    return Tok.Tok;
  }

  SMLoc getStartLoc() const override { return StartLoc; }
  SMLoc getEndLoc() const override { return EndLoc; }

  void addRegOperands(MCInst &, unsigned) const {
    // Required by the assembly matcher.
    llvm_unreachable("Assembly matcher creates register operands");
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    if (Kind == Integer)
      Inst.addOperand(MCOperand::createImm(Int.Val));
    else if (Kind == Float)
      Inst.addOperand(MCOperand::createFPImm(Flt.Val));
    else if (Kind == Symbol)
      Inst.addOperand(MCOperand::createExpr(Sym.Exp));
    else
      llvm_unreachable("Should be immediate or symbol!");
  }

  void addBrListOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && isBrList() && "Invalid BrList!");
    for (auto Br : BrL.List)
      Inst.addOperand(MCOperand::createImm(Br));
  }

  void print(raw_ostream &OS) const override {
    switch (Kind) {
    case Token:
      OS << "Tok:" << Tok.Tok;
      break;
    case Integer:
      OS << "Int:" << Int.Val;
      break;
    case Float:
      OS << "Flt:" << Flt.Val;
      break;
    case Symbol:
      OS << "Sym:" << Sym.Exp;
      break;
    case BrList:
      OS << "BrList:" << BrL.List.size();
      break;
    }
  }
};

class WebAssemblyAsmParser final : public MCTargetAsmParser {
  MCAsmParser &Parser;
  MCAsmLexer &Lexer;

  // Much like WebAssemblyAsmPrinter in the backend, we have to own these.
  std::vector<std::unique_ptr<wasm::WasmSignature>> Signatures;

  // Order of labels, directives and instructions in a .s file have no
  // syntactical enforcement. This class is a callback from the actual parser,
  // and yet we have to be feeding data to the streamer in a very particular
  // order to ensure a correct binary encoding that matches the regular backend
  // (the streamer does not enforce this). This "state machine" enum helps
  // guarantee that correct order.
  enum ParserState {
    FileStart,
    Label,
    FunctionStart,
    FunctionLocals,
    Instructions,
  } CurrentState = FileStart;

  // For ensuring blocks are properly nested.
  enum NestingType {
    Function,
    Block,
    Loop,
    Try,
    If,
    Else,
    Undefined,
  };
  std::vector<NestingType> NestingStack;

  // We track this to see if a .functype following a label is the same,
  // as this is how we recognize the start of a function.
  MCSymbol *LastLabel = nullptr;

public:
  WebAssemblyAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                       const MCInstrInfo &MII, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, STI, MII), Parser(Parser),
        Lexer(Parser.getLexer()) {
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }

#define GET_ASSEMBLER_HEADER
#include "WebAssemblyGenAsmMatcher.inc"

  // TODO: This is required to be implemented, but appears unused.
  bool ParseRegister(unsigned & /*RegNo*/, SMLoc & /*StartLoc*/,
                     SMLoc & /*EndLoc*/) override {
    llvm_unreachable("ParseRegister is not implemented.");
  }

  bool error(const Twine &Msg, const AsmToken &Tok) {
    return Parser.Error(Tok.getLoc(), Msg + Tok.getString());
  }

  bool error(const Twine &Msg) {
    return Parser.Error(Lexer.getTok().getLoc(), Msg);
  }

  void addSignature(std::unique_ptr<wasm::WasmSignature> &&Sig) {
    Signatures.push_back(std::move(Sig));
  }

  std::pair<StringRef, StringRef> nestingString(NestingType NT) {
    switch (NT) {
    case Function:
      return {"function", "end_function"};
    case Block:
      return {"block", "end_block"};
    case Loop:
      return {"loop", "end_loop"};
    case Try:
      return {"try", "end_try"};
    case If:
      return {"if", "end_if"};
    case Else:
      return {"else", "end_if"};
    default:
      llvm_unreachable("unknown NestingType");
    }
  }

  void push(NestingType NT) { NestingStack.push_back(NT); }

  bool pop(StringRef Ins, NestingType NT1, NestingType NT2 = Undefined) {
    if (NestingStack.empty())
      return error(Twine("End of block construct with no start: ") + Ins);
    auto Top = NestingStack.back();
    if (Top != NT1 && Top != NT2)
      return error(Twine("Block construct type mismatch, expected: ") +
                   nestingString(Top).second + ", instead got: " + Ins);
    NestingStack.pop_back();
    return false;
  }

  bool ensureEmptyNestingStack() {
    auto err = !NestingStack.empty();
    while (!NestingStack.empty()) {
      error(Twine("Unmatched block construct(s) at function end: ") +
            nestingString(NestingStack.back()).first);
      NestingStack.pop_back();
    }
    return err;
  }

  bool isNext(AsmToken::TokenKind Kind) {
    auto Ok = Lexer.is(Kind);
    if (Ok)
      Parser.Lex();
    return Ok;
  }

  bool expect(AsmToken::TokenKind Kind, const char *KindName) {
    if (!isNext(Kind))
      return error(std::string("Expected ") + KindName + ", instead got: ",
                   Lexer.getTok());
    return false;
  }

  StringRef expectIdent() {
    if (!Lexer.is(AsmToken::Identifier)) {
      error("Expected identifier, got: ", Lexer.getTok());
      return StringRef();
    }
    auto Name = Lexer.getTok().getString();
    Parser.Lex();
    return Name;
  }

  Optional<wasm::ValType> parseType(const StringRef &Type) {
    // FIXME: can't use StringSwitch because wasm::ValType doesn't have a
    // "invalid" value.
    if (Type == "i32")
      return wasm::ValType::I32;
    if (Type == "i64")
      return wasm::ValType::I64;
    if (Type == "f32")
      return wasm::ValType::F32;
    if (Type == "f64")
      return wasm::ValType::F64;
    if (Type == "v128" || Type == "i8x16" || Type == "i16x8" ||
        Type == "i32x4" || Type == "i64x2" || Type == "f32x4" ||
        Type == "f64x2")
      return wasm::ValType::V128;
    return Optional<wasm::ValType>();
  }

  WebAssembly::ExprType parseBlockType(StringRef ID) {
    return StringSwitch<WebAssembly::ExprType>(ID)
        .Case("i32", WebAssembly::ExprType::I32)
        .Case("i64", WebAssembly::ExprType::I64)
        .Case("f32", WebAssembly::ExprType::F32)
        .Case("f64", WebAssembly::ExprType::F64)
        .Case("v128", WebAssembly::ExprType::V128)
        .Case("except_ref", WebAssembly::ExprType::ExceptRef)
        .Case("void", WebAssembly::ExprType::Void)
        .Default(WebAssembly::ExprType::Invalid);
  }

  bool parseRegTypeList(SmallVectorImpl<wasm::ValType> &Types) {
    while (Lexer.is(AsmToken::Identifier)) {
      auto Type = parseType(Lexer.getTok().getString());
      if (!Type)
        return true;
      Types.push_back(Type.getValue());
      Parser.Lex();
      if (!isNext(AsmToken::Comma))
        break;
    }
    return false;
  }

  void parseSingleInteger(bool IsNegative, OperandVector &Operands) {
    auto &Int = Lexer.getTok();
    int64_t Val = Int.getIntVal();
    if (IsNegative)
      Val = -Val;
    Operands.push_back(make_unique<WebAssemblyOperand>(
        WebAssemblyOperand::Integer, Int.getLoc(), Int.getEndLoc(),
        WebAssemblyOperand::IntOp{Val}));
    Parser.Lex();
  }

  bool parseOperandStartingWithInteger(bool IsNegative, OperandVector &Operands,
                                       StringRef InstName) {
    parseSingleInteger(IsNegative, Operands);
    // FIXME: there is probably a cleaner way to do this.
    auto IsLoadStore = InstName.startswith("load") ||
                       InstName.startswith("store") ||
                       InstName.startswith("atomic_load") ||
                       InstName.startswith("atomic_store");
    if (IsLoadStore) {
      // Parse load/store operands of the form: offset align
      auto &Offset = Lexer.getTok();
      if (Offset.is(AsmToken::Integer)) {
        parseSingleInteger(false, Operands);
      } else {
        // Alignment not specified.
        // FIXME: correctly derive a default from the instruction.
        // We can't just call WebAssembly::GetDefaultP2Align since we don't have
        // an opcode until after the assembly matcher.
        Operands.push_back(make_unique<WebAssemblyOperand>(
            WebAssemblyOperand::Integer, Offset.getLoc(), Offset.getEndLoc(),
            WebAssemblyOperand::IntOp{0}));
      }
    }
    return false;
  }

  void addBlockTypeOperand(OperandVector &Operands, SMLoc NameLoc,
                           WebAssembly::ExprType BT) {
    Operands.push_back(make_unique<WebAssemblyOperand>(
        WebAssemblyOperand::Integer, NameLoc, NameLoc,
        WebAssemblyOperand::IntOp{static_cast<int64_t>(BT)}));
  }

  bool ParseInstruction(ParseInstructionInfo & /*Info*/, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override {
    // Note: Name does NOT point into the sourcecode, but to a local, so
    // use NameLoc instead.
    Name = StringRef(NameLoc.getPointer(), Name.size());

    // WebAssembly has instructions with / in them, which AsmLexer parses
    // as seperate tokens, so if we find such tokens immediately adjacent (no
    // whitespace), expand the name to include them:
    for (;;) {
      auto &Sep = Lexer.getTok();
      if (Sep.getLoc().getPointer() != Name.end() ||
          Sep.getKind() != AsmToken::Slash)
        break;
      // Extend name with /
      Name = StringRef(Name.begin(), Name.size() + Sep.getString().size());
      Parser.Lex();
      // We must now find another identifier, or error.
      auto &Id = Lexer.getTok();
      if (Id.getKind() != AsmToken::Identifier ||
          Id.getLoc().getPointer() != Name.end())
        return error("Incomplete instruction name: ", Id);
      Name = StringRef(Name.begin(), Name.size() + Id.getString().size());
      Parser.Lex();
    }

    // Now construct the name as first operand.
    Operands.push_back(make_unique<WebAssemblyOperand>(
        WebAssemblyOperand::Token, NameLoc, SMLoc::getFromPointer(Name.end()),
        WebAssemblyOperand::TokOp{Name}));
    auto NamePair = Name.split('.');
    // If no '.', there is no type prefix.
    auto BaseName = NamePair.second.empty() ? NamePair.first : NamePair.second;

    // If this instruction is part of a control flow structure, ensure
    // proper nesting.
    bool ExpectBlockType = false;
    if (BaseName == "block") {
      push(Block);
      ExpectBlockType = true;
    } else if (BaseName == "loop") {
      push(Loop);
      ExpectBlockType = true;
    } else if (BaseName == "try") {
      push(Try);
      ExpectBlockType = true;
    } else if (BaseName == "if") {
      push(If);
      ExpectBlockType = true;
    } else if (BaseName == "else") {
      if (pop(BaseName, If))
        return true;
      push(Else);
    } else if (BaseName == "catch") {
      if (pop(BaseName, Try))
        return true;
      push(Try);
    } else if (BaseName == "catch_all") {
      if (pop(BaseName, Try))
        return true;
      push(Try);
    } else if (BaseName == "end_if") {
      if (pop(BaseName, If, Else))
        return true;
    } else if (BaseName == "end_try") {
      if (pop(BaseName, Try))
        return true;
    } else if (BaseName == "end_loop") {
      if (pop(BaseName, Loop))
        return true;
    } else if (BaseName == "end_block") {
      if (pop(BaseName, Block))
        return true;
    } else if (BaseName == "end_function") {
      if (pop(BaseName, Function) || ensureEmptyNestingStack())
        return true;
    }

    while (Lexer.isNot(AsmToken::EndOfStatement)) {
      auto &Tok = Lexer.getTok();
      switch (Tok.getKind()) {
      case AsmToken::Identifier: {
        auto &Id = Lexer.getTok();
        if (ExpectBlockType) {
          // Assume this identifier is a block_type.
          auto BT = parseBlockType(Id.getString());
          if (BT == WebAssembly::ExprType::Invalid)
            return error("Unknown block type: ", Id);
          addBlockTypeOperand(Operands, NameLoc, BT);
          Parser.Lex();
        } else {
          // Assume this identifier is a label.
          const MCExpr *Val;
          SMLoc End;
          if (Parser.parsePrimaryExpr(Val, End))
            return error("Cannot parse symbol: ", Lexer.getTok());
          Operands.push_back(make_unique<WebAssemblyOperand>(
              WebAssemblyOperand::Symbol, Id.getLoc(), Id.getEndLoc(),
              WebAssemblyOperand::SymOp{Val}));
        }
        break;
      }
      case AsmToken::Minus:
        Parser.Lex();
        if (Lexer.isNot(AsmToken::Integer))
          return error("Expected integer instead got: ", Lexer.getTok());
        if (parseOperandStartingWithInteger(true, Operands, BaseName))
          return true;
        break;
      case AsmToken::Integer:
        if (parseOperandStartingWithInteger(false, Operands, BaseName))
          return true;
        break;
      case AsmToken::Real: {
        double Val;
        if (Tok.getString().getAsDouble(Val, false))
          return error("Cannot parse real: ", Tok);
        Operands.push_back(make_unique<WebAssemblyOperand>(
            WebAssemblyOperand::Float, Tok.getLoc(), Tok.getEndLoc(),
            WebAssemblyOperand::FltOp{Val}));
        Parser.Lex();
        break;
      }
      case AsmToken::LCurly: {
        Parser.Lex();
        auto Op = make_unique<WebAssemblyOperand>(
            WebAssemblyOperand::BrList, Tok.getLoc(), Tok.getEndLoc());
        if (!Lexer.is(AsmToken::RCurly))
          for (;;) {
            Op->BrL.List.push_back(Lexer.getTok().getIntVal());
            expect(AsmToken::Integer, "integer");
            if (!isNext(AsmToken::Comma))
              break;
          }
        expect(AsmToken::RCurly, "}");
        Operands.push_back(std::move(Op));
        break;
      }
      default:
        return error("Unexpected token in operand: ", Tok);
      }
      if (Lexer.isNot(AsmToken::EndOfStatement)) {
        if (expect(AsmToken::Comma, ","))
          return true;
      }
    }
    if (ExpectBlockType && Operands.size() == 1) {
      // Support blocks with no operands as default to void.
      addBlockTypeOperand(Operands, NameLoc, WebAssembly::ExprType::Void);
    }
    Parser.Lex();
    return false;
  }

  void onLabelParsed(MCSymbol *Symbol) override {
    LastLabel = Symbol;
    CurrentState = Label;
  }

  bool parseSignature(wasm::WasmSignature *Signature) {
    if (expect(AsmToken::LParen, "("))
      return true;
    if (parseRegTypeList(Signature->Params))
      return true;
    if (expect(AsmToken::RParen, ")"))
      return true;
    if (expect(AsmToken::MinusGreater, "->"))
      return true;
    if (expect(AsmToken::LParen, "("))
      return true;
    if (parseRegTypeList(Signature->Returns))
      return true;
    if (expect(AsmToken::RParen, ")"))
      return true;
    return false;
  }

  // This function processes wasm-specific directives streamed to
  // WebAssemblyTargetStreamer, all others go to the generic parser
  // (see WasmAsmParser).
  bool ParseDirective(AsmToken DirectiveID) override {
    // This function has a really weird return value behavior that is different
    // from all the other parsing functions:
    // - return true && no tokens consumed -> don't know this directive / let
    //   the generic parser handle it.
    // - return true && tokens consumed -> a parsing error occurred.
    // - return false -> processed this directive successfully.
    assert(DirectiveID.getKind() == AsmToken::Identifier);
    auto &Out = getStreamer();
    auto &TOut =
        reinterpret_cast<WebAssemblyTargetStreamer &>(*Out.getTargetStreamer());

    // TODO: any time we return an error, at least one token must have been
    // consumed, otherwise this will not signal an error to the caller.
    if (DirectiveID.getString() == ".globaltype") {
      auto SymName = expectIdent();
      if (SymName.empty())
        return true;
      if (expect(AsmToken::Comma, ","))
        return true;
      auto TypeTok = Lexer.getTok();
      auto TypeName = expectIdent();
      if (TypeName.empty())
        return true;
      auto Type = parseType(TypeName);
      if (!Type)
        return error("Unknown type in .globaltype directive: ", TypeTok);
      // Now set this symbol with the correct type.
      auto WasmSym = cast<MCSymbolWasm>(
          TOut.getStreamer().getContext().getOrCreateSymbol(SymName));
      WasmSym->setType(wasm::WASM_SYMBOL_TYPE_GLOBAL);
      WasmSym->setGlobalType(
          wasm::WasmGlobalType{uint8_t(Type.getValue()), true});
      // And emit the directive again.
      TOut.emitGlobalType(WasmSym);
      return expect(AsmToken::EndOfStatement, "EOL");
    }

    if (DirectiveID.getString() == ".functype") {
      // This code has to send things to the streamer similar to
      // WebAssemblyAsmPrinter::EmitFunctionBodyStart.
      // TODO: would be good to factor this into a common function, but the
      // assembler and backend really don't share any common code, and this code
      // parses the locals seperately.
      auto SymName = expectIdent();
      if (SymName.empty())
        return true;
      auto WasmSym = cast<MCSymbolWasm>(
          TOut.getStreamer().getContext().getOrCreateSymbol(SymName));
      if (CurrentState == Label && WasmSym == LastLabel) {
        // This .functype indicates a start of a function.
        if (ensureEmptyNestingStack())
          return true;
        CurrentState = FunctionStart;
        push(Function);
      }
      auto Signature = make_unique<wasm::WasmSignature>();
      if (parseSignature(Signature.get()))
        return true;
      WasmSym->setSignature(Signature.get());
      addSignature(std::move(Signature));
      WasmSym->setType(wasm::WASM_SYMBOL_TYPE_FUNCTION);
      TOut.emitFunctionType(WasmSym);
      // TODO: backend also calls TOut.emitIndIdx, but that is not implemented.
      return expect(AsmToken::EndOfStatement, "EOL");
    }

    if (DirectiveID.getString() == ".eventtype") {
      auto SymName = expectIdent();
      if (SymName.empty())
        return true;
      auto WasmSym = cast<MCSymbolWasm>(
          TOut.getStreamer().getContext().getOrCreateSymbol(SymName));
      auto Signature = make_unique<wasm::WasmSignature>();
      if (parseRegTypeList(Signature->Params))
        return true;
      WasmSym->setSignature(Signature.get());
      addSignature(std::move(Signature));
      WasmSym->setType(wasm::WASM_SYMBOL_TYPE_EVENT);
      TOut.emitEventType(WasmSym);
      // TODO: backend also calls TOut.emitIndIdx, but that is not implemented.
      return expect(AsmToken::EndOfStatement, "EOL");
    }

    if (DirectiveID.getString() == ".local") {
      if (CurrentState != FunctionStart)
        return error(".local directive should follow the start of a function",
                     Lexer.getTok());
      SmallVector<wasm::ValType, 4> Locals;
      if (parseRegTypeList(Locals))
        return true;
      TOut.emitLocal(Locals);
      CurrentState = FunctionLocals;
      return expect(AsmToken::EndOfStatement, "EOL");
    }

    return true; // We didn't process this directive.
  }

  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned & /*Opcode*/,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override {
    MCInst Inst;
    unsigned MatchResult =
        MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm);
    switch (MatchResult) {
    case Match_Success: {
      if (CurrentState == FunctionStart) {
        // This is the first instruction in a function, but we haven't seen
        // a .local directive yet. The streamer requires locals to be encoded
        // as a prelude to the instructions, so emit an empty list of locals
        // here.
        auto &TOut = reinterpret_cast<WebAssemblyTargetStreamer &>(
            *Out.getTargetStreamer());
        TOut.emitLocal(SmallVector<wasm::ValType, 0>());
      }
      CurrentState = Instructions;
      Out.EmitInstruction(Inst, getSTI());
      return false;
    }
    case Match_MissingFeature:
      return Parser.Error(
          IDLoc, "instruction requires a WASM feature not currently enabled");
    case Match_MnemonicFail:
      return Parser.Error(IDLoc, "invalid instruction");
    case Match_NearMisses:
      return Parser.Error(IDLoc, "ambiguous instruction");
    case Match_InvalidTiedOperand:
    case Match_InvalidOperand: {
      SMLoc ErrorLoc = IDLoc;
      if (ErrorInfo != ~0ULL) {
        if (ErrorInfo >= Operands.size())
          return Parser.Error(IDLoc, "too few operands for instruction");
        ErrorLoc = Operands[ErrorInfo]->getStartLoc();
        if (ErrorLoc == SMLoc())
          ErrorLoc = IDLoc;
      }
      return Parser.Error(ErrorLoc, "invalid operand for instruction");
    }
    }
    llvm_unreachable("Implement any new match types added!");
  }

  void onEndOfFile() override { ensureEmptyNestingStack(); }
};
} // end anonymous namespace

// Force static initialization.
extern "C" void LLVMInitializeWebAssemblyAsmParser() {
  RegisterMCAsmParser<WebAssemblyAsmParser> X(getTheWebAssemblyTarget32());
  RegisterMCAsmParser<WebAssemblyAsmParser> Y(getTheWebAssemblyTarget64());
}

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "WebAssemblyGenAsmMatcher.inc"
