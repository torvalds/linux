//=======- VirtualCallChecker.cpp --------------------------------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines a checker that checks virtual function calls during
//  construction or destruction of C++ objects.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/DeclCXX.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SValBuilder.h"

using namespace clang;
using namespace ento;

namespace {
enum class ObjectState : bool { CtorCalled, DtorCalled };
} // end namespace
  // FIXME: Ascending over StackFrameContext maybe another method.

namespace llvm {
template <> struct FoldingSetTrait<ObjectState> {
  static inline void Profile(ObjectState X, FoldingSetNodeID &ID) {
    ID.AddInteger(static_cast<int>(X));
  }
};
} // end namespace llvm

namespace {
class VirtualCallChecker
    : public Checker<check::BeginFunction, check::EndFunction, check::PreCall> {
  mutable std::unique_ptr<BugType> BT;

public:
  // The flag to determine if pure virtual functions should be issued only.
  DefaultBool IsPureOnly;

  void checkBeginFunction(CheckerContext &C) const;
  void checkEndFunction(const ReturnStmt *RS, CheckerContext &C) const;
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;

private:
  void registerCtorDtorCallInState(bool IsBeginFunction,
                                   CheckerContext &C) const;
  void reportBug(StringRef Msg, bool PureError, const MemRegion *Reg,
                 CheckerContext &C) const;

  class VirtualBugVisitor : public BugReporterVisitor {
  private:
    const MemRegion *ObjectRegion;
    bool Found;

  public:
    VirtualBugVisitor(const MemRegion *R) : ObjectRegion(R), Found(false) {}

    void Profile(llvm::FoldingSetNodeID &ID) const override {
      static int X = 0;
      ID.AddPointer(&X);
      ID.AddPointer(ObjectRegion);
    }

    std::shared_ptr<PathDiagnosticPiece> VisitNode(const ExplodedNode *N,
                                                   BugReporterContext &BRC,
                                                   BugReport &BR) override;
  };
};
} // end namespace

// GDM (generic data map) to the memregion of this for the ctor and dtor.
REGISTER_MAP_WITH_PROGRAMSTATE(CtorDtorMap, const MemRegion *, ObjectState)

std::shared_ptr<PathDiagnosticPiece>
VirtualCallChecker::VirtualBugVisitor::VisitNode(const ExplodedNode *N,
                                                 BugReporterContext &BRC,
                                                 BugReport &) {
  // We need the last ctor/dtor which call the virtual function.
  // The visitor walks the ExplodedGraph backwards.
  if (Found)
    return nullptr;

  ProgramStateRef State = N->getState();
  const LocationContext *LCtx = N->getLocationContext();
  const CXXConstructorDecl *CD =
      dyn_cast_or_null<CXXConstructorDecl>(LCtx->getDecl());
  const CXXDestructorDecl *DD =
      dyn_cast_or_null<CXXDestructorDecl>(LCtx->getDecl());

  if (!CD && !DD)
    return nullptr;

  ProgramStateManager &PSM = State->getStateManager();
  auto &SVB = PSM.getSValBuilder();
  const auto *MD = dyn_cast<CXXMethodDecl>(LCtx->getDecl());
  if (!MD)
    return nullptr;
  auto ThiSVal =
      State->getSVal(SVB.getCXXThis(MD, LCtx->getStackFrame()));
  const MemRegion *Reg = ThiSVal.castAs<loc::MemRegionVal>().getRegion();
  if (!Reg)
    return nullptr;
  if (Reg != ObjectRegion)
    return nullptr;

  const Stmt *S = PathDiagnosticLocation::getStmt(N);
  if (!S)
    return nullptr;
  Found = true;

  std::string InfoText;
  if (CD)
    InfoText = "This constructor of an object of type '" +
               CD->getNameAsString() +
               "' has not returned when the virtual method was called";
  else
    InfoText = "This destructor of an object of type '" +
               DD->getNameAsString() +
               "' has not returned when the virtual method was called";

  // Generate the extra diagnostic.
  PathDiagnosticLocation Pos(S, BRC.getSourceManager(),
                             N->getLocationContext());
  return std::make_shared<PathDiagnosticEventPiece>(Pos, InfoText, true);
}

// The function to check if a callexpr is a virtual function.
static bool isVirtualCall(const CallExpr *CE) {
  bool CallIsNonVirtual = false;

  if (const MemberExpr *CME = dyn_cast<MemberExpr>(CE->getCallee())) {
    // The member access is fully qualified (i.e., X::F).
    // Treat this as a non-virtual call and do not warn.
    if (CME->getQualifier())
      CallIsNonVirtual = true;

    if (const Expr *Base = CME->getBase()) {
      // The most derived class is marked final.
      if (Base->getBestDynamicClassType()->hasAttr<FinalAttr>())
        CallIsNonVirtual = true;
    }
  }

  const CXXMethodDecl *MD =
      dyn_cast_or_null<CXXMethodDecl>(CE->getDirectCallee());
  if (MD && MD->isVirtual() && !CallIsNonVirtual && !MD->hasAttr<FinalAttr>() &&
      !MD->getParent()->hasAttr<FinalAttr>())
    return true;
  return false;
}

// The BeginFunction callback when enter a constructor or a destructor.
void VirtualCallChecker::checkBeginFunction(CheckerContext &C) const {
  registerCtorDtorCallInState(true, C);
}

// The EndFunction callback when leave a constructor or a destructor.
void VirtualCallChecker::checkEndFunction(const ReturnStmt *RS,
                                          CheckerContext &C) const {
  registerCtorDtorCallInState(false, C);
}

void VirtualCallChecker::checkPreCall(const CallEvent &Call,
                                      CheckerContext &C) const {
  const auto MC = dyn_cast<CXXMemberCall>(&Call);
  if (!MC)
    return;

  const CXXMethodDecl *MD = dyn_cast_or_null<CXXMethodDecl>(Call.getDecl());
  if (!MD)
    return;
  ProgramStateRef State = C.getState();
  const CallExpr *CE = dyn_cast_or_null<CallExpr>(Call.getOriginExpr());

  if (IsPureOnly && !MD->isPure())
    return;
  if (!isVirtualCall(CE))
    return;

  const MemRegion *Reg = MC->getCXXThisVal().getAsRegion();
  const ObjectState *ObState = State->get<CtorDtorMap>(Reg);
  if (!ObState)
    return;
  // Check if a virtual method is called.
  // The GDM of constructor and destructor should be true.
  if (*ObState == ObjectState::CtorCalled) {
    if (IsPureOnly && MD->isPure())
      reportBug("Call to pure virtual function during construction", true, Reg,
                C);
    else if (!MD->isPure())
      reportBug("Call to virtual function during construction", false, Reg, C);
    else
      reportBug("Call to pure virtual function during construction", false, Reg,
                C);
  }

  if (*ObState == ObjectState::DtorCalled) {
    if (IsPureOnly && MD->isPure())
      reportBug("Call to pure virtual function during destruction", true, Reg,
                C);
    else if (!MD->isPure())
      reportBug("Call to virtual function during destruction", false, Reg, C);
    else
      reportBug("Call to pure virtual function during construction", false, Reg,
                C);
  }
}

void VirtualCallChecker::registerCtorDtorCallInState(bool IsBeginFunction,
                                                     CheckerContext &C) const {
  const auto *LCtx = C.getLocationContext();
  const auto *MD = dyn_cast_or_null<CXXMethodDecl>(LCtx->getDecl());
  if (!MD)
    return;

  ProgramStateRef State = C.getState();
  auto &SVB = C.getSValBuilder();

  // Enter a constructor, set the corresponding memregion be true.
  if (isa<CXXConstructorDecl>(MD)) {
    auto ThiSVal =
        State->getSVal(SVB.getCXXThis(MD, LCtx->getStackFrame()));
    const MemRegion *Reg = ThiSVal.getAsRegion();
    if (IsBeginFunction)
      State = State->set<CtorDtorMap>(Reg, ObjectState::CtorCalled);
    else
      State = State->remove<CtorDtorMap>(Reg);

    C.addTransition(State);
    return;
  }

  // Enter a Destructor, set the corresponding memregion be true.
  if (isa<CXXDestructorDecl>(MD)) {
    auto ThiSVal =
        State->getSVal(SVB.getCXXThis(MD, LCtx->getStackFrame()));
    const MemRegion *Reg = ThiSVal.getAsRegion();
    if (IsBeginFunction)
      State = State->set<CtorDtorMap>(Reg, ObjectState::DtorCalled);
    else
      State = State->remove<CtorDtorMap>(Reg);

    C.addTransition(State);
    return;
  }
}

void VirtualCallChecker::reportBug(StringRef Msg, bool IsSink,
                                   const MemRegion *Reg,
                                   CheckerContext &C) const {
  ExplodedNode *N;
  if (IsSink)
    N = C.generateErrorNode();
  else
    N = C.generateNonFatalErrorNode();

  if (!N)
    return;
  if (!BT)
    BT.reset(new BugType(
        this, "Call to virtual function during construction or destruction",
        "C++ Object Lifecycle"));

  auto Reporter = llvm::make_unique<BugReport>(*BT, Msg, N);
  Reporter->addVisitor(llvm::make_unique<VirtualBugVisitor>(Reg));
  C.emitReport(std::move(Reporter));
}

void ento::registerVirtualCallChecker(CheckerManager &mgr) {
  VirtualCallChecker *checker = mgr.registerChecker<VirtualCallChecker>();

  checker->IsPureOnly =
      mgr.getAnalyzerOptions().getCheckerBooleanOption("PureOnly", false,
                                                       checker);
}
