//== CheckerHelpers.h - Helper functions for checkers ------------*- C++ -*--=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines CheckerVisitor.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_CHECKERHELPERS_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_CHECKERHELPERS_H

#include "clang/AST/Stmt.h"
#include <tuple>

namespace clang {

class Expr;
class VarDecl;
class QualType;
class AttributedType;

namespace ento {

bool containsMacro(const Stmt *S);
bool containsEnum(const Stmt *S);
bool containsStaticLocal(const Stmt *S);
bool containsBuiltinOffsetOf(const Stmt *S);
template <class T> bool containsStmt(const Stmt *S) {
  if (isa<T>(S))
      return true;

  for (const Stmt *Child : S->children())
    if (Child && containsStmt<T>(Child))
      return true;

  return false;
}

std::pair<const clang::VarDecl *, const clang::Expr *>
parseAssignment(const Stmt *S);

// Do not reorder! The getMostNullable method relies on the order.
// Optimization: Most pointers expected to be unspecified. When a symbol has an
// unspecified or nonnull type non of the rules would indicate any problem for
// that symbol. For this reason only nullable and contradicted nullability are
// stored for a symbol. When a symbol is already contradicted, it can not be
// casted back to nullable.
enum class Nullability : char {
  Contradicted, // Tracked nullability is contradicted by an explicit cast. Do
                // not report any nullability related issue for this symbol.
                // This nullability is propagated aggressively to avoid false
                // positive results. See the comment on getMostNullable method.
  Nullable,
  Unspecified,
  Nonnull
};

/// Get nullability annotation for a given type.
Nullability getNullabilityAnnotation(QualType Type);

} // end GR namespace

} // end clang namespace

#endif
