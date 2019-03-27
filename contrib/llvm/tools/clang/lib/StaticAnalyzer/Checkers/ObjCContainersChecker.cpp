//== ObjCContainersChecker.cpp - Path sensitive checker for CFArray *- C++ -*=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Performs path sensitive checks of Core Foundation static containers like
// CFArray.
// 1) Check for buffer overflows:
//      In CFArrayGetArrayAtIndex( myArray, index), if the index is outside the
//      index space of theArray (0 to N-1 inclusive (where N is the count of
//      theArray), the behavior is undefined.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/ParentMap.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"

using namespace clang;
using namespace ento;

namespace {
class ObjCContainersChecker : public Checker< check::PreStmt<CallExpr>,
                                             check::PostStmt<CallExpr>,
                                             check::PointerEscape> {
  mutable std::unique_ptr<BugType> BT;
  inline void initBugType() const {
    if (!BT)
      BT.reset(new BugType(this, "CFArray API",
                           categories::CoreFoundationObjectiveC));
  }

  inline SymbolRef getArraySym(const Expr *E, CheckerContext &C) const {
    SVal ArrayRef = C.getSVal(E);
    SymbolRef ArraySym = ArrayRef.getAsSymbol();
    return ArraySym;
  }

  void addSizeInfo(const Expr *Array, const Expr *Size,
                   CheckerContext &C) const;

public:
  /// A tag to id this checker.
  static void *getTag() { static int Tag; return &Tag; }

  void checkPostStmt(const CallExpr *CE, CheckerContext &C) const;
  void checkPreStmt(const CallExpr *CE, CheckerContext &C) const;
  ProgramStateRef checkPointerEscape(ProgramStateRef State,
                                     const InvalidatedSymbols &Escaped,
                                     const CallEvent *Call,
                                     PointerEscapeKind Kind) const;

  void printState(raw_ostream &OS, ProgramStateRef State,
                  const char *NL, const char *Sep) const;
};
} // end anonymous namespace

// ProgramState trait - a map from array symbol to its state.
REGISTER_MAP_WITH_PROGRAMSTATE(ArraySizeMap, SymbolRef, DefinedSVal)

void ObjCContainersChecker::addSizeInfo(const Expr *Array, const Expr *Size,
                                        CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  SVal SizeV = C.getSVal(Size);
  // Undefined is reported by another checker.
  if (SizeV.isUnknownOrUndef())
    return;

  // Get the ArrayRef symbol.
  SVal ArrayRef = C.getSVal(Array);
  SymbolRef ArraySym = ArrayRef.getAsSymbol();
  if (!ArraySym)
    return;

  C.addTransition(
      State->set<ArraySizeMap>(ArraySym, SizeV.castAs<DefinedSVal>()));
}

void ObjCContainersChecker::checkPostStmt(const CallExpr *CE,
                                          CheckerContext &C) const {
  StringRef Name = C.getCalleeName(CE);
  if (Name.empty() || CE->getNumArgs() < 1)
    return;

  // Add array size information to the state.
  if (Name.equals("CFArrayCreate")) {
    if (CE->getNumArgs() < 3)
      return;
    // Note, we can visit the Create method in the post-visit because
    // the CFIndex parameter is passed in by value and will not be invalidated
    // by the call.
    addSizeInfo(CE, CE->getArg(2), C);
    return;
  }

  if (Name.equals("CFArrayGetCount")) {
    addSizeInfo(CE->getArg(0), CE, C);
    return;
  }
}

void ObjCContainersChecker::checkPreStmt(const CallExpr *CE,
                                         CheckerContext &C) const {
  StringRef Name = C.getCalleeName(CE);
  if (Name.empty() || CE->getNumArgs() < 2)
    return;

  // Check the array access.
  if (Name.equals("CFArrayGetValueAtIndex")) {
    ProgramStateRef State = C.getState();
    // Retrieve the size.
    // Find out if we saw this array symbol before and have information about
    // it.
    const Expr *ArrayExpr = CE->getArg(0);
    SymbolRef ArraySym = getArraySym(ArrayExpr, C);
    if (!ArraySym)
      return;

    const DefinedSVal *Size = State->get<ArraySizeMap>(ArraySym);

    if (!Size)
      return;

    // Get the index.
    const Expr *IdxExpr = CE->getArg(1);
    SVal IdxVal = C.getSVal(IdxExpr);
    if (IdxVal.isUnknownOrUndef())
      return;
    DefinedSVal Idx = IdxVal.castAs<DefinedSVal>();

    // Now, check if 'Idx in [0, Size-1]'.
    const QualType T = IdxExpr->getType();
    ProgramStateRef StInBound = State->assumeInBound(Idx, *Size, true, T);
    ProgramStateRef StOutBound = State->assumeInBound(Idx, *Size, false, T);
    if (StOutBound && !StInBound) {
      ExplodedNode *N = C.generateErrorNode(StOutBound);
      if (!N)
        return;
      initBugType();
      auto R = llvm::make_unique<BugReport>(*BT, "Index is out of bounds", N);
      R->addRange(IdxExpr->getSourceRange());
      bugreporter::trackExpressionValue(N, IdxExpr, *R,
                                        /*EnableNullFPSuppression=*/false);
      C.emitReport(std::move(R));
      return;
    }
  }
}

ProgramStateRef
ObjCContainersChecker::checkPointerEscape(ProgramStateRef State,
                                          const InvalidatedSymbols &Escaped,
                                          const CallEvent *Call,
                                          PointerEscapeKind Kind) const {
  for (const auto &Sym : Escaped) {
    // When a symbol for a mutable array escapes, we can't reason precisely
    // about its size any more -- so remove it from the map.
    // Note that we aren't notified here when a CFMutableArrayRef escapes as a
    // CFArrayRef. This is because CFArrayRef is typedef'd as a pointer to a
    // const-qualified type.
    State = State->remove<ArraySizeMap>(Sym);
  }
  return State;
}

void ObjCContainersChecker::printState(raw_ostream &OS, ProgramStateRef State,
                                       const char *NL, const char *Sep) const {
  ArraySizeMapTy Map = State->get<ArraySizeMap>();
  if (Map.isEmpty())
    return;

  OS << Sep << "ObjC container sizes :" << NL;
  for (auto I : Map) {
    OS << I.first << " : " << I.second << NL;
  }
}

/// Register checker.
void ento::registerObjCContainersChecker(CheckerManager &mgr) {
  mgr.registerChecker<ObjCContainersChecker>();
}
