//=== InnerPointerChecker.cpp -------------------------------------*- C++ -*--//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a check that marks a raw pointer to a C++ container's
// inner buffer released when the object is destroyed. This information can
// be used by MallocChecker to detect use-after-free problems.
//
//===----------------------------------------------------------------------===//

#include "AllocationState.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "InterCheckerAPI.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/BugReporter/CommonBugCategories.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

// Associate container objects with a set of raw pointer symbols.
REGISTER_SET_FACTORY_WITH_PROGRAMSTATE(PtrSet, SymbolRef)
REGISTER_MAP_WITH_PROGRAMSTATE(RawPtrMap, const MemRegion *, PtrSet)


namespace {

class InnerPointerChecker
    : public Checker<check::DeadSymbols, check::PostCall> {

  CallDescription AppendFn, AssignFn, ClearFn, CStrFn, DataFn, EraseFn,
      InsertFn, PopBackFn, PushBackFn, ReplaceFn, ReserveFn, ResizeFn,
      ShrinkToFitFn, SwapFn;

public:
  class InnerPointerBRVisitor : public BugReporterVisitor {
    SymbolRef PtrToBuf;

  public:
    InnerPointerBRVisitor(SymbolRef Sym) : PtrToBuf(Sym) {}

    static void *getTag() {
      static int Tag = 0;
      return &Tag;
    }

    void Profile(llvm::FoldingSetNodeID &ID) const override {
      ID.AddPointer(getTag());
    }

    virtual std::shared_ptr<PathDiagnosticPiece> VisitNode(const ExplodedNode *N,
                                                   BugReporterContext &BRC,
                                                   BugReport &BR) override;

    // FIXME: Scan the map once in the visitor's constructor and do a direct
    // lookup by region.
    bool isSymbolTracked(ProgramStateRef State, SymbolRef Sym) {
      RawPtrMapTy Map = State->get<RawPtrMap>();
      for (const auto Entry : Map) {
        if (Entry.second.contains(Sym))
          return true;
      }
      return false;
    }
  };

  InnerPointerChecker()
      : AppendFn({"std", "basic_string", "append"}),
        AssignFn({"std", "basic_string", "assign"}),
        ClearFn({"std", "basic_string", "clear"}),
        CStrFn({"std", "basic_string", "c_str"}),
        DataFn({"std", "basic_string", "data"}),
        EraseFn({"std", "basic_string", "erase"}),
        InsertFn({"std", "basic_string", "insert"}),
        PopBackFn({"std", "basic_string", "pop_back"}),
        PushBackFn({"std", "basic_string", "push_back"}),
        ReplaceFn({"std", "basic_string", "replace"}),
        ReserveFn({"std", "basic_string", "reserve"}),
        ResizeFn({"std", "basic_string", "resize"}),
        ShrinkToFitFn({"std", "basic_string", "shrink_to_fit"}),
        SwapFn({"std", "basic_string", "swap"}) {}

  /// Check whether the called member function potentially invalidates
  /// pointers referring to the container object's inner buffer.
  bool isInvalidatingMemberFunction(const CallEvent &Call) const;

  /// Mark pointer symbols associated with the given memory region released
  /// in the program state.
  void markPtrSymbolsReleased(const CallEvent &Call, ProgramStateRef State,
                              const MemRegion *ObjRegion,
                              CheckerContext &C) const;

  /// Standard library functions that take a non-const `basic_string` argument by
  /// reference may invalidate its inner pointers. Check for these cases and
  /// mark the pointers released.
  void checkFunctionArguments(const CallEvent &Call, ProgramStateRef State,
                              CheckerContext &C) const;

  /// Record the connection between raw pointers referring to a container
  /// object's inner buffer and the object's memory region in the program state.
  /// Mark potentially invalidated pointers released.
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;

  /// Clean up the program state map.
  void checkDeadSymbols(SymbolReaper &SymReaper, CheckerContext &C) const;
};

} // end anonymous namespace

bool InnerPointerChecker::isInvalidatingMemberFunction(
        const CallEvent &Call) const {
  if (const auto *MemOpCall = dyn_cast<CXXMemberOperatorCall>(&Call)) {
    OverloadedOperatorKind Opc = MemOpCall->getOriginExpr()->getOperator();
    if (Opc == OO_Equal || Opc == OO_PlusEqual)
      return true;
    return false;
  }
  return (isa<CXXDestructorCall>(Call) || Call.isCalled(AppendFn) ||
          Call.isCalled(AssignFn) || Call.isCalled(ClearFn) ||
          Call.isCalled(EraseFn) || Call.isCalled(InsertFn) ||
          Call.isCalled(PopBackFn) || Call.isCalled(PushBackFn) ||
          Call.isCalled(ReplaceFn) || Call.isCalled(ReserveFn) ||
          Call.isCalled(ResizeFn) || Call.isCalled(ShrinkToFitFn) ||
          Call.isCalled(SwapFn));
}

void InnerPointerChecker::markPtrSymbolsReleased(const CallEvent &Call,
                                                 ProgramStateRef State,
                                                 const MemRegion *MR,
                                                 CheckerContext &C) const {
  if (const PtrSet *PS = State->get<RawPtrMap>(MR)) {
    const Expr *Origin = Call.getOriginExpr();
    for (const auto Symbol : *PS) {
      // NOTE: `Origin` may be null, and will be stored so in the symbol's
      // `RefState` in MallocChecker's `RegionState` program state map.
      State = allocation_state::markReleased(State, Symbol, Origin);
    }
    State = State->remove<RawPtrMap>(MR);
    C.addTransition(State);
    return;
  }
}

void InnerPointerChecker::checkFunctionArguments(const CallEvent &Call,
                                                 ProgramStateRef State,
                                                 CheckerContext &C) const {
  if (const auto *FC = dyn_cast<AnyFunctionCall>(&Call)) {
    const FunctionDecl *FD = FC->getDecl();
    if (!FD || !FD->isInStdNamespace())
      return;

    for (unsigned I = 0, E = FD->getNumParams(); I != E; ++I) {
      QualType ParamTy = FD->getParamDecl(I)->getType();
      if (!ParamTy->isReferenceType() ||
          ParamTy->getPointeeType().isConstQualified())
        continue;

      // In case of member operator calls, `this` is counted as an
      // argument but not as a parameter.
      bool isaMemberOpCall = isa<CXXMemberOperatorCall>(FC);
      unsigned ArgI = isaMemberOpCall ? I+1 : I;

      SVal Arg = FC->getArgSVal(ArgI);
      const auto *ArgRegion =
          dyn_cast_or_null<TypedValueRegion>(Arg.getAsRegion());
      if (!ArgRegion)
        continue;

      markPtrSymbolsReleased(Call, State, ArgRegion, C);
    }
  }
}

// [string.require]
//
// "References, pointers, and iterators referring to the elements of a
// basic_string sequence may be invalidated by the following uses of that
// basic_string object:
//
// -- As an argument to any standard library function taking a reference
// to non-const basic_string as an argument. For example, as an argument to
// non-member functions swap(), operator>>(), and getline(), or as an argument
// to basic_string::swap().
//
// -- Calling non-const member functions, except operator[], at, front, back,
// begin, rbegin, end, and rend."

void InnerPointerChecker::checkPostCall(const CallEvent &Call,
                                        CheckerContext &C) const {
  ProgramStateRef State = C.getState();

  if (const auto *ICall = dyn_cast<CXXInstanceCall>(&Call)) {
    // TODO: Do we need these to be typed?
    const auto *ObjRegion = dyn_cast_or_null<TypedValueRegion>(
        ICall->getCXXThisVal().getAsRegion());
    if (!ObjRegion)
      return;

    if (Call.isCalled(CStrFn) || Call.isCalled(DataFn)) {
      SVal RawPtr = Call.getReturnValue();
      if (SymbolRef Sym = RawPtr.getAsSymbol(/*IncludeBaseRegions=*/true)) {
        // Start tracking this raw pointer by adding it to the set of symbols
        // associated with this container object in the program state map.

        PtrSet::Factory &F = State->getStateManager().get_context<PtrSet>();
        const PtrSet *SetPtr = State->get<RawPtrMap>(ObjRegion);
        PtrSet Set = SetPtr ? *SetPtr : F.getEmptySet();
        assert(C.wasInlined || !Set.contains(Sym));
        Set = F.add(Set, Sym);

        State = State->set<RawPtrMap>(ObjRegion, Set);
        C.addTransition(State);
      }
      return;
    }

    // Check [string.require] / second point.
    if (isInvalidatingMemberFunction(Call)) {
      markPtrSymbolsReleased(Call, State, ObjRegion, C);
      return;
    }
  }

  // Check [string.require] / first point.
  checkFunctionArguments(Call, State, C);
}

void InnerPointerChecker::checkDeadSymbols(SymbolReaper &SymReaper,
                                           CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  PtrSet::Factory &F = State->getStateManager().get_context<PtrSet>();
  RawPtrMapTy RPM = State->get<RawPtrMap>();
  for (const auto Entry : RPM) {
    if (!SymReaper.isLiveRegion(Entry.first)) {
      // Due to incomplete destructor support, some dead regions might
      // remain in the program state map. Clean them up.
      State = State->remove<RawPtrMap>(Entry.first);
    }
    if (const PtrSet *OldSet = State->get<RawPtrMap>(Entry.first)) {
      PtrSet CleanedUpSet = *OldSet;
      for (const auto Symbol : Entry.second) {
        if (!SymReaper.isLive(Symbol))
          CleanedUpSet = F.remove(CleanedUpSet, Symbol);
      }
      State = CleanedUpSet.isEmpty()
                  ? State->remove<RawPtrMap>(Entry.first)
                  : State->set<RawPtrMap>(Entry.first, CleanedUpSet);
    }
  }
  C.addTransition(State);
}

namespace clang {
namespace ento {
namespace allocation_state {

std::unique_ptr<BugReporterVisitor> getInnerPointerBRVisitor(SymbolRef Sym) {
  return llvm::make_unique<InnerPointerChecker::InnerPointerBRVisitor>(Sym);
}

const MemRegion *getContainerObjRegion(ProgramStateRef State, SymbolRef Sym) {
  RawPtrMapTy Map = State->get<RawPtrMap>();
  for (const auto Entry : Map) {
    if (Entry.second.contains(Sym)) {
      return Entry.first;
    }
  }
  return nullptr;
}

} // end namespace allocation_state
} // end namespace ento
} // end namespace clang

std::shared_ptr<PathDiagnosticPiece>
InnerPointerChecker::InnerPointerBRVisitor::VisitNode(const ExplodedNode *N,
                                                      BugReporterContext &BRC,
                                                      BugReport &) {
  if (!isSymbolTracked(N->getState(), PtrToBuf) ||
      isSymbolTracked(N->getFirstPred()->getState(), PtrToBuf))
    return nullptr;

  const Stmt *S = PathDiagnosticLocation::getStmt(N);
  if (!S)
    return nullptr;

  const MemRegion *ObjRegion =
      allocation_state::getContainerObjRegion(N->getState(), PtrToBuf);
  const auto *TypedRegion = cast<TypedValueRegion>(ObjRegion);
  QualType ObjTy = TypedRegion->getValueType();

  SmallString<256> Buf;
  llvm::raw_svector_ostream OS(Buf);
  OS << "Pointer to inner buffer of '" << ObjTy.getAsString()
     << "' obtained here";
  PathDiagnosticLocation Pos(S, BRC.getSourceManager(),
                             N->getLocationContext());
  return std::make_shared<PathDiagnosticEventPiece>(Pos, OS.str(), true,
                                                    nullptr);
}

void ento::registerInnerPointerChecker(CheckerManager &Mgr) {
  registerInnerPointerCheckerAux(Mgr);
  Mgr.registerChecker<InnerPointerChecker>();
}
