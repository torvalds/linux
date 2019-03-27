//===- ExprEngine.cpp - Path-Sensitive Expression-Level Dataflow ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines a meta-engine for path-sensitive dataflow analysis that
//  is built on GREngine, but provides the boilerplate to execute transfer
//  functions and build the ExplodedGraph at the expression level.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "PrettyStackTraceLocationContext.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ParentMap.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/StmtObjC.h"
#include "clang/AST/Type.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/ConstructionContext.h"
#include "clang/Analysis/ProgramPoint.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/PrettyStackTrace.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Specifiers.h"
#include "clang/StaticAnalyzer/Core/AnalyzerOptions.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ConstraintManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CoreEngine.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExplodedGraph.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/LoopUnrolling.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/LoopWidening.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState_Fwd.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SValBuilder.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/Store.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymExpr.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ImmutableMap.h"
#include "llvm/ADT/ImmutableSet.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DOTGraphTraits.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace clang;
using namespace ento;

#define DEBUG_TYPE "ExprEngine"

STATISTIC(NumRemoveDeadBindings,
            "The # of times RemoveDeadBindings is called");
STATISTIC(NumMaxBlockCountReached,
            "The # of aborted paths due to reaching the maximum block count in "
            "a top level function");
STATISTIC(NumMaxBlockCountReachedInInlined,
            "The # of aborted paths due to reaching the maximum block count in "
            "an inlined function");
STATISTIC(NumTimesRetriedWithoutInlining,
            "The # of times we re-evaluated a call without inlining");

//===----------------------------------------------------------------------===//
// Internal program state traits.
//===----------------------------------------------------------------------===//

namespace {

// When modeling a C++ constructor, for a variety of reasons we need to track
// the location of the object for the duration of its ConstructionContext.
// ObjectsUnderConstruction maps statements within the construction context
// to the object's location, so that on every such statement the location
// could have been retrieved.

/// ConstructedObjectKey is used for being able to find the path-sensitive
/// memory region of a freshly constructed object while modeling the AST node
/// that syntactically represents the object that is being constructed.
/// Semantics of such nodes may sometimes require access to the region that's
/// not otherwise present in the program state, or to the very fact that
/// the construction context was present and contained references to these
/// AST nodes.
class ConstructedObjectKey {
  typedef std::pair<ConstructionContextItem, const LocationContext *>
      ConstructedObjectKeyImpl;

  const ConstructedObjectKeyImpl Impl;

  const void *getAnyASTNodePtr() const {
    if (const Stmt *S = getItem().getStmtOrNull())
      return S;
    else
      return getItem().getCXXCtorInitializer();
  }

public:
  explicit ConstructedObjectKey(const ConstructionContextItem &Item,
                       const LocationContext *LC)
      : Impl(Item, LC) {}

  const ConstructionContextItem &getItem() const { return Impl.first; }
  const LocationContext *getLocationContext() const { return Impl.second; }

  ASTContext &getASTContext() const {
    return getLocationContext()->getDecl()->getASTContext();
  }

  void print(llvm::raw_ostream &OS, PrinterHelper *Helper, PrintingPolicy &PP) {
    OS << "(LC" << getLocationContext()->getID() << ',';
    if (const Stmt *S = getItem().getStmtOrNull())
      OS << 'S' << S->getID(getASTContext());
    else
      OS << 'I' << getItem().getCXXCtorInitializer()->getID(getASTContext());
    OS << ',' << getItem().getKindAsString();
    if (getItem().getKind() == ConstructionContextItem::ArgumentKind)
      OS << " #" << getItem().getIndex();
    OS << ") ";
    if (const Stmt *S = getItem().getStmtOrNull()) {
      S->printPretty(OS, Helper, PP);
    } else {
      const CXXCtorInitializer *I = getItem().getCXXCtorInitializer();
      OS << I->getAnyMember()->getNameAsString();
    }
  }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.Add(Impl.first);
    ID.AddPointer(Impl.second);
  }

  bool operator==(const ConstructedObjectKey &RHS) const {
    return Impl == RHS.Impl;
  }

  bool operator<(const ConstructedObjectKey &RHS) const {
    return Impl < RHS.Impl;
  }
};
} // namespace

typedef llvm::ImmutableMap<ConstructedObjectKey, SVal>
    ObjectsUnderConstructionMap;
REGISTER_TRAIT_WITH_PROGRAMSTATE(ObjectsUnderConstruction,
                                 ObjectsUnderConstructionMap)

//===----------------------------------------------------------------------===//
// Engine construction and deletion.
//===----------------------------------------------------------------------===//

static const char* TagProviderName = "ExprEngine";

ExprEngine::ExprEngine(cross_tu::CrossTranslationUnitContext &CTU,
                       AnalysisManager &mgr,
                       SetOfConstDecls *VisitedCalleesIn,
                       FunctionSummariesTy *FS,
                       InliningModes HowToInlineIn)
    : CTU(CTU), AMgr(mgr),
      AnalysisDeclContexts(mgr.getAnalysisDeclContextManager()),
      Engine(*this, FS, mgr.getAnalyzerOptions()), G(Engine.getGraph()),
      StateMgr(getContext(), mgr.getStoreManagerCreator(),
               mgr.getConstraintManagerCreator(), G.getAllocator(),
               this),
      SymMgr(StateMgr.getSymbolManager()),
      svalBuilder(StateMgr.getSValBuilder()), ObjCNoRet(mgr.getASTContext()),
      BR(mgr, *this),
      VisitedCallees(VisitedCalleesIn), HowToInline(HowToInlineIn) {
  unsigned TrimInterval = mgr.options.GraphTrimInterval;
  if (TrimInterval != 0) {
    // Enable eager node reclamation when constructing the ExplodedGraph.
    G.enableNodeReclamation(TrimInterval);
  }
}

ExprEngine::~ExprEngine() {
  BR.FlushReports();
}

//===----------------------------------------------------------------------===//
// Utility methods.
//===----------------------------------------------------------------------===//

ProgramStateRef ExprEngine::getInitialState(const LocationContext *InitLoc) {
  ProgramStateRef state = StateMgr.getInitialState(InitLoc);
  const Decl *D = InitLoc->getDecl();

  // Preconditions.
  // FIXME: It would be nice if we had a more general mechanism to add
  // such preconditions.  Some day.
  do {
    if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
      // Precondition: the first argument of 'main' is an integer guaranteed
      //  to be > 0.
      const IdentifierInfo *II = FD->getIdentifier();
      if (!II || !(II->getName() == "main" && FD->getNumParams() > 0))
        break;

      const ParmVarDecl *PD = FD->getParamDecl(0);
      QualType T = PD->getType();
      const auto *BT = dyn_cast<BuiltinType>(T);
      if (!BT || !BT->isInteger())
        break;

      const MemRegion *R = state->getRegion(PD, InitLoc);
      if (!R)
        break;

      SVal V = state->getSVal(loc::MemRegionVal(R));
      SVal Constraint_untested = evalBinOp(state, BO_GT, V,
                                           svalBuilder.makeZeroVal(T),
                                           svalBuilder.getConditionType());

      Optional<DefinedOrUnknownSVal> Constraint =
          Constraint_untested.getAs<DefinedOrUnknownSVal>();

      if (!Constraint)
        break;

      if (ProgramStateRef newState = state->assume(*Constraint, true))
        state = newState;
    }
    break;
  }
  while (false);

  if (const auto *MD = dyn_cast<ObjCMethodDecl>(D)) {
    // Precondition: 'self' is always non-null upon entry to an Objective-C
    // method.
    const ImplicitParamDecl *SelfD = MD->getSelfDecl();
    const MemRegion *R = state->getRegion(SelfD, InitLoc);
    SVal V = state->getSVal(loc::MemRegionVal(R));

    if (Optional<Loc> LV = V.getAs<Loc>()) {
      // Assume that the pointer value in 'self' is non-null.
      state = state->assume(*LV, true);
      assert(state && "'self' cannot be null");
    }
  }

  if (const auto *MD = dyn_cast<CXXMethodDecl>(D)) {
    if (!MD->isStatic()) {
      // Precondition: 'this' is always non-null upon entry to the
      // top-level function.  This is our starting assumption for
      // analyzing an "open" program.
      const StackFrameContext *SFC = InitLoc->getStackFrame();
      if (SFC->getParent() == nullptr) {
        loc::MemRegionVal L = svalBuilder.getCXXThis(MD, SFC);
        SVal V = state->getSVal(L);
        if (Optional<Loc> LV = V.getAs<Loc>()) {
          state = state->assume(*LV, true);
          assert(state && "'this' cannot be null");
        }
      }
    }
  }

  return state;
}

ProgramStateRef ExprEngine::createTemporaryRegionIfNeeded(
    ProgramStateRef State, const LocationContext *LC,
    const Expr *InitWithAdjustments, const Expr *Result,
    const SubRegion **OutRegionWithAdjustments) {
  // FIXME: This function is a hack that works around the quirky AST
  // we're often having with respect to C++ temporaries. If only we modelled
  // the actual execution order of statements properly in the CFG,
  // all the hassle with adjustments would not be necessary,
  // and perhaps the whole function would be removed.
  SVal InitValWithAdjustments = State->getSVal(InitWithAdjustments, LC);
  if (!Result) {
    // If we don't have an explicit result expression, we're in "if needed"
    // mode. Only create a region if the current value is a NonLoc.
    if (!InitValWithAdjustments.getAs<NonLoc>()) {
      if (OutRegionWithAdjustments)
        *OutRegionWithAdjustments = nullptr;
      return State;
    }
    Result = InitWithAdjustments;
  } else {
    // We need to create a region no matter what. For sanity, make sure we don't
    // try to stuff a Loc into a non-pointer temporary region.
    assert(!InitValWithAdjustments.getAs<Loc>() ||
           Loc::isLocType(Result->getType()) ||
           Result->getType()->isMemberPointerType());
  }

  ProgramStateManager &StateMgr = State->getStateManager();
  MemRegionManager &MRMgr = StateMgr.getRegionManager();
  StoreManager &StoreMgr = StateMgr.getStoreManager();

  // MaterializeTemporaryExpr may appear out of place, after a few field and
  // base-class accesses have been made to the object, even though semantically
  // it is the whole object that gets materialized and lifetime-extended.
  //
  // For example:
  //
  //   `-MaterializeTemporaryExpr
  //     `-MemberExpr
  //       `-CXXTemporaryObjectExpr
  //
  // instead of the more natural
  //
  //   `-MemberExpr
  //     `-MaterializeTemporaryExpr
  //       `-CXXTemporaryObjectExpr
  //
  // Use the usual methods for obtaining the expression of the base object,
  // and record the adjustments that we need to make to obtain the sub-object
  // that the whole expression 'Ex' refers to. This trick is usual,
  // in the sense that CodeGen takes a similar route.

  SmallVector<const Expr *, 2> CommaLHSs;
  SmallVector<SubobjectAdjustment, 2> Adjustments;

  const Expr *Init = InitWithAdjustments->skipRValueSubobjectAdjustments(
      CommaLHSs, Adjustments);

  // Take the region for Init, i.e. for the whole object. If we do not remember
  // the region in which the object originally was constructed, come up with
  // a new temporary region out of thin air and copy the contents of the object
  // (which are currently present in the Environment, because Init is an rvalue)
  // into that region. This is not correct, but it is better than nothing.
  const TypedValueRegion *TR = nullptr;
  if (const auto *MT = dyn_cast<MaterializeTemporaryExpr>(Result)) {
    if (Optional<SVal> V = getObjectUnderConstruction(State, MT, LC)) {
      State = finishObjectConstruction(State, MT, LC);
      State = State->BindExpr(Result, LC, *V);
      return State;
    } else {
      StorageDuration SD = MT->getStorageDuration();
      // If this object is bound to a reference with static storage duration, we
      // put it in a different region to prevent "address leakage" warnings.
      if (SD == SD_Static || SD == SD_Thread) {
        TR = MRMgr.getCXXStaticTempObjectRegion(Init);
      } else {
        TR = MRMgr.getCXXTempObjectRegion(Init, LC);
      }
    }
  } else {
    TR = MRMgr.getCXXTempObjectRegion(Init, LC);
  }

  SVal Reg = loc::MemRegionVal(TR);
  SVal BaseReg = Reg;

  // Make the necessary adjustments to obtain the sub-object.
  for (auto I = Adjustments.rbegin(), E = Adjustments.rend(); I != E; ++I) {
    const SubobjectAdjustment &Adj = *I;
    switch (Adj.Kind) {
    case SubobjectAdjustment::DerivedToBaseAdjustment:
      Reg = StoreMgr.evalDerivedToBase(Reg, Adj.DerivedToBase.BasePath);
      break;
    case SubobjectAdjustment::FieldAdjustment:
      Reg = StoreMgr.getLValueField(Adj.Field, Reg);
      break;
    case SubobjectAdjustment::MemberPointerAdjustment:
      // FIXME: Unimplemented.
      State = State->invalidateRegions(Reg, InitWithAdjustments,
                                       currBldrCtx->blockCount(), LC, true,
                                       nullptr, nullptr, nullptr);
      return State;
    }
  }

  // What remains is to copy the value of the object to the new region.
  // FIXME: In other words, what we should always do is copy value of the
  // Init expression (which corresponds to the bigger object) to the whole
  // temporary region TR. However, this value is often no longer present
  // in the Environment. If it has disappeared, we instead invalidate TR.
  // Still, what we can do is assign the value of expression Ex (which
  // corresponds to the sub-object) to the TR's sub-region Reg. At least,
  // values inside Reg would be correct.
  SVal InitVal = State->getSVal(Init, LC);
  if (InitVal.isUnknown()) {
    InitVal = getSValBuilder().conjureSymbolVal(Result, LC, Init->getType(),
                                                currBldrCtx->blockCount());
    State = State->bindLoc(BaseReg.castAs<Loc>(), InitVal, LC, false);

    // Then we'd need to take the value that certainly exists and bind it
    // over.
    if (InitValWithAdjustments.isUnknown()) {
      // Try to recover some path sensitivity in case we couldn't
      // compute the value.
      InitValWithAdjustments = getSValBuilder().conjureSymbolVal(
          Result, LC, InitWithAdjustments->getType(),
          currBldrCtx->blockCount());
    }
    State =
        State->bindLoc(Reg.castAs<Loc>(), InitValWithAdjustments, LC, false);
  } else {
    State = State->bindLoc(BaseReg.castAs<Loc>(), InitVal, LC, false);
  }

  // The result expression would now point to the correct sub-region of the
  // newly created temporary region. Do this last in order to getSVal of Init
  // correctly in case (Result == Init).
  if (Result->isGLValue()) {
    State = State->BindExpr(Result, LC, Reg);
  } else {
    State = State->BindExpr(Result, LC, InitValWithAdjustments);
  }

  // Notify checkers once for two bindLoc()s.
  State = processRegionChange(State, TR, LC);

  if (OutRegionWithAdjustments)
    *OutRegionWithAdjustments = cast<SubRegion>(Reg.getAsRegion());
  return State;
}

ProgramStateRef
ExprEngine::addObjectUnderConstruction(ProgramStateRef State,
                                       const ConstructionContextItem &Item,
                                       const LocationContext *LC, SVal V) {
  ConstructedObjectKey Key(Item, LC->getStackFrame());
  // FIXME: Currently the state might already contain the marker due to
  // incorrect handling of temporaries bound to default parameters.
  assert(!State->get<ObjectsUnderConstruction>(Key) ||
         Key.getItem().getKind() ==
             ConstructionContextItem::TemporaryDestructorKind);
  return State->set<ObjectsUnderConstruction>(Key, V);
}

Optional<SVal>
ExprEngine::getObjectUnderConstruction(ProgramStateRef State,
                                       const ConstructionContextItem &Item,
                                       const LocationContext *LC) {
  ConstructedObjectKey Key(Item, LC->getStackFrame());
  return Optional<SVal>::create(State->get<ObjectsUnderConstruction>(Key));
}

ProgramStateRef
ExprEngine::finishObjectConstruction(ProgramStateRef State,
                                     const ConstructionContextItem &Item,
                                     const LocationContext *LC) {
  ConstructedObjectKey Key(Item, LC->getStackFrame());
  assert(State->contains<ObjectsUnderConstruction>(Key));
  return State->remove<ObjectsUnderConstruction>(Key);
}

ProgramStateRef ExprEngine::elideDestructor(ProgramStateRef State,
                                            const CXXBindTemporaryExpr *BTE,
                                            const LocationContext *LC) {
  ConstructedObjectKey Key({BTE, /*IsElided=*/true}, LC);
  // FIXME: Currently the state might already contain the marker due to
  // incorrect handling of temporaries bound to default parameters.
  return State->set<ObjectsUnderConstruction>(Key, UnknownVal());
}

ProgramStateRef
ExprEngine::cleanupElidedDestructor(ProgramStateRef State,
                                    const CXXBindTemporaryExpr *BTE,
                                    const LocationContext *LC) {
  ConstructedObjectKey Key({BTE, /*IsElided=*/true}, LC);
  assert(State->contains<ObjectsUnderConstruction>(Key));
  return State->remove<ObjectsUnderConstruction>(Key);
}

bool ExprEngine::isDestructorElided(ProgramStateRef State,
                                    const CXXBindTemporaryExpr *BTE,
                                    const LocationContext *LC) {
  ConstructedObjectKey Key({BTE, /*IsElided=*/true}, LC);
  return State->contains<ObjectsUnderConstruction>(Key);
}

bool ExprEngine::areAllObjectsFullyConstructed(ProgramStateRef State,
                                               const LocationContext *FromLC,
                                               const LocationContext *ToLC) {
  const LocationContext *LC = FromLC;
  while (LC != ToLC) {
    assert(LC && "ToLC must be a parent of FromLC!");
    for (auto I : State->get<ObjectsUnderConstruction>())
      if (I.first.getLocationContext() == LC)
        return false;

    LC = LC->getParent();
  }
  return true;
}


//===----------------------------------------------------------------------===//
// Top-level transfer function logic (Dispatcher).
//===----------------------------------------------------------------------===//

/// evalAssume - Called by ConstraintManager. Used to call checker-specific
///  logic for handling assumptions on symbolic values.
ProgramStateRef ExprEngine::processAssume(ProgramStateRef state,
                                              SVal cond, bool assumption) {
  return getCheckerManager().runCheckersForEvalAssume(state, cond, assumption);
}

ProgramStateRef
ExprEngine::processRegionChanges(ProgramStateRef state,
                                 const InvalidatedSymbols *invalidated,
                                 ArrayRef<const MemRegion *> Explicits,
                                 ArrayRef<const MemRegion *> Regions,
                                 const LocationContext *LCtx,
                                 const CallEvent *Call) {
  return getCheckerManager().runCheckersForRegionChanges(state, invalidated,
                                                         Explicits, Regions,
                                                         LCtx, Call);
}

static void printObjectsUnderConstructionForContext(raw_ostream &Out,
                                                    ProgramStateRef State,
                                                    const char *NL,
                                                    const LocationContext *LC) {
  PrintingPolicy PP =
      LC->getAnalysisDeclContext()->getASTContext().getPrintingPolicy();
  for (auto I : State->get<ObjectsUnderConstruction>()) {
    ConstructedObjectKey Key = I.first;
    SVal Value = I.second;
    if (Key.getLocationContext() != LC)
      continue;
    Key.print(Out, nullptr, PP);
    Out << " : " << Value << NL;
  }
}

void ExprEngine::printState(raw_ostream &Out, ProgramStateRef State,
                            const char *NL, const char *Sep,
                            const LocationContext *LCtx) {
  if (LCtx) {
    if (!State->get<ObjectsUnderConstruction>().isEmpty()) {
      Out << Sep << "Objects under construction:" << NL;

      LCtx->dumpStack(Out, "", NL, Sep, [&](const LocationContext *LC) {
        printObjectsUnderConstructionForContext(Out, State, NL, LC);
      });
    }
  }

  getCheckerManager().runCheckersForPrintState(Out, State, NL, Sep);
}

void ExprEngine::processEndWorklist() {
  getCheckerManager().runCheckersForEndAnalysis(G, BR, *this);
}

void ExprEngine::processCFGElement(const CFGElement E, ExplodedNode *Pred,
                                   unsigned StmtIdx, NodeBuilderContext *Ctx) {
  PrettyStackTraceLocationContext CrashInfo(Pred->getLocationContext());
  currStmtIdx = StmtIdx;
  currBldrCtx = Ctx;

  switch (E.getKind()) {
    case CFGElement::Statement:
    case CFGElement::Constructor:
    case CFGElement::CXXRecordTypedCall:
      ProcessStmt(E.castAs<CFGStmt>().getStmt(), Pred);
      return;
    case CFGElement::Initializer:
      ProcessInitializer(E.castAs<CFGInitializer>(), Pred);
      return;
    case CFGElement::NewAllocator:
      ProcessNewAllocator(E.castAs<CFGNewAllocator>().getAllocatorExpr(),
                          Pred);
      return;
    case CFGElement::AutomaticObjectDtor:
    case CFGElement::DeleteDtor:
    case CFGElement::BaseDtor:
    case CFGElement::MemberDtor:
    case CFGElement::TemporaryDtor:
      ProcessImplicitDtor(E.castAs<CFGImplicitDtor>(), Pred);
      return;
    case CFGElement::LoopExit:
      ProcessLoopExit(E.castAs<CFGLoopExit>().getLoopStmt(), Pred);
      return;
    case CFGElement::LifetimeEnds:
    case CFGElement::ScopeBegin:
    case CFGElement::ScopeEnd:
      return;
  }
}

static bool shouldRemoveDeadBindings(AnalysisManager &AMgr,
                                     const Stmt *S,
                                     const ExplodedNode *Pred,
                                     const LocationContext *LC) {
  // Are we never purging state values?
  if (AMgr.options.AnalysisPurgeOpt == PurgeNone)
    return false;

  // Is this the beginning of a basic block?
  if (Pred->getLocation().getAs<BlockEntrance>())
    return true;

  // Is this on a non-expression?
  if (!isa<Expr>(S))
    return true;

  // Run before processing a call.
  if (CallEvent::isCallStmt(S))
    return true;

  // Is this an expression that is consumed by another expression?  If so,
  // postpone cleaning out the state.
  ParentMap &PM = LC->getAnalysisDeclContext()->getParentMap();
  return !PM.isConsumedExpr(cast<Expr>(S));
}

void ExprEngine::removeDead(ExplodedNode *Pred, ExplodedNodeSet &Out,
                            const Stmt *ReferenceStmt,
                            const LocationContext *LC,
                            const Stmt *DiagnosticStmt,
                            ProgramPoint::Kind K) {
  assert((K == ProgramPoint::PreStmtPurgeDeadSymbolsKind ||
          ReferenceStmt == nullptr || isa<ReturnStmt>(ReferenceStmt))
          && "PostStmt is not generally supported by the SymbolReaper yet");
  assert(LC && "Must pass the current (or expiring) LocationContext");

  if (!DiagnosticStmt) {
    DiagnosticStmt = ReferenceStmt;
    assert(DiagnosticStmt && "Required for clearing a LocationContext");
  }

  NumRemoveDeadBindings++;
  ProgramStateRef CleanedState = Pred->getState();

  // LC is the location context being destroyed, but SymbolReaper wants a
  // location context that is still live. (If this is the top-level stack
  // frame, this will be null.)
  if (!ReferenceStmt) {
    assert(K == ProgramPoint::PostStmtPurgeDeadSymbolsKind &&
           "Use PostStmtPurgeDeadSymbolsKind for clearing a LocationContext");
    LC = LC->getParent();
  }

  const StackFrameContext *SFC = LC ? LC->getStackFrame() : nullptr;
  SymbolReaper SymReaper(SFC, ReferenceStmt, SymMgr, getStoreManager());

  for (auto I : CleanedState->get<ObjectsUnderConstruction>()) {
    if (SymbolRef Sym = I.second.getAsSymbol())
      SymReaper.markLive(Sym);
    if (const MemRegion *MR = I.second.getAsRegion())
      SymReaper.markLive(MR);
  }

  getCheckerManager().runCheckersForLiveSymbols(CleanedState, SymReaper);

  // Create a state in which dead bindings are removed from the environment
  // and the store. TODO: The function should just return new env and store,
  // not a new state.
  CleanedState = StateMgr.removeDeadBindings(CleanedState, SFC, SymReaper);

  // Process any special transfer function for dead symbols.
  // A tag to track convenience transitions, which can be removed at cleanup.
  static SimpleProgramPointTag cleanupTag(TagProviderName, "Clean Node");
  // Call checkers with the non-cleaned state so that they could query the
  // values of the soon to be dead symbols.
  ExplodedNodeSet CheckedSet;
  getCheckerManager().runCheckersForDeadSymbols(CheckedSet, Pred, SymReaper,
                                                DiagnosticStmt, *this, K);

  // For each node in CheckedSet, generate CleanedNodes that have the
  // environment, the store, and the constraints cleaned up but have the
  // user-supplied states as the predecessors.
  StmtNodeBuilder Bldr(CheckedSet, Out, *currBldrCtx);
  for (const auto I : CheckedSet) {
    ProgramStateRef CheckerState = I->getState();

    // The constraint manager has not been cleaned up yet, so clean up now.
    CheckerState =
        getConstraintManager().removeDeadBindings(CheckerState, SymReaper);

    assert(StateMgr.haveEqualEnvironments(CheckerState, Pred->getState()) &&
           "Checkers are not allowed to modify the Environment as a part of "
           "checkDeadSymbols processing.");
    assert(StateMgr.haveEqualStores(CheckerState, Pred->getState()) &&
           "Checkers are not allowed to modify the Store as a part of "
           "checkDeadSymbols processing.");

    // Create a state based on CleanedState with CheckerState GDM and
    // generate a transition to that state.
    ProgramStateRef CleanedCheckerSt =
        StateMgr.getPersistentStateWithGDM(CleanedState, CheckerState);
    Bldr.generateNode(DiagnosticStmt, I, CleanedCheckerSt, &cleanupTag, K);
  }
}

void ExprEngine::ProcessStmt(const Stmt *currStmt, ExplodedNode *Pred) {
  // Reclaim any unnecessary nodes in the ExplodedGraph.
  G.reclaimRecentlyAllocatedNodes();

  PrettyStackTraceLoc CrashInfo(getContext().getSourceManager(),
                                currStmt->getBeginLoc(),
                                "Error evaluating statement");

  // Remove dead bindings and symbols.
  ExplodedNodeSet CleanedStates;
  if (shouldRemoveDeadBindings(AMgr, currStmt, Pred,
                               Pred->getLocationContext())) {
    removeDead(Pred, CleanedStates, currStmt,
                                    Pred->getLocationContext());
  } else
    CleanedStates.Add(Pred);

  // Visit the statement.
  ExplodedNodeSet Dst;
  for (const auto I : CleanedStates) {
    ExplodedNodeSet DstI;
    // Visit the statement.
    Visit(currStmt, I, DstI);
    Dst.insert(DstI);
  }

  // Enqueue the new nodes onto the work list.
  Engine.enqueue(Dst, currBldrCtx->getBlock(), currStmtIdx);
}

void ExprEngine::ProcessLoopExit(const Stmt* S, ExplodedNode *Pred) {
  PrettyStackTraceLoc CrashInfo(getContext().getSourceManager(),
                                S->getBeginLoc(),
                                "Error evaluating end of the loop");
  ExplodedNodeSet Dst;
  Dst.Add(Pred);
  NodeBuilder Bldr(Pred, Dst, *currBldrCtx);
  ProgramStateRef NewState = Pred->getState();

  if(AMgr.options.ShouldUnrollLoops)
    NewState = processLoopEnd(S, NewState);

  LoopExit PP(S, Pred->getLocationContext());
  Bldr.generateNode(PP, NewState, Pred);
  // Enqueue the new nodes onto the work list.
  Engine.enqueue(Dst, currBldrCtx->getBlock(), currStmtIdx);
}

void ExprEngine::ProcessInitializer(const CFGInitializer CFGInit,
                                    ExplodedNode *Pred) {
  const CXXCtorInitializer *BMI = CFGInit.getInitializer();
  const Expr *Init = BMI->getInit()->IgnoreImplicit();
  const LocationContext *LC = Pred->getLocationContext();

  PrettyStackTraceLoc CrashInfo(getContext().getSourceManager(),
                                BMI->getSourceLocation(),
                                "Error evaluating initializer");

  // We don't clean up dead bindings here.
  const auto *stackFrame = cast<StackFrameContext>(Pred->getLocationContext());
  const auto *decl = cast<CXXConstructorDecl>(stackFrame->getDecl());

  ProgramStateRef State = Pred->getState();
  SVal thisVal = State->getSVal(svalBuilder.getCXXThis(decl, stackFrame));

  ExplodedNodeSet Tmp;
  SVal FieldLoc;

  // Evaluate the initializer, if necessary
  if (BMI->isAnyMemberInitializer()) {
    // Constructors build the object directly in the field,
    // but non-objects must be copied in from the initializer.
    if (getObjectUnderConstruction(State, BMI, LC)) {
      // The field was directly constructed, so there is no need to bind.
      // But we still need to stop tracking the object under construction.
      State = finishObjectConstruction(State, BMI, LC);
      NodeBuilder Bldr(Pred, Tmp, *currBldrCtx);
      PostStore PS(Init, LC, /*Loc*/ nullptr, /*tag*/ nullptr);
      Bldr.generateNode(PS, State, Pred);
    } else {
      const ValueDecl *Field;
      if (BMI->isIndirectMemberInitializer()) {
        Field = BMI->getIndirectMember();
        FieldLoc = State->getLValue(BMI->getIndirectMember(), thisVal);
      } else {
        Field = BMI->getMember();
        FieldLoc = State->getLValue(BMI->getMember(), thisVal);
      }

      SVal InitVal;
      if (Init->getType()->isArrayType()) {
        // Handle arrays of trivial type. We can represent this with a
        // primitive load/copy from the base array region.
        const ArraySubscriptExpr *ASE;
        while ((ASE = dyn_cast<ArraySubscriptExpr>(Init)))
          Init = ASE->getBase()->IgnoreImplicit();

        SVal LValue = State->getSVal(Init, stackFrame);
        if (!Field->getType()->isReferenceType())
          if (Optional<Loc> LValueLoc = LValue.getAs<Loc>())
            InitVal = State->getSVal(*LValueLoc);

        // If we fail to get the value for some reason, use a symbolic value.
        if (InitVal.isUnknownOrUndef()) {
          SValBuilder &SVB = getSValBuilder();
          InitVal = SVB.conjureSymbolVal(BMI->getInit(), stackFrame,
                                         Field->getType(),
                                         currBldrCtx->blockCount());
        }
      } else {
        InitVal = State->getSVal(BMI->getInit(), stackFrame);
      }

      PostInitializer PP(BMI, FieldLoc.getAsRegion(), stackFrame);
      evalBind(Tmp, Init, Pred, FieldLoc, InitVal, /*isInit=*/true, &PP);
    }
  } else {
    assert(BMI->isBaseInitializer() || BMI->isDelegatingInitializer());
    Tmp.insert(Pred);
    // We already did all the work when visiting the CXXConstructExpr.
  }

  // Construct PostInitializer nodes whether the state changed or not,
  // so that the diagnostics don't get confused.
  PostInitializer PP(BMI, FieldLoc.getAsRegion(), stackFrame);
  ExplodedNodeSet Dst;
  NodeBuilder Bldr(Tmp, Dst, *currBldrCtx);
  for (const auto I : Tmp) {
    ProgramStateRef State = I->getState();
    Bldr.generateNode(PP, State, I);
  }

  // Enqueue the new nodes onto the work list.
  Engine.enqueue(Dst, currBldrCtx->getBlock(), currStmtIdx);
}

void ExprEngine::ProcessImplicitDtor(const CFGImplicitDtor D,
                                     ExplodedNode *Pred) {
  ExplodedNodeSet Dst;
  switch (D.getKind()) {
  case CFGElement::AutomaticObjectDtor:
    ProcessAutomaticObjDtor(D.castAs<CFGAutomaticObjDtor>(), Pred, Dst);
    break;
  case CFGElement::BaseDtor:
    ProcessBaseDtor(D.castAs<CFGBaseDtor>(), Pred, Dst);
    break;
  case CFGElement::MemberDtor:
    ProcessMemberDtor(D.castAs<CFGMemberDtor>(), Pred, Dst);
    break;
  case CFGElement::TemporaryDtor:
    ProcessTemporaryDtor(D.castAs<CFGTemporaryDtor>(), Pred, Dst);
    break;
  case CFGElement::DeleteDtor:
    ProcessDeleteDtor(D.castAs<CFGDeleteDtor>(), Pred, Dst);
    break;
  default:
    llvm_unreachable("Unexpected dtor kind.");
  }

  // Enqueue the new nodes onto the work list.
  Engine.enqueue(Dst, currBldrCtx->getBlock(), currStmtIdx);
}

void ExprEngine::ProcessNewAllocator(const CXXNewExpr *NE,
                                     ExplodedNode *Pred) {
  ExplodedNodeSet Dst;
  AnalysisManager &AMgr = getAnalysisManager();
  AnalyzerOptions &Opts = AMgr.options;
  // TODO: We're not evaluating allocators for all cases just yet as
  // we're not handling the return value correctly, which causes false
  // positives when the alpha.cplusplus.NewDeleteLeaks check is on.
  if (Opts.MayInlineCXXAllocator)
    VisitCXXNewAllocatorCall(NE, Pred, Dst);
  else {
    NodeBuilder Bldr(Pred, Dst, *currBldrCtx);
    const LocationContext *LCtx = Pred->getLocationContext();
    PostImplicitCall PP(NE->getOperatorNew(), NE->getBeginLoc(), LCtx);
    Bldr.generateNode(PP, Pred->getState(), Pred);
  }
  Engine.enqueue(Dst, currBldrCtx->getBlock(), currStmtIdx);
}

void ExprEngine::ProcessAutomaticObjDtor(const CFGAutomaticObjDtor Dtor,
                                         ExplodedNode *Pred,
                                         ExplodedNodeSet &Dst) {
  const VarDecl *varDecl = Dtor.getVarDecl();
  QualType varType = varDecl->getType();

  ProgramStateRef state = Pred->getState();
  SVal dest = state->getLValue(varDecl, Pred->getLocationContext());
  const MemRegion *Region = dest.castAs<loc::MemRegionVal>().getRegion();

  if (varType->isReferenceType()) {
    const MemRegion *ValueRegion = state->getSVal(Region).getAsRegion();
    if (!ValueRegion) {
      // FIXME: This should not happen. The language guarantees a presence
      // of a valid initializer here, so the reference shall not be undefined.
      // It seems that we're calling destructors over variables that
      // were not initialized yet.
      return;
    }
    Region = ValueRegion->getBaseRegion();
    varType = cast<TypedValueRegion>(Region)->getValueType();
  }

  // FIXME: We need to run the same destructor on every element of the array.
  // This workaround will just run the first destructor (which will still
  // invalidate the entire array).
  EvalCallOptions CallOpts;
  Region = makeZeroElementRegion(state, loc::MemRegionVal(Region), varType,
                                 CallOpts.IsArrayCtorOrDtor).getAsRegion();

  VisitCXXDestructor(varType, Region, Dtor.getTriggerStmt(), /*IsBase=*/ false,
                     Pred, Dst, CallOpts);
}

void ExprEngine::ProcessDeleteDtor(const CFGDeleteDtor Dtor,
                                   ExplodedNode *Pred,
                                   ExplodedNodeSet &Dst) {
  ProgramStateRef State = Pred->getState();
  const LocationContext *LCtx = Pred->getLocationContext();
  const CXXDeleteExpr *DE = Dtor.getDeleteExpr();
  const Stmt *Arg = DE->getArgument();
  QualType DTy = DE->getDestroyedType();
  SVal ArgVal = State->getSVal(Arg, LCtx);

  // If the argument to delete is known to be a null value,
  // don't run destructor.
  if (State->isNull(ArgVal).isConstrainedTrue()) {
    QualType BTy = getContext().getBaseElementType(DTy);
    const CXXRecordDecl *RD = BTy->getAsCXXRecordDecl();
    const CXXDestructorDecl *Dtor = RD->getDestructor();

    PostImplicitCall PP(Dtor, DE->getBeginLoc(), LCtx);
    NodeBuilder Bldr(Pred, Dst, *currBldrCtx);
    Bldr.generateNode(PP, Pred->getState(), Pred);
    return;
  }

  EvalCallOptions CallOpts;
  const MemRegion *ArgR = ArgVal.getAsRegion();
  if (DE->isArrayForm()) {
    // FIXME: We need to run the same destructor on every element of the array.
    // This workaround will just run the first destructor (which will still
    // invalidate the entire array).
    CallOpts.IsArrayCtorOrDtor = true;
    // Yes, it may even be a multi-dimensional array.
    while (const auto *AT = getContext().getAsArrayType(DTy))
      DTy = AT->getElementType();
    if (ArgR)
      ArgR = getStoreManager().GetElementZeroRegion(cast<SubRegion>(ArgR), DTy);
  }

  VisitCXXDestructor(DTy, ArgR, DE, /*IsBase=*/false, Pred, Dst, CallOpts);
}

void ExprEngine::ProcessBaseDtor(const CFGBaseDtor D,
                                 ExplodedNode *Pred, ExplodedNodeSet &Dst) {
  const LocationContext *LCtx = Pred->getLocationContext();

  const auto *CurDtor = cast<CXXDestructorDecl>(LCtx->getDecl());
  Loc ThisPtr = getSValBuilder().getCXXThis(CurDtor,
                                            LCtx->getStackFrame());
  SVal ThisVal = Pred->getState()->getSVal(ThisPtr);

  // Create the base object region.
  const CXXBaseSpecifier *Base = D.getBaseSpecifier();
  QualType BaseTy = Base->getType();
  SVal BaseVal = getStoreManager().evalDerivedToBase(ThisVal, BaseTy,
                                                     Base->isVirtual());

  VisitCXXDestructor(BaseTy, BaseVal.castAs<loc::MemRegionVal>().getRegion(),
                     CurDtor->getBody(), /*IsBase=*/ true, Pred, Dst, {});
}

void ExprEngine::ProcessMemberDtor(const CFGMemberDtor D,
                                   ExplodedNode *Pred, ExplodedNodeSet &Dst) {
  const FieldDecl *Member = D.getFieldDecl();
  QualType T = Member->getType();
  ProgramStateRef State = Pred->getState();
  const LocationContext *LCtx = Pred->getLocationContext();

  const auto *CurDtor = cast<CXXDestructorDecl>(LCtx->getDecl());
  Loc ThisVal = getSValBuilder().getCXXThis(CurDtor,
                                            LCtx->getStackFrame());
  SVal FieldVal =
      State->getLValue(Member, State->getSVal(ThisVal).castAs<Loc>());

  // FIXME: We need to run the same destructor on every element of the array.
  // This workaround will just run the first destructor (which will still
  // invalidate the entire array).
  EvalCallOptions CallOpts;
  FieldVal = makeZeroElementRegion(State, FieldVal, T,
                                   CallOpts.IsArrayCtorOrDtor);

  VisitCXXDestructor(T, FieldVal.castAs<loc::MemRegionVal>().getRegion(),
                     CurDtor->getBody(), /*IsBase=*/false, Pred, Dst, CallOpts);
}

void ExprEngine::ProcessTemporaryDtor(const CFGTemporaryDtor D,
                                      ExplodedNode *Pred,
                                      ExplodedNodeSet &Dst) {
  const CXXBindTemporaryExpr *BTE = D.getBindTemporaryExpr();
  ProgramStateRef State = Pred->getState();
  const LocationContext *LC = Pred->getLocationContext();
  const MemRegion *MR = nullptr;

  if (Optional<SVal> V =
          getObjectUnderConstruction(State, D.getBindTemporaryExpr(),
                                     Pred->getLocationContext())) {
    // FIXME: Currently we insert temporary destructors for default parameters,
    // but we don't insert the constructors, so the entry in
    // ObjectsUnderConstruction may be missing.
    State = finishObjectConstruction(State, D.getBindTemporaryExpr(),
                                     Pred->getLocationContext());
    MR = V->getAsRegion();
  }

  // If copy elision has occurred, and the constructor corresponding to the
  // destructor was elided, we need to skip the destructor as well.
  if (isDestructorElided(State, BTE, LC)) {
    State = cleanupElidedDestructor(State, BTE, LC);
    NodeBuilder Bldr(Pred, Dst, *currBldrCtx);
    PostImplicitCall PP(D.getDestructorDecl(getContext()),
                        D.getBindTemporaryExpr()->getBeginLoc(),
                        Pred->getLocationContext());
    Bldr.generateNode(PP, State, Pred);
    return;
  }

  ExplodedNodeSet CleanDtorState;
  StmtNodeBuilder StmtBldr(Pred, CleanDtorState, *currBldrCtx);
  StmtBldr.generateNode(D.getBindTemporaryExpr(), Pred, State);

  QualType T = D.getBindTemporaryExpr()->getSubExpr()->getType();
  // FIXME: Currently CleanDtorState can be empty here due to temporaries being
  // bound to default parameters.
  assert(CleanDtorState.size() <= 1);
  ExplodedNode *CleanPred =
      CleanDtorState.empty() ? Pred : *CleanDtorState.begin();

  EvalCallOptions CallOpts;
  CallOpts.IsTemporaryCtorOrDtor = true;
  if (!MR) {
    CallOpts.IsCtorOrDtorWithImproperlyModeledTargetRegion = true;

    // If we have no MR, we still need to unwrap the array to avoid destroying
    // the whole array at once. Regardless, we'd eventually need to model array
    // destructors properly, element-by-element.
    while (const ArrayType *AT = getContext().getAsArrayType(T)) {
      T = AT->getElementType();
      CallOpts.IsArrayCtorOrDtor = true;
    }
  } else {
    // We'd eventually need to makeZeroElementRegion() trick here,
    // but for now we don't have the respective construction contexts,
    // so MR would always be null in this case. Do nothing for now.
  }
  VisitCXXDestructor(T, MR, D.getBindTemporaryExpr(),
                     /*IsBase=*/false, CleanPred, Dst, CallOpts);
}

void ExprEngine::processCleanupTemporaryBranch(const CXXBindTemporaryExpr *BTE,
                                               NodeBuilderContext &BldCtx,
                                               ExplodedNode *Pred,
                                               ExplodedNodeSet &Dst,
                                               const CFGBlock *DstT,
                                               const CFGBlock *DstF) {
  BranchNodeBuilder TempDtorBuilder(Pred, Dst, BldCtx, DstT, DstF);
  ProgramStateRef State = Pred->getState();
  const LocationContext *LC = Pred->getLocationContext();
  if (getObjectUnderConstruction(State, BTE, LC)) {
    TempDtorBuilder.markInfeasible(false);
    TempDtorBuilder.generateNode(State, true, Pred);
  } else {
    TempDtorBuilder.markInfeasible(true);
    TempDtorBuilder.generateNode(State, false, Pred);
  }
}

void ExprEngine::VisitCXXBindTemporaryExpr(const CXXBindTemporaryExpr *BTE,
                                           ExplodedNodeSet &PreVisit,
                                           ExplodedNodeSet &Dst) {
  // This is a fallback solution in case we didn't have a construction
  // context when we were constructing the temporary. Otherwise the map should
  // have been populated there.
  if (!getAnalysisManager().options.ShouldIncludeTemporaryDtorsInCFG) {
    // In case we don't have temporary destructors in the CFG, do not mark
    // the initialization - we would otherwise never clean it up.
    Dst = PreVisit;
    return;
  }
  StmtNodeBuilder StmtBldr(PreVisit, Dst, *currBldrCtx);
  for (ExplodedNode *Node : PreVisit) {
    ProgramStateRef State = Node->getState();
    const LocationContext *LC = Node->getLocationContext();
    if (!getObjectUnderConstruction(State, BTE, LC)) {
      // FIXME: Currently the state might also already contain the marker due to
      // incorrect handling of temporaries bound to default parameters; for
      // those, we currently skip the CXXBindTemporaryExpr but rely on adding
      // temporary destructor nodes.
      State = addObjectUnderConstruction(State, BTE, LC, UnknownVal());
    }
    StmtBldr.generateNode(BTE, Node, State);
  }
}

ProgramStateRef ExprEngine::escapeValue(ProgramStateRef State, SVal V,
                                        PointerEscapeKind K) const {
  class CollectReachableSymbolsCallback final : public SymbolVisitor {
    InvalidatedSymbols Symbols;

  public:
    explicit CollectReachableSymbolsCallback(ProgramStateRef) {}

    const InvalidatedSymbols &getSymbols() const { return Symbols; }

    bool VisitSymbol(SymbolRef Sym) override {
      Symbols.insert(Sym);
      return true;
    }
  };

  const CollectReachableSymbolsCallback &Scanner =
      State->scanReachableSymbols<CollectReachableSymbolsCallback>(V);
  return getCheckerManager().runCheckersForPointerEscape(
      State, Scanner.getSymbols(), /*CallEvent*/ nullptr, K, nullptr);
}

void ExprEngine::Visit(const Stmt *S, ExplodedNode *Pred,
                       ExplodedNodeSet &DstTop) {
  PrettyStackTraceLoc CrashInfo(getContext().getSourceManager(),
                                S->getBeginLoc(), "Error evaluating statement");
  ExplodedNodeSet Dst;
  StmtNodeBuilder Bldr(Pred, DstTop, *currBldrCtx);

  assert(!isa<Expr>(S) || S == cast<Expr>(S)->IgnoreParens());

  switch (S->getStmtClass()) {
    // C++, OpenMP and ARC stuff we don't support yet.
    case Expr::ObjCIndirectCopyRestoreExprClass:
    case Stmt::CXXDependentScopeMemberExprClass:
    case Stmt::CXXInheritedCtorInitExprClass:
    case Stmt::CXXTryStmtClass:
    case Stmt::CXXTypeidExprClass:
    case Stmt::CXXUuidofExprClass:
    case Stmt::CXXFoldExprClass:
    case Stmt::MSPropertyRefExprClass:
    case Stmt::MSPropertySubscriptExprClass:
    case Stmt::CXXUnresolvedConstructExprClass:
    case Stmt::DependentScopeDeclRefExprClass:
    case Stmt::ArrayTypeTraitExprClass:
    case Stmt::ExpressionTraitExprClass:
    case Stmt::UnresolvedLookupExprClass:
    case Stmt::UnresolvedMemberExprClass:
    case Stmt::TypoExprClass:
    case Stmt::CXXNoexceptExprClass:
    case Stmt::PackExpansionExprClass:
    case Stmt::SubstNonTypeTemplateParmPackExprClass:
    case Stmt::FunctionParmPackExprClass:
    case Stmt::CoroutineBodyStmtClass:
    case Stmt::CoawaitExprClass:
    case Stmt::DependentCoawaitExprClass:
    case Stmt::CoreturnStmtClass:
    case Stmt::CoyieldExprClass:
    case Stmt::SEHTryStmtClass:
    case Stmt::SEHExceptStmtClass:
    case Stmt::SEHLeaveStmtClass:
    case Stmt::SEHFinallyStmtClass:
    case Stmt::OMPParallelDirectiveClass:
    case Stmt::OMPSimdDirectiveClass:
    case Stmt::OMPForDirectiveClass:
    case Stmt::OMPForSimdDirectiveClass:
    case Stmt::OMPSectionsDirectiveClass:
    case Stmt::OMPSectionDirectiveClass:
    case Stmt::OMPSingleDirectiveClass:
    case Stmt::OMPMasterDirectiveClass:
    case Stmt::OMPCriticalDirectiveClass:
    case Stmt::OMPParallelForDirectiveClass:
    case Stmt::OMPParallelForSimdDirectiveClass:
    case Stmt::OMPParallelSectionsDirectiveClass:
    case Stmt::OMPTaskDirectiveClass:
    case Stmt::OMPTaskyieldDirectiveClass:
    case Stmt::OMPBarrierDirectiveClass:
    case Stmt::OMPTaskwaitDirectiveClass:
    case Stmt::OMPTaskgroupDirectiveClass:
    case Stmt::OMPFlushDirectiveClass:
    case Stmt::OMPOrderedDirectiveClass:
    case Stmt::OMPAtomicDirectiveClass:
    case Stmt::OMPTargetDirectiveClass:
    case Stmt::OMPTargetDataDirectiveClass:
    case Stmt::OMPTargetEnterDataDirectiveClass:
    case Stmt::OMPTargetExitDataDirectiveClass:
    case Stmt::OMPTargetParallelDirectiveClass:
    case Stmt::OMPTargetParallelForDirectiveClass:
    case Stmt::OMPTargetUpdateDirectiveClass:
    case Stmt::OMPTeamsDirectiveClass:
    case Stmt::OMPCancellationPointDirectiveClass:
    case Stmt::OMPCancelDirectiveClass:
    case Stmt::OMPTaskLoopDirectiveClass:
    case Stmt::OMPTaskLoopSimdDirectiveClass:
    case Stmt::OMPDistributeDirectiveClass:
    case Stmt::OMPDistributeParallelForDirectiveClass:
    case Stmt::OMPDistributeParallelForSimdDirectiveClass:
    case Stmt::OMPDistributeSimdDirectiveClass:
    case Stmt::OMPTargetParallelForSimdDirectiveClass:
    case Stmt::OMPTargetSimdDirectiveClass:
    case Stmt::OMPTeamsDistributeDirectiveClass:
    case Stmt::OMPTeamsDistributeSimdDirectiveClass:
    case Stmt::OMPTeamsDistributeParallelForSimdDirectiveClass:
    case Stmt::OMPTeamsDistributeParallelForDirectiveClass:
    case Stmt::OMPTargetTeamsDirectiveClass:
    case Stmt::OMPTargetTeamsDistributeDirectiveClass:
    case Stmt::OMPTargetTeamsDistributeParallelForDirectiveClass:
    case Stmt::OMPTargetTeamsDistributeParallelForSimdDirectiveClass:
    case Stmt::OMPTargetTeamsDistributeSimdDirectiveClass:
    case Stmt::CapturedStmtClass: {
      const ExplodedNode *node = Bldr.generateSink(S, Pred, Pred->getState());
      Engine.addAbortedBlock(node, currBldrCtx->getBlock());
      break;
    }

    case Stmt::ParenExprClass:
      llvm_unreachable("ParenExprs already handled.");
    case Stmt::GenericSelectionExprClass:
      llvm_unreachable("GenericSelectionExprs already handled.");
    // Cases that should never be evaluated simply because they shouldn't
    // appear in the CFG.
    case Stmt::BreakStmtClass:
    case Stmt::CaseStmtClass:
    case Stmt::CompoundStmtClass:
    case Stmt::ContinueStmtClass:
    case Stmt::CXXForRangeStmtClass:
    case Stmt::DefaultStmtClass:
    case Stmt::DoStmtClass:
    case Stmt::ForStmtClass:
    case Stmt::GotoStmtClass:
    case Stmt::IfStmtClass:
    case Stmt::IndirectGotoStmtClass:
    case Stmt::LabelStmtClass:
    case Stmt::NoStmtClass:
    case Stmt::NullStmtClass:
    case Stmt::SwitchStmtClass:
    case Stmt::WhileStmtClass:
    case Expr::MSDependentExistsStmtClass:
      llvm_unreachable("Stmt should not be in analyzer evaluation loop");

    case Stmt::ObjCSubscriptRefExprClass:
    case Stmt::ObjCPropertyRefExprClass:
      llvm_unreachable("These are handled by PseudoObjectExpr");

    case Stmt::GNUNullExprClass: {
      // GNU __null is a pointer-width integer, not an actual pointer.
      ProgramStateRef state = Pred->getState();
      state = state->BindExpr(S, Pred->getLocationContext(),
                              svalBuilder.makeIntValWithPtrWidth(0, false));
      Bldr.generateNode(S, Pred, state);
      break;
    }

    case Stmt::ObjCAtSynchronizedStmtClass:
      Bldr.takeNodes(Pred);
      VisitObjCAtSynchronizedStmt(cast<ObjCAtSynchronizedStmt>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Expr::ConstantExprClass:
    case Stmt::ExprWithCleanupsClass:
      // Handled due to fully linearised CFG.
      break;

    case Stmt::CXXBindTemporaryExprClass: {
      Bldr.takeNodes(Pred);
      ExplodedNodeSet PreVisit;
      getCheckerManager().runCheckersForPreStmt(PreVisit, Pred, S, *this);
      ExplodedNodeSet Next;
      VisitCXXBindTemporaryExpr(cast<CXXBindTemporaryExpr>(S), PreVisit, Next);
      getCheckerManager().runCheckersForPostStmt(Dst, Next, S, *this);
      Bldr.addNodes(Dst);
      break;
    }

    // Cases not handled yet; but will handle some day.
    case Stmt::DesignatedInitExprClass:
    case Stmt::DesignatedInitUpdateExprClass:
    case Stmt::ArrayInitLoopExprClass:
    case Stmt::ArrayInitIndexExprClass:
    case Stmt::ExtVectorElementExprClass:
    case Stmt::ImaginaryLiteralClass:
    case Stmt::ObjCAtCatchStmtClass:
    case Stmt::ObjCAtFinallyStmtClass:
    case Stmt::ObjCAtTryStmtClass:
    case Stmt::ObjCAutoreleasePoolStmtClass:
    case Stmt::ObjCEncodeExprClass:
    case Stmt::ObjCIsaExprClass:
    case Stmt::ObjCProtocolExprClass:
    case Stmt::ObjCSelectorExprClass:
    case Stmt::ParenListExprClass:
    case Stmt::ShuffleVectorExprClass:
    case Stmt::ConvertVectorExprClass:
    case Stmt::VAArgExprClass:
    case Stmt::CUDAKernelCallExprClass:
    case Stmt::OpaqueValueExprClass:
    case Stmt::AsTypeExprClass:
      // Fall through.

    // Cases we intentionally don't evaluate, since they don't need
    // to be explicitly evaluated.
    case Stmt::PredefinedExprClass:
    case Stmt::AddrLabelExprClass:
    case Stmt::AttributedStmtClass:
    case Stmt::IntegerLiteralClass:
    case Stmt::FixedPointLiteralClass:
    case Stmt::CharacterLiteralClass:
    case Stmt::ImplicitValueInitExprClass:
    case Stmt::CXXScalarValueInitExprClass:
    case Stmt::CXXBoolLiteralExprClass:
    case Stmt::ObjCBoolLiteralExprClass:
    case Stmt::ObjCAvailabilityCheckExprClass:
    case Stmt::FloatingLiteralClass:
    case Stmt::NoInitExprClass:
    case Stmt::SizeOfPackExprClass:
    case Stmt::StringLiteralClass:
    case Stmt::ObjCStringLiteralClass:
    case Stmt::CXXPseudoDestructorExprClass:
    case Stmt::SubstNonTypeTemplateParmExprClass:
    case Stmt::CXXNullPtrLiteralExprClass:
    case Stmt::OMPArraySectionExprClass:
    case Stmt::TypeTraitExprClass: {
      Bldr.takeNodes(Pred);
      ExplodedNodeSet preVisit;
      getCheckerManager().runCheckersForPreStmt(preVisit, Pred, S, *this);
      getCheckerManager().runCheckersForPostStmt(Dst, preVisit, S, *this);
      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::CXXDefaultArgExprClass:
    case Stmt::CXXDefaultInitExprClass: {
      Bldr.takeNodes(Pred);
      ExplodedNodeSet PreVisit;
      getCheckerManager().runCheckersForPreStmt(PreVisit, Pred, S, *this);

      ExplodedNodeSet Tmp;
      StmtNodeBuilder Bldr2(PreVisit, Tmp, *currBldrCtx);

      const Expr *ArgE;
      if (const auto *DefE = dyn_cast<CXXDefaultArgExpr>(S))
        ArgE = DefE->getExpr();
      else if (const auto *DefE = dyn_cast<CXXDefaultInitExpr>(S))
        ArgE = DefE->getExpr();
      else
        llvm_unreachable("unknown constant wrapper kind");

      bool IsTemporary = false;
      if (const auto *MTE = dyn_cast<MaterializeTemporaryExpr>(ArgE)) {
        ArgE = MTE->GetTemporaryExpr();
        IsTemporary = true;
      }

      Optional<SVal> ConstantVal = svalBuilder.getConstantVal(ArgE);
      if (!ConstantVal)
        ConstantVal = UnknownVal();

      const LocationContext *LCtx = Pred->getLocationContext();
      for (const auto I : PreVisit) {
        ProgramStateRef State = I->getState();
        State = State->BindExpr(S, LCtx, *ConstantVal);
        if (IsTemporary)
          State = createTemporaryRegionIfNeeded(State, LCtx,
                                                cast<Expr>(S),
                                                cast<Expr>(S));
        Bldr2.generateNode(S, I, State);
      }

      getCheckerManager().runCheckersForPostStmt(Dst, Tmp, S, *this);
      Bldr.addNodes(Dst);
      break;
    }

    // Cases we evaluate as opaque expressions, conjuring a symbol.
    case Stmt::CXXStdInitializerListExprClass:
    case Expr::ObjCArrayLiteralClass:
    case Expr::ObjCDictionaryLiteralClass:
    case Expr::ObjCBoxedExprClass: {
      Bldr.takeNodes(Pred);

      ExplodedNodeSet preVisit;
      getCheckerManager().runCheckersForPreStmt(preVisit, Pred, S, *this);

      ExplodedNodeSet Tmp;
      StmtNodeBuilder Bldr2(preVisit, Tmp, *currBldrCtx);

      const auto *Ex = cast<Expr>(S);
      QualType resultType = Ex->getType();

      for (const auto N : preVisit) {
        const LocationContext *LCtx = N->getLocationContext();
        SVal result = svalBuilder.conjureSymbolVal(nullptr, Ex, LCtx,
                                                   resultType,
                                                   currBldrCtx->blockCount());
        ProgramStateRef State = N->getState()->BindExpr(Ex, LCtx, result);

        // Escape pointers passed into the list, unless it's an ObjC boxed
        // expression which is not a boxable C structure.
        if (!(isa<ObjCBoxedExpr>(Ex) &&
              !cast<ObjCBoxedExpr>(Ex)->getSubExpr()
                                      ->getType()->isRecordType()))
          for (auto Child : Ex->children()) {
            assert(Child);
            SVal Val = State->getSVal(Child, LCtx);
            State = escapeValue(State, Val, PSK_EscapeOther);
          }

        Bldr2.generateNode(S, N, State);
      }

      getCheckerManager().runCheckersForPostStmt(Dst, Tmp, S, *this);
      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::ArraySubscriptExprClass:
      Bldr.takeNodes(Pred);
      VisitArraySubscriptExpr(cast<ArraySubscriptExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::GCCAsmStmtClass:
      Bldr.takeNodes(Pred);
      VisitGCCAsmStmt(cast<GCCAsmStmt>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::MSAsmStmtClass:
      Bldr.takeNodes(Pred);
      VisitMSAsmStmt(cast<MSAsmStmt>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::BlockExprClass:
      Bldr.takeNodes(Pred);
      VisitBlockExpr(cast<BlockExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::LambdaExprClass:
      if (AMgr.options.ShouldInlineLambdas) {
        Bldr.takeNodes(Pred);
        VisitLambdaExpr(cast<LambdaExpr>(S), Pred, Dst);
        Bldr.addNodes(Dst);
      } else {
        const ExplodedNode *node = Bldr.generateSink(S, Pred, Pred->getState());
        Engine.addAbortedBlock(node, currBldrCtx->getBlock());
      }
      break;

    case Stmt::BinaryOperatorClass: {
      const auto *B = cast<BinaryOperator>(S);
      if (B->isLogicalOp()) {
        Bldr.takeNodes(Pred);
        VisitLogicalExpr(B, Pred, Dst);
        Bldr.addNodes(Dst);
        break;
      }
      else if (B->getOpcode() == BO_Comma) {
        ProgramStateRef state = Pred->getState();
        Bldr.generateNode(B, Pred,
                          state->BindExpr(B, Pred->getLocationContext(),
                                          state->getSVal(B->getRHS(),
                                                  Pred->getLocationContext())));
        break;
      }

      Bldr.takeNodes(Pred);

      if (AMgr.options.ShouldEagerlyAssume &&
          (B->isRelationalOp() || B->isEqualityOp())) {
        ExplodedNodeSet Tmp;
        VisitBinaryOperator(cast<BinaryOperator>(S), Pred, Tmp);
        evalEagerlyAssumeBinOpBifurcation(Dst, Tmp, cast<Expr>(S));
      }
      else
        VisitBinaryOperator(cast<BinaryOperator>(S), Pred, Dst);

      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::CXXOperatorCallExprClass: {
      const auto *OCE = cast<CXXOperatorCallExpr>(S);

      // For instance method operators, make sure the 'this' argument has a
      // valid region.
      const Decl *Callee = OCE->getCalleeDecl();
      if (const auto *MD = dyn_cast_or_null<CXXMethodDecl>(Callee)) {
        if (MD->isInstance()) {
          ProgramStateRef State = Pred->getState();
          const LocationContext *LCtx = Pred->getLocationContext();
          ProgramStateRef NewState =
            createTemporaryRegionIfNeeded(State, LCtx, OCE->getArg(0));
          if (NewState != State) {
            Pred = Bldr.generateNode(OCE, Pred, NewState, /*Tag=*/nullptr,
                                     ProgramPoint::PreStmtKind);
            // Did we cache out?
            if (!Pred)
              break;
          }
        }
      }
      // FALLTHROUGH
      LLVM_FALLTHROUGH;
    }

    case Stmt::CallExprClass:
    case Stmt::CXXMemberCallExprClass:
    case Stmt::UserDefinedLiteralClass:
      Bldr.takeNodes(Pred);
      VisitCallExpr(cast<CallExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::CXXCatchStmtClass:
      Bldr.takeNodes(Pred);
      VisitCXXCatchStmt(cast<CXXCatchStmt>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::CXXTemporaryObjectExprClass:
    case Stmt::CXXConstructExprClass:
      Bldr.takeNodes(Pred);
      VisitCXXConstructExpr(cast<CXXConstructExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::CXXNewExprClass: {
      Bldr.takeNodes(Pred);

      ExplodedNodeSet PreVisit;
      getCheckerManager().runCheckersForPreStmt(PreVisit, Pred, S, *this);

      ExplodedNodeSet PostVisit;
      for (const auto i : PreVisit)
        VisitCXXNewExpr(cast<CXXNewExpr>(S), i, PostVisit);

      getCheckerManager().runCheckersForPostStmt(Dst, PostVisit, S, *this);
      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::CXXDeleteExprClass: {
      Bldr.takeNodes(Pred);
      ExplodedNodeSet PreVisit;
      const auto *CDE = cast<CXXDeleteExpr>(S);
      getCheckerManager().runCheckersForPreStmt(PreVisit, Pred, S, *this);

      for (const auto i : PreVisit)
        VisitCXXDeleteExpr(CDE, i, Dst);

      Bldr.addNodes(Dst);
      break;
    }
      // FIXME: ChooseExpr is really a constant.  We need to fix
      //        the CFG do not model them as explicit control-flow.

    case Stmt::ChooseExprClass: { // __builtin_choose_expr
      Bldr.takeNodes(Pred);
      const auto *C = cast<ChooseExpr>(S);
      VisitGuardedExpr(C, C->getLHS(), C->getRHS(), Pred, Dst);
      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::CompoundAssignOperatorClass:
      Bldr.takeNodes(Pred);
      VisitBinaryOperator(cast<BinaryOperator>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::CompoundLiteralExprClass:
      Bldr.takeNodes(Pred);
      VisitCompoundLiteralExpr(cast<CompoundLiteralExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::BinaryConditionalOperatorClass:
    case Stmt::ConditionalOperatorClass: { // '?' operator
      Bldr.takeNodes(Pred);
      const auto *C = cast<AbstractConditionalOperator>(S);
      VisitGuardedExpr(C, C->getTrueExpr(), C->getFalseExpr(), Pred, Dst);
      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::CXXThisExprClass:
      Bldr.takeNodes(Pred);
      VisitCXXThisExpr(cast<CXXThisExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::DeclRefExprClass: {
      Bldr.takeNodes(Pred);
      const auto *DE = cast<DeclRefExpr>(S);
      VisitCommonDeclRefExpr(DE, DE->getDecl(), Pred, Dst);
      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::DeclStmtClass:
      Bldr.takeNodes(Pred);
      VisitDeclStmt(cast<DeclStmt>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::ImplicitCastExprClass:
    case Stmt::CStyleCastExprClass:
    case Stmt::CXXStaticCastExprClass:
    case Stmt::CXXDynamicCastExprClass:
    case Stmt::CXXReinterpretCastExprClass:
    case Stmt::CXXConstCastExprClass:
    case Stmt::CXXFunctionalCastExprClass:
    case Stmt::ObjCBridgedCastExprClass: {
      Bldr.takeNodes(Pred);
      const auto *C = cast<CastExpr>(S);
      ExplodedNodeSet dstExpr;
      VisitCast(C, C->getSubExpr(), Pred, dstExpr);

      // Handle the postvisit checks.
      getCheckerManager().runCheckersForPostStmt(Dst, dstExpr, C, *this);
      Bldr.addNodes(Dst);
      break;
    }

    case Expr::MaterializeTemporaryExprClass: {
      Bldr.takeNodes(Pred);
      const auto *MTE = cast<MaterializeTemporaryExpr>(S);
      ExplodedNodeSet dstPrevisit;
      getCheckerManager().runCheckersForPreStmt(dstPrevisit, Pred, MTE, *this);
      ExplodedNodeSet dstExpr;
      for (const auto i : dstPrevisit)
        CreateCXXTemporaryObject(MTE, i, dstExpr);
      getCheckerManager().runCheckersForPostStmt(Dst, dstExpr, MTE, *this);
      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::InitListExprClass:
      Bldr.takeNodes(Pred);
      VisitInitListExpr(cast<InitListExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::MemberExprClass:
      Bldr.takeNodes(Pred);
      VisitMemberExpr(cast<MemberExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::AtomicExprClass:
      Bldr.takeNodes(Pred);
      VisitAtomicExpr(cast<AtomicExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::ObjCIvarRefExprClass:
      Bldr.takeNodes(Pred);
      VisitLvalObjCIvarRefExpr(cast<ObjCIvarRefExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::ObjCForCollectionStmtClass:
      Bldr.takeNodes(Pred);
      VisitObjCForCollectionStmt(cast<ObjCForCollectionStmt>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::ObjCMessageExprClass:
      Bldr.takeNodes(Pred);
      VisitObjCMessage(cast<ObjCMessageExpr>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::ObjCAtThrowStmtClass:
    case Stmt::CXXThrowExprClass:
      // FIXME: This is not complete.  We basically treat @throw as
      // an abort.
      Bldr.generateSink(S, Pred, Pred->getState());
      break;

    case Stmt::ReturnStmtClass:
      Bldr.takeNodes(Pred);
      VisitReturnStmt(cast<ReturnStmt>(S), Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::OffsetOfExprClass: {
      Bldr.takeNodes(Pred);
      ExplodedNodeSet PreVisit;
      getCheckerManager().runCheckersForPreStmt(PreVisit, Pred, S, *this);

      ExplodedNodeSet PostVisit;
      for (const auto Node : PreVisit)
        VisitOffsetOfExpr(cast<OffsetOfExpr>(S), Node, PostVisit);

      getCheckerManager().runCheckersForPostStmt(Dst, PostVisit, S, *this);
      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::UnaryExprOrTypeTraitExprClass:
      Bldr.takeNodes(Pred);
      VisitUnaryExprOrTypeTraitExpr(cast<UnaryExprOrTypeTraitExpr>(S),
                                    Pred, Dst);
      Bldr.addNodes(Dst);
      break;

    case Stmt::StmtExprClass: {
      const auto *SE = cast<StmtExpr>(S);

      if (SE->getSubStmt()->body_empty()) {
        // Empty statement expression.
        assert(SE->getType() == getContext().VoidTy
               && "Empty statement expression must have void type.");
        break;
      }

      if (const auto *LastExpr =
              dyn_cast<Expr>(*SE->getSubStmt()->body_rbegin())) {
        ProgramStateRef state = Pred->getState();
        Bldr.generateNode(SE, Pred,
                          state->BindExpr(SE, Pred->getLocationContext(),
                                          state->getSVal(LastExpr,
                                                  Pred->getLocationContext())));
      }
      break;
    }

    case Stmt::UnaryOperatorClass: {
      Bldr.takeNodes(Pred);
      const auto *U = cast<UnaryOperator>(S);
      if (AMgr.options.ShouldEagerlyAssume && (U->getOpcode() == UO_LNot)) {
        ExplodedNodeSet Tmp;
        VisitUnaryOperator(U, Pred, Tmp);
        evalEagerlyAssumeBinOpBifurcation(Dst, Tmp, U);
      }
      else
        VisitUnaryOperator(U, Pred, Dst);
      Bldr.addNodes(Dst);
      break;
    }

    case Stmt::PseudoObjectExprClass: {
      Bldr.takeNodes(Pred);
      ProgramStateRef state = Pred->getState();
      const auto *PE = cast<PseudoObjectExpr>(S);
      if (const Expr *Result = PE->getResultExpr()) {
        SVal V = state->getSVal(Result, Pred->getLocationContext());
        Bldr.generateNode(S, Pred,
                          state->BindExpr(S, Pred->getLocationContext(), V));
      }
      else
        Bldr.generateNode(S, Pred,
                          state->BindExpr(S, Pred->getLocationContext(),
                                                   UnknownVal()));

      Bldr.addNodes(Dst);
      break;
    }
  }
}

bool ExprEngine::replayWithoutInlining(ExplodedNode *N,
                                       const LocationContext *CalleeLC) {
  const StackFrameContext *CalleeSF = CalleeLC->getStackFrame();
  const StackFrameContext *CallerSF = CalleeSF->getParent()->getStackFrame();
  assert(CalleeSF && CallerSF);
  ExplodedNode *BeforeProcessingCall = nullptr;
  const Stmt *CE = CalleeSF->getCallSite();

  // Find the first node before we started processing the call expression.
  while (N) {
    ProgramPoint L = N->getLocation();
    BeforeProcessingCall = N;
    N = N->pred_empty() ? nullptr : *(N->pred_begin());

    // Skip the nodes corresponding to the inlined code.
    if (L.getStackFrame() != CallerSF)
      continue;
    // We reached the caller. Find the node right before we started
    // processing the call.
    if (L.isPurgeKind())
      continue;
    if (L.getAs<PreImplicitCall>())
      continue;
    if (L.getAs<CallEnter>())
      continue;
    if (Optional<StmtPoint> SP = L.getAs<StmtPoint>())
      if (SP->getStmt() == CE)
        continue;
    break;
  }

  if (!BeforeProcessingCall)
    return false;

  // TODO: Clean up the unneeded nodes.

  // Build an Epsilon node from which we will restart the analyzes.
  // Note that CE is permitted to be NULL!
  ProgramPoint NewNodeLoc =
               EpsilonPoint(BeforeProcessingCall->getLocationContext(), CE);
  // Add the special flag to GDM to signal retrying with no inlining.
  // Note, changing the state ensures that we are not going to cache out.
  ProgramStateRef NewNodeState = BeforeProcessingCall->getState();
  NewNodeState =
    NewNodeState->set<ReplayWithoutInlining>(const_cast<Stmt *>(CE));

  // Make the new node a successor of BeforeProcessingCall.
  bool IsNew = false;
  ExplodedNode *NewNode = G.getNode(NewNodeLoc, NewNodeState, false, &IsNew);
  // We cached out at this point. Caching out is common due to us backtracking
  // from the inlined function, which might spawn several paths.
  if (!IsNew)
    return true;

  NewNode->addPredecessor(BeforeProcessingCall, G);

  // Add the new node to the work list.
  Engine.enqueueStmtNode(NewNode, CalleeSF->getCallSiteBlock(),
                                  CalleeSF->getIndex());
  NumTimesRetriedWithoutInlining++;
  return true;
}

/// Block entrance.  (Update counters).
void ExprEngine::processCFGBlockEntrance(const BlockEdge &L,
                                         NodeBuilderWithSinks &nodeBuilder,
                                         ExplodedNode *Pred) {
  PrettyStackTraceLocationContext CrashInfo(Pred->getLocationContext());
  // If we reach a loop which has a known bound (and meets
  // other constraints) then consider completely unrolling it.
  if(AMgr.options.ShouldUnrollLoops) {
    unsigned maxBlockVisitOnPath = AMgr.options.maxBlockVisitOnPath;
    const Stmt *Term = nodeBuilder.getContext().getBlock()->getTerminator();
    if (Term) {
      ProgramStateRef NewState = updateLoopStack(Term, AMgr.getASTContext(),
                                                 Pred, maxBlockVisitOnPath);
      if (NewState != Pred->getState()) {
        ExplodedNode *UpdatedNode = nodeBuilder.generateNode(NewState, Pred);
        if (!UpdatedNode)
          return;
        Pred = UpdatedNode;
      }
    }
    // Is we are inside an unrolled loop then no need the check the counters.
    if(isUnrolledState(Pred->getState()))
      return;
  }

  // If this block is terminated by a loop and it has already been visited the
  // maximum number of times, widen the loop.
  unsigned int BlockCount = nodeBuilder.getContext().blockCount();
  if (BlockCount == AMgr.options.maxBlockVisitOnPath - 1 &&
      AMgr.options.ShouldWidenLoops) {
    const Stmt *Term = nodeBuilder.getContext().getBlock()->getTerminator();
    if (!(Term &&
          (isa<ForStmt>(Term) || isa<WhileStmt>(Term) || isa<DoStmt>(Term))))
      return;
    // Widen.
    const LocationContext *LCtx = Pred->getLocationContext();
    ProgramStateRef WidenedState =
        getWidenedLoopState(Pred->getState(), LCtx, BlockCount, Term);
    nodeBuilder.generateNode(WidenedState, Pred);
    return;
  }

  // FIXME: Refactor this into a checker.
  if (BlockCount >= AMgr.options.maxBlockVisitOnPath) {
    static SimpleProgramPointTag tag(TagProviderName, "Block count exceeded");
    const ExplodedNode *Sink =
                   nodeBuilder.generateSink(Pred->getState(), Pred, &tag);

    // Check if we stopped at the top level function or not.
    // Root node should have the location context of the top most function.
    const LocationContext *CalleeLC = Pred->getLocation().getLocationContext();
    const LocationContext *CalleeSF = CalleeLC->getStackFrame();
    const LocationContext *RootLC =
                        (*G.roots_begin())->getLocation().getLocationContext();
    if (RootLC->getStackFrame() != CalleeSF) {
      Engine.FunctionSummaries->markReachedMaxBlockCount(CalleeSF->getDecl());

      // Re-run the call evaluation without inlining it, by storing the
      // no-inlining policy in the state and enqueuing the new work item on
      // the list. Replay should almost never fail. Use the stats to catch it
      // if it does.
      if ((!AMgr.options.NoRetryExhausted &&
           replayWithoutInlining(Pred, CalleeLC)))
        return;
      NumMaxBlockCountReachedInInlined++;
    } else
      NumMaxBlockCountReached++;

    // Make sink nodes as exhausted(for stats) only if retry failed.
    Engine.blocksExhausted.push_back(std::make_pair(L, Sink));
  }
}

//===----------------------------------------------------------------------===//
// Branch processing.
//===----------------------------------------------------------------------===//

/// RecoverCastedSymbol - A helper function for ProcessBranch that is used
/// to try to recover some path-sensitivity for casts of symbolic
/// integers that promote their values (which are currently not tracked well).
/// This function returns the SVal bound to Condition->IgnoreCasts if all the
//  cast(s) did was sign-extend the original value.
static SVal RecoverCastedSymbol(ProgramStateRef state,
                                const Stmt *Condition,
                                const LocationContext *LCtx,
                                ASTContext &Ctx) {

  const auto *Ex = dyn_cast<Expr>(Condition);
  if (!Ex)
    return UnknownVal();

  uint64_t bits = 0;
  bool bitsInit = false;

  while (const auto *CE = dyn_cast<CastExpr>(Ex)) {
    QualType T = CE->getType();

    if (!T->isIntegralOrEnumerationType())
      return UnknownVal();

    uint64_t newBits = Ctx.getTypeSize(T);
    if (!bitsInit || newBits < bits) {
      bitsInit = true;
      bits = newBits;
    }

    Ex = CE->getSubExpr();
  }

  // We reached a non-cast.  Is it a symbolic value?
  QualType T = Ex->getType();

  if (!bitsInit || !T->isIntegralOrEnumerationType() ||
      Ctx.getTypeSize(T) > bits)
    return UnknownVal();

  return state->getSVal(Ex, LCtx);
}

#ifndef NDEBUG
static const Stmt *getRightmostLeaf(const Stmt *Condition) {
  while (Condition) {
    const auto *BO = dyn_cast<BinaryOperator>(Condition);
    if (!BO || !BO->isLogicalOp()) {
      return Condition;
    }
    Condition = BO->getRHS()->IgnoreParens();
  }
  return nullptr;
}
#endif

// Returns the condition the branch at the end of 'B' depends on and whose value
// has been evaluated within 'B'.
// In most cases, the terminator condition of 'B' will be evaluated fully in
// the last statement of 'B'; in those cases, the resolved condition is the
// given 'Condition'.
// If the condition of the branch is a logical binary operator tree, the CFG is
// optimized: in that case, we know that the expression formed by all but the
// rightmost leaf of the logical binary operator tree must be true, and thus
// the branch condition is at this point equivalent to the truth value of that
// rightmost leaf; the CFG block thus only evaluates this rightmost leaf
// expression in its final statement. As the full condition in that case was
// not evaluated, and is thus not in the SVal cache, we need to use that leaf
// expression to evaluate the truth value of the condition in the current state
// space.
static const Stmt *ResolveCondition(const Stmt *Condition,
                                    const CFGBlock *B) {
  if (const auto *Ex = dyn_cast<Expr>(Condition))
    Condition = Ex->IgnoreParens();

  const auto *BO = dyn_cast<BinaryOperator>(Condition);
  if (!BO || !BO->isLogicalOp())
    return Condition;

  assert(!B->getTerminator().isTemporaryDtorsBranch() &&
         "Temporary destructor branches handled by processBindTemporary.");

  // For logical operations, we still have the case where some branches
  // use the traditional "merge" approach and others sink the branch
  // directly into the basic blocks representing the logical operation.
  // We need to distinguish between those two cases here.

  // The invariants are still shifting, but it is possible that the
  // last element in a CFGBlock is not a CFGStmt.  Look for the last
  // CFGStmt as the value of the condition.
  CFGBlock::const_reverse_iterator I = B->rbegin(), E = B->rend();
  for (; I != E; ++I) {
    CFGElement Elem = *I;
    Optional<CFGStmt> CS = Elem.getAs<CFGStmt>();
    if (!CS)
      continue;
    const Stmt *LastStmt = CS->getStmt();
    assert(LastStmt == Condition || LastStmt == getRightmostLeaf(Condition));
    return LastStmt;
  }
  llvm_unreachable("could not resolve condition");
}

void ExprEngine::processBranch(const Stmt *Condition,
                               NodeBuilderContext& BldCtx,
                               ExplodedNode *Pred,
                               ExplodedNodeSet &Dst,
                               const CFGBlock *DstT,
                               const CFGBlock *DstF) {
  assert((!Condition || !isa<CXXBindTemporaryExpr>(Condition)) &&
         "CXXBindTemporaryExprs are handled by processBindTemporary.");
  const LocationContext *LCtx = Pred->getLocationContext();
  PrettyStackTraceLocationContext StackCrashInfo(LCtx);
  currBldrCtx = &BldCtx;

  // Check for NULL conditions; e.g. "for(;;)"
  if (!Condition) {
    BranchNodeBuilder NullCondBldr(Pred, Dst, BldCtx, DstT, DstF);
    NullCondBldr.markInfeasible(false);
    NullCondBldr.generateNode(Pred->getState(), true, Pred);
    return;
  }

  if (const auto *Ex = dyn_cast<Expr>(Condition))
    Condition = Ex->IgnoreParens();

  Condition = ResolveCondition(Condition, BldCtx.getBlock());
  PrettyStackTraceLoc CrashInfo(getContext().getSourceManager(),
                                Condition->getBeginLoc(),
                                "Error evaluating branch");

  ExplodedNodeSet CheckersOutSet;
  getCheckerManager().runCheckersForBranchCondition(Condition, CheckersOutSet,
                                                    Pred, *this);
  // We generated only sinks.
  if (CheckersOutSet.empty())
    return;

  BranchNodeBuilder builder(CheckersOutSet, Dst, BldCtx, DstT, DstF);
  for (const auto PredI : CheckersOutSet) {
    if (PredI->isSink())
      continue;

    ProgramStateRef PrevState = PredI->getState();
    SVal X = PrevState->getSVal(Condition, PredI->getLocationContext());

    if (X.isUnknownOrUndef()) {
      // Give it a chance to recover from unknown.
      if (const auto *Ex = dyn_cast<Expr>(Condition)) {
        if (Ex->getType()->isIntegralOrEnumerationType()) {
          // Try to recover some path-sensitivity.  Right now casts of symbolic
          // integers that promote their values are currently not tracked well.
          // If 'Condition' is such an expression, try and recover the
          // underlying value and use that instead.
          SVal recovered = RecoverCastedSymbol(PrevState, Condition,
                                               PredI->getLocationContext(),
                                               getContext());

          if (!recovered.isUnknown()) {
            X = recovered;
          }
        }
      }
    }

    // If the condition is still unknown, give up.
    if (X.isUnknownOrUndef()) {
      builder.generateNode(PrevState, true, PredI);
      builder.generateNode(PrevState, false, PredI);
      continue;
    }

    DefinedSVal V = X.castAs<DefinedSVal>();

    ProgramStateRef StTrue, StFalse;
    std::tie(StTrue, StFalse) = PrevState->assume(V);

    // Process the true branch.
    if (builder.isFeasible(true)) {
      if (StTrue)
        builder.generateNode(StTrue, true, PredI);
      else
        builder.markInfeasible(true);
    }

    // Process the false branch.
    if (builder.isFeasible(false)) {
      if (StFalse)
        builder.generateNode(StFalse, false, PredI);
      else
        builder.markInfeasible(false);
    }
  }
  currBldrCtx = nullptr;
}

/// The GDM component containing the set of global variables which have been
/// previously initialized with explicit initializers.
REGISTER_TRAIT_WITH_PROGRAMSTATE(InitializedGlobalsSet,
                                 llvm::ImmutableSet<const VarDecl *>)

void ExprEngine::processStaticInitializer(const DeclStmt *DS,
                                          NodeBuilderContext &BuilderCtx,
                                          ExplodedNode *Pred,
                                          ExplodedNodeSet &Dst,
                                          const CFGBlock *DstT,
                                          const CFGBlock *DstF) {
  PrettyStackTraceLocationContext CrashInfo(Pred->getLocationContext());
  currBldrCtx = &BuilderCtx;

  const auto *VD = cast<VarDecl>(DS->getSingleDecl());
  ProgramStateRef state = Pred->getState();
  bool initHasRun = state->contains<InitializedGlobalsSet>(VD);
  BranchNodeBuilder builder(Pred, Dst, BuilderCtx, DstT, DstF);

  if (!initHasRun) {
    state = state->add<InitializedGlobalsSet>(VD);
  }

  builder.generateNode(state, initHasRun, Pred);
  builder.markInfeasible(!initHasRun);

  currBldrCtx = nullptr;
}

/// processIndirectGoto - Called by CoreEngine.  Used to generate successor
///  nodes by processing the 'effects' of a computed goto jump.
void ExprEngine::processIndirectGoto(IndirectGotoNodeBuilder &builder) {
  ProgramStateRef state = builder.getState();
  SVal V = state->getSVal(builder.getTarget(), builder.getLocationContext());

  // Three possibilities:
  //
  //   (1) We know the computed label.
  //   (2) The label is NULL (or some other constant), or Undefined.
  //   (3) We have no clue about the label.  Dispatch to all targets.
  //

  using iterator = IndirectGotoNodeBuilder::iterator;

  if (Optional<loc::GotoLabel> LV = V.getAs<loc::GotoLabel>()) {
    const LabelDecl *L = LV->getLabel();

    for (iterator I = builder.begin(), E = builder.end(); I != E; ++I) {
      if (I.getLabel() == L) {
        builder.generateNode(I, state);
        return;
      }
    }

    llvm_unreachable("No block with label.");
  }

  if (V.getAs<loc::ConcreteInt>() || V.getAs<UndefinedVal>()) {
    // Dispatch to the first target and mark it as a sink.
    //ExplodedNode* N = builder.generateNode(builder.begin(), state, true);
    // FIXME: add checker visit.
    //    UndefBranches.insert(N);
    return;
  }

  // This is really a catch-all.  We don't support symbolics yet.
  // FIXME: Implement dispatch for symbolic pointers.

  for (iterator I = builder.begin(), E = builder.end(); I != E; ++I)
    builder.generateNode(I, state);
}

void ExprEngine::processBeginOfFunction(NodeBuilderContext &BC,
                                        ExplodedNode *Pred,
                                        ExplodedNodeSet &Dst,
                                        const BlockEdge &L) {
  SaveAndRestore<const NodeBuilderContext *> NodeContextRAII(currBldrCtx, &BC);
  getCheckerManager().runCheckersForBeginFunction(Dst, L, Pred, *this);
}

/// ProcessEndPath - Called by CoreEngine.  Used to generate end-of-path
///  nodes when the control reaches the end of a function.
void ExprEngine::processEndOfFunction(NodeBuilderContext& BC,
                                      ExplodedNode *Pred,
                                      const ReturnStmt *RS) {
  ProgramStateRef State = Pred->getState();

  if (!Pred->getStackFrame()->inTopFrame())
    State = finishArgumentConstruction(
        State, *getStateManager().getCallEventManager().getCaller(
                   Pred->getStackFrame(), Pred->getState()));

  // FIXME: We currently cannot assert that temporaries are clear, because
  // lifetime extended temporaries are not always modelled correctly. In some
  // cases when we materialize the temporary, we do
  // createTemporaryRegionIfNeeded(), and the region changes, and also the
  // respective destructor becomes automatic from temporary. So for now clean up
  // the state manually before asserting. Ideally, this braced block of code
  // should go away.
  {
    const LocationContext *FromLC = Pred->getLocationContext();
    const LocationContext *ToLC = FromLC->getStackFrame()->getParent();
    const LocationContext *LC = FromLC;
    while (LC != ToLC) {
      assert(LC && "ToLC must be a parent of FromLC!");
      for (auto I : State->get<ObjectsUnderConstruction>())
        if (I.first.getLocationContext() == LC) {
          // The comment above only pardons us for not cleaning up a
          // temporary destructor. If any other statements are found here,
          // it must be a separate problem.
          assert(I.first.getItem().getKind() ==
                     ConstructionContextItem::TemporaryDestructorKind ||
                 I.first.getItem().getKind() ==
                     ConstructionContextItem::ElidedDestructorKind);
          State = State->remove<ObjectsUnderConstruction>(I.first);
        }
      LC = LC->getParent();
    }
  }

  // Perform the transition with cleanups.
  if (State != Pred->getState()) {
    ExplodedNodeSet PostCleanup;
    NodeBuilder Bldr(Pred, PostCleanup, BC);
    Pred = Bldr.generateNode(Pred->getLocation(), State, Pred);
    if (!Pred) {
      // The node with clean temporaries already exists. We might have reached
      // it on a path on which we initialize different temporaries.
      return;
    }
  }

  assert(areAllObjectsFullyConstructed(Pred->getState(),
                                       Pred->getLocationContext(),
                                       Pred->getStackFrame()->getParent()));

  PrettyStackTraceLocationContext CrashInfo(Pred->getLocationContext());
  StateMgr.EndPath(Pred->getState());

  ExplodedNodeSet Dst;
  if (Pred->getLocationContext()->inTopFrame()) {
    // Remove dead symbols.
    ExplodedNodeSet AfterRemovedDead;
    removeDeadOnEndOfFunction(BC, Pred, AfterRemovedDead);

    // Notify checkers.
    for (const auto I : AfterRemovedDead)
      getCheckerManager().runCheckersForEndFunction(BC, Dst, I, *this, RS);
  } else {
    getCheckerManager().runCheckersForEndFunction(BC, Dst, Pred, *this, RS);
  }

  Engine.enqueueEndOfFunction(Dst, RS);
}

/// ProcessSwitch - Called by CoreEngine.  Used to generate successor
///  nodes by processing the 'effects' of a switch statement.
void ExprEngine::processSwitch(SwitchNodeBuilder& builder) {
  using iterator = SwitchNodeBuilder::iterator;

  ProgramStateRef state = builder.getState();
  const Expr *CondE = builder.getCondition();
  SVal  CondV_untested = state->getSVal(CondE, builder.getLocationContext());

  if (CondV_untested.isUndef()) {
    //ExplodedNode* N = builder.generateDefaultCaseNode(state, true);
    // FIXME: add checker
    //UndefBranches.insert(N);

    return;
  }
  DefinedOrUnknownSVal CondV = CondV_untested.castAs<DefinedOrUnknownSVal>();

  ProgramStateRef DefaultSt = state;

  iterator I = builder.begin(), EI = builder.end();
  bool defaultIsFeasible = I == EI;

  for ( ; I != EI; ++I) {
    // Successor may be pruned out during CFG construction.
    if (!I.getBlock())
      continue;

    const CaseStmt *Case = I.getCase();

    // Evaluate the LHS of the case value.
    llvm::APSInt V1 = Case->getLHS()->EvaluateKnownConstInt(getContext());
    assert(V1.getBitWidth() == getContext().getIntWidth(CondE->getType()));

    // Get the RHS of the case, if it exists.
    llvm::APSInt V2;
    if (const Expr *E = Case->getRHS())
      V2 = E->EvaluateKnownConstInt(getContext());
    else
      V2 = V1;

    ProgramStateRef StateCase;
    if (Optional<NonLoc> NL = CondV.getAs<NonLoc>())
      std::tie(StateCase, DefaultSt) =
          DefaultSt->assumeInclusiveRange(*NL, V1, V2);
    else // UnknownVal
      StateCase = DefaultSt;

    if (StateCase)
      builder.generateCaseStmtNode(I, StateCase);

    // Now "assume" that the case doesn't match.  Add this state
    // to the default state (if it is feasible).
    if (DefaultSt)
      defaultIsFeasible = true;
    else {
      defaultIsFeasible = false;
      break;
    }
  }

  if (!defaultIsFeasible)
    return;

  // If we have switch(enum value), the default branch is not
  // feasible if all of the enum constants not covered by 'case:' statements
  // are not feasible values for the switch condition.
  //
  // Note that this isn't as accurate as it could be.  Even if there isn't
  // a case for a particular enum value as long as that enum value isn't
  // feasible then it shouldn't be considered for making 'default:' reachable.
  const SwitchStmt *SS = builder.getSwitch();
  const Expr *CondExpr = SS->getCond()->IgnoreParenImpCasts();
  if (CondExpr->getType()->getAs<EnumType>()) {
    if (SS->isAllEnumCasesCovered())
      return;
  }

  builder.generateDefaultCaseNode(DefaultSt);
}

//===----------------------------------------------------------------------===//
// Transfer functions: Loads and stores.
//===----------------------------------------------------------------------===//

void ExprEngine::VisitCommonDeclRefExpr(const Expr *Ex, const NamedDecl *D,
                                        ExplodedNode *Pred,
                                        ExplodedNodeSet &Dst) {
  StmtNodeBuilder Bldr(Pred, Dst, *currBldrCtx);

  ProgramStateRef state = Pred->getState();
  const LocationContext *LCtx = Pred->getLocationContext();

  if (const auto *VD = dyn_cast<VarDecl>(D)) {
    // C permits "extern void v", and if you cast the address to a valid type,
    // you can even do things with it. We simply pretend
    assert(Ex->isGLValue() || VD->getType()->isVoidType());
    const LocationContext *LocCtxt = Pred->getLocationContext();
    const Decl *D = LocCtxt->getDecl();
    const auto *MD = dyn_cast_or_null<CXXMethodDecl>(D);
    const auto *DeclRefEx = dyn_cast<DeclRefExpr>(Ex);
    Optional<std::pair<SVal, QualType>> VInfo;

    if (AMgr.options.ShouldInlineLambdas && DeclRefEx &&
        DeclRefEx->refersToEnclosingVariableOrCapture() && MD &&
        MD->getParent()->isLambda()) {
      // Lookup the field of the lambda.
      const CXXRecordDecl *CXXRec = MD->getParent();
      llvm::DenseMap<const VarDecl *, FieldDecl *> LambdaCaptureFields;
      FieldDecl *LambdaThisCaptureField;
      CXXRec->getCaptureFields(LambdaCaptureFields, LambdaThisCaptureField);

      // Sema follows a sequence of complex rules to determine whether the
      // variable should be captured.
      if (const FieldDecl *FD = LambdaCaptureFields[VD]) {
        Loc CXXThis =
            svalBuilder.getCXXThis(MD, LocCtxt->getStackFrame());
        SVal CXXThisVal = state->getSVal(CXXThis);
        VInfo = std::make_pair(state->getLValue(FD, CXXThisVal), FD->getType());
      }
    }

    if (!VInfo)
      VInfo = std::make_pair(state->getLValue(VD, LocCtxt), VD->getType());

    SVal V = VInfo->first;
    bool IsReference = VInfo->second->isReferenceType();

    // For references, the 'lvalue' is the pointer address stored in the
    // reference region.
    if (IsReference) {
      if (const MemRegion *R = V.getAsRegion())
        V = state->getSVal(R);
      else
        V = UnknownVal();
    }

    Bldr.generateNode(Ex, Pred, state->BindExpr(Ex, LCtx, V), nullptr,
                      ProgramPoint::PostLValueKind);
    return;
  }
  if (const auto *ED = dyn_cast<EnumConstantDecl>(D)) {
    assert(!Ex->isGLValue());
    SVal V = svalBuilder.makeIntVal(ED->getInitVal());
    Bldr.generateNode(Ex, Pred, state->BindExpr(Ex, LCtx, V));
    return;
  }
  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    SVal V = svalBuilder.getFunctionPointer(FD);
    Bldr.generateNode(Ex, Pred, state->BindExpr(Ex, LCtx, V), nullptr,
                      ProgramPoint::PostLValueKind);
    return;
  }
  if (isa<FieldDecl>(D) || isa<IndirectFieldDecl>(D)) {
    // FIXME: Compute lvalue of field pointers-to-member.
    // Right now we just use a non-null void pointer, so that it gives proper
    // results in boolean contexts.
    // FIXME: Maybe delegate this to the surrounding operator&.
    // Note how this expression is lvalue, however pointer-to-member is NonLoc.
    SVal V = svalBuilder.conjureSymbolVal(Ex, LCtx, getContext().VoidPtrTy,
                                          currBldrCtx->blockCount());
    state = state->assume(V.castAs<DefinedOrUnknownSVal>(), true);
    Bldr.generateNode(Ex, Pred, state->BindExpr(Ex, LCtx, V), nullptr,
                      ProgramPoint::PostLValueKind);
    return;
  }
  if (isa<BindingDecl>(D)) {
    // FIXME: proper support for bound declarations.
    // For now, let's just prevent crashing.
    return;
  }

  llvm_unreachable("Support for this Decl not implemented.");
}

/// VisitArraySubscriptExpr - Transfer function for array accesses
void ExprEngine::VisitArraySubscriptExpr(const ArraySubscriptExpr *A,
                                             ExplodedNode *Pred,
                                             ExplodedNodeSet &Dst){
  const Expr *Base = A->getBase()->IgnoreParens();
  const Expr *Idx  = A->getIdx()->IgnoreParens();

  ExplodedNodeSet CheckerPreStmt;
  getCheckerManager().runCheckersForPreStmt(CheckerPreStmt, Pred, A, *this);

  ExplodedNodeSet EvalSet;
  StmtNodeBuilder Bldr(CheckerPreStmt, EvalSet, *currBldrCtx);

  bool IsVectorType = A->getBase()->getType()->isVectorType();

  // The "like" case is for situations where C standard prohibits the type to
  // be an lvalue, e.g. taking the address of a subscript of an expression of
  // type "void *".
  bool IsGLValueLike = A->isGLValue() ||
    (A->getType().isCForbiddenLValueType() && !AMgr.getLangOpts().CPlusPlus);

  for (auto *Node : CheckerPreStmt) {
    const LocationContext *LCtx = Node->getLocationContext();
    ProgramStateRef state = Node->getState();

    if (IsGLValueLike) {
      QualType T = A->getType();

      // One of the forbidden LValue types! We still need to have sensible
      // symbolic locations to represent this stuff. Note that arithmetic on
      // void pointers is a GCC extension.
      if (T->isVoidType())
        T = getContext().CharTy;

      SVal V = state->getLValue(T,
                                state->getSVal(Idx, LCtx),
                                state->getSVal(Base, LCtx));
      Bldr.generateNode(A, Node, state->BindExpr(A, LCtx, V), nullptr,
          ProgramPoint::PostLValueKind);
    } else if (IsVectorType) {
      // FIXME: non-glvalue vector reads are not modelled.
      Bldr.generateNode(A, Node, state, nullptr);
    } else {
      llvm_unreachable("Array subscript should be an lValue when not \
a vector and not a forbidden lvalue type");
    }
  }

  getCheckerManager().runCheckersForPostStmt(Dst, EvalSet, A, *this);
}

/// VisitMemberExpr - Transfer function for member expressions.
void ExprEngine::VisitMemberExpr(const MemberExpr *M, ExplodedNode *Pred,
                                 ExplodedNodeSet &Dst) {
  // FIXME: Prechecks eventually go in ::Visit().
  ExplodedNodeSet CheckedSet;
  getCheckerManager().runCheckersForPreStmt(CheckedSet, Pred, M, *this);

  ExplodedNodeSet EvalSet;
  ValueDecl *Member = M->getMemberDecl();

  // Handle static member variables and enum constants accessed via
  // member syntax.
  if (isa<VarDecl>(Member) || isa<EnumConstantDecl>(Member)) {
    for (const auto I : CheckedSet)
      VisitCommonDeclRefExpr(M, Member, I, EvalSet);
  } else {
    StmtNodeBuilder Bldr(CheckedSet, EvalSet, *currBldrCtx);
    ExplodedNodeSet Tmp;

    for (const auto I : CheckedSet) {
      ProgramStateRef state = I->getState();
      const LocationContext *LCtx = I->getLocationContext();
      Expr *BaseExpr = M->getBase();

      // Handle C++ method calls.
      if (const auto *MD = dyn_cast<CXXMethodDecl>(Member)) {
        if (MD->isInstance())
          state = createTemporaryRegionIfNeeded(state, LCtx, BaseExpr);

        SVal MDVal = svalBuilder.getFunctionPointer(MD);
        state = state->BindExpr(M, LCtx, MDVal);

        Bldr.generateNode(M, I, state);
        continue;
      }

      // Handle regular struct fields / member variables.
      const SubRegion *MR = nullptr;
      state = createTemporaryRegionIfNeeded(state, LCtx, BaseExpr,
                                            /*Result=*/nullptr,
                                            /*OutRegionWithAdjustments=*/&MR);
      SVal baseExprVal =
          MR ? loc::MemRegionVal(MR) : state->getSVal(BaseExpr, LCtx);

      const auto *field = cast<FieldDecl>(Member);
      SVal L = state->getLValue(field, baseExprVal);

      if (M->isGLValue() || M->getType()->isArrayType()) {
        // We special-case rvalues of array type because the analyzer cannot
        // reason about them, since we expect all regions to be wrapped in Locs.
        // We instead treat these as lvalues and assume that they will decay to
        // pointers as soon as they are used.
        if (!M->isGLValue()) {
          assert(M->getType()->isArrayType());
          const auto *PE =
            dyn_cast<ImplicitCastExpr>(I->getParentMap().getParentIgnoreParens(M));
          if (!PE || PE->getCastKind() != CK_ArrayToPointerDecay) {
            llvm_unreachable("should always be wrapped in ArrayToPointerDecay");
          }
        }

        if (field->getType()->isReferenceType()) {
          if (const MemRegion *R = L.getAsRegion())
            L = state->getSVal(R);
          else
            L = UnknownVal();
        }

        Bldr.generateNode(M, I, state->BindExpr(M, LCtx, L), nullptr,
                          ProgramPoint::PostLValueKind);
      } else {
        Bldr.takeNodes(I);
        evalLoad(Tmp, M, M, I, state, L);
        Bldr.addNodes(Tmp);
      }
    }
  }

  getCheckerManager().runCheckersForPostStmt(Dst, EvalSet, M, *this);
}

void ExprEngine::VisitAtomicExpr(const AtomicExpr *AE, ExplodedNode *Pred,
                                 ExplodedNodeSet &Dst) {
  ExplodedNodeSet AfterPreSet;
  getCheckerManager().runCheckersForPreStmt(AfterPreSet, Pred, AE, *this);

  // For now, treat all the arguments to C11 atomics as escaping.
  // FIXME: Ideally we should model the behavior of the atomics precisely here.

  ExplodedNodeSet AfterInvalidateSet;
  StmtNodeBuilder Bldr(AfterPreSet, AfterInvalidateSet, *currBldrCtx);

  for (const auto I : AfterPreSet) {
    ProgramStateRef State = I->getState();
    const LocationContext *LCtx = I->getLocationContext();

    SmallVector<SVal, 8> ValuesToInvalidate;
    for (unsigned SI = 0, Count = AE->getNumSubExprs(); SI != Count; SI++) {
      const Expr *SubExpr = AE->getSubExprs()[SI];
      SVal SubExprVal = State->getSVal(SubExpr, LCtx);
      ValuesToInvalidate.push_back(SubExprVal);
    }

    State = State->invalidateRegions(ValuesToInvalidate, AE,
                                    currBldrCtx->blockCount(),
                                    LCtx,
                                    /*CausedByPointerEscape*/true,
                                    /*Symbols=*/nullptr);

    SVal ResultVal = UnknownVal();
    State = State->BindExpr(AE, LCtx, ResultVal);
    Bldr.generateNode(AE, I, State, nullptr,
                      ProgramPoint::PostStmtKind);
  }

  getCheckerManager().runCheckersForPostStmt(Dst, AfterInvalidateSet, AE, *this);
}

// A value escapes in three possible cases:
// (1) We are binding to something that is not a memory region.
// (2) We are binding to a MemrRegion that does not have stack storage.
// (3) We are binding to a MemRegion with stack storage that the store
//     does not understand.
ProgramStateRef ExprEngine::processPointerEscapedOnBind(ProgramStateRef State,
                                                        SVal Loc,
                                                        SVal Val,
                                                        const LocationContext *LCtx) {
  // Are we storing to something that causes the value to "escape"?
  bool escapes = true;

  // TODO: Move to StoreManager.
  if (Optional<loc::MemRegionVal> regionLoc = Loc.getAs<loc::MemRegionVal>()) {
    escapes = !regionLoc->getRegion()->hasStackStorage();

    if (!escapes) {
      // To test (3), generate a new state with the binding added.  If it is
      // the same state, then it escapes (since the store cannot represent
      // the binding).
      // Do this only if we know that the store is not supposed to generate the
      // same state.
      SVal StoredVal = State->getSVal(regionLoc->getRegion());
      if (StoredVal != Val)
        escapes = (State == (State->bindLoc(*regionLoc, Val, LCtx)));
    }
  }

  // If our store can represent the binding and we aren't storing to something
  // that doesn't have local storage then just return and have the simulation
  // state continue as is.
  if (!escapes)
    return State;

  // Otherwise, find all symbols referenced by 'val' that we are tracking
  // and stop tracking them.
  State = escapeValue(State, Val, PSK_EscapeOnBind);
  return State;
}

ProgramStateRef
ExprEngine::notifyCheckersOfPointerEscape(ProgramStateRef State,
    const InvalidatedSymbols *Invalidated,
    ArrayRef<const MemRegion *> ExplicitRegions,
    const CallEvent *Call,
    RegionAndSymbolInvalidationTraits &ITraits) {
  if (!Invalidated || Invalidated->empty())
    return State;

  if (!Call)
    return getCheckerManager().runCheckersForPointerEscape(State,
                                                           *Invalidated,
                                                           nullptr,
                                                           PSK_EscapeOther,
                                                           &ITraits);

  // If the symbols were invalidated by a call, we want to find out which ones
  // were invalidated directly due to being arguments to the call.
  InvalidatedSymbols SymbolsDirectlyInvalidated;
  for (const auto I : ExplicitRegions) {
    if (const SymbolicRegion *R = I->StripCasts()->getAs<SymbolicRegion>())
      SymbolsDirectlyInvalidated.insert(R->getSymbol());
  }

  InvalidatedSymbols SymbolsIndirectlyInvalidated;
  for (const auto &sym : *Invalidated) {
    if (SymbolsDirectlyInvalidated.count(sym))
      continue;
    SymbolsIndirectlyInvalidated.insert(sym);
  }

  if (!SymbolsDirectlyInvalidated.empty())
    State = getCheckerManager().runCheckersForPointerEscape(State,
        SymbolsDirectlyInvalidated, Call, PSK_DirectEscapeOnCall, &ITraits);

  // Notify about the symbols that get indirectly invalidated by the call.
  if (!SymbolsIndirectlyInvalidated.empty())
    State = getCheckerManager().runCheckersForPointerEscape(State,
        SymbolsIndirectlyInvalidated, Call, PSK_IndirectEscapeOnCall, &ITraits);

  return State;
}

/// evalBind - Handle the semantics of binding a value to a specific location.
///  This method is used by evalStore and (soon) VisitDeclStmt, and others.
void ExprEngine::evalBind(ExplodedNodeSet &Dst, const Stmt *StoreE,
                          ExplodedNode *Pred,
                          SVal location, SVal Val,
                          bool atDeclInit, const ProgramPoint *PP) {
  const LocationContext *LC = Pred->getLocationContext();
  PostStmt PS(StoreE, LC);
  if (!PP)
    PP = &PS;

  // Do a previsit of the bind.
  ExplodedNodeSet CheckedSet;
  getCheckerManager().runCheckersForBind(CheckedSet, Pred, location, Val,
                                         StoreE, *this, *PP);

  StmtNodeBuilder Bldr(CheckedSet, Dst, *currBldrCtx);

  // If the location is not a 'Loc', it will already be handled by
  // the checkers.  There is nothing left to do.
  if (!location.getAs<Loc>()) {
    const ProgramPoint L = PostStore(StoreE, LC, /*Loc*/nullptr,
                                     /*tag*/nullptr);
    ProgramStateRef state = Pred->getState();
    state = processPointerEscapedOnBind(state, location, Val, LC);
    Bldr.generateNode(L, state, Pred);
    return;
  }

  for (const auto PredI : CheckedSet) {
    ProgramStateRef state = PredI->getState();

    state = processPointerEscapedOnBind(state, location, Val, LC);

    // When binding the value, pass on the hint that this is a initialization.
    // For initializations, we do not need to inform clients of region
    // changes.
    state = state->bindLoc(location.castAs<Loc>(),
                           Val, LC, /* notifyChanges = */ !atDeclInit);

    const MemRegion *LocReg = nullptr;
    if (Optional<loc::MemRegionVal> LocRegVal =
            location.getAs<loc::MemRegionVal>()) {
      LocReg = LocRegVal->getRegion();
    }

    const ProgramPoint L = PostStore(StoreE, LC, LocReg, nullptr);
    Bldr.generateNode(L, state, PredI);
  }
}

/// evalStore - Handle the semantics of a store via an assignment.
///  @param Dst The node set to store generated state nodes
///  @param AssignE The assignment expression if the store happens in an
///         assignment.
///  @param LocationE The location expression that is stored to.
///  @param state The current simulation state
///  @param location The location to store the value
///  @param Val The value to be stored
void ExprEngine::evalStore(ExplodedNodeSet &Dst, const Expr *AssignE,
                             const Expr *LocationE,
                             ExplodedNode *Pred,
                             ProgramStateRef state, SVal location, SVal Val,
                             const ProgramPointTag *tag) {
  // Proceed with the store.  We use AssignE as the anchor for the PostStore
  // ProgramPoint if it is non-NULL, and LocationE otherwise.
  const Expr *StoreE = AssignE ? AssignE : LocationE;

  // Evaluate the location (checks for bad dereferences).
  ExplodedNodeSet Tmp;
  evalLocation(Tmp, AssignE, LocationE, Pred, state, location, false);

  if (Tmp.empty())
    return;

  if (location.isUndef())
    return;

  for (const auto I : Tmp)
    evalBind(Dst, StoreE, I, location, Val, false);
}

void ExprEngine::evalLoad(ExplodedNodeSet &Dst,
                          const Expr *NodeEx,
                          const Expr *BoundEx,
                          ExplodedNode *Pred,
                          ProgramStateRef state,
                          SVal location,
                          const ProgramPointTag *tag,
                          QualType LoadTy) {
  assert(!location.getAs<NonLoc>() && "location cannot be a NonLoc.");
  assert(NodeEx);
  assert(BoundEx);
  // Evaluate the location (checks for bad dereferences).
  ExplodedNodeSet Tmp;
  evalLocation(Tmp, NodeEx, BoundEx, Pred, state, location, true);
  if (Tmp.empty())
    return;

  StmtNodeBuilder Bldr(Tmp, Dst, *currBldrCtx);
  if (location.isUndef())
    return;

  // Proceed with the load.
  for (const auto I : Tmp) {
    state = I->getState();
    const LocationContext *LCtx = I->getLocationContext();

    SVal V = UnknownVal();
    if (location.isValid()) {
      if (LoadTy.isNull())
        LoadTy = BoundEx->getType();
      V = state->getSVal(location.castAs<Loc>(), LoadTy);
    }

    Bldr.generateNode(NodeEx, I, state->BindExpr(BoundEx, LCtx, V), tag,
                      ProgramPoint::PostLoadKind);
  }
}

void ExprEngine::evalLocation(ExplodedNodeSet &Dst,
                              const Stmt *NodeEx,
                              const Stmt *BoundEx,
                              ExplodedNode *Pred,
                              ProgramStateRef state,
                              SVal location,
                              bool isLoad) {
  StmtNodeBuilder BldrTop(Pred, Dst, *currBldrCtx);
  // Early checks for performance reason.
  if (location.isUnknown()) {
    return;
  }

  ExplodedNodeSet Src;
  BldrTop.takeNodes(Pred);
  StmtNodeBuilder Bldr(Pred, Src, *currBldrCtx);
  if (Pred->getState() != state) {
    // Associate this new state with an ExplodedNode.
    // FIXME: If I pass null tag, the graph is incorrect, e.g for
    //   int *p;
    //   p = 0;
    //   *p = 0xDEADBEEF;
    // "p = 0" is not noted as "Null pointer value stored to 'p'" but
    // instead "int *p" is noted as
    // "Variable 'p' initialized to a null pointer value"

    static SimpleProgramPointTag tag(TagProviderName, "Location");
    Bldr.generateNode(NodeEx, Pred, state, &tag);
  }
  ExplodedNodeSet Tmp;
  getCheckerManager().runCheckersForLocation(Tmp, Src, location, isLoad,
                                             NodeEx, BoundEx, *this);
  BldrTop.addNodes(Tmp);
}

std::pair<const ProgramPointTag *, const ProgramPointTag*>
ExprEngine::geteagerlyAssumeBinOpBifurcationTags() {
  static SimpleProgramPointTag
         eagerlyAssumeBinOpBifurcationTrue(TagProviderName,
                                           "Eagerly Assume True"),
         eagerlyAssumeBinOpBifurcationFalse(TagProviderName,
                                            "Eagerly Assume False");
  return std::make_pair(&eagerlyAssumeBinOpBifurcationTrue,
                        &eagerlyAssumeBinOpBifurcationFalse);
}

void ExprEngine::evalEagerlyAssumeBinOpBifurcation(ExplodedNodeSet &Dst,
                                                   ExplodedNodeSet &Src,
                                                   const Expr *Ex) {
  StmtNodeBuilder Bldr(Src, Dst, *currBldrCtx);

  for (const auto Pred : Src) {
    // Test if the previous node was as the same expression.  This can happen
    // when the expression fails to evaluate to anything meaningful and
    // (as an optimization) we don't generate a node.
    ProgramPoint P = Pred->getLocation();
    if (!P.getAs<PostStmt>() || P.castAs<PostStmt>().getStmt() != Ex) {
      continue;
    }

    ProgramStateRef state = Pred->getState();
    SVal V = state->getSVal(Ex, Pred->getLocationContext());
    Optional<nonloc::SymbolVal> SEV = V.getAs<nonloc::SymbolVal>();
    if (SEV && SEV->isExpression()) {
      const std::pair<const ProgramPointTag *, const ProgramPointTag*> &tags =
        geteagerlyAssumeBinOpBifurcationTags();

      ProgramStateRef StateTrue, StateFalse;
      std::tie(StateTrue, StateFalse) = state->assume(*SEV);

      // First assume that the condition is true.
      if (StateTrue) {
        SVal Val = svalBuilder.makeIntVal(1U, Ex->getType());
        StateTrue = StateTrue->BindExpr(Ex, Pred->getLocationContext(), Val);
        Bldr.generateNode(Ex, Pred, StateTrue, tags.first);
      }

      // Next, assume that the condition is false.
      if (StateFalse) {
        SVal Val = svalBuilder.makeIntVal(0U, Ex->getType());
        StateFalse = StateFalse->BindExpr(Ex, Pred->getLocationContext(), Val);
        Bldr.generateNode(Ex, Pred, StateFalse, tags.second);
      }
    }
  }
}

void ExprEngine::VisitGCCAsmStmt(const GCCAsmStmt *A, ExplodedNode *Pred,
                                 ExplodedNodeSet &Dst) {
  StmtNodeBuilder Bldr(Pred, Dst, *currBldrCtx);
  // We have processed both the inputs and the outputs.  All of the outputs
  // should evaluate to Locs.  Nuke all of their values.

  // FIXME: Some day in the future it would be nice to allow a "plug-in"
  // which interprets the inline asm and stores proper results in the
  // outputs.

  ProgramStateRef state = Pred->getState();

  for (const Expr *O : A->outputs()) {
    SVal X = state->getSVal(O, Pred->getLocationContext());
    assert(!X.getAs<NonLoc>());  // Should be an Lval, or unknown, undef.

    if (Optional<Loc> LV = X.getAs<Loc>())
      state = state->bindLoc(*LV, UnknownVal(), Pred->getLocationContext());
  }

  Bldr.generateNode(A, Pred, state);
}

void ExprEngine::VisitMSAsmStmt(const MSAsmStmt *A, ExplodedNode *Pred,
                                ExplodedNodeSet &Dst) {
  StmtNodeBuilder Bldr(Pred, Dst, *currBldrCtx);
  Bldr.generateNode(A, Pred, Pred->getState());
}

//===----------------------------------------------------------------------===//
// Visualization.
//===----------------------------------------------------------------------===//

#ifndef NDEBUG
namespace llvm {

template<>
struct DOTGraphTraits<ExplodedGraph*> : public DefaultDOTGraphTraits {
  DOTGraphTraits (bool isSimple = false) : DefaultDOTGraphTraits(isSimple) {}

  static bool nodeHasBugReport(const ExplodedNode *N) {
    BugReporter &BR = static_cast<ExprEngine &>(
      N->getState()->getStateManager().getOwningEngine()).getBugReporter();

    const auto EQClasses =
        llvm::make_range(BR.EQClasses_begin(), BR.EQClasses_end());

    for (const auto &EQ : EQClasses) {
      for (const BugReport &Report : EQ) {
        if (Report.getErrorNode() == N)
          return true;
      }
    }
    return false;
  }

  /// \p PreCallback: callback before break.
  /// \p PostCallback: callback after break.
  /// \p Stop: stop iteration if returns {@code true}
  /// \return Whether {@code Stop} ever returned {@code true}.
  static bool traverseHiddenNodes(
      const ExplodedNode *N,
      llvm::function_ref<void(const ExplodedNode *)> PreCallback,
      llvm::function_ref<void(const ExplodedNode *)> PostCallback,
      llvm::function_ref<bool(const ExplodedNode *)> Stop) {
    const ExplodedNode *FirstHiddenNode = N;
    while (FirstHiddenNode->pred_size() == 1 &&
           isNodeHidden(*FirstHiddenNode->pred_begin())) {
      FirstHiddenNode = *FirstHiddenNode->pred_begin();
    }
    const ExplodedNode *OtherNode = FirstHiddenNode;
    while (true) {
      PreCallback(OtherNode);
      if (Stop(OtherNode))
        return true;

      if (OtherNode == N)
        break;
      PostCallback(OtherNode);

      OtherNode = *OtherNode->succ_begin();
    }
    return false;
  }

  static std::string getNodeAttributes(const ExplodedNode *N,
                                       ExplodedGraph *) {
    SmallVector<StringRef, 10> Out;
    auto Noop = [](const ExplodedNode*){};
    if (traverseHiddenNodes(N, Noop, Noop, &nodeHasBugReport)) {
      Out.push_back("style=filled");
      Out.push_back("fillcolor=red");
    }

    if (traverseHiddenNodes(N, Noop, Noop,
                            [](const ExplodedNode *C) { return C->isSink(); }))
      Out.push_back("color=blue");
    return llvm::join(Out, ",");
  }

  static bool isNodeHidden(const ExplodedNode *N) {
    return N->isTrivial();
  }

  static std::string getNodeLabel(const ExplodedNode *N, ExplodedGraph *G){
    std::string sbuf;
    llvm::raw_string_ostream Out(sbuf);

    ProgramStateRef State = N->getState();

    // Dump program point for all the previously skipped nodes.
    traverseHiddenNodes(
        N,
        [&](const ExplodedNode *OtherNode) {
          OtherNode->getLocation().print(/*CR=*/"\\l", Out);
          if (const ProgramPointTag *Tag = OtherNode->getLocation().getTag())
            Out << "\\lTag:" << Tag->getTagDescription();
          if (N->isSink())
            Out << "\\lNode is sink\\l";
          if (nodeHasBugReport(N))
            Out << "\\lBug report attached\\l";
        },
        [&](const ExplodedNode *) { Out << "\\l--------\\l"; },
        [&](const ExplodedNode *) { return false; });

    Out << "\\l\\|";

    Out << "StateID: ST" << State->getID() << ", NodeID: N" << N->getID(G)
        << " <" << (const void *)N << ">\\|";

    bool SameAsAllPredecessors =
        std::all_of(N->pred_begin(), N->pred_end(), [&](const ExplodedNode *P) {
          return P->getState() == State;
        });
    if (!SameAsAllPredecessors)
      State->printDOT(Out, N->getLocationContext());
    return Out.str();
  }
};

} // namespace llvm
#endif

void ExprEngine::ViewGraph(bool trim) {
#ifndef NDEBUG
  std::string Filename = DumpGraph(trim);
  llvm::DisplayGraph(Filename, false, llvm::GraphProgram::DOT);
#endif
  llvm::errs() << "Warning: viewing graph requires assertions" << "\n";
}


void ExprEngine::ViewGraph(ArrayRef<const ExplodedNode*> Nodes) {
#ifndef NDEBUG
  std::string Filename = DumpGraph(Nodes);
  llvm::DisplayGraph(Filename, false, llvm::GraphProgram::DOT);
#endif
  llvm::errs() << "Warning: viewing graph requires assertions" << "\n";
}

std::string ExprEngine::DumpGraph(bool trim, StringRef Filename) {
#ifndef NDEBUG
  if (trim) {
    std::vector<const ExplodedNode *> Src;

    // Iterate through the reports and get their nodes.
    for (BugReporter::EQClasses_iterator
           EI = BR.EQClasses_begin(), EE = BR.EQClasses_end(); EI != EE; ++EI) {
      const auto *N = const_cast<ExplodedNode *>(EI->begin()->getErrorNode());
      if (N) Src.push_back(N);
    }
    return DumpGraph(Src, Filename);
  } else {
    return llvm::WriteGraph(&G, "ExprEngine", /*ShortNames=*/false,
                     /*Title=*/"Exploded Graph", /*Filename=*/Filename);
  }
#endif
  llvm::errs() << "Warning: dumping graph requires assertions" << "\n";
  return "";
}

std::string ExprEngine::DumpGraph(ArrayRef<const ExplodedNode*> Nodes,
                                  StringRef Filename) {
#ifndef NDEBUG
  std::unique_ptr<ExplodedGraph> TrimmedG(G.trim(Nodes));

  if (!TrimmedG.get()) {
    llvm::errs() << "warning: Trimmed ExplodedGraph is empty.\n";
  } else {
    return llvm::WriteGraph(TrimmedG.get(), "TrimmedExprEngine",
                            /*ShortNames=*/false,
                            /*Title=*/"Trimmed Exploded Graph",
                            /*Filename=*/Filename);
  }
#endif
  llvm::errs() << "Warning: dumping graph requires assertions" << "\n";
  return "";
}

void *ProgramStateTrait<ReplayWithoutInlining>::GDMIndex() {
  static int index = 0;
  return &index;
}
