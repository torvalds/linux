//===- MCAsmMacro.h - Assembly Macros ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASMMACRO_H
#define LLVM_MC_MCASMMACRO_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/SMLoc.h"
#include <vector>

namespace llvm {

/// Target independent representation for an assembler token.
class AsmToken {
public:
  enum TokenKind {
    // Markers
    Eof, Error,

    // String values.
    Identifier,
    String,

    // Integer values.
    Integer,
    BigNum, // larger than 64 bits

    // Real values.
    Real,

    // Comments
    Comment,
    HashDirective,
    // No-value.
    EndOfStatement,
    Colon,
    Space,
    Plus, Minus, Tilde,
    Slash,     // '/'
    BackSlash, // '\'
    LParen, RParen, LBrac, RBrac, LCurly, RCurly,
    Star, Dot, Comma, Dollar, Equal, EqualEqual,

    Pipe, PipePipe, Caret,
    Amp, AmpAmp, Exclaim, ExclaimEqual, Percent, Hash,
    Less, LessEqual, LessLess, LessGreater,
    Greater, GreaterEqual, GreaterGreater, At, MinusGreater,

    // MIPS unary expression operators such as %neg.
    PercentCall16, PercentCall_Hi, PercentCall_Lo, PercentDtprel_Hi,
    PercentDtprel_Lo, PercentGot, PercentGot_Disp, PercentGot_Hi, PercentGot_Lo,
    PercentGot_Ofst, PercentGot_Page, PercentGottprel, PercentGp_Rel, PercentHi,
    PercentHigher, PercentHighest, PercentLo, PercentNeg, PercentPcrel_Hi,
    PercentPcrel_Lo, PercentTlsgd, PercentTlsldm, PercentTprel_Hi,
    PercentTprel_Lo
  };

private:
  TokenKind Kind;

  /// A reference to the entire token contents; this is always a pointer into
  /// a memory buffer owned by the source manager.
  StringRef Str;

  APInt IntVal;

public:
  AsmToken() = default;
  AsmToken(TokenKind Kind, StringRef Str, APInt IntVal)
      : Kind(Kind), Str(Str), IntVal(std::move(IntVal)) {}
  AsmToken(TokenKind Kind, StringRef Str, int64_t IntVal = 0)
      : Kind(Kind), Str(Str), IntVal(64, IntVal, true) {}

  TokenKind getKind() const { return Kind; }
  bool is(TokenKind K) const { return Kind == K; }
  bool isNot(TokenKind K) const { return Kind != K; }

  SMLoc getLoc() const;
  SMLoc getEndLoc() const;
  SMRange getLocRange() const;

  /// Get the contents of a string token (without quotes).
  StringRef getStringContents() const {
    assert(Kind == String && "This token isn't a string!");
    return Str.slice(1, Str.size() - 1);
  }

  /// Get the identifier string for the current token, which should be an
  /// identifier or a string. This gets the portion of the string which should
  /// be used as the identifier, e.g., it does not include the quotes on
  /// strings.
  StringRef getIdentifier() const {
    if (Kind == Identifier)
      return getString();
    return getStringContents();
  }

  /// Get the string for the current token, this includes all characters (for
  /// example, the quotes on strings) in the token.
  ///
  /// The returned StringRef points into the source manager's memory buffer, and
  /// is safe to store across calls to Lex().
  StringRef getString() const { return Str; }

  // FIXME: Don't compute this in advance, it makes every token larger, and is
  // also not generally what we want (it is nicer for recovery etc. to lex 123br
  // as a single token, then diagnose as an invalid number).
  int64_t getIntVal() const {
    assert(Kind == Integer && "This token isn't an integer!");
    return IntVal.getZExtValue();
  }

  APInt getAPIntVal() const {
    assert((Kind == Integer || Kind == BigNum) &&
           "This token isn't an integer!");
    return IntVal;
  }

  void dump(raw_ostream &OS) const;
  void dump() const { dump(dbgs()); }
};

struct MCAsmMacroParameter {
  StringRef Name;
  std::vector<AsmToken> Value;
  bool Required = false;
  bool Vararg = false;

  MCAsmMacroParameter() = default;

  void dump() const { dump(dbgs()); }
  void dump(raw_ostream &OS) const;
};

typedef std::vector<MCAsmMacroParameter> MCAsmMacroParameters;
struct MCAsmMacro {
  StringRef Name;
  StringRef Body;
  MCAsmMacroParameters Parameters;

public:
  MCAsmMacro(StringRef N, StringRef B, MCAsmMacroParameters P)
      : Name(N), Body(B), Parameters(std::move(P)) {}

  void dump() const { dump(dbgs()); }
  void dump(raw_ostream &OS) const;
};
} // namespace llvm

#endif
