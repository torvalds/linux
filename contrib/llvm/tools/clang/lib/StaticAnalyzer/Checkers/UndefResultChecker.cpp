//=== UndefResultChecker.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This defines UndefResultChecker, a builtin check in ExprEngine that
// performs checks for undefined results of non-assignment binary operators.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

namespace {
class UndefResultChecker
  : public Checker< check::PostStmt<BinaryOperator> > {

  mutable std::unique_ptr<BugType> BT;

public:
  void checkPostStmt(const BinaryOperator *B, CheckerContext &C) const;
};
} // end anonymous namespace

static bool isArrayIndexOutOfBounds(CheckerContext &C, const Expr *Ex) {
  ProgramStateRef state = C.getState();

  if (!isa<ArraySubscriptExpr>(Ex))
    return false;

  SVal Loc = C.getSVal(Ex);
  if (!Loc.isValid())
    return false;

  const MemRegion *MR = Loc.castAs<loc::MemRegionVal>().getRegion();
  const ElementRegion *ER = dyn_cast<ElementRegion>(MR);
  if (!ER)
    return false;

  DefinedOrUnknownSVal Idx = ER->getIndex().castAs<DefinedOrUnknownSVal>();
  DefinedOrUnknownSVal NumElements = C.getStoreManager().getSizeInElements(
      state, ER->getSuperRegion(), ER->getValueType());
  ProgramStateRef StInBound = state->assumeInBound(Idx, NumElements, true);
  ProgramStateRef StOutBound = state->assumeInBound(Idx, NumElements, false);
  return StOutBound && !StInBound;
}

static bool isShiftOverflow(const BinaryOperator *B, CheckerContext &C) {
  return C.isGreaterOrEqual(
      B->getRHS(), C.getASTContext().getIntWidth(B->getLHS()->getType()));
}

static bool isLeftShiftResultUnrepresentable(const BinaryOperator *B,
                                             CheckerContext &C) {
  SValBuilder &SB = C.getSValBuilder();
  ProgramStateRef State = C.getState();
  const llvm::APSInt *LHS = SB.getKnownValue(State, C.getSVal(B->getLHS()));
  const llvm::APSInt *RHS = SB.getKnownValue(State, C.getSVal(B->getRHS()));
  assert(LHS && RHS && "Values unknown, inconsistent state");
  return (unsigned)RHS->getZExtValue() > LHS->countLeadingZeros();
}

void UndefResultChecker::checkPostStmt(const BinaryOperator *B,
                                       CheckerContext &C) const {
  if (C.getSVal(B).isUndef()) {

    // Do not report assignments of uninitialized values inside swap functions.
    // This should allow to swap partially uninitialized structs
    // (radar://14129997)
    if (const FunctionDecl *EnclosingFunctionDecl =
        dyn_cast<FunctionDecl>(C.getStackFrame()->getDecl()))
      if (C.getCalleeName(EnclosingFunctionDecl) == "swap")
        return;

    // Generate an error node.
    ExplodedNode *N = C.generateErrorNode();
    if (!N)
      return;

    if (!BT)
      BT.reset(
          new BuiltinBug(this, "Result of operation is garbage or undefined"));

    SmallString<256> sbuf;
    llvm::raw_svector_ostream OS(sbuf);
    const Expr *Ex = nullptr;
    bool isLeft = true;

    if (C.getSVal(B->getLHS()).isUndef()) {
      Ex = B->getLHS()->IgnoreParenCasts();
      isLeft = true;
    }
    else if (C.getSVal(B->getRHS()).isUndef()) {
      Ex = B->getRHS()->IgnoreParenCasts();
      isLeft = false;
    }

    if (Ex) {
      OS << "The " << (isLeft ? "left" : "right") << " operand of '"
         << BinaryOperator::getOpcodeStr(B->getOpcode())
         << "' is a garbage value";
      if (isArrayIndexOutOfBounds(C, Ex))
        OS << " due to array index out of bounds";
    } else {
      // Neither operand was undefined, but the result is undefined.
      if ((B->getOpcode() == BinaryOperatorKind::BO_Shl ||
           B->getOpcode() == BinaryOperatorKind::BO_Shr) &&
          C.isNegative(B->getRHS())) {
        OS << "The result of the "
           << ((B->getOpcode() == BinaryOperatorKind::BO_Shl) ? "left"
                                                              : "right")
           << " shift is undefined because the right operand is negative";
        Ex = B->getRHS();
      } else if ((B->getOpcode() == BinaryOperatorKind::BO_Shl ||
                  B->getOpcode() == BinaryOperatorKind::BO_Shr) &&
                 isShiftOverflow(B, C)) {

        OS << "The result of the "
           << ((B->getOpcode() == BinaryOperatorKind::BO_Shl) ? "left"
                                                              : "right")
           << " shift is undefined due to shifting by ";
        Ex = B->getRHS();

        SValBuilder &SB = C.getSValBuilder();
        const llvm::APSInt *I =
            SB.getKnownValue(C.getState(), C.getSVal(B->getRHS()));
        if (!I)
          OS << "a value that is";
        else if (I->isUnsigned())
          OS << '\'' << I->getZExtValue() << "\', which is";
        else
          OS << '\'' << I->getSExtValue() << "\', which is";

        OS << " greater or equal to the width of type '"
           << B->getLHS()->getType().getAsString() << "'.";
      } else if (B->getOpcode() == BinaryOperatorKind::BO_Shl &&
                 C.isNegative(B->getLHS())) {
        OS << "The result of the left shift is undefined because the left "
              "operand is negative";
        Ex = B->getLHS();
      } else if (B->getOpcode() == BinaryOperatorKind::BO_Shl &&
                 isLeftShiftResultUnrepresentable(B, C)) {
        ProgramStateRef State = C.getState();
        SValBuilder &SB = C.getSValBuilder();
        const llvm::APSInt *LHS =
            SB.getKnownValue(State, C.getSVal(B->getLHS()));
        const llvm::APSInt *RHS =
            SB.getKnownValue(State, C.getSVal(B->getRHS()));
        OS << "The result of the left shift is undefined due to shifting \'"
           << LHS->getSExtValue() << "\' by \'" << RHS->getZExtValue()
           << "\', which is unrepresentable in the unsigned version of "
           << "the return type \'" << B->getLHS()->getType().getAsString()
           << "\'";
        Ex = B->getLHS();
      } else {
        OS << "The result of the '"
           << BinaryOperator::getOpcodeStr(B->getOpcode())
           << "' expression is undefined";
      }
    }
    auto report = llvm::make_unique<BugReport>(*BT, OS.str(), N);
    if (Ex) {
      report->addRange(Ex->getSourceRange());
      bugreporter::trackExpressionValue(N, Ex, *report);
    }
    else
      bugreporter::trackExpressionValue(N, B, *report);

    C.emitReport(std::move(report));
  }
}

void ento::registerUndefResultChecker(CheckerManager &mgr) {
  mgr.registerChecker<UndefResultChecker>();
}
