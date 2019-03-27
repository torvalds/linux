//= ProgramState.cpp - Path-Sensitive "State" for tracking values --*- C++ -*--=
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements ProgramState and ProgramStateManager.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/Analysis/CFG.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SubEngine.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/TaintManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicTypeMap.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

namespace clang { namespace  ento {
/// Increments the number of times this state is referenced.

void ProgramStateRetain(const ProgramState *state) {
  ++const_cast<ProgramState*>(state)->refCount;
}

/// Decrement the number of times this state is referenced.
void ProgramStateRelease(const ProgramState *state) {
  assert(state->refCount > 0);
  ProgramState *s = const_cast<ProgramState*>(state);
  if (--s->refCount == 0) {
    ProgramStateManager &Mgr = s->getStateManager();
    Mgr.StateSet.RemoveNode(s);
    s->~ProgramState();
    Mgr.freeStates.push_back(s);
  }
}
}}

ProgramState::ProgramState(ProgramStateManager *mgr, const Environment& env,
                 StoreRef st, GenericDataMap gdm)
  : stateMgr(mgr),
    Env(env),
    store(st.getStore()),
    GDM(gdm),
    refCount(0) {
  stateMgr->getStoreManager().incrementReferenceCount(store);
}

ProgramState::ProgramState(const ProgramState &RHS)
    : llvm::FoldingSetNode(),
      stateMgr(RHS.stateMgr),
      Env(RHS.Env),
      store(RHS.store),
      GDM(RHS.GDM),
      refCount(0) {
  stateMgr->getStoreManager().incrementReferenceCount(store);
}

ProgramState::~ProgramState() {
  if (store)
    stateMgr->getStoreManager().decrementReferenceCount(store);
}

int64_t ProgramState::getID() const {
  return getStateManager().Alloc.identifyKnownAlignedObject<ProgramState>(this);
}

ProgramStateManager::ProgramStateManager(ASTContext &Ctx,
                                         StoreManagerCreator CreateSMgr,
                                         ConstraintManagerCreator CreateCMgr,
                                         llvm::BumpPtrAllocator &alloc,
                                         SubEngine *SubEng)
  : Eng(SubEng), EnvMgr(alloc), GDMFactory(alloc),
    svalBuilder(createSimpleSValBuilder(alloc, Ctx, *this)),
    CallEventMgr(new CallEventManager(alloc)), Alloc(alloc) {
  StoreMgr = (*CreateSMgr)(*this);
  ConstraintMgr = (*CreateCMgr)(*this, SubEng);
}


ProgramStateManager::~ProgramStateManager() {
  for (GDMContextsTy::iterator I=GDMContexts.begin(), E=GDMContexts.end();
       I!=E; ++I)
    I->second.second(I->second.first);
}

ProgramStateRef
ProgramStateManager::removeDeadBindings(ProgramStateRef state,
                                   const StackFrameContext *LCtx,
                                   SymbolReaper& SymReaper) {

  // This code essentially performs a "mark-and-sweep" of the VariableBindings.
  // The roots are any Block-level exprs and Decls that our liveness algorithm
  // tells us are live.  We then see what Decls they may reference, and keep
  // those around.  This code more than likely can be made faster, and the
  // frequency of which this method is called should be experimented with
  // for optimum performance.
  ProgramState NewState = *state;

  NewState.Env = EnvMgr.removeDeadBindings(NewState.Env, SymReaper, state);

  // Clean up the store.
  StoreRef newStore = StoreMgr->removeDeadBindings(NewState.getStore(), LCtx,
                                                   SymReaper);
  NewState.setStore(newStore);
  SymReaper.setReapedStore(newStore);

  ProgramStateRef Result = getPersistentState(NewState);
  return ConstraintMgr->removeDeadBindings(Result, SymReaper);
}

ProgramStateRef ProgramState::bindLoc(Loc LV,
                                      SVal V,
                                      const LocationContext *LCtx,
                                      bool notifyChanges) const {
  ProgramStateManager &Mgr = getStateManager();
  ProgramStateRef newState = makeWithStore(Mgr.StoreMgr->Bind(getStore(),
                                                             LV, V));
  const MemRegion *MR = LV.getAsRegion();
  if (MR && notifyChanges)
    return Mgr.getOwningEngine().processRegionChange(newState, MR, LCtx);

  return newState;
}

ProgramStateRef
ProgramState::bindDefaultInitial(SVal loc, SVal V,
                                 const LocationContext *LCtx) const {
  ProgramStateManager &Mgr = getStateManager();
  const MemRegion *R = loc.castAs<loc::MemRegionVal>().getRegion();
  const StoreRef &newStore = Mgr.StoreMgr->BindDefaultInitial(getStore(), R, V);
  ProgramStateRef new_state = makeWithStore(newStore);
  return Mgr.getOwningEngine().processRegionChange(new_state, R, LCtx);
}

ProgramStateRef
ProgramState::bindDefaultZero(SVal loc, const LocationContext *LCtx) const {
  ProgramStateManager &Mgr = getStateManager();
  const MemRegion *R = loc.castAs<loc::MemRegionVal>().getRegion();
  const StoreRef &newStore = Mgr.StoreMgr->BindDefaultZero(getStore(), R);
  ProgramStateRef new_state = makeWithStore(newStore);
  return Mgr.getOwningEngine().processRegionChange(new_state, R, LCtx);
}

typedef ArrayRef<const MemRegion *> RegionList;
typedef ArrayRef<SVal> ValueList;

ProgramStateRef
ProgramState::invalidateRegions(RegionList Regions,
                             const Expr *E, unsigned Count,
                             const LocationContext *LCtx,
                             bool CausedByPointerEscape,
                             InvalidatedSymbols *IS,
                             const CallEvent *Call,
                             RegionAndSymbolInvalidationTraits *ITraits) const {
  SmallVector<SVal, 8> Values;
  for (RegionList::const_iterator I = Regions.begin(),
                                  End = Regions.end(); I != End; ++I)
    Values.push_back(loc::MemRegionVal(*I));

  return invalidateRegionsImpl(Values, E, Count, LCtx, CausedByPointerEscape,
                               IS, ITraits, Call);
}

ProgramStateRef
ProgramState::invalidateRegions(ValueList Values,
                             const Expr *E, unsigned Count,
                             const LocationContext *LCtx,
                             bool CausedByPointerEscape,
                             InvalidatedSymbols *IS,
                             const CallEvent *Call,
                             RegionAndSymbolInvalidationTraits *ITraits) const {

  return invalidateRegionsImpl(Values, E, Count, LCtx, CausedByPointerEscape,
                               IS, ITraits, Call);
}

ProgramStateRef
ProgramState::invalidateRegionsImpl(ValueList Values,
                                    const Expr *E, unsigned Count,
                                    const LocationContext *LCtx,
                                    bool CausedByPointerEscape,
                                    InvalidatedSymbols *IS,
                                    RegionAndSymbolInvalidationTraits *ITraits,
                                    const CallEvent *Call) const {
  ProgramStateManager &Mgr = getStateManager();
  SubEngine &Eng = Mgr.getOwningEngine();

  InvalidatedSymbols InvalidatedSyms;
  if (!IS)
    IS = &InvalidatedSyms;

  RegionAndSymbolInvalidationTraits ITraitsLocal;
  if (!ITraits)
    ITraits = &ITraitsLocal;

  StoreManager::InvalidatedRegions TopLevelInvalidated;
  StoreManager::InvalidatedRegions Invalidated;
  const StoreRef &newStore
  = Mgr.StoreMgr->invalidateRegions(getStore(), Values, E, Count, LCtx, Call,
                                    *IS, *ITraits, &TopLevelInvalidated,
                                    &Invalidated);

  ProgramStateRef newState = makeWithStore(newStore);

  if (CausedByPointerEscape) {
    newState = Eng.notifyCheckersOfPointerEscape(newState, IS,
                                                 TopLevelInvalidated,
                                                 Call,
                                                 *ITraits);
  }

  return Eng.processRegionChanges(newState, IS, TopLevelInvalidated,
                                  Invalidated, LCtx, Call);
}

ProgramStateRef ProgramState::killBinding(Loc LV) const {
  assert(!LV.getAs<loc::MemRegionVal>() && "Use invalidateRegion instead.");

  Store OldStore = getStore();
  const StoreRef &newStore =
    getStateManager().StoreMgr->killBinding(OldStore, LV);

  if (newStore.getStore() == OldStore)
    return this;

  return makeWithStore(newStore);
}

ProgramStateRef
ProgramState::enterStackFrame(const CallEvent &Call,
                              const StackFrameContext *CalleeCtx) const {
  const StoreRef &NewStore =
    getStateManager().StoreMgr->enterStackFrame(getStore(), Call, CalleeCtx);
  return makeWithStore(NewStore);
}

SVal ProgramState::getSValAsScalarOrLoc(const MemRegion *R) const {
  // We only want to do fetches from regions that we can actually bind
  // values.  For example, SymbolicRegions of type 'id<...>' cannot
  // have direct bindings (but their can be bindings on their subregions).
  if (!R->isBoundable())
    return UnknownVal();

  if (const TypedValueRegion *TR = dyn_cast<TypedValueRegion>(R)) {
    QualType T = TR->getValueType();
    if (Loc::isLocType(T) || T->isIntegralOrEnumerationType())
      return getSVal(R);
  }

  return UnknownVal();
}

SVal ProgramState::getSVal(Loc location, QualType T) const {
  SVal V = getRawSVal(location, T);

  // If 'V' is a symbolic value that is *perfectly* constrained to
  // be a constant value, use that value instead to lessen the burden
  // on later analysis stages (so we have less symbolic values to reason
  // about).
  // We only go into this branch if we can convert the APSInt value we have
  // to the type of T, which is not always the case (e.g. for void).
  if (!T.isNull() && (T->isIntegralOrEnumerationType() || Loc::isLocType(T))) {
    if (SymbolRef sym = V.getAsSymbol()) {
      if (const llvm::APSInt *Int = getStateManager()
                                    .getConstraintManager()
                                    .getSymVal(this, sym)) {
        // FIXME: Because we don't correctly model (yet) sign-extension
        // and truncation of symbolic values, we need to convert
        // the integer value to the correct signedness and bitwidth.
        //
        // This shows up in the following:
        //
        //   char foo();
        //   unsigned x = foo();
        //   if (x == 54)
        //     ...
        //
        //  The symbolic value stored to 'x' is actually the conjured
        //  symbol for the call to foo(); the type of that symbol is 'char',
        //  not unsigned.
        const llvm::APSInt &NewV = getBasicVals().Convert(T, *Int);

        if (V.getAs<Loc>())
          return loc::ConcreteInt(NewV);
        else
          return nonloc::ConcreteInt(NewV);
      }
    }
  }

  return V;
}

ProgramStateRef ProgramState::BindExpr(const Stmt *S,
                                           const LocationContext *LCtx,
                                           SVal V, bool Invalidate) const{
  Environment NewEnv =
    getStateManager().EnvMgr.bindExpr(Env, EnvironmentEntry(S, LCtx), V,
                                      Invalidate);
  if (NewEnv == Env)
    return this;

  ProgramState NewSt = *this;
  NewSt.Env = NewEnv;
  return getStateManager().getPersistentState(NewSt);
}

ProgramStateRef ProgramState::assumeInBound(DefinedOrUnknownSVal Idx,
                                      DefinedOrUnknownSVal UpperBound,
                                      bool Assumption,
                                      QualType indexTy) const {
  if (Idx.isUnknown() || UpperBound.isUnknown())
    return this;

  // Build an expression for 0 <= Idx < UpperBound.
  // This is the same as Idx + MIN < UpperBound + MIN, if overflow is allowed.
  // FIXME: This should probably be part of SValBuilder.
  ProgramStateManager &SM = getStateManager();
  SValBuilder &svalBuilder = SM.getSValBuilder();
  ASTContext &Ctx = svalBuilder.getContext();

  // Get the offset: the minimum value of the array index type.
  BasicValueFactory &BVF = svalBuilder.getBasicValueFactory();
  if (indexTy.isNull())
    indexTy = svalBuilder.getArrayIndexType();
  nonloc::ConcreteInt Min(BVF.getMinValue(indexTy));

  // Adjust the index.
  SVal newIdx = svalBuilder.evalBinOpNN(this, BO_Add,
                                        Idx.castAs<NonLoc>(), Min, indexTy);
  if (newIdx.isUnknownOrUndef())
    return this;

  // Adjust the upper bound.
  SVal newBound =
    svalBuilder.evalBinOpNN(this, BO_Add, UpperBound.castAs<NonLoc>(),
                            Min, indexTy);

  if (newBound.isUnknownOrUndef())
    return this;

  // Build the actual comparison.
  SVal inBound = svalBuilder.evalBinOpNN(this, BO_LT, newIdx.castAs<NonLoc>(),
                                         newBound.castAs<NonLoc>(), Ctx.IntTy);
  if (inBound.isUnknownOrUndef())
    return this;

  // Finally, let the constraint manager take care of it.
  ConstraintManager &CM = SM.getConstraintManager();
  return CM.assume(this, inBound.castAs<DefinedSVal>(), Assumption);
}

ConditionTruthVal ProgramState::isNonNull(SVal V) const {
  ConditionTruthVal IsNull = isNull(V);
  if (IsNull.isUnderconstrained())
    return IsNull;
  return ConditionTruthVal(!IsNull.getValue());
}

ConditionTruthVal ProgramState::areEqual(SVal Lhs, SVal Rhs) const {
  return stateMgr->getSValBuilder().areEqual(this, Lhs, Rhs);
}

ConditionTruthVal ProgramState::isNull(SVal V) const {
  if (V.isZeroConstant())
    return true;

  if (V.isConstant())
    return false;

  SymbolRef Sym = V.getAsSymbol(/* IncludeBaseRegion */ true);
  if (!Sym)
    return ConditionTruthVal();

  return getStateManager().ConstraintMgr->isNull(this, Sym);
}

ProgramStateRef ProgramStateManager::getInitialState(const LocationContext *InitLoc) {
  ProgramState State(this,
                EnvMgr.getInitialEnvironment(),
                StoreMgr->getInitialStore(InitLoc),
                GDMFactory.getEmptyMap());

  return getPersistentState(State);
}

ProgramStateRef ProgramStateManager::getPersistentStateWithGDM(
                                                     ProgramStateRef FromState,
                                                     ProgramStateRef GDMState) {
  ProgramState NewState(*FromState);
  NewState.GDM = GDMState->GDM;
  return getPersistentState(NewState);
}

ProgramStateRef ProgramStateManager::getPersistentState(ProgramState &State) {

  llvm::FoldingSetNodeID ID;
  State.Profile(ID);
  void *InsertPos;

  if (ProgramState *I = StateSet.FindNodeOrInsertPos(ID, InsertPos))
    return I;

  ProgramState *newState = nullptr;
  if (!freeStates.empty()) {
    newState = freeStates.back();
    freeStates.pop_back();
  }
  else {
    newState = (ProgramState*) Alloc.Allocate<ProgramState>();
  }
  new (newState) ProgramState(State);
  StateSet.InsertNode(newState, InsertPos);
  return newState;
}

ProgramStateRef ProgramState::makeWithStore(const StoreRef &store) const {
  ProgramState NewSt(*this);
  NewSt.setStore(store);
  return getStateManager().getPersistentState(NewSt);
}

void ProgramState::setStore(const StoreRef &newStore) {
  Store newStoreStore = newStore.getStore();
  if (newStoreStore)
    stateMgr->getStoreManager().incrementReferenceCount(newStoreStore);
  if (store)
    stateMgr->getStoreManager().decrementReferenceCount(store);
  store = newStoreStore;
}

//===----------------------------------------------------------------------===//
//  State pretty-printing.
//===----------------------------------------------------------------------===//

void ProgramState::print(raw_ostream &Out,
                         const char *NL, const char *Sep,
                         const LocationContext *LC) const {
  // Print the store.
  ProgramStateManager &Mgr = getStateManager();
  const ASTContext &Context = getStateManager().getContext();
  Mgr.getStoreManager().print(getStore(), Out, NL);

  // Print out the environment.
  Env.print(Out, NL, Sep, Context, LC);

  // Print out the constraints.
  Mgr.getConstraintManager().print(this, Out, NL, Sep);

  // Print out the tracked dynamic types.
  printDynamicTypeInfo(this, Out, NL, Sep);

  // Print out tainted symbols.
  printTaint(Out, NL);

  // Print checker-specific data.
  Mgr.getOwningEngine().printState(Out, this, NL, Sep, LC);
}

void ProgramState::printDOT(raw_ostream &Out,
                            const LocationContext *LC) const {
  print(Out, "\\l", "\\|", LC);
}

LLVM_DUMP_METHOD void ProgramState::dump() const {
  print(llvm::errs());
}

void ProgramState::printTaint(raw_ostream &Out,
                              const char *NL) const {
  TaintMapImpl TM = get<TaintMap>();

  if (!TM.isEmpty())
    Out <<"Tainted symbols:" << NL;

  for (TaintMapImpl::iterator I = TM.begin(), E = TM.end(); I != E; ++I) {
    Out << I->first << " : " << I->second << NL;
  }
}

void ProgramState::dumpTaint() const {
  printTaint(llvm::errs());
}

AnalysisManager& ProgramState::getAnalysisManager() const {
  return stateMgr->getOwningEngine().getAnalysisManager();
}

//===----------------------------------------------------------------------===//
// Generic Data Map.
//===----------------------------------------------------------------------===//

void *const* ProgramState::FindGDM(void *K) const {
  return GDM.lookup(K);
}

void*
ProgramStateManager::FindGDMContext(void *K,
                               void *(*CreateContext)(llvm::BumpPtrAllocator&),
                               void (*DeleteContext)(void*)) {

  std::pair<void*, void (*)(void*)>& p = GDMContexts[K];
  if (!p.first) {
    p.first = CreateContext(Alloc);
    p.second = DeleteContext;
  }

  return p.first;
}

ProgramStateRef ProgramStateManager::addGDM(ProgramStateRef St, void *Key, void *Data){
  ProgramState::GenericDataMap M1 = St->getGDM();
  ProgramState::GenericDataMap M2 = GDMFactory.add(M1, Key, Data);

  if (M1 == M2)
    return St;

  ProgramState NewSt = *St;
  NewSt.GDM = M2;
  return getPersistentState(NewSt);
}

ProgramStateRef ProgramStateManager::removeGDM(ProgramStateRef state, void *Key) {
  ProgramState::GenericDataMap OldM = state->getGDM();
  ProgramState::GenericDataMap NewM = GDMFactory.remove(OldM, Key);

  if (NewM == OldM)
    return state;

  ProgramState NewState = *state;
  NewState.GDM = NewM;
  return getPersistentState(NewState);
}

bool ScanReachableSymbols::scan(nonloc::LazyCompoundVal val) {
  bool wasVisited = !visited.insert(val.getCVData()).second;
  if (wasVisited)
    return true;

  StoreManager &StoreMgr = state->getStateManager().getStoreManager();
  // FIXME: We don't really want to use getBaseRegion() here because pointer
  // arithmetic doesn't apply, but scanReachableSymbols only accepts base
  // regions right now.
  const MemRegion *R = val.getRegion()->getBaseRegion();
  return StoreMgr.scanReachableSymbols(val.getStore(), R, *this);
}

bool ScanReachableSymbols::scan(nonloc::CompoundVal val) {
  for (nonloc::CompoundVal::iterator I=val.begin(), E=val.end(); I!=E; ++I)
    if (!scan(*I))
      return false;

  return true;
}

bool ScanReachableSymbols::scan(const SymExpr *sym) {
  for (SymExpr::symbol_iterator SI = sym->symbol_begin(),
                                SE = sym->symbol_end();
       SI != SE; ++SI) {
    bool wasVisited = !visited.insert(*SI).second;
    if (wasVisited)
      continue;

    if (!visitor.VisitSymbol(*SI))
      return false;
  }

  return true;
}

bool ScanReachableSymbols::scan(SVal val) {
  if (Optional<loc::MemRegionVal> X = val.getAs<loc::MemRegionVal>())
    return scan(X->getRegion());

  if (Optional<nonloc::LazyCompoundVal> X =
          val.getAs<nonloc::LazyCompoundVal>())
    return scan(*X);

  if (Optional<nonloc::LocAsInteger> X = val.getAs<nonloc::LocAsInteger>())
    return scan(X->getLoc());

  if (SymbolRef Sym = val.getAsSymbol())
    return scan(Sym);

  if (const SymExpr *Sym = val.getAsSymbolicExpression())
    return scan(Sym);

  if (Optional<nonloc::CompoundVal> X = val.getAs<nonloc::CompoundVal>())
    return scan(*X);

  return true;
}

bool ScanReachableSymbols::scan(const MemRegion *R) {
  if (isa<MemSpaceRegion>(R))
    return true;

  bool wasVisited = !visited.insert(R).second;
  if (wasVisited)
    return true;

  if (!visitor.VisitMemRegion(R))
    return false;

  // If this is a symbolic region, visit the symbol for the region.
  if (const SymbolicRegion *SR = dyn_cast<SymbolicRegion>(R))
    if (!visitor.VisitSymbol(SR->getSymbol()))
      return false;

  // If this is a subregion, also visit the parent regions.
  if (const SubRegion *SR = dyn_cast<SubRegion>(R)) {
    const MemRegion *Super = SR->getSuperRegion();
    if (!scan(Super))
      return false;

    // When we reach the topmost region, scan all symbols in it.
    if (isa<MemSpaceRegion>(Super)) {
      StoreManager &StoreMgr = state->getStateManager().getStoreManager();
      if (!StoreMgr.scanReachableSymbols(state->getStore(), SR, *this))
        return false;
    }
  }

  // Regions captured by a block are also implicitly reachable.
  if (const BlockDataRegion *BDR = dyn_cast<BlockDataRegion>(R)) {
    BlockDataRegion::referenced_vars_iterator I = BDR->referenced_vars_begin(),
                                              E = BDR->referenced_vars_end();
    for ( ; I != E; ++I) {
      if (!scan(I.getCapturedRegion()))
        return false;
    }
  }

  return true;
}

bool ProgramState::scanReachableSymbols(SVal val, SymbolVisitor& visitor) const {
  ScanReachableSymbols S(this, visitor);
  return S.scan(val);
}

bool ProgramState::scanReachableSymbols(
    llvm::iterator_range<region_iterator> Reachable,
    SymbolVisitor &visitor) const {
  ScanReachableSymbols S(this, visitor);
  for (const MemRegion *R : Reachable) {
    if (!S.scan(R))
      return false;
  }
  return true;
}

ProgramStateRef ProgramState::addTaint(const Stmt *S,
                                           const LocationContext *LCtx,
                                           TaintTagType Kind) const {
  if (const Expr *E = dyn_cast_or_null<Expr>(S))
    S = E->IgnoreParens();

  return addTaint(getSVal(S, LCtx), Kind);
}

ProgramStateRef ProgramState::addTaint(SVal V,
                                       TaintTagType Kind) const {
  SymbolRef Sym = V.getAsSymbol();
  if (Sym)
    return addTaint(Sym, Kind);

  // If the SVal represents a structure, try to mass-taint all values within the
  // structure. For now it only works efficiently on lazy compound values that
  // were conjured during a conservative evaluation of a function - either as
  // return values of functions that return structures or arrays by value, or as
  // values of structures or arrays passed into the function by reference,
  // directly or through pointer aliasing. Such lazy compound values are
  // characterized by having exactly one binding in their captured store within
  // their parent region, which is a conjured symbol default-bound to the base
  // region of the parent region.
  if (auto LCV = V.getAs<nonloc::LazyCompoundVal>()) {
    if (Optional<SVal> binding = getStateManager().StoreMgr->getDefaultBinding(*LCV)) {
      if (SymbolRef Sym = binding->getAsSymbol())
        return addPartialTaint(Sym, LCV->getRegion(), Kind);
    }
  }

  const MemRegion *R = V.getAsRegion();
  return addTaint(R, Kind);
}

ProgramStateRef ProgramState::addTaint(const MemRegion *R,
                                           TaintTagType Kind) const {
  if (const SymbolicRegion *SR = dyn_cast_or_null<SymbolicRegion>(R))
    return addTaint(SR->getSymbol(), Kind);
  return this;
}

ProgramStateRef ProgramState::addTaint(SymbolRef Sym,
                                           TaintTagType Kind) const {
  // If this is a symbol cast, remove the cast before adding the taint. Taint
  // is cast agnostic.
  while (const SymbolCast *SC = dyn_cast<SymbolCast>(Sym))
    Sym = SC->getOperand();

  ProgramStateRef NewState = set<TaintMap>(Sym, Kind);
  assert(NewState);
  return NewState;
}

ProgramStateRef ProgramState::addPartialTaint(SymbolRef ParentSym,
                                              const SubRegion *SubRegion,
                                              TaintTagType Kind) const {
  // Ignore partial taint if the entire parent symbol is already tainted.
  if (contains<TaintMap>(ParentSym) && *get<TaintMap>(ParentSym) == Kind)
    return this;

  // Partial taint applies if only a portion of the symbol is tainted.
  if (SubRegion == SubRegion->getBaseRegion())
    return addTaint(ParentSym, Kind);

  const TaintedSubRegions *SavedRegs = get<DerivedSymTaint>(ParentSym);
  TaintedSubRegions Regs =
      SavedRegs ? *SavedRegs : stateMgr->TSRFactory.getEmptyMap();

  Regs = stateMgr->TSRFactory.add(Regs, SubRegion, Kind);
  ProgramStateRef NewState = set<DerivedSymTaint>(ParentSym, Regs);
  assert(NewState);
  return NewState;
}

bool ProgramState::isTainted(const Stmt *S, const LocationContext *LCtx,
                             TaintTagType Kind) const {
  if (const Expr *E = dyn_cast_or_null<Expr>(S))
    S = E->IgnoreParens();

  SVal val = getSVal(S, LCtx);
  return isTainted(val, Kind);
}

bool ProgramState::isTainted(SVal V, TaintTagType Kind) const {
  if (const SymExpr *Sym = V.getAsSymExpr())
    return isTainted(Sym, Kind);
  if (const MemRegion *Reg = V.getAsRegion())
    return isTainted(Reg, Kind);
  return false;
}

bool ProgramState::isTainted(const MemRegion *Reg, TaintTagType K) const {
  if (!Reg)
    return false;

  // Element region (array element) is tainted if either the base or the offset
  // are tainted.
  if (const ElementRegion *ER = dyn_cast<ElementRegion>(Reg))
    return isTainted(ER->getSuperRegion(), K) || isTainted(ER->getIndex(), K);

  if (const SymbolicRegion *SR = dyn_cast<SymbolicRegion>(Reg))
    return isTainted(SR->getSymbol(), K);

  if (const SubRegion *ER = dyn_cast<SubRegion>(Reg))
    return isTainted(ER->getSuperRegion(), K);

  return false;
}

bool ProgramState::isTainted(SymbolRef Sym, TaintTagType Kind) const {
  if (!Sym)
    return false;

  // Traverse all the symbols this symbol depends on to see if any are tainted.
  for (SymExpr::symbol_iterator SI = Sym->symbol_begin(), SE =Sym->symbol_end();
       SI != SE; ++SI) {
    if (!isa<SymbolData>(*SI))
      continue;

    if (const TaintTagType *Tag = get<TaintMap>(*SI)) {
      if (*Tag == Kind)
        return true;
    }

    if (const SymbolDerived *SD = dyn_cast<SymbolDerived>(*SI)) {
      // If this is a SymbolDerived with a tainted parent, it's also tainted.
      if (isTainted(SD->getParentSymbol(), Kind))
        return true;

      // If this is a SymbolDerived with the same parent symbol as another
      // tainted SymbolDerived and a region that's a sub-region of that tainted
      // symbol, it's also tainted.
      if (const TaintedSubRegions *Regs =
              get<DerivedSymTaint>(SD->getParentSymbol())) {
        const TypedValueRegion *R = SD->getRegion();
        for (auto I : *Regs) {
          // FIXME: The logic to identify tainted regions could be more
          // complete. For example, this would not currently identify
          // overlapping fields in a union as tainted. To identify this we can
          // check for overlapping/nested byte offsets.
          if (Kind == I.second && R->isSubRegionOf(I.first))
            return true;
        }
      }
    }

    // If memory region is tainted, data is also tainted.
    if (const SymbolRegionValue *SRV = dyn_cast<SymbolRegionValue>(*SI)) {
      if (isTainted(SRV->getRegion(), Kind))
        return true;
    }

    // If this is a SymbolCast from a tainted value, it's also tainted.
    if (const SymbolCast *SC = dyn_cast<SymbolCast>(*SI)) {
      if (isTainted(SC->getOperand(), Kind))
        return true;
    }
  }

  return false;
}
