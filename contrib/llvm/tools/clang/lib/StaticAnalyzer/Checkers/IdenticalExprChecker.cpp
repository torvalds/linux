//== IdenticalExprChecker.cpp - Identical expression checker----------------==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This defines IdenticalExprChecker, a check that warns about
/// unintended use of identical expressions.
///
/// It checks for use of identical expressions with comparison operators and
/// inside conditional expressions.
///
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

static bool isIdenticalStmt(const ASTContext &Ctx, const Stmt *Stmt1,
                            const Stmt *Stmt2, bool IgnoreSideEffects = false);
//===----------------------------------------------------------------------===//
// FindIdenticalExprVisitor - Identify nodes using identical expressions.
//===----------------------------------------------------------------------===//

namespace {
class FindIdenticalExprVisitor
    : public RecursiveASTVisitor<FindIdenticalExprVisitor> {
  BugReporter &BR;
  const CheckerBase *Checker;
  AnalysisDeclContext *AC;
public:
  explicit FindIdenticalExprVisitor(BugReporter &B,
                                    const CheckerBase *Checker,
                                    AnalysisDeclContext *A)
      : BR(B), Checker(Checker), AC(A) {}
  // FindIdenticalExprVisitor only visits nodes
  // that are binary operators, if statements or
  // conditional operators.
  bool VisitBinaryOperator(const BinaryOperator *B);
  bool VisitIfStmt(const IfStmt *I);
  bool VisitConditionalOperator(const ConditionalOperator *C);

private:
  void reportIdenticalExpr(const BinaryOperator *B, bool CheckBitwise,
                           ArrayRef<SourceRange> Sr);
  void checkBitwiseOrLogicalOp(const BinaryOperator *B, bool CheckBitwise);
  void checkComparisonOp(const BinaryOperator *B);
};
} // end anonymous namespace

void FindIdenticalExprVisitor::reportIdenticalExpr(const BinaryOperator *B,
                                                   bool CheckBitwise,
                                                   ArrayRef<SourceRange> Sr) {
  StringRef Message;
  if (CheckBitwise)
    Message = "identical expressions on both sides of bitwise operator";
  else
    Message = "identical expressions on both sides of logical operator";

  PathDiagnosticLocation ELoc =
      PathDiagnosticLocation::createOperatorLoc(B, BR.getSourceManager());
  BR.EmitBasicReport(AC->getDecl(), Checker,
                     "Use of identical expressions",
                     categories::LogicError,
                     Message, ELoc, Sr);
}

void FindIdenticalExprVisitor::checkBitwiseOrLogicalOp(const BinaryOperator *B,
                                                       bool CheckBitwise) {
  SourceRange Sr[2];

  const Expr *LHS = B->getLHS();
  const Expr *RHS = B->getRHS();

  // Split operators as long as we still have operators to split on. We will
  // get called for every binary operator in an expression so there is no need
  // to check every one against each other here, just the right most one with
  // the others.
  while (const BinaryOperator *B2 = dyn_cast<BinaryOperator>(LHS)) {
    if (B->getOpcode() != B2->getOpcode())
      break;
    if (isIdenticalStmt(AC->getASTContext(), RHS, B2->getRHS())) {
      Sr[0] = RHS->getSourceRange();
      Sr[1] = B2->getRHS()->getSourceRange();
      reportIdenticalExpr(B, CheckBitwise, Sr);
    }
    LHS = B2->getLHS();
  }

  if (isIdenticalStmt(AC->getASTContext(), RHS, LHS)) {
    Sr[0] = RHS->getSourceRange();
    Sr[1] = LHS->getSourceRange();
    reportIdenticalExpr(B, CheckBitwise, Sr);
  }
}

bool FindIdenticalExprVisitor::VisitIfStmt(const IfStmt *I) {
  const Stmt *Stmt1 = I->getThen();
  const Stmt *Stmt2 = I->getElse();

  // Check for identical inner condition:
  //
  // if (x<10) {
  //   if (x<10) {
  //   ..
  if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(Stmt1)) {
    if (!CS->body_empty()) {
      const IfStmt *InnerIf = dyn_cast<IfStmt>(*CS->body_begin());
      if (InnerIf && isIdenticalStmt(AC->getASTContext(), I->getCond(), InnerIf->getCond(), /*ignoreSideEffects=*/ false)) {
        PathDiagnosticLocation ELoc(InnerIf->getCond(), BR.getSourceManager(), AC);
        BR.EmitBasicReport(AC->getDecl(), Checker, "Identical conditions",
          categories::LogicError,
          "conditions of the inner and outer statements are identical",
          ELoc);
      }
    }
  }

  // Check for identical conditions:
  //
  // if (b) {
  //   foo1();
  // } else if (b) {
  //   foo2();
  // }
  if (Stmt1 && Stmt2) {
    const Expr *Cond1 = I->getCond();
    const Stmt *Else = Stmt2;
    while (const IfStmt *I2 = dyn_cast_or_null<IfStmt>(Else)) {
      const Expr *Cond2 = I2->getCond();
      if (isIdenticalStmt(AC->getASTContext(), Cond1, Cond2, false)) {
        SourceRange Sr = Cond1->getSourceRange();
        PathDiagnosticLocation ELoc(Cond2, BR.getSourceManager(), AC);
        BR.EmitBasicReport(AC->getDecl(), Checker, "Identical conditions",
                           categories::LogicError,
                           "expression is identical to previous condition",
                           ELoc, Sr);
      }
      Else = I2->getElse();
    }
  }

  if (!Stmt1 || !Stmt2)
    return true;

  // Special handling for code like:
  //
  // if (b) {
  //   i = 1;
  // } else
  //   i = 1;
  if (const CompoundStmt *CompStmt = dyn_cast<CompoundStmt>(Stmt1)) {
    if (CompStmt->size() == 1)
      Stmt1 = CompStmt->body_back();
  }
  if (const CompoundStmt *CompStmt = dyn_cast<CompoundStmt>(Stmt2)) {
    if (CompStmt->size() == 1)
      Stmt2 = CompStmt->body_back();
  }

  if (isIdenticalStmt(AC->getASTContext(), Stmt1, Stmt2, true)) {
      PathDiagnosticLocation ELoc =
          PathDiagnosticLocation::createBegin(I, BR.getSourceManager(), AC);
      BR.EmitBasicReport(AC->getDecl(), Checker,
                         "Identical branches",
                         categories::LogicError,
                         "true and false branches are identical", ELoc);
  }
  return true;
}

bool FindIdenticalExprVisitor::VisitBinaryOperator(const BinaryOperator *B) {
  BinaryOperator::Opcode Op = B->getOpcode();

  if (BinaryOperator::isBitwiseOp(Op))
    checkBitwiseOrLogicalOp(B, true);

  if (BinaryOperator::isLogicalOp(Op))
    checkBitwiseOrLogicalOp(B, false);

  if (BinaryOperator::isComparisonOp(Op))
    checkComparisonOp(B);

  // We want to visit ALL nodes (subexpressions of binary comparison
  // expressions too) that contains comparison operators.
  // True is always returned to traverse ALL nodes.
  return true;
}

void FindIdenticalExprVisitor::checkComparisonOp(const BinaryOperator *B) {
  BinaryOperator::Opcode Op = B->getOpcode();

  //
  // Special case for floating-point representation.
  //
  // If expressions on both sides of comparison operator are of type float,
  // then for some comparison operators no warning shall be
  // reported even if the expressions are identical from a symbolic point of
  // view. Comparison between expressions, declared variables and literals
  // are treated differently.
  //
  // != and == between float literals that have the same value should NOT warn.
  // < > between float literals that have the same value SHOULD warn.
  //
  // != and == between the same float declaration should NOT warn.
  // < > between the same float declaration SHOULD warn.
  //
  // != and == between eq. expressions that evaluates into float
  //           should NOT warn.
  // < >       between eq. expressions that evaluates into float
  //           should NOT warn.
  //
  const Expr *LHS = B->getLHS()->IgnoreParenImpCasts();
  const Expr *RHS = B->getRHS()->IgnoreParenImpCasts();

  const DeclRefExpr *DeclRef1 = dyn_cast<DeclRefExpr>(LHS);
  const DeclRefExpr *DeclRef2 = dyn_cast<DeclRefExpr>(RHS);
  const FloatingLiteral *FloatLit1 = dyn_cast<FloatingLiteral>(LHS);
  const FloatingLiteral *FloatLit2 = dyn_cast<FloatingLiteral>(RHS);
  if ((DeclRef1) && (DeclRef2)) {
    if ((DeclRef1->getType()->hasFloatingRepresentation()) &&
        (DeclRef2->getType()->hasFloatingRepresentation())) {
      if (DeclRef1->getDecl() == DeclRef2->getDecl()) {
        if ((Op == BO_EQ) || (Op == BO_NE)) {
          return;
        }
      }
    }
  } else if ((FloatLit1) && (FloatLit2)) {
    if (FloatLit1->getValue().bitwiseIsEqual(FloatLit2->getValue())) {
      if ((Op == BO_EQ) || (Op == BO_NE)) {
        return;
      }
    }
  } else if (LHS->getType()->hasFloatingRepresentation()) {
    // If any side of comparison operator still has floating-point
    // representation, then it's an expression. Don't warn.
    // Here only LHS is checked since RHS will be implicit casted to float.
    return;
  } else {
    // No special case with floating-point representation, report as usual.
  }

  if (isIdenticalStmt(AC->getASTContext(), B->getLHS(), B->getRHS())) {
    PathDiagnosticLocation ELoc =
        PathDiagnosticLocation::createOperatorLoc(B, BR.getSourceManager());
    StringRef Message;
    if (Op == BO_Cmp)
      Message = "comparison of identical expressions always evaluates to "
                "'equal'";
    else if (((Op == BO_EQ) || (Op == BO_LE) || (Op == BO_GE)))
      Message = "comparison of identical expressions always evaluates to true";
    else
      Message = "comparison of identical expressions always evaluates to false";
    BR.EmitBasicReport(AC->getDecl(), Checker,
                       "Compare of identical expressions",
                       categories::LogicError, Message, ELoc);
  }
}

bool FindIdenticalExprVisitor::VisitConditionalOperator(
    const ConditionalOperator *C) {

  // Check if expressions in conditional expression are identical
  // from a symbolic point of view.

  if (isIdenticalStmt(AC->getASTContext(), C->getTrueExpr(),
                      C->getFalseExpr(), true)) {
    PathDiagnosticLocation ELoc =
        PathDiagnosticLocation::createConditionalColonLoc(
            C, BR.getSourceManager());

    SourceRange Sr[2];
    Sr[0] = C->getTrueExpr()->getSourceRange();
    Sr[1] = C->getFalseExpr()->getSourceRange();
    BR.EmitBasicReport(
        AC->getDecl(), Checker,
        "Identical expressions in conditional expression",
        categories::LogicError,
        "identical expressions on both sides of ':' in conditional expression",
        ELoc, Sr);
  }
  // We want to visit ALL nodes (expressions in conditional
  // expressions too) that contains conditional operators,
  // thus always return true to traverse ALL nodes.
  return true;
}

/// Determines whether two statement trees are identical regarding
/// operators and symbols.
///
/// Exceptions: expressions containing macros or functions with possible side
/// effects are never considered identical.
/// Limitations: (t + u) and (u + t) are not considered identical.
/// t*(u + t) and t*u + t*t are not considered identical.
///
static bool isIdenticalStmt(const ASTContext &Ctx, const Stmt *Stmt1,
                            const Stmt *Stmt2, bool IgnoreSideEffects) {

  if (!Stmt1 || !Stmt2) {
    return !Stmt1 && !Stmt2;
  }

  // If Stmt1 & Stmt2 are of different class then they are not
  // identical statements.
  if (Stmt1->getStmtClass() != Stmt2->getStmtClass())
    return false;

  const Expr *Expr1 = dyn_cast<Expr>(Stmt1);
  const Expr *Expr2 = dyn_cast<Expr>(Stmt2);

  if (Expr1 && Expr2) {
    // If Stmt1 has side effects then don't warn even if expressions
    // are identical.
    if (!IgnoreSideEffects && Expr1->HasSideEffects(Ctx))
      return false;
    // If either expression comes from a macro then don't warn even if
    // the expressions are identical.
    if ((Expr1->getExprLoc().isMacroID()) || (Expr2->getExprLoc().isMacroID()))
      return false;

    // If all children of two expressions are identical, return true.
    Expr::const_child_iterator I1 = Expr1->child_begin();
    Expr::const_child_iterator I2 = Expr2->child_begin();
    while (I1 != Expr1->child_end() && I2 != Expr2->child_end()) {
      if (!*I1 || !*I2 || !isIdenticalStmt(Ctx, *I1, *I2, IgnoreSideEffects))
        return false;
      ++I1;
      ++I2;
    }
    // If there are different number of children in the statements, return
    // false.
    if (I1 != Expr1->child_end())
      return false;
    if (I2 != Expr2->child_end())
      return false;
  }

  switch (Stmt1->getStmtClass()) {
  default:
    return false;
  case Stmt::CallExprClass:
  case Stmt::ArraySubscriptExprClass:
  case Stmt::OMPArraySectionExprClass:
  case Stmt::ImplicitCastExprClass:
  case Stmt::ParenExprClass:
  case Stmt::BreakStmtClass:
  case Stmt::ContinueStmtClass:
  case Stmt::NullStmtClass:
    return true;
  case Stmt::CStyleCastExprClass: {
    const CStyleCastExpr* CastExpr1 = cast<CStyleCastExpr>(Stmt1);
    const CStyleCastExpr* CastExpr2 = cast<CStyleCastExpr>(Stmt2);

    return CastExpr1->getTypeAsWritten() == CastExpr2->getTypeAsWritten();
  }
  case Stmt::ReturnStmtClass: {
    const ReturnStmt *ReturnStmt1 = cast<ReturnStmt>(Stmt1);
    const ReturnStmt *ReturnStmt2 = cast<ReturnStmt>(Stmt2);

    return isIdenticalStmt(Ctx, ReturnStmt1->getRetValue(),
                           ReturnStmt2->getRetValue(), IgnoreSideEffects);
  }
  case Stmt::ForStmtClass: {
    const ForStmt *ForStmt1 = cast<ForStmt>(Stmt1);
    const ForStmt *ForStmt2 = cast<ForStmt>(Stmt2);

    if (!isIdenticalStmt(Ctx, ForStmt1->getInit(), ForStmt2->getInit(),
                         IgnoreSideEffects))
      return false;
    if (!isIdenticalStmt(Ctx, ForStmt1->getCond(), ForStmt2->getCond(),
                         IgnoreSideEffects))
      return false;
    if (!isIdenticalStmt(Ctx, ForStmt1->getInc(), ForStmt2->getInc(),
                         IgnoreSideEffects))
      return false;
    if (!isIdenticalStmt(Ctx, ForStmt1->getBody(), ForStmt2->getBody(),
                         IgnoreSideEffects))
      return false;
    return true;
  }
  case Stmt::DoStmtClass: {
    const DoStmt *DStmt1 = cast<DoStmt>(Stmt1);
    const DoStmt *DStmt2 = cast<DoStmt>(Stmt2);

    if (!isIdenticalStmt(Ctx, DStmt1->getCond(), DStmt2->getCond(),
                         IgnoreSideEffects))
      return false;
    if (!isIdenticalStmt(Ctx, DStmt1->getBody(), DStmt2->getBody(),
                         IgnoreSideEffects))
      return false;
    return true;
  }
  case Stmt::WhileStmtClass: {
    const WhileStmt *WStmt1 = cast<WhileStmt>(Stmt1);
    const WhileStmt *WStmt2 = cast<WhileStmt>(Stmt2);

    if (!isIdenticalStmt(Ctx, WStmt1->getCond(), WStmt2->getCond(),
                         IgnoreSideEffects))
      return false;
    if (!isIdenticalStmt(Ctx, WStmt1->getBody(), WStmt2->getBody(),
                         IgnoreSideEffects))
      return false;
    return true;
  }
  case Stmt::IfStmtClass: {
    const IfStmt *IStmt1 = cast<IfStmt>(Stmt1);
    const IfStmt *IStmt2 = cast<IfStmt>(Stmt2);

    if (!isIdenticalStmt(Ctx, IStmt1->getCond(), IStmt2->getCond(),
                         IgnoreSideEffects))
      return false;
    if (!isIdenticalStmt(Ctx, IStmt1->getThen(), IStmt2->getThen(),
                         IgnoreSideEffects))
      return false;
    if (!isIdenticalStmt(Ctx, IStmt1->getElse(), IStmt2->getElse(),
                         IgnoreSideEffects))
      return false;
    return true;
  }
  case Stmt::CompoundStmtClass: {
    const CompoundStmt *CompStmt1 = cast<CompoundStmt>(Stmt1);
    const CompoundStmt *CompStmt2 = cast<CompoundStmt>(Stmt2);

    if (CompStmt1->size() != CompStmt2->size())
      return false;

    CompoundStmt::const_body_iterator I1 = CompStmt1->body_begin();
    CompoundStmt::const_body_iterator I2 = CompStmt2->body_begin();
    while (I1 != CompStmt1->body_end() && I2 != CompStmt2->body_end()) {
      if (!isIdenticalStmt(Ctx, *I1, *I2, IgnoreSideEffects))
        return false;
      ++I1;
      ++I2;
    }

    return true;
  }
  case Stmt::CompoundAssignOperatorClass:
  case Stmt::BinaryOperatorClass: {
    const BinaryOperator *BinOp1 = cast<BinaryOperator>(Stmt1);
    const BinaryOperator *BinOp2 = cast<BinaryOperator>(Stmt2);
    return BinOp1->getOpcode() == BinOp2->getOpcode();
  }
  case Stmt::CharacterLiteralClass: {
    const CharacterLiteral *CharLit1 = cast<CharacterLiteral>(Stmt1);
    const CharacterLiteral *CharLit2 = cast<CharacterLiteral>(Stmt2);
    return CharLit1->getValue() == CharLit2->getValue();
  }
  case Stmt::DeclRefExprClass: {
    const DeclRefExpr *DeclRef1 = cast<DeclRefExpr>(Stmt1);
    const DeclRefExpr *DeclRef2 = cast<DeclRefExpr>(Stmt2);
    return DeclRef1->getDecl() == DeclRef2->getDecl();
  }
  case Stmt::IntegerLiteralClass: {
    const IntegerLiteral *IntLit1 = cast<IntegerLiteral>(Stmt1);
    const IntegerLiteral *IntLit2 = cast<IntegerLiteral>(Stmt2);

    llvm::APInt I1 = IntLit1->getValue();
    llvm::APInt I2 = IntLit2->getValue();
    if (I1.getBitWidth() != I2.getBitWidth())
      return false;
    return  I1 == I2;
  }
  case Stmt::FloatingLiteralClass: {
    const FloatingLiteral *FloatLit1 = cast<FloatingLiteral>(Stmt1);
    const FloatingLiteral *FloatLit2 = cast<FloatingLiteral>(Stmt2);
    return FloatLit1->getValue().bitwiseIsEqual(FloatLit2->getValue());
  }
  case Stmt::StringLiteralClass: {
    const StringLiteral *StringLit1 = cast<StringLiteral>(Stmt1);
    const StringLiteral *StringLit2 = cast<StringLiteral>(Stmt2);
    return StringLit1->getBytes() == StringLit2->getBytes();
  }
  case Stmt::MemberExprClass: {
    const MemberExpr *MemberStmt1 = cast<MemberExpr>(Stmt1);
    const MemberExpr *MemberStmt2 = cast<MemberExpr>(Stmt2);
    return MemberStmt1->getMemberDecl() == MemberStmt2->getMemberDecl();
  }
  case Stmt::UnaryOperatorClass: {
    const UnaryOperator *UnaryOp1 = cast<UnaryOperator>(Stmt1);
    const UnaryOperator *UnaryOp2 = cast<UnaryOperator>(Stmt2);
    return UnaryOp1->getOpcode() == UnaryOp2->getOpcode();
  }
  }
}

//===----------------------------------------------------------------------===//
// FindIdenticalExprChecker
//===----------------------------------------------------------------------===//

namespace {
class FindIdenticalExprChecker : public Checker<check::ASTCodeBody> {
public:
  void checkASTCodeBody(const Decl *D, AnalysisManager &Mgr,
                        BugReporter &BR) const {
    FindIdenticalExprVisitor Visitor(BR, this, Mgr.getAnalysisDeclContext(D));
    Visitor.TraverseDecl(const_cast<Decl *>(D));
  }
};
} // end anonymous namespace

void ento::registerIdenticalExprChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<FindIdenticalExprChecker>();
}
