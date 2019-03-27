//===- llvm/MC/MCTargetAsmParser.h - Target Assembly Parser -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCPARSER_MCTARGETASMPARSER_H
#define LLVM_MC_MCPARSER_MCTARGETASMPARSER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCAsmParserExtension.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/SMLoc.h"
#include <cstdint>
#include <memory>

namespace llvm {

class MCInst;
class MCParsedAsmOperand;
class MCStreamer;
class MCSubtargetInfo;
template <typename T> class SmallVectorImpl;

using OperandVector = SmallVectorImpl<std::unique_ptr<MCParsedAsmOperand>>;

enum AsmRewriteKind {
  AOK_Align,          // Rewrite align as .align.
  AOK_EVEN,           // Rewrite even as .even.
  AOK_Emit,           // Rewrite _emit as .byte.
  AOK_Input,          // Rewrite in terms of $N.
  AOK_Output,         // Rewrite in terms of $N.
  AOK_SizeDirective,  // Add a sizing directive (e.g., dword ptr).
  AOK_Label,          // Rewrite local labels.
  AOK_EndOfStatement, // Add EndOfStatement (e.g., "\n\t").
  AOK_Skip,           // Skip emission (e.g., offset/type operators).
  AOK_IntelExpr       // SizeDirective SymDisp [BaseReg + IndexReg * Scale + ImmDisp]
};

const char AsmRewritePrecedence [] = {
  2, // AOK_Align
  2, // AOK_EVEN
  2, // AOK_Emit
  3, // AOK_Input
  3, // AOK_Output
  5, // AOK_SizeDirective
  1, // AOK_Label
  5, // AOK_EndOfStatement
  2, // AOK_Skip
  2  // AOK_IntelExpr
};

// Represnt the various parts which makes up an intel expression,
// used for emitting compound intel expressions
struct IntelExpr {
  bool NeedBracs;
  int64_t Imm;
  StringRef BaseReg;
  StringRef IndexReg;
  unsigned Scale;

  IntelExpr(bool needBracs = false) : NeedBracs(needBracs), Imm(0),
    BaseReg(StringRef()), IndexReg(StringRef()),
    Scale(1) {}
  // Compund immediate expression
  IntelExpr(int64_t imm, bool needBracs) : IntelExpr(needBracs) {
    Imm = imm;
  }
  // [Reg + ImmediateExpression]
  // We don't bother to emit an immediate expression evaluated to zero
  IntelExpr(StringRef reg, int64_t imm = 0, unsigned scale = 0,
    bool needBracs = true) :
    IntelExpr(imm, needBracs) {
    IndexReg = reg;
    if (scale)
      Scale = scale;
  }
  // [BaseReg + IndexReg * ScaleExpression + ImmediateExpression]
  IntelExpr(StringRef baseReg, StringRef indexReg, unsigned scale = 0,
    int64_t imm = 0, bool needBracs = true) :
    IntelExpr(indexReg, imm, scale, needBracs) {
    BaseReg = baseReg;
  }
  bool hasBaseReg() const {
    return BaseReg.size();
  }
  bool hasIndexReg() const {
    return IndexReg.size();
  }
  bool hasRegs() const {
    return hasBaseReg() || hasIndexReg();
  }
  bool isValid() const {
    return (Scale == 1) ||
           (hasIndexReg() && (Scale == 2 || Scale == 4 || Scale == 8));
  }
};

struct AsmRewrite {
  AsmRewriteKind Kind;
  SMLoc Loc;
  unsigned Len;
  int64_t Val;
  StringRef Label;
  IntelExpr IntelExp;

public:
  AsmRewrite(AsmRewriteKind kind, SMLoc loc, unsigned len = 0, int64_t val = 0)
    : Kind(kind), Loc(loc), Len(len), Val(val) {}
  AsmRewrite(AsmRewriteKind kind, SMLoc loc, unsigned len, StringRef label)
    : AsmRewrite(kind, loc, len) { Label = label; }
  AsmRewrite(SMLoc loc, unsigned len, IntelExpr exp)
    : AsmRewrite(AOK_IntelExpr, loc, len) { IntelExp = exp; }
};

struct ParseInstructionInfo {
  SmallVectorImpl<AsmRewrite> *AsmRewrites = nullptr;

  ParseInstructionInfo() = default;
  ParseInstructionInfo(SmallVectorImpl<AsmRewrite> *rewrites)
    : AsmRewrites(rewrites) {}
};

enum OperandMatchResultTy {
  MatchOperand_Success,  // operand matched successfully
  MatchOperand_NoMatch,  // operand did not match
  MatchOperand_ParseFail // operand matched but had errors
};

enum class DiagnosticPredicateTy {
  Match,
  NearMatch,
  NoMatch,
};

// When an operand is parsed, the assembler will try to iterate through a set of
// possible operand classes that the operand might match and call the
// corresponding PredicateMethod to determine that.
//
// If there are two AsmOperands that would give a specific diagnostic if there
// is no match, there is currently no mechanism to distinguish which operand is
// a closer match. The DiagnosticPredicate distinguishes between 'completely
// no match' and 'near match', so the assembler can decide whether to give a
// specific diagnostic, or use 'InvalidOperand' and continue to find a
// 'better matching' diagnostic.
//
// For example:
//    opcode opnd0, onpd1, opnd2
//
// where:
//    opnd2 could be an 'immediate of range [-8, 7]'
//    opnd2 could be a  'register + shift/extend'.
//
// If opnd2 is a valid register, but with a wrong shift/extend suffix, it makes
// little sense to give a diagnostic that the operand should be an immediate
// in range [-8, 7].
//
// This is a light-weight alternative to the 'NearMissInfo' approach
// below which collects *all* possible diagnostics. This alternative
// is optional and fully backward compatible with existing
// PredicateMethods that return a 'bool' (match or no match).
struct DiagnosticPredicate {
  DiagnosticPredicateTy Type;

  explicit DiagnosticPredicate(bool Match)
      : Type(Match ? DiagnosticPredicateTy::Match
                   : DiagnosticPredicateTy::NearMatch) {}
  DiagnosticPredicate(DiagnosticPredicateTy T) : Type(T) {}
  DiagnosticPredicate(const DiagnosticPredicate &) = default;

  operator bool() const { return Type == DiagnosticPredicateTy::Match; }
  bool isMatch() const { return Type == DiagnosticPredicateTy::Match; }
  bool isNearMatch() const { return Type == DiagnosticPredicateTy::NearMatch; }
  bool isNoMatch() const { return Type == DiagnosticPredicateTy::NoMatch; }
};

// When matching of an assembly instruction fails, there may be multiple
// encodings that are close to being a match. It's often ambiguous which one
// the programmer intended to use, so we want to report an error which mentions
// each of these "near-miss" encodings. This struct contains information about
// one such encoding, and why it did not match the parsed instruction.
class NearMissInfo {
public:
  enum NearMissKind {
    NoNearMiss,
    NearMissOperand,
    NearMissFeature,
    NearMissPredicate,
    NearMissTooFewOperands,
  };

  // The encoding is valid for the parsed assembly string. This is only used
  // internally to the table-generated assembly matcher.
  static NearMissInfo getSuccess() { return NearMissInfo(); }

  // The instruction encoding is not valid because it requires some target
  // features that are not currently enabled. MissingFeatures has a bit set for
  // each feature that the encoding needs but which is not enabled.
  static NearMissInfo getMissedFeature(uint64_t MissingFeatures) {
    NearMissInfo Result;
    Result.Kind = NearMissFeature;
    Result.Features = MissingFeatures;
    return Result;
  }

  // The instruction encoding is not valid because the target-specific
  // predicate function returned an error code. FailureCode is the
  // target-specific error code returned by the predicate.
  static NearMissInfo getMissedPredicate(unsigned FailureCode) {
    NearMissInfo Result;
    Result.Kind = NearMissPredicate;
    Result.PredicateError = FailureCode;
    return Result;
  }

  // The instruction encoding is not valid because one (and only one) parsed
  // operand is not of the correct type. OperandError is the error code
  // relating to the operand class expected by the encoding. OperandClass is
  // the type of the expected operand. Opcode is the opcode of the encoding.
  // OperandIndex is the index into the parsed operand list.
  static NearMissInfo getMissedOperand(unsigned OperandError,
                                       unsigned OperandClass, unsigned Opcode,
                                       unsigned OperandIndex) {
    NearMissInfo Result;
    Result.Kind = NearMissOperand;
    Result.MissedOperand.Error = OperandError;
    Result.MissedOperand.Class = OperandClass;
    Result.MissedOperand.Opcode = Opcode;
    Result.MissedOperand.Index = OperandIndex;
    return Result;
  }

  // The instruction encoding is not valid because it expects more operands
  // than were parsed. OperandClass is the class of the expected operand that
  // was not provided. Opcode is the instruction encoding.
  static NearMissInfo getTooFewOperands(unsigned OperandClass,
                                        unsigned Opcode) {
    NearMissInfo Result;
    Result.Kind = NearMissTooFewOperands;
    Result.TooFewOperands.Class = OperandClass;
    Result.TooFewOperands.Opcode = Opcode;
    return Result;
  }

  operator bool() const { return Kind != NoNearMiss; }

  NearMissKind getKind() const { return Kind; }

  // Feature flags required by the instruction, that the current target does
  // not have.
  uint64_t getFeatures() const {
    assert(Kind == NearMissFeature);
    return Features;
  }
  // Error code returned by the target predicate when validating this
  // instruction encoding.
  unsigned getPredicateError() const {
    assert(Kind == NearMissPredicate);
    return PredicateError;
  }
  // MatchClassKind of the operand that we expected to see.
  unsigned getOperandClass() const {
    assert(Kind == NearMissOperand || Kind == NearMissTooFewOperands);
    return MissedOperand.Class;
  }
  // Opcode of the encoding we were trying to match.
  unsigned getOpcode() const {
    assert(Kind == NearMissOperand || Kind == NearMissTooFewOperands);
    return MissedOperand.Opcode;
  }
  // Error code returned when validating the operand.
  unsigned getOperandError() const {
    assert(Kind == NearMissOperand);
    return MissedOperand.Error;
  }
  // Index of the actual operand we were trying to match in the list of parsed
  // operands.
  unsigned getOperandIndex() const {
    assert(Kind == NearMissOperand);
    return MissedOperand.Index;
  }

private:
  NearMissKind Kind;

  // These two structs share a common prefix, so we can safely rely on the fact
  // that they overlap in the union.
  struct MissedOpInfo {
    unsigned Class;
    unsigned Opcode;
    unsigned Error;
    unsigned Index;
  };

  struct TooFewOperandsInfo {
    unsigned Class;
    unsigned Opcode;
  };

  union {
    uint64_t Features;
    unsigned PredicateError;
    MissedOpInfo MissedOperand;
    TooFewOperandsInfo TooFewOperands;
  };

  NearMissInfo() : Kind(NoNearMiss) {}
};

/// MCTargetAsmParser - Generic interface to target specific assembly parsers.
class MCTargetAsmParser : public MCAsmParserExtension {
public:
  enum MatchResultTy {
    Match_InvalidOperand,
    Match_InvalidTiedOperand,
    Match_MissingFeature,
    Match_MnemonicFail,
    Match_Success,
    Match_NearMisses,
    FIRST_TARGET_MATCH_RESULT_TY
  };

protected: // Can only create subclasses.
  MCTargetAsmParser(MCTargetOptions const &, const MCSubtargetInfo &STI,
                    const MCInstrInfo &MII);

  /// Create a copy of STI and return a non-const reference to it.
  MCSubtargetInfo &copySTI();

  /// AvailableFeatures - The current set of available features.
  uint64_t AvailableFeatures = 0;

  /// ParsingInlineAsm - Are we parsing ms-style inline assembly?
  bool ParsingInlineAsm = false;

  /// SemaCallback - The Sema callback implementation.  Must be set when parsing
  /// ms-style inline assembly.
  MCAsmParserSemaCallback *SemaCallback;

  /// Set of options which affects instrumentation of inline assembly.
  MCTargetOptions MCOptions;

  /// Current STI.
  const MCSubtargetInfo *STI;

  const MCInstrInfo &MII;

public:
  MCTargetAsmParser(const MCTargetAsmParser &) = delete;
  MCTargetAsmParser &operator=(const MCTargetAsmParser &) = delete;

  ~MCTargetAsmParser() override;

  const MCSubtargetInfo &getSTI() const;

  uint64_t getAvailableFeatures() const { return AvailableFeatures; }
  void setAvailableFeatures(uint64_t Value) { AvailableFeatures = Value; }

  bool isParsingInlineAsm () { return ParsingInlineAsm; }
  void setParsingInlineAsm (bool Value) { ParsingInlineAsm = Value; }

  MCTargetOptions getTargetOptions() const { return MCOptions; }

  void setSemaCallback(MCAsmParserSemaCallback *Callback) {
    SemaCallback = Callback;
  }

  // Target-specific parsing of expression.
  virtual bool parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc) {
    return getParser().parsePrimaryExpr(Res, EndLoc);
  }

  virtual bool ParseRegister(unsigned &RegNo, SMLoc &StartLoc,
                             SMLoc &EndLoc) = 0;

  /// Sets frame register corresponding to the current MachineFunction.
  virtual void SetFrameRegister(unsigned RegNo) {}

  /// ParseInstruction - Parse one assembly instruction.
  ///
  /// The parser is positioned following the instruction name. The target
  /// specific instruction parser should parse the entire instruction and
  /// construct the appropriate MCInst, or emit an error. On success, the entire
  /// line should be parsed up to and including the end-of-statement token. On
  /// failure, the parser is not required to read to the end of the line.
  //
  /// \param Name - The instruction name.
  /// \param NameLoc - The source location of the name.
  /// \param Operands [out] - The list of parsed operands, this returns
  ///        ownership of them to the caller.
  /// \return True on failure.
  virtual bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                                SMLoc NameLoc, OperandVector &Operands) = 0;
  virtual bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                                AsmToken Token, OperandVector &Operands) {
    return ParseInstruction(Info, Name, Token.getLoc(), Operands);
  }

  /// ParseDirective - Parse a target specific assembler directive
  ///
  /// The parser is positioned following the directive name.  The target
  /// specific directive parser should parse the entire directive doing or
  /// recording any target specific work, or return true and do nothing if the
  /// directive is not target specific. If the directive is specific for
  /// the target, the entire line is parsed up to and including the
  /// end-of-statement token and false is returned.
  ///
  /// \param DirectiveID - the identifier token of the directive.
  virtual bool ParseDirective(AsmToken DirectiveID) = 0;

  /// MatchAndEmitInstruction - Recognize a series of operands of a parsed
  /// instruction as an actual MCInst and emit it to the specified MCStreamer.
  /// This returns false on success and returns true on failure to match.
  ///
  /// On failure, the target parser is responsible for emitting a diagnostic
  /// explaining the match failure.
  virtual bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                       OperandVector &Operands, MCStreamer &Out,
                                       uint64_t &ErrorInfo,
                                       bool MatchingInlineAsm) = 0;

  /// Allows targets to let registers opt out of clobber lists.
  virtual bool OmitRegisterFromClobberLists(unsigned RegNo) { return false; }

  /// Allow a target to add special case operand matching for things that
  /// tblgen doesn't/can't handle effectively. For example, literal
  /// immediates on ARM. TableGen expects a token operand, but the parser
  /// will recognize them as immediates.
  virtual unsigned validateTargetOperandClass(MCParsedAsmOperand &Op,
                                              unsigned Kind) {
    return Match_InvalidOperand;
  }

  /// Validate the instruction match against any complex target predicates
  /// before rendering any operands to it.
  virtual unsigned
  checkEarlyTargetMatchPredicate(MCInst &Inst, const OperandVector &Operands) {
    return Match_Success;
  }

  /// checkTargetMatchPredicate - Validate the instruction match against
  /// any complex target predicates not expressible via match classes.
  virtual unsigned checkTargetMatchPredicate(MCInst &Inst) {
    return Match_Success;
  }

  virtual void convertToMapAndConstraints(unsigned Kind,
                                          const OperandVector &Operands) = 0;

  /// Returns whether two registers are equal and is used by the tied-operands
  /// checks in the AsmMatcher. This method can be overridden allow e.g. a
  /// sub- or super-register as the tied operand.
  virtual bool regsEqual(const MCParsedAsmOperand &Op1,
                         const MCParsedAsmOperand &Op2) const {
    assert(Op1.isReg() && Op2.isReg() && "Operands not all regs");
    return Op1.getReg() == Op2.getReg();
  }

  // Return whether this parser uses assignment statements with equals tokens
  virtual bool equalIsAsmAssignment() { return true; };
  // Return whether this start of statement identifier is a label
  virtual bool isLabel(AsmToken &Token) { return true; };
  // Return whether this parser accept star as start of statement
  virtual bool starIsStartOfStatement() { return false; };

  virtual const MCExpr *applyModifierToExpr(const MCExpr *E,
                                            MCSymbolRefExpr::VariantKind,
                                            MCContext &Ctx) {
    return nullptr;
  }

  // For actions that have to be performed before a label is emitted
  virtual void doBeforeLabelEmit(MCSymbol *Symbol) {}
  
  virtual void onLabelParsed(MCSymbol *Symbol) {}

  /// Ensure that all previously parsed instructions have been emitted to the
  /// output streamer, if the target does not emit them immediately.
  virtual void flushPendingInstructions(MCStreamer &Out) {}

  virtual const MCExpr *createTargetUnaryExpr(const MCExpr *E,
                                              AsmToken::TokenKind OperatorToken,
                                              MCContext &Ctx) {
    return nullptr;
  }

  // For any checks or cleanups at the end of parsing.
  virtual void onEndOfFile() {}
};

} // end namespace llvm

#endif // LLVM_MC_MCPARSER_MCTARGETASMPARSER_H
