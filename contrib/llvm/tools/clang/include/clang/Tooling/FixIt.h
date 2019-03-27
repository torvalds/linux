//===--- FixIt.h - FixIt Hint utilities -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements functions to ease source rewriting from AST-nodes.
//
//  Example swapping A and B expressions:
//
//    Expr *A, *B;
//    tooling::fixit::createReplacement(*A, *B);
//    tooling::fixit::createReplacement(*B, *A);
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_FIXIT_H
#define LLVM_CLANG_TOOLING_FIXIT_H

#include "clang/AST/ASTContext.h"

namespace clang {
namespace tooling {
namespace fixit {

namespace internal {
StringRef getText(SourceRange Range, const ASTContext &Context);

/// Returns the SourceRange of a SourceRange. This identity function is
///        used by the following template abstractions.
inline SourceRange getSourceRange(const SourceRange &Range) { return Range; }

/// Returns the SourceRange of the token at Location \p Loc.
inline SourceRange getSourceRange(const SourceLocation &Loc) {
  return SourceRange(Loc);
}

/// Returns the SourceRange of an given Node. \p Node is typically a
///        'Stmt', 'Expr' or a 'Decl'.
template <typename T> SourceRange getSourceRange(const T &Node) {
  return Node.getSourceRange();
}
} // end namespace internal

// Returns a textual representation of \p Node.
template <typename T>
StringRef getText(const T &Node, const ASTContext &Context) {
  return internal::getText(internal::getSourceRange(Node), Context);
}

// Returns a FixItHint to remove \p Node.
// TODO: Add support for related syntactical elements (i.e. comments, ...).
template <typename T> FixItHint createRemoval(const T &Node) {
  return FixItHint::CreateRemoval(internal::getSourceRange(Node));
}

// Returns a FixItHint to replace \p Destination by \p Source.
template <typename D, typename S>
FixItHint createReplacement(const D &Destination, const S &Source,
                                   const ASTContext &Context) {
  return FixItHint::CreateReplacement(internal::getSourceRange(Destination),
                                      getText(Source, Context));
}

// Returns a FixItHint to replace \p Destination by \p Source.
template <typename D>
FixItHint createReplacement(const D &Destination, StringRef Source) {
  return FixItHint::CreateReplacement(internal::getSourceRange(Destination),
                                      Source);
}

} // end namespace fixit
} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_FIXINT_H
