//== ReturnUndefChecker.cpp -------------------------------------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines ReturnUndefChecker, which is a path-sensitive
// check which looks for undefined or garbage values being returned to the
// caller.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

namespace {
class ReturnUndefChecker : public Checker< check::PreStmt<ReturnStmt> > {
  mutable std::unique_ptr<BuiltinBug> BT_Undef;
  mutable std::unique_ptr<BuiltinBug> BT_NullReference;

  void emitUndef(CheckerContext &C, const Expr *RetE) const;
  void checkReference(CheckerContext &C, const Expr *RetE,
                      DefinedOrUnknownSVal RetVal) const;
public:
  void checkPreStmt(const ReturnStmt *RS, CheckerContext &C) const;
};
}

void ReturnUndefChecker::checkPreStmt(const ReturnStmt *RS,
                                      CheckerContext &C) const {
  const Expr *RetE = RS->getRetValue();
  if (!RetE)
    return;
  SVal RetVal = C.getSVal(RetE);

  const StackFrameContext *SFC = C.getStackFrame();
  QualType RT = CallEvent::getDeclaredResultType(SFC->getDecl());

  if (RetVal.isUndef()) {
    // "return;" is modeled to evaluate to an UndefinedVal. Allow UndefinedVal
    // to be returned in functions returning void to support this pattern:
    //   void foo() {
    //     return;
    //   }
    //   void test() {
    //     return foo();
    //   }
    if (!RT.isNull() && RT->isVoidType())
      return;

    // Not all blocks have explicitly-specified return types; if the return type
    // is not available, but the return value expression has 'void' type, assume
    // Sema already checked it.
    if (RT.isNull() && isa<BlockDecl>(SFC->getDecl()) &&
        RetE->getType()->isVoidType())
      return;

    emitUndef(C, RetE);
    return;
  }

  if (RT.isNull())
    return;

  if (RT->isReferenceType()) {
    checkReference(C, RetE, RetVal.castAs<DefinedOrUnknownSVal>());
    return;
  }
}

static void emitBug(CheckerContext &C, BuiltinBug &BT, const Expr *RetE,
                    const Expr *TrackingE = nullptr) {
  ExplodedNode *N = C.generateErrorNode();
  if (!N)
    return;

  auto Report = llvm::make_unique<BugReport>(BT, BT.getDescription(), N);

  Report->addRange(RetE->getSourceRange());
  bugreporter::trackExpressionValue(N, TrackingE ? TrackingE : RetE, *Report);

  C.emitReport(std::move(Report));
}

void ReturnUndefChecker::emitUndef(CheckerContext &C, const Expr *RetE) const {
  if (!BT_Undef)
    BT_Undef.reset(
        new BuiltinBug(this, "Garbage return value",
                       "Undefined or garbage value returned to caller"));
  emitBug(C, *BT_Undef, RetE);
}

void ReturnUndefChecker::checkReference(CheckerContext &C, const Expr *RetE,
                                        DefinedOrUnknownSVal RetVal) const {
  ProgramStateRef StNonNull, StNull;
  std::tie(StNonNull, StNull) = C.getState()->assume(RetVal);

  if (StNonNull) {
    // Going forward, assume the location is non-null.
    C.addTransition(StNonNull);
    return;
  }

  // The return value is known to be null. Emit a bug report.
  if (!BT_NullReference)
    BT_NullReference.reset(new BuiltinBug(this, "Returning null reference"));

  emitBug(C, *BT_NullReference, RetE, bugreporter::getDerefExpr(RetE));
}

void ento::registerReturnUndefChecker(CheckerManager &mgr) {
  mgr.registerChecker<ReturnUndefChecker>();
}
