//===---------- ExprMutationAnalyzer.h ------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_EXPRMUTATIONANALYZER_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_EXPRMUTATIONANALYZER_H

#include <type_traits>

#include "clang/AST/AST.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "llvm/ADT/DenseMap.h"

namespace clang {

class FunctionParmMutationAnalyzer;

/// Analyzes whether any mutative operations are applied to an expression within
/// a given statement.
class ExprMutationAnalyzer {
public:
  ExprMutationAnalyzer(const Stmt &Stm, ASTContext &Context)
      : Stm(Stm), Context(Context) {}

  bool isMutated(const Expr *Exp) { return findMutation(Exp) != nullptr; }
  bool isMutated(const Decl *Dec) { return findMutation(Dec) != nullptr; }
  const Stmt *findMutation(const Expr *Exp);
  const Stmt *findMutation(const Decl *Dec);

  bool isPointeeMutated(const Expr *Exp) {
    return findPointeeMutation(Exp) != nullptr;
  }
  bool isPointeeMutated(const Decl *Dec) {
    return findPointeeMutation(Dec) != nullptr;
  }
  const Stmt *findPointeeMutation(const Expr *Exp);
  const Stmt *findPointeeMutation(const Decl *Dec);

private:
  using MutationFinder = const Stmt *(ExprMutationAnalyzer::*)(const Expr *);
  using ResultMap = llvm::DenseMap<const Expr *, const Stmt *>;

  const Stmt *findMutationMemoized(const Expr *Exp,
                                   llvm::ArrayRef<MutationFinder> Finders,
                                   ResultMap &MemoizedResults);
  const Stmt *tryEachDeclRef(const Decl *Dec, MutationFinder Finder);

  bool isUnevaluated(const Expr *Exp);

  const Stmt *findExprMutation(ArrayRef<ast_matchers::BoundNodes> Matches);
  const Stmt *findDeclMutation(ArrayRef<ast_matchers::BoundNodes> Matches);
  const Stmt *
  findExprPointeeMutation(ArrayRef<ast_matchers::BoundNodes> Matches);
  const Stmt *
  findDeclPointeeMutation(ArrayRef<ast_matchers::BoundNodes> Matches);

  const Stmt *findDirectMutation(const Expr *Exp);
  const Stmt *findMemberMutation(const Expr *Exp);
  const Stmt *findArrayElementMutation(const Expr *Exp);
  const Stmt *findCastMutation(const Expr *Exp);
  const Stmt *findRangeLoopMutation(const Expr *Exp);
  const Stmt *findReferenceMutation(const Expr *Exp);
  const Stmt *findFunctionArgMutation(const Expr *Exp);

  const Stmt &Stm;
  ASTContext &Context;
  llvm::DenseMap<const FunctionDecl *,
                 std::unique_ptr<FunctionParmMutationAnalyzer>>
      FuncParmAnalyzer;
  ResultMap Results;
  ResultMap PointeeResults;
};

// A convenient wrapper around ExprMutationAnalyzer for analyzing function
// params.
class FunctionParmMutationAnalyzer {
public:
  FunctionParmMutationAnalyzer(const FunctionDecl &Func, ASTContext &Context);

  bool isMutated(const ParmVarDecl *Parm) {
    return findMutation(Parm) != nullptr;
  }
  const Stmt *findMutation(const ParmVarDecl *Parm);

private:
  ExprMutationAnalyzer BodyAnalyzer;
  llvm::DenseMap<const ParmVarDecl *, const Stmt *> Results;
};

} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_ANALYSES_EXPRMUTATIONANALYZER_H
