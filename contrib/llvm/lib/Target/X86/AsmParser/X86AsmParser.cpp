//===-- X86AsmParser.cpp - Parse X86 assembly to MCInst instructions ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "InstPrinter/X86IntelInstPrinter.h"
#include "MCTargetDesc/X86BaseInfo.h"
#include "MCTargetDesc/X86MCExpr.h"
#include "MCTargetDesc/X86TargetStreamer.h"
#include "X86AsmInstrumentation.h"
#include "X86AsmParserCommon.h"
#include "X86Operand.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <memory>

using namespace llvm;

static bool checkScale(unsigned Scale, StringRef &ErrMsg) {
  if (Scale != 1 && Scale != 2 && Scale != 4 && Scale != 8) {
    ErrMsg = "scale factor in address must be 1, 2, 4 or 8";
    return true;
  }
  return false;
}

namespace {

static const char OpPrecedence[] = {
  0, // IC_OR
  1, // IC_XOR
  2, // IC_AND
  3, // IC_LSHIFT
  3, // IC_RSHIFT
  4, // IC_PLUS
  4, // IC_MINUS
  5, // IC_MULTIPLY
  5, // IC_DIVIDE
  5, // IC_MOD
  6, // IC_NOT
  7, // IC_NEG
  8, // IC_RPAREN
  9, // IC_LPAREN
  0, // IC_IMM
  0  // IC_REGISTER
};

class X86AsmParser : public MCTargetAsmParser {
  ParseInstructionInfo *InstInfo;
  std::unique_ptr<X86AsmInstrumentation> Instrumentation;
  bool Code16GCC;

private:
  SMLoc consumeToken() {
    MCAsmParser &Parser = getParser();
    SMLoc Result = Parser.getTok().getLoc();
    Parser.Lex();
    return Result;
  }

  X86TargetStreamer &getTargetStreamer() {
    assert(getParser().getStreamer().getTargetStreamer() &&
           "do not have a target streamer");
    MCTargetStreamer &TS = *getParser().getStreamer().getTargetStreamer();
    return static_cast<X86TargetStreamer &>(TS);
  }

  unsigned MatchInstruction(const OperandVector &Operands, MCInst &Inst,
                            uint64_t &ErrorInfo, bool matchingInlineAsm,
                            unsigned VariantID = 0) {
    // In Code16GCC mode, match as 32-bit.
    if (Code16GCC)
      SwitchMode(X86::Mode32Bit);
    unsigned rv = MatchInstructionImpl(Operands, Inst, ErrorInfo,
                                       matchingInlineAsm, VariantID);
    if (Code16GCC)
      SwitchMode(X86::Mode16Bit);
    return rv;
  }

  enum InfixCalculatorTok {
    IC_OR = 0,
    IC_XOR,
    IC_AND,
    IC_LSHIFT,
    IC_RSHIFT,
    IC_PLUS,
    IC_MINUS,
    IC_MULTIPLY,
    IC_DIVIDE,
    IC_MOD,
    IC_NOT,
    IC_NEG,
    IC_RPAREN,
    IC_LPAREN,
    IC_IMM,
    IC_REGISTER
  };

  enum IntelOperatorKind {
    IOK_INVALID = 0,
    IOK_LENGTH,
    IOK_SIZE,
    IOK_TYPE,
    IOK_OFFSET
  };

  class InfixCalculator {
    typedef std::pair< InfixCalculatorTok, int64_t > ICToken;
    SmallVector<InfixCalculatorTok, 4> InfixOperatorStack;
    SmallVector<ICToken, 4> PostfixStack;

    bool isUnaryOperator(const InfixCalculatorTok Op) {
      return Op == IC_NEG || Op == IC_NOT;
    }

  public:
    int64_t popOperand() {
      assert (!PostfixStack.empty() && "Poped an empty stack!");
      ICToken Op = PostfixStack.pop_back_val();
      if (!(Op.first == IC_IMM || Op.first == IC_REGISTER))
        return -1; // The invalid Scale value will be caught later by checkScale
      return Op.second;
    }
    void pushOperand(InfixCalculatorTok Op, int64_t Val = 0) {
      assert ((Op == IC_IMM || Op == IC_REGISTER) &&
              "Unexpected operand!");
      PostfixStack.push_back(std::make_pair(Op, Val));
    }

    void popOperator() { InfixOperatorStack.pop_back(); }
    void pushOperator(InfixCalculatorTok Op) {
      // Push the new operator if the stack is empty.
      if (InfixOperatorStack.empty()) {
        InfixOperatorStack.push_back(Op);
        return;
      }

      // Push the new operator if it has a higher precedence than the operator
      // on the top of the stack or the operator on the top of the stack is a
      // left parentheses.
      unsigned Idx = InfixOperatorStack.size() - 1;
      InfixCalculatorTok StackOp = InfixOperatorStack[Idx];
      if (OpPrecedence[Op] > OpPrecedence[StackOp] || StackOp == IC_LPAREN) {
        InfixOperatorStack.push_back(Op);
        return;
      }

      // The operator on the top of the stack has higher precedence than the
      // new operator.
      unsigned ParenCount = 0;
      while (1) {
        // Nothing to process.
        if (InfixOperatorStack.empty())
          break;

        Idx = InfixOperatorStack.size() - 1;
        StackOp = InfixOperatorStack[Idx];
        if (!(OpPrecedence[StackOp] >= OpPrecedence[Op] || ParenCount))
          break;

        // If we have an even parentheses count and we see a left parentheses,
        // then stop processing.
        if (!ParenCount && StackOp == IC_LPAREN)
          break;

        if (StackOp == IC_RPAREN) {
          ++ParenCount;
          InfixOperatorStack.pop_back();
        } else if (StackOp == IC_LPAREN) {
          --ParenCount;
          InfixOperatorStack.pop_back();
        } else {
          InfixOperatorStack.pop_back();
          PostfixStack.push_back(std::make_pair(StackOp, 0));
        }
      }
      // Push the new operator.
      InfixOperatorStack.push_back(Op);
    }

    int64_t execute() {
      // Push any remaining operators onto the postfix stack.
      while (!InfixOperatorStack.empty()) {
        InfixCalculatorTok StackOp = InfixOperatorStack.pop_back_val();
        if (StackOp != IC_LPAREN && StackOp != IC_RPAREN)
          PostfixStack.push_back(std::make_pair(StackOp, 0));
      }

      if (PostfixStack.empty())
        return 0;

      SmallVector<ICToken, 16> OperandStack;
      for (unsigned i = 0, e = PostfixStack.size(); i != e; ++i) {
        ICToken Op = PostfixStack[i];
        if (Op.first == IC_IMM || Op.first == IC_REGISTER) {
          OperandStack.push_back(Op);
        } else if (isUnaryOperator(Op.first)) {
          assert (OperandStack.size() > 0 && "Too few operands.");
          ICToken Operand = OperandStack.pop_back_val();
          assert (Operand.first == IC_IMM &&
                  "Unary operation with a register!");
          switch (Op.first) {
          default:
            report_fatal_error("Unexpected operator!");
            break;
          case IC_NEG:
            OperandStack.push_back(std::make_pair(IC_IMM, -Operand.second));
            break;
          case IC_NOT:
            OperandStack.push_back(std::make_pair(IC_IMM, ~Operand.second));
            break;
          }
        } else {
          assert (OperandStack.size() > 1 && "Too few operands.");
          int64_t Val;
          ICToken Op2 = OperandStack.pop_back_val();
          ICToken Op1 = OperandStack.pop_back_val();
          switch (Op.first) {
          default:
            report_fatal_error("Unexpected operator!");
            break;
          case IC_PLUS:
            Val = Op1.second + Op2.second;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_MINUS:
            Val = Op1.second - Op2.second;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_MULTIPLY:
            assert (Op1.first == IC_IMM && Op2.first == IC_IMM &&
                    "Multiply operation with an immediate and a register!");
            Val = Op1.second * Op2.second;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_DIVIDE:
            assert (Op1.first == IC_IMM && Op2.first == IC_IMM &&
                    "Divide operation with an immediate and a register!");
            assert (Op2.second != 0 && "Division by zero!");
            Val = Op1.second / Op2.second;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_MOD:
            assert (Op1.first == IC_IMM && Op2.first == IC_IMM &&
                    "Modulo operation with an immediate and a register!");
            Val = Op1.second % Op2.second;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_OR:
            assert (Op1.first == IC_IMM && Op2.first == IC_IMM &&
                    "Or operation with an immediate and a register!");
            Val = Op1.second | Op2.second;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_XOR:
            assert(Op1.first == IC_IMM && Op2.first == IC_IMM &&
              "Xor operation with an immediate and a register!");
            Val = Op1.second ^ Op2.second;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_AND:
            assert (Op1.first == IC_IMM && Op2.first == IC_IMM &&
                    "And operation with an immediate and a register!");
            Val = Op1.second & Op2.second;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_LSHIFT:
            assert (Op1.first == IC_IMM && Op2.first == IC_IMM &&
                    "Left shift operation with an immediate and a register!");
            Val = Op1.second << Op2.second;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          case IC_RSHIFT:
            assert (Op1.first == IC_IMM && Op2.first == IC_IMM &&
                    "Right shift operation with an immediate and a register!");
            Val = Op1.second >> Op2.second;
            OperandStack.push_back(std::make_pair(IC_IMM, Val));
            break;
          }
        }
      }
      assert (OperandStack.size() == 1 && "Expected a single result.");
      return OperandStack.pop_back_val().second;
    }
  };

  enum IntelExprState {
    IES_INIT,
    IES_OR,
    IES_XOR,
    IES_AND,
    IES_LSHIFT,
    IES_RSHIFT,
    IES_PLUS,
    IES_MINUS,
    IES_NOT,
    IES_MULTIPLY,
    IES_DIVIDE,
    IES_MOD,
    IES_LBRAC,
    IES_RBRAC,
    IES_LPAREN,
    IES_RPAREN,
    IES_REGISTER,
    IES_INTEGER,
    IES_IDENTIFIER,
    IES_ERROR
  };

  class IntelExprStateMachine {
    IntelExprState State, PrevState;
    unsigned BaseReg, IndexReg, TmpReg, Scale;
    int64_t Imm;
    const MCExpr *Sym;
    StringRef SymName;
    InfixCalculator IC;
    InlineAsmIdentifierInfo Info;
    short BracCount;
    bool MemExpr;

  public:
    IntelExprStateMachine()
        : State(IES_INIT), PrevState(IES_ERROR), BaseReg(0), IndexReg(0),
          TmpReg(0), Scale(0), Imm(0), Sym(nullptr), BracCount(0),
          MemExpr(false) {}

    void addImm(int64_t imm) { Imm += imm; }
    short getBracCount() { return BracCount; }
    bool isMemExpr() { return MemExpr; }
    unsigned getBaseReg() { return BaseReg; }
    unsigned getIndexReg() { return IndexReg; }
    unsigned getScale() { return Scale; }
    const MCExpr *getSym() { return Sym; }
    StringRef getSymName() { return SymName; }
    int64_t getImm() { return Imm + IC.execute(); }
    bool isValidEndState() {
      return State == IES_RBRAC || State == IES_INTEGER;
    }
    bool hadError() { return State == IES_ERROR; }
    InlineAsmIdentifierInfo &getIdentifierInfo() { return Info; }

    void onOr() {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
      case IES_REGISTER:
        State = IES_OR;
        IC.pushOperator(IC_OR);
        break;
      }
      PrevState = CurrState;
    }
    void onXor() {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
      case IES_REGISTER:
        State = IES_XOR;
        IC.pushOperator(IC_XOR);
        break;
      }
      PrevState = CurrState;
    }
    void onAnd() {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
      case IES_REGISTER:
        State = IES_AND;
        IC.pushOperator(IC_AND);
        break;
      }
      PrevState = CurrState;
    }
    void onLShift() {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
      case IES_REGISTER:
        State = IES_LSHIFT;
        IC.pushOperator(IC_LSHIFT);
        break;
      }
      PrevState = CurrState;
    }
    void onRShift() {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
      case IES_REGISTER:
        State = IES_RSHIFT;
        IC.pushOperator(IC_RSHIFT);
        break;
      }
      PrevState = CurrState;
    }
    bool onPlus(StringRef &ErrMsg) {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
      case IES_REGISTER:
        State = IES_PLUS;
        IC.pushOperator(IC_PLUS);
        if (CurrState == IES_REGISTER && PrevState != IES_MULTIPLY) {
          // If we already have a BaseReg, then assume this is the IndexReg with
          // no explicit scale.
          if (!BaseReg) {
            BaseReg = TmpReg;
          } else {
            if (IndexReg) {
              ErrMsg = "BaseReg/IndexReg already set!";
              return true;
            }
            IndexReg = TmpReg;
            Scale = 0;
          }
        }
        break;
      }
      PrevState = CurrState;
      return false;
    }
    bool onMinus(StringRef &ErrMsg) {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_OR:
      case IES_XOR:
      case IES_AND:
      case IES_LSHIFT:
      case IES_RSHIFT:
      case IES_PLUS:
      case IES_NOT:
      case IES_MULTIPLY:
      case IES_DIVIDE:
      case IES_MOD:
      case IES_LPAREN:
      case IES_RPAREN:
      case IES_LBRAC:
      case IES_RBRAC:
      case IES_INTEGER:
      case IES_REGISTER:
      case IES_INIT:
        State = IES_MINUS;
        // push minus operator if it is not a negate operator
        if (CurrState == IES_REGISTER || CurrState == IES_RPAREN ||
            CurrState == IES_INTEGER  || CurrState == IES_RBRAC)
          IC.pushOperator(IC_MINUS);
        else if (PrevState == IES_REGISTER && CurrState == IES_MULTIPLY) {
          // We have negate operator for Scale: it's illegal
          ErrMsg = "Scale can't be negative";
          return true;
        } else
          IC.pushOperator(IC_NEG);
        if (CurrState == IES_REGISTER && PrevState != IES_MULTIPLY) {
          // If we already have a BaseReg, then assume this is the IndexReg with
          // no explicit scale.
          if (!BaseReg) {
            BaseReg = TmpReg;
          } else {
            if (IndexReg) {
              ErrMsg = "BaseReg/IndexReg already set!";
              return true;
            }
            IndexReg = TmpReg;
            Scale = 0;
          }
        }
        break;
      }
      PrevState = CurrState;
      return false;
    }
    void onNot() {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_OR:
      case IES_XOR:
      case IES_AND:
      case IES_LSHIFT:
      case IES_RSHIFT:
      case IES_PLUS:
      case IES_MINUS:
      case IES_NOT:
      case IES_MULTIPLY:
      case IES_DIVIDE:
      case IES_MOD:
      case IES_LPAREN:
      case IES_LBRAC:
      case IES_INIT:
        State = IES_NOT;
        IC.pushOperator(IC_NOT);
        break;
      }
      PrevState = CurrState;
    }

    bool onRegister(unsigned Reg, StringRef &ErrMsg) {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_PLUS:
      case IES_LPAREN:
      case IES_LBRAC:
        State = IES_REGISTER;
        TmpReg = Reg;
        IC.pushOperand(IC_REGISTER);
        break;
      case IES_MULTIPLY:
        // Index Register - Scale * Register
        if (PrevState == IES_INTEGER) {
          if (IndexReg) {
            ErrMsg = "BaseReg/IndexReg already set!";
            return true;
          }
          State = IES_REGISTER;
          IndexReg = Reg;
          // Get the scale and replace the 'Scale * Register' with '0'.
          Scale = IC.popOperand();
          if (checkScale(Scale, ErrMsg))
            return true;
          IC.pushOperand(IC_IMM);
          IC.popOperator();
        } else {
          State = IES_ERROR;
        }
        break;
      }
      PrevState = CurrState;
      return false;
    }
    bool onIdentifierExpr(const MCExpr *SymRef, StringRef SymRefName,
                          const InlineAsmIdentifierInfo &IDInfo,
                          bool ParsingInlineAsm, StringRef &ErrMsg) {
      // InlineAsm: Treat an enum value as an integer
      if (ParsingInlineAsm)
        if (IDInfo.isKind(InlineAsmIdentifierInfo::IK_EnumVal))
          return onInteger(IDInfo.Enum.EnumVal, ErrMsg);
      // Treat a symbolic constant like an integer
      if (auto *CE = dyn_cast<MCConstantExpr>(SymRef))
        return onInteger(CE->getValue(), ErrMsg);
      PrevState = State;
      bool HasSymbol = Sym != nullptr;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_PLUS:
      case IES_MINUS:
      case IES_NOT:
      case IES_INIT:
      case IES_LBRAC:
        MemExpr = true;
        State = IES_INTEGER;
        Sym = SymRef;
        SymName = SymRefName;
        IC.pushOperand(IC_IMM);
        if (ParsingInlineAsm)
          Info = IDInfo;
        break;
      }
      if (HasSymbol)
        ErrMsg = "cannot use more than one symbol in memory operand";
      return HasSymbol;
    }
    bool onInteger(int64_t TmpInt, StringRef &ErrMsg) {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_PLUS:
      case IES_MINUS:
      case IES_NOT:
      case IES_OR:
      case IES_XOR:
      case IES_AND:
      case IES_LSHIFT:
      case IES_RSHIFT:
      case IES_DIVIDE:
      case IES_MOD:
      case IES_MULTIPLY:
      case IES_LPAREN:
      case IES_INIT:
      case IES_LBRAC:
        State = IES_INTEGER;
        if (PrevState == IES_REGISTER && CurrState == IES_MULTIPLY) {
          // Index Register - Register * Scale
          if (IndexReg) {
            ErrMsg = "BaseReg/IndexReg already set!";
            return true;
          }
          IndexReg = TmpReg;
          Scale = TmpInt;
          if (checkScale(Scale, ErrMsg))
            return true;
          // Get the scale and replace the 'Register * Scale' with '0'.
          IC.popOperator();
        } else {
          IC.pushOperand(IC_IMM, TmpInt);
        }
        break;
      }
      PrevState = CurrState;
      return false;
    }
    void onStar() {
      PrevState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_REGISTER:
      case IES_RPAREN:
        State = IES_MULTIPLY;
        IC.pushOperator(IC_MULTIPLY);
        break;
      }
    }
    void onDivide() {
      PrevState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
        State = IES_DIVIDE;
        IC.pushOperator(IC_DIVIDE);
        break;
      }
    }
    void onMod() {
      PrevState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_RPAREN:
        State = IES_MOD;
        IC.pushOperator(IC_MOD);
        break;
      }
    }
    bool onLBrac() {
      if (BracCount)
        return true;
      PrevState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_RBRAC:
      case IES_INTEGER:
      case IES_RPAREN:
        State = IES_PLUS;
        IC.pushOperator(IC_PLUS);
        break;
      case IES_INIT:
        assert(!BracCount && "BracCount should be zero on parsing's start");
        State = IES_LBRAC;
        break;
      }
      MemExpr = true;
      BracCount++;
      return false;
    }
    bool onRBrac() {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_REGISTER:
      case IES_RPAREN:
        if (BracCount-- != 1)
          return true;
        State = IES_RBRAC;
        if (CurrState == IES_REGISTER && PrevState != IES_MULTIPLY) {
          // If we already have a BaseReg, then assume this is the IndexReg with
          // no explicit scale.
          if (!BaseReg) {
            BaseReg = TmpReg;
          } else {
            assert (!IndexReg && "BaseReg/IndexReg already set!");
            IndexReg = TmpReg;
            Scale = 0;
          }
        }
        break;
      }
      PrevState = CurrState;
      return false;
    }
    void onLParen() {
      IntelExprState CurrState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_PLUS:
      case IES_MINUS:
      case IES_NOT:
      case IES_OR:
      case IES_XOR:
      case IES_AND:
      case IES_LSHIFT:
      case IES_RSHIFT:
      case IES_MULTIPLY:
      case IES_DIVIDE:
      case IES_MOD:
      case IES_LPAREN:
      case IES_INIT:
      case IES_LBRAC:
        State = IES_LPAREN;
        IC.pushOperator(IC_LPAREN);
        break;
      }
      PrevState = CurrState;
    }
    void onRParen() {
      PrevState = State;
      switch (State) {
      default:
        State = IES_ERROR;
        break;
      case IES_INTEGER:
      case IES_REGISTER:
      case IES_RPAREN:
        State = IES_RPAREN;
        IC.pushOperator(IC_RPAREN);
        break;
      }
    }
  };

  bool Error(SMLoc L, const Twine &Msg, SMRange Range = None,
             bool MatchingInlineAsm = false) {
    MCAsmParser &Parser = getParser();
    if (MatchingInlineAsm) {
      if (!getLexer().isAtStartOfStatement())
        Parser.eatToEndOfStatement();
      return false;
    }
    return Parser.Error(L, Msg, Range);
  }

  std::nullptr_t ErrorOperand(SMLoc Loc, StringRef Msg, SMRange R = SMRange()) {
    Error(Loc, Msg, R);
    return nullptr;
  }

  std::unique_ptr<X86Operand> DefaultMemSIOperand(SMLoc Loc);
  std::unique_ptr<X86Operand> DefaultMemDIOperand(SMLoc Loc);
  bool IsSIReg(unsigned Reg);
  unsigned GetSIDIForRegClass(unsigned RegClassID, unsigned Reg, bool IsSIReg);
  void
  AddDefaultSrcDestOperands(OperandVector &Operands,
                            std::unique_ptr<llvm::MCParsedAsmOperand> &&Src,
                            std::unique_ptr<llvm::MCParsedAsmOperand> &&Dst);
  bool VerifyAndAdjustOperands(OperandVector &OrigOperands,
                               OperandVector &FinalOperands);
  std::unique_ptr<X86Operand> ParseOperand();
  std::unique_ptr<X86Operand> ParseATTOperand();
  std::unique_ptr<X86Operand> ParseIntelOperand();
  std::unique_ptr<X86Operand> ParseIntelOffsetOfOperator();
  bool ParseIntelDotOperator(IntelExprStateMachine &SM, SMLoc &End);
  unsigned IdentifyIntelInlineAsmOperator(StringRef Name);
  unsigned ParseIntelInlineAsmOperator(unsigned OpKind);
  std::unique_ptr<X86Operand> ParseRoundingModeOp(SMLoc Start);
  bool ParseIntelNamedOperator(StringRef Name, IntelExprStateMachine &SM);
  void RewriteIntelExpression(IntelExprStateMachine &SM, SMLoc Start,
                              SMLoc End);
  bool ParseIntelExpression(IntelExprStateMachine &SM, SMLoc &End);
  bool ParseIntelInlineAsmIdentifier(const MCExpr *&Val, StringRef &Identifier,
                                     InlineAsmIdentifierInfo &Info,
                                     bool IsUnevaluatedOperand, SMLoc &End);

  std::unique_ptr<X86Operand> ParseMemOperand(unsigned SegReg,
                                              const MCExpr *&Disp,
                                              const SMLoc &StartLoc,
                                              SMLoc &EndLoc);

  bool ParseIntelMemoryOperandSize(unsigned &Size);
  std::unique_ptr<X86Operand>
  CreateMemForInlineAsm(unsigned SegReg, const MCExpr *Disp, unsigned BaseReg,
                        unsigned IndexReg, unsigned Scale, SMLoc Start,
                        SMLoc End, unsigned Size, StringRef Identifier,
                        const InlineAsmIdentifierInfo &Info);

  bool parseDirectiveEven(SMLoc L);
  bool ParseDirectiveCode(StringRef IDVal, SMLoc L);

  /// CodeView FPO data directives.
  bool parseDirectiveFPOProc(SMLoc L);
  bool parseDirectiveFPOSetFrame(SMLoc L);
  bool parseDirectiveFPOPushReg(SMLoc L);
  bool parseDirectiveFPOStackAlloc(SMLoc L);
  bool parseDirectiveFPOStackAlign(SMLoc L);
  bool parseDirectiveFPOEndPrologue(SMLoc L);
  bool parseDirectiveFPOEndProc(SMLoc L);
  bool parseDirectiveFPOData(SMLoc L);

  bool validateInstruction(MCInst &Inst, const OperandVector &Ops);
  bool processInstruction(MCInst &Inst, const OperandVector &Ops);

  /// Wrapper around MCStreamer::EmitInstruction(). Possibly adds
  /// instrumentation around Inst.
  void EmitInstruction(MCInst &Inst, OperandVector &Operands, MCStreamer &Out);

  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;

  void MatchFPUWaitAlias(SMLoc IDLoc, X86Operand &Op, OperandVector &Operands,
                         MCStreamer &Out, bool MatchingInlineAsm);

  bool ErrorMissingFeature(SMLoc IDLoc, uint64_t ErrorInfo,
                           bool MatchingInlineAsm);

  bool MatchAndEmitATTInstruction(SMLoc IDLoc, unsigned &Opcode,
                                  OperandVector &Operands, MCStreamer &Out,
                                  uint64_t &ErrorInfo,
                                  bool MatchingInlineAsm);

  bool MatchAndEmitIntelInstruction(SMLoc IDLoc, unsigned &Opcode,
                                    OperandVector &Operands, MCStreamer &Out,
                                    uint64_t &ErrorInfo,
                                    bool MatchingInlineAsm);

  bool OmitRegisterFromClobberLists(unsigned RegNo) override;

  /// Parses AVX512 specific operand primitives: masked registers ({%k<NUM>}, {z})
  /// and memory broadcasting ({1to<NUM>}) primitives, updating Operands vector if required.
  /// return false if no parsing errors occurred, true otherwise.
  bool HandleAVX512Operand(OperandVector &Operands,
                           const MCParsedAsmOperand &Op);

  bool ParseZ(std::unique_ptr<X86Operand> &Z, const SMLoc &StartLoc);

  bool is64BitMode() const {
    // FIXME: Can tablegen auto-generate this?
    return getSTI().getFeatureBits()[X86::Mode64Bit];
  }
  bool is32BitMode() const {
    // FIXME: Can tablegen auto-generate this?
    return getSTI().getFeatureBits()[X86::Mode32Bit];
  }
  bool is16BitMode() const {
    // FIXME: Can tablegen auto-generate this?
    return getSTI().getFeatureBits()[X86::Mode16Bit];
  }
  void SwitchMode(unsigned mode) {
    MCSubtargetInfo &STI = copySTI();
    FeatureBitset AllModes({X86::Mode64Bit, X86::Mode32Bit, X86::Mode16Bit});
    FeatureBitset OldMode = STI.getFeatureBits() & AllModes;
    uint64_t FB = ComputeAvailableFeatures(
      STI.ToggleFeature(OldMode.flip(mode)));
    setAvailableFeatures(FB);

    assert(FeatureBitset({mode}) == (STI.getFeatureBits() & AllModes));
  }

  unsigned getPointerWidth() {
    if (is16BitMode()) return 16;
    if (is32BitMode()) return 32;
    if (is64BitMode()) return 64;
    llvm_unreachable("invalid mode");
  }

  bool isParsingIntelSyntax() {
    return getParser().getAssemblerDialect();
  }

  /// @name Auto-generated Matcher Functions
  /// {

#define GET_ASSEMBLER_HEADER
#include "X86GenAsmMatcher.inc"

  /// }

public:

  X86AsmParser(const MCSubtargetInfo &sti, MCAsmParser &Parser,
               const MCInstrInfo &mii, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, sti, mii),  InstInfo(nullptr),
        Code16GCC(false) {

    Parser.addAliasForDirective(".word", ".2byte");

    // Initialize the set of available features.
    setAvailableFeatures(ComputeAvailableFeatures(getSTI().getFeatureBits()));
    Instrumentation.reset(
        CreateX86AsmInstrumentation(Options, Parser.getContext(), STI));
  }

  bool ParseRegister(unsigned &RegNo, SMLoc &StartLoc, SMLoc &EndLoc) override;

  void SetFrameRegister(unsigned RegNo) override;

  bool parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc) override;

  bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;

  bool ParseDirective(AsmToken DirectiveID) override;
};
} // end anonymous namespace

/// @name Auto-generated Match Functions
/// {

static unsigned MatchRegisterName(StringRef Name);

/// }

static bool CheckBaseRegAndIndexRegAndScale(unsigned BaseReg, unsigned IndexReg,
                                            unsigned Scale, bool Is64BitMode,
                                            StringRef &ErrMsg) {
  // If we have both a base register and an index register make sure they are
  // both 64-bit or 32-bit registers.
  // To support VSIB, IndexReg can be 128-bit or 256-bit registers.

  if (BaseReg != 0 &&
      !(BaseReg == X86::RIP || BaseReg == X86::EIP ||
        X86MCRegisterClasses[X86::GR16RegClassID].contains(BaseReg) ||
        X86MCRegisterClasses[X86::GR32RegClassID].contains(BaseReg) ||
        X86MCRegisterClasses[X86::GR64RegClassID].contains(BaseReg))) {
    ErrMsg = "invalid base+index expression";
    return true;
  }

  if (IndexReg != 0 &&
      !(IndexReg == X86::EIZ || IndexReg == X86::RIZ ||
        X86MCRegisterClasses[X86::GR16RegClassID].contains(IndexReg) ||
        X86MCRegisterClasses[X86::GR32RegClassID].contains(IndexReg) ||
        X86MCRegisterClasses[X86::GR64RegClassID].contains(IndexReg) ||
        X86MCRegisterClasses[X86::VR128XRegClassID].contains(IndexReg) ||
        X86MCRegisterClasses[X86::VR256XRegClassID].contains(IndexReg) ||
        X86MCRegisterClasses[X86::VR512RegClassID].contains(IndexReg))) {
    ErrMsg = "invalid base+index expression";
    return true;
  }

  if (((BaseReg == X86::RIP || BaseReg == X86::EIP) && IndexReg != 0) ||
      IndexReg == X86::EIP || IndexReg == X86::RIP ||
      IndexReg == X86::ESP || IndexReg == X86::RSP) {
    ErrMsg = "invalid base+index expression";
    return true;
  }

  // Check for use of invalid 16-bit registers. Only BX/BP/SI/DI are allowed,
  // and then only in non-64-bit modes.
  if (X86MCRegisterClasses[X86::GR16RegClassID].contains(BaseReg) &&
      (Is64BitMode || (BaseReg != X86::BX && BaseReg != X86::BP &&
                       BaseReg != X86::SI && BaseReg != X86::DI))) {
    ErrMsg = "invalid 16-bit base register";
    return true;
  }

  if (BaseReg == 0 &&
      X86MCRegisterClasses[X86::GR16RegClassID].contains(IndexReg)) {
    ErrMsg = "16-bit memory operand may not include only index register";
    return true;
  }

  if (BaseReg != 0 && IndexReg != 0) {
    if (X86MCRegisterClasses[X86::GR64RegClassID].contains(BaseReg) &&
        (X86MCRegisterClasses[X86::GR16RegClassID].contains(IndexReg) ||
         X86MCRegisterClasses[X86::GR32RegClassID].contains(IndexReg) ||
         IndexReg == X86::EIZ)) {
      ErrMsg = "base register is 64-bit, but index register is not";
      return true;
    }
    if (X86MCRegisterClasses[X86::GR32RegClassID].contains(BaseReg) &&
        (X86MCRegisterClasses[X86::GR16RegClassID].contains(IndexReg) ||
         X86MCRegisterClasses[X86::GR64RegClassID].contains(IndexReg) ||
         IndexReg == X86::RIZ)) {
      ErrMsg = "base register is 32-bit, but index register is not";
      return true;
    }
    if (X86MCRegisterClasses[X86::GR16RegClassID].contains(BaseReg)) {
      if (X86MCRegisterClasses[X86::GR32RegClassID].contains(IndexReg) ||
          X86MCRegisterClasses[X86::GR64RegClassID].contains(IndexReg)) {
        ErrMsg = "base register is 16-bit, but index register is not";
        return true;
      }
      if ((BaseReg != X86::BX && BaseReg != X86::BP) ||
          (IndexReg != X86::SI && IndexReg != X86::DI)) {
        ErrMsg = "invalid 16-bit base/index register combination";
        return true;
      }
    }
  }

  // RIP/EIP-relative addressing is only supported in 64-bit mode.
  if (!Is64BitMode && BaseReg != 0 &&
      (BaseReg == X86::RIP || BaseReg == X86::EIP)) {
    ErrMsg = "IP-relative addressing requires 64-bit mode";
    return true;
  }

  return checkScale(Scale, ErrMsg);
}

bool X86AsmParser::ParseRegister(unsigned &RegNo,
                                 SMLoc &StartLoc, SMLoc &EndLoc) {
  MCAsmParser &Parser = getParser();
  RegNo = 0;
  const AsmToken &PercentTok = Parser.getTok();
  StartLoc = PercentTok.getLoc();

  // If we encounter a %, ignore it. This code handles registers with and
  // without the prefix, unprefixed registers can occur in cfi directives.
  if (!isParsingIntelSyntax() && PercentTok.is(AsmToken::Percent))
    Parser.Lex(); // Eat percent token.

  const AsmToken &Tok = Parser.getTok();
  EndLoc = Tok.getEndLoc();

  if (Tok.isNot(AsmToken::Identifier)) {
    if (isParsingIntelSyntax()) return true;
    return Error(StartLoc, "invalid register name",
                 SMRange(StartLoc, EndLoc));
  }

  RegNo = MatchRegisterName(Tok.getString());

  // If the match failed, try the register name as lowercase.
  if (RegNo == 0)
    RegNo = MatchRegisterName(Tok.getString().lower());

  // The "flags" register cannot be referenced directly.
  // Treat it as an identifier instead.
  if (isParsingInlineAsm() && isParsingIntelSyntax() && RegNo == X86::EFLAGS)
    RegNo = 0;

  if (!is64BitMode()) {
    // FIXME: This should be done using Requires<Not64BitMode> and
    // Requires<In64BitMode> so "eiz" usage in 64-bit instructions can be also
    // checked.
    // FIXME: Check AH, CH, DH, BH cannot be used in an instruction requiring a
    // REX prefix.
    if (RegNo == X86::RIZ || RegNo == X86::RIP ||
        X86MCRegisterClasses[X86::GR64RegClassID].contains(RegNo) ||
        X86II::isX86_64NonExtLowByteReg(RegNo) ||
        X86II::isX86_64ExtendedReg(RegNo)) {
      StringRef RegName = Tok.getString();
      Parser.Lex(); // Eat register name.
      return Error(StartLoc,
                   "register %" + RegName + " is only available in 64-bit mode",
                   SMRange(StartLoc, EndLoc));
    }
  }

  // Parse "%st" as "%st(0)" and "%st(1)", which is multiple tokens.
  if (RegNo == X86::ST0) {
    Parser.Lex(); // Eat 'st'

    // Check to see if we have '(4)' after %st.
    if (getLexer().isNot(AsmToken::LParen))
      return false;
    // Lex the paren.
    getParser().Lex();

    const AsmToken &IntTok = Parser.getTok();
    if (IntTok.isNot(AsmToken::Integer))
      return Error(IntTok.getLoc(), "expected stack index");
    switch (IntTok.getIntVal()) {
    case 0: RegNo = X86::ST0; break;
    case 1: RegNo = X86::ST1; break;
    case 2: RegNo = X86::ST2; break;
    case 3: RegNo = X86::ST3; break;
    case 4: RegNo = X86::ST4; break;
    case 5: RegNo = X86::ST5; break;
    case 6: RegNo = X86::ST6; break;
    case 7: RegNo = X86::ST7; break;
    default: return Error(IntTok.getLoc(), "invalid stack index");
    }

    if (getParser().Lex().isNot(AsmToken::RParen))
      return Error(Parser.getTok().getLoc(), "expected ')'");

    EndLoc = Parser.getTok().getEndLoc();
    Parser.Lex(); // Eat ')'
    return false;
  }

  EndLoc = Parser.getTok().getEndLoc();

  // If this is "db[0-15]", match it as an alias
  // for dr[0-15].
  if (RegNo == 0 && Tok.getString().startswith("db")) {
    if (Tok.getString().size() == 3) {
      switch (Tok.getString()[2]) {
      case '0': RegNo = X86::DR0; break;
      case '1': RegNo = X86::DR1; break;
      case '2': RegNo = X86::DR2; break;
      case '3': RegNo = X86::DR3; break;
      case '4': RegNo = X86::DR4; break;
      case '5': RegNo = X86::DR5; break;
      case '6': RegNo = X86::DR6; break;
      case '7': RegNo = X86::DR7; break;
      case '8': RegNo = X86::DR8; break;
      case '9': RegNo = X86::DR9; break;
      }
    } else if (Tok.getString().size() == 4 && Tok.getString()[2] == '1') {
      switch (Tok.getString()[3]) {
      case '0': RegNo = X86::DR10; break;
      case '1': RegNo = X86::DR11; break;
      case '2': RegNo = X86::DR12; break;
      case '3': RegNo = X86::DR13; break;
      case '4': RegNo = X86::DR14; break;
      case '5': RegNo = X86::DR15; break;
      }
    }

    if (RegNo != 0) {
      EndLoc = Parser.getTok().getEndLoc();
      Parser.Lex(); // Eat it.
      return false;
    }
  }

  if (RegNo == 0) {
    if (isParsingIntelSyntax()) return true;
    return Error(StartLoc, "invalid register name",
                 SMRange(StartLoc, EndLoc));
  }

  Parser.Lex(); // Eat identifier token.
  return false;
}

void X86AsmParser::SetFrameRegister(unsigned RegNo) {
  Instrumentation->SetInitialFrameRegister(RegNo);
}

std::unique_ptr<X86Operand> X86AsmParser::DefaultMemSIOperand(SMLoc Loc) {
  bool Parse32 = is32BitMode() || Code16GCC;
  unsigned Basereg = is64BitMode() ? X86::RSI : (Parse32 ? X86::ESI : X86::SI);
  const MCExpr *Disp = MCConstantExpr::create(0, getContext());
  return X86Operand::CreateMem(getPointerWidth(), /*SegReg=*/0, Disp,
                               /*BaseReg=*/Basereg, /*IndexReg=*/0, /*Scale=*/1,
                               Loc, Loc, 0);
}

std::unique_ptr<X86Operand> X86AsmParser::DefaultMemDIOperand(SMLoc Loc) {
  bool Parse32 = is32BitMode() || Code16GCC;
  unsigned Basereg = is64BitMode() ? X86::RDI : (Parse32 ? X86::EDI : X86::DI);
  const MCExpr *Disp = MCConstantExpr::create(0, getContext());
  return X86Operand::CreateMem(getPointerWidth(), /*SegReg=*/0, Disp,
                               /*BaseReg=*/Basereg, /*IndexReg=*/0, /*Scale=*/1,
                               Loc, Loc, 0);
}

bool X86AsmParser::IsSIReg(unsigned Reg) {
  switch (Reg) {
  default: llvm_unreachable("Only (R|E)SI and (R|E)DI are expected!");
  case X86::RSI:
  case X86::ESI:
  case X86::SI:
    return true;
  case X86::RDI:
  case X86::EDI:
  case X86::DI:
    return false;
  }
}

unsigned X86AsmParser::GetSIDIForRegClass(unsigned RegClassID, unsigned Reg,
                                          bool IsSIReg) {
  switch (RegClassID) {
  default: llvm_unreachable("Unexpected register class");
  case X86::GR64RegClassID:
    return IsSIReg ? X86::RSI : X86::RDI;
  case X86::GR32RegClassID:
    return IsSIReg ? X86::ESI : X86::EDI;
  case X86::GR16RegClassID:
    return IsSIReg ? X86::SI : X86::DI;
  }
}

void X86AsmParser::AddDefaultSrcDestOperands(
    OperandVector& Operands, std::unique_ptr<llvm::MCParsedAsmOperand> &&Src,
    std::unique_ptr<llvm::MCParsedAsmOperand> &&Dst) {
  if (isParsingIntelSyntax()) {
    Operands.push_back(std::move(Dst));
    Operands.push_back(std::move(Src));
  }
  else {
    Operands.push_back(std::move(Src));
    Operands.push_back(std::move(Dst));
  }
}

bool X86AsmParser::VerifyAndAdjustOperands(OperandVector &OrigOperands,
                                           OperandVector &FinalOperands) {

  if (OrigOperands.size() > 1) {
    // Check if sizes match, OrigOperands also contains the instruction name
    assert(OrigOperands.size() == FinalOperands.size() + 1 &&
           "Operand size mismatch");

    SmallVector<std::pair<SMLoc, std::string>, 2> Warnings;
    // Verify types match
    int RegClassID = -1;
    for (unsigned int i = 0; i < FinalOperands.size(); ++i) {
      X86Operand &OrigOp = static_cast<X86Operand &>(*OrigOperands[i + 1]);
      X86Operand &FinalOp = static_cast<X86Operand &>(*FinalOperands[i]);

      if (FinalOp.isReg() &&
          (!OrigOp.isReg() || FinalOp.getReg() != OrigOp.getReg()))
        // Return false and let a normal complaint about bogus operands happen
        return false;

      if (FinalOp.isMem()) {

        if (!OrigOp.isMem())
          // Return false and let a normal complaint about bogus operands happen
          return false;

        unsigned OrigReg = OrigOp.Mem.BaseReg;
        unsigned FinalReg = FinalOp.Mem.BaseReg;

        // If we've already encounterd a register class, make sure all register
        // bases are of the same register class
        if (RegClassID != -1 &&
            !X86MCRegisterClasses[RegClassID].contains(OrigReg)) {
          return Error(OrigOp.getStartLoc(),
                       "mismatching source and destination index registers");
        }

        if (X86MCRegisterClasses[X86::GR64RegClassID].contains(OrigReg))
          RegClassID = X86::GR64RegClassID;
        else if (X86MCRegisterClasses[X86::GR32RegClassID].contains(OrigReg))
          RegClassID = X86::GR32RegClassID;
        else if (X86MCRegisterClasses[X86::GR16RegClassID].contains(OrigReg))
          RegClassID = X86::GR16RegClassID;
        else
          // Unexpected register class type
          // Return false and let a normal complaint about bogus operands happen
          return false;

        bool IsSI = IsSIReg(FinalReg);
        FinalReg = GetSIDIForRegClass(RegClassID, FinalReg, IsSI);

        if (FinalReg != OrigReg) {
          std::string RegName = IsSI ? "ES:(R|E)SI" : "ES:(R|E)DI";
          Warnings.push_back(std::make_pair(
              OrigOp.getStartLoc(),
              "memory operand is only for determining the size, " + RegName +
                  " will be used for the location"));
        }

        FinalOp.Mem.Size = OrigOp.Mem.Size;
        FinalOp.Mem.SegReg = OrigOp.Mem.SegReg;
        FinalOp.Mem.BaseReg = FinalReg;
      }
    }

    // Produce warnings only if all the operands passed the adjustment - prevent
    // legal cases like "movsd (%rax), %xmm0" mistakenly produce warnings
    for (auto &WarningMsg : Warnings) {
      Warning(WarningMsg.first, WarningMsg.second);
    }

    // Remove old operands
    for (unsigned int i = 0; i < FinalOperands.size(); ++i)
      OrigOperands.pop_back();
  }
  // OrigOperands.append(FinalOperands.begin(), FinalOperands.end());
  for (unsigned int i = 0; i < FinalOperands.size(); ++i)
    OrigOperands.push_back(std::move(FinalOperands[i]));

  return false;
}

std::unique_ptr<X86Operand> X86AsmParser::ParseOperand() {
  if (isParsingIntelSyntax())
    return ParseIntelOperand();
  return ParseATTOperand();
}

std::unique_ptr<X86Operand> X86AsmParser::CreateMemForInlineAsm(
    unsigned SegReg, const MCExpr *Disp, unsigned BaseReg, unsigned IndexReg,
    unsigned Scale, SMLoc Start, SMLoc End, unsigned Size, StringRef Identifier,
    const InlineAsmIdentifierInfo &Info) {
  // If we found a decl other than a VarDecl, then assume it is a FuncDecl or
  // some other label reference.
  if (Info.isKind(InlineAsmIdentifierInfo::IK_Label)) {
    // Insert an explicit size if the user didn't have one.
    if (!Size) {
      Size = getPointerWidth();
      InstInfo->AsmRewrites->emplace_back(AOK_SizeDirective, Start,
                                          /*Len=*/0, Size);
    }
    // Create an absolute memory reference in order to match against
    // instructions taking a PC relative operand.
    return X86Operand::CreateMem(getPointerWidth(), Disp, Start, End, Size,
                                 Identifier, Info.Label.Decl);
  }
  // We either have a direct symbol reference, or an offset from a symbol.  The
  // parser always puts the symbol on the LHS, so look there for size
  // calculation purposes.
  unsigned FrontendSize = 0;
  void *Decl = nullptr;
  bool IsGlobalLV = false;
  if (Info.isKind(InlineAsmIdentifierInfo::IK_Var)) {
    // Size is in terms of bits in this context.
    FrontendSize = Info.Var.Type * 8;
    Decl = Info.Var.Decl;
    IsGlobalLV = Info.Var.IsGlobalLV;
  }
  // It is widely common for MS InlineAsm to use a global variable and one/two
  // registers in a mmory expression, and though unaccessible via rip/eip.
  if (IsGlobalLV && (BaseReg || IndexReg)) {
    return X86Operand::CreateMem(getPointerWidth(), Disp, Start, End);
  // Otherwise, we set the base register to a non-zero value
  // if we don't know the actual value at this time.  This is necessary to
  // get the matching correct in some cases.
  } else {
    BaseReg = BaseReg ? BaseReg : 1;
    return X86Operand::CreateMem(getPointerWidth(), SegReg, Disp, BaseReg,
                                 IndexReg, Scale, Start, End, Size, Identifier,
                                 Decl, FrontendSize);
  }
}

// Some binary bitwise operators have a named synonymous
// Query a candidate string for being such a named operator
// and if so - invoke the appropriate handler
bool X86AsmParser::ParseIntelNamedOperator(StringRef Name, IntelExprStateMachine &SM) {
  // A named operator should be either lower or upper case, but not a mix
  if (Name.compare(Name.lower()) && Name.compare(Name.upper()))
    return false;
  if (Name.equals_lower("not"))
    SM.onNot();
  else if (Name.equals_lower("or"))
    SM.onOr();
  else if (Name.equals_lower("shl"))
    SM.onLShift();
  else if (Name.equals_lower("shr"))
    SM.onRShift();
  else if (Name.equals_lower("xor"))
    SM.onXor();
  else if (Name.equals_lower("and"))
    SM.onAnd();
  else if (Name.equals_lower("mod"))
    SM.onMod();
  else
    return false;
  return true;
}

bool X86AsmParser::ParseIntelExpression(IntelExprStateMachine &SM, SMLoc &End) {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  StringRef ErrMsg;

  AsmToken::TokenKind PrevTK = AsmToken::Error;
  bool Done = false;
  while (!Done) {
    bool UpdateLocLex = true;
    AsmToken::TokenKind TK = getLexer().getKind();

    switch (TK) {
    default:
      if ((Done = SM.isValidEndState()))
        break;
      return Error(Tok.getLoc(), "unknown token in expression");
    case AsmToken::EndOfStatement:
      Done = true;
      break;
    case AsmToken::Real:
      // DotOperator: [ebx].0
      UpdateLocLex = false;
      if (ParseIntelDotOperator(SM, End))
        return true;
      break;
    case AsmToken::At:
    case AsmToken::String:
    case AsmToken::Identifier: {
      SMLoc IdentLoc = Tok.getLoc();
      StringRef Identifier = Tok.getString();
      UpdateLocLex = false;
      // Register
      unsigned Reg;
      if (Tok.is(AsmToken::Identifier) && !ParseRegister(Reg, IdentLoc, End)) {
        if (SM.onRegister(Reg, ErrMsg))
          return Error(Tok.getLoc(), ErrMsg);
        break;
      }
      // Operator synonymous ("not", "or" etc.)
      if ((UpdateLocLex = ParseIntelNamedOperator(Identifier, SM)))
        break;
      // Symbol reference, when parsing assembly content
      InlineAsmIdentifierInfo Info;
      const MCExpr *Val;
      if (!isParsingInlineAsm()) {
        if (getParser().parsePrimaryExpr(Val, End)) {
          return Error(Tok.getLoc(), "Unexpected identifier!");
        } else if (SM.onIdentifierExpr(Val, Identifier, Info, false, ErrMsg)) {
          return Error(IdentLoc, ErrMsg);
        } else
          break;
      }
      // MS InlineAsm operators (TYPE/LENGTH/SIZE)
      if (unsigned OpKind = IdentifyIntelInlineAsmOperator(Identifier)) {
        if (OpKind == IOK_OFFSET)
          return Error(IdentLoc, "Dealing OFFSET operator as part of"
            "a compound immediate expression is yet to be supported");
        if (int64_t Val = ParseIntelInlineAsmOperator(OpKind)) {
          if (SM.onInteger(Val, ErrMsg))
            return Error(IdentLoc, ErrMsg);
        } else
          return true;
        break;
      }
      // MS Dot Operator expression
      if (Identifier.count('.') && PrevTK == AsmToken::RBrac) {
        if (ParseIntelDotOperator(SM, End))
          return true;
        break;
      }
      // MS InlineAsm identifier
      // Call parseIdentifier() to combine @ with the identifier behind it.
      if (TK == AsmToken::At && Parser.parseIdentifier(Identifier))
        return Error(IdentLoc, "expected identifier");
      if (ParseIntelInlineAsmIdentifier(Val, Identifier, Info, false, End))
        return true;
      else if (SM.onIdentifierExpr(Val, Identifier, Info, true, ErrMsg))
        return Error(IdentLoc, ErrMsg);
      break;
    }
    case AsmToken::Integer: {
      // Look for 'b' or 'f' following an Integer as a directional label
      SMLoc Loc = getTok().getLoc();
      int64_t IntVal = getTok().getIntVal();
      End = consumeToken();
      UpdateLocLex = false;
      if (getLexer().getKind() == AsmToken::Identifier) {
        StringRef IDVal = getTok().getString();
        if (IDVal == "f" || IDVal == "b") {
          MCSymbol *Sym =
              getContext().getDirectionalLocalSymbol(IntVal, IDVal == "b");
          MCSymbolRefExpr::VariantKind Variant = MCSymbolRefExpr::VK_None;
          const MCExpr *Val =
              MCSymbolRefExpr::create(Sym, Variant, getContext());
          if (IDVal == "b" && Sym->isUndefined())
            return Error(Loc, "invalid reference to undefined symbol");
          StringRef Identifier = Sym->getName();
          InlineAsmIdentifierInfo Info;
          if (SM.onIdentifierExpr(Val, Identifier, Info,
              isParsingInlineAsm(), ErrMsg))
            return Error(Loc, ErrMsg);
          End = consumeToken();
        } else {
          if (SM.onInteger(IntVal, ErrMsg))
            return Error(Loc, ErrMsg);
        }
      } else {
        if (SM.onInteger(IntVal, ErrMsg))
          return Error(Loc, ErrMsg);
      }
      break;
    }
    case AsmToken::Plus:
      if (SM.onPlus(ErrMsg))
        return Error(getTok().getLoc(), ErrMsg);
      break;
    case AsmToken::Minus:
      if (SM.onMinus(ErrMsg))
        return Error(getTok().getLoc(), ErrMsg);
      break;
    case AsmToken::Tilde:   SM.onNot(); break;
    case AsmToken::Star:    SM.onStar(); break;
    case AsmToken::Slash:   SM.onDivide(); break;
    case AsmToken::Percent: SM.onMod(); break;
    case AsmToken::Pipe:    SM.onOr(); break;
    case AsmToken::Caret:   SM.onXor(); break;
    case AsmToken::Amp:     SM.onAnd(); break;
    case AsmToken::LessLess:
                            SM.onLShift(); break;
    case AsmToken::GreaterGreater:
                            SM.onRShift(); break;
    case AsmToken::LBrac:
      if (SM.onLBrac())
        return Error(Tok.getLoc(), "unexpected bracket encountered");
      break;
    case AsmToken::RBrac:
      if (SM.onRBrac())
        return Error(Tok.getLoc(), "unexpected bracket encountered");
      break;
    case AsmToken::LParen:  SM.onLParen(); break;
    case AsmToken::RParen:  SM.onRParen(); break;
    }
    if (SM.hadError())
      return Error(Tok.getLoc(), "unknown token in expression");

    if (!Done && UpdateLocLex)
      End = consumeToken();

    PrevTK = TK;
  }
  return false;
}

void X86AsmParser::RewriteIntelExpression(IntelExprStateMachine &SM,
                                          SMLoc Start, SMLoc End) {
  SMLoc Loc = Start;
  unsigned ExprLen = End.getPointer() - Start.getPointer();
  // Skip everything before a symbol displacement (if we have one)
  if (SM.getSym()) {
    StringRef SymName = SM.getSymName();
    if (unsigned Len =  SymName.data() - Start.getPointer())
      InstInfo->AsmRewrites->emplace_back(AOK_Skip, Start, Len);
    Loc = SMLoc::getFromPointer(SymName.data() + SymName.size());
    ExprLen = End.getPointer() - (SymName.data() + SymName.size());
    // If we have only a symbol than there's no need for complex rewrite,
    // simply skip everything after it
    if (!(SM.getBaseReg() || SM.getIndexReg() || SM.getImm())) {
      if (ExprLen)
        InstInfo->AsmRewrites->emplace_back(AOK_Skip, Loc, ExprLen);
      return;
    }
  }
  // Build an Intel Expression rewrite
  StringRef BaseRegStr;
  StringRef IndexRegStr;
  if (SM.getBaseReg())
    BaseRegStr = X86IntelInstPrinter::getRegisterName(SM.getBaseReg());
  if (SM.getIndexReg())
    IndexRegStr = X86IntelInstPrinter::getRegisterName(SM.getIndexReg());
  // Emit it
  IntelExpr Expr(BaseRegStr, IndexRegStr, SM.getScale(), SM.getImm(), SM.isMemExpr());
  InstInfo->AsmRewrites->emplace_back(Loc, ExprLen, Expr);
}

// Inline assembly may use variable names with namespace alias qualifiers.
bool X86AsmParser::ParseIntelInlineAsmIdentifier(const MCExpr *&Val,
                                                 StringRef &Identifier,
                                                 InlineAsmIdentifierInfo &Info,
                                                 bool IsUnevaluatedOperand,
                                                 SMLoc &End) {
  MCAsmParser &Parser = getParser();
  assert(isParsingInlineAsm() && "Expected to be parsing inline assembly.");
  Val = nullptr;

  StringRef LineBuf(Identifier.data());
  SemaCallback->LookupInlineAsmIdentifier(LineBuf, Info, IsUnevaluatedOperand);

  const AsmToken &Tok = Parser.getTok();
  SMLoc Loc = Tok.getLoc();

  // Advance the token stream until the end of the current token is
  // after the end of what the frontend claimed.
  const char *EndPtr = Tok.getLoc().getPointer() + LineBuf.size();
  do {
    End = Tok.getEndLoc();
    getLexer().Lex();
  } while (End.getPointer() < EndPtr);
  Identifier = LineBuf;

  // The frontend should end parsing on an assembler token boundary, unless it
  // failed parsing.
  assert((End.getPointer() == EndPtr ||
          Info.isKind(InlineAsmIdentifierInfo::IK_Invalid)) &&
          "frontend claimed part of a token?");

  // If the identifier lookup was unsuccessful, assume that we are dealing with
  // a label.
  if (Info.isKind(InlineAsmIdentifierInfo::IK_Invalid)) {
    StringRef InternalName =
      SemaCallback->LookupInlineAsmLabel(Identifier, getSourceManager(),
                                         Loc, false);
    assert(InternalName.size() && "We should have an internal name here.");
    // Push a rewrite for replacing the identifier name with the internal name.
    InstInfo->AsmRewrites->emplace_back(AOK_Label, Loc, Identifier.size(),
                                        InternalName);
  } else if (Info.isKind(InlineAsmIdentifierInfo::IK_EnumVal))
    return false;
  // Create the symbol reference.
  MCSymbol *Sym = getContext().getOrCreateSymbol(Identifier);
  MCSymbolRefExpr::VariantKind Variant = MCSymbolRefExpr::VK_None;
  Val = MCSymbolRefExpr::create(Sym, Variant, getParser().getContext());
  return false;
}

//ParseRoundingModeOp - Parse AVX-512 rounding mode operand
std::unique_ptr<X86Operand>
X86AsmParser::ParseRoundingModeOp(SMLoc Start) {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  // Eat "{" and mark the current place.
  const SMLoc consumedToken = consumeToken();
  if (Tok.getIdentifier().startswith("r")){
    int rndMode = StringSwitch<int>(Tok.getIdentifier())
      .Case("rn", X86::STATIC_ROUNDING::TO_NEAREST_INT)
      .Case("rd", X86::STATIC_ROUNDING::TO_NEG_INF)
      .Case("ru", X86::STATIC_ROUNDING::TO_POS_INF)
      .Case("rz", X86::STATIC_ROUNDING::TO_ZERO)
      .Default(-1);
    if (-1 == rndMode)
      return ErrorOperand(Tok.getLoc(), "Invalid rounding mode.");
     Parser.Lex();  // Eat "r*" of r*-sae
    if (!getLexer().is(AsmToken::Minus))
      return ErrorOperand(Tok.getLoc(), "Expected - at this point");
    Parser.Lex();  // Eat "-"
    Parser.Lex();  // Eat the sae
    if (!getLexer().is(AsmToken::RCurly))
      return ErrorOperand(Tok.getLoc(), "Expected } at this point");
    SMLoc End = Tok.getEndLoc();
    Parser.Lex();  // Eat "}"
    const MCExpr *RndModeOp =
      MCConstantExpr::create(rndMode, Parser.getContext());
    return X86Operand::CreateImm(RndModeOp, Start, End);
  }
  if(Tok.getIdentifier().equals("sae")){
    Parser.Lex();  // Eat the sae
    if (!getLexer().is(AsmToken::RCurly))
      return ErrorOperand(Tok.getLoc(), "Expected } at this point");
    Parser.Lex();  // Eat "}"
    return X86Operand::CreateToken("{sae}", consumedToken);
  }
  return ErrorOperand(Tok.getLoc(), "unknown token in expression");
}

/// Parse the '.' operator.
bool X86AsmParser::ParseIntelDotOperator(IntelExprStateMachine &SM, SMLoc &End) {
  const AsmToken &Tok = getTok();
  unsigned Offset;

  // Drop the optional '.'.
  StringRef DotDispStr = Tok.getString();
  if (DotDispStr.startswith("."))
    DotDispStr = DotDispStr.drop_front(1);

  // .Imm gets lexed as a real.
  if (Tok.is(AsmToken::Real)) {
    APInt DotDisp;
    DotDispStr.getAsInteger(10, DotDisp);
    Offset = DotDisp.getZExtValue();
  } else if (isParsingInlineAsm() && Tok.is(AsmToken::Identifier)) {
    std::pair<StringRef, StringRef> BaseMember = DotDispStr.split('.');
    if (SemaCallback->LookupInlineAsmField(BaseMember.first, BaseMember.second,
                                           Offset))
      return Error(Tok.getLoc(), "Unable to lookup field reference!");
  } else
    return Error(Tok.getLoc(), "Unexpected token type!");

  // Eat the DotExpression and update End
  End = SMLoc::getFromPointer(DotDispStr.data());
  const char *DotExprEndLoc = DotDispStr.data() + DotDispStr.size();
  while (Tok.getLoc().getPointer() < DotExprEndLoc)
    Lex();
  SM.addImm(Offset);
  return false;
}

/// Parse the 'offset' operator.  This operator is used to specify the
/// location rather then the content of a variable.
std::unique_ptr<X86Operand> X86AsmParser::ParseIntelOffsetOfOperator() {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  SMLoc OffsetOfLoc = Tok.getLoc();
  Parser.Lex(); // Eat offset.

  const MCExpr *Val;
  InlineAsmIdentifierInfo Info;
  SMLoc Start = Tok.getLoc(), End;
  StringRef Identifier = Tok.getString();
  if (ParseIntelInlineAsmIdentifier(Val, Identifier, Info,
                                    /*Unevaluated=*/false, End))
    return nullptr;

  void *Decl = nullptr;
  // FIXME: MS evaluates "offset <Constant>" to the underlying integral
  if (Info.isKind(InlineAsmIdentifierInfo::IK_EnumVal))
    return ErrorOperand(Start, "offset operator cannot yet handle constants");
  else if (Info.isKind(InlineAsmIdentifierInfo::IK_Var))
    Decl = Info.Var.Decl;
  // Don't emit the offset operator.
  InstInfo->AsmRewrites->emplace_back(AOK_Skip, OffsetOfLoc, 7);

  // The offset operator will have an 'r' constraint, thus we need to create
  // register operand to ensure proper matching.  Just pick a GPR based on
  // the size of a pointer.
  bool Parse32 = is32BitMode() || Code16GCC;
  unsigned RegNo = is64BitMode() ? X86::RBX : (Parse32 ? X86::EBX : X86::BX);

  return X86Operand::CreateReg(RegNo, Start, End, /*GetAddress=*/true,
                               OffsetOfLoc, Identifier, Decl);
}

// Query a candidate string for being an Intel assembly operator
// Report back its kind, or IOK_INVALID if does not evaluated as a known one
unsigned X86AsmParser::IdentifyIntelInlineAsmOperator(StringRef Name) {
  return StringSwitch<unsigned>(Name)
    .Cases("TYPE","type",IOK_TYPE)
    .Cases("SIZE","size",IOK_SIZE)
    .Cases("LENGTH","length",IOK_LENGTH)
    .Cases("OFFSET","offset",IOK_OFFSET)
    .Default(IOK_INVALID);
}

/// Parse the 'LENGTH', 'TYPE' and 'SIZE' operators.  The LENGTH operator
/// returns the number of elements in an array.  It returns the value 1 for
/// non-array variables.  The SIZE operator returns the size of a C or C++
/// variable.  A variable's size is the product of its LENGTH and TYPE.  The
/// TYPE operator returns the size of a C or C++ type or variable. If the
/// variable is an array, TYPE returns the size of a single element.
unsigned X86AsmParser::ParseIntelInlineAsmOperator(unsigned OpKind) {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  Parser.Lex(); // Eat operator.

  const MCExpr *Val = nullptr;
  InlineAsmIdentifierInfo Info;
  SMLoc Start = Tok.getLoc(), End;
  StringRef Identifier = Tok.getString();
  if (ParseIntelInlineAsmIdentifier(Val, Identifier, Info,
                                    /*Unevaluated=*/true, End))
    return 0;

  if (!Info.isKind(InlineAsmIdentifierInfo::IK_Var)) {
    Error(Start, "unable to lookup expression");
    return 0;
  }

  unsigned CVal = 0;
  switch(OpKind) {
  default: llvm_unreachable("Unexpected operand kind!");
  case IOK_LENGTH: CVal = Info.Var.Length; break;
  case IOK_SIZE: CVal = Info.Var.Size; break;
  case IOK_TYPE: CVal = Info.Var.Type; break;
  }

  return CVal;
}

bool X86AsmParser::ParseIntelMemoryOperandSize(unsigned &Size) {
  Size = StringSwitch<unsigned>(getTok().getString())
    .Cases("BYTE", "byte", 8)
    .Cases("WORD", "word", 16)
    .Cases("DWORD", "dword", 32)
    .Cases("FLOAT", "float", 32)
    .Cases("LONG", "long", 32)
    .Cases("FWORD", "fword", 48)
    .Cases("DOUBLE", "double", 64)
    .Cases("QWORD", "qword", 64)
    .Cases("MMWORD","mmword", 64)
    .Cases("XWORD", "xword", 80)
    .Cases("TBYTE", "tbyte", 80)
    .Cases("XMMWORD", "xmmword", 128)
    .Cases("YMMWORD", "ymmword", 256)
    .Cases("ZMMWORD", "zmmword", 512)
    .Default(0);
  if (Size) {
    const AsmToken &Tok = Lex(); // Eat operand size (e.g., byte, word).
    if (!(Tok.getString().equals("PTR") || Tok.getString().equals("ptr")))
      return Error(Tok.getLoc(), "Expected 'PTR' or 'ptr' token!");
    Lex(); // Eat ptr.
  }
  return false;
}

std::unique_ptr<X86Operand> X86AsmParser::ParseIntelOperand() {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  SMLoc Start, End;

  // FIXME: Offset operator
  // Should be handled as part of immediate expression, as other operators
  // Currently, only supported as a stand-alone operand
  if (isParsingInlineAsm())
    if (IdentifyIntelInlineAsmOperator(Tok.getString()) == IOK_OFFSET)
      return ParseIntelOffsetOfOperator();

  // Parse optional Size directive.
  unsigned Size;
  if (ParseIntelMemoryOperandSize(Size))
    return nullptr;
  bool PtrInOperand = bool(Size);

  Start = Tok.getLoc();

  // Rounding mode operand.
  if (getLexer().is(AsmToken::LCurly))
    return ParseRoundingModeOp(Start);

  // Register operand.
  unsigned RegNo = 0;
  if (Tok.is(AsmToken::Identifier) && !ParseRegister(RegNo, Start, End)) {
    if (RegNo == X86::RIP)
      return ErrorOperand(Start, "rip can only be used as a base register");
    // A Register followed by ':' is considered a segment override
    if (Tok.isNot(AsmToken::Colon))
      return !PtrInOperand ? X86Operand::CreateReg(RegNo, Start, End) :
        ErrorOperand(Start, "expected memory operand after 'ptr', "
                            "found register operand instead");
    // An alleged segment override. check if we have a valid segment register
    if (!X86MCRegisterClasses[X86::SEGMENT_REGRegClassID].contains(RegNo))
      return ErrorOperand(Start, "invalid segment register");
    // Eat ':' and update Start location
    Start = Lex().getLoc();
  }

  // Immediates and Memory
  IntelExprStateMachine SM;
  if (ParseIntelExpression(SM, End))
    return nullptr;

  if (isParsingInlineAsm())
    RewriteIntelExpression(SM, Start, Tok.getLoc());

  int64_t Imm = SM.getImm();
  const MCExpr *Disp = SM.getSym();
  const MCExpr *ImmDisp = MCConstantExpr::create(Imm, getContext());
  if (Disp && Imm)
    Disp = MCBinaryExpr::createAdd(Disp, ImmDisp, getContext());
  if (!Disp)
    Disp = ImmDisp;

  // RegNo != 0 specifies a valid segment register,
  // and we are parsing a segment override
  if (!SM.isMemExpr() && !RegNo)
    return X86Operand::CreateImm(Disp, Start, End);

  StringRef ErrMsg;
  unsigned BaseReg = SM.getBaseReg();
  unsigned IndexReg = SM.getIndexReg();
  unsigned Scale = SM.getScale();

  if (Scale == 0 && BaseReg != X86::ESP && BaseReg != X86::RSP &&
      (IndexReg == X86::ESP || IndexReg == X86::RSP))
    std::swap(BaseReg, IndexReg);

  // If BaseReg is a vector register and IndexReg is not, swap them unless
  // Scale was specified in which case it would be an error.
  if (Scale == 0 &&
      !(X86MCRegisterClasses[X86::VR128XRegClassID].contains(IndexReg) ||
        X86MCRegisterClasses[X86::VR256XRegClassID].contains(IndexReg) ||
        X86MCRegisterClasses[X86::VR512RegClassID].contains(IndexReg)) &&
      (X86MCRegisterClasses[X86::VR128XRegClassID].contains(BaseReg) ||
       X86MCRegisterClasses[X86::VR256XRegClassID].contains(BaseReg) ||
       X86MCRegisterClasses[X86::VR512RegClassID].contains(BaseReg)))
    std::swap(BaseReg, IndexReg);

  if (Scale != 0 &&
      X86MCRegisterClasses[X86::GR16RegClassID].contains(IndexReg))
    return ErrorOperand(Start, "16-bit addresses cannot have a scale");

  // If there was no explicit scale specified, change it to 1.
  if (Scale == 0)
    Scale = 1;

  // If this is a 16-bit addressing mode with the base and index in the wrong
  // order, swap them so CheckBaseRegAndIndexRegAndScale doesn't fail. It is
  // shared with att syntax where order matters.
  if ((BaseReg == X86::SI || BaseReg == X86::DI) &&
      (IndexReg == X86::BX || IndexReg == X86::BP))
    std::swap(BaseReg, IndexReg);

  if ((BaseReg || IndexReg) &&
      CheckBaseRegAndIndexRegAndScale(BaseReg, IndexReg, Scale, is64BitMode(),
                                      ErrMsg))
    return ErrorOperand(Start, ErrMsg);
  if (isParsingInlineAsm())
    return CreateMemForInlineAsm(RegNo, Disp, BaseReg, IndexReg,
                                 Scale, Start, End, Size, SM.getSymName(),
                                 SM.getIdentifierInfo());
  if (!(BaseReg || IndexReg || RegNo))
    return X86Operand::CreateMem(getPointerWidth(), Disp, Start, End, Size);
  return X86Operand::CreateMem(getPointerWidth(), RegNo, Disp,
                               BaseReg, IndexReg, Scale, Start, End, Size);
}

std::unique_ptr<X86Operand> X86AsmParser::ParseATTOperand() {
  MCAsmParser &Parser = getParser();
  switch (getLexer().getKind()) {
  case AsmToken::Dollar: {
    // $42 or $ID -> immediate.
    SMLoc Start = Parser.getTok().getLoc(), End;
    Parser.Lex();
    const MCExpr *Val;
    // This is an immediate, so we should not parse a register. Do a precheck
    // for '%' to supercede intra-register parse errors.
    SMLoc L = Parser.getTok().getLoc();
    if (check(getLexer().is(AsmToken::Percent), L,
              "expected immediate expression") ||
        getParser().parseExpression(Val, End) ||
        check(isa<X86MCExpr>(Val), L, "expected immediate expression"))
      return nullptr;
    return X86Operand::CreateImm(Val, Start, End);
  }
  case AsmToken::LCurly: {
    SMLoc Start = Parser.getTok().getLoc();
    return ParseRoundingModeOp(Start);
  }
  default: {
    // This a memory operand or a register. We have some parsing complications
    // as a '(' may be part of an immediate expression or the addressing mode
    // block. This is complicated by the fact that an assembler-level variable
    // may refer either to a register or an immediate expression.

    SMLoc Loc = Parser.getTok().getLoc(), EndLoc;
    const MCExpr *Expr = nullptr;
    unsigned Reg = 0;
    if (getLexer().isNot(AsmToken::LParen)) {
      // No '(' so this is either a displacement expression or a register.
      if (Parser.parseExpression(Expr, EndLoc))
        return nullptr;
      if (auto *RE = dyn_cast<X86MCExpr>(Expr)) {
        // Segment Register. Reset Expr and copy value to register.
        Expr = nullptr;
        Reg = RE->getRegNo();

        // Sanity check register.
        if (Reg == X86::EIZ || Reg == X86::RIZ)
          return ErrorOperand(
              Loc, "%eiz and %riz can only be used as index registers",
              SMRange(Loc, EndLoc));
        if (Reg == X86::RIP)
          return ErrorOperand(Loc, "%rip can only be used as a base register",
                              SMRange(Loc, EndLoc));
        // Return register that are not segment prefixes immediately.
        if (!Parser.parseOptionalToken(AsmToken::Colon))
          return X86Operand::CreateReg(Reg, Loc, EndLoc);
        if (!X86MCRegisterClasses[X86::SEGMENT_REGRegClassID].contains(Reg))
          return ErrorOperand(Loc, "invalid segment register");
      }
    }
    // This is a Memory operand.
    return ParseMemOperand(Reg, Expr, Loc, EndLoc);
  }
  }
}

// true on failure, false otherwise
// If no {z} mark was found - Parser doesn't advance
bool X86AsmParser::ParseZ(std::unique_ptr<X86Operand> &Z,
                          const SMLoc &StartLoc) {
  MCAsmParser &Parser = getParser();
  // Assuming we are just pass the '{' mark, quering the next token
  // Searched for {z}, but none was found. Return false, as no parsing error was
  // encountered
  if (!(getLexer().is(AsmToken::Identifier) &&
        (getLexer().getTok().getIdentifier() == "z")))
    return false;
  Parser.Lex(); // Eat z
  // Query and eat the '}' mark
  if (!getLexer().is(AsmToken::RCurly))
    return Error(getLexer().getLoc(), "Expected } at this point");
  Parser.Lex(); // Eat '}'
  // Assign Z with the {z} mark opernad
  Z = X86Operand::CreateToken("{z}", StartLoc);
  return false;
}

// true on failure, false otherwise
bool X86AsmParser::HandleAVX512Operand(OperandVector &Operands,
                                       const MCParsedAsmOperand &Op) {
  MCAsmParser &Parser = getParser();
  if (getLexer().is(AsmToken::LCurly)) {
    // Eat "{" and mark the current place.
    const SMLoc consumedToken = consumeToken();
    // Distinguish {1to<NUM>} from {%k<NUM>}.
    if(getLexer().is(AsmToken::Integer)) {
      // Parse memory broadcasting ({1to<NUM>}).
      if (getLexer().getTok().getIntVal() != 1)
        return TokError("Expected 1to<NUM> at this point");
      Parser.Lex();  // Eat "1" of 1to8
      if (!getLexer().is(AsmToken::Identifier) ||
          !getLexer().getTok().getIdentifier().startswith("to"))
        return TokError("Expected 1to<NUM> at this point");
      // Recognize only reasonable suffixes.
      const char *BroadcastPrimitive =
        StringSwitch<const char*>(getLexer().getTok().getIdentifier())
          .Case("to2",  "{1to2}")
          .Case("to4",  "{1to4}")
          .Case("to8",  "{1to8}")
          .Case("to16", "{1to16}")
          .Default(nullptr);
      if (!BroadcastPrimitive)
        return TokError("Invalid memory broadcast primitive.");
      Parser.Lex();  // Eat "toN" of 1toN
      if (!getLexer().is(AsmToken::RCurly))
        return TokError("Expected } at this point");
      Parser.Lex();  // Eat "}"
      Operands.push_back(X86Operand::CreateToken(BroadcastPrimitive,
                                                 consumedToken));
      // No AVX512 specific primitives can pass
      // after memory broadcasting, so return.
      return false;
    } else {
      // Parse either {k}{z}, {z}{k}, {k} or {z}
      // last one have no meaning, but GCC accepts it
      // Currently, we're just pass a '{' mark
      std::unique_ptr<X86Operand> Z;
      if (ParseZ(Z, consumedToken))
        return true;
      // Reaching here means that parsing of the allegadly '{z}' mark yielded
      // no errors.
      // Query for the need of further parsing for a {%k<NUM>} mark
      if (!Z || getLexer().is(AsmToken::LCurly)) {
        SMLoc StartLoc = Z ? consumeToken() : consumedToken;
        // Parse an op-mask register mark ({%k<NUM>}), which is now to be
        // expected
        unsigned RegNo;
        SMLoc RegLoc;
        if (!ParseRegister(RegNo, RegLoc, StartLoc) &&
            X86MCRegisterClasses[X86::VK1RegClassID].contains(RegNo)) {
          if (RegNo == X86::K0)
            return Error(RegLoc, "Register k0 can't be used as write mask");
          if (!getLexer().is(AsmToken::RCurly))
            return Error(getLexer().getLoc(), "Expected } at this point");
          Operands.push_back(X86Operand::CreateToken("{", StartLoc));
          Operands.push_back(
              X86Operand::CreateReg(RegNo, StartLoc, StartLoc));
          Operands.push_back(X86Operand::CreateToken("}", consumeToken()));
        } else
          return Error(getLexer().getLoc(),
                        "Expected an op-mask register at this point");
        // {%k<NUM>} mark is found, inquire for {z}
        if (getLexer().is(AsmToken::LCurly) && !Z) {
          // Have we've found a parsing error, or found no (expected) {z} mark
          // - report an error
          if (ParseZ(Z, consumeToken()) || !Z)
            return Error(getLexer().getLoc(),
                         "Expected a {z} mark at this point");

        }
        // '{z}' on its own is meaningless, hence should be ignored.
        // on the contrary - have it been accompanied by a K register,
        // allow it.
        if (Z)
          Operands.push_back(std::move(Z));
      }
    }
  }
  return false;
}

/// ParseMemOperand: 'seg : disp(basereg, indexreg, scale)'.  The '%ds:' prefix
/// has already been parsed if present. disp may be provided as well.
std::unique_ptr<X86Operand> X86AsmParser::ParseMemOperand(unsigned SegReg,
                                                          const MCExpr *&Disp,
                                                          const SMLoc &StartLoc,
                                                          SMLoc &EndLoc) {
  MCAsmParser &Parser = getParser();
  SMLoc Loc;
  // Based on the initial passed values, we may be in any of these cases, we are
  // in one of these cases (with current position (*)):

  //   1. seg : * disp  (base-index-scale-expr)
  //   2. seg : *(disp) (base-index-scale-expr)
  //   3. seg :       *(base-index-scale-expr)
  //   4.        disp  *(base-index-scale-expr)
  //   5.      *(disp)  (base-index-scale-expr)
  //   6.             *(base-index-scale-expr)
  //   7.  disp *
  //   8. *(disp)

  // If we do not have an displacement yet, check if we're in cases 4 or 6 by
  // checking if the first object after the parenthesis is a register (or an
  // identifier referring to a register) and parse the displacement or default
  // to 0 as appropriate.
  auto isAtMemOperand = [this]() {
    if (this->getLexer().isNot(AsmToken::LParen))
      return false;
    AsmToken Buf[2];
    StringRef Id;
    auto TokCount = this->getLexer().peekTokens(Buf, true);
    if (TokCount == 0)
      return false;
    switch (Buf[0].getKind()) {
    case AsmToken::Percent:
    case AsmToken::Comma:
      return true;
    // These lower cases are doing a peekIdentifier.
    case AsmToken::At:
    case AsmToken::Dollar:
      if ((TokCount > 1) &&
          (Buf[1].is(AsmToken::Identifier) || Buf[1].is(AsmToken::String)) &&
          (Buf[0].getLoc().getPointer() + 1 == Buf[1].getLoc().getPointer()))
        Id = StringRef(Buf[0].getLoc().getPointer(),
                       Buf[1].getIdentifier().size() + 1);
      break;
    case AsmToken::Identifier:
    case AsmToken::String:
      Id = Buf[0].getIdentifier();
      break;
    default:
      return false;
    }
    // We have an ID. Check if it is bound to a register.
    if (!Id.empty()) {
      MCSymbol *Sym = this->getContext().getOrCreateSymbol(Id);
      if (Sym->isVariable()) {
        auto V = Sym->getVariableValue(/*SetUsed*/ false);
        return isa<X86MCExpr>(V);
      }
    }
    return false;
  };

  if (!Disp) {
    // Parse immediate if we're not at a mem operand yet.
    if (!isAtMemOperand()) {
      if (Parser.parseTokenLoc(Loc) || Parser.parseExpression(Disp, EndLoc))
        return nullptr;
      assert(!isa<X86MCExpr>(Disp) && "Expected non-register here.");
    } else {
      // Disp is implicitly zero if we haven't parsed it yet.
      Disp = MCConstantExpr::create(0, Parser.getContext());
    }
  }

  // We are now either at the end of the operand or at the '(' at the start of a
  // base-index-scale-expr.

  if (!parseOptionalToken(AsmToken::LParen)) {
    if (SegReg == 0)
      return X86Operand::CreateMem(getPointerWidth(), Disp, StartLoc, EndLoc);
    return X86Operand::CreateMem(getPointerWidth(), SegReg, Disp, 0, 0, 1,
                                 StartLoc, EndLoc);
  }

  // If we reached here, then eat the '(' and Process
  // the rest of the memory operand.
  unsigned BaseReg = 0, IndexReg = 0, Scale = 1;
  SMLoc BaseLoc = getLexer().getLoc();
  const MCExpr *E;
  StringRef ErrMsg;

  // Parse BaseReg if one is provided.
  if (getLexer().isNot(AsmToken::Comma) && getLexer().isNot(AsmToken::RParen)) {
    if (Parser.parseExpression(E, EndLoc) ||
        check(!isa<X86MCExpr>(E), BaseLoc, "expected register here"))
      return nullptr;

    // Sanity check register.
    BaseReg = cast<X86MCExpr>(E)->getRegNo();
    if (BaseReg == X86::EIZ || BaseReg == X86::RIZ)
      return ErrorOperand(BaseLoc,
                          "eiz and riz can only be used as index registers",
                          SMRange(BaseLoc, EndLoc));
  }

  if (parseOptionalToken(AsmToken::Comma)) {
    // Following the comma we should have either an index register, or a scale
    // value. We don't support the later form, but we want to parse it
    // correctly.
    //
    // Even though it would be completely consistent to support syntax like
    // "1(%eax,,1)", the assembler doesn't. Use "eiz" or "riz" for this.
    if (getLexer().isNot(AsmToken::RParen)) {
      if (Parser.parseTokenLoc(Loc) || Parser.parseExpression(E, EndLoc))
        return nullptr;

      if (!isa<X86MCExpr>(E)) {
        // We've parsed an unexpected Scale Value instead of an index
        // register. Interpret it as an absolute.
        int64_t ScaleVal;
        if (!E->evaluateAsAbsolute(ScaleVal, getStreamer().getAssemblerPtr()))
          return ErrorOperand(Loc, "expected absolute expression");
        if (ScaleVal != 1)
          Warning(Loc, "scale factor without index register is ignored");
        Scale = 1;
      } else { // IndexReg Found.
        IndexReg = cast<X86MCExpr>(E)->getRegNo();

        if (BaseReg == X86::RIP)
          return ErrorOperand(
              Loc, "%rip as base register can not have an index register");
        if (IndexReg == X86::RIP)
          return ErrorOperand(Loc, "%rip is not allowed as an index register");

        if (parseOptionalToken(AsmToken::Comma)) {
          // Parse the scale amount:
          //  ::= ',' [scale-expression]

          // A scale amount without an index is ignored.
          if (getLexer().isNot(AsmToken::RParen)) {
            int64_t ScaleVal;
            if (Parser.parseTokenLoc(Loc) ||
                Parser.parseAbsoluteExpression(ScaleVal))
              return ErrorOperand(Loc, "expected scale expression");
            Scale = (unsigned)ScaleVal;
            // Validate the scale amount.
            if (X86MCRegisterClasses[X86::GR16RegClassID].contains(BaseReg) &&
                Scale != 1)
              return ErrorOperand(Loc,
                                  "scale factor in 16-bit address must be 1");
            if (checkScale(Scale, ErrMsg))
              return ErrorOperand(Loc, ErrMsg);
          }
        }
      }
    }
  }

  // Ok, we've eaten the memory operand, verify we have a ')' and eat it too.
  if (parseToken(AsmToken::RParen, "unexpected token in memory operand"))
    return nullptr;

  // This is to support otherwise illegal operand (%dx) found in various
  // unofficial manuals examples (e.g. "out[s]?[bwl]? %al, (%dx)") and must now
  // be supported. Mark such DX variants separately fix only in special cases.
  if (BaseReg == X86::DX && IndexReg == 0 && Scale == 1 && SegReg == 0 &&
      isa<MCConstantExpr>(Disp) && cast<MCConstantExpr>(Disp)->getValue() == 0)
    return X86Operand::CreateDXReg(BaseLoc, BaseLoc);

  if (CheckBaseRegAndIndexRegAndScale(BaseReg, IndexReg, Scale, is64BitMode(),
                                      ErrMsg))
    return ErrorOperand(BaseLoc, ErrMsg);

  if (SegReg || BaseReg || IndexReg)
    return X86Operand::CreateMem(getPointerWidth(), SegReg, Disp, BaseReg,
                                 IndexReg, Scale, StartLoc, EndLoc);
  return X86Operand::CreateMem(getPointerWidth(), Disp, StartLoc, EndLoc);
}

// Parse either a standard primary expression or a register.
bool X86AsmParser::parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc) {
  MCAsmParser &Parser = getParser();
  // See if this is a register first.
  if (getTok().is(AsmToken::Percent) ||
      (isParsingIntelSyntax() && getTok().is(AsmToken::Identifier) &&
       MatchRegisterName(Parser.getTok().getString()))) {
    SMLoc StartLoc = Parser.getTok().getLoc();
    unsigned RegNo;
    if (ParseRegister(RegNo, StartLoc, EndLoc))
      return true;
    Res = X86MCExpr::create(RegNo, Parser.getContext());
    return false;
  }
  return Parser.parsePrimaryExpr(Res, EndLoc);
}

bool X86AsmParser::ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                                    SMLoc NameLoc, OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  InstInfo = &Info;
  StringRef PatchedName = Name;

  if ((Name.equals("jmp") || Name.equals("jc") || Name.equals("jz")) &&
      isParsingIntelSyntax() && isParsingInlineAsm()) {
    StringRef NextTok = Parser.getTok().getString();
    if (NextTok == "short") {
      SMLoc NameEndLoc =
          NameLoc.getFromPointer(NameLoc.getPointer() + Name.size());
      // Eat the short keyword
      Parser.Lex();
      // MS ignores the short keyword, it determines the jmp type based
      // on the distance of the label
      InstInfo->AsmRewrites->emplace_back(AOK_Skip, NameEndLoc,
                                          NextTok.size() + 1);
    }
  }

  // FIXME: Hack to recognize setneb as setne.
  if (PatchedName.startswith("set") && PatchedName.endswith("b") &&
      PatchedName != "setb" && PatchedName != "setnb")
    PatchedName = PatchedName.substr(0, Name.size()-1);

  // FIXME: Hack to recognize cmp<comparison code>{ss,sd,ps,pd}.
  if ((PatchedName.startswith("cmp") || PatchedName.startswith("vcmp")) &&
      (PatchedName.endswith("ss") || PatchedName.endswith("sd") ||
       PatchedName.endswith("ps") || PatchedName.endswith("pd"))) {
    bool IsVCMP = PatchedName[0] == 'v';
    unsigned CCIdx = IsVCMP ? 4 : 3;
    unsigned ComparisonCode = StringSwitch<unsigned>(
      PatchedName.slice(CCIdx, PatchedName.size() - 2))
      .Case("eq",       0x00)
      .Case("eq_oq",    0x00)
      .Case("lt",       0x01)
      .Case("lt_os",    0x01)
      .Case("le",       0x02)
      .Case("le_os",    0x02)
      .Case("unord",    0x03)
      .Case("unord_q",  0x03)
      .Case("neq",      0x04)
      .Case("neq_uq",   0x04)
      .Case("nlt",      0x05)
      .Case("nlt_us",   0x05)
      .Case("nle",      0x06)
      .Case("nle_us",   0x06)
      .Case("ord",      0x07)
      .Case("ord_q",    0x07)
      /* AVX only from here */
      .Case("eq_uq",    0x08)
      .Case("nge",      0x09)
      .Case("nge_us",   0x09)
      .Case("ngt",      0x0A)
      .Case("ngt_us",   0x0A)
      .Case("false",    0x0B)
      .Case("false_oq", 0x0B)
      .Case("neq_oq",   0x0C)
      .Case("ge",       0x0D)
      .Case("ge_os",    0x0D)
      .Case("gt",       0x0E)
      .Case("gt_os",    0x0E)
      .Case("true",     0x0F)
      .Case("true_uq",  0x0F)
      .Case("eq_os",    0x10)
      .Case("lt_oq",    0x11)
      .Case("le_oq",    0x12)
      .Case("unord_s",  0x13)
      .Case("neq_us",   0x14)
      .Case("nlt_uq",   0x15)
      .Case("nle_uq",   0x16)
      .Case("ord_s",    0x17)
      .Case("eq_us",    0x18)
      .Case("nge_uq",   0x19)
      .Case("ngt_uq",   0x1A)
      .Case("false_os", 0x1B)
      .Case("neq_os",   0x1C)
      .Case("ge_oq",    0x1D)
      .Case("gt_oq",    0x1E)
      .Case("true_us",  0x1F)
      .Default(~0U);
    if (ComparisonCode != ~0U && (IsVCMP || ComparisonCode < 8)) {

      Operands.push_back(X86Operand::CreateToken(PatchedName.slice(0, CCIdx),
                                                 NameLoc));

      const MCExpr *ImmOp = MCConstantExpr::create(ComparisonCode,
                                                   getParser().getContext());
      Operands.push_back(X86Operand::CreateImm(ImmOp, NameLoc, NameLoc));

      PatchedName = PatchedName.substr(PatchedName.size() - 2);
    }
  }

  // FIXME: Hack to recognize vpcmp<comparison code>{ub,uw,ud,uq,b,w,d,q}.
  if (PatchedName.startswith("vpcmp") &&
      (PatchedName.endswith("b") || PatchedName.endswith("w") ||
       PatchedName.endswith("d") || PatchedName.endswith("q"))) {
    unsigned CCIdx = PatchedName.drop_back().back() == 'u' ? 2 : 1;
    unsigned ComparisonCode = StringSwitch<unsigned>(
      PatchedName.slice(5, PatchedName.size() - CCIdx))
      .Case("eq",    0x0) // Only allowed on unsigned. Checked below.
      .Case("lt",    0x1)
      .Case("le",    0x2)
      //.Case("false", 0x3) // Not a documented alias.
      .Case("neq",   0x4)
      .Case("nlt",   0x5)
      .Case("nle",   0x6)
      //.Case("true",  0x7) // Not a documented alias.
      .Default(~0U);
    if (ComparisonCode != ~0U && (ComparisonCode != 0 || CCIdx == 2)) {
      Operands.push_back(X86Operand::CreateToken("vpcmp", NameLoc));

      const MCExpr *ImmOp = MCConstantExpr::create(ComparisonCode,
                                                   getParser().getContext());
      Operands.push_back(X86Operand::CreateImm(ImmOp, NameLoc, NameLoc));

      PatchedName = PatchedName.substr(PatchedName.size() - CCIdx);
    }
  }

  // FIXME: Hack to recognize vpcom<comparison code>{ub,uw,ud,uq,b,w,d,q}.
  if (PatchedName.startswith("vpcom") &&
      (PatchedName.endswith("b") || PatchedName.endswith("w") ||
       PatchedName.endswith("d") || PatchedName.endswith("q"))) {
    unsigned CCIdx = PatchedName.drop_back().back() == 'u' ? 2 : 1;
    unsigned ComparisonCode = StringSwitch<unsigned>(
      PatchedName.slice(5, PatchedName.size() - CCIdx))
      .Case("lt",    0x0)
      .Case("le",    0x1)
      .Case("gt",    0x2)
      .Case("ge",    0x3)
      .Case("eq",    0x4)
      .Case("neq",   0x5)
      .Case("false", 0x6)
      .Case("true",  0x7)
      .Default(~0U);
    if (ComparisonCode != ~0U) {
      Operands.push_back(X86Operand::CreateToken("vpcom", NameLoc));

      const MCExpr *ImmOp = MCConstantExpr::create(ComparisonCode,
                                                   getParser().getContext());
      Operands.push_back(X86Operand::CreateImm(ImmOp, NameLoc, NameLoc));

      PatchedName = PatchedName.substr(PatchedName.size() - CCIdx);
    }
  }


  // Determine whether this is an instruction prefix.
  // FIXME:
  // Enhance prefixes integrity robustness. for example, following forms
  // are currently tolerated:
  // repz repnz <insn>    ; GAS errors for the use of two similar prefixes
  // lock addq %rax, %rbx ; Destination operand must be of memory type
  // xacquire <insn>      ; xacquire must be accompanied by 'lock'
  bool isPrefix = StringSwitch<bool>(Name)
                      .Cases("rex64", "data32", "data16", true)
                      .Cases("xacquire", "xrelease", true)
                      .Cases("acquire", "release", isParsingIntelSyntax())
                      .Default(false);

  auto isLockRepeatNtPrefix = [](StringRef N) {
    return StringSwitch<bool>(N)
        .Cases("lock", "rep", "repe", "repz", "repne", "repnz", "notrack", true)
        .Default(false);
  };

  bool CurlyAsEndOfStatement = false;

  unsigned Flags = X86::IP_NO_PREFIX;
  while (isLockRepeatNtPrefix(Name.lower())) {
    unsigned Prefix =
        StringSwitch<unsigned>(Name)
            .Cases("lock", "lock", X86::IP_HAS_LOCK)
            .Cases("rep", "repe", "repz", X86::IP_HAS_REPEAT)
            .Cases("repne", "repnz", X86::IP_HAS_REPEAT_NE)
            .Cases("notrack", "notrack", X86::IP_HAS_NOTRACK)
            .Default(X86::IP_NO_PREFIX); // Invalid prefix (impossible)
    Flags |= Prefix;
    if (getLexer().is(AsmToken::EndOfStatement)) {
      // We don't have real instr with the given prefix
      //  let's use the prefix as the instr.
      // TODO: there could be several prefixes one after another
      Flags = X86::IP_NO_PREFIX;
      break;
    }
    Name = Parser.getTok().getString();
    Parser.Lex(); // eat the prefix
    // Hack: we could have something like "rep # some comment" or
    //    "lock; cmpxchg16b $1" or "lock\0A\09incl" or "lock/incl"
    while (Name.startswith(";") || Name.startswith("\n") ||
           Name.startswith("#") || Name.startswith("\t") ||
           Name.startswith("/")) {
      Name = Parser.getTok().getString();
      Parser.Lex(); // go to next prefix or instr
    }
  }

  if (Flags)
    PatchedName = Name;

  // Hacks to handle 'data16' and 'data32'
  if (PatchedName == "data16" && is16BitMode()) {
    return Error(NameLoc, "redundant data16 prefix");
  }
  if (PatchedName == "data32") {
    if (is32BitMode())
      return Error(NameLoc, "redundant data32 prefix");
    if (is64BitMode())
      return Error(NameLoc, "'data32' is not supported in 64-bit mode");
    // Hack to 'data16' for the table lookup.
    PatchedName = "data16";
  }

  Operands.push_back(X86Operand::CreateToken(PatchedName, NameLoc));

  // This does the actual operand parsing.  Don't parse any more if we have a
  // prefix juxtaposed with an operation like "lock incl 4(%rax)", because we
  // just want to parse the "lock" as the first instruction and the "incl" as
  // the next one.
  if (getLexer().isNot(AsmToken::EndOfStatement) && !isPrefix) {
    // Parse '*' modifier.
    if (getLexer().is(AsmToken::Star))
      Operands.push_back(X86Operand::CreateToken("*", consumeToken()));

    // Read the operands.
    while(1) {
      if (std::unique_ptr<X86Operand> Op = ParseOperand()) {
        Operands.push_back(std::move(Op));
        if (HandleAVX512Operand(Operands, *Operands.back()))
          return true;
      } else {
         return true;
      }
      // check for comma and eat it
      if (getLexer().is(AsmToken::Comma))
        Parser.Lex();
      else
        break;
     }

    // In MS inline asm curly braces mark the beginning/end of a block,
    // therefore they should be interepreted as end of statement
    CurlyAsEndOfStatement =
        isParsingIntelSyntax() && isParsingInlineAsm() &&
        (getLexer().is(AsmToken::LCurly) || getLexer().is(AsmToken::RCurly));
    if (getLexer().isNot(AsmToken::EndOfStatement) && !CurlyAsEndOfStatement)
      return TokError("unexpected token in argument list");
  }

  // Consume the EndOfStatement or the prefix separator Slash
  if (getLexer().is(AsmToken::EndOfStatement) ||
      (isPrefix && getLexer().is(AsmToken::Slash)))
    Parser.Lex();
  else if (CurlyAsEndOfStatement)
    // Add an actual EndOfStatement before the curly brace
    Info.AsmRewrites->emplace_back(AOK_EndOfStatement,
                                   getLexer().getTok().getLoc(), 0);

  // This is for gas compatibility and cannot be done in td.
  // Adding "p" for some floating point with no argument.
  // For example: fsub --> fsubp
  bool IsFp =
    Name == "fsub" || Name == "fdiv" || Name == "fsubr" || Name == "fdivr";
  if (IsFp && Operands.size() == 1) {
    const char *Repl = StringSwitch<const char *>(Name)
      .Case("fsub", "fsubp")
      .Case("fdiv", "fdivp")
      .Case("fsubr", "fsubrp")
      .Case("fdivr", "fdivrp");
    static_cast<X86Operand &>(*Operands[0]).setTokenValue(Repl);
  }

  // Moving a 32 or 16 bit value into a segment register has the same
  // behavior. Modify such instructions to always take shorter form.
  if ((Name == "mov" || Name == "movw" || Name == "movl") &&
      (Operands.size() == 3)) {
    X86Operand &Op1 = (X86Operand &)*Operands[1];
    X86Operand &Op2 = (X86Operand &)*Operands[2];
    SMLoc Loc = Op1.getEndLoc();
    if (Op1.isReg() && Op2.isReg() &&
        X86MCRegisterClasses[X86::SEGMENT_REGRegClassID].contains(
            Op2.getReg()) &&
        (X86MCRegisterClasses[X86::GR16RegClassID].contains(Op1.getReg()) ||
         X86MCRegisterClasses[X86::GR32RegClassID].contains(Op1.getReg()))) {
      // Change instruction name to match new instruction.
      if (Name != "mov" && Name[3] == (is16BitMode() ? 'l' : 'w')) {
        Name = is16BitMode() ? "movw" : "movl";
        Operands[0] = X86Operand::CreateToken(Name, NameLoc);
      }
      // Select the correct equivalent 16-/32-bit source register.
      unsigned Reg =
          getX86SubSuperRegisterOrZero(Op1.getReg(), is16BitMode() ? 16 : 32);
      Operands[1] = X86Operand::CreateReg(Reg, Loc, Loc);
    }
  }

  // This is a terrible hack to handle "out[s]?[bwl]? %al, (%dx)" ->
  // "outb %al, %dx".  Out doesn't take a memory form, but this is a widely
  // documented form in various unofficial manuals, so a lot of code uses it.
  if ((Name == "outb" || Name == "outsb" || Name == "outw" || Name == "outsw" ||
       Name == "outl" || Name == "outsl" || Name == "out" || Name == "outs") &&
      Operands.size() == 3) {
    X86Operand &Op = (X86Operand &)*Operands.back();
    if (Op.isDXReg())
      Operands.back() = X86Operand::CreateReg(X86::DX, Op.getStartLoc(),
                                              Op.getEndLoc());
  }
  // Same hack for "in[s]?[bwl]? (%dx), %al" -> "inb %dx, %al".
  if ((Name == "inb" || Name == "insb" || Name == "inw" || Name == "insw" ||
       Name == "inl" || Name == "insl" || Name == "in" || Name == "ins") &&
      Operands.size() == 3) {
    X86Operand &Op = (X86Operand &)*Operands[1];
    if (Op.isDXReg())
      Operands[1] = X86Operand::CreateReg(X86::DX, Op.getStartLoc(),
                                          Op.getEndLoc());
  }

  SmallVector<std::unique_ptr<MCParsedAsmOperand>, 2> TmpOperands;
  bool HadVerifyError = false;

  // Append default arguments to "ins[bwld]"
  if (Name.startswith("ins") &&
      (Operands.size() == 1 || Operands.size() == 3) &&
      (Name == "insb" || Name == "insw" || Name == "insl" || Name == "insd" ||
       Name == "ins")) {

    AddDefaultSrcDestOperands(TmpOperands,
                              X86Operand::CreateReg(X86::DX, NameLoc, NameLoc),
                              DefaultMemDIOperand(NameLoc));
    HadVerifyError = VerifyAndAdjustOperands(Operands, TmpOperands);
  }

  // Append default arguments to "outs[bwld]"
  if (Name.startswith("outs") &&
      (Operands.size() == 1 || Operands.size() == 3) &&
      (Name == "outsb" || Name == "outsw" || Name == "outsl" ||
       Name == "outsd" || Name == "outs")) {
    AddDefaultSrcDestOperands(TmpOperands, DefaultMemSIOperand(NameLoc),
                              X86Operand::CreateReg(X86::DX, NameLoc, NameLoc));
    HadVerifyError = VerifyAndAdjustOperands(Operands, TmpOperands);
  }

  // Transform "lods[bwlq]" into "lods[bwlq] ($SIREG)" for appropriate
  // values of $SIREG according to the mode. It would be nice if this
  // could be achieved with InstAlias in the tables.
  if (Name.startswith("lods") &&
      (Operands.size() == 1 || Operands.size() == 2) &&
      (Name == "lods" || Name == "lodsb" || Name == "lodsw" ||
       Name == "lodsl" || Name == "lodsd" || Name == "lodsq")) {
    TmpOperands.push_back(DefaultMemSIOperand(NameLoc));
    HadVerifyError = VerifyAndAdjustOperands(Operands, TmpOperands);
  }

  // Transform "stos[bwlq]" into "stos[bwlq] ($DIREG)" for appropriate
  // values of $DIREG according to the mode. It would be nice if this
  // could be achieved with InstAlias in the tables.
  if (Name.startswith("stos") &&
      (Operands.size() == 1 || Operands.size() == 2) &&
      (Name == "stos" || Name == "stosb" || Name == "stosw" ||
       Name == "stosl" || Name == "stosd" || Name == "stosq")) {
    TmpOperands.push_back(DefaultMemDIOperand(NameLoc));
    HadVerifyError = VerifyAndAdjustOperands(Operands, TmpOperands);
  }

  // Transform "scas[bwlq]" into "scas[bwlq] ($DIREG)" for appropriate
  // values of $DIREG according to the mode. It would be nice if this
  // could be achieved with InstAlias in the tables.
  if (Name.startswith("scas") &&
      (Operands.size() == 1 || Operands.size() == 2) &&
      (Name == "scas" || Name == "scasb" || Name == "scasw" ||
       Name == "scasl" || Name == "scasd" || Name == "scasq")) {
    TmpOperands.push_back(DefaultMemDIOperand(NameLoc));
    HadVerifyError = VerifyAndAdjustOperands(Operands, TmpOperands);
  }

  // Add default SI and DI operands to "cmps[bwlq]".
  if (Name.startswith("cmps") &&
      (Operands.size() == 1 || Operands.size() == 3) &&
      (Name == "cmps" || Name == "cmpsb" || Name == "cmpsw" ||
       Name == "cmpsl" || Name == "cmpsd" || Name == "cmpsq")) {
    AddDefaultSrcDestOperands(TmpOperands, DefaultMemDIOperand(NameLoc),
                              DefaultMemSIOperand(NameLoc));
    HadVerifyError = VerifyAndAdjustOperands(Operands, TmpOperands);
  }

  // Add default SI and DI operands to "movs[bwlq]".
  if (((Name.startswith("movs") &&
        (Name == "movs" || Name == "movsb" || Name == "movsw" ||
         Name == "movsl" || Name == "movsd" || Name == "movsq")) ||
       (Name.startswith("smov") &&
        (Name == "smov" || Name == "smovb" || Name == "smovw" ||
         Name == "smovl" || Name == "smovd" || Name == "smovq"))) &&
      (Operands.size() == 1 || Operands.size() == 3)) {
    if (Name == "movsd" && Operands.size() == 1 && !isParsingIntelSyntax())
      Operands.back() = X86Operand::CreateToken("movsl", NameLoc);
    AddDefaultSrcDestOperands(TmpOperands, DefaultMemSIOperand(NameLoc),
                              DefaultMemDIOperand(NameLoc));
    HadVerifyError = VerifyAndAdjustOperands(Operands, TmpOperands);
  }

  // Check if we encountered an error for one the string insturctions
  if (HadVerifyError) {
    return HadVerifyError;
  }

  // FIXME: Hack to handle recognize s{hr,ar,hl} $1, <op>.  Canonicalize to
  // "shift <op>".
  if ((Name.startswith("shr") || Name.startswith("sar") ||
       Name.startswith("shl") || Name.startswith("sal") ||
       Name.startswith("rcl") || Name.startswith("rcr") ||
       Name.startswith("rol") || Name.startswith("ror")) &&
      Operands.size() == 3) {
    if (isParsingIntelSyntax()) {
      // Intel syntax
      X86Operand &Op1 = static_cast<X86Operand &>(*Operands[2]);
      if (Op1.isImm() && isa<MCConstantExpr>(Op1.getImm()) &&
          cast<MCConstantExpr>(Op1.getImm())->getValue() == 1)
        Operands.pop_back();
    } else {
      X86Operand &Op1 = static_cast<X86Operand &>(*Operands[1]);
      if (Op1.isImm() && isa<MCConstantExpr>(Op1.getImm()) &&
          cast<MCConstantExpr>(Op1.getImm())->getValue() == 1)
        Operands.erase(Operands.begin() + 1);
    }
  }

  // Transforms "int $3" into "int3" as a size optimization.  We can't write an
  // instalias with an immediate operand yet.
  if (Name == "int" && Operands.size() == 2) {
    X86Operand &Op1 = static_cast<X86Operand &>(*Operands[1]);
    if (Op1.isImm())
      if (auto *CE = dyn_cast<MCConstantExpr>(Op1.getImm()))
        if (CE->getValue() == 3) {
          Operands.erase(Operands.begin() + 1);
          static_cast<X86Operand &>(*Operands[0]).setTokenValue("int3");
        }
  }

  // Transforms "xlat mem8" into "xlatb"
  if ((Name == "xlat" || Name == "xlatb") && Operands.size() == 2) {
    X86Operand &Op1 = static_cast<X86Operand &>(*Operands[1]);
    if (Op1.isMem8()) {
      Warning(Op1.getStartLoc(), "memory operand is only for determining the "
                                 "size, (R|E)BX will be used for the location");
      Operands.pop_back();
      static_cast<X86Operand &>(*Operands[0]).setTokenValue("xlatb");
    }
  }

  if (Flags)
    Operands.push_back(X86Operand::CreatePrefix(Flags, NameLoc, NameLoc));
  return false;
}

bool X86AsmParser::processInstruction(MCInst &Inst, const OperandVector &Ops) {
  return false;
}

bool X86AsmParser::validateInstruction(MCInst &Inst, const OperandVector &Ops) {
  const MCRegisterInfo *MRI = getContext().getRegisterInfo();

  switch (Inst.getOpcode()) {
  case X86::VGATHERDPDYrm:
  case X86::VGATHERDPDrm:
  case X86::VGATHERDPSYrm:
  case X86::VGATHERDPSrm:
  case X86::VGATHERQPDYrm:
  case X86::VGATHERQPDrm:
  case X86::VGATHERQPSYrm:
  case X86::VGATHERQPSrm:
  case X86::VPGATHERDDYrm:
  case X86::VPGATHERDDrm:
  case X86::VPGATHERDQYrm:
  case X86::VPGATHERDQrm:
  case X86::VPGATHERQDYrm:
  case X86::VPGATHERQDrm:
  case X86::VPGATHERQQYrm:
  case X86::VPGATHERQQrm: {
    unsigned Dest = MRI->getEncodingValue(Inst.getOperand(0).getReg());
    unsigned Mask = MRI->getEncodingValue(Inst.getOperand(1).getReg());
    unsigned Index =
      MRI->getEncodingValue(Inst.getOperand(3 + X86::AddrIndexReg).getReg());
    if (Dest == Mask || Dest == Index || Mask == Index)
      return Warning(Ops[0]->getStartLoc(), "mask, index, and destination "
                                            "registers should be distinct");
    break;
  }
  case X86::VGATHERDPDZ128rm:
  case X86::VGATHERDPDZ256rm:
  case X86::VGATHERDPDZrm:
  case X86::VGATHERDPSZ128rm:
  case X86::VGATHERDPSZ256rm:
  case X86::VGATHERDPSZrm:
  case X86::VGATHERQPDZ128rm:
  case X86::VGATHERQPDZ256rm:
  case X86::VGATHERQPDZrm:
  case X86::VGATHERQPSZ128rm:
  case X86::VGATHERQPSZ256rm:
  case X86::VGATHERQPSZrm:
  case X86::VPGATHERDDZ128rm:
  case X86::VPGATHERDDZ256rm:
  case X86::VPGATHERDDZrm:
  case X86::VPGATHERDQZ128rm:
  case X86::VPGATHERDQZ256rm:
  case X86::VPGATHERDQZrm:
  case X86::VPGATHERQDZ128rm:
  case X86::VPGATHERQDZ256rm:
  case X86::VPGATHERQDZrm:
  case X86::VPGATHERQQZ128rm:
  case X86::VPGATHERQQZ256rm:
  case X86::VPGATHERQQZrm: {
    unsigned Dest = MRI->getEncodingValue(Inst.getOperand(0).getReg());
    unsigned Index =
      MRI->getEncodingValue(Inst.getOperand(4 + X86::AddrIndexReg).getReg());
    if (Dest == Index)
      return Warning(Ops[0]->getStartLoc(), "index and destination registers "
                                            "should be distinct");
    break;
  }
  case X86::V4FMADDPSrm:
  case X86::V4FMADDPSrmk:
  case X86::V4FMADDPSrmkz:
  case X86::V4FMADDSSrm:
  case X86::V4FMADDSSrmk:
  case X86::V4FMADDSSrmkz:
  case X86::V4FNMADDPSrm:
  case X86::V4FNMADDPSrmk:
  case X86::V4FNMADDPSrmkz:
  case X86::V4FNMADDSSrm:
  case X86::V4FNMADDSSrmk:
  case X86::V4FNMADDSSrmkz:
  case X86::VP4DPWSSDSrm:
  case X86::VP4DPWSSDSrmk:
  case X86::VP4DPWSSDSrmkz:
  case X86::VP4DPWSSDrm:
  case X86::VP4DPWSSDrmk:
  case X86::VP4DPWSSDrmkz: {
    unsigned Src2 = Inst.getOperand(Inst.getNumOperands() -
                                    X86::AddrNumOperands - 1).getReg();
    unsigned Src2Enc = MRI->getEncodingValue(Src2);
    if (Src2Enc % 4 != 0) {
      StringRef RegName = X86IntelInstPrinter::getRegisterName(Src2);
      unsigned GroupStart = (Src2Enc / 4) * 4;
      unsigned GroupEnd = GroupStart + 3;
      return Warning(Ops[0]->getStartLoc(),
                     "source register '" + RegName + "' implicitly denotes '" +
                     RegName.take_front(3) + Twine(GroupStart) + "' to '" +
                     RegName.take_front(3) + Twine(GroupEnd) +
                     "' source group");
    }
    break;
  }
  }

  return false;
}

static const char *getSubtargetFeatureName(uint64_t Val);

void X86AsmParser::EmitInstruction(MCInst &Inst, OperandVector &Operands,
                                   MCStreamer &Out) {
  Instrumentation->InstrumentAndEmitInstruction(
      Inst, Operands, getContext(), MII, Out,
      getParser().shouldPrintSchedInfo());
}

bool X86AsmParser::MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                           OperandVector &Operands,
                                           MCStreamer &Out, uint64_t &ErrorInfo,
                                           bool MatchingInlineAsm) {
  if (isParsingIntelSyntax())
    return MatchAndEmitIntelInstruction(IDLoc, Opcode, Operands, Out, ErrorInfo,
                                        MatchingInlineAsm);
  return MatchAndEmitATTInstruction(IDLoc, Opcode, Operands, Out, ErrorInfo,
                                    MatchingInlineAsm);
}

void X86AsmParser::MatchFPUWaitAlias(SMLoc IDLoc, X86Operand &Op,
                                     OperandVector &Operands, MCStreamer &Out,
                                     bool MatchingInlineAsm) {
  // FIXME: This should be replaced with a real .td file alias mechanism.
  // Also, MatchInstructionImpl should actually *do* the EmitInstruction
  // call.
  const char *Repl = StringSwitch<const char *>(Op.getToken())
                         .Case("finit", "fninit")
                         .Case("fsave", "fnsave")
                         .Case("fstcw", "fnstcw")
                         .Case("fstcww", "fnstcw")
                         .Case("fstenv", "fnstenv")
                         .Case("fstsw", "fnstsw")
                         .Case("fstsww", "fnstsw")
                         .Case("fclex", "fnclex")
                         .Default(nullptr);
  if (Repl) {
    MCInst Inst;
    Inst.setOpcode(X86::WAIT);
    Inst.setLoc(IDLoc);
    if (!MatchingInlineAsm)
      EmitInstruction(Inst, Operands, Out);
    Operands[0] = X86Operand::CreateToken(Repl, IDLoc);
  }
}

bool X86AsmParser::ErrorMissingFeature(SMLoc IDLoc, uint64_t ErrorInfo,
                                       bool MatchingInlineAsm) {
  assert(ErrorInfo && "Unknown missing feature!");
  SmallString<126> Msg;
  raw_svector_ostream OS(Msg);
  OS << "instruction requires:";
  uint64_t Mask = 1;
  for (unsigned i = 0; i < (sizeof(ErrorInfo)*8-1); ++i) {
    if (ErrorInfo & Mask)
      OS << ' ' << getSubtargetFeatureName(ErrorInfo & Mask);
    Mask <<= 1;
  }
  return Error(IDLoc, OS.str(), SMRange(), MatchingInlineAsm);
}

static unsigned getPrefixes(OperandVector &Operands) {
  unsigned Result = 0;
  X86Operand &Prefix = static_cast<X86Operand &>(*Operands.back());
  if (Prefix.isPrefix()) {
    Result = Prefix.getPrefix();
    Operands.pop_back();
  }
  return Result;
}

bool X86AsmParser::MatchAndEmitATTInstruction(SMLoc IDLoc, unsigned &Opcode,
                                              OperandVector &Operands,
                                              MCStreamer &Out,
                                              uint64_t &ErrorInfo,
                                              bool MatchingInlineAsm) {
  assert(!Operands.empty() && "Unexpect empty operand list!");
  X86Operand &Op = static_cast<X86Operand &>(*Operands[0]);
  assert(Op.isToken() && "Leading operand should always be a mnemonic!");
  SMRange EmptyRange = None;

  // First, handle aliases that expand to multiple instructions.
  MatchFPUWaitAlias(IDLoc, Op, Operands, Out, MatchingInlineAsm);

  bool WasOriginallyInvalidOperand = false;
  unsigned Prefixes = getPrefixes(Operands);

  MCInst Inst;

  if (Prefixes)
    Inst.setFlags(Prefixes);

  // First, try a direct match.
  switch (MatchInstruction(Operands, Inst, ErrorInfo, MatchingInlineAsm,
                           isParsingIntelSyntax())) {
  default: llvm_unreachable("Unexpected match result!");
  case Match_Success:
    if (!MatchingInlineAsm && validateInstruction(Inst, Operands))
      return true;
    // Some instructions need post-processing to, for example, tweak which
    // encoding is selected. Loop on it while changes happen so the
    // individual transformations can chain off each other.
    if (!MatchingInlineAsm)
      while (processInstruction(Inst, Operands))
        ;

    Inst.setLoc(IDLoc);
    if (!MatchingInlineAsm)
      EmitInstruction(Inst, Operands, Out);
    Opcode = Inst.getOpcode();
    return false;
  case Match_MissingFeature:
    return ErrorMissingFeature(IDLoc, ErrorInfo, MatchingInlineAsm);
  case Match_InvalidOperand:
    WasOriginallyInvalidOperand = true;
    break;
  case Match_MnemonicFail:
    break;
  }

  // FIXME: Ideally, we would only attempt suffix matches for things which are
  // valid prefixes, and we could just infer the right unambiguous
  // type. However, that requires substantially more matcher support than the
  // following hack.

  // Change the operand to point to a temporary token.
  StringRef Base = Op.getToken();
  SmallString<16> Tmp;
  Tmp += Base;
  Tmp += ' ';
  Op.setTokenValue(Tmp);

  // If this instruction starts with an 'f', then it is a floating point stack
  // instruction.  These come in up to three forms for 32-bit, 64-bit, and
  // 80-bit floating point, which use the suffixes s,l,t respectively.
  //
  // Otherwise, we assume that this may be an integer instruction, which comes
  // in 8/16/32/64-bit forms using the b,w,l,q suffixes respectively.
  const char *Suffixes = Base[0] != 'f' ? "bwlq" : "slt\0";

  // Check for the various suffix matches.
  uint64_t ErrorInfoIgnore;
  uint64_t ErrorInfoMissingFeature = 0; // Init suppresses compiler warnings.
  unsigned Match[4];

  for (unsigned I = 0, E = array_lengthof(Match); I != E; ++I) {
    Tmp.back() = Suffixes[I];
    Match[I] = MatchInstruction(Operands, Inst, ErrorInfoIgnore,
                                MatchingInlineAsm, isParsingIntelSyntax());
    // If this returned as a missing feature failure, remember that.
    if (Match[I] == Match_MissingFeature)
      ErrorInfoMissingFeature = ErrorInfoIgnore;
  }

  // Restore the old token.
  Op.setTokenValue(Base);

  // If exactly one matched, then we treat that as a successful match (and the
  // instruction will already have been filled in correctly, since the failing
  // matches won't have modified it).
  unsigned NumSuccessfulMatches =
      std::count(std::begin(Match), std::end(Match), Match_Success);
  if (NumSuccessfulMatches == 1) {
    Inst.setLoc(IDLoc);
    if (!MatchingInlineAsm)
      EmitInstruction(Inst, Operands, Out);
    Opcode = Inst.getOpcode();
    return false;
  }

  // Otherwise, the match failed, try to produce a decent error message.

  // If we had multiple suffix matches, then identify this as an ambiguous
  // match.
  if (NumSuccessfulMatches > 1) {
    char MatchChars[4];
    unsigned NumMatches = 0;
    for (unsigned I = 0, E = array_lengthof(Match); I != E; ++I)
      if (Match[I] == Match_Success)
        MatchChars[NumMatches++] = Suffixes[I];

    SmallString<126> Msg;
    raw_svector_ostream OS(Msg);
    OS << "ambiguous instructions require an explicit suffix (could be ";
    for (unsigned i = 0; i != NumMatches; ++i) {
      if (i != 0)
        OS << ", ";
      if (i + 1 == NumMatches)
        OS << "or ";
      OS << "'" << Base << MatchChars[i] << "'";
    }
    OS << ")";
    Error(IDLoc, OS.str(), EmptyRange, MatchingInlineAsm);
    return true;
  }

  // Okay, we know that none of the variants matched successfully.

  // If all of the instructions reported an invalid mnemonic, then the original
  // mnemonic was invalid.
  if (std::count(std::begin(Match), std::end(Match), Match_MnemonicFail) == 4) {
    if (!WasOriginallyInvalidOperand) {
      return Error(IDLoc, "invalid instruction mnemonic '" + Base + "'",
                   Op.getLocRange(), MatchingInlineAsm);
    }

    // Recover location info for the operand if we know which was the problem.
    if (ErrorInfo != ~0ULL) {
      if (ErrorInfo >= Operands.size())
        return Error(IDLoc, "too few operands for instruction", EmptyRange,
                     MatchingInlineAsm);

      X86Operand &Operand = (X86Operand &)*Operands[ErrorInfo];
      if (Operand.getStartLoc().isValid()) {
        SMRange OperandRange = Operand.getLocRange();
        return Error(Operand.getStartLoc(), "invalid operand for instruction",
                     OperandRange, MatchingInlineAsm);
      }
    }

    return Error(IDLoc, "invalid operand for instruction", EmptyRange,
                 MatchingInlineAsm);
  }

  // If one instruction matched with a missing feature, report this as a
  // missing feature.
  if (std::count(std::begin(Match), std::end(Match),
                 Match_MissingFeature) == 1) {
    ErrorInfo = ErrorInfoMissingFeature;
    return ErrorMissingFeature(IDLoc, ErrorInfoMissingFeature,
                               MatchingInlineAsm);
  }

  // If one instruction matched with an invalid operand, report this as an
  // operand failure.
  if (std::count(std::begin(Match), std::end(Match),
                 Match_InvalidOperand) == 1) {
    return Error(IDLoc, "invalid operand for instruction", EmptyRange,
                 MatchingInlineAsm);
  }

  // If all of these were an outright failure, report it in a useless way.
  Error(IDLoc, "unknown use of instruction mnemonic without a size suffix",
        EmptyRange, MatchingInlineAsm);
  return true;
}

bool X86AsmParser::MatchAndEmitIntelInstruction(SMLoc IDLoc, unsigned &Opcode,
                                                OperandVector &Operands,
                                                MCStreamer &Out,
                                                uint64_t &ErrorInfo,
                                                bool MatchingInlineAsm) {
  assert(!Operands.empty() && "Unexpect empty operand list!");
  X86Operand &Op = static_cast<X86Operand &>(*Operands[0]);
  assert(Op.isToken() && "Leading operand should always be a mnemonic!");
  StringRef Mnemonic = Op.getToken();
  SMRange EmptyRange = None;
  StringRef Base = Op.getToken();
  unsigned Prefixes = getPrefixes(Operands);

  // First, handle aliases that expand to multiple instructions.
  MatchFPUWaitAlias(IDLoc, Op, Operands, Out, MatchingInlineAsm);

  MCInst Inst;

  if (Prefixes)
    Inst.setFlags(Prefixes);

  // Find one unsized memory operand, if present.
  X86Operand *UnsizedMemOp = nullptr;
  for (const auto &Op : Operands) {
    X86Operand *X86Op = static_cast<X86Operand *>(Op.get());
    if (X86Op->isMemUnsized()) {
      UnsizedMemOp = X86Op;
      // Have we found an unqualified memory operand,
      // break. IA allows only one memory operand.
      break;
    }
  }

  // Allow some instructions to have implicitly pointer-sized operands.  This is
  // compatible with gas.
  if (UnsizedMemOp) {
    static const char *const PtrSizedInstrs[] = {"call", "jmp", "push"};
    for (const char *Instr : PtrSizedInstrs) {
      if (Mnemonic == Instr) {
        UnsizedMemOp->Mem.Size = getPointerWidth();
        break;
      }
    }
  }

  SmallVector<unsigned, 8> Match;
  uint64_t ErrorInfoMissingFeature = 0;

  // If unsized push has immediate operand we should default the default pointer
  // size for the size.
  if (Mnemonic == "push" && Operands.size() == 2) {
    auto *X86Op = static_cast<X86Operand *>(Operands[1].get());
    if (X86Op->isImm()) {
      // If it's not a constant fall through and let remainder take care of it.
      const auto *CE = dyn_cast<MCConstantExpr>(X86Op->getImm());
      unsigned Size = getPointerWidth();
      if (CE &&
          (isIntN(Size, CE->getValue()) || isUIntN(Size, CE->getValue()))) {
        SmallString<16> Tmp;
        Tmp += Base;
        Tmp += (is64BitMode())
                   ? "q"
                   : (is32BitMode()) ? "l" : (is16BitMode()) ? "w" : " ";
        Op.setTokenValue(Tmp);
        // Do match in ATT mode to allow explicit suffix usage.
        Match.push_back(MatchInstruction(Operands, Inst, ErrorInfo,
                                         MatchingInlineAsm,
                                         false /*isParsingIntelSyntax()*/));
        Op.setTokenValue(Base);
      }
    }
  }

  // If an unsized memory operand is present, try to match with each memory
  // operand size.  In Intel assembly, the size is not part of the instruction
  // mnemonic.
  if (UnsizedMemOp && UnsizedMemOp->isMemUnsized()) {
    static const unsigned MopSizes[] = {8, 16, 32, 64, 80, 128, 256, 512};
    for (unsigned Size : MopSizes) {
      UnsizedMemOp->Mem.Size = Size;
      uint64_t ErrorInfoIgnore;
      unsigned LastOpcode = Inst.getOpcode();
      unsigned M = MatchInstruction(Operands, Inst, ErrorInfoIgnore,
                                    MatchingInlineAsm, isParsingIntelSyntax());
      if (Match.empty() || LastOpcode != Inst.getOpcode())
        Match.push_back(M);

      // If this returned as a missing feature failure, remember that.
      if (Match.back() == Match_MissingFeature)
        ErrorInfoMissingFeature = ErrorInfoIgnore;
    }

    // Restore the size of the unsized memory operand if we modified it.
    UnsizedMemOp->Mem.Size = 0;
  }

  // If we haven't matched anything yet, this is not a basic integer or FPU
  // operation.  There shouldn't be any ambiguity in our mnemonic table, so try
  // matching with the unsized operand.
  if (Match.empty()) {
    Match.push_back(MatchInstruction(
        Operands, Inst, ErrorInfo, MatchingInlineAsm, isParsingIntelSyntax()));
    // If this returned as a missing feature failure, remember that.
    if (Match.back() == Match_MissingFeature)
      ErrorInfoMissingFeature = ErrorInfo;
  }

  // Restore the size of the unsized memory operand if we modified it.
  if (UnsizedMemOp)
    UnsizedMemOp->Mem.Size = 0;

  // If it's a bad mnemonic, all results will be the same.
  if (Match.back() == Match_MnemonicFail) {
    return Error(IDLoc, "invalid instruction mnemonic '" + Mnemonic + "'",
                 Op.getLocRange(), MatchingInlineAsm);
  }

  unsigned NumSuccessfulMatches =
      std::count(std::begin(Match), std::end(Match), Match_Success);

  // If matching was ambiguous and we had size information from the frontend,
  // try again with that. This handles cases like "movxz eax, m8/m16".
  if (UnsizedMemOp && NumSuccessfulMatches > 1 &&
      UnsizedMemOp->getMemFrontendSize()) {
    UnsizedMemOp->Mem.Size = UnsizedMemOp->getMemFrontendSize();
    unsigned M = MatchInstruction(
        Operands, Inst, ErrorInfo, MatchingInlineAsm, isParsingIntelSyntax());
    if (M == Match_Success)
      NumSuccessfulMatches = 1;

    // Add a rewrite that encodes the size information we used from the
    // frontend.
    InstInfo->AsmRewrites->emplace_back(
        AOK_SizeDirective, UnsizedMemOp->getStartLoc(),
        /*Len=*/0, UnsizedMemOp->getMemFrontendSize());
  }

  // If exactly one matched, then we treat that as a successful match (and the
  // instruction will already have been filled in correctly, since the failing
  // matches won't have modified it).
  if (NumSuccessfulMatches == 1) {
    if (!MatchingInlineAsm && validateInstruction(Inst, Operands))
      return true;
    // Some instructions need post-processing to, for example, tweak which
    // encoding is selected. Loop on it while changes happen so the individual
    // transformations can chain off each other.
    if (!MatchingInlineAsm)
      while (processInstruction(Inst, Operands))
        ;
    Inst.setLoc(IDLoc);
    if (!MatchingInlineAsm)
      EmitInstruction(Inst, Operands, Out);
    Opcode = Inst.getOpcode();
    return false;
  } else if (NumSuccessfulMatches > 1) {
    assert(UnsizedMemOp &&
           "multiple matches only possible with unsized memory operands");
    return Error(UnsizedMemOp->getStartLoc(),
                 "ambiguous operand size for instruction '" + Mnemonic + "\'",
                 UnsizedMemOp->getLocRange());
  }

  // If one instruction matched with a missing feature, report this as a
  // missing feature.
  if (std::count(std::begin(Match), std::end(Match),
                 Match_MissingFeature) == 1) {
    ErrorInfo = ErrorInfoMissingFeature;
    return ErrorMissingFeature(IDLoc, ErrorInfoMissingFeature,
                               MatchingInlineAsm);
  }

  // If one instruction matched with an invalid operand, report this as an
  // operand failure.
  if (std::count(std::begin(Match), std::end(Match),
                 Match_InvalidOperand) == 1) {
    return Error(IDLoc, "invalid operand for instruction", EmptyRange,
                 MatchingInlineAsm);
  }

  // If all of these were an outright failure, report it in a useless way.
  return Error(IDLoc, "unknown instruction mnemonic", EmptyRange,
               MatchingInlineAsm);
}

bool X86AsmParser::OmitRegisterFromClobberLists(unsigned RegNo) {
  return X86MCRegisterClasses[X86::SEGMENT_REGRegClassID].contains(RegNo);
}

bool X86AsmParser::ParseDirective(AsmToken DirectiveID) {
  MCAsmParser &Parser = getParser();
  StringRef IDVal = DirectiveID.getIdentifier();
  if (IDVal.startswith(".code"))
    return ParseDirectiveCode(IDVal, DirectiveID.getLoc());
  else if (IDVal.startswith(".att_syntax")) {
    if (getLexer().isNot(AsmToken::EndOfStatement)) {
      if (Parser.getTok().getString() == "prefix")
        Parser.Lex();
      else if (Parser.getTok().getString() == "noprefix")
        return Error(DirectiveID.getLoc(), "'.att_syntax noprefix' is not "
                                           "supported: registers must have a "
                                           "'%' prefix in .att_syntax");
    }
    getParser().setAssemblerDialect(0);
    return false;
  } else if (IDVal.startswith(".intel_syntax")) {
    getParser().setAssemblerDialect(1);
    if (getLexer().isNot(AsmToken::EndOfStatement)) {
      if (Parser.getTok().getString() == "noprefix")
        Parser.Lex();
      else if (Parser.getTok().getString() == "prefix")
        return Error(DirectiveID.getLoc(), "'.intel_syntax prefix' is not "
                                           "supported: registers must not have "
                                           "a '%' prefix in .intel_syntax");
    }
    return false;
  } else if (IDVal == ".even")
    return parseDirectiveEven(DirectiveID.getLoc());
  else if (IDVal == ".cv_fpo_proc")
    return parseDirectiveFPOProc(DirectiveID.getLoc());
  else if (IDVal == ".cv_fpo_setframe")
    return parseDirectiveFPOSetFrame(DirectiveID.getLoc());
  else if (IDVal == ".cv_fpo_pushreg")
    return parseDirectiveFPOPushReg(DirectiveID.getLoc());
  else if (IDVal == ".cv_fpo_stackalloc")
    return parseDirectiveFPOStackAlloc(DirectiveID.getLoc());
  else if (IDVal == ".cv_fpo_stackalign")
    return parseDirectiveFPOStackAlign(DirectiveID.getLoc());
  else if (IDVal == ".cv_fpo_endprologue")
    return parseDirectiveFPOEndPrologue(DirectiveID.getLoc());
  else if (IDVal == ".cv_fpo_endproc")
    return parseDirectiveFPOEndProc(DirectiveID.getLoc());

  return true;
}

/// parseDirectiveEven
///  ::= .even
bool X86AsmParser::parseDirectiveEven(SMLoc L) {
  if (parseToken(AsmToken::EndOfStatement, "unexpected token in directive"))
    return false;

  const MCSection *Section = getStreamer().getCurrentSectionOnly();
  if (!Section) {
    getStreamer().InitSections(false);
    Section = getStreamer().getCurrentSectionOnly();
  }
  if (Section->UseCodeAlign())
    getStreamer().EmitCodeAlignment(2, 0);
  else
    getStreamer().EmitValueToAlignment(2, 0, 1, 0);
  return false;
}

/// ParseDirectiveCode
///  ::= .code16 | .code32 | .code64
bool X86AsmParser::ParseDirectiveCode(StringRef IDVal, SMLoc L) {
  MCAsmParser &Parser = getParser();
  Code16GCC = false;
  if (IDVal == ".code16") {
    Parser.Lex();
    if (!is16BitMode()) {
      SwitchMode(X86::Mode16Bit);
      getParser().getStreamer().EmitAssemblerFlag(MCAF_Code16);
    }
  } else if (IDVal == ".code16gcc") {
    // .code16gcc parses as if in 32-bit mode, but emits code in 16-bit mode.
    Parser.Lex();
    Code16GCC = true;
    if (!is16BitMode()) {
      SwitchMode(X86::Mode16Bit);
      getParser().getStreamer().EmitAssemblerFlag(MCAF_Code16);
    }
  } else if (IDVal == ".code32") {
    Parser.Lex();
    if (!is32BitMode()) {
      SwitchMode(X86::Mode32Bit);
      getParser().getStreamer().EmitAssemblerFlag(MCAF_Code32);
    }
  } else if (IDVal == ".code64") {
    Parser.Lex();
    if (!is64BitMode()) {
      SwitchMode(X86::Mode64Bit);
      getParser().getStreamer().EmitAssemblerFlag(MCAF_Code64);
    }
  } else {
    Error(L, "unknown directive " + IDVal);
    return false;
  }

  return false;
}

// .cv_fpo_proc foo
bool X86AsmParser::parseDirectiveFPOProc(SMLoc L) {
  MCAsmParser &Parser = getParser();
  StringRef ProcName;
  int64_t ParamsSize;
  if (Parser.parseIdentifier(ProcName))
    return Parser.TokError("expected symbol name");
  if (Parser.parseIntToken(ParamsSize, "expected parameter byte count"))
    return true;
  if (!isUIntN(32, ParamsSize))
    return Parser.TokError("parameters size out of range");
  if (Parser.parseEOL("unexpected tokens"))
    return addErrorSuffix(" in '.cv_fpo_proc' directive");
  MCSymbol *ProcSym = getContext().getOrCreateSymbol(ProcName);
  return getTargetStreamer().emitFPOProc(ProcSym, ParamsSize, L);
}

// .cv_fpo_setframe ebp
bool X86AsmParser::parseDirectiveFPOSetFrame(SMLoc L) {
  MCAsmParser &Parser = getParser();
  unsigned Reg;
  SMLoc DummyLoc;
  if (ParseRegister(Reg, DummyLoc, DummyLoc) ||
      Parser.parseEOL("unexpected tokens"))
    return addErrorSuffix(" in '.cv_fpo_setframe' directive");
  return getTargetStreamer().emitFPOSetFrame(Reg, L);
}

// .cv_fpo_pushreg ebx
bool X86AsmParser::parseDirectiveFPOPushReg(SMLoc L) {
  MCAsmParser &Parser = getParser();
  unsigned Reg;
  SMLoc DummyLoc;
  if (ParseRegister(Reg, DummyLoc, DummyLoc) ||
      Parser.parseEOL("unexpected tokens"))
    return addErrorSuffix(" in '.cv_fpo_pushreg' directive");
  return getTargetStreamer().emitFPOPushReg(Reg, L);
}

// .cv_fpo_stackalloc 20
bool X86AsmParser::parseDirectiveFPOStackAlloc(SMLoc L) {
  MCAsmParser &Parser = getParser();
  int64_t Offset;
  if (Parser.parseIntToken(Offset, "expected offset") ||
      Parser.parseEOL("unexpected tokens"))
    return addErrorSuffix(" in '.cv_fpo_stackalloc' directive");
  return getTargetStreamer().emitFPOStackAlloc(Offset, L);
}

// .cv_fpo_stackalign 8
bool X86AsmParser::parseDirectiveFPOStackAlign(SMLoc L) {
  MCAsmParser &Parser = getParser();
  int64_t Offset;
  if (Parser.parseIntToken(Offset, "expected offset") ||
      Parser.parseEOL("unexpected tokens"))
    return addErrorSuffix(" in '.cv_fpo_stackalign' directive");
  return getTargetStreamer().emitFPOStackAlign(Offset, L);
}

// .cv_fpo_endprologue
bool X86AsmParser::parseDirectiveFPOEndPrologue(SMLoc L) {
  MCAsmParser &Parser = getParser();
  if (Parser.parseEOL("unexpected tokens"))
    return addErrorSuffix(" in '.cv_fpo_endprologue' directive");
  return getTargetStreamer().emitFPOEndPrologue(L);
}

// .cv_fpo_endproc
bool X86AsmParser::parseDirectiveFPOEndProc(SMLoc L) {
  MCAsmParser &Parser = getParser();
  if (Parser.parseEOL("unexpected tokens"))
    return addErrorSuffix(" in '.cv_fpo_endproc' directive");
  return getTargetStreamer().emitFPOEndProc(L);
}

// Force static initialization.
extern "C" void LLVMInitializeX86AsmParser() {
  RegisterMCAsmParser<X86AsmParser> X(getTheX86_32Target());
  RegisterMCAsmParser<X86AsmParser> Y(getTheX86_64Target());
}

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#define GET_SUBTARGET_FEATURE_NAME
#include "X86GenAsmMatcher.inc"
