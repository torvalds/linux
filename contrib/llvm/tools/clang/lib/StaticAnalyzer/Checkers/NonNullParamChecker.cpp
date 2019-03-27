//===--- NonNullParamChecker.cpp - Undefined arguments checker -*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This defines NonNullParamChecker, which checks for arguments expected not to
// be null due to:
//   - the corresponding parameters being declared to have nonnull attribute
//   - the corresponding parameters being references; since the call would form
//     a reference to a null pointer
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/Attr.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

namespace {
class NonNullParamChecker
  : public Checker< check::PreCall, EventDispatcher<ImplicitNullDerefEvent> > {
  mutable std::unique_ptr<BugType> BTAttrNonNull;
  mutable std::unique_ptr<BugType> BTNullRefArg;

public:

  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;

  std::unique_ptr<BugReport>
  genReportNullAttrNonNull(const ExplodedNode *ErrorN, const Expr *ArgE) const;
  std::unique_ptr<BugReport>
  genReportReferenceToNullPointer(const ExplodedNode *ErrorN,
                                  const Expr *ArgE) const;
};
} // end anonymous namespace

/// \return Bitvector marking non-null attributes.
static llvm::SmallBitVector getNonNullAttrs(const CallEvent &Call) {
  const Decl *FD = Call.getDecl();
  unsigned NumArgs = Call.getNumArgs();
  llvm::SmallBitVector AttrNonNull(NumArgs);
  for (const auto *NonNull : FD->specific_attrs<NonNullAttr>()) {
    if (!NonNull->args_size()) {
      AttrNonNull.set(0, NumArgs);
      break;
    }
    for (const ParamIdx &Idx : NonNull->args()) {
      unsigned IdxAST = Idx.getASTIndex();
      if (IdxAST >= NumArgs)
        continue;
      AttrNonNull.set(IdxAST);
    }
  }
  return AttrNonNull;
}

void NonNullParamChecker::checkPreCall(const CallEvent &Call,
                                       CheckerContext &C) const {
  if (!Call.getDecl())
    return;

  llvm::SmallBitVector AttrNonNull = getNonNullAttrs(Call);
  unsigned NumArgs = Call.getNumArgs();

  ProgramStateRef state = C.getState();
  ArrayRef<ParmVarDecl*> parms = Call.parameters();

  for (unsigned idx = 0; idx < NumArgs; ++idx) {
    // For vararg functions, a corresponding parameter decl may not exist.
    bool HasParam = idx < parms.size();

    // Check if the parameter is a reference. We want to report when reference
    // to a null pointer is passed as a parameter.
    bool haveRefTypeParam =
        HasParam ? parms[idx]->getType()->isReferenceType() : false;
    bool haveAttrNonNull = AttrNonNull[idx];

    // Check if the parameter is also marked 'nonnull'.
    if (!haveAttrNonNull && HasParam)
      haveAttrNonNull = parms[idx]->hasAttr<NonNullAttr>();

    if (!haveAttrNonNull && !haveRefTypeParam)
      continue;

    // If the value is unknown or undefined, we can't perform this check.
    const Expr *ArgE = Call.getArgExpr(idx);
    SVal V = Call.getArgSVal(idx);
    auto DV = V.getAs<DefinedSVal>();
    if (!DV)
      continue;

    assert(!haveRefTypeParam || DV->getAs<Loc>());

    // Process the case when the argument is not a location.
    if (haveAttrNonNull && !DV->getAs<Loc>()) {
      // If the argument is a union type, we want to handle a potential
      // transparent_union GCC extension.
      if (!ArgE)
        continue;

      QualType T = ArgE->getType();
      const RecordType *UT = T->getAsUnionType();
      if (!UT || !UT->getDecl()->hasAttr<TransparentUnionAttr>())
        continue;

      auto CSV = DV->getAs<nonloc::CompoundVal>();

      // FIXME: Handle LazyCompoundVals?
      if (!CSV)
        continue;

      V = *(CSV->begin());
      DV = V.getAs<DefinedSVal>();
      assert(++CSV->begin() == CSV->end());
      // FIXME: Handle (some_union){ some_other_union_val }, which turns into
      // a LazyCompoundVal inside a CompoundVal.
      if (!V.getAs<Loc>())
        continue;

      // Retrieve the corresponding expression.
      if (const auto *CE = dyn_cast<CompoundLiteralExpr>(ArgE))
        if (const auto *IE = dyn_cast<InitListExpr>(CE->getInitializer()))
          ArgE = dyn_cast<Expr>(*(IE->begin()));
    }

    ConstraintManager &CM = C.getConstraintManager();
    ProgramStateRef stateNotNull, stateNull;
    std::tie(stateNotNull, stateNull) = CM.assumeDual(state, *DV);

    // Generate an error node.  Check for a null node in case
    // we cache out.
    if (stateNull && !stateNotNull) {
      if (ExplodedNode *errorNode = C.generateErrorNode(stateNull)) {

        std::unique_ptr<BugReport> R;
        if (haveAttrNonNull)
          R = genReportNullAttrNonNull(errorNode, ArgE);
        else if (haveRefTypeParam)
          R = genReportReferenceToNullPointer(errorNode, ArgE);

        // Highlight the range of the argument that was null.
        R->addRange(Call.getArgSourceRange(idx));

        // Emit the bug report.
        C.emitReport(std::move(R));
      }

      // Always return.  Either we cached out or we just emitted an error.
      return;
    }

    if (stateNull) {
      if (ExplodedNode *N = C.generateSink(stateNull, C.getPredecessor())) {
        ImplicitNullDerefEvent event = {
          V, false, N, &C.getBugReporter(),
          /*IsDirectDereference=*/haveRefTypeParam};
        dispatchEvent(event);
      }
    }

    // If a pointer value passed the check we should assume that it is
    // indeed not null from this point forward.
    state = stateNotNull;
  }

  // If we reach here all of the arguments passed the nonnull check.
  // If 'state' has been updated generated a new node.
  C.addTransition(state);
}

std::unique_ptr<BugReport>
NonNullParamChecker::genReportNullAttrNonNull(const ExplodedNode *ErrorNode,
                                              const Expr *ArgE) const {
  // Lazily allocate the BugType object if it hasn't already been
  // created. Ownership is transferred to the BugReporter object once
  // the BugReport is passed to 'EmitWarning'.
  if (!BTAttrNonNull)
    BTAttrNonNull.reset(new BugType(
        this, "Argument with 'nonnull' attribute passed null", "API"));

  auto R = llvm::make_unique<BugReport>(
      *BTAttrNonNull,
      "Null pointer passed as an argument to a 'nonnull' parameter", ErrorNode);
  if (ArgE)
    bugreporter::trackExpressionValue(ErrorNode, ArgE, *R);

  return R;
}

std::unique_ptr<BugReport> NonNullParamChecker::genReportReferenceToNullPointer(
    const ExplodedNode *ErrorNode, const Expr *ArgE) const {
  if (!BTNullRefArg)
    BTNullRefArg.reset(new BuiltinBug(this, "Dereference of null pointer"));

  auto R = llvm::make_unique<BugReport>(
      *BTNullRefArg, "Forming reference to null pointer", ErrorNode);
  if (ArgE) {
    const Expr *ArgEDeref = bugreporter::getDerefExpr(ArgE);
    if (!ArgEDeref)
      ArgEDeref = ArgE;
    bugreporter::trackExpressionValue(ErrorNode, ArgEDeref, *R);
  }
  return R;

}

void ento::registerNonNullParamChecker(CheckerManager &mgr) {
  mgr.registerChecker<NonNullParamChecker>();
}
