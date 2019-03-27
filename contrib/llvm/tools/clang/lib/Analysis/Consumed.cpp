//===- Consumed.cpp -------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// A intra-procedural analysis for checking consumed properties.  This is based,
// in part, on research on linear types.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/Consumed.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/Type.h"
#include "clang/Analysis/Analyses/PostOrderCFGView.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <memory>
#include <utility>

// TODO: Adjust states of args to constructors in the same way that arguments to
//       function calls are handled.
// TODO: Use information from tests in for- and while-loop conditional.
// TODO: Add notes about the actual and expected state for
// TODO: Correctly identify unreachable blocks when chaining boolean operators.
// TODO: Adjust the parser and AttributesList class to support lists of
//       identifiers.
// TODO: Warn about unreachable code.
// TODO: Switch to using a bitmap to track unreachable blocks.
// TODO: Handle variable definitions, e.g. bool valid = x.isValid();
//       if (valid) ...; (Deferred)
// TODO: Take notes on state transitions to provide better warning messages.
//       (Deferred)
// TODO: Test nested conditionals: A) Checking the same value multiple times,
//       and 2) Checking different values. (Deferred)

using namespace clang;
using namespace consumed;

// Key method definition
ConsumedWarningsHandlerBase::~ConsumedWarningsHandlerBase() = default;

static SourceLocation getFirstStmtLoc(const CFGBlock *Block) {
  // Find the source location of the first statement in the block, if the block
  // is not empty.
  for (const auto &B : *Block)
    if (Optional<CFGStmt> CS = B.getAs<CFGStmt>())
      return CS->getStmt()->getBeginLoc();

  // Block is empty.
  // If we have one successor, return the first statement in that block
  if (Block->succ_size() == 1 && *Block->succ_begin())
    return getFirstStmtLoc(*Block->succ_begin());

  return {};
}

static SourceLocation getLastStmtLoc(const CFGBlock *Block) {
  // Find the source location of the last statement in the block, if the block
  // is not empty.
  if (const Stmt *StmtNode = Block->getTerminator()) {
    return StmtNode->getBeginLoc();
  } else {
    for (CFGBlock::const_reverse_iterator BI = Block->rbegin(),
         BE = Block->rend(); BI != BE; ++BI) {
      if (Optional<CFGStmt> CS = BI->getAs<CFGStmt>())
        return CS->getStmt()->getBeginLoc();
    }
  }

  // If we have one successor, return the first statement in that block
  SourceLocation Loc;
  if (Block->succ_size() == 1 && *Block->succ_begin())
    Loc = getFirstStmtLoc(*Block->succ_begin());
  if (Loc.isValid())
    return Loc;

  // If we have one predecessor, return the last statement in that block
  if (Block->pred_size() == 1 && *Block->pred_begin())
    return getLastStmtLoc(*Block->pred_begin());

  return Loc;
}

static ConsumedState invertConsumedUnconsumed(ConsumedState State) {
  switch (State) {
  case CS_Unconsumed:
    return CS_Consumed;
  case CS_Consumed:
    return CS_Unconsumed;
  case CS_None:
    return CS_None;
  case CS_Unknown:
    return CS_Unknown;
  }
  llvm_unreachable("invalid enum");
}

static bool isCallableInState(const CallableWhenAttr *CWAttr,
                              ConsumedState State) {
  for (const auto &S : CWAttr->callableStates()) {
    ConsumedState MappedAttrState = CS_None;

    switch (S) {
    case CallableWhenAttr::Unknown:
      MappedAttrState = CS_Unknown;
      break;

    case CallableWhenAttr::Unconsumed:
      MappedAttrState = CS_Unconsumed;
      break;

    case CallableWhenAttr::Consumed:
      MappedAttrState = CS_Consumed;
      break;
    }

    if (MappedAttrState == State)
      return true;
  }

  return false;
}

static bool isConsumableType(const QualType &QT) {
  if (QT->isPointerType() || QT->isReferenceType())
    return false;

  if (const CXXRecordDecl *RD = QT->getAsCXXRecordDecl())
    return RD->hasAttr<ConsumableAttr>();

  return false;
}

static bool isAutoCastType(const QualType &QT) {
  if (QT->isPointerType() || QT->isReferenceType())
    return false;

  if (const CXXRecordDecl *RD = QT->getAsCXXRecordDecl())
    return RD->hasAttr<ConsumableAutoCastAttr>();

  return false;
}

static bool isSetOnReadPtrType(const QualType &QT) {
  if (const CXXRecordDecl *RD = QT->getPointeeCXXRecordDecl())
    return RD->hasAttr<ConsumableSetOnReadAttr>();
  return false;
}

static bool isKnownState(ConsumedState State) {
  switch (State) {
  case CS_Unconsumed:
  case CS_Consumed:
    return true;
  case CS_None:
  case CS_Unknown:
    return false;
  }
  llvm_unreachable("invalid enum");
}

static bool isRValueRef(QualType ParamType) {
  return ParamType->isRValueReferenceType();
}

static bool isTestingFunction(const FunctionDecl *FunDecl) {
  return FunDecl->hasAttr<TestTypestateAttr>();
}

static bool isPointerOrRef(QualType ParamType) {
  return ParamType->isPointerType() || ParamType->isReferenceType();
}

static ConsumedState mapConsumableAttrState(const QualType QT) {
  assert(isConsumableType(QT));

  const ConsumableAttr *CAttr =
      QT->getAsCXXRecordDecl()->getAttr<ConsumableAttr>();

  switch (CAttr->getDefaultState()) {
  case ConsumableAttr::Unknown:
    return CS_Unknown;
  case ConsumableAttr::Unconsumed:
    return CS_Unconsumed;
  case ConsumableAttr::Consumed:
    return CS_Consumed;
  }
  llvm_unreachable("invalid enum");
}

static ConsumedState
mapParamTypestateAttrState(const ParamTypestateAttr *PTAttr) {
  switch (PTAttr->getParamState()) {
  case ParamTypestateAttr::Unknown:
    return CS_Unknown;
  case ParamTypestateAttr::Unconsumed:
    return CS_Unconsumed;
  case ParamTypestateAttr::Consumed:
    return CS_Consumed;
  }
  llvm_unreachable("invalid_enum");
}

static ConsumedState
mapReturnTypestateAttrState(const ReturnTypestateAttr *RTSAttr) {
  switch (RTSAttr->getState()) {
  case ReturnTypestateAttr::Unknown:
    return CS_Unknown;
  case ReturnTypestateAttr::Unconsumed:
    return CS_Unconsumed;
  case ReturnTypestateAttr::Consumed:
    return CS_Consumed;
  }
  llvm_unreachable("invalid enum");
}

static ConsumedState mapSetTypestateAttrState(const SetTypestateAttr *STAttr) {
  switch (STAttr->getNewState()) {
  case SetTypestateAttr::Unknown:
    return CS_Unknown;
  case SetTypestateAttr::Unconsumed:
    return CS_Unconsumed;
  case SetTypestateAttr::Consumed:
    return CS_Consumed;
  }
  llvm_unreachable("invalid_enum");
}

static StringRef stateToString(ConsumedState State) {
  switch (State) {
  case consumed::CS_None:
    return "none";

  case consumed::CS_Unknown:
    return "unknown";

  case consumed::CS_Unconsumed:
    return "unconsumed";

  case consumed::CS_Consumed:
    return "consumed";
  }
  llvm_unreachable("invalid enum");
}

static ConsumedState testsFor(const FunctionDecl *FunDecl) {
  assert(isTestingFunction(FunDecl));
  switch (FunDecl->getAttr<TestTypestateAttr>()->getTestState()) {
  case TestTypestateAttr::Unconsumed:
    return CS_Unconsumed;
  case TestTypestateAttr::Consumed:
    return CS_Consumed;
  }
  llvm_unreachable("invalid enum");
}

namespace {

struct VarTestResult {
  const VarDecl *Var;
  ConsumedState TestsFor;
};

} // namespace

namespace clang {
namespace consumed {

enum EffectiveOp {
  EO_And,
  EO_Or
};

class PropagationInfo {
  enum {
    IT_None,
    IT_State,
    IT_VarTest,
    IT_BinTest,
    IT_Var,
    IT_Tmp
  } InfoType = IT_None;

  struct BinTestTy {
    const BinaryOperator *Source;
    EffectiveOp EOp;
    VarTestResult LTest;
    VarTestResult RTest;
  };

  union {
    ConsumedState State;
    VarTestResult VarTest;
    const VarDecl *Var;
    const CXXBindTemporaryExpr *Tmp;
    BinTestTy BinTest;
  };

public:
  PropagationInfo() = default;
  PropagationInfo(const VarTestResult &VarTest)
      : InfoType(IT_VarTest), VarTest(VarTest) {}

  PropagationInfo(const VarDecl *Var, ConsumedState TestsFor)
      : InfoType(IT_VarTest) {
    VarTest.Var      = Var;
    VarTest.TestsFor = TestsFor;
  }

  PropagationInfo(const BinaryOperator *Source, EffectiveOp EOp,
                  const VarTestResult &LTest, const VarTestResult &RTest)
      : InfoType(IT_BinTest) {
    BinTest.Source  = Source;
    BinTest.EOp     = EOp;
    BinTest.LTest   = LTest;
    BinTest.RTest   = RTest;
  }

  PropagationInfo(const BinaryOperator *Source, EffectiveOp EOp,
                  const VarDecl *LVar, ConsumedState LTestsFor,
                  const VarDecl *RVar, ConsumedState RTestsFor)
      : InfoType(IT_BinTest) {
    BinTest.Source         = Source;
    BinTest.EOp            = EOp;
    BinTest.LTest.Var      = LVar;
    BinTest.LTest.TestsFor = LTestsFor;
    BinTest.RTest.Var      = RVar;
    BinTest.RTest.TestsFor = RTestsFor;
  }

  PropagationInfo(ConsumedState State)
      : InfoType(IT_State), State(State) {}
  PropagationInfo(const VarDecl *Var) : InfoType(IT_Var), Var(Var) {}
  PropagationInfo(const CXXBindTemporaryExpr *Tmp)
      : InfoType(IT_Tmp), Tmp(Tmp) {}

  const ConsumedState &getState() const {
    assert(InfoType == IT_State);
    return State;
  }

  const VarTestResult &getVarTest() const {
    assert(InfoType == IT_VarTest);
    return VarTest;
  }

  const VarTestResult &getLTest() const {
    assert(InfoType == IT_BinTest);
    return BinTest.LTest;
  }

  const VarTestResult &getRTest() const {
    assert(InfoType == IT_BinTest);
    return BinTest.RTest;
  }

  const VarDecl *getVar() const {
    assert(InfoType == IT_Var);
    return Var;
  }

  const CXXBindTemporaryExpr *getTmp() const {
    assert(InfoType == IT_Tmp);
    return Tmp;
  }

  ConsumedState getAsState(const ConsumedStateMap *StateMap) const {
    assert(isVar() || isTmp() || isState());

    if (isVar())
      return StateMap->getState(Var);
    else if (isTmp())
      return StateMap->getState(Tmp);
    else if (isState())
      return State;
    else
      return CS_None;
  }

  EffectiveOp testEffectiveOp() const {
    assert(InfoType == IT_BinTest);
    return BinTest.EOp;
  }

  const BinaryOperator * testSourceNode() const {
    assert(InfoType == IT_BinTest);
    return BinTest.Source;
  }

  bool isValid() const { return InfoType != IT_None; }
  bool isState() const { return InfoType == IT_State; }
  bool isVarTest() const { return InfoType == IT_VarTest; }
  bool isBinTest() const { return InfoType == IT_BinTest; }
  bool isVar() const { return InfoType == IT_Var; }
  bool isTmp() const { return InfoType == IT_Tmp; }

  bool isTest() const {
    return InfoType == IT_VarTest || InfoType == IT_BinTest;
  }

  bool isPointerToValue() const {
    return InfoType == IT_Var || InfoType == IT_Tmp;
  }

  PropagationInfo invertTest() const {
    assert(InfoType == IT_VarTest || InfoType == IT_BinTest);

    if (InfoType == IT_VarTest) {
      return PropagationInfo(VarTest.Var,
                             invertConsumedUnconsumed(VarTest.TestsFor));

    } else if (InfoType == IT_BinTest) {
      return PropagationInfo(BinTest.Source,
        BinTest.EOp == EO_And ? EO_Or : EO_And,
        BinTest.LTest.Var, invertConsumedUnconsumed(BinTest.LTest.TestsFor),
        BinTest.RTest.Var, invertConsumedUnconsumed(BinTest.RTest.TestsFor));
    } else {
      return {};
    }
  }
};

} // namespace consumed
} // namespace clang

static void
setStateForVarOrTmp(ConsumedStateMap *StateMap, const PropagationInfo &PInfo,
                    ConsumedState State) {
  assert(PInfo.isVar() || PInfo.isTmp());

  if (PInfo.isVar())
    StateMap->setState(PInfo.getVar(), State);
  else
    StateMap->setState(PInfo.getTmp(), State);
}

namespace clang {
namespace consumed {

class ConsumedStmtVisitor : public ConstStmtVisitor<ConsumedStmtVisitor> {
  using MapType = llvm::DenseMap<const Stmt *, PropagationInfo>;
  using PairType= std::pair<const Stmt *, PropagationInfo>;
  using InfoEntry = MapType::iterator;
  using ConstInfoEntry = MapType::const_iterator;

  ConsumedAnalyzer &Analyzer;
  ConsumedStateMap *StateMap;
  MapType PropagationMap;

  InfoEntry findInfo(const Expr *E) {
    if (const auto Cleanups = dyn_cast<ExprWithCleanups>(E))
      if (!Cleanups->cleanupsHaveSideEffects())
        E = Cleanups->getSubExpr();
    return PropagationMap.find(E->IgnoreParens());
  }

  ConstInfoEntry findInfo(const Expr *E) const {
    if (const auto Cleanups = dyn_cast<ExprWithCleanups>(E))
      if (!Cleanups->cleanupsHaveSideEffects())
        E = Cleanups->getSubExpr();
    return PropagationMap.find(E->IgnoreParens());
  }

  void insertInfo(const Expr *E, const PropagationInfo &PI) {
    PropagationMap.insert(PairType(E->IgnoreParens(), PI));
  }

  void forwardInfo(const Expr *From, const Expr *To);
  void copyInfo(const Expr *From, const Expr *To, ConsumedState CS);
  ConsumedState getInfo(const Expr *From);
  void setInfo(const Expr *To, ConsumedState NS);
  void propagateReturnType(const Expr *Call, const FunctionDecl *Fun);

public:
  void checkCallability(const PropagationInfo &PInfo,
                        const FunctionDecl *FunDecl,
                        SourceLocation BlameLoc);
  bool handleCall(const CallExpr *Call, const Expr *ObjArg,
                  const FunctionDecl *FunD);

  void VisitBinaryOperator(const BinaryOperator *BinOp);
  void VisitCallExpr(const CallExpr *Call);
  void VisitCastExpr(const CastExpr *Cast);
  void VisitCXXBindTemporaryExpr(const CXXBindTemporaryExpr *Temp);
  void VisitCXXConstructExpr(const CXXConstructExpr *Call);
  void VisitCXXMemberCallExpr(const CXXMemberCallExpr *Call);
  void VisitCXXOperatorCallExpr(const CXXOperatorCallExpr *Call);
  void VisitDeclRefExpr(const DeclRefExpr *DeclRef);
  void VisitDeclStmt(const DeclStmt *DelcS);
  void VisitMaterializeTemporaryExpr(const MaterializeTemporaryExpr *Temp);
  void VisitMemberExpr(const MemberExpr *MExpr);
  void VisitParmVarDecl(const ParmVarDecl *Param);
  void VisitReturnStmt(const ReturnStmt *Ret);
  void VisitUnaryOperator(const UnaryOperator *UOp);
  void VisitVarDecl(const VarDecl *Var);

  ConsumedStmtVisitor(ConsumedAnalyzer &Analyzer, ConsumedStateMap *StateMap)
      : Analyzer(Analyzer), StateMap(StateMap) {}

  PropagationInfo getInfo(const Expr *StmtNode) const {
    ConstInfoEntry Entry = findInfo(StmtNode);

    if (Entry != PropagationMap.end())
      return Entry->second;
    else
      return {};
  }

  void reset(ConsumedStateMap *NewStateMap) {
    StateMap = NewStateMap;
  }
};

} // namespace consumed
} // namespace clang

void ConsumedStmtVisitor::forwardInfo(const Expr *From, const Expr *To) {
  InfoEntry Entry = findInfo(From);
  if (Entry != PropagationMap.end())
    insertInfo(To, Entry->second);
}

// Create a new state for To, which is initialized to the state of From.
// If NS is not CS_None, sets the state of From to NS.
void ConsumedStmtVisitor::copyInfo(const Expr *From, const Expr *To,
                                   ConsumedState NS) {
  InfoEntry Entry = findInfo(From);
  if (Entry != PropagationMap.end()) {
    PropagationInfo& PInfo = Entry->second;
    ConsumedState CS = PInfo.getAsState(StateMap);
    if (CS != CS_None)
      insertInfo(To, PropagationInfo(CS));
    if (NS != CS_None && PInfo.isPointerToValue())
      setStateForVarOrTmp(StateMap, PInfo, NS);
  }
}

// Get the ConsumedState for From
ConsumedState ConsumedStmtVisitor::getInfo(const Expr *From) {
  InfoEntry Entry = findInfo(From);
  if (Entry != PropagationMap.end()) {
    PropagationInfo& PInfo = Entry->second;
    return PInfo.getAsState(StateMap);
  }
  return CS_None;
}

// If we already have info for To then update it, otherwise create a new entry.
void ConsumedStmtVisitor::setInfo(const Expr *To, ConsumedState NS) {
  InfoEntry Entry = findInfo(To);
  if (Entry != PropagationMap.end()) {
    PropagationInfo& PInfo = Entry->second;
    if (PInfo.isPointerToValue())
      setStateForVarOrTmp(StateMap, PInfo, NS);
  } else if (NS != CS_None) {
     insertInfo(To, PropagationInfo(NS));
  }
}

void ConsumedStmtVisitor::checkCallability(const PropagationInfo &PInfo,
                                           const FunctionDecl *FunDecl,
                                           SourceLocation BlameLoc) {
  assert(!PInfo.isTest());

  const CallableWhenAttr *CWAttr = FunDecl->getAttr<CallableWhenAttr>();
  if (!CWAttr)
    return;

  if (PInfo.isVar()) {
    ConsumedState VarState = StateMap->getState(PInfo.getVar());

    if (VarState == CS_None || isCallableInState(CWAttr, VarState))
      return;

    Analyzer.WarningsHandler.warnUseInInvalidState(
      FunDecl->getNameAsString(), PInfo.getVar()->getNameAsString(),
      stateToString(VarState), BlameLoc);
  } else {
    ConsumedState TmpState = PInfo.getAsState(StateMap);

    if (TmpState == CS_None || isCallableInState(CWAttr, TmpState))
      return;

    Analyzer.WarningsHandler.warnUseOfTempInInvalidState(
      FunDecl->getNameAsString(), stateToString(TmpState), BlameLoc);
  }
}

// Factors out common behavior for function, method, and operator calls.
// Check parameters and set parameter state if necessary.
// Returns true if the state of ObjArg is set, or false otherwise.
bool ConsumedStmtVisitor::handleCall(const CallExpr *Call, const Expr *ObjArg,
                                     const FunctionDecl *FunD) {
  unsigned Offset = 0;
  if (isa<CXXOperatorCallExpr>(Call) && isa<CXXMethodDecl>(FunD))
    Offset = 1;  // first argument is 'this'

  // check explicit parameters
  for (unsigned Index = Offset; Index < Call->getNumArgs(); ++Index) {
    // Skip variable argument lists.
    if (Index - Offset >= FunD->getNumParams())
      break;

    const ParmVarDecl *Param = FunD->getParamDecl(Index - Offset);
    QualType ParamType = Param->getType();

    InfoEntry Entry = findInfo(Call->getArg(Index));

    if (Entry == PropagationMap.end() || Entry->second.isTest())
      continue;
    PropagationInfo PInfo = Entry->second;

    // Check that the parameter is in the correct state.
    if (ParamTypestateAttr *PTA = Param->getAttr<ParamTypestateAttr>()) {
      ConsumedState ParamState = PInfo.getAsState(StateMap);
      ConsumedState ExpectedState = mapParamTypestateAttrState(PTA);

      if (ParamState != ExpectedState)
        Analyzer.WarningsHandler.warnParamTypestateMismatch(
          Call->getArg(Index)->getExprLoc(),
          stateToString(ExpectedState), stateToString(ParamState));
    }

    if (!(Entry->second.isVar() || Entry->second.isTmp()))
      continue;

    // Adjust state on the caller side.
    if (isRValueRef(ParamType))
      setStateForVarOrTmp(StateMap, PInfo, consumed::CS_Consumed);
    else if (ReturnTypestateAttr *RT = Param->getAttr<ReturnTypestateAttr>())
      setStateForVarOrTmp(StateMap, PInfo, mapReturnTypestateAttrState(RT));
    else if (isPointerOrRef(ParamType) &&
             (!ParamType->getPointeeType().isConstQualified() ||
              isSetOnReadPtrType(ParamType)))
      setStateForVarOrTmp(StateMap, PInfo, consumed::CS_Unknown);
  }

  if (!ObjArg)
    return false;

  // check implicit 'self' parameter, if present
  InfoEntry Entry = findInfo(ObjArg);
  if (Entry != PropagationMap.end()) {
    PropagationInfo PInfo = Entry->second;
    checkCallability(PInfo, FunD, Call->getExprLoc());

    if (SetTypestateAttr *STA = FunD->getAttr<SetTypestateAttr>()) {
      if (PInfo.isVar()) {
        StateMap->setState(PInfo.getVar(), mapSetTypestateAttrState(STA));
        return true;
      }
      else if (PInfo.isTmp()) {
        StateMap->setState(PInfo.getTmp(), mapSetTypestateAttrState(STA));
        return true;
      }
    }
    else if (isTestingFunction(FunD) && PInfo.isVar()) {
      PropagationMap.insert(PairType(Call,
        PropagationInfo(PInfo.getVar(), testsFor(FunD))));
    }
  }
  return false;
}

void ConsumedStmtVisitor::propagateReturnType(const Expr *Call,
                                              const FunctionDecl *Fun) {
  QualType RetType = Fun->getCallResultType();
  if (RetType->isReferenceType())
    RetType = RetType->getPointeeType();

  if (isConsumableType(RetType)) {
    ConsumedState ReturnState;
    if (ReturnTypestateAttr *RTA = Fun->getAttr<ReturnTypestateAttr>())
      ReturnState = mapReturnTypestateAttrState(RTA);
    else
      ReturnState = mapConsumableAttrState(RetType);

    PropagationMap.insert(PairType(Call, PropagationInfo(ReturnState)));
  }
}

void ConsumedStmtVisitor::VisitBinaryOperator(const BinaryOperator *BinOp) {
  switch (BinOp->getOpcode()) {
  case BO_LAnd:
  case BO_LOr : {
    InfoEntry LEntry = findInfo(BinOp->getLHS()),
              REntry = findInfo(BinOp->getRHS());

    VarTestResult LTest, RTest;

    if (LEntry != PropagationMap.end() && LEntry->second.isVarTest()) {
      LTest = LEntry->second.getVarTest();
    } else {
      LTest.Var      = nullptr;
      LTest.TestsFor = CS_None;
    }

    if (REntry != PropagationMap.end() && REntry->second.isVarTest()) {
      RTest = REntry->second.getVarTest();
    } else {
      RTest.Var      = nullptr;
      RTest.TestsFor = CS_None;
    }

    if (!(LTest.Var == nullptr && RTest.Var == nullptr))
      PropagationMap.insert(PairType(BinOp, PropagationInfo(BinOp,
        static_cast<EffectiveOp>(BinOp->getOpcode() == BO_LOr), LTest, RTest)));
    break;
  }

  case BO_PtrMemD:
  case BO_PtrMemI:
    forwardInfo(BinOp->getLHS(), BinOp);
    break;

  default:
    break;
  }
}

void ConsumedStmtVisitor::VisitCallExpr(const CallExpr *Call) {
  const FunctionDecl *FunDecl = Call->getDirectCallee();
  if (!FunDecl)
    return;

  // Special case for the std::move function.
  // TODO: Make this more specific. (Deferred)
  if (Call->isCallToStdMove()) {
    copyInfo(Call->getArg(0), Call, CS_Consumed);
    return;
  }

  handleCall(Call, nullptr, FunDecl);
  propagateReturnType(Call, FunDecl);
}

void ConsumedStmtVisitor::VisitCastExpr(const CastExpr *Cast) {
  forwardInfo(Cast->getSubExpr(), Cast);
}

void ConsumedStmtVisitor::VisitCXXBindTemporaryExpr(
  const CXXBindTemporaryExpr *Temp) {

  InfoEntry Entry = findInfo(Temp->getSubExpr());

  if (Entry != PropagationMap.end() && !Entry->second.isTest()) {
    StateMap->setState(Temp, Entry->second.getAsState(StateMap));
    PropagationMap.insert(PairType(Temp, PropagationInfo(Temp)));
  }
}

void ConsumedStmtVisitor::VisitCXXConstructExpr(const CXXConstructExpr *Call) {
  CXXConstructorDecl *Constructor = Call->getConstructor();

  QualType ThisType = Constructor->getThisType()->getPointeeType();

  if (!isConsumableType(ThisType))
    return;

  // FIXME: What should happen if someone annotates the move constructor?
  if (ReturnTypestateAttr *RTA = Constructor->getAttr<ReturnTypestateAttr>()) {
    // TODO: Adjust state of args appropriately.
    ConsumedState RetState = mapReturnTypestateAttrState(RTA);
    PropagationMap.insert(PairType(Call, PropagationInfo(RetState)));
  } else if (Constructor->isDefaultConstructor()) {
    PropagationMap.insert(PairType(Call,
      PropagationInfo(consumed::CS_Consumed)));
  } else if (Constructor->isMoveConstructor()) {
    copyInfo(Call->getArg(0), Call, CS_Consumed);
  } else if (Constructor->isCopyConstructor()) {
    // Copy state from arg.  If setStateOnRead then set arg to CS_Unknown.
    ConsumedState NS =
      isSetOnReadPtrType(Constructor->getThisType()) ?
      CS_Unknown : CS_None;
    copyInfo(Call->getArg(0), Call, NS);
  } else {
    // TODO: Adjust state of args appropriately.
    ConsumedState RetState = mapConsumableAttrState(ThisType);
    PropagationMap.insert(PairType(Call, PropagationInfo(RetState)));
  }
}

void ConsumedStmtVisitor::VisitCXXMemberCallExpr(
    const CXXMemberCallExpr *Call) {
  CXXMethodDecl* MD = Call->getMethodDecl();
  if (!MD)
    return;

  handleCall(Call, Call->getImplicitObjectArgument(), MD);
  propagateReturnType(Call, MD);
}

void ConsumedStmtVisitor::VisitCXXOperatorCallExpr(
    const CXXOperatorCallExpr *Call) {
  const auto *FunDecl = dyn_cast_or_null<FunctionDecl>(Call->getDirectCallee());
  if (!FunDecl) return;

  if (Call->getOperator() == OO_Equal) {
    ConsumedState CS = getInfo(Call->getArg(1));
    if (!handleCall(Call, Call->getArg(0), FunDecl))
      setInfo(Call->getArg(0), CS);
    return;
  }

  if (const auto *MCall = dyn_cast<CXXMemberCallExpr>(Call))
    handleCall(MCall, MCall->getImplicitObjectArgument(), FunDecl);
  else
    handleCall(Call, Call->getArg(0), FunDecl);

  propagateReturnType(Call, FunDecl);
}

void ConsumedStmtVisitor::VisitDeclRefExpr(const DeclRefExpr *DeclRef) {
  if (const auto *Var = dyn_cast_or_null<VarDecl>(DeclRef->getDecl()))
    if (StateMap->getState(Var) != consumed::CS_None)
      PropagationMap.insert(PairType(DeclRef, PropagationInfo(Var)));
}

void ConsumedStmtVisitor::VisitDeclStmt(const DeclStmt *DeclS) {
  for (const auto *DI : DeclS->decls())
    if (isa<VarDecl>(DI))
      VisitVarDecl(cast<VarDecl>(DI));

  if (DeclS->isSingleDecl())
    if (const auto *Var = dyn_cast_or_null<VarDecl>(DeclS->getSingleDecl()))
      PropagationMap.insert(PairType(DeclS, PropagationInfo(Var)));
}

void ConsumedStmtVisitor::VisitMaterializeTemporaryExpr(
  const MaterializeTemporaryExpr *Temp) {
  forwardInfo(Temp->GetTemporaryExpr(), Temp);
}

void ConsumedStmtVisitor::VisitMemberExpr(const MemberExpr *MExpr) {
  forwardInfo(MExpr->getBase(), MExpr);
}

void ConsumedStmtVisitor::VisitParmVarDecl(const ParmVarDecl *Param) {
  QualType ParamType = Param->getType();
  ConsumedState ParamState = consumed::CS_None;

  if (const ParamTypestateAttr *PTA = Param->getAttr<ParamTypestateAttr>())
    ParamState = mapParamTypestateAttrState(PTA);
  else if (isConsumableType(ParamType))
    ParamState = mapConsumableAttrState(ParamType);
  else if (isRValueRef(ParamType) &&
           isConsumableType(ParamType->getPointeeType()))
    ParamState = mapConsumableAttrState(ParamType->getPointeeType());
  else if (ParamType->isReferenceType() &&
           isConsumableType(ParamType->getPointeeType()))
    ParamState = consumed::CS_Unknown;

  if (ParamState != CS_None)
    StateMap->setState(Param, ParamState);
}

void ConsumedStmtVisitor::VisitReturnStmt(const ReturnStmt *Ret) {
  ConsumedState ExpectedState = Analyzer.getExpectedReturnState();

  if (ExpectedState != CS_None) {
    InfoEntry Entry = findInfo(Ret->getRetValue());

    if (Entry != PropagationMap.end()) {
      ConsumedState RetState = Entry->second.getAsState(StateMap);

      if (RetState != ExpectedState)
        Analyzer.WarningsHandler.warnReturnTypestateMismatch(
          Ret->getReturnLoc(), stateToString(ExpectedState),
          stateToString(RetState));
    }
  }

  StateMap->checkParamsForReturnTypestate(Ret->getBeginLoc(),
                                          Analyzer.WarningsHandler);
}

void ConsumedStmtVisitor::VisitUnaryOperator(const UnaryOperator *UOp) {
  InfoEntry Entry = findInfo(UOp->getSubExpr());
  if (Entry == PropagationMap.end()) return;

  switch (UOp->getOpcode()) {
  case UO_AddrOf:
    PropagationMap.insert(PairType(UOp, Entry->second));
    break;

  case UO_LNot:
    if (Entry->second.isTest())
      PropagationMap.insert(PairType(UOp, Entry->second.invertTest()));
    break;

  default:
    break;
  }
}

// TODO: See if I need to check for reference types here.
void ConsumedStmtVisitor::VisitVarDecl(const VarDecl *Var) {
  if (isConsumableType(Var->getType())) {
    if (Var->hasInit()) {
      MapType::iterator VIT = findInfo(Var->getInit()->IgnoreImplicit());
      if (VIT != PropagationMap.end()) {
        PropagationInfo PInfo = VIT->second;
        ConsumedState St = PInfo.getAsState(StateMap);

        if (St != consumed::CS_None) {
          StateMap->setState(Var, St);
          return;
        }
      }
    }
    // Otherwise
    StateMap->setState(Var, consumed::CS_Unknown);
  }
}

static void splitVarStateForIf(const IfStmt *IfNode, const VarTestResult &Test,
                               ConsumedStateMap *ThenStates,
                               ConsumedStateMap *ElseStates) {
  ConsumedState VarState = ThenStates->getState(Test.Var);

  if (VarState == CS_Unknown) {
    ThenStates->setState(Test.Var, Test.TestsFor);
    ElseStates->setState(Test.Var, invertConsumedUnconsumed(Test.TestsFor));
  } else if (VarState == invertConsumedUnconsumed(Test.TestsFor)) {
    ThenStates->markUnreachable();
  } else if (VarState == Test.TestsFor) {
    ElseStates->markUnreachable();
  }
}

static void splitVarStateForIfBinOp(const PropagationInfo &PInfo,
                                    ConsumedStateMap *ThenStates,
                                    ConsumedStateMap *ElseStates) {
  const VarTestResult &LTest = PInfo.getLTest(),
                      &RTest = PInfo.getRTest();

  ConsumedState LState = LTest.Var ? ThenStates->getState(LTest.Var) : CS_None,
                RState = RTest.Var ? ThenStates->getState(RTest.Var) : CS_None;

  if (LTest.Var) {
    if (PInfo.testEffectiveOp() == EO_And) {
      if (LState == CS_Unknown) {
        ThenStates->setState(LTest.Var, LTest.TestsFor);
      } else if (LState == invertConsumedUnconsumed(LTest.TestsFor)) {
        ThenStates->markUnreachable();
      } else if (LState == LTest.TestsFor && isKnownState(RState)) {
        if (RState == RTest.TestsFor)
          ElseStates->markUnreachable();
        else
          ThenStates->markUnreachable();
      }
    } else {
      if (LState == CS_Unknown) {
        ElseStates->setState(LTest.Var,
                             invertConsumedUnconsumed(LTest.TestsFor));
      } else if (LState == LTest.TestsFor) {
        ElseStates->markUnreachable();
      } else if (LState == invertConsumedUnconsumed(LTest.TestsFor) &&
                 isKnownState(RState)) {
        if (RState == RTest.TestsFor)
          ElseStates->markUnreachable();
        else
          ThenStates->markUnreachable();
      }
    }
  }

  if (RTest.Var) {
    if (PInfo.testEffectiveOp() == EO_And) {
      if (RState == CS_Unknown)
        ThenStates->setState(RTest.Var, RTest.TestsFor);
      else if (RState == invertConsumedUnconsumed(RTest.TestsFor))
        ThenStates->markUnreachable();
    } else {
      if (RState == CS_Unknown)
        ElseStates->setState(RTest.Var,
                             invertConsumedUnconsumed(RTest.TestsFor));
      else if (RState == RTest.TestsFor)
        ElseStates->markUnreachable();
    }
  }
}

bool ConsumedBlockInfo::allBackEdgesVisited(const CFGBlock *CurrBlock,
                                            const CFGBlock *TargetBlock) {
  assert(CurrBlock && "Block pointer must not be NULL");
  assert(TargetBlock && "TargetBlock pointer must not be NULL");

  unsigned int CurrBlockOrder = VisitOrder[CurrBlock->getBlockID()];
  for (CFGBlock::const_pred_iterator PI = TargetBlock->pred_begin(),
       PE = TargetBlock->pred_end(); PI != PE; ++PI) {
    if (*PI && CurrBlockOrder < VisitOrder[(*PI)->getBlockID()] )
      return false;
  }
  return true;
}

void ConsumedBlockInfo::addInfo(
    const CFGBlock *Block, ConsumedStateMap *StateMap,
    std::unique_ptr<ConsumedStateMap> &OwnedStateMap) {
  assert(Block && "Block pointer must not be NULL");

  auto &Entry = StateMapsArray[Block->getBlockID()];

  if (Entry) {
    Entry->intersect(*StateMap);
  } else if (OwnedStateMap)
    Entry = std::move(OwnedStateMap);
  else
    Entry = llvm::make_unique<ConsumedStateMap>(*StateMap);
}

void ConsumedBlockInfo::addInfo(const CFGBlock *Block,
                                std::unique_ptr<ConsumedStateMap> StateMap) {
  assert(Block && "Block pointer must not be NULL");

  auto &Entry = StateMapsArray[Block->getBlockID()];

  if (Entry) {
    Entry->intersect(*StateMap);
  } else {
    Entry = std::move(StateMap);
  }
}

ConsumedStateMap* ConsumedBlockInfo::borrowInfo(const CFGBlock *Block) {
  assert(Block && "Block pointer must not be NULL");
  assert(StateMapsArray[Block->getBlockID()] && "Block has no block info");

  return StateMapsArray[Block->getBlockID()].get();
}

void ConsumedBlockInfo::discardInfo(const CFGBlock *Block) {
  StateMapsArray[Block->getBlockID()] = nullptr;
}

std::unique_ptr<ConsumedStateMap>
ConsumedBlockInfo::getInfo(const CFGBlock *Block) {
  assert(Block && "Block pointer must not be NULL");

  auto &Entry = StateMapsArray[Block->getBlockID()];
  return isBackEdgeTarget(Block) ? llvm::make_unique<ConsumedStateMap>(*Entry)
                                 : std::move(Entry);
}

bool ConsumedBlockInfo::isBackEdge(const CFGBlock *From, const CFGBlock *To) {
  assert(From && "From block must not be NULL");
  assert(To   && "From block must not be NULL");

  return VisitOrder[From->getBlockID()] > VisitOrder[To->getBlockID()];
}

bool ConsumedBlockInfo::isBackEdgeTarget(const CFGBlock *Block) {
  assert(Block && "Block pointer must not be NULL");

  // Anything with less than two predecessors can't be the target of a back
  // edge.
  if (Block->pred_size() < 2)
    return false;

  unsigned int BlockVisitOrder = VisitOrder[Block->getBlockID()];
  for (CFGBlock::const_pred_iterator PI = Block->pred_begin(),
       PE = Block->pred_end(); PI != PE; ++PI) {
    if (*PI && BlockVisitOrder < VisitOrder[(*PI)->getBlockID()])
      return true;
  }
  return false;
}

void ConsumedStateMap::checkParamsForReturnTypestate(SourceLocation BlameLoc,
  ConsumedWarningsHandlerBase &WarningsHandler) const {

  for (const auto &DM : VarMap) {
    if (isa<ParmVarDecl>(DM.first)) {
      const auto *Param = cast<ParmVarDecl>(DM.first);
      const ReturnTypestateAttr *RTA = Param->getAttr<ReturnTypestateAttr>();

      if (!RTA)
        continue;

      ConsumedState ExpectedState = mapReturnTypestateAttrState(RTA);
      if (DM.second != ExpectedState)
        WarningsHandler.warnParamReturnTypestateMismatch(BlameLoc,
          Param->getNameAsString(), stateToString(ExpectedState),
          stateToString(DM.second));
    }
  }
}

void ConsumedStateMap::clearTemporaries() {
  TmpMap.clear();
}

ConsumedState ConsumedStateMap::getState(const VarDecl *Var) const {
  VarMapType::const_iterator Entry = VarMap.find(Var);

  if (Entry != VarMap.end())
    return Entry->second;

  return CS_None;
}

ConsumedState
ConsumedStateMap::getState(const CXXBindTemporaryExpr *Tmp) const {
  TmpMapType::const_iterator Entry = TmpMap.find(Tmp);

  if (Entry != TmpMap.end())
    return Entry->second;

  return CS_None;
}

void ConsumedStateMap::intersect(const ConsumedStateMap &Other) {
  ConsumedState LocalState;

  if (this->From && this->From == Other.From && !Other.Reachable) {
    this->markUnreachable();
    return;
  }

  for (const auto &DM : Other.VarMap) {
    LocalState = this->getState(DM.first);

    if (LocalState == CS_None)
      continue;

    if (LocalState != DM.second)
     VarMap[DM.first] = CS_Unknown;
  }
}

void ConsumedStateMap::intersectAtLoopHead(const CFGBlock *LoopHead,
  const CFGBlock *LoopBack, const ConsumedStateMap *LoopBackStates,
  ConsumedWarningsHandlerBase &WarningsHandler) {

  ConsumedState LocalState;
  SourceLocation BlameLoc = getLastStmtLoc(LoopBack);

  for (const auto &DM : LoopBackStates->VarMap) {
    LocalState = this->getState(DM.first);

    if (LocalState == CS_None)
      continue;

    if (LocalState != DM.second) {
      VarMap[DM.first] = CS_Unknown;
      WarningsHandler.warnLoopStateMismatch(BlameLoc,
                                            DM.first->getNameAsString());
    }
  }
}

void ConsumedStateMap::markUnreachable() {
  this->Reachable = false;
  VarMap.clear();
  TmpMap.clear();
}

void ConsumedStateMap::setState(const VarDecl *Var, ConsumedState State) {
  VarMap[Var] = State;
}

void ConsumedStateMap::setState(const CXXBindTemporaryExpr *Tmp,
                                ConsumedState State) {
  TmpMap[Tmp] = State;
}

void ConsumedStateMap::remove(const CXXBindTemporaryExpr *Tmp) {
  TmpMap.erase(Tmp);
}

bool ConsumedStateMap::operator!=(const ConsumedStateMap *Other) const {
  for (const auto &DM : Other->VarMap)
    if (this->getState(DM.first) != DM.second)
      return true;
  return false;
}

void ConsumedAnalyzer::determineExpectedReturnState(AnalysisDeclContext &AC,
                                                    const FunctionDecl *D) {
  QualType ReturnType;
  if (const auto *Constructor = dyn_cast<CXXConstructorDecl>(D)) {
    ReturnType = Constructor->getThisType()->getPointeeType();
  } else
    ReturnType = D->getCallResultType();

  if (const ReturnTypestateAttr *RTSAttr = D->getAttr<ReturnTypestateAttr>()) {
    const CXXRecordDecl *RD = ReturnType->getAsCXXRecordDecl();
    if (!RD || !RD->hasAttr<ConsumableAttr>()) {
      // FIXME: This should be removed when template instantiation propagates
      //        attributes at template specialization definition, not
      //        declaration. When it is removed the test needs to be enabled
      //        in SemaDeclAttr.cpp.
      WarningsHandler.warnReturnTypestateForUnconsumableType(
          RTSAttr->getLocation(), ReturnType.getAsString());
      ExpectedReturnState = CS_None;
    } else
      ExpectedReturnState = mapReturnTypestateAttrState(RTSAttr);
  } else if (isConsumableType(ReturnType)) {
    if (isAutoCastType(ReturnType))   // We can auto-cast the state to the
      ExpectedReturnState = CS_None;  // expected state.
    else
      ExpectedReturnState = mapConsumableAttrState(ReturnType);
  }
  else
    ExpectedReturnState = CS_None;
}

bool ConsumedAnalyzer::splitState(const CFGBlock *CurrBlock,
                                  const ConsumedStmtVisitor &Visitor) {
  std::unique_ptr<ConsumedStateMap> FalseStates(
      new ConsumedStateMap(*CurrStates));
  PropagationInfo PInfo;

  if (const auto *IfNode =
          dyn_cast_or_null<IfStmt>(CurrBlock->getTerminator().getStmt())) {
    const Expr *Cond = IfNode->getCond();

    PInfo = Visitor.getInfo(Cond);
    if (!PInfo.isValid() && isa<BinaryOperator>(Cond))
      PInfo = Visitor.getInfo(cast<BinaryOperator>(Cond)->getRHS());

    if (PInfo.isVarTest()) {
      CurrStates->setSource(Cond);
      FalseStates->setSource(Cond);
      splitVarStateForIf(IfNode, PInfo.getVarTest(), CurrStates.get(),
                         FalseStates.get());
    } else if (PInfo.isBinTest()) {
      CurrStates->setSource(PInfo.testSourceNode());
      FalseStates->setSource(PInfo.testSourceNode());
      splitVarStateForIfBinOp(PInfo, CurrStates.get(), FalseStates.get());
    } else {
      return false;
    }
  } else if (const auto *BinOp =
       dyn_cast_or_null<BinaryOperator>(CurrBlock->getTerminator().getStmt())) {
    PInfo = Visitor.getInfo(BinOp->getLHS());
    if (!PInfo.isVarTest()) {
      if ((BinOp = dyn_cast_or_null<BinaryOperator>(BinOp->getLHS()))) {
        PInfo = Visitor.getInfo(BinOp->getRHS());

        if (!PInfo.isVarTest())
          return false;
      } else {
        return false;
      }
    }

    CurrStates->setSource(BinOp);
    FalseStates->setSource(BinOp);

    const VarTestResult &Test = PInfo.getVarTest();
    ConsumedState VarState = CurrStates->getState(Test.Var);

    if (BinOp->getOpcode() == BO_LAnd) {
      if (VarState == CS_Unknown)
        CurrStates->setState(Test.Var, Test.TestsFor);
      else if (VarState == invertConsumedUnconsumed(Test.TestsFor))
        CurrStates->markUnreachable();

    } else if (BinOp->getOpcode() == BO_LOr) {
      if (VarState == CS_Unknown)
        FalseStates->setState(Test.Var,
                              invertConsumedUnconsumed(Test.TestsFor));
      else if (VarState == Test.TestsFor)
        FalseStates->markUnreachable();
    }
  } else {
    return false;
  }

  CFGBlock::const_succ_iterator SI = CurrBlock->succ_begin();

  if (*SI)
    BlockInfo.addInfo(*SI, std::move(CurrStates));
  else
    CurrStates = nullptr;

  if (*++SI)
    BlockInfo.addInfo(*SI, std::move(FalseStates));

  return true;
}

void ConsumedAnalyzer::run(AnalysisDeclContext &AC) {
  const auto *D = dyn_cast_or_null<FunctionDecl>(AC.getDecl());
  if (!D)
    return;

  CFG *CFGraph = AC.getCFG();
  if (!CFGraph)
    return;

  determineExpectedReturnState(AC, D);

  PostOrderCFGView *SortedGraph = AC.getAnalysis<PostOrderCFGView>();
  // AC.getCFG()->viewCFG(LangOptions());

  BlockInfo = ConsumedBlockInfo(CFGraph->getNumBlockIDs(), SortedGraph);

  CurrStates = llvm::make_unique<ConsumedStateMap>();
  ConsumedStmtVisitor Visitor(*this, CurrStates.get());

  // Add all trackable parameters to the state map.
  for (const auto *PI : D->parameters())
    Visitor.VisitParmVarDecl(PI);

  // Visit all of the function's basic blocks.
  for (const auto *CurrBlock : *SortedGraph) {
    if (!CurrStates)
      CurrStates = BlockInfo.getInfo(CurrBlock);

    if (!CurrStates) {
      continue;
    } else if (!CurrStates->isReachable()) {
      CurrStates = nullptr;
      continue;
    }

    Visitor.reset(CurrStates.get());

    // Visit all of the basic block's statements.
    for (const auto &B : *CurrBlock) {
      switch (B.getKind()) {
      case CFGElement::Statement:
        Visitor.Visit(B.castAs<CFGStmt>().getStmt());
        break;

      case CFGElement::TemporaryDtor: {
        const CFGTemporaryDtor &DTor = B.castAs<CFGTemporaryDtor>();
        const CXXBindTemporaryExpr *BTE = DTor.getBindTemporaryExpr();

        Visitor.checkCallability(PropagationInfo(BTE),
                                 DTor.getDestructorDecl(AC.getASTContext()),
                                 BTE->getExprLoc());
        CurrStates->remove(BTE);
        break;
      }

      case CFGElement::AutomaticObjectDtor: {
        const CFGAutomaticObjDtor &DTor = B.castAs<CFGAutomaticObjDtor>();
        SourceLocation Loc = DTor.getTriggerStmt()->getEndLoc();
        const VarDecl *Var = DTor.getVarDecl();

        Visitor.checkCallability(PropagationInfo(Var),
                                 DTor.getDestructorDecl(AC.getASTContext()),
                                 Loc);
        break;
      }

      default:
        break;
      }
    }

    // TODO: Handle other forms of branching with precision, including while-
    //       and for-loops. (Deferred)
    if (!splitState(CurrBlock, Visitor)) {
      CurrStates->setSource(nullptr);

      if (CurrBlock->succ_size() > 1 ||
          (CurrBlock->succ_size() == 1 &&
           (*CurrBlock->succ_begin())->pred_size() > 1)) {

        auto *RawState = CurrStates.get();

        for (CFGBlock::const_succ_iterator SI = CurrBlock->succ_begin(),
             SE = CurrBlock->succ_end(); SI != SE; ++SI) {
          if (*SI == nullptr) continue;

          if (BlockInfo.isBackEdge(CurrBlock, *SI)) {
            BlockInfo.borrowInfo(*SI)->intersectAtLoopHead(
                *SI, CurrBlock, RawState, WarningsHandler);

            if (BlockInfo.allBackEdgesVisited(CurrBlock, *SI))
              BlockInfo.discardInfo(*SI);
          } else {
            BlockInfo.addInfo(*SI, RawState, CurrStates);
          }
        }

        CurrStates = nullptr;
      }
    }

    if (CurrBlock == &AC.getCFG()->getExit() &&
        D->getCallResultType()->isVoidType())
      CurrStates->checkParamsForReturnTypestate(D->getLocation(),
                                                WarningsHandler);
  } // End of block iterator.

  // Delete the last existing state map.
  CurrStates = nullptr;

  WarningsHandler.emitDiagnostics();
}
