//===--- clang/Basic/CharInfo.h - Classifying ASCII Characters --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_CHARINFO_H
#define LLVM_CLANG_BASIC_CHARINFO_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"

namespace clang {
namespace charinfo {
  extern const uint16_t InfoTable[256];

  enum {
    CHAR_HORZ_WS  = 0x0001,  // '\t', '\f', '\v'.  Note, no '\0'
    CHAR_VERT_WS  = 0x0002,  // '\r', '\n'
    CHAR_SPACE    = 0x0004,  // ' '
    CHAR_DIGIT    = 0x0008,  // 0-9
    CHAR_XLETTER  = 0x0010,  // a-f,A-F
    CHAR_UPPER    = 0x0020,  // A-Z
    CHAR_LOWER    = 0x0040,  // a-z
    CHAR_UNDER    = 0x0080,  // _
    CHAR_PERIOD   = 0x0100,  // .
    CHAR_RAWDEL   = 0x0200,  // {}[]#<>%:;?*+-/^&|~!=,"'
    CHAR_PUNCT    = 0x0400   // `$@()
  };

  enum {
    CHAR_XUPPER = CHAR_XLETTER | CHAR_UPPER,
    CHAR_XLOWER = CHAR_XLETTER | CHAR_LOWER
  };
} // end namespace charinfo

/// Returns true if this is an ASCII character.
LLVM_READNONE inline bool isASCII(char c) {
  return static_cast<unsigned char>(c) <= 127;
}

/// Returns true if this is a valid first character of a C identifier,
/// which is [a-zA-Z_].
LLVM_READONLY inline bool isIdentifierHead(unsigned char c,
                                           bool AllowDollar = false) {
  using namespace charinfo;
  if (InfoTable[c] & (CHAR_UPPER|CHAR_LOWER|CHAR_UNDER))
    return true;
  return AllowDollar && c == '$';
}

/// Returns true if this is a body character of a C identifier,
/// which is [a-zA-Z0-9_].
LLVM_READONLY inline bool isIdentifierBody(unsigned char c,
                                           bool AllowDollar = false) {
  using namespace charinfo;
  if (InfoTable[c] & (CHAR_UPPER|CHAR_LOWER|CHAR_DIGIT|CHAR_UNDER))
    return true;
  return AllowDollar && c == '$';
}

/// Returns true if this character is horizontal ASCII whitespace:
/// ' ', '\\t', '\\f', '\\v'.
///
/// Note that this returns false for '\\0'.
LLVM_READONLY inline bool isHorizontalWhitespace(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & (CHAR_HORZ_WS|CHAR_SPACE)) != 0;
}

/// Returns true if this character is vertical ASCII whitespace: '\\n', '\\r'.
///
/// Note that this returns false for '\\0'.
LLVM_READONLY inline bool isVerticalWhitespace(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & CHAR_VERT_WS) != 0;
}

/// Return true if this character is horizontal or vertical ASCII whitespace:
/// ' ', '\\t', '\\f', '\\v', '\\n', '\\r'.
///
/// Note that this returns false for '\\0'.
LLVM_READONLY inline bool isWhitespace(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & (CHAR_HORZ_WS|CHAR_VERT_WS|CHAR_SPACE)) != 0;
}

/// Return true if this character is an ASCII digit: [0-9]
LLVM_READONLY inline bool isDigit(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & CHAR_DIGIT) != 0;
}

/// Return true if this character is a lowercase ASCII letter: [a-z]
LLVM_READONLY inline bool isLowercase(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & CHAR_LOWER) != 0;
}

/// Return true if this character is an uppercase ASCII letter: [A-Z]
LLVM_READONLY inline bool isUppercase(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & CHAR_UPPER) != 0;
}

/// Return true if this character is an ASCII letter: [a-zA-Z]
LLVM_READONLY inline bool isLetter(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & (CHAR_UPPER|CHAR_LOWER)) != 0;
}

/// Return true if this character is an ASCII letter or digit: [a-zA-Z0-9]
LLVM_READONLY inline bool isAlphanumeric(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & (CHAR_DIGIT|CHAR_UPPER|CHAR_LOWER)) != 0;
}

/// Return true if this character is an ASCII hex digit: [0-9a-fA-F]
LLVM_READONLY inline bool isHexDigit(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & (CHAR_DIGIT|CHAR_XLETTER)) != 0;
}

/// Return true if this character is an ASCII punctuation character.
///
/// Note that '_' is both a punctuation character and an identifier character!
LLVM_READONLY inline bool isPunctuation(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & (CHAR_UNDER|CHAR_PERIOD|CHAR_RAWDEL|CHAR_PUNCT)) != 0;
}

/// Return true if this character is an ASCII printable character; that is, a
/// character that should take exactly one column to print in a fixed-width
/// terminal.
LLVM_READONLY inline bool isPrintable(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & (CHAR_UPPER|CHAR_LOWER|CHAR_PERIOD|CHAR_PUNCT|
                          CHAR_DIGIT|CHAR_UNDER|CHAR_RAWDEL|CHAR_SPACE)) != 0;
}

/// Return true if this is the body character of a C preprocessing number,
/// which is [a-zA-Z0-9_.].
LLVM_READONLY inline bool isPreprocessingNumberBody(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] &
          (CHAR_UPPER|CHAR_LOWER|CHAR_DIGIT|CHAR_UNDER|CHAR_PERIOD)) != 0;
}

/// Return true if this is the body character of a C++ raw string delimiter.
LLVM_READONLY inline bool isRawStringDelimBody(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & (CHAR_UPPER|CHAR_LOWER|CHAR_PERIOD|
                          CHAR_DIGIT|CHAR_UNDER|CHAR_RAWDEL)) != 0;
}


/// Converts the given ASCII character to its lowercase equivalent.
///
/// If the character is not an uppercase character, it is returned as is.
LLVM_READONLY inline char toLowercase(char c) {
  if (isUppercase(c))
    return c + 'a' - 'A';
  return c;
}

/// Converts the given ASCII character to its uppercase equivalent.
///
/// If the character is not a lowercase character, it is returned as is.
LLVM_READONLY inline char toUppercase(char c) {
  if (isLowercase(c))
    return c + 'A' - 'a';
  return c;
}


/// Return true if this is a valid ASCII identifier.
///
/// Note that this is a very simple check; it does not accept UCNs as valid
/// identifier characters.
LLVM_READONLY inline bool isValidIdentifier(StringRef S,
                                            bool AllowDollar = false) {
  if (S.empty() || !isIdentifierHead(S[0], AllowDollar))
    return false;

  for (StringRef::iterator I = S.begin(), E = S.end(); I != E; ++I)
    if (!isIdentifierBody(*I, AllowDollar))
      return false;

  return true;
}

} // end namespace clang

#endif
