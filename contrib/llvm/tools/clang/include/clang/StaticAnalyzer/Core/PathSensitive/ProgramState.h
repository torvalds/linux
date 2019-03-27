//== ProgramState.h - Path-sensitive "State" for tracking values -*- C++ -*--=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the state of the program along the analysisa path.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_PROGRAMSTATE_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_PROGRAMSTATE_H

#include "clang/Basic/LLVM.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ConstraintManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicTypeInfo.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/Environment.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState_Fwd.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SValBuilder.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/Store.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/TaintTag.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/ImmutableMap.h"
#include "llvm/Support/Allocator.h"
#include <utility>

namespace llvm {
class APSInt;
}

namespace clang {
class ASTContext;

namespace ento {

class AnalysisManager;
class CallEvent;
class CallEventManager;

typedef std::unique_ptr<ConstraintManager>(*ConstraintManagerCreator)(
    ProgramStateManager &, SubEngine *);
typedef std::unique_ptr<StoreManager>(*StoreManagerCreator)(
    ProgramStateManager &);
typedef llvm::ImmutableMap<const SubRegion*, TaintTagType> TaintedSubRegions;

//===----------------------------------------------------------------------===//
// ProgramStateTrait - Traits used by the Generic Data Map of a ProgramState.
//===----------------------------------------------------------------------===//

template <typename T> struct ProgramStatePartialTrait;

template <typename T> struct ProgramStateTrait {
  typedef typename T::data_type data_type;
  static inline void *MakeVoidPtr(data_type D) { return (void*) D; }
  static inline data_type MakeData(void *const* P) {
    return P ? (data_type) *P : (data_type) 0;
  }
};

/// \class ProgramState
/// ProgramState - This class encapsulates:
///
///    1. A mapping from expressions to values (Environment)
///    2. A mapping from locations to values (Store)
///    3. Constraints on symbolic values (GenericDataMap)
///
///  Together these represent the "abstract state" of a program.
///
///  ProgramState is intended to be used as a functional object; that is,
///  once it is created and made "persistent" in a FoldingSet, its
///  values will never change.
class ProgramState : public llvm::FoldingSetNode {
public:
  typedef llvm::ImmutableSet<llvm::APSInt*>                IntSetTy;
  typedef llvm::ImmutableMap<void*, void*>                 GenericDataMap;

private:
  void operator=(const ProgramState& R) = delete;

  friend class ProgramStateManager;
  friend class ExplodedGraph;
  friend class ExplodedNode;

  ProgramStateManager *stateMgr;
  Environment Env;           // Maps a Stmt to its current SVal.
  Store store;               // Maps a location to its current value.
  GenericDataMap   GDM;      // Custom data stored by a client of this class.
  unsigned refCount;

  /// makeWithStore - Return a ProgramState with the same values as the current
  ///  state with the exception of using the specified Store.
  ProgramStateRef makeWithStore(const StoreRef &store) const;

  void setStore(const StoreRef &storeRef);

public:
  /// This ctor is used when creating the first ProgramState object.
  ProgramState(ProgramStateManager *mgr, const Environment& env,
          StoreRef st, GenericDataMap gdm);

  /// Copy ctor - We must explicitly define this or else the "Next" ptr
  ///  in FoldingSetNode will also get copied.
  ProgramState(const ProgramState &RHS);

  ~ProgramState();

  int64_t getID() const;

  /// Return the ProgramStateManager associated with this state.
  ProgramStateManager &getStateManager() const {
    return *stateMgr;
  }

  AnalysisManager &getAnalysisManager() const;

  /// Return the ConstraintManager.
  ConstraintManager &getConstraintManager() const;

  /// getEnvironment - Return the environment associated with this state.
  ///  The environment is the mapping from expressions to values.
  const Environment& getEnvironment() const { return Env; }

  /// Return the store associated with this state.  The store
  ///  is a mapping from locations to values.
  Store getStore() const { return store; }


  /// getGDM - Return the generic data map associated with this state.
  GenericDataMap getGDM() const { return GDM; }

  void setGDM(GenericDataMap gdm) { GDM = gdm; }

  /// Profile - Profile the contents of a ProgramState object for use in a
  ///  FoldingSet.  Two ProgramState objects are considered equal if they
  ///  have the same Environment, Store, and GenericDataMap.
  static void Profile(llvm::FoldingSetNodeID& ID, const ProgramState *V) {
    V->Env.Profile(ID);
    ID.AddPointer(V->store);
    V->GDM.Profile(ID);
  }

  /// Profile - Used to profile the contents of this object for inclusion
  ///  in a FoldingSet.
  void Profile(llvm::FoldingSetNodeID& ID) const {
    Profile(ID, this);
  }

  BasicValueFactory &getBasicVals() const;
  SymbolManager &getSymbolManager() const;

  //==---------------------------------------------------------------------==//
  // Constraints on values.
  //==---------------------------------------------------------------------==//
  //
  // Each ProgramState records constraints on symbolic values.  These constraints
  // are managed using the ConstraintManager associated with a ProgramStateManager.
  // As constraints gradually accrue on symbolic values, added constraints
  // may conflict and indicate that a state is infeasible (as no real values
  // could satisfy all the constraints).  This is the principal mechanism
  // for modeling path-sensitivity in ExprEngine/ProgramState.
  //
  // Various "assume" methods form the interface for adding constraints to
  // symbolic values.  A call to 'assume' indicates an assumption being placed
  // on one or symbolic values.  'assume' methods take the following inputs:
  //
  //  (1) A ProgramState object representing the current state.
  //
  //  (2) The assumed constraint (which is specific to a given "assume" method).
  //
  //  (3) A binary value "Assumption" that indicates whether the constraint is
  //      assumed to be true or false.
  //
  // The output of "assume*" is a new ProgramState object with the added constraints.
  // If no new state is feasible, NULL is returned.
  //

  /// Assumes that the value of \p cond is zero (if \p assumption is "false")
  /// or non-zero (if \p assumption is "true").
  ///
  /// This returns a new state with the added constraint on \p cond.
  /// If no new state is feasible, NULL is returned.
  LLVM_NODISCARD ProgramStateRef assume(DefinedOrUnknownSVal cond,
                                        bool assumption) const;

  /// Assumes both "true" and "false" for \p cond, and returns both
  /// corresponding states (respectively).
  ///
  /// This is more efficient than calling assume() twice. Note that one (but not
  /// both) of the returned states may be NULL.
  LLVM_NODISCARD std::pair<ProgramStateRef, ProgramStateRef>
  assume(DefinedOrUnknownSVal cond) const;

  LLVM_NODISCARD ProgramStateRef
  assumeInBound(DefinedOrUnknownSVal idx, DefinedOrUnknownSVal upperBound,
                bool assumption, QualType IndexType = QualType()) const;

  /// Assumes that the value of \p Val is bounded with [\p From; \p To]
  /// (if \p assumption is "true") or it is fully out of this range
  /// (if \p assumption is "false").
  ///
  /// This returns a new state with the added constraint on \p cond.
  /// If no new state is feasible, NULL is returned.
  LLVM_NODISCARD ProgramStateRef assumeInclusiveRange(DefinedOrUnknownSVal Val,
                                                      const llvm::APSInt &From,
                                                      const llvm::APSInt &To,
                                                      bool assumption) const;

  /// Assumes given range both "true" and "false" for \p Val, and returns both
  /// corresponding states (respectively).
  ///
  /// This is more efficient than calling assume() twice. Note that one (but not
  /// both) of the returned states may be NULL.
  LLVM_NODISCARD std::pair<ProgramStateRef, ProgramStateRef>
  assumeInclusiveRange(DefinedOrUnknownSVal Val, const llvm::APSInt &From,
                       const llvm::APSInt &To) const;

  /// Check if the given SVal is not constrained to zero and is not
  ///        a zero constant.
  ConditionTruthVal isNonNull(SVal V) const;

  /// Check if the given SVal is constrained to zero or is a zero
  ///        constant.
  ConditionTruthVal isNull(SVal V) const;

  /// \return Whether values \p Lhs and \p Rhs are equal.
  ConditionTruthVal areEqual(SVal Lhs, SVal Rhs) const;

  /// Utility method for getting regions.
  const VarRegion* getRegion(const VarDecl *D, const LocationContext *LC) const;

  //==---------------------------------------------------------------------==//
  // Binding and retrieving values to/from the environment and symbolic store.
  //==---------------------------------------------------------------------==//

  /// Create a new state by binding the value 'V' to the statement 'S' in the
  /// state's environment.
  LLVM_NODISCARD ProgramStateRef BindExpr(const Stmt *S,
                                          const LocationContext *LCtx, SVal V,
                                          bool Invalidate = true) const;

  LLVM_NODISCARD ProgramStateRef bindLoc(Loc location, SVal V,
                                         const LocationContext *LCtx,
                                         bool notifyChanges = true) const;

  LLVM_NODISCARD ProgramStateRef bindLoc(SVal location, SVal V,
                                         const LocationContext *LCtx) const;

  /// Initializes the region of memory represented by \p loc with an initial
  /// value. Once initialized, all values loaded from any sub-regions of that
  /// region will be equal to \p V, unless overwritten later by the program.
  /// This method should not be used on regions that are already initialized.
  /// If you need to indicate that memory contents have suddenly become unknown
  /// within a certain region of memory, consider invalidateRegions().
  LLVM_NODISCARD ProgramStateRef
  bindDefaultInitial(SVal loc, SVal V, const LocationContext *LCtx) const;

  /// Performs C++ zero-initialization procedure on the region of memory
  /// represented by \p loc.
  LLVM_NODISCARD ProgramStateRef
  bindDefaultZero(SVal loc, const LocationContext *LCtx) const;

  LLVM_NODISCARD ProgramStateRef killBinding(Loc LV) const;

  /// Returns the state with bindings for the given regions
  ///  cleared from the store.
  ///
  /// Optionally invalidates global regions as well.
  ///
  /// \param Regions the set of regions to be invalidated.
  /// \param E the expression that caused the invalidation.
  /// \param BlockCount The number of times the current basic block has been
  //         visited.
  /// \param CausesPointerEscape the flag is set to true when
  ///        the invalidation entails escape of a symbol (representing a
  ///        pointer). For example, due to it being passed as an argument in a
  ///        call.
  /// \param IS the set of invalidated symbols.
  /// \param Call if non-null, the invalidated regions represent parameters to
  ///        the call and should be considered directly invalidated.
  /// \param ITraits information about special handling for a particular
  ///        region/symbol.
  LLVM_NODISCARD ProgramStateRef
  invalidateRegions(ArrayRef<const MemRegion *> Regions, const Expr *E,
                    unsigned BlockCount, const LocationContext *LCtx,
                    bool CausesPointerEscape, InvalidatedSymbols *IS = nullptr,
                    const CallEvent *Call = nullptr,
                    RegionAndSymbolInvalidationTraits *ITraits = nullptr) const;

  LLVM_NODISCARD ProgramStateRef
  invalidateRegions(ArrayRef<SVal> Regions, const Expr *E,
                    unsigned BlockCount, const LocationContext *LCtx,
                    bool CausesPointerEscape, InvalidatedSymbols *IS = nullptr,
                    const CallEvent *Call = nullptr,
                    RegionAndSymbolInvalidationTraits *ITraits = nullptr) const;

  /// enterStackFrame - Returns the state for entry to the given stack frame,
  ///  preserving the current state.
  LLVM_NODISCARD ProgramStateRef enterStackFrame(
      const CallEvent &Call, const StackFrameContext *CalleeCtx) const;

  /// Get the lvalue for a base class object reference.
  Loc getLValue(const CXXBaseSpecifier &BaseSpec, const SubRegion *Super) const;

  /// Get the lvalue for a base class object reference.
  Loc getLValue(const CXXRecordDecl *BaseClass, const SubRegion *Super,
                bool IsVirtual) const;

  /// Get the lvalue for a variable reference.
  Loc getLValue(const VarDecl *D, const LocationContext *LC) const;

  Loc getLValue(const CompoundLiteralExpr *literal,
                const LocationContext *LC) const;

  /// Get the lvalue for an ivar reference.
  SVal getLValue(const ObjCIvarDecl *decl, SVal base) const;

  /// Get the lvalue for a field reference.
  SVal getLValue(const FieldDecl *decl, SVal Base) const;

  /// Get the lvalue for an indirect field reference.
  SVal getLValue(const IndirectFieldDecl *decl, SVal Base) const;

  /// Get the lvalue for an array index.
  SVal getLValue(QualType ElementType, SVal Idx, SVal Base) const;

  /// Returns the SVal bound to the statement 'S' in the state's environment.
  SVal getSVal(const Stmt *S, const LocationContext *LCtx) const;

  SVal getSValAsScalarOrLoc(const Stmt *Ex, const LocationContext *LCtx) const;

  /// Return the value bound to the specified location.
  /// Returns UnknownVal() if none found.
  SVal getSVal(Loc LV, QualType T = QualType()) const;

  /// Returns the "raw" SVal bound to LV before any value simplfication.
  SVal getRawSVal(Loc LV, QualType T= QualType()) const;

  /// Return the value bound to the specified location.
  /// Returns UnknownVal() if none found.
  SVal getSVal(const MemRegion* R, QualType T = QualType()) const;

  /// Return the value bound to the specified location, assuming
  /// that the value is a scalar integer or an enumeration or a pointer.
  /// Returns UnknownVal() if none found or the region is not known to hold
  /// a value of such type.
  SVal getSValAsScalarOrLoc(const MemRegion *R) const;

  using region_iterator = const MemRegion **;

  /// Visits the symbols reachable from the given SVal using the provided
  /// SymbolVisitor.
  ///
  /// This is a convenience API. Consider using ScanReachableSymbols class
  /// directly when making multiple scans on the same state with the same
  /// visitor to avoid repeated initialization cost.
  /// \sa ScanReachableSymbols
  bool scanReachableSymbols(SVal val, SymbolVisitor& visitor) const;

  /// Visits the symbols reachable from the regions in the given
  /// MemRegions range using the provided SymbolVisitor.
  bool scanReachableSymbols(llvm::iterator_range<region_iterator> Reachable,
                            SymbolVisitor &visitor) const;

  template <typename CB> CB scanReachableSymbols(SVal val) const;
  template <typename CB> CB
  scanReachableSymbols(llvm::iterator_range<region_iterator> Reachable) const;

  /// Create a new state in which the statement is marked as tainted.
  LLVM_NODISCARD ProgramStateRef
  addTaint(const Stmt *S, const LocationContext *LCtx,
           TaintTagType Kind = TaintTagGeneric) const;

  /// Create a new state in which the value is marked as tainted.
  LLVM_NODISCARD ProgramStateRef
  addTaint(SVal V, TaintTagType Kind = TaintTagGeneric) const;

  /// Create a new state in which the symbol is marked as tainted.
  LLVM_NODISCARD ProgramStateRef addTaint(SymbolRef S,
                               TaintTagType Kind = TaintTagGeneric) const;

  /// Create a new state in which the region symbol is marked as tainted.
  LLVM_NODISCARD ProgramStateRef
  addTaint(const MemRegion *R, TaintTagType Kind = TaintTagGeneric) const;

  /// Create a new state in a which a sub-region of a given symbol is tainted.
  /// This might be necessary when referring to regions that can not have an
  /// individual symbol, e.g. if they are represented by the default binding of
  /// a LazyCompoundVal.
  LLVM_NODISCARD ProgramStateRef
  addPartialTaint(SymbolRef ParentSym, const SubRegion *SubRegion,
                  TaintTagType Kind = TaintTagGeneric) const;

  /// Check if the statement is tainted in the current state.
  bool isTainted(const Stmt *S, const LocationContext *LCtx,
                 TaintTagType Kind = TaintTagGeneric) const;
  bool isTainted(SVal V, TaintTagType Kind = TaintTagGeneric) const;
  bool isTainted(SymbolRef Sym, TaintTagType Kind = TaintTagGeneric) const;
  bool isTainted(const MemRegion *Reg, TaintTagType Kind=TaintTagGeneric) const;

  //==---------------------------------------------------------------------==//
  // Accessing the Generic Data Map (GDM).
  //==---------------------------------------------------------------------==//

  void *const* FindGDM(void *K) const;

  template <typename T>
  LLVM_NODISCARD ProgramStateRef
  add(typename ProgramStateTrait<T>::key_type K) const;

  template <typename T>
  typename ProgramStateTrait<T>::data_type
  get() const {
    return ProgramStateTrait<T>::MakeData(FindGDM(ProgramStateTrait<T>::GDMIndex()));
  }

  template<typename T>
  typename ProgramStateTrait<T>::lookup_type
  get(typename ProgramStateTrait<T>::key_type key) const {
    void *const* d = FindGDM(ProgramStateTrait<T>::GDMIndex());
    return ProgramStateTrait<T>::Lookup(ProgramStateTrait<T>::MakeData(d), key);
  }

  template <typename T>
  typename ProgramStateTrait<T>::context_type get_context() const;

  template <typename T>
  LLVM_NODISCARD ProgramStateRef
  remove(typename ProgramStateTrait<T>::key_type K) const;

  template <typename T>
  LLVM_NODISCARD ProgramStateRef
  remove(typename ProgramStateTrait<T>::key_type K,
         typename ProgramStateTrait<T>::context_type C) const;

  template <typename T> LLVM_NODISCARD ProgramStateRef remove() const;

  template <typename T>
  LLVM_NODISCARD ProgramStateRef
  set(typename ProgramStateTrait<T>::data_type D) const;

  template <typename T>
  LLVM_NODISCARD ProgramStateRef
  set(typename ProgramStateTrait<T>::key_type K,
      typename ProgramStateTrait<T>::value_type E) const;

  template <typename T>
  LLVM_NODISCARD ProgramStateRef
  set(typename ProgramStateTrait<T>::key_type K,
      typename ProgramStateTrait<T>::value_type E,
      typename ProgramStateTrait<T>::context_type C) const;

  template<typename T>
  bool contains(typename ProgramStateTrait<T>::key_type key) const {
    void *const* d = FindGDM(ProgramStateTrait<T>::GDMIndex());
    return ProgramStateTrait<T>::Contains(ProgramStateTrait<T>::MakeData(d), key);
  }

  // Pretty-printing.
  void print(raw_ostream &Out, const char *nl = "\n", const char *sep = "",
             const LocationContext *CurrentLC = nullptr) const;
  void printDOT(raw_ostream &Out,
                const LocationContext *CurrentLC = nullptr) const;
  void printTaint(raw_ostream &Out, const char *nl = "\n") const;

  void dump() const;
  void dumpTaint() const;

private:
  friend void ProgramStateRetain(const ProgramState *state);
  friend void ProgramStateRelease(const ProgramState *state);

  /// \sa invalidateValues()
  /// \sa invalidateRegions()
  ProgramStateRef
  invalidateRegionsImpl(ArrayRef<SVal> Values,
                        const Expr *E, unsigned BlockCount,
                        const LocationContext *LCtx,
                        bool ResultsInSymbolEscape,
                        InvalidatedSymbols *IS,
                        RegionAndSymbolInvalidationTraits *HTraits,
                        const CallEvent *Call) const;
};

//===----------------------------------------------------------------------===//
// ProgramStateManager - Factory object for ProgramStates.
//===----------------------------------------------------------------------===//

class ProgramStateManager {
  friend class ProgramState;
  friend void ProgramStateRelease(const ProgramState *state);
private:
  /// Eng - The SubEngine that owns this state manager.
  SubEngine *Eng; /* Can be null. */

  EnvironmentManager                   EnvMgr;
  std::unique_ptr<StoreManager>        StoreMgr;
  std::unique_ptr<ConstraintManager>   ConstraintMgr;

  ProgramState::GenericDataMap::Factory     GDMFactory;
  TaintedSubRegions::Factory TSRFactory;

  typedef llvm::DenseMap<void*,std::pair<void*,void (*)(void*)> > GDMContextsTy;
  GDMContextsTy GDMContexts;

  /// StateSet - FoldingSet containing all the states created for analyzing
  ///  a particular function.  This is used to unique states.
  llvm::FoldingSet<ProgramState> StateSet;

  /// Object that manages the data for all created SVals.
  std::unique_ptr<SValBuilder> svalBuilder;

  /// Manages memory for created CallEvents.
  std::unique_ptr<CallEventManager> CallEventMgr;

  /// A BumpPtrAllocator to allocate states.
  llvm::BumpPtrAllocator &Alloc;

  /// A vector of ProgramStates that we can reuse.
  std::vector<ProgramState *> freeStates;

public:
  ProgramStateManager(ASTContext &Ctx,
                 StoreManagerCreator CreateStoreManager,
                 ConstraintManagerCreator CreateConstraintManager,
                 llvm::BumpPtrAllocator& alloc,
                 SubEngine *subeng);

  ~ProgramStateManager();

  ProgramStateRef getInitialState(const LocationContext *InitLoc);

  ASTContext &getContext() { return svalBuilder->getContext(); }
  const ASTContext &getContext() const { return svalBuilder->getContext(); }

  BasicValueFactory &getBasicVals() {
    return svalBuilder->getBasicValueFactory();
  }

  SValBuilder &getSValBuilder() {
    return *svalBuilder;
  }

  SymbolManager &getSymbolManager() {
    return svalBuilder->getSymbolManager();
  }
  const SymbolManager &getSymbolManager() const {
    return svalBuilder->getSymbolManager();
  }

  llvm::BumpPtrAllocator& getAllocator() { return Alloc; }

  MemRegionManager& getRegionManager() {
    return svalBuilder->getRegionManager();
  }
  const MemRegionManager &getRegionManager() const {
    return svalBuilder->getRegionManager();
  }

  CallEventManager &getCallEventManager() { return *CallEventMgr; }

  StoreManager &getStoreManager() { return *StoreMgr; }
  ConstraintManager &getConstraintManager() { return *ConstraintMgr; }
  SubEngine &getOwningEngine() { return *Eng; }

  ProgramStateRef removeDeadBindings(ProgramStateRef St,
                                    const StackFrameContext *LCtx,
                                    SymbolReaper& SymReaper);

public:

  SVal ArrayToPointer(Loc Array, QualType ElementTy) {
    return StoreMgr->ArrayToPointer(Array, ElementTy);
  }

  // Methods that manipulate the GDM.
  ProgramStateRef addGDM(ProgramStateRef St, void *Key, void *Data);
  ProgramStateRef removeGDM(ProgramStateRef state, void *Key);

  // Methods that query & manipulate the Store.

  void iterBindings(ProgramStateRef state, StoreManager::BindingsHandler& F) {
    StoreMgr->iterBindings(state->getStore(), F);
  }

  ProgramStateRef getPersistentState(ProgramState &Impl);
  ProgramStateRef getPersistentStateWithGDM(ProgramStateRef FromState,
                                           ProgramStateRef GDMState);

  bool haveEqualEnvironments(ProgramStateRef S1, ProgramStateRef S2) {
    return S1->Env == S2->Env;
  }

  bool haveEqualStores(ProgramStateRef S1, ProgramStateRef S2) {
    return S1->store == S2->store;
  }

  //==---------------------------------------------------------------------==//
  // Generic Data Map methods.
  //==---------------------------------------------------------------------==//
  //
  // ProgramStateManager and ProgramState support a "generic data map" that allows
  // different clients of ProgramState objects to embed arbitrary data within a
  // ProgramState object.  The generic data map is essentially an immutable map
  // from a "tag" (that acts as the "key" for a client) and opaque values.
  // Tags/keys and values are simply void* values.  The typical way that clients
  // generate unique tags are by taking the address of a static variable.
  // Clients are responsible for ensuring that data values referred to by a
  // the data pointer are immutable (and thus are essentially purely functional
  // data).
  //
  // The templated methods below use the ProgramStateTrait<T> class
  // to resolve keys into the GDM and to return data values to clients.
  //

  // Trait based GDM dispatch.
  template <typename T>
  ProgramStateRef set(ProgramStateRef st, typename ProgramStateTrait<T>::data_type D) {
    return addGDM(st, ProgramStateTrait<T>::GDMIndex(),
                  ProgramStateTrait<T>::MakeVoidPtr(D));
  }

  template<typename T>
  ProgramStateRef set(ProgramStateRef st,
                     typename ProgramStateTrait<T>::key_type K,
                     typename ProgramStateTrait<T>::value_type V,
                     typename ProgramStateTrait<T>::context_type C) {

    return addGDM(st, ProgramStateTrait<T>::GDMIndex(),
     ProgramStateTrait<T>::MakeVoidPtr(ProgramStateTrait<T>::Set(st->get<T>(), K, V, C)));
  }

  template <typename T>
  ProgramStateRef add(ProgramStateRef st,
                     typename ProgramStateTrait<T>::key_type K,
                     typename ProgramStateTrait<T>::context_type C) {
    return addGDM(st, ProgramStateTrait<T>::GDMIndex(),
        ProgramStateTrait<T>::MakeVoidPtr(ProgramStateTrait<T>::Add(st->get<T>(), K, C)));
  }

  template <typename T>
  ProgramStateRef remove(ProgramStateRef st,
                        typename ProgramStateTrait<T>::key_type K,
                        typename ProgramStateTrait<T>::context_type C) {

    return addGDM(st, ProgramStateTrait<T>::GDMIndex(),
     ProgramStateTrait<T>::MakeVoidPtr(ProgramStateTrait<T>::Remove(st->get<T>(), K, C)));
  }

  template <typename T>
  ProgramStateRef remove(ProgramStateRef st) {
    return removeGDM(st, ProgramStateTrait<T>::GDMIndex());
  }

  void *FindGDMContext(void *index,
                       void *(*CreateContext)(llvm::BumpPtrAllocator&),
                       void  (*DeleteContext)(void*));

  template <typename T>
  typename ProgramStateTrait<T>::context_type get_context() {
    void *p = FindGDMContext(ProgramStateTrait<T>::GDMIndex(),
                             ProgramStateTrait<T>::CreateContext,
                             ProgramStateTrait<T>::DeleteContext);

    return ProgramStateTrait<T>::MakeContext(p);
  }

  void EndPath(ProgramStateRef St) {
    ConstraintMgr->EndPath(St);
  }
};


//===----------------------------------------------------------------------===//
// Out-of-line method definitions for ProgramState.
//===----------------------------------------------------------------------===//

inline ConstraintManager &ProgramState::getConstraintManager() const {
  return stateMgr->getConstraintManager();
}

inline const VarRegion* ProgramState::getRegion(const VarDecl *D,
                                                const LocationContext *LC) const
{
  return getStateManager().getRegionManager().getVarRegion(D, LC);
}

inline ProgramStateRef ProgramState::assume(DefinedOrUnknownSVal Cond,
                                      bool Assumption) const {
  if (Cond.isUnknown())
    return this;

  return getStateManager().ConstraintMgr
      ->assume(this, Cond.castAs<DefinedSVal>(), Assumption);
}

inline std::pair<ProgramStateRef , ProgramStateRef >
ProgramState::assume(DefinedOrUnknownSVal Cond) const {
  if (Cond.isUnknown())
    return std::make_pair(this, this);

  return getStateManager().ConstraintMgr
      ->assumeDual(this, Cond.castAs<DefinedSVal>());
}

inline ProgramStateRef ProgramState::assumeInclusiveRange(
    DefinedOrUnknownSVal Val, const llvm::APSInt &From, const llvm::APSInt &To,
    bool Assumption) const {
  if (Val.isUnknown())
    return this;

  assert(Val.getAs<NonLoc>() && "Only NonLocs are supported!");

  return getStateManager().ConstraintMgr->assumeInclusiveRange(
      this, Val.castAs<NonLoc>(), From, To, Assumption);
}

inline std::pair<ProgramStateRef, ProgramStateRef>
ProgramState::assumeInclusiveRange(DefinedOrUnknownSVal Val,
                                   const llvm::APSInt &From,
                                   const llvm::APSInt &To) const {
  if (Val.isUnknown())
    return std::make_pair(this, this);

  assert(Val.getAs<NonLoc>() && "Only NonLocs are supported!");

  return getStateManager().ConstraintMgr->assumeInclusiveRangeDual(
      this, Val.castAs<NonLoc>(), From, To);
}

inline ProgramStateRef ProgramState::bindLoc(SVal LV, SVal V, const LocationContext *LCtx) const {
  if (Optional<Loc> L = LV.getAs<Loc>())
    return bindLoc(*L, V, LCtx);
  return this;
}

inline Loc ProgramState::getLValue(const CXXBaseSpecifier &BaseSpec,
                                   const SubRegion *Super) const {
  const auto *Base = BaseSpec.getType()->getAsCXXRecordDecl();
  return loc::MemRegionVal(
           getStateManager().getRegionManager().getCXXBaseObjectRegion(
                                            Base, Super, BaseSpec.isVirtual()));
}

inline Loc ProgramState::getLValue(const CXXRecordDecl *BaseClass,
                                   const SubRegion *Super,
                                   bool IsVirtual) const {
  return loc::MemRegionVal(
           getStateManager().getRegionManager().getCXXBaseObjectRegion(
                                                  BaseClass, Super, IsVirtual));
}

inline Loc ProgramState::getLValue(const VarDecl *VD,
                               const LocationContext *LC) const {
  return getStateManager().StoreMgr->getLValueVar(VD, LC);
}

inline Loc ProgramState::getLValue(const CompoundLiteralExpr *literal,
                               const LocationContext *LC) const {
  return getStateManager().StoreMgr->getLValueCompoundLiteral(literal, LC);
}

inline SVal ProgramState::getLValue(const ObjCIvarDecl *D, SVal Base) const {
  return getStateManager().StoreMgr->getLValueIvar(D, Base);
}

inline SVal ProgramState::getLValue(const FieldDecl *D, SVal Base) const {
  return getStateManager().StoreMgr->getLValueField(D, Base);
}

inline SVal ProgramState::getLValue(const IndirectFieldDecl *D,
                                    SVal Base) const {
  StoreManager &SM = *getStateManager().StoreMgr;
  for (const auto *I : D->chain()) {
    Base = SM.getLValueField(cast<FieldDecl>(I), Base);
  }

  return Base;
}

inline SVal ProgramState::getLValue(QualType ElementType, SVal Idx, SVal Base) const{
  if (Optional<NonLoc> N = Idx.getAs<NonLoc>())
    return getStateManager().StoreMgr->getLValueElement(ElementType, *N, Base);
  return UnknownVal();
}

inline SVal ProgramState::getSVal(const Stmt *Ex,
                                  const LocationContext *LCtx) const{
  return Env.getSVal(EnvironmentEntry(Ex, LCtx),
                     *getStateManager().svalBuilder);
}

inline SVal
ProgramState::getSValAsScalarOrLoc(const Stmt *S,
                                   const LocationContext *LCtx) const {
  if (const Expr *Ex = dyn_cast<Expr>(S)) {
    QualType T = Ex->getType();
    if (Ex->isGLValue() || Loc::isLocType(T) ||
        T->isIntegralOrEnumerationType())
      return getSVal(S, LCtx);
  }

  return UnknownVal();
}

inline SVal ProgramState::getRawSVal(Loc LV, QualType T) const {
  return getStateManager().StoreMgr->getBinding(getStore(), LV, T);
}

inline SVal ProgramState::getSVal(const MemRegion* R, QualType T) const {
  return getStateManager().StoreMgr->getBinding(getStore(),
                                                loc::MemRegionVal(R),
                                                T);
}

inline BasicValueFactory &ProgramState::getBasicVals() const {
  return getStateManager().getBasicVals();
}

inline SymbolManager &ProgramState::getSymbolManager() const {
  return getStateManager().getSymbolManager();
}

template<typename T>
ProgramStateRef ProgramState::add(typename ProgramStateTrait<T>::key_type K) const {
  return getStateManager().add<T>(this, K, get_context<T>());
}

template <typename T>
typename ProgramStateTrait<T>::context_type ProgramState::get_context() const {
  return getStateManager().get_context<T>();
}

template<typename T>
ProgramStateRef ProgramState::remove(typename ProgramStateTrait<T>::key_type K) const {
  return getStateManager().remove<T>(this, K, get_context<T>());
}

template<typename T>
ProgramStateRef ProgramState::remove(typename ProgramStateTrait<T>::key_type K,
                               typename ProgramStateTrait<T>::context_type C) const {
  return getStateManager().remove<T>(this, K, C);
}

template <typename T>
ProgramStateRef ProgramState::remove() const {
  return getStateManager().remove<T>(this);
}

template<typename T>
ProgramStateRef ProgramState::set(typename ProgramStateTrait<T>::data_type D) const {
  return getStateManager().set<T>(this, D);
}

template<typename T>
ProgramStateRef ProgramState::set(typename ProgramStateTrait<T>::key_type K,
                            typename ProgramStateTrait<T>::value_type E) const {
  return getStateManager().set<T>(this, K, E, get_context<T>());
}

template<typename T>
ProgramStateRef ProgramState::set(typename ProgramStateTrait<T>::key_type K,
                            typename ProgramStateTrait<T>::value_type E,
                            typename ProgramStateTrait<T>::context_type C) const {
  return getStateManager().set<T>(this, K, E, C);
}

template <typename CB>
CB ProgramState::scanReachableSymbols(SVal val) const {
  CB cb(this);
  scanReachableSymbols(val, cb);
  return cb;
}

template <typename CB>
CB ProgramState::scanReachableSymbols(
    llvm::iterator_range<region_iterator> Reachable) const {
  CB cb(this);
  scanReachableSymbols(Reachable, cb);
  return cb;
}

/// \class ScanReachableSymbols
/// A utility class that visits the reachable symbols using a custom
/// SymbolVisitor. Terminates recursive traversal when the visitor function
/// returns false.
class ScanReachableSymbols {
  typedef llvm::DenseSet<const void*> VisitedItems;

  VisitedItems visited;
  ProgramStateRef state;
  SymbolVisitor &visitor;
public:
  ScanReachableSymbols(ProgramStateRef st, SymbolVisitor &v)
      : state(std::move(st)), visitor(v) {}

  bool scan(nonloc::LazyCompoundVal val);
  bool scan(nonloc::CompoundVal val);
  bool scan(SVal val);
  bool scan(const MemRegion *R);
  bool scan(const SymExpr *sym);
};

} // end ento namespace

} // end clang namespace

#endif
