//===- ObjCSuperDeallocChecker.cpp - Check correct use of [super dealloc] -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This defines ObjCSuperDeallocChecker, a builtin check that warns when
// self is used after a call to [super dealloc] in MRR mode.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"

using namespace clang;
using namespace ento;

namespace {
class ObjCSuperDeallocChecker
    : public Checker<check::PostObjCMessage, check::PreObjCMessage,
                     check::PreCall, check::Location> {

  mutable IdentifierInfo *IIdealloc, *IINSObject;
  mutable Selector SELdealloc;

  std::unique_ptr<BugType> DoubleSuperDeallocBugType;

  void initIdentifierInfoAndSelectors(ASTContext &Ctx) const;

  bool isSuperDeallocMessage(const ObjCMethodCall &M) const;

public:
  ObjCSuperDeallocChecker();
  void checkPostObjCMessage(const ObjCMethodCall &M, CheckerContext &C) const;
  void checkPreObjCMessage(const ObjCMethodCall &M, CheckerContext &C) const;

  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;

  void checkLocation(SVal l, bool isLoad, const Stmt *S,
                     CheckerContext &C) const;

private:

  void diagnoseCallArguments(const CallEvent &CE, CheckerContext &C) const;

  void reportUseAfterDealloc(SymbolRef Sym, StringRef Desc, const Stmt *S,
                             CheckerContext &C) const;
};

} // End anonymous namespace.

// Remember whether [super dealloc] has previously been called on the
// SymbolRef for the receiver.
REGISTER_SET_WITH_PROGRAMSTATE(CalledSuperDealloc, SymbolRef)

namespace {
class SuperDeallocBRVisitor final : public BugReporterVisitor {
  SymbolRef ReceiverSymbol;
  bool Satisfied;

public:
  SuperDeallocBRVisitor(SymbolRef ReceiverSymbol)
      : ReceiverSymbol(ReceiverSymbol),
        Satisfied(false) {}

  std::shared_ptr<PathDiagnosticPiece> VisitNode(const ExplodedNode *Succ,
                                                 BugReporterContext &BRC,
                                                 BugReport &BR) override;

  void Profile(llvm::FoldingSetNodeID &ID) const override {
    ID.Add(ReceiverSymbol);
  }
};
} // End anonymous namespace.

void ObjCSuperDeallocChecker::checkPreObjCMessage(const ObjCMethodCall &M,
                                                  CheckerContext &C) const {

  ProgramStateRef State = C.getState();
  SymbolRef ReceiverSymbol = M.getReceiverSVal().getAsSymbol();
  if (!ReceiverSymbol) {
    diagnoseCallArguments(M, C);
    return;
  }

  bool AlreadyCalled = State->contains<CalledSuperDealloc>(ReceiverSymbol);
  if (!AlreadyCalled)
    return;

  StringRef Desc;

  if (isSuperDeallocMessage(M)) {
    Desc = "[super dealloc] should not be called multiple times";
  } else {
    Desc = StringRef();
  }

  reportUseAfterDealloc(ReceiverSymbol, Desc, M.getOriginExpr(), C);
}

void ObjCSuperDeallocChecker::checkPreCall(const CallEvent &Call,
                                           CheckerContext &C) const {
  diagnoseCallArguments(Call, C);
}

void ObjCSuperDeallocChecker::checkPostObjCMessage(const ObjCMethodCall &M,
                                                   CheckerContext &C) const {
  // Check for [super dealloc] method call.
  if (!isSuperDeallocMessage(M))
    return;

  ProgramStateRef State = C.getState();
  SymbolRef ReceiverSymbol = M.getSelfSVal().getAsSymbol();
  assert(ReceiverSymbol && "No receiver symbol at call to [super dealloc]?");

  // We add this transition in checkPostObjCMessage to avoid warning when
  // we inline a call to [super dealloc] where the inlined call itself
  // calls [super dealloc].
  State = State->add<CalledSuperDealloc>(ReceiverSymbol);
  C.addTransition(State);
}

void ObjCSuperDeallocChecker::checkLocation(SVal L, bool IsLoad, const Stmt *S,
                                  CheckerContext &C) const {
  SymbolRef BaseSym = L.getLocSymbolInBase();
  if (!BaseSym)
    return;

  ProgramStateRef State = C.getState();

  if (!State->contains<CalledSuperDealloc>(BaseSym))
    return;

  const MemRegion *R = L.getAsRegion();
  if (!R)
    return;

  // Climb the super regions to find the base symbol while recording
  // the second-to-last region for error reporting.
  const MemRegion *PriorSubRegion = nullptr;
  while (const SubRegion *SR = dyn_cast<SubRegion>(R)) {
    if (const SymbolicRegion *SymR = dyn_cast<SymbolicRegion>(SR)) {
      BaseSym = SymR->getSymbol();
      break;
    } else {
      R = SR->getSuperRegion();
      PriorSubRegion = SR;
    }
  }

  StringRef Desc = StringRef();
  auto *IvarRegion = dyn_cast_or_null<ObjCIvarRegion>(PriorSubRegion);

  std::string Buf;
  llvm::raw_string_ostream OS(Buf);
  if (IvarRegion) {
    OS << "Use of instance variable '" << *IvarRegion->getDecl() <<
          "' after 'self' has been deallocated";
    Desc = OS.str();
  }

  reportUseAfterDealloc(BaseSym, Desc, S, C);
}

/// Report a use-after-dealloc on Sym. If not empty,
/// Desc will be used to describe the error; otherwise,
/// a default warning will be used.
void ObjCSuperDeallocChecker::reportUseAfterDealloc(SymbolRef Sym,
                                                    StringRef Desc,
                                                    const Stmt *S,
                                                    CheckerContext &C) const {
  // We have a use of self after free.
  // This likely causes a crash, so stop exploring the
  // path by generating a sink.
  ExplodedNode *ErrNode = C.generateErrorNode();
  // If we've already reached this node on another path, return.
  if (!ErrNode)
    return;

  if (Desc.empty())
    Desc = "Use of 'self' after it has been deallocated";

  // Generate the report.
  std::unique_ptr<BugReport> BR(
      new BugReport(*DoubleSuperDeallocBugType, Desc, ErrNode));
  BR->addRange(S->getSourceRange());
  BR->addVisitor(llvm::make_unique<SuperDeallocBRVisitor>(Sym));
  C.emitReport(std::move(BR));
}

/// Diagnose if any of the arguments to CE have already been
/// dealloc'd.
void ObjCSuperDeallocChecker::diagnoseCallArguments(const CallEvent &CE,
                                                    CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  unsigned ArgCount = CE.getNumArgs();
  for (unsigned I = 0; I < ArgCount; I++) {
    SymbolRef Sym = CE.getArgSVal(I).getAsSymbol();
    if (!Sym)
      continue;

    if (State->contains<CalledSuperDealloc>(Sym)) {
      reportUseAfterDealloc(Sym, StringRef(), CE.getArgExpr(I), C);
      return;
    }
  }
}

ObjCSuperDeallocChecker::ObjCSuperDeallocChecker()
    : IIdealloc(nullptr), IINSObject(nullptr) {

  DoubleSuperDeallocBugType.reset(
      new BugType(this, "[super dealloc] should not be called more than once",
                  categories::CoreFoundationObjectiveC));
}

void
ObjCSuperDeallocChecker::initIdentifierInfoAndSelectors(ASTContext &Ctx) const {
  if (IIdealloc)
    return;

  IIdealloc = &Ctx.Idents.get("dealloc");
  IINSObject = &Ctx.Idents.get("NSObject");

  SELdealloc = Ctx.Selectors.getSelector(0, &IIdealloc);
}

bool
ObjCSuperDeallocChecker::isSuperDeallocMessage(const ObjCMethodCall &M) const {
  if (M.getOriginExpr()->getReceiverKind() != ObjCMessageExpr::SuperInstance)
    return false;

  ASTContext &Ctx = M.getState()->getStateManager().getContext();
  initIdentifierInfoAndSelectors(Ctx);

  return M.getSelector() == SELdealloc;
}

std::shared_ptr<PathDiagnosticPiece>
SuperDeallocBRVisitor::VisitNode(const ExplodedNode *Succ,
                                 BugReporterContext &BRC, BugReport &) {
  if (Satisfied)
    return nullptr;

  ProgramStateRef State = Succ->getState();

  bool CalledNow =
      Succ->getState()->contains<CalledSuperDealloc>(ReceiverSymbol);
  bool CalledBefore =
      Succ->getFirstPred()->getState()->contains<CalledSuperDealloc>(
          ReceiverSymbol);

  // Is Succ the node on which the analyzer noted that [super dealloc] was
  // called on ReceiverSymbol?
  if (CalledNow && !CalledBefore) {
    Satisfied = true;

    ProgramPoint P = Succ->getLocation();
    PathDiagnosticLocation L =
        PathDiagnosticLocation::create(P, BRC.getSourceManager());

    if (!L.isValid() || !L.asLocation().isValid())
      return nullptr;

    return std::make_shared<PathDiagnosticEventPiece>(
        L, "[super dealloc] called here");
  }

  return nullptr;
}

//===----------------------------------------------------------------------===//
// Checker Registration.
//===----------------------------------------------------------------------===//

void ento::registerObjCSuperDeallocChecker(CheckerManager &Mgr) {
  const LangOptions &LangOpts = Mgr.getLangOpts();
  if (LangOpts.getGC() == LangOptions::GCOnly || LangOpts.ObjCAutoRefCount)
    return;
  Mgr.registerChecker<ObjCSuperDeallocChecker>();
}
