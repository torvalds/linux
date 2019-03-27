//== TrustNonnullChecker.cpp --------- API nullability modeling -*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This checker adds nullability-related assumptions:
//
// 1. Methods annotated with _Nonnull
// which come from system headers actually return a non-null pointer.
//
// 2. NSDictionary key is non-null after the keyword subscript operation
// on read if and only if the resulting expression is non-null.
//
// 3. NSMutableDictionary index is non-null after a write operation.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/Analysis/SelectorExtras.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerHelpers.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"

using namespace clang;
using namespace ento;

/// Records implications between symbols.
/// The semantics is:
///    (antecedent != 0) => (consequent != 0)
/// These implications are then read during the evaluation of the assumption,
/// and the appropriate antecedents are applied.
REGISTER_MAP_WITH_PROGRAMSTATE(NonNullImplicationMap, SymbolRef, SymbolRef)

/// The semantics is:
///    (antecedent == 0) => (consequent == 0)
REGISTER_MAP_WITH_PROGRAMSTATE(NullImplicationMap, SymbolRef, SymbolRef)

namespace {

class TrustNonnullChecker : public Checker<check::PostCall,
                                           check::PostObjCMessage,
                                           check::DeadSymbols,
                                           eval::Assume> {
  // Do not try to iterate over symbols with higher complexity.
  static unsigned constexpr ComplexityThreshold = 10;
  Selector ObjectForKeyedSubscriptSel;
  Selector ObjectForKeySel;
  Selector SetObjectForKeyedSubscriptSel;
  Selector SetObjectForKeySel;

public:
  TrustNonnullChecker(ASTContext &Ctx)
      : ObjectForKeyedSubscriptSel(
            getKeywordSelector(Ctx, "objectForKeyedSubscript")),
        ObjectForKeySel(getKeywordSelector(Ctx, "objectForKey")),
        SetObjectForKeyedSubscriptSel(
            getKeywordSelector(Ctx, "setObject", "forKeyedSubscript")),
        SetObjectForKeySel(getKeywordSelector(Ctx, "setObject", "forKey")) {}

  ProgramStateRef evalAssume(ProgramStateRef State,
                             SVal Cond,
                             bool Assumption) const {
    const SymbolRef CondS = Cond.getAsSymbol();
    if (!CondS || CondS->computeComplexity() > ComplexityThreshold)
      return State;

    for (auto B=CondS->symbol_begin(), E=CondS->symbol_end(); B != E; ++B) {
      const SymbolRef Antecedent = *B;
      State = addImplication(Antecedent, State, true);
      State = addImplication(Antecedent, State, false);
    }

    return State;
  }

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const {
    // Only trust annotations for system headers for non-protocols.
    if (!Call.isInSystemHeader())
      return;

    ProgramStateRef State = C.getState();

    if (isNonNullPtr(Call, C))
      if (auto L = Call.getReturnValue().getAs<Loc>())
        State = State->assume(*L, /*Assumption=*/true);

    C.addTransition(State);
  }

  void checkPostObjCMessage(const ObjCMethodCall &Msg,
                            CheckerContext &C) const {
    const ObjCInterfaceDecl *ID = Msg.getReceiverInterface();
    if (!ID)
      return;

    ProgramStateRef State = C.getState();

    // Index to setter for NSMutableDictionary is assumed to be non-null,
    // as an exception is thrown otherwise.
    if (interfaceHasSuperclass(ID, "NSMutableDictionary") &&
        (Msg.getSelector() == SetObjectForKeyedSubscriptSel ||
         Msg.getSelector() == SetObjectForKeySel)) {
      if (auto L = Msg.getArgSVal(1).getAs<Loc>())
        State = State->assume(*L, /*Assumption=*/true);
    }

    // Record an implication: index is non-null if the output is non-null.
    if (interfaceHasSuperclass(ID, "NSDictionary") &&
        (Msg.getSelector() == ObjectForKeyedSubscriptSel ||
         Msg.getSelector() == ObjectForKeySel)) {
      SymbolRef ArgS = Msg.getArgSVal(0).getAsSymbol();
      SymbolRef RetS = Msg.getReturnValue().getAsSymbol();

      if (ArgS && RetS) {
        // Emulate an implication: the argument is non-null if
        // the return value is non-null.
        State = State->set<NonNullImplicationMap>(RetS, ArgS);

        // Conversely, when the argument is null, the return value
        // is definitely null.
        State = State->set<NullImplicationMap>(ArgS, RetS);
      }
    }

    C.addTransition(State);
  }

  void checkDeadSymbols(SymbolReaper &SymReaper, CheckerContext &C) const {
    ProgramStateRef State = C.getState();

    State = dropDeadFromGDM<NullImplicationMap>(SymReaper, State);
    State = dropDeadFromGDM<NonNullImplicationMap>(SymReaper, State);

    C.addTransition(State);
  }

private:

  /// \returns State with GDM \p MapName where all dead symbols were
  // removed.
  template <typename MapName>
  ProgramStateRef dropDeadFromGDM(SymbolReaper &SymReaper,
                                  ProgramStateRef State) const {
    for (const std::pair<SymbolRef, SymbolRef> &P : State->get<MapName>())
      if (!SymReaper.isLive(P.first) || !SymReaper.isLive(P.second))
        State = State->remove<MapName>(P.first);
    return State;
  }

  /// \returns Whether we trust the result of the method call to be
  /// a non-null pointer.
  bool isNonNullPtr(const CallEvent &Call, CheckerContext &C) const {
    QualType ExprRetType = Call.getResultType();
    if (!ExprRetType->isAnyPointerType())
      return false;

    if (getNullabilityAnnotation(ExprRetType) == Nullability::Nonnull)
      return true;

    // The logic for ObjC instance method calls is more complicated,
    // as the return value is nil when the receiver is nil.
    if (!isa<ObjCMethodCall>(&Call))
      return false;

    const auto *MCall = cast<ObjCMethodCall>(&Call);
    const ObjCMethodDecl *MD = MCall->getDecl();

    // Distrust protocols.
    if (isa<ObjCProtocolDecl>(MD->getDeclContext()))
      return false;

    QualType DeclRetType = MD->getReturnType();
    if (getNullabilityAnnotation(DeclRetType) != Nullability::Nonnull)
      return false;

    // For class messages it is sufficient for the declaration to be
    // annotated _Nonnull.
    if (!MCall->isInstanceMessage())
      return true;

    // Alternatively, the analyzer could know that the receiver is not null.
    SVal Receiver = MCall->getReceiverSVal();
    ConditionTruthVal TV = C.getState()->isNonNull(Receiver);
    if (TV.isConstrainedTrue())
      return true;

    return false;
  }

  /// \return Whether \p ID has a superclass by the name \p ClassName.
  bool interfaceHasSuperclass(const ObjCInterfaceDecl *ID,
                         StringRef ClassName) const {
    if (ID->getIdentifier()->getName() == ClassName)
      return true;

    if (const ObjCInterfaceDecl *Super = ID->getSuperClass())
      return interfaceHasSuperclass(Super, ClassName);

    return false;
  }


  /// \return a state with an optional implication added (if exists)
  /// from a map of recorded implications.
  /// If \p Negated is true, checks NullImplicationMap, and assumes
  /// the negation of \p Antecedent.
  /// Checks NonNullImplicationMap and assumes \p Antecedent otherwise.
  ProgramStateRef addImplication(SymbolRef Antecedent,
                                 ProgramStateRef InputState,
                                 bool Negated) const {
    if (!InputState)
      return nullptr;
    SValBuilder &SVB = InputState->getStateManager().getSValBuilder();
    const SymbolRef *Consequent =
        Negated ? InputState->get<NonNullImplicationMap>(Antecedent)
                : InputState->get<NullImplicationMap>(Antecedent);
    if (!Consequent)
      return InputState;

    SVal AntecedentV = SVB.makeSymbolVal(Antecedent);
    ProgramStateRef State = InputState;

    if ((Negated && InputState->isNonNull(AntecedentV).isConstrainedTrue())
        || (!Negated && InputState->isNull(AntecedentV).isConstrainedTrue())) {
      SVal ConsequentS = SVB.makeSymbolVal(*Consequent);
      State = InputState->assume(ConsequentS.castAs<DefinedSVal>(), Negated);
      if (!State)
        return nullptr;

      // Drop implications from the map.
      if (Negated) {
        State = State->remove<NonNullImplicationMap>(Antecedent);
        State = State->remove<NullImplicationMap>(*Consequent);
      } else {
        State = State->remove<NullImplicationMap>(Antecedent);
        State = State->remove<NonNullImplicationMap>(*Consequent);
      }
    }

    return State;
  }
};

} // end empty namespace


void ento::registerTrustNonnullChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<TrustNonnullChecker>(Mgr.getASTContext());
}
