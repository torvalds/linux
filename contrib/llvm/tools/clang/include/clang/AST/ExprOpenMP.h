//===--- ExprOpenMP.h - Classes for representing expressions ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Expr interface and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_EXPROPENMP_H
#define LLVM_CLANG_AST_EXPROPENMP_H

#include "clang/AST/Expr.h"

namespace clang {
/// OpenMP 4.0 [2.4, Array Sections].
/// To specify an array section in an OpenMP construct, array subscript
/// expressions are extended with the following syntax:
/// \code
/// [ lower-bound : length ]
/// [ lower-bound : ]
/// [ : length ]
/// [ : ]
/// \endcode
/// The array section must be a subset of the original array.
/// Array sections are allowed on multidimensional arrays. Base language array
/// subscript expressions can be used to specify length-one dimensions of
/// multidimensional array sections.
/// The lower-bound and length are integral type expressions. When evaluated
/// they represent a set of integer values as follows:
/// \code
/// { lower-bound, lower-bound + 1, lower-bound + 2,... , lower-bound + length -
/// 1 }
/// \endcode
/// The lower-bound and length must evaluate to non-negative integers.
/// When the size of the array dimension is not known, the length must be
/// specified explicitly.
/// When the length is absent, it defaults to the size of the array dimension
/// minus the lower-bound.
/// When the lower-bound is absent it defaults to 0.
class OMPArraySectionExpr : public Expr {
  enum { BASE, LOWER_BOUND, LENGTH, END_EXPR };
  Stmt *SubExprs[END_EXPR];
  SourceLocation ColonLoc;
  SourceLocation RBracketLoc;

public:
  OMPArraySectionExpr(Expr *Base, Expr *LowerBound, Expr *Length, QualType Type,
                      ExprValueKind VK, ExprObjectKind OK,
                      SourceLocation ColonLoc, SourceLocation RBracketLoc)
      : Expr(
            OMPArraySectionExprClass, Type, VK, OK,
            Base->isTypeDependent() ||
                (LowerBound && LowerBound->isTypeDependent()) ||
                (Length && Length->isTypeDependent()),
            Base->isValueDependent() ||
                (LowerBound && LowerBound->isValueDependent()) ||
                (Length && Length->isValueDependent()),
            Base->isInstantiationDependent() ||
                (LowerBound && LowerBound->isInstantiationDependent()) ||
                (Length && Length->isInstantiationDependent()),
            Base->containsUnexpandedParameterPack() ||
                (LowerBound && LowerBound->containsUnexpandedParameterPack()) ||
                (Length && Length->containsUnexpandedParameterPack())),
        ColonLoc(ColonLoc), RBracketLoc(RBracketLoc) {
    SubExprs[BASE] = Base;
    SubExprs[LOWER_BOUND] = LowerBound;
    SubExprs[LENGTH] = Length;
  }

  /// Create an empty array section expression.
  explicit OMPArraySectionExpr(EmptyShell Shell)
      : Expr(OMPArraySectionExprClass, Shell) {}

  /// An array section can be written only as Base[LowerBound:Length].

  /// Get base of the array section.
  Expr *getBase() { return cast<Expr>(SubExprs[BASE]); }
  const Expr *getBase() const { return cast<Expr>(SubExprs[BASE]); }
  /// Set base of the array section.
  void setBase(Expr *E) { SubExprs[BASE] = E; }

  /// Return original type of the base expression for array section.
  static QualType getBaseOriginalType(const Expr *Base);

  /// Get lower bound of array section.
  Expr *getLowerBound() { return cast_or_null<Expr>(SubExprs[LOWER_BOUND]); }
  const Expr *getLowerBound() const {
    return cast_or_null<Expr>(SubExprs[LOWER_BOUND]);
  }
  /// Set lower bound of the array section.
  void setLowerBound(Expr *E) { SubExprs[LOWER_BOUND] = E; }

  /// Get length of array section.
  Expr *getLength() { return cast_or_null<Expr>(SubExprs[LENGTH]); }
  const Expr *getLength() const { return cast_or_null<Expr>(SubExprs[LENGTH]); }
  /// Set length of the array section.
  void setLength(Expr *E) { SubExprs[LENGTH] = E; }

  SourceLocation getBeginLoc() const LLVM_READONLY {
    return getBase()->getBeginLoc();
  }
  SourceLocation getEndLoc() const LLVM_READONLY { return RBracketLoc; }

  SourceLocation getColonLoc() const { return ColonLoc; }
  void setColonLoc(SourceLocation L) { ColonLoc = L; }

  SourceLocation getRBracketLoc() const { return RBracketLoc; }
  void setRBracketLoc(SourceLocation L) { RBracketLoc = L; }

  SourceLocation getExprLoc() const LLVM_READONLY {
    return getBase()->getExprLoc();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPArraySectionExprClass;
  }

  child_range children() {
    return child_range(&SubExprs[BASE], &SubExprs[END_EXPR]);
  }
};
} // end namespace clang

#endif
