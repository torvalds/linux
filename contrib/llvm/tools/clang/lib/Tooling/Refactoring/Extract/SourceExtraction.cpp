//===--- SourceExtraction.cpp - Clang refactoring library -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SourceExtraction.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/StmtObjC.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

using namespace clang;

namespace {

/// Returns true if the token at the given location is a semicolon.
bool isSemicolonAtLocation(SourceLocation TokenLoc, const SourceManager &SM,
                           const LangOptions &LangOpts) {
  return Lexer::getSourceText(
             CharSourceRange::getTokenRange(TokenLoc, TokenLoc), SM,
             LangOpts) == ";";
}

/// Returns true if there should be a semicolon after the given statement.
bool isSemicolonRequiredAfter(const Stmt *S) {
  if (isa<CompoundStmt>(S))
    return false;
  if (const auto *If = dyn_cast<IfStmt>(S))
    return isSemicolonRequiredAfter(If->getElse() ? If->getElse()
                                                  : If->getThen());
  if (const auto *While = dyn_cast<WhileStmt>(S))
    return isSemicolonRequiredAfter(While->getBody());
  if (const auto *For = dyn_cast<ForStmt>(S))
    return isSemicolonRequiredAfter(For->getBody());
  if (const auto *CXXFor = dyn_cast<CXXForRangeStmt>(S))
    return isSemicolonRequiredAfter(CXXFor->getBody());
  if (const auto *ObjCFor = dyn_cast<ObjCForCollectionStmt>(S))
    return isSemicolonRequiredAfter(ObjCFor->getBody());
  switch (S->getStmtClass()) {
  case Stmt::SwitchStmtClass:
  case Stmt::CXXTryStmtClass:
  case Stmt::ObjCAtSynchronizedStmtClass:
  case Stmt::ObjCAutoreleasePoolStmtClass:
  case Stmt::ObjCAtTryStmtClass:
    return false;
  default:
    return true;
  }
}

/// Returns true if the two source locations are on the same line.
bool areOnSameLine(SourceLocation Loc1, SourceLocation Loc2,
                   const SourceManager &SM) {
  return !Loc1.isMacroID() && !Loc2.isMacroID() &&
         SM.getSpellingLineNumber(Loc1) == SM.getSpellingLineNumber(Loc2);
}

} // end anonymous namespace

namespace clang {
namespace tooling {

ExtractionSemicolonPolicy
ExtractionSemicolonPolicy::compute(const Stmt *S, SourceRange &ExtractedRange,
                                   const SourceManager &SM,
                                   const LangOptions &LangOpts) {
  auto neededInExtractedFunction = []() {
    return ExtractionSemicolonPolicy(true, false);
  };
  auto neededInOriginalFunction = []() {
    return ExtractionSemicolonPolicy(false, true);
  };

  /// The extracted expression should be terminated with a ';'. The call to
  /// the extracted function will replace this expression, so it won't need
  /// a terminating ';'.
  if (isa<Expr>(S))
    return neededInExtractedFunction();

  /// Some statements don't need to be terminated with ';'. The call to the
  /// extracted function will be a standalone statement, so it should be
  /// terminated with a ';'.
  bool NeedsSemi = isSemicolonRequiredAfter(S);
  if (!NeedsSemi)
    return neededInOriginalFunction();

  /// Some statements might end at ';'. The extraction will move that ';', so
  /// the call to the extracted function should be terminated with a ';'.
  SourceLocation End = ExtractedRange.getEnd();
  if (isSemicolonAtLocation(End, SM, LangOpts))
    return neededInOriginalFunction();

  /// Other statements should generally have a trailing ';'. We can try to find
  /// it and move it together it with the extracted code.
  Optional<Token> NextToken = Lexer::findNextToken(End, SM, LangOpts);
  if (NextToken && NextToken->is(tok::semi) &&
      areOnSameLine(NextToken->getLocation(), End, SM)) {
    ExtractedRange.setEnd(NextToken->getLocation());
    return neededInOriginalFunction();
  }

  /// Otherwise insert semicolons in both places.
  return ExtractionSemicolonPolicy(true, true);
}

} // end namespace tooling
} // end namespace clang
