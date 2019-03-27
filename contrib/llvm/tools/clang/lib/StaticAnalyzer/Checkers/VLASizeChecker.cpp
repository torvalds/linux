//=== VLASizeChecker.cpp - Undefined dereference checker --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This defines VLASizeChecker, a builtin check in ExprEngine that
// performs checks for declaration of VLA of undefined or zero size.
// In addition, VLASizeChecker is responsible for defining the extent
// of the MemRegion that represents a VLA.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/CharUnits.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

namespace {
class VLASizeChecker : public Checker< check::PreStmt<DeclStmt> > {
  mutable std::unique_ptr<BugType> BT;
  enum VLASize_Kind { VLA_Garbage, VLA_Zero, VLA_Tainted, VLA_Negative };

  void reportBug(VLASize_Kind Kind, const Expr *SizeE, ProgramStateRef State,
                 CheckerContext &C,
                 std::unique_ptr<BugReporterVisitor> Visitor = nullptr) const;

public:
  void checkPreStmt(const DeclStmt *DS, CheckerContext &C) const;
};
} // end anonymous namespace

void VLASizeChecker::reportBug(
    VLASize_Kind Kind, const Expr *SizeE, ProgramStateRef State,
    CheckerContext &C, std::unique_ptr<BugReporterVisitor> Visitor) const {
  // Generate an error node.
  ExplodedNode *N = C.generateErrorNode(State);
  if (!N)
    return;

  if (!BT)
    BT.reset(new BuiltinBug(
        this, "Dangerous variable-length array (VLA) declaration"));

  SmallString<256> buf;
  llvm::raw_svector_ostream os(buf);
  os << "Declared variable-length array (VLA) ";
  switch (Kind) {
  case VLA_Garbage:
    os << "uses a garbage value as its size";
    break;
  case VLA_Zero:
    os << "has zero size";
    break;
  case VLA_Tainted:
    os << "has tainted size";
    break;
  case VLA_Negative:
    os << "has negative size";
    break;
  }

  auto report = llvm::make_unique<BugReport>(*BT, os.str(), N);
  report->addVisitor(std::move(Visitor));
  report->addRange(SizeE->getSourceRange());
  bugreporter::trackExpressionValue(N, SizeE, *report);
  C.emitReport(std::move(report));
}

void VLASizeChecker::checkPreStmt(const DeclStmt *DS, CheckerContext &C) const {
  if (!DS->isSingleDecl())
    return;

  const VarDecl *VD = dyn_cast<VarDecl>(DS->getSingleDecl());
  if (!VD)
    return;

  ASTContext &Ctx = C.getASTContext();
  const VariableArrayType *VLA = Ctx.getAsVariableArrayType(VD->getType());
  if (!VLA)
    return;

  // FIXME: Handle multi-dimensional VLAs.
  const Expr *SE = VLA->getSizeExpr();
  ProgramStateRef state = C.getState();
  SVal sizeV = C.getSVal(SE);

  if (sizeV.isUndef()) {
    reportBug(VLA_Garbage, SE, state, C);
    return;
  }

  // See if the size value is known. It can't be undefined because we would have
  // warned about that already.
  if (sizeV.isUnknown())
    return;

  // Check if the size is tainted.
  if (state->isTainted(sizeV)) {
    reportBug(VLA_Tainted, SE, nullptr, C,
              llvm::make_unique<TaintBugVisitor>(sizeV));
    return;
  }

  // Check if the size is zero.
  DefinedSVal sizeD = sizeV.castAs<DefinedSVal>();

  ProgramStateRef stateNotZero, stateZero;
  std::tie(stateNotZero, stateZero) = state->assume(sizeD);

  if (stateZero && !stateNotZero) {
    reportBug(VLA_Zero, SE, stateZero, C);
    return;
  }

  // From this point on, assume that the size is not zero.
  state = stateNotZero;

  // VLASizeChecker is responsible for defining the extent of the array being
  // declared. We do this by multiplying the array length by the element size,
  // then matching that with the array region's extent symbol.

  // Check if the size is negative.
  SValBuilder &svalBuilder = C.getSValBuilder();

  QualType Ty = SE->getType();
  DefinedOrUnknownSVal Zero = svalBuilder.makeZeroVal(Ty);

  SVal LessThanZeroVal = svalBuilder.evalBinOp(state, BO_LT, sizeD, Zero, Ty);
  if (Optional<DefinedSVal> LessThanZeroDVal =
        LessThanZeroVal.getAs<DefinedSVal>()) {
    ConstraintManager &CM = C.getConstraintManager();
    ProgramStateRef StatePos, StateNeg;

    std::tie(StateNeg, StatePos) = CM.assumeDual(state, *LessThanZeroDVal);
    if (StateNeg && !StatePos) {
      reportBug(VLA_Negative, SE, state, C);
      return;
    }
    state = StatePos;
  }

  // Convert the array length to size_t.
  QualType SizeTy = Ctx.getSizeType();
  NonLoc ArrayLength =
      svalBuilder.evalCast(sizeD, SizeTy, SE->getType()).castAs<NonLoc>();

  // Get the element size.
  CharUnits EleSize = Ctx.getTypeSizeInChars(VLA->getElementType());
  SVal EleSizeVal = svalBuilder.makeIntVal(EleSize.getQuantity(), SizeTy);

  // Multiply the array length by the element size.
  SVal ArraySizeVal = svalBuilder.evalBinOpNN(
      state, BO_Mul, ArrayLength, EleSizeVal.castAs<NonLoc>(), SizeTy);

  // Finally, assume that the array's extent matches the given size.
  const LocationContext *LC = C.getLocationContext();
  DefinedOrUnknownSVal Extent =
    state->getRegion(VD, LC)->getExtent(svalBuilder);
  DefinedOrUnknownSVal ArraySize = ArraySizeVal.castAs<DefinedOrUnknownSVal>();
  DefinedOrUnknownSVal sizeIsKnown =
    svalBuilder.evalEQ(state, Extent, ArraySize);
  state = state->assume(sizeIsKnown, true);

  // Assume should not fail at this point.
  assert(state);

  // Remember our assumptions!
  C.addTransition(state);
}

void ento::registerVLASizeChecker(CheckerManager &mgr) {
  mgr.registerChecker<VLASizeChecker>();
}
