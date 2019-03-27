//== TraversalChecker.cpp -------------------------------------- -*- C++ -*--=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// These checkers print various aspects of the ExprEngine's traversal of the CFG
// as it builds the ExplodedGraph.
//
//===----------------------------------------------------------------------===//
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/ParentMap.h"
#include "clang/AST/StmtObjC.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

namespace {
class TraversalDumper : public Checker< check::BranchCondition,
                                        check::BeginFunction,
                                        check::EndFunction > {
public:
  void checkBranchCondition(const Stmt *Condition, CheckerContext &C) const;
  void checkBeginFunction(CheckerContext &C) const;
  void checkEndFunction(const ReturnStmt *RS, CheckerContext &C) const;
};
}

void TraversalDumper::checkBranchCondition(const Stmt *Condition,
                                           CheckerContext &C) const {
  // Special-case Objective-C's for-in loop, which uses the entire loop as its
  // condition. We just print the collection expression.
  const Stmt *Parent = dyn_cast<ObjCForCollectionStmt>(Condition);
  if (!Parent) {
    const ParentMap &Parents = C.getLocationContext()->getParentMap();
    Parent = Parents.getParent(Condition);
  }

  // It is mildly evil to print directly to llvm::outs() rather than emitting
  // warnings, but this ensures things do not get filtered out by the rest of
  // the static analyzer machinery.
  SourceLocation Loc = Parent->getBeginLoc();
  llvm::outs() << C.getSourceManager().getSpellingLineNumber(Loc) << " "
               << Parent->getStmtClassName() << "\n";
}

void TraversalDumper::checkBeginFunction(CheckerContext &C) const {
  llvm::outs() << "--BEGIN FUNCTION--\n";
}

void TraversalDumper::checkEndFunction(const ReturnStmt *RS,
                                       CheckerContext &C) const {
  llvm::outs() << "--END FUNCTION--\n";
}

void ento::registerTraversalDumper(CheckerManager &mgr) {
  mgr.registerChecker<TraversalDumper>();
}

//------------------------------------------------------------------------------

namespace {
class CallDumper : public Checker< check::PreCall,
                                   check::PostCall > {
public:
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
};
}

void CallDumper::checkPreCall(const CallEvent &Call, CheckerContext &C) const {
  unsigned Indentation = 0;
  for (const LocationContext *LC = C.getLocationContext()->getParent();
       LC != nullptr; LC = LC->getParent())
    ++Indentation;

  // It is mildly evil to print directly to llvm::outs() rather than emitting
  // warnings, but this ensures things do not get filtered out by the rest of
  // the static analyzer machinery.
  llvm::outs().indent(Indentation);
  Call.dump(llvm::outs());
}

void CallDumper::checkPostCall(const CallEvent &Call, CheckerContext &C) const {
  const Expr *CallE = Call.getOriginExpr();
  if (!CallE)
    return;

  unsigned Indentation = 0;
  for (const LocationContext *LC = C.getLocationContext()->getParent();
       LC != nullptr; LC = LC->getParent())
    ++Indentation;

  // It is mildly evil to print directly to llvm::outs() rather than emitting
  // warnings, but this ensures things do not get filtered out by the rest of
  // the static analyzer machinery.
  llvm::outs().indent(Indentation);
  if (Call.getResultType()->isVoidType())
    llvm::outs() << "Returning void\n";
  else
    llvm::outs() << "Returning " << C.getSVal(CallE) << "\n";
}

void ento::registerCallDumper(CheckerManager &mgr) {
  mgr.registerChecker<CallDumper>();
}
