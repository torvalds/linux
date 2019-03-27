//==- CheckObjCDealloc.cpp - Check ObjC -dealloc implementation --*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This checker analyzes Objective-C -dealloc methods and their callees
//  to warn about improper releasing of instance variables that back synthesized
// properties. It warns about missing releases in the following cases:
//  - When a class has a synthesized instance variable for a 'retain' or 'copy'
//    property and lacks a -dealloc method in its implementation.
//  - When a class has a synthesized instance variable for a 'retain'/'copy'
//   property but the ivar is not released in -dealloc by either -release
//   or by nilling out the property.
//
//  It warns about extra releases in -dealloc (but not in callees) when a
//  synthesized instance variable is released in the following cases:
//  - When the property is 'assign' and is not 'readonly'.
//  - When the property is 'weak'.
//
//  This checker only warns for instance variables synthesized to back
//  properties. Handling the more general case would require inferring whether
//  an instance variable is stored retained or not. For synthesized properties,
//  this is specified in the property declaration itself.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprObjC.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/BugReporter/PathDiagnostic.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

/// Indicates whether an instance variable is required to be released in
/// -dealloc.
enum class ReleaseRequirement {
  /// The instance variable must be released, either by calling
  /// -release on it directly or by nilling it out with a property setter.
  MustRelease,

  /// The instance variable must not be directly released with -release.
  MustNotReleaseDirectly,

  /// The requirement for the instance variable could not be determined.
  Unknown
};

/// Returns true if the property implementation is synthesized and the
/// type of the property is retainable.
static bool isSynthesizedRetainableProperty(const ObjCPropertyImplDecl *I,
                                            const ObjCIvarDecl **ID,
                                            const ObjCPropertyDecl **PD) {

  if (I->getPropertyImplementation() != ObjCPropertyImplDecl::Synthesize)
    return false;

  (*ID) = I->getPropertyIvarDecl();
  if (!(*ID))
    return false;

  QualType T = (*ID)->getType();
  if (!T->isObjCRetainableType())
    return false;

  (*PD) = I->getPropertyDecl();
  // Shouldn't be able to synthesize a property that doesn't exist.
  assert(*PD);

  return true;
}

namespace {

class ObjCDeallocChecker
    : public Checker<check::ASTDecl<ObjCImplementationDecl>,
                     check::PreObjCMessage, check::PostObjCMessage,
                     check::PreCall,
                     check::BeginFunction, check::EndFunction,
                     eval::Assume,
                     check::PointerEscape,
                     check::PreStmt<ReturnStmt>> {

  mutable IdentifierInfo *NSObjectII, *SenTestCaseII, *XCTestCaseII,
      *Block_releaseII, *CIFilterII;

  mutable Selector DeallocSel, ReleaseSel;

  std::unique_ptr<BugType> MissingReleaseBugType;
  std::unique_ptr<BugType> ExtraReleaseBugType;
  std::unique_ptr<BugType> MistakenDeallocBugType;

public:
  ObjCDeallocChecker();

  void checkASTDecl(const ObjCImplementationDecl *D, AnalysisManager& Mgr,
                    BugReporter &BR) const;
  void checkBeginFunction(CheckerContext &Ctx) const;
  void checkPreObjCMessage(const ObjCMethodCall &M, CheckerContext &C) const;
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPostObjCMessage(const ObjCMethodCall &M, CheckerContext &C) const;

  ProgramStateRef evalAssume(ProgramStateRef State, SVal Cond,
                             bool Assumption) const;

  ProgramStateRef checkPointerEscape(ProgramStateRef State,
                                     const InvalidatedSymbols &Escaped,
                                     const CallEvent *Call,
                                     PointerEscapeKind Kind) const;
  void checkPreStmt(const ReturnStmt *RS, CheckerContext &C) const;
  void checkEndFunction(const ReturnStmt *RS, CheckerContext &Ctx) const;

private:
  void diagnoseMissingReleases(CheckerContext &C) const;

  bool diagnoseExtraRelease(SymbolRef ReleasedValue, const ObjCMethodCall &M,
                            CheckerContext &C) const;

  bool diagnoseMistakenDealloc(SymbolRef DeallocedValue,
                               const ObjCMethodCall &M,
                               CheckerContext &C) const;

  SymbolRef getValueReleasedByNillingOut(const ObjCMethodCall &M,
                                         CheckerContext &C) const;

  const ObjCIvarRegion *getIvarRegionForIvarSymbol(SymbolRef IvarSym) const;
  SymbolRef getInstanceSymbolFromIvarSymbol(SymbolRef IvarSym) const;

  const ObjCPropertyImplDecl*
  findPropertyOnDeallocatingInstance(SymbolRef IvarSym,
                                     CheckerContext &C) const;

  ReleaseRequirement
  getDeallocReleaseRequirement(const ObjCPropertyImplDecl *PropImpl) const;

  bool isInInstanceDealloc(const CheckerContext &C, SVal &SelfValOut) const;
  bool isInInstanceDealloc(const CheckerContext &C, const LocationContext *LCtx,
                           SVal &SelfValOut) const;
  bool instanceDeallocIsOnStack(const CheckerContext &C,
                                SVal &InstanceValOut) const;

  bool isSuperDeallocMessage(const ObjCMethodCall &M) const;

  const ObjCImplDecl *getContainingObjCImpl(const LocationContext *LCtx) const;

  const ObjCPropertyDecl *
  findShadowedPropertyDecl(const ObjCPropertyImplDecl *PropImpl) const;

  void transitionToReleaseValue(CheckerContext &C, SymbolRef Value) const;
  ProgramStateRef removeValueRequiringRelease(ProgramStateRef State,
                                              SymbolRef InstanceSym,
                                              SymbolRef ValueSym) const;

  void initIdentifierInfoAndSelectors(ASTContext &Ctx) const;

  bool classHasSeparateTeardown(const ObjCInterfaceDecl *ID) const;

  bool isReleasedByCIFilterDealloc(const ObjCPropertyImplDecl *PropImpl) const;
  bool isNibLoadedIvarWithoutRetain(const ObjCPropertyImplDecl *PropImpl) const;
};
} // End anonymous namespace.


/// Maps from the symbol for a class instance to the set of
/// symbols remaining that must be released in -dealloc.
REGISTER_SET_FACTORY_WITH_PROGRAMSTATE(SymbolSet, SymbolRef)
REGISTER_MAP_WITH_PROGRAMSTATE(UnreleasedIvarMap, SymbolRef, SymbolSet)


/// An AST check that diagnose when the class requires a -dealloc method and
/// is missing one.
void ObjCDeallocChecker::checkASTDecl(const ObjCImplementationDecl *D,
                                      AnalysisManager &Mgr,
                                      BugReporter &BR) const {
  assert(Mgr.getLangOpts().getGC() != LangOptions::GCOnly);
  assert(!Mgr.getLangOpts().ObjCAutoRefCount);
  initIdentifierInfoAndSelectors(Mgr.getASTContext());

  const ObjCInterfaceDecl *ID = D->getClassInterface();
  // If the class is known to have a lifecycle with a separate teardown method
  // then it may not require a -dealloc method.
  if (classHasSeparateTeardown(ID))
    return;

  // Does the class contain any synthesized properties that are retainable?
  // If not, skip the check entirely.
  const ObjCPropertyImplDecl *PropImplRequiringRelease = nullptr;
  bool HasOthers = false;
  for (const auto *I : D->property_impls()) {
    if (getDeallocReleaseRequirement(I) == ReleaseRequirement::MustRelease) {
      if (!PropImplRequiringRelease)
        PropImplRequiringRelease = I;
      else {
        HasOthers = true;
        break;
      }
    }
  }

  if (!PropImplRequiringRelease)
    return;

  const ObjCMethodDecl *MD = nullptr;

  // Scan the instance methods for "dealloc".
  for (const auto *I : D->instance_methods()) {
    if (I->getSelector() == DeallocSel) {
      MD = I;
      break;
    }
  }

  if (!MD) { // No dealloc found.
    const char* Name = "Missing -dealloc";

    std::string Buf;
    llvm::raw_string_ostream OS(Buf);
    OS << "'" << *D << "' lacks a 'dealloc' instance method but "
       << "must release '" << *PropImplRequiringRelease->getPropertyIvarDecl()
       << "'";

    if (HasOthers)
      OS << " and others";
    PathDiagnosticLocation DLoc =
        PathDiagnosticLocation::createBegin(D, BR.getSourceManager());

    BR.EmitBasicReport(D, this, Name, categories::CoreFoundationObjectiveC,
                       OS.str(), DLoc);
    return;
  }
}

/// If this is the beginning of -dealloc, mark the values initially stored in
/// instance variables that must be released by the end of -dealloc
/// as unreleased in the state.
void ObjCDeallocChecker::checkBeginFunction(
    CheckerContext &C) const {
  initIdentifierInfoAndSelectors(C.getASTContext());

  // Only do this if the current method is -dealloc.
  SVal SelfVal;
  if (!isInInstanceDealloc(C, SelfVal))
    return;

  SymbolRef SelfSymbol = SelfVal.getAsSymbol();

  const LocationContext *LCtx = C.getLocationContext();
  ProgramStateRef InitialState = C.getState();

  ProgramStateRef State = InitialState;

  SymbolSet::Factory &F = State->getStateManager().get_context<SymbolSet>();

  // Symbols that must be released by the end of the -dealloc;
  SymbolSet RequiredReleases = F.getEmptySet();

  // If we're an inlined -dealloc, we should add our symbols to the existing
  // set from our subclass.
  if (const SymbolSet *CurrSet = State->get<UnreleasedIvarMap>(SelfSymbol))
    RequiredReleases = *CurrSet;

  for (auto *PropImpl : getContainingObjCImpl(LCtx)->property_impls()) {
    ReleaseRequirement Requirement = getDeallocReleaseRequirement(PropImpl);
    if (Requirement != ReleaseRequirement::MustRelease)
      continue;

    SVal LVal = State->getLValue(PropImpl->getPropertyIvarDecl(), SelfVal);
    Optional<Loc> LValLoc = LVal.getAs<Loc>();
    if (!LValLoc)
      continue;

    SVal InitialVal = State->getSVal(LValLoc.getValue());
    SymbolRef Symbol = InitialVal.getAsSymbol();
    if (!Symbol || !isa<SymbolRegionValue>(Symbol))
      continue;

    // Mark the value as requiring a release.
    RequiredReleases = F.add(RequiredReleases, Symbol);
  }

  if (!RequiredReleases.isEmpty()) {
    State = State->set<UnreleasedIvarMap>(SelfSymbol, RequiredReleases);
  }

  if (State != InitialState) {
    C.addTransition(State);
  }
}

/// Given a symbol for an ivar, return the ivar region it was loaded from.
/// Returns nullptr if the instance symbol cannot be found.
const ObjCIvarRegion *
ObjCDeallocChecker::getIvarRegionForIvarSymbol(SymbolRef IvarSym) const {
  return dyn_cast_or_null<ObjCIvarRegion>(IvarSym->getOriginRegion());
}

/// Given a symbol for an ivar, return a symbol for the instance containing
/// the ivar. Returns nullptr if the instance symbol cannot be found.
SymbolRef
ObjCDeallocChecker::getInstanceSymbolFromIvarSymbol(SymbolRef IvarSym) const {

  const ObjCIvarRegion *IvarRegion = getIvarRegionForIvarSymbol(IvarSym);
  if (!IvarRegion)
    return nullptr;

  return IvarRegion->getSymbolicBase()->getSymbol();
}

/// If we are in -dealloc or -dealloc is on the stack, handle the call if it is
/// a release or a nilling-out property setter.
void ObjCDeallocChecker::checkPreObjCMessage(
    const ObjCMethodCall &M, CheckerContext &C) const {
  // Only run if -dealloc is on the stack.
  SVal DeallocedInstance;
  if (!instanceDeallocIsOnStack(C, DeallocedInstance))
    return;

  SymbolRef ReleasedValue = nullptr;

  if (M.getSelector() == ReleaseSel) {
    ReleasedValue = M.getReceiverSVal().getAsSymbol();
  } else if (M.getSelector() == DeallocSel && !M.isReceiverSelfOrSuper()) {
    if (diagnoseMistakenDealloc(M.getReceiverSVal().getAsSymbol(), M, C))
      return;
  }

  if (ReleasedValue) {
    // An instance variable symbol was released with -release:
    //    [_property release];
    if (diagnoseExtraRelease(ReleasedValue,M, C))
      return;
  } else {
    // An instance variable symbol was released nilling out its property:
    //    self.property = nil;
    ReleasedValue = getValueReleasedByNillingOut(M, C);
  }

  if (!ReleasedValue)
    return;

  transitionToReleaseValue(C, ReleasedValue);
}

/// If we are in -dealloc or -dealloc is on the stack, handle the call if it is
/// call to Block_release().
void ObjCDeallocChecker::checkPreCall(const CallEvent &Call,
                                      CheckerContext &C) const {
  const IdentifierInfo *II = Call.getCalleeIdentifier();
  if (II != Block_releaseII)
    return;

  if (Call.getNumArgs() != 1)
    return;

  SymbolRef ReleasedValue = Call.getArgSVal(0).getAsSymbol();
  if (!ReleasedValue)
    return;

  transitionToReleaseValue(C, ReleasedValue);
}
/// If the message was a call to '[super dealloc]', diagnose any missing
/// releases.
void ObjCDeallocChecker::checkPostObjCMessage(
    const ObjCMethodCall &M, CheckerContext &C) const {
  // We perform this check post-message so that if the super -dealloc
  // calls a helper method and that this class overrides, any ivars released in
  // the helper method will be recorded before checking.
  if (isSuperDeallocMessage(M))
    diagnoseMissingReleases(C);
}

/// Check for missing releases even when -dealloc does not call
/// '[super dealloc]'.
void ObjCDeallocChecker::checkEndFunction(
    const ReturnStmt *RS, CheckerContext &C) const {
  diagnoseMissingReleases(C);
}

/// Check for missing releases on early return.
void ObjCDeallocChecker::checkPreStmt(
    const ReturnStmt *RS, CheckerContext &C) const {
  diagnoseMissingReleases(C);
}

/// When a symbol is assumed to be nil, remove it from the set of symbols
/// require to be nil.
ProgramStateRef ObjCDeallocChecker::evalAssume(ProgramStateRef State, SVal Cond,
                                               bool Assumption) const {
  if (State->get<UnreleasedIvarMap>().isEmpty())
    return State;

  auto *CondBSE = dyn_cast_or_null<BinarySymExpr>(Cond.getAsSymExpr());
  if (!CondBSE)
    return State;

  BinaryOperator::Opcode OpCode = CondBSE->getOpcode();
  if (Assumption) {
    if (OpCode != BO_EQ)
      return State;
  } else {
    if (OpCode != BO_NE)
      return State;
  }

  SymbolRef NullSymbol = nullptr;
  if (auto *SIE = dyn_cast<SymIntExpr>(CondBSE)) {
    const llvm::APInt &RHS = SIE->getRHS();
    if (RHS != 0)
      return State;
    NullSymbol = SIE->getLHS();
  } else if (auto *SIE = dyn_cast<IntSymExpr>(CondBSE)) {
    const llvm::APInt &LHS = SIE->getLHS();
    if (LHS != 0)
      return State;
    NullSymbol = SIE->getRHS();
  } else {
    return State;
  }

  SymbolRef InstanceSymbol = getInstanceSymbolFromIvarSymbol(NullSymbol);
  if (!InstanceSymbol)
    return State;

  State = removeValueRequiringRelease(State, InstanceSymbol, NullSymbol);

  return State;
}

/// If a symbol escapes conservatively assume unseen code released it.
ProgramStateRef ObjCDeallocChecker::checkPointerEscape(
    ProgramStateRef State, const InvalidatedSymbols &Escaped,
    const CallEvent *Call, PointerEscapeKind Kind) const {

  if (State->get<UnreleasedIvarMap>().isEmpty())
    return State;

  // Don't treat calls to '[super dealloc]' as escaping for the purposes
  // of this checker. Because the checker diagnoses missing releases in the
  // post-message handler for '[super dealloc], escaping here would cause
  // the checker to never warn.
  auto *OMC = dyn_cast_or_null<ObjCMethodCall>(Call);
  if (OMC && isSuperDeallocMessage(*OMC))
    return State;

  for (const auto &Sym : Escaped) {
    if (!Call || (Call && !Call->isInSystemHeader())) {
      // If Sym is a symbol for an object with instance variables that
      // must be released, remove these obligations when the object escapes
      // unless via a call to a system function. System functions are
      // very unlikely to release instance variables on objects passed to them,
      // and are frequently called on 'self' in -dealloc (e.g., to remove
      // observers) -- we want to avoid false negatives from escaping on
      // them.
      State = State->remove<UnreleasedIvarMap>(Sym);
    }


    SymbolRef InstanceSymbol = getInstanceSymbolFromIvarSymbol(Sym);
    if (!InstanceSymbol)
      continue;

    State = removeValueRequiringRelease(State, InstanceSymbol, Sym);
  }

  return State;
}

/// Report any unreleased instance variables for the current instance being
/// dealloced.
void ObjCDeallocChecker::diagnoseMissingReleases(CheckerContext &C) const {
  ProgramStateRef State = C.getState();

  SVal SelfVal;
  if (!isInInstanceDealloc(C, SelfVal))
    return;

  const MemRegion *SelfRegion = SelfVal.castAs<loc::MemRegionVal>().getRegion();
  const LocationContext *LCtx = C.getLocationContext();

  ExplodedNode *ErrNode = nullptr;

  SymbolRef SelfSym = SelfVal.getAsSymbol();
  if (!SelfSym)
    return;

  const SymbolSet *OldUnreleased = State->get<UnreleasedIvarMap>(SelfSym);
  if (!OldUnreleased)
    return;

  SymbolSet NewUnreleased = *OldUnreleased;
  SymbolSet::Factory &F = State->getStateManager().get_context<SymbolSet>();

  ProgramStateRef InitialState = State;

  for (auto *IvarSymbol : *OldUnreleased) {
    const TypedValueRegion *TVR =
        cast<SymbolRegionValue>(IvarSymbol)->getRegion();
    const ObjCIvarRegion *IvarRegion = cast<ObjCIvarRegion>(TVR);

    // Don't warn if the ivar is not for this instance.
    if (SelfRegion != IvarRegion->getSuperRegion())
      continue;

    const ObjCIvarDecl *IvarDecl = IvarRegion->getDecl();
    // Prevent an inlined call to -dealloc in a super class from warning
    // about the values the subclass's -dealloc should release.
    if (IvarDecl->getContainingInterface() !=
        cast<ObjCMethodDecl>(LCtx->getDecl())->getClassInterface())
      continue;

    // Prevents diagnosing multiple times for the same instance variable
    // at, for example, both a return and at the end of the function.
    NewUnreleased = F.remove(NewUnreleased, IvarSymbol);

    if (State->getStateManager()
            .getConstraintManager()
            .isNull(State, IvarSymbol)
            .isConstrainedTrue()) {
      continue;
    }

    // A missing release manifests as a leak, so treat as a non-fatal error.
    if (!ErrNode)
      ErrNode = C.generateNonFatalErrorNode();
    // If we've already reached this node on another path, return without
    // diagnosing.
    if (!ErrNode)
      return;

    std::string Buf;
    llvm::raw_string_ostream OS(Buf);

    const ObjCInterfaceDecl *Interface = IvarDecl->getContainingInterface();
    // If the class is known to have a lifecycle with teardown that is
    // separate from -dealloc, do not warn about missing releases. We
    // suppress here (rather than not tracking for instance variables in
    // such classes) because these classes are rare.
    if (classHasSeparateTeardown(Interface))
      return;

    ObjCImplDecl *ImplDecl = Interface->getImplementation();

    const ObjCPropertyImplDecl *PropImpl =
        ImplDecl->FindPropertyImplIvarDecl(IvarDecl->getIdentifier());

    const ObjCPropertyDecl *PropDecl = PropImpl->getPropertyDecl();

    assert(PropDecl->getSetterKind() == ObjCPropertyDecl::Copy ||
           PropDecl->getSetterKind() == ObjCPropertyDecl::Retain);

    OS << "The '" << *IvarDecl << "' ivar in '" << *ImplDecl
       << "' was ";

    if (PropDecl->getSetterKind() == ObjCPropertyDecl::Retain)
      OS << "retained";
    else
      OS << "copied";

    OS << " by a synthesized property but not released"
          " before '[super dealloc]'";

    std::unique_ptr<BugReport> BR(
        new BugReport(*MissingReleaseBugType, OS.str(), ErrNode));

    C.emitReport(std::move(BR));
  }

  if (NewUnreleased.isEmpty()) {
    State = State->remove<UnreleasedIvarMap>(SelfSym);
  } else {
    State = State->set<UnreleasedIvarMap>(SelfSym, NewUnreleased);
  }

  if (ErrNode) {
    C.addTransition(State, ErrNode);
  } else if (State != InitialState) {
    C.addTransition(State);
  }

  // Make sure that after checking in the top-most frame the list of
  // tracked ivars is empty. This is intended to detect accidental leaks in
  // the UnreleasedIvarMap program state.
  assert(!LCtx->inTopFrame() || State->get<UnreleasedIvarMap>().isEmpty());
}

/// Given a symbol, determine whether the symbol refers to an ivar on
/// the top-most deallocating instance. If so, find the property for that
/// ivar, if one exists. Otherwise return null.
const ObjCPropertyImplDecl *
ObjCDeallocChecker::findPropertyOnDeallocatingInstance(
    SymbolRef IvarSym, CheckerContext &C) const {
  SVal DeallocedInstance;
  if (!isInInstanceDealloc(C, DeallocedInstance))
    return nullptr;

  // Try to get the region from which the ivar value was loaded.
  auto *IvarRegion = getIvarRegionForIvarSymbol(IvarSym);
  if (!IvarRegion)
    return nullptr;

  // Don't try to find the property if the ivar was not loaded from the
  // given instance.
  if (DeallocedInstance.castAs<loc::MemRegionVal>().getRegion() !=
      IvarRegion->getSuperRegion())
    return nullptr;

  const LocationContext *LCtx = C.getLocationContext();
  const ObjCIvarDecl *IvarDecl = IvarRegion->getDecl();

  const ObjCImplDecl *Container = getContainingObjCImpl(LCtx);
  const ObjCPropertyImplDecl *PropImpl =
      Container->FindPropertyImplIvarDecl(IvarDecl->getIdentifier());
  return PropImpl;
}

/// Emits a warning if the current context is -dealloc and ReleasedValue
/// must not be directly released in a -dealloc. Returns true if a diagnostic
/// was emitted.
bool ObjCDeallocChecker::diagnoseExtraRelease(SymbolRef ReleasedValue,
                                              const ObjCMethodCall &M,
                                              CheckerContext &C) const {
  // Try to get the region from which the released value was loaded.
  // Note that, unlike diagnosing for missing releases, here we don't track
  // values that must not be released in the state. This is because even if
  // these values escape, it is still an error under the rules of MRR to
  // release them in -dealloc.
  const ObjCPropertyImplDecl *PropImpl =
      findPropertyOnDeallocatingInstance(ReleasedValue, C);

  if (!PropImpl)
    return false;

  // If the ivar belongs to a property that must not be released directly
  // in dealloc, emit a warning.
  if (getDeallocReleaseRequirement(PropImpl) !=
      ReleaseRequirement::MustNotReleaseDirectly) {
    return false;
  }

  // If the property is readwrite but it shadows a read-only property in its
  // external interface, treat the property a read-only. If the outside
  // world cannot write to a property then the internal implementation is free
  // to make its own convention about whether the value is stored retained
  // or not. We look up the shadow here rather than in
  // getDeallocReleaseRequirement() because doing so can be expensive.
  const ObjCPropertyDecl *PropDecl = findShadowedPropertyDecl(PropImpl);
  if (PropDecl) {
    if (PropDecl->isReadOnly())
      return false;
  } else {
    PropDecl = PropImpl->getPropertyDecl();
  }

  ExplodedNode *ErrNode = C.generateNonFatalErrorNode();
  if (!ErrNode)
    return false;

  std::string Buf;
  llvm::raw_string_ostream OS(Buf);

  assert(PropDecl->getSetterKind() == ObjCPropertyDecl::Weak ||
         (PropDecl->getSetterKind() == ObjCPropertyDecl::Assign &&
          !PropDecl->isReadOnly()) ||
         isReleasedByCIFilterDealloc(PropImpl)
         );

  const ObjCImplDecl *Container = getContainingObjCImpl(C.getLocationContext());
  OS << "The '" << *PropImpl->getPropertyIvarDecl()
     << "' ivar in '" << *Container;


  if (isReleasedByCIFilterDealloc(PropImpl)) {
    OS << "' will be released by '-[CIFilter dealloc]' but also released here";
  } else {
    OS << "' was synthesized for ";

    if (PropDecl->getSetterKind() == ObjCPropertyDecl::Weak)
      OS << "a weak";
    else
      OS << "an assign, readwrite";

    OS <<  " property but was released in 'dealloc'";
  }

  std::unique_ptr<BugReport> BR(
      new BugReport(*ExtraReleaseBugType, OS.str(), ErrNode));
  BR->addRange(M.getOriginExpr()->getSourceRange());

  C.emitReport(std::move(BR));

  return true;
}

/// Emits a warning if the current context is -dealloc and DeallocedValue
/// must not be directly dealloced in a -dealloc. Returns true if a diagnostic
/// was emitted.
bool ObjCDeallocChecker::diagnoseMistakenDealloc(SymbolRef DeallocedValue,
                                                 const ObjCMethodCall &M,
                                                 CheckerContext &C) const {
  // TODO: Apart from unknown/undefined receivers, this may happen when
  // dealloc is called as a class method. Should we warn?
  if (!DeallocedValue)
    return false;

  // Find the property backing the instance variable that M
  // is dealloc'ing.
  const ObjCPropertyImplDecl *PropImpl =
      findPropertyOnDeallocatingInstance(DeallocedValue, C);
  if (!PropImpl)
    return false;

  if (getDeallocReleaseRequirement(PropImpl) !=
      ReleaseRequirement::MustRelease) {
    return false;
  }

  ExplodedNode *ErrNode = C.generateErrorNode();
  if (!ErrNode)
    return false;

  std::string Buf;
  llvm::raw_string_ostream OS(Buf);

  OS << "'" << *PropImpl->getPropertyIvarDecl()
     << "' should be released rather than deallocated";

  std::unique_ptr<BugReport> BR(
      new BugReport(*MistakenDeallocBugType, OS.str(), ErrNode));
  BR->addRange(M.getOriginExpr()->getSourceRange());

  C.emitReport(std::move(BR));

  return true;
}

ObjCDeallocChecker::ObjCDeallocChecker()
    : NSObjectII(nullptr), SenTestCaseII(nullptr), XCTestCaseII(nullptr),
      CIFilterII(nullptr) {

  MissingReleaseBugType.reset(
      new BugType(this, "Missing ivar release (leak)",
                  categories::MemoryRefCount));

  ExtraReleaseBugType.reset(
      new BugType(this, "Extra ivar release",
                  categories::MemoryRefCount));

  MistakenDeallocBugType.reset(
      new BugType(this, "Mistaken dealloc",
                  categories::MemoryRefCount));
}

void ObjCDeallocChecker::initIdentifierInfoAndSelectors(
    ASTContext &Ctx) const {
  if (NSObjectII)
    return;

  NSObjectII = &Ctx.Idents.get("NSObject");
  SenTestCaseII = &Ctx.Idents.get("SenTestCase");
  XCTestCaseII = &Ctx.Idents.get("XCTestCase");
  Block_releaseII = &Ctx.Idents.get("_Block_release");
  CIFilterII = &Ctx.Idents.get("CIFilter");

  IdentifierInfo *DeallocII = &Ctx.Idents.get("dealloc");
  IdentifierInfo *ReleaseII = &Ctx.Idents.get("release");
  DeallocSel = Ctx.Selectors.getSelector(0, &DeallocII);
  ReleaseSel = Ctx.Selectors.getSelector(0, &ReleaseII);
}

/// Returns true if M is a call to '[super dealloc]'.
bool ObjCDeallocChecker::isSuperDeallocMessage(
    const ObjCMethodCall &M) const {
  if (M.getOriginExpr()->getReceiverKind() != ObjCMessageExpr::SuperInstance)
    return false;

  return M.getSelector() == DeallocSel;
}

/// Returns the ObjCImplDecl containing the method declaration in LCtx.
const ObjCImplDecl *
ObjCDeallocChecker::getContainingObjCImpl(const LocationContext *LCtx) const {
  auto *MD = cast<ObjCMethodDecl>(LCtx->getDecl());
  return cast<ObjCImplDecl>(MD->getDeclContext());
}

/// Returns the property that shadowed by PropImpl if one exists and
/// nullptr otherwise.
const ObjCPropertyDecl *ObjCDeallocChecker::findShadowedPropertyDecl(
    const ObjCPropertyImplDecl *PropImpl) const {
  const ObjCPropertyDecl *PropDecl = PropImpl->getPropertyDecl();

  // Only readwrite properties can shadow.
  if (PropDecl->isReadOnly())
    return nullptr;

  auto *CatDecl = dyn_cast<ObjCCategoryDecl>(PropDecl->getDeclContext());

  // Only class extensions can contain shadowing properties.
  if (!CatDecl || !CatDecl->IsClassExtension())
    return nullptr;

  IdentifierInfo *ID = PropDecl->getIdentifier();
  DeclContext::lookup_result R = CatDecl->getClassInterface()->lookup(ID);
  for (DeclContext::lookup_iterator I = R.begin(), E = R.end(); I != E; ++I) {
    auto *ShadowedPropDecl = dyn_cast<ObjCPropertyDecl>(*I);
    if (!ShadowedPropDecl)
      continue;

    if (ShadowedPropDecl->isInstanceProperty()) {
      assert(ShadowedPropDecl->isReadOnly());
      return ShadowedPropDecl;
    }
  }

  return nullptr;
}

/// Add a transition noting the release of the given value.
void ObjCDeallocChecker::transitionToReleaseValue(CheckerContext &C,
                                                  SymbolRef Value) const {
  assert(Value);
  SymbolRef InstanceSym = getInstanceSymbolFromIvarSymbol(Value);
  if (!InstanceSym)
    return;
  ProgramStateRef InitialState = C.getState();

  ProgramStateRef ReleasedState =
      removeValueRequiringRelease(InitialState, InstanceSym, Value);

  if (ReleasedState != InitialState) {
    C.addTransition(ReleasedState);
  }
}

/// Remove the Value requiring a release from the tracked set for
/// Instance and return the resultant state.
ProgramStateRef ObjCDeallocChecker::removeValueRequiringRelease(
    ProgramStateRef State, SymbolRef Instance, SymbolRef Value) const {
  assert(Instance);
  assert(Value);
  const ObjCIvarRegion *RemovedRegion = getIvarRegionForIvarSymbol(Value);
  if (!RemovedRegion)
    return State;

  const SymbolSet *Unreleased = State->get<UnreleasedIvarMap>(Instance);
  if (!Unreleased)
    return State;

  // Mark the value as no longer requiring a release.
  SymbolSet::Factory &F = State->getStateManager().get_context<SymbolSet>();
  SymbolSet NewUnreleased = *Unreleased;
  for (auto &Sym : *Unreleased) {
    const ObjCIvarRegion *UnreleasedRegion = getIvarRegionForIvarSymbol(Sym);
    assert(UnreleasedRegion);
    if (RemovedRegion->getDecl() == UnreleasedRegion->getDecl()) {
      NewUnreleased = F.remove(NewUnreleased, Sym);
    }
  }

  if (NewUnreleased.isEmpty()) {
    return State->remove<UnreleasedIvarMap>(Instance);
  }

  return State->set<UnreleasedIvarMap>(Instance, NewUnreleased);
}

/// Determines whether the instance variable for \p PropImpl must or must not be
/// released in -dealloc or whether it cannot be determined.
ReleaseRequirement ObjCDeallocChecker::getDeallocReleaseRequirement(
    const ObjCPropertyImplDecl *PropImpl) const {
  const ObjCIvarDecl *IvarDecl;
  const ObjCPropertyDecl *PropDecl;
  if (!isSynthesizedRetainableProperty(PropImpl, &IvarDecl, &PropDecl))
    return ReleaseRequirement::Unknown;

  ObjCPropertyDecl::SetterKind SK = PropDecl->getSetterKind();

  switch (SK) {
  // Retain and copy setters retain/copy their values before storing and so
  // the value in their instance variables must be released in -dealloc.
  case ObjCPropertyDecl::Retain:
  case ObjCPropertyDecl::Copy:
    if (isReleasedByCIFilterDealloc(PropImpl))
      return ReleaseRequirement::MustNotReleaseDirectly;

    if (isNibLoadedIvarWithoutRetain(PropImpl))
      return ReleaseRequirement::Unknown;

    return ReleaseRequirement::MustRelease;

  case ObjCPropertyDecl::Weak:
    return ReleaseRequirement::MustNotReleaseDirectly;

  case ObjCPropertyDecl::Assign:
    // It is common for the ivars for read-only assign properties to
    // always be stored retained, so their release requirement cannot be
    // be determined.
    if (PropDecl->isReadOnly())
      return ReleaseRequirement::Unknown;

    return ReleaseRequirement::MustNotReleaseDirectly;
  }
  llvm_unreachable("Unrecognized setter kind");
}

/// Returns the released value if M is a call a setter that releases
/// and nils out its underlying instance variable.
SymbolRef
ObjCDeallocChecker::getValueReleasedByNillingOut(const ObjCMethodCall &M,
                                                 CheckerContext &C) const {
  SVal ReceiverVal = M.getReceiverSVal();
  if (!ReceiverVal.isValid())
    return nullptr;

  if (M.getNumArgs() == 0)
    return nullptr;

  if (!M.getArgExpr(0)->getType()->isObjCRetainableType())
    return nullptr;

  // Is the first argument nil?
  SVal Arg = M.getArgSVal(0);
  ProgramStateRef notNilState, nilState;
  std::tie(notNilState, nilState) =
      M.getState()->assume(Arg.castAs<DefinedOrUnknownSVal>());
  if (!(nilState && !notNilState))
    return nullptr;

  const ObjCPropertyDecl *Prop = M.getAccessedProperty();
  if (!Prop)
    return nullptr;

  ObjCIvarDecl *PropIvarDecl = Prop->getPropertyIvarDecl();
  if (!PropIvarDecl)
    return nullptr;

  ProgramStateRef State = C.getState();

  SVal LVal = State->getLValue(PropIvarDecl, ReceiverVal);
  Optional<Loc> LValLoc = LVal.getAs<Loc>();
  if (!LValLoc)
    return nullptr;

  SVal CurrentValInIvar = State->getSVal(LValLoc.getValue());
  return CurrentValInIvar.getAsSymbol();
}

/// Returns true if the current context is a call to -dealloc and false
/// otherwise. If true, it also sets SelfValOut to the value of
/// 'self'.
bool ObjCDeallocChecker::isInInstanceDealloc(const CheckerContext &C,
                                             SVal &SelfValOut) const {
  return isInInstanceDealloc(C, C.getLocationContext(), SelfValOut);
}

/// Returns true if LCtx is a call to -dealloc and false
/// otherwise. If true, it also sets SelfValOut to the value of
/// 'self'.
bool ObjCDeallocChecker::isInInstanceDealloc(const CheckerContext &C,
                                             const LocationContext *LCtx,
                                             SVal &SelfValOut) const {
  auto *MD = dyn_cast<ObjCMethodDecl>(LCtx->getDecl());
  if (!MD || !MD->isInstanceMethod() || MD->getSelector() != DeallocSel)
    return false;

  const ImplicitParamDecl *SelfDecl = LCtx->getSelfDecl();
  assert(SelfDecl && "No self in -dealloc?");

  ProgramStateRef State = C.getState();
  SelfValOut = State->getSVal(State->getRegion(SelfDecl, LCtx));
  return true;
}

/// Returns true if there is a call to -dealloc anywhere on the stack and false
/// otherwise. If true, it also sets InstanceValOut to the value of
/// 'self' in the frame for -dealloc.
bool ObjCDeallocChecker::instanceDeallocIsOnStack(const CheckerContext &C,
                                                  SVal &InstanceValOut) const {
  const LocationContext *LCtx = C.getLocationContext();

  while (LCtx) {
    if (isInInstanceDealloc(C, LCtx, InstanceValOut))
      return true;

    LCtx = LCtx->getParent();
  }

  return false;
}

/// Returns true if the ID is a class in which which is known to have
/// a separate teardown lifecycle. In this case, -dealloc warnings
/// about missing releases should be suppressed.
bool ObjCDeallocChecker::classHasSeparateTeardown(
    const ObjCInterfaceDecl *ID) const {
  // Suppress if the class is not a subclass of NSObject.
  for ( ; ID ; ID = ID->getSuperClass()) {
    IdentifierInfo *II = ID->getIdentifier();

    if (II == NSObjectII)
      return false;

    // FIXME: For now, ignore classes that subclass SenTestCase and XCTestCase,
    // as these don't need to implement -dealloc.  They implement tear down in
    // another way, which we should try and catch later.
    //  http://llvm.org/bugs/show_bug.cgi?id=3187
    if (II == XCTestCaseII || II == SenTestCaseII)
      return true;
  }

  return true;
}

/// The -dealloc method in CIFilter highly unusual in that is will release
/// instance variables belonging to its *subclasses* if the variable name
/// starts with "input" or backs a property whose name starts with "input".
/// Subclasses should not release these ivars in their own -dealloc method --
/// doing so could result in an over release.
///
/// This method returns true if the property will be released by
/// -[CIFilter dealloc].
bool ObjCDeallocChecker::isReleasedByCIFilterDealloc(
    const ObjCPropertyImplDecl *PropImpl) const {
  assert(PropImpl->getPropertyIvarDecl());
  StringRef PropName = PropImpl->getPropertyDecl()->getName();
  StringRef IvarName = PropImpl->getPropertyIvarDecl()->getName();

  const char *ReleasePrefix = "input";
  if (!(PropName.startswith(ReleasePrefix) ||
        IvarName.startswith(ReleasePrefix))) {
    return false;
  }

  const ObjCInterfaceDecl *ID =
      PropImpl->getPropertyIvarDecl()->getContainingInterface();
  for ( ; ID ; ID = ID->getSuperClass()) {
    IdentifierInfo *II = ID->getIdentifier();
    if (II == CIFilterII)
      return true;
  }

  return false;
}

/// Returns whether the ivar backing the property is an IBOutlet that
/// has its value set by nib loading code without retaining the value.
///
/// On macOS, if there is no setter, the nib-loading code sets the ivar
/// directly, without retaining the value,
///
/// On iOS and its derivatives, the nib-loading code will call
/// -setValue:forKey:, which retains the value before directly setting the ivar.
bool ObjCDeallocChecker::isNibLoadedIvarWithoutRetain(
    const ObjCPropertyImplDecl *PropImpl) const {
  const ObjCIvarDecl *IvarDecl = PropImpl->getPropertyIvarDecl();
  if (!IvarDecl->hasAttr<IBOutletAttr>())
    return false;

  const llvm::Triple &Target =
      IvarDecl->getASTContext().getTargetInfo().getTriple();

  if (!Target.isMacOSX())
    return false;

  if (PropImpl->getPropertyDecl()->getSetterMethodDecl())
    return false;

  return true;
}

void ento::registerObjCDeallocChecker(CheckerManager &Mgr) {
  const LangOptions &LangOpts = Mgr.getLangOpts();
  // These checker only makes sense under MRR.
  if (LangOpts.getGC() == LangOptions::GCOnly || LangOpts.ObjCAutoRefCount)
    return;

  Mgr.registerChecker<ObjCDeallocChecker>();
}
