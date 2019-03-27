//===- SymbolManager.h - Management of Symbolic Values ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines SymbolManager, a class that manages symbolic values
//  created for use by ExprEngine and related classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SYMBOLMANAGER_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SYMBOLMANAGER_H

#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Basic/LLVM.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/StoreRef.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymExpr.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/Support/Allocator.h"
#include <cassert>

namespace clang {

class ASTContext;
class Stmt;

namespace ento {

class BasicValueFactory;
class StoreManager;

///A symbol representing the value stored at a MemRegion.
class SymbolRegionValue : public SymbolData {
  const TypedValueRegion *R;

public:
  SymbolRegionValue(SymbolID sym, const TypedValueRegion *r)
      : SymbolData(SymbolRegionValueKind, sym), R(r) {
    assert(r);
    assert(isValidTypeForSymbol(r->getValueType()));
  }

  const TypedValueRegion* getRegion() const { return R; }

  static void Profile(llvm::FoldingSetNodeID& profile, const TypedValueRegion* R) {
    profile.AddInteger((unsigned) SymbolRegionValueKind);
    profile.AddPointer(R);
  }

  void Profile(llvm::FoldingSetNodeID& profile) override {
    Profile(profile, R);
  }

  void dumpToStream(raw_ostream &os) const override;
  const MemRegion *getOriginRegion() const override { return getRegion(); }

  QualType getType() const override;

  // Implement isa<T> support.
  static bool classof(const SymExpr *SE) {
    return SE->getKind() == SymbolRegionValueKind;
  }
};

/// A symbol representing the result of an expression in the case when we do
/// not know anything about what the expression is.
class SymbolConjured : public SymbolData {
  const Stmt *S;
  QualType T;
  unsigned Count;
  const LocationContext *LCtx;
  const void *SymbolTag;

public:
  SymbolConjured(SymbolID sym, const Stmt *s, const LocationContext *lctx,
                 QualType t, unsigned count, const void *symbolTag)
      : SymbolData(SymbolConjuredKind, sym), S(s), T(t), Count(count),
        LCtx(lctx), SymbolTag(symbolTag) {
    // FIXME: 's' might be a nullptr if we're conducting invalidation
    // that was caused by a destructor call on a temporary object,
    // which has no statement associated with it.
    // Due to this, we might be creating the same invalidation symbol for
    // two different invalidation passes (for two different temporaries).
    assert(lctx);
    assert(isValidTypeForSymbol(t));
  }

  const Stmt *getStmt() const { return S; }
  unsigned getCount() const { return Count; }
  const void *getTag() const { return SymbolTag; }

  QualType getType() const override;

  void dumpToStream(raw_ostream &os) const override;

  static void Profile(llvm::FoldingSetNodeID& profile, const Stmt *S,
                      QualType T, unsigned Count, const LocationContext *LCtx,
                      const void *SymbolTag) {
    profile.AddInteger((unsigned) SymbolConjuredKind);
    profile.AddPointer(S);
    profile.AddPointer(LCtx);
    profile.Add(T);
    profile.AddInteger(Count);
    profile.AddPointer(SymbolTag);
  }

  void Profile(llvm::FoldingSetNodeID& profile) override {
    Profile(profile, S, T, Count, LCtx, SymbolTag);
  }

  // Implement isa<T> support.
  static bool classof(const SymExpr *SE) {
    return SE->getKind() == SymbolConjuredKind;
  }
};

/// A symbol representing the value of a MemRegion whose parent region has
/// symbolic value.
class SymbolDerived : public SymbolData {
  SymbolRef parentSymbol;
  const TypedValueRegion *R;

public:
  SymbolDerived(SymbolID sym, SymbolRef parent, const TypedValueRegion *r)
      : SymbolData(SymbolDerivedKind, sym), parentSymbol(parent), R(r) {
    assert(parent);
    assert(r);
    assert(isValidTypeForSymbol(r->getValueType()));
  }

  SymbolRef getParentSymbol() const { return parentSymbol; }
  const TypedValueRegion *getRegion() const { return R; }

  QualType getType() const override;

  void dumpToStream(raw_ostream &os) const override;
  const MemRegion *getOriginRegion() const override { return getRegion(); }

  static void Profile(llvm::FoldingSetNodeID& profile, SymbolRef parent,
                      const TypedValueRegion *r) {
    profile.AddInteger((unsigned) SymbolDerivedKind);
    profile.AddPointer(r);
    profile.AddPointer(parent);
  }

  void Profile(llvm::FoldingSetNodeID& profile) override {
    Profile(profile, parentSymbol, R);
  }

  // Implement isa<T> support.
  static bool classof(const SymExpr *SE) {
    return SE->getKind() == SymbolDerivedKind;
  }
};

/// SymbolExtent - Represents the extent (size in bytes) of a bounded region.
///  Clients should not ask the SymbolManager for a region's extent. Always use
///  SubRegion::getExtent instead -- the value returned may not be a symbol.
class SymbolExtent : public SymbolData {
  const SubRegion *R;

public:
  SymbolExtent(SymbolID sym, const SubRegion *r)
      : SymbolData(SymbolExtentKind, sym), R(r) {
    assert(r);
  }

  const SubRegion *getRegion() const { return R; }

  QualType getType() const override;

  void dumpToStream(raw_ostream &os) const override;

  static void Profile(llvm::FoldingSetNodeID& profile, const SubRegion *R) {
    profile.AddInteger((unsigned) SymbolExtentKind);
    profile.AddPointer(R);
  }

  void Profile(llvm::FoldingSetNodeID& profile) override {
    Profile(profile, R);
  }

  // Implement isa<T> support.
  static bool classof(const SymExpr *SE) {
    return SE->getKind() == SymbolExtentKind;
  }
};

/// SymbolMetadata - Represents path-dependent metadata about a specific region.
///  Metadata symbols remain live as long as they are marked as in use before
///  dead-symbol sweeping AND their associated regions are still alive.
///  Intended for use by checkers.
class SymbolMetadata : public SymbolData {
  const MemRegion* R;
  const Stmt *S;
  QualType T;
  const LocationContext *LCtx;
  unsigned Count;
  const void *Tag;

public:
  SymbolMetadata(SymbolID sym, const MemRegion* r, const Stmt *s, QualType t,
                 const LocationContext *LCtx, unsigned count, const void *tag)
      : SymbolData(SymbolMetadataKind, sym), R(r), S(s), T(t), LCtx(LCtx),
        Count(count), Tag(tag) {
      assert(r);
      assert(s);
      assert(isValidTypeForSymbol(t));
      assert(LCtx);
      assert(tag);
    }

  const MemRegion *getRegion() const { return R; }
  const Stmt *getStmt() const { return S; }
  const LocationContext *getLocationContext() const { return LCtx; }
  unsigned getCount() const { return Count; }
  const void *getTag() const { return Tag; }

  QualType getType() const override;

  void dumpToStream(raw_ostream &os) const override;

  static void Profile(llvm::FoldingSetNodeID& profile, const MemRegion *R,
                      const Stmt *S, QualType T, const LocationContext *LCtx,
                      unsigned Count, const void *Tag) {
    profile.AddInteger((unsigned) SymbolMetadataKind);
    profile.AddPointer(R);
    profile.AddPointer(S);
    profile.Add(T);
    profile.AddPointer(LCtx);
    profile.AddInteger(Count);
    profile.AddPointer(Tag);
  }

  void Profile(llvm::FoldingSetNodeID& profile) override {
    Profile(profile, R, S, T, LCtx, Count, Tag);
  }

  // Implement isa<T> support.
  static bool classof(const SymExpr *SE) {
    return SE->getKind() == SymbolMetadataKind;
  }
};

/// Represents a cast expression.
class SymbolCast : public SymExpr {
  const SymExpr *Operand;

  /// Type of the operand.
  QualType FromTy;

  /// The type of the result.
  QualType ToTy;

public:
  SymbolCast(const SymExpr *In, QualType From, QualType To)
      : SymExpr(SymbolCastKind), Operand(In), FromTy(From), ToTy(To) {
    assert(In);
    assert(isValidTypeForSymbol(From));
    // FIXME: GenericTaintChecker creates symbols of void type.
    // Otherwise, 'To' should also be a valid type.
  }

  unsigned computeComplexity() const override {
    if (Complexity == 0)
      Complexity = 1 + Operand->computeComplexity();
    return Complexity;
  }

  QualType getType() const override { return ToTy; }

  const SymExpr *getOperand() const { return Operand; }

  void dumpToStream(raw_ostream &os) const override;

  static void Profile(llvm::FoldingSetNodeID& ID,
                      const SymExpr *In, QualType From, QualType To) {
    ID.AddInteger((unsigned) SymbolCastKind);
    ID.AddPointer(In);
    ID.Add(From);
    ID.Add(To);
  }

  void Profile(llvm::FoldingSetNodeID& ID) override {
    Profile(ID, Operand, FromTy, ToTy);
  }

  // Implement isa<T> support.
  static bool classof(const SymExpr *SE) {
    return SE->getKind() == SymbolCastKind;
  }
};

/// Represents a symbolic expression involving a binary operator
class BinarySymExpr : public SymExpr {
  BinaryOperator::Opcode Op;
  QualType T;

protected:
  BinarySymExpr(Kind k, BinaryOperator::Opcode op, QualType t)
      : SymExpr(k), Op(op), T(t) {
    assert(classof(this));
    // Binary expressions are results of arithmetic. Pointer arithmetic is not
    // handled by binary expressions, but it is instead handled by applying
    // sub-regions to regions.
    assert(isValidTypeForSymbol(t) && !Loc::isLocType(t));
  }

public:
  // FIXME: We probably need to make this out-of-line to avoid redundant
  // generation of virtual functions.
  QualType getType() const override { return T; }

  BinaryOperator::Opcode getOpcode() const { return Op; }

  // Implement isa<T> support.
  static bool classof(const SymExpr *SE) {
    Kind k = SE->getKind();
    return k >= BEGIN_BINARYSYMEXPRS && k <= END_BINARYSYMEXPRS;
  }
};

/// Represents a symbolic expression like 'x' + 3.
class SymIntExpr : public BinarySymExpr {
  const SymExpr *LHS;
  const llvm::APSInt& RHS;

public:
  SymIntExpr(const SymExpr *lhs, BinaryOperator::Opcode op,
             const llvm::APSInt &rhs, QualType t)
      : BinarySymExpr(SymIntExprKind, op, t), LHS(lhs), RHS(rhs) {
    assert(lhs);
  }

  void dumpToStream(raw_ostream &os) const override;

  const SymExpr *getLHS() const { return LHS; }
  const llvm::APSInt &getRHS() const { return RHS; }

  unsigned computeComplexity() const override {
    if (Complexity == 0)
      Complexity = 1 + LHS->computeComplexity();
    return Complexity;
  }

  static void Profile(llvm::FoldingSetNodeID& ID, const SymExpr *lhs,
                      BinaryOperator::Opcode op, const llvm::APSInt& rhs,
                      QualType t) {
    ID.AddInteger((unsigned) SymIntExprKind);
    ID.AddPointer(lhs);
    ID.AddInteger(op);
    ID.AddPointer(&rhs);
    ID.Add(t);
  }

  void Profile(llvm::FoldingSetNodeID& ID) override {
    Profile(ID, LHS, getOpcode(), RHS, getType());
  }

  // Implement isa<T> support.
  static bool classof(const SymExpr *SE) {
    return SE->getKind() == SymIntExprKind;
  }
};

/// Represents a symbolic expression like 3 - 'x'.
class IntSymExpr : public BinarySymExpr {
  const llvm::APSInt& LHS;
  const SymExpr *RHS;

public:
  IntSymExpr(const llvm::APSInt &lhs, BinaryOperator::Opcode op,
             const SymExpr *rhs, QualType t)
      : BinarySymExpr(IntSymExprKind, op, t), LHS(lhs), RHS(rhs) {
    assert(rhs);
  }

  void dumpToStream(raw_ostream &os) const override;

  const SymExpr *getRHS() const { return RHS; }
  const llvm::APSInt &getLHS() const { return LHS; }

  unsigned computeComplexity() const override {
    if (Complexity == 0)
      Complexity = 1 + RHS->computeComplexity();
    return Complexity;
  }

  static void Profile(llvm::FoldingSetNodeID& ID, const llvm::APSInt& lhs,
                      BinaryOperator::Opcode op, const SymExpr *rhs,
                      QualType t) {
    ID.AddInteger((unsigned) IntSymExprKind);
    ID.AddPointer(&lhs);
    ID.AddInteger(op);
    ID.AddPointer(rhs);
    ID.Add(t);
  }

  void Profile(llvm::FoldingSetNodeID& ID) override {
    Profile(ID, LHS, getOpcode(), RHS, getType());
  }

  // Implement isa<T> support.
  static bool classof(const SymExpr *SE) {
    return SE->getKind() == IntSymExprKind;
  }
};

/// Represents a symbolic expression like 'x' + 'y'.
class SymSymExpr : public BinarySymExpr {
  const SymExpr *LHS;
  const SymExpr *RHS;

public:
  SymSymExpr(const SymExpr *lhs, BinaryOperator::Opcode op, const SymExpr *rhs,
             QualType t)
      : BinarySymExpr(SymSymExprKind, op, t), LHS(lhs), RHS(rhs) {
    assert(lhs);
    assert(rhs);
  }

  const SymExpr *getLHS() const { return LHS; }
  const SymExpr *getRHS() const { return RHS; }

  void dumpToStream(raw_ostream &os) const override;

  unsigned computeComplexity() const override {
    if (Complexity == 0)
      Complexity = RHS->computeComplexity() + LHS->computeComplexity();
    return Complexity;
  }

  static void Profile(llvm::FoldingSetNodeID& ID, const SymExpr *lhs,
                    BinaryOperator::Opcode op, const SymExpr *rhs, QualType t) {
    ID.AddInteger((unsigned) SymSymExprKind);
    ID.AddPointer(lhs);
    ID.AddInteger(op);
    ID.AddPointer(rhs);
    ID.Add(t);
  }

  void Profile(llvm::FoldingSetNodeID& ID) override {
    Profile(ID, LHS, getOpcode(), RHS, getType());
  }

  // Implement isa<T> support.
  static bool classof(const SymExpr *SE) {
    return SE->getKind() == SymSymExprKind;
  }
};

class SymbolManager {
  using DataSetTy = llvm::FoldingSet<SymExpr>;
  using SymbolDependTy = llvm::DenseMap<SymbolRef, SymbolRefSmallVectorTy *>;

  DataSetTy DataSet;

  /// Stores the extra dependencies between symbols: the data should be kept
  /// alive as long as the key is live.
  SymbolDependTy SymbolDependencies;

  unsigned SymbolCounter = 0;
  llvm::BumpPtrAllocator& BPAlloc;
  BasicValueFactory &BV;
  ASTContext &Ctx;

public:
  SymbolManager(ASTContext &ctx, BasicValueFactory &bv,
                llvm::BumpPtrAllocator& bpalloc)
      : SymbolDependencies(16), BPAlloc(bpalloc), BV(bv), Ctx(ctx) {}
  ~SymbolManager();

  static bool canSymbolicate(QualType T);

  /// Make a unique symbol for MemRegion R according to its kind.
  const SymbolRegionValue* getRegionValueSymbol(const TypedValueRegion* R);

  const SymbolConjured* conjureSymbol(const Stmt *E,
                                      const LocationContext *LCtx,
                                      QualType T,
                                      unsigned VisitCount,
                                      const void *SymbolTag = nullptr);

  const SymbolConjured* conjureSymbol(const Expr *E,
                                      const LocationContext *LCtx,
                                      unsigned VisitCount,
                                      const void *SymbolTag = nullptr) {
    return conjureSymbol(E, LCtx, E->getType(), VisitCount, SymbolTag);
  }

  const SymbolDerived *getDerivedSymbol(SymbolRef parentSymbol,
                                        const TypedValueRegion *R);

  const SymbolExtent *getExtentSymbol(const SubRegion *R);

  /// Creates a metadata symbol associated with a specific region.
  ///
  /// VisitCount can be used to differentiate regions corresponding to
  /// different loop iterations, thus, making the symbol path-dependent.
  const SymbolMetadata *getMetadataSymbol(const MemRegion *R, const Stmt *S,
                                          QualType T,
                                          const LocationContext *LCtx,
                                          unsigned VisitCount,
                                          const void *SymbolTag = nullptr);

  const SymbolCast* getCastSymbol(const SymExpr *Operand,
                                  QualType From, QualType To);

  const SymIntExpr *getSymIntExpr(const SymExpr *lhs, BinaryOperator::Opcode op,
                                  const llvm::APSInt& rhs, QualType t);

  const SymIntExpr *getSymIntExpr(const SymExpr &lhs, BinaryOperator::Opcode op,
                                  const llvm::APSInt& rhs, QualType t) {
    return getSymIntExpr(&lhs, op, rhs, t);
  }

  const IntSymExpr *getIntSymExpr(const llvm::APSInt& lhs,
                                  BinaryOperator::Opcode op,
                                  const SymExpr *rhs, QualType t);

  const SymSymExpr *getSymSymExpr(const SymExpr *lhs, BinaryOperator::Opcode op,
                                  const SymExpr *rhs, QualType t);

  QualType getType(const SymExpr *SE) const {
    return SE->getType();
  }

  /// Add artificial symbol dependency.
  ///
  /// The dependent symbol should stay alive as long as the primary is alive.
  void addSymbolDependency(const SymbolRef Primary, const SymbolRef Dependent);

  const SymbolRefSmallVectorTy *getDependentSymbols(const SymbolRef Primary);

  ASTContext &getContext() { return Ctx; }
  BasicValueFactory &getBasicVals() { return BV; }
};

/// A class responsible for cleaning up unused symbols.
class SymbolReaper {
  enum SymbolStatus {
    NotProcessed,
    HaveMarkedDependents
  };

  using SymbolSetTy = llvm::DenseSet<SymbolRef>;
  using SymbolMapTy = llvm::DenseMap<SymbolRef, SymbolStatus>;
  using RegionSetTy = llvm::DenseSet<const MemRegion *>;

  SymbolMapTy TheLiving;
  SymbolSetTy MetadataInUse;

  RegionSetTy RegionRoots;

  const StackFrameContext *LCtx;
  const Stmt *Loc;
  SymbolManager& SymMgr;
  StoreRef reapedStore;
  llvm::DenseMap<const MemRegion *, unsigned> includedRegionCache;

public:
  /// Construct a reaper object, which removes everything which is not
  /// live before we execute statement s in the given location context.
  ///
  /// If the statement is NULL, everything is this and parent contexts is
  /// considered live.
  /// If the stack frame context is NULL, everything on stack is considered
  /// dead.
  SymbolReaper(const StackFrameContext *Ctx, const Stmt *s,
               SymbolManager &symmgr, StoreManager &storeMgr)
      : LCtx(Ctx), Loc(s), SymMgr(symmgr), reapedStore(nullptr, storeMgr) {}

  const LocationContext *getLocationContext() const { return LCtx; }

  bool isLive(SymbolRef sym);
  bool isLiveRegion(const MemRegion *region);
  bool isLive(const Stmt *ExprVal, const LocationContext *LCtx) const;
  bool isLive(const VarRegion *VR, bool includeStoreBindings = false) const;

  /// Unconditionally marks a symbol as live.
  ///
  /// This should never be
  /// used by checkers, only by the state infrastructure such as the store and
  /// environment. Checkers should instead use metadata symbols and markInUse.
  void markLive(SymbolRef sym);

  /// Marks a symbol as important to a checker.
  ///
  /// For metadata symbols,
  /// this will keep the symbol alive as long as its associated region is also
  /// live. For other symbols, this has no effect; checkers are not permitted
  /// to influence the life of other symbols. This should be used before any
  /// symbol marking has occurred, i.e. in the MarkLiveSymbols callback.
  void markInUse(SymbolRef sym);

  using region_iterator = RegionSetTy::const_iterator;

  region_iterator region_begin() const { return RegionRoots.begin(); }
  region_iterator region_end() const { return RegionRoots.end(); }

  /// Returns whether or not a symbol has been confirmed dead.
  ///
  /// This should only be called once all marking of dead symbols has completed.
  /// (For checkers, this means only in the checkDeadSymbols callback.)
  bool isDead(SymbolRef sym) {
    return !isLive(sym);
  }

  void markLive(const MemRegion *region);
  void markElementIndicesLive(const MemRegion *region);

  /// Set to the value of the symbolic store after
  /// StoreManager::removeDeadBindings has been called.
  void setReapedStore(StoreRef st) { reapedStore = st; }

private:
  /// Mark the symbols dependent on the input symbol as live.
  void markDependentsLive(SymbolRef sym);
};

class SymbolVisitor {
protected:
  ~SymbolVisitor() = default;

public:
  SymbolVisitor() = default;
  SymbolVisitor(const SymbolVisitor &) = default;
  SymbolVisitor(SymbolVisitor &&) {}

  /// A visitor method invoked by ProgramStateManager::scanReachableSymbols.
  ///
  /// The method returns \c true if symbols should continue be scanned and \c
  /// false otherwise.
  virtual bool VisitSymbol(SymbolRef sym) = 0;
  virtual bool VisitMemRegion(const MemRegion *) { return true; }
};

} // namespace ento

} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SYMBOLMANAGER_H
