//== CheckerContext.cpp - Context info for path-sensitive checkers-----------=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines CheckerContext that provides contextual info for
//  path-sensitive checkers.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/Basic/Builtins.h"
#include "clang/Lex/Lexer.h"

using namespace clang;
using namespace ento;

const FunctionDecl *CheckerContext::getCalleeDecl(const CallExpr *CE) const {
  const Expr *Callee = CE->getCallee();
  SVal L = Pred->getSVal(Callee);
  return L.getAsFunctionDecl();
}

StringRef CheckerContext::getCalleeName(const FunctionDecl *FunDecl) const {
  if (!FunDecl)
    return StringRef();
  IdentifierInfo *funI = FunDecl->getIdentifier();
  if (!funI)
    return StringRef();
  return funI->getName();
}

StringRef CheckerContext::getDeclDescription(const Decl *D) {
  if (isa<ObjCMethodDecl>(D) || isa<CXXMethodDecl>(D))
    return "method";
  if (isa<BlockDecl>(D))
    return "anonymous block";
  return "function";
}

bool CheckerContext::isCLibraryFunction(const FunctionDecl *FD,
                                        StringRef Name) {
  // To avoid false positives (Ex: finding user defined functions with
  // similar names), only perform fuzzy name matching when it's a builtin.
  // Using a string compare is slow, we might want to switch on BuiltinID here.
  unsigned BId = FD->getBuiltinID();
  if (BId != 0) {
    if (Name.empty())
      return true;
    StringRef BName = FD->getASTContext().BuiltinInfo.getName(BId);
    if (BName.find(Name) != StringRef::npos)
      return true;
  }

  const IdentifierInfo *II = FD->getIdentifier();
  // If this is a special C++ name without IdentifierInfo, it can't be a
  // C library function.
  if (!II)
    return false;

  // Look through 'extern "C"' and anything similar invented in the future.
  // If this function is not in TU directly, it is not a C library function.
  if (!FD->getDeclContext()->getRedeclContext()->isTranslationUnit())
    return false;

  // If this function is not externally visible, it is not a C library function.
  // Note that we make an exception for inline functions, which may be
  // declared in header files without external linkage.
  if (!FD->isInlined() && !FD->isExternallyVisible())
    return false;

  if (Name.empty())
    return true;

  StringRef FName = II->getName();
  if (FName.equals(Name))
    return true;

  if (FName.startswith("__inline") && (FName.find(Name) != StringRef::npos))
    return true;

  if (FName.startswith("__") && FName.endswith("_chk") &&
      FName.find(Name) != StringRef::npos)
    return true;

  return false;
}

StringRef CheckerContext::getMacroNameOrSpelling(SourceLocation &Loc) {
  if (Loc.isMacroID())
    return Lexer::getImmediateMacroName(Loc, getSourceManager(),
                                             getLangOpts());
  SmallVector<char, 16> buf;
  return Lexer::getSpelling(Loc, buf, getSourceManager(), getLangOpts());
}

/// Evaluate comparison and return true if it's known that condition is true
static bool evalComparison(SVal LHSVal, BinaryOperatorKind ComparisonOp,
                           SVal RHSVal, ProgramStateRef State) {
  if (LHSVal.isUnknownOrUndef())
    return false;
  ProgramStateManager &Mgr = State->getStateManager();
  if (!LHSVal.getAs<NonLoc>()) {
    LHSVal = Mgr.getStoreManager().getBinding(State->getStore(),
                                              LHSVal.castAs<Loc>());
    if (LHSVal.isUnknownOrUndef() || !LHSVal.getAs<NonLoc>())
      return false;
  }

  SValBuilder &Bldr = Mgr.getSValBuilder();
  SVal Eval = Bldr.evalBinOp(State, ComparisonOp, LHSVal, RHSVal,
                             Bldr.getConditionType());
  if (Eval.isUnknownOrUndef())
    return false;
  ProgramStateRef StTrue, StFalse;
  std::tie(StTrue, StFalse) = State->assume(Eval.castAs<DefinedSVal>());
  return StTrue && !StFalse;
}

bool CheckerContext::isGreaterOrEqual(const Expr *E, unsigned long long Val) {
  DefinedSVal V = getSValBuilder().makeIntVal(Val, getASTContext().LongLongTy);
  return evalComparison(getSVal(E), BO_GE, V, getState());
}

bool CheckerContext::isNegative(const Expr *E) {
  DefinedSVal V = getSValBuilder().makeIntVal(0, false);
  return evalComparison(getSVal(E), BO_LT, V, getState());
}
