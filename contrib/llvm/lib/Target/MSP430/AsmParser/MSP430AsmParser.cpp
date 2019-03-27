//===- MSP430AsmParser.cpp - Parse MSP430 assembly to MCInst instructions -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MSP430.h"
#include "MSP430RegisterInfo.h"
#include "MCTargetDesc/MSP430MCTargetDesc.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/TargetRegistry.h"

#define DEBUG_TYPE "msp430-asm-parser"

using namespace llvm;

namespace {

/// Parses MSP430 assembly from a stream.
class MSP430AsmParser : public MCTargetAsmParser {
  const MCSubtargetInfo &STI;
  MCAsmParser &Parser;
  const MCRegisterInfo *MRI;

  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;

  bool ParseRegister(unsigned &RegNo, SMLoc &StartLoc, SMLoc &EndLoc) override;

  bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;

  bool ParseDirective(AsmToken DirectiveID) override;
  bool ParseDirectiveRefSym(AsmToken DirectiveID);

  unsigned validateTargetOperandClass(MCParsedAsmOperand &Op,
                                      unsigned Kind) override;

  bool parseJccInstruction(ParseInstructionInfo &Info, StringRef Name,
                           SMLoc NameLoc, OperandVector &Operands);

  bool ParseOperand(OperandVector &Operands);

  bool ParseLiteralValues(unsigned Size, SMLoc L);

  MCAsmParser &getParser() const { return Parser; }
  MCAsmLexer &getLexer() const { return Parser.getLexer(); }

  /// @name Auto-generated Matcher Functions
  /// {

#define GET_ASSEMBLER_HEADER
#include "MSP430GenAsmMatcher.inc"

  /// }

public:
  MSP430AsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                  const MCInstrInfo &MII, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, STI, MII), STI(STI), Parser(Parser) {
    MCAsmParserExtension::Initialize(Parser);
    MRI = getContext().getRegisterInfo();

    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }
};

/// A parsed MSP430 assembly operand.
class MSP430Operand : public MCParsedAsmOperand {
  typedef MCParsedAsmOperand Base;

  enum KindTy {
    k_Imm,
    k_Reg,
    k_Tok,
    k_Mem,
    k_IndReg,
    k_PostIndReg
  } Kind;

  struct Memory {
    unsigned Reg;
    const MCExpr *Offset;
  };
  union {
    const MCExpr *Imm;
    unsigned      Reg;
    StringRef     Tok;
    Memory        Mem;
  };

  SMLoc Start, End;

public:
  MSP430Operand(StringRef Tok, SMLoc const &S)
      : Base(), Kind(k_Tok), Tok(Tok), Start(S), End(S) {}
  MSP430Operand(KindTy Kind, unsigned Reg, SMLoc const &S, SMLoc const &E)
      : Base(), Kind(Kind), Reg(Reg), Start(S), End(E) {}
  MSP430Operand(MCExpr const *Imm, SMLoc const &S, SMLoc const &E)
      : Base(), Kind(k_Imm), Imm(Imm), Start(S), End(E) {}
  MSP430Operand(unsigned Reg, MCExpr const *Expr, SMLoc const &S, SMLoc const &E)
      : Base(), Kind(k_Mem), Mem({Reg, Expr}), Start(S), End(E) {}

  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert((Kind == k_Reg || Kind == k_IndReg || Kind == k_PostIndReg) &&
        "Unexpected operand kind");
    assert(N == 1 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::createReg(Reg));
  }

  void addExprOperand(MCInst &Inst, const MCExpr *Expr) const {
    // Add as immediate when possible
    if (!Expr)
      Inst.addOperand(MCOperand::createImm(0));
    else if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(Kind == k_Imm && "Unexpected operand kind");
    assert(N == 1 && "Invalid number of operands!");

    addExprOperand(Inst, Imm);
  }

  void addMemOperands(MCInst &Inst, unsigned N) const {
    assert(Kind == k_Mem && "Unexpected operand kind");
    assert(N == 2 && "Invalid number of operands");

    Inst.addOperand(MCOperand::createReg(Mem.Reg));
    addExprOperand(Inst, Mem.Offset);
  }

  bool isReg() const        { return Kind == k_Reg; }
  bool isImm() const        { return Kind == k_Imm; }
  bool isToken() const      { return Kind == k_Tok; }
  bool isMem() const        { return Kind == k_Mem; }
  bool isIndReg() const     { return Kind == k_IndReg; }
  bool isPostIndReg() const { return Kind == k_PostIndReg; }

  bool isCGImm() const {
    if (Kind != k_Imm)
      return false;

    int64_t Val;
    if (!Imm->evaluateAsAbsolute(Val))
      return false;
    
    if (Val == 0 || Val == 1 || Val == 2 || Val == 4 || Val == 8 || Val == -1)
      return true;

    return false;
  }

  StringRef getToken() const {
    assert(Kind == k_Tok && "Invalid access!");
    return Tok;
  }

  unsigned getReg() const {
    assert(Kind == k_Reg && "Invalid access!");
    return Reg;
  }

  void setReg(unsigned RegNo) {
    assert(Kind == k_Reg && "Invalid access!");
    Reg = RegNo;
  }

  static std::unique_ptr<MSP430Operand> CreateToken(StringRef Str, SMLoc S) {
    return make_unique<MSP430Operand>(Str, S);
  }

  static std::unique_ptr<MSP430Operand> CreateReg(unsigned RegNum, SMLoc S,
                                                  SMLoc E) {
    return make_unique<MSP430Operand>(k_Reg, RegNum, S, E);
  }

  static std::unique_ptr<MSP430Operand> CreateImm(const MCExpr *Val, SMLoc S,
                                                  SMLoc E) {
    return make_unique<MSP430Operand>(Val, S, E);
  }

  static std::unique_ptr<MSP430Operand> CreateMem(unsigned RegNum,
                                                  const MCExpr *Val,
                                                  SMLoc S, SMLoc E) {
    return make_unique<MSP430Operand>(RegNum, Val, S, E);
  }

  static std::unique_ptr<MSP430Operand> CreateIndReg(unsigned RegNum, SMLoc S,
                                                  SMLoc E) {
    return make_unique<MSP430Operand>(k_IndReg, RegNum, S, E);
  }

  static std::unique_ptr<MSP430Operand> CreatePostIndReg(unsigned RegNum, SMLoc S,
                                                  SMLoc E) {
    return make_unique<MSP430Operand>(k_PostIndReg, RegNum, S, E);
  }

  SMLoc getStartLoc() const { return Start; }
  SMLoc getEndLoc() const { return End; }

  virtual void print(raw_ostream &O) const {
    switch (Kind) {
    case k_Tok:
      O << "Token " << Tok;
      break;
    case k_Reg:
      O << "Register " << Reg;
      break;
    case k_Imm:
      O << "Immediate " << *Imm;
      break;
    case k_Mem:
      O << "Memory ";
      O << *Mem.Offset << "(" << Reg << ")";
      break;
    case k_IndReg:
      O << "RegInd " << Reg;
      break;
    case k_PostIndReg:
      O << "PostInc " << Reg;
      break;
    }
  }
};
} // end anonymous namespace

bool MSP430AsmParser::MatchAndEmitInstruction(SMLoc Loc, unsigned &Opcode,
                                              OperandVector &Operands,
                                              MCStreamer &Out,
                                              uint64_t &ErrorInfo,
                                              bool MatchingInlineAsm) {
  MCInst Inst;
  unsigned MatchResult =
      MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm);

  switch (MatchResult) {
  case Match_Success:
    Inst.setLoc(Loc);
    Out.EmitInstruction(Inst, STI);
    return false;
  case Match_MnemonicFail:
    return Error(Loc, "invalid instruction mnemonic");
  case Match_InvalidOperand: {
    SMLoc ErrorLoc = Loc;
    if (ErrorInfo != ~0U) {
      if (ErrorInfo >= Operands.size())
        return Error(ErrorLoc, "too few operands for instruction");

      ErrorLoc = ((MSP430Operand &)*Operands[ErrorInfo]).getStartLoc();
      if (ErrorLoc == SMLoc())
        ErrorLoc = Loc;
    }
    return Error(ErrorLoc, "invalid operand for instruction");
  }
  default:
    return true;
  }
}

// Auto-generated by TableGen
static unsigned MatchRegisterName(StringRef Name);
static unsigned MatchRegisterAltName(StringRef Name);

bool MSP430AsmParser::ParseRegister(unsigned &RegNo, SMLoc &StartLoc,
                                    SMLoc &EndLoc) {
  if (getLexer().getKind() == AsmToken::Identifier) {
    auto Name = getLexer().getTok().getIdentifier().lower();
    RegNo = MatchRegisterName(Name);
    if (RegNo == MSP430::NoRegister) {
      RegNo = MatchRegisterAltName(Name);
      if (RegNo == MSP430::NoRegister)
        return true;
    }

    AsmToken const &T = getParser().getTok();
    StartLoc = T.getLoc();
    EndLoc = T.getEndLoc();
    getLexer().Lex(); // eat register token

    return false;
  }

  return Error(StartLoc, "invalid register name");
}

bool MSP430AsmParser::parseJccInstruction(ParseInstructionInfo &Info,
                                          StringRef Name, SMLoc NameLoc,
                                          OperandVector &Operands) {
  if (!Name.startswith_lower("j"))
    return true;

  auto CC = Name.drop_front().lower();
  unsigned CondCode;
  if (CC == "ne" || CC == "nz")
    CondCode = MSP430CC::COND_NE;
  else if (CC == "eq" || CC == "z")
    CondCode = MSP430CC::COND_E;
  else if (CC == "lo" || CC == "nc")
    CondCode = MSP430CC::COND_LO;
  else if (CC == "hs" || CC == "c")
    CondCode = MSP430CC::COND_HS;
  else if (CC == "n")
    CondCode = MSP430CC::COND_N;
  else if (CC == "ge")
    CondCode = MSP430CC::COND_GE;
  else if (CC == "l")
    CondCode = MSP430CC::COND_L;
  else if (CC == "mp")
    CondCode = MSP430CC::COND_NONE;
  else
    return Error(NameLoc, "unknown instruction");

  if (CondCode == (unsigned)MSP430CC::COND_NONE)
    Operands.push_back(MSP430Operand::CreateToken("jmp", NameLoc));
  else {
    Operands.push_back(MSP430Operand::CreateToken("j", NameLoc));
    const MCExpr *CCode = MCConstantExpr::create(CondCode, getContext());
    Operands.push_back(MSP430Operand::CreateImm(CCode, SMLoc(), SMLoc()));
  }

  // Skip optional '$' sign.
  if (getLexer().getKind() == AsmToken::Dollar)
    getLexer().Lex(); // Eat '$'

  const MCExpr *Val;
  SMLoc ExprLoc = getLexer().getLoc();
  if (getParser().parseExpression(Val))
    return Error(ExprLoc, "expected expression operand");

  int64_t Res;
  if (Val->evaluateAsAbsolute(Res))
    if (Res < -512 || Res > 511)
      return Error(ExprLoc, "invalid jump offset");

  Operands.push_back(MSP430Operand::CreateImm(Val, ExprLoc,
    getLexer().getLoc()));

  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    SMLoc Loc = getLexer().getLoc();
    getParser().eatToEndOfStatement();
    return Error(Loc, "unexpected token");
  }

  getParser().Lex(); // Consume the EndOfStatement.
  return false;
}

bool MSP430AsmParser::ParseInstruction(ParseInstructionInfo &Info,
                                       StringRef Name, SMLoc NameLoc,
                                       OperandVector &Operands) {
  // Drop .w suffix
  if (Name.endswith_lower(".w"))
    Name = Name.drop_back(2);

  if (!parseJccInstruction(Info, Name, NameLoc, Operands))
    return false;

  // First operand is instruction mnemonic
  Operands.push_back(MSP430Operand::CreateToken(Name, NameLoc));

  // If there are no more operands, then finish
  if (getLexer().is(AsmToken::EndOfStatement))
    return false;

  // Parse first operand
  if (ParseOperand(Operands))
    return true;

  // Parse second operand if any
  if (getLexer().is(AsmToken::Comma)) {
    getLexer().Lex(); // Eat ','
    if (ParseOperand(Operands))
      return true;
  }

  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    SMLoc Loc = getLexer().getLoc();
    getParser().eatToEndOfStatement();
    return Error(Loc, "unexpected token");
  }

  getParser().Lex(); // Consume the EndOfStatement.
  return false;
}

bool MSP430AsmParser::ParseDirectiveRefSym(AsmToken DirectiveID) {
    StringRef Name;
    if (getParser().parseIdentifier(Name))
      return TokError("expected identifier in directive");

    MCSymbol *Sym = getContext().getOrCreateSymbol(Name);
    getStreamer().EmitSymbolAttribute(Sym, MCSA_Global);
    return false;
}

bool MSP430AsmParser::ParseDirective(AsmToken DirectiveID) {
  StringRef IDVal = DirectiveID.getIdentifier();
  if (IDVal.lower() == ".long") {
    ParseLiteralValues(4, DirectiveID.getLoc());
  } else if (IDVal.lower() == ".word" || IDVal.lower() == ".short") {
    ParseLiteralValues(2, DirectiveID.getLoc());
  } else if (IDVal.lower() == ".byte") {
    ParseLiteralValues(1, DirectiveID.getLoc());
  } else if (IDVal.lower() == ".refsym") {
    return ParseDirectiveRefSym(DirectiveID);
  }
  return true;
}

bool MSP430AsmParser::ParseOperand(OperandVector &Operands) {
  switch (getLexer().getKind()) {
    default: return true;
    case AsmToken::Identifier: {
      // try rN
      unsigned RegNo;
      SMLoc StartLoc, EndLoc;
      if (!ParseRegister(RegNo, StartLoc, EndLoc)) {
        Operands.push_back(MSP430Operand::CreateReg(RegNo, StartLoc, EndLoc));
        return false;
      }
      LLVM_FALLTHROUGH;
    }
    case AsmToken::Integer:
    case AsmToken::Plus:
    case AsmToken::Minus: {
      SMLoc StartLoc = getParser().getTok().getLoc();
      const MCExpr *Val;
      // Try constexpr[(rN)]
      if (!getParser().parseExpression(Val)) {
        unsigned RegNo = MSP430::PC;
        SMLoc EndLoc = getParser().getTok().getLoc();
        // Try (rN)
        if (getLexer().getKind() == AsmToken::LParen) {
          getLexer().Lex(); // Eat '('
          SMLoc RegStartLoc;
          if (ParseRegister(RegNo, RegStartLoc, EndLoc))
            return true;
          if (getLexer().getKind() != AsmToken::RParen)
            return true;
          EndLoc = getParser().getTok().getEndLoc();
          getLexer().Lex(); // Eat ')'
        }
        Operands.push_back(MSP430Operand::CreateMem(RegNo, Val, StartLoc,
          EndLoc));
        return false;
      }
      return true;
    }
    case AsmToken::Amp: {
      // Try &constexpr
      SMLoc StartLoc = getParser().getTok().getLoc();
      getLexer().Lex(); // Eat '&'
      const MCExpr *Val;
      if (!getParser().parseExpression(Val)) {
        SMLoc EndLoc = getParser().getTok().getLoc();
        Operands.push_back(MSP430Operand::CreateMem(MSP430::SR, Val, StartLoc,
          EndLoc));
        return false;
      }
      return true;
    }
    case AsmToken::At: {
      // Try @rN[+]
      SMLoc StartLoc = getParser().getTok().getLoc();
      getLexer().Lex(); // Eat '@'
      unsigned RegNo;
      SMLoc RegStartLoc, EndLoc;
      if (ParseRegister(RegNo, RegStartLoc, EndLoc))
        return true;
      if (getLexer().getKind() == AsmToken::Plus) {
        Operands.push_back(MSP430Operand::CreatePostIndReg(RegNo, StartLoc, EndLoc));
        getLexer().Lex(); // Eat '+'
        return false;
      }
      if (Operands.size() > 1) // Emulate @rd in destination position as 0(rd)
        Operands.push_back(MSP430Operand::CreateMem(RegNo,
            MCConstantExpr::create(0, getContext()), StartLoc, EndLoc));
      else
        Operands.push_back(MSP430Operand::CreateIndReg(RegNo, StartLoc, EndLoc));
      return false;
    }
    case AsmToken::Hash:
      // Try #constexpr
      SMLoc StartLoc = getParser().getTok().getLoc();
      getLexer().Lex(); // Eat '#'
      const MCExpr *Val;
      if (!getParser().parseExpression(Val)) {
        SMLoc EndLoc = getParser().getTok().getLoc();
        Operands.push_back(MSP430Operand::CreateImm(Val, StartLoc, EndLoc));
        return false;
      }
      return true;
  }
}

bool MSP430AsmParser::ParseLiteralValues(unsigned Size, SMLoc L) {
  auto parseOne = [&]() -> bool {
    const MCExpr *Value;
    if (getParser().parseExpression(Value))
      return true;
    getParser().getStreamer().EmitValue(Value, Size, L);
    return false;
  };
  return (parseMany(parseOne));
}

extern "C" void LLVMInitializeMSP430AsmParser() {
  RegisterMCAsmParser<MSP430AsmParser> X(getTheMSP430Target());
}

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "MSP430GenAsmMatcher.inc"

static unsigned convertGR16ToGR8(unsigned Reg) {
  switch (Reg) {
  default:
    llvm_unreachable("Unknown GR16 register");
  case MSP430::PC:  return MSP430::PCB;
  case MSP430::SP:  return MSP430::SPB;
  case MSP430::SR:  return MSP430::SRB;
  case MSP430::CG:  return MSP430::CGB;
  case MSP430::FP:  return MSP430::FPB;
  case MSP430::R5:  return MSP430::R5B;
  case MSP430::R6:  return MSP430::R6B;
  case MSP430::R7:  return MSP430::R7B;
  case MSP430::R8:  return MSP430::R8B;
  case MSP430::R9:  return MSP430::R9B;
  case MSP430::R10: return MSP430::R10B;
  case MSP430::R11: return MSP430::R11B;
  case MSP430::R12: return MSP430::R12B;
  case MSP430::R13: return MSP430::R13B;
  case MSP430::R14: return MSP430::R14B;
  case MSP430::R15: return MSP430::R15B;
  }
}

unsigned MSP430AsmParser::validateTargetOperandClass(MCParsedAsmOperand &AsmOp,
                                                     unsigned Kind) {
  MSP430Operand &Op = static_cast<MSP430Operand &>(AsmOp);

  if (!Op.isReg())
    return Match_InvalidOperand;

  unsigned Reg = Op.getReg();
  bool isGR16 =
      MSP430MCRegisterClasses[MSP430::GR16RegClassID].contains(Reg);

  if (isGR16 && (Kind == MCK_GR8)) {
    Op.setReg(convertGR16ToGR8(Reg));
    return Match_Success;
  }

  return Match_InvalidOperand;
}
