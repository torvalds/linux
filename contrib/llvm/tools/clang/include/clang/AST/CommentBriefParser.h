//===--- CommentBriefParser.h - Dumb comment parser -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines a very simple Doxygen comment parser.
//
//===----------------------------------------------------------------------===//


#ifndef LLVM_CLANG_AST_COMMENTBRIEFPARSER_H
#define LLVM_CLANG_AST_COMMENTBRIEFPARSER_H

#include "clang/AST/CommentLexer.h"

namespace clang {
namespace comments {

/// A very simple comment parser that extracts "a brief description".
///
/// Due to a variety of comment styles, it considers the following as "a brief
/// description", in order of priority:
/// \li a \or \\short command,
/// \li the first paragraph,
/// \li a \\result or \\return or \\returns paragraph.
class BriefParser {
  Lexer &L;

  const CommandTraits &Traits;

  /// Current lookahead token.
  Token Tok;

  SourceLocation ConsumeToken() {
    SourceLocation Loc = Tok.getLocation();
    L.lex(Tok);
    return Loc;
  }

public:
  BriefParser(Lexer &L, const CommandTraits &Traits);

  /// Return the best "brief description" we can find.
  std::string Parse();
};

} // end namespace comments
} // end namespace clang

#endif

