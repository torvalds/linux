//===- CFG.h - Classes for representing and building CFGs -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the CFG and CFGBuilder classes for representing and
//  building Control-Flow Graphs (CFGs) from ASTs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_CFG_H
#define LLVM_CLANG_ANALYSIS_CFG_H

#include "clang/Analysis/Support/BumpVector.h"
#include "clang/Analysis/ConstructionContext.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/raw_ostream.h"
#include <bitset>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <vector>

namespace clang {

class ASTContext;
class BinaryOperator;
class CFG;
class CXXBaseSpecifier;
class CXXBindTemporaryExpr;
class CXXCtorInitializer;
class CXXDeleteExpr;
class CXXDestructorDecl;
class CXXNewExpr;
class CXXRecordDecl;
class Decl;
class FieldDecl;
class LangOptions;
class VarDecl;

/// Represents a top-level expression in a basic block.
class CFGElement {
public:
  enum Kind {
    // main kind
    Initializer,
    ScopeBegin,
    ScopeEnd,
    NewAllocator,
    LifetimeEnds,
    LoopExit,
    // stmt kind
    Statement,
    Constructor,
    CXXRecordTypedCall,
    STMT_BEGIN = Statement,
    STMT_END = CXXRecordTypedCall,
    // dtor kind
    AutomaticObjectDtor,
    DeleteDtor,
    BaseDtor,
    MemberDtor,
    TemporaryDtor,
    DTOR_BEGIN = AutomaticObjectDtor,
    DTOR_END = TemporaryDtor
  };

protected:
  // The int bits are used to mark the kind.
  llvm::PointerIntPair<void *, 2> Data1;
  llvm::PointerIntPair<void *, 2> Data2;

  CFGElement(Kind kind, const void *Ptr1, const void *Ptr2 = nullptr)
      : Data1(const_cast<void*>(Ptr1), ((unsigned) kind) & 0x3),
        Data2(const_cast<void*>(Ptr2), (((unsigned) kind) >> 2) & 0x3) {
    assert(getKind() == kind);
  }

  CFGElement() = default;

public:
  /// Convert to the specified CFGElement type, asserting that this
  /// CFGElement is of the desired type.
  template<typename T>
  T castAs() const {
    assert(T::isKind(*this));
    T t;
    CFGElement& e = t;
    e = *this;
    return t;
  }

  /// Convert to the specified CFGElement type, returning None if this
  /// CFGElement is not of the desired type.
  template<typename T>
  Optional<T> getAs() const {
    if (!T::isKind(*this))
      return None;
    T t;
    CFGElement& e = t;
    e = *this;
    return t;
  }

  Kind getKind() const {
    unsigned x = Data2.getInt();
    x <<= 2;
    x |= Data1.getInt();
    return (Kind) x;
  }
};

class CFGStmt : public CFGElement {
public:
  explicit CFGStmt(Stmt *S, Kind K = Statement) : CFGElement(K, S) {
    assert(isKind(*this));
  }

  const Stmt *getStmt() const {
    return static_cast<const Stmt *>(Data1.getPointer());
  }

private:
  friend class CFGElement;

  static bool isKind(const CFGElement &E) {
    return E.getKind() >= STMT_BEGIN && E.getKind() <= STMT_END;
  }

protected:
  CFGStmt() = default;
};

/// Represents C++ constructor call. Maintains information necessary to figure
/// out what memory is being initialized by the constructor expression. For now
/// this is only used by the analyzer's CFG.
class CFGConstructor : public CFGStmt {
public:
  explicit CFGConstructor(CXXConstructExpr *CE, const ConstructionContext *C)
      : CFGStmt(CE, Constructor) {
    assert(C);
    Data2.setPointer(const_cast<ConstructionContext *>(C));
  }

  const ConstructionContext *getConstructionContext() const {
    return static_cast<ConstructionContext *>(Data2.getPointer());
  }

private:
  friend class CFGElement;

  CFGConstructor() = default;

  static bool isKind(const CFGElement &E) {
    return E.getKind() == Constructor;
  }
};

/// Represents a function call that returns a C++ object by value. This, like
/// constructor, requires a construction context in order to understand the
/// storage of the returned object . In C such tracking is not necessary because
/// no additional effort is required for destroying the object or modeling copy
/// elision. Like CFGConstructor, this element is for now only used by the
/// analyzer's CFG.
class CFGCXXRecordTypedCall : public CFGStmt {
public:
  /// Returns true when call expression \p CE needs to be represented
  /// by CFGCXXRecordTypedCall, as opposed to a regular CFGStmt.
  static bool isCXXRecordTypedCall(Expr *E) {
    assert(isa<CallExpr>(E) || isa<ObjCMessageExpr>(E));
    // There is no such thing as reference-type expression. If the function
    // returns a reference, it'll return the respective lvalue or xvalue
    // instead, and we're only interested in objects.
    return !E->isGLValue() &&
           E->getType().getCanonicalType()->getAsCXXRecordDecl();
  }

  explicit CFGCXXRecordTypedCall(Expr *E, const ConstructionContext *C)
      : CFGStmt(E, CXXRecordTypedCall) {
    assert(isCXXRecordTypedCall(E));
    assert(C && (isa<TemporaryObjectConstructionContext>(C) ||
                 // These are possible in C++17 due to mandatory copy elision.
                 isa<ReturnedValueConstructionContext>(C) ||
                 isa<VariableConstructionContext>(C) ||
                 isa<ConstructorInitializerConstructionContext>(C) ||
                 isa<ArgumentConstructionContext>(C)));
    Data2.setPointer(const_cast<ConstructionContext *>(C));
  }

  const ConstructionContext *getConstructionContext() const {
    return static_cast<ConstructionContext *>(Data2.getPointer());
  }

private:
  friend class CFGElement;

  CFGCXXRecordTypedCall() = default;

  static bool isKind(const CFGElement &E) {
    return E.getKind() == CXXRecordTypedCall;
  }
};

/// Represents C++ base or member initializer from constructor's initialization
/// list.
class CFGInitializer : public CFGElement {
public:
  explicit CFGInitializer(CXXCtorInitializer *initializer)
      : CFGElement(Initializer, initializer) {}

  CXXCtorInitializer* getInitializer() const {
    return static_cast<CXXCtorInitializer*>(Data1.getPointer());
  }

private:
  friend class CFGElement;

  CFGInitializer() = default;

  static bool isKind(const CFGElement &E) {
    return E.getKind() == Initializer;
  }
};

/// Represents C++ allocator call.
class CFGNewAllocator : public CFGElement {
public:
  explicit CFGNewAllocator(const CXXNewExpr *S)
    : CFGElement(NewAllocator, S) {}

  // Get the new expression.
  const CXXNewExpr *getAllocatorExpr() const {
    return static_cast<CXXNewExpr *>(Data1.getPointer());
  }

private:
  friend class CFGElement;

  CFGNewAllocator() = default;

  static bool isKind(const CFGElement &elem) {
    return elem.getKind() == NewAllocator;
  }
};

/// Represents the point where a loop ends.
/// This element is is only produced when building the CFG for the static
/// analyzer and hidden behind the 'cfg-loopexit' analyzer config flag.
///
/// Note: a loop exit element can be reached even when the loop body was never
/// entered.
class CFGLoopExit : public CFGElement {
public:
  explicit CFGLoopExit(const Stmt *stmt) : CFGElement(LoopExit, stmt) {}

  const Stmt *getLoopStmt() const {
    return static_cast<Stmt *>(Data1.getPointer());
  }

private:
  friend class CFGElement;

  CFGLoopExit() = default;

  static bool isKind(const CFGElement &elem) {
    return elem.getKind() == LoopExit;
  }
};

/// Represents the point where the lifetime of an automatic object ends
class CFGLifetimeEnds : public CFGElement {
public:
  explicit CFGLifetimeEnds(const VarDecl *var, const Stmt *stmt)
      : CFGElement(LifetimeEnds, var, stmt) {}

  const VarDecl *getVarDecl() const {
    return static_cast<VarDecl *>(Data1.getPointer());
  }

  const Stmt *getTriggerStmt() const {
    return static_cast<Stmt *>(Data2.getPointer());
  }

private:
  friend class CFGElement;

  CFGLifetimeEnds() = default;

  static bool isKind(const CFGElement &elem) {
    return elem.getKind() == LifetimeEnds;
  }
};

/// Represents beginning of a scope implicitly generated
/// by the compiler on encountering a CompoundStmt
class CFGScopeBegin : public CFGElement {
public:
  CFGScopeBegin() {}
  CFGScopeBegin(const VarDecl *VD, const Stmt *S)
      : CFGElement(ScopeBegin, VD, S) {}

  // Get statement that triggered a new scope.
  const Stmt *getTriggerStmt() const {
    return static_cast<Stmt*>(Data2.getPointer());
  }

  // Get VD that triggered a new scope.
  const VarDecl *getVarDecl() const {
    return static_cast<VarDecl *>(Data1.getPointer());
  }

private:
  friend class CFGElement;
  static bool isKind(const CFGElement &E) {
    Kind kind = E.getKind();
    return kind == ScopeBegin;
  }
};

/// Represents end of a scope implicitly generated by
/// the compiler after the last Stmt in a CompoundStmt's body
class CFGScopeEnd : public CFGElement {
public:
  CFGScopeEnd() {}
  CFGScopeEnd(const VarDecl *VD, const Stmt *S) : CFGElement(ScopeEnd, VD, S) {}

  const VarDecl *getVarDecl() const {
    return static_cast<VarDecl *>(Data1.getPointer());
  }

  const Stmt *getTriggerStmt() const {
    return static_cast<Stmt *>(Data2.getPointer());
  }

private:
  friend class CFGElement;
  static bool isKind(const CFGElement &E) {
    Kind kind = E.getKind();
    return kind == ScopeEnd;
  }
};

/// Represents C++ object destructor implicitly generated by compiler on various
/// occasions.
class CFGImplicitDtor : public CFGElement {
protected:
  CFGImplicitDtor() = default;

  CFGImplicitDtor(Kind kind, const void *data1, const void *data2 = nullptr)
    : CFGElement(kind, data1, data2) {
    assert(kind >= DTOR_BEGIN && kind <= DTOR_END);
  }

public:
  const CXXDestructorDecl *getDestructorDecl(ASTContext &astContext) const;
  bool isNoReturn(ASTContext &astContext) const;

private:
  friend class CFGElement;

  static bool isKind(const CFGElement &E) {
    Kind kind = E.getKind();
    return kind >= DTOR_BEGIN && kind <= DTOR_END;
  }
};

/// Represents C++ object destructor implicitly generated for automatic object
/// or temporary bound to const reference at the point of leaving its local
/// scope.
class CFGAutomaticObjDtor: public CFGImplicitDtor {
public:
  CFGAutomaticObjDtor(const VarDecl *var, const Stmt *stmt)
      : CFGImplicitDtor(AutomaticObjectDtor, var, stmt) {}

  const VarDecl *getVarDecl() const {
    return static_cast<VarDecl*>(Data1.getPointer());
  }

  // Get statement end of which triggered the destructor call.
  const Stmt *getTriggerStmt() const {
    return static_cast<Stmt*>(Data2.getPointer());
  }

private:
  friend class CFGElement;

  CFGAutomaticObjDtor() = default;

  static bool isKind(const CFGElement &elem) {
    return elem.getKind() == AutomaticObjectDtor;
  }
};

/// Represents C++ object destructor generated from a call to delete.
class CFGDeleteDtor : public CFGImplicitDtor {
public:
  CFGDeleteDtor(const CXXRecordDecl *RD, const CXXDeleteExpr *DE)
      : CFGImplicitDtor(DeleteDtor, RD, DE) {}

  const CXXRecordDecl *getCXXRecordDecl() const {
    return static_cast<CXXRecordDecl*>(Data1.getPointer());
  }

  // Get Delete expression which triggered the destructor call.
  const CXXDeleteExpr *getDeleteExpr() const {
    return static_cast<CXXDeleteExpr *>(Data2.getPointer());
  }

private:
  friend class CFGElement;

  CFGDeleteDtor() = default;

  static bool isKind(const CFGElement &elem) {
    return elem.getKind() == DeleteDtor;
  }
};

/// Represents C++ object destructor implicitly generated for base object in
/// destructor.
class CFGBaseDtor : public CFGImplicitDtor {
public:
  CFGBaseDtor(const CXXBaseSpecifier *base)
      : CFGImplicitDtor(BaseDtor, base) {}

  const CXXBaseSpecifier *getBaseSpecifier() const {
    return static_cast<const CXXBaseSpecifier*>(Data1.getPointer());
  }

private:
  friend class CFGElement;

  CFGBaseDtor() = default;

  static bool isKind(const CFGElement &E) {
    return E.getKind() == BaseDtor;
  }
};

/// Represents C++ object destructor implicitly generated for member object in
/// destructor.
class CFGMemberDtor : public CFGImplicitDtor {
public:
  CFGMemberDtor(const FieldDecl *field)
      : CFGImplicitDtor(MemberDtor, field, nullptr) {}

  const FieldDecl *getFieldDecl() const {
    return static_cast<const FieldDecl*>(Data1.getPointer());
  }

private:
  friend class CFGElement;

  CFGMemberDtor() = default;

  static bool isKind(const CFGElement &E) {
    return E.getKind() == MemberDtor;
  }
};

/// Represents C++ object destructor implicitly generated at the end of full
/// expression for temporary object.
class CFGTemporaryDtor : public CFGImplicitDtor {
public:
  CFGTemporaryDtor(CXXBindTemporaryExpr *expr)
      : CFGImplicitDtor(TemporaryDtor, expr, nullptr) {}

  const CXXBindTemporaryExpr *getBindTemporaryExpr() const {
    return static_cast<const CXXBindTemporaryExpr *>(Data1.getPointer());
  }

private:
  friend class CFGElement;

  CFGTemporaryDtor() = default;

  static bool isKind(const CFGElement &E) {
    return E.getKind() == TemporaryDtor;
  }
};

/// Represents CFGBlock terminator statement.
///
/// TemporaryDtorsBranch bit is set to true if the terminator marks a branch
/// in control flow of destructors of temporaries. In this case terminator
/// statement is the same statement that branches control flow in evaluation
/// of matching full expression.
class CFGTerminator {
  llvm::PointerIntPair<Stmt *, 1> Data;

public:
  CFGTerminator() = default;
  CFGTerminator(Stmt *S, bool TemporaryDtorsBranch = false)
      : Data(S, TemporaryDtorsBranch) {}

  Stmt *getStmt() { return Data.getPointer(); }
  const Stmt *getStmt() const { return Data.getPointer(); }

  bool isTemporaryDtorsBranch() const { return Data.getInt(); }

  operator Stmt *() { return getStmt(); }
  operator const Stmt *() const { return getStmt(); }

  Stmt *operator->() { return getStmt(); }
  const Stmt *operator->() const { return getStmt(); }

  Stmt &operator*() { return *getStmt(); }
  const Stmt &operator*() const { return *getStmt(); }

  explicit operator bool() const { return getStmt(); }
};

/// Represents a single basic block in a source-level CFG.
///  It consists of:
///
///  (1) A set of statements/expressions (which may contain subexpressions).
///  (2) A "terminator" statement (not in the set of statements).
///  (3) A list of successors and predecessors.
///
/// Terminator: The terminator represents the type of control-flow that occurs
/// at the end of the basic block.  The terminator is a Stmt* referring to an
/// AST node that has control-flow: if-statements, breaks, loops, etc.
/// If the control-flow is conditional, the condition expression will appear
/// within the set of statements in the block (usually the last statement).
///
/// Predecessors: the order in the set of predecessors is arbitrary.
///
/// Successors: the order in the set of successors is NOT arbitrary.  We
///  currently have the following orderings based on the terminator:
///
///     Terminator       Successor Ordering
///  -----------------------------------------------------
///       if            Then Block;  Else Block
///     ? operator      LHS expression;  RHS expression
///     &&, ||          expression that uses result of && or ||, RHS
///
/// But note that any of that may be NULL in case of optimized-out edges.
class CFGBlock {
  class ElementList {
    using ImplTy = BumpVector<CFGElement>;

    ImplTy Impl;

  public:
    ElementList(BumpVectorContext &C) : Impl(C, 4) {}

    using iterator = std::reverse_iterator<ImplTy::iterator>;
    using const_iterator = std::reverse_iterator<ImplTy::const_iterator>;
    using reverse_iterator = ImplTy::iterator;
    using const_reverse_iterator = ImplTy::const_iterator;
    using const_reference = ImplTy::const_reference;

    void push_back(CFGElement e, BumpVectorContext &C) { Impl.push_back(e, C); }

    reverse_iterator insert(reverse_iterator I, size_t Cnt, CFGElement E,
        BumpVectorContext &C) {
      return Impl.insert(I, Cnt, E, C);
    }

    const_reference front() const { return Impl.back(); }
    const_reference back() const { return Impl.front(); }

    iterator begin() { return Impl.rbegin(); }
    iterator end() { return Impl.rend(); }
    const_iterator begin() const { return Impl.rbegin(); }
    const_iterator end() const { return Impl.rend(); }
    reverse_iterator rbegin() { return Impl.begin(); }
    reverse_iterator rend() { return Impl.end(); }
    const_reverse_iterator rbegin() const { return Impl.begin(); }
    const_reverse_iterator rend() const { return Impl.end(); }

    CFGElement operator[](size_t i) const  {
      assert(i < Impl.size());
      return Impl[Impl.size() - 1 - i];
    }

    size_t size() const { return Impl.size(); }
    bool empty() const { return Impl.empty(); }
  };

  /// The set of statements in the basic block.
  ElementList Elements;

  /// An (optional) label that prefixes the executable statements in the block.
  /// When this variable is non-NULL, it is either an instance of LabelStmt,
  /// SwitchCase or CXXCatchStmt.
  Stmt *Label = nullptr;

  /// The terminator for a basic block that indicates the type of control-flow
  /// that occurs between a block and its successors.
  CFGTerminator Terminator;

  /// Some blocks are used to represent the "loop edge" to the start of a loop
  /// from within the loop body. This Stmt* will be refer to the loop statement
  /// for such blocks (and be null otherwise).
  const Stmt *LoopTarget = nullptr;

  /// A numerical ID assigned to a CFGBlock during construction of the CFG.
  unsigned BlockID;

public:
  /// This class represents a potential adjacent block in the CFG.  It encodes
  /// whether or not the block is actually reachable, or can be proved to be
  /// trivially unreachable.  For some cases it allows one to encode scenarios
  /// where a block was substituted because the original (now alternate) block
  /// is unreachable.
  class AdjacentBlock {
    enum Kind {
      AB_Normal,
      AB_Unreachable,
      AB_Alternate
    };

    CFGBlock *ReachableBlock;
    llvm::PointerIntPair<CFGBlock *, 2> UnreachableBlock;

  public:
    /// Construct an AdjacentBlock with a possibly unreachable block.
    AdjacentBlock(CFGBlock *B, bool IsReachable);

    /// Construct an AdjacentBlock with a reachable block and an alternate
    /// unreachable block.
    AdjacentBlock(CFGBlock *B, CFGBlock *AlternateBlock);

    /// Get the reachable block, if one exists.
    CFGBlock *getReachableBlock() const {
      return ReachableBlock;
    }

    /// Get the potentially unreachable block.
    CFGBlock *getPossiblyUnreachableBlock() const {
      return UnreachableBlock.getPointer();
    }

    /// Provide an implicit conversion to CFGBlock* so that
    /// AdjacentBlock can be substituted for CFGBlock*.
    operator CFGBlock*() const {
      return getReachableBlock();
    }

    CFGBlock& operator *() const {
      return *getReachableBlock();
    }

    CFGBlock* operator ->() const {
      return getReachableBlock();
    }

    bool isReachable() const {
      Kind K = (Kind) UnreachableBlock.getInt();
      return K == AB_Normal || K == AB_Alternate;
    }
  };

private:
  /// Keep track of the predecessor / successor CFG blocks.
  using AdjacentBlocks = BumpVector<AdjacentBlock>;
  AdjacentBlocks Preds;
  AdjacentBlocks Succs;

  /// This bit is set when the basic block contains a function call
  /// or implicit destructor that is attributed as 'noreturn'. In that case,
  /// control cannot technically ever proceed past this block. All such blocks
  /// will have a single immediate successor: the exit block. This allows them
  /// to be easily reached from the exit block and using this bit quickly
  /// recognized without scanning the contents of the block.
  ///
  /// Optimization Note: This bit could be profitably folded with Terminator's
  /// storage if the memory usage of CFGBlock becomes an issue.
  unsigned HasNoReturnElement : 1;

  /// The parent CFG that owns this CFGBlock.
  CFG *Parent;

public:
  explicit CFGBlock(unsigned blockid, BumpVectorContext &C, CFG *parent)
      : Elements(C), Terminator(nullptr), BlockID(blockid), Preds(C, 1),
        Succs(C, 1), HasNoReturnElement(false), Parent(parent) {}

  // Statement iterators
  using iterator = ElementList::iterator;
  using const_iterator = ElementList::const_iterator;
  using reverse_iterator = ElementList::reverse_iterator;
  using const_reverse_iterator = ElementList::const_reverse_iterator;

  CFGElement                 front()       const { return Elements.front();   }
  CFGElement                 back()        const { return Elements.back();    }

  iterator                   begin()             { return Elements.begin();   }
  iterator                   end()               { return Elements.end();     }
  const_iterator             begin()       const { return Elements.begin();   }
  const_iterator             end()         const { return Elements.end();     }

  reverse_iterator           rbegin()            { return Elements.rbegin();  }
  reverse_iterator           rend()              { return Elements.rend();    }
  const_reverse_iterator     rbegin()      const { return Elements.rbegin();  }
  const_reverse_iterator     rend()        const { return Elements.rend();    }

  unsigned                   size()        const { return Elements.size();    }
  bool                       empty()       const { return Elements.empty();   }

  CFGElement operator[](size_t i) const  { return Elements[i]; }

  // CFG iterators
  using pred_iterator = AdjacentBlocks::iterator;
  using const_pred_iterator = AdjacentBlocks::const_iterator;
  using pred_reverse_iterator = AdjacentBlocks::reverse_iterator;
  using const_pred_reverse_iterator = AdjacentBlocks::const_reverse_iterator;
  using pred_range = llvm::iterator_range<pred_iterator>;
  using pred_const_range = llvm::iterator_range<const_pred_iterator>;

  using succ_iterator = AdjacentBlocks::iterator;
  using const_succ_iterator = AdjacentBlocks::const_iterator;
  using succ_reverse_iterator = AdjacentBlocks::reverse_iterator;
  using const_succ_reverse_iterator = AdjacentBlocks::const_reverse_iterator;
  using succ_range = llvm::iterator_range<succ_iterator>;
  using succ_const_range = llvm::iterator_range<const_succ_iterator>;

  pred_iterator                pred_begin()        { return Preds.begin();   }
  pred_iterator                pred_end()          { return Preds.end();     }
  const_pred_iterator          pred_begin()  const { return Preds.begin();   }
  const_pred_iterator          pred_end()    const { return Preds.end();     }

  pred_reverse_iterator        pred_rbegin()       { return Preds.rbegin();  }
  pred_reverse_iterator        pred_rend()         { return Preds.rend();    }
  const_pred_reverse_iterator  pred_rbegin() const { return Preds.rbegin();  }
  const_pred_reverse_iterator  pred_rend()   const { return Preds.rend();    }

  pred_range preds() {
    return pred_range(pred_begin(), pred_end());
  }

  pred_const_range preds() const {
    return pred_const_range(pred_begin(), pred_end());
  }

  succ_iterator                succ_begin()        { return Succs.begin();   }
  succ_iterator                succ_end()          { return Succs.end();     }
  const_succ_iterator          succ_begin()  const { return Succs.begin();   }
  const_succ_iterator          succ_end()    const { return Succs.end();     }

  succ_reverse_iterator        succ_rbegin()       { return Succs.rbegin();  }
  succ_reverse_iterator        succ_rend()         { return Succs.rend();    }
  const_succ_reverse_iterator  succ_rbegin() const { return Succs.rbegin();  }
  const_succ_reverse_iterator  succ_rend()   const { return Succs.rend();    }

  succ_range succs() {
    return succ_range(succ_begin(), succ_end());
  }

  succ_const_range succs() const {
    return succ_const_range(succ_begin(), succ_end());
  }

  unsigned                     succ_size()   const { return Succs.size();    }
  bool                         succ_empty()  const { return Succs.empty();   }

  unsigned                     pred_size()   const { return Preds.size();    }
  bool                         pred_empty()  const { return Preds.empty();   }


  class FilterOptions {
  public:
    unsigned IgnoreNullPredecessors : 1;
    unsigned IgnoreDefaultsWithCoveredEnums : 1;

    FilterOptions()
        : IgnoreNullPredecessors(1), IgnoreDefaultsWithCoveredEnums(0) {}
  };

  static bool FilterEdge(const FilterOptions &F, const CFGBlock *Src,
       const CFGBlock *Dst);

  template <typename IMPL, bool IsPred>
  class FilteredCFGBlockIterator {
  private:
    IMPL I, E;
    const FilterOptions F;
    const CFGBlock *From;

  public:
    explicit FilteredCFGBlockIterator(const IMPL &i, const IMPL &e,
                                      const CFGBlock *from,
                                      const FilterOptions &f)
        : I(i), E(e), F(f), From(from) {
      while (hasMore() && Filter(*I))
        ++I;
    }

    bool hasMore() const { return I != E; }

    FilteredCFGBlockIterator &operator++() {
      do { ++I; } while (hasMore() && Filter(*I));
      return *this;
    }

    const CFGBlock *operator*() const { return *I; }

  private:
    bool Filter(const CFGBlock *To) {
      return IsPred ? FilterEdge(F, To, From) : FilterEdge(F, From, To);
    }
  };

  using filtered_pred_iterator =
      FilteredCFGBlockIterator<const_pred_iterator, true>;

  using filtered_succ_iterator =
      FilteredCFGBlockIterator<const_succ_iterator, false>;

  filtered_pred_iterator filtered_pred_start_end(const FilterOptions &f) const {
    return filtered_pred_iterator(pred_begin(), pred_end(), this, f);
  }

  filtered_succ_iterator filtered_succ_start_end(const FilterOptions &f) const {
    return filtered_succ_iterator(succ_begin(), succ_end(), this, f);
  }

  // Manipulation of block contents

  void setTerminator(CFGTerminator Term) { Terminator = Term; }
  void setLabel(Stmt *Statement) { Label = Statement; }
  void setLoopTarget(const Stmt *loopTarget) { LoopTarget = loopTarget; }
  void setHasNoReturnElement() { HasNoReturnElement = true; }

  CFGTerminator getTerminator() { return Terminator; }
  const CFGTerminator getTerminator() const { return Terminator; }

  Stmt *getTerminatorCondition(bool StripParens = true);

  const Stmt *getTerminatorCondition(bool StripParens = true) const {
    return const_cast<CFGBlock*>(this)->getTerminatorCondition(StripParens);
  }

  const Stmt *getLoopTarget() const { return LoopTarget; }

  Stmt *getLabel() { return Label; }
  const Stmt *getLabel() const { return Label; }

  bool hasNoReturnElement() const { return HasNoReturnElement; }

  unsigned getBlockID() const { return BlockID; }

  CFG *getParent() const { return Parent; }

  void dump() const;

  void dump(const CFG *cfg, const LangOptions &LO, bool ShowColors = false) const;
  void print(raw_ostream &OS, const CFG* cfg, const LangOptions &LO,
             bool ShowColors) const;
  void printTerminator(raw_ostream &OS, const LangOptions &LO) const;
  void printAsOperand(raw_ostream &OS, bool /*PrintType*/) {
    OS << "BB#" << getBlockID();
  }

  /// Adds a (potentially unreachable) successor block to the current block.
  void addSuccessor(AdjacentBlock Succ, BumpVectorContext &C);

  void appendStmt(Stmt *statement, BumpVectorContext &C) {
    Elements.push_back(CFGStmt(statement), C);
  }

  void appendConstructor(CXXConstructExpr *CE, const ConstructionContext *CC,
                         BumpVectorContext &C) {
    Elements.push_back(CFGConstructor(CE, CC), C);
  }

  void appendCXXRecordTypedCall(Expr *E,
                                const ConstructionContext *CC,
                                BumpVectorContext &C) {
    Elements.push_back(CFGCXXRecordTypedCall(E, CC), C);
  }

  void appendInitializer(CXXCtorInitializer *initializer,
                        BumpVectorContext &C) {
    Elements.push_back(CFGInitializer(initializer), C);
  }

  void appendNewAllocator(CXXNewExpr *NE,
                          BumpVectorContext &C) {
    Elements.push_back(CFGNewAllocator(NE), C);
  }

  void appendScopeBegin(const VarDecl *VD, const Stmt *S,
                        BumpVectorContext &C) {
    Elements.push_back(CFGScopeBegin(VD, S), C);
  }

  void prependScopeBegin(const VarDecl *VD, const Stmt *S,
                         BumpVectorContext &C) {
    Elements.insert(Elements.rbegin(), 1, CFGScopeBegin(VD, S), C);
  }

  void appendScopeEnd(const VarDecl *VD, const Stmt *S, BumpVectorContext &C) {
    Elements.push_back(CFGScopeEnd(VD, S), C);
  }

  void prependScopeEnd(const VarDecl *VD, const Stmt *S, BumpVectorContext &C) {
    Elements.insert(Elements.rbegin(), 1, CFGScopeEnd(VD, S), C);
  }

  void appendBaseDtor(const CXXBaseSpecifier *BS, BumpVectorContext &C) {
    Elements.push_back(CFGBaseDtor(BS), C);
  }

  void appendMemberDtor(FieldDecl *FD, BumpVectorContext &C) {
    Elements.push_back(CFGMemberDtor(FD), C);
  }

  void appendTemporaryDtor(CXXBindTemporaryExpr *E, BumpVectorContext &C) {
    Elements.push_back(CFGTemporaryDtor(E), C);
  }

  void appendAutomaticObjDtor(VarDecl *VD, Stmt *S, BumpVectorContext &C) {
    Elements.push_back(CFGAutomaticObjDtor(VD, S), C);
  }

  void appendLifetimeEnds(VarDecl *VD, Stmt *S, BumpVectorContext &C) {
    Elements.push_back(CFGLifetimeEnds(VD, S), C);
  }

  void appendLoopExit(const Stmt *LoopStmt, BumpVectorContext &C) {
    Elements.push_back(CFGLoopExit(LoopStmt), C);
  }

  void appendDeleteDtor(CXXRecordDecl *RD, CXXDeleteExpr *DE, BumpVectorContext &C) {
    Elements.push_back(CFGDeleteDtor(RD, DE), C);
  }

  // Destructors must be inserted in reversed order. So insertion is in two
  // steps. First we prepare space for some number of elements, then we insert
  // the elements beginning at the last position in prepared space.
  iterator beginAutomaticObjDtorsInsert(iterator I, size_t Cnt,
      BumpVectorContext &C) {
    return iterator(Elements.insert(I.base(), Cnt,
                                    CFGAutomaticObjDtor(nullptr, nullptr), C));
  }
  iterator insertAutomaticObjDtor(iterator I, VarDecl *VD, Stmt *S) {
    *I = CFGAutomaticObjDtor(VD, S);
    return ++I;
  }

  // Scope leaving must be performed in reversed order. So insertion is in two
  // steps. First we prepare space for some number of elements, then we insert
  // the elements beginning at the last position in prepared space.
  iterator beginLifetimeEndsInsert(iterator I, size_t Cnt,
                                   BumpVectorContext &C) {
    return iterator(
        Elements.insert(I.base(), Cnt, CFGLifetimeEnds(nullptr, nullptr), C));
  }
  iterator insertLifetimeEnds(iterator I, VarDecl *VD, Stmt *S) {
    *I = CFGLifetimeEnds(VD, S);
    return ++I;
  }

  // Scope leaving must be performed in reversed order. So insertion is in two
  // steps. First we prepare space for some number of elements, then we insert
  // the elements beginning at the last position in prepared space.
  iterator beginScopeEndInsert(iterator I, size_t Cnt, BumpVectorContext &C) {
    return iterator(
        Elements.insert(I.base(), Cnt, CFGScopeEnd(nullptr, nullptr), C));
  }
  iterator insertScopeEnd(iterator I, VarDecl *VD, Stmt *S) {
    *I = CFGScopeEnd(VD, S);
    return ++I;
  }

};

/// CFGCallback defines methods that should be called when a logical
/// operator error is found when building the CFG.
class CFGCallback {
public:
  CFGCallback() = default;
  virtual ~CFGCallback() = default;

  virtual void compareAlwaysTrue(const BinaryOperator *B, bool isAlwaysTrue) {}
  virtual void compareBitwiseEquality(const BinaryOperator *B,
                                      bool isAlwaysTrue) {}
};

/// Represents a source-level, intra-procedural CFG that represents the
///  control-flow of a Stmt.  The Stmt can represent an entire function body,
///  or a single expression.  A CFG will always contain one empty block that
///  represents the Exit point of the CFG.  A CFG will also contain a designated
///  Entry block.  The CFG solely represents control-flow; it consists of
///  CFGBlocks which are simply containers of Stmt*'s in the AST the CFG
///  was constructed from.
class CFG {
public:
  //===--------------------------------------------------------------------===//
  // CFG Construction & Manipulation.
  //===--------------------------------------------------------------------===//

  class BuildOptions {
    std::bitset<Stmt::lastStmtConstant> alwaysAddMask;

  public:
    using ForcedBlkExprs = llvm::DenseMap<const Stmt *, const CFGBlock *>;

    ForcedBlkExprs **forcedBlkExprs = nullptr;
    CFGCallback *Observer = nullptr;
    bool PruneTriviallyFalseEdges = true;
    bool AddEHEdges = false;
    bool AddInitializers = false;
    bool AddImplicitDtors = false;
    bool AddLifetime = false;
    bool AddLoopExit = false;
    bool AddTemporaryDtors = false;
    bool AddScopes = false;
    bool AddStaticInitBranches = false;
    bool AddCXXNewAllocator = false;
    bool AddCXXDefaultInitExprInCtors = false;
    bool AddRichCXXConstructors = false;
    bool MarkElidedCXXConstructors = false;

    BuildOptions() = default;

    bool alwaysAdd(const Stmt *stmt) const {
      return alwaysAddMask[stmt->getStmtClass()];
    }

    BuildOptions &setAlwaysAdd(Stmt::StmtClass stmtClass, bool val = true) {
      alwaysAddMask[stmtClass] = val;
      return *this;
    }

    BuildOptions &setAllAlwaysAdd() {
      alwaysAddMask.set();
      return *this;
    }
  };

  /// Builds a CFG from an AST.
  static std::unique_ptr<CFG> buildCFG(const Decl *D, Stmt *AST, ASTContext *C,
                                       const BuildOptions &BO);

  /// Create a new block in the CFG. The CFG owns the block; the caller should
  /// not directly free it.
  CFGBlock *createBlock();

  /// Set the entry block of the CFG. This is typically used only during CFG
  /// construction. Most CFG clients expect that the entry block has no
  /// predecessors and contains no statements.
  void setEntry(CFGBlock *B) { Entry = B; }

  /// Set the block used for indirect goto jumps. This is typically used only
  /// during CFG construction.
  void setIndirectGotoBlock(CFGBlock *B) { IndirectGotoBlock = B; }

  //===--------------------------------------------------------------------===//
  // Block Iterators
  //===--------------------------------------------------------------------===//

  using CFGBlockListTy = BumpVector<CFGBlock *>;
  using iterator = CFGBlockListTy::iterator;
  using const_iterator = CFGBlockListTy::const_iterator;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  CFGBlock &                front()                { return *Blocks.front(); }
  CFGBlock &                back()                 { return *Blocks.back(); }

  iterator                  begin()                { return Blocks.begin(); }
  iterator                  end()                  { return Blocks.end(); }
  const_iterator            begin()       const    { return Blocks.begin(); }
  const_iterator            end()         const    { return Blocks.end(); }

  iterator nodes_begin() { return iterator(Blocks.begin()); }
  iterator nodes_end() { return iterator(Blocks.end()); }
  const_iterator nodes_begin() const { return const_iterator(Blocks.begin()); }
  const_iterator nodes_end() const { return const_iterator(Blocks.end()); }

  reverse_iterator          rbegin()               { return Blocks.rbegin(); }
  reverse_iterator          rend()                 { return Blocks.rend(); }
  const_reverse_iterator    rbegin()      const    { return Blocks.rbegin(); }
  const_reverse_iterator    rend()        const    { return Blocks.rend(); }

  CFGBlock &                getEntry()             { return *Entry; }
  const CFGBlock &          getEntry()    const    { return *Entry; }
  CFGBlock &                getExit()              { return *Exit; }
  const CFGBlock &          getExit()     const    { return *Exit; }

  CFGBlock *       getIndirectGotoBlock() { return IndirectGotoBlock; }
  const CFGBlock * getIndirectGotoBlock() const { return IndirectGotoBlock; }

  using try_block_iterator = std::vector<const CFGBlock *>::const_iterator;

  try_block_iterator try_blocks_begin() const {
    return TryDispatchBlocks.begin();
  }

  try_block_iterator try_blocks_end() const {
    return TryDispatchBlocks.end();
  }

  void addTryDispatchBlock(const CFGBlock *block) {
    TryDispatchBlocks.push_back(block);
  }

  /// Records a synthetic DeclStmt and the DeclStmt it was constructed from.
  ///
  /// The CFG uses synthetic DeclStmts when a single AST DeclStmt contains
  /// multiple decls.
  void addSyntheticDeclStmt(const DeclStmt *Synthetic,
                            const DeclStmt *Source) {
    assert(Synthetic->isSingleDecl() && "Can handle single declarations only");
    assert(Synthetic != Source && "Don't include original DeclStmts in map");
    assert(!SyntheticDeclStmts.count(Synthetic) && "Already in map");
    SyntheticDeclStmts[Synthetic] = Source;
  }

  using synthetic_stmt_iterator =
      llvm::DenseMap<const DeclStmt *, const DeclStmt *>::const_iterator;
  using synthetic_stmt_range = llvm::iterator_range<synthetic_stmt_iterator>;

  /// Iterates over synthetic DeclStmts in the CFG.
  ///
  /// Each element is a (synthetic statement, source statement) pair.
  ///
  /// \sa addSyntheticDeclStmt
  synthetic_stmt_iterator synthetic_stmt_begin() const {
    return SyntheticDeclStmts.begin();
  }

  /// \sa synthetic_stmt_begin
  synthetic_stmt_iterator synthetic_stmt_end() const {
    return SyntheticDeclStmts.end();
  }

  /// \sa synthetic_stmt_begin
  synthetic_stmt_range synthetic_stmts() const {
    return synthetic_stmt_range(synthetic_stmt_begin(), synthetic_stmt_end());
  }

  //===--------------------------------------------------------------------===//
  // Member templates useful for various batch operations over CFGs.
  //===--------------------------------------------------------------------===//

  template <typename CALLBACK>
  void VisitBlockStmts(CALLBACK& O) const {
    for (const_iterator I = begin(), E = end(); I != E; ++I)
      for (CFGBlock::const_iterator BI = (*I)->begin(), BE = (*I)->end();
           BI != BE; ++BI) {
        if (Optional<CFGStmt> stmt = BI->getAs<CFGStmt>())
          O(const_cast<Stmt*>(stmt->getStmt()));
      }
  }

  //===--------------------------------------------------------------------===//
  // CFG Introspection.
  //===--------------------------------------------------------------------===//

  /// Returns the total number of BlockIDs allocated (which start at 0).
  unsigned getNumBlockIDs() const { return NumBlockIDs; }

  /// Return the total number of CFGBlocks within the CFG This is simply a
  /// renaming of the getNumBlockIDs(). This is necessary because the dominator
  /// implementation needs such an interface.
  unsigned size() const { return NumBlockIDs; }

  //===--------------------------------------------------------------------===//
  // CFG Debugging: Pretty-Printing and Visualization.
  //===--------------------------------------------------------------------===//

  void viewCFG(const LangOptions &LO) const;
  void print(raw_ostream &OS, const LangOptions &LO, bool ShowColors) const;
  void dump(const LangOptions &LO, bool ShowColors) const;

  //===--------------------------------------------------------------------===//
  // Internal: constructors and data.
  //===--------------------------------------------------------------------===//

  CFG() : Blocks(BlkBVC, 10) {}

  llvm::BumpPtrAllocator& getAllocator() {
    return BlkBVC.getAllocator();
  }

  BumpVectorContext &getBumpVectorContext() {
    return BlkBVC;
  }

private:
  CFGBlock *Entry = nullptr;
  CFGBlock *Exit = nullptr;

  // Special block to contain collective dispatch for indirect gotos
  CFGBlock* IndirectGotoBlock = nullptr;

  unsigned  NumBlockIDs = 0;

  BumpVectorContext BlkBVC;

  CFGBlockListTy Blocks;

  /// C++ 'try' statements are modeled with an indirect dispatch block.
  /// This is the collection of such blocks present in the CFG.
  std::vector<const CFGBlock *> TryDispatchBlocks;

  /// Collects DeclStmts synthesized for this CFG and maps each one back to its
  /// source DeclStmt.
  llvm::DenseMap<const DeclStmt *, const DeclStmt *> SyntheticDeclStmts;
};

} // namespace clang

//===----------------------------------------------------------------------===//
// GraphTraits specializations for CFG basic block graphs (source-level CFGs)
//===----------------------------------------------------------------------===//

namespace llvm {

/// Implement simplify_type for CFGTerminator, so that we can dyn_cast from
/// CFGTerminator to a specific Stmt class.
template <> struct simplify_type< ::clang::CFGTerminator> {
  using SimpleType = ::clang::Stmt *;

  static SimpleType getSimplifiedValue(::clang::CFGTerminator Val) {
    return Val.getStmt();
  }
};

// Traits for: CFGBlock

template <> struct GraphTraits< ::clang::CFGBlock *> {
  using NodeRef = ::clang::CFGBlock *;
  using ChildIteratorType = ::clang::CFGBlock::succ_iterator;

  static NodeRef getEntryNode(::clang::CFGBlock *BB) { return BB; }
  static ChildIteratorType child_begin(NodeRef N) { return N->succ_begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->succ_end(); }
};

template <> struct GraphTraits< const ::clang::CFGBlock *> {
  using NodeRef = const ::clang::CFGBlock *;
  using ChildIteratorType = ::clang::CFGBlock::const_succ_iterator;

  static NodeRef getEntryNode(const clang::CFGBlock *BB) { return BB; }
  static ChildIteratorType child_begin(NodeRef N) { return N->succ_begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->succ_end(); }
};

template <> struct GraphTraits<Inverse< ::clang::CFGBlock *>> {
  using NodeRef = ::clang::CFGBlock *;
  using ChildIteratorType = ::clang::CFGBlock::const_pred_iterator;

  static NodeRef getEntryNode(Inverse<::clang::CFGBlock *> G) {
    return G.Graph;
  }

  static ChildIteratorType child_begin(NodeRef N) { return N->pred_begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->pred_end(); }
};

template <> struct GraphTraits<Inverse<const ::clang::CFGBlock *>> {
  using NodeRef = const ::clang::CFGBlock *;
  using ChildIteratorType = ::clang::CFGBlock::const_pred_iterator;

  static NodeRef getEntryNode(Inverse<const ::clang::CFGBlock *> G) {
    return G.Graph;
  }

  static ChildIteratorType child_begin(NodeRef N) { return N->pred_begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->pred_end(); }
};

// Traits for: CFG

template <> struct GraphTraits< ::clang::CFG* >
    : public GraphTraits< ::clang::CFGBlock *>  {
  using nodes_iterator = ::clang::CFG::iterator;

  static NodeRef getEntryNode(::clang::CFG *F) { return &F->getEntry(); }
  static nodes_iterator nodes_begin(::clang::CFG* F) { return F->nodes_begin();}
  static nodes_iterator   nodes_end(::clang::CFG* F) { return F->nodes_end(); }
  static unsigned              size(::clang::CFG* F) { return F->size(); }
};

template <> struct GraphTraits<const ::clang::CFG* >
    : public GraphTraits<const ::clang::CFGBlock *>  {
  using nodes_iterator = ::clang::CFG::const_iterator;

  static NodeRef getEntryNode(const ::clang::CFG *F) { return &F->getEntry(); }

  static nodes_iterator nodes_begin( const ::clang::CFG* F) {
    return F->nodes_begin();
  }

  static nodes_iterator nodes_end( const ::clang::CFG* F) {
    return F->nodes_end();
  }

  static unsigned size(const ::clang::CFG* F) {
    return F->size();
  }
};

template <> struct GraphTraits<Inverse< ::clang::CFG *>>
  : public GraphTraits<Inverse< ::clang::CFGBlock *>> {
  using nodes_iterator = ::clang::CFG::iterator;

  static NodeRef getEntryNode(::clang::CFG *F) { return &F->getExit(); }
  static nodes_iterator nodes_begin( ::clang::CFG* F) {return F->nodes_begin();}
  static nodes_iterator nodes_end( ::clang::CFG* F) { return F->nodes_end(); }
};

template <> struct GraphTraits<Inverse<const ::clang::CFG *>>
  : public GraphTraits<Inverse<const ::clang::CFGBlock *>> {
  using nodes_iterator = ::clang::CFG::const_iterator;

  static NodeRef getEntryNode(const ::clang::CFG *F) { return &F->getExit(); }

  static nodes_iterator nodes_begin(const ::clang::CFG* F) {
    return F->nodes_begin();
  }

  static nodes_iterator nodes_end(const ::clang::CFG* F) {
    return F->nodes_end();
  }
};

} // namespace llvm

#endif // LLVM_CLANG_ANALYSIS_CFG_H
