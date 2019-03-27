//===--- ParentMap.h - Mappings from Stmts to their Parents -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ParentMap class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_PARENTMAP_H
#define LLVM_CLANG_AST_PARENTMAP_H

namespace clang {
class Stmt;
class Expr;

class ParentMap {
  void* Impl;
public:
  ParentMap(Stmt* ASTRoot);
  ~ParentMap();

  /// Adds and/or updates the parent/child-relations of the complete
  /// stmt tree of S. All children of S including indirect descendants are
  /// visited and updated or inserted but not the parents of S.
  void addStmt(Stmt* S);

  /// Manually sets the parent of \p S to \p Parent.
  ///
  /// If \p S is already in the map, this method will update the mapping.
  void setParent(const Stmt *S, const Stmt *Parent);

  Stmt *getParent(Stmt*) const;
  Stmt *getParentIgnoreParens(Stmt *) const;
  Stmt *getParentIgnoreParenCasts(Stmt *) const;
  Stmt *getParentIgnoreParenImpCasts(Stmt *) const;
  Stmt *getOuterParenParent(Stmt *) const;

  const Stmt *getParent(const Stmt* S) const {
    return getParent(const_cast<Stmt*>(S));
  }

  const Stmt *getParentIgnoreParens(const Stmt *S) const {
    return getParentIgnoreParens(const_cast<Stmt*>(S));
  }

  const Stmt *getParentIgnoreParenCasts(const Stmt *S) const {
    return getParentIgnoreParenCasts(const_cast<Stmt*>(S));
  }

  bool hasParent(Stmt* S) const {
    return getParent(S) != nullptr;
  }

  bool isConsumedExpr(Expr *E) const;

  bool isConsumedExpr(const Expr *E) const {
    return isConsumedExpr(const_cast<Expr*>(E));
  }
};

} // end clang namespace
#endif
