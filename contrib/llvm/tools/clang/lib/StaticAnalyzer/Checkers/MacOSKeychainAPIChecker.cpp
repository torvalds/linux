//==--- MacOSKeychainAPIChecker.cpp ------------------------------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This checker flags misuses of KeyChainAPI. In particular, the password data
// allocated/returned by SecKeychainItemCopyContent,
// SecKeychainFindGenericPassword, SecKeychainFindInternetPassword functions has
// to be freed using a call to SecKeychainItemFreeContent.
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

namespace {
class MacOSKeychainAPIChecker : public Checker<check::PreStmt<CallExpr>,
                                               check::PostStmt<CallExpr>,
                                               check::DeadSymbols,
                                               check::PointerEscape,
                                               eval::Assume> {
  mutable std::unique_ptr<BugType> BT;

public:
  /// AllocationState is a part of the checker specific state together with the
  /// MemRegion corresponding to the allocated data.
  struct AllocationState {
    /// The index of the allocator function.
    unsigned int AllocatorIdx;
    SymbolRef Region;

    AllocationState(const Expr *E, unsigned int Idx, SymbolRef R) :
      AllocatorIdx(Idx),
      Region(R) {}

    bool operator==(const AllocationState &X) const {
      return (AllocatorIdx == X.AllocatorIdx &&
              Region == X.Region);
    }

    void Profile(llvm::FoldingSetNodeID &ID) const {
      ID.AddInteger(AllocatorIdx);
      ID.AddPointer(Region);
    }
  };

  void checkPreStmt(const CallExpr *S, CheckerContext &C) const;
  void checkPostStmt(const CallExpr *S, CheckerContext &C) const;
  void checkDeadSymbols(SymbolReaper &SR, CheckerContext &C) const;
  ProgramStateRef checkPointerEscape(ProgramStateRef State,
                                     const InvalidatedSymbols &Escaped,
                                     const CallEvent *Call,
                                     PointerEscapeKind Kind) const;
  ProgramStateRef evalAssume(ProgramStateRef state, SVal Cond,
                             bool Assumption) const;
  void printState(raw_ostream &Out, ProgramStateRef State,
                  const char *NL, const char *Sep) const;

private:
  typedef std::pair<SymbolRef, const AllocationState*> AllocationPair;
  typedef SmallVector<AllocationPair, 2> AllocationPairVec;

  enum APIKind {
    /// Denotes functions tracked by this checker.
    ValidAPI = 0,
    /// The functions commonly/mistakenly used in place of the given API.
    ErrorAPI = 1,
    /// The functions which may allocate the data. These are tracked to reduce
    /// the false alarm rate.
    PossibleAPI = 2
  };
  /// Stores the information about the allocator and deallocator functions -
  /// these are the functions the checker is tracking.
  struct ADFunctionInfo {
    const char* Name;
    unsigned int Param;
    unsigned int DeallocatorIdx;
    APIKind Kind;
  };
  static const unsigned InvalidIdx = 100000;
  static const unsigned FunctionsToTrackSize = 8;
  static const ADFunctionInfo FunctionsToTrack[FunctionsToTrackSize];
  /// The value, which represents no error return value for allocator functions.
  static const unsigned NoErr = 0;

  /// Given the function name, returns the index of the allocator/deallocator
  /// function.
  static unsigned getTrackedFunctionIndex(StringRef Name, bool IsAllocator);

  inline void initBugType() const {
    if (!BT)
      BT.reset(new BugType(this, "Improper use of SecKeychain API",
                           "API Misuse (Apple)"));
  }

  void generateDeallocatorMismatchReport(const AllocationPair &AP,
                                         const Expr *ArgExpr,
                                         CheckerContext &C) const;

  /// Find the allocation site for Sym on the path leading to the node N.
  const ExplodedNode *getAllocationNode(const ExplodedNode *N, SymbolRef Sym,
                                        CheckerContext &C) const;

  std::unique_ptr<BugReport> generateAllocatedDataNotReleasedReport(
      const AllocationPair &AP, ExplodedNode *N, CheckerContext &C) const;

  /// Mark an AllocationPair interesting for diagnostic reporting.
  void markInteresting(BugReport *R, const AllocationPair &AP) const {
    R->markInteresting(AP.first);
    R->markInteresting(AP.second->Region);
  }

  /// The bug visitor which allows us to print extra diagnostics along the
  /// BugReport path. For example, showing the allocation site of the leaked
  /// region.
  class SecKeychainBugVisitor : public BugReporterVisitor {
  protected:
    // The allocated region symbol tracked by the main analysis.
    SymbolRef Sym;

  public:
    SecKeychainBugVisitor(SymbolRef S) : Sym(S) {}

    void Profile(llvm::FoldingSetNodeID &ID) const override {
      static int X = 0;
      ID.AddPointer(&X);
      ID.AddPointer(Sym);
    }

    std::shared_ptr<PathDiagnosticPiece> VisitNode(const ExplodedNode *N,
                                                   BugReporterContext &BRC,
                                                   BugReport &BR) override;
  };
};
}

/// ProgramState traits to store the currently allocated (and not yet freed)
/// symbols. This is a map from the allocated content symbol to the
/// corresponding AllocationState.
REGISTER_MAP_WITH_PROGRAMSTATE(AllocatedData,
                               SymbolRef,
                               MacOSKeychainAPIChecker::AllocationState)

static bool isEnclosingFunctionParam(const Expr *E) {
  E = E->IgnoreParenCasts();
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    const ValueDecl *VD = DRE->getDecl();
    if (isa<ImplicitParamDecl>(VD) || isa<ParmVarDecl>(VD))
      return true;
  }
  return false;
}

const MacOSKeychainAPIChecker::ADFunctionInfo
  MacOSKeychainAPIChecker::FunctionsToTrack[FunctionsToTrackSize] = {
    {"SecKeychainItemCopyContent", 4, 3, ValidAPI},                       // 0
    {"SecKeychainFindGenericPassword", 6, 3, ValidAPI},                   // 1
    {"SecKeychainFindInternetPassword", 13, 3, ValidAPI},                 // 2
    {"SecKeychainItemFreeContent", 1, InvalidIdx, ValidAPI},              // 3
    {"SecKeychainItemCopyAttributesAndData", 5, 5, ValidAPI},             // 4
    {"SecKeychainItemFreeAttributesAndData", 1, InvalidIdx, ValidAPI},    // 5
    {"free", 0, InvalidIdx, ErrorAPI},                                    // 6
    {"CFStringCreateWithBytesNoCopy", 1, InvalidIdx, PossibleAPI},        // 7
};

unsigned MacOSKeychainAPIChecker::getTrackedFunctionIndex(StringRef Name,
                                                          bool IsAllocator) {
  for (unsigned I = 0; I < FunctionsToTrackSize; ++I) {
    ADFunctionInfo FI = FunctionsToTrack[I];
    if (FI.Name != Name)
      continue;
    // Make sure the function is of the right type (allocator vs deallocator).
    if (IsAllocator && (FI.DeallocatorIdx == InvalidIdx))
      return InvalidIdx;
    if (!IsAllocator && (FI.DeallocatorIdx != InvalidIdx))
      return InvalidIdx;

    return I;
  }
  // The function is not tracked.
  return InvalidIdx;
}

static bool isBadDeallocationArgument(const MemRegion *Arg) {
  if (!Arg)
    return false;
  return isa<AllocaRegion>(Arg) || isa<BlockDataRegion>(Arg) ||
         isa<TypedRegion>(Arg);
}

/// Given the address expression, retrieve the value it's pointing to. Assume
/// that value is itself an address, and return the corresponding symbol.
static SymbolRef getAsPointeeSymbol(const Expr *Expr,
                                    CheckerContext &C) {
  ProgramStateRef State = C.getState();
  SVal ArgV = C.getSVal(Expr);

  if (Optional<loc::MemRegionVal> X = ArgV.getAs<loc::MemRegionVal>()) {
    StoreManager& SM = C.getStoreManager();
    SymbolRef sym = SM.getBinding(State->getStore(), *X).getAsLocSymbol();
    if (sym)
      return sym;
  }
  return nullptr;
}

// Report deallocator mismatch. Remove the region from tracking - reporting a
// missing free error after this one is redundant.
void MacOSKeychainAPIChecker::
  generateDeallocatorMismatchReport(const AllocationPair &AP,
                                    const Expr *ArgExpr,
                                    CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  State = State->remove<AllocatedData>(AP.first);
  ExplodedNode *N = C.generateNonFatalErrorNode(State);

  if (!N)
    return;
  initBugType();
  SmallString<80> sbuf;
  llvm::raw_svector_ostream os(sbuf);
  unsigned int PDeallocIdx =
               FunctionsToTrack[AP.second->AllocatorIdx].DeallocatorIdx;

  os << "Deallocator doesn't match the allocator: '"
     << FunctionsToTrack[PDeallocIdx].Name << "' should be used.";
  auto Report = llvm::make_unique<BugReport>(*BT, os.str(), N);
  Report->addVisitor(llvm::make_unique<SecKeychainBugVisitor>(AP.first));
  Report->addRange(ArgExpr->getSourceRange());
  markInteresting(Report.get(), AP);
  C.emitReport(std::move(Report));
}

void MacOSKeychainAPIChecker::checkPreStmt(const CallExpr *CE,
                                           CheckerContext &C) const {
  unsigned idx = InvalidIdx;
  ProgramStateRef State = C.getState();

  const FunctionDecl *FD = C.getCalleeDecl(CE);
  if (!FD || FD->getKind() != Decl::Function)
    return;

  StringRef funName = C.getCalleeName(FD);
  if (funName.empty())
    return;

  // If it is a call to an allocator function, it could be a double allocation.
  idx = getTrackedFunctionIndex(funName, true);
  if (idx != InvalidIdx) {
    unsigned paramIdx = FunctionsToTrack[idx].Param;
    if (CE->getNumArgs() <= paramIdx)
      return;

    const Expr *ArgExpr = CE->getArg(paramIdx);
    if (SymbolRef V = getAsPointeeSymbol(ArgExpr, C))
      if (const AllocationState *AS = State->get<AllocatedData>(V)) {
        // Remove the value from the state. The new symbol will be added for
        // tracking when the second allocator is processed in checkPostStmt().
        State = State->remove<AllocatedData>(V);
        ExplodedNode *N = C.generateNonFatalErrorNode(State);
        if (!N)
          return;
        initBugType();
        SmallString<128> sbuf;
        llvm::raw_svector_ostream os(sbuf);
        unsigned int DIdx = FunctionsToTrack[AS->AllocatorIdx].DeallocatorIdx;
        os << "Allocated data should be released before another call to "
            << "the allocator: missing a call to '"
            << FunctionsToTrack[DIdx].Name
            << "'.";
        auto Report = llvm::make_unique<BugReport>(*BT, os.str(), N);
        Report->addVisitor(llvm::make_unique<SecKeychainBugVisitor>(V));
        Report->addRange(ArgExpr->getSourceRange());
        Report->markInteresting(AS->Region);
        C.emitReport(std::move(Report));
      }
    return;
  }

  // Is it a call to one of deallocator functions?
  idx = getTrackedFunctionIndex(funName, false);
  if (idx == InvalidIdx)
    return;

  unsigned paramIdx = FunctionsToTrack[idx].Param;
  if (CE->getNumArgs() <= paramIdx)
    return;

  // Check the argument to the deallocator.
  const Expr *ArgExpr = CE->getArg(paramIdx);
  SVal ArgSVal = C.getSVal(ArgExpr);

  // Undef is reported by another checker.
  if (ArgSVal.isUndef())
    return;

  SymbolRef ArgSM = ArgSVal.getAsLocSymbol();

  // If the argument is coming from the heap, globals, or unknown, do not
  // report it.
  bool RegionArgIsBad = false;
  if (!ArgSM) {
    if (!isBadDeallocationArgument(ArgSVal.getAsRegion()))
      return;
    RegionArgIsBad = true;
  }

  // Is the argument to the call being tracked?
  const AllocationState *AS = State->get<AllocatedData>(ArgSM);
  if (!AS)
    return;

  // TODO: We might want to report double free here.
  // (that would involve tracking all the freed symbols in the checker state).
  if (RegionArgIsBad) {
    // It is possible that this is a false positive - the argument might
    // have entered as an enclosing function parameter.
    if (isEnclosingFunctionParam(ArgExpr))
      return;

    ExplodedNode *N = C.generateNonFatalErrorNode(State);
    if (!N)
      return;
    initBugType();
    auto Report = llvm::make_unique<BugReport>(
        *BT, "Trying to free data which has not been allocated.", N);
    Report->addRange(ArgExpr->getSourceRange());
    if (AS)
      Report->markInteresting(AS->Region);
    C.emitReport(std::move(Report));
    return;
  }

  // Process functions which might deallocate.
  if (FunctionsToTrack[idx].Kind == PossibleAPI) {

    if (funName == "CFStringCreateWithBytesNoCopy") {
      const Expr *DeallocatorExpr = CE->getArg(5)->IgnoreParenCasts();
      // NULL ~ default deallocator, so warn.
      if (DeallocatorExpr->isNullPointerConstant(C.getASTContext(),
          Expr::NPC_ValueDependentIsNotNull)) {
        const AllocationPair AP = std::make_pair(ArgSM, AS);
        generateDeallocatorMismatchReport(AP, ArgExpr, C);
        return;
      }
      // One of the default allocators, so warn.
      if (const DeclRefExpr *DE = dyn_cast<DeclRefExpr>(DeallocatorExpr)) {
        StringRef DeallocatorName = DE->getFoundDecl()->getName();
        if (DeallocatorName == "kCFAllocatorDefault" ||
            DeallocatorName == "kCFAllocatorSystemDefault" ||
            DeallocatorName == "kCFAllocatorMalloc") {
          const AllocationPair AP = std::make_pair(ArgSM, AS);
          generateDeallocatorMismatchReport(AP, ArgExpr, C);
          return;
        }
        // If kCFAllocatorNull, which does not deallocate, we still have to
        // find the deallocator.
        if (DE->getFoundDecl()->getName() == "kCFAllocatorNull")
          return;
      }
      // In all other cases, assume the user supplied a correct deallocator
      // that will free memory so stop tracking.
      State = State->remove<AllocatedData>(ArgSM);
      C.addTransition(State);
      return;
    }

    llvm_unreachable("We know of no other possible APIs.");
  }

  // The call is deallocating a value we previously allocated, so remove it
  // from the next state.
  State = State->remove<AllocatedData>(ArgSM);

  // Check if the proper deallocator is used.
  unsigned int PDeallocIdx = FunctionsToTrack[AS->AllocatorIdx].DeallocatorIdx;
  if (PDeallocIdx != idx || (FunctionsToTrack[idx].Kind == ErrorAPI)) {
    const AllocationPair AP = std::make_pair(ArgSM, AS);
    generateDeallocatorMismatchReport(AP, ArgExpr, C);
    return;
  }

  C.addTransition(State);
}

void MacOSKeychainAPIChecker::checkPostStmt(const CallExpr *CE,
                                            CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  const FunctionDecl *FD = C.getCalleeDecl(CE);
  if (!FD || FD->getKind() != Decl::Function)
    return;

  StringRef funName = C.getCalleeName(FD);

  // If a value has been allocated, add it to the set for tracking.
  unsigned idx = getTrackedFunctionIndex(funName, true);
  if (idx == InvalidIdx)
    return;

  const Expr *ArgExpr = CE->getArg(FunctionsToTrack[idx].Param);
  // If the argument entered as an enclosing function parameter, skip it to
  // avoid false positives.
  if (isEnclosingFunctionParam(ArgExpr) &&
      C.getLocationContext()->getParent() == nullptr)
    return;

  if (SymbolRef V = getAsPointeeSymbol(ArgExpr, C)) {
    // If the argument points to something that's not a symbolic region, it
    // can be:
    //  - unknown (cannot reason about it)
    //  - undefined (already reported by other checker)
    //  - constant (null - should not be tracked,
    //              other constant will generate a compiler warning)
    //  - goto (should be reported by other checker)

    // The call return value symbol should stay alive for as long as the
    // allocated value symbol, since our diagnostics depend on the value
    // returned by the call. Ex: Data should only be freed if noErr was
    // returned during allocation.)
    SymbolRef RetStatusSymbol = C.getSVal(CE).getAsSymbol();
    C.getSymbolManager().addSymbolDependency(V, RetStatusSymbol);

    // Track the allocated value in the checker state.
    State = State->set<AllocatedData>(V, AllocationState(ArgExpr, idx,
                                                         RetStatusSymbol));
    assert(State);
    C.addTransition(State);
  }
}

// TODO: This logic is the same as in Malloc checker.
const ExplodedNode *
MacOSKeychainAPIChecker::getAllocationNode(const ExplodedNode *N,
                                           SymbolRef Sym,
                                           CheckerContext &C) const {
  const LocationContext *LeakContext = N->getLocationContext();
  // Walk the ExplodedGraph backwards and find the first node that referred to
  // the tracked symbol.
  const ExplodedNode *AllocNode = N;

  while (N) {
    if (!N->getState()->get<AllocatedData>(Sym))
      break;
    // Allocation node, is the last node in the current or parent context in
    // which the symbol was tracked.
    const LocationContext *NContext = N->getLocationContext();
    if (NContext == LeakContext ||
        NContext->isParentOf(LeakContext))
      AllocNode = N;
    N = N->pred_empty() ? nullptr : *(N->pred_begin());
  }

  return AllocNode;
}

std::unique_ptr<BugReport>
MacOSKeychainAPIChecker::generateAllocatedDataNotReleasedReport(
    const AllocationPair &AP, ExplodedNode *N, CheckerContext &C) const {
  const ADFunctionInfo &FI = FunctionsToTrack[AP.second->AllocatorIdx];
  initBugType();
  SmallString<70> sbuf;
  llvm::raw_svector_ostream os(sbuf);
  os << "Allocated data is not released: missing a call to '"
      << FunctionsToTrack[FI.DeallocatorIdx].Name << "'.";

  // Most bug reports are cached at the location where they occurred.
  // With leaks, we want to unique them by the location where they were
  // allocated, and only report a single path.
  PathDiagnosticLocation LocUsedForUniqueing;
  const ExplodedNode *AllocNode = getAllocationNode(N, AP.first, C);
  const Stmt *AllocStmt = PathDiagnosticLocation::getStmt(AllocNode);

  if (AllocStmt)
    LocUsedForUniqueing = PathDiagnosticLocation::createBegin(AllocStmt,
                                              C.getSourceManager(),
                                              AllocNode->getLocationContext());

  auto Report =
      llvm::make_unique<BugReport>(*BT, os.str(), N, LocUsedForUniqueing,
                                  AllocNode->getLocationContext()->getDecl());

  Report->addVisitor(llvm::make_unique<SecKeychainBugVisitor>(AP.first));
  markInteresting(Report.get(), AP);
  return Report;
}

/// If the return symbol is assumed to be error, remove the allocated info
/// from consideration.
ProgramStateRef MacOSKeychainAPIChecker::evalAssume(ProgramStateRef State,
                                                    SVal Cond,
                                                    bool Assumption) const {
  AllocatedDataTy AMap = State->get<AllocatedData>();
  if (AMap.isEmpty())
    return State;

  auto *CondBSE = dyn_cast_or_null<BinarySymExpr>(Cond.getAsSymExpr());
  if (!CondBSE)
    return State;
  BinaryOperator::Opcode OpCode = CondBSE->getOpcode();
  if (OpCode != BO_EQ && OpCode != BO_NE)
    return State;

  // Match for a restricted set of patterns for cmparison of error codes.
  // Note, the comparisons of type '0 == st' are transformed into SymIntExpr.
  SymbolRef ReturnSymbol = nullptr;
  if (auto *SIE = dyn_cast<SymIntExpr>(CondBSE)) {
    const llvm::APInt &RHS = SIE->getRHS();
    bool ErrorIsReturned = (OpCode == BO_EQ && RHS != NoErr) ||
                           (OpCode == BO_NE && RHS == NoErr);
    if (!Assumption)
      ErrorIsReturned = !ErrorIsReturned;
    if (ErrorIsReturned)
      ReturnSymbol = SIE->getLHS();
  }

  if (ReturnSymbol)
    for (auto I = AMap.begin(), E = AMap.end(); I != E; ++I) {
      if (ReturnSymbol == I->second.Region)
        State = State->remove<AllocatedData>(I->first);
    }

  return State;
}

void MacOSKeychainAPIChecker::checkDeadSymbols(SymbolReaper &SR,
                                               CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  AllocatedDataTy AMap = State->get<AllocatedData>();
  if (AMap.isEmpty())
    return;

  bool Changed = false;
  AllocationPairVec Errors;
  for (auto I = AMap.begin(), E = AMap.end(); I != E; ++I) {
    if (!SR.isDead(I->first))
      continue;

    Changed = true;
    State = State->remove<AllocatedData>(I->first);
    // If the allocated symbol is null do not report.
    ConstraintManager &CMgr = State->getConstraintManager();
    ConditionTruthVal AllocFailed = CMgr.isNull(State, I.getKey());
    if (AllocFailed.isConstrainedTrue())
      continue;
    Errors.push_back(std::make_pair(I->first, &I->second));
  }
  if (!Changed) {
    // Generate the new, cleaned up state.
    C.addTransition(State);
    return;
  }

  static CheckerProgramPointTag Tag(this, "DeadSymbolsLeak");
  ExplodedNode *N = C.generateNonFatalErrorNode(C.getState(), &Tag);
  if (!N)
    return;

  // Generate the error reports.
  for (const auto &P : Errors)
    C.emitReport(generateAllocatedDataNotReleasedReport(P, N, C));

  // Generate the new, cleaned up state.
  C.addTransition(State, N);
}

ProgramStateRef MacOSKeychainAPIChecker::checkPointerEscape(
    ProgramStateRef State, const InvalidatedSymbols &Escaped,
    const CallEvent *Call, PointerEscapeKind Kind) const {
  // FIXME: This branch doesn't make any sense at all, but it is an overfitted
  // replacement for a previous overfitted code that was making even less sense.
  if (!Call || Call->getDecl())
    return State;

  for (auto I : State->get<AllocatedData>()) {
    SymbolRef Sym = I.first;
    if (Escaped.count(Sym))
      State = State->remove<AllocatedData>(Sym);

    // This checker is special. Most checkers in fact only track symbols of
    // SymbolConjured type, eg. symbols returned from functions such as
    // malloc(). This checker tracks symbols returned as out-parameters.
    //
    // When a function is evaluated conservatively, the out-parameter's pointee
    // base region gets invalidated with a SymbolConjured. If the base region is
    // larger than the region we're interested in, the value we're interested in
    // would be SymbolDerived based on that SymbolConjured. However, such
    // SymbolDerived will never be listed in the Escaped set when the base
    // region is invalidated because ExprEngine doesn't know which symbols
    // were derived from a given symbol, while there can be infinitely many
    // valid symbols derived from any given symbol.
    //
    // Hence the extra boilerplate: remove the derived symbol when its parent
    // symbol escapes.
    //
    if (const auto *SD = dyn_cast<SymbolDerived>(Sym)) {
      SymbolRef ParentSym = SD->getParentSymbol();
      if (Escaped.count(ParentSym))
        State = State->remove<AllocatedData>(Sym);
    }
  }
  return State;
}

std::shared_ptr<PathDiagnosticPiece>
MacOSKeychainAPIChecker::SecKeychainBugVisitor::VisitNode(
    const ExplodedNode *N, BugReporterContext &BRC, BugReport &BR) {
  const AllocationState *AS = N->getState()->get<AllocatedData>(Sym);
  if (!AS)
    return nullptr;
  const AllocationState *ASPrev =
      N->getFirstPred()->getState()->get<AllocatedData>(Sym);
  if (ASPrev)
    return nullptr;

  // (!ASPrev && AS) ~ We started tracking symbol in node N, it must be the
  // allocation site.
  const CallExpr *CE =
      cast<CallExpr>(N->getLocation().castAs<StmtPoint>().getStmt());
  const FunctionDecl *funDecl = CE->getDirectCallee();
  assert(funDecl && "We do not support indirect function calls as of now.");
  StringRef funName = funDecl->getName();

  // Get the expression of the corresponding argument.
  unsigned Idx = getTrackedFunctionIndex(funName, true);
  assert(Idx != InvalidIdx && "This should be a call to an allocator.");
  const Expr *ArgExpr = CE->getArg(FunctionsToTrack[Idx].Param);
  PathDiagnosticLocation Pos(ArgExpr, BRC.getSourceManager(),
                             N->getLocationContext());
  return std::make_shared<PathDiagnosticEventPiece>(Pos,
                                                    "Data is allocated here.");
}

void MacOSKeychainAPIChecker::printState(raw_ostream &Out,
                                         ProgramStateRef State,
                                         const char *NL,
                                         const char *Sep) const {

  AllocatedDataTy AMap = State->get<AllocatedData>();

  if (!AMap.isEmpty()) {
    Out << Sep << "KeychainAPIChecker :" << NL;
    for (auto I = AMap.begin(), E = AMap.end(); I != E; ++I) {
      I.getKey()->dumpToStream(Out);
    }
  }
}


void ento::registerMacOSKeychainAPIChecker(CheckerManager &mgr) {
  mgr.registerChecker<MacOSKeychainAPIChecker>();
}
