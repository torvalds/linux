//===--- FixIt.cpp - FixIt Hint utilities -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains implementations of utitilies to ease source code rewriting
// by providing helper functions related to FixItHint.
//
//===----------------------------------------------------------------------===//
#include "clang/Tooling/FixIt.h"
#include "clang/Lex/Lexer.h"

namespace clang {
namespace tooling {
namespace fixit {

namespace internal {
StringRef getText(SourceRange Range, const ASTContext &Context) {
  return Lexer::getSourceText(CharSourceRange::getTokenRange(Range),
                              Context.getSourceManager(),
                              Context.getLangOpts());
}
} // end namespace internal

} // end namespace fixit
} // end namespace tooling
} // end namespace clang
