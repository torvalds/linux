//===- AsmLexer.cpp - Lexer for Assembly Files ----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class implements the lexer for assembly files.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/SaveAndRestore.h"
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <tuple>
#include <utility>

using namespace llvm;

AsmLexer::AsmLexer(const MCAsmInfo &MAI) : MAI(MAI) {
  AllowAtInIdentifier = !StringRef(MAI.getCommentString()).startswith("@");
}

AsmLexer::~AsmLexer() = default;

void AsmLexer::setBuffer(StringRef Buf, const char *ptr) {
  CurBuf = Buf;

  if (ptr)
    CurPtr = ptr;
  else
    CurPtr = CurBuf.begin();

  TokStart = nullptr;
}

/// ReturnError - Set the error to the specified string at the specified
/// location.  This is defined to always return AsmToken::Error.
AsmToken AsmLexer::ReturnError(const char *Loc, const std::string &Msg) {
  SetError(SMLoc::getFromPointer(Loc), Msg);

  return AsmToken(AsmToken::Error, StringRef(Loc, CurPtr - Loc));
}

int AsmLexer::getNextChar() {
  if (CurPtr == CurBuf.end())
    return EOF;
  return (unsigned char)*CurPtr++;
}

/// LexFloatLiteral: [0-9]*[.][0-9]*([eE][+-]?[0-9]*)?
///
/// The leading integral digit sequence and dot should have already been
/// consumed, some or all of the fractional digit sequence *can* have been
/// consumed.
AsmToken AsmLexer::LexFloatLiteral() {
  // Skip the fractional digit sequence.
  while (isDigit(*CurPtr))
    ++CurPtr;

  // Check for exponent; we intentionally accept a slighlty wider set of
  // literals here and rely on the upstream client to reject invalid ones (e.g.,
  // "1e+").
  if (*CurPtr == 'e' || *CurPtr == 'E') {
    ++CurPtr;
    if (*CurPtr == '-' || *CurPtr == '+')
      ++CurPtr;
    while (isDigit(*CurPtr))
      ++CurPtr;
  }

  return AsmToken(AsmToken::Real,
                  StringRef(TokStart, CurPtr - TokStart));
}

/// LexHexFloatLiteral matches essentially (.[0-9a-fA-F]*)?[pP][+-]?[0-9a-fA-F]+
/// while making sure there are enough actual digits around for the constant to
/// be valid.
///
/// The leading "0x[0-9a-fA-F]*" (i.e. integer part) has already been consumed
/// before we get here.
AsmToken AsmLexer::LexHexFloatLiteral(bool NoIntDigits) {
  assert((*CurPtr == 'p' || *CurPtr == 'P' || *CurPtr == '.') &&
         "unexpected parse state in floating hex");
  bool NoFracDigits = true;

  // Skip the fractional part if there is one
  if (*CurPtr == '.') {
    ++CurPtr;

    const char *FracStart = CurPtr;
    while (isHexDigit(*CurPtr))
      ++CurPtr;

    NoFracDigits = CurPtr == FracStart;
  }

  if (NoIntDigits && NoFracDigits)
    return ReturnError(TokStart, "invalid hexadecimal floating-point constant: "
                                 "expected at least one significand digit");

  // Make sure we do have some kind of proper exponent part
  if (*CurPtr != 'p' && *CurPtr != 'P')
    return ReturnError(TokStart, "invalid hexadecimal floating-point constant: "
                                 "expected exponent part 'p'");
  ++CurPtr;

  if (*CurPtr == '+' || *CurPtr == '-')
    ++CurPtr;

  // N.b. exponent digits are *not* hex
  const char *ExpStart = CurPtr;
  while (isDigit(*CurPtr))
    ++CurPtr;

  if (CurPtr == ExpStart)
    return ReturnError(TokStart, "invalid hexadecimal floating-point constant: "
                                 "expected at least one exponent digit");

  return AsmToken(AsmToken::Real, StringRef(TokStart, CurPtr - TokStart));
}

/// LexIdentifier: [a-zA-Z_.][a-zA-Z0-9_$.@?]*
static bool IsIdentifierChar(char c, bool AllowAt) {
  return isAlnum(c) || c == '_' || c == '$' || c == '.' ||
         (c == '@' && AllowAt) || c == '?';
}

AsmToken AsmLexer::LexIdentifier() {
  // Check for floating point literals.
  if (CurPtr[-1] == '.' && isDigit(*CurPtr)) {
    // Disambiguate a .1243foo identifier from a floating literal.
    while (isDigit(*CurPtr))
      ++CurPtr;
    if (*CurPtr == 'e' || *CurPtr == 'E' ||
        !IsIdentifierChar(*CurPtr, AllowAtInIdentifier))
      return LexFloatLiteral();
  }

  while (IsIdentifierChar(*CurPtr, AllowAtInIdentifier))
    ++CurPtr;

  // Handle . as a special case.
  if (CurPtr == TokStart+1 && TokStart[0] == '.')
    return AsmToken(AsmToken::Dot, StringRef(TokStart, 1));

  return AsmToken(AsmToken::Identifier, StringRef(TokStart, CurPtr - TokStart));
}

/// LexSlash: Slash: /
///           C-Style Comment: /* ... */
AsmToken AsmLexer::LexSlash() {
  switch (*CurPtr) {
  case '*':
    IsAtStartOfStatement = false;
    break; // C style comment.
  case '/':
    ++CurPtr;
    return LexLineComment();
  default:
    IsAtStartOfStatement = false;
    return AsmToken(AsmToken::Slash, StringRef(TokStart, 1));
  }

  // C Style comment.
  ++CurPtr;  // skip the star.
  const char *CommentTextStart = CurPtr;
  while (CurPtr != CurBuf.end()) {
    switch (*CurPtr++) {
    case '*':
      // End of the comment?
      if (*CurPtr != '/')
        break;
      // If we have a CommentConsumer, notify it about the comment.
      if (CommentConsumer) {
        CommentConsumer->HandleComment(
            SMLoc::getFromPointer(CommentTextStart),
            StringRef(CommentTextStart, CurPtr - 1 - CommentTextStart));
      }
      ++CurPtr;   // End the */.
      return AsmToken(AsmToken::Comment,
                      StringRef(TokStart, CurPtr - TokStart));
    }
  }
  return ReturnError(TokStart, "unterminated comment");
}

/// LexLineComment: Comment: #[^\n]*
///                        : //[^\n]*
AsmToken AsmLexer::LexLineComment() {
  // Mark This as an end of statement with a body of the
  // comment. While it would be nicer to leave this two tokens,
  // backwards compatability with TargetParsers makes keeping this in this form
  // better.
  const char *CommentTextStart = CurPtr;
  int CurChar = getNextChar();
  while (CurChar != '\n' && CurChar != '\r' && CurChar != EOF)
    CurChar = getNextChar();
  if (CurChar == '\r' && CurPtr != CurBuf.end() && *CurPtr == '\n')
    ++CurPtr;

  // If we have a CommentConsumer, notify it about the comment.
  if (CommentConsumer) {
    CommentConsumer->HandleComment(
        SMLoc::getFromPointer(CommentTextStart),
        StringRef(CommentTextStart, CurPtr - 1 - CommentTextStart));
  }

  IsAtStartOfLine = true;
  // This is a whole line comment. leave newline
  if (IsAtStartOfStatement)
    return AsmToken(AsmToken::EndOfStatement,
                    StringRef(TokStart, CurPtr - TokStart));
  IsAtStartOfStatement = true;

  return AsmToken(AsmToken::EndOfStatement,
                  StringRef(TokStart, CurPtr - 1 - TokStart));
}

static void SkipIgnoredIntegerSuffix(const char *&CurPtr) {
  // Skip ULL, UL, U, L and LL suffices.
  if (CurPtr[0] == 'U')
    ++CurPtr;
  if (CurPtr[0] == 'L')
    ++CurPtr;
  if (CurPtr[0] == 'L')
    ++CurPtr;
}

// Look ahead to search for first non-hex digit, if it's [hH], then we treat the
// integer as a hexadecimal, possibly with leading zeroes.
static unsigned doHexLookAhead(const char *&CurPtr, unsigned DefaultRadix,
                               bool LexHex) {
  const char *FirstNonDec = nullptr;
  const char *LookAhead = CurPtr;
  while (true) {
    if (isDigit(*LookAhead)) {
      ++LookAhead;
    } else {
      if (!FirstNonDec)
        FirstNonDec = LookAhead;

      // Keep going if we are looking for a 'h' suffix.
      if (LexHex && isHexDigit(*LookAhead))
        ++LookAhead;
      else
        break;
    }
  }
  bool isHex = LexHex && (*LookAhead == 'h' || *LookAhead == 'H');
  CurPtr = isHex || !FirstNonDec ? LookAhead : FirstNonDec;
  if (isHex)
    return 16;
  return DefaultRadix;
}

static AsmToken intToken(StringRef Ref, APInt &Value)
{
  if (Value.isIntN(64))
    return AsmToken(AsmToken::Integer, Ref, Value);
  return AsmToken(AsmToken::BigNum, Ref, Value);
}

/// LexDigit: First character is [0-9].
///   Local Label: [0-9][:]
///   Forward/Backward Label: [0-9][fb]
///   Binary integer: 0b[01]+
///   Octal integer: 0[0-7]+
///   Hex integer: 0x[0-9a-fA-F]+ or [0x]?[0-9][0-9a-fA-F]*[hH]
///   Decimal integer: [1-9][0-9]*
AsmToken AsmLexer::LexDigit() {
  // MASM-flavor binary integer: [01]+[bB]
  // MASM-flavor hexadecimal integer: [0-9][0-9a-fA-F]*[hH]
  if (LexMasmIntegers && isdigit(CurPtr[-1])) {
    const char *FirstNonBinary = (CurPtr[-1] != '0' && CurPtr[-1] != '1') ?
                                   CurPtr - 1 : nullptr;
    const char *OldCurPtr = CurPtr;
    while (isHexDigit(*CurPtr)) {
      if (*CurPtr != '0' && *CurPtr != '1' && !FirstNonBinary)
        FirstNonBinary = CurPtr;
      ++CurPtr;
    }

    unsigned Radix = 0;
    if (*CurPtr == 'h' || *CurPtr == 'H') {
      // hexadecimal number
      ++CurPtr;
      Radix = 16;
    } else if (FirstNonBinary && FirstNonBinary + 1 == CurPtr &&
               (*FirstNonBinary == 'b' || *FirstNonBinary == 'B'))
      Radix = 2;

    if (Radix == 2 || Radix == 16) {
      StringRef Result(TokStart, CurPtr - TokStart);
      APInt Value(128, 0, true);

      if (Result.drop_back().getAsInteger(Radix, Value))
        return ReturnError(TokStart, Radix == 2 ? "invalid binary number" :
                             "invalid hexdecimal number");

      // MSVC accepts and ignores type suffices on integer literals.
      SkipIgnoredIntegerSuffix(CurPtr);

      return intToken(Result, Value);
   }

    // octal/decimal integers, or floating point numbers, fall through
    CurPtr = OldCurPtr;
  }

  // Decimal integer: [1-9][0-9]*
  if (CurPtr[-1] != '0' || CurPtr[0] == '.') {
    unsigned Radix = doHexLookAhead(CurPtr, 10, LexMasmIntegers);
    bool isHex = Radix == 16;
    // Check for floating point literals.
    if (!isHex && (*CurPtr == '.' || *CurPtr == 'e')) {
      ++CurPtr;
      return LexFloatLiteral();
    }

    StringRef Result(TokStart, CurPtr - TokStart);

    APInt Value(128, 0, true);
    if (Result.getAsInteger(Radix, Value))
      return ReturnError(TokStart, !isHex ? "invalid decimal number" :
                           "invalid hexdecimal number");

    // Consume the [hH].
    if (LexMasmIntegers && Radix == 16)
      ++CurPtr;

    // The darwin/x86 (and x86-64) assembler accepts and ignores type
    // suffices on integer literals.
    SkipIgnoredIntegerSuffix(CurPtr);

    return intToken(Result, Value);
  }

  if (!LexMasmIntegers && ((*CurPtr == 'b') || (*CurPtr == 'B'))) {
    ++CurPtr;
    // See if we actually have "0b" as part of something like "jmp 0b\n"
    if (!isDigit(CurPtr[0])) {
      --CurPtr;
      StringRef Result(TokStart, CurPtr - TokStart);
      return AsmToken(AsmToken::Integer, Result, 0);
    }
    const char *NumStart = CurPtr;
    while (CurPtr[0] == '0' || CurPtr[0] == '1')
      ++CurPtr;

    // Requires at least one binary digit.
    if (CurPtr == NumStart)
      return ReturnError(TokStart, "invalid binary number");

    StringRef Result(TokStart, CurPtr - TokStart);

    APInt Value(128, 0, true);
    if (Result.substr(2).getAsInteger(2, Value))
      return ReturnError(TokStart, "invalid binary number");

    // The darwin/x86 (and x86-64) assembler accepts and ignores ULL and LL
    // suffixes on integer literals.
    SkipIgnoredIntegerSuffix(CurPtr);

    return intToken(Result, Value);
  }

  if ((*CurPtr == 'x') || (*CurPtr == 'X')) {
    ++CurPtr;
    const char *NumStart = CurPtr;
    while (isHexDigit(CurPtr[0]))
      ++CurPtr;

    // "0x.0p0" is valid, and "0x0p0" (but not "0xp0" for example, which will be
    // diagnosed by LexHexFloatLiteral).
    if (CurPtr[0] == '.' || CurPtr[0] == 'p' || CurPtr[0] == 'P')
      return LexHexFloatLiteral(NumStart == CurPtr);

    // Otherwise requires at least one hex digit.
    if (CurPtr == NumStart)
      return ReturnError(CurPtr-2, "invalid hexadecimal number");

    APInt Result(128, 0);
    if (StringRef(TokStart, CurPtr - TokStart).getAsInteger(0, Result))
      return ReturnError(TokStart, "invalid hexadecimal number");

    // Consume the optional [hH].
    if (LexMasmIntegers && (*CurPtr == 'h' || *CurPtr == 'H'))
      ++CurPtr;

    // The darwin/x86 (and x86-64) assembler accepts and ignores ULL and LL
    // suffixes on integer literals.
    SkipIgnoredIntegerSuffix(CurPtr);

    return intToken(StringRef(TokStart, CurPtr - TokStart), Result);
  }

  // Either octal or hexadecimal.
  APInt Value(128, 0, true);
  unsigned Radix = doHexLookAhead(CurPtr, 8, LexMasmIntegers);
  bool isHex = Radix == 16;
  StringRef Result(TokStart, CurPtr - TokStart);
  if (Result.getAsInteger(Radix, Value))
    return ReturnError(TokStart, !isHex ? "invalid octal number" :
                       "invalid hexdecimal number");

  // Consume the [hH].
  if (Radix == 16)
    ++CurPtr;

  // The darwin/x86 (and x86-64) assembler accepts and ignores ULL and LL
  // suffixes on integer literals.
  SkipIgnoredIntegerSuffix(CurPtr);

  return intToken(Result, Value);
}

/// LexSingleQuote: Integer: 'b'
AsmToken AsmLexer::LexSingleQuote() {
  int CurChar = getNextChar();

  if (CurChar == '\\')
    CurChar = getNextChar();

  if (CurChar == EOF)
    return ReturnError(TokStart, "unterminated single quote");

  CurChar = getNextChar();

  if (CurChar != '\'')
    return ReturnError(TokStart, "single quote way too long");

  // The idea here being that 'c' is basically just an integral
  // constant.
  StringRef Res = StringRef(TokStart,CurPtr - TokStart);
  long long Value;

  if (Res.startswith("\'\\")) {
    char theChar = Res[2];
    switch (theChar) {
      default: Value = theChar; break;
      case '\'': Value = '\''; break;
      case 't': Value = '\t'; break;
      case 'n': Value = '\n'; break;
      case 'b': Value = '\b'; break;
    }
  } else
    Value = TokStart[1];

  return AsmToken(AsmToken::Integer, Res, Value);
}

/// LexQuote: String: "..."
AsmToken AsmLexer::LexQuote() {
  int CurChar = getNextChar();
  // TODO: does gas allow multiline string constants?
  while (CurChar != '"') {
    if (CurChar == '\\') {
      // Allow \", etc.
      CurChar = getNextChar();
    }

    if (CurChar == EOF)
      return ReturnError(TokStart, "unterminated string constant");

    CurChar = getNextChar();
  }

  return AsmToken(AsmToken::String, StringRef(TokStart, CurPtr - TokStart));
}

StringRef AsmLexer::LexUntilEndOfStatement() {
  TokStart = CurPtr;

  while (!isAtStartOfComment(CurPtr) &&     // Start of line comment.
         !isAtStatementSeparator(CurPtr) && // End of statement marker.
         *CurPtr != '\n' && *CurPtr != '\r' && CurPtr != CurBuf.end()) {
    ++CurPtr;
  }
  return StringRef(TokStart, CurPtr-TokStart);
}

StringRef AsmLexer::LexUntilEndOfLine() {
  TokStart = CurPtr;

  while (*CurPtr != '\n' && *CurPtr != '\r' && CurPtr != CurBuf.end()) {
    ++CurPtr;
  }
  return StringRef(TokStart, CurPtr-TokStart);
}

size_t AsmLexer::peekTokens(MutableArrayRef<AsmToken> Buf,
                            bool ShouldSkipSpace) {
  SaveAndRestore<const char *> SavedTokenStart(TokStart);
  SaveAndRestore<const char *> SavedCurPtr(CurPtr);
  SaveAndRestore<bool> SavedAtStartOfLine(IsAtStartOfLine);
  SaveAndRestore<bool> SavedAtStartOfStatement(IsAtStartOfStatement);
  SaveAndRestore<bool> SavedSkipSpace(SkipSpace, ShouldSkipSpace);
  SaveAndRestore<bool> SavedIsPeeking(IsPeeking, true);
  std::string SavedErr = getErr();
  SMLoc SavedErrLoc = getErrLoc();

  size_t ReadCount;
  for (ReadCount = 0; ReadCount < Buf.size(); ++ReadCount) {
    AsmToken Token = LexToken();

    Buf[ReadCount] = Token;

    if (Token.is(AsmToken::Eof))
      break;
  }

  SetError(SavedErrLoc, SavedErr);
  return ReadCount;
}

bool AsmLexer::isAtStartOfComment(const char *Ptr) {
  StringRef CommentString = MAI.getCommentString();

  if (CommentString.size() == 1)
    return CommentString[0] == Ptr[0];

  // Allow # preprocessor commments also be counted as comments for "##" cases
  if (CommentString[1] == '#')
    return CommentString[0] == Ptr[0];

  return strncmp(Ptr, CommentString.data(), CommentString.size()) == 0;
}

bool AsmLexer::isAtStatementSeparator(const char *Ptr) {
  return strncmp(Ptr, MAI.getSeparatorString(),
                 strlen(MAI.getSeparatorString())) == 0;
}

AsmToken AsmLexer::LexToken() {
  TokStart = CurPtr;
  // This always consumes at least one character.
  int CurChar = getNextChar();

  if (!IsPeeking && CurChar == '#' && IsAtStartOfStatement) {
    // If this starts with a '#', this may be a cpp
    // hash directive and otherwise a line comment.
    AsmToken TokenBuf[2];
    MutableArrayRef<AsmToken> Buf(TokenBuf, 2);
    size_t num = peekTokens(Buf, true);
    // There cannot be a space preceeding this
    if (IsAtStartOfLine && num == 2 && TokenBuf[0].is(AsmToken::Integer) &&
        TokenBuf[1].is(AsmToken::String)) {
      CurPtr = TokStart; // reset curPtr;
      StringRef s = LexUntilEndOfLine();
      UnLex(TokenBuf[1]);
      UnLex(TokenBuf[0]);
      return AsmToken(AsmToken::HashDirective, s);
    }
    return LexLineComment();
  }

  if (isAtStartOfComment(TokStart))
    return LexLineComment();

  if (isAtStatementSeparator(TokStart)) {
    CurPtr += strlen(MAI.getSeparatorString()) - 1;
    IsAtStartOfLine = true;
    IsAtStartOfStatement = true;
    return AsmToken(AsmToken::EndOfStatement,
                    StringRef(TokStart, strlen(MAI.getSeparatorString())));
  }

  // If we're missing a newline at EOF, make sure we still get an
  // EndOfStatement token before the Eof token.
  if (CurChar == EOF && !IsAtStartOfStatement) {
    IsAtStartOfLine = true;
    IsAtStartOfStatement = true;
    return AsmToken(AsmToken::EndOfStatement, StringRef(TokStart, 1));
  }
  IsAtStartOfLine = false;
  bool OldIsAtStartOfStatement = IsAtStartOfStatement;
  IsAtStartOfStatement = false;
  switch (CurChar) {
  default:
    // Handle identifier: [a-zA-Z_.][a-zA-Z0-9_$.@]*
    if (isalpha(CurChar) || CurChar == '_' || CurChar == '.')
      return LexIdentifier();

    // Unknown character, emit an error.
    return ReturnError(TokStart, "invalid character in input");
  case EOF:
    IsAtStartOfLine = true;
    IsAtStartOfStatement = true;
    return AsmToken(AsmToken::Eof, StringRef(TokStart, 0));
  case 0:
  case ' ':
  case '\t':
    IsAtStartOfStatement = OldIsAtStartOfStatement;
    while (*CurPtr == ' ' || *CurPtr == '\t')
      CurPtr++;
    if (SkipSpace)
      return LexToken(); // Ignore whitespace.
    else
      return AsmToken(AsmToken::Space, StringRef(TokStart, CurPtr - TokStart));
  case '\r': {
    IsAtStartOfLine = true;
    IsAtStartOfStatement = true;
    // If this is a CR followed by LF, treat that as one token.
    if (CurPtr != CurBuf.end() && *CurPtr == '\n')
      ++CurPtr;
    return AsmToken(AsmToken::EndOfStatement,
                    StringRef(TokStart, CurPtr - TokStart));
  }
  case '\n':
    IsAtStartOfLine = true;
    IsAtStartOfStatement = true;
    return AsmToken(AsmToken::EndOfStatement, StringRef(TokStart, 1));
  case ':': return AsmToken(AsmToken::Colon, StringRef(TokStart, 1));
  case '+': return AsmToken(AsmToken::Plus, StringRef(TokStart, 1));
  case '~': return AsmToken(AsmToken::Tilde, StringRef(TokStart, 1));
  case '(': return AsmToken(AsmToken::LParen, StringRef(TokStart, 1));
  case ')': return AsmToken(AsmToken::RParen, StringRef(TokStart, 1));
  case '[': return AsmToken(AsmToken::LBrac, StringRef(TokStart, 1));
  case ']': return AsmToken(AsmToken::RBrac, StringRef(TokStart, 1));
  case '{': return AsmToken(AsmToken::LCurly, StringRef(TokStart, 1));
  case '}': return AsmToken(AsmToken::RCurly, StringRef(TokStart, 1));
  case '*': return AsmToken(AsmToken::Star, StringRef(TokStart, 1));
  case ',': return AsmToken(AsmToken::Comma, StringRef(TokStart, 1));
  case '$': return AsmToken(AsmToken::Dollar, StringRef(TokStart, 1));
  case '@': return AsmToken(AsmToken::At, StringRef(TokStart, 1));
  case '\\': return AsmToken(AsmToken::BackSlash, StringRef(TokStart, 1));
  case '=':
    if (*CurPtr == '=') {
      ++CurPtr;
      return AsmToken(AsmToken::EqualEqual, StringRef(TokStart, 2));
    }
    return AsmToken(AsmToken::Equal, StringRef(TokStart, 1));
  case '-':
    if (*CurPtr == '>') {
      ++CurPtr;
      return AsmToken(AsmToken::MinusGreater, StringRef(TokStart, 2));
    }
    return AsmToken(AsmToken::Minus, StringRef(TokStart, 1));
  case '|':
    if (*CurPtr == '|') {
      ++CurPtr;
      return AsmToken(AsmToken::PipePipe, StringRef(TokStart, 2));
    }
    return AsmToken(AsmToken::Pipe, StringRef(TokStart, 1));
  case '^': return AsmToken(AsmToken::Caret, StringRef(TokStart, 1));
  case '&':
    if (*CurPtr == '&') {
      ++CurPtr;
      return AsmToken(AsmToken::AmpAmp, StringRef(TokStart, 2));
    }
    return AsmToken(AsmToken::Amp, StringRef(TokStart, 1));
  case '!':
    if (*CurPtr == '=') {
      ++CurPtr;
      return AsmToken(AsmToken::ExclaimEqual, StringRef(TokStart, 2));
    }
    return AsmToken(AsmToken::Exclaim, StringRef(TokStart, 1));
  case '%':
    if (MAI.hasMipsExpressions()) {
      AsmToken::TokenKind Operator;
      unsigned OperatorLength;

      std::tie(Operator, OperatorLength) =
          StringSwitch<std::pair<AsmToken::TokenKind, unsigned>>(
              StringRef(CurPtr))
              .StartsWith("call16", {AsmToken::PercentCall16, 7})
              .StartsWith("call_hi", {AsmToken::PercentCall_Hi, 8})
              .StartsWith("call_lo", {AsmToken::PercentCall_Lo, 8})
              .StartsWith("dtprel_hi", {AsmToken::PercentDtprel_Hi, 10})
              .StartsWith("dtprel_lo", {AsmToken::PercentDtprel_Lo, 10})
              .StartsWith("got_disp", {AsmToken::PercentGot_Disp, 9})
              .StartsWith("got_hi", {AsmToken::PercentGot_Hi, 7})
              .StartsWith("got_lo", {AsmToken::PercentGot_Lo, 7})
              .StartsWith("got_ofst", {AsmToken::PercentGot_Ofst, 9})
              .StartsWith("got_page", {AsmToken::PercentGot_Page, 9})
              .StartsWith("gottprel", {AsmToken::PercentGottprel, 9})
              .StartsWith("got", {AsmToken::PercentGot, 4})
              .StartsWith("gp_rel", {AsmToken::PercentGp_Rel, 7})
              .StartsWith("higher", {AsmToken::PercentHigher, 7})
              .StartsWith("highest", {AsmToken::PercentHighest, 8})
              .StartsWith("hi", {AsmToken::PercentHi, 3})
              .StartsWith("lo", {AsmToken::PercentLo, 3})
              .StartsWith("neg", {AsmToken::PercentNeg, 4})
              .StartsWith("pcrel_hi", {AsmToken::PercentPcrel_Hi, 9})
              .StartsWith("pcrel_lo", {AsmToken::PercentPcrel_Lo, 9})
              .StartsWith("tlsgd", {AsmToken::PercentTlsgd, 6})
              .StartsWith("tlsldm", {AsmToken::PercentTlsldm, 7})
              .StartsWith("tprel_hi", {AsmToken::PercentTprel_Hi, 9})
              .StartsWith("tprel_lo", {AsmToken::PercentTprel_Lo, 9})
              .Default({AsmToken::Percent, 1});

      if (Operator != AsmToken::Percent) {
        CurPtr += OperatorLength - 1;
        return AsmToken(Operator, StringRef(TokStart, OperatorLength));
      }
    }
    return AsmToken(AsmToken::Percent, StringRef(TokStart, 1));
  case '/':
    IsAtStartOfStatement = OldIsAtStartOfStatement;
    return LexSlash();
  case '#': return AsmToken(AsmToken::Hash, StringRef(TokStart, 1));
  case '\'': return LexSingleQuote();
  case '"': return LexQuote();
  case '0': case '1': case '2': case '3': case '4':
  case '5': case '6': case '7': case '8': case '9':
    return LexDigit();
  case '<':
    switch (*CurPtr) {
    case '<':
      ++CurPtr;
      return AsmToken(AsmToken::LessLess, StringRef(TokStart, 2));
    case '=':
      ++CurPtr;
      return AsmToken(AsmToken::LessEqual, StringRef(TokStart, 2));
    case '>':
      ++CurPtr;
      return AsmToken(AsmToken::LessGreater, StringRef(TokStart, 2));
    default:
      return AsmToken(AsmToken::Less, StringRef(TokStart, 1));
    }
  case '>':
    switch (*CurPtr) {
    case '>':
      ++CurPtr;
      return AsmToken(AsmToken::GreaterGreater, StringRef(TokStart, 2));
    case '=':
      ++CurPtr;
      return AsmToken(AsmToken::GreaterEqual, StringRef(TokStart, 2));
    default:
      return AsmToken(AsmToken::Greater, StringRef(TokStart, 1));
    }

  // TODO: Quoted identifiers (objc methods etc)
  // local labels: [0-9][:]
  // Forward/backward labels: [0-9][fb]
  // Integers, fp constants, character constants.
  }
}
