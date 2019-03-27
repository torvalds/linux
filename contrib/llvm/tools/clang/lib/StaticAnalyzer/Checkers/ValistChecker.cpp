//== ValistChecker.cpp - stdarg.h macro usage checker -----------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This defines checkers which detect usage of uninitialized va_list values
// and va_start calls with no matching va_end.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

REGISTER_SET_WITH_PROGRAMSTATE(InitializedVALists, const MemRegion *)

namespace {
typedef SmallVector<const MemRegion *, 2> RegionVector;

class ValistChecker : public Checker<check::PreCall, check::PreStmt<VAArgExpr>,
                                     check::DeadSymbols> {
  mutable std::unique_ptr<BugType> BT_leakedvalist, BT_uninitaccess;

  struct VAListAccepter {
    CallDescription Func;
    int VAListPos;
  };
  static const SmallVector<VAListAccepter, 15> VAListAccepters;
  static const CallDescription VaStart, VaEnd, VaCopy;

public:
  enum CheckKind {
    CK_Uninitialized,
    CK_Unterminated,
    CK_CopyToSelf,
    CK_NumCheckKinds
  };

  DefaultBool ChecksEnabled[CK_NumCheckKinds];
  CheckName CheckNames[CK_NumCheckKinds];

  void checkPreStmt(const VAArgExpr *VAA, CheckerContext &C) const;
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkDeadSymbols(SymbolReaper &SR, CheckerContext &C) const;

private:
  const MemRegion *getVAListAsRegion(SVal SV, const Expr *VAExpr,
                                     bool &IsSymbolic, CheckerContext &C) const;
  const ExplodedNode *getStartCallSite(const ExplodedNode *N,
                                       const MemRegion *Reg) const;

  void reportUninitializedAccess(const MemRegion *VAList, StringRef Msg,
                                 CheckerContext &C) const;
  void reportLeakedVALists(const RegionVector &LeakedVALists, StringRef Msg1,
                           StringRef Msg2, CheckerContext &C, ExplodedNode *N,
                           bool ReportUninit = false) const;

  void checkVAListStartCall(const CallEvent &Call, CheckerContext &C,
                            bool IsCopy) const;
  void checkVAListEndCall(const CallEvent &Call, CheckerContext &C) const;

  class ValistBugVisitor : public BugReporterVisitor {
  public:
    ValistBugVisitor(const MemRegion *Reg, bool IsLeak = false)
        : Reg(Reg), IsLeak(IsLeak) {}
    void Profile(llvm::FoldingSetNodeID &ID) const override {
      static int X = 0;
      ID.AddPointer(&X);
      ID.AddPointer(Reg);
    }
    std::shared_ptr<PathDiagnosticPiece>
    getEndPath(BugReporterContext &BRC, const ExplodedNode *EndPathNode,
               BugReport &BR) override {
      if (!IsLeak)
        return nullptr;

      PathDiagnosticLocation L = PathDiagnosticLocation::createEndOfPath(
          EndPathNode, BRC.getSourceManager());
      // Do not add the statement itself as a range in case of leak.
      return std::make_shared<PathDiagnosticEventPiece>(L, BR.getDescription(), false);
    }
    std::shared_ptr<PathDiagnosticPiece> VisitNode(const ExplodedNode *N,
                                                   BugReporterContext &BRC,
                                                   BugReport &BR) override;

  private:
    const MemRegion *Reg;
    bool IsLeak;
  };
};

const SmallVector<ValistChecker::VAListAccepter, 15>
    ValistChecker::VAListAccepters = {
        {{"vfprintf", 3}, 2},
        {{"vfscanf", 3}, 2},
        {{"vprintf", 2}, 1},
        {{"vscanf", 2}, 1},
        {{"vsnprintf", 4}, 3},
        {{"vsprintf", 3}, 2},
        {{"vsscanf", 3}, 2},
        {{"vfwprintf", 3}, 2},
        {{"vfwscanf", 3}, 2},
        {{"vwprintf", 2}, 1},
        {{"vwscanf", 2}, 1},
        {{"vswprintf", 4}, 3},
        // vswprintf is the wide version of vsnprintf,
        // vsprintf has no wide version
        {{"vswscanf", 3}, 2}};
const CallDescription ValistChecker::VaStart("__builtin_va_start", 2),
    ValistChecker::VaCopy("__builtin_va_copy", 2),
    ValistChecker::VaEnd("__builtin_va_end", 1);
} // end anonymous namespace

void ValistChecker::checkPreCall(const CallEvent &Call,
                                 CheckerContext &C) const {
  if (!Call.isGlobalCFunction())
    return;
  if (Call.isCalled(VaStart))
    checkVAListStartCall(Call, C, false);
  else if (Call.isCalled(VaCopy))
    checkVAListStartCall(Call, C, true);
  else if (Call.isCalled(VaEnd))
    checkVAListEndCall(Call, C);
  else {
    for (auto FuncInfo : VAListAccepters) {
      if (!Call.isCalled(FuncInfo.Func))
        continue;
      bool Symbolic;
      const MemRegion *VAList =
          getVAListAsRegion(Call.getArgSVal(FuncInfo.VAListPos),
                            Call.getArgExpr(FuncInfo.VAListPos), Symbolic, C);
      if (!VAList)
        return;

      if (C.getState()->contains<InitializedVALists>(VAList))
        return;

      // We did not see va_start call, but the source of the region is unknown.
      // Be conservative and assume the best.
      if (Symbolic)
        return;

      SmallString<80> Errmsg("Function '");
      Errmsg += FuncInfo.Func.getFunctionName();
      Errmsg += "' is called with an uninitialized va_list argument";
      reportUninitializedAccess(VAList, Errmsg.c_str(), C);
      break;
    }
  }
}

const MemRegion *ValistChecker::getVAListAsRegion(SVal SV, const Expr *E,
                                                  bool &IsSymbolic,
                                                  CheckerContext &C) const {
  const MemRegion *Reg = SV.getAsRegion();
  if (!Reg)
    return nullptr;
  // TODO: In the future this should be abstracted away by the analyzer.
  bool VaListModelledAsArray = false;
  if (const auto *Cast = dyn_cast<CastExpr>(E)) {
    QualType Ty = Cast->getType();
    VaListModelledAsArray =
        Ty->isPointerType() && Ty->getPointeeType()->isRecordType();
  }
  if (const auto *DeclReg = Reg->getAs<DeclRegion>()) {
    if (isa<ParmVarDecl>(DeclReg->getDecl()))
      Reg = C.getState()->getSVal(SV.castAs<Loc>()).getAsRegion();
  }
  IsSymbolic = Reg && Reg->getAs<SymbolicRegion>();
  // Some VarRegion based VA lists reach here as ElementRegions.
  const auto *EReg = dyn_cast_or_null<ElementRegion>(Reg);
  return (EReg && VaListModelledAsArray) ? EReg->getSuperRegion() : Reg;
}

void ValistChecker::checkPreStmt(const VAArgExpr *VAA,
                                 CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  const Expr *VASubExpr = VAA->getSubExpr();
  SVal VAListSVal = C.getSVal(VASubExpr);
  bool Symbolic;
  const MemRegion *VAList =
      getVAListAsRegion(VAListSVal, VASubExpr, Symbolic, C);
  if (!VAList)
    return;
  if (Symbolic)
    return;
  if (!State->contains<InitializedVALists>(VAList))
    reportUninitializedAccess(
        VAList, "va_arg() is called on an uninitialized va_list", C);
}

void ValistChecker::checkDeadSymbols(SymbolReaper &SR,
                                     CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  InitializedVAListsTy TrackedVALists = State->get<InitializedVALists>();
  RegionVector LeakedVALists;
  for (auto Reg : TrackedVALists) {
    if (SR.isLiveRegion(Reg))
      continue;
    LeakedVALists.push_back(Reg);
    State = State->remove<InitializedVALists>(Reg);
  }
  if (ExplodedNode *N = C.addTransition(State))
    reportLeakedVALists(LeakedVALists, "Initialized va_list", " is leaked", C,
                        N);
}

// This function traverses the exploded graph backwards and finds the node where
// the va_list is initialized. That node is used for uniquing the bug paths.
// It is not likely that there are several different va_lists that belongs to
// different stack frames, so that case is not yet handled.
const ExplodedNode *
ValistChecker::getStartCallSite(const ExplodedNode *N,
                                const MemRegion *Reg) const {
  const LocationContext *LeakContext = N->getLocationContext();
  const ExplodedNode *StartCallNode = N;

  bool FoundInitializedState = false;

  while (N) {
    ProgramStateRef State = N->getState();
    if (!State->contains<InitializedVALists>(Reg)) {
      if (FoundInitializedState)
        break;
    } else {
      FoundInitializedState = true;
    }
    const LocationContext *NContext = N->getLocationContext();
    if (NContext == LeakContext || NContext->isParentOf(LeakContext))
      StartCallNode = N;
    N = N->pred_empty() ? nullptr : *(N->pred_begin());
  }

  return StartCallNode;
}

void ValistChecker::reportUninitializedAccess(const MemRegion *VAList,
                                              StringRef Msg,
                                              CheckerContext &C) const {
  if (!ChecksEnabled[CK_Uninitialized])
    return;
  if (ExplodedNode *N = C.generateErrorNode()) {
    if (!BT_uninitaccess)
      BT_uninitaccess.reset(new BugType(CheckNames[CK_Uninitialized],
                                        "Uninitialized va_list",
                                        categories::MemoryError));
    auto R = llvm::make_unique<BugReport>(*BT_uninitaccess, Msg, N);
    R->markInteresting(VAList);
    R->addVisitor(llvm::make_unique<ValistBugVisitor>(VAList));
    C.emitReport(std::move(R));
  }
}

void ValistChecker::reportLeakedVALists(const RegionVector &LeakedVALists,
                                        StringRef Msg1, StringRef Msg2,
                                        CheckerContext &C, ExplodedNode *N,
                                        bool ReportUninit) const {
  if (!(ChecksEnabled[CK_Unterminated] ||
        (ChecksEnabled[CK_Uninitialized] && ReportUninit)))
    return;
  for (auto Reg : LeakedVALists) {
    if (!BT_leakedvalist) {
      // FIXME: maybe creating a new check name for this type of bug is a better
      // solution.
      BT_leakedvalist.reset(
          new BugType(CheckNames[CK_Unterminated].getName().empty()
                          ? CheckNames[CK_Uninitialized]
                          : CheckNames[CK_Unterminated],
                      "Leaked va_list", categories::MemoryError));
      BT_leakedvalist->setSuppressOnSink(true);
    }

    const ExplodedNode *StartNode = getStartCallSite(N, Reg);
    PathDiagnosticLocation LocUsedForUniqueing;

    if (const Stmt *StartCallStmt = PathDiagnosticLocation::getStmt(StartNode))
      LocUsedForUniqueing = PathDiagnosticLocation::createBegin(
          StartCallStmt, C.getSourceManager(), StartNode->getLocationContext());

    SmallString<100> Buf;
    llvm::raw_svector_ostream OS(Buf);
    OS << Msg1;
    std::string VariableName = Reg->getDescriptiveName();
    if (!VariableName.empty())
      OS << " " << VariableName;
    OS << Msg2;

    auto R = llvm::make_unique<BugReport>(
        *BT_leakedvalist, OS.str(), N, LocUsedForUniqueing,
        StartNode->getLocationContext()->getDecl());
    R->markInteresting(Reg);
    R->addVisitor(llvm::make_unique<ValistBugVisitor>(Reg, true));
    C.emitReport(std::move(R));
  }
}

void ValistChecker::checkVAListStartCall(const CallEvent &Call,
                                         CheckerContext &C, bool IsCopy) const {
  bool Symbolic;
  const MemRegion *VAList =
      getVAListAsRegion(Call.getArgSVal(0), Call.getArgExpr(0), Symbolic, C);
  if (!VAList)
    return;

  ProgramStateRef State = C.getState();

  if (IsCopy) {
    const MemRegion *Arg2 =
        getVAListAsRegion(Call.getArgSVal(1), Call.getArgExpr(1), Symbolic, C);
    if (Arg2) {
      if (ChecksEnabled[CK_CopyToSelf] && VAList == Arg2) {
        RegionVector LeakedVALists{VAList};
        if (ExplodedNode *N = C.addTransition(State))
          reportLeakedVALists(LeakedVALists, "va_list",
                              " is copied onto itself", C, N, true);
        return;
      } else if (!State->contains<InitializedVALists>(Arg2) && !Symbolic) {
        if (State->contains<InitializedVALists>(VAList)) {
          State = State->remove<InitializedVALists>(VAList);
          RegionVector LeakedVALists{VAList};
          if (ExplodedNode *N = C.addTransition(State))
            reportLeakedVALists(LeakedVALists, "Initialized va_list",
                                " is overwritten by an uninitialized one", C, N,
                                true);
        } else {
          reportUninitializedAccess(Arg2, "Uninitialized va_list is copied", C);
        }
        return;
      }
    }
  }
  if (State->contains<InitializedVALists>(VAList)) {
    RegionVector LeakedVALists{VAList};
    if (ExplodedNode *N = C.addTransition(State))
      reportLeakedVALists(LeakedVALists, "Initialized va_list",
                          " is initialized again", C, N);
    return;
  }

  State = State->add<InitializedVALists>(VAList);
  C.addTransition(State);
}

void ValistChecker::checkVAListEndCall(const CallEvent &Call,
                                       CheckerContext &C) const {
  bool Symbolic;
  const MemRegion *VAList =
      getVAListAsRegion(Call.getArgSVal(0), Call.getArgExpr(0), Symbolic, C);
  if (!VAList)
    return;

  // We did not see va_start call, but the source of the region is unknown.
  // Be conservative and assume the best.
  if (Symbolic)
    return;

  if (!C.getState()->contains<InitializedVALists>(VAList)) {
    reportUninitializedAccess(
        VAList, "va_end() is called on an uninitialized va_list", C);
    return;
  }
  ProgramStateRef State = C.getState();
  State = State->remove<InitializedVALists>(VAList);
  C.addTransition(State);
}

std::shared_ptr<PathDiagnosticPiece> ValistChecker::ValistBugVisitor::VisitNode(
    const ExplodedNode *N, BugReporterContext &BRC,
    BugReport &) {
  ProgramStateRef State = N->getState();
  ProgramStateRef StatePrev = N->getFirstPred()->getState();

  const Stmt *S = PathDiagnosticLocation::getStmt(N);
  if (!S)
    return nullptr;

  StringRef Msg;
  if (State->contains<InitializedVALists>(Reg) &&
      !StatePrev->contains<InitializedVALists>(Reg))
    Msg = "Initialized va_list";
  else if (!State->contains<InitializedVALists>(Reg) &&
           StatePrev->contains<InitializedVALists>(Reg))
    Msg = "Ended va_list";

  if (Msg.empty())
    return nullptr;

  PathDiagnosticLocation Pos(S, BRC.getSourceManager(),
                             N->getLocationContext());
  return std::make_shared<PathDiagnosticEventPiece>(Pos, Msg, true);
}

#define REGISTER_CHECKER(name)                                                 \
  void ento::register##name##Checker(CheckerManager &mgr) {                    \
    ValistChecker *checker = mgr.registerChecker<ValistChecker>();             \
    checker->ChecksEnabled[ValistChecker::CK_##name] = true;                   \
    checker->CheckNames[ValistChecker::CK_##name] = mgr.getCurrentCheckName(); \
  }

REGISTER_CHECKER(Uninitialized)
REGISTER_CHECKER(Unterminated)
REGISTER_CHECKER(CopyToSelf)
