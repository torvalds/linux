//== TestAfterDivZeroChecker.cpp - Test after division by zero checker --*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This defines TestAfterDivZeroChecker, a builtin check that performs checks
//  for division by zero where the division occurs before comparison with zero.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "llvm/ADT/FoldingSet.h"

using namespace clang;
using namespace ento;

namespace {

class ZeroState {
private:
  SymbolRef ZeroSymbol;
  unsigned BlockID;
  const StackFrameContext *SFC;

public:
  ZeroState(SymbolRef S, unsigned B, const StackFrameContext *SFC)
      : ZeroSymbol(S), BlockID(B), SFC(SFC) {}

  const StackFrameContext *getStackFrameContext() const { return SFC; }

  bool operator==(const ZeroState &X) const {
    return BlockID == X.BlockID && SFC == X.SFC && ZeroSymbol == X.ZeroSymbol;
  }

  bool operator<(const ZeroState &X) const {
    if (BlockID != X.BlockID)
      return BlockID < X.BlockID;
    if (SFC != X.SFC)
      return SFC < X.SFC;
    return ZeroSymbol < X.ZeroSymbol;
  }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddInteger(BlockID);
    ID.AddPointer(SFC);
    ID.AddPointer(ZeroSymbol);
  }
};

class DivisionBRVisitor : public BugReporterVisitor {
private:
  SymbolRef ZeroSymbol;
  const StackFrameContext *SFC;
  bool Satisfied;

public:
  DivisionBRVisitor(SymbolRef ZeroSymbol, const StackFrameContext *SFC)
      : ZeroSymbol(ZeroSymbol), SFC(SFC), Satisfied(false) {}

  void Profile(llvm::FoldingSetNodeID &ID) const override {
    ID.Add(ZeroSymbol);
    ID.Add(SFC);
  }

  std::shared_ptr<PathDiagnosticPiece> VisitNode(const ExplodedNode *Succ,
                                                 BugReporterContext &BRC,
                                                 BugReport &BR) override;
};

class TestAfterDivZeroChecker
    : public Checker<check::PreStmt<BinaryOperator>, check::BranchCondition,
                     check::EndFunction> {
  mutable std::unique_ptr<BuiltinBug> DivZeroBug;
  void reportBug(SVal Val, CheckerContext &C) const;

public:
  void checkPreStmt(const BinaryOperator *B, CheckerContext &C) const;
  void checkBranchCondition(const Stmt *Condition, CheckerContext &C) const;
  void checkEndFunction(const ReturnStmt *RS, CheckerContext &C) const;
  void setDivZeroMap(SVal Var, CheckerContext &C) const;
  bool hasDivZeroMap(SVal Var, const CheckerContext &C) const;
  bool isZero(SVal S, CheckerContext &C) const;
};
} // end anonymous namespace

REGISTER_SET_WITH_PROGRAMSTATE(DivZeroMap, ZeroState)

std::shared_ptr<PathDiagnosticPiece>
DivisionBRVisitor::VisitNode(const ExplodedNode *Succ, 
                             BugReporterContext &BRC, BugReport &BR) {
  if (Satisfied)
    return nullptr;

  const Expr *E = nullptr;

  if (Optional<PostStmt> P = Succ->getLocationAs<PostStmt>())
    if (const BinaryOperator *BO = P->getStmtAs<BinaryOperator>()) {
      BinaryOperator::Opcode Op = BO->getOpcode();
      if (Op == BO_Div || Op == BO_Rem || Op == BO_DivAssign ||
          Op == BO_RemAssign) {
        E = BO->getRHS();
      }
    }

  if (!E)
    return nullptr;

  SVal S = Succ->getSVal(E);
  if (ZeroSymbol == S.getAsSymbol() && SFC == Succ->getStackFrame()) {
    Satisfied = true;

    // Construct a new PathDiagnosticPiece.
    ProgramPoint P = Succ->getLocation();
    PathDiagnosticLocation L =
        PathDiagnosticLocation::create(P, BRC.getSourceManager());

    if (!L.isValid() || !L.asLocation().isValid())
      return nullptr;

    return std::make_shared<PathDiagnosticEventPiece>(
        L, "Division with compared value made here");
  }

  return nullptr;
}

bool TestAfterDivZeroChecker::isZero(SVal S, CheckerContext &C) const {
  Optional<DefinedSVal> DSV = S.getAs<DefinedSVal>();

  if (!DSV)
    return false;

  ConstraintManager &CM = C.getConstraintManager();
  return !CM.assume(C.getState(), *DSV, true);
}

void TestAfterDivZeroChecker::setDivZeroMap(SVal Var, CheckerContext &C) const {
  SymbolRef SR = Var.getAsSymbol();
  if (!SR)
    return;

  ProgramStateRef State = C.getState();
  State =
      State->add<DivZeroMap>(ZeroState(SR, C.getBlockID(), C.getStackFrame()));
  C.addTransition(State);
}

bool TestAfterDivZeroChecker::hasDivZeroMap(SVal Var,
                                            const CheckerContext &C) const {
  SymbolRef SR = Var.getAsSymbol();
  if (!SR)
    return false;

  ZeroState ZS(SR, C.getBlockID(), C.getStackFrame());
  return C.getState()->contains<DivZeroMap>(ZS);
}

void TestAfterDivZeroChecker::reportBug(SVal Val, CheckerContext &C) const {
  if (ExplodedNode *N = C.generateErrorNode(C.getState())) {
    if (!DivZeroBug)
      DivZeroBug.reset(new BuiltinBug(this, "Division by zero"));

    auto R = llvm::make_unique<BugReport>(
        *DivZeroBug, "Value being compared against zero has already been used "
                     "for division",
        N);

    R->addVisitor(llvm::make_unique<DivisionBRVisitor>(Val.getAsSymbol(),
                                                       C.getStackFrame()));
    C.emitReport(std::move(R));
  }
}

void TestAfterDivZeroChecker::checkEndFunction(const ReturnStmt *,
                                               CheckerContext &C) const {
  ProgramStateRef State = C.getState();

  DivZeroMapTy DivZeroes = State->get<DivZeroMap>();
  if (DivZeroes.isEmpty())
    return;

  DivZeroMapTy::Factory &F = State->get_context<DivZeroMap>();
  for (llvm::ImmutableSet<ZeroState>::iterator I = DivZeroes.begin(),
                                               E = DivZeroes.end();
       I != E; ++I) {
    ZeroState ZS = *I;
    if (ZS.getStackFrameContext() == C.getStackFrame())
      DivZeroes = F.remove(DivZeroes, ZS);
  }
  C.addTransition(State->set<DivZeroMap>(DivZeroes));
}

void TestAfterDivZeroChecker::checkPreStmt(const BinaryOperator *B,
                                           CheckerContext &C) const {
  BinaryOperator::Opcode Op = B->getOpcode();
  if (Op == BO_Div || Op == BO_Rem || Op == BO_DivAssign ||
      Op == BO_RemAssign) {
    SVal S = C.getSVal(B->getRHS());

    if (!isZero(S, C))
      setDivZeroMap(S, C);
  }
}

void TestAfterDivZeroChecker::checkBranchCondition(const Stmt *Condition,
                                                   CheckerContext &C) const {
  if (const BinaryOperator *B = dyn_cast<BinaryOperator>(Condition)) {
    if (B->isComparisonOp()) {
      const IntegerLiteral *IntLiteral = dyn_cast<IntegerLiteral>(B->getRHS());
      bool LRHS = true;
      if (!IntLiteral) {
        IntLiteral = dyn_cast<IntegerLiteral>(B->getLHS());
        LRHS = false;
      }

      if (!IntLiteral || IntLiteral->getValue() != 0)
        return;

      SVal Val = C.getSVal(LRHS ? B->getLHS() : B->getRHS());
      if (hasDivZeroMap(Val, C))
        reportBug(Val, C);
    }
  } else if (const UnaryOperator *U = dyn_cast<UnaryOperator>(Condition)) {
    if (U->getOpcode() == UO_LNot) {
      SVal Val;
      if (const ImplicitCastExpr *I =
              dyn_cast<ImplicitCastExpr>(U->getSubExpr()))
        Val = C.getSVal(I->getSubExpr());

      if (hasDivZeroMap(Val, C))
        reportBug(Val, C);
      else {
        Val = C.getSVal(U->getSubExpr());
        if (hasDivZeroMap(Val, C))
          reportBug(Val, C);
      }
    }
  } else if (const ImplicitCastExpr *IE =
                 dyn_cast<ImplicitCastExpr>(Condition)) {
    SVal Val = C.getSVal(IE->getSubExpr());

    if (hasDivZeroMap(Val, C))
      reportBug(Val, C);
    else {
      SVal Val = C.getSVal(Condition);

      if (hasDivZeroMap(Val, C))
        reportBug(Val, C);
    }
  }
}

void ento::registerTestAfterDivZeroChecker(CheckerManager &mgr) {
  mgr.registerChecker<TestAfterDivZeroChecker>();
}
