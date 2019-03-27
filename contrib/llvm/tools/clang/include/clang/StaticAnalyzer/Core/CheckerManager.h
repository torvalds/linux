//===- CheckerManager.h - Static Analyzer Checker Manager -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines the Static Analyzer Checker Manager.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_CHECKERMANAGER_H
#define LLVM_CLANG_STATICANALYZER_CORE_CHECKERMANAGER_H

#include "clang/Analysis/ProgramPoint.h"
#include "clang/Basic/LangOptions.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState_Fwd.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/Store.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <vector>

namespace clang {

class AnalyzerOptions;
class CallExpr;
class CXXNewExpr;
class Decl;
class LocationContext;
class Stmt;
class TranslationUnitDecl;

namespace ento {

class AnalysisManager;
class BugReporter;
class CallEvent;
class CheckerBase;
class CheckerContext;
class CheckerRegistry;
class ExplodedGraph;
class ExplodedNode;
class ExplodedNodeSet;
class ExprEngine;
class MemRegion;
struct NodeBuilderContext;
class ObjCMethodCall;
class RegionAndSymbolInvalidationTraits;
class SVal;
class SymbolReaper;

template <typename T> class CheckerFn;

template <typename RET, typename... Ps>
class CheckerFn<RET(Ps...)> {
  using Func = RET (*)(void *, Ps...);

  Func Fn;

public:
  CheckerBase *Checker;

  CheckerFn(CheckerBase *checker, Func fn) : Fn(fn), Checker(checker) {}

  RET operator()(Ps... ps) const {
    return Fn(Checker, ps...);
  }
};

/// Describes the different reasons a pointer escapes
/// during analysis.
enum PointerEscapeKind {
  /// A pointer escapes due to binding its value to a location
  /// that the analyzer cannot track.
  PSK_EscapeOnBind,

  /// The pointer has been passed to a function call directly.
  PSK_DirectEscapeOnCall,

  /// The pointer has been passed to a function indirectly.
  /// For example, the pointer is accessible through an
  /// argument to a function.
  PSK_IndirectEscapeOnCall,

  /// The reason for pointer escape is unknown. For example,
  /// a region containing this pointer is invalidated.
  PSK_EscapeOther
};

// This wrapper is used to ensure that only StringRefs originating from the
// CheckerRegistry are used as check names. We want to make sure all check
// name strings have a lifetime that keeps them alive at least until the path
// diagnostics have been processed.
class CheckName {
  friend class ::clang::ento::CheckerRegistry;

  StringRef Name;

  explicit CheckName(StringRef Name) : Name(Name) {}

public:
  CheckName() = default;

  StringRef getName() const { return Name; }
};

enum class ObjCMessageVisitKind {
  Pre,
  Post,
  MessageNil
};

class CheckerManager {
  ASTContext &Context;
  const LangOptions LangOpts;
  AnalyzerOptions &AOptions;
  CheckName CurrentCheckName;

public:
  CheckerManager(ASTContext &Context, AnalyzerOptions &AOptions)
      : Context(Context), LangOpts(Context.getLangOpts()), AOptions(AOptions) {}

  ~CheckerManager();

  void setCurrentCheckName(CheckName name) { CurrentCheckName = name; }
  CheckName getCurrentCheckName() const { return CurrentCheckName; }

  bool hasPathSensitiveCheckers() const;

  void finishedCheckerRegistration();

  const LangOptions &getLangOpts() const { return LangOpts; }
  AnalyzerOptions &getAnalyzerOptions() { return AOptions; }
  ASTContext &getASTContext() { return Context; }

  using CheckerRef = CheckerBase *;
  using CheckerTag = const void *;
  using CheckerDtor = CheckerFn<void ()>;

//===----------------------------------------------------------------------===//
// registerChecker
//===----------------------------------------------------------------------===//

  /// Used to register checkers.
  /// All arguments are automatically passed through to the checker
  /// constructor.
  ///
  /// \returns a pointer to the checker object.
  template <typename CHECKER, typename... AT>
  CHECKER *registerChecker(AT &&... Args) {
    CheckerTag tag = getTag<CHECKER>();
    CheckerRef &ref = CheckerTags[tag];
    if (ref)
      return static_cast<CHECKER *>(ref); // already registered.

    CHECKER *checker = new CHECKER(std::forward<AT>(Args)...);
    checker->Name = CurrentCheckName;
    CheckerDtors.push_back(CheckerDtor(checker, destruct<CHECKER>));
    CHECKER::_register(checker, *this);
    ref = checker;
    return checker;
  }

//===----------------------------------------------------------------------===//
// Functions for running checkers for AST traversing..
//===----------------------------------------------------------------------===//

  /// Run checkers handling Decls.
  void runCheckersOnASTDecl(const Decl *D, AnalysisManager& mgr,
                            BugReporter &BR);

  /// Run checkers handling Decls containing a Stmt body.
  void runCheckersOnASTBody(const Decl *D, AnalysisManager& mgr,
                            BugReporter &BR);

//===----------------------------------------------------------------------===//
// Functions for running checkers for path-sensitive checking.
//===----------------------------------------------------------------------===//

  /// Run checkers for pre-visiting Stmts.
  ///
  /// The notification is performed for every explored CFGElement, which does
  /// not include the control flow statements such as IfStmt.
  ///
  /// \sa runCheckersForBranchCondition, runCheckersForPostStmt
  void runCheckersForPreStmt(ExplodedNodeSet &Dst,
                             const ExplodedNodeSet &Src,
                             const Stmt *S,
                             ExprEngine &Eng) {
    runCheckersForStmt(/*isPreVisit=*/true, Dst, Src, S, Eng);
  }

  /// Run checkers for post-visiting Stmts.
  ///
  /// The notification is performed for every explored CFGElement, which does
  /// not include the control flow statements such as IfStmt.
  ///
  /// \sa runCheckersForBranchCondition, runCheckersForPreStmt
  void runCheckersForPostStmt(ExplodedNodeSet &Dst,
                              const ExplodedNodeSet &Src,
                              const Stmt *S,
                              ExprEngine &Eng,
                              bool wasInlined = false) {
    runCheckersForStmt(/*isPreVisit=*/false, Dst, Src, S, Eng, wasInlined);
  }

  /// Run checkers for visiting Stmts.
  void runCheckersForStmt(bool isPreVisit,
                          ExplodedNodeSet &Dst, const ExplodedNodeSet &Src,
                          const Stmt *S, ExprEngine &Eng,
                          bool wasInlined = false);

  /// Run checkers for pre-visiting obj-c messages.
  void runCheckersForPreObjCMessage(ExplodedNodeSet &Dst,
                                    const ExplodedNodeSet &Src,
                                    const ObjCMethodCall &msg,
                                    ExprEngine &Eng) {
    runCheckersForObjCMessage(ObjCMessageVisitKind::Pre, Dst, Src, msg, Eng);
  }

  /// Run checkers for post-visiting obj-c messages.
  void runCheckersForPostObjCMessage(ExplodedNodeSet &Dst,
                                     const ExplodedNodeSet &Src,
                                     const ObjCMethodCall &msg,
                                     ExprEngine &Eng,
                                     bool wasInlined = false) {
    runCheckersForObjCMessage(ObjCMessageVisitKind::Post, Dst, Src, msg, Eng,
                              wasInlined);
  }

  /// Run checkers for visiting an obj-c message to nil.
  void runCheckersForObjCMessageNil(ExplodedNodeSet &Dst,
                                    const ExplodedNodeSet &Src,
                                    const ObjCMethodCall &msg,
                                    ExprEngine &Eng) {
    runCheckersForObjCMessage(ObjCMessageVisitKind::MessageNil, Dst, Src, msg,
                              Eng);
  }

  /// Run checkers for visiting obj-c messages.
  void runCheckersForObjCMessage(ObjCMessageVisitKind visitKind,
                                 ExplodedNodeSet &Dst,
                                 const ExplodedNodeSet &Src,
                                 const ObjCMethodCall &msg, ExprEngine &Eng,
                                 bool wasInlined = false);

  /// Run checkers for pre-visiting obj-c messages.
  void runCheckersForPreCall(ExplodedNodeSet &Dst, const ExplodedNodeSet &Src,
                             const CallEvent &Call, ExprEngine &Eng) {
    runCheckersForCallEvent(/*isPreVisit=*/true, Dst, Src, Call, Eng);
  }

  /// Run checkers for post-visiting obj-c messages.
  void runCheckersForPostCall(ExplodedNodeSet &Dst, const ExplodedNodeSet &Src,
                              const CallEvent &Call, ExprEngine &Eng,
                              bool wasInlined = false) {
    runCheckersForCallEvent(/*isPreVisit=*/false, Dst, Src, Call, Eng,
                            wasInlined);
  }

  /// Run checkers for visiting obj-c messages.
  void runCheckersForCallEvent(bool isPreVisit, ExplodedNodeSet &Dst,
                               const ExplodedNodeSet &Src,
                               const CallEvent &Call, ExprEngine &Eng,
                               bool wasInlined = false);

  /// Run checkers for load/store of a location.
  void runCheckersForLocation(ExplodedNodeSet &Dst,
                              const ExplodedNodeSet &Src,
                              SVal location,
                              bool isLoad,
                              const Stmt *NodeEx,
                              const Stmt *BoundEx,
                              ExprEngine &Eng);

  /// Run checkers for binding of a value to a location.
  void runCheckersForBind(ExplodedNodeSet &Dst,
                          const ExplodedNodeSet &Src,
                          SVal location, SVal val,
                          const Stmt *S, ExprEngine &Eng,
                          const ProgramPoint &PP);

  /// Run checkers for end of analysis.
  void runCheckersForEndAnalysis(ExplodedGraph &G, BugReporter &BR,
                                 ExprEngine &Eng);

  /// Run checkers on beginning of function.
  void runCheckersForBeginFunction(ExplodedNodeSet &Dst,
                                   const BlockEdge &L,
                                   ExplodedNode *Pred,
                                   ExprEngine &Eng);

  /// Run checkers on end of function.
  void runCheckersForEndFunction(NodeBuilderContext &BC,
                                 ExplodedNodeSet &Dst,
                                 ExplodedNode *Pred,
                                 ExprEngine &Eng,
                                 const ReturnStmt *RS);

  /// Run checkers for branch condition.
  void runCheckersForBranchCondition(const Stmt *condition,
                                     ExplodedNodeSet &Dst, ExplodedNode *Pred,
                                     ExprEngine &Eng);

  /// Run checkers between C++ operator new and constructor calls.
  void runCheckersForNewAllocator(const CXXNewExpr *NE, SVal Target,
                                  ExplodedNodeSet &Dst,
                                  ExplodedNode *Pred,
                                  ExprEngine &Eng,
                                  bool wasInlined = false);

  /// Run checkers for live symbols.
  ///
  /// Allows modifying SymbolReaper object. For example, checkers can explicitly
  /// register symbols of interest as live. These symbols will not be marked
  /// dead and removed.
  void runCheckersForLiveSymbols(ProgramStateRef state,
                                 SymbolReaper &SymReaper);

  /// Run checkers for dead symbols.
  ///
  /// Notifies checkers when symbols become dead. For example, this allows
  /// checkers to aggressively clean up/reduce the checker state and produce
  /// precise diagnostics.
  void runCheckersForDeadSymbols(ExplodedNodeSet &Dst,
                                 const ExplodedNodeSet &Src,
                                 SymbolReaper &SymReaper, const Stmt *S,
                                 ExprEngine &Eng,
                                 ProgramPoint::Kind K);

  /// Run checkers for region changes.
  ///
  /// This corresponds to the check::RegionChanges callback.
  /// \param state The current program state.
  /// \param invalidated A set of all symbols potentially touched by the change.
  /// \param ExplicitRegions The regions explicitly requested for invalidation.
  ///   For example, in the case of a function call, these would be arguments.
  /// \param Regions The transitive closure of accessible regions,
  ///   i.e. all regions that may have been touched by this change.
  /// \param Call The call expression wrapper if the regions are invalidated
  ///   by a call.
  ProgramStateRef
  runCheckersForRegionChanges(ProgramStateRef state,
                              const InvalidatedSymbols *invalidated,
                              ArrayRef<const MemRegion *> ExplicitRegions,
                              ArrayRef<const MemRegion *> Regions,
                              const LocationContext *LCtx,
                              const CallEvent *Call);

  /// Run checkers when pointers escape.
  ///
  /// This notifies the checkers about pointer escape, which occurs whenever
  /// the analyzer cannot track the symbol any more. For example, as a
  /// result of assigning a pointer into a global or when it's passed to a
  /// function call the analyzer cannot model.
  ///
  /// \param State The state at the point of escape.
  /// \param Escaped The list of escaped symbols.
  /// \param Call The corresponding CallEvent, if the symbols escape as
  ///        parameters to the given call.
  /// \param Kind The reason of pointer escape.
  /// \param ITraits Information about invalidation for a particular
  ///        region/symbol.
  /// \returns Checkers can modify the state by returning a new one.
  ProgramStateRef
  runCheckersForPointerEscape(ProgramStateRef State,
                              const InvalidatedSymbols &Escaped,
                              const CallEvent *Call,
                              PointerEscapeKind Kind,
                              RegionAndSymbolInvalidationTraits *ITraits);

  /// Run checkers for handling assumptions on symbolic values.
  ProgramStateRef runCheckersForEvalAssume(ProgramStateRef state,
                                           SVal Cond, bool Assumption);

  /// Run checkers for evaluating a call.
  ///
  /// Warning: Currently, the CallEvent MUST come from a CallExpr!
  void runCheckersForEvalCall(ExplodedNodeSet &Dst,
                              const ExplodedNodeSet &Src,
                              const CallEvent &CE, ExprEngine &Eng);

  /// Run checkers for the entire Translation Unit.
  void runCheckersOnEndOfTranslationUnit(const TranslationUnitDecl *TU,
                                         AnalysisManager &mgr,
                                         BugReporter &BR);

  /// Run checkers for debug-printing a ProgramState.
  ///
  /// Unlike most other callbacks, any checker can simply implement the virtual
  /// method CheckerBase::printState if it has custom data to print.
  /// \param Out The output stream
  /// \param State The state being printed
  /// \param NL The preferred representation of a newline.
  /// \param Sep The preferred separator between different kinds of data.
  void runCheckersForPrintState(raw_ostream &Out, ProgramStateRef State,
                                const char *NL, const char *Sep);

//===----------------------------------------------------------------------===//
// Internal registration functions for AST traversing.
//===----------------------------------------------------------------------===//

  // Functions used by the registration mechanism, checkers should not touch
  // these directly.

  using CheckDeclFunc =
      CheckerFn<void (const Decl *, AnalysisManager&, BugReporter &)>;

  using HandlesDeclFunc = bool (*)(const Decl *D);

  void _registerForDecl(CheckDeclFunc checkfn, HandlesDeclFunc isForDeclFn);

  void _registerForBody(CheckDeclFunc checkfn);

//===----------------------------------------------------------------------===//
// Internal registration functions for path-sensitive checking.
//===----------------------------------------------------------------------===//

  using CheckStmtFunc = CheckerFn<void (const Stmt *, CheckerContext &)>;

  using CheckObjCMessageFunc =
      CheckerFn<void (const ObjCMethodCall &, CheckerContext &)>;

  using CheckCallFunc =
      CheckerFn<void (const CallEvent &, CheckerContext &)>;

  using CheckLocationFunc =
      CheckerFn<void (const SVal &location, bool isLoad, const Stmt *S,
                      CheckerContext &)>;

  using CheckBindFunc =
      CheckerFn<void (const SVal &location, const SVal &val, const Stmt *S,
                      CheckerContext &)>;

  using CheckEndAnalysisFunc =
      CheckerFn<void (ExplodedGraph &, BugReporter &, ExprEngine &)>;

  using CheckBeginFunctionFunc = CheckerFn<void (CheckerContext &)>;

  using CheckEndFunctionFunc =
      CheckerFn<void (const ReturnStmt *, CheckerContext &)>;

  using CheckBranchConditionFunc =
      CheckerFn<void (const Stmt *, CheckerContext &)>;

  using CheckNewAllocatorFunc =
      CheckerFn<void (const CXXNewExpr *, SVal, CheckerContext &)>;

  using CheckDeadSymbolsFunc =
      CheckerFn<void (SymbolReaper &, CheckerContext &)>;

  using CheckLiveSymbolsFunc = CheckerFn<void (ProgramStateRef,SymbolReaper &)>;

  using CheckRegionChangesFunc =
      CheckerFn<ProgramStateRef (ProgramStateRef,
                                 const InvalidatedSymbols *symbols,
                                 ArrayRef<const MemRegion *> ExplicitRegions,
                                 ArrayRef<const MemRegion *> Regions,
                                 const LocationContext *LCtx,
                                 const CallEvent *Call)>;

  using CheckPointerEscapeFunc =
      CheckerFn<ProgramStateRef (ProgramStateRef,
                                 const InvalidatedSymbols &Escaped,
                                 const CallEvent *Call, PointerEscapeKind Kind,
                                 RegionAndSymbolInvalidationTraits *ITraits)>;

  using EvalAssumeFunc =
      CheckerFn<ProgramStateRef (ProgramStateRef, const SVal &cond,
                                 bool assumption)>;

  using EvalCallFunc = CheckerFn<bool (const CallExpr *, CheckerContext &)>;

  using CheckEndOfTranslationUnit =
      CheckerFn<void (const TranslationUnitDecl *, AnalysisManager &,
                      BugReporter &)>;

  using HandlesStmtFunc = bool (*)(const Stmt *D);

  void _registerForPreStmt(CheckStmtFunc checkfn,
                           HandlesStmtFunc isForStmtFn);
  void _registerForPostStmt(CheckStmtFunc checkfn,
                            HandlesStmtFunc isForStmtFn);

  void _registerForPreObjCMessage(CheckObjCMessageFunc checkfn);
  void _registerForPostObjCMessage(CheckObjCMessageFunc checkfn);

  void _registerForObjCMessageNil(CheckObjCMessageFunc checkfn);

  void _registerForPreCall(CheckCallFunc checkfn);
  void _registerForPostCall(CheckCallFunc checkfn);

  void _registerForLocation(CheckLocationFunc checkfn);

  void _registerForBind(CheckBindFunc checkfn);

  void _registerForEndAnalysis(CheckEndAnalysisFunc checkfn);

  void _registerForBeginFunction(CheckBeginFunctionFunc checkfn);
  void _registerForEndFunction(CheckEndFunctionFunc checkfn);

  void _registerForBranchCondition(CheckBranchConditionFunc checkfn);

  void _registerForNewAllocator(CheckNewAllocatorFunc checkfn);

  void _registerForLiveSymbols(CheckLiveSymbolsFunc checkfn);

  void _registerForDeadSymbols(CheckDeadSymbolsFunc checkfn);

  void _registerForRegionChanges(CheckRegionChangesFunc checkfn);

  void _registerForPointerEscape(CheckPointerEscapeFunc checkfn);

  void _registerForConstPointerEscape(CheckPointerEscapeFunc checkfn);

  void _registerForEvalAssume(EvalAssumeFunc checkfn);

  void _registerForEvalCall(EvalCallFunc checkfn);

  void _registerForEndOfTranslationUnit(CheckEndOfTranslationUnit checkfn);

//===----------------------------------------------------------------------===//
// Internal registration functions for events.
//===----------------------------------------------------------------------===//

  using EventTag = void *;
  using CheckEventFunc = CheckerFn<void (const void *event)>;

  template <typename EVENT>
  void _registerListenerForEvent(CheckEventFunc checkfn) {
    EventInfo &info = Events[&EVENT::Tag];
    info.Checkers.push_back(checkfn);
  }

  template <typename EVENT>
  void _registerDispatcherForEvent() {
    EventInfo &info = Events[&EVENT::Tag];
    info.HasDispatcher = true;
  }

  template <typename EVENT>
  void _dispatchEvent(const EVENT &event) const {
    EventsTy::const_iterator I = Events.find(&EVENT::Tag);
    if (I == Events.end())
      return;
    const EventInfo &info = I->second;
    for (const auto Checker : info.Checkers)
      Checker(&event);
  }

//===----------------------------------------------------------------------===//
// Implementation details.
//===----------------------------------------------------------------------===//

private:
  template <typename CHECKER>
  static void destruct(void *obj) { delete static_cast<CHECKER *>(obj); }

  template <typename T>
  static void *getTag() { static int tag; return &tag; }

  llvm::DenseMap<CheckerTag, CheckerRef> CheckerTags;

  std::vector<CheckerDtor> CheckerDtors;

  struct DeclCheckerInfo {
    CheckDeclFunc CheckFn;
    HandlesDeclFunc IsForDeclFn;
  };
  std::vector<DeclCheckerInfo> DeclCheckers;

  std::vector<CheckDeclFunc> BodyCheckers;

  using CachedDeclCheckers = SmallVector<CheckDeclFunc, 4>;
  using CachedDeclCheckersMapTy = llvm::DenseMap<unsigned, CachedDeclCheckers>;
  CachedDeclCheckersMapTy CachedDeclCheckersMap;

  struct StmtCheckerInfo {
    CheckStmtFunc CheckFn;
    HandlesStmtFunc IsForStmtFn;
    bool IsPreVisit;
  };
  std::vector<StmtCheckerInfo> StmtCheckers;

  using CachedStmtCheckers = SmallVector<CheckStmtFunc, 4>;
  using CachedStmtCheckersMapTy = llvm::DenseMap<unsigned, CachedStmtCheckers>;
  CachedStmtCheckersMapTy CachedStmtCheckersMap;

  const CachedStmtCheckers &getCachedStmtCheckersFor(const Stmt *S,
                                                     bool isPreVisit);

  /// Returns the checkers that have registered for callbacks of the
  /// given \p Kind.
  const std::vector<CheckObjCMessageFunc> &
  getObjCMessageCheckers(ObjCMessageVisitKind Kind);

  std::vector<CheckObjCMessageFunc> PreObjCMessageCheckers;
  std::vector<CheckObjCMessageFunc> PostObjCMessageCheckers;
  std::vector<CheckObjCMessageFunc> ObjCMessageNilCheckers;

  std::vector<CheckCallFunc> PreCallCheckers;
  std::vector<CheckCallFunc> PostCallCheckers;

  std::vector<CheckLocationFunc> LocationCheckers;

  std::vector<CheckBindFunc> BindCheckers;

  std::vector<CheckEndAnalysisFunc> EndAnalysisCheckers;

  std::vector<CheckBeginFunctionFunc> BeginFunctionCheckers;
  std::vector<CheckEndFunctionFunc> EndFunctionCheckers;

  std::vector<CheckBranchConditionFunc> BranchConditionCheckers;

  std::vector<CheckNewAllocatorFunc> NewAllocatorCheckers;

  std::vector<CheckLiveSymbolsFunc> LiveSymbolsCheckers;

  std::vector<CheckDeadSymbolsFunc> DeadSymbolsCheckers;

  std::vector<CheckRegionChangesFunc> RegionChangesCheckers;

  std::vector<CheckPointerEscapeFunc> PointerEscapeCheckers;

  std::vector<EvalAssumeFunc> EvalAssumeCheckers;

  std::vector<EvalCallFunc> EvalCallCheckers;

  std::vector<CheckEndOfTranslationUnit> EndOfTranslationUnitCheckers;

  struct EventInfo {
    SmallVector<CheckEventFunc, 4> Checkers;
    bool HasDispatcher = false;

    EventInfo() = default;
  };

  using EventsTy = llvm::DenseMap<EventTag, EventInfo>;
  EventsTy Events;
};

} // namespace ento

} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_CHECKERMANAGER_H
