//===--- LoopUnrolling.cpp - Unroll loops -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// This file contains functions which are used to decide if a loop worth to be
/// unrolled. Moreover, these functions manages the stack of loop which is
/// tracked by the ProgramState.
///
//===----------------------------------------------------------------------===//

#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/LoopUnrolling.h"

using namespace clang;
using namespace ento;
using namespace clang::ast_matchers;

static const int MAXIMUM_STEP_UNROLLED = 128;

struct LoopState {
private:
  enum Kind { Normal, Unrolled } K;
  const Stmt *LoopStmt;
  const LocationContext *LCtx;
  unsigned maxStep;
  LoopState(Kind InK, const Stmt *S, const LocationContext *L, unsigned N)
      : K(InK), LoopStmt(S), LCtx(L), maxStep(N) {}

public:
  static LoopState getNormal(const Stmt *S, const LocationContext *L,
                             unsigned N) {
    return LoopState(Normal, S, L, N);
  }
  static LoopState getUnrolled(const Stmt *S, const LocationContext *L,
                               unsigned N) {
    return LoopState(Unrolled, S, L, N);
  }
  bool isUnrolled() const { return K == Unrolled; }
  unsigned getMaxStep() const { return maxStep; }
  const Stmt *getLoopStmt() const { return LoopStmt; }
  const LocationContext *getLocationContext() const { return LCtx; }
  bool operator==(const LoopState &X) const {
    return K == X.K && LoopStmt == X.LoopStmt;
  }
  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddInteger(K);
    ID.AddPointer(LoopStmt);
    ID.AddPointer(LCtx);
    ID.AddInteger(maxStep);
  }
};

// The tracked stack of loops. The stack indicates that which loops the
// simulated element contained by. The loops are marked depending if we decided
// to unroll them.
// TODO: The loop stack should not need to be in the program state since it is
// lexical in nature. Instead, the stack of loops should be tracked in the
// LocationContext.
REGISTER_LIST_WITH_PROGRAMSTATE(LoopStack, LoopState)

namespace clang {
namespace ento {

static bool isLoopStmt(const Stmt *S) {
  return S && (isa<ForStmt>(S) || isa<WhileStmt>(S) || isa<DoStmt>(S));
}

ProgramStateRef processLoopEnd(const Stmt *LoopStmt, ProgramStateRef State) {
  auto LS = State->get<LoopStack>();
  if (!LS.isEmpty() && LS.getHead().getLoopStmt() == LoopStmt)
    State = State->set<LoopStack>(LS.getTail());
  return State;
}

static internal::Matcher<Stmt> simpleCondition(StringRef BindName) {
  return binaryOperator(anyOf(hasOperatorName("<"), hasOperatorName(">"),
                              hasOperatorName("<="), hasOperatorName(">="),
                              hasOperatorName("!=")),
                        hasEitherOperand(ignoringParenImpCasts(declRefExpr(
                            to(varDecl(hasType(isInteger())).bind(BindName))))),
                        hasEitherOperand(ignoringParenImpCasts(
                            integerLiteral().bind("boundNum"))))
      .bind("conditionOperator");
}

static internal::Matcher<Stmt>
changeIntBoundNode(internal::Matcher<Decl> VarNodeMatcher) {
  return anyOf(
      unaryOperator(anyOf(hasOperatorName("--"), hasOperatorName("++")),
                    hasUnaryOperand(ignoringParenImpCasts(
                        declRefExpr(to(varDecl(VarNodeMatcher)))))),
      binaryOperator(isAssignmentOperator(),
                     hasLHS(ignoringParenImpCasts(
                         declRefExpr(to(varDecl(VarNodeMatcher)))))));
}

static internal::Matcher<Stmt>
callByRef(internal::Matcher<Decl> VarNodeMatcher) {
  return callExpr(forEachArgumentWithParam(
      declRefExpr(to(varDecl(VarNodeMatcher))),
      parmVarDecl(hasType(references(qualType(unless(isConstQualified())))))));
}

static internal::Matcher<Stmt>
assignedToRef(internal::Matcher<Decl> VarNodeMatcher) {
  return declStmt(hasDescendant(varDecl(
      allOf(hasType(referenceType()),
            hasInitializer(anyOf(
                initListExpr(has(declRefExpr(to(varDecl(VarNodeMatcher))))),
                declRefExpr(to(varDecl(VarNodeMatcher)))))))));
}

static internal::Matcher<Stmt>
getAddrTo(internal::Matcher<Decl> VarNodeMatcher) {
  return unaryOperator(
      hasOperatorName("&"),
      hasUnaryOperand(declRefExpr(hasDeclaration(VarNodeMatcher))));
}

static internal::Matcher<Stmt> hasSuspiciousStmt(StringRef NodeName) {
  return hasDescendant(stmt(
      anyOf(gotoStmt(), switchStmt(), returnStmt(),
            // Escaping and not known mutation of the loop counter is handled
            // by exclusion of assigning and address-of operators and
            // pass-by-ref function calls on the loop counter from the body.
            changeIntBoundNode(equalsBoundNode(NodeName)),
            callByRef(equalsBoundNode(NodeName)),
            getAddrTo(equalsBoundNode(NodeName)),
            assignedToRef(equalsBoundNode(NodeName)))));
}

static internal::Matcher<Stmt> forLoopMatcher() {
  return forStmt(
             hasCondition(simpleCondition("initVarName")),
             // Initialization should match the form: 'int i = 6' or 'i = 42'.
             hasLoopInit(
                 anyOf(declStmt(hasSingleDecl(
                           varDecl(allOf(hasInitializer(ignoringParenImpCasts(
                                             integerLiteral().bind("initNum"))),
                                         equalsBoundNode("initVarName"))))),
                       binaryOperator(hasLHS(declRefExpr(to(varDecl(
                                          equalsBoundNode("initVarName"))))),
                                      hasRHS(ignoringParenImpCasts(
                                          integerLiteral().bind("initNum")))))),
             // Incrementation should be a simple increment or decrement
             // operator call.
             hasIncrement(unaryOperator(
                 anyOf(hasOperatorName("++"), hasOperatorName("--")),
                 hasUnaryOperand(declRefExpr(
                     to(varDecl(allOf(equalsBoundNode("initVarName"),
                                      hasType(isInteger())))))))),
             unless(hasBody(hasSuspiciousStmt("initVarName")))).bind("forLoop");
}

static bool isPossiblyEscaped(const VarDecl *VD, ExplodedNode *N) {
  // Global variables assumed as escaped variables.
  if (VD->hasGlobalStorage())
    return true;

  while (!N->pred_empty()) {
    const Stmt *S = PathDiagnosticLocation::getStmt(N);
    if (!S) {
      N = N->getFirstPred();
      continue;
    }

    if (const DeclStmt *DS = dyn_cast<DeclStmt>(S)) {
      for (const Decl *D : DS->decls()) {
        // Once we reach the declaration of the VD we can return.
        if (D->getCanonicalDecl() == VD)
          return false;
      }
    }
    // Check the usage of the pass-by-ref function calls and adress-of operator
    // on VD and reference initialized by VD.
    ASTContext &ASTCtx =
        N->getLocationContext()->getAnalysisDeclContext()->getASTContext();
    auto Match =
        match(stmt(anyOf(callByRef(equalsNode(VD)), getAddrTo(equalsNode(VD)),
                         assignedToRef(equalsNode(VD)))),
              *S, ASTCtx);
    if (!Match.empty())
      return true;

    N = N->getFirstPred();
  }
  llvm_unreachable("Reached root without finding the declaration of VD");
}

bool shouldCompletelyUnroll(const Stmt *LoopStmt, ASTContext &ASTCtx,
                            ExplodedNode *Pred, unsigned &maxStep) {

  if (!isLoopStmt(LoopStmt))
    return false;

  // TODO: Match the cases where the bound is not a concrete literal but an
  // integer with known value
  auto Matches = match(forLoopMatcher(), *LoopStmt, ASTCtx);
  if (Matches.empty())
    return false;

  auto CounterVar = Matches[0].getNodeAs<VarDecl>("initVarName");
  llvm::APInt BoundNum =
      Matches[0].getNodeAs<IntegerLiteral>("boundNum")->getValue();
  llvm::APInt InitNum =
      Matches[0].getNodeAs<IntegerLiteral>("initNum")->getValue();
  auto CondOp = Matches[0].getNodeAs<BinaryOperator>("conditionOperator");
  if (InitNum.getBitWidth() != BoundNum.getBitWidth()) {
    InitNum = InitNum.zextOrSelf(BoundNum.getBitWidth());
    BoundNum = BoundNum.zextOrSelf(InitNum.getBitWidth());
  }

  if (CondOp->getOpcode() == BO_GE || CondOp->getOpcode() == BO_LE)
    maxStep = (BoundNum - InitNum + 1).abs().getZExtValue();
  else
    maxStep = (BoundNum - InitNum).abs().getZExtValue();

  // Check if the counter of the loop is not escaped before.
  return !isPossiblyEscaped(CounterVar->getCanonicalDecl(), Pred);
}

bool madeNewBranch(ExplodedNode *N, const Stmt *LoopStmt) {
  const Stmt *S = nullptr;
  while (!N->pred_empty()) {
    if (N->succ_size() > 1)
      return true;

    ProgramPoint P = N->getLocation();
    if (Optional<BlockEntrance> BE = P.getAs<BlockEntrance>())
      S = BE->getBlock()->getTerminator();

    if (S == LoopStmt)
      return false;

    N = N->getFirstPred();
  }

  llvm_unreachable("Reached root without encountering the previous step");
}

// updateLoopStack is called on every basic block, therefore it needs to be fast
ProgramStateRef updateLoopStack(const Stmt *LoopStmt, ASTContext &ASTCtx,
                                ExplodedNode *Pred, unsigned maxVisitOnPath) {
  auto State = Pred->getState();
  auto LCtx = Pred->getLocationContext();

  if (!isLoopStmt(LoopStmt))
    return State;

  auto LS = State->get<LoopStack>();
  if (!LS.isEmpty() && LoopStmt == LS.getHead().getLoopStmt() &&
      LCtx == LS.getHead().getLocationContext()) {
    if (LS.getHead().isUnrolled() && madeNewBranch(Pred, LoopStmt)) {
      State = State->set<LoopStack>(LS.getTail());
      State = State->add<LoopStack>(
          LoopState::getNormal(LoopStmt, LCtx, maxVisitOnPath));
    }
    return State;
  }
  unsigned maxStep;
  if (!shouldCompletelyUnroll(LoopStmt, ASTCtx, Pred, maxStep)) {
    State = State->add<LoopStack>(
        LoopState::getNormal(LoopStmt, LCtx, maxVisitOnPath));
    return State;
  }

  unsigned outerStep = (LS.isEmpty() ? 1 : LS.getHead().getMaxStep());

  unsigned innerMaxStep = maxStep * outerStep;
  if (innerMaxStep > MAXIMUM_STEP_UNROLLED)
    State = State->add<LoopStack>(
        LoopState::getNormal(LoopStmt, LCtx, maxVisitOnPath));
  else
    State = State->add<LoopStack>(
        LoopState::getUnrolled(LoopStmt, LCtx, innerMaxStep));
  return State;
}

bool isUnrolledState(ProgramStateRef State) {
  auto LS = State->get<LoopStack>();
  if (LS.isEmpty() || !LS.getHead().isUnrolled())
    return false;
  return true;
}
}
}
