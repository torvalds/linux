//===- StmtOpenMP.h - Classes for OpenMP directives  ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file defines OpenMP AST classes for executable directives and
/// clauses.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_STMTOPENMP_H
#define LLVM_CLANG_AST_STMTOPENMP_H

#include "clang/AST/Expr.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/OpenMPKinds.h"
#include "clang/Basic/SourceLocation.h"

namespace clang {

//===----------------------------------------------------------------------===//
// AST classes for directives.
//===----------------------------------------------------------------------===//

/// This is a basic class for representing single OpenMP executable
/// directive.
///
class OMPExecutableDirective : public Stmt {
  friend class ASTStmtReader;
  /// Kind of the directive.
  OpenMPDirectiveKind Kind;
  /// Starting location of the directive (directive keyword).
  SourceLocation StartLoc;
  /// Ending location of the directive.
  SourceLocation EndLoc;
  /// Numbers of clauses.
  const unsigned NumClauses;
  /// Number of child expressions/stmts.
  const unsigned NumChildren;
  /// Offset from this to the start of clauses.
  /// There are NumClauses pointers to clauses, they are followed by
  /// NumChildren pointers to child stmts/exprs (if the directive type
  /// requires an associated stmt, then it has to be the first of them).
  const unsigned ClausesOffset;

  /// Get the clauses storage.
  MutableArrayRef<OMPClause *> getClauses() {
    OMPClause **ClauseStorage = reinterpret_cast<OMPClause **>(
        reinterpret_cast<char *>(this) + ClausesOffset);
    return MutableArrayRef<OMPClause *>(ClauseStorage, NumClauses);
  }

protected:
  /// Build instance of directive of class \a K.
  ///
  /// \param SC Statement class.
  /// \param K Kind of OpenMP directive.
  /// \param StartLoc Starting location of the directive (directive keyword).
  /// \param EndLoc Ending location of the directive.
  ///
  template <typename T>
  OMPExecutableDirective(const T *, StmtClass SC, OpenMPDirectiveKind K,
                         SourceLocation StartLoc, SourceLocation EndLoc,
                         unsigned NumClauses, unsigned NumChildren)
      : Stmt(SC), Kind(K), StartLoc(std::move(StartLoc)),
        EndLoc(std::move(EndLoc)), NumClauses(NumClauses),
        NumChildren(NumChildren),
        ClausesOffset(llvm::alignTo(sizeof(T), alignof(OMPClause *))) {}

  /// Sets the list of variables for this clause.
  ///
  /// \param Clauses The list of clauses for the directive.
  ///
  void setClauses(ArrayRef<OMPClause *> Clauses);

  /// Set the associated statement for the directive.
  ///
  /// /param S Associated statement.
  ///
  void setAssociatedStmt(Stmt *S) {
    assert(hasAssociatedStmt() && "no associated statement.");
    *child_begin() = S;
  }

public:
  /// Iterates over a filtered subrange of clauses applied to a
  /// directive.
  ///
  /// This iterator visits only clauses of type SpecificClause.
  template <typename SpecificClause>
  class specific_clause_iterator
      : public llvm::iterator_adaptor_base<
            specific_clause_iterator<SpecificClause>,
            ArrayRef<OMPClause *>::const_iterator, std::forward_iterator_tag,
            const SpecificClause *, ptrdiff_t, const SpecificClause *,
            const SpecificClause *> {
    ArrayRef<OMPClause *>::const_iterator End;

    void SkipToNextClause() {
      while (this->I != End && !isa<SpecificClause>(*this->I))
        ++this->I;
    }

  public:
    explicit specific_clause_iterator(ArrayRef<OMPClause *> Clauses)
        : specific_clause_iterator::iterator_adaptor_base(Clauses.begin()),
          End(Clauses.end()) {
      SkipToNextClause();
    }

    const SpecificClause *operator*() const {
      return cast<SpecificClause>(*this->I);
    }
    const SpecificClause *operator->() const { return **this; }

    specific_clause_iterator &operator++() {
      ++this->I;
      SkipToNextClause();
      return *this;
    }
  };

  template <typename SpecificClause>
  static llvm::iterator_range<specific_clause_iterator<SpecificClause>>
  getClausesOfKind(ArrayRef<OMPClause *> Clauses) {
    return {specific_clause_iterator<SpecificClause>(Clauses),
            specific_clause_iterator<SpecificClause>(
                llvm::makeArrayRef(Clauses.end(), 0))};
  }

  template <typename SpecificClause>
  llvm::iterator_range<specific_clause_iterator<SpecificClause>>
  getClausesOfKind() const {
    return getClausesOfKind<SpecificClause>(clauses());
  }

  /// Gets a single clause of the specified kind associated with the
  /// current directive iff there is only one clause of this kind (and assertion
  /// is fired if there is more than one clause is associated with the
  /// directive). Returns nullptr if no clause of this kind is associated with
  /// the directive.
  template <typename SpecificClause>
  const SpecificClause *getSingleClause() const {
    auto Clauses = getClausesOfKind<SpecificClause>();

    if (Clauses.begin() != Clauses.end()) {
      assert(std::next(Clauses.begin()) == Clauses.end() &&
             "There are at least 2 clauses of the specified kind");
      return *Clauses.begin();
    }
    return nullptr;
  }

  /// Returns true if the current directive has one or more clauses of a
  /// specific kind.
  template <typename SpecificClause>
  bool hasClausesOfKind() const {
    auto Clauses = getClausesOfKind<SpecificClause>();
    return Clauses.begin() != Clauses.end();
  }

  /// Returns starting location of directive kind.
  SourceLocation getBeginLoc() const { return StartLoc; }
  /// Returns ending location of directive.
  SourceLocation getEndLoc() const { return EndLoc; }

  /// Set starting location of directive kind.
  ///
  /// \param Loc New starting location of directive.
  ///
  void setLocStart(SourceLocation Loc) { StartLoc = Loc; }
  /// Set ending location of directive.
  ///
  /// \param Loc New ending location of directive.
  ///
  void setLocEnd(SourceLocation Loc) { EndLoc = Loc; }

  /// Get number of clauses.
  unsigned getNumClauses() const { return NumClauses; }

  /// Returns specified clause.
  ///
  /// \param i Number of clause.
  ///
  OMPClause *getClause(unsigned i) const { return clauses()[i]; }

  /// Returns true if directive has associated statement.
  bool hasAssociatedStmt() const { return NumChildren > 0; }

  /// Returns statement associated with the directive.
  const Stmt *getAssociatedStmt() const {
    assert(hasAssociatedStmt() && "no associated statement.");
    return *child_begin();
  }
  Stmt *getAssociatedStmt() {
    assert(hasAssociatedStmt() && "no associated statement.");
    return *child_begin();
  }

  /// Returns the captured statement associated with the
  /// component region within the (combined) directive.
  //
  // \param RegionKind Component region kind.
  const CapturedStmt *getCapturedStmt(OpenMPDirectiveKind RegionKind) const {
    SmallVector<OpenMPDirectiveKind, 4> CaptureRegions;
    getOpenMPCaptureRegions(CaptureRegions, getDirectiveKind());
    assert(std::any_of(
               CaptureRegions.begin(), CaptureRegions.end(),
               [=](const OpenMPDirectiveKind K) { return K == RegionKind; }) &&
           "RegionKind not found in OpenMP CaptureRegions.");
    auto *CS = cast<CapturedStmt>(getAssociatedStmt());
    for (auto ThisCaptureRegion : CaptureRegions) {
      if (ThisCaptureRegion == RegionKind)
        return CS;
      CS = cast<CapturedStmt>(CS->getCapturedStmt());
    }
    llvm_unreachable("Incorrect RegionKind specified for directive.");
  }

  /// Get innermost captured statement for the construct.
  CapturedStmt *getInnermostCapturedStmt() {
    assert(hasAssociatedStmt() && getAssociatedStmt() &&
           "Must have associated statement.");
    SmallVector<OpenMPDirectiveKind, 4> CaptureRegions;
    getOpenMPCaptureRegions(CaptureRegions, getDirectiveKind());
    assert(!CaptureRegions.empty() &&
           "At least one captured statement must be provided.");
    auto *CS = cast<CapturedStmt>(getAssociatedStmt());
    for (unsigned Level = CaptureRegions.size(); Level > 1; --Level)
      CS = cast<CapturedStmt>(CS->getCapturedStmt());
    return CS;
  }

  const CapturedStmt *getInnermostCapturedStmt() const {
    return const_cast<OMPExecutableDirective *>(this)
        ->getInnermostCapturedStmt();
  }

  OpenMPDirectiveKind getDirectiveKind() const { return Kind; }

  static bool classof(const Stmt *S) {
    return S->getStmtClass() >= firstOMPExecutableDirectiveConstant &&
           S->getStmtClass() <= lastOMPExecutableDirectiveConstant;
  }

  child_range children() {
    if (!hasAssociatedStmt())
      return child_range(child_iterator(), child_iterator());
    Stmt **ChildStorage = reinterpret_cast<Stmt **>(getClauses().end());
    /// Do not mark all the special expression/statements as children, except
    /// for the associated statement.
    return child_range(ChildStorage, ChildStorage + 1);
  }

  ArrayRef<OMPClause *> clauses() { return getClauses(); }

  ArrayRef<OMPClause *> clauses() const {
    return const_cast<OMPExecutableDirective *>(this)->getClauses();
  }
};

/// This represents '#pragma omp parallel' directive.
///
/// \code
/// #pragma omp parallel private(a,b) reduction(+: c,d)
/// \endcode
/// In this example directive '#pragma omp parallel' has clauses 'private'
/// with the variables 'a' and 'b' and 'reduction' with operator '+' and
/// variables 'c' and 'd'.
///
class OMPParallelDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  /// true if the construct has inner cancel directive.
  bool HasCancel;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive (directive keyword).
  /// \param EndLoc Ending Location of the directive.
  ///
  OMPParallelDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                       unsigned NumClauses)
      : OMPExecutableDirective(this, OMPParallelDirectiveClass, OMPD_parallel,
                               StartLoc, EndLoc, NumClauses, 1),
        HasCancel(false) {}

  /// Build an empty directive.
  ///
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPParallelDirective(unsigned NumClauses)
      : OMPExecutableDirective(this, OMPParallelDirectiveClass, OMPD_parallel,
                               SourceLocation(), SourceLocation(), NumClauses,
                               1),
        HasCancel(false) {}

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement associated with the directive.
  /// \param HasCancel true if this directive has inner cancel directive.
  ///
  static OMPParallelDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt, bool HasCancel);

  /// Creates an empty directive with the place for \a N clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPParallelDirective *CreateEmpty(const ASTContext &C,
                                           unsigned NumClauses, EmptyShell);

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPParallelDirectiveClass;
  }
};

/// This is a common base class for loop directives ('omp simd', 'omp
/// for', 'omp for simd' etc.). It is responsible for the loop code generation.
///
class OMPLoopDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  /// Number of collapsed loops as specified by 'collapse' clause.
  unsigned CollapsedNum;

  /// Offsets to the stored exprs.
  /// This enumeration contains offsets to all the pointers to children
  /// expressions stored in OMPLoopDirective.
  /// The first 9 children are necessary for all the loop directives,
  /// the next 8 are specific to the worksharing ones, and the next 11 are
  /// used for combined constructs containing two pragmas associated to loops.
  /// After the fixed children, three arrays of length CollapsedNum are
  /// allocated: loop counters, their updates and final values.
  /// PrevLowerBound and PrevUpperBound are used to communicate blocking
  /// information in composite constructs which require loop blocking
  /// DistInc is used to generate the increment expression for the distribute
  /// loop when combined with a further nested loop
  /// PrevEnsureUpperBound is used as the EnsureUpperBound expression for the
  /// for loop when combined with a previous distribute loop in the same pragma
  /// (e.g. 'distribute parallel for')
  ///
  enum {
    AssociatedStmtOffset = 0,
    IterationVariableOffset = 1,
    LastIterationOffset = 2,
    CalcLastIterationOffset = 3,
    PreConditionOffset = 4,
    CondOffset = 5,
    InitOffset = 6,
    IncOffset = 7,
    PreInitsOffset = 8,
    // The '...End' enumerators do not correspond to child expressions - they
    // specify the offset to the end (and start of the following counters/
    // updates/finals arrays).
    DefaultEnd = 9,
    // The following 8 exprs are used by worksharing and distribute loops only.
    IsLastIterVariableOffset = 9,
    LowerBoundVariableOffset = 10,
    UpperBoundVariableOffset = 11,
    StrideVariableOffset = 12,
    EnsureUpperBoundOffset = 13,
    NextLowerBoundOffset = 14,
    NextUpperBoundOffset = 15,
    NumIterationsOffset = 16,
    // Offset to the end for worksharing loop directives.
    WorksharingEnd = 17,
    PrevLowerBoundVariableOffset = 17,
    PrevUpperBoundVariableOffset = 18,
    DistIncOffset = 19,
    PrevEnsureUpperBoundOffset = 20,
    CombinedLowerBoundVariableOffset = 21,
    CombinedUpperBoundVariableOffset = 22,
    CombinedEnsureUpperBoundOffset = 23,
    CombinedInitOffset = 24,
    CombinedConditionOffset = 25,
    CombinedNextLowerBoundOffset = 26,
    CombinedNextUpperBoundOffset = 27,
    CombinedDistConditionOffset = 28,
    CombinedParForInDistConditionOffset = 29,
    // Offset to the end (and start of the following counters/updates/finals
    // arrays) for combined distribute loop directives.
    CombinedDistributeEnd = 30,
  };

  /// Get the counters storage.
  MutableArrayRef<Expr *> getCounters() {
    Expr **Storage = reinterpret_cast<Expr **>(
        &(*(std::next(child_begin(), getArraysOffset(getDirectiveKind())))));
    return MutableArrayRef<Expr *>(Storage, CollapsedNum);
  }

  /// Get the private counters storage.
  MutableArrayRef<Expr *> getPrivateCounters() {
    Expr **Storage = reinterpret_cast<Expr **>(&*std::next(
        child_begin(), getArraysOffset(getDirectiveKind()) + CollapsedNum));
    return MutableArrayRef<Expr *>(Storage, CollapsedNum);
  }

  /// Get the updates storage.
  MutableArrayRef<Expr *> getInits() {
    Expr **Storage = reinterpret_cast<Expr **>(
        &*std::next(child_begin(),
                    getArraysOffset(getDirectiveKind()) + 2 * CollapsedNum));
    return MutableArrayRef<Expr *>(Storage, CollapsedNum);
  }

  /// Get the updates storage.
  MutableArrayRef<Expr *> getUpdates() {
    Expr **Storage = reinterpret_cast<Expr **>(
        &*std::next(child_begin(),
                    getArraysOffset(getDirectiveKind()) + 3 * CollapsedNum));
    return MutableArrayRef<Expr *>(Storage, CollapsedNum);
  }

  /// Get the final counter updates storage.
  MutableArrayRef<Expr *> getFinals() {
    Expr **Storage = reinterpret_cast<Expr **>(
        &*std::next(child_begin(),
                    getArraysOffset(getDirectiveKind()) + 4 * CollapsedNum));
    return MutableArrayRef<Expr *>(Storage, CollapsedNum);
  }

protected:
  /// Build instance of loop directive of class \a Kind.
  ///
  /// \param SC Statement class.
  /// \param Kind Kind of OpenMP directive.
  /// \param StartLoc Starting location of the directive (directive keyword).
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed loops from 'collapse' clause.
  /// \param NumClauses Number of clauses.
  /// \param NumSpecialChildren Number of additional directive-specific stmts.
  ///
  template <typename T>
  OMPLoopDirective(const T *That, StmtClass SC, OpenMPDirectiveKind Kind,
                   SourceLocation StartLoc, SourceLocation EndLoc,
                   unsigned CollapsedNum, unsigned NumClauses,
                   unsigned NumSpecialChildren = 0)
      : OMPExecutableDirective(That, SC, Kind, StartLoc, EndLoc, NumClauses,
                               numLoopChildren(CollapsedNum, Kind) +
                                   NumSpecialChildren),
        CollapsedNum(CollapsedNum) {}

  /// Offset to the start of children expression arrays.
  static unsigned getArraysOffset(OpenMPDirectiveKind Kind) {
    if (isOpenMPLoopBoundSharingDirective(Kind))
      return CombinedDistributeEnd;
    if (isOpenMPWorksharingDirective(Kind) || isOpenMPTaskLoopDirective(Kind) ||
        isOpenMPDistributeDirective(Kind))
      return WorksharingEnd;
    return DefaultEnd;
  }

  /// Children number.
  static unsigned numLoopChildren(unsigned CollapsedNum,
                                  OpenMPDirectiveKind Kind) {
    return getArraysOffset(Kind) + 5 * CollapsedNum; // Counters,
                                                     // PrivateCounters, Inits,
                                                     // Updates and Finals
  }

  void setIterationVariable(Expr *IV) {
    *std::next(child_begin(), IterationVariableOffset) = IV;
  }
  void setLastIteration(Expr *LI) {
    *std::next(child_begin(), LastIterationOffset) = LI;
  }
  void setCalcLastIteration(Expr *CLI) {
    *std::next(child_begin(), CalcLastIterationOffset) = CLI;
  }
  void setPreCond(Expr *PC) {
    *std::next(child_begin(), PreConditionOffset) = PC;
  }
  void setCond(Expr *Cond) {
    *std::next(child_begin(), CondOffset) = Cond;
  }
  void setInit(Expr *Init) { *std::next(child_begin(), InitOffset) = Init; }
  void setInc(Expr *Inc) { *std::next(child_begin(), IncOffset) = Inc; }
  void setPreInits(Stmt *PreInits) {
    *std::next(child_begin(), PreInitsOffset) = PreInits;
  }
  void setIsLastIterVariable(Expr *IL) {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    *std::next(child_begin(), IsLastIterVariableOffset) = IL;
  }
  void setLowerBoundVariable(Expr *LB) {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    *std::next(child_begin(), LowerBoundVariableOffset) = LB;
  }
  void setUpperBoundVariable(Expr *UB) {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    *std::next(child_begin(), UpperBoundVariableOffset) = UB;
  }
  void setStrideVariable(Expr *ST) {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    *std::next(child_begin(), StrideVariableOffset) = ST;
  }
  void setEnsureUpperBound(Expr *EUB) {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    *std::next(child_begin(), EnsureUpperBoundOffset) = EUB;
  }
  void setNextLowerBound(Expr *NLB) {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    *std::next(child_begin(), NextLowerBoundOffset) = NLB;
  }
  void setNextUpperBound(Expr *NUB) {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    *std::next(child_begin(), NextUpperBoundOffset) = NUB;
  }
  void setNumIterations(Expr *NI) {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    *std::next(child_begin(), NumIterationsOffset) = NI;
  }
  void setPrevLowerBoundVariable(Expr *PrevLB) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    *std::next(child_begin(), PrevLowerBoundVariableOffset) = PrevLB;
  }
  void setPrevUpperBoundVariable(Expr *PrevUB) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    *std::next(child_begin(), PrevUpperBoundVariableOffset) = PrevUB;
  }
  void setDistInc(Expr *DistInc) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    *std::next(child_begin(), DistIncOffset) = DistInc;
  }
  void setPrevEnsureUpperBound(Expr *PrevEUB) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    *std::next(child_begin(), PrevEnsureUpperBoundOffset) = PrevEUB;
  }
  void setCombinedLowerBoundVariable(Expr *CombLB) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    *std::next(child_begin(), CombinedLowerBoundVariableOffset) = CombLB;
  }
  void setCombinedUpperBoundVariable(Expr *CombUB) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    *std::next(child_begin(), CombinedUpperBoundVariableOffset) = CombUB;
  }
  void setCombinedEnsureUpperBound(Expr *CombEUB) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    *std::next(child_begin(), CombinedEnsureUpperBoundOffset) = CombEUB;
  }
  void setCombinedInit(Expr *CombInit) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    *std::next(child_begin(), CombinedInitOffset) = CombInit;
  }
  void setCombinedCond(Expr *CombCond) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    *std::next(child_begin(), CombinedConditionOffset) = CombCond;
  }
  void setCombinedNextLowerBound(Expr *CombNLB) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    *std::next(child_begin(), CombinedNextLowerBoundOffset) = CombNLB;
  }
  void setCombinedNextUpperBound(Expr *CombNUB) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    *std::next(child_begin(), CombinedNextUpperBoundOffset) = CombNUB;
  }
  void setCombinedDistCond(Expr *CombDistCond) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound distribute sharing directive");
    *std::next(child_begin(), CombinedDistConditionOffset) = CombDistCond;
  }
  void setCombinedParForInDistCond(Expr *CombParForInDistCond) {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound distribute sharing directive");
    *std::next(child_begin(),
               CombinedParForInDistConditionOffset) = CombParForInDistCond;
  }
  void setCounters(ArrayRef<Expr *> A);
  void setPrivateCounters(ArrayRef<Expr *> A);
  void setInits(ArrayRef<Expr *> A);
  void setUpdates(ArrayRef<Expr *> A);
  void setFinals(ArrayRef<Expr *> A);

public:
  /// The expressions built to support OpenMP loops in combined/composite
  /// pragmas (e.g. pragma omp distribute parallel for)
  struct DistCombinedHelperExprs {
    /// DistributeLowerBound - used when composing 'omp distribute' with
    /// 'omp for' in a same construct.
    Expr *LB;
    /// DistributeUpperBound - used when composing 'omp distribute' with
    /// 'omp for' in a same construct.
    Expr *UB;
    /// DistributeEnsureUpperBound - used when composing 'omp distribute'
    ///  with 'omp for' in a same construct, EUB depends on DistUB
    Expr *EUB;
    /// Distribute loop iteration variable init used when composing 'omp
    /// distribute'
    ///  with 'omp for' in a same construct
    Expr *Init;
    /// Distribute Loop condition used when composing 'omp distribute'
    ///  with 'omp for' in a same construct
    Expr *Cond;
    /// Update of LowerBound for statically scheduled omp loops for
    /// outer loop in combined constructs (e.g. 'distribute parallel for')
    Expr *NLB;
    /// Update of UpperBound for statically scheduled omp loops for
    /// outer loop in combined constructs (e.g. 'distribute parallel for')
    Expr *NUB;
    /// Distribute Loop condition used when composing 'omp distribute'
    ///  with 'omp for' in a same construct when schedule is chunked.
    Expr *DistCond;
    /// 'omp parallel for' loop condition used when composed with
    /// 'omp distribute' in the same construct and when schedule is
    /// chunked and the chunk size is 1.
    Expr *ParForInDistCond;
  };

  /// The expressions built for the OpenMP loop CodeGen for the
  /// whole collapsed loop nest.
  struct HelperExprs {
    /// Loop iteration variable.
    Expr *IterationVarRef;
    /// Loop last iteration number.
    Expr *LastIteration;
    /// Loop number of iterations.
    Expr *NumIterations;
    /// Calculation of last iteration.
    Expr *CalcLastIteration;
    /// Loop pre-condition.
    Expr *PreCond;
    /// Loop condition.
    Expr *Cond;
    /// Loop iteration variable init.
    Expr *Init;
    /// Loop increment.
    Expr *Inc;
    /// IsLastIteration - local flag variable passed to runtime.
    Expr *IL;
    /// LowerBound - local variable passed to runtime.
    Expr *LB;
    /// UpperBound - local variable passed to runtime.
    Expr *UB;
    /// Stride - local variable passed to runtime.
    Expr *ST;
    /// EnsureUpperBound -- expression UB = min(UB, NumIterations).
    Expr *EUB;
    /// Update of LowerBound for statically scheduled 'omp for' loops.
    Expr *NLB;
    /// Update of UpperBound for statically scheduled 'omp for' loops.
    Expr *NUB;
    /// PreviousLowerBound - local variable passed to runtime in the
    /// enclosing schedule or null if that does not apply.
    Expr *PrevLB;
    /// PreviousUpperBound - local variable passed to runtime in the
    /// enclosing schedule or null if that does not apply.
    Expr *PrevUB;
    /// DistInc - increment expression for distribute loop when found
    /// combined with a further loop level (e.g. in 'distribute parallel for')
    /// expression IV = IV + ST
    Expr *DistInc;
    /// PrevEUB - expression similar to EUB but to be used when loop
    /// scheduling uses PrevLB and PrevUB (e.g.  in 'distribute parallel for'
    /// when ensuring that the UB is either the calculated UB by the runtime or
    /// the end of the assigned distribute chunk)
    /// expression UB = min (UB, PrevUB)
    Expr *PrevEUB;
    /// Counters Loop counters.
    SmallVector<Expr *, 4> Counters;
    /// PrivateCounters Loop counters.
    SmallVector<Expr *, 4> PrivateCounters;
    /// Expressions for loop counters inits for CodeGen.
    SmallVector<Expr *, 4> Inits;
    /// Expressions for loop counters update for CodeGen.
    SmallVector<Expr *, 4> Updates;
    /// Final loop counter values for GodeGen.
    SmallVector<Expr *, 4> Finals;
    /// Init statement for all captured expressions.
    Stmt *PreInits;

    /// Expressions used when combining OpenMP loop pragmas
    DistCombinedHelperExprs DistCombinedFields;

    /// Check if all the expressions are built (does not check the
    /// worksharing ones).
    bool builtAll() {
      return IterationVarRef != nullptr && LastIteration != nullptr &&
             NumIterations != nullptr && PreCond != nullptr &&
             Cond != nullptr && Init != nullptr && Inc != nullptr;
    }

    /// Initialize all the fields to null.
    /// \param Size Number of elements in the counters/finals/updates arrays.
    void clear(unsigned Size) {
      IterationVarRef = nullptr;
      LastIteration = nullptr;
      CalcLastIteration = nullptr;
      PreCond = nullptr;
      Cond = nullptr;
      Init = nullptr;
      Inc = nullptr;
      IL = nullptr;
      LB = nullptr;
      UB = nullptr;
      ST = nullptr;
      EUB = nullptr;
      NLB = nullptr;
      NUB = nullptr;
      NumIterations = nullptr;
      PrevLB = nullptr;
      PrevUB = nullptr;
      DistInc = nullptr;
      PrevEUB = nullptr;
      Counters.resize(Size);
      PrivateCounters.resize(Size);
      Inits.resize(Size);
      Updates.resize(Size);
      Finals.resize(Size);
      for (unsigned i = 0; i < Size; ++i) {
        Counters[i] = nullptr;
        PrivateCounters[i] = nullptr;
        Inits[i] = nullptr;
        Updates[i] = nullptr;
        Finals[i] = nullptr;
      }
      PreInits = nullptr;
      DistCombinedFields.LB = nullptr;
      DistCombinedFields.UB = nullptr;
      DistCombinedFields.EUB = nullptr;
      DistCombinedFields.Init = nullptr;
      DistCombinedFields.Cond = nullptr;
      DistCombinedFields.NLB = nullptr;
      DistCombinedFields.NUB = nullptr;
      DistCombinedFields.DistCond = nullptr;
      DistCombinedFields.ParForInDistCond = nullptr;
    }
  };

  /// Get number of collapsed loops.
  unsigned getCollapsedNumber() const { return CollapsedNum; }

  Expr *getIterationVariable() const {
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), IterationVariableOffset)));
  }
  Expr *getLastIteration() const {
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), LastIterationOffset)));
  }
  Expr *getCalcLastIteration() const {
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), CalcLastIterationOffset)));
  }
  Expr *getPreCond() const {
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), PreConditionOffset)));
  }
  Expr *getCond() const {
    return const_cast<Expr *>(
        reinterpret_cast<const Expr *>(*std::next(child_begin(), CondOffset)));
  }
  Expr *getInit() const {
    return const_cast<Expr *>(
        reinterpret_cast<const Expr *>(*std::next(child_begin(), InitOffset)));
  }
  Expr *getInc() const {
    return const_cast<Expr *>(
        reinterpret_cast<const Expr *>(*std::next(child_begin(), IncOffset)));
  }
  const Stmt *getPreInits() const {
    return *std::next(child_begin(), PreInitsOffset);
  }
  Stmt *getPreInits() { return *std::next(child_begin(), PreInitsOffset); }
  Expr *getIsLastIterVariable() const {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), IsLastIterVariableOffset)));
  }
  Expr *getLowerBoundVariable() const {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), LowerBoundVariableOffset)));
  }
  Expr *getUpperBoundVariable() const {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), UpperBoundVariableOffset)));
  }
  Expr *getStrideVariable() const {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), StrideVariableOffset)));
  }
  Expr *getEnsureUpperBound() const {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), EnsureUpperBoundOffset)));
  }
  Expr *getNextLowerBound() const {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), NextLowerBoundOffset)));
  }
  Expr *getNextUpperBound() const {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), NextUpperBoundOffset)));
  }
  Expr *getNumIterations() const {
    assert((isOpenMPWorksharingDirective(getDirectiveKind()) ||
            isOpenMPTaskLoopDirective(getDirectiveKind()) ||
            isOpenMPDistributeDirective(getDirectiveKind())) &&
           "expected worksharing loop directive");
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), NumIterationsOffset)));
  }
  Expr *getPrevLowerBoundVariable() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), PrevLowerBoundVariableOffset)));
  }
  Expr *getPrevUpperBoundVariable() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), PrevUpperBoundVariableOffset)));
  }
  Expr *getDistInc() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), DistIncOffset)));
  }
  Expr *getPrevEnsureUpperBound() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), PrevEnsureUpperBoundOffset)));
  }
  Expr *getCombinedLowerBoundVariable() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), CombinedLowerBoundVariableOffset)));
  }
  Expr *getCombinedUpperBoundVariable() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), CombinedUpperBoundVariableOffset)));
  }
  Expr *getCombinedEnsureUpperBound() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), CombinedEnsureUpperBoundOffset)));
  }
  Expr *getCombinedInit() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), CombinedInitOffset)));
  }
  Expr *getCombinedCond() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), CombinedConditionOffset)));
  }
  Expr *getCombinedNextLowerBound() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), CombinedNextLowerBoundOffset)));
  }
  Expr *getCombinedNextUpperBound() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound sharing directive");
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), CombinedNextUpperBoundOffset)));
  }
  Expr *getCombinedDistCond() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound distribute sharing directive");
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), CombinedDistConditionOffset)));
  }
  Expr *getCombinedParForInDistCond() const {
    assert(isOpenMPLoopBoundSharingDirective(getDirectiveKind()) &&
           "expected loop bound distribute sharing directive");
    return const_cast<Expr *>(reinterpret_cast<const Expr *>(
        *std::next(child_begin(), CombinedParForInDistConditionOffset)));
  }
  const Stmt *getBody() const {
    // This relies on the loop form is already checked by Sema.
    const Stmt *Body =
        getInnermostCapturedStmt()->getCapturedStmt()->IgnoreContainers();
    Body = cast<ForStmt>(Body)->getBody();
    for (unsigned Cnt = 1; Cnt < CollapsedNum; ++Cnt) {
      Body = Body->IgnoreContainers();
      Body = cast<ForStmt>(Body)->getBody();
    }
    return Body;
  }

  ArrayRef<Expr *> counters() { return getCounters(); }

  ArrayRef<Expr *> counters() const {
    return const_cast<OMPLoopDirective *>(this)->getCounters();
  }

  ArrayRef<Expr *> private_counters() { return getPrivateCounters(); }

  ArrayRef<Expr *> private_counters() const {
    return const_cast<OMPLoopDirective *>(this)->getPrivateCounters();
  }

  ArrayRef<Expr *> inits() { return getInits(); }

  ArrayRef<Expr *> inits() const {
    return const_cast<OMPLoopDirective *>(this)->getInits();
  }

  ArrayRef<Expr *> updates() { return getUpdates(); }

  ArrayRef<Expr *> updates() const {
    return const_cast<OMPLoopDirective *>(this)->getUpdates();
  }

  ArrayRef<Expr *> finals() { return getFinals(); }

  ArrayRef<Expr *> finals() const {
    return const_cast<OMPLoopDirective *>(this)->getFinals();
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPSimdDirectiveClass ||
           T->getStmtClass() == OMPForDirectiveClass ||
           T->getStmtClass() == OMPForSimdDirectiveClass ||
           T->getStmtClass() == OMPParallelForDirectiveClass ||
           T->getStmtClass() == OMPParallelForSimdDirectiveClass ||
           T->getStmtClass() == OMPTaskLoopDirectiveClass ||
           T->getStmtClass() == OMPTaskLoopSimdDirectiveClass ||
           T->getStmtClass() == OMPDistributeDirectiveClass ||
           T->getStmtClass() == OMPTargetParallelForDirectiveClass ||
           T->getStmtClass() == OMPDistributeParallelForDirectiveClass ||
           T->getStmtClass() == OMPDistributeParallelForSimdDirectiveClass ||
           T->getStmtClass() == OMPDistributeSimdDirectiveClass ||
           T->getStmtClass() == OMPTargetParallelForSimdDirectiveClass ||
           T->getStmtClass() == OMPTargetSimdDirectiveClass ||
           T->getStmtClass() == OMPTeamsDistributeDirectiveClass ||
           T->getStmtClass() == OMPTeamsDistributeSimdDirectiveClass ||
           T->getStmtClass() ==
               OMPTeamsDistributeParallelForSimdDirectiveClass ||
           T->getStmtClass() == OMPTeamsDistributeParallelForDirectiveClass ||
           T->getStmtClass() ==
               OMPTargetTeamsDistributeParallelForDirectiveClass ||
           T->getStmtClass() ==
               OMPTargetTeamsDistributeParallelForSimdDirectiveClass ||
           T->getStmtClass() == OMPTargetTeamsDistributeDirectiveClass ||
           T->getStmtClass() == OMPTargetTeamsDistributeSimdDirectiveClass;
  }
};

/// This represents '#pragma omp simd' directive.
///
/// \code
/// #pragma omp simd private(a,b) linear(i,j:s) reduction(+:c,d)
/// \endcode
/// In this example directive '#pragma omp simd' has clauses 'private'
/// with the variables 'a' and 'b', 'linear' with variables 'i', 'j' and
/// linear step 's', 'reduction' with operator '+' and variables 'c' and 'd'.
///
class OMPSimdDirective : public OMPLoopDirective {
  friend class ASTStmtReader;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPSimdDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                   unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPSimdDirectiveClass, OMPD_simd, StartLoc,
                         EndLoc, CollapsedNum, NumClauses) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPSimdDirective(unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPSimdDirectiveClass, OMPD_simd,
                         SourceLocation(), SourceLocation(), CollapsedNum,
                         NumClauses) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPSimdDirective *Create(const ASTContext &C, SourceLocation StartLoc,
                                  SourceLocation EndLoc, unsigned CollapsedNum,
                                  ArrayRef<OMPClause *> Clauses,
                                  Stmt *AssociatedStmt,
                                  const HelperExprs &Exprs);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPSimdDirective *CreateEmpty(const ASTContext &C, unsigned NumClauses,
                                       unsigned CollapsedNum, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPSimdDirectiveClass;
  }
};

/// This represents '#pragma omp for' directive.
///
/// \code
/// #pragma omp for private(a,b) reduction(+:c,d)
/// \endcode
/// In this example directive '#pragma omp for' has clauses 'private' with the
/// variables 'a' and 'b' and 'reduction' with operator '+' and variables 'c'
/// and 'd'.
///
class OMPForDirective : public OMPLoopDirective {
  friend class ASTStmtReader;

  /// true if current directive has inner cancel directive.
  bool HasCancel;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPForDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                  unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPForDirectiveClass, OMPD_for, StartLoc, EndLoc,
                         CollapsedNum, NumClauses),
        HasCancel(false) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPForDirective(unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPForDirectiveClass, OMPD_for, SourceLocation(),
                         SourceLocation(), CollapsedNum, NumClauses),
        HasCancel(false) {}

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  /// \param HasCancel true if current directive has inner cancel directive.
  ///
  static OMPForDirective *Create(const ASTContext &C, SourceLocation StartLoc,
                                 SourceLocation EndLoc, unsigned CollapsedNum,
                                 ArrayRef<OMPClause *> Clauses,
                                 Stmt *AssociatedStmt, const HelperExprs &Exprs,
                                 bool HasCancel);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPForDirective *CreateEmpty(const ASTContext &C, unsigned NumClauses,
                                      unsigned CollapsedNum, EmptyShell);

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPForDirectiveClass;
  }
};

/// This represents '#pragma omp for simd' directive.
///
/// \code
/// #pragma omp for simd private(a,b) linear(i,j:s) reduction(+:c,d)
/// \endcode
/// In this example directive '#pragma omp for simd' has clauses 'private'
/// with the variables 'a' and 'b', 'linear' with variables 'i', 'j' and
/// linear step 's', 'reduction' with operator '+' and variables 'c' and 'd'.
///
class OMPForSimdDirective : public OMPLoopDirective {
  friend class ASTStmtReader;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPForSimdDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                      unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPForSimdDirectiveClass, OMPD_for_simd,
                         StartLoc, EndLoc, CollapsedNum, NumClauses) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPForSimdDirective(unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPForSimdDirectiveClass, OMPD_for_simd,
                         SourceLocation(), SourceLocation(), CollapsedNum,
                         NumClauses) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPForSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPForSimdDirective *CreateEmpty(const ASTContext &C,
                                          unsigned NumClauses,
                                          unsigned CollapsedNum, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPForSimdDirectiveClass;
  }
};

/// This represents '#pragma omp sections' directive.
///
/// \code
/// #pragma omp sections private(a,b) reduction(+:c,d)
/// \endcode
/// In this example directive '#pragma omp sections' has clauses 'private' with
/// the variables 'a' and 'b' and 'reduction' with operator '+' and variables
/// 'c' and 'd'.
///
class OMPSectionsDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;

  /// true if current directive has inner cancel directive.
  bool HasCancel;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param NumClauses Number of clauses.
  ///
  OMPSectionsDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                       unsigned NumClauses)
      : OMPExecutableDirective(this, OMPSectionsDirectiveClass, OMPD_sections,
                               StartLoc, EndLoc, NumClauses, 1),
        HasCancel(false) {}

  /// Build an empty directive.
  ///
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPSectionsDirective(unsigned NumClauses)
      : OMPExecutableDirective(this, OMPSectionsDirectiveClass, OMPD_sections,
                               SourceLocation(), SourceLocation(), NumClauses,
                               1),
        HasCancel(false) {}

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param HasCancel true if current directive has inner directive.
  ///
  static OMPSectionsDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt, bool HasCancel);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPSectionsDirective *CreateEmpty(const ASTContext &C,
                                           unsigned NumClauses, EmptyShell);

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPSectionsDirectiveClass;
  }
};

/// This represents '#pragma omp section' directive.
///
/// \code
/// #pragma omp section
/// \endcode
///
class OMPSectionDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;

  /// true if current directive has inner cancel directive.
  bool HasCancel;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPSectionDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(this, OMPSectionDirectiveClass, OMPD_section,
                               StartLoc, EndLoc, 0, 1),
        HasCancel(false) {}

  /// Build an empty directive.
  ///
  explicit OMPSectionDirective()
      : OMPExecutableDirective(this, OMPSectionDirectiveClass, OMPD_section,
                               SourceLocation(), SourceLocation(), 0, 1),
        HasCancel(false) {}

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param HasCancel true if current directive has inner directive.
  ///
  static OMPSectionDirective *Create(const ASTContext &C,
                                     SourceLocation StartLoc,
                                     SourceLocation EndLoc,
                                     Stmt *AssociatedStmt, bool HasCancel);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  ///
  static OMPSectionDirective *CreateEmpty(const ASTContext &C, EmptyShell);

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPSectionDirectiveClass;
  }
};

/// This represents '#pragma omp single' directive.
///
/// \code
/// #pragma omp single private(a,b) copyprivate(c,d)
/// \endcode
/// In this example directive '#pragma omp single' has clauses 'private' with
/// the variables 'a' and 'b' and 'copyprivate' with variables 'c' and 'd'.
///
class OMPSingleDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param NumClauses Number of clauses.
  ///
  OMPSingleDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                     unsigned NumClauses)
      : OMPExecutableDirective(this, OMPSingleDirectiveClass, OMPD_single,
                               StartLoc, EndLoc, NumClauses, 1) {}

  /// Build an empty directive.
  ///
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPSingleDirective(unsigned NumClauses)
      : OMPExecutableDirective(this, OMPSingleDirectiveClass, OMPD_single,
                               SourceLocation(), SourceLocation(), NumClauses,
                               1) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPSingleDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPSingleDirective *CreateEmpty(const ASTContext &C,
                                         unsigned NumClauses, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPSingleDirectiveClass;
  }
};

/// This represents '#pragma omp master' directive.
///
/// \code
/// #pragma omp master
/// \endcode
///
class OMPMasterDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPMasterDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(this, OMPMasterDirectiveClass, OMPD_master,
                               StartLoc, EndLoc, 0, 1) {}

  /// Build an empty directive.
  ///
  explicit OMPMasterDirective()
      : OMPExecutableDirective(this, OMPMasterDirectiveClass, OMPD_master,
                               SourceLocation(), SourceLocation(), 0, 1) {}

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPMasterDirective *Create(const ASTContext &C,
                                    SourceLocation StartLoc,
                                    SourceLocation EndLoc,
                                    Stmt *AssociatedStmt);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  ///
  static OMPMasterDirective *CreateEmpty(const ASTContext &C, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPMasterDirectiveClass;
  }
};

/// This represents '#pragma omp critical' directive.
///
/// \code
/// #pragma omp critical
/// \endcode
///
class OMPCriticalDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  /// Name of the directive.
  DeclarationNameInfo DirName;
  /// Build directive with the given start and end location.
  ///
  /// \param Name Name of the directive.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param NumClauses Number of clauses.
  ///
  OMPCriticalDirective(const DeclarationNameInfo &Name, SourceLocation StartLoc,
                       SourceLocation EndLoc, unsigned NumClauses)
      : OMPExecutableDirective(this, OMPCriticalDirectiveClass, OMPD_critical,
                               StartLoc, EndLoc, NumClauses, 1),
        DirName(Name) {}

  /// Build an empty directive.
  ///
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPCriticalDirective(unsigned NumClauses)
      : OMPExecutableDirective(this, OMPCriticalDirectiveClass, OMPD_critical,
                               SourceLocation(), SourceLocation(), NumClauses,
                               1),
        DirName() {}

  /// Set name of the directive.
  ///
  /// \param Name Name of the directive.
  ///
  void setDirectiveName(const DeclarationNameInfo &Name) { DirName = Name; }

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param Name Name of the directive.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPCriticalDirective *
  Create(const ASTContext &C, const DeclarationNameInfo &Name,
         SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPCriticalDirective *CreateEmpty(const ASTContext &C,
                                           unsigned NumClauses, EmptyShell);

  /// Return name of the directive.
  ///
  DeclarationNameInfo getDirectiveName() const { return DirName; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPCriticalDirectiveClass;
  }
};

/// This represents '#pragma omp parallel for' directive.
///
/// \code
/// #pragma omp parallel for private(a,b) reduction(+:c,d)
/// \endcode
/// In this example directive '#pragma omp parallel for' has clauses 'private'
/// with the variables 'a' and 'b' and 'reduction' with operator '+' and
/// variables 'c' and 'd'.
///
class OMPParallelForDirective : public OMPLoopDirective {
  friend class ASTStmtReader;

  /// true if current region has inner cancel directive.
  bool HasCancel;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPParallelForDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                          unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPParallelForDirectiveClass, OMPD_parallel_for,
                         StartLoc, EndLoc, CollapsedNum, NumClauses),
        HasCancel(false) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPParallelForDirective(unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPParallelForDirectiveClass, OMPD_parallel_for,
                         SourceLocation(), SourceLocation(), CollapsedNum,
                         NumClauses),
        HasCancel(false) {}

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  /// \param HasCancel true if current directive has inner cancel directive.
  ///
  static OMPParallelForDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs, bool HasCancel);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPParallelForDirective *CreateEmpty(const ASTContext &C,
                                              unsigned NumClauses,
                                              unsigned CollapsedNum,
                                              EmptyShell);

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPParallelForDirectiveClass;
  }
};

/// This represents '#pragma omp parallel for simd' directive.
///
/// \code
/// #pragma omp parallel for simd private(a,b) linear(i,j:s) reduction(+:c,d)
/// \endcode
/// In this example directive '#pragma omp parallel for simd' has clauses
/// 'private' with the variables 'a' and 'b', 'linear' with variables 'i', 'j'
/// and linear step 's', 'reduction' with operator '+' and variables 'c' and
/// 'd'.
///
class OMPParallelForSimdDirective : public OMPLoopDirective {
  friend class ASTStmtReader;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPParallelForSimdDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                              unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPParallelForSimdDirectiveClass,
                         OMPD_parallel_for_simd, StartLoc, EndLoc, CollapsedNum,
                         NumClauses) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPParallelForSimdDirective(unsigned CollapsedNum,
                                       unsigned NumClauses)
      : OMPLoopDirective(this, OMPParallelForSimdDirectiveClass,
                         OMPD_parallel_for_simd, SourceLocation(),
                         SourceLocation(), CollapsedNum, NumClauses) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPParallelForSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPParallelForSimdDirective *CreateEmpty(const ASTContext &C,
                                                  unsigned NumClauses,
                                                  unsigned CollapsedNum,
                                                  EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPParallelForSimdDirectiveClass;
  }
};

/// This represents '#pragma omp parallel sections' directive.
///
/// \code
/// #pragma omp parallel sections private(a,b) reduction(+:c,d)
/// \endcode
/// In this example directive '#pragma omp parallel sections' has clauses
/// 'private' with the variables 'a' and 'b' and 'reduction' with operator '+'
/// and variables 'c' and 'd'.
///
class OMPParallelSectionsDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;

  /// true if current directive has inner cancel directive.
  bool HasCancel;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param NumClauses Number of clauses.
  ///
  OMPParallelSectionsDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                               unsigned NumClauses)
      : OMPExecutableDirective(this, OMPParallelSectionsDirectiveClass,
                               OMPD_parallel_sections, StartLoc, EndLoc,
                               NumClauses, 1),
        HasCancel(false) {}

  /// Build an empty directive.
  ///
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPParallelSectionsDirective(unsigned NumClauses)
      : OMPExecutableDirective(this, OMPParallelSectionsDirectiveClass,
                               OMPD_parallel_sections, SourceLocation(),
                               SourceLocation(), NumClauses, 1),
        HasCancel(false) {}

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param HasCancel true if current directive has inner cancel directive.
  ///
  static OMPParallelSectionsDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt, bool HasCancel);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPParallelSectionsDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, EmptyShell);

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPParallelSectionsDirectiveClass;
  }
};

/// This represents '#pragma omp task' directive.
///
/// \code
/// #pragma omp task private(a,b) final(d)
/// \endcode
/// In this example directive '#pragma omp task' has clauses 'private' with the
/// variables 'a' and 'b' and 'final' with condition 'd'.
///
class OMPTaskDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  /// true if this directive has inner cancel directive.
  bool HasCancel;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param NumClauses Number of clauses.
  ///
  OMPTaskDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                   unsigned NumClauses)
      : OMPExecutableDirective(this, OMPTaskDirectiveClass, OMPD_task, StartLoc,
                               EndLoc, NumClauses, 1),
        HasCancel(false) {}

  /// Build an empty directive.
  ///
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTaskDirective(unsigned NumClauses)
      : OMPExecutableDirective(this, OMPTaskDirectiveClass, OMPD_task,
                               SourceLocation(), SourceLocation(), NumClauses,
                               1),
        HasCancel(false) {}

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param HasCancel true, if current directive has inner cancel directive.
  ///
  static OMPTaskDirective *Create(const ASTContext &C, SourceLocation StartLoc,
                                  SourceLocation EndLoc,
                                  ArrayRef<OMPClause *> Clauses,
                                  Stmt *AssociatedStmt, bool HasCancel);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTaskDirective *CreateEmpty(const ASTContext &C, unsigned NumClauses,
                                       EmptyShell);

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTaskDirectiveClass;
  }
};

/// This represents '#pragma omp taskyield' directive.
///
/// \code
/// #pragma omp taskyield
/// \endcode
///
class OMPTaskyieldDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPTaskyieldDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(this, OMPTaskyieldDirectiveClass, OMPD_taskyield,
                               StartLoc, EndLoc, 0, 0) {}

  /// Build an empty directive.
  ///
  explicit OMPTaskyieldDirective()
      : OMPExecutableDirective(this, OMPTaskyieldDirectiveClass, OMPD_taskyield,
                               SourceLocation(), SourceLocation(), 0, 0) {}

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  ///
  static OMPTaskyieldDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  ///
  static OMPTaskyieldDirective *CreateEmpty(const ASTContext &C, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTaskyieldDirectiveClass;
  }
};

/// This represents '#pragma omp barrier' directive.
///
/// \code
/// #pragma omp barrier
/// \endcode
///
class OMPBarrierDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPBarrierDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(this, OMPBarrierDirectiveClass, OMPD_barrier,
                               StartLoc, EndLoc, 0, 0) {}

  /// Build an empty directive.
  ///
  explicit OMPBarrierDirective()
      : OMPExecutableDirective(this, OMPBarrierDirectiveClass, OMPD_barrier,
                               SourceLocation(), SourceLocation(), 0, 0) {}

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  ///
  static OMPBarrierDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  ///
  static OMPBarrierDirective *CreateEmpty(const ASTContext &C, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPBarrierDirectiveClass;
  }
};

/// This represents '#pragma omp taskwait' directive.
///
/// \code
/// #pragma omp taskwait
/// \endcode
///
class OMPTaskwaitDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPTaskwaitDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(this, OMPTaskwaitDirectiveClass, OMPD_taskwait,
                               StartLoc, EndLoc, 0, 0) {}

  /// Build an empty directive.
  ///
  explicit OMPTaskwaitDirective()
      : OMPExecutableDirective(this, OMPTaskwaitDirectiveClass, OMPD_taskwait,
                               SourceLocation(), SourceLocation(), 0, 0) {}

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  ///
  static OMPTaskwaitDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  ///
  static OMPTaskwaitDirective *CreateEmpty(const ASTContext &C, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTaskwaitDirectiveClass;
  }
};

/// This represents '#pragma omp taskgroup' directive.
///
/// \code
/// #pragma omp taskgroup
/// \endcode
///
class OMPTaskgroupDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param NumClauses Number of clauses.
  ///
  OMPTaskgroupDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                        unsigned NumClauses)
      : OMPExecutableDirective(this, OMPTaskgroupDirectiveClass, OMPD_taskgroup,
                               StartLoc, EndLoc, NumClauses, 2) {}

  /// Build an empty directive.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTaskgroupDirective(unsigned NumClauses)
      : OMPExecutableDirective(this, OMPTaskgroupDirectiveClass, OMPD_taskgroup,
                               SourceLocation(), SourceLocation(), NumClauses,
                               2) {}

  /// Sets the task_reduction return variable.
  void setReductionRef(Expr *RR) {
    *std::next(child_begin(), 1) = RR;
  }

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param ReductionRef Reference to the task_reduction return variable.
  ///
  static OMPTaskgroupDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt,
         Expr *ReductionRef);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTaskgroupDirective *CreateEmpty(const ASTContext &C,
                                            unsigned NumClauses, EmptyShell);


  /// Returns reference to the task_reduction return variable.
  const Expr *getReductionRef() const {
    return static_cast<const Expr *>(*std::next(child_begin(), 1));
  }
  Expr *getReductionRef() {
    return static_cast<Expr *>(*std::next(child_begin(), 1));
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTaskgroupDirectiveClass;
  }
};

/// This represents '#pragma omp flush' directive.
///
/// \code
/// #pragma omp flush(a,b)
/// \endcode
/// In this example directive '#pragma omp flush' has 2 arguments- variables 'a'
/// and 'b'.
/// 'omp flush' directive does not have clauses but have an optional list of
/// variables to flush. This list of variables is stored within some fake clause
/// FlushClause.
class OMPFlushDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param NumClauses Number of clauses.
  ///
  OMPFlushDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                    unsigned NumClauses)
      : OMPExecutableDirective(this, OMPFlushDirectiveClass, OMPD_flush,
                               StartLoc, EndLoc, NumClauses, 0) {}

  /// Build an empty directive.
  ///
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPFlushDirective(unsigned NumClauses)
      : OMPExecutableDirective(this, OMPFlushDirectiveClass, OMPD_flush,
                               SourceLocation(), SourceLocation(), NumClauses,
                               0) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses (only single OMPFlushClause clause is
  /// allowed).
  ///
  static OMPFlushDirective *Create(const ASTContext &C, SourceLocation StartLoc,
                                   SourceLocation EndLoc,
                                   ArrayRef<OMPClause *> Clauses);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPFlushDirective *CreateEmpty(const ASTContext &C,
                                        unsigned NumClauses, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPFlushDirectiveClass;
  }
};

/// This represents '#pragma omp ordered' directive.
///
/// \code
/// #pragma omp ordered
/// \endcode
///
class OMPOrderedDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param NumClauses Number of clauses.
  ///
  OMPOrderedDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                      unsigned NumClauses)
      : OMPExecutableDirective(this, OMPOrderedDirectiveClass, OMPD_ordered,
                               StartLoc, EndLoc, NumClauses, 1) {}

  /// Build an empty directive.
  ///
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPOrderedDirective(unsigned NumClauses)
      : OMPExecutableDirective(this, OMPOrderedDirectiveClass, OMPD_ordered,
                               SourceLocation(), SourceLocation(), NumClauses,
                               1) {}

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPOrderedDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPOrderedDirective *CreateEmpty(const ASTContext &C,
                                          unsigned NumClauses, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPOrderedDirectiveClass;
  }
};

/// This represents '#pragma omp atomic' directive.
///
/// \code
/// #pragma omp atomic capture
/// \endcode
/// In this example directive '#pragma omp atomic' has clause 'capture'.
///
class OMPAtomicDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  /// Used for 'atomic update' or 'atomic capture' constructs. They may
  /// have atomic expressions of forms
  /// \code
  /// x = x binop expr;
  /// x = expr binop x;
  /// \endcode
  /// This field is true for the first form of the expression and false for the
  /// second. Required for correct codegen of non-associative operations (like
  /// << or >>).
  bool IsXLHSInRHSPart;
  /// Used for 'atomic update' or 'atomic capture' constructs. They may
  /// have atomic expressions of forms
  /// \code
  /// v = x; <update x>;
  /// <update x>; v = x;
  /// \endcode
  /// This field is true for the first(postfix) form of the expression and false
  /// otherwise.
  bool IsPostfixUpdate;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param NumClauses Number of clauses.
  ///
  OMPAtomicDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                     unsigned NumClauses)
      : OMPExecutableDirective(this, OMPAtomicDirectiveClass, OMPD_atomic,
                               StartLoc, EndLoc, NumClauses, 5),
        IsXLHSInRHSPart(false), IsPostfixUpdate(false) {}

  /// Build an empty directive.
  ///
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPAtomicDirective(unsigned NumClauses)
      : OMPExecutableDirective(this, OMPAtomicDirectiveClass, OMPD_atomic,
                               SourceLocation(), SourceLocation(), NumClauses,
                               5),
        IsXLHSInRHSPart(false), IsPostfixUpdate(false) {}

  /// Set 'x' part of the associated expression/statement.
  void setX(Expr *X) { *std::next(child_begin()) = X; }
  /// Set helper expression of the form
  /// 'OpaqueValueExpr(x) binop OpaqueValueExpr(expr)' or
  /// 'OpaqueValueExpr(expr) binop OpaqueValueExpr(x)'.
  void setUpdateExpr(Expr *UE) { *std::next(child_begin(), 2) = UE; }
  /// Set 'v' part of the associated expression/statement.
  void setV(Expr *V) { *std::next(child_begin(), 3) = V; }
  /// Set 'expr' part of the associated expression/statement.
  void setExpr(Expr *E) { *std::next(child_begin(), 4) = E; }

public:
  /// Creates directive with a list of \a Clauses and 'x', 'v' and 'expr'
  /// parts of the atomic construct (see Section 2.12.6, atomic Construct, for
  /// detailed description of 'x', 'v' and 'expr').
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param X 'x' part of the associated expression/statement.
  /// \param V 'v' part of the associated expression/statement.
  /// \param E 'expr' part of the associated expression/statement.
  /// \param UE Helper expression of the form
  /// 'OpaqueValueExpr(x) binop OpaqueValueExpr(expr)' or
  /// 'OpaqueValueExpr(expr) binop OpaqueValueExpr(x)'.
  /// \param IsXLHSInRHSPart true if \a UE has the first form and false if the
  /// second.
  /// \param IsPostfixUpdate true if original value of 'x' must be stored in
  /// 'v', not an updated one.
  static OMPAtomicDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt, Expr *X, Expr *V,
         Expr *E, Expr *UE, bool IsXLHSInRHSPart, bool IsPostfixUpdate);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPAtomicDirective *CreateEmpty(const ASTContext &C,
                                         unsigned NumClauses, EmptyShell);

  /// Get 'x' part of the associated expression/statement.
  Expr *getX() { return cast_or_null<Expr>(*std::next(child_begin())); }
  const Expr *getX() const {
    return cast_or_null<Expr>(*std::next(child_begin()));
  }
  /// Get helper expression of the form
  /// 'OpaqueValueExpr(x) binop OpaqueValueExpr(expr)' or
  /// 'OpaqueValueExpr(expr) binop OpaqueValueExpr(x)'.
  Expr *getUpdateExpr() {
    return cast_or_null<Expr>(*std::next(child_begin(), 2));
  }
  const Expr *getUpdateExpr() const {
    return cast_or_null<Expr>(*std::next(child_begin(), 2));
  }
  /// Return true if helper update expression has form
  /// 'OpaqueValueExpr(x) binop OpaqueValueExpr(expr)' and false if it has form
  /// 'OpaqueValueExpr(expr) binop OpaqueValueExpr(x)'.
  bool isXLHSInRHSPart() const { return IsXLHSInRHSPart; }
  /// Return true if 'v' expression must be updated to original value of
  /// 'x', false if 'v' must be updated to the new value of 'x'.
  bool isPostfixUpdate() const { return IsPostfixUpdate; }
  /// Get 'v' part of the associated expression/statement.
  Expr *getV() { return cast_or_null<Expr>(*std::next(child_begin(), 3)); }
  const Expr *getV() const {
    return cast_or_null<Expr>(*std::next(child_begin(), 3));
  }
  /// Get 'expr' part of the associated expression/statement.
  Expr *getExpr() { return cast_or_null<Expr>(*std::next(child_begin(), 4)); }
  const Expr *getExpr() const {
    return cast_or_null<Expr>(*std::next(child_begin(), 4));
  }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPAtomicDirectiveClass;
  }
};

/// This represents '#pragma omp target' directive.
///
/// \code
/// #pragma omp target if(a)
/// \endcode
/// In this example directive '#pragma omp target' has clause 'if' with
/// condition 'a'.
///
class OMPTargetDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param NumClauses Number of clauses.
  ///
  OMPTargetDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                     unsigned NumClauses)
      : OMPExecutableDirective(this, OMPTargetDirectiveClass, OMPD_target,
                               StartLoc, EndLoc, NumClauses, 1) {}

  /// Build an empty directive.
  ///
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTargetDirective(unsigned NumClauses)
      : OMPExecutableDirective(this, OMPTargetDirectiveClass, OMPD_target,
                               SourceLocation(), SourceLocation(), NumClauses,
                               1) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPTargetDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetDirective *CreateEmpty(const ASTContext &C,
                                         unsigned NumClauses, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetDirectiveClass;
  }
};

/// This represents '#pragma omp target data' directive.
///
/// \code
/// #pragma omp target data device(0) if(a) map(b[:])
/// \endcode
/// In this example directive '#pragma omp target data' has clauses 'device'
/// with the value '0', 'if' with condition 'a' and 'map' with array
/// section 'b[:]'.
///
class OMPTargetDataDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param NumClauses The number of clauses.
  ///
  OMPTargetDataDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                         unsigned NumClauses)
      : OMPExecutableDirective(this, OMPTargetDataDirectiveClass,
                               OMPD_target_data, StartLoc, EndLoc, NumClauses,
                               1) {}

  /// Build an empty directive.
  ///
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTargetDataDirective(unsigned NumClauses)
      : OMPExecutableDirective(this, OMPTargetDataDirectiveClass,
                               OMPD_target_data, SourceLocation(),
                               SourceLocation(), NumClauses, 1) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPTargetDataDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt);

  /// Creates an empty directive with the place for \a N clauses.
  ///
  /// \param C AST context.
  /// \param N The number of clauses.
  ///
  static OMPTargetDataDirective *CreateEmpty(const ASTContext &C, unsigned N,
                                             EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetDataDirectiveClass;
  }
};

/// This represents '#pragma omp target enter data' directive.
///
/// \code
/// #pragma omp target enter data device(0) if(a) map(b[:])
/// \endcode
/// In this example directive '#pragma omp target enter data' has clauses
/// 'device' with the value '0', 'if' with condition 'a' and 'map' with array
/// section 'b[:]'.
///
class OMPTargetEnterDataDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param NumClauses The number of clauses.
  ///
  OMPTargetEnterDataDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                              unsigned NumClauses)
      : OMPExecutableDirective(this, OMPTargetEnterDataDirectiveClass,
                               OMPD_target_enter_data, StartLoc, EndLoc,
                               NumClauses, /*NumChildren=*/1) {}

  /// Build an empty directive.
  ///
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTargetEnterDataDirective(unsigned NumClauses)
      : OMPExecutableDirective(this, OMPTargetEnterDataDirectiveClass,
                               OMPD_target_enter_data, SourceLocation(),
                               SourceLocation(), NumClauses,
                               /*NumChildren=*/1) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPTargetEnterDataDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt);

  /// Creates an empty directive with the place for \a N clauses.
  ///
  /// \param C AST context.
  /// \param N The number of clauses.
  ///
  static OMPTargetEnterDataDirective *CreateEmpty(const ASTContext &C,
                                                  unsigned N, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetEnterDataDirectiveClass;
  }
};

/// This represents '#pragma omp target exit data' directive.
///
/// \code
/// #pragma omp target exit data device(0) if(a) map(b[:])
/// \endcode
/// In this example directive '#pragma omp target exit data' has clauses
/// 'device' with the value '0', 'if' with condition 'a' and 'map' with array
/// section 'b[:]'.
///
class OMPTargetExitDataDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param NumClauses The number of clauses.
  ///
  OMPTargetExitDataDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                             unsigned NumClauses)
      : OMPExecutableDirective(this, OMPTargetExitDataDirectiveClass,
                               OMPD_target_exit_data, StartLoc, EndLoc,
                               NumClauses, /*NumChildren=*/1) {}

  /// Build an empty directive.
  ///
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTargetExitDataDirective(unsigned NumClauses)
      : OMPExecutableDirective(this, OMPTargetExitDataDirectiveClass,
                               OMPD_target_exit_data, SourceLocation(),
                               SourceLocation(), NumClauses,
                               /*NumChildren=*/1) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPTargetExitDataDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt);

  /// Creates an empty directive with the place for \a N clauses.
  ///
  /// \param C AST context.
  /// \param N The number of clauses.
  ///
  static OMPTargetExitDataDirective *CreateEmpty(const ASTContext &C,
                                                 unsigned N, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetExitDataDirectiveClass;
  }
};

/// This represents '#pragma omp target parallel' directive.
///
/// \code
/// #pragma omp target parallel if(a)
/// \endcode
/// In this example directive '#pragma omp target parallel' has clause 'if' with
/// condition 'a'.
///
class OMPTargetParallelDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param NumClauses Number of clauses.
  ///
  OMPTargetParallelDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                             unsigned NumClauses)
      : OMPExecutableDirective(this, OMPTargetParallelDirectiveClass,
                               OMPD_target_parallel, StartLoc, EndLoc,
                               NumClauses, /*NumChildren=*/1) {}

  /// Build an empty directive.
  ///
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTargetParallelDirective(unsigned NumClauses)
      : OMPExecutableDirective(this, OMPTargetParallelDirectiveClass,
                               OMPD_target_parallel, SourceLocation(),
                               SourceLocation(), NumClauses,
                               /*NumChildren=*/1) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPTargetParallelDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetParallelDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetParallelDirectiveClass;
  }
};

/// This represents '#pragma omp target parallel for' directive.
///
/// \code
/// #pragma omp target parallel for private(a,b) reduction(+:c,d)
/// \endcode
/// In this example directive '#pragma omp target parallel for' has clauses
/// 'private' with the variables 'a' and 'b' and 'reduction' with operator '+'
/// and variables 'c' and 'd'.
///
class OMPTargetParallelForDirective : public OMPLoopDirective {
  friend class ASTStmtReader;

  /// true if current region has inner cancel directive.
  bool HasCancel;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPTargetParallelForDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                                unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPTargetParallelForDirectiveClass,
                         OMPD_target_parallel_for, StartLoc, EndLoc,
                         CollapsedNum, NumClauses),
        HasCancel(false) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTargetParallelForDirective(unsigned CollapsedNum,
                                         unsigned NumClauses)
      : OMPLoopDirective(this, OMPTargetParallelForDirectiveClass,
                         OMPD_target_parallel_for, SourceLocation(),
                         SourceLocation(), CollapsedNum, NumClauses),
        HasCancel(false) {}

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  /// \param HasCancel true if current directive has inner cancel directive.
  ///
  static OMPTargetParallelForDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs, bool HasCancel);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetParallelForDirective *CreateEmpty(const ASTContext &C,
                                                    unsigned NumClauses,
                                                    unsigned CollapsedNum,
                                                    EmptyShell);

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetParallelForDirectiveClass;
  }
};

/// This represents '#pragma omp teams' directive.
///
/// \code
/// #pragma omp teams if(a)
/// \endcode
/// In this example directive '#pragma omp teams' has clause 'if' with
/// condition 'a'.
///
class OMPTeamsDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param NumClauses Number of clauses.
  ///
  OMPTeamsDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                    unsigned NumClauses)
      : OMPExecutableDirective(this, OMPTeamsDirectiveClass, OMPD_teams,
                               StartLoc, EndLoc, NumClauses, 1) {}

  /// Build an empty directive.
  ///
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTeamsDirective(unsigned NumClauses)
      : OMPExecutableDirective(this, OMPTeamsDirectiveClass, OMPD_teams,
                               SourceLocation(), SourceLocation(), NumClauses,
                               1) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPTeamsDirective *Create(const ASTContext &C, SourceLocation StartLoc,
                                   SourceLocation EndLoc,
                                   ArrayRef<OMPClause *> Clauses,
                                   Stmt *AssociatedStmt);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTeamsDirective *CreateEmpty(const ASTContext &C,
                                        unsigned NumClauses, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTeamsDirectiveClass;
  }
};

/// This represents '#pragma omp cancellation point' directive.
///
/// \code
/// #pragma omp cancellation point for
/// \endcode
///
/// In this example a cancellation point is created for innermost 'for' region.
class OMPCancellationPointDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  OpenMPDirectiveKind CancelRegion;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  ///
  OMPCancellationPointDirective(SourceLocation StartLoc, SourceLocation EndLoc)
      : OMPExecutableDirective(this, OMPCancellationPointDirectiveClass,
                               OMPD_cancellation_point, StartLoc, EndLoc, 0, 0),
        CancelRegion(OMPD_unknown) {}

  /// Build an empty directive.
  ///
  explicit OMPCancellationPointDirective()
      : OMPExecutableDirective(this, OMPCancellationPointDirectiveClass,
                               OMPD_cancellation_point, SourceLocation(),
                               SourceLocation(), 0, 0),
        CancelRegion(OMPD_unknown) {}

  /// Set cancel region for current cancellation point.
  /// \param CR Cancellation region.
  void setCancelRegion(OpenMPDirectiveKind CR) { CancelRegion = CR; }

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  ///
  static OMPCancellationPointDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         OpenMPDirectiveKind CancelRegion);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  ///
  static OMPCancellationPointDirective *CreateEmpty(const ASTContext &C,
                                                    EmptyShell);

  /// Get cancellation region for the current cancellation point.
  OpenMPDirectiveKind getCancelRegion() const { return CancelRegion; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPCancellationPointDirectiveClass;
  }
};

/// This represents '#pragma omp cancel' directive.
///
/// \code
/// #pragma omp cancel for
/// \endcode
///
/// In this example a cancel is created for innermost 'for' region.
class OMPCancelDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  OpenMPDirectiveKind CancelRegion;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param NumClauses Number of clauses.
  ///
  OMPCancelDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                     unsigned NumClauses)
      : OMPExecutableDirective(this, OMPCancelDirectiveClass, OMPD_cancel,
                               StartLoc, EndLoc, NumClauses, 0),
        CancelRegion(OMPD_unknown) {}

  /// Build an empty directive.
  ///
  /// \param NumClauses Number of clauses.
  explicit OMPCancelDirective(unsigned NumClauses)
      : OMPExecutableDirective(this, OMPCancelDirectiveClass, OMPD_cancel,
                               SourceLocation(), SourceLocation(), NumClauses,
                               0),
        CancelRegion(OMPD_unknown) {}

  /// Set cancel region for current cancellation point.
  /// \param CR Cancellation region.
  void setCancelRegion(OpenMPDirectiveKind CR) { CancelRegion = CR; }

public:
  /// Creates directive.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  ///
  static OMPCancelDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, OpenMPDirectiveKind CancelRegion);

  /// Creates an empty directive.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPCancelDirective *CreateEmpty(const ASTContext &C,
                                         unsigned NumClauses, EmptyShell);

  /// Get cancellation region for the current cancellation point.
  OpenMPDirectiveKind getCancelRegion() const { return CancelRegion; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPCancelDirectiveClass;
  }
};

/// This represents '#pragma omp taskloop' directive.
///
/// \code
/// #pragma omp taskloop private(a,b) grainsize(val) num_tasks(num)
/// \endcode
/// In this example directive '#pragma omp taskloop' has clauses 'private'
/// with the variables 'a' and 'b', 'grainsize' with expression 'val' and
/// 'num_tasks' with expression 'num'.
///
class OMPTaskLoopDirective : public OMPLoopDirective {
  friend class ASTStmtReader;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPTaskLoopDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                       unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPTaskLoopDirectiveClass, OMPD_taskloop,
                         StartLoc, EndLoc, CollapsedNum, NumClauses) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTaskLoopDirective(unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPTaskLoopDirectiveClass, OMPD_taskloop,
                         SourceLocation(), SourceLocation(), CollapsedNum,
                         NumClauses) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTaskLoopDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTaskLoopDirective *CreateEmpty(const ASTContext &C,
                                           unsigned NumClauses,
                                           unsigned CollapsedNum, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTaskLoopDirectiveClass;
  }
};

/// This represents '#pragma omp taskloop simd' directive.
///
/// \code
/// #pragma omp taskloop simd private(a,b) grainsize(val) num_tasks(num)
/// \endcode
/// In this example directive '#pragma omp taskloop simd' has clauses 'private'
/// with the variables 'a' and 'b', 'grainsize' with expression 'val' and
/// 'num_tasks' with expression 'num'.
///
class OMPTaskLoopSimdDirective : public OMPLoopDirective {
  friend class ASTStmtReader;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPTaskLoopSimdDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                           unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPTaskLoopSimdDirectiveClass,
                         OMPD_taskloop_simd, StartLoc, EndLoc, CollapsedNum,
                         NumClauses) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTaskLoopSimdDirective(unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPTaskLoopSimdDirectiveClass,
                         OMPD_taskloop_simd, SourceLocation(), SourceLocation(),
                         CollapsedNum, NumClauses) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTaskLoopSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTaskLoopSimdDirective *CreateEmpty(const ASTContext &C,
                                               unsigned NumClauses,
                                               unsigned CollapsedNum,
                                               EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTaskLoopSimdDirectiveClass;
  }
};

/// This represents '#pragma omp distribute' directive.
///
/// \code
/// #pragma omp distribute private(a,b)
/// \endcode
/// In this example directive '#pragma omp distribute' has clauses 'private'
/// with the variables 'a' and 'b'
///
class OMPDistributeDirective : public OMPLoopDirective {
  friend class ASTStmtReader;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPDistributeDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                         unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPDistributeDirectiveClass, OMPD_distribute,
                         StartLoc, EndLoc, CollapsedNum, NumClauses)
        {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPDistributeDirective(unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPDistributeDirectiveClass, OMPD_distribute,
                         SourceLocation(), SourceLocation(), CollapsedNum,
                         NumClauses)
        {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPDistributeDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPDistributeDirective *CreateEmpty(const ASTContext &C,
                                             unsigned NumClauses,
                                             unsigned CollapsedNum, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPDistributeDirectiveClass;
  }
};

/// This represents '#pragma omp target update' directive.
///
/// \code
/// #pragma omp target update to(a) from(b) device(1)
/// \endcode
/// In this example directive '#pragma omp target update' has clause 'to' with
/// argument 'a', clause 'from' with argument 'b' and clause 'device' with
/// argument '1'.
///
class OMPTargetUpdateDirective : public OMPExecutableDirective {
  friend class ASTStmtReader;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param NumClauses The number of clauses.
  ///
  OMPTargetUpdateDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                           unsigned NumClauses)
      : OMPExecutableDirective(this, OMPTargetUpdateDirectiveClass,
                               OMPD_target_update, StartLoc, EndLoc, NumClauses,
                               1) {}

  /// Build an empty directive.
  ///
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTargetUpdateDirective(unsigned NumClauses)
      : OMPExecutableDirective(this, OMPTargetUpdateDirectiveClass,
                               OMPD_target_update, SourceLocation(),
                               SourceLocation(), NumClauses, 1) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPTargetUpdateDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         ArrayRef<OMPClause *> Clauses, Stmt *AssociatedStmt);

  /// Creates an empty directive with the place for \a NumClauses
  /// clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses The number of clauses.
  ///
  static OMPTargetUpdateDirective *CreateEmpty(const ASTContext &C,
                                               unsigned NumClauses, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetUpdateDirectiveClass;
  }
};

/// This represents '#pragma omp distribute parallel for' composite
///  directive.
///
/// \code
/// #pragma omp distribute parallel for private(a,b)
/// \endcode
/// In this example directive '#pragma omp distribute parallel for' has clause
/// 'private' with the variables 'a' and 'b'
///
class OMPDistributeParallelForDirective : public OMPLoopDirective {
  friend class ASTStmtReader;
  /// true if the construct has inner cancel directive.
  bool HasCancel = false;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPDistributeParallelForDirective(SourceLocation StartLoc,
                                    SourceLocation EndLoc,
                                    unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPDistributeParallelForDirectiveClass,
                         OMPD_distribute_parallel_for, StartLoc, EndLoc,
                         CollapsedNum, NumClauses), HasCancel(false) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPDistributeParallelForDirective(unsigned CollapsedNum,
                                             unsigned NumClauses)
      : OMPLoopDirective(this, OMPDistributeParallelForDirectiveClass,
                         OMPD_distribute_parallel_for, SourceLocation(),
                         SourceLocation(), CollapsedNum, NumClauses),
        HasCancel(false) {}

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  /// \param HasCancel true if this directive has inner cancel directive.
  ///
  static OMPDistributeParallelForDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs, bool HasCancel);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPDistributeParallelForDirective *CreateEmpty(const ASTContext &C,
                                                        unsigned NumClauses,
                                                        unsigned CollapsedNum,
                                                        EmptyShell);

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPDistributeParallelForDirectiveClass;
  }
};

/// This represents '#pragma omp distribute parallel for simd' composite
/// directive.
///
/// \code
/// #pragma omp distribute parallel for simd private(x)
/// \endcode
/// In this example directive '#pragma omp distribute parallel for simd' has
/// clause 'private' with the variables 'x'
///
class OMPDistributeParallelForSimdDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPDistributeParallelForSimdDirective(SourceLocation StartLoc,
                                        SourceLocation EndLoc,
                                        unsigned CollapsedNum,
                                        unsigned NumClauses)
      : OMPLoopDirective(this, OMPDistributeParallelForSimdDirectiveClass,
                         OMPD_distribute_parallel_for_simd, StartLoc,
                         EndLoc, CollapsedNum, NumClauses) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPDistributeParallelForSimdDirective(unsigned CollapsedNum,
                                                 unsigned NumClauses)
      : OMPLoopDirective(this, OMPDistributeParallelForSimdDirectiveClass,
                         OMPD_distribute_parallel_for_simd,
                         SourceLocation(), SourceLocation(), CollapsedNum,
                         NumClauses) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPDistributeParallelForSimdDirective *Create(
      const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
      unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
      Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPDistributeParallelForSimdDirective *CreateEmpty(
      const ASTContext &C, unsigned NumClauses, unsigned CollapsedNum,
      EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPDistributeParallelForSimdDirectiveClass;
  }
};

/// This represents '#pragma omp distribute simd' composite directive.
///
/// \code
/// #pragma omp distribute simd private(x)
/// \endcode
/// In this example directive '#pragma omp distribute simd' has clause
/// 'private' with the variables 'x'
///
class OMPDistributeSimdDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPDistributeSimdDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                             unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPDistributeSimdDirectiveClass,
                         OMPD_distribute_simd, StartLoc, EndLoc, CollapsedNum,
                         NumClauses) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPDistributeSimdDirective(unsigned CollapsedNum,
                                      unsigned NumClauses)
      : OMPLoopDirective(this, OMPDistributeSimdDirectiveClass,
                         OMPD_distribute_simd, SourceLocation(),
                         SourceLocation(), CollapsedNum, NumClauses) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPDistributeSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPDistributeSimdDirective *CreateEmpty(const ASTContext &C,
                                                 unsigned NumClauses,
                                                 unsigned CollapsedNum,
                                                 EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPDistributeSimdDirectiveClass;
  }
};

/// This represents '#pragma omp target parallel for simd' directive.
///
/// \code
/// #pragma omp target parallel for simd private(a) map(b) safelen(c)
/// \endcode
/// In this example directive '#pragma omp target parallel for simd' has clauses
/// 'private' with the variable 'a', 'map' with the variable 'b' and 'safelen'
/// with the variable 'c'.
///
class OMPTargetParallelForSimdDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPTargetParallelForSimdDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                                unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPTargetParallelForSimdDirectiveClass,
                         OMPD_target_parallel_for_simd, StartLoc, EndLoc,
                         CollapsedNum, NumClauses) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTargetParallelForSimdDirective(unsigned CollapsedNum,
                                             unsigned NumClauses)
      : OMPLoopDirective(this, OMPTargetParallelForSimdDirectiveClass,
                         OMPD_target_parallel_for_simd, SourceLocation(),
                         SourceLocation(), CollapsedNum, NumClauses) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTargetParallelForSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetParallelForSimdDirective *CreateEmpty(const ASTContext &C,
                                                        unsigned NumClauses,
                                                        unsigned CollapsedNum,
                                                        EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetParallelForSimdDirectiveClass;
  }
};

/// This represents '#pragma omp target simd' directive.
///
/// \code
/// #pragma omp target simd private(a) map(b) safelen(c)
/// \endcode
/// In this example directive '#pragma omp target simd' has clauses 'private'
/// with the variable 'a', 'map' with the variable 'b' and 'safelen' with
/// the variable 'c'.
///
class OMPTargetSimdDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPTargetSimdDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                         unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPTargetSimdDirectiveClass,
                         OMPD_target_simd, StartLoc, EndLoc, CollapsedNum,
                         NumClauses) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTargetSimdDirective(unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPTargetSimdDirectiveClass, OMPD_target_simd,
                         SourceLocation(),SourceLocation(), CollapsedNum,
                         NumClauses) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTargetSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetSimdDirective *CreateEmpty(const ASTContext &C,
                                             unsigned NumClauses,
                                             unsigned CollapsedNum,
                                             EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetSimdDirectiveClass;
  }
};

/// This represents '#pragma omp teams distribute' directive.
///
/// \code
/// #pragma omp teams distribute private(a,b)
/// \endcode
/// In this example directive '#pragma omp teams distribute' has clauses
/// 'private' with the variables 'a' and 'b'
///
class OMPTeamsDistributeDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPTeamsDistributeDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                              unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPTeamsDistributeDirectiveClass,
                         OMPD_teams_distribute, StartLoc, EndLoc,
                         CollapsedNum, NumClauses) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTeamsDistributeDirective(unsigned CollapsedNum,
                                       unsigned NumClauses)
      : OMPLoopDirective(this, OMPTeamsDistributeDirectiveClass,
                         OMPD_teams_distribute, SourceLocation(),
                         SourceLocation(), CollapsedNum, NumClauses) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTeamsDistributeDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTeamsDistributeDirective *CreateEmpty(const ASTContext &C,
                                                  unsigned NumClauses,
                                                  unsigned CollapsedNum,
                                                  EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTeamsDistributeDirectiveClass;
  }
};

/// This represents '#pragma omp teams distribute simd'
/// combined directive.
///
/// \code
/// #pragma omp teams distribute simd private(a,b)
/// \endcode
/// In this example directive '#pragma omp teams distribute simd'
/// has clause 'private' with the variables 'a' and 'b'
///
class OMPTeamsDistributeSimdDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPTeamsDistributeSimdDirective(SourceLocation StartLoc,
                                  SourceLocation EndLoc, unsigned CollapsedNum,
                                  unsigned NumClauses)
      : OMPLoopDirective(this, OMPTeamsDistributeSimdDirectiveClass,
                         OMPD_teams_distribute_simd, StartLoc, EndLoc,
                         CollapsedNum, NumClauses) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTeamsDistributeSimdDirective(unsigned CollapsedNum,
                                           unsigned NumClauses)
      : OMPLoopDirective(this, OMPTeamsDistributeSimdDirectiveClass,
                         OMPD_teams_distribute_simd, SourceLocation(),
                         SourceLocation(), CollapsedNum, NumClauses) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTeamsDistributeSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place
  /// for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTeamsDistributeSimdDirective *CreateEmpty(const ASTContext &C,
                                                      unsigned NumClauses,
                                                      unsigned CollapsedNum,
                                                      EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTeamsDistributeSimdDirectiveClass;
  }
};

/// This represents '#pragma omp teams distribute parallel for simd' composite
/// directive.
///
/// \code
/// #pragma omp teams distribute parallel for simd private(x)
/// \endcode
/// In this example directive '#pragma omp teams distribute parallel for simd'
/// has clause 'private' with the variables 'x'
///
class OMPTeamsDistributeParallelForSimdDirective final
    : public OMPLoopDirective {
  friend class ASTStmtReader;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPTeamsDistributeParallelForSimdDirective(SourceLocation StartLoc,
                                             SourceLocation EndLoc,
                                             unsigned CollapsedNum,
                                             unsigned NumClauses)
      : OMPLoopDirective(this, OMPTeamsDistributeParallelForSimdDirectiveClass,
                         OMPD_teams_distribute_parallel_for_simd, StartLoc,
                         EndLoc, CollapsedNum, NumClauses) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTeamsDistributeParallelForSimdDirective(unsigned CollapsedNum,
                                                      unsigned NumClauses)
      : OMPLoopDirective(this, OMPTeamsDistributeParallelForSimdDirectiveClass,
                         OMPD_teams_distribute_parallel_for_simd,
                         SourceLocation(), SourceLocation(), CollapsedNum,
                         NumClauses) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTeamsDistributeParallelForSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTeamsDistributeParallelForSimdDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, unsigned CollapsedNum,
              EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTeamsDistributeParallelForSimdDirectiveClass;
  }
};

/// This represents '#pragma omp teams distribute parallel for' composite
/// directive.
///
/// \code
/// #pragma omp teams distribute parallel for private(x)
/// \endcode
/// In this example directive '#pragma omp teams distribute parallel for'
/// has clause 'private' with the variables 'x'
///
class OMPTeamsDistributeParallelForDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;
  /// true if the construct has inner cancel directive.
  bool HasCancel = false;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPTeamsDistributeParallelForDirective(SourceLocation StartLoc,
                                         SourceLocation EndLoc,
                                         unsigned CollapsedNum,
                                         unsigned NumClauses)
      : OMPLoopDirective(this, OMPTeamsDistributeParallelForDirectiveClass,
                         OMPD_teams_distribute_parallel_for, StartLoc, EndLoc,
                         CollapsedNum, NumClauses), HasCancel(false) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTeamsDistributeParallelForDirective(unsigned CollapsedNum,
                                                  unsigned NumClauses)
      : OMPLoopDirective(this, OMPTeamsDistributeParallelForDirectiveClass,
                         OMPD_teams_distribute_parallel_for, SourceLocation(),
                         SourceLocation(), CollapsedNum, NumClauses),
        HasCancel(false) {}

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  /// \param HasCancel true if this directive has inner cancel directive.
  ///
  static OMPTeamsDistributeParallelForDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs, bool HasCancel);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTeamsDistributeParallelForDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, unsigned CollapsedNum,
              EmptyShell);

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTeamsDistributeParallelForDirectiveClass;
  }
};

/// This represents '#pragma omp target teams' directive.
///
/// \code
/// #pragma omp target teams if(a>0)
/// \endcode
/// In this example directive '#pragma omp target teams' has clause 'if' with
/// condition 'a>0'.
///
class OMPTargetTeamsDirective final : public OMPExecutableDirective {
  friend class ASTStmtReader;
  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param NumClauses Number of clauses.
  ///
  OMPTargetTeamsDirective(SourceLocation StartLoc, SourceLocation EndLoc,
                          unsigned NumClauses)
      : OMPExecutableDirective(this, OMPTargetTeamsDirectiveClass,
                               OMPD_target_teams, StartLoc, EndLoc, NumClauses,
                               1) {}

  /// Build an empty directive.
  ///
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTargetTeamsDirective(unsigned NumClauses)
      : OMPExecutableDirective(this, OMPTargetTeamsDirectiveClass,
                               OMPD_target_teams, SourceLocation(),
                               SourceLocation(), NumClauses, 1) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  ///
  static OMPTargetTeamsDirective *Create(const ASTContext &C,
                                         SourceLocation StartLoc,
                                         SourceLocation EndLoc,
                                         ArrayRef<OMPClause *> Clauses,
                                         Stmt *AssociatedStmt);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetTeamsDirective *CreateEmpty(const ASTContext &C,
                                              unsigned NumClauses, EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetTeamsDirectiveClass;
  }
};

/// This represents '#pragma omp target teams distribute' combined directive.
///
/// \code
/// #pragma omp target teams distribute private(x)
/// \endcode
/// In this example directive '#pragma omp target teams distribute' has clause
/// 'private' with the variables 'x'
///
class OMPTargetTeamsDistributeDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPTargetTeamsDistributeDirective(SourceLocation StartLoc,
                                    SourceLocation EndLoc,
                                    unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(this, OMPTargetTeamsDistributeDirectiveClass,
                         OMPD_target_teams_distribute, StartLoc, EndLoc,
                         CollapsedNum, NumClauses) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTargetTeamsDistributeDirective(unsigned CollapsedNum,
                                             unsigned NumClauses)
      : OMPLoopDirective(this, OMPTargetTeamsDistributeDirectiveClass,
                         OMPD_target_teams_distribute, SourceLocation(),
                         SourceLocation(), CollapsedNum, NumClauses) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTargetTeamsDistributeDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetTeamsDistributeDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, unsigned CollapsedNum,
              EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetTeamsDistributeDirectiveClass;
  }
};

/// This represents '#pragma omp target teams distribute parallel for' combined
/// directive.
///
/// \code
/// #pragma omp target teams distribute parallel for private(x)
/// \endcode
/// In this example directive '#pragma omp target teams distribute parallel
/// for' has clause 'private' with the variables 'x'
///
class OMPTargetTeamsDistributeParallelForDirective final
    : public OMPLoopDirective {
  friend class ASTStmtReader;
  /// true if the construct has inner cancel directive.
  bool HasCancel = false;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPTargetTeamsDistributeParallelForDirective(SourceLocation StartLoc,
                                               SourceLocation EndLoc,
                                               unsigned CollapsedNum,
                                               unsigned NumClauses)
      : OMPLoopDirective(this,
                         OMPTargetTeamsDistributeParallelForDirectiveClass,
                         OMPD_target_teams_distribute_parallel_for, StartLoc,
                         EndLoc, CollapsedNum, NumClauses),
        HasCancel(false) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTargetTeamsDistributeParallelForDirective(unsigned CollapsedNum,
                                                        unsigned NumClauses)
      : OMPLoopDirective(
            this, OMPTargetTeamsDistributeParallelForDirectiveClass,
            OMPD_target_teams_distribute_parallel_for, SourceLocation(),
            SourceLocation(), CollapsedNum, NumClauses),
        HasCancel(false) {}

  /// Set cancel state.
  void setHasCancel(bool Has) { HasCancel = Has; }

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  /// \param HasCancel true if this directive has inner cancel directive.
  ///
  static OMPTargetTeamsDistributeParallelForDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs, bool HasCancel);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetTeamsDistributeParallelForDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, unsigned CollapsedNum,
              EmptyShell);

  /// Return true if current directive has inner cancel directive.
  bool hasCancel() const { return HasCancel; }

  static bool classof(const Stmt *T) {
    return T->getStmtClass() ==
           OMPTargetTeamsDistributeParallelForDirectiveClass;
  }
};

/// This represents '#pragma omp target teams distribute parallel for simd'
/// combined directive.
///
/// \code
/// #pragma omp target teams distribute parallel for simd private(x)
/// \endcode
/// In this example directive '#pragma omp target teams distribute parallel
/// for simd' has clause 'private' with the variables 'x'
///
class OMPTargetTeamsDistributeParallelForSimdDirective final
    : public OMPLoopDirective {
  friend class ASTStmtReader;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPTargetTeamsDistributeParallelForSimdDirective(SourceLocation StartLoc,
                                                   SourceLocation EndLoc,
                                                   unsigned CollapsedNum,
                                                   unsigned NumClauses)
      : OMPLoopDirective(this,
                         OMPTargetTeamsDistributeParallelForSimdDirectiveClass,
                         OMPD_target_teams_distribute_parallel_for_simd,
                         StartLoc, EndLoc, CollapsedNum, NumClauses) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTargetTeamsDistributeParallelForSimdDirective(
      unsigned CollapsedNum, unsigned NumClauses)
      : OMPLoopDirective(
            this, OMPTargetTeamsDistributeParallelForSimdDirectiveClass,
            OMPD_target_teams_distribute_parallel_for_simd, SourceLocation(),
            SourceLocation(), CollapsedNum, NumClauses) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTargetTeamsDistributeParallelForSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetTeamsDistributeParallelForSimdDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, unsigned CollapsedNum,
              EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() ==
           OMPTargetTeamsDistributeParallelForSimdDirectiveClass;
  }
};

/// This represents '#pragma omp target teams distribute simd' combined
/// directive.
///
/// \code
/// #pragma omp target teams distribute simd private(x)
/// \endcode
/// In this example directive '#pragma omp target teams distribute simd'
/// has clause 'private' with the variables 'x'
///
class OMPTargetTeamsDistributeSimdDirective final : public OMPLoopDirective {
  friend class ASTStmtReader;

  /// Build directive with the given start and end location.
  ///
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending location of the directive.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  OMPTargetTeamsDistributeSimdDirective(SourceLocation StartLoc,
                                        SourceLocation EndLoc,
                                        unsigned CollapsedNum,
                                        unsigned NumClauses)
      : OMPLoopDirective(this, OMPTargetTeamsDistributeSimdDirectiveClass,
                         OMPD_target_teams_distribute_simd, StartLoc, EndLoc,
                         CollapsedNum, NumClauses) {}

  /// Build an empty directive.
  ///
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  explicit OMPTargetTeamsDistributeSimdDirective(unsigned CollapsedNum,
                                                 unsigned NumClauses)
      : OMPLoopDirective(this, OMPTargetTeamsDistributeSimdDirectiveClass,
                         OMPD_target_teams_distribute_simd, SourceLocation(),
                         SourceLocation(), CollapsedNum, NumClauses) {}

public:
  /// Creates directive with a list of \a Clauses.
  ///
  /// \param C AST context.
  /// \param StartLoc Starting location of the directive kind.
  /// \param EndLoc Ending Location of the directive.
  /// \param CollapsedNum Number of collapsed loops.
  /// \param Clauses List of clauses.
  /// \param AssociatedStmt Statement, associated with the directive.
  /// \param Exprs Helper expressions for CodeGen.
  ///
  static OMPTargetTeamsDistributeSimdDirective *
  Create(const ASTContext &C, SourceLocation StartLoc, SourceLocation EndLoc,
         unsigned CollapsedNum, ArrayRef<OMPClause *> Clauses,
         Stmt *AssociatedStmt, const HelperExprs &Exprs);

  /// Creates an empty directive with the place for \a NumClauses clauses.
  ///
  /// \param C AST context.
  /// \param CollapsedNum Number of collapsed nested loops.
  /// \param NumClauses Number of clauses.
  ///
  static OMPTargetTeamsDistributeSimdDirective *
  CreateEmpty(const ASTContext &C, unsigned NumClauses, unsigned CollapsedNum,
              EmptyShell);

  static bool classof(const Stmt *T) {
    return T->getStmtClass() == OMPTargetTeamsDistributeSimdDirectiveClass;
  }
};

} // end namespace clang

#endif
