//===-- SparcAsmParser.cpp - Parse Sparc assembly to MCInst instructions --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/SparcMCExpr.h"
#include "MCTargetDesc/SparcMCTargetDesc.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>

using namespace llvm;

// The generated AsmMatcher SparcGenAsmMatcher uses "Sparc" as the target
// namespace. But SPARC backend uses "SP" as its namespace.
namespace llvm {
namespace Sparc {

    using namespace SP;

} // end namespace Sparc
} // end namespace llvm

namespace {

class SparcOperand;

class SparcAsmParser : public MCTargetAsmParser {
  MCAsmParser &Parser;

  /// @name Auto-generated Match Functions
  /// {

#define GET_ASSEMBLER_HEADER
#include "SparcGenAsmMatcher.inc"

  /// }

  // public interface of the MCTargetAsmParser.
  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;
  bool ParseRegister(unsigned &RegNo, SMLoc &StartLoc, SMLoc &EndLoc) override;
  bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;
  bool ParseDirective(AsmToken DirectiveID) override;

  unsigned validateTargetOperandClass(MCParsedAsmOperand &Op,
                                      unsigned Kind) override;

  // Custom parse functions for Sparc specific operands.
  OperandMatchResultTy parseMEMOperand(OperandVector &Operands);

  OperandMatchResultTy parseMembarTag(OperandVector &Operands);

  OperandMatchResultTy parseOperand(OperandVector &Operands, StringRef Name);

  OperandMatchResultTy
  parseSparcAsmOperand(std::unique_ptr<SparcOperand> &Operand,
                       bool isCall = false);

  OperandMatchResultTy parseBranchModifiers(OperandVector &Operands);

  // Helper function for dealing with %lo / %hi in PIC mode.
  const SparcMCExpr *adjustPICRelocation(SparcMCExpr::VariantKind VK,
                                         const MCExpr *subExpr);

  // returns true if Tok is matched to a register and returns register in RegNo.
  bool matchRegisterName(const AsmToken &Tok, unsigned &RegNo,
                         unsigned &RegKind);

  bool matchSparcAsmModifiers(const MCExpr *&EVal, SMLoc &EndLoc);

  bool is64Bit() const {
    return getSTI().getTargetTriple().getArch() == Triple::sparcv9;
  }

  bool expandSET(MCInst &Inst, SMLoc IDLoc,
                 SmallVectorImpl<MCInst> &Instructions);

public:
  SparcAsmParser(const MCSubtargetInfo &sti, MCAsmParser &parser,
                const MCInstrInfo &MII,
                const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, sti, MII), Parser(parser) {
    Parser.addAliasForDirective(".half", ".2byte");
    Parser.addAliasForDirective(".uahalf", ".2byte");
    Parser.addAliasForDirective(".word", ".4byte");
    Parser.addAliasForDirective(".uaword", ".4byte");
    Parser.addAliasForDirective(".nword", is64Bit() ? ".8byte" : ".4byte");
    if (is64Bit())
      Parser.addAliasForDirective(".xword", ".8byte");

    // Initialize the set of available features.
    setAvailableFeatures(ComputeAvailableFeatures(getSTI().getFeatureBits()));
  }
};

} // end anonymous namespace

  static const MCPhysReg IntRegs[32] = {
    Sparc::G0, Sparc::G1, Sparc::G2, Sparc::G3,
    Sparc::G4, Sparc::G5, Sparc::G6, Sparc::G7,
    Sparc::O0, Sparc::O1, Sparc::O2, Sparc::O3,
    Sparc::O4, Sparc::O5, Sparc::O6, Sparc::O7,
    Sparc::L0, Sparc::L1, Sparc::L2, Sparc::L3,
    Sparc::L4, Sparc::L5, Sparc::L6, Sparc::L7,
    Sparc::I0, Sparc::I1, Sparc::I2, Sparc::I3,
    Sparc::I4, Sparc::I5, Sparc::I6, Sparc::I7 };

  static const MCPhysReg FloatRegs[32] = {
    Sparc::F0,  Sparc::F1,  Sparc::F2,  Sparc::F3,
    Sparc::F4,  Sparc::F5,  Sparc::F6,  Sparc::F7,
    Sparc::F8,  Sparc::F9,  Sparc::F10, Sparc::F11,
    Sparc::F12, Sparc::F13, Sparc::F14, Sparc::F15,
    Sparc::F16, Sparc::F17, Sparc::F18, Sparc::F19,
    Sparc::F20, Sparc::F21, Sparc::F22, Sparc::F23,
    Sparc::F24, Sparc::F25, Sparc::F26, Sparc::F27,
    Sparc::F28, Sparc::F29, Sparc::F30, Sparc::F31 };

  static const MCPhysReg DoubleRegs[32] = {
    Sparc::D0,  Sparc::D1,  Sparc::D2,  Sparc::D3,
    Sparc::D4,  Sparc::D5,  Sparc::D6,  Sparc::D7,
    Sparc::D8,  Sparc::D9,  Sparc::D10, Sparc::D11,
    Sparc::D12, Sparc::D13, Sparc::D14, Sparc::D15,
    Sparc::D16, Sparc::D17, Sparc::D18, Sparc::D19,
    Sparc::D20, Sparc::D21, Sparc::D22, Sparc::D23,
    Sparc::D24, Sparc::D25, Sparc::D26, Sparc::D27,
    Sparc::D28, Sparc::D29, Sparc::D30, Sparc::D31 };

  static const MCPhysReg QuadFPRegs[32] = {
    Sparc::Q0,  Sparc::Q1,  Sparc::Q2,  Sparc::Q3,
    Sparc::Q4,  Sparc::Q5,  Sparc::Q6,  Sparc::Q7,
    Sparc::Q8,  Sparc::Q9,  Sparc::Q10, Sparc::Q11,
    Sparc::Q12, Sparc::Q13, Sparc::Q14, Sparc::Q15 };

  static const MCPhysReg ASRRegs[32] = {
    SP::Y,     SP::ASR1,  SP::ASR2,  SP::ASR3,
    SP::ASR4,  SP::ASR5,  SP::ASR6, SP::ASR7,
    SP::ASR8,  SP::ASR9,  SP::ASR10, SP::ASR11,
    SP::ASR12, SP::ASR13, SP::ASR14, SP::ASR15,
    SP::ASR16, SP::ASR17, SP::ASR18, SP::ASR19,
    SP::ASR20, SP::ASR21, SP::ASR22, SP::ASR23,
    SP::ASR24, SP::ASR25, SP::ASR26, SP::ASR27,
    SP::ASR28, SP::ASR29, SP::ASR30, SP::ASR31};

  static const MCPhysReg IntPairRegs[] = {
    Sparc::G0_G1, Sparc::G2_G3, Sparc::G4_G5, Sparc::G6_G7,
    Sparc::O0_O1, Sparc::O2_O3, Sparc::O4_O5, Sparc::O6_O7,
    Sparc::L0_L1, Sparc::L2_L3, Sparc::L4_L5, Sparc::L6_L7,
    Sparc::I0_I1, Sparc::I2_I3, Sparc::I4_I5, Sparc::I6_I7};

  static const MCPhysReg CoprocRegs[32] = {
    Sparc::C0,  Sparc::C1,  Sparc::C2,  Sparc::C3,
    Sparc::C4,  Sparc::C5,  Sparc::C6,  Sparc::C7,
    Sparc::C8,  Sparc::C9,  Sparc::C10, Sparc::C11,
    Sparc::C12, Sparc::C13, Sparc::C14, Sparc::C15,
    Sparc::C16, Sparc::C17, Sparc::C18, Sparc::C19,
    Sparc::C20, Sparc::C21, Sparc::C22, Sparc::C23,
    Sparc::C24, Sparc::C25, Sparc::C26, Sparc::C27,
    Sparc::C28, Sparc::C29, Sparc::C30, Sparc::C31 };

  static const MCPhysReg CoprocPairRegs[] = {
    Sparc::C0_C1,   Sparc::C2_C3,   Sparc::C4_C5,   Sparc::C6_C7,
    Sparc::C8_C9,   Sparc::C10_C11, Sparc::C12_C13, Sparc::C14_C15,
    Sparc::C16_C17, Sparc::C18_C19, Sparc::C20_C21, Sparc::C22_C23,
    Sparc::C24_C25, Sparc::C26_C27, Sparc::C28_C29, Sparc::C30_C31};

namespace {

/// SparcOperand - Instances of this class represent a parsed Sparc machine
/// instruction.
class SparcOperand : public MCParsedAsmOperand {
public:
  enum RegisterKind {
    rk_None,
    rk_IntReg,
    rk_IntPairReg,
    rk_FloatReg,
    rk_DoubleReg,
    rk_QuadReg,
    rk_CoprocReg,
    rk_CoprocPairReg,
    rk_Special,
  };

private:
  enum KindTy {
    k_Token,
    k_Register,
    k_Immediate,
    k_MemoryReg,
    k_MemoryImm
  } Kind;

  SMLoc StartLoc, EndLoc;

  struct Token {
    const char *Data;
    unsigned Length;
  };

  struct RegOp {
    unsigned RegNum;
    RegisterKind Kind;
  };

  struct ImmOp {
    const MCExpr *Val;
  };

  struct MemOp {
    unsigned Base;
    unsigned OffsetReg;
    const MCExpr *Off;
  };

  union {
    struct Token Tok;
    struct RegOp Reg;
    struct ImmOp Imm;
    struct MemOp Mem;
  };

public:
  SparcOperand(KindTy K) : MCParsedAsmOperand(), Kind(K) {}

  bool isToken() const override { return Kind == k_Token; }
  bool isReg() const override { return Kind == k_Register; }
  bool isImm() const override { return Kind == k_Immediate; }
  bool isMem() const override { return isMEMrr() || isMEMri(); }
  bool isMEMrr() const { return Kind == k_MemoryReg; }
  bool isMEMri() const { return Kind == k_MemoryImm; }
  bool isMembarTag() const { return Kind == k_Immediate; }

  bool isIntReg() const {
    return (Kind == k_Register && Reg.Kind == rk_IntReg);
  }

  bool isFloatReg() const {
    return (Kind == k_Register && Reg.Kind == rk_FloatReg);
  }

  bool isFloatOrDoubleReg() const {
    return (Kind == k_Register && (Reg.Kind == rk_FloatReg
                                   || Reg.Kind == rk_DoubleReg));
  }

  bool isCoprocReg() const {
    return (Kind == k_Register && Reg.Kind == rk_CoprocReg);
  }

  StringRef getToken() const {
    assert(Kind == k_Token && "Invalid access!");
    return StringRef(Tok.Data, Tok.Length);
  }

  unsigned getReg() const override {
    assert((Kind == k_Register) && "Invalid access!");
    return Reg.RegNum;
  }

  const MCExpr *getImm() const {
    assert((Kind == k_Immediate) && "Invalid access!");
    return Imm.Val;
  }

  unsigned getMemBase() const {
    assert((Kind == k_MemoryReg || Kind == k_MemoryImm) && "Invalid access!");
    return Mem.Base;
  }

  unsigned getMemOffsetReg() const {
    assert((Kind == k_MemoryReg) && "Invalid access!");
    return Mem.OffsetReg;
  }

  const MCExpr *getMemOff() const {
    assert((Kind == k_MemoryImm) && "Invalid access!");
    return Mem.Off;
  }

  /// getStartLoc - Get the location of the first token of this operand.
  SMLoc getStartLoc() const override {
    return StartLoc;
  }
  /// getEndLoc - Get the location of the last token of this operand.
  SMLoc getEndLoc() const override {
    return EndLoc;
  }

  void print(raw_ostream &OS) const override {
    switch (Kind) {
    case k_Token:     OS << "Token: " << getToken() << "\n"; break;
    case k_Register:  OS << "Reg: #" << getReg() << "\n"; break;
    case k_Immediate: OS << "Imm: " << getImm() << "\n"; break;
    case k_MemoryReg: OS << "Mem: " << getMemBase() << "+"
                         << getMemOffsetReg() << "\n"; break;
    case k_MemoryImm: assert(getMemOff() != nullptr);
      OS << "Mem: " << getMemBase()
         << "+" << *getMemOff()
         << "\n"; break;
    }
  }

  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCExpr *Expr = getImm();
    addExpr(Inst, Expr);
  }

  void addExpr(MCInst &Inst, const MCExpr *Expr) const{
    // Add as immediate when possible.  Null MCExpr = 0.
    if (!Expr)
      Inst.addOperand(MCOperand::createImm(0));
    else if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }

  void addMEMrrOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::createReg(getMemBase()));

    assert(getMemOffsetReg() != 0 && "Invalid offset");
    Inst.addOperand(MCOperand::createReg(getMemOffsetReg()));
  }

  void addMEMriOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::createReg(getMemBase()));

    const MCExpr *Expr = getMemOff();
    addExpr(Inst, Expr);
  }

  void addMembarTagOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCExpr *Expr = getImm();
    addExpr(Inst, Expr);
  }

  static std::unique_ptr<SparcOperand> CreateToken(StringRef Str, SMLoc S) {
    auto Op = make_unique<SparcOperand>(k_Token);
    Op->Tok.Data = Str.data();
    Op->Tok.Length = Str.size();
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<SparcOperand> CreateReg(unsigned RegNum, unsigned Kind,
                                                 SMLoc S, SMLoc E) {
    auto Op = make_unique<SparcOperand>(k_Register);
    Op->Reg.RegNum = RegNum;
    Op->Reg.Kind   = (SparcOperand::RegisterKind)Kind;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<SparcOperand> CreateImm(const MCExpr *Val, SMLoc S,
                                                 SMLoc E) {
    auto Op = make_unique<SparcOperand>(k_Immediate);
    Op->Imm.Val = Val;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static bool MorphToIntPairReg(SparcOperand &Op) {
    unsigned Reg = Op.getReg();
    assert(Op.Reg.Kind == rk_IntReg);
    unsigned regIdx = 32;
    if (Reg >= Sparc::G0 && Reg <= Sparc::G7)
      regIdx = Reg - Sparc::G0;
    else if (Reg >= Sparc::O0 && Reg <= Sparc::O7)
      regIdx = Reg - Sparc::O0 + 8;
    else if (Reg >= Sparc::L0 && Reg <= Sparc::L7)
      regIdx = Reg - Sparc::L0 + 16;
    else if (Reg >= Sparc::I0 && Reg <= Sparc::I7)
      regIdx = Reg - Sparc::I0 + 24;
    if (regIdx % 2 || regIdx > 31)
      return false;
    Op.Reg.RegNum = IntPairRegs[regIdx / 2];
    Op.Reg.Kind = rk_IntPairReg;
    return true;
  }

  static bool MorphToDoubleReg(SparcOperand &Op) {
    unsigned Reg = Op.getReg();
    assert(Op.Reg.Kind == rk_FloatReg);
    unsigned regIdx = Reg - Sparc::F0;
    if (regIdx % 2 || regIdx > 31)
      return false;
    Op.Reg.RegNum = DoubleRegs[regIdx / 2];
    Op.Reg.Kind = rk_DoubleReg;
    return true;
  }

  static bool MorphToQuadReg(SparcOperand &Op) {
    unsigned Reg = Op.getReg();
    unsigned regIdx = 0;
    switch (Op.Reg.Kind) {
    default: llvm_unreachable("Unexpected register kind!");
    case rk_FloatReg:
      regIdx = Reg - Sparc::F0;
      if (regIdx % 4 || regIdx > 31)
        return false;
      Reg = QuadFPRegs[regIdx / 4];
      break;
    case rk_DoubleReg:
      regIdx =  Reg - Sparc::D0;
      if (regIdx % 2 || regIdx > 31)
        return false;
      Reg = QuadFPRegs[regIdx / 2];
      break;
    }
    Op.Reg.RegNum = Reg;
    Op.Reg.Kind = rk_QuadReg;
    return true;
  }

  static bool MorphToCoprocPairReg(SparcOperand &Op) {
    unsigned Reg = Op.getReg();
    assert(Op.Reg.Kind == rk_CoprocReg);
    unsigned regIdx = 32;
    if (Reg >= Sparc::C0 && Reg <= Sparc::C31)
      regIdx = Reg - Sparc::C0;
    if (regIdx % 2 || regIdx > 31)
      return false;
    Op.Reg.RegNum = CoprocPairRegs[regIdx / 2];
    Op.Reg.Kind = rk_CoprocPairReg;
    return true;
  }

  static std::unique_ptr<SparcOperand>
  MorphToMEMrr(unsigned Base, std::unique_ptr<SparcOperand> Op) {
    unsigned offsetReg = Op->getReg();
    Op->Kind = k_MemoryReg;
    Op->Mem.Base = Base;
    Op->Mem.OffsetReg = offsetReg;
    Op->Mem.Off = nullptr;
    return Op;
  }

  static std::unique_ptr<SparcOperand>
  CreateMEMr(unsigned Base, SMLoc S, SMLoc E) {
    auto Op = make_unique<SparcOperand>(k_MemoryReg);
    Op->Mem.Base = Base;
    Op->Mem.OffsetReg = Sparc::G0;  // always 0
    Op->Mem.Off = nullptr;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<SparcOperand>
  MorphToMEMri(unsigned Base, std::unique_ptr<SparcOperand> Op) {
    const MCExpr *Imm  = Op->getImm();
    Op->Kind = k_MemoryImm;
    Op->Mem.Base = Base;
    Op->Mem.OffsetReg = 0;
    Op->Mem.Off = Imm;
    return Op;
  }
};

} // end anonymous namespace

bool SparcAsmParser::expandSET(MCInst &Inst, SMLoc IDLoc,
                               SmallVectorImpl<MCInst> &Instructions) {
  MCOperand MCRegOp = Inst.getOperand(0);
  MCOperand MCValOp = Inst.getOperand(1);
  assert(MCRegOp.isReg());
  assert(MCValOp.isImm() || MCValOp.isExpr());

  // the imm operand can be either an expression or an immediate.
  bool IsImm = Inst.getOperand(1).isImm();
  int64_t RawImmValue = IsImm ? MCValOp.getImm() : 0;

  // Allow either a signed or unsigned 32-bit immediate.
  if (RawImmValue < -2147483648LL || RawImmValue > 4294967295LL) {
    return Error(IDLoc,
                 "set: argument must be between -2147483648 and 4294967295");
  }

  // If the value was expressed as a large unsigned number, that's ok.
  // We want to see if it "looks like" a small signed number.
  int32_t ImmValue = RawImmValue;
  // For 'set' you can't use 'or' with a negative operand on V9 because
  // that would splat the sign bit across the upper half of the destination
  // register, whereas 'set' is defined to zero the high 32 bits.
  bool IsEffectivelyImm13 =
      IsImm && ((is64Bit() ? 0 : -4096) <= ImmValue && ImmValue < 4096);
  const MCExpr *ValExpr;
  if (IsImm)
    ValExpr = MCConstantExpr::create(ImmValue, getContext());
  else
    ValExpr = MCValOp.getExpr();

  MCOperand PrevReg = MCOperand::createReg(Sparc::G0);

  // If not just a signed imm13 value, then either we use a 'sethi' with a
  // following 'or', or a 'sethi' by itself if there are no more 1 bits.
  // In either case, start with the 'sethi'.
  if (!IsEffectivelyImm13) {
    MCInst TmpInst;
    const MCExpr *Expr = adjustPICRelocation(SparcMCExpr::VK_Sparc_HI, ValExpr);
    TmpInst.setLoc(IDLoc);
    TmpInst.setOpcode(SP::SETHIi);
    TmpInst.addOperand(MCRegOp);
    TmpInst.addOperand(MCOperand::createExpr(Expr));
    Instructions.push_back(TmpInst);
    PrevReg = MCRegOp;
  }

  // The low bits require touching in 3 cases:
  // * A non-immediate value will always require both instructions.
  // * An effectively imm13 value needs only an 'or' instruction.
  // * Otherwise, an immediate that is not effectively imm13 requires the
  //   'or' only if bits remain after clearing the 22 bits that 'sethi' set.
  // If the low bits are known zeros, there's nothing to do.
  // In the second case, and only in that case, must we NOT clear
  // bits of the immediate value via the %lo() assembler function.
  // Note also, the 'or' instruction doesn't mind a large value in the case
  // where the operand to 'set' was 0xFFFFFzzz - it does exactly what you mean.
  if (!IsImm || IsEffectivelyImm13 || (ImmValue & 0x3ff)) {
    MCInst TmpInst;
    const MCExpr *Expr;
    if (IsEffectivelyImm13)
      Expr = ValExpr;
    else
      Expr = adjustPICRelocation(SparcMCExpr::VK_Sparc_LO, ValExpr);
    TmpInst.setLoc(IDLoc);
    TmpInst.setOpcode(SP::ORri);
    TmpInst.addOperand(MCRegOp);
    TmpInst.addOperand(PrevReg);
    TmpInst.addOperand(MCOperand::createExpr(Expr));
    Instructions.push_back(TmpInst);
  }
  return false;
}

bool SparcAsmParser::MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                             OperandVector &Operands,
                                             MCStreamer &Out,
                                             uint64_t &ErrorInfo,
                                             bool MatchingInlineAsm) {
  MCInst Inst;
  SmallVector<MCInst, 8> Instructions;
  unsigned MatchResult = MatchInstructionImpl(Operands, Inst, ErrorInfo,
                                              MatchingInlineAsm);
  switch (MatchResult) {
  case Match_Success: {
    switch (Inst.getOpcode()) {
    default:
      Inst.setLoc(IDLoc);
      Instructions.push_back(Inst);
      break;
    case SP::SET:
      if (expandSET(Inst, IDLoc, Instructions))
        return true;
      break;
    }

    for (const MCInst &I : Instructions) {
      Out.EmitInstruction(I, getSTI());
    }
    return false;
  }

  case Match_MissingFeature:
    return Error(IDLoc,
                 "instruction requires a CPU feature not currently enabled");

  case Match_InvalidOperand: {
    SMLoc ErrorLoc = IDLoc;
    if (ErrorInfo != ~0ULL) {
      if (ErrorInfo >= Operands.size())
        return Error(IDLoc, "too few operands for instruction");

      ErrorLoc = ((SparcOperand &)*Operands[ErrorInfo]).getStartLoc();
      if (ErrorLoc == SMLoc())
        ErrorLoc = IDLoc;
    }

    return Error(ErrorLoc, "invalid operand for instruction");
  }
  case Match_MnemonicFail:
    return Error(IDLoc, "invalid instruction mnemonic");
  }
  llvm_unreachable("Implement any new match types added!");
}

bool SparcAsmParser::ParseRegister(unsigned &RegNo, SMLoc &StartLoc,
                                   SMLoc &EndLoc) {
  const AsmToken &Tok = Parser.getTok();
  StartLoc = Tok.getLoc();
  EndLoc = Tok.getEndLoc();
  RegNo = 0;
  if (getLexer().getKind() != AsmToken::Percent)
    return false;
  Parser.Lex();
  unsigned regKind = SparcOperand::rk_None;
  if (matchRegisterName(Tok, RegNo, regKind)) {
    Parser.Lex();
    return false;
  }

  return Error(StartLoc, "invalid register name");
}

static void applyMnemonicAliases(StringRef &Mnemonic, uint64_t Features,
                                 unsigned VariantID);

bool SparcAsmParser::ParseInstruction(ParseInstructionInfo &Info,
                                      StringRef Name, SMLoc NameLoc,
                                      OperandVector &Operands) {

  // First operand in MCInst is instruction mnemonic.
  Operands.push_back(SparcOperand::CreateToken(Name, NameLoc));

  // apply mnemonic aliases, if any, so that we can parse operands correctly.
  applyMnemonicAliases(Name, getAvailableFeatures(), 0);

  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    // Read the first operand.
    if (getLexer().is(AsmToken::Comma)) {
      if (parseBranchModifiers(Operands) != MatchOperand_Success) {
        SMLoc Loc = getLexer().getLoc();
        return Error(Loc, "unexpected token");
      }
    }
    if (parseOperand(Operands, Name) != MatchOperand_Success) {
      SMLoc Loc = getLexer().getLoc();
      return Error(Loc, "unexpected token");
    }

    while (getLexer().is(AsmToken::Comma) || getLexer().is(AsmToken::Plus)) {
      if (getLexer().is(AsmToken::Plus)) {
      // Plus tokens are significant in software_traps (p83, sparcv8.pdf). We must capture them.
        Operands.push_back(SparcOperand::CreateToken("+", Parser.getTok().getLoc()));
      }
      Parser.Lex(); // Eat the comma or plus.
      // Parse and remember the operand.
      if (parseOperand(Operands, Name) != MatchOperand_Success) {
        SMLoc Loc = getLexer().getLoc();
        return Error(Loc, "unexpected token");
      }
    }
  }
  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    SMLoc Loc = getLexer().getLoc();
    return Error(Loc, "unexpected token");
  }
  Parser.Lex(); // Consume the EndOfStatement.
  return false;
}

bool SparcAsmParser::
ParseDirective(AsmToken DirectiveID)
{
  StringRef IDVal = DirectiveID.getString();

  if (IDVal == ".register") {
    // For now, ignore .register directive.
    Parser.eatToEndOfStatement();
    return false;
  }
  if (IDVal == ".proc") {
    // For compatibility, ignore this directive.
    // (It's supposed to be an "optimization" in the Sun assembler)
    Parser.eatToEndOfStatement();
    return false;
  }

  // Let the MC layer to handle other directives.
  return true;
}

OperandMatchResultTy
SparcAsmParser::parseMEMOperand(OperandVector &Operands) {
  SMLoc S, E;
  unsigned BaseReg = 0;

  if (ParseRegister(BaseReg, S, E)) {
    return MatchOperand_NoMatch;
  }

  switch (getLexer().getKind()) {
  default: return MatchOperand_NoMatch;

  case AsmToken::Comma:
  case AsmToken::RBrac:
  case AsmToken::EndOfStatement:
    Operands.push_back(SparcOperand::CreateMEMr(BaseReg, S, E));
    return MatchOperand_Success;

  case AsmToken:: Plus:
    Parser.Lex(); // Eat the '+'
    break;
  case AsmToken::Minus:
    break;
  }

  std::unique_ptr<SparcOperand> Offset;
  OperandMatchResultTy ResTy = parseSparcAsmOperand(Offset);
  if (ResTy != MatchOperand_Success || !Offset)
    return MatchOperand_NoMatch;

  Operands.push_back(
      Offset->isImm() ? SparcOperand::MorphToMEMri(BaseReg, std::move(Offset))
                      : SparcOperand::MorphToMEMrr(BaseReg, std::move(Offset)));

  return MatchOperand_Success;
}

OperandMatchResultTy SparcAsmParser::parseMembarTag(OperandVector &Operands) {
  SMLoc S = Parser.getTok().getLoc();
  const MCExpr *EVal;
  int64_t ImmVal = 0;

  std::unique_ptr<SparcOperand> Mask;
  if (parseSparcAsmOperand(Mask) == MatchOperand_Success) {
    if (!Mask->isImm() || !Mask->getImm()->evaluateAsAbsolute(ImmVal) ||
        ImmVal < 0 || ImmVal > 127) {
      Error(S, "invalid membar mask number");
      return MatchOperand_ParseFail;
    }
  }

  while (getLexer().getKind() == AsmToken::Hash) {
    SMLoc TagStart = getLexer().getLoc();
    Parser.Lex(); // Eat the '#'.
    unsigned MaskVal = StringSwitch<unsigned>(Parser.getTok().getString())
      .Case("LoadLoad", 0x1)
      .Case("StoreLoad", 0x2)
      .Case("LoadStore", 0x4)
      .Case("StoreStore", 0x8)
      .Case("Lookaside", 0x10)
      .Case("MemIssue", 0x20)
      .Case("Sync", 0x40)
      .Default(0);

    Parser.Lex(); // Eat the identifier token.

    if (!MaskVal) {
      Error(TagStart, "unknown membar tag");
      return MatchOperand_ParseFail;
    }

    ImmVal |= MaskVal;

    if (getLexer().getKind() == AsmToken::Pipe)
      Parser.Lex(); // Eat the '|'.
  }

  EVal = MCConstantExpr::create(ImmVal, getContext());
  SMLoc E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
  Operands.push_back(SparcOperand::CreateImm(EVal, S, E));
  return MatchOperand_Success;
}

OperandMatchResultTy
SparcAsmParser::parseOperand(OperandVector &Operands, StringRef Mnemonic) {

  OperandMatchResultTy ResTy = MatchOperandParserImpl(Operands, Mnemonic);

  // If there wasn't a custom match, try the generic matcher below. Otherwise,
  // there was a match, but an error occurred, in which case, just return that
  // the operand parsing failed.
  if (ResTy == MatchOperand_Success || ResTy == MatchOperand_ParseFail)
    return ResTy;

  if (getLexer().is(AsmToken::LBrac)) {
    // Memory operand
    Operands.push_back(SparcOperand::CreateToken("[",
                                                 Parser.getTok().getLoc()));
    Parser.Lex(); // Eat the [

    if (Mnemonic == "cas" || Mnemonic == "casx" || Mnemonic == "casa") {
      SMLoc S = Parser.getTok().getLoc();
      if (getLexer().getKind() != AsmToken::Percent)
        return MatchOperand_NoMatch;
      Parser.Lex(); // eat %

      unsigned RegNo, RegKind;
      if (!matchRegisterName(Parser.getTok(), RegNo, RegKind))
        return MatchOperand_NoMatch;

      Parser.Lex(); // Eat the identifier token.
      SMLoc E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer()-1);
      Operands.push_back(SparcOperand::CreateReg(RegNo, RegKind, S, E));
      ResTy = MatchOperand_Success;
    } else {
      ResTy = parseMEMOperand(Operands);
    }

    if (ResTy != MatchOperand_Success)
      return ResTy;

    if (!getLexer().is(AsmToken::RBrac))
      return MatchOperand_ParseFail;

    Operands.push_back(SparcOperand::CreateToken("]",
                                                 Parser.getTok().getLoc()));
    Parser.Lex(); // Eat the ]

    // Parse an optional address-space identifier after the address.
    if (getLexer().is(AsmToken::Integer)) {
      std::unique_ptr<SparcOperand> Op;
      ResTy = parseSparcAsmOperand(Op, false);
      if (ResTy != MatchOperand_Success || !Op)
        return MatchOperand_ParseFail;
      Operands.push_back(std::move(Op));
    }
    return MatchOperand_Success;
  }

  std::unique_ptr<SparcOperand> Op;

  ResTy = parseSparcAsmOperand(Op, (Mnemonic == "call"));
  if (ResTy != MatchOperand_Success || !Op)
    return MatchOperand_ParseFail;

  // Push the parsed operand into the list of operands
  Operands.push_back(std::move(Op));

  return MatchOperand_Success;
}

OperandMatchResultTy
SparcAsmParser::parseSparcAsmOperand(std::unique_ptr<SparcOperand> &Op,
                                     bool isCall) {
  SMLoc S = Parser.getTok().getLoc();
  SMLoc E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
  const MCExpr *EVal;

  Op = nullptr;
  switch (getLexer().getKind()) {
  default:  break;

  case AsmToken::Percent:
    Parser.Lex(); // Eat the '%'.
    unsigned RegNo;
    unsigned RegKind;
    if (matchRegisterName(Parser.getTok(), RegNo, RegKind)) {
      StringRef name = Parser.getTok().getString();
      Parser.Lex(); // Eat the identifier token.
      E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
      switch (RegNo) {
      default:
        Op = SparcOperand::CreateReg(RegNo, RegKind, S, E);
        break;
      case Sparc::PSR:
        Op = SparcOperand::CreateToken("%psr", S);
        break;
      case Sparc::FSR:
        Op = SparcOperand::CreateToken("%fsr", S);
        break;
      case Sparc::FQ:
        Op = SparcOperand::CreateToken("%fq", S);
        break;
      case Sparc::CPSR:
        Op = SparcOperand::CreateToken("%csr", S);
        break;
      case Sparc::CPQ:
        Op = SparcOperand::CreateToken("%cq", S);
        break;
      case Sparc::WIM:
        Op = SparcOperand::CreateToken("%wim", S);
        break;
      case Sparc::TBR:
        Op = SparcOperand::CreateToken("%tbr", S);
        break;
      case Sparc::ICC:
        if (name == "xcc")
          Op = SparcOperand::CreateToken("%xcc", S);
        else
          Op = SparcOperand::CreateToken("%icc", S);
        break;
      }
      break;
    }
    if (matchSparcAsmModifiers(EVal, E)) {
      E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
      Op = SparcOperand::CreateImm(EVal, S, E);
    }
    break;

  case AsmToken::Minus:
  case AsmToken::Integer:
  case AsmToken::LParen:
  case AsmToken::Dot:
    if (!getParser().parseExpression(EVal, E))
      Op = SparcOperand::CreateImm(EVal, S, E);
    break;

  case AsmToken::Identifier: {
    StringRef Identifier;
    if (!getParser().parseIdentifier(Identifier)) {
      E = SMLoc::getFromPointer(Parser.getTok().getLoc().getPointer() - 1);
      MCSymbol *Sym = getContext().getOrCreateSymbol(Identifier);

      const MCExpr *Res = MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_None,
                                                  getContext());
      SparcMCExpr::VariantKind Kind = SparcMCExpr::VK_Sparc_13;

      if (getContext().getObjectFileInfo()->isPositionIndependent()) {
        if (isCall)
          Kind = SparcMCExpr::VK_Sparc_WPLT30;
        else
          Kind = SparcMCExpr::VK_Sparc_GOT13;
      }

      Res = SparcMCExpr::create(Kind, Res, getContext());

      Op = SparcOperand::CreateImm(Res, S, E);
    }
    break;
  }
  }
  return (Op) ? MatchOperand_Success : MatchOperand_ParseFail;
}

OperandMatchResultTy
SparcAsmParser::parseBranchModifiers(OperandVector &Operands) {
  // parse (,a|,pn|,pt)+

  while (getLexer().is(AsmToken::Comma)) {
    Parser.Lex(); // Eat the comma

    if (!getLexer().is(AsmToken::Identifier))
      return MatchOperand_ParseFail;
    StringRef modName = Parser.getTok().getString();
    if (modName == "a" || modName == "pn" || modName == "pt") {
      Operands.push_back(SparcOperand::CreateToken(modName,
                                                   Parser.getTok().getLoc()));
      Parser.Lex(); // eat the identifier.
    }
  }
  return MatchOperand_Success;
}

bool SparcAsmParser::matchRegisterName(const AsmToken &Tok, unsigned &RegNo,
                                       unsigned &RegKind) {
  int64_t intVal = 0;
  RegNo = 0;
  RegKind = SparcOperand::rk_None;
  if (Tok.is(AsmToken::Identifier)) {
    StringRef name = Tok.getString();

    // %fp
    if (name.equals("fp")) {
      RegNo = Sparc::I6;
      RegKind = SparcOperand::rk_IntReg;
      return true;
    }
    // %sp
    if (name.equals("sp")) {
      RegNo = Sparc::O6;
      RegKind = SparcOperand::rk_IntReg;
      return true;
    }

    if (name.equals("y")) {
      RegNo = Sparc::Y;
      RegKind = SparcOperand::rk_Special;
      return true;
    }

    if (name.substr(0, 3).equals_lower("asr")
        && !name.substr(3).getAsInteger(10, intVal)
        && intVal > 0 && intVal < 32) {
      RegNo = ASRRegs[intVal];
      RegKind = SparcOperand::rk_Special;
      return true;
    }

    // %fprs is an alias of %asr6.
    if (name.equals("fprs")) {
      RegNo = ASRRegs[6];
      RegKind = SparcOperand::rk_Special;
      return true;
    }

    if (name.equals("icc")) {
      RegNo = Sparc::ICC;
      RegKind = SparcOperand::rk_Special;
      return true;
    }

    if (name.equals("psr")) {
      RegNo = Sparc::PSR;
      RegKind = SparcOperand::rk_Special;
      return true;
    }

    if (name.equals("fsr")) {
      RegNo = Sparc::FSR;
      RegKind = SparcOperand::rk_Special;
      return true;
    }

    if (name.equals("fq")) {
      RegNo = Sparc::FQ;
      RegKind = SparcOperand::rk_Special;
      return true;
    }

    if (name.equals("csr")) {
      RegNo = Sparc::CPSR;
      RegKind = SparcOperand::rk_Special;
      return true;
    }

    if (name.equals("cq")) {
      RegNo = Sparc::CPQ;
      RegKind = SparcOperand::rk_Special;
      return true;
    }

    if (name.equals("wim")) {
      RegNo = Sparc::WIM;
      RegKind = SparcOperand::rk_Special;
      return true;
    }

    if (name.equals("tbr")) {
      RegNo = Sparc::TBR;
      RegKind = SparcOperand::rk_Special;
      return true;
    }

    if (name.equals("xcc")) {
      // FIXME:: check 64bit.
      RegNo = Sparc::ICC;
      RegKind = SparcOperand::rk_Special;
      return true;
    }

    // %fcc0 - %fcc3
    if (name.substr(0, 3).equals_lower("fcc")
        && !name.substr(3).getAsInteger(10, intVal)
        && intVal < 4) {
      // FIXME: check 64bit and  handle %fcc1 - %fcc3
      RegNo = Sparc::FCC0 + intVal;
      RegKind = SparcOperand::rk_Special;
      return true;
    }

    // %g0 - %g7
    if (name.substr(0, 1).equals_lower("g")
        && !name.substr(1).getAsInteger(10, intVal)
        && intVal < 8) {
      RegNo = IntRegs[intVal];
      RegKind = SparcOperand::rk_IntReg;
      return true;
    }
    // %o0 - %o7
    if (name.substr(0, 1).equals_lower("o")
        && !name.substr(1).getAsInteger(10, intVal)
        && intVal < 8) {
      RegNo = IntRegs[8 + intVal];
      RegKind = SparcOperand::rk_IntReg;
      return true;
    }
    if (name.substr(0, 1).equals_lower("l")
        && !name.substr(1).getAsInteger(10, intVal)
        && intVal < 8) {
      RegNo = IntRegs[16 + intVal];
      RegKind = SparcOperand::rk_IntReg;
      return true;
    }
    if (name.substr(0, 1).equals_lower("i")
        && !name.substr(1).getAsInteger(10, intVal)
        && intVal < 8) {
      RegNo = IntRegs[24 + intVal];
      RegKind = SparcOperand::rk_IntReg;
      return true;
    }
    // %f0 - %f31
    if (name.substr(0, 1).equals_lower("f")
        && !name.substr(1, 2).getAsInteger(10, intVal) && intVal < 32) {
      RegNo = FloatRegs[intVal];
      RegKind = SparcOperand::rk_FloatReg;
      return true;
    }
    // %f32 - %f62
    if (name.substr(0, 1).equals_lower("f")
        && !name.substr(1, 2).getAsInteger(10, intVal)
        && intVal >= 32 && intVal <= 62 && (intVal % 2 == 0)) {
      // FIXME: Check V9
      RegNo = DoubleRegs[intVal/2];
      RegKind = SparcOperand::rk_DoubleReg;
      return true;
    }

    // %r0 - %r31
    if (name.substr(0, 1).equals_lower("r")
        && !name.substr(1, 2).getAsInteger(10, intVal) && intVal < 31) {
      RegNo = IntRegs[intVal];
      RegKind = SparcOperand::rk_IntReg;
      return true;
    }

    // %c0 - %c31
    if (name.substr(0, 1).equals_lower("c")
        && !name.substr(1).getAsInteger(10, intVal)
        && intVal < 32) {
      RegNo = CoprocRegs[intVal];
      RegKind = SparcOperand::rk_CoprocReg;
      return true;
    }

    if (name.equals("tpc")) {
      RegNo = Sparc::TPC;
      RegKind = SparcOperand::rk_Special;
      return true;
    }
    if (name.equals("tnpc")) {
      RegNo = Sparc::TNPC;
      RegKind = SparcOperand::rk_Special;
      return true;
    }
    if (name.equals("tstate")) {
      RegNo = Sparc::TSTATE;
      RegKind = SparcOperand::rk_Special;
      return true;
    }
    if (name.equals("tt")) {
      RegNo = Sparc::TT;
      RegKind = SparcOperand::rk_Special;
      return true;
    }
    if (name.equals("tick")) {
      RegNo = Sparc::TICK;
      RegKind = SparcOperand::rk_Special;
      return true;
    }
    if (name.equals("tba")) {
      RegNo = Sparc::TBA;
      RegKind = SparcOperand::rk_Special;
      return true;
    }
    if (name.equals("pstate")) {
      RegNo = Sparc::PSTATE;
      RegKind = SparcOperand::rk_Special;
      return true;
    }
    if (name.equals("tl")) {
      RegNo = Sparc::TL;
      RegKind = SparcOperand::rk_Special;
      return true;
    }
    if (name.equals("pil")) {
      RegNo = Sparc::PIL;
      RegKind = SparcOperand::rk_Special;
      return true;
    }
    if (name.equals("cwp")) {
      RegNo = Sparc::CWP;
      RegKind = SparcOperand::rk_Special;
      return true;
    }
    if (name.equals("cansave")) {
      RegNo = Sparc::CANSAVE;
      RegKind = SparcOperand::rk_Special;
      return true;
    }
    if (name.equals("canrestore")) {
      RegNo = Sparc::CANRESTORE;
      RegKind = SparcOperand::rk_Special;
      return true;
    }
    if (name.equals("cleanwin")) {
      RegNo = Sparc::CLEANWIN;
      RegKind = SparcOperand::rk_Special;
      return true;
    }
    if (name.equals("otherwin")) {
      RegNo = Sparc::OTHERWIN;
      RegKind = SparcOperand::rk_Special;
      return true;
    }
    if (name.equals("wstate")) {
      RegNo = Sparc::WSTATE;
      RegKind = SparcOperand::rk_Special;
      return true;
    }
  }
  return false;
}

// Determine if an expression contains a reference to the symbol
// "_GLOBAL_OFFSET_TABLE_".
static bool hasGOTReference(const MCExpr *Expr) {
  switch (Expr->getKind()) {
  case MCExpr::Target:
    if (const SparcMCExpr *SE = dyn_cast<SparcMCExpr>(Expr))
      return hasGOTReference(SE->getSubExpr());
    break;

  case MCExpr::Constant:
    break;

  case MCExpr::Binary: {
    const MCBinaryExpr *BE = cast<MCBinaryExpr>(Expr);
    return hasGOTReference(BE->getLHS()) || hasGOTReference(BE->getRHS());
  }

  case MCExpr::SymbolRef: {
    const MCSymbolRefExpr &SymRef = *cast<MCSymbolRefExpr>(Expr);
    return (SymRef.getSymbol().getName() == "_GLOBAL_OFFSET_TABLE_");
  }

  case MCExpr::Unary:
    return hasGOTReference(cast<MCUnaryExpr>(Expr)->getSubExpr());
  }
  return false;
}

const SparcMCExpr *
SparcAsmParser::adjustPICRelocation(SparcMCExpr::VariantKind VK,
                                    const MCExpr *subExpr) {
  // When in PIC mode, "%lo(...)" and "%hi(...)" behave differently.
  // If the expression refers contains _GLOBAL_OFFSETE_TABLE, it is
  // actually a %pc10 or %pc22 relocation. Otherwise, they are interpreted
  // as %got10 or %got22 relocation.

  if (getContext().getObjectFileInfo()->isPositionIndependent()) {
    switch(VK) {
    default: break;
    case SparcMCExpr::VK_Sparc_LO:
      VK = (hasGOTReference(subExpr) ? SparcMCExpr::VK_Sparc_PC10
                                     : SparcMCExpr::VK_Sparc_GOT10);
      break;
    case SparcMCExpr::VK_Sparc_HI:
      VK = (hasGOTReference(subExpr) ? SparcMCExpr::VK_Sparc_PC22
                                     : SparcMCExpr::VK_Sparc_GOT22);
      break;
    }
  }

  return SparcMCExpr::create(VK, subExpr, getContext());
}

bool SparcAsmParser::matchSparcAsmModifiers(const MCExpr *&EVal,
                                            SMLoc &EndLoc) {
  AsmToken Tok = Parser.getTok();
  if (!Tok.is(AsmToken::Identifier))
    return false;

  StringRef name = Tok.getString();

  SparcMCExpr::VariantKind VK = SparcMCExpr::parseVariantKind(name);

  if (VK == SparcMCExpr::VK_Sparc_None)
    return false;

  Parser.Lex(); // Eat the identifier.
  if (Parser.getTok().getKind() != AsmToken::LParen)
    return false;

  Parser.Lex(); // Eat the LParen token.
  const MCExpr *subExpr;
  if (Parser.parseParenExpression(subExpr, EndLoc))
    return false;

  EVal = adjustPICRelocation(VK, subExpr);
  return true;
}

extern "C" void LLVMInitializeSparcAsmParser() {
  RegisterMCAsmParser<SparcAsmParser> A(getTheSparcTarget());
  RegisterMCAsmParser<SparcAsmParser> B(getTheSparcV9Target());
  RegisterMCAsmParser<SparcAsmParser> C(getTheSparcelTarget());
}

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "SparcGenAsmMatcher.inc"

unsigned SparcAsmParser::validateTargetOperandClass(MCParsedAsmOperand &GOp,
                                                    unsigned Kind) {
  SparcOperand &Op = (SparcOperand &)GOp;
  if (Op.isFloatOrDoubleReg()) {
    switch (Kind) {
    default: break;
    case MCK_DFPRegs:
      if (!Op.isFloatReg() || SparcOperand::MorphToDoubleReg(Op))
        return MCTargetAsmParser::Match_Success;
      break;
    case MCK_QFPRegs:
      if (SparcOperand::MorphToQuadReg(Op))
        return MCTargetAsmParser::Match_Success;
      break;
    }
  }
  if (Op.isIntReg() && Kind == MCK_IntPair) {
    if (SparcOperand::MorphToIntPairReg(Op))
      return MCTargetAsmParser::Match_Success;
  }
  if (Op.isCoprocReg() && Kind == MCK_CoprocPair) {
     if (SparcOperand::MorphToCoprocPairReg(Op))
       return MCTargetAsmParser::Match_Success;
   }
  return Match_InvalidOperand;
}
