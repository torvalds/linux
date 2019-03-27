//===- CFG.cpp - Classes for representing and building CFGs ---------------===//
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

#include "clang/Analysis/CFG.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/StmtObjC.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/Type.h"
#include "clang/Analysis/Support/BumpVector.h"
#include "clang/Analysis/ConstructionContext.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/ExceptionSpecificationType.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DOTGraphTraits.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace clang;

static SourceLocation GetEndLoc(Decl *D) {
  if (VarDecl *VD = dyn_cast<VarDecl>(D))
    if (Expr *Ex = VD->getInit())
      return Ex->getSourceRange().getEnd();
  return D->getLocation();
}

/// Helper for tryNormalizeBinaryOperator. Attempts to extract an IntegerLiteral
/// or EnumConstantDecl from the given Expr. If it fails, returns nullptr.
static const Expr *tryTransformToIntOrEnumConstant(const Expr *E) {
  E = E->IgnoreParens();
  if (isa<IntegerLiteral>(E))
    return E;
  if (auto *DR = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts()))
    return isa<EnumConstantDecl>(DR->getDecl()) ? DR : nullptr;
  return nullptr;
}

/// Tries to interpret a binary operator into `Decl Op Expr` form, if Expr is
/// an integer literal or an enum constant.
///
/// If this fails, at least one of the returned DeclRefExpr or Expr will be
/// null.
static std::tuple<const DeclRefExpr *, BinaryOperatorKind, const Expr *>
tryNormalizeBinaryOperator(const BinaryOperator *B) {
  BinaryOperatorKind Op = B->getOpcode();

  const Expr *MaybeDecl = B->getLHS();
  const Expr *Constant = tryTransformToIntOrEnumConstant(B->getRHS());
  // Expr looked like `0 == Foo` instead of `Foo == 0`
  if (Constant == nullptr) {
    // Flip the operator
    if (Op == BO_GT)
      Op = BO_LT;
    else if (Op == BO_GE)
      Op = BO_LE;
    else if (Op == BO_LT)
      Op = BO_GT;
    else if (Op == BO_LE)
      Op = BO_GE;

    MaybeDecl = B->getRHS();
    Constant = tryTransformToIntOrEnumConstant(B->getLHS());
  }

  auto *D = dyn_cast<DeclRefExpr>(MaybeDecl->IgnoreParenImpCasts());
  return std::make_tuple(D, Op, Constant);
}

/// For an expression `x == Foo && x == Bar`, this determines whether the
/// `Foo` and `Bar` are either of the same enumeration type, or both integer
/// literals.
///
/// It's an error to pass this arguments that are not either IntegerLiterals
/// or DeclRefExprs (that have decls of type EnumConstantDecl)
static bool areExprTypesCompatible(const Expr *E1, const Expr *E2) {
  // User intent isn't clear if they're mixing int literals with enum
  // constants.
  if (isa<IntegerLiteral>(E1) != isa<IntegerLiteral>(E2))
    return false;

  // Integer literal comparisons, regardless of literal type, are acceptable.
  if (isa<IntegerLiteral>(E1))
    return true;

  // IntegerLiterals are handled above and only EnumConstantDecls are expected
  // beyond this point
  assert(isa<DeclRefExpr>(E1) && isa<DeclRefExpr>(E2));
  auto *Decl1 = cast<DeclRefExpr>(E1)->getDecl();
  auto *Decl2 = cast<DeclRefExpr>(E2)->getDecl();

  assert(isa<EnumConstantDecl>(Decl1) && isa<EnumConstantDecl>(Decl2));
  const DeclContext *DC1 = Decl1->getDeclContext();
  const DeclContext *DC2 = Decl2->getDeclContext();

  assert(isa<EnumDecl>(DC1) && isa<EnumDecl>(DC2));
  return DC1 == DC2;
}

namespace {

class CFGBuilder;

/// The CFG builder uses a recursive algorithm to build the CFG.  When
///  we process an expression, sometimes we know that we must add the
///  subexpressions as block-level expressions.  For example:
///
///    exp1 || exp2
///
///  When processing the '||' expression, we know that exp1 and exp2
///  need to be added as block-level expressions, even though they
///  might not normally need to be.  AddStmtChoice records this
///  contextual information.  If AddStmtChoice is 'NotAlwaysAdd', then
///  the builder has an option not to add a subexpression as a
///  block-level expression.
class AddStmtChoice {
public:
  enum Kind { NotAlwaysAdd = 0, AlwaysAdd = 1 };

  AddStmtChoice(Kind a_kind = NotAlwaysAdd) : kind(a_kind) {}

  bool alwaysAdd(CFGBuilder &builder,
                 const Stmt *stmt) const;

  /// Return a copy of this object, except with the 'always-add' bit
  ///  set as specified.
  AddStmtChoice withAlwaysAdd(bool alwaysAdd) const {
    return AddStmtChoice(alwaysAdd ? AlwaysAdd : NotAlwaysAdd);
  }

private:
  Kind kind;
};

/// LocalScope - Node in tree of local scopes created for C++ implicit
/// destructor calls generation. It contains list of automatic variables
/// declared in the scope and link to position in previous scope this scope
/// began in.
///
/// The process of creating local scopes is as follows:
/// - Init CFGBuilder::ScopePos with invalid position (equivalent for null),
/// - Before processing statements in scope (e.g. CompoundStmt) create
///   LocalScope object using CFGBuilder::ScopePos as link to previous scope
///   and set CFGBuilder::ScopePos to the end of new scope,
/// - On every occurrence of VarDecl increase CFGBuilder::ScopePos if it points
///   at this VarDecl,
/// - For every normal (without jump) end of scope add to CFGBlock destructors
///   for objects in the current scope,
/// - For every jump add to CFGBlock destructors for objects
///   between CFGBuilder::ScopePos and local scope position saved for jump
///   target. Thanks to C++ restrictions on goto jumps we can be sure that
///   jump target position will be on the path to root from CFGBuilder::ScopePos
///   (adding any variable that doesn't need constructor to be called to
///   LocalScope can break this assumption),
///
class LocalScope {
public:
  friend class const_iterator;

  using AutomaticVarsTy = BumpVector<VarDecl *>;

  /// const_iterator - Iterates local scope backwards and jumps to previous
  /// scope on reaching the beginning of currently iterated scope.
  class const_iterator {
    const LocalScope* Scope = nullptr;

    /// VarIter is guaranteed to be greater then 0 for every valid iterator.
    /// Invalid iterator (with null Scope) has VarIter equal to 0.
    unsigned VarIter = 0;

  public:
    /// Create invalid iterator. Dereferencing invalid iterator is not allowed.
    /// Incrementing invalid iterator is allowed and will result in invalid
    /// iterator.
    const_iterator() = default;

    /// Create valid iterator. In case when S.Prev is an invalid iterator and
    /// I is equal to 0, this will create invalid iterator.
    const_iterator(const LocalScope& S, unsigned I)
        : Scope(&S), VarIter(I) {
      // Iterator to "end" of scope is not allowed. Handle it by going up
      // in scopes tree possibly up to invalid iterator in the root.
      if (VarIter == 0 && Scope)
        *this = Scope->Prev;
    }

    VarDecl *const* operator->() const {
      assert(Scope && "Dereferencing invalid iterator is not allowed");
      assert(VarIter != 0 && "Iterator has invalid value of VarIter member");
      return &Scope->Vars[VarIter - 1];
    }

    const VarDecl *getFirstVarInScope() const {
      assert(Scope && "Dereferencing invalid iterator is not allowed");
      assert(VarIter != 0 && "Iterator has invalid value of VarIter member");
      return Scope->Vars[0];
    }

    VarDecl *operator*() const {
      return *this->operator->();
    }

    const_iterator &operator++() {
      if (!Scope)
        return *this;

      assert(VarIter != 0 && "Iterator has invalid value of VarIter member");
      --VarIter;
      if (VarIter == 0)
        *this = Scope->Prev;
      return *this;
    }
    const_iterator operator++(int) {
      const_iterator P = *this;
      ++*this;
      return P;
    }

    bool operator==(const const_iterator &rhs) const {
      return Scope == rhs.Scope && VarIter == rhs.VarIter;
    }
    bool operator!=(const const_iterator &rhs) const {
      return !(*this == rhs);
    }

    explicit operator bool() const {
      return *this != const_iterator();
    }

    int distance(const_iterator L);
    const_iterator shared_parent(const_iterator L);
    bool pointsToFirstDeclaredVar() { return VarIter == 1; }
  };

private:
  BumpVectorContext ctx;

  /// Automatic variables in order of declaration.
  AutomaticVarsTy Vars;

  /// Iterator to variable in previous scope that was declared just before
  /// begin of this scope.
  const_iterator Prev;

public:
  /// Constructs empty scope linked to previous scope in specified place.
  LocalScope(BumpVectorContext ctx, const_iterator P)
      : ctx(std::move(ctx)), Vars(this->ctx, 4), Prev(P) {}

  /// Begin of scope in direction of CFG building (backwards).
  const_iterator begin() const { return const_iterator(*this, Vars.size()); }

  void addVar(VarDecl *VD) {
    Vars.push_back(VD, ctx);
  }
};

} // namespace

/// distance - Calculates distance from this to L. L must be reachable from this
/// (with use of ++ operator). Cost of calculating the distance is linear w.r.t.
/// number of scopes between this and L.
int LocalScope::const_iterator::distance(LocalScope::const_iterator L) {
  int D = 0;
  const_iterator F = *this;
  while (F.Scope != L.Scope) {
    assert(F != const_iterator() &&
           "L iterator is not reachable from F iterator.");
    D += F.VarIter;
    F = F.Scope->Prev;
  }
  D += F.VarIter - L.VarIter;
  return D;
}

/// Calculates the closest parent of this iterator
/// that is in a scope reachable through the parents of L.
/// I.e. when using 'goto' from this to L, the lifetime of all variables
/// between this and shared_parent(L) end.
LocalScope::const_iterator
LocalScope::const_iterator::shared_parent(LocalScope::const_iterator L) {
  llvm::SmallPtrSet<const LocalScope *, 4> ScopesOfL;
  while (true) {
    ScopesOfL.insert(L.Scope);
    if (L == const_iterator())
      break;
    L = L.Scope->Prev;
  }

  const_iterator F = *this;
  while (true) {
    if (ScopesOfL.count(F.Scope))
      return F;
    assert(F != const_iterator() &&
           "L iterator is not reachable from F iterator.");
    F = F.Scope->Prev;
  }
}

namespace {

/// Structure for specifying position in CFG during its build process. It
/// consists of CFGBlock that specifies position in CFG and
/// LocalScope::const_iterator that specifies position in LocalScope graph.
struct BlockScopePosPair {
  CFGBlock *block = nullptr;
  LocalScope::const_iterator scopePosition;

  BlockScopePosPair() = default;
  BlockScopePosPair(CFGBlock *b, LocalScope::const_iterator scopePos)
      : block(b), scopePosition(scopePos) {}
};

/// TryResult - a class representing a variant over the values
///  'true', 'false', or 'unknown'.  This is returned by tryEvaluateBool,
///  and is used by the CFGBuilder to decide if a branch condition
///  can be decided up front during CFG construction.
class TryResult {
  int X = -1;

public:
  TryResult() = default;
  TryResult(bool b) : X(b ? 1 : 0) {}

  bool isTrue() const { return X == 1; }
  bool isFalse() const { return X == 0; }
  bool isKnown() const { return X >= 0; }

  void negate() {
    assert(isKnown());
    X ^= 0x1;
  }
};

} // namespace

static TryResult bothKnownTrue(TryResult R1, TryResult R2) {
  if (!R1.isKnown() || !R2.isKnown())
    return TryResult();
  return TryResult(R1.isTrue() && R2.isTrue());
}

namespace {

class reverse_children {
  llvm::SmallVector<Stmt *, 12> childrenBuf;
  ArrayRef<Stmt *> children;

public:
  reverse_children(Stmt *S);

  using iterator = ArrayRef<Stmt *>::reverse_iterator;

  iterator begin() const { return children.rbegin(); }
  iterator end() const { return children.rend(); }
};

} // namespace

reverse_children::reverse_children(Stmt *S) {
  if (CallExpr *CE = dyn_cast<CallExpr>(S)) {
    children = CE->getRawSubExprs();
    return;
  }
  switch (S->getStmtClass()) {
    // Note: Fill in this switch with more cases we want to optimize.
    case Stmt::InitListExprClass: {
      InitListExpr *IE = cast<InitListExpr>(S);
      children = llvm::makeArrayRef(reinterpret_cast<Stmt**>(IE->getInits()),
                                    IE->getNumInits());
      return;
    }
    default:
      break;
  }

  // Default case for all other statements.
  for (Stmt *SubStmt : S->children())
    childrenBuf.push_back(SubStmt);

  // This needs to be done *after* childrenBuf has been populated.
  children = childrenBuf;
}

namespace {

/// CFGBuilder - This class implements CFG construction from an AST.
///   The builder is stateful: an instance of the builder should be used to only
///   construct a single CFG.
///
///   Example usage:
///
///     CFGBuilder builder;
///     std::unique_ptr<CFG> cfg = builder.buildCFG(decl, stmt1);
///
///  CFG construction is done via a recursive walk of an AST.  We actually parse
///  the AST in reverse order so that the successor of a basic block is
///  constructed prior to its predecessor.  This allows us to nicely capture
///  implicit fall-throughs without extra basic blocks.
class CFGBuilder {
  using JumpTarget = BlockScopePosPair;
  using JumpSource = BlockScopePosPair;

  ASTContext *Context;
  std::unique_ptr<CFG> cfg;

  // Current block.
  CFGBlock *Block = nullptr;

  // Block after the current block.
  CFGBlock *Succ = nullptr;

  JumpTarget ContinueJumpTarget;
  JumpTarget BreakJumpTarget;
  JumpTarget SEHLeaveJumpTarget;
  CFGBlock *SwitchTerminatedBlock = nullptr;
  CFGBlock *DefaultCaseBlock = nullptr;

  // This can point either to a try or a __try block. The frontend forbids
  // mixing both kinds in one function, so having one for both is enough.
  CFGBlock *TryTerminatedBlock = nullptr;

  // Current position in local scope.
  LocalScope::const_iterator ScopePos;

  // LabelMap records the mapping from Label expressions to their jump targets.
  using LabelMapTy = llvm::DenseMap<LabelDecl *, JumpTarget>;
  LabelMapTy LabelMap;

  // A list of blocks that end with a "goto" that must be backpatched to their
  // resolved targets upon completion of CFG construction.
  using BackpatchBlocksTy = std::vector<JumpSource>;
  BackpatchBlocksTy BackpatchBlocks;

  // A list of labels whose address has been taken (for indirect gotos).
  using LabelSetTy = llvm::SmallSetVector<LabelDecl *, 8>;
  LabelSetTy AddressTakenLabels;

  // Information about the currently visited C++ object construction site.
  // This is set in the construction trigger and read when the constructor
  // or a function that returns an object by value is being visited.
  llvm::DenseMap<Expr *, const ConstructionContextLayer *>
      ConstructionContextMap;

  using DeclsWithEndedScopeSetTy = llvm::SmallSetVector<VarDecl *, 16>;
  DeclsWithEndedScopeSetTy DeclsWithEndedScope;

  bool badCFG = false;
  const CFG::BuildOptions &BuildOpts;

  // State to track for building switch statements.
  bool switchExclusivelyCovered = false;
  Expr::EvalResult *switchCond = nullptr;

  CFG::BuildOptions::ForcedBlkExprs::value_type *cachedEntry = nullptr;
  const Stmt *lastLookup = nullptr;

  // Caches boolean evaluations of expressions to avoid multiple re-evaluations
  // during construction of branches for chained logical operators.
  using CachedBoolEvalsTy = llvm::DenseMap<Expr *, TryResult>;
  CachedBoolEvalsTy CachedBoolEvals;

public:
  explicit CFGBuilder(ASTContext *astContext,
                      const CFG::BuildOptions &buildOpts)
      : Context(astContext), cfg(new CFG()), // crew a new CFG
        ConstructionContextMap(), BuildOpts(buildOpts) {}


  // buildCFG - Used by external clients to construct the CFG.
  std::unique_ptr<CFG> buildCFG(const Decl *D, Stmt *Statement);

  bool alwaysAdd(const Stmt *stmt);

private:
  // Visitors to walk an AST and construct the CFG.
  CFGBlock *VisitAddrLabelExpr(AddrLabelExpr *A, AddStmtChoice asc);
  CFGBlock *VisitBinaryOperator(BinaryOperator *B, AddStmtChoice asc);
  CFGBlock *VisitBreakStmt(BreakStmt *B);
  CFGBlock *VisitCallExpr(CallExpr *C, AddStmtChoice asc);
  CFGBlock *VisitCaseStmt(CaseStmt *C);
  CFGBlock *VisitChooseExpr(ChooseExpr *C, AddStmtChoice asc);
  CFGBlock *VisitCompoundStmt(CompoundStmt *C);
  CFGBlock *VisitConditionalOperator(AbstractConditionalOperator *C,
                                     AddStmtChoice asc);
  CFGBlock *VisitContinueStmt(ContinueStmt *C);
  CFGBlock *VisitCXXBindTemporaryExpr(CXXBindTemporaryExpr *E,
                                      AddStmtChoice asc);
  CFGBlock *VisitCXXCatchStmt(CXXCatchStmt *S);
  CFGBlock *VisitCXXConstructExpr(CXXConstructExpr *C, AddStmtChoice asc);
  CFGBlock *VisitCXXNewExpr(CXXNewExpr *DE, AddStmtChoice asc);
  CFGBlock *VisitCXXDeleteExpr(CXXDeleteExpr *DE, AddStmtChoice asc);
  CFGBlock *VisitCXXForRangeStmt(CXXForRangeStmt *S);
  CFGBlock *VisitCXXFunctionalCastExpr(CXXFunctionalCastExpr *E,
                                       AddStmtChoice asc);
  CFGBlock *VisitCXXTemporaryObjectExpr(CXXTemporaryObjectExpr *C,
                                        AddStmtChoice asc);
  CFGBlock *VisitCXXThrowExpr(CXXThrowExpr *T);
  CFGBlock *VisitCXXTryStmt(CXXTryStmt *S);
  CFGBlock *VisitDeclStmt(DeclStmt *DS);
  CFGBlock *VisitDeclSubExpr(DeclStmt *DS);
  CFGBlock *VisitDefaultStmt(DefaultStmt *D);
  CFGBlock *VisitDoStmt(DoStmt *D);
  CFGBlock *VisitExprWithCleanups(ExprWithCleanups *E, AddStmtChoice asc);
  CFGBlock *VisitForStmt(ForStmt *F);
  CFGBlock *VisitGotoStmt(GotoStmt *G);
  CFGBlock *VisitIfStmt(IfStmt *I);
  CFGBlock *VisitImplicitCastExpr(ImplicitCastExpr *E, AddStmtChoice asc);
  CFGBlock *VisitConstantExpr(ConstantExpr *E, AddStmtChoice asc);
  CFGBlock *VisitIndirectGotoStmt(IndirectGotoStmt *I);
  CFGBlock *VisitLabelStmt(LabelStmt *L);
  CFGBlock *VisitBlockExpr(BlockExpr *E, AddStmtChoice asc);
  CFGBlock *VisitLambdaExpr(LambdaExpr *E, AddStmtChoice asc);
  CFGBlock *VisitLogicalOperator(BinaryOperator *B);
  std::pair<CFGBlock *, CFGBlock *> VisitLogicalOperator(BinaryOperator *B,
                                                         Stmt *Term,
                                                         CFGBlock *TrueBlock,
                                                         CFGBlock *FalseBlock);
  CFGBlock *VisitMaterializeTemporaryExpr(MaterializeTemporaryExpr *MTE,
                                          AddStmtChoice asc);
  CFGBlock *VisitMemberExpr(MemberExpr *M, AddStmtChoice asc);
  CFGBlock *VisitObjCAtCatchStmt(ObjCAtCatchStmt *S);
  CFGBlock *VisitObjCAtSynchronizedStmt(ObjCAtSynchronizedStmt *S);
  CFGBlock *VisitObjCAtThrowStmt(ObjCAtThrowStmt *S);
  CFGBlock *VisitObjCAtTryStmt(ObjCAtTryStmt *S);
  CFGBlock *VisitObjCAutoreleasePoolStmt(ObjCAutoreleasePoolStmt *S);
  CFGBlock *VisitObjCForCollectionStmt(ObjCForCollectionStmt *S);
  CFGBlock *VisitObjCMessageExpr(ObjCMessageExpr *E, AddStmtChoice asc);
  CFGBlock *VisitPseudoObjectExpr(PseudoObjectExpr *E);
  CFGBlock *VisitReturnStmt(Stmt *S);
  CFGBlock *VisitSEHExceptStmt(SEHExceptStmt *S);
  CFGBlock *VisitSEHFinallyStmt(SEHFinallyStmt *S);
  CFGBlock *VisitSEHLeaveStmt(SEHLeaveStmt *S);
  CFGBlock *VisitSEHTryStmt(SEHTryStmt *S);
  CFGBlock *VisitStmtExpr(StmtExpr *S, AddStmtChoice asc);
  CFGBlock *VisitSwitchStmt(SwitchStmt *S);
  CFGBlock *VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *E,
                                          AddStmtChoice asc);
  CFGBlock *VisitUnaryOperator(UnaryOperator *U, AddStmtChoice asc);
  CFGBlock *VisitWhileStmt(WhileStmt *W);

  CFGBlock *Visit(Stmt *S, AddStmtChoice asc = AddStmtChoice::NotAlwaysAdd);
  CFGBlock *VisitStmt(Stmt *S, AddStmtChoice asc);
  CFGBlock *VisitChildren(Stmt *S);
  CFGBlock *VisitNoRecurse(Expr *E, AddStmtChoice asc);

  void maybeAddScopeBeginForVarDecl(CFGBlock *B, const VarDecl *VD,
                                    const Stmt *S) {
    if (ScopePos && (VD == ScopePos.getFirstVarInScope()))
      appendScopeBegin(B, VD, S);
  }

  /// When creating the CFG for temporary destructors, we want to mirror the
  /// branch structure of the corresponding constructor calls.
  /// Thus, while visiting a statement for temporary destructors, we keep a
  /// context to keep track of the following information:
  /// - whether a subexpression is executed unconditionally
  /// - if a subexpression is executed conditionally, the first
  ///   CXXBindTemporaryExpr we encounter in that subexpression (which
  ///   corresponds to the last temporary destructor we have to call for this
  ///   subexpression) and the CFG block at that point (which will become the
  ///   successor block when inserting the decision point).
  ///
  /// That way, we can build the branch structure for temporary destructors as
  /// follows:
  /// 1. If a subexpression is executed unconditionally, we add the temporary
  ///    destructor calls to the current block.
  /// 2. If a subexpression is executed conditionally, when we encounter a
  ///    CXXBindTemporaryExpr:
  ///    a) If it is the first temporary destructor call in the subexpression,
  ///       we remember the CXXBindTemporaryExpr and the current block in the
  ///       TempDtorContext; we start a new block, and insert the temporary
  ///       destructor call.
  ///    b) Otherwise, add the temporary destructor call to the current block.
  ///  3. When we finished visiting a conditionally executed subexpression,
  ///     and we found at least one temporary constructor during the visitation
  ///     (2.a has executed), we insert a decision block that uses the
  ///     CXXBindTemporaryExpr as terminator, and branches to the current block
  ///     if the CXXBindTemporaryExpr was marked executed, and otherwise
  ///     branches to the stored successor.
  struct TempDtorContext {
    TempDtorContext() = default;
    TempDtorContext(TryResult KnownExecuted)
        : IsConditional(true), KnownExecuted(KnownExecuted) {}

    /// Returns whether we need to start a new branch for a temporary destructor
    /// call. This is the case when the temporary destructor is
    /// conditionally executed, and it is the first one we encounter while
    /// visiting a subexpression - other temporary destructors at the same level
    /// will be added to the same block and are executed under the same
    /// condition.
    bool needsTempDtorBranch() const {
      return IsConditional && !TerminatorExpr;
    }

    /// Remember the successor S of a temporary destructor decision branch for
    /// the corresponding CXXBindTemporaryExpr E.
    void setDecisionPoint(CFGBlock *S, CXXBindTemporaryExpr *E) {
      Succ = S;
      TerminatorExpr = E;
    }

    const bool IsConditional = false;
    const TryResult KnownExecuted = true;
    CFGBlock *Succ = nullptr;
    CXXBindTemporaryExpr *TerminatorExpr = nullptr;
  };

  // Visitors to walk an AST and generate destructors of temporaries in
  // full expression.
  CFGBlock *VisitForTemporaryDtors(Stmt *E, bool BindToTemporary,
                                   TempDtorContext &Context);
  CFGBlock *VisitChildrenForTemporaryDtors(Stmt *E, TempDtorContext &Context);
  CFGBlock *VisitBinaryOperatorForTemporaryDtors(BinaryOperator *E,
                                                 TempDtorContext &Context);
  CFGBlock *VisitCXXBindTemporaryExprForTemporaryDtors(
      CXXBindTemporaryExpr *E, bool BindToTemporary, TempDtorContext &Context);
  CFGBlock *VisitConditionalOperatorForTemporaryDtors(
      AbstractConditionalOperator *E, bool BindToTemporary,
      TempDtorContext &Context);
  void InsertTempDtorDecisionBlock(const TempDtorContext &Context,
                                   CFGBlock *FalseSucc = nullptr);

  // NYS == Not Yet Supported
  CFGBlock *NYS() {
    badCFG = true;
    return Block;
  }

  // Remember to apply the construction context based on the current \p Layer
  // when constructing the CFG element for \p CE.
  void consumeConstructionContext(const ConstructionContextLayer *Layer,
                                  Expr *E);

  // Scan \p Child statement to find constructors in it, while keeping in mind
  // that its parent statement is providing a partial construction context
  // described by \p Layer. If a constructor is found, it would be assigned
  // the context based on the layer. If an additional construction context layer
  // is found, the function recurses into that.
  void findConstructionContexts(const ConstructionContextLayer *Layer,
                                Stmt *Child);

  // Scan all arguments of a call expression for a construction context.
  // These sorts of call expressions don't have a common superclass,
  // hence strict duck-typing.
  template <typename CallLikeExpr,
            typename = typename std::enable_if<
                std::is_same<CallLikeExpr, CallExpr>::value ||
                std::is_same<CallLikeExpr, CXXConstructExpr>::value ||
                std::is_same<CallLikeExpr, ObjCMessageExpr>::value>>
  void findConstructionContextsForArguments(CallLikeExpr *E) {
    for (unsigned i = 0, e = E->getNumArgs(); i != e; ++i) {
      Expr *Arg = E->getArg(i);
      if (Arg->getType()->getAsCXXRecordDecl() && !Arg->isGLValue())
        findConstructionContexts(
            ConstructionContextLayer::create(cfg->getBumpVectorContext(),
                                             ConstructionContextItem(E, i)),
            Arg);
    }
  }

  // Unset the construction context after consuming it. This is done immediately
  // after adding the CFGConstructor or CFGCXXRecordTypedCall element, so
  // there's no need to do this manually in every Visit... function.
  void cleanupConstructionContext(Expr *E);

  void autoCreateBlock() { if (!Block) Block = createBlock(); }
  CFGBlock *createBlock(bool add_successor = true);
  CFGBlock *createNoReturnBlock();

  CFGBlock *addStmt(Stmt *S) {
    return Visit(S, AddStmtChoice::AlwaysAdd);
  }

  CFGBlock *addInitializer(CXXCtorInitializer *I);
  void addLoopExit(const Stmt *LoopStmt);
  void addAutomaticObjDtors(LocalScope::const_iterator B,
                            LocalScope::const_iterator E, Stmt *S);
  void addLifetimeEnds(LocalScope::const_iterator B,
                       LocalScope::const_iterator E, Stmt *S);
  void addAutomaticObjHandling(LocalScope::const_iterator B,
                               LocalScope::const_iterator E, Stmt *S);
  void addImplicitDtorsForDestructor(const CXXDestructorDecl *DD);
  void addScopesEnd(LocalScope::const_iterator B, LocalScope::const_iterator E,
                    Stmt *S);

  void getDeclsWithEndedScope(LocalScope::const_iterator B,
                              LocalScope::const_iterator E, Stmt *S);

  // Local scopes creation.
  LocalScope* createOrReuseLocalScope(LocalScope* Scope);

  void addLocalScopeForStmt(Stmt *S);
  LocalScope* addLocalScopeForDeclStmt(DeclStmt *DS,
                                       LocalScope* Scope = nullptr);
  LocalScope* addLocalScopeForVarDecl(VarDecl *VD, LocalScope* Scope = nullptr);

  void addLocalScopeAndDtors(Stmt *S);

  const ConstructionContext *retrieveAndCleanupConstructionContext(Expr *E) {
    if (!BuildOpts.AddRichCXXConstructors)
      return nullptr;

    const ConstructionContextLayer *Layer = ConstructionContextMap.lookup(E);
    if (!Layer)
      return nullptr;

    cleanupConstructionContext(E);
    return ConstructionContext::createFromLayers(cfg->getBumpVectorContext(),
                                                 Layer);
  }

  // Interface to CFGBlock - adding CFGElements.

  void appendStmt(CFGBlock *B, const Stmt *S) {
    if (alwaysAdd(S) && cachedEntry)
      cachedEntry->second = B;

    // All block-level expressions should have already been IgnoreParens()ed.
    assert(!isa<Expr>(S) || cast<Expr>(S)->IgnoreParens() == S);
    B->appendStmt(const_cast<Stmt*>(S), cfg->getBumpVectorContext());
  }

  void appendConstructor(CFGBlock *B, CXXConstructExpr *CE) {
    if (const ConstructionContext *CC =
            retrieveAndCleanupConstructionContext(CE)) {
      B->appendConstructor(CE, CC, cfg->getBumpVectorContext());
      return;
    }

    // No valid construction context found. Fall back to statement.
    B->appendStmt(CE, cfg->getBumpVectorContext());
  }

  void appendCall(CFGBlock *B, CallExpr *CE) {
    if (alwaysAdd(CE) && cachedEntry)
      cachedEntry->second = B;

    if (const ConstructionContext *CC =
            retrieveAndCleanupConstructionContext(CE)) {
      B->appendCXXRecordTypedCall(CE, CC, cfg->getBumpVectorContext());
      return;
    }

    // No valid construction context found. Fall back to statement.
    B->appendStmt(CE, cfg->getBumpVectorContext());
  }

  void appendInitializer(CFGBlock *B, CXXCtorInitializer *I) {
    B->appendInitializer(I, cfg->getBumpVectorContext());
  }

  void appendNewAllocator(CFGBlock *B, CXXNewExpr *NE) {
    B->appendNewAllocator(NE, cfg->getBumpVectorContext());
  }

  void appendBaseDtor(CFGBlock *B, const CXXBaseSpecifier *BS) {
    B->appendBaseDtor(BS, cfg->getBumpVectorContext());
  }

  void appendMemberDtor(CFGBlock *B, FieldDecl *FD) {
    B->appendMemberDtor(FD, cfg->getBumpVectorContext());
  }

  void appendObjCMessage(CFGBlock *B, ObjCMessageExpr *ME) {
    if (alwaysAdd(ME) && cachedEntry)
      cachedEntry->second = B;

    if (const ConstructionContext *CC =
            retrieveAndCleanupConstructionContext(ME)) {
      B->appendCXXRecordTypedCall(ME, CC, cfg->getBumpVectorContext());
      return;
    }

    B->appendStmt(const_cast<ObjCMessageExpr *>(ME),
                  cfg->getBumpVectorContext());
  }

  void appendTemporaryDtor(CFGBlock *B, CXXBindTemporaryExpr *E) {
    B->appendTemporaryDtor(E, cfg->getBumpVectorContext());
  }

  void appendAutomaticObjDtor(CFGBlock *B, VarDecl *VD, Stmt *S) {
    B->appendAutomaticObjDtor(VD, S, cfg->getBumpVectorContext());
  }

  void appendLifetimeEnds(CFGBlock *B, VarDecl *VD, Stmt *S) {
    B->appendLifetimeEnds(VD, S, cfg->getBumpVectorContext());
  }

  void appendLoopExit(CFGBlock *B, const Stmt *LoopStmt) {
    B->appendLoopExit(LoopStmt, cfg->getBumpVectorContext());
  }

  void appendDeleteDtor(CFGBlock *B, CXXRecordDecl *RD, CXXDeleteExpr *DE) {
    B->appendDeleteDtor(RD, DE, cfg->getBumpVectorContext());
  }

  void prependAutomaticObjDtorsWithTerminator(CFGBlock *Blk,
      LocalScope::const_iterator B, LocalScope::const_iterator E);

  void prependAutomaticObjLifetimeWithTerminator(CFGBlock *Blk,
                                                 LocalScope::const_iterator B,
                                                 LocalScope::const_iterator E);

  const VarDecl *
  prependAutomaticObjScopeEndWithTerminator(CFGBlock *Blk,
                                            LocalScope::const_iterator B,
                                            LocalScope::const_iterator E);

  void addSuccessor(CFGBlock *B, CFGBlock *S, bool IsReachable = true) {
    B->addSuccessor(CFGBlock::AdjacentBlock(S, IsReachable),
                    cfg->getBumpVectorContext());
  }

  /// Add a reachable successor to a block, with the alternate variant that is
  /// unreachable.
  void addSuccessor(CFGBlock *B, CFGBlock *ReachableBlock, CFGBlock *AltBlock) {
    B->addSuccessor(CFGBlock::AdjacentBlock(ReachableBlock, AltBlock),
                    cfg->getBumpVectorContext());
  }

  void appendScopeBegin(CFGBlock *B, const VarDecl *VD, const Stmt *S) {
    if (BuildOpts.AddScopes)
      B->appendScopeBegin(VD, S, cfg->getBumpVectorContext());
  }

  void prependScopeBegin(CFGBlock *B, const VarDecl *VD, const Stmt *S) {
    if (BuildOpts.AddScopes)
      B->prependScopeBegin(VD, S, cfg->getBumpVectorContext());
  }

  void appendScopeEnd(CFGBlock *B, const VarDecl *VD, const Stmt *S) {
    if (BuildOpts.AddScopes)
      B->appendScopeEnd(VD, S, cfg->getBumpVectorContext());
  }

  void prependScopeEnd(CFGBlock *B, const VarDecl *VD, const Stmt *S) {
    if (BuildOpts.AddScopes)
      B->prependScopeEnd(VD, S, cfg->getBumpVectorContext());
  }

  /// Find a relational comparison with an expression evaluating to a
  /// boolean and a constant other than 0 and 1.
  /// e.g. if ((x < y) == 10)
  TryResult checkIncorrectRelationalOperator(const BinaryOperator *B) {
    const Expr *LHSExpr = B->getLHS()->IgnoreParens();
    const Expr *RHSExpr = B->getRHS()->IgnoreParens();

    const IntegerLiteral *IntLiteral = dyn_cast<IntegerLiteral>(LHSExpr);
    const Expr *BoolExpr = RHSExpr;
    bool IntFirst = true;
    if (!IntLiteral) {
      IntLiteral = dyn_cast<IntegerLiteral>(RHSExpr);
      BoolExpr = LHSExpr;
      IntFirst = false;
    }

    if (!IntLiteral || !BoolExpr->isKnownToHaveBooleanValue())
      return TryResult();

    llvm::APInt IntValue = IntLiteral->getValue();
    if ((IntValue == 1) || (IntValue == 0))
      return TryResult();

    bool IntLarger = IntLiteral->getType()->isUnsignedIntegerType() ||
                     !IntValue.isNegative();

    BinaryOperatorKind Bok = B->getOpcode();
    if (Bok == BO_GT || Bok == BO_GE) {
      // Always true for 10 > bool and bool > -1
      // Always false for -1 > bool and bool > 10
      return TryResult(IntFirst == IntLarger);
    } else {
      // Always true for -1 < bool and bool < 10
      // Always false for 10 < bool and bool < -1
      return TryResult(IntFirst != IntLarger);
    }
  }

  /// Find an incorrect equality comparison. Either with an expression
  /// evaluating to a boolean and a constant other than 0 and 1.
  /// e.g. if (!x == 10) or a bitwise and/or operation that always evaluates to
  /// true/false e.q. (x & 8) == 4.
  TryResult checkIncorrectEqualityOperator(const BinaryOperator *B) {
    const Expr *LHSExpr = B->getLHS()->IgnoreParens();
    const Expr *RHSExpr = B->getRHS()->IgnoreParens();

    const IntegerLiteral *IntLiteral = dyn_cast<IntegerLiteral>(LHSExpr);
    const Expr *BoolExpr = RHSExpr;

    if (!IntLiteral) {
      IntLiteral = dyn_cast<IntegerLiteral>(RHSExpr);
      BoolExpr = LHSExpr;
    }

    if (!IntLiteral)
      return TryResult();

    const BinaryOperator *BitOp = dyn_cast<BinaryOperator>(BoolExpr);
    if (BitOp && (BitOp->getOpcode() == BO_And ||
                  BitOp->getOpcode() == BO_Or)) {
      const Expr *LHSExpr2 = BitOp->getLHS()->IgnoreParens();
      const Expr *RHSExpr2 = BitOp->getRHS()->IgnoreParens();

      const IntegerLiteral *IntLiteral2 = dyn_cast<IntegerLiteral>(LHSExpr2);

      if (!IntLiteral2)
        IntLiteral2 = dyn_cast<IntegerLiteral>(RHSExpr2);

      if (!IntLiteral2)
        return TryResult();

      llvm::APInt L1 = IntLiteral->getValue();
      llvm::APInt L2 = IntLiteral2->getValue();
      if ((BitOp->getOpcode() == BO_And && (L2 & L1) != L1) ||
          (BitOp->getOpcode() == BO_Or  && (L2 | L1) != L1)) {
        if (BuildOpts.Observer)
          BuildOpts.Observer->compareBitwiseEquality(B,
                                                     B->getOpcode() != BO_EQ);
        TryResult(B->getOpcode() != BO_EQ);
      }
    } else if (BoolExpr->isKnownToHaveBooleanValue()) {
      llvm::APInt IntValue = IntLiteral->getValue();
      if ((IntValue == 1) || (IntValue == 0)) {
        return TryResult();
      }
      return TryResult(B->getOpcode() != BO_EQ);
    }

    return TryResult();
  }

  TryResult analyzeLogicOperatorCondition(BinaryOperatorKind Relation,
                                          const llvm::APSInt &Value1,
                                          const llvm::APSInt &Value2) {
    assert(Value1.isSigned() == Value2.isSigned());
    switch (Relation) {
      default:
        return TryResult();
      case BO_EQ:
        return TryResult(Value1 == Value2);
      case BO_NE:
        return TryResult(Value1 != Value2);
      case BO_LT:
        return TryResult(Value1 <  Value2);
      case BO_LE:
        return TryResult(Value1 <= Value2);
      case BO_GT:
        return TryResult(Value1 >  Value2);
      case BO_GE:
        return TryResult(Value1 >= Value2);
    }
  }

  /// Find a pair of comparison expressions with or without parentheses
  /// with a shared variable and constants and a logical operator between them
  /// that always evaluates to either true or false.
  /// e.g. if (x != 3 || x != 4)
  TryResult checkIncorrectLogicOperator(const BinaryOperator *B) {
    assert(B->isLogicalOp());
    const BinaryOperator *LHS =
        dyn_cast<BinaryOperator>(B->getLHS()->IgnoreParens());
    const BinaryOperator *RHS =
        dyn_cast<BinaryOperator>(B->getRHS()->IgnoreParens());
    if (!LHS || !RHS)
      return {};

    if (!LHS->isComparisonOp() || !RHS->isComparisonOp())
      return {};

    const DeclRefExpr *Decl1;
    const Expr *Expr1;
    BinaryOperatorKind BO1;
    std::tie(Decl1, BO1, Expr1) = tryNormalizeBinaryOperator(LHS);

    if (!Decl1 || !Expr1)
      return {};

    const DeclRefExpr *Decl2;
    const Expr *Expr2;
    BinaryOperatorKind BO2;
    std::tie(Decl2, BO2, Expr2) = tryNormalizeBinaryOperator(RHS);

    if (!Decl2 || !Expr2)
      return {};

    // Check that it is the same variable on both sides.
    if (Decl1->getDecl() != Decl2->getDecl())
      return {};

    // Make sure the user's intent is clear (e.g. they're comparing against two
    // int literals, or two things from the same enum)
    if (!areExprTypesCompatible(Expr1, Expr2))
      return {};

    Expr::EvalResult L1Result, L2Result;
    if (!Expr1->EvaluateAsInt(L1Result, *Context) ||
        !Expr2->EvaluateAsInt(L2Result, *Context))
      return {};

    llvm::APSInt L1 = L1Result.Val.getInt();
    llvm::APSInt L2 = L2Result.Val.getInt();

    // Can't compare signed with unsigned or with different bit width.
    if (L1.isSigned() != L2.isSigned() || L1.getBitWidth() != L2.getBitWidth())
      return {};

    // Values that will be used to determine if result of logical
    // operator is always true/false
    const llvm::APSInt Values[] = {
      // Value less than both Value1 and Value2
      llvm::APSInt::getMinValue(L1.getBitWidth(), L1.isUnsigned()),
      // L1
      L1,
      // Value between Value1 and Value2
      ((L1 < L2) ? L1 : L2) + llvm::APSInt(llvm::APInt(L1.getBitWidth(), 1),
                              L1.isUnsigned()),
      // L2
      L2,
      // Value greater than both Value1 and Value2
      llvm::APSInt::getMaxValue(L1.getBitWidth(), L1.isUnsigned()),
    };

    // Check whether expression is always true/false by evaluating the following
    // * variable x is less than the smallest literal.
    // * variable x is equal to the smallest literal.
    // * Variable x is between smallest and largest literal.
    // * Variable x is equal to the largest literal.
    // * Variable x is greater than largest literal.
    bool AlwaysTrue = true, AlwaysFalse = true;
    for (const llvm::APSInt &Value : Values) {
      TryResult Res1, Res2;
      Res1 = analyzeLogicOperatorCondition(BO1, Value, L1);
      Res2 = analyzeLogicOperatorCondition(BO2, Value, L2);

      if (!Res1.isKnown() || !Res2.isKnown())
        return {};

      if (B->getOpcode() == BO_LAnd) {
        AlwaysTrue &= (Res1.isTrue() && Res2.isTrue());
        AlwaysFalse &= !(Res1.isTrue() && Res2.isTrue());
      } else {
        AlwaysTrue &= (Res1.isTrue() || Res2.isTrue());
        AlwaysFalse &= !(Res1.isTrue() || Res2.isTrue());
      }
    }

    if (AlwaysTrue || AlwaysFalse) {
      if (BuildOpts.Observer)
        BuildOpts.Observer->compareAlwaysTrue(B, AlwaysTrue);
      return TryResult(AlwaysTrue);
    }
    return {};
  }

  /// Try and evaluate an expression to an integer constant.
  bool tryEvaluate(Expr *S, Expr::EvalResult &outResult) {
    if (!BuildOpts.PruneTriviallyFalseEdges)
      return false;
    return !S->isTypeDependent() &&
           !S->isValueDependent() &&
           S->EvaluateAsRValue(outResult, *Context);
  }

  /// tryEvaluateBool - Try and evaluate the Stmt and return 0 or 1
  /// if we can evaluate to a known value, otherwise return -1.
  TryResult tryEvaluateBool(Expr *S) {
    if (!BuildOpts.PruneTriviallyFalseEdges ||
        S->isTypeDependent() || S->isValueDependent())
      return {};

    if (BinaryOperator *Bop = dyn_cast<BinaryOperator>(S)) {
      if (Bop->isLogicalOp()) {
        // Check the cache first.
        CachedBoolEvalsTy::iterator I = CachedBoolEvals.find(S);
        if (I != CachedBoolEvals.end())
          return I->second; // already in map;

        // Retrieve result at first, or the map might be updated.
        TryResult Result = evaluateAsBooleanConditionNoCache(S);
        CachedBoolEvals[S] = Result; // update or insert
        return Result;
      }
      else {
        switch (Bop->getOpcode()) {
          default: break;
          // For 'x & 0' and 'x * 0', we can determine that
          // the value is always false.
          case BO_Mul:
          case BO_And: {
            // If either operand is zero, we know the value
            // must be false.
            Expr::EvalResult LHSResult;
            if (Bop->getLHS()->EvaluateAsInt(LHSResult, *Context)) {
              llvm::APSInt IntVal = LHSResult.Val.getInt();
              if (!IntVal.getBoolValue()) {
                return TryResult(false);
              }
            }
            Expr::EvalResult RHSResult;
            if (Bop->getRHS()->EvaluateAsInt(RHSResult, *Context)) {
              llvm::APSInt IntVal = RHSResult.Val.getInt();
              if (!IntVal.getBoolValue()) {
                return TryResult(false);
              }
            }
          }
          break;
        }
      }
    }

    return evaluateAsBooleanConditionNoCache(S);
  }

  /// Evaluate as boolean \param E without using the cache.
  TryResult evaluateAsBooleanConditionNoCache(Expr *E) {
    if (BinaryOperator *Bop = dyn_cast<BinaryOperator>(E)) {
      if (Bop->isLogicalOp()) {
        TryResult LHS = tryEvaluateBool(Bop->getLHS());
        if (LHS.isKnown()) {
          // We were able to evaluate the LHS, see if we can get away with not
          // evaluating the RHS: 0 && X -> 0, 1 || X -> 1
          if (LHS.isTrue() == (Bop->getOpcode() == BO_LOr))
            return LHS.isTrue();

          TryResult RHS = tryEvaluateBool(Bop->getRHS());
          if (RHS.isKnown()) {
            if (Bop->getOpcode() == BO_LOr)
              return LHS.isTrue() || RHS.isTrue();
            else
              return LHS.isTrue() && RHS.isTrue();
          }
        } else {
          TryResult RHS = tryEvaluateBool(Bop->getRHS());
          if (RHS.isKnown()) {
            // We can't evaluate the LHS; however, sometimes the result
            // is determined by the RHS: X && 0 -> 0, X || 1 -> 1.
            if (RHS.isTrue() == (Bop->getOpcode() == BO_LOr))
              return RHS.isTrue();
          } else {
            TryResult BopRes = checkIncorrectLogicOperator(Bop);
            if (BopRes.isKnown())
              return BopRes.isTrue();
          }
        }

        return {};
      } else if (Bop->isEqualityOp()) {
          TryResult BopRes = checkIncorrectEqualityOperator(Bop);
          if (BopRes.isKnown())
            return BopRes.isTrue();
      } else if (Bop->isRelationalOp()) {
        TryResult BopRes = checkIncorrectRelationalOperator(Bop);
        if (BopRes.isKnown())
          return BopRes.isTrue();
      }
    }

    bool Result;
    if (E->EvaluateAsBooleanCondition(Result, *Context))
      return Result;

    return {};
  }

  bool hasTrivialDestructor(VarDecl *VD);
};

} // namespace

inline bool AddStmtChoice::alwaysAdd(CFGBuilder &builder,
                                     const Stmt *stmt) const {
  return builder.alwaysAdd(stmt) || kind == AlwaysAdd;
}

bool CFGBuilder::alwaysAdd(const Stmt *stmt) {
  bool shouldAdd = BuildOpts.alwaysAdd(stmt);

  if (!BuildOpts.forcedBlkExprs)
    return shouldAdd;

  if (lastLookup == stmt) {
    if (cachedEntry) {
      assert(cachedEntry->first == stmt);
      return true;
    }
    return shouldAdd;
  }

  lastLookup = stmt;

  // Perform the lookup!
  CFG::BuildOptions::ForcedBlkExprs *fb = *BuildOpts.forcedBlkExprs;

  if (!fb) {
    // No need to update 'cachedEntry', since it will always be null.
    assert(!cachedEntry);
    return shouldAdd;
  }

  CFG::BuildOptions::ForcedBlkExprs::iterator itr = fb->find(stmt);
  if (itr == fb->end()) {
    cachedEntry = nullptr;
    return shouldAdd;
  }

  cachedEntry = &*itr;
  return true;
}

// FIXME: Add support for dependent-sized array types in C++?
// Does it even make sense to build a CFG for an uninstantiated template?
static const VariableArrayType *FindVA(const Type *t) {
  while (const ArrayType *vt = dyn_cast<ArrayType>(t)) {
    if (const VariableArrayType *vat = dyn_cast<VariableArrayType>(vt))
      if (vat->getSizeExpr())
        return vat;

    t = vt->getElementType().getTypePtr();
  }

  return nullptr;
}

void CFGBuilder::consumeConstructionContext(
    const ConstructionContextLayer *Layer, Expr *E) {
  assert((isa<CXXConstructExpr>(E) || isa<CallExpr>(E) ||
          isa<ObjCMessageExpr>(E)) && "Expression cannot construct an object!");
  if (const ConstructionContextLayer *PreviouslyStoredLayer =
          ConstructionContextMap.lookup(E)) {
    (void)PreviouslyStoredLayer;
    // We might have visited this child when we were finding construction
    // contexts within its parents.
    assert(PreviouslyStoredLayer->isStrictlyMoreSpecificThan(Layer) &&
           "Already within a different construction context!");
  } else {
    ConstructionContextMap[E] = Layer;
  }
}

void CFGBuilder::findConstructionContexts(
    const ConstructionContextLayer *Layer, Stmt *Child) {
  if (!BuildOpts.AddRichCXXConstructors)
    return;

  if (!Child)
    return;

  auto withExtraLayer = [this, Layer](const ConstructionContextItem &Item) {
    return ConstructionContextLayer::create(cfg->getBumpVectorContext(), Item,
                                            Layer);
  };

  switch(Child->getStmtClass()) {
  case Stmt::CXXConstructExprClass:
  case Stmt::CXXTemporaryObjectExprClass: {
    // Support pre-C++17 copy elision AST.
    auto *CE = cast<CXXConstructExpr>(Child);
    if (BuildOpts.MarkElidedCXXConstructors && CE->isElidable()) {
      findConstructionContexts(withExtraLayer(CE), CE->getArg(0));
    }

    consumeConstructionContext(Layer, CE);
    break;
  }
  // FIXME: This, like the main visit, doesn't support CUDAKernelCallExpr.
  // FIXME: An isa<> would look much better but this whole switch is a
  // workaround for an internal compiler error in MSVC 2015 (see r326021).
  case Stmt::CallExprClass:
  case Stmt::CXXMemberCallExprClass:
  case Stmt::CXXOperatorCallExprClass:
  case Stmt::UserDefinedLiteralClass:
  case Stmt::ObjCMessageExprClass: {
    auto *E = cast<Expr>(Child);
    if (CFGCXXRecordTypedCall::isCXXRecordTypedCall(E))
      consumeConstructionContext(Layer, E);
    break;
  }
  case Stmt::ExprWithCleanupsClass: {
    auto *Cleanups = cast<ExprWithCleanups>(Child);
    findConstructionContexts(Layer, Cleanups->getSubExpr());
    break;
  }
  case Stmt::CXXFunctionalCastExprClass: {
    auto *Cast = cast<CXXFunctionalCastExpr>(Child);
    findConstructionContexts(Layer, Cast->getSubExpr());
    break;
  }
  case Stmt::ImplicitCastExprClass: {
    auto *Cast = cast<ImplicitCastExpr>(Child);
    // Should we support other implicit cast kinds?
    switch (Cast->getCastKind()) {
    case CK_NoOp:
    case CK_ConstructorConversion:
      findConstructionContexts(Layer, Cast->getSubExpr());
      break;
    default:
      break;
    }
    break;
  }
  case Stmt::CXXBindTemporaryExprClass: {
    auto *BTE = cast<CXXBindTemporaryExpr>(Child);
    findConstructionContexts(withExtraLayer(BTE), BTE->getSubExpr());
    break;
  }
  case Stmt::MaterializeTemporaryExprClass: {
    // Normally we don't want to search in MaterializeTemporaryExpr because
    // it indicates the beginning of a temporary object construction context,
    // so it shouldn't be found in the middle. However, if it is the beginning
    // of an elidable copy or move construction context, we need to include it.
    if (Layer->getItem().getKind() ==
        ConstructionContextItem::ElidableConstructorKind) {
      auto *MTE = cast<MaterializeTemporaryExpr>(Child);
      findConstructionContexts(withExtraLayer(MTE), MTE->GetTemporaryExpr());
    }
    break;
  }
  case Stmt::ConditionalOperatorClass: {
    auto *CO = cast<ConditionalOperator>(Child);
    if (Layer->getItem().getKind() !=
        ConstructionContextItem::MaterializationKind) {
      // If the object returned by the conditional operator is not going to be a
      // temporary object that needs to be immediately materialized, then
      // it must be C++17 with its mandatory copy elision. Do not yet promise
      // to support this case.
      assert(!CO->getType()->getAsCXXRecordDecl() || CO->isGLValue() ||
             Context->getLangOpts().CPlusPlus17);
      break;
    }
    findConstructionContexts(Layer, CO->getLHS());
    findConstructionContexts(Layer, CO->getRHS());
    break;
  }
  default:
    break;
  }
}

void CFGBuilder::cleanupConstructionContext(Expr *E) {
  assert(BuildOpts.AddRichCXXConstructors &&
         "We should not be managing construction contexts!");
  assert(ConstructionContextMap.count(E) &&
         "Cannot exit construction context without the context!");
  ConstructionContextMap.erase(E);
}


/// BuildCFG - Constructs a CFG from an AST (a Stmt*).  The AST can represent an
///  arbitrary statement.  Examples include a single expression or a function
///  body (compound statement).  The ownership of the returned CFG is
///  transferred to the caller.  If CFG construction fails, this method returns
///  NULL.
std::unique_ptr<CFG> CFGBuilder::buildCFG(const Decl *D, Stmt *Statement) {
  assert(cfg.get());
  if (!Statement)
    return nullptr;

  // Create an empty block that will serve as the exit block for the CFG.  Since
  // this is the first block added to the CFG, it will be implicitly registered
  // as the exit block.
  Succ = createBlock();
  assert(Succ == &cfg->getExit());
  Block = nullptr;  // the EXIT block is empty.  Create all other blocks lazily.

  assert(!(BuildOpts.AddImplicitDtors && BuildOpts.AddLifetime) &&
         "AddImplicitDtors and AddLifetime cannot be used at the same time");

  if (BuildOpts.AddImplicitDtors)
    if (const CXXDestructorDecl *DD = dyn_cast_or_null<CXXDestructorDecl>(D))
      addImplicitDtorsForDestructor(DD);

  // Visit the statements and create the CFG.
  CFGBlock *B = addStmt(Statement);

  if (badCFG)
    return nullptr;

  // For C++ constructor add initializers to CFG.
  if (const CXXConstructorDecl *CD = dyn_cast_or_null<CXXConstructorDecl>(D)) {
    for (auto *I : llvm::reverse(CD->inits())) {
      B = addInitializer(I);
      if (badCFG)
        return nullptr;
    }
  }

  if (B)
    Succ = B;

  // Backpatch the gotos whose label -> block mappings we didn't know when we
  // encountered them.
  for (BackpatchBlocksTy::iterator I = BackpatchBlocks.begin(),
                                   E = BackpatchBlocks.end(); I != E; ++I ) {

    CFGBlock *B = I->block;
    const GotoStmt *G = cast<GotoStmt>(B->getTerminator());
    LabelMapTy::iterator LI = LabelMap.find(G->getLabel());

    // If there is no target for the goto, then we are looking at an
    // incomplete AST.  Handle this by not registering a successor.
    if (LI == LabelMap.end()) continue;

    JumpTarget JT = LI->second;
    prependAutomaticObjLifetimeWithTerminator(B, I->scopePosition,
                                              JT.scopePosition);
    prependAutomaticObjDtorsWithTerminator(B, I->scopePosition,
                                           JT.scopePosition);
    const VarDecl *VD = prependAutomaticObjScopeEndWithTerminator(
        B, I->scopePosition, JT.scopePosition);
    appendScopeBegin(JT.block, VD, G);
    addSuccessor(B, JT.block);
  }

  // Add successors to the Indirect Goto Dispatch block (if we have one).
  if (CFGBlock *B = cfg->getIndirectGotoBlock())
    for (LabelSetTy::iterator I = AddressTakenLabels.begin(),
                              E = AddressTakenLabels.end(); I != E; ++I ) {
      // Lookup the target block.
      LabelMapTy::iterator LI = LabelMap.find(*I);

      // If there is no target block that contains label, then we are looking
      // at an incomplete AST.  Handle this by not registering a successor.
      if (LI == LabelMap.end()) continue;

      addSuccessor(B, LI->second.block);
    }

  // Create an empty entry block that has no predecessors.
  cfg->setEntry(createBlock());

  if (BuildOpts.AddRichCXXConstructors)
    assert(ConstructionContextMap.empty() &&
           "Not all construction contexts were cleaned up!");

  return std::move(cfg);
}

/// createBlock - Used to lazily create blocks that are connected
///  to the current (global) succcessor.
CFGBlock *CFGBuilder::createBlock(bool add_successor) {
  CFGBlock *B = cfg->createBlock();
  if (add_successor && Succ)
    addSuccessor(B, Succ);
  return B;
}

/// createNoReturnBlock - Used to create a block is a 'noreturn' point in the
/// CFG. It is *not* connected to the current (global) successor, and instead
/// directly tied to the exit block in order to be reachable.
CFGBlock *CFGBuilder::createNoReturnBlock() {
  CFGBlock *B = createBlock(false);
  B->setHasNoReturnElement();
  addSuccessor(B, &cfg->getExit(), Succ);
  return B;
}

/// addInitializer - Add C++ base or member initializer element to CFG.
CFGBlock *CFGBuilder::addInitializer(CXXCtorInitializer *I) {
  if (!BuildOpts.AddInitializers)
    return Block;

  bool HasTemporaries = false;

  // Destructors of temporaries in initialization expression should be called
  // after initialization finishes.
  Expr *Init = I->getInit();
  if (Init) {
    HasTemporaries = isa<ExprWithCleanups>(Init);

    if (BuildOpts.AddTemporaryDtors && HasTemporaries) {
      // Generate destructors for temporaries in initialization expression.
      TempDtorContext Context;
      VisitForTemporaryDtors(cast<ExprWithCleanups>(Init)->getSubExpr(),
                             /*BindToTemporary=*/false, Context);
    }
  }

  autoCreateBlock();
  appendInitializer(Block, I);

  if (Init) {
    findConstructionContexts(
        ConstructionContextLayer::create(cfg->getBumpVectorContext(), I),
        Init);

    if (HasTemporaries) {
      // For expression with temporaries go directly to subexpression to omit
      // generating destructors for the second time.
      return Visit(cast<ExprWithCleanups>(Init)->getSubExpr());
    }
    if (BuildOpts.AddCXXDefaultInitExprInCtors) {
      if (CXXDefaultInitExpr *Default = dyn_cast<CXXDefaultInitExpr>(Init)) {
        // In general, appending the expression wrapped by a CXXDefaultInitExpr
        // may cause the same Expr to appear more than once in the CFG. Doing it
        // here is safe because there's only one initializer per field.
        autoCreateBlock();
        appendStmt(Block, Default);
        if (Stmt *Child = Default->getExpr())
          if (CFGBlock *R = Visit(Child))
            Block = R;
        return Block;
      }
    }
    return Visit(Init);
  }

  return Block;
}

/// Retrieve the type of the temporary object whose lifetime was
/// extended by a local reference with the given initializer.
static QualType getReferenceInitTemporaryType(const Expr *Init,
                                              bool *FoundMTE = nullptr) {
  while (true) {
    // Skip parentheses.
    Init = Init->IgnoreParens();

    // Skip through cleanups.
    if (const ExprWithCleanups *EWC = dyn_cast<ExprWithCleanups>(Init)) {
      Init = EWC->getSubExpr();
      continue;
    }

    // Skip through the temporary-materialization expression.
    if (const MaterializeTemporaryExpr *MTE
          = dyn_cast<MaterializeTemporaryExpr>(Init)) {
      Init = MTE->GetTemporaryExpr();
      if (FoundMTE)
        *FoundMTE = true;
      continue;
    }

    // Skip sub-object accesses into rvalues.
    SmallVector<const Expr *, 2> CommaLHSs;
    SmallVector<SubobjectAdjustment, 2> Adjustments;
    const Expr *SkippedInit =
        Init->skipRValueSubobjectAdjustments(CommaLHSs, Adjustments);
    if (SkippedInit != Init) {
      Init = SkippedInit;
      continue;
    }

    break;
  }

  return Init->getType();
}

// TODO: Support adding LoopExit element to the CFG in case where the loop is
// ended by ReturnStmt, GotoStmt or ThrowExpr.
void CFGBuilder::addLoopExit(const Stmt *LoopStmt){
  if(!BuildOpts.AddLoopExit)
    return;
  autoCreateBlock();
  appendLoopExit(Block, LoopStmt);
}

void CFGBuilder::getDeclsWithEndedScope(LocalScope::const_iterator B,
                                        LocalScope::const_iterator E, Stmt *S) {
  if (!BuildOpts.AddScopes)
    return;

  if (B == E)
    return;

  // To go from B to E, one first goes up the scopes from B to P
  // then sideways in one scope from P to P' and then down
  // the scopes from P' to E.
  // The lifetime of all objects between B and P end.
  LocalScope::const_iterator P = B.shared_parent(E);
  int Dist = B.distance(P);
  if (Dist <= 0)
    return;

  for (LocalScope::const_iterator I = B; I != P; ++I)
    if (I.pointsToFirstDeclaredVar())
      DeclsWithEndedScope.insert(*I);
}

void CFGBuilder::addAutomaticObjHandling(LocalScope::const_iterator B,
                                         LocalScope::const_iterator E,
                                         Stmt *S) {
  getDeclsWithEndedScope(B, E, S);
  if (BuildOpts.AddScopes)
    addScopesEnd(B, E, S);
  if (BuildOpts.AddImplicitDtors)
    addAutomaticObjDtors(B, E, S);
  if (BuildOpts.AddLifetime)
    addLifetimeEnds(B, E, S);
}

/// Add to current block automatic objects that leave the scope.
void CFGBuilder::addLifetimeEnds(LocalScope::const_iterator B,
                                 LocalScope::const_iterator E, Stmt *S) {
  if (!BuildOpts.AddLifetime)
    return;

  if (B == E)
    return;

  // To go from B to E, one first goes up the scopes from B to P
  // then sideways in one scope from P to P' and then down
  // the scopes from P' to E.
  // The lifetime of all objects between B and P end.
  LocalScope::const_iterator P = B.shared_parent(E);
  int dist = B.distance(P);
  if (dist <= 0)
    return;

  // We need to perform the scope leaving in reverse order
  SmallVector<VarDecl *, 10> DeclsTrivial;
  SmallVector<VarDecl *, 10> DeclsNonTrivial;
  DeclsTrivial.reserve(dist);
  DeclsNonTrivial.reserve(dist);

  for (LocalScope::const_iterator I = B; I != P; ++I)
    if (hasTrivialDestructor(*I))
      DeclsTrivial.push_back(*I);
    else
      DeclsNonTrivial.push_back(*I);

  autoCreateBlock();
  // object with trivial destructor end their lifetime last (when storage
  // duration ends)
  for (SmallVectorImpl<VarDecl *>::reverse_iterator I = DeclsTrivial.rbegin(),
                                                    E = DeclsTrivial.rend();
       I != E; ++I)
    appendLifetimeEnds(Block, *I, S);

  for (SmallVectorImpl<VarDecl *>::reverse_iterator
           I = DeclsNonTrivial.rbegin(),
           E = DeclsNonTrivial.rend();
       I != E; ++I)
    appendLifetimeEnds(Block, *I, S);
}

/// Add to current block markers for ending scopes.
void CFGBuilder::addScopesEnd(LocalScope::const_iterator B,
                              LocalScope::const_iterator E, Stmt *S) {
  // If implicit destructors are enabled, we'll add scope ends in
  // addAutomaticObjDtors.
  if (BuildOpts.AddImplicitDtors)
    return;

  autoCreateBlock();

  for (auto I = DeclsWithEndedScope.rbegin(), E = DeclsWithEndedScope.rend();
       I != E; ++I)
    appendScopeEnd(Block, *I, S);

  return;
}

/// addAutomaticObjDtors - Add to current block automatic objects destructors
/// for objects in range of local scope positions. Use S as trigger statement
/// for destructors.
void CFGBuilder::addAutomaticObjDtors(LocalScope::const_iterator B,
                                      LocalScope::const_iterator E, Stmt *S) {
  if (!BuildOpts.AddImplicitDtors)
    return;

  if (B == E)
    return;

  // We need to append the destructors in reverse order, but any one of them
  // may be a no-return destructor which changes the CFG. As a result, buffer
  // this sequence up and replay them in reverse order when appending onto the
  // CFGBlock(s).
  SmallVector<VarDecl*, 10> Decls;
  Decls.reserve(B.distance(E));
  for (LocalScope::const_iterator I = B; I != E; ++I)
    Decls.push_back(*I);

  for (SmallVectorImpl<VarDecl*>::reverse_iterator I = Decls.rbegin(),
                                                   E = Decls.rend();
       I != E; ++I) {
    if (hasTrivialDestructor(*I)) {
      // If AddScopes is enabled and *I is a first variable in a scope, add a
      // ScopeEnd marker in a Block.
      if (BuildOpts.AddScopes && DeclsWithEndedScope.count(*I)) {
        autoCreateBlock();
        appendScopeEnd(Block, *I, S);
      }
      continue;
    }
    // If this destructor is marked as a no-return destructor, we need to
    // create a new block for the destructor which does not have as a successor
    // anything built thus far: control won't flow out of this block.
    QualType Ty = (*I)->getType();
    if (Ty->isReferenceType()) {
      Ty = getReferenceInitTemporaryType((*I)->getInit());
    }
    Ty = Context->getBaseElementType(Ty);

    if (Ty->getAsCXXRecordDecl()->isAnyDestructorNoReturn())
      Block = createNoReturnBlock();
    else
      autoCreateBlock();

    // Add ScopeEnd just after automatic obj destructor.
    if (BuildOpts.AddScopes && DeclsWithEndedScope.count(*I))
      appendScopeEnd(Block, *I, S);
    appendAutomaticObjDtor(Block, *I, S);
  }
}

/// addImplicitDtorsForDestructor - Add implicit destructors generated for
/// base and member objects in destructor.
void CFGBuilder::addImplicitDtorsForDestructor(const CXXDestructorDecl *DD) {
  assert(BuildOpts.AddImplicitDtors &&
         "Can be called only when dtors should be added");
  const CXXRecordDecl *RD = DD->getParent();

  // At the end destroy virtual base objects.
  for (const auto &VI : RD->vbases()) {
    const CXXRecordDecl *CD = VI.getType()->getAsCXXRecordDecl();
    if (!CD->hasTrivialDestructor()) {
      autoCreateBlock();
      appendBaseDtor(Block, &VI);
    }
  }

  // Before virtual bases destroy direct base objects.
  for (const auto &BI : RD->bases()) {
    if (!BI.isVirtual()) {
      const CXXRecordDecl *CD = BI.getType()->getAsCXXRecordDecl();
      if (!CD->hasTrivialDestructor()) {
        autoCreateBlock();
        appendBaseDtor(Block, &BI);
      }
    }
  }

  // First destroy member objects.
  for (auto *FI : RD->fields()) {
    // Check for constant size array. Set type to array element type.
    QualType QT = FI->getType();
    if (const ConstantArrayType *AT = Context->getAsConstantArrayType(QT)) {
      if (AT->getSize() == 0)
        continue;
      QT = AT->getElementType();
    }

    if (const CXXRecordDecl *CD = QT->getAsCXXRecordDecl())
      if (!CD->hasTrivialDestructor()) {
        autoCreateBlock();
        appendMemberDtor(Block, FI);
      }
  }
}

/// createOrReuseLocalScope - If Scope is NULL create new LocalScope. Either
/// way return valid LocalScope object.
LocalScope* CFGBuilder::createOrReuseLocalScope(LocalScope* Scope) {
  if (Scope)
    return Scope;
  llvm::BumpPtrAllocator &alloc = cfg->getAllocator();
  return new (alloc.Allocate<LocalScope>())
      LocalScope(BumpVectorContext(alloc), ScopePos);
}

/// addLocalScopeForStmt - Add LocalScope to local scopes tree for statement
/// that should create implicit scope (e.g. if/else substatements).
void CFGBuilder::addLocalScopeForStmt(Stmt *S) {
  if (!BuildOpts.AddImplicitDtors && !BuildOpts.AddLifetime &&
      !BuildOpts.AddScopes)
    return;

  LocalScope *Scope = nullptr;

  // For compound statement we will be creating explicit scope.
  if (CompoundStmt *CS = dyn_cast<CompoundStmt>(S)) {
    for (auto *BI : CS->body()) {
      Stmt *SI = BI->stripLabelLikeStatements();
      if (DeclStmt *DS = dyn_cast<DeclStmt>(SI))
        Scope = addLocalScopeForDeclStmt(DS, Scope);
    }
    return;
  }

  // For any other statement scope will be implicit and as such will be
  // interesting only for DeclStmt.
  if (DeclStmt *DS = dyn_cast<DeclStmt>(S->stripLabelLikeStatements()))
    addLocalScopeForDeclStmt(DS);
}

/// addLocalScopeForDeclStmt - Add LocalScope for declaration statement. Will
/// reuse Scope if not NULL.
LocalScope* CFGBuilder::addLocalScopeForDeclStmt(DeclStmt *DS,
                                                 LocalScope* Scope) {
  if (!BuildOpts.AddImplicitDtors && !BuildOpts.AddLifetime &&
      !BuildOpts.AddScopes)
    return Scope;

  for (auto *DI : DS->decls())
    if (VarDecl *VD = dyn_cast<VarDecl>(DI))
      Scope = addLocalScopeForVarDecl(VD, Scope);
  return Scope;
}

bool CFGBuilder::hasTrivialDestructor(VarDecl *VD) {
  // Check for const references bound to temporary. Set type to pointee.
  QualType QT = VD->getType();
  if (QT->isReferenceType()) {
    // Attempt to determine whether this declaration lifetime-extends a
    // temporary.
    //
    // FIXME: This is incorrect. Non-reference declarations can lifetime-extend
    // temporaries, and a single declaration can extend multiple temporaries.
    // We should look at the storage duration on each nested
    // MaterializeTemporaryExpr instead.

    const Expr *Init = VD->getInit();
    if (!Init) {
      // Probably an exception catch-by-reference variable.
      // FIXME: It doesn't really mean that the object has a trivial destructor.
      // Also are there other cases?
      return true;
    }

    // Lifetime-extending a temporary?
    bool FoundMTE = false;
    QT = getReferenceInitTemporaryType(Init, &FoundMTE);
    if (!FoundMTE)
      return true;
  }

  // Check for constant size array. Set type to array element type.
  while (const ConstantArrayType *AT = Context->getAsConstantArrayType(QT)) {
    if (AT->getSize() == 0)
      return true;
    QT = AT->getElementType();
  }

  // Check if type is a C++ class with non-trivial destructor.
  if (const CXXRecordDecl *CD = QT->getAsCXXRecordDecl())
    return !CD->hasDefinition() || CD->hasTrivialDestructor();
  return true;
}

/// addLocalScopeForVarDecl - Add LocalScope for variable declaration. It will
/// create add scope for automatic objects and temporary objects bound to
/// const reference. Will reuse Scope if not NULL.
LocalScope* CFGBuilder::addLocalScopeForVarDecl(VarDecl *VD,
                                                LocalScope* Scope) {
  assert(!(BuildOpts.AddImplicitDtors && BuildOpts.AddLifetime) &&
         "AddImplicitDtors and AddLifetime cannot be used at the same time");
  if (!BuildOpts.AddImplicitDtors && !BuildOpts.AddLifetime &&
      !BuildOpts.AddScopes)
    return Scope;

  // Check if variable is local.
  switch (VD->getStorageClass()) {
  case SC_None:
  case SC_Auto:
  case SC_Register:
    break;
  default: return Scope;
  }

  if (BuildOpts.AddImplicitDtors) {
    if (!hasTrivialDestructor(VD) || BuildOpts.AddScopes) {
      // Add the variable to scope
      Scope = createOrReuseLocalScope(Scope);
      Scope->addVar(VD);
      ScopePos = Scope->begin();
    }
    return Scope;
  }

  assert(BuildOpts.AddLifetime);
  // Add the variable to scope
  Scope = createOrReuseLocalScope(Scope);
  Scope->addVar(VD);
  ScopePos = Scope->begin();
  return Scope;
}

/// addLocalScopeAndDtors - For given statement add local scope for it and
/// add destructors that will cleanup the scope. Will reuse Scope if not NULL.
void CFGBuilder::addLocalScopeAndDtors(Stmt *S) {
  LocalScope::const_iterator scopeBeginPos = ScopePos;
  addLocalScopeForStmt(S);
  addAutomaticObjHandling(ScopePos, scopeBeginPos, S);
}

/// prependAutomaticObjDtorsWithTerminator - Prepend destructor CFGElements for
/// variables with automatic storage duration to CFGBlock's elements vector.
/// Elements will be prepended to physical beginning of the vector which
/// happens to be logical end. Use blocks terminator as statement that specifies
/// destructors call site.
/// FIXME: This mechanism for adding automatic destructors doesn't handle
/// no-return destructors properly.
void CFGBuilder::prependAutomaticObjDtorsWithTerminator(CFGBlock *Blk,
    LocalScope::const_iterator B, LocalScope::const_iterator E) {
  if (!BuildOpts.AddImplicitDtors)
    return;
  BumpVectorContext &C = cfg->getBumpVectorContext();
  CFGBlock::iterator InsertPos
    = Blk->beginAutomaticObjDtorsInsert(Blk->end(), B.distance(E), C);
  for (LocalScope::const_iterator I = B; I != E; ++I)
    InsertPos = Blk->insertAutomaticObjDtor(InsertPos, *I,
                                            Blk->getTerminator());
}

/// prependAutomaticObjLifetimeWithTerminator - Prepend lifetime CFGElements for
/// variables with automatic storage duration to CFGBlock's elements vector.
/// Elements will be prepended to physical beginning of the vector which
/// happens to be logical end. Use blocks terminator as statement that specifies
/// where lifetime ends.
void CFGBuilder::prependAutomaticObjLifetimeWithTerminator(
    CFGBlock *Blk, LocalScope::const_iterator B, LocalScope::const_iterator E) {
  if (!BuildOpts.AddLifetime)
    return;
  BumpVectorContext &C = cfg->getBumpVectorContext();
  CFGBlock::iterator InsertPos =
      Blk->beginLifetimeEndsInsert(Blk->end(), B.distance(E), C);
  for (LocalScope::const_iterator I = B; I != E; ++I)
    InsertPos = Blk->insertLifetimeEnds(InsertPos, *I, Blk->getTerminator());
}

/// prependAutomaticObjScopeEndWithTerminator - Prepend scope end CFGElements for
/// variables with automatic storage duration to CFGBlock's elements vector.
/// Elements will be prepended to physical beginning of the vector which
/// happens to be logical end. Use blocks terminator as statement that specifies
/// where scope ends.
const VarDecl *
CFGBuilder::prependAutomaticObjScopeEndWithTerminator(
    CFGBlock *Blk, LocalScope::const_iterator B, LocalScope::const_iterator E) {
  if (!BuildOpts.AddScopes)
    return nullptr;
  BumpVectorContext &C = cfg->getBumpVectorContext();
  CFGBlock::iterator InsertPos =
      Blk->beginScopeEndInsert(Blk->end(), 1, C);
  LocalScope::const_iterator PlaceToInsert = B;
  for (LocalScope::const_iterator I = B; I != E; ++I)
    PlaceToInsert = I;
  Blk->insertScopeEnd(InsertPos, *PlaceToInsert, Blk->getTerminator());
  return *PlaceToInsert;
}

/// Visit - Walk the subtree of a statement and add extra
///   blocks for ternary operators, &&, and ||.  We also process "," and
///   DeclStmts (which may contain nested control-flow).
CFGBlock *CFGBuilder::Visit(Stmt * S, AddStmtChoice asc) {
  if (!S) {
    badCFG = true;
    return nullptr;
  }

  if (Expr *E = dyn_cast<Expr>(S))
    S = E->IgnoreParens();

  switch (S->getStmtClass()) {
    default:
      return VisitStmt(S, asc);

    case Stmt::AddrLabelExprClass:
      return VisitAddrLabelExpr(cast<AddrLabelExpr>(S), asc);

    case Stmt::BinaryConditionalOperatorClass:
      return VisitConditionalOperator(cast<BinaryConditionalOperator>(S), asc);

    case Stmt::BinaryOperatorClass:
      return VisitBinaryOperator(cast<BinaryOperator>(S), asc);

    case Stmt::BlockExprClass:
      return VisitBlockExpr(cast<BlockExpr>(S), asc);

    case Stmt::BreakStmtClass:
      return VisitBreakStmt(cast<BreakStmt>(S));

    case Stmt::CallExprClass:
    case Stmt::CXXOperatorCallExprClass:
    case Stmt::CXXMemberCallExprClass:
    case Stmt::UserDefinedLiteralClass:
      return VisitCallExpr(cast<CallExpr>(S), asc);

    case Stmt::CaseStmtClass:
      return VisitCaseStmt(cast<CaseStmt>(S));

    case Stmt::ChooseExprClass:
      return VisitChooseExpr(cast<ChooseExpr>(S), asc);

    case Stmt::CompoundStmtClass:
      return VisitCompoundStmt(cast<CompoundStmt>(S));

    case Stmt::ConditionalOperatorClass:
      return VisitConditionalOperator(cast<ConditionalOperator>(S), asc);

    case Stmt::ContinueStmtClass:
      return VisitContinueStmt(cast<ContinueStmt>(S));

    case Stmt::CXXCatchStmtClass:
      return VisitCXXCatchStmt(cast<CXXCatchStmt>(S));

    case Stmt::ExprWithCleanupsClass:
      return VisitExprWithCleanups(cast<ExprWithCleanups>(S), asc);

    case Stmt::CXXDefaultArgExprClass:
    case Stmt::CXXDefaultInitExprClass:
      // FIXME: The expression inside a CXXDefaultArgExpr is owned by the
      // called function's declaration, not by the caller. If we simply add
      // this expression to the CFG, we could end up with the same Expr
      // appearing multiple times.
      // PR13385 / <rdar://problem/12156507>
      //
      // It's likewise possible for multiple CXXDefaultInitExprs for the same
      // expression to be used in the same function (through aggregate
      // initialization).
      return VisitStmt(S, asc);

    case Stmt::CXXBindTemporaryExprClass:
      return VisitCXXBindTemporaryExpr(cast<CXXBindTemporaryExpr>(S), asc);

    case Stmt::CXXConstructExprClass:
      return VisitCXXConstructExpr(cast<CXXConstructExpr>(S), asc);

    case Stmt::CXXNewExprClass:
      return VisitCXXNewExpr(cast<CXXNewExpr>(S), asc);

    case Stmt::CXXDeleteExprClass:
      return VisitCXXDeleteExpr(cast<CXXDeleteExpr>(S), asc);

    case Stmt::CXXFunctionalCastExprClass:
      return VisitCXXFunctionalCastExpr(cast<CXXFunctionalCastExpr>(S), asc);

    case Stmt::CXXTemporaryObjectExprClass:
      return VisitCXXTemporaryObjectExpr(cast<CXXTemporaryObjectExpr>(S), asc);

    case Stmt::CXXThrowExprClass:
      return VisitCXXThrowExpr(cast<CXXThrowExpr>(S));

    case Stmt::CXXTryStmtClass:
      return VisitCXXTryStmt(cast<CXXTryStmt>(S));

    case Stmt::CXXForRangeStmtClass:
      return VisitCXXForRangeStmt(cast<CXXForRangeStmt>(S));

    case Stmt::DeclStmtClass:
      return VisitDeclStmt(cast<DeclStmt>(S));

    case Stmt::DefaultStmtClass:
      return VisitDefaultStmt(cast<DefaultStmt>(S));

    case Stmt::DoStmtClass:
      return VisitDoStmt(cast<DoStmt>(S));

    case Stmt::ForStmtClass:
      return VisitForStmt(cast<ForStmt>(S));

    case Stmt::GotoStmtClass:
      return VisitGotoStmt(cast<GotoStmt>(S));

    case Stmt::IfStmtClass:
      return VisitIfStmt(cast<IfStmt>(S));

    case Stmt::ImplicitCastExprClass:
      return VisitImplicitCastExpr(cast<ImplicitCastExpr>(S), asc);

    case Stmt::ConstantExprClass:
      return VisitConstantExpr(cast<ConstantExpr>(S), asc);

    case Stmt::IndirectGotoStmtClass:
      return VisitIndirectGotoStmt(cast<IndirectGotoStmt>(S));

    case Stmt::LabelStmtClass:
      return VisitLabelStmt(cast<LabelStmt>(S));

    case Stmt::LambdaExprClass:
      return VisitLambdaExpr(cast<LambdaExpr>(S), asc);

    case Stmt::MaterializeTemporaryExprClass:
      return VisitMaterializeTemporaryExpr(cast<MaterializeTemporaryExpr>(S),
                                           asc);

    case Stmt::MemberExprClass:
      return VisitMemberExpr(cast<MemberExpr>(S), asc);

    case Stmt::NullStmtClass:
      return Block;

    case Stmt::ObjCAtCatchStmtClass:
      return VisitObjCAtCatchStmt(cast<ObjCAtCatchStmt>(S));

    case Stmt::ObjCAutoreleasePoolStmtClass:
    return VisitObjCAutoreleasePoolStmt(cast<ObjCAutoreleasePoolStmt>(S));

    case Stmt::ObjCAtSynchronizedStmtClass:
      return VisitObjCAtSynchronizedStmt(cast<ObjCAtSynchronizedStmt>(S));

    case Stmt::ObjCAtThrowStmtClass:
      return VisitObjCAtThrowStmt(cast<ObjCAtThrowStmt>(S));

    case Stmt::ObjCAtTryStmtClass:
      return VisitObjCAtTryStmt(cast<ObjCAtTryStmt>(S));

    case Stmt::ObjCForCollectionStmtClass:
      return VisitObjCForCollectionStmt(cast<ObjCForCollectionStmt>(S));

    case Stmt::ObjCMessageExprClass:
      return VisitObjCMessageExpr(cast<ObjCMessageExpr>(S), asc);

    case Stmt::OpaqueValueExprClass:
      return Block;

    case Stmt::PseudoObjectExprClass:
      return VisitPseudoObjectExpr(cast<PseudoObjectExpr>(S));

    case Stmt::ReturnStmtClass:
    case Stmt::CoreturnStmtClass:
      return VisitReturnStmt(S);

    case Stmt::SEHExceptStmtClass:
      return VisitSEHExceptStmt(cast<SEHExceptStmt>(S));

    case Stmt::SEHFinallyStmtClass:
      return VisitSEHFinallyStmt(cast<SEHFinallyStmt>(S));

    case Stmt::SEHLeaveStmtClass:
      return VisitSEHLeaveStmt(cast<SEHLeaveStmt>(S));

    case Stmt::SEHTryStmtClass:
      return VisitSEHTryStmt(cast<SEHTryStmt>(S));

    case Stmt::UnaryExprOrTypeTraitExprClass:
      return VisitUnaryExprOrTypeTraitExpr(cast<UnaryExprOrTypeTraitExpr>(S),
                                           asc);

    case Stmt::StmtExprClass:
      return VisitStmtExpr(cast<StmtExpr>(S), asc);

    case Stmt::SwitchStmtClass:
      return VisitSwitchStmt(cast<SwitchStmt>(S));

    case Stmt::UnaryOperatorClass:
      return VisitUnaryOperator(cast<UnaryOperator>(S), asc);

    case Stmt::WhileStmtClass:
      return VisitWhileStmt(cast<WhileStmt>(S));
  }
}

CFGBlock *CFGBuilder::VisitStmt(Stmt *S, AddStmtChoice asc) {
  if (asc.alwaysAdd(*this, S)) {
    autoCreateBlock();
    appendStmt(Block, S);
  }

  return VisitChildren(S);
}

/// VisitChildren - Visit the children of a Stmt.
CFGBlock *CFGBuilder::VisitChildren(Stmt *S) {
  CFGBlock *B = Block;

  // Visit the children in their reverse order so that they appear in
  // left-to-right (natural) order in the CFG.
  reverse_children RChildren(S);
  for (reverse_children::iterator I = RChildren.begin(), E = RChildren.end();
       I != E; ++I) {
    if (Stmt *Child = *I)
      if (CFGBlock *R = Visit(Child))
        B = R;
  }
  return B;
}

CFGBlock *CFGBuilder::VisitAddrLabelExpr(AddrLabelExpr *A,
                                         AddStmtChoice asc) {
  AddressTakenLabels.insert(A->getLabel());

  if (asc.alwaysAdd(*this, A)) {
    autoCreateBlock();
    appendStmt(Block, A);
  }

  return Block;
}

CFGBlock *CFGBuilder::VisitUnaryOperator(UnaryOperator *U,
           AddStmtChoice asc) {
  if (asc.alwaysAdd(*this, U)) {
    autoCreateBlock();
    appendStmt(Block, U);
  }

  return Visit(U->getSubExpr(), AddStmtChoice());
}

CFGBlock *CFGBuilder::VisitLogicalOperator(BinaryOperator *B) {
  CFGBlock *ConfluenceBlock = Block ? Block : createBlock();
  appendStmt(ConfluenceBlock, B);

  if (badCFG)
    return nullptr;

  return VisitLogicalOperator(B, nullptr, ConfluenceBlock,
                              ConfluenceBlock).first;
}

std::pair<CFGBlock*, CFGBlock*>
CFGBuilder::VisitLogicalOperator(BinaryOperator *B,
                                 Stmt *Term,
                                 CFGBlock *TrueBlock,
                                 CFGBlock *FalseBlock) {
  // Introspect the RHS.  If it is a nested logical operation, we recursively
  // build the CFG using this function.  Otherwise, resort to default
  // CFG construction behavior.
  Expr *RHS = B->getRHS()->IgnoreParens();
  CFGBlock *RHSBlock, *ExitBlock;

  do {
    if (BinaryOperator *B_RHS = dyn_cast<BinaryOperator>(RHS))
      if (B_RHS->isLogicalOp()) {
        std::tie(RHSBlock, ExitBlock) =
          VisitLogicalOperator(B_RHS, Term, TrueBlock, FalseBlock);
        break;
      }

    // The RHS is not a nested logical operation.  Don't push the terminator
    // down further, but instead visit RHS and construct the respective
    // pieces of the CFG, and link up the RHSBlock with the terminator
    // we have been provided.
    ExitBlock = RHSBlock = createBlock(false);

    // Even though KnownVal is only used in the else branch of the next
    // conditional, tryEvaluateBool performs additional checking on the
    // Expr, so it should be called unconditionally.
    TryResult KnownVal = tryEvaluateBool(RHS);
    if (!KnownVal.isKnown())
      KnownVal = tryEvaluateBool(B);

    if (!Term) {
      assert(TrueBlock == FalseBlock);
      addSuccessor(RHSBlock, TrueBlock);
    }
    else {
      RHSBlock->setTerminator(Term);
      addSuccessor(RHSBlock, TrueBlock, !KnownVal.isFalse());
      addSuccessor(RHSBlock, FalseBlock, !KnownVal.isTrue());
    }

    Block = RHSBlock;
    RHSBlock = addStmt(RHS);
  }
  while (false);

  if (badCFG)
    return std::make_pair(nullptr, nullptr);

  // Generate the blocks for evaluating the LHS.
  Expr *LHS = B->getLHS()->IgnoreParens();

  if (BinaryOperator *B_LHS = dyn_cast<BinaryOperator>(LHS))
    if (B_LHS->isLogicalOp()) {
      if (B->getOpcode() == BO_LOr)
        FalseBlock = RHSBlock;
      else
        TrueBlock = RHSBlock;

      // For the LHS, treat 'B' as the terminator that we want to sink
      // into the nested branch.  The RHS always gets the top-most
      // terminator.
      return VisitLogicalOperator(B_LHS, B, TrueBlock, FalseBlock);
    }

  // Create the block evaluating the LHS.
  // This contains the '&&' or '||' as the terminator.
  CFGBlock *LHSBlock = createBlock(false);
  LHSBlock->setTerminator(B);

  Block = LHSBlock;
  CFGBlock *EntryLHSBlock = addStmt(LHS);

  if (badCFG)
    return std::make_pair(nullptr, nullptr);

  // See if this is a known constant.
  TryResult KnownVal = tryEvaluateBool(LHS);

  // Now link the LHSBlock with RHSBlock.
  if (B->getOpcode() == BO_LOr) {
    addSuccessor(LHSBlock, TrueBlock, !KnownVal.isFalse());
    addSuccessor(LHSBlock, RHSBlock, !KnownVal.isTrue());
  } else {
    assert(B->getOpcode() == BO_LAnd);
    addSuccessor(LHSBlock, RHSBlock, !KnownVal.isFalse());
    addSuccessor(LHSBlock, FalseBlock, !KnownVal.isTrue());
  }

  return std::make_pair(EntryLHSBlock, ExitBlock);
}

CFGBlock *CFGBuilder::VisitBinaryOperator(BinaryOperator *B,
                                          AddStmtChoice asc) {
   // && or ||
  if (B->isLogicalOp())
    return VisitLogicalOperator(B);

  if (B->getOpcode() == BO_Comma) { // ,
    autoCreateBlock();
    appendStmt(Block, B);
    addStmt(B->getRHS());
    return addStmt(B->getLHS());
  }

  if (B->isAssignmentOp()) {
    if (asc.alwaysAdd(*this, B)) {
      autoCreateBlock();
      appendStmt(Block, B);
    }
    Visit(B->getLHS());
    return Visit(B->getRHS());
  }

  if (asc.alwaysAdd(*this, B)) {
    autoCreateBlock();
    appendStmt(Block, B);
  }

  CFGBlock *RBlock = Visit(B->getRHS());
  CFGBlock *LBlock = Visit(B->getLHS());
  // If visiting RHS causes us to finish 'Block', e.g. the RHS is a StmtExpr
  // containing a DoStmt, and the LHS doesn't create a new block, then we should
  // return RBlock.  Otherwise we'll incorrectly return NULL.
  return (LBlock ? LBlock : RBlock);
}

CFGBlock *CFGBuilder::VisitNoRecurse(Expr *E, AddStmtChoice asc) {
  if (asc.alwaysAdd(*this, E)) {
    autoCreateBlock();
    appendStmt(Block, E);
  }
  return Block;
}

CFGBlock *CFGBuilder::VisitBreakStmt(BreakStmt *B) {
  // "break" is a control-flow statement.  Thus we stop processing the current
  // block.
  if (badCFG)
    return nullptr;

  // Now create a new block that ends with the break statement.
  Block = createBlock(false);
  Block->setTerminator(B);

  // If there is no target for the break, then we are looking at an incomplete
  // AST.  This means that the CFG cannot be constructed.
  if (BreakJumpTarget.block) {
    addAutomaticObjHandling(ScopePos, BreakJumpTarget.scopePosition, B);
    addSuccessor(Block, BreakJumpTarget.block);
  } else
    badCFG = true;

  return Block;
}

static bool CanThrow(Expr *E, ASTContext &Ctx) {
  QualType Ty = E->getType();
  if (Ty->isFunctionPointerType())
    Ty = Ty->getAs<PointerType>()->getPointeeType();
  else if (Ty->isBlockPointerType())
    Ty = Ty->getAs<BlockPointerType>()->getPointeeType();

  const FunctionType *FT = Ty->getAs<FunctionType>();
  if (FT) {
    if (const FunctionProtoType *Proto = dyn_cast<FunctionProtoType>(FT))
      if (!isUnresolvedExceptionSpec(Proto->getExceptionSpecType()) &&
          Proto->isNothrow())
        return false;
  }
  return true;
}

CFGBlock *CFGBuilder::VisitCallExpr(CallExpr *C, AddStmtChoice asc) {
  // Compute the callee type.
  QualType calleeType = C->getCallee()->getType();
  if (calleeType == Context->BoundMemberTy) {
    QualType boundType = Expr::findBoundMemberType(C->getCallee());

    // We should only get a null bound type if processing a dependent
    // CFG.  Recover by assuming nothing.
    if (!boundType.isNull()) calleeType = boundType;
  }

  // If this is a call to a no-return function, this stops the block here.
  bool NoReturn = getFunctionExtInfo(*calleeType).getNoReturn();

  bool AddEHEdge = false;

  // Languages without exceptions are assumed to not throw.
  if (Context->getLangOpts().Exceptions) {
    if (BuildOpts.AddEHEdges)
      AddEHEdge = true;
  }

  // If this is a call to a builtin function, it might not actually evaluate
  // its arguments. Don't add them to the CFG if this is the case.
  bool OmitArguments = false;

  if (FunctionDecl *FD = C->getDirectCallee()) {
    // TODO: Support construction contexts for variadic function arguments.
    // These are a bit problematic and not very useful because passing
    // C++ objects as C-style variadic arguments doesn't work in general
    // (see [expr.call]).
    if (!FD->isVariadic())
      findConstructionContextsForArguments(C);

    if (FD->isNoReturn() || C->isBuiltinAssumeFalse(*Context))
      NoReturn = true;
    if (FD->hasAttr<NoThrowAttr>())
      AddEHEdge = false;
    if (FD->getBuiltinID() == Builtin::BI__builtin_object_size)
      OmitArguments = true;
  }

  if (!CanThrow(C->getCallee(), *Context))
    AddEHEdge = false;

  if (OmitArguments) {
    assert(!NoReturn && "noreturn calls with unevaluated args not implemented");
    assert(!AddEHEdge && "EH calls with unevaluated args not implemented");
    autoCreateBlock();
    appendStmt(Block, C);
    return Visit(C->getCallee());
  }

  if (!NoReturn && !AddEHEdge) {
    autoCreateBlock();
    appendCall(Block, C);

    return VisitChildren(C);
  }

  if (Block) {
    Succ = Block;
    if (badCFG)
      return nullptr;
  }

  if (NoReturn)
    Block = createNoReturnBlock();
  else
    Block = createBlock();

  appendCall(Block, C);

  if (AddEHEdge) {
    // Add exceptional edges.
    if (TryTerminatedBlock)
      addSuccessor(Block, TryTerminatedBlock);
    else
      addSuccessor(Block, &cfg->getExit());
  }

  return VisitChildren(C);
}

CFGBlock *CFGBuilder::VisitChooseExpr(ChooseExpr *C,
                                      AddStmtChoice asc) {
  CFGBlock *ConfluenceBlock = Block ? Block : createBlock();
  appendStmt(ConfluenceBlock, C);
  if (badCFG)
    return nullptr;

  AddStmtChoice alwaysAdd = asc.withAlwaysAdd(true);
  Succ = ConfluenceBlock;
  Block = nullptr;
  CFGBlock *LHSBlock = Visit(C->getLHS(), alwaysAdd);
  if (badCFG)
    return nullptr;

  Succ = ConfluenceBlock;
  Block = nullptr;
  CFGBlock *RHSBlock = Visit(C->getRHS(), alwaysAdd);
  if (badCFG)
    return nullptr;

  Block = createBlock(false);
  // See if this is a known constant.
  const TryResult& KnownVal = tryEvaluateBool(C->getCond());
  addSuccessor(Block, KnownVal.isFalse() ? nullptr : LHSBlock);
  addSuccessor(Block, KnownVal.isTrue() ? nullptr : RHSBlock);
  Block->setTerminator(C);
  return addStmt(C->getCond());
}

CFGBlock *CFGBuilder::VisitCompoundStmt(CompoundStmt *C) {
  LocalScope::const_iterator scopeBeginPos = ScopePos;
  addLocalScopeForStmt(C);

  if (!C->body_empty() && !isa<ReturnStmt>(*C->body_rbegin())) {
    // If the body ends with a ReturnStmt, the dtors will be added in
    // VisitReturnStmt.
    addAutomaticObjHandling(ScopePos, scopeBeginPos, C);
  }

  CFGBlock *LastBlock = Block;

  for (CompoundStmt::reverse_body_iterator I=C->body_rbegin(), E=C->body_rend();
       I != E; ++I ) {
    // If we hit a segment of code just containing ';' (NullStmts), we can
    // get a null block back.  In such cases, just use the LastBlock
    if (CFGBlock *newBlock = addStmt(*I))
      LastBlock = newBlock;

    if (badCFG)
      return nullptr;
  }

  return LastBlock;
}

CFGBlock *CFGBuilder::VisitConditionalOperator(AbstractConditionalOperator *C,
                                               AddStmtChoice asc) {
  const BinaryConditionalOperator *BCO = dyn_cast<BinaryConditionalOperator>(C);
  const OpaqueValueExpr *opaqueValue = (BCO ? BCO->getOpaqueValue() : nullptr);

  // Create the confluence block that will "merge" the results of the ternary
  // expression.
  CFGBlock *ConfluenceBlock = Block ? Block : createBlock();
  appendStmt(ConfluenceBlock, C);
  if (badCFG)
    return nullptr;

  AddStmtChoice alwaysAdd = asc.withAlwaysAdd(true);

  // Create a block for the LHS expression if there is an LHS expression.  A
  // GCC extension allows LHS to be NULL, causing the condition to be the
  // value that is returned instead.
  //  e.g: x ?: y is shorthand for: x ? x : y;
  Succ = ConfluenceBlock;
  Block = nullptr;
  CFGBlock *LHSBlock = nullptr;
  const Expr *trueExpr = C->getTrueExpr();
  if (trueExpr != opaqueValue) {
    LHSBlock = Visit(C->getTrueExpr(), alwaysAdd);
    if (badCFG)
      return nullptr;
    Block = nullptr;
  }
  else
    LHSBlock = ConfluenceBlock;

  // Create the block for the RHS expression.
  Succ = ConfluenceBlock;
  CFGBlock *RHSBlock = Visit(C->getFalseExpr(), alwaysAdd);
  if (badCFG)
    return nullptr;

  // If the condition is a logical '&&' or '||', build a more accurate CFG.
  if (BinaryOperator *Cond =
        dyn_cast<BinaryOperator>(C->getCond()->IgnoreParens()))
    if (Cond->isLogicalOp())
      return VisitLogicalOperator(Cond, C, LHSBlock, RHSBlock).first;

  // Create the block that will contain the condition.
  Block = createBlock(false);

  // See if this is a known constant.
  const TryResult& KnownVal = tryEvaluateBool(C->getCond());
  addSuccessor(Block, LHSBlock, !KnownVal.isFalse());
  addSuccessor(Block, RHSBlock, !KnownVal.isTrue());
  Block->setTerminator(C);
  Expr *condExpr = C->getCond();

  if (opaqueValue) {
    // Run the condition expression if it's not trivially expressed in
    // terms of the opaque value (or if there is no opaque value).
    if (condExpr != opaqueValue)
      addStmt(condExpr);

    // Before that, run the common subexpression if there was one.
    // At least one of this or the above will be run.
    return addStmt(BCO->getCommon());
  }

  return addStmt(condExpr);
}

CFGBlock *CFGBuilder::VisitDeclStmt(DeclStmt *DS) {
  // Check if the Decl is for an __label__.  If so, elide it from the
  // CFG entirely.
  if (isa<LabelDecl>(*DS->decl_begin()))
    return Block;

  // This case also handles static_asserts.
  if (DS->isSingleDecl())
    return VisitDeclSubExpr(DS);

  CFGBlock *B = nullptr;

  // Build an individual DeclStmt for each decl.
  for (DeclStmt::reverse_decl_iterator I = DS->decl_rbegin(),
                                       E = DS->decl_rend();
       I != E; ++I) {

    // Allocate the DeclStmt using the BumpPtrAllocator.  It will get
    // automatically freed with the CFG.
    DeclGroupRef DG(*I);
    Decl *D = *I;
    DeclStmt *DSNew = new (Context) DeclStmt(DG, D->getLocation(), GetEndLoc(D));
    cfg->addSyntheticDeclStmt(DSNew, DS);

    // Append the fake DeclStmt to block.
    B = VisitDeclSubExpr(DSNew);
  }

  return B;
}

/// VisitDeclSubExpr - Utility method to add block-level expressions for
/// DeclStmts and initializers in them.
CFGBlock *CFGBuilder::VisitDeclSubExpr(DeclStmt *DS) {
  assert(DS->isSingleDecl() && "Can handle single declarations only.");
  VarDecl *VD = dyn_cast<VarDecl>(DS->getSingleDecl());

  if (!VD) {
    // Of everything that can be declared in a DeclStmt, only VarDecls impact
    // runtime semantics.
    return Block;
  }

  bool HasTemporaries = false;

  // Guard static initializers under a branch.
  CFGBlock *blockAfterStaticInit = nullptr;

  if (BuildOpts.AddStaticInitBranches && VD->isStaticLocal()) {
    // For static variables, we need to create a branch to track
    // whether or not they are initialized.
    if (Block) {
      Succ = Block;
      Block = nullptr;
      if (badCFG)
        return nullptr;
    }
    blockAfterStaticInit = Succ;
  }

  // Destructors of temporaries in initialization expression should be called
  // after initialization finishes.
  Expr *Init = VD->getInit();
  if (Init) {
    HasTemporaries = isa<ExprWithCleanups>(Init);

    if (BuildOpts.AddTemporaryDtors && HasTemporaries) {
      // Generate destructors for temporaries in initialization expression.
      TempDtorContext Context;
      VisitForTemporaryDtors(cast<ExprWithCleanups>(Init)->getSubExpr(),
                             /*BindToTemporary=*/false, Context);
    }
  }

  autoCreateBlock();
  appendStmt(Block, DS);

  findConstructionContexts(
      ConstructionContextLayer::create(cfg->getBumpVectorContext(), DS),
      Init);

  // Keep track of the last non-null block, as 'Block' can be nulled out
  // if the initializer expression is something like a 'while' in a
  // statement-expression.
  CFGBlock *LastBlock = Block;

  if (Init) {
    if (HasTemporaries) {
      // For expression with temporaries go directly to subexpression to omit
      // generating destructors for the second time.
      ExprWithCleanups *EC = cast<ExprWithCleanups>(Init);
      if (CFGBlock *newBlock = Visit(EC->getSubExpr()))
        LastBlock = newBlock;
    }
    else {
      if (CFGBlock *newBlock = Visit(Init))
        LastBlock = newBlock;
    }
  }

  // If the type of VD is a VLA, then we must process its size expressions.
  for (const VariableArrayType* VA = FindVA(VD->getType().getTypePtr());
       VA != nullptr; VA = FindVA(VA->getElementType().getTypePtr())) {
    if (CFGBlock *newBlock = addStmt(VA->getSizeExpr()))
      LastBlock = newBlock;
  }

  maybeAddScopeBeginForVarDecl(Block, VD, DS);

  // Remove variable from local scope.
  if (ScopePos && VD == *ScopePos)
    ++ScopePos;

  CFGBlock *B = LastBlock;
  if (blockAfterStaticInit) {
    Succ = B;
    Block = createBlock(false);
    Block->setTerminator(DS);
    addSuccessor(Block, blockAfterStaticInit);
    addSuccessor(Block, B);
    B = Block;
  }

  return B;
}

CFGBlock *CFGBuilder::VisitIfStmt(IfStmt *I) {
  // We may see an if statement in the middle of a basic block, or it may be the
  // first statement we are processing.  In either case, we create a new basic
  // block.  First, we create the blocks for the then...else statements, and
  // then we create the block containing the if statement.  If we were in the
  // middle of a block, we stop processing that block.  That block is then the
  // implicit successor for the "then" and "else" clauses.

  // Save local scope position because in case of condition variable ScopePos
  // won't be restored when traversing AST.
  SaveAndRestore<LocalScope::const_iterator> save_scope_pos(ScopePos);

  // Create local scope for C++17 if init-stmt if one exists.
  if (Stmt *Init = I->getInit())
    addLocalScopeForStmt(Init);

  // Create local scope for possible condition variable.
  // Store scope position. Add implicit destructor.
  if (VarDecl *VD = I->getConditionVariable())
    addLocalScopeForVarDecl(VD);

  addAutomaticObjHandling(ScopePos, save_scope_pos.get(), I);

  // The block we were processing is now finished.  Make it the successor
  // block.
  if (Block) {
    Succ = Block;
    if (badCFG)
      return nullptr;
  }

  // Process the false branch.
  CFGBlock *ElseBlock = Succ;

  if (Stmt *Else = I->getElse()) {
    SaveAndRestore<CFGBlock*> sv(Succ);

    // NULL out Block so that the recursive call to Visit will
    // create a new basic block.
    Block = nullptr;

    // If branch is not a compound statement create implicit scope
    // and add destructors.
    if (!isa<CompoundStmt>(Else))
      addLocalScopeAndDtors(Else);

    ElseBlock = addStmt(Else);

    if (!ElseBlock) // Can occur when the Else body has all NullStmts.
      ElseBlock = sv.get();
    else if (Block) {
      if (badCFG)
        return nullptr;
    }
  }

  // Process the true branch.
  CFGBlock *ThenBlock;
  {
    Stmt *Then = I->getThen();
    assert(Then);
    SaveAndRestore<CFGBlock*> sv(Succ);
    Block = nullptr;

    // If branch is not a compound statement create implicit scope
    // and add destructors.
    if (!isa<CompoundStmt>(Then))
      addLocalScopeAndDtors(Then);

    ThenBlock = addStmt(Then);

    if (!ThenBlock) {
      // We can reach here if the "then" body has all NullStmts.
      // Create an empty block so we can distinguish between true and false
      // branches in path-sensitive analyses.
      ThenBlock = createBlock(false);
      addSuccessor(ThenBlock, sv.get());
    } else if (Block) {
      if (badCFG)
        return nullptr;
    }
  }

  // Specially handle "if (expr1 || ...)" and "if (expr1 && ...)" by
  // having these handle the actual control-flow jump.  Note that
  // if we introduce a condition variable, e.g. "if (int x = exp1 || exp2)"
  // we resort to the old control-flow behavior.  This special handling
  // removes infeasible paths from the control-flow graph by having the
  // control-flow transfer of '&&' or '||' go directly into the then/else
  // blocks directly.
  BinaryOperator *Cond =
      I->getConditionVariable()
          ? nullptr
          : dyn_cast<BinaryOperator>(I->getCond()->IgnoreParens());
  CFGBlock *LastBlock;
  if (Cond && Cond->isLogicalOp())
    LastBlock = VisitLogicalOperator(Cond, I, ThenBlock, ElseBlock).first;
  else {
    // Now create a new block containing the if statement.
    Block = createBlock(false);

    // Set the terminator of the new block to the If statement.
    Block->setTerminator(I);

    // See if this is a known constant.
    const TryResult &KnownVal = tryEvaluateBool(I->getCond());

    // Add the successors.  If we know that specific branches are
    // unreachable, inform addSuccessor() of that knowledge.
    addSuccessor(Block, ThenBlock, /* isReachable = */ !KnownVal.isFalse());
    addSuccessor(Block, ElseBlock, /* isReachable = */ !KnownVal.isTrue());

    // Add the condition as the last statement in the new block.  This may
    // create new blocks as the condition may contain control-flow.  Any newly
    // created blocks will be pointed to be "Block".
    LastBlock = addStmt(I->getCond());

    // If the IfStmt contains a condition variable, add it and its
    // initializer to the CFG.
    if (const DeclStmt* DS = I->getConditionVariableDeclStmt()) {
      autoCreateBlock();
      LastBlock = addStmt(const_cast<DeclStmt *>(DS));
    }
  }

  // Finally, if the IfStmt contains a C++17 init-stmt, add it to the CFG.
  if (Stmt *Init = I->getInit()) {
    autoCreateBlock();
    LastBlock = addStmt(Init);
  }

  return LastBlock;
}

CFGBlock *CFGBuilder::VisitReturnStmt(Stmt *S) {
  // If we were in the middle of a block we stop processing that block.
  //
  // NOTE: If a "return" or "co_return" appears in the middle of a block, this
  //       means that the code afterwards is DEAD (unreachable).  We still keep
  //       a basic block for that code; a simple "mark-and-sweep" from the entry
  //       block will be able to report such dead blocks.
  assert(isa<ReturnStmt>(S) || isa<CoreturnStmt>(S));

  // Create the new block.
  Block = createBlock(false);

  addAutomaticObjHandling(ScopePos, LocalScope::const_iterator(), S);

  if (auto *R = dyn_cast<ReturnStmt>(S))
    findConstructionContexts(
        ConstructionContextLayer::create(cfg->getBumpVectorContext(), R),
        R->getRetValue());

  // If the one of the destructors does not return, we already have the Exit
  // block as a successor.
  if (!Block->hasNoReturnElement())
    addSuccessor(Block, &cfg->getExit());

  // Add the return statement to the block.  This may create new blocks if R
  // contains control-flow (short-circuit operations).
  return VisitStmt(S, AddStmtChoice::AlwaysAdd);
}

CFGBlock *CFGBuilder::VisitSEHExceptStmt(SEHExceptStmt *ES) {
  // SEHExceptStmt are treated like labels, so they are the first statement in a
  // block.

  // Save local scope position because in case of exception variable ScopePos
  // won't be restored when traversing AST.
  SaveAndRestore<LocalScope::const_iterator> save_scope_pos(ScopePos);

  addStmt(ES->getBlock());
  CFGBlock *SEHExceptBlock = Block;
  if (!SEHExceptBlock)
    SEHExceptBlock = createBlock();

  appendStmt(SEHExceptBlock, ES);

  // Also add the SEHExceptBlock as a label, like with regular labels.
  SEHExceptBlock->setLabel(ES);

  // Bail out if the CFG is bad.
  if (badCFG)
    return nullptr;

  // We set Block to NULL to allow lazy creation of a new block (if necessary).
  Block = nullptr;

  return SEHExceptBlock;
}

CFGBlock *CFGBuilder::VisitSEHFinallyStmt(SEHFinallyStmt *FS) {
  return VisitCompoundStmt(FS->getBlock());
}

CFGBlock *CFGBuilder::VisitSEHLeaveStmt(SEHLeaveStmt *LS) {
  // "__leave" is a control-flow statement.  Thus we stop processing the current
  // block.
  if (badCFG)
    return nullptr;

  // Now create a new block that ends with the __leave statement.
  Block = createBlock(false);
  Block->setTerminator(LS);

  // If there is no target for the __leave, then we are looking at an incomplete
  // AST.  This means that the CFG cannot be constructed.
  if (SEHLeaveJumpTarget.block) {
    addAutomaticObjHandling(ScopePos, SEHLeaveJumpTarget.scopePosition, LS);
    addSuccessor(Block, SEHLeaveJumpTarget.block);
  } else
    badCFG = true;

  return Block;
}

CFGBlock *CFGBuilder::VisitSEHTryStmt(SEHTryStmt *Terminator) {
  // "__try"/"__except"/"__finally" is a control-flow statement.  Thus we stop
  // processing the current block.
  CFGBlock *SEHTrySuccessor = nullptr;

  if (Block) {
    if (badCFG)
      return nullptr;
    SEHTrySuccessor = Block;
  } else SEHTrySuccessor = Succ;

  // FIXME: Implement __finally support.
  if (Terminator->getFinallyHandler())
    return NYS();

  CFGBlock *PrevSEHTryTerminatedBlock = TryTerminatedBlock;

  // Create a new block that will contain the __try statement.
  CFGBlock *NewTryTerminatedBlock = createBlock(false);

  // Add the terminator in the __try block.
  NewTryTerminatedBlock->setTerminator(Terminator);

  if (SEHExceptStmt *Except = Terminator->getExceptHandler()) {
    // The code after the try is the implicit successor if there's an __except.
    Succ = SEHTrySuccessor;
    Block = nullptr;
    CFGBlock *ExceptBlock = VisitSEHExceptStmt(Except);
    if (!ExceptBlock)
      return nullptr;
    // Add this block to the list of successors for the block with the try
    // statement.
    addSuccessor(NewTryTerminatedBlock, ExceptBlock);
  }
  if (PrevSEHTryTerminatedBlock)
    addSuccessor(NewTryTerminatedBlock, PrevSEHTryTerminatedBlock);
  else
    addSuccessor(NewTryTerminatedBlock, &cfg->getExit());

  // The code after the try is the implicit successor.
  Succ = SEHTrySuccessor;

  // Save the current "__try" context.
  SaveAndRestore<CFGBlock *> save_try(TryTerminatedBlock,
                                      NewTryTerminatedBlock);
  cfg->addTryDispatchBlock(TryTerminatedBlock);

  // Save the current value for the __leave target.
  // All __leaves should go to the code following the __try
  // (FIXME: or if the __try has a __finally, to the __finally.)
  SaveAndRestore<JumpTarget> save_break(SEHLeaveJumpTarget);
  SEHLeaveJumpTarget = JumpTarget(SEHTrySuccessor, ScopePos);

  assert(Terminator->getTryBlock() && "__try must contain a non-NULL body");
  Block = nullptr;
  return addStmt(Terminator->getTryBlock());
}

CFGBlock *CFGBuilder::VisitLabelStmt(LabelStmt *L) {
  // Get the block of the labeled statement.  Add it to our map.
  addStmt(L->getSubStmt());
  CFGBlock *LabelBlock = Block;

  if (!LabelBlock)              // This can happen when the body is empty, i.e.
    LabelBlock = createBlock(); // scopes that only contains NullStmts.

  assert(LabelMap.find(L->getDecl()) == LabelMap.end() &&
         "label already in map");
  LabelMap[L->getDecl()] = JumpTarget(LabelBlock, ScopePos);

  // Labels partition blocks, so this is the end of the basic block we were
  // processing (L is the block's label).  Because this is label (and we have
  // already processed the substatement) there is no extra control-flow to worry
  // about.
  LabelBlock->setLabel(L);
  if (badCFG)
    return nullptr;

  // We set Block to NULL to allow lazy creation of a new block (if necessary);
  Block = nullptr;

  // This block is now the implicit successor of other blocks.
  Succ = LabelBlock;

  return LabelBlock;
}

CFGBlock *CFGBuilder::VisitBlockExpr(BlockExpr *E, AddStmtChoice asc) {
  CFGBlock *LastBlock = VisitNoRecurse(E, asc);
  for (const BlockDecl::Capture &CI : E->getBlockDecl()->captures()) {
    if (Expr *CopyExpr = CI.getCopyExpr()) {
      CFGBlock *Tmp = Visit(CopyExpr);
      if (Tmp)
        LastBlock = Tmp;
    }
  }
  return LastBlock;
}

CFGBlock *CFGBuilder::VisitLambdaExpr(LambdaExpr *E, AddStmtChoice asc) {
  CFGBlock *LastBlock = VisitNoRecurse(E, asc);
  for (LambdaExpr::capture_init_iterator it = E->capture_init_begin(),
       et = E->capture_init_end(); it != et; ++it) {
    if (Expr *Init = *it) {
      CFGBlock *Tmp = Visit(Init);
      if (Tmp)
        LastBlock = Tmp;
    }
  }
  return LastBlock;
}

CFGBlock *CFGBuilder::VisitGotoStmt(GotoStmt *G) {
  // Goto is a control-flow statement.  Thus we stop processing the current
  // block and create a new one.

  Block = createBlock(false);
  Block->setTerminator(G);

  // If we already know the mapping to the label block add the successor now.
  LabelMapTy::iterator I = LabelMap.find(G->getLabel());

  if (I == LabelMap.end())
    // We will need to backpatch this block later.
    BackpatchBlocks.push_back(JumpSource(Block, ScopePos));
  else {
    JumpTarget JT = I->second;
    addAutomaticObjHandling(ScopePos, JT.scopePosition, G);
    addSuccessor(Block, JT.block);
  }

  return Block;
}

CFGBlock *CFGBuilder::VisitForStmt(ForStmt *F) {
  CFGBlock *LoopSuccessor = nullptr;

  // Save local scope position because in case of condition variable ScopePos
  // won't be restored when traversing AST.
  SaveAndRestore<LocalScope::const_iterator> save_scope_pos(ScopePos);

  // Create local scope for init statement and possible condition variable.
  // Add destructor for init statement and condition variable.
  // Store scope position for continue statement.
  if (Stmt *Init = F->getInit())
    addLocalScopeForStmt(Init);
  LocalScope::const_iterator LoopBeginScopePos = ScopePos;

  if (VarDecl *VD = F->getConditionVariable())
    addLocalScopeForVarDecl(VD);
  LocalScope::const_iterator ContinueScopePos = ScopePos;

  addAutomaticObjHandling(ScopePos, save_scope_pos.get(), F);

  addLoopExit(F);

  // "for" is a control-flow statement.  Thus we stop processing the current
  // block.
  if (Block) {
    if (badCFG)
      return nullptr;
    LoopSuccessor = Block;
  } else
    LoopSuccessor = Succ;

  // Save the current value for the break targets.
  // All breaks should go to the code following the loop.
  SaveAndRestore<JumpTarget> save_break(BreakJumpTarget);
  BreakJumpTarget = JumpTarget(LoopSuccessor, ScopePos);

  CFGBlock *BodyBlock = nullptr, *TransitionBlock = nullptr;

  // Now create the loop body.
  {
    assert(F->getBody());

    // Save the current values for Block, Succ, continue and break targets.
    SaveAndRestore<CFGBlock*> save_Block(Block), save_Succ(Succ);
    SaveAndRestore<JumpTarget> save_continue(ContinueJumpTarget);

    // Create an empty block to represent the transition block for looping back
    // to the head of the loop.  If we have increment code, it will
    // go in this block as well.
    Block = Succ = TransitionBlock = createBlock(false);
    TransitionBlock->setLoopTarget(F);

    if (Stmt *I = F->getInc()) {
      // Generate increment code in its own basic block.  This is the target of
      // continue statements.
      Succ = addStmt(I);
    }

    // Finish up the increment (or empty) block if it hasn't been already.
    if (Block) {
      assert(Block == Succ);
      if (badCFG)
        return nullptr;
      Block = nullptr;
    }

   // The starting block for the loop increment is the block that should
   // represent the 'loop target' for looping back to the start of the loop.
   ContinueJumpTarget = JumpTarget(Succ, ContinueScopePos);
   ContinueJumpTarget.block->setLoopTarget(F);

    // Loop body should end with destructor of Condition variable (if any).
   addAutomaticObjHandling(ScopePos, LoopBeginScopePos, F);

    // If body is not a compound statement create implicit scope
    // and add destructors.
    if (!isa<CompoundStmt>(F->getBody()))
      addLocalScopeAndDtors(F->getBody());

    // Now populate the body block, and in the process create new blocks as we
    // walk the body of the loop.
    BodyBlock = addStmt(F->getBody());

    if (!BodyBlock) {
      // In the case of "for (...;...;...);" we can have a null BodyBlock.
      // Use the continue jump target as the proxy for the body.
      BodyBlock = ContinueJumpTarget.block;
    }
    else if (badCFG)
      return nullptr;
  }

  // Because of short-circuit evaluation, the condition of the loop can span
  // multiple basic blocks.  Thus we need the "Entry" and "Exit" blocks that
  // evaluate the condition.
  CFGBlock *EntryConditionBlock = nullptr, *ExitConditionBlock = nullptr;

  do {
    Expr *C = F->getCond();
    SaveAndRestore<LocalScope::const_iterator> save_scope_pos(ScopePos);

    // Specially handle logical operators, which have a slightly
    // more optimal CFG representation.
    if (BinaryOperator *Cond =
            dyn_cast_or_null<BinaryOperator>(C ? C->IgnoreParens() : nullptr))
      if (Cond->isLogicalOp()) {
        std::tie(EntryConditionBlock, ExitConditionBlock) =
          VisitLogicalOperator(Cond, F, BodyBlock, LoopSuccessor);
        break;
      }

    // The default case when not handling logical operators.
    EntryConditionBlock = ExitConditionBlock = createBlock(false);
    ExitConditionBlock->setTerminator(F);

    // See if this is a known constant.
    TryResult KnownVal(true);

    if (C) {
      // Now add the actual condition to the condition block.
      // Because the condition itself may contain control-flow, new blocks may
      // be created.  Thus we update "Succ" after adding the condition.
      Block = ExitConditionBlock;
      EntryConditionBlock = addStmt(C);

      // If this block contains a condition variable, add both the condition
      // variable and initializer to the CFG.
      if (VarDecl *VD = F->getConditionVariable()) {
        if (Expr *Init = VD->getInit()) {
          autoCreateBlock();
          const DeclStmt *DS = F->getConditionVariableDeclStmt();
          assert(DS->isSingleDecl());
          findConstructionContexts(
              ConstructionContextLayer::create(cfg->getBumpVectorContext(), DS),
              Init);
          appendStmt(Block, DS);
          EntryConditionBlock = addStmt(Init);
          assert(Block == EntryConditionBlock);
          maybeAddScopeBeginForVarDecl(EntryConditionBlock, VD, C);
        }
      }

      if (Block && badCFG)
        return nullptr;

      KnownVal = tryEvaluateBool(C);
    }

    // Add the loop body entry as a successor to the condition.
    addSuccessor(ExitConditionBlock, KnownVal.isFalse() ? nullptr : BodyBlock);
    // Link up the condition block with the code that follows the loop.  (the
    // false branch).
    addSuccessor(ExitConditionBlock,
                 KnownVal.isTrue() ? nullptr : LoopSuccessor);
  } while (false);

  // Link up the loop-back block to the entry condition block.
  addSuccessor(TransitionBlock, EntryConditionBlock);

  // The condition block is the implicit successor for any code above the loop.
  Succ = EntryConditionBlock;

  // If the loop contains initialization, create a new block for those
  // statements.  This block can also contain statements that precede the loop.
  if (Stmt *I = F->getInit()) {
    SaveAndRestore<LocalScope::const_iterator> save_scope_pos(ScopePos);
    ScopePos = LoopBeginScopePos;
    Block = createBlock();
    return addStmt(I);
  }

  // There is no loop initialization.  We are thus basically a while loop.
  // NULL out Block to force lazy block construction.
  Block = nullptr;
  Succ = EntryConditionBlock;
  return EntryConditionBlock;
}

CFGBlock *
CFGBuilder::VisitMaterializeTemporaryExpr(MaterializeTemporaryExpr *MTE,
                                          AddStmtChoice asc) {
  findConstructionContexts(
      ConstructionContextLayer::create(cfg->getBumpVectorContext(), MTE),
      MTE->getTemporary());

  return VisitStmt(MTE, asc);
}

CFGBlock *CFGBuilder::VisitMemberExpr(MemberExpr *M, AddStmtChoice asc) {
  if (asc.alwaysAdd(*this, M)) {
    autoCreateBlock();
    appendStmt(Block, M);
  }
  return Visit(M->getBase());
}

CFGBlock *CFGBuilder::VisitObjCForCollectionStmt(ObjCForCollectionStmt *S) {
  // Objective-C fast enumeration 'for' statements:
  //  http://developer.apple.com/documentation/Cocoa/Conceptual/ObjectiveC
  //
  //  for ( Type newVariable in collection_expression ) { statements }
  //
  //  becomes:
  //
  //   prologue:
  //     1. collection_expression
  //     T. jump to loop_entry
  //   loop_entry:
  //     1. side-effects of element expression
  //     1. ObjCForCollectionStmt [performs binding to newVariable]
  //     T. ObjCForCollectionStmt  TB, FB  [jumps to TB if newVariable != nil]
  //   TB:
  //     statements
  //     T. jump to loop_entry
  //   FB:
  //     what comes after
  //
  //  and
  //
  //  Type existingItem;
  //  for ( existingItem in expression ) { statements }
  //
  //  becomes:
  //
  //   the same with newVariable replaced with existingItem; the binding works
  //   the same except that for one ObjCForCollectionStmt::getElement() returns
  //   a DeclStmt and the other returns a DeclRefExpr.

  CFGBlock *LoopSuccessor = nullptr;

  if (Block) {
    if (badCFG)
      return nullptr;
    LoopSuccessor = Block;
    Block = nullptr;
  } else
    LoopSuccessor = Succ;

  // Build the condition blocks.
  CFGBlock *ExitConditionBlock = createBlock(false);

  // Set the terminator for the "exit" condition block.
  ExitConditionBlock->setTerminator(S);

  // The last statement in the block should be the ObjCForCollectionStmt, which
  // performs the actual binding to 'element' and determines if there are any
  // more items in the collection.
  appendStmt(ExitConditionBlock, S);
  Block = ExitConditionBlock;

  // Walk the 'element' expression to see if there are any side-effects.  We
  // generate new blocks as necessary.  We DON'T add the statement by default to
  // the CFG unless it contains control-flow.
  CFGBlock *EntryConditionBlock = Visit(S->getElement(),
                                        AddStmtChoice::NotAlwaysAdd);
  if (Block) {
    if (badCFG)
      return nullptr;
    Block = nullptr;
  }

  // The condition block is the implicit successor for the loop body as well as
  // any code above the loop.
  Succ = EntryConditionBlock;

  // Now create the true branch.
  {
    // Save the current values for Succ, continue and break targets.
    SaveAndRestore<CFGBlock*> save_Block(Block), save_Succ(Succ);
    SaveAndRestore<JumpTarget> save_continue(ContinueJumpTarget),
                               save_break(BreakJumpTarget);

    // Add an intermediate block between the BodyBlock and the
    // EntryConditionBlock to represent the "loop back" transition, for looping
    // back to the head of the loop.
    CFGBlock *LoopBackBlock = nullptr;
    Succ = LoopBackBlock = createBlock();
    LoopBackBlock->setLoopTarget(S);

    BreakJumpTarget = JumpTarget(LoopSuccessor, ScopePos);
    ContinueJumpTarget = JumpTarget(Succ, ScopePos);

    CFGBlock *BodyBlock = addStmt(S->getBody());

    if (!BodyBlock)
      BodyBlock = ContinueJumpTarget.block; // can happen for "for (X in Y) ;"
    else if (Block) {
      if (badCFG)
        return nullptr;
    }

    // This new body block is a successor to our "exit" condition block.
    addSuccessor(ExitConditionBlock, BodyBlock);
  }

  // Link up the condition block with the code that follows the loop.
  // (the false branch).
  addSuccessor(ExitConditionBlock, LoopSuccessor);

  // Now create a prologue block to contain the collection expression.
  Block = createBlock();
  return addStmt(S->getCollection());
}

CFGBlock *CFGBuilder::VisitObjCAutoreleasePoolStmt(ObjCAutoreleasePoolStmt *S) {
  // Inline the body.
  return addStmt(S->getSubStmt());
  // TODO: consider adding cleanups for the end of @autoreleasepool scope.
}

CFGBlock *CFGBuilder::VisitObjCAtSynchronizedStmt(ObjCAtSynchronizedStmt *S) {
  // FIXME: Add locking 'primitives' to CFG for @synchronized.

  // Inline the body.
  CFGBlock *SyncBlock = addStmt(S->getSynchBody());

  // The sync body starts its own basic block.  This makes it a little easier
  // for diagnostic clients.
  if (SyncBlock) {
    if (badCFG)
      return nullptr;

    Block = nullptr;
    Succ = SyncBlock;
  }

  // Add the @synchronized to the CFG.
  autoCreateBlock();
  appendStmt(Block, S);

  // Inline the sync expression.
  return addStmt(S->getSynchExpr());
}

CFGBlock *CFGBuilder::VisitObjCAtTryStmt(ObjCAtTryStmt *S) {
  // FIXME
  return NYS();
}

CFGBlock *CFGBuilder::VisitPseudoObjectExpr(PseudoObjectExpr *E) {
  autoCreateBlock();

  // Add the PseudoObject as the last thing.
  appendStmt(Block, E);

  CFGBlock *lastBlock = Block;

  // Before that, evaluate all of the semantics in order.  In
  // CFG-land, that means appending them in reverse order.
  for (unsigned i = E->getNumSemanticExprs(); i != 0; ) {
    Expr *Semantic = E->getSemanticExpr(--i);

    // If the semantic is an opaque value, we're being asked to bind
    // it to its source expression.
    if (OpaqueValueExpr *OVE = dyn_cast<OpaqueValueExpr>(Semantic))
      Semantic = OVE->getSourceExpr();

    if (CFGBlock *B = Visit(Semantic))
      lastBlock = B;
  }

  return lastBlock;
}

CFGBlock *CFGBuilder::VisitWhileStmt(WhileStmt *W) {
  CFGBlock *LoopSuccessor = nullptr;

  // Save local scope position because in case of condition variable ScopePos
  // won't be restored when traversing AST.
  SaveAndRestore<LocalScope::const_iterator> save_scope_pos(ScopePos);

  // Create local scope for possible condition variable.
  // Store scope position for continue statement.
  LocalScope::const_iterator LoopBeginScopePos = ScopePos;
  if (VarDecl *VD = W->getConditionVariable()) {
    addLocalScopeForVarDecl(VD);
    addAutomaticObjHandling(ScopePos, LoopBeginScopePos, W);
  }
  addLoopExit(W);

  // "while" is a control-flow statement.  Thus we stop processing the current
  // block.
  if (Block) {
    if (badCFG)
      return nullptr;
    LoopSuccessor = Block;
    Block = nullptr;
  } else {
    LoopSuccessor = Succ;
  }

  CFGBlock *BodyBlock = nullptr, *TransitionBlock = nullptr;

  // Process the loop body.
  {
    assert(W->getBody());

    // Save the current values for Block, Succ, continue and break targets.
    SaveAndRestore<CFGBlock*> save_Block(Block), save_Succ(Succ);
    SaveAndRestore<JumpTarget> save_continue(ContinueJumpTarget),
                               save_break(BreakJumpTarget);

    // Create an empty block to represent the transition block for looping back
    // to the head of the loop.
    Succ = TransitionBlock = createBlock(false);
    TransitionBlock->setLoopTarget(W);
    ContinueJumpTarget = JumpTarget(Succ, LoopBeginScopePos);

    // All breaks should go to the code following the loop.
    BreakJumpTarget = JumpTarget(LoopSuccessor, ScopePos);

    // Loop body should end with destructor of Condition variable (if any).
    addAutomaticObjHandling(ScopePos, LoopBeginScopePos, W);

    // If body is not a compound statement create implicit scope
    // and add destructors.
    if (!isa<CompoundStmt>(W->getBody()))
      addLocalScopeAndDtors(W->getBody());

    // Create the body.  The returned block is the entry to the loop body.
    BodyBlock = addStmt(W->getBody());

    if (!BodyBlock)
      BodyBlock = ContinueJumpTarget.block; // can happen for "while(...) ;"
    else if (Block && badCFG)
      return nullptr;
  }

  // Because of short-circuit evaluation, the condition of the loop can span
  // multiple basic blocks.  Thus we need the "Entry" and "Exit" blocks that
  // evaluate the condition.
  CFGBlock *EntryConditionBlock = nullptr, *ExitConditionBlock = nullptr;

  do {
    Expr *C = W->getCond();

    // Specially handle logical operators, which have a slightly
    // more optimal CFG representation.
    if (BinaryOperator *Cond = dyn_cast<BinaryOperator>(C->IgnoreParens()))
      if (Cond->isLogicalOp()) {
        std::tie(EntryConditionBlock, ExitConditionBlock) =
            VisitLogicalOperator(Cond, W, BodyBlock, LoopSuccessor);
        break;
      }

    // The default case when not handling logical operators.
    ExitConditionBlock = createBlock(false);
    ExitConditionBlock->setTerminator(W);

    // Now add the actual condition to the condition block.
    // Because the condition itself may contain control-flow, new blocks may
    // be created.  Thus we update "Succ" after adding the condition.
    Block = ExitConditionBlock;
    Block = EntryConditionBlock = addStmt(C);

    // If this block contains a condition variable, add both the condition
    // variable and initializer to the CFG.
    if (VarDecl *VD = W->getConditionVariable()) {
      if (Expr *Init = VD->getInit()) {
        autoCreateBlock();
        const DeclStmt *DS = W->getConditionVariableDeclStmt();
        assert(DS->isSingleDecl());
        findConstructionContexts(
            ConstructionContextLayer::create(cfg->getBumpVectorContext(),
                                             const_cast<DeclStmt *>(DS)),
            Init);
        appendStmt(Block, DS);
        EntryConditionBlock = addStmt(Init);
        assert(Block == EntryConditionBlock);
        maybeAddScopeBeginForVarDecl(EntryConditionBlock, VD, C);
      }
    }

    if (Block && badCFG)
      return nullptr;

    // See if this is a known constant.
    const TryResult& KnownVal = tryEvaluateBool(C);

    // Add the loop body entry as a successor to the condition.
    addSuccessor(ExitConditionBlock, KnownVal.isFalse() ? nullptr : BodyBlock);
    // Link up the condition block with the code that follows the loop.  (the
    // false branch).
    addSuccessor(ExitConditionBlock,
                 KnownVal.isTrue() ? nullptr : LoopSuccessor);
  } while(false);

  // Link up the loop-back block to the entry condition block.
  addSuccessor(TransitionBlock, EntryConditionBlock);

  // There can be no more statements in the condition block since we loop back
  // to this block.  NULL out Block to force lazy creation of another block.
  Block = nullptr;

  // Return the condition block, which is the dominating block for the loop.
  Succ = EntryConditionBlock;
  return EntryConditionBlock;
}

CFGBlock *CFGBuilder::VisitObjCAtCatchStmt(ObjCAtCatchStmt *S) {
  // FIXME: For now we pretend that @catch and the code it contains does not
  //  exit.
  return Block;
}

CFGBlock *CFGBuilder::VisitObjCAtThrowStmt(ObjCAtThrowStmt *S) {
  // FIXME: This isn't complete.  We basically treat @throw like a return
  //  statement.

  // If we were in the middle of a block we stop processing that block.
  if (badCFG)
    return nullptr;

  // Create the new block.
  Block = createBlock(false);

  // The Exit block is the only successor.
  addSuccessor(Block, &cfg->getExit());

  // Add the statement to the block.  This may create new blocks if S contains
  // control-flow (short-circuit operations).
  return VisitStmt(S, AddStmtChoice::AlwaysAdd);
}

CFGBlock *CFGBuilder::VisitObjCMessageExpr(ObjCMessageExpr *ME,
                                           AddStmtChoice asc) {
  findConstructionContextsForArguments(ME);

  autoCreateBlock();
  appendObjCMessage(Block, ME);

  return VisitChildren(ME);
}

CFGBlock *CFGBuilder::VisitCXXThrowExpr(CXXThrowExpr *T) {
  // If we were in the middle of a block we stop processing that block.
  if (badCFG)
    return nullptr;

  // Create the new block.
  Block = createBlock(false);

  if (TryTerminatedBlock)
    // The current try statement is the only successor.
    addSuccessor(Block, TryTerminatedBlock);
  else
    // otherwise the Exit block is the only successor.
    addSuccessor(Block, &cfg->getExit());

  // Add the statement to the block.  This may create new blocks if S contains
  // control-flow (short-circuit operations).
  return VisitStmt(T, AddStmtChoice::AlwaysAdd);
}

CFGBlock *CFGBuilder::VisitDoStmt(DoStmt *D) {
  CFGBlock *LoopSuccessor = nullptr;

  addLoopExit(D);

  // "do...while" is a control-flow statement.  Thus we stop processing the
  // current block.
  if (Block) {
    if (badCFG)
      return nullptr;
    LoopSuccessor = Block;
  } else
    LoopSuccessor = Succ;

  // Because of short-circuit evaluation, the condition of the loop can span
  // multiple basic blocks.  Thus we need the "Entry" and "Exit" blocks that
  // evaluate the condition.
  CFGBlock *ExitConditionBlock = createBlock(false);
  CFGBlock *EntryConditionBlock = ExitConditionBlock;

  // Set the terminator for the "exit" condition block.
  ExitConditionBlock->setTerminator(D);

  // Now add the actual condition to the condition block.  Because the condition
  // itself may contain control-flow, new blocks may be created.
  if (Stmt *C = D->getCond()) {
    Block = ExitConditionBlock;
    EntryConditionBlock = addStmt(C);
    if (Block) {
      if (badCFG)
        return nullptr;
    }
  }

  // The condition block is the implicit successor for the loop body.
  Succ = EntryConditionBlock;

  // See if this is a known constant.
  const TryResult &KnownVal = tryEvaluateBool(D->getCond());

  // Process the loop body.
  CFGBlock *BodyBlock = nullptr;
  {
    assert(D->getBody());

    // Save the current values for Block, Succ, and continue and break targets
    SaveAndRestore<CFGBlock*> save_Block(Block), save_Succ(Succ);
    SaveAndRestore<JumpTarget> save_continue(ContinueJumpTarget),
        save_break(BreakJumpTarget);

    // All continues within this loop should go to the condition block
    ContinueJumpTarget = JumpTarget(EntryConditionBlock, ScopePos);

    // All breaks should go to the code following the loop.
    BreakJumpTarget = JumpTarget(LoopSuccessor, ScopePos);

    // NULL out Block to force lazy instantiation of blocks for the body.
    Block = nullptr;

    // If body is not a compound statement create implicit scope
    // and add destructors.
    if (!isa<CompoundStmt>(D->getBody()))
      addLocalScopeAndDtors(D->getBody());

    // Create the body.  The returned block is the entry to the loop body.
    BodyBlock = addStmt(D->getBody());

    if (!BodyBlock)
      BodyBlock = EntryConditionBlock; // can happen for "do ; while(...)"
    else if (Block) {
      if (badCFG)
        return nullptr;
    }

    // Add an intermediate block between the BodyBlock and the
    // ExitConditionBlock to represent the "loop back" transition.  Create an
    // empty block to represent the transition block for looping back to the
    // head of the loop.
    // FIXME: Can we do this more efficiently without adding another block?
    Block = nullptr;
    Succ = BodyBlock;
    CFGBlock *LoopBackBlock = createBlock();
    LoopBackBlock->setLoopTarget(D);

    if (!KnownVal.isFalse())
      // Add the loop body entry as a successor to the condition.
      addSuccessor(ExitConditionBlock, LoopBackBlock);
    else
      addSuccessor(ExitConditionBlock, nullptr);
  }

  // Link up the condition block with the code that follows the loop.
  // (the false branch).
  addSuccessor(ExitConditionBlock, KnownVal.isTrue() ? nullptr : LoopSuccessor);

  // There can be no more statements in the body block(s) since we loop back to
  // the body.  NULL out Block to force lazy creation of another block.
  Block = nullptr;

  // Return the loop body, which is the dominating block for the loop.
  Succ = BodyBlock;
  return BodyBlock;
}

CFGBlock *CFGBuilder::VisitContinueStmt(ContinueStmt *C) {
  // "continue" is a control-flow statement.  Thus we stop processing the
  // current block.
  if (badCFG)
    return nullptr;

  // Now create a new block that ends with the continue statement.
  Block = createBlock(false);
  Block->setTerminator(C);

  // If there is no target for the continue, then we are looking at an
  // incomplete AST.  This means the CFG cannot be constructed.
  if (ContinueJumpTarget.block) {
    addAutomaticObjHandling(ScopePos, ContinueJumpTarget.scopePosition, C);
    addSuccessor(Block, ContinueJumpTarget.block);
  } else
    badCFG = true;

  return Block;
}

CFGBlock *CFGBuilder::VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *E,
                                                    AddStmtChoice asc) {
  if (asc.alwaysAdd(*this, E)) {
    autoCreateBlock();
    appendStmt(Block, E);
  }

  // VLA types have expressions that must be evaluated.
  CFGBlock *lastBlock = Block;

  if (E->isArgumentType()) {
    for (const VariableArrayType *VA =FindVA(E->getArgumentType().getTypePtr());
         VA != nullptr; VA = FindVA(VA->getElementType().getTypePtr()))
      lastBlock = addStmt(VA->getSizeExpr());
  }
  return lastBlock;
}

/// VisitStmtExpr - Utility method to handle (nested) statement
///  expressions (a GCC extension).
CFGBlock *CFGBuilder::VisitStmtExpr(StmtExpr *SE, AddStmtChoice asc) {
  if (asc.alwaysAdd(*this, SE)) {
    autoCreateBlock();
    appendStmt(Block, SE);
  }
  return VisitCompoundStmt(SE->getSubStmt());
}

CFGBlock *CFGBuilder::VisitSwitchStmt(SwitchStmt *Terminator) {
  // "switch" is a control-flow statement.  Thus we stop processing the current
  // block.
  CFGBlock *SwitchSuccessor = nullptr;

  // Save local scope position because in case of condition variable ScopePos
  // won't be restored when traversing AST.
  SaveAndRestore<LocalScope::const_iterator> save_scope_pos(ScopePos);

  // Create local scope for C++17 switch init-stmt if one exists.
  if (Stmt *Init = Terminator->getInit())
    addLocalScopeForStmt(Init);

  // Create local scope for possible condition variable.
  // Store scope position. Add implicit destructor.
  if (VarDecl *VD = Terminator->getConditionVariable())
    addLocalScopeForVarDecl(VD);

  addAutomaticObjHandling(ScopePos, save_scope_pos.get(), Terminator);

  if (Block) {
    if (badCFG)
      return nullptr;
    SwitchSuccessor = Block;
  } else SwitchSuccessor = Succ;

  // Save the current "switch" context.
  SaveAndRestore<CFGBlock*> save_switch(SwitchTerminatedBlock),
                            save_default(DefaultCaseBlock);
  SaveAndRestore<JumpTarget> save_break(BreakJumpTarget);

  // Set the "default" case to be the block after the switch statement.  If the
  // switch statement contains a "default:", this value will be overwritten with
  // the block for that code.
  DefaultCaseBlock = SwitchSuccessor;

  // Create a new block that will contain the switch statement.
  SwitchTerminatedBlock = createBlock(false);

  // Now process the switch body.  The code after the switch is the implicit
  // successor.
  Succ = SwitchSuccessor;
  BreakJumpTarget = JumpTarget(SwitchSuccessor, ScopePos);

  // When visiting the body, the case statements should automatically get linked
  // up to the switch.  We also don't keep a pointer to the body, since all
  // control-flow from the switch goes to case/default statements.
  assert(Terminator->getBody() && "switch must contain a non-NULL body");
  Block = nullptr;

  // For pruning unreachable case statements, save the current state
  // for tracking the condition value.
  SaveAndRestore<bool> save_switchExclusivelyCovered(switchExclusivelyCovered,
                                                     false);

  // Determine if the switch condition can be explicitly evaluated.
  assert(Terminator->getCond() && "switch condition must be non-NULL");
  Expr::EvalResult result;
  bool b = tryEvaluate(Terminator->getCond(), result);
  SaveAndRestore<Expr::EvalResult*> save_switchCond(switchCond,
                                                    b ? &result : nullptr);

  // If body is not a compound statement create implicit scope
  // and add destructors.
  if (!isa<CompoundStmt>(Terminator->getBody()))
    addLocalScopeAndDtors(Terminator->getBody());

  addStmt(Terminator->getBody());
  if (Block) {
    if (badCFG)
      return nullptr;
  }

  // If we have no "default:" case, the default transition is to the code
  // following the switch body.  Moreover, take into account if all the
  // cases of a switch are covered (e.g., switching on an enum value).
  //
  // Note: We add a successor to a switch that is considered covered yet has no
  //       case statements if the enumeration has no enumerators.
  bool SwitchAlwaysHasSuccessor = false;
  SwitchAlwaysHasSuccessor |= switchExclusivelyCovered;
  SwitchAlwaysHasSuccessor |= Terminator->isAllEnumCasesCovered() &&
                              Terminator->getSwitchCaseList();
  addSuccessor(SwitchTerminatedBlock, DefaultCaseBlock,
               !SwitchAlwaysHasSuccessor);

  // Add the terminator and condition in the switch block.
  SwitchTerminatedBlock->setTerminator(Terminator);
  Block = SwitchTerminatedBlock;
  CFGBlock *LastBlock = addStmt(Terminator->getCond());

  // If the SwitchStmt contains a condition variable, add both the
  // SwitchStmt and the condition variable initialization to the CFG.
  if (VarDecl *VD = Terminator->getConditionVariable()) {
    if (Expr *Init = VD->getInit()) {
      autoCreateBlock();
      appendStmt(Block, Terminator->getConditionVariableDeclStmt());
      LastBlock = addStmt(Init);
      maybeAddScopeBeginForVarDecl(LastBlock, VD, Init);
    }
  }

  // Finally, if the SwitchStmt contains a C++17 init-stmt, add it to the CFG.
  if (Stmt *Init = Terminator->getInit()) {
    autoCreateBlock();
    LastBlock = addStmt(Init);
  }

  return LastBlock;
}

static bool shouldAddCase(bool &switchExclusivelyCovered,
                          const Expr::EvalResult *switchCond,
                          const CaseStmt *CS,
                          ASTContext &Ctx) {
  if (!switchCond)
    return true;

  bool addCase = false;

  if (!switchExclusivelyCovered) {
    if (switchCond->Val.isInt()) {
      // Evaluate the LHS of the case value.
      const llvm::APSInt &lhsInt = CS->getLHS()->EvaluateKnownConstInt(Ctx);
      const llvm::APSInt &condInt = switchCond->Val.getInt();

      if (condInt == lhsInt) {
        addCase = true;
        switchExclusivelyCovered = true;
      }
      else if (condInt > lhsInt) {
        if (const Expr *RHS = CS->getRHS()) {
          // Evaluate the RHS of the case value.
          const llvm::APSInt &V2 = RHS->EvaluateKnownConstInt(Ctx);
          if (V2 >= condInt) {
            addCase = true;
            switchExclusivelyCovered = true;
          }
        }
      }
    }
    else
      addCase = true;
  }
  return addCase;
}

CFGBlock *CFGBuilder::VisitCaseStmt(CaseStmt *CS) {
  // CaseStmts are essentially labels, so they are the first statement in a
  // block.
  CFGBlock *TopBlock = nullptr, *LastBlock = nullptr;

  if (Stmt *Sub = CS->getSubStmt()) {
    // For deeply nested chains of CaseStmts, instead of doing a recursion
    // (which can blow out the stack), manually unroll and create blocks
    // along the way.
    while (isa<CaseStmt>(Sub)) {
      CFGBlock *currentBlock = createBlock(false);
      currentBlock->setLabel(CS);

      if (TopBlock)
        addSuccessor(LastBlock, currentBlock);
      else
        TopBlock = currentBlock;

      addSuccessor(SwitchTerminatedBlock,
                   shouldAddCase(switchExclusivelyCovered, switchCond,
                                 CS, *Context)
                   ? currentBlock : nullptr);

      LastBlock = currentBlock;
      CS = cast<CaseStmt>(Sub);
      Sub = CS->getSubStmt();
    }

    addStmt(Sub);
  }

  CFGBlock *CaseBlock = Block;
  if (!CaseBlock)
    CaseBlock = createBlock();

  // Cases statements partition blocks, so this is the top of the basic block we
  // were processing (the "case XXX:" is the label).
  CaseBlock->setLabel(CS);

  if (badCFG)
    return nullptr;

  // Add this block to the list of successors for the block with the switch
  // statement.
  assert(SwitchTerminatedBlock);
  addSuccessor(SwitchTerminatedBlock, CaseBlock,
               shouldAddCase(switchExclusivelyCovered, switchCond,
                             CS, *Context));

  // We set Block to NULL to allow lazy creation of a new block (if necessary)
  Block = nullptr;

  if (TopBlock) {
    addSuccessor(LastBlock, CaseBlock);
    Succ = TopBlock;
  } else {
    // This block is now the implicit successor of other blocks.
    Succ = CaseBlock;
  }

  return Succ;
}

CFGBlock *CFGBuilder::VisitDefaultStmt(DefaultStmt *Terminator) {
  if (Terminator->getSubStmt())
    addStmt(Terminator->getSubStmt());

  DefaultCaseBlock = Block;

  if (!DefaultCaseBlock)
    DefaultCaseBlock = createBlock();

  // Default statements partition blocks, so this is the top of the basic block
  // we were processing (the "default:" is the label).
  DefaultCaseBlock->setLabel(Terminator);

  if (badCFG)
    return nullptr;

  // Unlike case statements, we don't add the default block to the successors
  // for the switch statement immediately.  This is done when we finish
  // processing the switch statement.  This allows for the default case
  // (including a fall-through to the code after the switch statement) to always
  // be the last successor of a switch-terminated block.

  // We set Block to NULL to allow lazy creation of a new block (if necessary)
  Block = nullptr;

  // This block is now the implicit successor of other blocks.
  Succ = DefaultCaseBlock;

  return DefaultCaseBlock;
}

CFGBlock *CFGBuilder::VisitCXXTryStmt(CXXTryStmt *Terminator) {
  // "try"/"catch" is a control-flow statement.  Thus we stop processing the
  // current block.
  CFGBlock *TrySuccessor = nullptr;

  if (Block) {
    if (badCFG)
      return nullptr;
    TrySuccessor = Block;
  } else TrySuccessor = Succ;

  CFGBlock *PrevTryTerminatedBlock = TryTerminatedBlock;

  // Create a new block that will contain the try statement.
  CFGBlock *NewTryTerminatedBlock = createBlock(false);
  // Add the terminator in the try block.
  NewTryTerminatedBlock->setTerminator(Terminator);

  bool HasCatchAll = false;
  for (unsigned h = 0; h <Terminator->getNumHandlers(); ++h) {
    // The code after the try is the implicit successor.
    Succ = TrySuccessor;
    CXXCatchStmt *CS = Terminator->getHandler(h);
    if (CS->getExceptionDecl() == nullptr) {
      HasCatchAll = true;
    }
    Block = nullptr;
    CFGBlock *CatchBlock = VisitCXXCatchStmt(CS);
    if (!CatchBlock)
      return nullptr;
    // Add this block to the list of successors for the block with the try
    // statement.
    addSuccessor(NewTryTerminatedBlock, CatchBlock);
  }
  if (!HasCatchAll) {
    if (PrevTryTerminatedBlock)
      addSuccessor(NewTryTerminatedBlock, PrevTryTerminatedBlock);
    else
      addSuccessor(NewTryTerminatedBlock, &cfg->getExit());
  }

  // The code after the try is the implicit successor.
  Succ = TrySuccessor;

  // Save the current "try" context.
  SaveAndRestore<CFGBlock*> save_try(TryTerminatedBlock, NewTryTerminatedBlock);
  cfg->addTryDispatchBlock(TryTerminatedBlock);

  assert(Terminator->getTryBlock() && "try must contain a non-NULL body");
  Block = nullptr;
  return addStmt(Terminator->getTryBlock());
}

CFGBlock *CFGBuilder::VisitCXXCatchStmt(CXXCatchStmt *CS) {
  // CXXCatchStmt are treated like labels, so they are the first statement in a
  // block.

  // Save local scope position because in case of exception variable ScopePos
  // won't be restored when traversing AST.
  SaveAndRestore<LocalScope::const_iterator> save_scope_pos(ScopePos);

  // Create local scope for possible exception variable.
  // Store scope position. Add implicit destructor.
  if (VarDecl *VD = CS->getExceptionDecl()) {
    LocalScope::const_iterator BeginScopePos = ScopePos;
    addLocalScopeForVarDecl(VD);
    addAutomaticObjHandling(ScopePos, BeginScopePos, CS);
  }

  if (CS->getHandlerBlock())
    addStmt(CS->getHandlerBlock());

  CFGBlock *CatchBlock = Block;
  if (!CatchBlock)
    CatchBlock = createBlock();

  // CXXCatchStmt is more than just a label.  They have semantic meaning
  // as well, as they implicitly "initialize" the catch variable.  Add
  // it to the CFG as a CFGElement so that the control-flow of these
  // semantics gets captured.
  appendStmt(CatchBlock, CS);

  // Also add the CXXCatchStmt as a label, to mirror handling of regular
  // labels.
  CatchBlock->setLabel(CS);

  // Bail out if the CFG is bad.
  if (badCFG)
    return nullptr;

  // We set Block to NULL to allow lazy creation of a new block (if necessary)
  Block = nullptr;

  return CatchBlock;
}

CFGBlock *CFGBuilder::VisitCXXForRangeStmt(CXXForRangeStmt *S) {
  // C++0x for-range statements are specified as [stmt.ranged]:
  //
  // {
  //   auto && __range = range-init;
  //   for ( auto __begin = begin-expr,
  //         __end = end-expr;
  //         __begin != __end;
  //         ++__begin ) {
  //     for-range-declaration = *__begin;
  //     statement
  //   }
  // }

  // Save local scope position before the addition of the implicit variables.
  SaveAndRestore<LocalScope::const_iterator> save_scope_pos(ScopePos);

  // Create local scopes and destructors for range, begin and end variables.
  if (Stmt *Range = S->getRangeStmt())
    addLocalScopeForStmt(Range);
  if (Stmt *Begin = S->getBeginStmt())
    addLocalScopeForStmt(Begin);
  if (Stmt *End = S->getEndStmt())
    addLocalScopeForStmt(End);
  addAutomaticObjHandling(ScopePos, save_scope_pos.get(), S);

  LocalScope::const_iterator ContinueScopePos = ScopePos;

  // "for" is a control-flow statement.  Thus we stop processing the current
  // block.
  CFGBlock *LoopSuccessor = nullptr;
  if (Block) {
    if (badCFG)
      return nullptr;
    LoopSuccessor = Block;
  } else
    LoopSuccessor = Succ;

  // Save the current value for the break targets.
  // All breaks should go to the code following the loop.
  SaveAndRestore<JumpTarget> save_break(BreakJumpTarget);
  BreakJumpTarget = JumpTarget(LoopSuccessor, ScopePos);

  // The block for the __begin != __end expression.
  CFGBlock *ConditionBlock = createBlock(false);
  ConditionBlock->setTerminator(S);

  // Now add the actual condition to the condition block.
  if (Expr *C = S->getCond()) {
    Block = ConditionBlock;
    CFGBlock *BeginConditionBlock = addStmt(C);
    if (badCFG)
      return nullptr;
    assert(BeginConditionBlock == ConditionBlock &&
           "condition block in for-range was unexpectedly complex");
    (void)BeginConditionBlock;
  }

  // The condition block is the implicit successor for the loop body as well as
  // any code above the loop.
  Succ = ConditionBlock;

  // See if this is a known constant.
  TryResult KnownVal(true);

  if (S->getCond())
    KnownVal = tryEvaluateBool(S->getCond());

  // Now create the loop body.
  {
    assert(S->getBody());

    // Save the current values for Block, Succ, and continue targets.
    SaveAndRestore<CFGBlock*> save_Block(Block), save_Succ(Succ);
    SaveAndRestore<JumpTarget> save_continue(ContinueJumpTarget);

    // Generate increment code in its own basic block.  This is the target of
    // continue statements.
    Block = nullptr;
    Succ = addStmt(S->getInc());
    if (badCFG)
      return nullptr;
    ContinueJumpTarget = JumpTarget(Succ, ContinueScopePos);

    // The starting block for the loop increment is the block that should
    // represent the 'loop target' for looping back to the start of the loop.
    ContinueJumpTarget.block->setLoopTarget(S);

    // Finish up the increment block and prepare to start the loop body.
    assert(Block);
    if (badCFG)
      return nullptr;
    Block = nullptr;

    // Add implicit scope and dtors for loop variable.
    addLocalScopeAndDtors(S->getLoopVarStmt());

    // Populate a new block to contain the loop body and loop variable.
    addStmt(S->getBody());
    if (badCFG)
      return nullptr;
    CFGBlock *LoopVarStmtBlock = addStmt(S->getLoopVarStmt());
    if (badCFG)
      return nullptr;

    // This new body block is a successor to our condition block.
    addSuccessor(ConditionBlock,
                 KnownVal.isFalse() ? nullptr : LoopVarStmtBlock);
  }

  // Link up the condition block with the code that follows the loop (the
  // false branch).
  addSuccessor(ConditionBlock, KnownVal.isTrue() ? nullptr : LoopSuccessor);

  // Add the initialization statements.
  Block = createBlock();
  addStmt(S->getBeginStmt());
  addStmt(S->getEndStmt());
  CFGBlock *Head = addStmt(S->getRangeStmt());
  if (S->getInit())
    Head = addStmt(S->getInit());
  return Head;
}

CFGBlock *CFGBuilder::VisitExprWithCleanups(ExprWithCleanups *E,
    AddStmtChoice asc) {
  if (BuildOpts.AddTemporaryDtors) {
    // If adding implicit destructors visit the full expression for adding
    // destructors of temporaries.
    TempDtorContext Context;
    VisitForTemporaryDtors(E->getSubExpr(), false, Context);

    // Full expression has to be added as CFGStmt so it will be sequenced
    // before destructors of it's temporaries.
    asc = asc.withAlwaysAdd(true);
  }
  return Visit(E->getSubExpr(), asc);
}

CFGBlock *CFGBuilder::VisitCXXBindTemporaryExpr(CXXBindTemporaryExpr *E,
                                                AddStmtChoice asc) {
  if (asc.alwaysAdd(*this, E)) {
    autoCreateBlock();
    appendStmt(Block, E);

    findConstructionContexts(
        ConstructionContextLayer::create(cfg->getBumpVectorContext(), E),
        E->getSubExpr());

    // We do not want to propagate the AlwaysAdd property.
    asc = asc.withAlwaysAdd(false);
  }
  return Visit(E->getSubExpr(), asc);
}

CFGBlock *CFGBuilder::VisitCXXConstructExpr(CXXConstructExpr *C,
                                            AddStmtChoice asc) {
  // If the constructor takes objects as arguments by value, we need to properly
  // construct these objects. Construction contexts we find here aren't for the
  // constructor C, they're for its arguments only.
  findConstructionContextsForArguments(C);

  autoCreateBlock();
  appendConstructor(Block, C);

  return VisitChildren(C);
}

CFGBlock *CFGBuilder::VisitCXXNewExpr(CXXNewExpr *NE,
                                      AddStmtChoice asc) {
  autoCreateBlock();
  appendStmt(Block, NE);

  findConstructionContexts(
      ConstructionContextLayer::create(cfg->getBumpVectorContext(), NE),
      const_cast<CXXConstructExpr *>(NE->getConstructExpr()));

  if (NE->getInitializer())
    Block = Visit(NE->getInitializer());

  if (BuildOpts.AddCXXNewAllocator)
    appendNewAllocator(Block, NE);

  if (NE->isArray())
    Block = Visit(NE->getArraySize());

  for (CXXNewExpr::arg_iterator I = NE->placement_arg_begin(),
       E = NE->placement_arg_end(); I != E; ++I)
    Block = Visit(*I);

  return Block;
}

CFGBlock *CFGBuilder::VisitCXXDeleteExpr(CXXDeleteExpr *DE,
                                         AddStmtChoice asc) {
  autoCreateBlock();
  appendStmt(Block, DE);
  QualType DTy = DE->getDestroyedType();
  if (!DTy.isNull()) {
    DTy = DTy.getNonReferenceType();
    CXXRecordDecl *RD = Context->getBaseElementType(DTy)->getAsCXXRecordDecl();
    if (RD) {
      if (RD->isCompleteDefinition() && !RD->hasTrivialDestructor())
        appendDeleteDtor(Block, RD, DE);
    }
  }

  return VisitChildren(DE);
}

CFGBlock *CFGBuilder::VisitCXXFunctionalCastExpr(CXXFunctionalCastExpr *E,
                                                 AddStmtChoice asc) {
  if (asc.alwaysAdd(*this, E)) {
    autoCreateBlock();
    appendStmt(Block, E);
    // We do not want to propagate the AlwaysAdd property.
    asc = asc.withAlwaysAdd(false);
  }
  return Visit(E->getSubExpr(), asc);
}

CFGBlock *CFGBuilder::VisitCXXTemporaryObjectExpr(CXXTemporaryObjectExpr *C,
                                                  AddStmtChoice asc) {
  // If the constructor takes objects as arguments by value, we need to properly
  // construct these objects. Construction contexts we find here aren't for the
  // constructor C, they're for its arguments only.
  findConstructionContextsForArguments(C);

  autoCreateBlock();
  appendConstructor(Block, C);
  return VisitChildren(C);
}

CFGBlock *CFGBuilder::VisitImplicitCastExpr(ImplicitCastExpr *E,
                                            AddStmtChoice asc) {
  if (asc.alwaysAdd(*this, E)) {
    autoCreateBlock();
    appendStmt(Block, E);
  }
  return Visit(E->getSubExpr(), AddStmtChoice());
}

CFGBlock *CFGBuilder::VisitConstantExpr(ConstantExpr *E, AddStmtChoice asc) {
  return Visit(E->getSubExpr(), AddStmtChoice());
}

CFGBlock *CFGBuilder::VisitIndirectGotoStmt(IndirectGotoStmt *I) {
  // Lazily create the indirect-goto dispatch block if there isn't one already.
  CFGBlock *IBlock = cfg->getIndirectGotoBlock();

  if (!IBlock) {
    IBlock = createBlock(false);
    cfg->setIndirectGotoBlock(IBlock);
  }

  // IndirectGoto is a control-flow statement.  Thus we stop processing the
  // current block and create a new one.
  if (badCFG)
    return nullptr;

  Block = createBlock(false);
  Block->setTerminator(I);
  addSuccessor(Block, IBlock);
  return addStmt(I->getTarget());
}

CFGBlock *CFGBuilder::VisitForTemporaryDtors(Stmt *E, bool BindToTemporary,
                                             TempDtorContext &Context) {
  assert(BuildOpts.AddImplicitDtors && BuildOpts.AddTemporaryDtors);

tryAgain:
  if (!E) {
    badCFG = true;
    return nullptr;
  }
  switch (E->getStmtClass()) {
    default:
      return VisitChildrenForTemporaryDtors(E, Context);

    case Stmt::BinaryOperatorClass:
      return VisitBinaryOperatorForTemporaryDtors(cast<BinaryOperator>(E),
                                                  Context);

    case Stmt::CXXBindTemporaryExprClass:
      return VisitCXXBindTemporaryExprForTemporaryDtors(
          cast<CXXBindTemporaryExpr>(E), BindToTemporary, Context);

    case Stmt::BinaryConditionalOperatorClass:
    case Stmt::ConditionalOperatorClass:
      return VisitConditionalOperatorForTemporaryDtors(
          cast<AbstractConditionalOperator>(E), BindToTemporary, Context);

    case Stmt::ImplicitCastExprClass:
      // For implicit cast we want BindToTemporary to be passed further.
      E = cast<CastExpr>(E)->getSubExpr();
      goto tryAgain;

    case Stmt::CXXFunctionalCastExprClass:
      // For functional cast we want BindToTemporary to be passed further.
      E = cast<CXXFunctionalCastExpr>(E)->getSubExpr();
      goto tryAgain;

    case Stmt::ConstantExprClass:
      E = cast<ConstantExpr>(E)->getSubExpr();
      goto tryAgain;

    case Stmt::ParenExprClass:
      E = cast<ParenExpr>(E)->getSubExpr();
      goto tryAgain;

    case Stmt::MaterializeTemporaryExprClass: {
      const MaterializeTemporaryExpr* MTE = cast<MaterializeTemporaryExpr>(E);
      BindToTemporary = (MTE->getStorageDuration() != SD_FullExpression);
      SmallVector<const Expr *, 2> CommaLHSs;
      SmallVector<SubobjectAdjustment, 2> Adjustments;
      // Find the expression whose lifetime needs to be extended.
      E = const_cast<Expr *>(
          cast<MaterializeTemporaryExpr>(E)
              ->GetTemporaryExpr()
              ->skipRValueSubobjectAdjustments(CommaLHSs, Adjustments));
      // Visit the skipped comma operator left-hand sides for other temporaries.
      for (const Expr *CommaLHS : CommaLHSs) {
        VisitForTemporaryDtors(const_cast<Expr *>(CommaLHS),
                               /*BindToTemporary=*/false, Context);
      }
      goto tryAgain;
    }

    case Stmt::BlockExprClass:
      // Don't recurse into blocks; their subexpressions don't get evaluated
      // here.
      return Block;

    case Stmt::LambdaExprClass: {
      // For lambda expressions, only recurse into the capture initializers,
      // and not the body.
      auto *LE = cast<LambdaExpr>(E);
      CFGBlock *B = Block;
      for (Expr *Init : LE->capture_inits()) {
        if (Init) {
          if (CFGBlock *R = VisitForTemporaryDtors(
                  Init, /*BindToTemporary=*/false, Context))
            B = R;
        }
      }
      return B;
    }

    case Stmt::CXXDefaultArgExprClass:
      E = cast<CXXDefaultArgExpr>(E)->getExpr();
      goto tryAgain;

    case Stmt::CXXDefaultInitExprClass:
      E = cast<CXXDefaultInitExpr>(E)->getExpr();
      goto tryAgain;
  }
}

CFGBlock *CFGBuilder::VisitChildrenForTemporaryDtors(Stmt *E,
                                                     TempDtorContext &Context) {
  if (isa<LambdaExpr>(E)) {
    // Do not visit the children of lambdas; they have their own CFGs.
    return Block;
  }

  // When visiting children for destructors we want to visit them in reverse
  // order that they will appear in the CFG.  Because the CFG is built
  // bottom-up, this means we visit them in their natural order, which
  // reverses them in the CFG.
  CFGBlock *B = Block;
  for (Stmt *Child : E->children())
    if (Child)
      if (CFGBlock *R = VisitForTemporaryDtors(Child, false, Context))
        B = R;

  return B;
}

CFGBlock *CFGBuilder::VisitBinaryOperatorForTemporaryDtors(
    BinaryOperator *E, TempDtorContext &Context) {
  if (E->isLogicalOp()) {
    VisitForTemporaryDtors(E->getLHS(), false, Context);
    TryResult RHSExecuted = tryEvaluateBool(E->getLHS());
    if (RHSExecuted.isKnown() && E->getOpcode() == BO_LOr)
      RHSExecuted.negate();

    // We do not know at CFG-construction time whether the right-hand-side was
    // executed, thus we add a branch node that depends on the temporary
    // constructor call.
    TempDtorContext RHSContext(
        bothKnownTrue(Context.KnownExecuted, RHSExecuted));
    VisitForTemporaryDtors(E->getRHS(), false, RHSContext);
    InsertTempDtorDecisionBlock(RHSContext);

    return Block;
  }

  if (E->isAssignmentOp()) {
    // For assignment operator (=) LHS expression is visited
    // before RHS expression. For destructors visit them in reverse order.
    CFGBlock *RHSBlock = VisitForTemporaryDtors(E->getRHS(), false, Context);
    CFGBlock *LHSBlock = VisitForTemporaryDtors(E->getLHS(), false, Context);
    return LHSBlock ? LHSBlock : RHSBlock;
  }

  // For any other binary operator RHS expression is visited before
  // LHS expression (order of children). For destructors visit them in reverse
  // order.
  CFGBlock *LHSBlock = VisitForTemporaryDtors(E->getLHS(), false, Context);
  CFGBlock *RHSBlock = VisitForTemporaryDtors(E->getRHS(), false, Context);
  return RHSBlock ? RHSBlock : LHSBlock;
}

CFGBlock *CFGBuilder::VisitCXXBindTemporaryExprForTemporaryDtors(
    CXXBindTemporaryExpr *E, bool BindToTemporary, TempDtorContext &Context) {
  // First add destructors for temporaries in subexpression.
  CFGBlock *B = VisitForTemporaryDtors(E->getSubExpr(), false, Context);
  if (!BindToTemporary) {
    // If lifetime of temporary is not prolonged (by assigning to constant
    // reference) add destructor for it.

    const CXXDestructorDecl *Dtor = E->getTemporary()->getDestructor();

    if (Dtor->getParent()->isAnyDestructorNoReturn()) {
      // If the destructor is marked as a no-return destructor, we need to
      // create a new block for the destructor which does not have as a
      // successor anything built thus far. Control won't flow out of this
      // block.
      if (B) Succ = B;
      Block = createNoReturnBlock();
    } else if (Context.needsTempDtorBranch()) {
      // If we need to introduce a branch, we add a new block that we will hook
      // up to a decision block later.
      if (B) Succ = B;
      Block = createBlock();
    } else {
      autoCreateBlock();
    }
    if (Context.needsTempDtorBranch()) {
      Context.setDecisionPoint(Succ, E);
    }
    appendTemporaryDtor(Block, E);

    B = Block;
  }
  return B;
}

void CFGBuilder::InsertTempDtorDecisionBlock(const TempDtorContext &Context,
                                             CFGBlock *FalseSucc) {
  if (!Context.TerminatorExpr) {
    // If no temporary was found, we do not need to insert a decision point.
    return;
  }
  assert(Context.TerminatorExpr);
  CFGBlock *Decision = createBlock(false);
  Decision->setTerminator(CFGTerminator(Context.TerminatorExpr, true));
  addSuccessor(Decision, Block, !Context.KnownExecuted.isFalse());
  addSuccessor(Decision, FalseSucc ? FalseSucc : Context.Succ,
               !Context.KnownExecuted.isTrue());
  Block = Decision;
}

CFGBlock *CFGBuilder::VisitConditionalOperatorForTemporaryDtors(
    AbstractConditionalOperator *E, bool BindToTemporary,
    TempDtorContext &Context) {
  VisitForTemporaryDtors(E->getCond(), false, Context);
  CFGBlock *ConditionBlock = Block;
  CFGBlock *ConditionSucc = Succ;
  TryResult ConditionVal = tryEvaluateBool(E->getCond());
  TryResult NegatedVal = ConditionVal;
  if (NegatedVal.isKnown()) NegatedVal.negate();

  TempDtorContext TrueContext(
      bothKnownTrue(Context.KnownExecuted, ConditionVal));
  VisitForTemporaryDtors(E->getTrueExpr(), BindToTemporary, TrueContext);
  CFGBlock *TrueBlock = Block;

  Block = ConditionBlock;
  Succ = ConditionSucc;
  TempDtorContext FalseContext(
      bothKnownTrue(Context.KnownExecuted, NegatedVal));
  VisitForTemporaryDtors(E->getFalseExpr(), BindToTemporary, FalseContext);

  if (TrueContext.TerminatorExpr && FalseContext.TerminatorExpr) {
    InsertTempDtorDecisionBlock(FalseContext, TrueBlock);
  } else if (TrueContext.TerminatorExpr) {
    Block = TrueBlock;
    InsertTempDtorDecisionBlock(TrueContext);
  } else {
    InsertTempDtorDecisionBlock(FalseContext);
  }
  return Block;
}

/// createBlock - Constructs and adds a new CFGBlock to the CFG.  The block has
///  no successors or predecessors.  If this is the first block created in the
///  CFG, it is automatically set to be the Entry and Exit of the CFG.
CFGBlock *CFG::createBlock() {
  bool first_block = begin() == end();

  // Create the block.
  CFGBlock *Mem = getAllocator().Allocate<CFGBlock>();
  new (Mem) CFGBlock(NumBlockIDs++, BlkBVC, this);
  Blocks.push_back(Mem, BlkBVC);

  // If this is the first block, set it as the Entry and Exit.
  if (first_block)
    Entry = Exit = &back();

  // Return the block.
  return &back();
}

/// buildCFG - Constructs a CFG from an AST.
std::unique_ptr<CFG> CFG::buildCFG(const Decl *D, Stmt *Statement,
                                   ASTContext *C, const BuildOptions &BO) {
  CFGBuilder Builder(C, BO);
  return Builder.buildCFG(D, Statement);
}

const CXXDestructorDecl *
CFGImplicitDtor::getDestructorDecl(ASTContext &astContext) const {
  switch (getKind()) {
    case CFGElement::Initializer:
    case CFGElement::NewAllocator:
    case CFGElement::LoopExit:
    case CFGElement::LifetimeEnds:
    case CFGElement::Statement:
    case CFGElement::Constructor:
    case CFGElement::CXXRecordTypedCall:
    case CFGElement::ScopeBegin:
    case CFGElement::ScopeEnd:
      llvm_unreachable("getDestructorDecl should only be used with "
                       "ImplicitDtors");
    case CFGElement::AutomaticObjectDtor: {
      const VarDecl *var = castAs<CFGAutomaticObjDtor>().getVarDecl();
      QualType ty = var->getType();

      // FIXME: See CFGBuilder::addLocalScopeForVarDecl.
      //
      // Lifetime-extending constructs are handled here. This works for a single
      // temporary in an initializer expression.
      if (ty->isReferenceType()) {
        if (const Expr *Init = var->getInit()) {
          ty = getReferenceInitTemporaryType(Init);
        }
      }

      while (const ArrayType *arrayType = astContext.getAsArrayType(ty)) {
        ty = arrayType->getElementType();
      }
      const RecordType *recordType = ty->getAs<RecordType>();
      const CXXRecordDecl *classDecl =
      cast<CXXRecordDecl>(recordType->getDecl());
      return classDecl->getDestructor();
    }
    case CFGElement::DeleteDtor: {
      const CXXDeleteExpr *DE = castAs<CFGDeleteDtor>().getDeleteExpr();
      QualType DTy = DE->getDestroyedType();
      DTy = DTy.getNonReferenceType();
      const CXXRecordDecl *classDecl =
          astContext.getBaseElementType(DTy)->getAsCXXRecordDecl();
      return classDecl->getDestructor();
    }
    case CFGElement::TemporaryDtor: {
      const CXXBindTemporaryExpr *bindExpr =
        castAs<CFGTemporaryDtor>().getBindTemporaryExpr();
      const CXXTemporary *temp = bindExpr->getTemporary();
      return temp->getDestructor();
    }
    case CFGElement::BaseDtor:
    case CFGElement::MemberDtor:
      // Not yet supported.
      return nullptr;
  }
  llvm_unreachable("getKind() returned bogus value");
}

bool CFGImplicitDtor::isNoReturn(ASTContext &astContext) const {
  if (const CXXDestructorDecl *DD = getDestructorDecl(astContext))
    return DD->isNoReturn();
  return false;
}

//===----------------------------------------------------------------------===//
// CFGBlock operations.
//===----------------------------------------------------------------------===//

CFGBlock::AdjacentBlock::AdjacentBlock(CFGBlock *B, bool IsReachable)
    : ReachableBlock(IsReachable ? B : nullptr),
      UnreachableBlock(!IsReachable ? B : nullptr,
                       B && IsReachable ? AB_Normal : AB_Unreachable) {}

CFGBlock::AdjacentBlock::AdjacentBlock(CFGBlock *B, CFGBlock *AlternateBlock)
    : ReachableBlock(B),
      UnreachableBlock(B == AlternateBlock ? nullptr : AlternateBlock,
                       B == AlternateBlock ? AB_Alternate : AB_Normal) {}

void CFGBlock::addSuccessor(AdjacentBlock Succ,
                            BumpVectorContext &C) {
  if (CFGBlock *B = Succ.getReachableBlock())
    B->Preds.push_back(AdjacentBlock(this, Succ.isReachable()), C);

  if (CFGBlock *UnreachableB = Succ.getPossiblyUnreachableBlock())
    UnreachableB->Preds.push_back(AdjacentBlock(this, false), C);

  Succs.push_back(Succ, C);
}

bool CFGBlock::FilterEdge(const CFGBlock::FilterOptions &F,
        const CFGBlock *From, const CFGBlock *To) {
  if (F.IgnoreNullPredecessors && !From)
    return true;

  if (To && From && F.IgnoreDefaultsWithCoveredEnums) {
    // If the 'To' has no label or is labeled but the label isn't a
    // CaseStmt then filter this edge.
    if (const SwitchStmt *S =
        dyn_cast_or_null<SwitchStmt>(From->getTerminator().getStmt())) {
      if (S->isAllEnumCasesCovered()) {
        const Stmt *L = To->getLabel();
        if (!L || !isa<CaseStmt>(L))
          return true;
      }
    }
  }

  return false;
}

//===----------------------------------------------------------------------===//
// CFG pretty printing
//===----------------------------------------------------------------------===//

namespace {

class StmtPrinterHelper : public PrinterHelper  {
  using StmtMapTy = llvm::DenseMap<const Stmt *, std::pair<unsigned, unsigned>>;
  using DeclMapTy = llvm::DenseMap<const Decl *, std::pair<unsigned, unsigned>>;

  StmtMapTy StmtMap;
  DeclMapTy DeclMap;
  signed currentBlock = 0;
  unsigned currStmt = 0;
  const LangOptions &LangOpts;

public:
  StmtPrinterHelper(const CFG* cfg, const LangOptions &LO)
      : LangOpts(LO) {
    for (CFG::const_iterator I = cfg->begin(), E = cfg->end(); I != E; ++I ) {
      unsigned j = 1;
      for (CFGBlock::const_iterator BI = (*I)->begin(), BEnd = (*I)->end() ;
           BI != BEnd; ++BI, ++j ) {
        if (Optional<CFGStmt> SE = BI->getAs<CFGStmt>()) {
          const Stmt *stmt= SE->getStmt();
          std::pair<unsigned, unsigned> P((*I)->getBlockID(), j);
          StmtMap[stmt] = P;

          switch (stmt->getStmtClass()) {
            case Stmt::DeclStmtClass:
              DeclMap[cast<DeclStmt>(stmt)->getSingleDecl()] = P;
              break;
            case Stmt::IfStmtClass: {
              const VarDecl *var = cast<IfStmt>(stmt)->getConditionVariable();
              if (var)
                DeclMap[var] = P;
              break;
            }
            case Stmt::ForStmtClass: {
              const VarDecl *var = cast<ForStmt>(stmt)->getConditionVariable();
              if (var)
                DeclMap[var] = P;
              break;
            }
            case Stmt::WhileStmtClass: {
              const VarDecl *var =
                cast<WhileStmt>(stmt)->getConditionVariable();
              if (var)
                DeclMap[var] = P;
              break;
            }
            case Stmt::SwitchStmtClass: {
              const VarDecl *var =
                cast<SwitchStmt>(stmt)->getConditionVariable();
              if (var)
                DeclMap[var] = P;
              break;
            }
            case Stmt::CXXCatchStmtClass: {
              const VarDecl *var =
                cast<CXXCatchStmt>(stmt)->getExceptionDecl();
              if (var)
                DeclMap[var] = P;
              break;
            }
            default:
              break;
          }
        }
      }
    }
  }

  ~StmtPrinterHelper() override = default;

  const LangOptions &getLangOpts() const { return LangOpts; }
  void setBlockID(signed i) { currentBlock = i; }
  void setStmtID(unsigned i) { currStmt = i; }

  bool handledStmt(Stmt *S, raw_ostream &OS) override {
    StmtMapTy::iterator I = StmtMap.find(S);

    if (I == StmtMap.end())
      return false;

    if (currentBlock >= 0 && I->second.first == (unsigned) currentBlock
                          && I->second.second == currStmt) {
      return false;
    }

    OS << "[B" << I->second.first << "." << I->second.second << "]";
    return true;
  }

  bool handleDecl(const Decl *D, raw_ostream &OS) {
    DeclMapTy::iterator I = DeclMap.find(D);

    if (I == DeclMap.end())
      return false;

    if (currentBlock >= 0 && I->second.first == (unsigned) currentBlock
                          && I->second.second == currStmt) {
      return false;
    }

    OS << "[B" << I->second.first << "." << I->second.second << "]";
    return true;
  }
};

class CFGBlockTerminatorPrint
    : public StmtVisitor<CFGBlockTerminatorPrint,void> {
  raw_ostream &OS;
  StmtPrinterHelper* Helper;
  PrintingPolicy Policy;

public:
  CFGBlockTerminatorPrint(raw_ostream &os, StmtPrinterHelper* helper,
                          const PrintingPolicy &Policy)
      : OS(os), Helper(helper), Policy(Policy) {
    this->Policy.IncludeNewlines = false;
  }

  void VisitIfStmt(IfStmt *I) {
    OS << "if ";
    if (Stmt *C = I->getCond())
      C->printPretty(OS, Helper, Policy);
  }

  // Default case.
  void VisitStmt(Stmt *Terminator) {
    Terminator->printPretty(OS, Helper, Policy);
  }

  void VisitDeclStmt(DeclStmt *DS) {
    VarDecl *VD = cast<VarDecl>(DS->getSingleDecl());
    OS << "static init " << VD->getName();
  }

  void VisitForStmt(ForStmt *F) {
    OS << "for (" ;
    if (F->getInit())
      OS << "...";
    OS << "; ";
    if (Stmt *C = F->getCond())
      C->printPretty(OS, Helper, Policy);
    OS << "; ";
    if (F->getInc())
      OS << "...";
    OS << ")";
  }

  void VisitWhileStmt(WhileStmt *W) {
    OS << "while " ;
    if (Stmt *C = W->getCond())
      C->printPretty(OS, Helper, Policy);
  }

  void VisitDoStmt(DoStmt *D) {
    OS << "do ... while ";
    if (Stmt *C = D->getCond())
      C->printPretty(OS, Helper, Policy);
  }

  void VisitSwitchStmt(SwitchStmt *Terminator) {
    OS << "switch ";
    Terminator->getCond()->printPretty(OS, Helper, Policy);
  }

  void VisitCXXTryStmt(CXXTryStmt *CS) {
    OS << "try ...";
  }

  void VisitSEHTryStmt(SEHTryStmt *CS) {
    OS << "__try ...";
  }

  void VisitAbstractConditionalOperator(AbstractConditionalOperator* C) {
    if (Stmt *Cond = C->getCond())
      Cond->printPretty(OS, Helper, Policy);
    OS << " ? ... : ...";
  }

  void VisitChooseExpr(ChooseExpr *C) {
    OS << "__builtin_choose_expr( ";
    if (Stmt *Cond = C->getCond())
      Cond->printPretty(OS, Helper, Policy);
    OS << " )";
  }

  void VisitIndirectGotoStmt(IndirectGotoStmt *I) {
    OS << "goto *";
    if (Stmt *T = I->getTarget())
      T->printPretty(OS, Helper, Policy);
  }

  void VisitBinaryOperator(BinaryOperator* B) {
    if (!B->isLogicalOp()) {
      VisitExpr(B);
      return;
    }

    if (B->getLHS())
      B->getLHS()->printPretty(OS, Helper, Policy);

    switch (B->getOpcode()) {
      case BO_LOr:
        OS << " || ...";
        return;
      case BO_LAnd:
        OS << " && ...";
        return;
      default:
        llvm_unreachable("Invalid logical operator.");
    }
  }

  void VisitExpr(Expr *E) {
    E->printPretty(OS, Helper, Policy);
  }

public:
  void print(CFGTerminator T) {
    if (T.isTemporaryDtorsBranch())
      OS << "(Temp Dtor) ";
    Visit(T.getStmt());
  }
};

} // namespace

static void print_initializer(raw_ostream &OS, StmtPrinterHelper &Helper,
                              const CXXCtorInitializer *I) {
  if (I->isBaseInitializer())
    OS << I->getBaseClass()->getAsCXXRecordDecl()->getName();
  else if (I->isDelegatingInitializer())
    OS << I->getTypeSourceInfo()->getType()->getAsCXXRecordDecl()->getName();
  else
    OS << I->getAnyMember()->getName();
  OS << "(";
  if (Expr *IE = I->getInit())
    IE->printPretty(OS, &Helper, PrintingPolicy(Helper.getLangOpts()));
  OS << ")";

  if (I->isBaseInitializer())
    OS << " (Base initializer)";
  else if (I->isDelegatingInitializer())
    OS << " (Delegating initializer)";
  else
    OS << " (Member initializer)";
}

static void print_construction_context(raw_ostream &OS,
                                       StmtPrinterHelper &Helper,
                                       const ConstructionContext *CC) {
  SmallVector<const Stmt *, 3> Stmts;
  switch (CC->getKind()) {
  case ConstructionContext::SimpleConstructorInitializerKind: {
    OS << ", ";
    const auto *SICC = cast<SimpleConstructorInitializerConstructionContext>(CC);
    print_initializer(OS, Helper, SICC->getCXXCtorInitializer());
    return;
  }
  case ConstructionContext::CXX17ElidedCopyConstructorInitializerKind: {
    OS << ", ";
    const auto *CICC =
        cast<CXX17ElidedCopyConstructorInitializerConstructionContext>(CC);
    print_initializer(OS, Helper, CICC->getCXXCtorInitializer());
    Stmts.push_back(CICC->getCXXBindTemporaryExpr());
    break;
  }
  case ConstructionContext::SimpleVariableKind: {
    const auto *SDSCC = cast<SimpleVariableConstructionContext>(CC);
    Stmts.push_back(SDSCC->getDeclStmt());
    break;
  }
  case ConstructionContext::CXX17ElidedCopyVariableKind: {
    const auto *CDSCC = cast<CXX17ElidedCopyVariableConstructionContext>(CC);
    Stmts.push_back(CDSCC->getDeclStmt());
    Stmts.push_back(CDSCC->getCXXBindTemporaryExpr());
    break;
  }
  case ConstructionContext::NewAllocatedObjectKind: {
    const auto *NECC = cast<NewAllocatedObjectConstructionContext>(CC);
    Stmts.push_back(NECC->getCXXNewExpr());
    break;
  }
  case ConstructionContext::SimpleReturnedValueKind: {
    const auto *RSCC = cast<SimpleReturnedValueConstructionContext>(CC);
    Stmts.push_back(RSCC->getReturnStmt());
    break;
  }
  case ConstructionContext::CXX17ElidedCopyReturnedValueKind: {
    const auto *RSCC =
        cast<CXX17ElidedCopyReturnedValueConstructionContext>(CC);
    Stmts.push_back(RSCC->getReturnStmt());
    Stmts.push_back(RSCC->getCXXBindTemporaryExpr());
    break;
  }
  case ConstructionContext::SimpleTemporaryObjectKind: {
    const auto *TOCC = cast<SimpleTemporaryObjectConstructionContext>(CC);
    Stmts.push_back(TOCC->getCXXBindTemporaryExpr());
    Stmts.push_back(TOCC->getMaterializedTemporaryExpr());
    break;
  }
  case ConstructionContext::ElidedTemporaryObjectKind: {
    const auto *TOCC = cast<ElidedTemporaryObjectConstructionContext>(CC);
    Stmts.push_back(TOCC->getCXXBindTemporaryExpr());
    Stmts.push_back(TOCC->getMaterializedTemporaryExpr());
    Stmts.push_back(TOCC->getConstructorAfterElision());
    break;
  }
  case ConstructionContext::ArgumentKind: {
    const auto *ACC = cast<ArgumentConstructionContext>(CC);
    if (const Stmt *BTE = ACC->getCXXBindTemporaryExpr()) {
      OS << ", ";
      Helper.handledStmt(const_cast<Stmt *>(BTE), OS);
    }
    OS << ", ";
    Helper.handledStmt(const_cast<Expr *>(ACC->getCallLikeExpr()), OS);
    OS << "+" << ACC->getIndex();
    return;
  }
  }
  for (auto I: Stmts)
    if (I) {
      OS << ", ";
      Helper.handledStmt(const_cast<Stmt *>(I), OS);
    }
}

static void print_elem(raw_ostream &OS, StmtPrinterHelper &Helper,
                       const CFGElement &E) {
  if (Optional<CFGStmt> CS = E.getAs<CFGStmt>()) {
    const Stmt *S = CS->getStmt();
    assert(S != nullptr && "Expecting non-null Stmt");

    // special printing for statement-expressions.
    if (const StmtExpr *SE = dyn_cast<StmtExpr>(S)) {
      const CompoundStmt *Sub = SE->getSubStmt();

      auto Children = Sub->children();
      if (Children.begin() != Children.end()) {
        OS << "({ ... ; ";
        Helper.handledStmt(*SE->getSubStmt()->body_rbegin(),OS);
        OS << " })\n";
        return;
      }
    }
    // special printing for comma expressions.
    if (const BinaryOperator* B = dyn_cast<BinaryOperator>(S)) {
      if (B->getOpcode() == BO_Comma) {
        OS << "... , ";
        Helper.handledStmt(B->getRHS(),OS);
        OS << '\n';
        return;
      }
    }
    S->printPretty(OS, &Helper, PrintingPolicy(Helper.getLangOpts()));

    if (auto VTC = E.getAs<CFGCXXRecordTypedCall>()) {
      if (isa<CXXOperatorCallExpr>(S))
        OS << " (OperatorCall)";
      OS << " (CXXRecordTypedCall";
      print_construction_context(OS, Helper, VTC->getConstructionContext());
      OS << ")";
    } else if (isa<CXXOperatorCallExpr>(S)) {
      OS << " (OperatorCall)";
    } else if (isa<CXXBindTemporaryExpr>(S)) {
      OS << " (BindTemporary)";
    } else if (const CXXConstructExpr *CCE = dyn_cast<CXXConstructExpr>(S)) {
      OS << " (CXXConstructExpr";
      if (Optional<CFGConstructor> CE = E.getAs<CFGConstructor>()) {
        print_construction_context(OS, Helper, CE->getConstructionContext());
      }
      OS << ", " << CCE->getType().getAsString() << ")";
    } else if (const CastExpr *CE = dyn_cast<CastExpr>(S)) {
      OS << " (" << CE->getStmtClassName() << ", "
         << CE->getCastKindName()
         << ", " << CE->getType().getAsString()
         << ")";
    }

    // Expressions need a newline.
    if (isa<Expr>(S))
      OS << '\n';
  } else if (Optional<CFGInitializer> IE = E.getAs<CFGInitializer>()) {
    print_initializer(OS, Helper, IE->getInitializer());
    OS << '\n';
  } else if (Optional<CFGAutomaticObjDtor> DE =
                 E.getAs<CFGAutomaticObjDtor>()) {
    const VarDecl *VD = DE->getVarDecl();
    Helper.handleDecl(VD, OS);

    ASTContext &ACtx = VD->getASTContext();
    QualType T = VD->getType();
    if (T->isReferenceType())
      T = getReferenceInitTemporaryType(VD->getInit(), nullptr);
    if (const ArrayType *AT = ACtx.getAsArrayType(T))
      T = ACtx.getBaseElementType(AT);

    OS << ".~" << T->getAsCXXRecordDecl()->getName().str() << "()";
    OS << " (Implicit destructor)\n";
  } else if (Optional<CFGLifetimeEnds> DE = E.getAs<CFGLifetimeEnds>()) {
    const VarDecl *VD = DE->getVarDecl();
    Helper.handleDecl(VD, OS);

    OS << " (Lifetime ends)\n";
  } else if (Optional<CFGLoopExit> LE = E.getAs<CFGLoopExit>()) {
    const Stmt *LoopStmt = LE->getLoopStmt();
    OS << LoopStmt->getStmtClassName() << " (LoopExit)\n";
  } else if (Optional<CFGScopeBegin> SB = E.getAs<CFGScopeBegin>()) {
    OS << "CFGScopeBegin(";
    if (const VarDecl *VD = SB->getVarDecl())
      OS << VD->getQualifiedNameAsString();
    OS << ")\n";
  } else if (Optional<CFGScopeEnd> SE = E.getAs<CFGScopeEnd>()) {
    OS << "CFGScopeEnd(";
    if (const VarDecl *VD = SE->getVarDecl())
      OS << VD->getQualifiedNameAsString();
    OS << ")\n";
  } else if (Optional<CFGNewAllocator> NE = E.getAs<CFGNewAllocator>()) {
    OS << "CFGNewAllocator(";
    if (const CXXNewExpr *AllocExpr = NE->getAllocatorExpr())
      AllocExpr->getType().print(OS, PrintingPolicy(Helper.getLangOpts()));
    OS << ")\n";
  } else if (Optional<CFGDeleteDtor> DE = E.getAs<CFGDeleteDtor>()) {
    const CXXRecordDecl *RD = DE->getCXXRecordDecl();
    if (!RD)
      return;
    CXXDeleteExpr *DelExpr =
        const_cast<CXXDeleteExpr*>(DE->getDeleteExpr());
    Helper.handledStmt(cast<Stmt>(DelExpr->getArgument()), OS);
    OS << "->~" << RD->getName().str() << "()";
    OS << " (Implicit destructor)\n";
  } else if (Optional<CFGBaseDtor> BE = E.getAs<CFGBaseDtor>()) {
    const CXXBaseSpecifier *BS = BE->getBaseSpecifier();
    OS << "~" << BS->getType()->getAsCXXRecordDecl()->getName() << "()";
    OS << " (Base object destructor)\n";
  } else if (Optional<CFGMemberDtor> ME = E.getAs<CFGMemberDtor>()) {
    const FieldDecl *FD = ME->getFieldDecl();
    const Type *T = FD->getType()->getBaseElementTypeUnsafe();
    OS << "this->" << FD->getName();
    OS << ".~" << T->getAsCXXRecordDecl()->getName() << "()";
    OS << " (Member object destructor)\n";
  } else if (Optional<CFGTemporaryDtor> TE = E.getAs<CFGTemporaryDtor>()) {
    const CXXBindTemporaryExpr *BT = TE->getBindTemporaryExpr();
    OS << "~";
    BT->getType().print(OS, PrintingPolicy(Helper.getLangOpts()));
    OS << "() (Temporary object destructor)\n";
  }
}

static void print_block(raw_ostream &OS, const CFG* cfg,
                        const CFGBlock &B,
                        StmtPrinterHelper &Helper, bool print_edges,
                        bool ShowColors) {
  Helper.setBlockID(B.getBlockID());

  // Print the header.
  if (ShowColors)
    OS.changeColor(raw_ostream::YELLOW, true);

  OS << "\n [B" << B.getBlockID();

  if (&B == &cfg->getEntry())
    OS << " (ENTRY)]\n";
  else if (&B == &cfg->getExit())
    OS << " (EXIT)]\n";
  else if (&B == cfg->getIndirectGotoBlock())
    OS << " (INDIRECT GOTO DISPATCH)]\n";
  else if (B.hasNoReturnElement())
    OS << " (NORETURN)]\n";
  else
    OS << "]\n";

  if (ShowColors)
    OS.resetColor();

  // Print the label of this block.
  if (Stmt *Label = const_cast<Stmt*>(B.getLabel())) {
    if (print_edges)
      OS << "  ";

    if (LabelStmt *L = dyn_cast<LabelStmt>(Label))
      OS << L->getName();
    else if (CaseStmt *C = dyn_cast<CaseStmt>(Label)) {
      OS << "case ";
      if (C->getLHS())
        C->getLHS()->printPretty(OS, &Helper,
                                 PrintingPolicy(Helper.getLangOpts()));
      if (C->getRHS()) {
        OS << " ... ";
        C->getRHS()->printPretty(OS, &Helper,
                                 PrintingPolicy(Helper.getLangOpts()));
      }
    } else if (isa<DefaultStmt>(Label))
      OS << "default";
    else if (CXXCatchStmt *CS = dyn_cast<CXXCatchStmt>(Label)) {
      OS << "catch (";
      if (CS->getExceptionDecl())
        CS->getExceptionDecl()->print(OS, PrintingPolicy(Helper.getLangOpts()),
                                      0);
      else
        OS << "...";
      OS << ")";
    } else if (SEHExceptStmt *ES = dyn_cast<SEHExceptStmt>(Label)) {
      OS << "__except (";
      ES->getFilterExpr()->printPretty(OS, &Helper,
                                       PrintingPolicy(Helper.getLangOpts()), 0);
      OS << ")";
    } else
      llvm_unreachable("Invalid label statement in CFGBlock.");

    OS << ":\n";
  }

  // Iterate through the statements in the block and print them.
  unsigned j = 1;

  for (CFGBlock::const_iterator I = B.begin(), E = B.end() ;
       I != E ; ++I, ++j ) {
    // Print the statement # in the basic block and the statement itself.
    if (print_edges)
      OS << " ";

    OS << llvm::format("%3d", j) << ": ";

    Helper.setStmtID(j);

    print_elem(OS, Helper, *I);
  }

  // Print the terminator of this block.
  if (B.getTerminator()) {
    if (ShowColors)
      OS.changeColor(raw_ostream::GREEN);

    OS << "   T: ";

    Helper.setBlockID(-1);

    PrintingPolicy PP(Helper.getLangOpts());
    CFGBlockTerminatorPrint TPrinter(OS, &Helper, PP);
    TPrinter.print(B.getTerminator());
    OS << '\n';

    if (ShowColors)
      OS.resetColor();
  }

  if (print_edges) {
    // Print the predecessors of this block.
    if (!B.pred_empty()) {
      const raw_ostream::Colors Color = raw_ostream::BLUE;
      if (ShowColors)
        OS.changeColor(Color);
      OS << "   Preds " ;
      if (ShowColors)
        OS.resetColor();
      OS << '(' << B.pred_size() << "):";
      unsigned i = 0;

      if (ShowColors)
        OS.changeColor(Color);

      for (CFGBlock::const_pred_iterator I = B.pred_begin(), E = B.pred_end();
           I != E; ++I, ++i) {
        if (i % 10 == 8)
          OS << "\n     ";

        CFGBlock *B = *I;
        bool Reachable = true;
        if (!B) {
          Reachable = false;
          B = I->getPossiblyUnreachableBlock();
        }

        OS << " B" << B->getBlockID();
        if (!Reachable)
          OS << "(Unreachable)";
      }

      if (ShowColors)
        OS.resetColor();

      OS << '\n';
    }

    // Print the successors of this block.
    if (!B.succ_empty()) {
      const raw_ostream::Colors Color = raw_ostream::MAGENTA;
      if (ShowColors)
        OS.changeColor(Color);
      OS << "   Succs ";
      if (ShowColors)
        OS.resetColor();
      OS << '(' << B.succ_size() << "):";
      unsigned i = 0;

      if (ShowColors)
        OS.changeColor(Color);

      for (CFGBlock::const_succ_iterator I = B.succ_begin(), E = B.succ_end();
           I != E; ++I, ++i) {
        if (i % 10 == 8)
          OS << "\n    ";

        CFGBlock *B = *I;

        bool Reachable = true;
        if (!B) {
          Reachable = false;
          B = I->getPossiblyUnreachableBlock();
        }

        if (B) {
          OS << " B" << B->getBlockID();
          if (!Reachable)
            OS << "(Unreachable)";
        }
        else {
          OS << " NULL";
        }
      }

      if (ShowColors)
        OS.resetColor();
      OS << '\n';
    }
  }
}

/// dump - A simple pretty printer of a CFG that outputs to stderr.
void CFG::dump(const LangOptions &LO, bool ShowColors) const {
  print(llvm::errs(), LO, ShowColors);
}

/// print - A simple pretty printer of a CFG that outputs to an ostream.
void CFG::print(raw_ostream &OS, const LangOptions &LO, bool ShowColors) const {
  StmtPrinterHelper Helper(this, LO);

  // Print the entry block.
  print_block(OS, this, getEntry(), Helper, true, ShowColors);

  // Iterate through the CFGBlocks and print them one by one.
  for (const_iterator I = Blocks.begin(), E = Blocks.end() ; I != E ; ++I) {
    // Skip the entry block, because we already printed it.
    if (&(**I) == &getEntry() || &(**I) == &getExit())
      continue;

    print_block(OS, this, **I, Helper, true, ShowColors);
  }

  // Print the exit block.
  print_block(OS, this, getExit(), Helper, true, ShowColors);
  OS << '\n';
  OS.flush();
}

/// dump - A simply pretty printer of a CFGBlock that outputs to stderr.
void CFGBlock::dump(const CFG* cfg, const LangOptions &LO,
                    bool ShowColors) const {
  print(llvm::errs(), cfg, LO, ShowColors);
}

LLVM_DUMP_METHOD void CFGBlock::dump() const {
  dump(getParent(), LangOptions(), false);
}

/// print - A simple pretty printer of a CFGBlock that outputs to an ostream.
///   Generally this will only be called from CFG::print.
void CFGBlock::print(raw_ostream &OS, const CFG* cfg,
                     const LangOptions &LO, bool ShowColors) const {
  StmtPrinterHelper Helper(cfg, LO);
  print_block(OS, cfg, *this, Helper, true, ShowColors);
  OS << '\n';
}

/// printTerminator - A simple pretty printer of the terminator of a CFGBlock.
void CFGBlock::printTerminator(raw_ostream &OS,
                               const LangOptions &LO) const {
  CFGBlockTerminatorPrint TPrinter(OS, nullptr, PrintingPolicy(LO));
  TPrinter.print(getTerminator());
}

Stmt *CFGBlock::getTerminatorCondition(bool StripParens) {
  Stmt *Terminator = this->Terminator;
  if (!Terminator)
    return nullptr;

  Expr *E = nullptr;

  switch (Terminator->getStmtClass()) {
    default:
      break;

    case Stmt::CXXForRangeStmtClass:
      E = cast<CXXForRangeStmt>(Terminator)->getCond();
      break;

    case Stmt::ForStmtClass:
      E = cast<ForStmt>(Terminator)->getCond();
      break;

    case Stmt::WhileStmtClass:
      E = cast<WhileStmt>(Terminator)->getCond();
      break;

    case Stmt::DoStmtClass:
      E = cast<DoStmt>(Terminator)->getCond();
      break;

    case Stmt::IfStmtClass:
      E = cast<IfStmt>(Terminator)->getCond();
      break;

    case Stmt::ChooseExprClass:
      E = cast<ChooseExpr>(Terminator)->getCond();
      break;

    case Stmt::IndirectGotoStmtClass:
      E = cast<IndirectGotoStmt>(Terminator)->getTarget();
      break;

    case Stmt::SwitchStmtClass:
      E = cast<SwitchStmt>(Terminator)->getCond();
      break;

    case Stmt::BinaryConditionalOperatorClass:
      E = cast<BinaryConditionalOperator>(Terminator)->getCond();
      break;

    case Stmt::ConditionalOperatorClass:
      E = cast<ConditionalOperator>(Terminator)->getCond();
      break;

    case Stmt::BinaryOperatorClass: // '&&' and '||'
      E = cast<BinaryOperator>(Terminator)->getLHS();
      break;

    case Stmt::ObjCForCollectionStmtClass:
      return Terminator;
  }

  if (!StripParens)
    return E;

  return E ? E->IgnoreParens() : nullptr;
}

//===----------------------------------------------------------------------===//
// CFG Graphviz Visualization
//===----------------------------------------------------------------------===//

#ifndef NDEBUG
static StmtPrinterHelper* GraphHelper;
#endif

void CFG::viewCFG(const LangOptions &LO) const {
#ifndef NDEBUG
  StmtPrinterHelper H(this, LO);
  GraphHelper = &H;
  llvm::ViewGraph(this,"CFG");
  GraphHelper = nullptr;
#endif
}

namespace llvm {

template<>
struct DOTGraphTraits<const CFG*> : public DefaultDOTGraphTraits {
  DOTGraphTraits(bool isSimple = false) : DefaultDOTGraphTraits(isSimple) {}

  static std::string getNodeLabel(const CFGBlock *Node, const CFG* Graph) {
#ifndef NDEBUG
    std::string OutSStr;
    llvm::raw_string_ostream Out(OutSStr);
    print_block(Out,Graph, *Node, *GraphHelper, false, false);
    std::string& OutStr = Out.str();

    if (OutStr[0] == '\n') OutStr.erase(OutStr.begin());

    // Process string output to make it nicer...
    for (unsigned i = 0; i != OutStr.length(); ++i)
      if (OutStr[i] == '\n') {                            // Left justify
        OutStr[i] = '\\';
        OutStr.insert(OutStr.begin()+i+1, 'l');
      }

    return OutStr;
#else
    return {};
#endif
  }
};

} // namespace llvm
