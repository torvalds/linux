//===--- RuntimeDyldChecker.cpp - RuntimeDyld tester framework --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/RuntimeDyldChecker.h"
#include "RuntimeDyldCheckerImpl.h"
#include "RuntimeDyldImpl.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/MSVCErrorWorkarounds.h"
#include "llvm/Support/Path.h"
#include <cctype>
#include <future>
#include <memory>
#include <utility>

#define DEBUG_TYPE "rtdyld"

using namespace llvm;

namespace llvm {

// Helper class that implements the language evaluated by RuntimeDyldChecker.
class RuntimeDyldCheckerExprEval {
public:
  RuntimeDyldCheckerExprEval(const RuntimeDyldCheckerImpl &Checker,
                             raw_ostream &ErrStream)
      : Checker(Checker) {}

  bool evaluate(StringRef Expr) const {
    // Expect equality expression of the form 'LHS = RHS'.
    Expr = Expr.trim();
    size_t EQIdx = Expr.find('=');

    ParseContext OutsideLoad(false);

    // Evaluate LHS.
    StringRef LHSExpr = Expr.substr(0, EQIdx).rtrim();
    StringRef RemainingExpr;
    EvalResult LHSResult;
    std::tie(LHSResult, RemainingExpr) =
        evalComplexExpr(evalSimpleExpr(LHSExpr, OutsideLoad), OutsideLoad);
    if (LHSResult.hasError())
      return handleError(Expr, LHSResult);
    if (RemainingExpr != "")
      return handleError(Expr, unexpectedToken(RemainingExpr, LHSExpr, ""));

    // Evaluate RHS.
    StringRef RHSExpr = Expr.substr(EQIdx + 1).ltrim();
    EvalResult RHSResult;
    std::tie(RHSResult, RemainingExpr) =
        evalComplexExpr(evalSimpleExpr(RHSExpr, OutsideLoad), OutsideLoad);
    if (RHSResult.hasError())
      return handleError(Expr, RHSResult);
    if (RemainingExpr != "")
      return handleError(Expr, unexpectedToken(RemainingExpr, RHSExpr, ""));

    if (LHSResult.getValue() != RHSResult.getValue()) {
      Checker.ErrStream << "Expression '" << Expr << "' is false: "
                        << format("0x%" PRIx64, LHSResult.getValue())
                        << " != " << format("0x%" PRIx64, RHSResult.getValue())
                        << "\n";
      return false;
    }
    return true;
  }

private:
  // RuntimeDyldCheckerExprEval requires some context when parsing exprs. In
  // particular, it needs to know whether a symbol is being evaluated in the
  // context of a load, in which case we want the linker's local address for
  // the symbol, or outside of a load, in which case we want the symbol's
  // address in the remote target.

  struct ParseContext {
    bool IsInsideLoad;
    ParseContext(bool IsInsideLoad) : IsInsideLoad(IsInsideLoad) {}
  };

  const RuntimeDyldCheckerImpl &Checker;

  enum class BinOpToken : unsigned {
    Invalid,
    Add,
    Sub,
    BitwiseAnd,
    BitwiseOr,
    ShiftLeft,
    ShiftRight
  };

  class EvalResult {
  public:
    EvalResult() : Value(0), ErrorMsg("") {}
    EvalResult(uint64_t Value) : Value(Value), ErrorMsg("") {}
    EvalResult(std::string ErrorMsg)
        : Value(0), ErrorMsg(std::move(ErrorMsg)) {}
    uint64_t getValue() const { return Value; }
    bool hasError() const { return ErrorMsg != ""; }
    const std::string &getErrorMsg() const { return ErrorMsg; }

  private:
    uint64_t Value;
    std::string ErrorMsg;
  };

  StringRef getTokenForError(StringRef Expr) const {
    if (Expr.empty())
      return "";

    StringRef Token, Remaining;
    if (isalpha(Expr[0]))
      std::tie(Token, Remaining) = parseSymbol(Expr);
    else if (isdigit(Expr[0]))
      std::tie(Token, Remaining) = parseNumberString(Expr);
    else {
      unsigned TokLen = 1;
      if (Expr.startswith("<<") || Expr.startswith(">>"))
        TokLen = 2;
      Token = Expr.substr(0, TokLen);
    }
    return Token;
  }

  EvalResult unexpectedToken(StringRef TokenStart, StringRef SubExpr,
                             StringRef ErrText) const {
    std::string ErrorMsg("Encountered unexpected token '");
    ErrorMsg += getTokenForError(TokenStart);
    if (SubExpr != "") {
      ErrorMsg += "' while parsing subexpression '";
      ErrorMsg += SubExpr;
    }
    ErrorMsg += "'";
    if (ErrText != "") {
      ErrorMsg += " ";
      ErrorMsg += ErrText;
    }
    return EvalResult(std::move(ErrorMsg));
  }

  bool handleError(StringRef Expr, const EvalResult &R) const {
    assert(R.hasError() && "Not an error result.");
    Checker.ErrStream << "Error evaluating expression '" << Expr
                      << "': " << R.getErrorMsg() << "\n";
    return false;
  }

  std::pair<BinOpToken, StringRef> parseBinOpToken(StringRef Expr) const {
    if (Expr.empty())
      return std::make_pair(BinOpToken::Invalid, "");

    // Handle the two 2-character tokens.
    if (Expr.startswith("<<"))
      return std::make_pair(BinOpToken::ShiftLeft, Expr.substr(2).ltrim());
    if (Expr.startswith(">>"))
      return std::make_pair(BinOpToken::ShiftRight, Expr.substr(2).ltrim());

    // Handle one-character tokens.
    BinOpToken Op;
    switch (Expr[0]) {
    default:
      return std::make_pair(BinOpToken::Invalid, Expr);
    case '+':
      Op = BinOpToken::Add;
      break;
    case '-':
      Op = BinOpToken::Sub;
      break;
    case '&':
      Op = BinOpToken::BitwiseAnd;
      break;
    case '|':
      Op = BinOpToken::BitwiseOr;
      break;
    }

    return std::make_pair(Op, Expr.substr(1).ltrim());
  }

  EvalResult computeBinOpResult(BinOpToken Op, const EvalResult &LHSResult,
                                const EvalResult &RHSResult) const {
    switch (Op) {
    default:
      llvm_unreachable("Tried to evaluate unrecognized operation.");
    case BinOpToken::Add:
      return EvalResult(LHSResult.getValue() + RHSResult.getValue());
    case BinOpToken::Sub:
      return EvalResult(LHSResult.getValue() - RHSResult.getValue());
    case BinOpToken::BitwiseAnd:
      return EvalResult(LHSResult.getValue() & RHSResult.getValue());
    case BinOpToken::BitwiseOr:
      return EvalResult(LHSResult.getValue() | RHSResult.getValue());
    case BinOpToken::ShiftLeft:
      return EvalResult(LHSResult.getValue() << RHSResult.getValue());
    case BinOpToken::ShiftRight:
      return EvalResult(LHSResult.getValue() >> RHSResult.getValue());
    }
  }

  // Parse a symbol and return a (string, string) pair representing the symbol
  // name and expression remaining to be parsed.
  std::pair<StringRef, StringRef> parseSymbol(StringRef Expr) const {
    size_t FirstNonSymbol = Expr.find_first_not_of("0123456789"
                                                   "abcdefghijklmnopqrstuvwxyz"
                                                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                                   ":_.$");
    return std::make_pair(Expr.substr(0, FirstNonSymbol),
                          Expr.substr(FirstNonSymbol).ltrim());
  }

  // Evaluate a call to decode_operand. Decode the instruction operand at the
  // given symbol and get the value of the requested operand.
  // Returns an error if the instruction cannot be decoded, or the requested
  // operand is not an immediate.
  // On success, returns a pair containing the value of the operand, plus
  // the expression remaining to be evaluated.
  std::pair<EvalResult, StringRef> evalDecodeOperand(StringRef Expr) const {
    if (!Expr.startswith("("))
      return std::make_pair(unexpectedToken(Expr, Expr, "expected '('"), "");
    StringRef RemainingExpr = Expr.substr(1).ltrim();
    StringRef Symbol;
    std::tie(Symbol, RemainingExpr) = parseSymbol(RemainingExpr);

    if (!Checker.isSymbolValid(Symbol))
      return std::make_pair(
          EvalResult(("Cannot decode unknown symbol '" + Symbol + "'").str()),
          "");

    if (!RemainingExpr.startswith(","))
      return std::make_pair(
          unexpectedToken(RemainingExpr, RemainingExpr, "expected ','"), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    EvalResult OpIdxExpr;
    std::tie(OpIdxExpr, RemainingExpr) = evalNumberExpr(RemainingExpr);
    if (OpIdxExpr.hasError())
      return std::make_pair(OpIdxExpr, "");

    if (!RemainingExpr.startswith(")"))
      return std::make_pair(
          unexpectedToken(RemainingExpr, RemainingExpr, "expected ')'"), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    MCInst Inst;
    uint64_t Size;
    if (!decodeInst(Symbol, Inst, Size))
      return std::make_pair(
          EvalResult(("Couldn't decode instruction at '" + Symbol + "'").str()),
          "");

    unsigned OpIdx = OpIdxExpr.getValue();
    if (OpIdx >= Inst.getNumOperands()) {
      std::string ErrMsg;
      raw_string_ostream ErrMsgStream(ErrMsg);
      ErrMsgStream << "Invalid operand index '" << format("%i", OpIdx)
                   << "' for instruction '" << Symbol
                   << "'. Instruction has only "
                   << format("%i", Inst.getNumOperands())
                   << " operands.\nInstruction is:\n  ";
      Inst.dump_pretty(ErrMsgStream, Checker.InstPrinter);
      return std::make_pair(EvalResult(ErrMsgStream.str()), "");
    }

    const MCOperand &Op = Inst.getOperand(OpIdx);
    if (!Op.isImm()) {
      std::string ErrMsg;
      raw_string_ostream ErrMsgStream(ErrMsg);
      ErrMsgStream << "Operand '" << format("%i", OpIdx) << "' of instruction '"
                   << Symbol << "' is not an immediate.\nInstruction is:\n  ";
      Inst.dump_pretty(ErrMsgStream, Checker.InstPrinter);

      return std::make_pair(EvalResult(ErrMsgStream.str()), "");
    }

    return std::make_pair(EvalResult(Op.getImm()), RemainingExpr);
  }

  // Evaluate a call to next_pc.
  // Decode the instruction at the given symbol and return the following program
  // counter.
  // Returns an error if the instruction cannot be decoded.
  // On success, returns a pair containing the next PC, plus of the
  // expression remaining to be evaluated.
  std::pair<EvalResult, StringRef> evalNextPC(StringRef Expr,
                                              ParseContext PCtx) const {
    if (!Expr.startswith("("))
      return std::make_pair(unexpectedToken(Expr, Expr, "expected '('"), "");
    StringRef RemainingExpr = Expr.substr(1).ltrim();
    StringRef Symbol;
    std::tie(Symbol, RemainingExpr) = parseSymbol(RemainingExpr);

    if (!Checker.isSymbolValid(Symbol))
      return std::make_pair(
          EvalResult(("Cannot decode unknown symbol '" + Symbol + "'").str()),
          "");

    if (!RemainingExpr.startswith(")"))
      return std::make_pair(
          unexpectedToken(RemainingExpr, RemainingExpr, "expected ')'"), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    MCInst Inst;
    uint64_t InstSize;
    if (!decodeInst(Symbol, Inst, InstSize))
      return std::make_pair(
          EvalResult(("Couldn't decode instruction at '" + Symbol + "'").str()),
          "");

    uint64_t SymbolAddr = PCtx.IsInsideLoad
                              ? Checker.getSymbolLocalAddr(Symbol)
                              : Checker.getSymbolRemoteAddr(Symbol);
    uint64_t NextPC = SymbolAddr + InstSize;

    return std::make_pair(EvalResult(NextPC), RemainingExpr);
  }

  // Evaluate a call to stub_addr.
  // Look up and return the address of the stub for the given
  // (<file name>, <section name>, <symbol name>) tuple.
  // On success, returns a pair containing the stub address, plus the expression
  // remaining to be evaluated.
  std::pair<EvalResult, StringRef> evalStubAddr(StringRef Expr,
                                                ParseContext PCtx) const {
    if (!Expr.startswith("("))
      return std::make_pair(unexpectedToken(Expr, Expr, "expected '('"), "");
    StringRef RemainingExpr = Expr.substr(1).ltrim();

    // Handle file-name specially, as it may contain characters that aren't
    // legal for symbols.
    StringRef FileName;
    size_t ComaIdx = RemainingExpr.find(',');
    FileName = RemainingExpr.substr(0, ComaIdx).rtrim();
    RemainingExpr = RemainingExpr.substr(ComaIdx).ltrim();

    if (!RemainingExpr.startswith(","))
      return std::make_pair(
          unexpectedToken(RemainingExpr, Expr, "expected ','"), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    StringRef SectionName;
    std::tie(SectionName, RemainingExpr) = parseSymbol(RemainingExpr);

    if (!RemainingExpr.startswith(","))
      return std::make_pair(
          unexpectedToken(RemainingExpr, Expr, "expected ','"), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    StringRef Symbol;
    std::tie(Symbol, RemainingExpr) = parseSymbol(RemainingExpr);

    if (!RemainingExpr.startswith(")"))
      return std::make_pair(
          unexpectedToken(RemainingExpr, Expr, "expected ')'"), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    uint64_t StubAddr;
    std::string ErrorMsg = "";
    std::tie(StubAddr, ErrorMsg) = Checker.getStubAddrFor(
        FileName, SectionName, Symbol, PCtx.IsInsideLoad);

    if (ErrorMsg != "")
      return std::make_pair(EvalResult(ErrorMsg), "");

    return std::make_pair(EvalResult(StubAddr), RemainingExpr);
  }

  std::pair<EvalResult, StringRef> evalSectionAddr(StringRef Expr,
                                                   ParseContext PCtx) const {
    if (!Expr.startswith("("))
      return std::make_pair(unexpectedToken(Expr, Expr, "expected '('"), "");
    StringRef RemainingExpr = Expr.substr(1).ltrim();

    // Handle file-name specially, as it may contain characters that aren't
    // legal for symbols.
    StringRef FileName;
    size_t ComaIdx = RemainingExpr.find(',');
    FileName = RemainingExpr.substr(0, ComaIdx).rtrim();
    RemainingExpr = RemainingExpr.substr(ComaIdx).ltrim();

    if (!RemainingExpr.startswith(","))
      return std::make_pair(
          unexpectedToken(RemainingExpr, Expr, "expected ','"), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    StringRef SectionName;
    std::tie(SectionName, RemainingExpr) = parseSymbol(RemainingExpr);

    if (!RemainingExpr.startswith(")"))
      return std::make_pair(
          unexpectedToken(RemainingExpr, Expr, "expected ')'"), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    uint64_t StubAddr;
    std::string ErrorMsg = "";
    std::tie(StubAddr, ErrorMsg) = Checker.getSectionAddr(
        FileName, SectionName, PCtx.IsInsideLoad);

    if (ErrorMsg != "")
      return std::make_pair(EvalResult(ErrorMsg), "");

    return std::make_pair(EvalResult(StubAddr), RemainingExpr);
  }

  // Evaluate an identiefer expr, which may be a symbol, or a call to
  // one of the builtin functions: get_insn_opcode or get_insn_length.
  // Return the result, plus the expression remaining to be parsed.
  std::pair<EvalResult, StringRef> evalIdentifierExpr(StringRef Expr,
                                                      ParseContext PCtx) const {
    StringRef Symbol;
    StringRef RemainingExpr;
    std::tie(Symbol, RemainingExpr) = parseSymbol(Expr);

    // Check for builtin function calls.
    if (Symbol == "decode_operand")
      return evalDecodeOperand(RemainingExpr);
    else if (Symbol == "next_pc")
      return evalNextPC(RemainingExpr, PCtx);
    else if (Symbol == "stub_addr")
      return evalStubAddr(RemainingExpr, PCtx);
    else if (Symbol == "section_addr")
      return evalSectionAddr(RemainingExpr, PCtx);

    if (!Checker.isSymbolValid(Symbol)) {
      std::string ErrMsg("No known address for symbol '");
      ErrMsg += Symbol;
      ErrMsg += "'";
      if (Symbol.startswith("L"))
        ErrMsg += " (this appears to be an assembler local label - "
                  " perhaps drop the 'L'?)";

      return std::make_pair(EvalResult(ErrMsg), "");
    }

    // The value for the symbol depends on the context we're evaluating in:
    // Inside a load this is the address in the linker's memory, outside a
    // load it's the address in the target processes memory.
    uint64_t Value = PCtx.IsInsideLoad ? Checker.getSymbolLocalAddr(Symbol)
                                       : Checker.getSymbolRemoteAddr(Symbol);

    // Looks like a plain symbol reference.
    return std::make_pair(EvalResult(Value), RemainingExpr);
  }

  // Parse a number (hexadecimal or decimal) and return a (string, string)
  // pair representing the number and the expression remaining to be parsed.
  std::pair<StringRef, StringRef> parseNumberString(StringRef Expr) const {
    size_t FirstNonDigit = StringRef::npos;
    if (Expr.startswith("0x")) {
      FirstNonDigit = Expr.find_first_not_of("0123456789abcdefABCDEF", 2);
      if (FirstNonDigit == StringRef::npos)
        FirstNonDigit = Expr.size();
    } else {
      FirstNonDigit = Expr.find_first_not_of("0123456789");
      if (FirstNonDigit == StringRef::npos)
        FirstNonDigit = Expr.size();
    }
    return std::make_pair(Expr.substr(0, FirstNonDigit),
                          Expr.substr(FirstNonDigit));
  }

  // Evaluate a constant numeric expression (hexadecimal or decimal) and
  // return a pair containing the result, and the expression remaining to be
  // evaluated.
  std::pair<EvalResult, StringRef> evalNumberExpr(StringRef Expr) const {
    StringRef ValueStr;
    StringRef RemainingExpr;
    std::tie(ValueStr, RemainingExpr) = parseNumberString(Expr);

    if (ValueStr.empty() || !isdigit(ValueStr[0]))
      return std::make_pair(
          unexpectedToken(RemainingExpr, RemainingExpr, "expected number"), "");
    uint64_t Value;
    ValueStr.getAsInteger(0, Value);
    return std::make_pair(EvalResult(Value), RemainingExpr);
  }

  // Evaluate an expression of the form "(<expr>)" and return a pair
  // containing the result of evaluating <expr>, plus the expression
  // remaining to be parsed.
  std::pair<EvalResult, StringRef> evalParensExpr(StringRef Expr,
                                                  ParseContext PCtx) const {
    assert(Expr.startswith("(") && "Not a parenthesized expression");
    EvalResult SubExprResult;
    StringRef RemainingExpr;
    std::tie(SubExprResult, RemainingExpr) =
        evalComplexExpr(evalSimpleExpr(Expr.substr(1).ltrim(), PCtx), PCtx);
    if (SubExprResult.hasError())
      return std::make_pair(SubExprResult, "");
    if (!RemainingExpr.startswith(")"))
      return std::make_pair(
          unexpectedToken(RemainingExpr, Expr, "expected ')'"), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();
    return std::make_pair(SubExprResult, RemainingExpr);
  }

  // Evaluate an expression in one of the following forms:
  //   *{<number>}<expr>
  // Return a pair containing the result, plus the expression remaining to be
  // parsed.
  std::pair<EvalResult, StringRef> evalLoadExpr(StringRef Expr) const {
    assert(Expr.startswith("*") && "Not a load expression");
    StringRef RemainingExpr = Expr.substr(1).ltrim();

    // Parse read size.
    if (!RemainingExpr.startswith("{"))
      return std::make_pair(EvalResult("Expected '{' following '*'."), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();
    EvalResult ReadSizeExpr;
    std::tie(ReadSizeExpr, RemainingExpr) = evalNumberExpr(RemainingExpr);
    if (ReadSizeExpr.hasError())
      return std::make_pair(ReadSizeExpr, RemainingExpr);
    uint64_t ReadSize = ReadSizeExpr.getValue();
    if (ReadSize < 1 || ReadSize > 8)
      return std::make_pair(EvalResult("Invalid size for dereference."), "");
    if (!RemainingExpr.startswith("}"))
      return std::make_pair(EvalResult("Missing '}' for dereference."), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    // Evaluate the expression representing the load address.
    ParseContext LoadCtx(true);
    EvalResult LoadAddrExprResult;
    std::tie(LoadAddrExprResult, RemainingExpr) =
        evalComplexExpr(evalSimpleExpr(RemainingExpr, LoadCtx), LoadCtx);

    if (LoadAddrExprResult.hasError())
      return std::make_pair(LoadAddrExprResult, "");

    uint64_t LoadAddr = LoadAddrExprResult.getValue();

    return std::make_pair(
        EvalResult(Checker.readMemoryAtAddr(LoadAddr, ReadSize)),
        RemainingExpr);
  }

  // Evaluate a "simple" expression. This is any expression that _isn't_ an
  // un-parenthesized binary expression.
  //
  // "Simple" expressions can be optionally bit-sliced. See evalSlicedExpr.
  //
  // Returns a pair containing the result of the evaluation, plus the
  // expression remaining to be parsed.
  std::pair<EvalResult, StringRef> evalSimpleExpr(StringRef Expr,
                                                  ParseContext PCtx) const {
    EvalResult SubExprResult;
    StringRef RemainingExpr;

    if (Expr.empty())
      return std::make_pair(EvalResult("Unexpected end of expression"), "");

    if (Expr[0] == '(')
      std::tie(SubExprResult, RemainingExpr) = evalParensExpr(Expr, PCtx);
    else if (Expr[0] == '*')
      std::tie(SubExprResult, RemainingExpr) = evalLoadExpr(Expr);
    else if (isalpha(Expr[0]) || Expr[0] == '_')
      std::tie(SubExprResult, RemainingExpr) = evalIdentifierExpr(Expr, PCtx);
    else if (isdigit(Expr[0]))
      std::tie(SubExprResult, RemainingExpr) = evalNumberExpr(Expr);
    else
      return std::make_pair(
          unexpectedToken(Expr, Expr,
                          "expected '(', '*', identifier, or number"), "");

    if (SubExprResult.hasError())
      return std::make_pair(SubExprResult, RemainingExpr);

    // Evaluate bit-slice if present.
    if (RemainingExpr.startswith("["))
      std::tie(SubExprResult, RemainingExpr) =
          evalSliceExpr(std::make_pair(SubExprResult, RemainingExpr));

    return std::make_pair(SubExprResult, RemainingExpr);
  }

  // Evaluate a bit-slice of an expression.
  // A bit-slice has the form "<expr>[high:low]". The result of evaluating a
  // slice is the bits between high and low (inclusive) in the original
  // expression, right shifted so that the "low" bit is in position 0 in the
  // result.
  // Returns a pair containing the result of the slice operation, plus the
  // expression remaining to be parsed.
  std::pair<EvalResult, StringRef>
  evalSliceExpr(const std::pair<EvalResult, StringRef> &Ctx) const {
    EvalResult SubExprResult;
    StringRef RemainingExpr;
    std::tie(SubExprResult, RemainingExpr) = Ctx;

    assert(RemainingExpr.startswith("[") && "Not a slice expr.");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    EvalResult HighBitExpr;
    std::tie(HighBitExpr, RemainingExpr) = evalNumberExpr(RemainingExpr);

    if (HighBitExpr.hasError())
      return std::make_pair(HighBitExpr, RemainingExpr);

    if (!RemainingExpr.startswith(":"))
      return std::make_pair(
          unexpectedToken(RemainingExpr, RemainingExpr, "expected ':'"), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    EvalResult LowBitExpr;
    std::tie(LowBitExpr, RemainingExpr) = evalNumberExpr(RemainingExpr);

    if (LowBitExpr.hasError())
      return std::make_pair(LowBitExpr, RemainingExpr);

    if (!RemainingExpr.startswith("]"))
      return std::make_pair(
          unexpectedToken(RemainingExpr, RemainingExpr, "expected ']'"), "");
    RemainingExpr = RemainingExpr.substr(1).ltrim();

    unsigned HighBit = HighBitExpr.getValue();
    unsigned LowBit = LowBitExpr.getValue();
    uint64_t Mask = ((uint64_t)1 << (HighBit - LowBit + 1)) - 1;
    uint64_t SlicedValue = (SubExprResult.getValue() >> LowBit) & Mask;
    return std::make_pair(EvalResult(SlicedValue), RemainingExpr);
  }

  // Evaluate a "complex" expression.
  // Takes an already evaluated subexpression and checks for the presence of a
  // binary operator, computing the result of the binary operation if one is
  // found. Used to make arithmetic expressions left-associative.
  // Returns a pair containing the ultimate result of evaluating the
  // expression, plus the expression remaining to be evaluated.
  std::pair<EvalResult, StringRef>
  evalComplexExpr(const std::pair<EvalResult, StringRef> &LHSAndRemaining,
                  ParseContext PCtx) const {
    EvalResult LHSResult;
    StringRef RemainingExpr;
    std::tie(LHSResult, RemainingExpr) = LHSAndRemaining;

    // If there was an error, or there's nothing left to evaluate, return the
    // result.
    if (LHSResult.hasError() || RemainingExpr == "")
      return std::make_pair(LHSResult, RemainingExpr);

    // Otherwise check if this is a binary expressioan.
    BinOpToken BinOp;
    std::tie(BinOp, RemainingExpr) = parseBinOpToken(RemainingExpr);

    // If this isn't a recognized expression just return.
    if (BinOp == BinOpToken::Invalid)
      return std::make_pair(LHSResult, RemainingExpr);

    // This is a recognized bin-op. Evaluate the RHS, then evaluate the binop.
    EvalResult RHSResult;
    std::tie(RHSResult, RemainingExpr) = evalSimpleExpr(RemainingExpr, PCtx);

    // If there was an error evaluating the RHS, return it.
    if (RHSResult.hasError())
      return std::make_pair(RHSResult, RemainingExpr);

    // This is a binary expression - evaluate and try to continue as a
    // complex expr.
    EvalResult ThisResult(computeBinOpResult(BinOp, LHSResult, RHSResult));

    return evalComplexExpr(std::make_pair(ThisResult, RemainingExpr), PCtx);
  }

  bool decodeInst(StringRef Symbol, MCInst &Inst, uint64_t &Size) const {
    MCDisassembler *Dis = Checker.Disassembler;
    StringRef SectionMem = Checker.getSubsectionStartingAt(Symbol);
    ArrayRef<uint8_t> SectionBytes(
        reinterpret_cast<const uint8_t *>(SectionMem.data()),
        SectionMem.size());

    MCDisassembler::DecodeStatus S =
        Dis->getInstruction(Inst, Size, SectionBytes, 0, nulls(), nulls());

    return (S == MCDisassembler::Success);
  }
};
}

RuntimeDyldCheckerImpl::RuntimeDyldCheckerImpl(RuntimeDyld &RTDyld,
                                               MCDisassembler *Disassembler,
                                               MCInstPrinter *InstPrinter,
                                               raw_ostream &ErrStream)
    : RTDyld(RTDyld), Disassembler(Disassembler), InstPrinter(InstPrinter),
      ErrStream(ErrStream) {
  RTDyld.Checker = this;
}

bool RuntimeDyldCheckerImpl::check(StringRef CheckExpr) const {
  CheckExpr = CheckExpr.trim();
  LLVM_DEBUG(dbgs() << "RuntimeDyldChecker: Checking '" << CheckExpr
                    << "'...\n");
  RuntimeDyldCheckerExprEval P(*this, ErrStream);
  bool Result = P.evaluate(CheckExpr);
  (void)Result;
  LLVM_DEBUG(dbgs() << "RuntimeDyldChecker: '" << CheckExpr << "' "
                    << (Result ? "passed" : "FAILED") << ".\n");
  return Result;
}

bool RuntimeDyldCheckerImpl::checkAllRulesInBuffer(StringRef RulePrefix,
                                                   MemoryBuffer *MemBuf) const {
  bool DidAllTestsPass = true;
  unsigned NumRules = 0;

  const char *LineStart = MemBuf->getBufferStart();

  // Eat whitespace.
  while (LineStart != MemBuf->getBufferEnd() && std::isspace(*LineStart))
    ++LineStart;

  while (LineStart != MemBuf->getBufferEnd() && *LineStart != '\0') {
    const char *LineEnd = LineStart;
    while (LineEnd != MemBuf->getBufferEnd() && *LineEnd != '\r' &&
           *LineEnd != '\n')
      ++LineEnd;

    StringRef Line(LineStart, LineEnd - LineStart);
    if (Line.startswith(RulePrefix)) {
      DidAllTestsPass &= check(Line.substr(RulePrefix.size()));
      ++NumRules;
    }

    // Eat whitespace.
    LineStart = LineEnd;
    while (LineStart != MemBuf->getBufferEnd() && std::isspace(*LineStart))
      ++LineStart;
  }
  return DidAllTestsPass && (NumRules != 0);
}

Expected<JITSymbolResolver::LookupResult> RuntimeDyldCheckerImpl::lookup(
    const JITSymbolResolver::LookupSet &Symbols) const {

#ifdef _MSC_VER
  using ExpectedLookupResult = MSVCPExpected<JITSymbolResolver::LookupResult>;
#else
  using ExpectedLookupResult = Expected<JITSymbolResolver::LookupResult>;
#endif

  auto ResultP = std::make_shared<std::promise<ExpectedLookupResult>>();
  auto ResultF = ResultP->get_future();

  getRTDyld().Resolver.lookup(
      Symbols, [=](Expected<JITSymbolResolver::LookupResult> Result) {
        ResultP->set_value(std::move(Result));
      });
  return ResultF.get();
}

bool RuntimeDyldCheckerImpl::isSymbolValid(StringRef Symbol) const {
  if (getRTDyld().getSymbol(Symbol))
    return true;
  auto Result = lookup({Symbol});

  if (!Result) {
    logAllUnhandledErrors(Result.takeError(), errs(), "RTDyldChecker: ");
    return false;
  }

  assert(Result->count(Symbol) && "Missing symbol result");
  return true;
}

uint64_t RuntimeDyldCheckerImpl::getSymbolLocalAddr(StringRef Symbol) const {
  return static_cast<uint64_t>(
      reinterpret_cast<uintptr_t>(getRTDyld().getSymbolLocalAddress(Symbol)));
}

uint64_t RuntimeDyldCheckerImpl::getSymbolRemoteAddr(StringRef Symbol) const {
  if (auto InternalSymbol = getRTDyld().getSymbol(Symbol))
    return InternalSymbol.getAddress();

  auto Result = lookup({Symbol});
  if (!Result) {
    logAllUnhandledErrors(Result.takeError(), errs(), "RTDyldChecker: ");
    return 0;
  }
  auto I = Result->find(Symbol);
  assert(I != Result->end() && "Missing symbol result");
  return I->second.getAddress();
}

uint64_t RuntimeDyldCheckerImpl::readMemoryAtAddr(uint64_t SrcAddr,
                                                  unsigned Size) const {
  uintptr_t PtrSizedAddr = static_cast<uintptr_t>(SrcAddr);
  assert(PtrSizedAddr == SrcAddr && "Linker memory pointer out-of-range.");
  uint8_t *Src = reinterpret_cast<uint8_t*>(PtrSizedAddr);
  return getRTDyld().readBytesUnaligned(Src, Size);
}


std::pair<const RuntimeDyldCheckerImpl::SectionAddressInfo*, std::string>
RuntimeDyldCheckerImpl::findSectionAddrInfo(StringRef FileName,
                                            StringRef SectionName) const {

  auto SectionMapItr = Stubs.find(FileName);
  if (SectionMapItr == Stubs.end()) {
    std::string ErrorMsg = "File '";
    ErrorMsg += FileName;
    ErrorMsg += "' not found. ";
    if (Stubs.empty())
      ErrorMsg += "No stubs registered.";
    else {
      ErrorMsg += "Available files are:";
      for (const auto& StubEntry : Stubs) {
        ErrorMsg += " '";
        ErrorMsg += StubEntry.first;
        ErrorMsg += "'";
      }
    }
    ErrorMsg += "\n";
    return std::make_pair(nullptr, ErrorMsg);
  }

  auto SectionInfoItr = SectionMapItr->second.find(SectionName);
  if (SectionInfoItr == SectionMapItr->second.end())
    return std::make_pair(nullptr,
                          ("Section '" + SectionName + "' not found in file '" +
                           FileName + "'\n").str());

  return std::make_pair(&SectionInfoItr->second, std::string(""));
}

std::pair<uint64_t, std::string> RuntimeDyldCheckerImpl::getSectionAddr(
    StringRef FileName, StringRef SectionName, bool IsInsideLoad) const {

  const SectionAddressInfo *SectionInfo = nullptr;
  {
    std::string ErrorMsg;
    std::tie(SectionInfo, ErrorMsg) =
      findSectionAddrInfo(FileName, SectionName);
    if (ErrorMsg != "")
      return std::make_pair(0, ErrorMsg);
  }

  unsigned SectionID = SectionInfo->SectionID;
  uint64_t Addr;
  if (IsInsideLoad)
    Addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(
        getRTDyld().Sections[SectionID].getAddress()));
  else
    Addr = getRTDyld().Sections[SectionID].getLoadAddress();

  return std::make_pair(Addr, std::string(""));
}

std::pair<uint64_t, std::string> RuntimeDyldCheckerImpl::getStubAddrFor(
    StringRef FileName, StringRef SectionName, StringRef SymbolName,
    bool IsInsideLoad) const {

  const SectionAddressInfo *SectionInfo = nullptr;
  {
    std::string ErrorMsg;
    std::tie(SectionInfo, ErrorMsg) =
      findSectionAddrInfo(FileName, SectionName);
    if (ErrorMsg != "")
      return std::make_pair(0, ErrorMsg);
  }

  unsigned SectionID = SectionInfo->SectionID;
  const StubOffsetsMap &SymbolStubs = SectionInfo->StubOffsets;
  auto StubOffsetItr = SymbolStubs.find(SymbolName);
  if (StubOffsetItr == SymbolStubs.end())
    return std::make_pair(0,
                          ("Stub for symbol '" + SymbolName + "' not found. "
                           "If '" + SymbolName + "' is an internal symbol this "
                           "may indicate that the stub target offset is being "
                           "computed incorrectly.\n").str());

  uint64_t StubOffset = StubOffsetItr->second;

  uint64_t Addr;
  if (IsInsideLoad) {
    uintptr_t SectionBase = reinterpret_cast<uintptr_t>(
        getRTDyld().Sections[SectionID].getAddress());
    Addr = static_cast<uint64_t>(SectionBase) + StubOffset;
  } else {
    uint64_t SectionBase = getRTDyld().Sections[SectionID].getLoadAddress();
    Addr = SectionBase + StubOffset;
  }

  return std::make_pair(Addr, std::string(""));
}

StringRef
RuntimeDyldCheckerImpl::getSubsectionStartingAt(StringRef Name) const {
  RTDyldSymbolTable::const_iterator pos =
      getRTDyld().GlobalSymbolTable.find(Name);
  if (pos == getRTDyld().GlobalSymbolTable.end())
    return StringRef();
  const auto &SymInfo = pos->second;
  uint8_t *SectionAddr = getRTDyld().getSectionAddress(SymInfo.getSectionID());
  return StringRef(reinterpret_cast<const char *>(SectionAddr) +
                       SymInfo.getOffset(),
                   getRTDyld().Sections[SymInfo.getSectionID()].getSize() -
                       SymInfo.getOffset());
}

Optional<uint64_t>
RuntimeDyldCheckerImpl::getSectionLoadAddress(void *LocalAddress) const {
  for (auto &S : getRTDyld().Sections) {
    if (S.getAddress() == LocalAddress)
      return S.getLoadAddress();
  }
  return Optional<uint64_t>();
}

void RuntimeDyldCheckerImpl::registerSection(
    StringRef FilePath, unsigned SectionID) {
  StringRef FileName = sys::path::filename(FilePath);
  const SectionEntry &Section = getRTDyld().Sections[SectionID];
  StringRef SectionName = Section.getName();

  Stubs[FileName][SectionName].SectionID = SectionID;
}

void RuntimeDyldCheckerImpl::registerStubMap(
    StringRef FilePath, unsigned SectionID,
    const RuntimeDyldImpl::StubMap &RTDyldStubs) {
  StringRef FileName = sys::path::filename(FilePath);
  const SectionEntry &Section = getRTDyld().Sections[SectionID];
  StringRef SectionName = Section.getName();

  Stubs[FileName][SectionName].SectionID = SectionID;

  for (auto &StubMapEntry : RTDyldStubs) {
    std::string SymbolName = "";

    if (StubMapEntry.first.SymbolName)
      SymbolName = StubMapEntry.first.SymbolName;
    else {
      // If this is a (Section, Offset) pair, do a reverse lookup in the
      // global symbol table to find the name.
      for (auto &GSTEntry : getRTDyld().GlobalSymbolTable) {
        const auto &SymInfo = GSTEntry.second;
        if (SymInfo.getSectionID() == StubMapEntry.first.SectionID &&
            SymInfo.getOffset() ==
              static_cast<uint64_t>(StubMapEntry.first.Offset)) {
          SymbolName = GSTEntry.first();
          break;
        }
      }
    }

    if (SymbolName != "")
      Stubs[FileName][SectionName].StubOffsets[SymbolName] =
        StubMapEntry.second;
  }
}

RuntimeDyldChecker::RuntimeDyldChecker(RuntimeDyld &RTDyld,
                                       MCDisassembler *Disassembler,
                                       MCInstPrinter *InstPrinter,
                                       raw_ostream &ErrStream)
    : Impl(make_unique<RuntimeDyldCheckerImpl>(RTDyld, Disassembler,
                                               InstPrinter, ErrStream)) {}

RuntimeDyldChecker::~RuntimeDyldChecker() {}

RuntimeDyld& RuntimeDyldChecker::getRTDyld() {
  return Impl->RTDyld;
}

const RuntimeDyld& RuntimeDyldChecker::getRTDyld() const {
  return Impl->RTDyld;
}

bool RuntimeDyldChecker::check(StringRef CheckExpr) const {
  return Impl->check(CheckExpr);
}

bool RuntimeDyldChecker::checkAllRulesInBuffer(StringRef RulePrefix,
                                               MemoryBuffer *MemBuf) const {
  return Impl->checkAllRulesInBuffer(RulePrefix, MemBuf);
}

std::pair<uint64_t, std::string>
RuntimeDyldChecker::getSectionAddr(StringRef FileName, StringRef SectionName,
                                   bool LocalAddress) {
  return Impl->getSectionAddr(FileName, SectionName, LocalAddress);
}

Optional<uint64_t>
RuntimeDyldChecker::getSectionLoadAddress(void *LocalAddress) const {
  return Impl->getSectionLoadAddress(LocalAddress);
}
