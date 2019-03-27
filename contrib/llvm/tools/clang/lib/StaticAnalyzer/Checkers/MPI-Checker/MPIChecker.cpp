//===-- MPIChecker.cpp - Checker Entry Point Class --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the main class of MPI-Checker which serves as an entry
/// point. It is created once for each translation unit analysed.
/// The checker defines path-sensitive checks, to verify correct usage of the
/// MPI API.
///
//===----------------------------------------------------------------------===//

#include "MPIChecker.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"

namespace clang {
namespace ento {
namespace mpi {

void MPIChecker::checkDoubleNonblocking(const CallEvent &PreCallEvent,
                                        CheckerContext &Ctx) const {
  if (!FuncClassifier->isNonBlockingType(PreCallEvent.getCalleeIdentifier())) {
    return;
  }
  const MemRegion *const MR =
      PreCallEvent.getArgSVal(PreCallEvent.getNumArgs() - 1).getAsRegion();
  if (!MR)
    return;
  const ElementRegion *const ER = dyn_cast<ElementRegion>(MR);

  // The region must be typed, in order to reason about it.
  if (!isa<TypedRegion>(MR) || (ER && !isa<TypedRegion>(ER->getSuperRegion())))
    return;

  ProgramStateRef State = Ctx.getState();
  const Request *const Req = State->get<RequestMap>(MR);

  // double nonblocking detected
  if (Req && Req->CurrentState == Request::State::Nonblocking) {
    ExplodedNode *ErrorNode = Ctx.generateNonFatalErrorNode();
    BReporter.reportDoubleNonblocking(PreCallEvent, *Req, MR, ErrorNode,
                                      Ctx.getBugReporter());
    Ctx.addTransition(ErrorNode->getState(), ErrorNode);
  }
  // no error
  else {
    State = State->set<RequestMap>(MR, Request::State::Nonblocking);
    Ctx.addTransition(State);
  }
}

void MPIChecker::checkUnmatchedWaits(const CallEvent &PreCallEvent,
                                     CheckerContext &Ctx) const {
  if (!FuncClassifier->isWaitType(PreCallEvent.getCalleeIdentifier()))
    return;
  const MemRegion *const MR = topRegionUsedByWait(PreCallEvent);
  if (!MR)
    return;
  const ElementRegion *const ER = dyn_cast<ElementRegion>(MR);

  // The region must be typed, in order to reason about it.
  if (!isa<TypedRegion>(MR) || (ER && !isa<TypedRegion>(ER->getSuperRegion())))
    return;

  llvm::SmallVector<const MemRegion *, 2> ReqRegions;
  allRegionsUsedByWait(ReqRegions, MR, PreCallEvent, Ctx);
  if (ReqRegions.empty())
    return;

  ProgramStateRef State = Ctx.getState();
  static CheckerProgramPointTag Tag("MPI-Checker", "UnmatchedWait");
  ExplodedNode *ErrorNode{nullptr};

  // Check all request regions used by the wait function.
  for (const auto &ReqRegion : ReqRegions) {
    const Request *const Req = State->get<RequestMap>(ReqRegion);
    State = State->set<RequestMap>(ReqRegion, Request::State::Wait);
    if (!Req) {
      if (!ErrorNode) {
        ErrorNode = Ctx.generateNonFatalErrorNode(State, &Tag);
        State = ErrorNode->getState();
      }
      // A wait has no matching nonblocking call.
      BReporter.reportUnmatchedWait(PreCallEvent, ReqRegion, ErrorNode,
                                    Ctx.getBugReporter());
    }
  }

  if (!ErrorNode) {
    Ctx.addTransition(State);
  } else {
    Ctx.addTransition(State, ErrorNode);
  }
}

void MPIChecker::checkMissingWaits(SymbolReaper &SymReaper,
                                   CheckerContext &Ctx) const {
  ProgramStateRef State = Ctx.getState();
  const auto &Requests = State->get<RequestMap>();
  if (Requests.isEmpty())
    return;

  static CheckerProgramPointTag Tag("MPI-Checker", "MissingWait");
  ExplodedNode *ErrorNode{nullptr};

  auto ReqMap = State->get<RequestMap>();
  for (const auto &Req : ReqMap) {
    if (!SymReaper.isLiveRegion(Req.first)) {
      if (Req.second.CurrentState == Request::State::Nonblocking) {

        if (!ErrorNode) {
          ErrorNode = Ctx.generateNonFatalErrorNode(State, &Tag);
          State = ErrorNode->getState();
        }
        BReporter.reportMissingWait(Req.second, Req.first, ErrorNode,
                                    Ctx.getBugReporter());
      }
      State = State->remove<RequestMap>(Req.first);
    }
  }

  // Transition to update the state regarding removed requests.
  if (!ErrorNode) {
    Ctx.addTransition(State);
  } else {
    Ctx.addTransition(State, ErrorNode);
  }
}

const MemRegion *MPIChecker::topRegionUsedByWait(const CallEvent &CE) const {

  if (FuncClassifier->isMPI_Wait(CE.getCalleeIdentifier())) {
    return CE.getArgSVal(0).getAsRegion();
  } else if (FuncClassifier->isMPI_Waitall(CE.getCalleeIdentifier())) {
    return CE.getArgSVal(1).getAsRegion();
  } else {
    return (const MemRegion *)nullptr;
  }
}

void MPIChecker::allRegionsUsedByWait(
    llvm::SmallVector<const MemRegion *, 2> &ReqRegions,
    const MemRegion *const MR, const CallEvent &CE, CheckerContext &Ctx) const {

  MemRegionManager *const RegionManager = MR->getMemRegionManager();

  if (FuncClassifier->isMPI_Waitall(CE.getCalleeIdentifier())) {
    const SubRegion *SuperRegion{nullptr};
    if (const ElementRegion *const ER = MR->getAs<ElementRegion>()) {
      SuperRegion = cast<SubRegion>(ER->getSuperRegion());
    }

    // A single request is passed to MPI_Waitall.
    if (!SuperRegion) {
      ReqRegions.push_back(MR);
      return;
    }

    const auto &Size = Ctx.getStoreManager().getSizeInElements(
        Ctx.getState(), SuperRegion,
        CE.getArgExpr(1)->getType()->getPointeeType());
    const llvm::APSInt &ArrSize = Size.getAs<nonloc::ConcreteInt>()->getValue();

    for (size_t i = 0; i < ArrSize; ++i) {
      const NonLoc Idx = Ctx.getSValBuilder().makeArrayIndex(i);

      const ElementRegion *const ER = RegionManager->getElementRegion(
          CE.getArgExpr(1)->getType()->getPointeeType(), Idx, SuperRegion,
          Ctx.getASTContext());

      ReqRegions.push_back(ER->getAs<MemRegion>());
    }
  } else if (FuncClassifier->isMPI_Wait(CE.getCalleeIdentifier())) {
    ReqRegions.push_back(MR);
  }
}

} // end of namespace: mpi
} // end of namespace: ento
} // end of namespace: clang

// Registers the checker for static analysis.
void clang::ento::registerMPIChecker(CheckerManager &MGR) {
  MGR.registerChecker<clang::ento::mpi::MPIChecker>();
}
