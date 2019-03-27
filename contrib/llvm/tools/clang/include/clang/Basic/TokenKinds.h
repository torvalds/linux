//===--- TokenKinds.h - Enum values for C Token Kinds -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines the clang::TokenKind enum and support functions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_TOKENKINDS_H
#define LLVM_CLANG_BASIC_TOKENKINDS_H

#include "llvm/Support/Compiler.h"

namespace clang {

namespace tok {

/// Provides a simple uniform namespace for tokens from all C languages.
enum TokenKind : unsigned short {
#define TOK(X) X,
#include "clang/Basic/TokenKinds.def"
  NUM_TOKENS
};

/// Provides a namespace for preprocessor keywords which start with a
/// '#' at the beginning of the line.
enum PPKeywordKind {
#define PPKEYWORD(X) pp_##X,
#include "clang/Basic/TokenKinds.def"
  NUM_PP_KEYWORDS
};

/// Provides a namespace for Objective-C keywords which start with
/// an '@'.
enum ObjCKeywordKind {
#define OBJC_AT_KEYWORD(X) objc_##X,
#include "clang/Basic/TokenKinds.def"
  NUM_OBJC_KEYWORDS
};

/// Defines the possible values of an on-off-switch (C99 6.10.6p2).
enum OnOffSwitch {
  OOS_ON, OOS_OFF, OOS_DEFAULT
};

/// Determines the name of a token as used within the front end.
///
/// The name of a token will be an internal name (such as "l_square")
/// and should not be used as part of diagnostic messages.
const char *getTokenName(TokenKind Kind) LLVM_READNONE;

/// Determines the spelling of simple punctuation tokens like
/// '!' or '%', and returns NULL for literal and annotation tokens.
///
/// This routine only retrieves the "simple" spelling of the token,
/// and will not produce any alternative spellings (e.g., a
/// digraph). For the actual spelling of a given Token, use
/// Preprocessor::getSpelling().
const char *getPunctuatorSpelling(TokenKind Kind) LLVM_READNONE;

/// Determines the spelling of simple keyword and contextual keyword
/// tokens like 'int' and 'dynamic_cast'. Returns NULL for other token kinds.
const char *getKeywordSpelling(TokenKind Kind) LLVM_READNONE;

/// Return true if this is a raw identifier or an identifier kind.
inline bool isAnyIdentifier(TokenKind K) {
  return (K == tok::identifier) || (K == tok::raw_identifier);
}

/// Return true if this is a C or C++ string-literal (or
/// C++11 user-defined-string-literal) token.
inline bool isStringLiteral(TokenKind K) {
  return K == tok::string_literal || K == tok::wide_string_literal ||
         K == tok::utf8_string_literal || K == tok::utf16_string_literal ||
         K == tok::utf32_string_literal;
}

/// Return true if this is a "literal" kind, like a numeric
/// constant, string, etc.
inline bool isLiteral(TokenKind K) {
  return K == tok::numeric_constant || K == tok::char_constant ||
         K == tok::wide_char_constant || K == tok::utf8_char_constant ||
         K == tok::utf16_char_constant || K == tok::utf32_char_constant ||
         isStringLiteral(K) || K == tok::angle_string_literal;
}

/// Return true if this is any of tok::annot_* kinds.
inline bool isAnnotation(TokenKind K) {
#define ANNOTATION(NAME) \
  if (K == tok::annot_##NAME) \
    return true;
#include "clang/Basic/TokenKinds.def"
  return false;
}

}  // end namespace tok
}  // end namespace clang

#endif
