//==- AArch64AsmParser.cpp - Parse AArch64 assembly to MCInst instructions -==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/AArch64AddressingModes.h"
#include "MCTargetDesc/AArch64MCExpr.h"
#include "MCTargetDesc/AArch64MCTargetDesc.h"
#include "MCTargetDesc/AArch64TargetStreamer.h"
#include "AArch64InstrInfo.h"
#include "Utils/AArch64BaseInfo.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCLinkerOptimizationHint.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCAsmParserExtension.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/TargetParser.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

namespace {

enum class RegKind {
  Scalar,
  NeonVector,
  SVEDataVector,
  SVEPredicateVector
};

enum RegConstraintEqualityTy {
  EqualsReg,
  EqualsSuperReg,
  EqualsSubReg
};

class AArch64AsmParser : public MCTargetAsmParser {
private:
  StringRef Mnemonic; ///< Instruction mnemonic.

  // Map of register aliases registers via the .req directive.
  StringMap<std::pair<RegKind, unsigned>> RegisterReqs;

  class PrefixInfo {
  public:
    static PrefixInfo CreateFromInst(const MCInst &Inst, uint64_t TSFlags) {
      PrefixInfo Prefix;
      switch (Inst.getOpcode()) {
      case AArch64::MOVPRFX_ZZ:
        Prefix.Active = true;
        Prefix.Dst = Inst.getOperand(0).getReg();
        break;
      case AArch64::MOVPRFX_ZPmZ_B:
      case AArch64::MOVPRFX_ZPmZ_H:
      case AArch64::MOVPRFX_ZPmZ_S:
      case AArch64::MOVPRFX_ZPmZ_D:
        Prefix.Active = true;
        Prefix.Predicated = true;
        Prefix.ElementSize = TSFlags & AArch64::ElementSizeMask;
        assert(Prefix.ElementSize != AArch64::ElementSizeNone &&
               "No destructive element size set for movprfx");
        Prefix.Dst = Inst.getOperand(0).getReg();
        Prefix.Pg = Inst.getOperand(2).getReg();
        break;
      case AArch64::MOVPRFX_ZPzZ_B:
      case AArch64::MOVPRFX_ZPzZ_H:
      case AArch64::MOVPRFX_ZPzZ_S:
      case AArch64::MOVPRFX_ZPzZ_D:
        Prefix.Active = true;
        Prefix.Predicated = true;
        Prefix.ElementSize = TSFlags & AArch64::ElementSizeMask;
        assert(Prefix.ElementSize != AArch64::ElementSizeNone &&
               "No destructive element size set for movprfx");
        Prefix.Dst = Inst.getOperand(0).getReg();
        Prefix.Pg = Inst.getOperand(1).getReg();
        break;
      default:
        break;
      }

      return Prefix;
    }

    PrefixInfo() : Active(false), Predicated(false) {}
    bool isActive() const { return Active; }
    bool isPredicated() const { return Predicated; }
    unsigned getElementSize() const {
      assert(Predicated);
      return ElementSize;
    }
    unsigned getDstReg() const { return Dst; }
    unsigned getPgReg() const {
      assert(Predicated);
      return Pg;
    }

  private:
    bool Active;
    bool Predicated;
    unsigned ElementSize;
    unsigned Dst;
    unsigned Pg;
  } NextPrefix;

  AArch64TargetStreamer &getTargetStreamer() {
    MCTargetStreamer &TS = *getParser().getStreamer().getTargetStreamer();
    return static_cast<AArch64TargetStreamer &>(TS);
  }

  SMLoc getLoc() const { return getParser().getTok().getLoc(); }

  bool parseSysAlias(StringRef Name, SMLoc NameLoc, OperandVector &Operands);
  void createSysAlias(uint16_t Encoding, OperandVector &Operands, SMLoc S);
  AArch64CC::CondCode parseCondCodeString(StringRef Cond);
  bool parseCondCode(OperandVector &Operands, bool invertCondCode);
  unsigned matchRegisterNameAlias(StringRef Name, RegKind Kind);
  bool parseRegister(OperandVector &Operands);
  bool parseSymbolicImmVal(const MCExpr *&ImmVal);
  bool parseNeonVectorList(OperandVector &Operands);
  bool parseOptionalMulOperand(OperandVector &Operands);
  bool parseOperand(OperandVector &Operands, bool isCondCode,
                    bool invertCondCode);

  bool showMatchError(SMLoc Loc, unsigned ErrCode, uint64_t ErrorInfo,
                      OperandVector &Operands);

  bool parseDirectiveArch(SMLoc L);
  bool parseDirectiveArchExtension(SMLoc L);
  bool parseDirectiveCPU(SMLoc L);
  bool parseDirectiveInst(SMLoc L);

  bool parseDirectiveTLSDescCall(SMLoc L);

  bool parseDirectiveLOH(StringRef LOH, SMLoc L);
  bool parseDirectiveLtorg(SMLoc L);

  bool parseDirectiveReq(StringRef Name, SMLoc L);
  bool parseDirectiveUnreq(SMLoc L);
  bool parseDirectiveCFINegateRAState();
  bool parseDirectiveCFIBKeyFrame();

  bool validateInstruction(MCInst &Inst, SMLoc &IDLoc,
                           SmallVectorImpl<SMLoc> &Loc);
  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;
/// @name Auto-generated Match Functions
/// {

#define GET_ASSEMBLER_HEADER
#include "AArch64GenAsmMatcher.inc"

  /// }

  OperandMatchResultTy tryParseScalarRegister(unsigned &Reg);
  OperandMatchResultTy tryParseVectorRegister(unsigned &Reg, StringRef &Kind,
                                              RegKind MatchKind);
  OperandMatchResultTy tryParseOptionalShiftExtend(OperandVector &Operands);
  OperandMatchResultTy tryParseBarrierOperand(OperandVector &Operands);
  OperandMatchResultTy tryParseMRSSystemRegister(OperandVector &Operands);
  OperandMatchResultTy tryParseSysReg(OperandVector &Operands);
  OperandMatchResultTy tryParseSysCROperand(OperandVector &Operands);
  template <bool IsSVEPrefetch = false>
  OperandMatchResultTy tryParsePrefetch(OperandVector &Operands);
  OperandMatchResultTy tryParsePSBHint(OperandVector &Operands);
  OperandMatchResultTy tryParseBTIHint(OperandVector &Operands);
  OperandMatchResultTy tryParseAdrpLabel(OperandVector &Operands);
  OperandMatchResultTy tryParseAdrLabel(OperandVector &Operands);
  template<bool AddFPZeroAsLiteral>
  OperandMatchResultTy tryParseFPImm(OperandVector &Operands);
  OperandMatchResultTy tryParseImmWithOptionalShift(OperandVector &Operands);
  OperandMatchResultTy tryParseGPR64sp0Operand(OperandVector &Operands);
  bool tryParseNeonVectorRegister(OperandVector &Operands);
  OperandMatchResultTy tryParseVectorIndex(OperandVector &Operands);
  OperandMatchResultTy tryParseGPRSeqPair(OperandVector &Operands);
  template <bool ParseShiftExtend,
            RegConstraintEqualityTy EqTy = RegConstraintEqualityTy::EqualsReg>
  OperandMatchResultTy tryParseGPROperand(OperandVector &Operands);
  template <bool ParseShiftExtend, bool ParseSuffix>
  OperandMatchResultTy tryParseSVEDataVector(OperandVector &Operands);
  OperandMatchResultTy tryParseSVEPredicateVector(OperandVector &Operands);
  template <RegKind VectorKind>
  OperandMatchResultTy tryParseVectorList(OperandVector &Operands,
                                          bool ExpectMatch = false);
  OperandMatchResultTy tryParseSVEPattern(OperandVector &Operands);

public:
  enum AArch64MatchResultTy {
    Match_InvalidSuffix = FIRST_TARGET_MATCH_RESULT_TY,
#define GET_OPERAND_DIAGNOSTIC_TYPES
#include "AArch64GenAsmMatcher.inc"
  };
  bool IsILP32;

  AArch64AsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                   const MCInstrInfo &MII, const MCTargetOptions &Options)
    : MCTargetAsmParser(Options, STI, MII) {
    IsILP32 = Options.getABIName() == "ilp32";
    MCAsmParserExtension::Initialize(Parser);
    MCStreamer &S = getParser().getStreamer();
    if (S.getTargetStreamer() == nullptr)
      new AArch64TargetStreamer(S);

    // Alias .hword/.word/xword to the target-independent .2byte/.4byte/.8byte
    // directives as they have the same form and semantics:
    ///  ::= (.hword | .word | .xword ) [ expression (, expression)* ]
    Parser.addAliasForDirective(".hword", ".2byte");
    Parser.addAliasForDirective(".word", ".4byte");
    Parser.addAliasForDirective(".xword", ".8byte");

    // Initialize the set of available features.
    setAvailableFeatures(ComputeAvailableFeatures(getSTI().getFeatureBits()));
  }

  bool regsEqual(const MCParsedAsmOperand &Op1,
                 const MCParsedAsmOperand &Op2) const override;
  bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;
  bool ParseRegister(unsigned &RegNo, SMLoc &StartLoc, SMLoc &EndLoc) override;
  bool ParseDirective(AsmToken DirectiveID) override;
  unsigned validateTargetOperandClass(MCParsedAsmOperand &Op,
                                      unsigned Kind) override;

  static bool classifySymbolRef(const MCExpr *Expr,
                                AArch64MCExpr::VariantKind &ELFRefKind,
                                MCSymbolRefExpr::VariantKind &DarwinRefKind,
                                int64_t &Addend);
};

/// AArch64Operand - Instances of this class represent a parsed AArch64 machine
/// instruction.
class AArch64Operand : public MCParsedAsmOperand {
private:
  enum KindTy {
    k_Immediate,
    k_ShiftedImm,
    k_CondCode,
    k_Register,
    k_VectorList,
    k_VectorIndex,
    k_Token,
    k_SysReg,
    k_SysCR,
    k_Prefetch,
    k_ShiftExtend,
    k_FPImm,
    k_Barrier,
    k_PSBHint,
    k_BTIHint,
  } Kind;

  SMLoc StartLoc, EndLoc;

  struct TokOp {
    const char *Data;
    unsigned Length;
    bool IsSuffix; // Is the operand actually a suffix on the mnemonic.
  };

  // Separate shift/extend operand.
  struct ShiftExtendOp {
    AArch64_AM::ShiftExtendType Type;
    unsigned Amount;
    bool HasExplicitAmount;
  };

  struct RegOp {
    unsigned RegNum;
    RegKind Kind;
    int ElementWidth;

    // The register may be allowed as a different register class,
    // e.g. for GPR64as32 or GPR32as64.
    RegConstraintEqualityTy EqualityTy;

    // In some cases the shift/extend needs to be explicitly parsed together
    // with the register, rather than as a separate operand. This is needed
    // for addressing modes where the instruction as a whole dictates the
    // scaling/extend, rather than specific bits in the instruction.
    // By parsing them as a single operand, we avoid the need to pass an
    // extra operand in all CodeGen patterns (because all operands need to
    // have an associated value), and we avoid the need to update TableGen to
    // accept operands that have no associated bits in the instruction.
    //
    // An added benefit of parsing them together is that the assembler
    // can give a sensible diagnostic if the scaling is not correct.
    //
    // The default is 'lsl #0' (HasExplicitAmount = false) if no
    // ShiftExtend is specified.
    ShiftExtendOp ShiftExtend;
  };

  struct VectorListOp {
    unsigned RegNum;
    unsigned Count;
    unsigned NumElements;
    unsigned ElementWidth;
    RegKind  RegisterKind;
  };

  struct VectorIndexOp {
    unsigned Val;
  };

  struct ImmOp {
    const MCExpr *Val;
  };

  struct ShiftedImmOp {
    const MCExpr *Val;
    unsigned ShiftAmount;
  };

  struct CondCodeOp {
    AArch64CC::CondCode Code;
  };

  struct FPImmOp {
    uint64_t Val; // APFloat value bitcasted to uint64_t.
    bool IsExact; // describes whether parsed value was exact.
  };

  struct BarrierOp {
    const char *Data;
    unsigned Length;
    unsigned Val; // Not the enum since not all values have names.
  };

  struct SysRegOp {
    const char *Data;
    unsigned Length;
    uint32_t MRSReg;
    uint32_t MSRReg;
    uint32_t PStateField;
  };

  struct SysCRImmOp {
    unsigned Val;
  };

  struct PrefetchOp {
    const char *Data;
    unsigned Length;
    unsigned Val;
  };

  struct PSBHintOp {
    const char *Data;
    unsigned Length;
    unsigned Val;
  };

  struct BTIHintOp {
    const char *Data;
    unsigned Length;
    unsigned Val;
  };

  struct ExtendOp {
    unsigned Val;
  };

  union {
    struct TokOp Tok;
    struct RegOp Reg;
    struct VectorListOp VectorList;
    struct VectorIndexOp VectorIndex;
    struct ImmOp Imm;
    struct ShiftedImmOp ShiftedImm;
    struct CondCodeOp CondCode;
    struct FPImmOp FPImm;
    struct BarrierOp Barrier;
    struct SysRegOp SysReg;
    struct SysCRImmOp SysCRImm;
    struct PrefetchOp Prefetch;
    struct PSBHintOp PSBHint;
    struct BTIHintOp BTIHint;
    struct ShiftExtendOp ShiftExtend;
  };

  // Keep the MCContext around as the MCExprs may need manipulated during
  // the add<>Operands() calls.
  MCContext &Ctx;

public:
  AArch64Operand(KindTy K, MCContext &Ctx) : Kind(K), Ctx(Ctx) {}

  AArch64Operand(const AArch64Operand &o) : MCParsedAsmOperand(), Ctx(o.Ctx) {
    Kind = o.Kind;
    StartLoc = o.StartLoc;
    EndLoc = o.EndLoc;
    switch (Kind) {
    case k_Token:
      Tok = o.Tok;
      break;
    case k_Immediate:
      Imm = o.Imm;
      break;
    case k_ShiftedImm:
      ShiftedImm = o.ShiftedImm;
      break;
    case k_CondCode:
      CondCode = o.CondCode;
      break;
    case k_FPImm:
      FPImm = o.FPImm;
      break;
    case k_Barrier:
      Barrier = o.Barrier;
      break;
    case k_Register:
      Reg = o.Reg;
      break;
    case k_VectorList:
      VectorList = o.VectorList;
      break;
    case k_VectorIndex:
      VectorIndex = o.VectorIndex;
      break;
    case k_SysReg:
      SysReg = o.SysReg;
      break;
    case k_SysCR:
      SysCRImm = o.SysCRImm;
      break;
    case k_Prefetch:
      Prefetch = o.Prefetch;
      break;
    case k_PSBHint:
      PSBHint = o.PSBHint;
      break;
    case k_BTIHint:
      BTIHint = o.BTIHint;
      break;
    case k_ShiftExtend:
      ShiftExtend = o.ShiftExtend;
      break;
    }
  }

  /// getStartLoc - Get the location of the first token of this operand.
  SMLoc getStartLoc() const override { return StartLoc; }
  /// getEndLoc - Get the location of the last token of this operand.
  SMLoc getEndLoc() const override { return EndLoc; }

  StringRef getToken() const {
    assert(Kind == k_Token && "Invalid access!");
    return StringRef(Tok.Data, Tok.Length);
  }

  bool isTokenSuffix() const {
    assert(Kind == k_Token && "Invalid access!");
    return Tok.IsSuffix;
  }

  const MCExpr *getImm() const {
    assert(Kind == k_Immediate && "Invalid access!");
    return Imm.Val;
  }

  const MCExpr *getShiftedImmVal() const {
    assert(Kind == k_ShiftedImm && "Invalid access!");
    return ShiftedImm.Val;
  }

  unsigned getShiftedImmShift() const {
    assert(Kind == k_ShiftedImm && "Invalid access!");
    return ShiftedImm.ShiftAmount;
  }

  AArch64CC::CondCode getCondCode() const {
    assert(Kind == k_CondCode && "Invalid access!");
    return CondCode.Code;
  }

  APFloat getFPImm() const {
    assert (Kind == k_FPImm && "Invalid access!");
    return APFloat(APFloat::IEEEdouble(), APInt(64, FPImm.Val, true));
  }

  bool getFPImmIsExact() const {
    assert (Kind == k_FPImm && "Invalid access!");
    return FPImm.IsExact;
  }

  unsigned getBarrier() const {
    assert(Kind == k_Barrier && "Invalid access!");
    return Barrier.Val;
  }

  StringRef getBarrierName() const {
    assert(Kind == k_Barrier && "Invalid access!");
    return StringRef(Barrier.Data, Barrier.Length);
  }

  unsigned getReg() const override {
    assert(Kind == k_Register && "Invalid access!");
    return Reg.RegNum;
  }

  RegConstraintEqualityTy getRegEqualityTy() const {
    assert(Kind == k_Register && "Invalid access!");
    return Reg.EqualityTy;
  }

  unsigned getVectorListStart() const {
    assert(Kind == k_VectorList && "Invalid access!");
    return VectorList.RegNum;
  }

  unsigned getVectorListCount() const {
    assert(Kind == k_VectorList && "Invalid access!");
    return VectorList.Count;
  }

  unsigned getVectorIndex() const {
    assert(Kind == k_VectorIndex && "Invalid access!");
    return VectorIndex.Val;
  }

  StringRef getSysReg() const {
    assert(Kind == k_SysReg && "Invalid access!");
    return StringRef(SysReg.Data, SysReg.Length);
  }

  unsigned getSysCR() const {
    assert(Kind == k_SysCR && "Invalid access!");
    return SysCRImm.Val;
  }

  unsigned getPrefetch() const {
    assert(Kind == k_Prefetch && "Invalid access!");
    return Prefetch.Val;
  }

  unsigned getPSBHint() const {
    assert(Kind == k_PSBHint && "Invalid access!");
    return PSBHint.Val;
  }

  StringRef getPSBHintName() const {
    assert(Kind == k_PSBHint && "Invalid access!");
    return StringRef(PSBHint.Data, PSBHint.Length);
  }

  unsigned getBTIHint() const {
    assert(Kind == k_BTIHint && "Invalid access!");
    return BTIHint.Val;
  }

  StringRef getBTIHintName() const {
    assert(Kind == k_BTIHint && "Invalid access!");
    return StringRef(BTIHint.Data, BTIHint.Length);
  }

  StringRef getPrefetchName() const {
    assert(Kind == k_Prefetch && "Invalid access!");
    return StringRef(Prefetch.Data, Prefetch.Length);
  }

  AArch64_AM::ShiftExtendType getShiftExtendType() const {
    if (Kind == k_ShiftExtend)
      return ShiftExtend.Type;
    if (Kind == k_Register)
      return Reg.ShiftExtend.Type;
    llvm_unreachable("Invalid access!");
  }

  unsigned getShiftExtendAmount() const {
    if (Kind == k_ShiftExtend)
      return ShiftExtend.Amount;
    if (Kind == k_Register)
      return Reg.ShiftExtend.Amount;
    llvm_unreachable("Invalid access!");
  }

  bool hasShiftExtendAmount() const {
    if (Kind == k_ShiftExtend)
      return ShiftExtend.HasExplicitAmount;
    if (Kind == k_Register)
      return Reg.ShiftExtend.HasExplicitAmount;
    llvm_unreachable("Invalid access!");
  }

  bool isImm() const override { return Kind == k_Immediate; }
  bool isMem() const override { return false; }

  bool isUImm6() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val >= 0 && Val < 64);
  }

  template <int Width> bool isSImm() const { return isSImmScaled<Width, 1>(); }

  template <int Bits, int Scale> DiagnosticPredicate isSImmScaled() const {
    return isImmScaled<Bits, Scale>(true);
  }

  template <int Bits, int Scale> DiagnosticPredicate isUImmScaled() const {
    return isImmScaled<Bits, Scale>(false);
  }

  template <int Bits, int Scale>
  DiagnosticPredicate isImmScaled(bool Signed) const {
    if (!isImm())
      return DiagnosticPredicateTy::NoMatch;

    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return DiagnosticPredicateTy::NoMatch;

    int64_t MinVal, MaxVal;
    if (Signed) {
      int64_t Shift = Bits - 1;
      MinVal = (int64_t(1) << Shift) * -Scale;
      MaxVal = ((int64_t(1) << Shift) - 1) * Scale;
    } else {
      MinVal = 0;
      MaxVal = ((int64_t(1) << Bits) - 1) * Scale;
    }

    int64_t Val = MCE->getValue();
    if (Val >= MinVal && Val <= MaxVal && (Val % Scale) == 0)
      return DiagnosticPredicateTy::Match;

    return DiagnosticPredicateTy::NearMatch;
  }

  DiagnosticPredicate isSVEPattern() const {
    if (!isImm())
      return DiagnosticPredicateTy::NoMatch;
    auto *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return DiagnosticPredicateTy::NoMatch;
    int64_t Val = MCE->getValue();
    if (Val >= 0 && Val < 32)
      return DiagnosticPredicateTy::Match;
    return DiagnosticPredicateTy::NearMatch;
  }

  bool isSymbolicUImm12Offset(const MCExpr *Expr) const {
    AArch64MCExpr::VariantKind ELFRefKind;
    MCSymbolRefExpr::VariantKind DarwinRefKind;
    int64_t Addend;
    if (!AArch64AsmParser::classifySymbolRef(Expr, ELFRefKind, DarwinRefKind,
                                           Addend)) {
      // If we don't understand the expression, assume the best and
      // let the fixup and relocation code deal with it.
      return true;
    }

    if (DarwinRefKind == MCSymbolRefExpr::VK_PAGEOFF ||
        ELFRefKind == AArch64MCExpr::VK_LO12 ||
        ELFRefKind == AArch64MCExpr::VK_GOT_LO12 ||
        ELFRefKind == AArch64MCExpr::VK_DTPREL_LO12 ||
        ELFRefKind == AArch64MCExpr::VK_DTPREL_LO12_NC ||
        ELFRefKind == AArch64MCExpr::VK_TPREL_LO12 ||
        ELFRefKind == AArch64MCExpr::VK_TPREL_LO12_NC ||
        ELFRefKind == AArch64MCExpr::VK_GOTTPREL_LO12_NC ||
        ELFRefKind == AArch64MCExpr::VK_TLSDESC_LO12 ||
        ELFRefKind == AArch64MCExpr::VK_SECREL_LO12 ||
        ELFRefKind == AArch64MCExpr::VK_SECREL_HI12) {
      // Note that we don't range-check the addend. It's adjusted modulo page
      // size when converted, so there is no "out of range" condition when using
      // @pageoff.
      return true;
    } else if (DarwinRefKind == MCSymbolRefExpr::VK_GOTPAGEOFF ||
               DarwinRefKind == MCSymbolRefExpr::VK_TLVPPAGEOFF) {
      // @gotpageoff/@tlvppageoff can only be used directly, not with an addend.
      return Addend == 0;
    }

    return false;
  }

  template <int Scale> bool isUImm12Offset() const {
    if (!isImm())
      return false;

    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return isSymbolicUImm12Offset(getImm());

    int64_t Val = MCE->getValue();
    return (Val % Scale) == 0 && Val >= 0 && (Val / Scale) < 0x1000;
  }

  template <int N, int M>
  bool isImmInRange() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    int64_t Val = MCE->getValue();
    return (Val >= N && Val <= M);
  }

  // NOTE: Also used for isLogicalImmNot as anything that can be represented as
  // a logical immediate can always be represented when inverted.
  template <typename T>
  bool isLogicalImm() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;

    int64_t Val = MCE->getValue();
    int64_t SVal = typename std::make_signed<T>::type(Val);
    int64_t UVal = typename std::make_unsigned<T>::type(Val);
    if (Val != SVal && Val != UVal)
      return false;

    return AArch64_AM::isLogicalImmediate(UVal, sizeof(T) * 8);
  }

  bool isShiftedImm() const { return Kind == k_ShiftedImm; }

  /// Returns the immediate value as a pair of (imm, shift) if the immediate is
  /// a shifted immediate by value 'Shift' or '0', or if it is an unshifted
  /// immediate that can be shifted by 'Shift'.
  template <unsigned Width>
  Optional<std::pair<int64_t, unsigned> > getShiftedVal() const {
    if (isShiftedImm() && Width == getShiftedImmShift())
      if (auto *CE = dyn_cast<MCConstantExpr>(getShiftedImmVal()))
        return std::make_pair(CE->getValue(), Width);

    if (isImm())
      if (auto *CE = dyn_cast<MCConstantExpr>(getImm())) {
        int64_t Val = CE->getValue();
        if ((Val != 0) && (uint64_t(Val >> Width) << Width) == uint64_t(Val))
          return std::make_pair(Val >> Width, Width);
        else
          return std::make_pair(Val, 0u);
      }

    return {};
  }

  bool isAddSubImm() const {
    if (!isShiftedImm() && !isImm())
      return false;

    const MCExpr *Expr;

    // An ADD/SUB shifter is either 'lsl #0' or 'lsl #12'.
    if (isShiftedImm()) {
      unsigned Shift = ShiftedImm.ShiftAmount;
      Expr = ShiftedImm.Val;
      if (Shift != 0 && Shift != 12)
        return false;
    } else {
      Expr = getImm();
    }

    AArch64MCExpr::VariantKind ELFRefKind;
    MCSymbolRefExpr::VariantKind DarwinRefKind;
    int64_t Addend;
    if (AArch64AsmParser::classifySymbolRef(Expr, ELFRefKind,
                                          DarwinRefKind, Addend)) {
      return DarwinRefKind == MCSymbolRefExpr::VK_PAGEOFF
          || DarwinRefKind == MCSymbolRefExpr::VK_TLVPPAGEOFF
          || (DarwinRefKind == MCSymbolRefExpr::VK_GOTPAGEOFF && Addend == 0)
          || ELFRefKind == AArch64MCExpr::VK_LO12
          || ELFRefKind == AArch64MCExpr::VK_DTPREL_HI12
          || ELFRefKind == AArch64MCExpr::VK_DTPREL_LO12
          || ELFRefKind == AArch64MCExpr::VK_DTPREL_LO12_NC
          || ELFRefKind == AArch64MCExpr::VK_TPREL_HI12
          || ELFRefKind == AArch64MCExpr::VK_TPREL_LO12
          || ELFRefKind == AArch64MCExpr::VK_TPREL_LO12_NC
          || ELFRefKind == AArch64MCExpr::VK_TLSDESC_LO12
          || ELFRefKind == AArch64MCExpr::VK_SECREL_HI12
          || ELFRefKind == AArch64MCExpr::VK_SECREL_LO12;
    }

    // If it's a constant, it should be a real immediate in range.
    if (auto ShiftedVal = getShiftedVal<12>())
      return ShiftedVal->first >= 0 && ShiftedVal->first <= 0xfff;

    // If it's an expression, we hope for the best and let the fixup/relocation
    // code deal with it.
    return true;
  }

  bool isAddSubImmNeg() const {
    if (!isShiftedImm() && !isImm())
      return false;

    // Otherwise it should be a real negative immediate in range.
    if (auto ShiftedVal = getShiftedVal<12>())
      return ShiftedVal->first < 0 && -ShiftedVal->first <= 0xfff;

    return false;
  }

  // Signed value in the range -128 to +127. For element widths of
  // 16 bits or higher it may also be a signed multiple of 256 in the
  // range -32768 to +32512.
  // For element-width of 8 bits a range of -128 to 255 is accepted,
  // since a copy of a byte can be either signed/unsigned.
  template <typename T>
  DiagnosticPredicate isSVECpyImm() const {
    if (!isShiftedImm() && (!isImm() || !isa<MCConstantExpr>(getImm())))
      return DiagnosticPredicateTy::NoMatch;

    bool IsByte =
        std::is_same<int8_t, typename std::make_signed<T>::type>::value;
    if (auto ShiftedImm = getShiftedVal<8>())
      if (!(IsByte && ShiftedImm->second) &&
          AArch64_AM::isSVECpyImm<T>(uint64_t(ShiftedImm->first)
                                     << ShiftedImm->second))
        return DiagnosticPredicateTy::Match;

    return DiagnosticPredicateTy::NearMatch;
  }

  // Unsigned value in the range 0 to 255. For element widths of
  // 16 bits or higher it may also be a signed multiple of 256 in the
  // range 0 to 65280.
  template <typename T> DiagnosticPredicate isSVEAddSubImm() const {
    if (!isShiftedImm() && (!isImm() || !isa<MCConstantExpr>(getImm())))
      return DiagnosticPredicateTy::NoMatch;

    bool IsByte =
        std::is_same<int8_t, typename std::make_signed<T>::type>::value;
    if (auto ShiftedImm = getShiftedVal<8>())
      if (!(IsByte && ShiftedImm->second) &&
          AArch64_AM::isSVEAddSubImm<T>(ShiftedImm->first
                                        << ShiftedImm->second))
        return DiagnosticPredicateTy::Match;

    return DiagnosticPredicateTy::NearMatch;
  }

  template <typename T> DiagnosticPredicate isSVEPreferredLogicalImm() const {
    if (isLogicalImm<T>() && !isSVECpyImm<T>())
      return DiagnosticPredicateTy::Match;
    return DiagnosticPredicateTy::NoMatch;
  }

  bool isCondCode() const { return Kind == k_CondCode; }

  bool isSIMDImmType10() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return false;
    return AArch64_AM::isAdvSIMDModImmType10(MCE->getValue());
  }

  template<int N>
  bool isBranchTarget() const {
    if (!isImm())
      return false;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      return true;
    int64_t Val = MCE->getValue();
    if (Val & 0x3)
      return false;
    assert(N > 0 && "Branch target immediate cannot be 0 bits!");
    return (Val >= -((1<<(N-1)) << 2) && Val <= (((1<<(N-1))-1) << 2));
  }

  bool
  isMovWSymbol(ArrayRef<AArch64MCExpr::VariantKind> AllowedModifiers) const {
    if (!isImm())
      return false;

    AArch64MCExpr::VariantKind ELFRefKind;
    MCSymbolRefExpr::VariantKind DarwinRefKind;
    int64_t Addend;
    if (!AArch64AsmParser::classifySymbolRef(getImm(), ELFRefKind,
                                             DarwinRefKind, Addend)) {
      return false;
    }
    if (DarwinRefKind != MCSymbolRefExpr::VK_None)
      return false;

    for (unsigned i = 0; i != AllowedModifiers.size(); ++i) {
      if (ELFRefKind == AllowedModifiers[i])
        return true;
    }

    return false;
  }

  bool isMovZSymbolG3() const {
    return isMovWSymbol(AArch64MCExpr::VK_ABS_G3);
  }

  bool isMovZSymbolG2() const {
    return isMovWSymbol({AArch64MCExpr::VK_ABS_G2, AArch64MCExpr::VK_ABS_G2_S,
                         AArch64MCExpr::VK_TPREL_G2,
                         AArch64MCExpr::VK_DTPREL_G2});
  }

  bool isMovZSymbolG1() const {
    return isMovWSymbol({
        AArch64MCExpr::VK_ABS_G1, AArch64MCExpr::VK_ABS_G1_S,
        AArch64MCExpr::VK_GOTTPREL_G1, AArch64MCExpr::VK_TPREL_G1,
        AArch64MCExpr::VK_DTPREL_G1,
    });
  }

  bool isMovZSymbolG0() const {
    return isMovWSymbol({AArch64MCExpr::VK_ABS_G0, AArch64MCExpr::VK_ABS_G0_S,
                         AArch64MCExpr::VK_TPREL_G0,
                         AArch64MCExpr::VK_DTPREL_G0});
  }

  bool isMovKSymbolG3() const {
    return isMovWSymbol(AArch64MCExpr::VK_ABS_G3);
  }

  bool isMovKSymbolG2() const {
    return isMovWSymbol(AArch64MCExpr::VK_ABS_G2_NC);
  }

  bool isMovKSymbolG1() const {
    return isMovWSymbol({AArch64MCExpr::VK_ABS_G1_NC,
                         AArch64MCExpr::VK_TPREL_G1_NC,
                         AArch64MCExpr::VK_DTPREL_G1_NC});
  }

  bool isMovKSymbolG0() const {
    return isMovWSymbol(
        {AArch64MCExpr::VK_ABS_G0_NC, AArch64MCExpr::VK_GOTTPREL_G0_NC,
         AArch64MCExpr::VK_TPREL_G0_NC, AArch64MCExpr::VK_DTPREL_G0_NC});
  }

  template<int RegWidth, int Shift>
  bool isMOVZMovAlias() const {
    if (!isImm()) return false;

    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    uint64_t Value = CE->getValue();

    return AArch64_AM::isMOVZMovAlias(Value, Shift, RegWidth);
  }

  template<int RegWidth, int Shift>
  bool isMOVNMovAlias() const {
    if (!isImm()) return false;

    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return false;
    uint64_t Value = CE->getValue();

    return AArch64_AM::isMOVNMovAlias(Value, Shift, RegWidth);
  }

  bool isFPImm() const {
    return Kind == k_FPImm &&
           AArch64_AM::getFP64Imm(getFPImm().bitcastToAPInt()) != -1;
  }

  bool isBarrier() const { return Kind == k_Barrier; }
  bool isSysReg() const { return Kind == k_SysReg; }

  bool isMRSSystemRegister() const {
    if (!isSysReg()) return false;

    return SysReg.MRSReg != -1U;
  }

  bool isMSRSystemRegister() const {
    if (!isSysReg()) return false;
    return SysReg.MSRReg != -1U;
  }

  bool isSystemPStateFieldWithImm0_1() const {
    if (!isSysReg()) return false;
    return (SysReg.PStateField == AArch64PState::PAN ||
            SysReg.PStateField == AArch64PState::DIT ||
            SysReg.PStateField == AArch64PState::UAO ||
            SysReg.PStateField == AArch64PState::SSBS);
  }

  bool isSystemPStateFieldWithImm0_15() const {
    if (!isSysReg() || isSystemPStateFieldWithImm0_1()) return false;
    return SysReg.PStateField != -1U;
  }

  bool isReg() const override {
    return Kind == k_Register;
  }

  bool isScalarReg() const {
    return Kind == k_Register && Reg.Kind == RegKind::Scalar;
  }

  bool isNeonVectorReg() const {
    return Kind == k_Register && Reg.Kind == RegKind::NeonVector;
  }

  bool isNeonVectorRegLo() const {
    return Kind == k_Register && Reg.Kind == RegKind::NeonVector &&
           AArch64MCRegisterClasses[AArch64::FPR128_loRegClassID].contains(
               Reg.RegNum);
  }

  template <unsigned Class> bool isSVEVectorReg() const {
    RegKind RK;
    switch (Class) {
    case AArch64::ZPRRegClassID:
    case AArch64::ZPR_3bRegClassID:
    case AArch64::ZPR_4bRegClassID:
      RK = RegKind::SVEDataVector;
      break;
    case AArch64::PPRRegClassID:
    case AArch64::PPR_3bRegClassID:
      RK = RegKind::SVEPredicateVector;
      break;
    default:
      llvm_unreachable("Unsupport register class");
    }

    return (Kind == k_Register && Reg.Kind == RK) &&
           AArch64MCRegisterClasses[Class].contains(getReg());
  }

  template <unsigned Class> bool isFPRasZPR() const {
    return Kind == k_Register && Reg.Kind == RegKind::Scalar &&
           AArch64MCRegisterClasses[Class].contains(getReg());
  }

  template <int ElementWidth, unsigned Class>
  DiagnosticPredicate isSVEPredicateVectorRegOfWidth() const {
    if (Kind != k_Register || Reg.Kind != RegKind::SVEPredicateVector)
      return DiagnosticPredicateTy::NoMatch;

    if (isSVEVectorReg<Class>() &&
           (ElementWidth == 0 || Reg.ElementWidth == ElementWidth))
      return DiagnosticPredicateTy::Match;

    return DiagnosticPredicateTy::NearMatch;
  }

  template <int ElementWidth, unsigned Class>
  DiagnosticPredicate isSVEDataVectorRegOfWidth() const {
    if (Kind != k_Register || Reg.Kind != RegKind::SVEDataVector)
      return DiagnosticPredicateTy::NoMatch;

    if (isSVEVectorReg<Class>() &&
           (ElementWidth == 0 || Reg.ElementWidth == ElementWidth))
      return DiagnosticPredicateTy::Match;

    return DiagnosticPredicateTy::NearMatch;
  }

  template <int ElementWidth, unsigned Class,
            AArch64_AM::ShiftExtendType ShiftExtendTy, int ShiftWidth,
            bool ShiftWidthAlwaysSame>
  DiagnosticPredicate isSVEDataVectorRegWithShiftExtend() const {
    auto VectorMatch = isSVEDataVectorRegOfWidth<ElementWidth, Class>();
    if (!VectorMatch.isMatch())
      return DiagnosticPredicateTy::NoMatch;

    // Give a more specific diagnostic when the user has explicitly typed in
    // a shift-amount that does not match what is expected, but for which
    // there is also an unscaled addressing mode (e.g. sxtw/uxtw).
    bool MatchShift = getShiftExtendAmount() == Log2_32(ShiftWidth / 8);
    if (!MatchShift && (ShiftExtendTy == AArch64_AM::UXTW ||
                        ShiftExtendTy == AArch64_AM::SXTW) &&
        !ShiftWidthAlwaysSame && hasShiftExtendAmount() && ShiftWidth == 8)
      return DiagnosticPredicateTy::NoMatch;

    if (MatchShift && ShiftExtendTy == getShiftExtendType())
      return DiagnosticPredicateTy::Match;

    return DiagnosticPredicateTy::NearMatch;
  }

  bool isGPR32as64() const {
    return Kind == k_Register && Reg.Kind == RegKind::Scalar &&
      AArch64MCRegisterClasses[AArch64::GPR64RegClassID].contains(Reg.RegNum);
  }

  bool isGPR64as32() const {
    return Kind == k_Register && Reg.Kind == RegKind::Scalar &&
      AArch64MCRegisterClasses[AArch64::GPR32RegClassID].contains(Reg.RegNum);
  }

  bool isWSeqPair() const {
    return Kind == k_Register && Reg.Kind == RegKind::Scalar &&
           AArch64MCRegisterClasses[AArch64::WSeqPairsClassRegClassID].contains(
               Reg.RegNum);
  }

  bool isXSeqPair() const {
    return Kind == k_Register && Reg.Kind == RegKind::Scalar &&
           AArch64MCRegisterClasses[AArch64::XSeqPairsClassRegClassID].contains(
               Reg.RegNum);
  }

  template<int64_t Angle, int64_t Remainder>
  DiagnosticPredicate isComplexRotation() const {
    if (!isImm()) return DiagnosticPredicateTy::NoMatch;

    const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(getImm());
    if (!CE) return DiagnosticPredicateTy::NoMatch;
    uint64_t Value = CE->getValue();

    if (Value % Angle == Remainder && Value <= 270)
      return DiagnosticPredicateTy::Match;
    return DiagnosticPredicateTy::NearMatch;
  }

  template <unsigned RegClassID> bool isGPR64() const {
    return Kind == k_Register && Reg.Kind == RegKind::Scalar &&
           AArch64MCRegisterClasses[RegClassID].contains(getReg());
  }

  template <unsigned RegClassID, int ExtWidth>
  DiagnosticPredicate isGPR64WithShiftExtend() const {
    if (Kind != k_Register || Reg.Kind != RegKind::Scalar)
      return DiagnosticPredicateTy::NoMatch;

    if (isGPR64<RegClassID>() && getShiftExtendType() == AArch64_AM::LSL &&
        getShiftExtendAmount() == Log2_32(ExtWidth / 8))
      return DiagnosticPredicateTy::Match;
    return DiagnosticPredicateTy::NearMatch;
  }

  /// Is this a vector list with the type implicit (presumably attached to the
  /// instruction itself)?
  template <RegKind VectorKind, unsigned NumRegs>
  bool isImplicitlyTypedVectorList() const {
    return Kind == k_VectorList && VectorList.Count == NumRegs &&
           VectorList.NumElements == 0 &&
           VectorList.RegisterKind == VectorKind;
  }

  template <RegKind VectorKind, unsigned NumRegs, unsigned NumElements,
            unsigned ElementWidth>
  bool isTypedVectorList() const {
    if (Kind != k_VectorList)
      return false;
    if (VectorList.Count != NumRegs)
      return false;
    if (VectorList.RegisterKind != VectorKind)
      return false;
    if (VectorList.ElementWidth != ElementWidth)
      return false;
    return VectorList.NumElements == NumElements;
  }

  template <int Min, int Max>
  DiagnosticPredicate isVectorIndex() const {
    if (Kind != k_VectorIndex)
      return DiagnosticPredicateTy::NoMatch;
    if (VectorIndex.Val >= Min && VectorIndex.Val <= Max)
      return DiagnosticPredicateTy::Match;
    return DiagnosticPredicateTy::NearMatch;
  }

  bool isToken() const override { return Kind == k_Token; }

  bool isTokenEqual(StringRef Str) const {
    return Kind == k_Token && getToken() == Str;
  }
  bool isSysCR() const { return Kind == k_SysCR; }
  bool isPrefetch() const { return Kind == k_Prefetch; }
  bool isPSBHint() const { return Kind == k_PSBHint; }
  bool isBTIHint() const { return Kind == k_BTIHint; }
  bool isShiftExtend() const { return Kind == k_ShiftExtend; }
  bool isShifter() const {
    if (!isShiftExtend())
      return false;

    AArch64_AM::ShiftExtendType ST = getShiftExtendType();
    return (ST == AArch64_AM::LSL || ST == AArch64_AM::LSR ||
            ST == AArch64_AM::ASR || ST == AArch64_AM::ROR ||
            ST == AArch64_AM::MSL);
  }

  template <unsigned ImmEnum> DiagnosticPredicate isExactFPImm() const {
    if (Kind != k_FPImm)
      return DiagnosticPredicateTy::NoMatch;

    if (getFPImmIsExact()) {
      // Lookup the immediate from table of supported immediates.
      auto *Desc = AArch64ExactFPImm::lookupExactFPImmByEnum(ImmEnum);
      assert(Desc && "Unknown enum value");

      // Calculate its FP value.
      APFloat RealVal(APFloat::IEEEdouble());
      if (RealVal.convertFromString(Desc->Repr, APFloat::rmTowardZero) !=
          APFloat::opOK)
        llvm_unreachable("FP immediate is not exact");

      if (getFPImm().bitwiseIsEqual(RealVal))
        return DiagnosticPredicateTy::Match;
    }

    return DiagnosticPredicateTy::NearMatch;
  }

  template <unsigned ImmA, unsigned ImmB>
  DiagnosticPredicate isExactFPImm() const {
    DiagnosticPredicate Res = DiagnosticPredicateTy::NoMatch;
    if ((Res = isExactFPImm<ImmA>()))
      return DiagnosticPredicateTy::Match;
    if ((Res = isExactFPImm<ImmB>()))
      return DiagnosticPredicateTy::Match;
    return Res;
  }

  bool isExtend() const {
    if (!isShiftExtend())
      return false;

    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    return (ET == AArch64_AM::UXTB || ET == AArch64_AM::SXTB ||
            ET == AArch64_AM::UXTH || ET == AArch64_AM::SXTH ||
            ET == AArch64_AM::UXTW || ET == AArch64_AM::SXTW ||
            ET == AArch64_AM::UXTX || ET == AArch64_AM::SXTX ||
            ET == AArch64_AM::LSL) &&
           getShiftExtendAmount() <= 4;
  }

  bool isExtend64() const {
    if (!isExtend())
      return false;
    // UXTX and SXTX require a 64-bit source register (the ExtendLSL64 class).
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    return ET != AArch64_AM::UXTX && ET != AArch64_AM::SXTX;
  }

  bool isExtendLSL64() const {
    if (!isExtend())
      return false;
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    return (ET == AArch64_AM::UXTX || ET == AArch64_AM::SXTX ||
            ET == AArch64_AM::LSL) &&
           getShiftExtendAmount() <= 4;
  }

  template<int Width> bool isMemXExtend() const {
    if (!isExtend())
      return false;
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    return (ET == AArch64_AM::LSL || ET == AArch64_AM::SXTX) &&
           (getShiftExtendAmount() == Log2_32(Width / 8) ||
            getShiftExtendAmount() == 0);
  }

  template<int Width> bool isMemWExtend() const {
    if (!isExtend())
      return false;
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    return (ET == AArch64_AM::UXTW || ET == AArch64_AM::SXTW) &&
           (getShiftExtendAmount() == Log2_32(Width / 8) ||
            getShiftExtendAmount() == 0);
  }

  template <unsigned width>
  bool isArithmeticShifter() const {
    if (!isShifter())
      return false;

    // An arithmetic shifter is LSL, LSR, or ASR.
    AArch64_AM::ShiftExtendType ST = getShiftExtendType();
    return (ST == AArch64_AM::LSL || ST == AArch64_AM::LSR ||
            ST == AArch64_AM::ASR) && getShiftExtendAmount() < width;
  }

  template <unsigned width>
  bool isLogicalShifter() const {
    if (!isShifter())
      return false;

    // A logical shifter is LSL, LSR, ASR or ROR.
    AArch64_AM::ShiftExtendType ST = getShiftExtendType();
    return (ST == AArch64_AM::LSL || ST == AArch64_AM::LSR ||
            ST == AArch64_AM::ASR || ST == AArch64_AM::ROR) &&
           getShiftExtendAmount() < width;
  }

  bool isMovImm32Shifter() const {
    if (!isShifter())
      return false;

    // A MOVi shifter is LSL of 0, 16, 32, or 48.
    AArch64_AM::ShiftExtendType ST = getShiftExtendType();
    if (ST != AArch64_AM::LSL)
      return false;
    uint64_t Val = getShiftExtendAmount();
    return (Val == 0 || Val == 16);
  }

  bool isMovImm64Shifter() const {
    if (!isShifter())
      return false;

    // A MOVi shifter is LSL of 0 or 16.
    AArch64_AM::ShiftExtendType ST = getShiftExtendType();
    if (ST != AArch64_AM::LSL)
      return false;
    uint64_t Val = getShiftExtendAmount();
    return (Val == 0 || Val == 16 || Val == 32 || Val == 48);
  }

  bool isLogicalVecShifter() const {
    if (!isShifter())
      return false;

    // A logical vector shifter is a left shift by 0, 8, 16, or 24.
    unsigned Shift = getShiftExtendAmount();
    return getShiftExtendType() == AArch64_AM::LSL &&
           (Shift == 0 || Shift == 8 || Shift == 16 || Shift == 24);
  }

  bool isLogicalVecHalfWordShifter() const {
    if (!isLogicalVecShifter())
      return false;

    // A logical vector shifter is a left shift by 0 or 8.
    unsigned Shift = getShiftExtendAmount();
    return getShiftExtendType() == AArch64_AM::LSL &&
           (Shift == 0 || Shift == 8);
  }

  bool isMoveVecShifter() const {
    if (!isShiftExtend())
      return false;

    // A logical vector shifter is a left shift by 8 or 16.
    unsigned Shift = getShiftExtendAmount();
    return getShiftExtendType() == AArch64_AM::MSL &&
           (Shift == 8 || Shift == 16);
  }

  // Fallback unscaled operands are for aliases of LDR/STR that fall back
  // to LDUR/STUR when the offset is not legal for the former but is for
  // the latter. As such, in addition to checking for being a legal unscaled
  // address, also check that it is not a legal scaled address. This avoids
  // ambiguity in the matcher.
  template<int Width>
  bool isSImm9OffsetFB() const {
    return isSImm<9>() && !isUImm12Offset<Width / 8>();
  }

  bool isAdrpLabel() const {
    // Validation was handled during parsing, so we just sanity check that
    // something didn't go haywire.
    if (!isImm())
        return false;

    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Imm.Val)) {
      int64_t Val = CE->getValue();
      int64_t Min = - (4096 * (1LL << (21 - 1)));
      int64_t Max = 4096 * ((1LL << (21 - 1)) - 1);
      return (Val % 4096) == 0 && Val >= Min && Val <= Max;
    }

    return true;
  }

  bool isAdrLabel() const {
    // Validation was handled during parsing, so we just sanity check that
    // something didn't go haywire.
    if (!isImm())
        return false;

    if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Imm.Val)) {
      int64_t Val = CE->getValue();
      int64_t Min = - (1LL << (21 - 1));
      int64_t Max = ((1LL << (21 - 1)) - 1);
      return Val >= Min && Val <= Max;
    }

    return true;
  }

  void addExpr(MCInst &Inst, const MCExpr *Expr) const {
    // Add as immediates when possible.  Null MCExpr = 0.
    if (!Expr)
      Inst.addOperand(MCOperand::createImm(0));
    else if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }

  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addGPR32as64Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    assert(
        AArch64MCRegisterClasses[AArch64::GPR64RegClassID].contains(getReg()));

    const MCRegisterInfo *RI = Ctx.getRegisterInfo();
    uint32_t Reg = RI->getRegClass(AArch64::GPR32RegClassID).getRegister(
        RI->getEncodingValue(getReg()));

    Inst.addOperand(MCOperand::createReg(Reg));
  }

  void addGPR64as32Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    assert(
        AArch64MCRegisterClasses[AArch64::GPR32RegClassID].contains(getReg()));

    const MCRegisterInfo *RI = Ctx.getRegisterInfo();
    uint32_t Reg = RI->getRegClass(AArch64::GPR64RegClassID).getRegister(
        RI->getEncodingValue(getReg()));

    Inst.addOperand(MCOperand::createReg(Reg));
  }

  template <int Width>
  void addFPRasZPRRegOperands(MCInst &Inst, unsigned N) const {
    unsigned Base;
    switch (Width) {
    case 8:   Base = AArch64::B0; break;
    case 16:  Base = AArch64::H0; break;
    case 32:  Base = AArch64::S0; break;
    case 64:  Base = AArch64::D0; break;
    case 128: Base = AArch64::Q0; break;
    default:
      llvm_unreachable("Unsupported width");
    }
    Inst.addOperand(MCOperand::createReg(AArch64::Z0 + getReg() - Base));
  }

  void addVectorReg64Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    assert(
        AArch64MCRegisterClasses[AArch64::FPR128RegClassID].contains(getReg()));
    Inst.addOperand(MCOperand::createReg(AArch64::D0 + getReg() - AArch64::Q0));
  }

  void addVectorReg128Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    assert(
        AArch64MCRegisterClasses[AArch64::FPR128RegClassID].contains(getReg()));
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addVectorRegLoOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  enum VecListIndexType {
    VecListIdx_DReg = 0,
    VecListIdx_QReg = 1,
    VecListIdx_ZReg = 2,
  };

  template <VecListIndexType RegTy, unsigned NumRegs>
  void addVectorListOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    static const unsigned FirstRegs[][5] = {
      /* DReg */ { AArch64::Q0,
                   AArch64::D0,       AArch64::D0_D1,
                   AArch64::D0_D1_D2, AArch64::D0_D1_D2_D3 },
      /* QReg */ { AArch64::Q0,
                   AArch64::Q0,       AArch64::Q0_Q1,
                   AArch64::Q0_Q1_Q2, AArch64::Q0_Q1_Q2_Q3 },
      /* ZReg */ { AArch64::Z0,
                   AArch64::Z0,       AArch64::Z0_Z1,
                   AArch64::Z0_Z1_Z2, AArch64::Z0_Z1_Z2_Z3 }
    };

    assert((RegTy != VecListIdx_ZReg || NumRegs <= 4) &&
           " NumRegs must be <= 4 for ZRegs");

    unsigned FirstReg = FirstRegs[(unsigned)RegTy][NumRegs];
    Inst.addOperand(MCOperand::createReg(FirstReg + getVectorListStart() -
                                         FirstRegs[(unsigned)RegTy][0]));
  }

  void addVectorIndexOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getVectorIndex()));
  }

  template <unsigned ImmIs0, unsigned ImmIs1>
  void addExactFPImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    assert(bool(isExactFPImm<ImmIs0, ImmIs1>()) && "Invalid operand");
    Inst.addOperand(MCOperand::createImm(bool(isExactFPImm<ImmIs1>())));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    // If this is a pageoff symrefexpr with an addend, adjust the addend
    // to be only the page-offset portion. Otherwise, just add the expr
    // as-is.
    addExpr(Inst, getImm());
  }

  template <int Shift>
  void addImmWithOptionalShiftOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    if (auto ShiftedVal = getShiftedVal<Shift>()) {
      Inst.addOperand(MCOperand::createImm(ShiftedVal->first));
      Inst.addOperand(MCOperand::createImm(ShiftedVal->second));
    } else if (isShiftedImm()) {
      addExpr(Inst, getShiftedImmVal());
      Inst.addOperand(MCOperand::createImm(getShiftedImmShift()));
    } else {
      addExpr(Inst, getImm());
      Inst.addOperand(MCOperand::createImm(0));
    }
  }

  template <int Shift>
  void addImmNegWithOptionalShiftOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    if (auto ShiftedVal = getShiftedVal<Shift>()) {
      Inst.addOperand(MCOperand::createImm(-ShiftedVal->first));
      Inst.addOperand(MCOperand::createImm(ShiftedVal->second));
    } else
      llvm_unreachable("Not a shifted negative immediate");
  }

  void addCondCodeOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getCondCode()));
  }

  void addAdrpLabelOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE)
      addExpr(Inst, getImm());
    else
      Inst.addOperand(MCOperand::createImm(MCE->getValue() >> 12));
  }

  void addAdrLabelOperands(MCInst &Inst, unsigned N) const {
    addImmOperands(Inst, N);
  }

  template<int Scale>
  void addUImm12OffsetOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());

    if (!MCE) {
      Inst.addOperand(MCOperand::createExpr(getImm()));
      return;
    }
    Inst.addOperand(MCOperand::createImm(MCE->getValue() / Scale));
  }

  void addUImm6Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue()));
  }

  template <int Scale>
  void addImmScaledOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue() / Scale));
  }

  template <typename T>
  void addLogicalImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    typename std::make_unsigned<T>::type Val = MCE->getValue();
    uint64_t encoding = AArch64_AM::encodeLogicalImmediate(Val, sizeof(T) * 8);
    Inst.addOperand(MCOperand::createImm(encoding));
  }

  template <typename T>
  void addLogicalImmNotOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    typename std::make_unsigned<T>::type Val = ~MCE->getValue();
    uint64_t encoding = AArch64_AM::encodeLogicalImmediate(Val, sizeof(T) * 8);
    Inst.addOperand(MCOperand::createImm(encoding));
  }

  void addSIMDImmType10Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    uint64_t encoding = AArch64_AM::encodeAdvSIMDModImmType10(MCE->getValue());
    Inst.addOperand(MCOperand::createImm(encoding));
  }

  void addBranchTarget26Operands(MCInst &Inst, unsigned N) const {
    // Branch operands don't encode the low bits, so shift them off
    // here. If it's a label, however, just put it on directly as there's
    // not enough information now to do anything.
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE) {
      addExpr(Inst, getImm());
      return;
    }
    assert(MCE && "Invalid constant immediate operand!");
    Inst.addOperand(MCOperand::createImm(MCE->getValue() >> 2));
  }

  void addPCRelLabel19Operands(MCInst &Inst, unsigned N) const {
    // Branch operands don't encode the low bits, so shift them off
    // here. If it's a label, however, just put it on directly as there's
    // not enough information now to do anything.
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE) {
      addExpr(Inst, getImm());
      return;
    }
    assert(MCE && "Invalid constant immediate operand!");
    Inst.addOperand(MCOperand::createImm(MCE->getValue() >> 2));
  }

  void addBranchTarget14Operands(MCInst &Inst, unsigned N) const {
    // Branch operands don't encode the low bits, so shift them off
    // here. If it's a label, however, just put it on directly as there's
    // not enough information now to do anything.
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(getImm());
    if (!MCE) {
      addExpr(Inst, getImm());
      return;
    }
    assert(MCE && "Invalid constant immediate operand!");
    Inst.addOperand(MCOperand::createImm(MCE->getValue() >> 2));
  }

  void addFPImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(
        AArch64_AM::getFP64Imm(getFPImm().bitcastToAPInt())));
  }

  void addBarrierOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getBarrier()));
  }

  void addMRSSystemRegisterOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::createImm(SysReg.MRSReg));
  }

  void addMSRSystemRegisterOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::createImm(SysReg.MSRReg));
  }

  void addSystemPStateFieldWithImm0_1Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::createImm(SysReg.PStateField));
  }

  void addSystemPStateFieldWithImm0_15Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");

    Inst.addOperand(MCOperand::createImm(SysReg.PStateField));
  }

  void addSysCROperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getSysCR()));
  }

  void addPrefetchOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getPrefetch()));
  }

  void addPSBHintOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getPSBHint()));
  }

  void addBTIHintOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createImm(getBTIHint()));
  }

  void addShifterOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    unsigned Imm =
        AArch64_AM::getShifterImm(getShiftExtendType(), getShiftExtendAmount());
    Inst.addOperand(MCOperand::createImm(Imm));
  }

  void addExtendOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    if (ET == AArch64_AM::LSL) ET = AArch64_AM::UXTW;
    unsigned Imm = AArch64_AM::getArithExtendImm(ET, getShiftExtendAmount());
    Inst.addOperand(MCOperand::createImm(Imm));
  }

  void addExtend64Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    if (ET == AArch64_AM::LSL) ET = AArch64_AM::UXTX;
    unsigned Imm = AArch64_AM::getArithExtendImm(ET, getShiftExtendAmount());
    Inst.addOperand(MCOperand::createImm(Imm));
  }

  void addMemExtendOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    bool IsSigned = ET == AArch64_AM::SXTW || ET == AArch64_AM::SXTX;
    Inst.addOperand(MCOperand::createImm(IsSigned));
    Inst.addOperand(MCOperand::createImm(getShiftExtendAmount() != 0));
  }

  // For 8-bit load/store instructions with a register offset, both the
  // "DoShift" and "NoShift" variants have a shift of 0. Because of this,
  // they're disambiguated by whether the shift was explicit or implicit rather
  // than its size.
  void addMemExtend8Operands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    AArch64_AM::ShiftExtendType ET = getShiftExtendType();
    bool IsSigned = ET == AArch64_AM::SXTW || ET == AArch64_AM::SXTX;
    Inst.addOperand(MCOperand::createImm(IsSigned));
    Inst.addOperand(MCOperand::createImm(hasShiftExtendAmount()));
  }

  template<int Shift>
  void addMOVZMovAliasOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");

    const MCConstantExpr *CE = cast<MCConstantExpr>(getImm());
    uint64_t Value = CE->getValue();
    Inst.addOperand(MCOperand::createImm((Value >> Shift) & 0xffff));
  }

  template<int Shift>
  void addMOVNMovAliasOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");

    const MCConstantExpr *CE = cast<MCConstantExpr>(getImm());
    uint64_t Value = CE->getValue();
    Inst.addOperand(MCOperand::createImm((~Value >> Shift) & 0xffff));
  }

  void addComplexRotationEvenOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm(MCE->getValue() / 90));
  }

  void addComplexRotationOddOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    const MCConstantExpr *MCE = cast<MCConstantExpr>(getImm());
    Inst.addOperand(MCOperand::createImm((MCE->getValue() - 90) / 180));
  }

  void print(raw_ostream &OS) const override;

  static std::unique_ptr<AArch64Operand>
  CreateToken(StringRef Str, bool IsSuffix, SMLoc S, MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_Token, Ctx);
    Op->Tok.Data = Str.data();
    Op->Tok.Length = Str.size();
    Op->Tok.IsSuffix = IsSuffix;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateReg(unsigned RegNum, RegKind Kind, SMLoc S, SMLoc E, MCContext &Ctx,
            RegConstraintEqualityTy EqTy = RegConstraintEqualityTy::EqualsReg,
            AArch64_AM::ShiftExtendType ExtTy = AArch64_AM::LSL,
            unsigned ShiftAmount = 0,
            unsigned HasExplicitAmount = false) {
    auto Op = make_unique<AArch64Operand>(k_Register, Ctx);
    Op->Reg.RegNum = RegNum;
    Op->Reg.Kind = Kind;
    Op->Reg.ElementWidth = 0;
    Op->Reg.EqualityTy = EqTy;
    Op->Reg.ShiftExtend.Type = ExtTy;
    Op->Reg.ShiftExtend.Amount = ShiftAmount;
    Op->Reg.ShiftExtend.HasExplicitAmount = HasExplicitAmount;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateVectorReg(unsigned RegNum, RegKind Kind, unsigned ElementWidth,
                  SMLoc S, SMLoc E, MCContext &Ctx,
                  AArch64_AM::ShiftExtendType ExtTy = AArch64_AM::LSL,
                  unsigned ShiftAmount = 0,
                  unsigned HasExplicitAmount = false) {
    assert((Kind == RegKind::NeonVector || Kind == RegKind::SVEDataVector ||
            Kind == RegKind::SVEPredicateVector) &&
           "Invalid vector kind");
    auto Op = CreateReg(RegNum, Kind, S, E, Ctx, EqualsReg, ExtTy, ShiftAmount,
                        HasExplicitAmount);
    Op->Reg.ElementWidth = ElementWidth;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateVectorList(unsigned RegNum, unsigned Count, unsigned NumElements,
                   unsigned ElementWidth, RegKind RegisterKind, SMLoc S, SMLoc E,
                   MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_VectorList, Ctx);
    Op->VectorList.RegNum = RegNum;
    Op->VectorList.Count = Count;
    Op->VectorList.NumElements = NumElements;
    Op->VectorList.ElementWidth = ElementWidth;
    Op->VectorList.RegisterKind = RegisterKind;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateVectorIndex(unsigned Idx, SMLoc S, SMLoc E, MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_VectorIndex, Ctx);
    Op->VectorIndex.Val = Idx;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreateImm(const MCExpr *Val, SMLoc S,
                                                   SMLoc E, MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_Immediate, Ctx);
    Op->Imm.Val = Val;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreateShiftedImm(const MCExpr *Val,
                                                          unsigned ShiftAmount,
                                                          SMLoc S, SMLoc E,
                                                          MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_ShiftedImm, Ctx);
    Op->ShiftedImm .Val = Val;
    Op->ShiftedImm.ShiftAmount = ShiftAmount;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateCondCode(AArch64CC::CondCode Code, SMLoc S, SMLoc E, MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_CondCode, Ctx);
    Op->CondCode.Code = Code;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateFPImm(APFloat Val, bool IsExact, SMLoc S, MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_FPImm, Ctx);
    Op->FPImm.Val = Val.bitcastToAPInt().getSExtValue();
    Op->FPImm.IsExact = IsExact;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreateBarrier(unsigned Val,
                                                       StringRef Str,
                                                       SMLoc S,
                                                       MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_Barrier, Ctx);
    Op->Barrier.Val = Val;
    Op->Barrier.Data = Str.data();
    Op->Barrier.Length = Str.size();
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreateSysReg(StringRef Str, SMLoc S,
                                                      uint32_t MRSReg,
                                                      uint32_t MSRReg,
                                                      uint32_t PStateField,
                                                      MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_SysReg, Ctx);
    Op->SysReg.Data = Str.data();
    Op->SysReg.Length = Str.size();
    Op->SysReg.MRSReg = MRSReg;
    Op->SysReg.MSRReg = MSRReg;
    Op->SysReg.PStateField = PStateField;
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreateSysCR(unsigned Val, SMLoc S,
                                                     SMLoc E, MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_SysCR, Ctx);
    Op->SysCRImm.Val = Val;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreatePrefetch(unsigned Val,
                                                        StringRef Str,
                                                        SMLoc S,
                                                        MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_Prefetch, Ctx);
    Op->Prefetch.Val = Val;
    Op->Barrier.Data = Str.data();
    Op->Barrier.Length = Str.size();
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreatePSBHint(unsigned Val,
                                                       StringRef Str,
                                                       SMLoc S,
                                                       MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_PSBHint, Ctx);
    Op->PSBHint.Val = Val;
    Op->PSBHint.Data = Str.data();
    Op->PSBHint.Length = Str.size();
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<AArch64Operand> CreateBTIHint(unsigned Val,
                                                       StringRef Str,
                                                       SMLoc S,
                                                       MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_BTIHint, Ctx);
    Op->BTIHint.Val = Val << 1 | 32;
    Op->BTIHint.Data = Str.data();
    Op->BTIHint.Length = Str.size();
    Op->StartLoc = S;
    Op->EndLoc = S;
    return Op;
  }

  static std::unique_ptr<AArch64Operand>
  CreateShiftExtend(AArch64_AM::ShiftExtendType ShOp, unsigned Val,
                    bool HasExplicitAmount, SMLoc S, SMLoc E, MCContext &Ctx) {
    auto Op = make_unique<AArch64Operand>(k_ShiftExtend, Ctx);
    Op->ShiftExtend.Type = ShOp;
    Op->ShiftExtend.Amount = Val;
    Op->ShiftExtend.HasExplicitAmount = HasExplicitAmount;
    Op->StartLoc = S;
    Op->EndLoc = E;
    return Op;
  }
};

} // end anonymous namespace.

void AArch64Operand::print(raw_ostream &OS) const {
  switch (Kind) {
  case k_FPImm:
    OS << "<fpimm " << getFPImm().bitcastToAPInt().getZExtValue();
    if (!getFPImmIsExact())
      OS << " (inexact)";
    OS << ">";
    break;
  case k_Barrier: {
    StringRef Name = getBarrierName();
    if (!Name.empty())
      OS << "<barrier " << Name << ">";
    else
      OS << "<barrier invalid #" << getBarrier() << ">";
    break;
  }
  case k_Immediate:
    OS << *getImm();
    break;
  case k_ShiftedImm: {
    unsigned Shift = getShiftedImmShift();
    OS << "<shiftedimm ";
    OS << *getShiftedImmVal();
    OS << ", lsl #" << AArch64_AM::getShiftValue(Shift) << ">";
    break;
  }
  case k_CondCode:
    OS << "<condcode " << getCondCode() << ">";
    break;
  case k_VectorList: {
    OS << "<vectorlist ";
    unsigned Reg = getVectorListStart();
    for (unsigned i = 0, e = getVectorListCount(); i != e; ++i)
      OS << Reg + i << " ";
    OS << ">";
    break;
  }
  case k_VectorIndex:
    OS << "<vectorindex " << getVectorIndex() << ">";
    break;
  case k_SysReg:
    OS << "<sysreg: " << getSysReg() << '>';
    break;
  case k_Token:
    OS << "'" << getToken() << "'";
    break;
  case k_SysCR:
    OS << "c" << getSysCR();
    break;
  case k_Prefetch: {
    StringRef Name = getPrefetchName();
    if (!Name.empty())
      OS << "<prfop " << Name << ">";
    else
      OS << "<prfop invalid #" << getPrefetch() << ">";
    break;
  }
  case k_PSBHint:
    OS << getPSBHintName();
    break;
  case k_Register:
    OS << "<register " << getReg() << ">";
    if (!getShiftExtendAmount() && !hasShiftExtendAmount())
      break;
    LLVM_FALLTHROUGH;
  case k_BTIHint:
    OS << getBTIHintName();
    break;
  case k_ShiftExtend:
    OS << "<" << AArch64_AM::getShiftExtendName(getShiftExtendType()) << " #"
       << getShiftExtendAmount();
    if (!hasShiftExtendAmount())
      OS << "<imp>";
    OS << '>';
    break;
  }
}

/// @name Auto-generated Match Functions
/// {

static unsigned MatchRegisterName(StringRef Name);

/// }

static unsigned MatchNeonVectorRegName(StringRef Name) {
  return StringSwitch<unsigned>(Name.lower())
      .Case("v0", AArch64::Q0)
      .Case("v1", AArch64::Q1)
      .Case("v2", AArch64::Q2)
      .Case("v3", AArch64::Q3)
      .Case("v4", AArch64::Q4)
      .Case("v5", AArch64::Q5)
      .Case("v6", AArch64::Q6)
      .Case("v7", AArch64::Q7)
      .Case("v8", AArch64::Q8)
      .Case("v9", AArch64::Q9)
      .Case("v10", AArch64::Q10)
      .Case("v11", AArch64::Q11)
      .Case("v12", AArch64::Q12)
      .Case("v13", AArch64::Q13)
      .Case("v14", AArch64::Q14)
      .Case("v15", AArch64::Q15)
      .Case("v16", AArch64::Q16)
      .Case("v17", AArch64::Q17)
      .Case("v18", AArch64::Q18)
      .Case("v19", AArch64::Q19)
      .Case("v20", AArch64::Q20)
      .Case("v21", AArch64::Q21)
      .Case("v22", AArch64::Q22)
      .Case("v23", AArch64::Q23)
      .Case("v24", AArch64::Q24)
      .Case("v25", AArch64::Q25)
      .Case("v26", AArch64::Q26)
      .Case("v27", AArch64::Q27)
      .Case("v28", AArch64::Q28)
      .Case("v29", AArch64::Q29)
      .Case("v30", AArch64::Q30)
      .Case("v31", AArch64::Q31)
      .Default(0);
}

/// Returns an optional pair of (#elements, element-width) if Suffix
/// is a valid vector kind. Where the number of elements in a vector
/// or the vector width is implicit or explicitly unknown (but still a
/// valid suffix kind), 0 is used.
static Optional<std::pair<int, int>> parseVectorKind(StringRef Suffix,
                                                     RegKind VectorKind) {
  std::pair<int, int> Res = {-1, -1};

  switch (VectorKind) {
  case RegKind::NeonVector:
    Res =
        StringSwitch<std::pair<int, int>>(Suffix.lower())
            .Case("", {0, 0})
            .Case(".1d", {1, 64})
            .Case(".1q", {1, 128})
            // '.2h' needed for fp16 scalar pairwise reductions
            .Case(".2h", {2, 16})
            .Case(".2s", {2, 32})
            .Case(".2d", {2, 64})
            // '.4b' is another special case for the ARMv8.2a dot product
            // operand
            .Case(".4b", {4, 8})
            .Case(".4h", {4, 16})
            .Case(".4s", {4, 32})
            .Case(".8b", {8, 8})
            .Case(".8h", {8, 16})
            .Case(".16b", {16, 8})
            // Accept the width neutral ones, too, for verbose syntax. If those
            // aren't used in the right places, the token operand won't match so
            // all will work out.
            .Case(".b", {0, 8})
            .Case(".h", {0, 16})
            .Case(".s", {0, 32})
            .Case(".d", {0, 64})
            .Default({-1, -1});
    break;
  case RegKind::SVEPredicateVector:
  case RegKind::SVEDataVector:
    Res = StringSwitch<std::pair<int, int>>(Suffix.lower())
              .Case("", {0, 0})
              .Case(".b", {0, 8})
              .Case(".h", {0, 16})
              .Case(".s", {0, 32})
              .Case(".d", {0, 64})
              .Case(".q", {0, 128})
              .Default({-1, -1});
    break;
  default:
    llvm_unreachable("Unsupported RegKind");
  }

  if (Res == std::make_pair(-1, -1))
    return Optional<std::pair<int, int>>();

  return Optional<std::pair<int, int>>(Res);
}

static bool isValidVectorKind(StringRef Suffix, RegKind VectorKind) {
  return parseVectorKind(Suffix, VectorKind).hasValue();
}

static unsigned matchSVEDataVectorRegName(StringRef Name) {
  return StringSwitch<unsigned>(Name.lower())
      .Case("z0", AArch64::Z0)
      .Case("z1", AArch64::Z1)
      .Case("z2", AArch64::Z2)
      .Case("z3", AArch64::Z3)
      .Case("z4", AArch64::Z4)
      .Case("z5", AArch64::Z5)
      .Case("z6", AArch64::Z6)
      .Case("z7", AArch64::Z7)
      .Case("z8", AArch64::Z8)
      .Case("z9", AArch64::Z9)
      .Case("z10", AArch64::Z10)
      .Case("z11", AArch64::Z11)
      .Case("z12", AArch64::Z12)
      .Case("z13", AArch64::Z13)
      .Case("z14", AArch64::Z14)
      .Case("z15", AArch64::Z15)
      .Case("z16", AArch64::Z16)
      .Case("z17", AArch64::Z17)
      .Case("z18", AArch64::Z18)
      .Case("z19", AArch64::Z19)
      .Case("z20", AArch64::Z20)
      .Case("z21", AArch64::Z21)
      .Case("z22", AArch64::Z22)
      .Case("z23", AArch64::Z23)
      .Case("z24", AArch64::Z24)
      .Case("z25", AArch64::Z25)
      .Case("z26", AArch64::Z26)
      .Case("z27", AArch64::Z27)
      .Case("z28", AArch64::Z28)
      .Case("z29", AArch64::Z29)
      .Case("z30", AArch64::Z30)
      .Case("z31", AArch64::Z31)
      .Default(0);
}

static unsigned matchSVEPredicateVectorRegName(StringRef Name) {
  return StringSwitch<unsigned>(Name.lower())
      .Case("p0", AArch64::P0)
      .Case("p1", AArch64::P1)
      .Case("p2", AArch64::P2)
      .Case("p3", AArch64::P3)
      .Case("p4", AArch64::P4)
      .Case("p5", AArch64::P5)
      .Case("p6", AArch64::P6)
      .Case("p7", AArch64::P7)
      .Case("p8", AArch64::P8)
      .Case("p9", AArch64::P9)
      .Case("p10", AArch64::P10)
      .Case("p11", AArch64::P11)
      .Case("p12", AArch64::P12)
      .Case("p13", AArch64::P13)
      .Case("p14", AArch64::P14)
      .Case("p15", AArch64::P15)
      .Default(0);
}

bool AArch64AsmParser::ParseRegister(unsigned &RegNo, SMLoc &StartLoc,
                                     SMLoc &EndLoc) {
  StartLoc = getLoc();
  auto Res = tryParseScalarRegister(RegNo);
  EndLoc = SMLoc::getFromPointer(getLoc().getPointer() - 1);
  return Res != MatchOperand_Success;
}

// Matches a register name or register alias previously defined by '.req'
unsigned AArch64AsmParser::matchRegisterNameAlias(StringRef Name,
                                                  RegKind Kind) {
  unsigned RegNum = 0;
  if ((RegNum = matchSVEDataVectorRegName(Name)))
    return Kind == RegKind::SVEDataVector ? RegNum : 0;

  if ((RegNum = matchSVEPredicateVectorRegName(Name)))
    return Kind == RegKind::SVEPredicateVector ? RegNum : 0;

  if ((RegNum = MatchNeonVectorRegName(Name)))
    return Kind == RegKind::NeonVector ? RegNum : 0;

  // The parsed register must be of RegKind Scalar
  if ((RegNum = MatchRegisterName(Name)))
    return Kind == RegKind::Scalar ? RegNum : 0;

  if (!RegNum) {
    // Handle a few common aliases of registers.
    if (auto RegNum = StringSwitch<unsigned>(Name.lower())
                    .Case("fp", AArch64::FP)
                    .Case("lr",  AArch64::LR)
                    .Case("x31", AArch64::XZR)
                    .Case("w31", AArch64::WZR)
                    .Default(0))
      return Kind == RegKind::Scalar ? RegNum : 0;

    // Check for aliases registered via .req. Canonicalize to lower case.
    // That's more consistent since register names are case insensitive, and
    // it's how the original entry was passed in from MC/MCParser/AsmParser.
    auto Entry = RegisterReqs.find(Name.lower());
    if (Entry == RegisterReqs.end())
      return 0;

    // set RegNum if the match is the right kind of register
    if (Kind == Entry->getValue().first)
      RegNum = Entry->getValue().second;
  }
  return RegNum;
}

/// tryParseScalarRegister - Try to parse a register name. The token must be an
/// Identifier when called, and if it is a register name the token is eaten and
/// the register is added to the operand list.
OperandMatchResultTy
AArch64AsmParser::tryParseScalarRegister(unsigned &RegNum) {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  if (Tok.isNot(AsmToken::Identifier))
    return MatchOperand_NoMatch;

  std::string lowerCase = Tok.getString().lower();
  unsigned Reg = matchRegisterNameAlias(lowerCase, RegKind::Scalar);
  if (Reg == 0)
    return MatchOperand_NoMatch;

  RegNum = Reg;
  Parser.Lex(); // Eat identifier token.
  return MatchOperand_Success;
}

/// tryParseSysCROperand - Try to parse a system instruction CR operand name.
OperandMatchResultTy
AArch64AsmParser::tryParseSysCROperand(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = getLoc();

  if (Parser.getTok().isNot(AsmToken::Identifier)) {
    Error(S, "Expected cN operand where 0 <= N <= 15");
    return MatchOperand_ParseFail;
  }

  StringRef Tok = Parser.getTok().getIdentifier();
  if (Tok[0] != 'c' && Tok[0] != 'C') {
    Error(S, "Expected cN operand where 0 <= N <= 15");
    return MatchOperand_ParseFail;
  }

  uint32_t CRNum;
  bool BadNum = Tok.drop_front().getAsInteger(10, CRNum);
  if (BadNum || CRNum > 15) {
    Error(S, "Expected cN operand where 0 <= N <= 15");
    return MatchOperand_ParseFail;
  }

  Parser.Lex(); // Eat identifier token.
  Operands.push_back(
      AArch64Operand::CreateSysCR(CRNum, S, getLoc(), getContext()));
  return MatchOperand_Success;
}

/// tryParsePrefetch - Try to parse a prefetch operand.
template <bool IsSVEPrefetch>
OperandMatchResultTy
AArch64AsmParser::tryParsePrefetch(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = getLoc();
  const AsmToken &Tok = Parser.getTok();

  auto LookupByName = [](StringRef N) {
    if (IsSVEPrefetch) {
      if (auto Res = AArch64SVEPRFM::lookupSVEPRFMByName(N))
        return Optional<unsigned>(Res->Encoding);
    } else if (auto Res = AArch64PRFM::lookupPRFMByName(N))
      return Optional<unsigned>(Res->Encoding);
    return Optional<unsigned>();
  };

  auto LookupByEncoding = [](unsigned E) {
    if (IsSVEPrefetch) {
      if (auto Res = AArch64SVEPRFM::lookupSVEPRFMByEncoding(E))
        return Optional<StringRef>(Res->Name);
    } else if (auto Res = AArch64PRFM::lookupPRFMByEncoding(E))
      return Optional<StringRef>(Res->Name);
    return Optional<StringRef>();
  };
  unsigned MaxVal = IsSVEPrefetch ? 15 : 31;

  // Either an identifier for named values or a 5-bit immediate.
  // Eat optional hash.
  if (parseOptionalToken(AsmToken::Hash) ||
      Tok.is(AsmToken::Integer)) {
    const MCExpr *ImmVal;
    if (getParser().parseExpression(ImmVal))
      return MatchOperand_ParseFail;

    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(ImmVal);
    if (!MCE) {
      TokError("immediate value expected for prefetch operand");
      return MatchOperand_ParseFail;
    }
    unsigned prfop = MCE->getValue();
    if (prfop > MaxVal) {
      TokError("prefetch operand out of range, [0," + utostr(MaxVal) +
               "] expected");
      return MatchOperand_ParseFail;
    }

    auto PRFM = LookupByEncoding(MCE->getValue());
    Operands.push_back(AArch64Operand::CreatePrefetch(
        prfop, PRFM.getValueOr(""), S, getContext()));
    return MatchOperand_Success;
  }

  if (Tok.isNot(AsmToken::Identifier)) {
    TokError("prefetch hint expected");
    return MatchOperand_ParseFail;
  }

  auto PRFM = LookupByName(Tok.getString());
  if (!PRFM) {
    TokError("prefetch hint expected");
    return MatchOperand_ParseFail;
  }

  Parser.Lex(); // Eat identifier token.
  Operands.push_back(AArch64Operand::CreatePrefetch(
      *PRFM, Tok.getString(), S, getContext()));
  return MatchOperand_Success;
}

/// tryParsePSBHint - Try to parse a PSB operand, mapped to Hint command
OperandMatchResultTy
AArch64AsmParser::tryParsePSBHint(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = getLoc();
  const AsmToken &Tok = Parser.getTok();
  if (Tok.isNot(AsmToken::Identifier)) {
    TokError("invalid operand for instruction");
    return MatchOperand_ParseFail;
  }

  auto PSB = AArch64PSBHint::lookupPSBByName(Tok.getString());
  if (!PSB) {
    TokError("invalid operand for instruction");
    return MatchOperand_ParseFail;
  }

  Parser.Lex(); // Eat identifier token.
  Operands.push_back(AArch64Operand::CreatePSBHint(
      PSB->Encoding, Tok.getString(), S, getContext()));
  return MatchOperand_Success;
}

/// tryParseBTIHint - Try to parse a BTI operand, mapped to Hint command
OperandMatchResultTy
AArch64AsmParser::tryParseBTIHint(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = getLoc();
  const AsmToken &Tok = Parser.getTok();
  if (Tok.isNot(AsmToken::Identifier)) {
    TokError("invalid operand for instruction");
    return MatchOperand_ParseFail;
  }

  auto BTI = AArch64BTIHint::lookupBTIByName(Tok.getString());
  if (!BTI) {
    TokError("invalid operand for instruction");
    return MatchOperand_ParseFail;
  }

  Parser.Lex(); // Eat identifier token.
  Operands.push_back(AArch64Operand::CreateBTIHint(
      BTI->Encoding, Tok.getString(), S, getContext()));
  return MatchOperand_Success;
}

/// tryParseAdrpLabel - Parse and validate a source label for the ADRP
/// instruction.
OperandMatchResultTy
AArch64AsmParser::tryParseAdrpLabel(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = getLoc();
  const MCExpr *Expr;

  if (Parser.getTok().is(AsmToken::Hash)) {
    Parser.Lex(); // Eat hash token.
  }

  if (parseSymbolicImmVal(Expr))
    return MatchOperand_ParseFail;

  AArch64MCExpr::VariantKind ELFRefKind;
  MCSymbolRefExpr::VariantKind DarwinRefKind;
  int64_t Addend;
  if (classifySymbolRef(Expr, ELFRefKind, DarwinRefKind, Addend)) {
    if (DarwinRefKind == MCSymbolRefExpr::VK_None &&
        ELFRefKind == AArch64MCExpr::VK_INVALID) {
      // No modifier was specified at all; this is the syntax for an ELF basic
      // ADRP relocation (unfortunately).
      Expr =
          AArch64MCExpr::create(Expr, AArch64MCExpr::VK_ABS_PAGE, getContext());
    } else if ((DarwinRefKind == MCSymbolRefExpr::VK_GOTPAGE ||
                DarwinRefKind == MCSymbolRefExpr::VK_TLVPPAGE) &&
               Addend != 0) {
      Error(S, "gotpage label reference not allowed an addend");
      return MatchOperand_ParseFail;
    } else if (DarwinRefKind != MCSymbolRefExpr::VK_PAGE &&
               DarwinRefKind != MCSymbolRefExpr::VK_GOTPAGE &&
               DarwinRefKind != MCSymbolRefExpr::VK_TLVPPAGE &&
               ELFRefKind != AArch64MCExpr::VK_GOT_PAGE &&
               ELFRefKind != AArch64MCExpr::VK_GOTTPREL_PAGE &&
               ELFRefKind != AArch64MCExpr::VK_TLSDESC_PAGE) {
      // The operand must be an @page or @gotpage qualified symbolref.
      Error(S, "page or gotpage label reference expected");
      return MatchOperand_ParseFail;
    }
  }

  // We have either a label reference possibly with addend or an immediate. The
  // addend is a raw value here. The linker will adjust it to only reference the
  // page.
  SMLoc E = SMLoc::getFromPointer(getLoc().getPointer() - 1);
  Operands.push_back(AArch64Operand::CreateImm(Expr, S, E, getContext()));

  return MatchOperand_Success;
}

/// tryParseAdrLabel - Parse and validate a source label for the ADR
/// instruction.
OperandMatchResultTy
AArch64AsmParser::tryParseAdrLabel(OperandVector &Operands) {
  SMLoc S = getLoc();
  const MCExpr *Expr;

  // Leave anything with a bracket to the default for SVE
  if (getParser().getTok().is(AsmToken::LBrac))
    return MatchOperand_NoMatch;

  if (getParser().getTok().is(AsmToken::Hash))
    getParser().Lex(); // Eat hash token.

  if (parseSymbolicImmVal(Expr))
    return MatchOperand_ParseFail;

  AArch64MCExpr::VariantKind ELFRefKind;
  MCSymbolRefExpr::VariantKind DarwinRefKind;
  int64_t Addend;
  if (classifySymbolRef(Expr, ELFRefKind, DarwinRefKind, Addend)) {
    if (DarwinRefKind == MCSymbolRefExpr::VK_None &&
        ELFRefKind == AArch64MCExpr::VK_INVALID) {
      // No modifier was specified at all; this is the syntax for an ELF basic
      // ADR relocation (unfortunately).
      Expr = AArch64MCExpr::create(Expr, AArch64MCExpr::VK_ABS, getContext());
    } else {
      Error(S, "unexpected adr label");
      return MatchOperand_ParseFail;
    }
  }

  SMLoc E = SMLoc::getFromPointer(getLoc().getPointer() - 1);
  Operands.push_back(AArch64Operand::CreateImm(Expr, S, E, getContext()));
  return MatchOperand_Success;
}

/// tryParseFPImm - A floating point immediate expression operand.
template<bool AddFPZeroAsLiteral>
OperandMatchResultTy
AArch64AsmParser::tryParseFPImm(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = getLoc();

  bool Hash = parseOptionalToken(AsmToken::Hash);

  // Handle negation, as that still comes through as a separate token.
  bool isNegative = parseOptionalToken(AsmToken::Minus);

  const AsmToken &Tok = Parser.getTok();
  if (!Tok.is(AsmToken::Real) && !Tok.is(AsmToken::Integer)) {
    if (!Hash)
      return MatchOperand_NoMatch;
    TokError("invalid floating point immediate");
    return MatchOperand_ParseFail;
  }

  // Parse hexadecimal representation.
  if (Tok.is(AsmToken::Integer) && Tok.getString().startswith("0x")) {
    if (Tok.getIntVal() > 255 || isNegative) {
      TokError("encoded floating point value out of range");
      return MatchOperand_ParseFail;
    }

    APFloat F((double)AArch64_AM::getFPImmFloat(Tok.getIntVal()));
    Operands.push_back(
        AArch64Operand::CreateFPImm(F, true, S, getContext()));
  } else {
    // Parse FP representation.
    APFloat RealVal(APFloat::IEEEdouble());
    auto Status =
        RealVal.convertFromString(Tok.getString(), APFloat::rmTowardZero);
    if (isNegative)
      RealVal.changeSign();

    if (AddFPZeroAsLiteral && RealVal.isPosZero()) {
      Operands.push_back(
          AArch64Operand::CreateToken("#0", false, S, getContext()));
      Operands.push_back(
          AArch64Operand::CreateToken(".0", false, S, getContext()));
    } else
      Operands.push_back(AArch64Operand::CreateFPImm(
          RealVal, Status == APFloat::opOK, S, getContext()));
  }

  Parser.Lex(); // Eat the token.

  return MatchOperand_Success;
}

/// tryParseImmWithOptionalShift - Parse immediate operand, optionally with
/// a shift suffix, for example '#1, lsl #12'.
OperandMatchResultTy
AArch64AsmParser::tryParseImmWithOptionalShift(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  SMLoc S = getLoc();

  if (Parser.getTok().is(AsmToken::Hash))
    Parser.Lex(); // Eat '#'
  else if (Parser.getTok().isNot(AsmToken::Integer))
    // Operand should start from # or should be integer, emit error otherwise.
    return MatchOperand_NoMatch;

  const MCExpr *Imm;
  if (parseSymbolicImmVal(Imm))
    return MatchOperand_ParseFail;
  else if (Parser.getTok().isNot(AsmToken::Comma)) {
    SMLoc E = Parser.getTok().getLoc();
    Operands.push_back(
        AArch64Operand::CreateImm(Imm, S, E, getContext()));
    return MatchOperand_Success;
  }

  // Eat ','
  Parser.Lex();

  // The optional operand must be "lsl #N" where N is non-negative.
  if (!Parser.getTok().is(AsmToken::Identifier) ||
      !Parser.getTok().getIdentifier().equals_lower("lsl")) {
    Error(Parser.getTok().getLoc(), "only 'lsl #+N' valid after immediate");
    return MatchOperand_ParseFail;
  }

  // Eat 'lsl'
  Parser.Lex();

  parseOptionalToken(AsmToken::Hash);

  if (Parser.getTok().isNot(AsmToken::Integer)) {
    Error(Parser.getTok().getLoc(), "only 'lsl #+N' valid after immediate");
    return MatchOperand_ParseFail;
  }

  int64_t ShiftAmount = Parser.getTok().getIntVal();

  if (ShiftAmount < 0) {
    Error(Parser.getTok().getLoc(), "positive shift amount required");
    return MatchOperand_ParseFail;
  }
  Parser.Lex(); // Eat the number

  // Just in case the optional lsl #0 is used for immediates other than zero.
  if (ShiftAmount == 0 && Imm != 0) {
    SMLoc E = Parser.getTok().getLoc();
    Operands.push_back(AArch64Operand::CreateImm(Imm, S, E, getContext()));
    return MatchOperand_Success;
  }

  SMLoc E = Parser.getTok().getLoc();
  Operands.push_back(AArch64Operand::CreateShiftedImm(Imm, ShiftAmount,
                                                      S, E, getContext()));
  return MatchOperand_Success;
}

/// parseCondCodeString - Parse a Condition Code string.
AArch64CC::CondCode AArch64AsmParser::parseCondCodeString(StringRef Cond) {
  AArch64CC::CondCode CC = StringSwitch<AArch64CC::CondCode>(Cond.lower())
                    .Case("eq", AArch64CC::EQ)
                    .Case("ne", AArch64CC::NE)
                    .Case("cs", AArch64CC::HS)
                    .Case("hs", AArch64CC::HS)
                    .Case("cc", AArch64CC::LO)
                    .Case("lo", AArch64CC::LO)
                    .Case("mi", AArch64CC::MI)
                    .Case("pl", AArch64CC::PL)
                    .Case("vs", AArch64CC::VS)
                    .Case("vc", AArch64CC::VC)
                    .Case("hi", AArch64CC::HI)
                    .Case("ls", AArch64CC::LS)
                    .Case("ge", AArch64CC::GE)
                    .Case("lt", AArch64CC::LT)
                    .Case("gt", AArch64CC::GT)
                    .Case("le", AArch64CC::LE)
                    .Case("al", AArch64CC::AL)
                    .Case("nv", AArch64CC::NV)
                    .Default(AArch64CC::Invalid);

  if (CC == AArch64CC::Invalid &&
      getSTI().getFeatureBits()[AArch64::FeatureSVE])
    CC = StringSwitch<AArch64CC::CondCode>(Cond.lower())
                    .Case("none",  AArch64CC::EQ)
                    .Case("any",   AArch64CC::NE)
                    .Case("nlast", AArch64CC::HS)
                    .Case("last",  AArch64CC::LO)
                    .Case("first", AArch64CC::MI)
                    .Case("nfrst", AArch64CC::PL)
                    .Case("pmore", AArch64CC::HI)
                    .Case("plast", AArch64CC::LS)
                    .Case("tcont", AArch64CC::GE)
                    .Case("tstop", AArch64CC::LT)
                    .Default(AArch64CC::Invalid);

  return CC;
}

/// parseCondCode - Parse a Condition Code operand.
bool AArch64AsmParser::parseCondCode(OperandVector &Operands,
                                     bool invertCondCode) {
  MCAsmParser &Parser = getParser();
  SMLoc S = getLoc();
  const AsmToken &Tok = Parser.getTok();
  assert(Tok.is(AsmToken::Identifier) && "Token is not an Identifier");

  StringRef Cond = Tok.getString();
  AArch64CC::CondCode CC = parseCondCodeString(Cond);
  if (CC == AArch64CC::Invalid)
    return TokError("invalid condition code");
  Parser.Lex(); // Eat identifier token.

  if (invertCondCode) {
    if (CC == AArch64CC::AL || CC == AArch64CC::NV)
      return TokError("condition codes AL and NV are invalid for this instruction");
    CC = AArch64CC::getInvertedCondCode(AArch64CC::CondCode(CC));
  }

  Operands.push_back(
      AArch64Operand::CreateCondCode(CC, S, getLoc(), getContext()));
  return false;
}

/// tryParseOptionalShift - Some operands take an optional shift argument. Parse
/// them if present.
OperandMatchResultTy
AArch64AsmParser::tryParseOptionalShiftExtend(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  std::string LowerID = Tok.getString().lower();
  AArch64_AM::ShiftExtendType ShOp =
      StringSwitch<AArch64_AM::ShiftExtendType>(LowerID)
          .Case("lsl", AArch64_AM::LSL)
          .Case("lsr", AArch64_AM::LSR)
          .Case("asr", AArch64_AM::ASR)
          .Case("ror", AArch64_AM::ROR)
          .Case("msl", AArch64_AM::MSL)
          .Case("uxtb", AArch64_AM::UXTB)
          .Case("uxth", AArch64_AM::UXTH)
          .Case("uxtw", AArch64_AM::UXTW)
          .Case("uxtx", AArch64_AM::UXTX)
          .Case("sxtb", AArch64_AM::SXTB)
          .Case("sxth", AArch64_AM::SXTH)
          .Case("sxtw", AArch64_AM::SXTW)
          .Case("sxtx", AArch64_AM::SXTX)
          .Default(AArch64_AM::InvalidShiftExtend);

  if (ShOp == AArch64_AM::InvalidShiftExtend)
    return MatchOperand_NoMatch;

  SMLoc S = Tok.getLoc();
  Parser.Lex();

  bool Hash = parseOptionalToken(AsmToken::Hash);

  if (!Hash && getLexer().isNot(AsmToken::Integer)) {
    if (ShOp == AArch64_AM::LSL || ShOp == AArch64_AM::LSR ||
        ShOp == AArch64_AM::ASR || ShOp == AArch64_AM::ROR ||
        ShOp == AArch64_AM::MSL) {
      // We expect a number here.
      TokError("expected #imm after shift specifier");
      return MatchOperand_ParseFail;
    }

    // "extend" type operations don't need an immediate, #0 is implicit.
    SMLoc E = SMLoc::getFromPointer(getLoc().getPointer() - 1);
    Operands.push_back(
        AArch64Operand::CreateShiftExtend(ShOp, 0, false, S, E, getContext()));
    return MatchOperand_Success;
  }

  // Make sure we do actually have a number, identifier or a parenthesized
  // expression.
  SMLoc E = Parser.getTok().getLoc();
  if (!Parser.getTok().is(AsmToken::Integer) &&
      !Parser.getTok().is(AsmToken::LParen) &&
      !Parser.getTok().is(AsmToken::Identifier)) {
    Error(E, "expected integer shift amount");
    return MatchOperand_ParseFail;
  }

  const MCExpr *ImmVal;
  if (getParser().parseExpression(ImmVal))
    return MatchOperand_ParseFail;

  const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(ImmVal);
  if (!MCE) {
    Error(E, "expected constant '#imm' after shift specifier");
    return MatchOperand_ParseFail;
  }

  E = SMLoc::getFromPointer(getLoc().getPointer() - 1);
  Operands.push_back(AArch64Operand::CreateShiftExtend(
      ShOp, MCE->getValue(), true, S, E, getContext()));
  return MatchOperand_Success;
}

static const struct Extension {
  const char *Name;
  const FeatureBitset Features;
} ExtensionMap[] = {
    {"crc", {AArch64::FeatureCRC}},
    {"sm4", {AArch64::FeatureSM4}},
    {"sha3", {AArch64::FeatureSHA3}},
    {"sha2", {AArch64::FeatureSHA2}},
    {"aes", {AArch64::FeatureAES}},
    {"crypto", {AArch64::FeatureCrypto}},
    {"fp", {AArch64::FeatureFPARMv8}},
    {"simd", {AArch64::FeatureNEON}},
    {"ras", {AArch64::FeatureRAS}},
    {"lse", {AArch64::FeatureLSE}},
    {"predres", {AArch64::FeaturePredRes}},
    {"ccdp", {AArch64::FeatureCacheDeepPersist}},
    {"mte", {AArch64::FeatureMTE}},
    {"tlb-rmi", {AArch64::FeatureTLB_RMI}},
    {"pan-rwv", {AArch64::FeaturePAN_RWV}},
    {"ccpp", {AArch64::FeatureCCPP}},
    {"sve", {AArch64::FeatureSVE}},
    // FIXME: Unsupported extensions
    {"pan", {}},
    {"lor", {}},
    {"rdma", {}},
    {"profile", {}},
};

static void setRequiredFeatureString(FeatureBitset FBS, std::string &Str) {
  if (FBS[AArch64::HasV8_1aOps])
    Str += "ARMv8.1a";
  else if (FBS[AArch64::HasV8_2aOps])
    Str += "ARMv8.2a";
  else if (FBS[AArch64::HasV8_3aOps])
    Str += "ARMv8.3a";
  else if (FBS[AArch64::HasV8_4aOps])
    Str += "ARMv8.4a";
  else if (FBS[AArch64::HasV8_5aOps])
    Str += "ARMv8.5a";
  else {
    auto ext = std::find_if(std::begin(ExtensionMap),
      std::end(ExtensionMap),
      [&](const Extension& e)
      // Use & in case multiple features are enabled
      { return (FBS & e.Features) != FeatureBitset(); }
    );

    Str += ext != std::end(ExtensionMap) ? ext->Name : "(unknown)";
  }
}

void AArch64AsmParser::createSysAlias(uint16_t Encoding, OperandVector &Operands,
                                      SMLoc S) {
  const uint16_t Op2 = Encoding & 7;
  const uint16_t Cm = (Encoding & 0x78) >> 3;
  const uint16_t Cn = (Encoding & 0x780) >> 7;
  const uint16_t Op1 = (Encoding & 0x3800) >> 11;

  const MCExpr *Expr = MCConstantExpr::create(Op1, getContext());

  Operands.push_back(
      AArch64Operand::CreateImm(Expr, S, getLoc(), getContext()));
  Operands.push_back(
      AArch64Operand::CreateSysCR(Cn, S, getLoc(), getContext()));
  Operands.push_back(
      AArch64Operand::CreateSysCR(Cm, S, getLoc(), getContext()));
  Expr = MCConstantExpr::create(Op2, getContext());
  Operands.push_back(
      AArch64Operand::CreateImm(Expr, S, getLoc(), getContext()));
}

/// parseSysAlias - The IC, DC, AT, and TLBI instructions are simple aliases for
/// the SYS instruction. Parse them specially so that we create a SYS MCInst.
bool AArch64AsmParser::parseSysAlias(StringRef Name, SMLoc NameLoc,
                                   OperandVector &Operands) {
  if (Name.find('.') != StringRef::npos)
    return TokError("invalid operand");

  Mnemonic = Name;
  Operands.push_back(
      AArch64Operand::CreateToken("sys", false, NameLoc, getContext()));

  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  StringRef Op = Tok.getString();
  SMLoc S = Tok.getLoc();

  if (Mnemonic == "ic") {
    const AArch64IC::IC *IC = AArch64IC::lookupICByName(Op);
    if (!IC)
      return TokError("invalid operand for IC instruction");
    else if (!IC->haveFeatures(getSTI().getFeatureBits())) {
      std::string Str("IC " + std::string(IC->Name) + " requires ");
      setRequiredFeatureString(IC->getRequiredFeatures(), Str);
      return TokError(Str.c_str());
    }
    createSysAlias(IC->Encoding, Operands, S);
  } else if (Mnemonic == "dc") {
    const AArch64DC::DC *DC = AArch64DC::lookupDCByName(Op);
    if (!DC)
      return TokError("invalid operand for DC instruction");
    else if (!DC->haveFeatures(getSTI().getFeatureBits())) {
      std::string Str("DC " + std::string(DC->Name) + " requires ");
      setRequiredFeatureString(DC->getRequiredFeatures(), Str);
      return TokError(Str.c_str());
    }
    createSysAlias(DC->Encoding, Operands, S);
  } else if (Mnemonic == "at") {
    const AArch64AT::AT *AT = AArch64AT::lookupATByName(Op);
    if (!AT)
      return TokError("invalid operand for AT instruction");
    else if (!AT->haveFeatures(getSTI().getFeatureBits())) {
      std::string Str("AT " + std::string(AT->Name) + " requires ");
      setRequiredFeatureString(AT->getRequiredFeatures(), Str);
      return TokError(Str.c_str());
    }
    createSysAlias(AT->Encoding, Operands, S);
  } else if (Mnemonic == "tlbi") {
    const AArch64TLBI::TLBI *TLBI = AArch64TLBI::lookupTLBIByName(Op);
    if (!TLBI)
      return TokError("invalid operand for TLBI instruction");
    else if (!TLBI->haveFeatures(getSTI().getFeatureBits())) {
      std::string Str("TLBI " + std::string(TLBI->Name) + " requires ");
      setRequiredFeatureString(TLBI->getRequiredFeatures(), Str);
      return TokError(Str.c_str());
    }
    createSysAlias(TLBI->Encoding, Operands, S);
  } else if (Mnemonic == "cfp" || Mnemonic == "dvp" || Mnemonic == "cpp") {
    const AArch64PRCTX::PRCTX *PRCTX = AArch64PRCTX::lookupPRCTXByName(Op);
    if (!PRCTX)
      return TokError("invalid operand for prediction restriction instruction");
    else if (!PRCTX->haveFeatures(getSTI().getFeatureBits())) {
      std::string Str(
          Mnemonic.upper() + std::string(PRCTX->Name) + " requires ");
      setRequiredFeatureString(PRCTX->getRequiredFeatures(), Str);
      return TokError(Str.c_str());
    }
    uint16_t PRCTX_Op2 =
      Mnemonic == "cfp" ? 4 :
      Mnemonic == "dvp" ? 5 :
      Mnemonic == "cpp" ? 7 :
      0;
    assert(PRCTX_Op2 && "Invalid mnemonic for prediction restriction instruction");
    createSysAlias(PRCTX->Encoding << 3 | PRCTX_Op2 , Operands, S);
  }

  Parser.Lex(); // Eat operand.

  bool ExpectRegister = (Op.lower().find("all") == StringRef::npos);
  bool HasRegister = false;

  // Check for the optional register operand.
  if (parseOptionalToken(AsmToken::Comma)) {
    if (Tok.isNot(AsmToken::Identifier) || parseRegister(Operands))
      return TokError("expected register operand");
    HasRegister = true;
  }

  if (ExpectRegister && !HasRegister)
    return TokError("specified " + Mnemonic + " op requires a register");
  else if (!ExpectRegister && HasRegister)
    return TokError("specified " + Mnemonic + " op does not use a register");

  if (parseToken(AsmToken::EndOfStatement, "unexpected token in argument list"))
    return true;

  return false;
}

OperandMatchResultTy
AArch64AsmParser::tryParseBarrierOperand(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();

  if (Mnemonic == "tsb" && Tok.isNot(AsmToken::Identifier)) {
    TokError("'csync' operand expected");
    return MatchOperand_ParseFail;
  // Can be either a #imm style literal or an option name
  } else if (parseOptionalToken(AsmToken::Hash) || Tok.is(AsmToken::Integer)) {
    // Immediate operand.
    const MCExpr *ImmVal;
    SMLoc ExprLoc = getLoc();
    if (getParser().parseExpression(ImmVal))
      return MatchOperand_ParseFail;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(ImmVal);
    if (!MCE) {
      Error(ExprLoc, "immediate value expected for barrier operand");
      return MatchOperand_ParseFail;
    }
    if (MCE->getValue() < 0 || MCE->getValue() > 15) {
      Error(ExprLoc, "barrier operand out of range");
      return MatchOperand_ParseFail;
    }
    auto DB = AArch64DB::lookupDBByEncoding(MCE->getValue());
    Operands.push_back(AArch64Operand::CreateBarrier(
        MCE->getValue(), DB ? DB->Name : "", ExprLoc, getContext()));
    return MatchOperand_Success;
  }

  if (Tok.isNot(AsmToken::Identifier)) {
    TokError("invalid operand for instruction");
    return MatchOperand_ParseFail;
  }

  auto TSB = AArch64TSB::lookupTSBByName(Tok.getString());
  // The only valid named option for ISB is 'sy'
  auto DB = AArch64DB::lookupDBByName(Tok.getString());
  if (Mnemonic == "isb" && (!DB || DB->Encoding != AArch64DB::sy)) {
    TokError("'sy' or #imm operand expected");
    return MatchOperand_ParseFail;
  // The only valid named option for TSB is 'csync'
  } else if (Mnemonic == "tsb" && (!TSB || TSB->Encoding != AArch64TSB::csync)) {
    TokError("'csync' operand expected");
    return MatchOperand_ParseFail;
  } else if (!DB && !TSB) {
    TokError("invalid barrier option name");
    return MatchOperand_ParseFail;
  }

  Operands.push_back(AArch64Operand::CreateBarrier(
      DB ? DB->Encoding : TSB->Encoding, Tok.getString(), getLoc(), getContext()));
  Parser.Lex(); // Consume the option

  return MatchOperand_Success;
}

OperandMatchResultTy
AArch64AsmParser::tryParseSysReg(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();

  if (Tok.isNot(AsmToken::Identifier))
    return MatchOperand_NoMatch;

  int MRSReg, MSRReg;
  auto SysReg = AArch64SysReg::lookupSysRegByName(Tok.getString());
  if (SysReg && SysReg->haveFeatures(getSTI().getFeatureBits())) {
    MRSReg = SysReg->Readable ? SysReg->Encoding : -1;
    MSRReg = SysReg->Writeable ? SysReg->Encoding : -1;
  } else
    MRSReg = MSRReg = AArch64SysReg::parseGenericRegister(Tok.getString());

  auto PState = AArch64PState::lookupPStateByName(Tok.getString());
  unsigned PStateImm = -1;
  if (PState && PState->haveFeatures(getSTI().getFeatureBits()))
    PStateImm = PState->Encoding;

  Operands.push_back(
      AArch64Operand::CreateSysReg(Tok.getString(), getLoc(), MRSReg, MSRReg,
                                   PStateImm, getContext()));
  Parser.Lex(); // Eat identifier

  return MatchOperand_Success;
}

/// tryParseNeonVectorRegister - Parse a vector register operand.
bool AArch64AsmParser::tryParseNeonVectorRegister(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  if (Parser.getTok().isNot(AsmToken::Identifier))
    return true;

  SMLoc S = getLoc();
  // Check for a vector register specifier first.
  StringRef Kind;
  unsigned Reg;
  OperandMatchResultTy Res =
      tryParseVectorRegister(Reg, Kind, RegKind::NeonVector);
  if (Res != MatchOperand_Success)
    return true;

  const auto &KindRes = parseVectorKind(Kind, RegKind::NeonVector);
  if (!KindRes)
    return true;

  unsigned ElementWidth = KindRes->second;
  Operands.push_back(
      AArch64Operand::CreateVectorReg(Reg, RegKind::NeonVector, ElementWidth,
                                      S, getLoc(), getContext()));

  // If there was an explicit qualifier, that goes on as a literal text
  // operand.
  if (!Kind.empty())
    Operands.push_back(
        AArch64Operand::CreateToken(Kind, false, S, getContext()));

  return tryParseVectorIndex(Operands) == MatchOperand_ParseFail;
}

OperandMatchResultTy
AArch64AsmParser::tryParseVectorIndex(OperandVector &Operands) {
  SMLoc SIdx = getLoc();
  if (parseOptionalToken(AsmToken::LBrac)) {
    const MCExpr *ImmVal;
    if (getParser().parseExpression(ImmVal))
      return MatchOperand_NoMatch;
    const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(ImmVal);
    if (!MCE) {
      TokError("immediate value expected for vector index");
      return MatchOperand_ParseFail;;
    }

    SMLoc E = getLoc();

    if (parseToken(AsmToken::RBrac, "']' expected"))
      return MatchOperand_ParseFail;;

    Operands.push_back(AArch64Operand::CreateVectorIndex(MCE->getValue(), SIdx,
                                                         E, getContext()));
    return MatchOperand_Success;
  }

  return MatchOperand_NoMatch;
}

// tryParseVectorRegister - Try to parse a vector register name with
// optional kind specifier. If it is a register specifier, eat the token
// and return it.
OperandMatchResultTy
AArch64AsmParser::tryParseVectorRegister(unsigned &Reg, StringRef &Kind,
                                         RegKind MatchKind) {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();

  if (Tok.isNot(AsmToken::Identifier))
    return MatchOperand_NoMatch;

  StringRef Name = Tok.getString();
  // If there is a kind specifier, it's separated from the register name by
  // a '.'.
  size_t Start = 0, Next = Name.find('.');
  StringRef Head = Name.slice(Start, Next);
  unsigned RegNum = matchRegisterNameAlias(Head, MatchKind);

  if (RegNum) {
    if (Next != StringRef::npos) {
      Kind = Name.slice(Next, StringRef::npos);
      if (!isValidVectorKind(Kind, MatchKind)) {
        TokError("invalid vector kind qualifier");
        return MatchOperand_ParseFail;
      }
    }
    Parser.Lex(); // Eat the register token.

    Reg = RegNum;
    return MatchOperand_Success;
  }

  return MatchOperand_NoMatch;
}

/// tryParseSVEPredicateVector - Parse a SVE predicate register operand.
OperandMatchResultTy
AArch64AsmParser::tryParseSVEPredicateVector(OperandVector &Operands) {
  // Check for a SVE predicate register specifier first.
  const SMLoc S = getLoc();
  StringRef Kind;
  unsigned RegNum;
  auto Res = tryParseVectorRegister(RegNum, Kind, RegKind::SVEPredicateVector);
  if (Res != MatchOperand_Success)
    return Res;

  const auto &KindRes = parseVectorKind(Kind, RegKind::SVEPredicateVector);
  if (!KindRes)
    return MatchOperand_NoMatch;

  unsigned ElementWidth = KindRes->second;
  Operands.push_back(AArch64Operand::CreateVectorReg(
      RegNum, RegKind::SVEPredicateVector, ElementWidth, S,
      getLoc(), getContext()));

  // Not all predicates are followed by a '/m' or '/z'.
  MCAsmParser &Parser = getParser();
  if (Parser.getTok().isNot(AsmToken::Slash))
    return MatchOperand_Success;

  // But when they do they shouldn't have an element type suffix.
  if (!Kind.empty()) {
    Error(S, "not expecting size suffix");
    return MatchOperand_ParseFail;
  }

  // Add a literal slash as operand
  Operands.push_back(
      AArch64Operand::CreateToken("/" , false, getLoc(), getContext()));

  Parser.Lex(); // Eat the slash.

  // Zeroing or merging?
  auto Pred = Parser.getTok().getString().lower();
  if (Pred != "z" && Pred != "m") {
    Error(getLoc(), "expecting 'm' or 'z' predication");
    return MatchOperand_ParseFail;
  }

  // Add zero/merge token.
  const char *ZM = Pred == "z" ? "z" : "m";
  Operands.push_back(
    AArch64Operand::CreateToken(ZM, false, getLoc(), getContext()));

  Parser.Lex(); // Eat zero/merge token.
  return MatchOperand_Success;
}

/// parseRegister - Parse a register operand.
bool AArch64AsmParser::parseRegister(OperandVector &Operands) {
  // Try for a Neon vector register.
  if (!tryParseNeonVectorRegister(Operands))
    return false;

  // Otherwise try for a scalar register.
  if (tryParseGPROperand<false>(Operands) == MatchOperand_Success)
    return false;

  return true;
}

bool AArch64AsmParser::parseSymbolicImmVal(const MCExpr *&ImmVal) {
  MCAsmParser &Parser = getParser();
  bool HasELFModifier = false;
  AArch64MCExpr::VariantKind RefKind;

  if (parseOptionalToken(AsmToken::Colon)) {
    HasELFModifier = true;

    if (Parser.getTok().isNot(AsmToken::Identifier))
      return TokError("expect relocation specifier in operand after ':'");

    std::string LowerCase = Parser.getTok().getIdentifier().lower();
    RefKind = StringSwitch<AArch64MCExpr::VariantKind>(LowerCase)
                  .Case("lo12", AArch64MCExpr::VK_LO12)
                  .Case("abs_g3", AArch64MCExpr::VK_ABS_G3)
                  .Case("abs_g2", AArch64MCExpr::VK_ABS_G2)
                  .Case("abs_g2_s", AArch64MCExpr::VK_ABS_G2_S)
                  .Case("abs_g2_nc", AArch64MCExpr::VK_ABS_G2_NC)
                  .Case("abs_g1", AArch64MCExpr::VK_ABS_G1)
                  .Case("abs_g1_s", AArch64MCExpr::VK_ABS_G1_S)
                  .Case("abs_g1_nc", AArch64MCExpr::VK_ABS_G1_NC)
                  .Case("abs_g0", AArch64MCExpr::VK_ABS_G0)
                  .Case("abs_g0_s", AArch64MCExpr::VK_ABS_G0_S)
                  .Case("abs_g0_nc", AArch64MCExpr::VK_ABS_G0_NC)
                  .Case("dtprel_g2", AArch64MCExpr::VK_DTPREL_G2)
                  .Case("dtprel_g1", AArch64MCExpr::VK_DTPREL_G1)
                  .Case("dtprel_g1_nc", AArch64MCExpr::VK_DTPREL_G1_NC)
                  .Case("dtprel_g0", AArch64MCExpr::VK_DTPREL_G0)
                  .Case("dtprel_g0_nc", AArch64MCExpr::VK_DTPREL_G0_NC)
                  .Case("dtprel_hi12", AArch64MCExpr::VK_DTPREL_HI12)
                  .Case("dtprel_lo12", AArch64MCExpr::VK_DTPREL_LO12)
                  .Case("dtprel_lo12_nc", AArch64MCExpr::VK_DTPREL_LO12_NC)
                  .Case("tprel_g2", AArch64MCExpr::VK_TPREL_G2)
                  .Case("tprel_g1", AArch64MCExpr::VK_TPREL_G1)
                  .Case("tprel_g1_nc", AArch64MCExpr::VK_TPREL_G1_NC)
                  .Case("tprel_g0", AArch64MCExpr::VK_TPREL_G0)
                  .Case("tprel_g0_nc", AArch64MCExpr::VK_TPREL_G0_NC)
                  .Case("tprel_hi12", AArch64MCExpr::VK_TPREL_HI12)
                  .Case("tprel_lo12", AArch64MCExpr::VK_TPREL_LO12)
                  .Case("tprel_lo12_nc", AArch64MCExpr::VK_TPREL_LO12_NC)
                  .Case("tlsdesc_lo12", AArch64MCExpr::VK_TLSDESC_LO12)
                  .Case("got", AArch64MCExpr::VK_GOT_PAGE)
                  .Case("got_lo12", AArch64MCExpr::VK_GOT_LO12)
                  .Case("gottprel", AArch64MCExpr::VK_GOTTPREL_PAGE)
                  .Case("gottprel_lo12", AArch64MCExpr::VK_GOTTPREL_LO12_NC)
                  .Case("gottprel_g1", AArch64MCExpr::VK_GOTTPREL_G1)
                  .Case("gottprel_g0_nc", AArch64MCExpr::VK_GOTTPREL_G0_NC)
                  .Case("tlsdesc", AArch64MCExpr::VK_TLSDESC_PAGE)
                  .Case("secrel_lo12", AArch64MCExpr::VK_SECREL_LO12)
                  .Case("secrel_hi12", AArch64MCExpr::VK_SECREL_HI12)
                  .Default(AArch64MCExpr::VK_INVALID);

    if (RefKind == AArch64MCExpr::VK_INVALID)
      return TokError("expect relocation specifier in operand after ':'");

    Parser.Lex(); // Eat identifier

    if (parseToken(AsmToken::Colon, "expect ':' after relocation specifier"))
      return true;
  }

  if (getParser().parseExpression(ImmVal))
    return true;

  if (HasELFModifier)
    ImmVal = AArch64MCExpr::create(ImmVal, RefKind, getContext());

  return false;
}

template <RegKind VectorKind>
OperandMatchResultTy
AArch64AsmParser::tryParseVectorList(OperandVector &Operands,
                                     bool ExpectMatch) {
  MCAsmParser &Parser = getParser();
  if (!Parser.getTok().is(AsmToken::LCurly))
    return MatchOperand_NoMatch;

  // Wrapper around parse function
  auto ParseVector = [this, &Parser](unsigned &Reg, StringRef &Kind, SMLoc Loc,
                                     bool NoMatchIsError) {
    auto RegTok = Parser.getTok();
    auto ParseRes = tryParseVectorRegister(Reg, Kind, VectorKind);
    if (ParseRes == MatchOperand_Success) {
      if (parseVectorKind(Kind, VectorKind))
        return ParseRes;
      llvm_unreachable("Expected a valid vector kind");
    }

    if (RegTok.isNot(AsmToken::Identifier) ||
        ParseRes == MatchOperand_ParseFail ||
        (ParseRes == MatchOperand_NoMatch && NoMatchIsError)) {
      Error(Loc, "vector register expected");
      return MatchOperand_ParseFail;
    }

    return MatchOperand_NoMatch;
  };

  SMLoc S = getLoc();
  auto LCurly = Parser.getTok();
  Parser.Lex(); // Eat left bracket token.

  StringRef Kind;
  unsigned FirstReg;
  auto ParseRes = ParseVector(FirstReg, Kind, getLoc(), ExpectMatch);

  // Put back the original left bracket if there was no match, so that
  // different types of list-operands can be matched (e.g. SVE, Neon).
  if (ParseRes == MatchOperand_NoMatch)
    Parser.getLexer().UnLex(LCurly);

  if (ParseRes != MatchOperand_Success)
    return ParseRes;

  int64_t PrevReg = FirstReg;
  unsigned Count = 1;

  if (parseOptionalToken(AsmToken::Minus)) {
    SMLoc Loc = getLoc();
    StringRef NextKind;

    unsigned Reg;
    ParseRes = ParseVector(Reg, NextKind, getLoc(), true);
    if (ParseRes != MatchOperand_Success)
      return ParseRes;

    // Any Kind suffices must match on all regs in the list.
    if (Kind != NextKind) {
      Error(Loc, "mismatched register size suffix");
      return MatchOperand_ParseFail;
    }

    unsigned Space = (PrevReg < Reg) ? (Reg - PrevReg) : (Reg + 32 - PrevReg);

    if (Space == 0 || Space > 3) {
      Error(Loc, "invalid number of vectors");
      return MatchOperand_ParseFail;
    }

    Count += Space;
  }
  else {
    while (parseOptionalToken(AsmToken::Comma)) {
      SMLoc Loc = getLoc();
      StringRef NextKind;
      unsigned Reg;
      ParseRes = ParseVector(Reg, NextKind, getLoc(), true);
      if (ParseRes != MatchOperand_Success)
        return ParseRes;

      // Any Kind suffices must match on all regs in the list.
      if (Kind != NextKind) {
        Error(Loc, "mismatched register size suffix");
        return MatchOperand_ParseFail;
      }

      // Registers must be incremental (with wraparound at 31)
      if (getContext().getRegisterInfo()->getEncodingValue(Reg) !=
          (getContext().getRegisterInfo()->getEncodingValue(PrevReg) + 1) % 32) {
        Error(Loc, "registers must be sequential");
        return MatchOperand_ParseFail;
      }

      PrevReg = Reg;
      ++Count;
    }
  }

  if (parseToken(AsmToken::RCurly, "'}' expected"))
    return MatchOperand_ParseFail;

  if (Count > 4) {
    Error(S, "invalid number of vectors");
    return MatchOperand_ParseFail;
  }

  unsigned NumElements = 0;
  unsigned ElementWidth = 0;
  if (!Kind.empty()) {
    if (const auto &VK = parseVectorKind(Kind, VectorKind))
      std::tie(NumElements, ElementWidth) = *VK;
  }

  Operands.push_back(AArch64Operand::CreateVectorList(
      FirstReg, Count, NumElements, ElementWidth, VectorKind, S, getLoc(),
      getContext()));

  return MatchOperand_Success;
}

/// parseNeonVectorList - Parse a vector list operand for AdvSIMD instructions.
bool AArch64AsmParser::parseNeonVectorList(OperandVector &Operands) {
  auto ParseRes = tryParseVectorList<RegKind::NeonVector>(Operands, true);
  if (ParseRes != MatchOperand_Success)
    return true;

  return tryParseVectorIndex(Operands) == MatchOperand_ParseFail;
}

OperandMatchResultTy
AArch64AsmParser::tryParseGPR64sp0Operand(OperandVector &Operands) {
  SMLoc StartLoc = getLoc();

  unsigned RegNum;
  OperandMatchResultTy Res = tryParseScalarRegister(RegNum);
  if (Res != MatchOperand_Success)
    return Res;

  if (!parseOptionalToken(AsmToken::Comma)) {
    Operands.push_back(AArch64Operand::CreateReg(
        RegNum, RegKind::Scalar, StartLoc, getLoc(), getContext()));
    return MatchOperand_Success;
  }

  parseOptionalToken(AsmToken::Hash);

  if (getParser().getTok().isNot(AsmToken::Integer)) {
    Error(getLoc(), "index must be absent or #0");
    return MatchOperand_ParseFail;
  }

  const MCExpr *ImmVal;
  if (getParser().parseExpression(ImmVal) || !isa<MCConstantExpr>(ImmVal) ||
      cast<MCConstantExpr>(ImmVal)->getValue() != 0) {
    Error(getLoc(), "index must be absent or #0");
    return MatchOperand_ParseFail;
  }

  Operands.push_back(AArch64Operand::CreateReg(
      RegNum, RegKind::Scalar, StartLoc, getLoc(), getContext()));
  return MatchOperand_Success;
}

template <bool ParseShiftExtend, RegConstraintEqualityTy EqTy>
OperandMatchResultTy
AArch64AsmParser::tryParseGPROperand(OperandVector &Operands) {
  SMLoc StartLoc = getLoc();

  unsigned RegNum;
  OperandMatchResultTy Res = tryParseScalarRegister(RegNum);
  if (Res != MatchOperand_Success)
    return Res;

  // No shift/extend is the default.
  if (!ParseShiftExtend || getParser().getTok().isNot(AsmToken::Comma)) {
    Operands.push_back(AArch64Operand::CreateReg(
        RegNum, RegKind::Scalar, StartLoc, getLoc(), getContext(), EqTy));
    return MatchOperand_Success;
  }

  // Eat the comma
  getParser().Lex();

  // Match the shift
  SmallVector<std::unique_ptr<MCParsedAsmOperand>, 1> ExtOpnd;
  Res = tryParseOptionalShiftExtend(ExtOpnd);
  if (Res != MatchOperand_Success)
    return Res;

  auto Ext = static_cast<AArch64Operand*>(ExtOpnd.back().get());
  Operands.push_back(AArch64Operand::CreateReg(
      RegNum, RegKind::Scalar, StartLoc, Ext->getEndLoc(), getContext(), EqTy,
      Ext->getShiftExtendType(), Ext->getShiftExtendAmount(),
      Ext->hasShiftExtendAmount()));

  return MatchOperand_Success;
}

bool AArch64AsmParser::parseOptionalMulOperand(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();

  // Some SVE instructions have a decoration after the immediate, i.e.
  // "mul vl". We parse them here and add tokens, which must be present in the
  // asm string in the tablegen instruction.
  bool NextIsVL = Parser.getLexer().peekTok().getString().equals_lower("vl");
  bool NextIsHash = Parser.getLexer().peekTok().is(AsmToken::Hash);
  if (!Parser.getTok().getString().equals_lower("mul") ||
      !(NextIsVL || NextIsHash))
    return true;

  Operands.push_back(
    AArch64Operand::CreateToken("mul", false, getLoc(), getContext()));
  Parser.Lex(); // Eat the "mul"

  if (NextIsVL) {
    Operands.push_back(
        AArch64Operand::CreateToken("vl", false, getLoc(), getContext()));
    Parser.Lex(); // Eat the "vl"
    return false;
  }

  if (NextIsHash) {
    Parser.Lex(); // Eat the #
    SMLoc S = getLoc();

    // Parse immediate operand.
    const MCExpr *ImmVal;
    if (!Parser.parseExpression(ImmVal))
      if (const MCConstantExpr *MCE = dyn_cast<MCConstantExpr>(ImmVal)) {
        Operands.push_back(AArch64Operand::CreateImm(
            MCConstantExpr::create(MCE->getValue(), getContext()), S, getLoc(),
            getContext()));
        return MatchOperand_Success;
      }
  }

  return Error(getLoc(), "expected 'vl' or '#<imm>'");
}

/// parseOperand - Parse a arm instruction operand.  For now this parses the
/// operand regardless of the mnemonic.
bool AArch64AsmParser::parseOperand(OperandVector &Operands, bool isCondCode,
                                  bool invertCondCode) {
  MCAsmParser &Parser = getParser();

  OperandMatchResultTy ResTy =
      MatchOperandParserImpl(Operands, Mnemonic, /*ParseForAllFeatures=*/ true);

  // Check if the current operand has a custom associated parser, if so, try to
  // custom parse the operand, or fallback to the general approach.
  if (ResTy == MatchOperand_Success)
    return false;
  // If there wasn't a custom match, try the generic matcher below. Otherwise,
  // there was a match, but an error occurred, in which case, just return that
  // the operand parsing failed.
  if (ResTy == MatchOperand_ParseFail)
    return true;

  // Nothing custom, so do general case parsing.
  SMLoc S, E;
  switch (getLexer().getKind()) {
  default: {
    SMLoc S = getLoc();
    const MCExpr *Expr;
    if (parseSymbolicImmVal(Expr))
      return Error(S, "invalid operand");

    SMLoc E = SMLoc::getFromPointer(getLoc().getPointer() - 1);
    Operands.push_back(AArch64Operand::CreateImm(Expr, S, E, getContext()));
    return false;
  }
  case AsmToken::LBrac: {
    SMLoc Loc = Parser.getTok().getLoc();
    Operands.push_back(AArch64Operand::CreateToken("[", false, Loc,
                                                   getContext()));
    Parser.Lex(); // Eat '['

    // There's no comma after a '[', so we can parse the next operand
    // immediately.
    return parseOperand(Operands, false, false);
  }
  case AsmToken::LCurly:
    return parseNeonVectorList(Operands);
  case AsmToken::Identifier: {
    // If we're expecting a Condition Code operand, then just parse that.
    if (isCondCode)
      return parseCondCode(Operands, invertCondCode);

    // If it's a register name, parse it.
    if (!parseRegister(Operands))
      return false;

    // See if this is a "mul vl" decoration or "mul #<int>" operand used
    // by SVE instructions.
    if (!parseOptionalMulOperand(Operands))
      return false;

    // This could be an optional "shift" or "extend" operand.
    OperandMatchResultTy GotShift = tryParseOptionalShiftExtend(Operands);
    // We can only continue if no tokens were eaten.
    if (GotShift != MatchOperand_NoMatch)
      return GotShift;

    // This was not a register so parse other operands that start with an
    // identifier (like labels) as expressions and create them as immediates.
    const MCExpr *IdVal;
    S = getLoc();
    if (getParser().parseExpression(IdVal))
      return true;
    E = SMLoc::getFromPointer(getLoc().getPointer() - 1);
    Operands.push_back(AArch64Operand::CreateImm(IdVal, S, E, getContext()));
    return false;
  }
  case AsmToken::Integer:
  case AsmToken::Real:
  case AsmToken::Hash: {
    // #42 -> immediate.
    S = getLoc();

    parseOptionalToken(AsmToken::Hash);

    // Parse a negative sign
    bool isNegative = false;
    if (Parser.getTok().is(AsmToken::Minus)) {
      isNegative = true;
      // We need to consume this token only when we have a Real, otherwise
      // we let parseSymbolicImmVal take care of it
      if (Parser.getLexer().peekTok().is(AsmToken::Real))
        Parser.Lex();
    }

    // The only Real that should come through here is a literal #0.0 for
    // the fcmp[e] r, #0.0 instructions. They expect raw token operands,
    // so convert the value.
    const AsmToken &Tok = Parser.getTok();
    if (Tok.is(AsmToken::Real)) {
      APFloat RealVal(APFloat::IEEEdouble(), Tok.getString());
      uint64_t IntVal = RealVal.bitcastToAPInt().getZExtValue();
      if (Mnemonic != "fcmp" && Mnemonic != "fcmpe" && Mnemonic != "fcmeq" &&
          Mnemonic != "fcmge" && Mnemonic != "fcmgt" && Mnemonic != "fcmle" &&
          Mnemonic != "fcmlt" && Mnemonic != "fcmne")
        return TokError("unexpected floating point literal");
      else if (IntVal != 0 || isNegative)
        return TokError("expected floating-point constant #0.0");
      Parser.Lex(); // Eat the token.

      Operands.push_back(
          AArch64Operand::CreateToken("#0", false, S, getContext()));
      Operands.push_back(
          AArch64Operand::CreateToken(".0", false, S, getContext()));
      return false;
    }

    const MCExpr *ImmVal;
    if (parseSymbolicImmVal(ImmVal))
      return true;

    E = SMLoc::getFromPointer(getLoc().getPointer() - 1);
    Operands.push_back(AArch64Operand::CreateImm(ImmVal, S, E, getContext()));
    return false;
  }
  case AsmToken::Equal: {
    SMLoc Loc = getLoc();
    if (Mnemonic != "ldr") // only parse for ldr pseudo (e.g. ldr r0, =val)
      return TokError("unexpected token in operand");
    Parser.Lex(); // Eat '='
    const MCExpr *SubExprVal;
    if (getParser().parseExpression(SubExprVal))
      return true;

    if (Operands.size() < 2 ||
        !static_cast<AArch64Operand &>(*Operands[1]).isScalarReg())
      return Error(Loc, "Only valid when first operand is register");

    bool IsXReg =
        AArch64MCRegisterClasses[AArch64::GPR64allRegClassID].contains(
            Operands[1]->getReg());

    MCContext& Ctx = getContext();
    E = SMLoc::getFromPointer(Loc.getPointer() - 1);
    // If the op is an imm and can be fit into a mov, then replace ldr with mov.
    if (isa<MCConstantExpr>(SubExprVal)) {
      uint64_t Imm = (cast<MCConstantExpr>(SubExprVal))->getValue();
      uint32_t ShiftAmt = 0, MaxShiftAmt = IsXReg ? 48 : 16;
      while(Imm > 0xFFFF && countTrailingZeros(Imm) >= 16) {
        ShiftAmt += 16;
        Imm >>= 16;
      }
      if (ShiftAmt <= MaxShiftAmt && Imm <= 0xFFFF) {
          Operands[0] = AArch64Operand::CreateToken("movz", false, Loc, Ctx);
          Operands.push_back(AArch64Operand::CreateImm(
                     MCConstantExpr::create(Imm, Ctx), S, E, Ctx));
        if (ShiftAmt)
          Operands.push_back(AArch64Operand::CreateShiftExtend(AArch64_AM::LSL,
                     ShiftAmt, true, S, E, Ctx));
        return false;
      }
      APInt Simm = APInt(64, Imm << ShiftAmt);
      // check if the immediate is an unsigned or signed 32-bit int for W regs
      if (!IsXReg && !(Simm.isIntN(32) || Simm.isSignedIntN(32)))
        return Error(Loc, "Immediate too large for register");
    }
    // If it is a label or an imm that cannot fit in a movz, put it into CP.
    const MCExpr *CPLoc =
        getTargetStreamer().addConstantPoolEntry(SubExprVal, IsXReg ? 8 : 4, Loc);
    Operands.push_back(AArch64Operand::CreateImm(CPLoc, S, E, Ctx));
    return false;
  }
  }
}

bool AArch64AsmParser::regsEqual(const MCParsedAsmOperand &Op1,
                                 const MCParsedAsmOperand &Op2) const {
  auto &AOp1 = static_cast<const AArch64Operand&>(Op1);
  auto &AOp2 = static_cast<const AArch64Operand&>(Op2);
  if (AOp1.getRegEqualityTy() == RegConstraintEqualityTy::EqualsReg &&
      AOp2.getRegEqualityTy() == RegConstraintEqualityTy::EqualsReg)
    return MCTargetAsmParser::regsEqual(Op1, Op2);

  assert(AOp1.isScalarReg() && AOp2.isScalarReg() &&
         "Testing equality of non-scalar registers not supported");

  // Check if a registers match their sub/super register classes.
  if (AOp1.getRegEqualityTy() == EqualsSuperReg)
    return getXRegFromWReg(Op1.getReg()) == Op2.getReg();
  if (AOp1.getRegEqualityTy() == EqualsSubReg)
    return getWRegFromXReg(Op1.getReg()) == Op2.getReg();
  if (AOp2.getRegEqualityTy() == EqualsSuperReg)
    return getXRegFromWReg(Op2.getReg()) == Op1.getReg();
  if (AOp2.getRegEqualityTy() == EqualsSubReg)
    return getWRegFromXReg(Op2.getReg()) == Op1.getReg();

  return false;
}

/// ParseInstruction - Parse an AArch64 instruction mnemonic followed by its
/// operands.
bool AArch64AsmParser::ParseInstruction(ParseInstructionInfo &Info,
                                        StringRef Name, SMLoc NameLoc,
                                        OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  Name = StringSwitch<StringRef>(Name.lower())
             .Case("beq", "b.eq")
             .Case("bne", "b.ne")
             .Case("bhs", "b.hs")
             .Case("bcs", "b.cs")
             .Case("blo", "b.lo")
             .Case("bcc", "b.cc")
             .Case("bmi", "b.mi")
             .Case("bpl", "b.pl")
             .Case("bvs", "b.vs")
             .Case("bvc", "b.vc")
             .Case("bhi", "b.hi")
             .Case("bls", "b.ls")
             .Case("bge", "b.ge")
             .Case("blt", "b.lt")
             .Case("bgt", "b.gt")
             .Case("ble", "b.le")
             .Case("bal", "b.al")
             .Case("bnv", "b.nv")
             .Default(Name);

  // First check for the AArch64-specific .req directive.
  if (Parser.getTok().is(AsmToken::Identifier) &&
      Parser.getTok().getIdentifier() == ".req") {
    parseDirectiveReq(Name, NameLoc);
    // We always return 'error' for this, as we're done with this
    // statement and don't need to match the 'instruction."
    return true;
  }

  // Create the leading tokens for the mnemonic, split by '.' characters.
  size_t Start = 0, Next = Name.find('.');
  StringRef Head = Name.slice(Start, Next);

  // IC, DC, AT, TLBI and Prediction invalidation instructions are aliases for
  // the SYS instruction.
  if (Head == "ic" || Head == "dc" || Head == "at" || Head == "tlbi" ||
      Head == "cfp" || Head == "dvp" || Head == "cpp")
    return parseSysAlias(Head, NameLoc, Operands);

  Operands.push_back(
      AArch64Operand::CreateToken(Head, false, NameLoc, getContext()));
  Mnemonic = Head;

  // Handle condition codes for a branch mnemonic
  if (Head == "b" && Next != StringRef::npos) {
    Start = Next;
    Next = Name.find('.', Start + 1);
    Head = Name.slice(Start + 1, Next);

    SMLoc SuffixLoc = SMLoc::getFromPointer(NameLoc.getPointer() +
                                            (Head.data() - Name.data()));
    AArch64CC::CondCode CC = parseCondCodeString(Head);
    if (CC == AArch64CC::Invalid)
      return Error(SuffixLoc, "invalid condition code");
    Operands.push_back(
        AArch64Operand::CreateToken(".", true, SuffixLoc, getContext()));
    Operands.push_back(
        AArch64Operand::CreateCondCode(CC, NameLoc, NameLoc, getContext()));
  }

  // Add the remaining tokens in the mnemonic.
  while (Next != StringRef::npos) {
    Start = Next;
    Next = Name.find('.', Start + 1);
    Head = Name.slice(Start, Next);
    SMLoc SuffixLoc = SMLoc::getFromPointer(NameLoc.getPointer() +
                                            (Head.data() - Name.data()) + 1);
    Operands.push_back(
        AArch64Operand::CreateToken(Head, true, SuffixLoc, getContext()));
  }

  // Conditional compare instructions have a Condition Code operand, which needs
  // to be parsed and an immediate operand created.
  bool condCodeFourthOperand =
      (Head == "ccmp" || Head == "ccmn" || Head == "fccmp" ||
       Head == "fccmpe" || Head == "fcsel" || Head == "csel" ||
       Head == "csinc" || Head == "csinv" || Head == "csneg");

  // These instructions are aliases to some of the conditional select
  // instructions. However, the condition code is inverted in the aliased
  // instruction.
  //
  // FIXME: Is this the correct way to handle these? Or should the parser
  //        generate the aliased instructions directly?
  bool condCodeSecondOperand = (Head == "cset" || Head == "csetm");
  bool condCodeThirdOperand =
      (Head == "cinc" || Head == "cinv" || Head == "cneg");

  // Read the remaining operands.
  if (getLexer().isNot(AsmToken::EndOfStatement)) {

    unsigned N = 1;
    do {
      // Parse and remember the operand.
      if (parseOperand(Operands, (N == 4 && condCodeFourthOperand) ||
                                     (N == 3 && condCodeThirdOperand) ||
                                     (N == 2 && condCodeSecondOperand),
                       condCodeSecondOperand || condCodeThirdOperand)) {
        return true;
      }

      // After successfully parsing some operands there are two special cases to
      // consider (i.e. notional operands not separated by commas). Both are due
      // to memory specifiers:
      //  + An RBrac will end an address for load/store/prefetch
      //  + An '!' will indicate a pre-indexed operation.
      //
      // It's someone else's responsibility to make sure these tokens are sane
      // in the given context!

      SMLoc RLoc = Parser.getTok().getLoc();
      if (parseOptionalToken(AsmToken::RBrac))
        Operands.push_back(
            AArch64Operand::CreateToken("]", false, RLoc, getContext()));
      SMLoc ELoc = Parser.getTok().getLoc();
      if (parseOptionalToken(AsmToken::Exclaim))
        Operands.push_back(
            AArch64Operand::CreateToken("!", false, ELoc, getContext()));

      ++N;
    } while (parseOptionalToken(AsmToken::Comma));
  }

  if (parseToken(AsmToken::EndOfStatement, "unexpected token in argument list"))
    return true;

  return false;
}

static inline bool isMatchingOrAlias(unsigned ZReg, unsigned Reg) {
  assert((ZReg >= AArch64::Z0) && (ZReg <= AArch64::Z31));
  return (ZReg == ((Reg - AArch64::B0) + AArch64::Z0)) ||
         (ZReg == ((Reg - AArch64::H0) + AArch64::Z0)) ||
         (ZReg == ((Reg - AArch64::S0) + AArch64::Z0)) ||
         (ZReg == ((Reg - AArch64::D0) + AArch64::Z0)) ||
         (ZReg == ((Reg - AArch64::Q0) + AArch64::Z0)) ||
         (ZReg == ((Reg - AArch64::Z0) + AArch64::Z0));
}

// FIXME: This entire function is a giant hack to provide us with decent
// operand range validation/diagnostics until TableGen/MC can be extended
// to support autogeneration of this kind of validation.
bool AArch64AsmParser::validateInstruction(MCInst &Inst, SMLoc &IDLoc,
                                           SmallVectorImpl<SMLoc> &Loc) {
  const MCRegisterInfo *RI = getContext().getRegisterInfo();
  const MCInstrDesc &MCID = MII.get(Inst.getOpcode());

  // A prefix only applies to the instruction following it.  Here we extract
  // prefix information for the next instruction before validating the current
  // one so that in the case of failure we don't erronously continue using the
  // current prefix.
  PrefixInfo Prefix = NextPrefix;
  NextPrefix = PrefixInfo::CreateFromInst(Inst, MCID.TSFlags);

  // Before validating the instruction in isolation we run through the rules
  // applicable when it follows a prefix instruction.
  // NOTE: brk & hlt can be prefixed but require no additional validation.
  if (Prefix.isActive() &&
      (Inst.getOpcode() != AArch64::BRK) &&
      (Inst.getOpcode() != AArch64::HLT)) {

    // Prefixed intructions must have a destructive operand.
    if ((MCID.TSFlags & AArch64::DestructiveInstTypeMask) ==
        AArch64::NotDestructive)
      return Error(IDLoc, "instruction is unpredictable when following a"
                   " movprfx, suggest replacing movprfx with mov");

    // Destination operands must match.
    if (Inst.getOperand(0).getReg() != Prefix.getDstReg())
      return Error(Loc[0], "instruction is unpredictable when following a"
                   " movprfx writing to a different destination");

    // Destination operand must not be used in any other location.
    for (unsigned i = 1; i < Inst.getNumOperands(); ++i) {
      if (Inst.getOperand(i).isReg() &&
          (MCID.getOperandConstraint(i, MCOI::TIED_TO) == -1) &&
          isMatchingOrAlias(Prefix.getDstReg(), Inst.getOperand(i).getReg()))
        return Error(Loc[0], "instruction is unpredictable when following a"
                     " movprfx and destination also used as non-destructive"
                     " source");
    }

    auto PPRRegClass = AArch64MCRegisterClasses[AArch64::PPRRegClassID];
    if (Prefix.isPredicated()) {
      int PgIdx = -1;

      // Find the instructions general predicate.
      for (unsigned i = 1; i < Inst.getNumOperands(); ++i)
        if (Inst.getOperand(i).isReg() &&
            PPRRegClass.contains(Inst.getOperand(i).getReg())) {
          PgIdx = i;
          break;
        }

      // Instruction must be predicated if the movprfx is predicated.
      if (PgIdx == -1 ||
          (MCID.TSFlags & AArch64::ElementSizeMask) == AArch64::ElementSizeNone)
        return Error(IDLoc, "instruction is unpredictable when following a"
                     " predicated movprfx, suggest using unpredicated movprfx");

      // Instruction must use same general predicate as the movprfx.
      if (Inst.getOperand(PgIdx).getReg() != Prefix.getPgReg())
        return Error(IDLoc, "instruction is unpredictable when following a"
                     " predicated movprfx using a different general predicate");

      // Instruction element type must match the movprfx.
      if ((MCID.TSFlags & AArch64::ElementSizeMask) != Prefix.getElementSize())
        return Error(IDLoc, "instruction is unpredictable when following a"
                     " predicated movprfx with a different element size");
    }
  }

  // Check for indexed addressing modes w/ the base register being the
  // same as a destination/source register or pair load where
  // the Rt == Rt2. All of those are undefined behaviour.
  switch (Inst.getOpcode()) {
  case AArch64::LDPSWpre:
  case AArch64::LDPWpost:
  case AArch64::LDPWpre:
  case AArch64::LDPXpost:
  case AArch64::LDPXpre: {
    unsigned Rt = Inst.getOperand(1).getReg();
    unsigned Rt2 = Inst.getOperand(2).getReg();
    unsigned Rn = Inst.getOperand(3).getReg();
    if (RI->isSubRegisterEq(Rn, Rt))
      return Error(Loc[0], "unpredictable LDP instruction, writeback base "
                           "is also a destination");
    if (RI->isSubRegisterEq(Rn, Rt2))
      return Error(Loc[1], "unpredictable LDP instruction, writeback base "
                           "is also a destination");
    LLVM_FALLTHROUGH;
  }
  case AArch64::LDPDi:
  case AArch64::LDPQi:
  case AArch64::LDPSi:
  case AArch64::LDPSWi:
  case AArch64::LDPWi:
  case AArch64::LDPXi: {
    unsigned Rt = Inst.getOperand(0).getReg();
    unsigned Rt2 = Inst.getOperand(1).getReg();
    if (Rt == Rt2)
      return Error(Loc[1], "unpredictable LDP instruction, Rt2==Rt");
    break;
  }
  case AArch64::LDPDpost:
  case AArch64::LDPDpre:
  case AArch64::LDPQpost:
  case AArch64::LDPQpre:
  case AArch64::LDPSpost:
  case AArch64::LDPSpre:
  case AArch64::LDPSWpost: {
    unsigned Rt = Inst.getOperand(1).getReg();
    unsigned Rt2 = Inst.getOperand(2).getReg();
    if (Rt == Rt2)
      return Error(Loc[1], "unpredictable LDP instruction, Rt2==Rt");
    break;
  }
  case AArch64::STPDpost:
  case AArch64::STPDpre:
  case AArch64::STPQpost:
  case AArch64::STPQpre:
  case AArch64::STPSpost:
  case AArch64::STPSpre:
  case AArch64::STPWpost:
  case AArch64::STPWpre:
  case AArch64::STPXpost:
  case AArch64::STPXpre: {
    unsigned Rt = Inst.getOperand(1).getReg();
    unsigned Rt2 = Inst.getOperand(2).getReg();
    unsigned Rn = Inst.getOperand(3).getReg();
    if (RI->isSubRegisterEq(Rn, Rt))
      return Error(Loc[0], "unpredictable STP instruction, writeback base "
                           "is also a source");
    if (RI->isSubRegisterEq(Rn, Rt2))
      return Error(Loc[1], "unpredictable STP instruction, writeback base "
                           "is also a source");
    break;
  }
  case AArch64::LDRBBpre:
  case AArch64::LDRBpre:
  case AArch64::LDRHHpre:
  case AArch64::LDRHpre:
  case AArch64::LDRSBWpre:
  case AArch64::LDRSBXpre:
  case AArch64::LDRSHWpre:
  case AArch64::LDRSHXpre:
  case AArch64::LDRSWpre:
  case AArch64::LDRWpre:
  case AArch64::LDRXpre:
  case AArch64::LDRBBpost:
  case AArch64::LDRBpost:
  case AArch64::LDRHHpost:
  case AArch64::LDRHpost:
  case AArch64::LDRSBWpost:
  case AArch64::LDRSBXpost:
  case AArch64::LDRSHWpost:
  case AArch64::LDRSHXpost:
  case AArch64::LDRSWpost:
  case AArch64::LDRWpost:
  case AArch64::LDRXpost: {
    unsigned Rt = Inst.getOperand(1).getReg();
    unsigned Rn = Inst.getOperand(2).getReg();
    if (RI->isSubRegisterEq(Rn, Rt))
      return Error(Loc[0], "unpredictable LDR instruction, writeback base "
                           "is also a source");
    break;
  }
  case AArch64::STRBBpost:
  case AArch64::STRBpost:
  case AArch64::STRHHpost:
  case AArch64::STRHpost:
  case AArch64::STRWpost:
  case AArch64::STRXpost:
  case AArch64::STRBBpre:
  case AArch64::STRBpre:
  case AArch64::STRHHpre:
  case AArch64::STRHpre:
  case AArch64::STRWpre:
  case AArch64::STRXpre: {
    unsigned Rt = Inst.getOperand(1).getReg();
    unsigned Rn = Inst.getOperand(2).getReg();
    if (RI->isSubRegisterEq(Rn, Rt))
      return Error(Loc[0], "unpredictable STR instruction, writeback base "
                           "is also a source");
    break;
  }
  case AArch64::STXRB:
  case AArch64::STXRH:
  case AArch64::STXRW:
  case AArch64::STXRX:
  case AArch64::STLXRB:
  case AArch64::STLXRH:
  case AArch64::STLXRW:
  case AArch64::STLXRX: {
    unsigned Rs = Inst.getOperand(0).getReg();
    unsigned Rt = Inst.getOperand(1).getReg();
    unsigned Rn = Inst.getOperand(2).getReg();
    if (RI->isSubRegisterEq(Rt, Rs) ||
        (RI->isSubRegisterEq(Rn, Rs) && Rn != AArch64::SP))
      return Error(Loc[0],
                   "unpredictable STXR instruction, status is also a source");
    break;
  }
  case AArch64::STXPW:
  case AArch64::STXPX:
  case AArch64::STLXPW:
  case AArch64::STLXPX: {
    unsigned Rs = Inst.getOperand(0).getReg();
    unsigned Rt1 = Inst.getOperand(1).getReg();
    unsigned Rt2 = Inst.getOperand(2).getReg();
    unsigned Rn = Inst.getOperand(3).getReg();
    if (RI->isSubRegisterEq(Rt1, Rs) || RI->isSubRegisterEq(Rt2, Rs) ||
        (RI->isSubRegisterEq(Rn, Rs) && Rn != AArch64::SP))
      return Error(Loc[0],
                   "unpredictable STXP instruction, status is also a source");
    break;
  }
  case AArch64::LDGV: {
    unsigned Rt = Inst.getOperand(0).getReg();
    unsigned Rn = Inst.getOperand(1).getReg();
    if (RI->isSubRegisterEq(Rt, Rn)) {
      return Error(Loc[0],
                  "unpredictable LDGV instruction, writeback register is also "
                  "the target register");
    }
  }
  }


  // Now check immediate ranges. Separate from the above as there is overlap
  // in the instructions being checked and this keeps the nested conditionals
  // to a minimum.
  switch (Inst.getOpcode()) {
  case AArch64::ADDSWri:
  case AArch64::ADDSXri:
  case AArch64::ADDWri:
  case AArch64::ADDXri:
  case AArch64::SUBSWri:
  case AArch64::SUBSXri:
  case AArch64::SUBWri:
  case AArch64::SUBXri: {
    // Annoyingly we can't do this in the isAddSubImm predicate, so there is
    // some slight duplication here.
    if (Inst.getOperand(2).isExpr()) {
      const MCExpr *Expr = Inst.getOperand(2).getExpr();
      AArch64MCExpr::VariantKind ELFRefKind;
      MCSymbolRefExpr::VariantKind DarwinRefKind;
      int64_t Addend;
      if (classifySymbolRef(Expr, ELFRefKind, DarwinRefKind, Addend)) {

        // Only allow these with ADDXri.
        if ((DarwinRefKind == MCSymbolRefExpr::VK_PAGEOFF ||
             DarwinRefKind == MCSymbolRefExpr::VK_TLVPPAGEOFF) &&
            Inst.getOpcode() == AArch64::ADDXri)
          return false;

        // Only allow these with ADDXri/ADDWri
        if ((ELFRefKind == AArch64MCExpr::VK_LO12 ||
             ELFRefKind == AArch64MCExpr::VK_DTPREL_HI12 ||
             ELFRefKind == AArch64MCExpr::VK_DTPREL_LO12 ||
             ELFRefKind == AArch64MCExpr::VK_DTPREL_LO12_NC ||
             ELFRefKind == AArch64MCExpr::VK_TPREL_HI12 ||
             ELFRefKind == AArch64MCExpr::VK_TPREL_LO12 ||
             ELFRefKind == AArch64MCExpr::VK_TPREL_LO12_NC ||
             ELFRefKind == AArch64MCExpr::VK_TLSDESC_LO12 ||
             ELFRefKind == AArch64MCExpr::VK_SECREL_LO12 ||
             ELFRefKind == AArch64MCExpr::VK_SECREL_HI12) &&
            (Inst.getOpcode() == AArch64::ADDXri ||
             Inst.getOpcode() == AArch64::ADDWri))
          return false;

        // Don't allow symbol refs in the immediate field otherwise
        // Note: Loc.back() may be Loc[1] or Loc[2] depending on the number of
        // operands of the original instruction (i.e. 'add w0, w1, borked' vs
        // 'cmp w0, 'borked')
        return Error(Loc.back(), "invalid immediate expression");
      }
      // We don't validate more complex expressions here
    }
    return false;
  }
  default:
    return false;
  }
}

static std::string AArch64MnemonicSpellCheck(StringRef S, uint64_t FBS,
                                             unsigned VariantID = 0);

bool AArch64AsmParser::showMatchError(SMLoc Loc, unsigned ErrCode,
                                      uint64_t ErrorInfo,
                                      OperandVector &Operands) {
  switch (ErrCode) {
  case Match_InvalidTiedOperand: {
    RegConstraintEqualityTy EqTy =
        static_cast<const AArch64Operand &>(*Operands[ErrorInfo])
            .getRegEqualityTy();
    switch (EqTy) {
    case RegConstraintEqualityTy::EqualsSubReg:
      return Error(Loc, "operand must be 64-bit form of destination register");
    case RegConstraintEqualityTy::EqualsSuperReg:
      return Error(Loc, "operand must be 32-bit form of destination register");
    case RegConstraintEqualityTy::EqualsReg:
      return Error(Loc, "operand must match destination register");
    }
    llvm_unreachable("Unknown RegConstraintEqualityTy");
  }
  case Match_MissingFeature:
    return Error(Loc,
                 "instruction requires a CPU feature not currently enabled");
  case Match_InvalidOperand:
    return Error(Loc, "invalid operand for instruction");
  case Match_InvalidSuffix:
    return Error(Loc, "invalid type suffix for instruction");
  case Match_InvalidCondCode:
    return Error(Loc, "expected AArch64 condition code");
  case Match_AddSubRegExtendSmall:
    return Error(Loc,
      "expected '[su]xt[bhw]' or 'lsl' with optional integer in range [0, 4]");
  case Match_AddSubRegExtendLarge:
    return Error(Loc,
      "expected 'sxtx' 'uxtx' or 'lsl' with optional integer in range [0, 4]");
  case Match_AddSubSecondSource:
    return Error(Loc,
      "expected compatible register, symbol or integer in range [0, 4095]");
  case Match_LogicalSecondSource:
    return Error(Loc, "expected compatible register or logical immediate");
  case Match_InvalidMovImm32Shift:
    return Error(Loc, "expected 'lsl' with optional integer 0 or 16");
  case Match_InvalidMovImm64Shift:
    return Error(Loc, "expected 'lsl' with optional integer 0, 16, 32 or 48");
  case Match_AddSubRegShift32:
    return Error(Loc,
       "expected 'lsl', 'lsr' or 'asr' with optional integer in range [0, 31]");
  case Match_AddSubRegShift64:
    return Error(Loc,
       "expected 'lsl', 'lsr' or 'asr' with optional integer in range [0, 63]");
  case Match_InvalidFPImm:
    return Error(Loc,
                 "expected compatible register or floating-point constant");
  case Match_InvalidMemoryIndexedSImm6:
    return Error(Loc, "index must be an integer in range [-32, 31].");
  case Match_InvalidMemoryIndexedSImm5:
    return Error(Loc, "index must be an integer in range [-16, 15].");
  case Match_InvalidMemoryIndexed1SImm4:
    return Error(Loc, "index must be an integer in range [-8, 7].");
  case Match_InvalidMemoryIndexed2SImm4:
    return Error(Loc, "index must be a multiple of 2 in range [-16, 14].");
  case Match_InvalidMemoryIndexed3SImm4:
    return Error(Loc, "index must be a multiple of 3 in range [-24, 21].");
  case Match_InvalidMemoryIndexed4SImm4:
    return Error(Loc, "index must be a multiple of 4 in range [-32, 28].");
  case Match_InvalidMemoryIndexed16SImm4:
    return Error(Loc, "index must be a multiple of 16 in range [-128, 112].");
  case Match_InvalidMemoryIndexed1SImm6:
    return Error(Loc, "index must be an integer in range [-32, 31].");
  case Match_InvalidMemoryIndexedSImm8:
    return Error(Loc, "index must be an integer in range [-128, 127].");
  case Match_InvalidMemoryIndexedSImm9:
    return Error(Loc, "index must be an integer in range [-256, 255].");
  case Match_InvalidMemoryIndexed16SImm9:
    return Error(Loc, "index must be a multiple of 16 in range [-4096, 4080].");
  case Match_InvalidMemoryIndexed8SImm10:
    return Error(Loc, "index must be a multiple of 8 in range [-4096, 4088].");
  case Match_InvalidMemoryIndexed4SImm7:
    return Error(Loc, "index must be a multiple of 4 in range [-256, 252].");
  case Match_InvalidMemoryIndexed8SImm7:
    return Error(Loc, "index must be a multiple of 8 in range [-512, 504].");
  case Match_InvalidMemoryIndexed16SImm7:
    return Error(Loc, "index must be a multiple of 16 in range [-1024, 1008].");
  case Match_InvalidMemoryIndexed8UImm5:
    return Error(Loc, "index must be a multiple of 8 in range [0, 248].");
  case Match_InvalidMemoryIndexed4UImm5:
    return Error(Loc, "index must be a multiple of 4 in range [0, 124].");
  case Match_InvalidMemoryIndexed2UImm5:
    return Error(Loc, "index must be a multiple of 2 in range [0, 62].");
  case Match_InvalidMemoryIndexed8UImm6:
    return Error(Loc, "index must be a multiple of 8 in range [0, 504].");
  case Match_InvalidMemoryIndexed16UImm6:
    return Error(Loc, "index must be a multiple of 16 in range [0, 1008].");
  case Match_InvalidMemoryIndexed4UImm6:
    return Error(Loc, "index must be a multiple of 4 in range [0, 252].");
  case Match_InvalidMemoryIndexed2UImm6:
    return Error(Loc, "index must be a multiple of 2 in range [0, 126].");
  case Match_InvalidMemoryIndexed1UImm6:
    return Error(Loc, "index must be in range [0, 63].");
  case Match_InvalidMemoryWExtend8:
    return Error(Loc,
                 "expected 'uxtw' or 'sxtw' with optional shift of #0");
  case Match_InvalidMemoryWExtend16:
    return Error(Loc,
                 "expected 'uxtw' or 'sxtw' with optional shift of #0 or #1");
  case Match_InvalidMemoryWExtend32:
    return Error(Loc,
                 "expected 'uxtw' or 'sxtw' with optional shift of #0 or #2");
  case Match_InvalidMemoryWExtend64:
    return Error(Loc,
                 "expected 'uxtw' or 'sxtw' with optional shift of #0 or #3");
  case Match_InvalidMemoryWExtend128:
    return Error(Loc,
                 "expected 'uxtw' or 'sxtw' with optional shift of #0 or #4");
  case Match_InvalidMemoryXExtend8:
    return Error(Loc,
                 "expected 'lsl' or 'sxtx' with optional shift of #0");
  case Match_InvalidMemoryXExtend16:
    return Error(Loc,
                 "expected 'lsl' or 'sxtx' with optional shift of #0 or #1");
  case Match_InvalidMemoryXExtend32:
    return Error(Loc,
                 "expected 'lsl' or 'sxtx' with optional shift of #0 or #2");
  case Match_InvalidMemoryXExtend64:
    return Error(Loc,
                 "expected 'lsl' or 'sxtx' with optional shift of #0 or #3");
  case Match_InvalidMemoryXExtend128:
    return Error(Loc,
                 "expected 'lsl' or 'sxtx' with optional shift of #0 or #4");
  case Match_InvalidMemoryIndexed1:
    return Error(Loc, "index must be an integer in range [0, 4095].");
  case Match_InvalidMemoryIndexed2:
    return Error(Loc, "index must be a multiple of 2 in range [0, 8190].");
  case Match_InvalidMemoryIndexed4:
    return Error(Loc, "index must be a multiple of 4 in range [0, 16380].");
  case Match_InvalidMemoryIndexed8:
    return Error(Loc, "index must be a multiple of 8 in range [0, 32760].");
  case Match_InvalidMemoryIndexed16:
    return Error(Loc, "index must be a multiple of 16 in range [0, 65520].");
  case Match_InvalidImm0_1:
    return Error(Loc, "immediate must be an integer in range [0, 1].");
  case Match_InvalidImm0_7:
    return Error(Loc, "immediate must be an integer in range [0, 7].");
  case Match_InvalidImm0_15:
    return Error(Loc, "immediate must be an integer in range [0, 15].");
  case Match_InvalidImm0_31:
    return Error(Loc, "immediate must be an integer in range [0, 31].");
  case Match_InvalidImm0_63:
    return Error(Loc, "immediate must be an integer in range [0, 63].");
  case Match_InvalidImm0_127:
    return Error(Loc, "immediate must be an integer in range [0, 127].");
  case Match_InvalidImm0_255:
    return Error(Loc, "immediate must be an integer in range [0, 255].");
  case Match_InvalidImm0_65535:
    return Error(Loc, "immediate must be an integer in range [0, 65535].");
  case Match_InvalidImm1_8:
    return Error(Loc, "immediate must be an integer in range [1, 8].");
  case Match_InvalidImm1_16:
    return Error(Loc, "immediate must be an integer in range [1, 16].");
  case Match_InvalidImm1_32:
    return Error(Loc, "immediate must be an integer in range [1, 32].");
  case Match_InvalidImm1_64:
    return Error(Loc, "immediate must be an integer in range [1, 64].");
  case Match_InvalidSVEAddSubImm8:
    return Error(Loc, "immediate must be an integer in range [0, 255]"
                      " with a shift amount of 0");
  case Match_InvalidSVEAddSubImm16:
  case Match_InvalidSVEAddSubImm32:
  case Match_InvalidSVEAddSubImm64:
    return Error(Loc, "immediate must be an integer in range [0, 255] or a "
                      "multiple of 256 in range [256, 65280]");
  case Match_InvalidSVECpyImm8:
    return Error(Loc, "immediate must be an integer in range [-128, 255]"
                      " with a shift amount of 0");
  case Match_InvalidSVECpyImm16:
    return Error(Loc, "immediate must be an integer in range [-128, 127] or a "
                      "multiple of 256 in range [-32768, 65280]");
  case Match_InvalidSVECpyImm32:
  case Match_InvalidSVECpyImm64:
    return Error(Loc, "immediate must be an integer in range [-128, 127] or a "
                      "multiple of 256 in range [-32768, 32512]");
  case Match_InvalidIndexRange1_1:
    return Error(Loc, "expected lane specifier '[1]'");
  case Match_InvalidIndexRange0_15:
    return Error(Loc, "vector lane must be an integer in range [0, 15].");
  case Match_InvalidIndexRange0_7:
    return Error(Loc, "vector lane must be an integer in range [0, 7].");
  case Match_InvalidIndexRange0_3:
    return Error(Loc, "vector lane must be an integer in range [0, 3].");
  case Match_InvalidIndexRange0_1:
    return Error(Loc, "vector lane must be an integer in range [0, 1].");
  case Match_InvalidSVEIndexRange0_63:
    return Error(Loc, "vector lane must be an integer in range [0, 63].");
  case Match_InvalidSVEIndexRange0_31:
    return Error(Loc, "vector lane must be an integer in range [0, 31].");
  case Match_InvalidSVEIndexRange0_15:
    return Error(Loc, "vector lane must be an integer in range [0, 15].");
  case Match_InvalidSVEIndexRange0_7:
    return Error(Loc, "vector lane must be an integer in range [0, 7].");
  case Match_InvalidSVEIndexRange0_3:
    return Error(Loc, "vector lane must be an integer in range [0, 3].");
  case Match_InvalidLabel:
    return Error(Loc, "expected label or encodable integer pc offset");
  case Match_MRS:
    return Error(Loc, "expected readable system register");
  case Match_MSR:
    return Error(Loc, "expected writable system register or pstate");
  case Match_InvalidComplexRotationEven:
    return Error(Loc, "complex rotation must be 0, 90, 180 or 270.");
  case Match_InvalidComplexRotationOdd:
    return Error(Loc, "complex rotation must be 90 or 270.");
  case Match_MnemonicFail: {
    std::string Suggestion = AArch64MnemonicSpellCheck(
        ((AArch64Operand &)*Operands[0]).getToken(),
        ComputeAvailableFeatures(STI->getFeatureBits()));
    return Error(Loc, "unrecognized instruction mnemonic" + Suggestion);
  }
  case Match_InvalidGPR64shifted8:
    return Error(Loc, "register must be x0..x30 or xzr, without shift");
  case Match_InvalidGPR64shifted16:
    return Error(Loc, "register must be x0..x30 or xzr, with required shift 'lsl #1'");
  case Match_InvalidGPR64shifted32:
    return Error(Loc, "register must be x0..x30 or xzr, with required shift 'lsl #2'");
  case Match_InvalidGPR64shifted64:
    return Error(Loc, "register must be x0..x30 or xzr, with required shift 'lsl #3'");
  case Match_InvalidGPR64NoXZRshifted8:
    return Error(Loc, "register must be x0..x30 without shift");
  case Match_InvalidGPR64NoXZRshifted16:
    return Error(Loc, "register must be x0..x30 with required shift 'lsl #1'");
  case Match_InvalidGPR64NoXZRshifted32:
    return Error(Loc, "register must be x0..x30 with required shift 'lsl #2'");
  case Match_InvalidGPR64NoXZRshifted64:
    return Error(Loc, "register must be x0..x30 with required shift 'lsl #3'");
  case Match_InvalidZPR32UXTW8:
  case Match_InvalidZPR32SXTW8:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].s, (uxtw|sxtw)'");
  case Match_InvalidZPR32UXTW16:
  case Match_InvalidZPR32SXTW16:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].s, (uxtw|sxtw) #1'");
  case Match_InvalidZPR32UXTW32:
  case Match_InvalidZPR32SXTW32:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].s, (uxtw|sxtw) #2'");
  case Match_InvalidZPR32UXTW64:
  case Match_InvalidZPR32SXTW64:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].s, (uxtw|sxtw) #3'");
  case Match_InvalidZPR64UXTW8:
  case Match_InvalidZPR64SXTW8:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].d, (uxtw|sxtw)'");
  case Match_InvalidZPR64UXTW16:
  case Match_InvalidZPR64SXTW16:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].d, (lsl|uxtw|sxtw) #1'");
  case Match_InvalidZPR64UXTW32:
  case Match_InvalidZPR64SXTW32:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].d, (lsl|uxtw|sxtw) #2'");
  case Match_InvalidZPR64UXTW64:
  case Match_InvalidZPR64SXTW64:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].d, (lsl|uxtw|sxtw) #3'");
  case Match_InvalidZPR32LSL8:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].s'");
  case Match_InvalidZPR32LSL16:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].s, lsl #1'");
  case Match_InvalidZPR32LSL32:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].s, lsl #2'");
  case Match_InvalidZPR32LSL64:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].s, lsl #3'");
  case Match_InvalidZPR64LSL8:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].d'");
  case Match_InvalidZPR64LSL16:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].d, lsl #1'");
  case Match_InvalidZPR64LSL32:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].d, lsl #2'");
  case Match_InvalidZPR64LSL64:
    return Error(Loc, "invalid shift/extend specified, expected 'z[0..31].d, lsl #3'");
  case Match_InvalidZPR0:
    return Error(Loc, "expected register without element width sufix");
  case Match_InvalidZPR8:
  case Match_InvalidZPR16:
  case Match_InvalidZPR32:
  case Match_InvalidZPR64:
  case Match_InvalidZPR128:
    return Error(Loc, "invalid element width");
  case Match_InvalidZPR_3b8:
    return Error(Loc, "Invalid restricted vector register, expected z0.b..z7.b");
  case Match_InvalidZPR_3b16:
    return Error(Loc, "Invalid restricted vector register, expected z0.h..z7.h");
  case Match_InvalidZPR_3b32:
    return Error(Loc, "Invalid restricted vector register, expected z0.s..z7.s");
  case Match_InvalidZPR_4b16:
    return Error(Loc, "Invalid restricted vector register, expected z0.h..z15.h");
  case Match_InvalidZPR_4b32:
    return Error(Loc, "Invalid restricted vector register, expected z0.s..z15.s");
  case Match_InvalidZPR_4b64:
    return Error(Loc, "Invalid restricted vector register, expected z0.d..z15.d");
  case Match_InvalidSVEPattern:
    return Error(Loc, "invalid predicate pattern");
  case Match_InvalidSVEPredicateAnyReg:
  case Match_InvalidSVEPredicateBReg:
  case Match_InvalidSVEPredicateHReg:
  case Match_InvalidSVEPredicateSReg:
  case Match_InvalidSVEPredicateDReg:
    return Error(Loc, "invalid predicate register.");
  case Match_InvalidSVEPredicate3bAnyReg:
  case Match_InvalidSVEPredicate3bBReg:
  case Match_InvalidSVEPredicate3bHReg:
  case Match_InvalidSVEPredicate3bSReg:
  case Match_InvalidSVEPredicate3bDReg:
    return Error(Loc, "restricted predicate has range [0, 7].");
  case Match_InvalidSVEExactFPImmOperandHalfOne:
    return Error(Loc, "Invalid floating point constant, expected 0.5 or 1.0.");
  case Match_InvalidSVEExactFPImmOperandHalfTwo:
    return Error(Loc, "Invalid floating point constant, expected 0.5 or 2.0.");
  case Match_InvalidSVEExactFPImmOperandZeroOne:
    return Error(Loc, "Invalid floating point constant, expected 0.0 or 1.0.");
  default:
    llvm_unreachable("unexpected error code!");
  }
}

static const char *getSubtargetFeatureName(uint64_t Val);

bool AArch64AsmParser::MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                               OperandVector &Operands,
                                               MCStreamer &Out,
                                               uint64_t &ErrorInfo,
                                               bool MatchingInlineAsm) {
  assert(!Operands.empty() && "Unexpect empty operand list!");
  AArch64Operand &Op = static_cast<AArch64Operand &>(*Operands[0]);
  assert(Op.isToken() && "Leading operand should always be a mnemonic!");

  StringRef Tok = Op.getToken();
  unsigned NumOperands = Operands.size();

  if (NumOperands == 4 && Tok == "lsl") {
    AArch64Operand &Op2 = static_cast<AArch64Operand &>(*Operands[2]);
    AArch64Operand &Op3 = static_cast<AArch64Operand &>(*Operands[3]);
    if (Op2.isScalarReg() && Op3.isImm()) {
      const MCConstantExpr *Op3CE = dyn_cast<MCConstantExpr>(Op3.getImm());
      if (Op3CE) {
        uint64_t Op3Val = Op3CE->getValue();
        uint64_t NewOp3Val = 0;
        uint64_t NewOp4Val = 0;
        if (AArch64MCRegisterClasses[AArch64::GPR32allRegClassID].contains(
                Op2.getReg())) {
          NewOp3Val = (32 - Op3Val) & 0x1f;
          NewOp4Val = 31 - Op3Val;
        } else {
          NewOp3Val = (64 - Op3Val) & 0x3f;
          NewOp4Val = 63 - Op3Val;
        }

        const MCExpr *NewOp3 = MCConstantExpr::create(NewOp3Val, getContext());
        const MCExpr *NewOp4 = MCConstantExpr::create(NewOp4Val, getContext());

        Operands[0] = AArch64Operand::CreateToken(
            "ubfm", false, Op.getStartLoc(), getContext());
        Operands.push_back(AArch64Operand::CreateImm(
            NewOp4, Op3.getStartLoc(), Op3.getEndLoc(), getContext()));
        Operands[3] = AArch64Operand::CreateImm(NewOp3, Op3.getStartLoc(),
                                                Op3.getEndLoc(), getContext());
      }
    }
  } else if (NumOperands == 4 && Tok == "bfc") {
    // FIXME: Horrible hack to handle BFC->BFM alias.
    AArch64Operand &Op1 = static_cast<AArch64Operand &>(*Operands[1]);
    AArch64Operand LSBOp = static_cast<AArch64Operand &>(*Operands[2]);
    AArch64Operand WidthOp = static_cast<AArch64Operand &>(*Operands[3]);

    if (Op1.isScalarReg() && LSBOp.isImm() && WidthOp.isImm()) {
      const MCConstantExpr *LSBCE = dyn_cast<MCConstantExpr>(LSBOp.getImm());
      const MCConstantExpr *WidthCE = dyn_cast<MCConstantExpr>(WidthOp.getImm());

      if (LSBCE && WidthCE) {
        uint64_t LSB = LSBCE->getValue();
        uint64_t Width = WidthCE->getValue();

        uint64_t RegWidth = 0;
        if (AArch64MCRegisterClasses[AArch64::GPR64allRegClassID].contains(
                Op1.getReg()))
          RegWidth = 64;
        else
          RegWidth = 32;

        if (LSB >= RegWidth)
          return Error(LSBOp.getStartLoc(),
                       "expected integer in range [0, 31]");
        if (Width < 1 || Width > RegWidth)
          return Error(WidthOp.getStartLoc(),
                       "expected integer in range [1, 32]");

        uint64_t ImmR = 0;
        if (RegWidth == 32)
          ImmR = (32 - LSB) & 0x1f;
        else
          ImmR = (64 - LSB) & 0x3f;

        uint64_t ImmS = Width - 1;

        if (ImmR != 0 && ImmS >= ImmR)
          return Error(WidthOp.getStartLoc(),
                       "requested insert overflows register");

        const MCExpr *ImmRExpr = MCConstantExpr::create(ImmR, getContext());
        const MCExpr *ImmSExpr = MCConstantExpr::create(ImmS, getContext());
        Operands[0] = AArch64Operand::CreateToken(
              "bfm", false, Op.getStartLoc(), getContext());
        Operands[2] = AArch64Operand::CreateReg(
            RegWidth == 32 ? AArch64::WZR : AArch64::XZR, RegKind::Scalar,
            SMLoc(), SMLoc(), getContext());
        Operands[3] = AArch64Operand::CreateImm(
            ImmRExpr, LSBOp.getStartLoc(), LSBOp.getEndLoc(), getContext());
        Operands.emplace_back(
            AArch64Operand::CreateImm(ImmSExpr, WidthOp.getStartLoc(),
                                      WidthOp.getEndLoc(), getContext()));
      }
    }
  } else if (NumOperands == 5) {
    // FIXME: Horrible hack to handle the BFI -> BFM, SBFIZ->SBFM, and
    // UBFIZ -> UBFM aliases.
    if (Tok == "bfi" || Tok == "sbfiz" || Tok == "ubfiz") {
      AArch64Operand &Op1 = static_cast<AArch64Operand &>(*Operands[1]);
      AArch64Operand &Op3 = static_cast<AArch64Operand &>(*Operands[3]);
      AArch64Operand &Op4 = static_cast<AArch64Operand &>(*Operands[4]);

      if (Op1.isScalarReg() && Op3.isImm() && Op4.isImm()) {
        const MCConstantExpr *Op3CE = dyn_cast<MCConstantExpr>(Op3.getImm());
        const MCConstantExpr *Op4CE = dyn_cast<MCConstantExpr>(Op4.getImm());

        if (Op3CE && Op4CE) {
          uint64_t Op3Val = Op3CE->getValue();
          uint64_t Op4Val = Op4CE->getValue();

          uint64_t RegWidth = 0;
          if (AArch64MCRegisterClasses[AArch64::GPR64allRegClassID].contains(
                  Op1.getReg()))
            RegWidth = 64;
          else
            RegWidth = 32;

          if (Op3Val >= RegWidth)
            return Error(Op3.getStartLoc(),
                         "expected integer in range [0, 31]");
          if (Op4Val < 1 || Op4Val > RegWidth)
            return Error(Op4.getStartLoc(),
                         "expected integer in range [1, 32]");

          uint64_t NewOp3Val = 0;
          if (RegWidth == 32)
            NewOp3Val = (32 - Op3Val) & 0x1f;
          else
            NewOp3Val = (64 - Op3Val) & 0x3f;

          uint64_t NewOp4Val = Op4Val - 1;

          if (NewOp3Val != 0 && NewOp4Val >= NewOp3Val)
            return Error(Op4.getStartLoc(),
                         "requested insert overflows register");

          const MCExpr *NewOp3 =
              MCConstantExpr::create(NewOp3Val, getContext());
          const MCExpr *NewOp4 =
              MCConstantExpr::create(NewOp4Val, getContext());
          Operands[3] = AArch64Operand::CreateImm(
              NewOp3, Op3.getStartLoc(), Op3.getEndLoc(), getContext());
          Operands[4] = AArch64Operand::CreateImm(
              NewOp4, Op4.getStartLoc(), Op4.getEndLoc(), getContext());
          if (Tok == "bfi")
            Operands[0] = AArch64Operand::CreateToken(
                "bfm", false, Op.getStartLoc(), getContext());
          else if (Tok == "sbfiz")
            Operands[0] = AArch64Operand::CreateToken(
                "sbfm", false, Op.getStartLoc(), getContext());
          else if (Tok == "ubfiz")
            Operands[0] = AArch64Operand::CreateToken(
                "ubfm", false, Op.getStartLoc(), getContext());
          else
            llvm_unreachable("No valid mnemonic for alias?");
        }
      }

      // FIXME: Horrible hack to handle the BFXIL->BFM, SBFX->SBFM, and
      // UBFX -> UBFM aliases.
    } else if (NumOperands == 5 &&
               (Tok == "bfxil" || Tok == "sbfx" || Tok == "ubfx")) {
      AArch64Operand &Op1 = static_cast<AArch64Operand &>(*Operands[1]);
      AArch64Operand &Op3 = static_cast<AArch64Operand &>(*Operands[3]);
      AArch64Operand &Op4 = static_cast<AArch64Operand &>(*Operands[4]);

      if (Op1.isScalarReg() && Op3.isImm() && Op4.isImm()) {
        const MCConstantExpr *Op3CE = dyn_cast<MCConstantExpr>(Op3.getImm());
        const MCConstantExpr *Op4CE = dyn_cast<MCConstantExpr>(Op4.getImm());

        if (Op3CE && Op4CE) {
          uint64_t Op3Val = Op3CE->getValue();
          uint64_t Op4Val = Op4CE->getValue();

          uint64_t RegWidth = 0;
          if (AArch64MCRegisterClasses[AArch64::GPR64allRegClassID].contains(
                  Op1.getReg()))
            RegWidth = 64;
          else
            RegWidth = 32;

          if (Op3Val >= RegWidth)
            return Error(Op3.getStartLoc(),
                         "expected integer in range [0, 31]");
          if (Op4Val < 1 || Op4Val > RegWidth)
            return Error(Op4.getStartLoc(),
                         "expected integer in range [1, 32]");

          uint64_t NewOp4Val = Op3Val + Op4Val - 1;

          if (NewOp4Val >= RegWidth || NewOp4Val < Op3Val)
            return Error(Op4.getStartLoc(),
                         "requested extract overflows register");

          const MCExpr *NewOp4 =
              MCConstantExpr::create(NewOp4Val, getContext());
          Operands[4] = AArch64Operand::CreateImm(
              NewOp4, Op4.getStartLoc(), Op4.getEndLoc(), getContext());
          if (Tok == "bfxil")
            Operands[0] = AArch64Operand::CreateToken(
                "bfm", false, Op.getStartLoc(), getContext());
          else if (Tok == "sbfx")
            Operands[0] = AArch64Operand::CreateToken(
                "sbfm", false, Op.getStartLoc(), getContext());
          else if (Tok == "ubfx")
            Operands[0] = AArch64Operand::CreateToken(
                "ubfm", false, Op.getStartLoc(), getContext());
          else
            llvm_unreachable("No valid mnemonic for alias?");
        }
      }
    }
  }

  // The Cyclone CPU and early successors didn't execute the zero-cycle zeroing
  // instruction for FP registers correctly in some rare circumstances. Convert
  // it to a safe instruction and warn (because silently changing someone's
  // assembly is rude).
  if (getSTI().getFeatureBits()[AArch64::FeatureZCZeroingFPWorkaround] &&
      NumOperands == 4 && Tok == "movi") {
    AArch64Operand &Op1 = static_cast<AArch64Operand &>(*Operands[1]);
    AArch64Operand &Op2 = static_cast<AArch64Operand &>(*Operands[2]);
    AArch64Operand &Op3 = static_cast<AArch64Operand &>(*Operands[3]);
    if ((Op1.isToken() && Op2.isNeonVectorReg() && Op3.isImm()) ||
        (Op1.isNeonVectorReg() && Op2.isToken() && Op3.isImm())) {
      StringRef Suffix = Op1.isToken() ? Op1.getToken() : Op2.getToken();
      if (Suffix.lower() == ".2d" &&
          cast<MCConstantExpr>(Op3.getImm())->getValue() == 0) {
        Warning(IDLoc, "instruction movi.2d with immediate #0 may not function"
                " correctly on this CPU, converting to equivalent movi.16b");
        // Switch the suffix to .16b.
        unsigned Idx = Op1.isToken() ? 1 : 2;
        Operands[Idx] = AArch64Operand::CreateToken(".16b", false, IDLoc,
                                                  getContext());
      }
    }
  }

  // FIXME: Horrible hack for sxtw and uxtw with Wn src and Xd dst operands.
  //        InstAlias can't quite handle this since the reg classes aren't
  //        subclasses.
  if (NumOperands == 3 && (Tok == "sxtw" || Tok == "uxtw")) {
    // The source register can be Wn here, but the matcher expects a
    // GPR64. Twiddle it here if necessary.
    AArch64Operand &Op = static_cast<AArch64Operand &>(*Operands[2]);
    if (Op.isScalarReg()) {
      unsigned Reg = getXRegFromWReg(Op.getReg());
      Operands[2] = AArch64Operand::CreateReg(Reg, RegKind::Scalar,
                                              Op.getStartLoc(), Op.getEndLoc(),
                                              getContext());
    }
  }
  // FIXME: Likewise for sxt[bh] with a Xd dst operand
  else if (NumOperands == 3 && (Tok == "sxtb" || Tok == "sxth")) {
    AArch64Operand &Op = static_cast<AArch64Operand &>(*Operands[1]);
    if (Op.isScalarReg() &&
        AArch64MCRegisterClasses[AArch64::GPR64allRegClassID].contains(
            Op.getReg())) {
      // The source register can be Wn here, but the matcher expects a
      // GPR64. Twiddle it here if necessary.
      AArch64Operand &Op = static_cast<AArch64Operand &>(*Operands[2]);
      if (Op.isScalarReg()) {
        unsigned Reg = getXRegFromWReg(Op.getReg());
        Operands[2] = AArch64Operand::CreateReg(Reg, RegKind::Scalar,
                                                Op.getStartLoc(),
                                                Op.getEndLoc(), getContext());
      }
    }
  }
  // FIXME: Likewise for uxt[bh] with a Xd dst operand
  else if (NumOperands == 3 && (Tok == "uxtb" || Tok == "uxth")) {
    AArch64Operand &Op = static_cast<AArch64Operand &>(*Operands[1]);
    if (Op.isScalarReg() &&
        AArch64MCRegisterClasses[AArch64::GPR64allRegClassID].contains(
            Op.getReg())) {
      // The source register can be Wn here, but the matcher expects a
      // GPR32. Twiddle it here if necessary.
      AArch64Operand &Op = static_cast<AArch64Operand &>(*Operands[1]);
      if (Op.isScalarReg()) {
        unsigned Reg = getWRegFromXReg(Op.getReg());
        Operands[1] = AArch64Operand::CreateReg(Reg, RegKind::Scalar,
                                                Op.getStartLoc(),
                                                Op.getEndLoc(), getContext());
      }
    }
  }

  MCInst Inst;
  // First try to match against the secondary set of tables containing the
  // short-form NEON instructions (e.g. "fadd.2s v0, v1, v2").
  unsigned MatchResult =
      MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm, 1);

  // If that fails, try against the alternate table containing long-form NEON:
  // "fadd v0.2s, v1.2s, v2.2s"
  if (MatchResult != Match_Success) {
    // But first, save the short-form match result: we can use it in case the
    // long-form match also fails.
    auto ShortFormNEONErrorInfo = ErrorInfo;
    auto ShortFormNEONMatchResult = MatchResult;

    MatchResult =
        MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm, 0);

    // Now, both matches failed, and the long-form match failed on the mnemonic
    // suffix token operand.  The short-form match failure is probably more
    // relevant: use it instead.
    if (MatchResult == Match_InvalidOperand && ErrorInfo == 1 &&
        Operands.size() > 1 && ((AArch64Operand &)*Operands[1]).isToken() &&
        ((AArch64Operand &)*Operands[1]).isTokenSuffix()) {
      MatchResult = ShortFormNEONMatchResult;
      ErrorInfo = ShortFormNEONErrorInfo;
    }
  }

  switch (MatchResult) {
  case Match_Success: {
    // Perform range checking and other semantic validations
    SmallVector<SMLoc, 8> OperandLocs;
    NumOperands = Operands.size();
    for (unsigned i = 1; i < NumOperands; ++i)
      OperandLocs.push_back(Operands[i]->getStartLoc());
    if (validateInstruction(Inst, IDLoc, OperandLocs))
      return true;

    Inst.setLoc(IDLoc);
    Out.EmitInstruction(Inst, getSTI());
    return false;
  }
  case Match_MissingFeature: {
    assert(ErrorInfo && "Unknown missing feature!");
    // Special case the error message for the very common case where only
    // a single subtarget feature is missing (neon, e.g.).
    std::string Msg = "instruction requires:";
    uint64_t Mask = 1;
    for (unsigned i = 0; i < (sizeof(ErrorInfo)*8-1); ++i) {
      if (ErrorInfo & Mask) {
        Msg += " ";
        Msg += getSubtargetFeatureName(ErrorInfo & Mask);
      }
      Mask <<= 1;
    }
    return Error(IDLoc, Msg);
  }
  case Match_MnemonicFail:
    return showMatchError(IDLoc, MatchResult, ErrorInfo, Operands);
  case Match_InvalidOperand: {
    SMLoc ErrorLoc = IDLoc;

    if (ErrorInfo != ~0ULL) {
      if (ErrorInfo >= Operands.size())
        return Error(IDLoc, "too few operands for instruction",
                     SMRange(IDLoc, getTok().getLoc()));

      ErrorLoc = ((AArch64Operand &)*Operands[ErrorInfo]).getStartLoc();
      if (ErrorLoc == SMLoc())
        ErrorLoc = IDLoc;
    }
    // If the match failed on a suffix token operand, tweak the diagnostic
    // accordingly.
    if (((AArch64Operand &)*Operands[ErrorInfo]).isToken() &&
        ((AArch64Operand &)*Operands[ErrorInfo]).isTokenSuffix())
      MatchResult = Match_InvalidSuffix;

    return showMatchError(ErrorLoc, MatchResult, ErrorInfo, Operands);
  }
  case Match_InvalidTiedOperand:
  case Match_InvalidMemoryIndexed1:
  case Match_InvalidMemoryIndexed2:
  case Match_InvalidMemoryIndexed4:
  case Match_InvalidMemoryIndexed8:
  case Match_InvalidMemoryIndexed16:
  case Match_InvalidCondCode:
  case Match_AddSubRegExtendSmall:
  case Match_AddSubRegExtendLarge:
  case Match_AddSubSecondSource:
  case Match_LogicalSecondSource:
  case Match_AddSubRegShift32:
  case Match_AddSubRegShift64:
  case Match_InvalidMovImm32Shift:
  case Match_InvalidMovImm64Shift:
  case Match_InvalidFPImm:
  case Match_InvalidMemoryWExtend8:
  case Match_InvalidMemoryWExtend16:
  case Match_InvalidMemoryWExtend32:
  case Match_InvalidMemoryWExtend64:
  case Match_InvalidMemoryWExtend128:
  case Match_InvalidMemoryXExtend8:
  case Match_InvalidMemoryXExtend16:
  case Match_InvalidMemoryXExtend32:
  case Match_InvalidMemoryXExtend64:
  case Match_InvalidMemoryXExtend128:
  case Match_InvalidMemoryIndexed1SImm4:
  case Match_InvalidMemoryIndexed2SImm4:
  case Match_InvalidMemoryIndexed3SImm4:
  case Match_InvalidMemoryIndexed4SImm4:
  case Match_InvalidMemoryIndexed1SImm6:
  case Match_InvalidMemoryIndexed16SImm4:
  case Match_InvalidMemoryIndexed4SImm7:
  case Match_InvalidMemoryIndexed8SImm7:
  case Match_InvalidMemoryIndexed16SImm7:
  case Match_InvalidMemoryIndexed8UImm5:
  case Match_InvalidMemoryIndexed4UImm5:
  case Match_InvalidMemoryIndexed2UImm5:
  case Match_InvalidMemoryIndexed1UImm6:
  case Match_InvalidMemoryIndexed2UImm6:
  case Match_InvalidMemoryIndexed4UImm6:
  case Match_InvalidMemoryIndexed8UImm6:
  case Match_InvalidMemoryIndexed16UImm6:
  case Match_InvalidMemoryIndexedSImm6:
  case Match_InvalidMemoryIndexedSImm5:
  case Match_InvalidMemoryIndexedSImm8:
  case Match_InvalidMemoryIndexedSImm9:
  case Match_InvalidMemoryIndexed16SImm9:
  case Match_InvalidMemoryIndexed8SImm10:
  case Match_InvalidImm0_1:
  case Match_InvalidImm0_7:
  case Match_InvalidImm0_15:
  case Match_InvalidImm0_31:
  case Match_InvalidImm0_63:
  case Match_InvalidImm0_127:
  case Match_InvalidImm0_255:
  case Match_InvalidImm0_65535:
  case Match_InvalidImm1_8:
  case Match_InvalidImm1_16:
  case Match_InvalidImm1_32:
  case Match_InvalidImm1_64:
  case Match_InvalidSVEAddSubImm8:
  case Match_InvalidSVEAddSubImm16:
  case Match_InvalidSVEAddSubImm32:
  case Match_InvalidSVEAddSubImm64:
  case Match_InvalidSVECpyImm8:
  case Match_InvalidSVECpyImm16:
  case Match_InvalidSVECpyImm32:
  case Match_InvalidSVECpyImm64:
  case Match_InvalidIndexRange1_1:
  case Match_InvalidIndexRange0_15:
  case Match_InvalidIndexRange0_7:
  case Match_InvalidIndexRange0_3:
  case Match_InvalidIndexRange0_1:
  case Match_InvalidSVEIndexRange0_63:
  case Match_InvalidSVEIndexRange0_31:
  case Match_InvalidSVEIndexRange0_15:
  case Match_InvalidSVEIndexRange0_7:
  case Match_InvalidSVEIndexRange0_3:
  case Match_InvalidLabel:
  case Match_InvalidComplexRotationEven:
  case Match_InvalidComplexRotationOdd:
  case Match_InvalidGPR64shifted8:
  case Match_InvalidGPR64shifted16:
  case Match_InvalidGPR64shifted32:
  case Match_InvalidGPR64shifted64:
  case Match_InvalidGPR64NoXZRshifted8:
  case Match_InvalidGPR64NoXZRshifted16:
  case Match_InvalidGPR64NoXZRshifted32:
  case Match_InvalidGPR64NoXZRshifted64:
  case Match_InvalidZPR32UXTW8:
  case Match_InvalidZPR32UXTW16:
  case Match_InvalidZPR32UXTW32:
  case Match_InvalidZPR32UXTW64:
  case Match_InvalidZPR32SXTW8:
  case Match_InvalidZPR32SXTW16:
  case Match_InvalidZPR32SXTW32:
  case Match_InvalidZPR32SXTW64:
  case Match_InvalidZPR64UXTW8:
  case Match_InvalidZPR64SXTW8:
  case Match_InvalidZPR64UXTW16:
  case Match_InvalidZPR64SXTW16:
  case Match_InvalidZPR64UXTW32:
  case Match_InvalidZPR64SXTW32:
  case Match_InvalidZPR64UXTW64:
  case Match_InvalidZPR64SXTW64:
  case Match_InvalidZPR32LSL8:
  case Match_InvalidZPR32LSL16:
  case Match_InvalidZPR32LSL32:
  case Match_InvalidZPR32LSL64:
  case Match_InvalidZPR64LSL8:
  case Match_InvalidZPR64LSL16:
  case Match_InvalidZPR64LSL32:
  case Match_InvalidZPR64LSL64:
  case Match_InvalidZPR0:
  case Match_InvalidZPR8:
  case Match_InvalidZPR16:
  case Match_InvalidZPR32:
  case Match_InvalidZPR64:
  case Match_InvalidZPR128:
  case Match_InvalidZPR_3b8:
  case Match_InvalidZPR_3b16:
  case Match_InvalidZPR_3b32:
  case Match_InvalidZPR_4b16:
  case Match_InvalidZPR_4b32:
  case Match_InvalidZPR_4b64:
  case Match_InvalidSVEPredicateAnyReg:
  case Match_InvalidSVEPattern:
  case Match_InvalidSVEPredicateBReg:
  case Match_InvalidSVEPredicateHReg:
  case Match_InvalidSVEPredicateSReg:
  case Match_InvalidSVEPredicateDReg:
  case Match_InvalidSVEPredicate3bAnyReg:
  case Match_InvalidSVEPredicate3bBReg:
  case Match_InvalidSVEPredicate3bHReg:
  case Match_InvalidSVEPredicate3bSReg:
  case Match_InvalidSVEPredicate3bDReg:
  case Match_InvalidSVEExactFPImmOperandHalfOne:
  case Match_InvalidSVEExactFPImmOperandHalfTwo:
  case Match_InvalidSVEExactFPImmOperandZeroOne:
  case Match_MSR:
  case Match_MRS: {
    if (ErrorInfo >= Operands.size())
      return Error(IDLoc, "too few operands for instruction", SMRange(IDLoc, (*Operands.back()).getEndLoc()));
    // Any time we get here, there's nothing fancy to do. Just get the
    // operand SMLoc and display the diagnostic.
    SMLoc ErrorLoc = ((AArch64Operand &)*Operands[ErrorInfo]).getStartLoc();
    if (ErrorLoc == SMLoc())
      ErrorLoc = IDLoc;
    return showMatchError(ErrorLoc, MatchResult, ErrorInfo, Operands);
  }
  }

  llvm_unreachable("Implement any new match types added!");
}

/// ParseDirective parses the arm specific directives
bool AArch64AsmParser::ParseDirective(AsmToken DirectiveID) {
  const MCObjectFileInfo::Environment Format =
    getContext().getObjectFileInfo()->getObjectFileType();
  bool IsMachO = Format == MCObjectFileInfo::IsMachO;

  StringRef IDVal = DirectiveID.getIdentifier();
  SMLoc Loc = DirectiveID.getLoc();
  if (IDVal == ".arch")
    parseDirectiveArch(Loc);
  else if (IDVal == ".cpu")
    parseDirectiveCPU(Loc);
  else if (IDVal == ".tlsdesccall")
    parseDirectiveTLSDescCall(Loc);
  else if (IDVal == ".ltorg" || IDVal == ".pool")
    parseDirectiveLtorg(Loc);
  else if (IDVal == ".unreq")
    parseDirectiveUnreq(Loc);
  else if (IDVal == ".inst")
    parseDirectiveInst(Loc);
  else if (IDVal == ".cfi_negate_ra_state")
    parseDirectiveCFINegateRAState();
  else if (IDVal == ".cfi_b_key_frame")
    parseDirectiveCFIBKeyFrame();
  else if (IDVal == ".arch_extension")
    parseDirectiveArchExtension(Loc);
  else if (IsMachO) {
    if (IDVal == MCLOHDirectiveName())
      parseDirectiveLOH(IDVal, Loc);
    else
      return true;
  } else
    return true;
  return false;
}

static void ExpandCryptoAEK(AArch64::ArchKind ArchKind,
                            SmallVector<StringRef, 4> &RequestedExtensions) {
  const bool NoCrypto =
      (std::find(RequestedExtensions.begin(), RequestedExtensions.end(),
                 "nocrypto") != std::end(RequestedExtensions));
  const bool Crypto =
      (std::find(RequestedExtensions.begin(), RequestedExtensions.end(),
                 "crypto") != std::end(RequestedExtensions));

  if (!NoCrypto && Crypto) {
    switch (ArchKind) {
    default:
      // Map 'generic' (and others) to sha2 and aes, because
      // that was the traditional meaning of crypto.
    case AArch64::ArchKind::ARMV8_1A:
    case AArch64::ArchKind::ARMV8_2A:
    case AArch64::ArchKind::ARMV8_3A:
      RequestedExtensions.push_back("sha2");
      RequestedExtensions.push_back("aes");
      break;
    case AArch64::ArchKind::ARMV8_4A:
    case AArch64::ArchKind::ARMV8_5A:
      RequestedExtensions.push_back("sm4");
      RequestedExtensions.push_back("sha3");
      RequestedExtensions.push_back("sha2");
      RequestedExtensions.push_back("aes");
      break;
    }
  } else if (NoCrypto) {
    switch (ArchKind) {
    default:
      // Map 'generic' (and others) to sha2 and aes, because
      // that was the traditional meaning of crypto.
    case AArch64::ArchKind::ARMV8_1A:
    case AArch64::ArchKind::ARMV8_2A:
    case AArch64::ArchKind::ARMV8_3A:
      RequestedExtensions.push_back("nosha2");
      RequestedExtensions.push_back("noaes");
      break;
    case AArch64::ArchKind::ARMV8_4A:
    case AArch64::ArchKind::ARMV8_5A:
      RequestedExtensions.push_back("nosm4");
      RequestedExtensions.push_back("nosha3");
      RequestedExtensions.push_back("nosha2");
      RequestedExtensions.push_back("noaes");
      break;
    }
  }
}

/// parseDirectiveArch
///   ::= .arch token
bool AArch64AsmParser::parseDirectiveArch(SMLoc L) {
  SMLoc ArchLoc = getLoc();

  StringRef Arch, ExtensionString;
  std::tie(Arch, ExtensionString) =
      getParser().parseStringToEndOfStatement().trim().split('+');

  AArch64::ArchKind ID = AArch64::parseArch(Arch);
  if (ID == AArch64::ArchKind::INVALID)
    return Error(ArchLoc, "unknown arch name");

  if (parseToken(AsmToken::EndOfStatement))
    return true;

  // Get the architecture and extension features.
  std::vector<StringRef> AArch64Features;
  AArch64::getArchFeatures(ID, AArch64Features);
  AArch64::getExtensionFeatures(AArch64::getDefaultExtensions("generic", ID),
                                AArch64Features);

  MCSubtargetInfo &STI = copySTI();
  std::vector<std::string> ArchFeatures(AArch64Features.begin(), AArch64Features.end());
  STI.setDefaultFeatures("generic", join(ArchFeatures.begin(), ArchFeatures.end(), ","));

  SmallVector<StringRef, 4> RequestedExtensions;
  if (!ExtensionString.empty())
    ExtensionString.split(RequestedExtensions, '+');

  ExpandCryptoAEK(ID, RequestedExtensions);

  FeatureBitset Features = STI.getFeatureBits();
  for (auto Name : RequestedExtensions) {
    bool EnableFeature = true;

    if (Name.startswith_lower("no")) {
      EnableFeature = false;
      Name = Name.substr(2);
    }

    for (const auto &Extension : ExtensionMap) {
      if (Extension.Name != Name)
        continue;

      if (Extension.Features.none())
        report_fatal_error("unsupported architectural extension: " + Name);

      FeatureBitset ToggleFeatures = EnableFeature
                                         ? (~Features & Extension.Features)
                                         : ( Features & Extension.Features);
      uint64_t Features =
          ComputeAvailableFeatures(STI.ToggleFeature(ToggleFeatures));
      setAvailableFeatures(Features);
      break;
    }
  }
  return false;
}

/// parseDirectiveArchExtension
///   ::= .arch_extension [no]feature
bool AArch64AsmParser::parseDirectiveArchExtension(SMLoc L) {
  MCAsmParser &Parser = getParser();

  if (getLexer().isNot(AsmToken::Identifier))
    return Error(getLexer().getLoc(), "expected architecture extension name");

  const AsmToken &Tok = Parser.getTok();
  StringRef Name = Tok.getString();
  SMLoc ExtLoc = Tok.getLoc();
  Lex();

  if (parseToken(AsmToken::EndOfStatement,
                 "unexpected token in '.arch_extension' directive"))
    return true;

  bool EnableFeature = true;
  if (Name.startswith_lower("no")) {
    EnableFeature = false;
    Name = Name.substr(2);
  }

  MCSubtargetInfo &STI = copySTI();
  FeatureBitset Features = STI.getFeatureBits();
  for (const auto &Extension : ExtensionMap) {
    if (Extension.Name != Name)
      continue;

    if (Extension.Features.none())
      return Error(ExtLoc, "unsupported architectural extension: " + Name);

    FeatureBitset ToggleFeatures = EnableFeature
                                       ? (~Features & Extension.Features)
                                       : (Features & Extension.Features);
    uint64_t Features =
        ComputeAvailableFeatures(STI.ToggleFeature(ToggleFeatures));
    setAvailableFeatures(Features);
    return false;
  }

  return Error(ExtLoc, "unknown architectural extension: " + Name);
}

static SMLoc incrementLoc(SMLoc L, int Offset) {
  return SMLoc::getFromPointer(L.getPointer() + Offset);
}

/// parseDirectiveCPU
///   ::= .cpu id
bool AArch64AsmParser::parseDirectiveCPU(SMLoc L) {
  SMLoc CurLoc = getLoc();

  StringRef CPU, ExtensionString;
  std::tie(CPU, ExtensionString) =
      getParser().parseStringToEndOfStatement().trim().split('+');

  if (parseToken(AsmToken::EndOfStatement))
    return true;

  SmallVector<StringRef, 4> RequestedExtensions;
  if (!ExtensionString.empty())
    ExtensionString.split(RequestedExtensions, '+');

  // FIXME This is using tablegen data, but should be moved to ARMTargetParser
  // once that is tablegen'ed
  if (!getSTI().isCPUStringValid(CPU)) {
    Error(CurLoc, "unknown CPU name");
    return false;
  }

  MCSubtargetInfo &STI = copySTI();
  STI.setDefaultFeatures(CPU, "");
  CurLoc = incrementLoc(CurLoc, CPU.size());

  ExpandCryptoAEK(llvm::AArch64::getCPUArchKind(CPU), RequestedExtensions);

  FeatureBitset Features = STI.getFeatureBits();
  for (auto Name : RequestedExtensions) {
    // Advance source location past '+'.
    CurLoc = incrementLoc(CurLoc, 1);

    bool EnableFeature = true;

    if (Name.startswith_lower("no")) {
      EnableFeature = false;
      Name = Name.substr(2);
    }

    bool FoundExtension = false;
    for (const auto &Extension : ExtensionMap) {
      if (Extension.Name != Name)
        continue;

      if (Extension.Features.none())
        report_fatal_error("unsupported architectural extension: " + Name);

      FeatureBitset ToggleFeatures = EnableFeature
                                         ? (~Features & Extension.Features)
                                         : ( Features & Extension.Features);
      uint64_t Features =
          ComputeAvailableFeatures(STI.ToggleFeature(ToggleFeatures));
      setAvailableFeatures(Features);
      FoundExtension = true;

      break;
    }

    if (!FoundExtension)
      Error(CurLoc, "unsupported architectural extension");

    CurLoc = incrementLoc(CurLoc, Name.size());
  }
  return false;
}

/// parseDirectiveInst
///  ::= .inst opcode [, ...]
bool AArch64AsmParser::parseDirectiveInst(SMLoc Loc) {
  if (getLexer().is(AsmToken::EndOfStatement))
    return Error(Loc, "expected expression following '.inst' directive");

  auto parseOp = [&]() -> bool {
    SMLoc L = getLoc();
    const MCExpr *Expr;
    if (check(getParser().parseExpression(Expr), L, "expected expression"))
      return true;
    const MCConstantExpr *Value = dyn_cast_or_null<MCConstantExpr>(Expr);
    if (check(!Value, L, "expected constant expression"))
      return true;
    getTargetStreamer().emitInst(Value->getValue());
    return false;
  };

  if (parseMany(parseOp))
    return addErrorSuffix(" in '.inst' directive");
  return false;
}

// parseDirectiveTLSDescCall:
//   ::= .tlsdesccall symbol
bool AArch64AsmParser::parseDirectiveTLSDescCall(SMLoc L) {
  StringRef Name;
  if (check(getParser().parseIdentifier(Name), L,
            "expected symbol after directive") ||
      parseToken(AsmToken::EndOfStatement))
    return true;

  MCSymbol *Sym = getContext().getOrCreateSymbol(Name);
  const MCExpr *Expr = MCSymbolRefExpr::create(Sym, getContext());
  Expr = AArch64MCExpr::create(Expr, AArch64MCExpr::VK_TLSDESC, getContext());

  MCInst Inst;
  Inst.setOpcode(AArch64::TLSDESCCALL);
  Inst.addOperand(MCOperand::createExpr(Expr));

  getParser().getStreamer().EmitInstruction(Inst, getSTI());
  return false;
}

/// ::= .loh <lohName | lohId> label1, ..., labelN
/// The number of arguments depends on the loh identifier.
bool AArch64AsmParser::parseDirectiveLOH(StringRef IDVal, SMLoc Loc) {
  MCLOHType Kind;
  if (getParser().getTok().isNot(AsmToken::Identifier)) {
    if (getParser().getTok().isNot(AsmToken::Integer))
      return TokError("expected an identifier or a number in directive");
    // We successfully get a numeric value for the identifier.
    // Check if it is valid.
    int64_t Id = getParser().getTok().getIntVal();
    if (Id <= -1U && !isValidMCLOHType(Id))
      return TokError("invalid numeric identifier in directive");
    Kind = (MCLOHType)Id;
  } else {
    StringRef Name = getTok().getIdentifier();
    // We successfully parse an identifier.
    // Check if it is a recognized one.
    int Id = MCLOHNameToId(Name);

    if (Id == -1)
      return TokError("invalid identifier in directive");
    Kind = (MCLOHType)Id;
  }
  // Consume the identifier.
  Lex();
  // Get the number of arguments of this LOH.
  int NbArgs = MCLOHIdToNbArgs(Kind);

  assert(NbArgs != -1 && "Invalid number of arguments");

  SmallVector<MCSymbol *, 3> Args;
  for (int Idx = 0; Idx < NbArgs; ++Idx) {
    StringRef Name;
    if (getParser().parseIdentifier(Name))
      return TokError("expected identifier in directive");
    Args.push_back(getContext().getOrCreateSymbol(Name));

    if (Idx + 1 == NbArgs)
      break;
    if (parseToken(AsmToken::Comma,
                   "unexpected token in '" + Twine(IDVal) + "' directive"))
      return true;
  }
  if (parseToken(AsmToken::EndOfStatement,
                 "unexpected token in '" + Twine(IDVal) + "' directive"))
    return true;

  getStreamer().EmitLOHDirective((MCLOHType)Kind, Args);
  return false;
}

/// parseDirectiveLtorg
///  ::= .ltorg | .pool
bool AArch64AsmParser::parseDirectiveLtorg(SMLoc L) {
  if (parseToken(AsmToken::EndOfStatement, "unexpected token in directive"))
    return true;
  getTargetStreamer().emitCurrentConstantPool();
  return false;
}

/// parseDirectiveReq
///  ::= name .req registername
bool AArch64AsmParser::parseDirectiveReq(StringRef Name, SMLoc L) {
  MCAsmParser &Parser = getParser();
  Parser.Lex(); // Eat the '.req' token.
  SMLoc SRegLoc = getLoc();
  RegKind RegisterKind = RegKind::Scalar;
  unsigned RegNum;
  OperandMatchResultTy ParseRes = tryParseScalarRegister(RegNum);

  if (ParseRes != MatchOperand_Success) {
    StringRef Kind;
    RegisterKind = RegKind::NeonVector;
    ParseRes = tryParseVectorRegister(RegNum, Kind, RegKind::NeonVector);

    if (ParseRes == MatchOperand_ParseFail)
      return true;

    if (ParseRes == MatchOperand_Success && !Kind.empty())
      return Error(SRegLoc, "vector register without type specifier expected");
  }

  if (ParseRes != MatchOperand_Success) {
    StringRef Kind;
    RegisterKind = RegKind::SVEDataVector;
    ParseRes =
        tryParseVectorRegister(RegNum, Kind, RegKind::SVEDataVector);

    if (ParseRes == MatchOperand_ParseFail)
      return true;

    if (ParseRes == MatchOperand_Success && !Kind.empty())
      return Error(SRegLoc,
                   "sve vector register without type specifier expected");
  }

  if (ParseRes != MatchOperand_Success) {
    StringRef Kind;
    RegisterKind = RegKind::SVEPredicateVector;
    ParseRes = tryParseVectorRegister(RegNum, Kind, RegKind::SVEPredicateVector);

    if (ParseRes == MatchOperand_ParseFail)
      return true;

    if (ParseRes == MatchOperand_Success && !Kind.empty())
      return Error(SRegLoc,
                   "sve predicate register without type specifier expected");
  }

  if (ParseRes != MatchOperand_Success)
    return Error(SRegLoc, "register name or alias expected");

  // Shouldn't be anything else.
  if (parseToken(AsmToken::EndOfStatement,
                 "unexpected input in .req directive"))
    return true;

  auto pair = std::make_pair(RegisterKind, (unsigned) RegNum);
  if (RegisterReqs.insert(std::make_pair(Name, pair)).first->second != pair)
    Warning(L, "ignoring redefinition of register alias '" + Name + "'");

  return false;
}

/// parseDirectiveUneq
///  ::= .unreq registername
bool AArch64AsmParser::parseDirectiveUnreq(SMLoc L) {
  MCAsmParser &Parser = getParser();
  if (getTok().isNot(AsmToken::Identifier))
    return TokError("unexpected input in .unreq directive.");
  RegisterReqs.erase(Parser.getTok().getIdentifier().lower());
  Parser.Lex(); // Eat the identifier.
  if (parseToken(AsmToken::EndOfStatement))
    return addErrorSuffix("in '.unreq' directive");
  return false;
}

bool AArch64AsmParser::parseDirectiveCFINegateRAState() {
  if (parseToken(AsmToken::EndOfStatement, "unexpected token in directive"))
    return true;
  getStreamer().EmitCFINegateRAState();
  return false;
}

/// parseDirectiveCFIBKeyFrame
/// ::= .cfi_b_key
bool AArch64AsmParser::parseDirectiveCFIBKeyFrame() {
  if (parseToken(AsmToken::EndOfStatement,
                 "unexpected token in '.cfi_b_key_frame'"))
    return true;
  getStreamer().EmitCFIBKeyFrame();
  return false;
}

bool
AArch64AsmParser::classifySymbolRef(const MCExpr *Expr,
                                    AArch64MCExpr::VariantKind &ELFRefKind,
                                    MCSymbolRefExpr::VariantKind &DarwinRefKind,
                                    int64_t &Addend) {
  ELFRefKind = AArch64MCExpr::VK_INVALID;
  DarwinRefKind = MCSymbolRefExpr::VK_None;
  Addend = 0;

  if (const AArch64MCExpr *AE = dyn_cast<AArch64MCExpr>(Expr)) {
    ELFRefKind = AE->getKind();
    Expr = AE->getSubExpr();
  }

  const MCSymbolRefExpr *SE = dyn_cast<MCSymbolRefExpr>(Expr);
  if (SE) {
    // It's a simple symbol reference with no addend.
    DarwinRefKind = SE->getKind();
    return true;
  }

  // Check that it looks like a symbol + an addend
  MCValue Res;
  bool Relocatable = Expr->evaluateAsRelocatable(Res, nullptr, nullptr);
  if (!Relocatable || Res.getSymB())
    return false;

  // Treat expressions with an ELFRefKind (like ":abs_g1:3", or
  // ":abs_g1:x" where x is constant) as symbolic even if there is no symbol.
  if (!Res.getSymA() && ELFRefKind == AArch64MCExpr::VK_INVALID)
    return false;

  if (Res.getSymA())
    DarwinRefKind = Res.getSymA()->getKind();
  Addend = Res.getConstant();

  // It's some symbol reference + a constant addend, but really
  // shouldn't use both Darwin and ELF syntax.
  return ELFRefKind == AArch64MCExpr::VK_INVALID ||
         DarwinRefKind == MCSymbolRefExpr::VK_None;
}

/// Force static initialization.
extern "C" void LLVMInitializeAArch64AsmParser() {
  RegisterMCAsmParser<AArch64AsmParser> X(getTheAArch64leTarget());
  RegisterMCAsmParser<AArch64AsmParser> Y(getTheAArch64beTarget());
  RegisterMCAsmParser<AArch64AsmParser> Z(getTheARM64Target());
}

#define GET_REGISTER_MATCHER
#define GET_SUBTARGET_FEATURE_NAME
#define GET_MATCHER_IMPLEMENTATION
#define GET_MNEMONIC_SPELL_CHECKER
#include "AArch64GenAsmMatcher.inc"

// Define this matcher function after the auto-generated include so we
// have the match class enum definitions.
unsigned AArch64AsmParser::validateTargetOperandClass(MCParsedAsmOperand &AsmOp,
                                                      unsigned Kind) {
  AArch64Operand &Op = static_cast<AArch64Operand &>(AsmOp);
  // If the kind is a token for a literal immediate, check if our asm
  // operand matches. This is for InstAliases which have a fixed-value
  // immediate in the syntax.
  int64_t ExpectedVal;
  switch (Kind) {
  default:
    return Match_InvalidOperand;
  case MCK__35_0:
    ExpectedVal = 0;
    break;
  case MCK__35_1:
    ExpectedVal = 1;
    break;
  case MCK__35_12:
    ExpectedVal = 12;
    break;
  case MCK__35_16:
    ExpectedVal = 16;
    break;
  case MCK__35_2:
    ExpectedVal = 2;
    break;
  case MCK__35_24:
    ExpectedVal = 24;
    break;
  case MCK__35_3:
    ExpectedVal = 3;
    break;
  case MCK__35_32:
    ExpectedVal = 32;
    break;
  case MCK__35_4:
    ExpectedVal = 4;
    break;
  case MCK__35_48:
    ExpectedVal = 48;
    break;
  case MCK__35_6:
    ExpectedVal = 6;
    break;
  case MCK__35_64:
    ExpectedVal = 64;
    break;
  case MCK__35_8:
    ExpectedVal = 8;
    break;
  }
  if (!Op.isImm())
    return Match_InvalidOperand;
  const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Op.getImm());
  if (!CE)
    return Match_InvalidOperand;
  if (CE->getValue() == ExpectedVal)
    return Match_Success;
  return Match_InvalidOperand;
}

OperandMatchResultTy
AArch64AsmParser::tryParseGPRSeqPair(OperandVector &Operands) {

  SMLoc S = getLoc();

  if (getParser().getTok().isNot(AsmToken::Identifier)) {
    Error(S, "expected register");
    return MatchOperand_ParseFail;
  }

  unsigned FirstReg;
  OperandMatchResultTy Res = tryParseScalarRegister(FirstReg);
  if (Res != MatchOperand_Success)
    return MatchOperand_ParseFail;

  const MCRegisterClass &WRegClass =
      AArch64MCRegisterClasses[AArch64::GPR32RegClassID];
  const MCRegisterClass &XRegClass =
      AArch64MCRegisterClasses[AArch64::GPR64RegClassID];

  bool isXReg = XRegClass.contains(FirstReg),
       isWReg = WRegClass.contains(FirstReg);
  if (!isXReg && !isWReg) {
    Error(S, "expected first even register of a "
             "consecutive same-size even/odd register pair");
    return MatchOperand_ParseFail;
  }

  const MCRegisterInfo *RI = getContext().getRegisterInfo();
  unsigned FirstEncoding = RI->getEncodingValue(FirstReg);

  if (FirstEncoding & 0x1) {
    Error(S, "expected first even register of a "
             "consecutive same-size even/odd register pair");
    return MatchOperand_ParseFail;
  }

  if (getParser().getTok().isNot(AsmToken::Comma)) {
    Error(getLoc(), "expected comma");
    return MatchOperand_ParseFail;
  }
  // Eat the comma
  getParser().Lex();

  SMLoc E = getLoc();
  unsigned SecondReg;
  Res = tryParseScalarRegister(SecondReg);
  if (Res != MatchOperand_Success)
    return MatchOperand_ParseFail;

  if (RI->getEncodingValue(SecondReg) != FirstEncoding + 1 ||
      (isXReg && !XRegClass.contains(SecondReg)) ||
      (isWReg && !WRegClass.contains(SecondReg))) {
    Error(E,"expected second odd register of a "
             "consecutive same-size even/odd register pair");
    return MatchOperand_ParseFail;
  }

  unsigned Pair = 0;
  if (isXReg) {
    Pair = RI->getMatchingSuperReg(FirstReg, AArch64::sube64,
           &AArch64MCRegisterClasses[AArch64::XSeqPairsClassRegClassID]);
  } else {
    Pair = RI->getMatchingSuperReg(FirstReg, AArch64::sube32,
           &AArch64MCRegisterClasses[AArch64::WSeqPairsClassRegClassID]);
  }

  Operands.push_back(AArch64Operand::CreateReg(Pair, RegKind::Scalar, S,
      getLoc(), getContext()));

  return MatchOperand_Success;
}

template <bool ParseShiftExtend, bool ParseSuffix>
OperandMatchResultTy
AArch64AsmParser::tryParseSVEDataVector(OperandVector &Operands) {
  const SMLoc S = getLoc();
  // Check for a SVE vector register specifier first.
  unsigned RegNum;
  StringRef Kind;

  OperandMatchResultTy Res =
      tryParseVectorRegister(RegNum, Kind, RegKind::SVEDataVector);

  if (Res != MatchOperand_Success)
    return Res;

  if (ParseSuffix && Kind.empty())
    return MatchOperand_NoMatch;

  const auto &KindRes = parseVectorKind(Kind, RegKind::SVEDataVector);
  if (!KindRes)
    return MatchOperand_NoMatch;

  unsigned ElementWidth = KindRes->second;

  // No shift/extend is the default.
  if (!ParseShiftExtend || getParser().getTok().isNot(AsmToken::Comma)) {
    Operands.push_back(AArch64Operand::CreateVectorReg(
        RegNum, RegKind::SVEDataVector, ElementWidth, S, S, getContext()));

    OperandMatchResultTy Res = tryParseVectorIndex(Operands);
    if (Res == MatchOperand_ParseFail)
      return MatchOperand_ParseFail;
    return MatchOperand_Success;
  }

  // Eat the comma
  getParser().Lex();

  // Match the shift
  SmallVector<std::unique_ptr<MCParsedAsmOperand>, 1> ExtOpnd;
  Res = tryParseOptionalShiftExtend(ExtOpnd);
  if (Res != MatchOperand_Success)
    return Res;

  auto Ext = static_cast<AArch64Operand *>(ExtOpnd.back().get());
  Operands.push_back(AArch64Operand::CreateVectorReg(
      RegNum, RegKind::SVEDataVector, ElementWidth, S, Ext->getEndLoc(),
      getContext(), Ext->getShiftExtendType(), Ext->getShiftExtendAmount(),
      Ext->hasShiftExtendAmount()));

  return MatchOperand_Success;
}

OperandMatchResultTy
AArch64AsmParser::tryParseSVEPattern(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();

  SMLoc SS = getLoc();
  const AsmToken &TokE = Parser.getTok();
  bool IsHash = TokE.is(AsmToken::Hash);

  if (!IsHash && TokE.isNot(AsmToken::Identifier))
    return MatchOperand_NoMatch;

  int64_t Pattern;
  if (IsHash) {
    Parser.Lex(); // Eat hash

    // Parse the immediate operand.
    const MCExpr *ImmVal;
    SS = getLoc();
    if (Parser.parseExpression(ImmVal))
      return MatchOperand_ParseFail;

    auto *MCE = dyn_cast<MCConstantExpr>(ImmVal);
    if (!MCE)
      return MatchOperand_ParseFail;

    Pattern = MCE->getValue();
  } else {
    // Parse the pattern
    auto Pat = AArch64SVEPredPattern::lookupSVEPREDPATByName(TokE.getString());
    if (!Pat)
      return MatchOperand_NoMatch;

    Parser.Lex();
    Pattern = Pat->Encoding;
    assert(Pattern >= 0 && Pattern < 32);
  }

  Operands.push_back(
      AArch64Operand::CreateImm(MCConstantExpr::create(Pattern, getContext()),
                                SS, getLoc(), getContext()));

  return MatchOperand_Success;
}
