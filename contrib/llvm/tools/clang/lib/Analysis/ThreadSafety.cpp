//===- ThreadSafety.cpp ---------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// A intra-procedural analysis for thread safety (e.g. deadlocks and race
// conditions), based off of an annotation system.
//
// See http://clang.llvm.org/docs/ThreadSafetyAnalysis.html
// for more information.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/ThreadSafety.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/Type.h"
#include "clang/Analysis/Analyses/PostOrderCFGView.h"
#include "clang/Analysis/Analyses/ThreadSafetyCommon.h"
#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"
#include "clang/Analysis/Analyses/ThreadSafetyTraverse.h"
#include "clang/Analysis/Analyses/ThreadSafetyUtil.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ImmutableMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace clang;
using namespace threadSafety;

// Key method definition
ThreadSafetyHandler::~ThreadSafetyHandler() = default;

/// Issue a warning about an invalid lock expression
static void warnInvalidLock(ThreadSafetyHandler &Handler,
                            const Expr *MutexExp, const NamedDecl *D,
                            const Expr *DeclExp, StringRef Kind) {
  SourceLocation Loc;
  if (DeclExp)
    Loc = DeclExp->getExprLoc();

  // FIXME: add a note about the attribute location in MutexExp or D
  if (Loc.isValid())
    Handler.handleInvalidLockExp(Kind, Loc);
}

namespace {

/// A set of CapabilityExpr objects, which are compiled from thread safety
/// attributes on a function.
class CapExprSet : public SmallVector<CapabilityExpr, 4> {
public:
  /// Push M onto list, but discard duplicates.
  void push_back_nodup(const CapabilityExpr &CapE) {
    iterator It = std::find_if(begin(), end(),
                               [=](const CapabilityExpr &CapE2) {
      return CapE.equals(CapE2);
    });
    if (It == end())
      push_back(CapE);
  }
};

class FactManager;
class FactSet;

/// This is a helper class that stores a fact that is known at a
/// particular point in program execution.  Currently, a fact is a capability,
/// along with additional information, such as where it was acquired, whether
/// it is exclusive or shared, etc.
///
/// FIXME: this analysis does not currently support re-entrant locking.
class FactEntry : public CapabilityExpr {
private:
  /// Exclusive or shared.
  LockKind LKind;

  /// Where it was acquired.
  SourceLocation AcquireLoc;

  /// True if the lock was asserted.
  bool Asserted;

  /// True if the lock was declared.
  bool Declared;

public:
  FactEntry(const CapabilityExpr &CE, LockKind LK, SourceLocation Loc,
            bool Asrt, bool Declrd = false)
      : CapabilityExpr(CE), LKind(LK), AcquireLoc(Loc), Asserted(Asrt),
        Declared(Declrd) {}
  virtual ~FactEntry() = default;

  LockKind kind() const { return LKind;      }
  SourceLocation loc() const { return AcquireLoc; }
  bool asserted() const { return Asserted; }
  bool declared() const { return Declared; }

  void setDeclared(bool D) { Declared = D; }

  virtual void
  handleRemovalFromIntersection(const FactSet &FSet, FactManager &FactMan,
                                SourceLocation JoinLoc, LockErrorKind LEK,
                                ThreadSafetyHandler &Handler) const = 0;
  virtual void handleLock(FactSet &FSet, FactManager &FactMan,
                          const FactEntry &entry, ThreadSafetyHandler &Handler,
                          StringRef DiagKind) const = 0;
  virtual void handleUnlock(FactSet &FSet, FactManager &FactMan,
                            const CapabilityExpr &Cp, SourceLocation UnlockLoc,
                            bool FullyRemove, ThreadSafetyHandler &Handler,
                            StringRef DiagKind) const = 0;

  // Return true if LKind >= LK, where exclusive > shared
  bool isAtLeast(LockKind LK) const {
    return  (LKind == LK_Exclusive) || (LK == LK_Shared);
  }
};

using FactID = unsigned short;

/// FactManager manages the memory for all facts that are created during
/// the analysis of a single routine.
class FactManager {
private:
  std::vector<std::unique_ptr<const FactEntry>> Facts;

public:
  FactID newFact(std::unique_ptr<FactEntry> Entry) {
    Facts.push_back(std::move(Entry));
    return static_cast<unsigned short>(Facts.size() - 1);
  }

  const FactEntry &operator[](FactID F) const { return *Facts[F]; }
};

/// A FactSet is the set of facts that are known to be true at a
/// particular program point.  FactSets must be small, because they are
/// frequently copied, and are thus implemented as a set of indices into a
/// table maintained by a FactManager.  A typical FactSet only holds 1 or 2
/// locks, so we can get away with doing a linear search for lookup.  Note
/// that a hashtable or map is inappropriate in this case, because lookups
/// may involve partial pattern matches, rather than exact matches.
class FactSet {
private:
  using FactVec = SmallVector<FactID, 4>;

  FactVec FactIDs;

public:
  using iterator = FactVec::iterator;
  using const_iterator = FactVec::const_iterator;

  iterator begin() { return FactIDs.begin(); }
  const_iterator begin() const { return FactIDs.begin(); }

  iterator end() { return FactIDs.end(); }
  const_iterator end() const { return FactIDs.end(); }

  bool isEmpty() const { return FactIDs.size() == 0; }

  // Return true if the set contains only negative facts
  bool isEmpty(FactManager &FactMan) const {
    for (const auto FID : *this) {
      if (!FactMan[FID].negative())
        return false;
    }
    return true;
  }

  void addLockByID(FactID ID) { FactIDs.push_back(ID); }

  FactID addLock(FactManager &FM, std::unique_ptr<FactEntry> Entry) {
    FactID F = FM.newFact(std::move(Entry));
    FactIDs.push_back(F);
    return F;
  }

  bool removeLock(FactManager& FM, const CapabilityExpr &CapE) {
    unsigned n = FactIDs.size();
    if (n == 0)
      return false;

    for (unsigned i = 0; i < n-1; ++i) {
      if (FM[FactIDs[i]].matches(CapE)) {
        FactIDs[i] = FactIDs[n-1];
        FactIDs.pop_back();
        return true;
      }
    }
    if (FM[FactIDs[n-1]].matches(CapE)) {
      FactIDs.pop_back();
      return true;
    }
    return false;
  }

  iterator findLockIter(FactManager &FM, const CapabilityExpr &CapE) {
    return std::find_if(begin(), end(), [&](FactID ID) {
      return FM[ID].matches(CapE);
    });
  }

  const FactEntry *findLock(FactManager &FM, const CapabilityExpr &CapE) const {
    auto I = std::find_if(begin(), end(), [&](FactID ID) {
      return FM[ID].matches(CapE);
    });
    return I != end() ? &FM[*I] : nullptr;
  }

  const FactEntry *findLockUniv(FactManager &FM,
                                const CapabilityExpr &CapE) const {
    auto I = std::find_if(begin(), end(), [&](FactID ID) -> bool {
      return FM[ID].matchesUniv(CapE);
    });
    return I != end() ? &FM[*I] : nullptr;
  }

  const FactEntry *findPartialMatch(FactManager &FM,
                                    const CapabilityExpr &CapE) const {
    auto I = std::find_if(begin(), end(), [&](FactID ID) -> bool {
      return FM[ID].partiallyMatches(CapE);
    });
    return I != end() ? &FM[*I] : nullptr;
  }

  bool containsMutexDecl(FactManager &FM, const ValueDecl* Vd) const {
    auto I = std::find_if(begin(), end(), [&](FactID ID) -> bool {
      return FM[ID].valueDecl() == Vd;
    });
    return I != end();
  }
};

class ThreadSafetyAnalyzer;

} // namespace

namespace clang {
namespace threadSafety {

class BeforeSet {
private:
  using BeforeVect = SmallVector<const ValueDecl *, 4>;

  struct BeforeInfo {
    BeforeVect Vect;
    int Visited = 0;

    BeforeInfo() = default;
    BeforeInfo(BeforeInfo &&) = default;
  };

  using BeforeMap =
      llvm::DenseMap<const ValueDecl *, std::unique_ptr<BeforeInfo>>;
  using CycleMap = llvm::DenseMap<const ValueDecl *, bool>;

public:
  BeforeSet() = default;

  BeforeInfo* insertAttrExprs(const ValueDecl* Vd,
                              ThreadSafetyAnalyzer& Analyzer);

  BeforeInfo *getBeforeInfoForDecl(const ValueDecl *Vd,
                                   ThreadSafetyAnalyzer &Analyzer);

  void checkBeforeAfter(const ValueDecl* Vd,
                        const FactSet& FSet,
                        ThreadSafetyAnalyzer& Analyzer,
                        SourceLocation Loc, StringRef CapKind);

private:
  BeforeMap BMap;
  CycleMap CycMap;
};

} // namespace threadSafety
} // namespace clang

namespace {

class LocalVariableMap;

using LocalVarContext = llvm::ImmutableMap<const NamedDecl *, unsigned>;

/// A side (entry or exit) of a CFG node.
enum CFGBlockSide { CBS_Entry, CBS_Exit };

/// CFGBlockInfo is a struct which contains all the information that is
/// maintained for each block in the CFG.  See LocalVariableMap for more
/// information about the contexts.
struct CFGBlockInfo {
  // Lockset held at entry to block
  FactSet EntrySet;

  // Lockset held at exit from block
  FactSet ExitSet;

  // Context held at entry to block
  LocalVarContext EntryContext;

  // Context held at exit from block
  LocalVarContext ExitContext;

  // Location of first statement in block
  SourceLocation EntryLoc;

  // Location of last statement in block.
  SourceLocation ExitLoc;

  // Used to replay contexts later
  unsigned EntryIndex;

  // Is this block reachable?
  bool Reachable = false;

  const FactSet &getSet(CFGBlockSide Side) const {
    return Side == CBS_Entry ? EntrySet : ExitSet;
  }

  SourceLocation getLocation(CFGBlockSide Side) const {
    return Side == CBS_Entry ? EntryLoc : ExitLoc;
  }

private:
  CFGBlockInfo(LocalVarContext EmptyCtx)
      : EntryContext(EmptyCtx), ExitContext(EmptyCtx) {}

public:
  static CFGBlockInfo getEmptyBlockInfo(LocalVariableMap &M);
};

// A LocalVariableMap maintains a map from local variables to their currently
// valid definitions.  It provides SSA-like functionality when traversing the
// CFG.  Like SSA, each definition or assignment to a variable is assigned a
// unique name (an integer), which acts as the SSA name for that definition.
// The total set of names is shared among all CFG basic blocks.
// Unlike SSA, we do not rewrite expressions to replace local variables declrefs
// with their SSA-names.  Instead, we compute a Context for each point in the
// code, which maps local variables to the appropriate SSA-name.  This map
// changes with each assignment.
//
// The map is computed in a single pass over the CFG.  Subsequent analyses can
// then query the map to find the appropriate Context for a statement, and use
// that Context to look up the definitions of variables.
class LocalVariableMap {
public:
  using Context = LocalVarContext;

  /// A VarDefinition consists of an expression, representing the value of the
  /// variable, along with the context in which that expression should be
  /// interpreted.  A reference VarDefinition does not itself contain this
  /// information, but instead contains a pointer to a previous VarDefinition.
  struct VarDefinition {
  public:
    friend class LocalVariableMap;

    // The original declaration for this variable.
    const NamedDecl *Dec;

    // The expression for this variable, OR
    const Expr *Exp = nullptr;

    // Reference to another VarDefinition
    unsigned Ref = 0;

    // The map with which Exp should be interpreted.
    Context Ctx;

    bool isReference() { return !Exp; }

  private:
    // Create ordinary variable definition
    VarDefinition(const NamedDecl *D, const Expr *E, Context C)
        : Dec(D), Exp(E), Ctx(C) {}

    // Create reference to previous definition
    VarDefinition(const NamedDecl *D, unsigned R, Context C)
        : Dec(D), Ref(R), Ctx(C) {}
  };

private:
  Context::Factory ContextFactory;
  std::vector<VarDefinition> VarDefinitions;
  std::vector<unsigned> CtxIndices;
  std::vector<std::pair<const Stmt *, Context>> SavedContexts;

public:
  LocalVariableMap() {
    // index 0 is a placeholder for undefined variables (aka phi-nodes).
    VarDefinitions.push_back(VarDefinition(nullptr, 0u, getEmptyContext()));
  }

  /// Look up a definition, within the given context.
  const VarDefinition* lookup(const NamedDecl *D, Context Ctx) {
    const unsigned *i = Ctx.lookup(D);
    if (!i)
      return nullptr;
    assert(*i < VarDefinitions.size());
    return &VarDefinitions[*i];
  }

  /// Look up the definition for D within the given context.  Returns
  /// NULL if the expression is not statically known.  If successful, also
  /// modifies Ctx to hold the context of the return Expr.
  const Expr* lookupExpr(const NamedDecl *D, Context &Ctx) {
    const unsigned *P = Ctx.lookup(D);
    if (!P)
      return nullptr;

    unsigned i = *P;
    while (i > 0) {
      if (VarDefinitions[i].Exp) {
        Ctx = VarDefinitions[i].Ctx;
        return VarDefinitions[i].Exp;
      }
      i = VarDefinitions[i].Ref;
    }
    return nullptr;
  }

  Context getEmptyContext() { return ContextFactory.getEmptyMap(); }

  /// Return the next context after processing S.  This function is used by
  /// clients of the class to get the appropriate context when traversing the
  /// CFG.  It must be called for every assignment or DeclStmt.
  Context getNextContext(unsigned &CtxIndex, const Stmt *S, Context C) {
    if (SavedContexts[CtxIndex+1].first == S) {
      CtxIndex++;
      Context Result = SavedContexts[CtxIndex].second;
      return Result;
    }
    return C;
  }

  void dumpVarDefinitionName(unsigned i) {
    if (i == 0) {
      llvm::errs() << "Undefined";
      return;
    }
    const NamedDecl *Dec = VarDefinitions[i].Dec;
    if (!Dec) {
      llvm::errs() << "<<NULL>>";
      return;
    }
    Dec->printName(llvm::errs());
    llvm::errs() << "." << i << " " << ((const void*) Dec);
  }

  /// Dumps an ASCII representation of the variable map to llvm::errs()
  void dump() {
    for (unsigned i = 1, e = VarDefinitions.size(); i < e; ++i) {
      const Expr *Exp = VarDefinitions[i].Exp;
      unsigned Ref = VarDefinitions[i].Ref;

      dumpVarDefinitionName(i);
      llvm::errs() << " = ";
      if (Exp) Exp->dump();
      else {
        dumpVarDefinitionName(Ref);
        llvm::errs() << "\n";
      }
    }
  }

  /// Dumps an ASCII representation of a Context to llvm::errs()
  void dumpContext(Context C) {
    for (Context::iterator I = C.begin(), E = C.end(); I != E; ++I) {
      const NamedDecl *D = I.getKey();
      D->printName(llvm::errs());
      const unsigned *i = C.lookup(D);
      llvm::errs() << " -> ";
      dumpVarDefinitionName(*i);
      llvm::errs() << "\n";
    }
  }

  /// Builds the variable map.
  void traverseCFG(CFG *CFGraph, const PostOrderCFGView *SortedGraph,
                   std::vector<CFGBlockInfo> &BlockInfo);

protected:
  friend class VarMapBuilder;

  // Get the current context index
  unsigned getContextIndex() { return SavedContexts.size()-1; }

  // Save the current context for later replay
  void saveContext(const Stmt *S, Context C) {
    SavedContexts.push_back(std::make_pair(S, C));
  }

  // Adds a new definition to the given context, and returns a new context.
  // This method should be called when declaring a new variable.
  Context addDefinition(const NamedDecl *D, const Expr *Exp, Context Ctx) {
    assert(!Ctx.contains(D));
    unsigned newID = VarDefinitions.size();
    Context NewCtx = ContextFactory.add(Ctx, D, newID);
    VarDefinitions.push_back(VarDefinition(D, Exp, Ctx));
    return NewCtx;
  }

  // Add a new reference to an existing definition.
  Context addReference(const NamedDecl *D, unsigned i, Context Ctx) {
    unsigned newID = VarDefinitions.size();
    Context NewCtx = ContextFactory.add(Ctx, D, newID);
    VarDefinitions.push_back(VarDefinition(D, i, Ctx));
    return NewCtx;
  }

  // Updates a definition only if that definition is already in the map.
  // This method should be called when assigning to an existing variable.
  Context updateDefinition(const NamedDecl *D, Expr *Exp, Context Ctx) {
    if (Ctx.contains(D)) {
      unsigned newID = VarDefinitions.size();
      Context NewCtx = ContextFactory.remove(Ctx, D);
      NewCtx = ContextFactory.add(NewCtx, D, newID);
      VarDefinitions.push_back(VarDefinition(D, Exp, Ctx));
      return NewCtx;
    }
    return Ctx;
  }

  // Removes a definition from the context, but keeps the variable name
  // as a valid variable.  The index 0 is a placeholder for cleared definitions.
  Context clearDefinition(const NamedDecl *D, Context Ctx) {
    Context NewCtx = Ctx;
    if (NewCtx.contains(D)) {
      NewCtx = ContextFactory.remove(NewCtx, D);
      NewCtx = ContextFactory.add(NewCtx, D, 0);
    }
    return NewCtx;
  }

  // Remove a definition entirely frmo the context.
  Context removeDefinition(const NamedDecl *D, Context Ctx) {
    Context NewCtx = Ctx;
    if (NewCtx.contains(D)) {
      NewCtx = ContextFactory.remove(NewCtx, D);
    }
    return NewCtx;
  }

  Context intersectContexts(Context C1, Context C2);
  Context createReferenceContext(Context C);
  void intersectBackEdge(Context C1, Context C2);
};

} // namespace

// This has to be defined after LocalVariableMap.
CFGBlockInfo CFGBlockInfo::getEmptyBlockInfo(LocalVariableMap &M) {
  return CFGBlockInfo(M.getEmptyContext());
}

namespace {

/// Visitor which builds a LocalVariableMap
class VarMapBuilder : public ConstStmtVisitor<VarMapBuilder> {
public:
  LocalVariableMap* VMap;
  LocalVariableMap::Context Ctx;

  VarMapBuilder(LocalVariableMap *VM, LocalVariableMap::Context C)
      : VMap(VM), Ctx(C) {}

  void VisitDeclStmt(const DeclStmt *S);
  void VisitBinaryOperator(const BinaryOperator *BO);
};

} // namespace

// Add new local variables to the variable map
void VarMapBuilder::VisitDeclStmt(const DeclStmt *S) {
  bool modifiedCtx = false;
  const DeclGroupRef DGrp = S->getDeclGroup();
  for (const auto *D : DGrp) {
    if (const auto *VD = dyn_cast_or_null<VarDecl>(D)) {
      const Expr *E = VD->getInit();

      // Add local variables with trivial type to the variable map
      QualType T = VD->getType();
      if (T.isTrivialType(VD->getASTContext())) {
        Ctx = VMap->addDefinition(VD, E, Ctx);
        modifiedCtx = true;
      }
    }
  }
  if (modifiedCtx)
    VMap->saveContext(S, Ctx);
}

// Update local variable definitions in variable map
void VarMapBuilder::VisitBinaryOperator(const BinaryOperator *BO) {
  if (!BO->isAssignmentOp())
    return;

  Expr *LHSExp = BO->getLHS()->IgnoreParenCasts();

  // Update the variable map and current context.
  if (const auto *DRE = dyn_cast<DeclRefExpr>(LHSExp)) {
    const ValueDecl *VDec = DRE->getDecl();
    if (Ctx.lookup(VDec)) {
      if (BO->getOpcode() == BO_Assign)
        Ctx = VMap->updateDefinition(VDec, BO->getRHS(), Ctx);
      else
        // FIXME -- handle compound assignment operators
        Ctx = VMap->clearDefinition(VDec, Ctx);
      VMap->saveContext(BO, Ctx);
    }
  }
}

// Computes the intersection of two contexts.  The intersection is the
// set of variables which have the same definition in both contexts;
// variables with different definitions are discarded.
LocalVariableMap::Context
LocalVariableMap::intersectContexts(Context C1, Context C2) {
  Context Result = C1;
  for (const auto &P : C1) {
    const NamedDecl *Dec = P.first;
    const unsigned *i2 = C2.lookup(Dec);
    if (!i2)             // variable doesn't exist on second path
      Result = removeDefinition(Dec, Result);
    else if (*i2 != P.second)  // variable exists, but has different definition
      Result = clearDefinition(Dec, Result);
  }
  return Result;
}

// For every variable in C, create a new variable that refers to the
// definition in C.  Return a new context that contains these new variables.
// (We use this for a naive implementation of SSA on loop back-edges.)
LocalVariableMap::Context LocalVariableMap::createReferenceContext(Context C) {
  Context Result = getEmptyContext();
  for (const auto &P : C)
    Result = addReference(P.first, P.second, Result);
  return Result;
}

// This routine also takes the intersection of C1 and C2, but it does so by
// altering the VarDefinitions.  C1 must be the result of an earlier call to
// createReferenceContext.
void LocalVariableMap::intersectBackEdge(Context C1, Context C2) {
  for (const auto &P : C1) {
    unsigned i1 = P.second;
    VarDefinition *VDef = &VarDefinitions[i1];
    assert(VDef->isReference());

    const unsigned *i2 = C2.lookup(P.first);
    if (!i2 || (*i2 != i1))
      VDef->Ref = 0;    // Mark this variable as undefined
  }
}

// Traverse the CFG in topological order, so all predecessors of a block
// (excluding back-edges) are visited before the block itself.  At
// each point in the code, we calculate a Context, which holds the set of
// variable definitions which are visible at that point in execution.
// Visible variables are mapped to their definitions using an array that
// contains all definitions.
//
// At join points in the CFG, the set is computed as the intersection of
// the incoming sets along each edge, E.g.
//
//                       { Context                 | VarDefinitions }
//   int x = 0;          { x -> x1                 | x1 = 0 }
//   int y = 0;          { x -> x1, y -> y1        | y1 = 0, x1 = 0 }
//   if (b) x = 1;       { x -> x2, y -> y1        | x2 = 1, y1 = 0, ... }
//   else   x = 2;       { x -> x3, y -> y1        | x3 = 2, x2 = 1, ... }
//   ...                 { y -> y1  (x is unknown) | x3 = 2, x2 = 1, ... }
//
// This is essentially a simpler and more naive version of the standard SSA
// algorithm.  Those definitions that remain in the intersection are from blocks
// that strictly dominate the current block.  We do not bother to insert proper
// phi nodes, because they are not used in our analysis; instead, wherever
// a phi node would be required, we simply remove that definition from the
// context (E.g. x above).
//
// The initial traversal does not capture back-edges, so those need to be
// handled on a separate pass.  Whenever the first pass encounters an
// incoming back edge, it duplicates the context, creating new definitions
// that refer back to the originals.  (These correspond to places where SSA
// might have to insert a phi node.)  On the second pass, these definitions are
// set to NULL if the variable has changed on the back-edge (i.e. a phi
// node was actually required.)  E.g.
//
//                       { Context           | VarDefinitions }
//   int x = 0, y = 0;   { x -> x1, y -> y1  | y1 = 0, x1 = 0 }
//   while (b)           { x -> x2, y -> y1  | [1st:] x2=x1; [2nd:] x2=NULL; }
//     x = x+1;          { x -> x3, y -> y1  | x3 = x2 + 1, ... }
//   ...                 { y -> y1           | x3 = 2, x2 = 1, ... }
void LocalVariableMap::traverseCFG(CFG *CFGraph,
                                   const PostOrderCFGView *SortedGraph,
                                   std::vector<CFGBlockInfo> &BlockInfo) {
  PostOrderCFGView::CFGBlockSet VisitedBlocks(CFGraph);

  CtxIndices.resize(CFGraph->getNumBlockIDs());

  for (const auto *CurrBlock : *SortedGraph) {
    unsigned CurrBlockID = CurrBlock->getBlockID();
    CFGBlockInfo *CurrBlockInfo = &BlockInfo[CurrBlockID];

    VisitedBlocks.insert(CurrBlock);

    // Calculate the entry context for the current block
    bool HasBackEdges = false;
    bool CtxInit = true;
    for (CFGBlock::const_pred_iterator PI = CurrBlock->pred_begin(),
         PE  = CurrBlock->pred_end(); PI != PE; ++PI) {
      // if *PI -> CurrBlock is a back edge, so skip it
      if (*PI == nullptr || !VisitedBlocks.alreadySet(*PI)) {
        HasBackEdges = true;
        continue;
      }

      unsigned PrevBlockID = (*PI)->getBlockID();
      CFGBlockInfo *PrevBlockInfo = &BlockInfo[PrevBlockID];

      if (CtxInit) {
        CurrBlockInfo->EntryContext = PrevBlockInfo->ExitContext;
        CtxInit = false;
      }
      else {
        CurrBlockInfo->EntryContext =
          intersectContexts(CurrBlockInfo->EntryContext,
                            PrevBlockInfo->ExitContext);
      }
    }

    // Duplicate the context if we have back-edges, so we can call
    // intersectBackEdges later.
    if (HasBackEdges)
      CurrBlockInfo->EntryContext =
        createReferenceContext(CurrBlockInfo->EntryContext);

    // Create a starting context index for the current block
    saveContext(nullptr, CurrBlockInfo->EntryContext);
    CurrBlockInfo->EntryIndex = getContextIndex();

    // Visit all the statements in the basic block.
    VarMapBuilder VMapBuilder(this, CurrBlockInfo->EntryContext);
    for (const auto &BI : *CurrBlock) {
      switch (BI.getKind()) {
        case CFGElement::Statement: {
          CFGStmt CS = BI.castAs<CFGStmt>();
          VMapBuilder.Visit(CS.getStmt());
          break;
        }
        default:
          break;
      }
    }
    CurrBlockInfo->ExitContext = VMapBuilder.Ctx;

    // Mark variables on back edges as "unknown" if they've been changed.
    for (CFGBlock::const_succ_iterator SI = CurrBlock->succ_begin(),
         SE  = CurrBlock->succ_end(); SI != SE; ++SI) {
      // if CurrBlock -> *SI is *not* a back edge
      if (*SI == nullptr || !VisitedBlocks.alreadySet(*SI))
        continue;

      CFGBlock *FirstLoopBlock = *SI;
      Context LoopBegin = BlockInfo[FirstLoopBlock->getBlockID()].EntryContext;
      Context LoopEnd   = CurrBlockInfo->ExitContext;
      intersectBackEdge(LoopBegin, LoopEnd);
    }
  }

  // Put an extra entry at the end of the indexed context array
  unsigned exitID = CFGraph->getExit().getBlockID();
  saveContext(nullptr, BlockInfo[exitID].ExitContext);
}

/// Find the appropriate source locations to use when producing diagnostics for
/// each block in the CFG.
static void findBlockLocations(CFG *CFGraph,
                               const PostOrderCFGView *SortedGraph,
                               std::vector<CFGBlockInfo> &BlockInfo) {
  for (const auto *CurrBlock : *SortedGraph) {
    CFGBlockInfo *CurrBlockInfo = &BlockInfo[CurrBlock->getBlockID()];

    // Find the source location of the last statement in the block, if the
    // block is not empty.
    if (const Stmt *S = CurrBlock->getTerminator()) {
      CurrBlockInfo->EntryLoc = CurrBlockInfo->ExitLoc = S->getBeginLoc();
    } else {
      for (CFGBlock::const_reverse_iterator BI = CurrBlock->rbegin(),
           BE = CurrBlock->rend(); BI != BE; ++BI) {
        // FIXME: Handle other CFGElement kinds.
        if (Optional<CFGStmt> CS = BI->getAs<CFGStmt>()) {
          CurrBlockInfo->ExitLoc = CS->getStmt()->getBeginLoc();
          break;
        }
      }
    }

    if (CurrBlockInfo->ExitLoc.isValid()) {
      // This block contains at least one statement. Find the source location
      // of the first statement in the block.
      for (const auto &BI : *CurrBlock) {
        // FIXME: Handle other CFGElement kinds.
        if (Optional<CFGStmt> CS = BI.getAs<CFGStmt>()) {
          CurrBlockInfo->EntryLoc = CS->getStmt()->getBeginLoc();
          break;
        }
      }
    } else if (CurrBlock->pred_size() == 1 && *CurrBlock->pred_begin() &&
               CurrBlock != &CFGraph->getExit()) {
      // The block is empty, and has a single predecessor. Use its exit
      // location.
      CurrBlockInfo->EntryLoc = CurrBlockInfo->ExitLoc =
          BlockInfo[(*CurrBlock->pred_begin())->getBlockID()].ExitLoc;
    }
  }
}

namespace {

class LockableFactEntry : public FactEntry {
private:
  /// managed by ScopedLockable object
  bool Managed;

public:
  LockableFactEntry(const CapabilityExpr &CE, LockKind LK, SourceLocation Loc,
                    bool Mng = false, bool Asrt = false)
      : FactEntry(CE, LK, Loc, Asrt), Managed(Mng) {}

  void
  handleRemovalFromIntersection(const FactSet &FSet, FactManager &FactMan,
                                SourceLocation JoinLoc, LockErrorKind LEK,
                                ThreadSafetyHandler &Handler) const override {
    if (!Managed && !asserted() && !negative() && !isUniversal()) {
      Handler.handleMutexHeldEndOfScope("mutex", toString(), loc(), JoinLoc,
                                        LEK);
    }
  }

  void handleLock(FactSet &FSet, FactManager &FactMan, const FactEntry &entry,
                  ThreadSafetyHandler &Handler,
                  StringRef DiagKind) const override {
    Handler.handleDoubleLock(DiagKind, entry.toString(), entry.loc());
  }

  void handleUnlock(FactSet &FSet, FactManager &FactMan,
                    const CapabilityExpr &Cp, SourceLocation UnlockLoc,
                    bool FullyRemove, ThreadSafetyHandler &Handler,
                    StringRef DiagKind) const override {
    FSet.removeLock(FactMan, Cp);
    if (!Cp.negative()) {
      FSet.addLock(FactMan, llvm::make_unique<LockableFactEntry>(
                                !Cp, LK_Exclusive, UnlockLoc));
    }
  }
};

class ScopedLockableFactEntry : public FactEntry {
private:
  enum UnderlyingCapabilityKind {
    UCK_Acquired,          ///< Any kind of acquired capability.
    UCK_ReleasedShared,    ///< Shared capability that was released.
    UCK_ReleasedExclusive, ///< Exclusive capability that was released.
  };

  using UnderlyingCapability =
      llvm::PointerIntPair<const til::SExpr *, 2, UnderlyingCapabilityKind>;

  SmallVector<UnderlyingCapability, 4> UnderlyingMutexes;

public:
  ScopedLockableFactEntry(const CapabilityExpr &CE, SourceLocation Loc)
      : FactEntry(CE, LK_Exclusive, Loc, false) {}

  void addExclusiveLock(const CapabilityExpr &M) {
    UnderlyingMutexes.emplace_back(M.sexpr(), UCK_Acquired);
  }

  void addSharedLock(const CapabilityExpr &M) {
    UnderlyingMutexes.emplace_back(M.sexpr(), UCK_Acquired);
  }

  void addExclusiveUnlock(const CapabilityExpr &M) {
    UnderlyingMutexes.emplace_back(M.sexpr(), UCK_ReleasedExclusive);
  }

  void addSharedUnlock(const CapabilityExpr &M) {
    UnderlyingMutexes.emplace_back(M.sexpr(), UCK_ReleasedShared);
  }

  void
  handleRemovalFromIntersection(const FactSet &FSet, FactManager &FactMan,
                                SourceLocation JoinLoc, LockErrorKind LEK,
                                ThreadSafetyHandler &Handler) const override {
    for (const auto &UnderlyingMutex : UnderlyingMutexes) {
      const auto *Entry = FSet.findLock(
          FactMan, CapabilityExpr(UnderlyingMutex.getPointer(), false));
      if ((UnderlyingMutex.getInt() == UCK_Acquired && Entry) ||
          (UnderlyingMutex.getInt() != UCK_Acquired && !Entry)) {
        // If this scoped lock manages another mutex, and if the underlying
        // mutex is still/not held, then warn about the underlying mutex.
        Handler.handleMutexHeldEndOfScope(
            "mutex", sx::toString(UnderlyingMutex.getPointer()), loc(), JoinLoc,
            LEK);
      }
    }
  }

  void handleLock(FactSet &FSet, FactManager &FactMan, const FactEntry &entry,
                  ThreadSafetyHandler &Handler,
                  StringRef DiagKind) const override {
    for (const auto &UnderlyingMutex : UnderlyingMutexes) {
      CapabilityExpr UnderCp(UnderlyingMutex.getPointer(), false);

      if (UnderlyingMutex.getInt() == UCK_Acquired)
        lock(FSet, FactMan, UnderCp, entry.kind(), entry.loc(), &Handler,
             DiagKind);
      else
        unlock(FSet, FactMan, UnderCp, entry.loc(), &Handler, DiagKind);
    }
  }

  void handleUnlock(FactSet &FSet, FactManager &FactMan,
                    const CapabilityExpr &Cp, SourceLocation UnlockLoc,
                    bool FullyRemove, ThreadSafetyHandler &Handler,
                    StringRef DiagKind) const override {
    assert(!Cp.negative() && "Managing object cannot be negative.");
    for (const auto &UnderlyingMutex : UnderlyingMutexes) {
      CapabilityExpr UnderCp(UnderlyingMutex.getPointer(), false);

      // Remove/lock the underlying mutex if it exists/is still unlocked; warn
      // on double unlocking/locking if we're not destroying the scoped object.
      ThreadSafetyHandler *TSHandler = FullyRemove ? nullptr : &Handler;
      if (UnderlyingMutex.getInt() == UCK_Acquired) {
        unlock(FSet, FactMan, UnderCp, UnlockLoc, TSHandler, DiagKind);
      } else {
        LockKind kind = UnderlyingMutex.getInt() == UCK_ReleasedShared
                            ? LK_Shared
                            : LK_Exclusive;
        lock(FSet, FactMan, UnderCp, kind, UnlockLoc, TSHandler, DiagKind);
      }
    }
    if (FullyRemove)
      FSet.removeLock(FactMan, Cp);
  }

private:
  void lock(FactSet &FSet, FactManager &FactMan, const CapabilityExpr &Cp,
            LockKind kind, SourceLocation loc, ThreadSafetyHandler *Handler,
            StringRef DiagKind) const {
    if (!FSet.findLock(FactMan, Cp)) {
      FSet.removeLock(FactMan, !Cp);
      FSet.addLock(FactMan,
                   llvm::make_unique<LockableFactEntry>(Cp, kind, loc));
    } else if (Handler) {
      Handler->handleDoubleLock(DiagKind, Cp.toString(), loc);
    }
  }

  void unlock(FactSet &FSet, FactManager &FactMan, const CapabilityExpr &Cp,
              SourceLocation loc, ThreadSafetyHandler *Handler,
              StringRef DiagKind) const {
    if (FSet.findLock(FactMan, Cp)) {
      FSet.removeLock(FactMan, Cp);
      FSet.addLock(FactMan, llvm::make_unique<LockableFactEntry>(
                                !Cp, LK_Exclusive, loc));
    } else if (Handler) {
      Handler->handleUnmatchedUnlock(DiagKind, Cp.toString(), loc);
    }
  }
};

/// Class which implements the core thread safety analysis routines.
class ThreadSafetyAnalyzer {
  friend class BuildLockset;
  friend class threadSafety::BeforeSet;

  llvm::BumpPtrAllocator Bpa;
  threadSafety::til::MemRegionRef Arena;
  threadSafety::SExprBuilder SxBuilder;

  ThreadSafetyHandler &Handler;
  const CXXMethodDecl *CurrentMethod;
  LocalVariableMap LocalVarMap;
  FactManager FactMan;
  std::vector<CFGBlockInfo> BlockInfo;

  BeforeSet *GlobalBeforeSet;

public:
  ThreadSafetyAnalyzer(ThreadSafetyHandler &H, BeforeSet* Bset)
      : Arena(&Bpa), SxBuilder(Arena), Handler(H), GlobalBeforeSet(Bset) {}

  bool inCurrentScope(const CapabilityExpr &CapE);

  void addLock(FactSet &FSet, std::unique_ptr<FactEntry> Entry,
               StringRef DiagKind, bool ReqAttr = false);
  void removeLock(FactSet &FSet, const CapabilityExpr &CapE,
                  SourceLocation UnlockLoc, bool FullyRemove, LockKind Kind,
                  StringRef DiagKind);

  template <typename AttrType>
  void getMutexIDs(CapExprSet &Mtxs, AttrType *Attr, const Expr *Exp,
                   const NamedDecl *D, VarDecl *SelfDecl = nullptr);

  template <class AttrType>
  void getMutexIDs(CapExprSet &Mtxs, AttrType *Attr, const Expr *Exp,
                   const NamedDecl *D,
                   const CFGBlock *PredBlock, const CFGBlock *CurrBlock,
                   Expr *BrE, bool Neg);

  const CallExpr* getTrylockCallExpr(const Stmt *Cond, LocalVarContext C,
                                     bool &Negate);

  void getEdgeLockset(FactSet &Result, const FactSet &ExitSet,
                      const CFGBlock* PredBlock,
                      const CFGBlock *CurrBlock);

  void intersectAndWarn(FactSet &FSet1, const FactSet &FSet2,
                        SourceLocation JoinLoc,
                        LockErrorKind LEK1, LockErrorKind LEK2,
                        bool Modify=true);

  void intersectAndWarn(FactSet &FSet1, const FactSet &FSet2,
                        SourceLocation JoinLoc, LockErrorKind LEK1,
                        bool Modify=true) {
    intersectAndWarn(FSet1, FSet2, JoinLoc, LEK1, LEK1, Modify);
  }

  void runAnalysis(AnalysisDeclContext &AC);
};

} // namespace

/// Process acquired_before and acquired_after attributes on Vd.
BeforeSet::BeforeInfo* BeforeSet::insertAttrExprs(const ValueDecl* Vd,
    ThreadSafetyAnalyzer& Analyzer) {
  // Create a new entry for Vd.
  BeforeInfo *Info = nullptr;
  {
    // Keep InfoPtr in its own scope in case BMap is modified later and the
    // reference becomes invalid.
    std::unique_ptr<BeforeInfo> &InfoPtr = BMap[Vd];
    if (!InfoPtr)
      InfoPtr.reset(new BeforeInfo());
    Info = InfoPtr.get();
  }

  for (const auto *At : Vd->attrs()) {
    switch (At->getKind()) {
      case attr::AcquiredBefore: {
        const auto *A = cast<AcquiredBeforeAttr>(At);

        // Read exprs from the attribute, and add them to BeforeVect.
        for (const auto *Arg : A->args()) {
          CapabilityExpr Cp =
            Analyzer.SxBuilder.translateAttrExpr(Arg, nullptr);
          if (const ValueDecl *Cpvd = Cp.valueDecl()) {
            Info->Vect.push_back(Cpvd);
            const auto It = BMap.find(Cpvd);
            if (It == BMap.end())
              insertAttrExprs(Cpvd, Analyzer);
          }
        }
        break;
      }
      case attr::AcquiredAfter: {
        const auto *A = cast<AcquiredAfterAttr>(At);

        // Read exprs from the attribute, and add them to BeforeVect.
        for (const auto *Arg : A->args()) {
          CapabilityExpr Cp =
            Analyzer.SxBuilder.translateAttrExpr(Arg, nullptr);
          if (const ValueDecl *ArgVd = Cp.valueDecl()) {
            // Get entry for mutex listed in attribute
            BeforeInfo *ArgInfo = getBeforeInfoForDecl(ArgVd, Analyzer);
            ArgInfo->Vect.push_back(Vd);
          }
        }
        break;
      }
      default:
        break;
    }
  }

  return Info;
}

BeforeSet::BeforeInfo *
BeforeSet::getBeforeInfoForDecl(const ValueDecl *Vd,
                                ThreadSafetyAnalyzer &Analyzer) {
  auto It = BMap.find(Vd);
  BeforeInfo *Info = nullptr;
  if (It == BMap.end())
    Info = insertAttrExprs(Vd, Analyzer);
  else
    Info = It->second.get();
  assert(Info && "BMap contained nullptr?");
  return Info;
}

/// Return true if any mutexes in FSet are in the acquired_before set of Vd.
void BeforeSet::checkBeforeAfter(const ValueDecl* StartVd,
                                 const FactSet& FSet,
                                 ThreadSafetyAnalyzer& Analyzer,
                                 SourceLocation Loc, StringRef CapKind) {
  SmallVector<BeforeInfo*, 8> InfoVect;

  // Do a depth-first traversal of Vd.
  // Return true if there are cycles.
  std::function<bool (const ValueDecl*)> traverse = [&](const ValueDecl* Vd) {
    if (!Vd)
      return false;

    BeforeSet::BeforeInfo *Info = getBeforeInfoForDecl(Vd, Analyzer);

    if (Info->Visited == 1)
      return true;

    if (Info->Visited == 2)
      return false;

    if (Info->Vect.empty())
      return false;

    InfoVect.push_back(Info);
    Info->Visited = 1;
    for (const auto *Vdb : Info->Vect) {
      // Exclude mutexes in our immediate before set.
      if (FSet.containsMutexDecl(Analyzer.FactMan, Vdb)) {
        StringRef L1 = StartVd->getName();
        StringRef L2 = Vdb->getName();
        Analyzer.Handler.handleLockAcquiredBefore(CapKind, L1, L2, Loc);
      }
      // Transitively search other before sets, and warn on cycles.
      if (traverse(Vdb)) {
        if (CycMap.find(Vd) == CycMap.end()) {
          CycMap.insert(std::make_pair(Vd, true));
          StringRef L1 = Vd->getName();
          Analyzer.Handler.handleBeforeAfterCycle(L1, Vd->getLocation());
        }
      }
    }
    Info->Visited = 2;
    return false;
  };

  traverse(StartVd);

  for (auto *Info : InfoVect)
    Info->Visited = 0;
}

/// Gets the value decl pointer from DeclRefExprs or MemberExprs.
static const ValueDecl *getValueDecl(const Expr *Exp) {
  if (const auto *CE = dyn_cast<ImplicitCastExpr>(Exp))
    return getValueDecl(CE->getSubExpr());

  if (const auto *DR = dyn_cast<DeclRefExpr>(Exp))
    return DR->getDecl();

  if (const auto *ME = dyn_cast<MemberExpr>(Exp))
    return ME->getMemberDecl();

  return nullptr;
}

namespace {

template <typename Ty>
class has_arg_iterator_range {
  using yes = char[1];
  using no = char[2];

  template <typename Inner>
  static yes& test(Inner *I, decltype(I->args()) * = nullptr);

  template <typename>
  static no& test(...);

public:
  static const bool value = sizeof(test<Ty>(nullptr)) == sizeof(yes);
};

} // namespace

static StringRef ClassifyDiagnostic(const CapabilityAttr *A) {
  return A->getName();
}

static StringRef ClassifyDiagnostic(QualType VDT) {
  // We need to look at the declaration of the type of the value to determine
  // which it is. The type should either be a record or a typedef, or a pointer
  // or reference thereof.
  if (const auto *RT = VDT->getAs<RecordType>()) {
    if (const auto *RD = RT->getDecl())
      if (const auto *CA = RD->getAttr<CapabilityAttr>())
        return ClassifyDiagnostic(CA);
  } else if (const auto *TT = VDT->getAs<TypedefType>()) {
    if (const auto *TD = TT->getDecl())
      if (const auto *CA = TD->getAttr<CapabilityAttr>())
        return ClassifyDiagnostic(CA);
  } else if (VDT->isPointerType() || VDT->isReferenceType())
    return ClassifyDiagnostic(VDT->getPointeeType());

  return "mutex";
}

static StringRef ClassifyDiagnostic(const ValueDecl *VD) {
  assert(VD && "No ValueDecl passed");

  // The ValueDecl is the declaration of a mutex or role (hopefully).
  return ClassifyDiagnostic(VD->getType());
}

template <typename AttrTy>
static typename std::enable_if<!has_arg_iterator_range<AttrTy>::value,
                               StringRef>::type
ClassifyDiagnostic(const AttrTy *A) {
  if (const ValueDecl *VD = getValueDecl(A->getArg()))
    return ClassifyDiagnostic(VD);
  return "mutex";
}

template <typename AttrTy>
static typename std::enable_if<has_arg_iterator_range<AttrTy>::value,
                               StringRef>::type
ClassifyDiagnostic(const AttrTy *A) {
  for (const auto *Arg : A->args()) {
    if (const ValueDecl *VD = getValueDecl(Arg))
      return ClassifyDiagnostic(VD);
  }
  return "mutex";
}

bool ThreadSafetyAnalyzer::inCurrentScope(const CapabilityExpr &CapE) {
  if (!CurrentMethod)
      return false;
  if (const auto *P = dyn_cast_or_null<til::Project>(CapE.sexpr())) {
    const auto *VD = P->clangDecl();
    if (VD)
      return VD->getDeclContext() == CurrentMethod->getDeclContext();
  }
  return false;
}

/// Add a new lock to the lockset, warning if the lock is already there.
/// \param ReqAttr -- true if this is part of an initial Requires attribute.
void ThreadSafetyAnalyzer::addLock(FactSet &FSet,
                                   std::unique_ptr<FactEntry> Entry,
                                   StringRef DiagKind, bool ReqAttr) {
  if (Entry->shouldIgnore())
    return;

  if (!ReqAttr && !Entry->negative()) {
    // look for the negative capability, and remove it from the fact set.
    CapabilityExpr NegC = !*Entry;
    const FactEntry *Nen = FSet.findLock(FactMan, NegC);
    if (Nen) {
      FSet.removeLock(FactMan, NegC);
    }
    else {
      if (inCurrentScope(*Entry) && !Entry->asserted())
        Handler.handleNegativeNotHeld(DiagKind, Entry->toString(),
                                      NegC.toString(), Entry->loc());
    }
  }

  // Check before/after constraints
  if (Handler.issueBetaWarnings() &&
      !Entry->asserted() && !Entry->declared()) {
    GlobalBeforeSet->checkBeforeAfter(Entry->valueDecl(), FSet, *this,
                                      Entry->loc(), DiagKind);
  }

  // FIXME: Don't always warn when we have support for reentrant locks.
  if (const FactEntry *Cp = FSet.findLock(FactMan, *Entry)) {
    if (!Entry->asserted())
      Cp->handleLock(FSet, FactMan, *Entry, Handler, DiagKind);
  } else {
    FSet.addLock(FactMan, std::move(Entry));
  }
}

/// Remove a lock from the lockset, warning if the lock is not there.
/// \param UnlockLoc The source location of the unlock (only used in error msg)
void ThreadSafetyAnalyzer::removeLock(FactSet &FSet, const CapabilityExpr &Cp,
                                      SourceLocation UnlockLoc,
                                      bool FullyRemove, LockKind ReceivedKind,
                                      StringRef DiagKind) {
  if (Cp.shouldIgnore())
    return;

  const FactEntry *LDat = FSet.findLock(FactMan, Cp);
  if (!LDat) {
    Handler.handleUnmatchedUnlock(DiagKind, Cp.toString(), UnlockLoc);
    return;
  }

  // Generic lock removal doesn't care about lock kind mismatches, but
  // otherwise diagnose when the lock kinds are mismatched.
  if (ReceivedKind != LK_Generic && LDat->kind() != ReceivedKind) {
    Handler.handleIncorrectUnlockKind(DiagKind, Cp.toString(),
                                      LDat->kind(), ReceivedKind, UnlockLoc);
  }

  LDat->handleUnlock(FSet, FactMan, Cp, UnlockLoc, FullyRemove, Handler,
                     DiagKind);
}

/// Extract the list of mutexIDs from the attribute on an expression,
/// and push them onto Mtxs, discarding any duplicates.
template <typename AttrType>
void ThreadSafetyAnalyzer::getMutexIDs(CapExprSet &Mtxs, AttrType *Attr,
                                       const Expr *Exp, const NamedDecl *D,
                                       VarDecl *SelfDecl) {
  if (Attr->args_size() == 0) {
    // The mutex held is the "this" object.
    CapabilityExpr Cp = SxBuilder.translateAttrExpr(nullptr, D, Exp, SelfDecl);
    if (Cp.isInvalid()) {
       warnInvalidLock(Handler, nullptr, D, Exp, ClassifyDiagnostic(Attr));
       return;
    }
    //else
    if (!Cp.shouldIgnore())
      Mtxs.push_back_nodup(Cp);
    return;
  }

  for (const auto *Arg : Attr->args()) {
    CapabilityExpr Cp = SxBuilder.translateAttrExpr(Arg, D, Exp, SelfDecl);
    if (Cp.isInvalid()) {
       warnInvalidLock(Handler, nullptr, D, Exp, ClassifyDiagnostic(Attr));
       continue;
    }
    //else
    if (!Cp.shouldIgnore())
      Mtxs.push_back_nodup(Cp);
  }
}

/// Extract the list of mutexIDs from a trylock attribute.  If the
/// trylock applies to the given edge, then push them onto Mtxs, discarding
/// any duplicates.
template <class AttrType>
void ThreadSafetyAnalyzer::getMutexIDs(CapExprSet &Mtxs, AttrType *Attr,
                                       const Expr *Exp, const NamedDecl *D,
                                       const CFGBlock *PredBlock,
                                       const CFGBlock *CurrBlock,
                                       Expr *BrE, bool Neg) {
  // Find out which branch has the lock
  bool branch = false;
  if (const auto *BLE = dyn_cast_or_null<CXXBoolLiteralExpr>(BrE))
    branch = BLE->getValue();
  else if (const auto *ILE = dyn_cast_or_null<IntegerLiteral>(BrE))
    branch = ILE->getValue().getBoolValue();

  int branchnum = branch ? 0 : 1;
  if (Neg)
    branchnum = !branchnum;

  // If we've taken the trylock branch, then add the lock
  int i = 0;
  for (CFGBlock::const_succ_iterator SI = PredBlock->succ_begin(),
       SE = PredBlock->succ_end(); SI != SE && i < 2; ++SI, ++i) {
    if (*SI == CurrBlock && i == branchnum)
      getMutexIDs(Mtxs, Attr, Exp, D);
  }
}

static bool getStaticBooleanValue(Expr *E, bool &TCond) {
  if (isa<CXXNullPtrLiteralExpr>(E) || isa<GNUNullExpr>(E)) {
    TCond = false;
    return true;
  } else if (const auto *BLE = dyn_cast<CXXBoolLiteralExpr>(E)) {
    TCond = BLE->getValue();
    return true;
  } else if (const auto *ILE = dyn_cast<IntegerLiteral>(E)) {
    TCond = ILE->getValue().getBoolValue();
    return true;
  } else if (auto *CE = dyn_cast<ImplicitCastExpr>(E))
    return getStaticBooleanValue(CE->getSubExpr(), TCond);
  return false;
}

// If Cond can be traced back to a function call, return the call expression.
// The negate variable should be called with false, and will be set to true
// if the function call is negated, e.g. if (!mu.tryLock(...))
const CallExpr* ThreadSafetyAnalyzer::getTrylockCallExpr(const Stmt *Cond,
                                                         LocalVarContext C,
                                                         bool &Negate) {
  if (!Cond)
    return nullptr;

  if (const auto *CallExp = dyn_cast<CallExpr>(Cond)) {
    if (CallExp->getBuiltinCallee() == Builtin::BI__builtin_expect)
      return getTrylockCallExpr(CallExp->getArg(0), C, Negate);
    return CallExp;
  }
  else if (const auto *PE = dyn_cast<ParenExpr>(Cond))
    return getTrylockCallExpr(PE->getSubExpr(), C, Negate);
  else if (const auto *CE = dyn_cast<ImplicitCastExpr>(Cond))
    return getTrylockCallExpr(CE->getSubExpr(), C, Negate);
  else if (const auto *FE = dyn_cast<FullExpr>(Cond))
    return getTrylockCallExpr(FE->getSubExpr(), C, Negate);
  else if (const auto *DRE = dyn_cast<DeclRefExpr>(Cond)) {
    const Expr *E = LocalVarMap.lookupExpr(DRE->getDecl(), C);
    return getTrylockCallExpr(E, C, Negate);
  }
  else if (const auto *UOP = dyn_cast<UnaryOperator>(Cond)) {
    if (UOP->getOpcode() == UO_LNot) {
      Negate = !Negate;
      return getTrylockCallExpr(UOP->getSubExpr(), C, Negate);
    }
    return nullptr;
  }
  else if (const auto *BOP = dyn_cast<BinaryOperator>(Cond)) {
    if (BOP->getOpcode() == BO_EQ || BOP->getOpcode() == BO_NE) {
      if (BOP->getOpcode() == BO_NE)
        Negate = !Negate;

      bool TCond = false;
      if (getStaticBooleanValue(BOP->getRHS(), TCond)) {
        if (!TCond) Negate = !Negate;
        return getTrylockCallExpr(BOP->getLHS(), C, Negate);
      }
      TCond = false;
      if (getStaticBooleanValue(BOP->getLHS(), TCond)) {
        if (!TCond) Negate = !Negate;
        return getTrylockCallExpr(BOP->getRHS(), C, Negate);
      }
      return nullptr;
    }
    if (BOP->getOpcode() == BO_LAnd) {
      // LHS must have been evaluated in a different block.
      return getTrylockCallExpr(BOP->getRHS(), C, Negate);
    }
    if (BOP->getOpcode() == BO_LOr)
      return getTrylockCallExpr(BOP->getRHS(), C, Negate);
    return nullptr;
  } else if (const auto *COP = dyn_cast<ConditionalOperator>(Cond)) {
    bool TCond, FCond;
    if (getStaticBooleanValue(COP->getTrueExpr(), TCond) &&
        getStaticBooleanValue(COP->getFalseExpr(), FCond)) {
      if (TCond && !FCond)
        return getTrylockCallExpr(COP->getCond(), C, Negate);
      if (!TCond && FCond) {
        Negate = !Negate;
        return getTrylockCallExpr(COP->getCond(), C, Negate);
      }
    }
  }
  return nullptr;
}

/// Find the lockset that holds on the edge between PredBlock
/// and CurrBlock.  The edge set is the exit set of PredBlock (passed
/// as the ExitSet parameter) plus any trylocks, which are conditionally held.
void ThreadSafetyAnalyzer::getEdgeLockset(FactSet& Result,
                                          const FactSet &ExitSet,
                                          const CFGBlock *PredBlock,
                                          const CFGBlock *CurrBlock) {
  Result = ExitSet;

  const Stmt *Cond = PredBlock->getTerminatorCondition();
  // We don't acquire try-locks on ?: branches, only when its result is used.
  if (!Cond || isa<ConditionalOperator>(PredBlock->getTerminator()))
    return;

  bool Negate = false;
  const CFGBlockInfo *PredBlockInfo = &BlockInfo[PredBlock->getBlockID()];
  const LocalVarContext &LVarCtx = PredBlockInfo->ExitContext;
  StringRef CapDiagKind = "mutex";

  const auto *Exp = getTrylockCallExpr(Cond, LVarCtx, Negate);
  if (!Exp)
    return;

  auto *FunDecl = dyn_cast_or_null<NamedDecl>(Exp->getCalleeDecl());
  if(!FunDecl || !FunDecl->hasAttrs())
    return;

  CapExprSet ExclusiveLocksToAdd;
  CapExprSet SharedLocksToAdd;

  // If the condition is a call to a Trylock function, then grab the attributes
  for (const auto *Attr : FunDecl->attrs()) {
    switch (Attr->getKind()) {
      case attr::TryAcquireCapability: {
        auto *A = cast<TryAcquireCapabilityAttr>(Attr);
        getMutexIDs(A->isShared() ? SharedLocksToAdd : ExclusiveLocksToAdd, A,
                    Exp, FunDecl, PredBlock, CurrBlock, A->getSuccessValue(),
                    Negate);
        CapDiagKind = ClassifyDiagnostic(A);
        break;
      };
      case attr::ExclusiveTrylockFunction: {
        const auto *A = cast<ExclusiveTrylockFunctionAttr>(Attr);
        getMutexIDs(ExclusiveLocksToAdd, A, Exp, FunDecl,
                    PredBlock, CurrBlock, A->getSuccessValue(), Negate);
        CapDiagKind = ClassifyDiagnostic(A);
        break;
      }
      case attr::SharedTrylockFunction: {
        const auto *A = cast<SharedTrylockFunctionAttr>(Attr);
        getMutexIDs(SharedLocksToAdd, A, Exp, FunDecl,
                    PredBlock, CurrBlock, A->getSuccessValue(), Negate);
        CapDiagKind = ClassifyDiagnostic(A);
        break;
      }
      default:
        break;
    }
  }

  // Add and remove locks.
  SourceLocation Loc = Exp->getExprLoc();
  for (const auto &ExclusiveLockToAdd : ExclusiveLocksToAdd)
    addLock(Result, llvm::make_unique<LockableFactEntry>(ExclusiveLockToAdd,
                                                         LK_Exclusive, Loc),
            CapDiagKind);
  for (const auto &SharedLockToAdd : SharedLocksToAdd)
    addLock(Result, llvm::make_unique<LockableFactEntry>(SharedLockToAdd,
                                                         LK_Shared, Loc),
            CapDiagKind);
}

namespace {

/// We use this class to visit different types of expressions in
/// CFGBlocks, and build up the lockset.
/// An expression may cause us to add or remove locks from the lockset, or else
/// output error messages related to missing locks.
/// FIXME: In future, we may be able to not inherit from a visitor.
class BuildLockset : public ConstStmtVisitor<BuildLockset> {
  friend class ThreadSafetyAnalyzer;

  ThreadSafetyAnalyzer *Analyzer;
  FactSet FSet;
  LocalVariableMap::Context LVarCtx;
  unsigned CtxIndex;

  // helper functions
  void warnIfMutexNotHeld(const NamedDecl *D, const Expr *Exp, AccessKind AK,
                          Expr *MutexExp, ProtectedOperationKind POK,
                          StringRef DiagKind, SourceLocation Loc);
  void warnIfMutexHeld(const NamedDecl *D, const Expr *Exp, Expr *MutexExp,
                       StringRef DiagKind);

  void checkAccess(const Expr *Exp, AccessKind AK,
                   ProtectedOperationKind POK = POK_VarAccess);
  void checkPtAccess(const Expr *Exp, AccessKind AK,
                     ProtectedOperationKind POK = POK_VarAccess);

  void handleCall(const Expr *Exp, const NamedDecl *D, VarDecl *VD = nullptr);
  void examineArguments(const FunctionDecl *FD,
                        CallExpr::const_arg_iterator ArgBegin,
                        CallExpr::const_arg_iterator ArgEnd,
                        bool SkipFirstParam = false);

public:
  BuildLockset(ThreadSafetyAnalyzer *Anlzr, CFGBlockInfo &Info)
      : ConstStmtVisitor<BuildLockset>(), Analyzer(Anlzr), FSet(Info.EntrySet),
        LVarCtx(Info.EntryContext), CtxIndex(Info.EntryIndex) {}

  void VisitUnaryOperator(const UnaryOperator *UO);
  void VisitBinaryOperator(const BinaryOperator *BO);
  void VisitCastExpr(const CastExpr *CE);
  void VisitCallExpr(const CallExpr *Exp);
  void VisitCXXConstructExpr(const CXXConstructExpr *Exp);
  void VisitDeclStmt(const DeclStmt *S);
};

} // namespace

/// Warn if the LSet does not contain a lock sufficient to protect access
/// of at least the passed in AccessKind.
void BuildLockset::warnIfMutexNotHeld(const NamedDecl *D, const Expr *Exp,
                                      AccessKind AK, Expr *MutexExp,
                                      ProtectedOperationKind POK,
                                      StringRef DiagKind, SourceLocation Loc) {
  LockKind LK = getLockKindFromAccessKind(AK);

  CapabilityExpr Cp = Analyzer->SxBuilder.translateAttrExpr(MutexExp, D, Exp);
  if (Cp.isInvalid()) {
    warnInvalidLock(Analyzer->Handler, MutexExp, D, Exp, DiagKind);
    return;
  } else if (Cp.shouldIgnore()) {
    return;
  }

  if (Cp.negative()) {
    // Negative capabilities act like locks excluded
    const FactEntry *LDat = FSet.findLock(Analyzer->FactMan, !Cp);
    if (LDat) {
      Analyzer->Handler.handleFunExcludesLock(
          DiagKind, D->getNameAsString(), (!Cp).toString(), Loc);
      return;
    }

    // If this does not refer to a negative capability in the same class,
    // then stop here.
    if (!Analyzer->inCurrentScope(Cp))
      return;

    // Otherwise the negative requirement must be propagated to the caller.
    LDat = FSet.findLock(Analyzer->FactMan, Cp);
    if (!LDat) {
      Analyzer->Handler.handleMutexNotHeld("", D, POK, Cp.toString(),
                                           LK_Shared, Loc);
    }
    return;
  }

  const FactEntry *LDat = FSet.findLockUniv(Analyzer->FactMan, Cp);
  bool NoError = true;
  if (!LDat) {
    // No exact match found.  Look for a partial match.
    LDat = FSet.findPartialMatch(Analyzer->FactMan, Cp);
    if (LDat) {
      // Warn that there's no precise match.
      std::string PartMatchStr = LDat->toString();
      StringRef   PartMatchName(PartMatchStr);
      Analyzer->Handler.handleMutexNotHeld(DiagKind, D, POK, Cp.toString(),
                                           LK, Loc, &PartMatchName);
    } else {
      // Warn that there's no match at all.
      Analyzer->Handler.handleMutexNotHeld(DiagKind, D, POK, Cp.toString(),
                                           LK, Loc);
    }
    NoError = false;
  }
  // Make sure the mutex we found is the right kind.
  if (NoError && LDat && !LDat->isAtLeast(LK)) {
    Analyzer->Handler.handleMutexNotHeld(DiagKind, D, POK, Cp.toString(),
                                         LK, Loc);
  }
}

/// Warn if the LSet contains the given lock.
void BuildLockset::warnIfMutexHeld(const NamedDecl *D, const Expr *Exp,
                                   Expr *MutexExp, StringRef DiagKind) {
  CapabilityExpr Cp = Analyzer->SxBuilder.translateAttrExpr(MutexExp, D, Exp);
  if (Cp.isInvalid()) {
    warnInvalidLock(Analyzer->Handler, MutexExp, D, Exp, DiagKind);
    return;
  } else if (Cp.shouldIgnore()) {
    return;
  }

  const FactEntry *LDat = FSet.findLock(Analyzer->FactMan, Cp);
  if (LDat) {
    Analyzer->Handler.handleFunExcludesLock(
        DiagKind, D->getNameAsString(), Cp.toString(), Exp->getExprLoc());
  }
}

/// Checks guarded_by and pt_guarded_by attributes.
/// Whenever we identify an access (read or write) to a DeclRefExpr that is
/// marked with guarded_by, we must ensure the appropriate mutexes are held.
/// Similarly, we check if the access is to an expression that dereferences
/// a pointer marked with pt_guarded_by.
void BuildLockset::checkAccess(const Expr *Exp, AccessKind AK,
                               ProtectedOperationKind POK) {
  Exp = Exp->IgnoreImplicit()->IgnoreParenCasts();

  SourceLocation Loc = Exp->getExprLoc();

  // Local variables of reference type cannot be re-assigned;
  // map them to their initializer.
  while (const auto *DRE = dyn_cast<DeclRefExpr>(Exp)) {
    const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()->getCanonicalDecl());
    if (VD && VD->isLocalVarDecl() && VD->getType()->isReferenceType()) {
      if (const auto *E = VD->getInit()) {
        // Guard against self-initialization. e.g., int &i = i;
        if (E == Exp)
          break;
        Exp = E;
        continue;
      }
    }
    break;
  }

  if (const auto *UO = dyn_cast<UnaryOperator>(Exp)) {
    // For dereferences
    if (UO->getOpcode() == UO_Deref)
      checkPtAccess(UO->getSubExpr(), AK, POK);
    return;
  }

  if (const auto *AE = dyn_cast<ArraySubscriptExpr>(Exp)) {
    checkPtAccess(AE->getLHS(), AK, POK);
    return;
  }

  if (const auto *ME = dyn_cast<MemberExpr>(Exp)) {
    if (ME->isArrow())
      checkPtAccess(ME->getBase(), AK, POK);
    else
      checkAccess(ME->getBase(), AK, POK);
  }

  const ValueDecl *D = getValueDecl(Exp);
  if (!D || !D->hasAttrs())
    return;

  if (D->hasAttr<GuardedVarAttr>() && FSet.isEmpty(Analyzer->FactMan)) {
    Analyzer->Handler.handleNoMutexHeld("mutex", D, POK, AK, Loc);
  }

  for (const auto *I : D->specific_attrs<GuardedByAttr>())
    warnIfMutexNotHeld(D, Exp, AK, I->getArg(), POK,
                       ClassifyDiagnostic(I), Loc);
}

/// Checks pt_guarded_by and pt_guarded_var attributes.
/// POK is the same  operationKind that was passed to checkAccess.
void BuildLockset::checkPtAccess(const Expr *Exp, AccessKind AK,
                                 ProtectedOperationKind POK) {
  while (true) {
    if (const auto *PE = dyn_cast<ParenExpr>(Exp)) {
      Exp = PE->getSubExpr();
      continue;
    }
    if (const auto *CE = dyn_cast<CastExpr>(Exp)) {
      if (CE->getCastKind() == CK_ArrayToPointerDecay) {
        // If it's an actual array, and not a pointer, then it's elements
        // are protected by GUARDED_BY, not PT_GUARDED_BY;
        checkAccess(CE->getSubExpr(), AK, POK);
        return;
      }
      Exp = CE->getSubExpr();
      continue;
    }
    break;
  }

  // Pass by reference warnings are under a different flag.
  ProtectedOperationKind PtPOK = POK_VarDereference;
  if (POK == POK_PassByRef) PtPOK = POK_PtPassByRef;

  const ValueDecl *D = getValueDecl(Exp);
  if (!D || !D->hasAttrs())
    return;

  if (D->hasAttr<PtGuardedVarAttr>() && FSet.isEmpty(Analyzer->FactMan))
    Analyzer->Handler.handleNoMutexHeld("mutex", D, PtPOK, AK,
                                        Exp->getExprLoc());

  for (auto const *I : D->specific_attrs<PtGuardedByAttr>())
    warnIfMutexNotHeld(D, Exp, AK, I->getArg(), PtPOK,
                       ClassifyDiagnostic(I), Exp->getExprLoc());
}

/// Process a function call, method call, constructor call,
/// or destructor call.  This involves looking at the attributes on the
/// corresponding function/method/constructor/destructor, issuing warnings,
/// and updating the locksets accordingly.
///
/// FIXME: For classes annotated with one of the guarded annotations, we need
/// to treat const method calls as reads and non-const method calls as writes,
/// and check that the appropriate locks are held. Non-const method calls with
/// the same signature as const method calls can be also treated as reads.
///
void BuildLockset::handleCall(const Expr *Exp, const NamedDecl *D,
                              VarDecl *VD) {
  SourceLocation Loc = Exp->getExprLoc();
  CapExprSet ExclusiveLocksToAdd, SharedLocksToAdd;
  CapExprSet ExclusiveLocksToRemove, SharedLocksToRemove, GenericLocksToRemove;
  CapExprSet ScopedExclusiveReqs, ScopedSharedReqs;
  StringRef CapDiagKind = "mutex";

  // Figure out if we're constructing an object of scoped lockable class
  bool isScopedVar = false;
  if (VD) {
    if (const auto *CD = dyn_cast<const CXXConstructorDecl>(D)) {
      const CXXRecordDecl* PD = CD->getParent();
      if (PD && PD->hasAttr<ScopedLockableAttr>())
        isScopedVar = true;
    }
  }

  for(const Attr *At : D->attrs()) {
    switch (At->getKind()) {
      // When we encounter a lock function, we need to add the lock to our
      // lockset.
      case attr::AcquireCapability: {
        const auto *A = cast<AcquireCapabilityAttr>(At);
        Analyzer->getMutexIDs(A->isShared() ? SharedLocksToAdd
                                            : ExclusiveLocksToAdd,
                              A, Exp, D, VD);

        CapDiagKind = ClassifyDiagnostic(A);
        break;
      }

      // An assert will add a lock to the lockset, but will not generate
      // a warning if it is already there, and will not generate a warning
      // if it is not removed.
      case attr::AssertExclusiveLock: {
        const auto *A = cast<AssertExclusiveLockAttr>(At);

        CapExprSet AssertLocks;
        Analyzer->getMutexIDs(AssertLocks, A, Exp, D, VD);
        for (const auto &AssertLock : AssertLocks)
          Analyzer->addLock(FSet,
                            llvm::make_unique<LockableFactEntry>(
                                AssertLock, LK_Exclusive, Loc, false, true),
                            ClassifyDiagnostic(A));
        break;
      }
      case attr::AssertSharedLock: {
        const auto *A = cast<AssertSharedLockAttr>(At);

        CapExprSet AssertLocks;
        Analyzer->getMutexIDs(AssertLocks, A, Exp, D, VD);
        for (const auto &AssertLock : AssertLocks)
          Analyzer->addLock(FSet,
                            llvm::make_unique<LockableFactEntry>(
                                AssertLock, LK_Shared, Loc, false, true),
                            ClassifyDiagnostic(A));
        break;
      }

      case attr::AssertCapability: {
        const auto *A = cast<AssertCapabilityAttr>(At);
        CapExprSet AssertLocks;
        Analyzer->getMutexIDs(AssertLocks, A, Exp, D, VD);
        for (const auto &AssertLock : AssertLocks)
          Analyzer->addLock(FSet,
                            llvm::make_unique<LockableFactEntry>(
                                AssertLock,
                                A->isShared() ? LK_Shared : LK_Exclusive, Loc,
                                false, true),
                            ClassifyDiagnostic(A));
        break;
      }

      // When we encounter an unlock function, we need to remove unlocked
      // mutexes from the lockset, and flag a warning if they are not there.
      case attr::ReleaseCapability: {
        const auto *A = cast<ReleaseCapabilityAttr>(At);
        if (A->isGeneric())
          Analyzer->getMutexIDs(GenericLocksToRemove, A, Exp, D, VD);
        else if (A->isShared())
          Analyzer->getMutexIDs(SharedLocksToRemove, A, Exp, D, VD);
        else
          Analyzer->getMutexIDs(ExclusiveLocksToRemove, A, Exp, D, VD);

        CapDiagKind = ClassifyDiagnostic(A);
        break;
      }

      case attr::RequiresCapability: {
        const auto *A = cast<RequiresCapabilityAttr>(At);
        for (auto *Arg : A->args()) {
          warnIfMutexNotHeld(D, Exp, A->isShared() ? AK_Read : AK_Written, Arg,
                             POK_FunctionCall, ClassifyDiagnostic(A),
                             Exp->getExprLoc());
          // use for adopting a lock
          if (isScopedVar) {
            Analyzer->getMutexIDs(A->isShared() ? ScopedSharedReqs
                                                : ScopedExclusiveReqs,
                                  A, Exp, D, VD);
          }
        }
        break;
      }

      case attr::LocksExcluded: {
        const auto *A = cast<LocksExcludedAttr>(At);
        for (auto *Arg : A->args())
          warnIfMutexHeld(D, Exp, Arg, ClassifyDiagnostic(A));
        break;
      }

      // Ignore attributes unrelated to thread-safety
      default:
        break;
    }
  }

  // Remove locks first to allow lock upgrading/downgrading.
  // FIXME -- should only fully remove if the attribute refers to 'this'.
  bool Dtor = isa<CXXDestructorDecl>(D);
  for (const auto &M : ExclusiveLocksToRemove)
    Analyzer->removeLock(FSet, M, Loc, Dtor, LK_Exclusive, CapDiagKind);
  for (const auto &M : SharedLocksToRemove)
    Analyzer->removeLock(FSet, M, Loc, Dtor, LK_Shared, CapDiagKind);
  for (const auto &M : GenericLocksToRemove)
    Analyzer->removeLock(FSet, M, Loc, Dtor, LK_Generic, CapDiagKind);

  // Add locks.
  for (const auto &M : ExclusiveLocksToAdd)
    Analyzer->addLock(FSet, llvm::make_unique<LockableFactEntry>(
                                M, LK_Exclusive, Loc, isScopedVar),
                      CapDiagKind);
  for (const auto &M : SharedLocksToAdd)
    Analyzer->addLock(FSet, llvm::make_unique<LockableFactEntry>(
                                M, LK_Shared, Loc, isScopedVar),
                      CapDiagKind);

  if (isScopedVar) {
    // Add the managing object as a dummy mutex, mapped to the underlying mutex.
    SourceLocation MLoc = VD->getLocation();
    DeclRefExpr DRE(VD->getASTContext(), VD, false, VD->getType(), VK_LValue,
                    VD->getLocation());
    // FIXME: does this store a pointer to DRE?
    CapabilityExpr Scp = Analyzer->SxBuilder.translateAttrExpr(&DRE, nullptr);

    auto ScopedEntry = llvm::make_unique<ScopedLockableFactEntry>(Scp, MLoc);
    for (const auto &M : ExclusiveLocksToAdd)
      ScopedEntry->addExclusiveLock(M);
    for (const auto &M : ScopedExclusiveReqs)
      ScopedEntry->addExclusiveLock(M);
    for (const auto &M : SharedLocksToAdd)
      ScopedEntry->addSharedLock(M);
    for (const auto &M : ScopedSharedReqs)
      ScopedEntry->addSharedLock(M);
    for (const auto &M : ExclusiveLocksToRemove)
      ScopedEntry->addExclusiveUnlock(M);
    for (const auto &M : SharedLocksToRemove)
      ScopedEntry->addSharedUnlock(M);
    Analyzer->addLock(FSet, std::move(ScopedEntry), CapDiagKind);
  }
}

/// For unary operations which read and write a variable, we need to
/// check whether we hold any required mutexes. Reads are checked in
/// VisitCastExpr.
void BuildLockset::VisitUnaryOperator(const UnaryOperator *UO) {
  switch (UO->getOpcode()) {
    case UO_PostDec:
    case UO_PostInc:
    case UO_PreDec:
    case UO_PreInc:
      checkAccess(UO->getSubExpr(), AK_Written);
      break;
    default:
      break;
  }
}

/// For binary operations which assign to a variable (writes), we need to check
/// whether we hold any required mutexes.
/// FIXME: Deal with non-primitive types.
void BuildLockset::VisitBinaryOperator(const BinaryOperator *BO) {
  if (!BO->isAssignmentOp())
    return;

  // adjust the context
  LVarCtx = Analyzer->LocalVarMap.getNextContext(CtxIndex, BO, LVarCtx);

  checkAccess(BO->getLHS(), AK_Written);
}

/// Whenever we do an LValue to Rvalue cast, we are reading a variable and
/// need to ensure we hold any required mutexes.
/// FIXME: Deal with non-primitive types.
void BuildLockset::VisitCastExpr(const CastExpr *CE) {
  if (CE->getCastKind() != CK_LValueToRValue)
    return;
  checkAccess(CE->getSubExpr(), AK_Read);
}

void BuildLockset::examineArguments(const FunctionDecl *FD,
                                    CallExpr::const_arg_iterator ArgBegin,
                                    CallExpr::const_arg_iterator ArgEnd,
                                    bool SkipFirstParam) {
  // Currently we can't do anything if we don't know the function declaration.
  if (!FD)
    return;

  // NO_THREAD_SAFETY_ANALYSIS does double duty here.  Normally it
  // only turns off checking within the body of a function, but we also
  // use it to turn off checking in arguments to the function.  This
  // could result in some false negatives, but the alternative is to
  // create yet another attribute.
  if (FD->hasAttr<NoThreadSafetyAnalysisAttr>())
    return;

  const ArrayRef<ParmVarDecl *> Params = FD->parameters();
  auto Param = Params.begin();
  if (SkipFirstParam)
    ++Param;

  // There can be default arguments, so we stop when one iterator is at end().
  for (auto Arg = ArgBegin; Param != Params.end() && Arg != ArgEnd;
       ++Param, ++Arg) {
    QualType Qt = (*Param)->getType();
    if (Qt->isReferenceType())
      checkAccess(*Arg, AK_Read, POK_PassByRef);
  }
}

void BuildLockset::VisitCallExpr(const CallExpr *Exp) {
  if (const auto *CE = dyn_cast<CXXMemberCallExpr>(Exp)) {
    const auto *ME = dyn_cast<MemberExpr>(CE->getCallee());
    // ME can be null when calling a method pointer
    const CXXMethodDecl *MD = CE->getMethodDecl();

    if (ME && MD) {
      if (ME->isArrow()) {
        if (MD->isConst())
          checkPtAccess(CE->getImplicitObjectArgument(), AK_Read);
        else // FIXME -- should be AK_Written
          checkPtAccess(CE->getImplicitObjectArgument(), AK_Read);
      } else {
        if (MD->isConst())
          checkAccess(CE->getImplicitObjectArgument(), AK_Read);
        else     // FIXME -- should be AK_Written
          checkAccess(CE->getImplicitObjectArgument(), AK_Read);
      }
    }

    examineArguments(CE->getDirectCallee(), CE->arg_begin(), CE->arg_end());
  } else if (const auto *OE = dyn_cast<CXXOperatorCallExpr>(Exp)) {
    auto OEop = OE->getOperator();
    switch (OEop) {
      case OO_Equal: {
        const Expr *Target = OE->getArg(0);
        const Expr *Source = OE->getArg(1);
        checkAccess(Target, AK_Written);
        checkAccess(Source, AK_Read);
        break;
      }
      case OO_Star:
      case OO_Arrow:
      case OO_Subscript:
        if (!(OEop == OO_Star && OE->getNumArgs() > 1)) {
          // Grrr.  operator* can be multiplication...
          checkPtAccess(OE->getArg(0), AK_Read);
        }
        LLVM_FALLTHROUGH;
      default: {
        // TODO: get rid of this, and rely on pass-by-ref instead.
        const Expr *Obj = OE->getArg(0);
        checkAccess(Obj, AK_Read);
        // Check the remaining arguments. For method operators, the first
        // argument is the implicit self argument, and doesn't appear in the
        // FunctionDecl, but for non-methods it does.
        const FunctionDecl *FD = OE->getDirectCallee();
        examineArguments(FD, std::next(OE->arg_begin()), OE->arg_end(),
                         /*SkipFirstParam*/ !isa<CXXMethodDecl>(FD));
        break;
      }
    }
  } else {
    examineArguments(Exp->getDirectCallee(), Exp->arg_begin(), Exp->arg_end());
  }

  auto *D = dyn_cast_or_null<NamedDecl>(Exp->getCalleeDecl());
  if(!D || !D->hasAttrs())
    return;
  handleCall(Exp, D);
}

void BuildLockset::VisitCXXConstructExpr(const CXXConstructExpr *Exp) {
  const CXXConstructorDecl *D = Exp->getConstructor();
  if (D && D->isCopyConstructor()) {
    const Expr* Source = Exp->getArg(0);
    checkAccess(Source, AK_Read);
  } else {
    examineArguments(D, Exp->arg_begin(), Exp->arg_end());
  }
}

static CXXConstructorDecl *
findConstructorForByValueReturn(const CXXRecordDecl *RD) {
  // Prefer a move constructor over a copy constructor. If there's more than
  // one copy constructor or more than one move constructor, we arbitrarily
  // pick the first declared such constructor rather than trying to guess which
  // one is more appropriate.
  CXXConstructorDecl *CopyCtor = nullptr;
  for (auto *Ctor : RD->ctors()) {
    if (Ctor->isDeleted())
      continue;
    if (Ctor->isMoveConstructor())
      return Ctor;
    if (!CopyCtor && Ctor->isCopyConstructor())
      CopyCtor = Ctor;
  }
  return CopyCtor;
}

static Expr *buildFakeCtorCall(CXXConstructorDecl *CD, ArrayRef<Expr *> Args,
                               SourceLocation Loc) {
  ASTContext &Ctx = CD->getASTContext();
  return CXXConstructExpr::Create(Ctx, Ctx.getRecordType(CD->getParent()), Loc,
                                  CD, true, Args, false, false, false, false,
                                  CXXConstructExpr::CK_Complete,
                                  SourceRange(Loc, Loc));
}

void BuildLockset::VisitDeclStmt(const DeclStmt *S) {
  // adjust the context
  LVarCtx = Analyzer->LocalVarMap.getNextContext(CtxIndex, S, LVarCtx);

  for (auto *D : S->getDeclGroup()) {
    if (auto *VD = dyn_cast_or_null<VarDecl>(D)) {
      Expr *E = VD->getInit();
      if (!E)
        continue;
      E = E->IgnoreParens();

      // handle constructors that involve temporaries
      if (auto *EWC = dyn_cast<ExprWithCleanups>(E))
        E = EWC->getSubExpr();
      if (auto *BTE = dyn_cast<CXXBindTemporaryExpr>(E))
        E = BTE->getSubExpr();

      if (const auto *CE = dyn_cast<CXXConstructExpr>(E)) {
        const auto *CtorD = dyn_cast_or_null<NamedDecl>(CE->getConstructor());
        if (!CtorD || !CtorD->hasAttrs())
          continue;
        handleCall(E, CtorD, VD);
      } else if (isa<CallExpr>(E) && E->isRValue()) {
        // If the object is initialized by a function call that returns a
        // scoped lockable by value, use the attributes on the copy or move
        // constructor to figure out what effect that should have on the
        // lockset.
        // FIXME: Is this really the best way to handle this situation?
        auto *RD = E->getType()->getAsCXXRecordDecl();
        if (!RD || !RD->hasAttr<ScopedLockableAttr>())
          continue;
        CXXConstructorDecl *CtorD = findConstructorForByValueReturn(RD);
        if (!CtorD || !CtorD->hasAttrs())
          continue;
        handleCall(buildFakeCtorCall(CtorD, {E}, E->getBeginLoc()), CtorD, VD);
      }
    }
  }
}

/// Compute the intersection of two locksets and issue warnings for any
/// locks in the symmetric difference.
///
/// This function is used at a merge point in the CFG when comparing the lockset
/// of each branch being merged. For example, given the following sequence:
/// A; if () then B; else C; D; we need to check that the lockset after B and C
/// are the same. In the event of a difference, we use the intersection of these
/// two locksets at the start of D.
///
/// \param FSet1 The first lockset.
/// \param FSet2 The second lockset.
/// \param JoinLoc The location of the join point for error reporting
/// \param LEK1 The error message to report if a mutex is missing from LSet1
/// \param LEK2 The error message to report if a mutex is missing from Lset2
void ThreadSafetyAnalyzer::intersectAndWarn(FactSet &FSet1,
                                            const FactSet &FSet2,
                                            SourceLocation JoinLoc,
                                            LockErrorKind LEK1,
                                            LockErrorKind LEK2,
                                            bool Modify) {
  FactSet FSet1Orig = FSet1;

  // Find locks in FSet2 that conflict or are not in FSet1, and warn.
  for (const auto &Fact : FSet2) {
    const FactEntry *LDat1 = nullptr;
    const FactEntry *LDat2 = &FactMan[Fact];
    FactSet::iterator Iter1  = FSet1.findLockIter(FactMan, *LDat2);
    if (Iter1 != FSet1.end()) LDat1 = &FactMan[*Iter1];

    if (LDat1) {
      if (LDat1->kind() != LDat2->kind()) {
        Handler.handleExclusiveAndShared("mutex", LDat2->toString(),
                                         LDat2->loc(), LDat1->loc());
        if (Modify && LDat1->kind() != LK_Exclusive) {
          // Take the exclusive lock, which is the one in FSet2.
          *Iter1 = Fact;
        }
      }
      else if (Modify && LDat1->asserted() && !LDat2->asserted()) {
        // The non-asserted lock in FSet2 is the one we want to track.
        *Iter1 = Fact;
      }
    } else {
      LDat2->handleRemovalFromIntersection(FSet2, FactMan, JoinLoc, LEK1,
                                           Handler);
    }
  }

  // Find locks in FSet1 that are not in FSet2, and remove them.
  for (const auto &Fact : FSet1Orig) {
    const FactEntry *LDat1 = &FactMan[Fact];
    const FactEntry *LDat2 = FSet2.findLock(FactMan, *LDat1);

    if (!LDat2) {
      LDat1->handleRemovalFromIntersection(FSet1Orig, FactMan, JoinLoc, LEK2,
                                           Handler);
      if (Modify)
        FSet1.removeLock(FactMan, *LDat1);
    }
  }
}

// Return true if block B never continues to its successors.
static bool neverReturns(const CFGBlock *B) {
  if (B->hasNoReturnElement())
    return true;
  if (B->empty())
    return false;

  CFGElement Last = B->back();
  if (Optional<CFGStmt> S = Last.getAs<CFGStmt>()) {
    if (isa<CXXThrowExpr>(S->getStmt()))
      return true;
  }
  return false;
}

/// Check a function's CFG for thread-safety violations.
///
/// We traverse the blocks in the CFG, compute the set of mutexes that are held
/// at the end of each block, and issue warnings for thread safety violations.
/// Each block in the CFG is traversed exactly once.
void ThreadSafetyAnalyzer::runAnalysis(AnalysisDeclContext &AC) {
  // TODO: this whole function needs be rewritten as a visitor for CFGWalker.
  // For now, we just use the walker to set things up.
  threadSafety::CFGWalker walker;
  if (!walker.init(AC))
    return;

  // AC.dumpCFG(true);
  // threadSafety::printSCFG(walker);

  CFG *CFGraph = walker.getGraph();
  const NamedDecl *D = walker.getDecl();
  const auto *CurrentFunction = dyn_cast<FunctionDecl>(D);
  CurrentMethod = dyn_cast<CXXMethodDecl>(D);

  if (D->hasAttr<NoThreadSafetyAnalysisAttr>())
    return;

  // FIXME: Do something a bit more intelligent inside constructor and
  // destructor code.  Constructors and destructors must assume unique access
  // to 'this', so checks on member variable access is disabled, but we should
  // still enable checks on other objects.
  if (isa<CXXConstructorDecl>(D))
    return;  // Don't check inside constructors.
  if (isa<CXXDestructorDecl>(D))
    return;  // Don't check inside destructors.

  Handler.enterFunction(CurrentFunction);

  BlockInfo.resize(CFGraph->getNumBlockIDs(),
    CFGBlockInfo::getEmptyBlockInfo(LocalVarMap));

  // We need to explore the CFG via a "topological" ordering.
  // That way, we will be guaranteed to have information about required
  // predecessor locksets when exploring a new block.
  const PostOrderCFGView *SortedGraph = walker.getSortedGraph();
  PostOrderCFGView::CFGBlockSet VisitedBlocks(CFGraph);

  // Mark entry block as reachable
  BlockInfo[CFGraph->getEntry().getBlockID()].Reachable = true;

  // Compute SSA names for local variables
  LocalVarMap.traverseCFG(CFGraph, SortedGraph, BlockInfo);

  // Fill in source locations for all CFGBlocks.
  findBlockLocations(CFGraph, SortedGraph, BlockInfo);

  CapExprSet ExclusiveLocksAcquired;
  CapExprSet SharedLocksAcquired;
  CapExprSet LocksReleased;

  // Add locks from exclusive_locks_required and shared_locks_required
  // to initial lockset. Also turn off checking for lock and unlock functions.
  // FIXME: is there a more intelligent way to check lock/unlock functions?
  if (!SortedGraph->empty() && D->hasAttrs()) {
    const CFGBlock *FirstBlock = *SortedGraph->begin();
    FactSet &InitialLockset = BlockInfo[FirstBlock->getBlockID()].EntrySet;

    CapExprSet ExclusiveLocksToAdd;
    CapExprSet SharedLocksToAdd;
    StringRef CapDiagKind = "mutex";

    SourceLocation Loc = D->getLocation();
    for (const auto *Attr : D->attrs()) {
      Loc = Attr->getLocation();
      if (const auto *A = dyn_cast<RequiresCapabilityAttr>(Attr)) {
        getMutexIDs(A->isShared() ? SharedLocksToAdd : ExclusiveLocksToAdd, A,
                    nullptr, D);
        CapDiagKind = ClassifyDiagnostic(A);
      } else if (const auto *A = dyn_cast<ReleaseCapabilityAttr>(Attr)) {
        // UNLOCK_FUNCTION() is used to hide the underlying lock implementation.
        // We must ignore such methods.
        if (A->args_size() == 0)
          return;
        getMutexIDs(A->isShared() ? SharedLocksToAdd : ExclusiveLocksToAdd, A,
                    nullptr, D);
        getMutexIDs(LocksReleased, A, nullptr, D);
        CapDiagKind = ClassifyDiagnostic(A);
      } else if (const auto *A = dyn_cast<AcquireCapabilityAttr>(Attr)) {
        if (A->args_size() == 0)
          return;
        getMutexIDs(A->isShared() ? SharedLocksAcquired
                                  : ExclusiveLocksAcquired,
                    A, nullptr, D);
        CapDiagKind = ClassifyDiagnostic(A);
      } else if (isa<ExclusiveTrylockFunctionAttr>(Attr)) {
        // Don't try to check trylock functions for now.
        return;
      } else if (isa<SharedTrylockFunctionAttr>(Attr)) {
        // Don't try to check trylock functions for now.
        return;
      } else if (isa<TryAcquireCapabilityAttr>(Attr)) {
        // Don't try to check trylock functions for now.
        return;
      }
    }

    // FIXME -- Loc can be wrong here.
    for (const auto &Mu : ExclusiveLocksToAdd) {
      auto Entry = llvm::make_unique<LockableFactEntry>(Mu, LK_Exclusive, Loc);
      Entry->setDeclared(true);
      addLock(InitialLockset, std::move(Entry), CapDiagKind, true);
    }
    for (const auto &Mu : SharedLocksToAdd) {
      auto Entry = llvm::make_unique<LockableFactEntry>(Mu, LK_Shared, Loc);
      Entry->setDeclared(true);
      addLock(InitialLockset, std::move(Entry), CapDiagKind, true);
    }
  }

  for (const auto *CurrBlock : *SortedGraph) {
    unsigned CurrBlockID = CurrBlock->getBlockID();
    CFGBlockInfo *CurrBlockInfo = &BlockInfo[CurrBlockID];

    // Use the default initial lockset in case there are no predecessors.
    VisitedBlocks.insert(CurrBlock);

    // Iterate through the predecessor blocks and warn if the lockset for all
    // predecessors is not the same. We take the entry lockset of the current
    // block to be the intersection of all previous locksets.
    // FIXME: By keeping the intersection, we may output more errors in future
    // for a lock which is not in the intersection, but was in the union. We
    // may want to also keep the union in future. As an example, let's say
    // the intersection contains Mutex L, and the union contains L and M.
    // Later we unlock M. At this point, we would output an error because we
    // never locked M; although the real error is probably that we forgot to
    // lock M on all code paths. Conversely, let's say that later we lock M.
    // In this case, we should compare against the intersection instead of the
    // union because the real error is probably that we forgot to unlock M on
    // all code paths.
    bool LocksetInitialized = false;
    SmallVector<CFGBlock *, 8> SpecialBlocks;
    for (CFGBlock::const_pred_iterator PI = CurrBlock->pred_begin(),
         PE  = CurrBlock->pred_end(); PI != PE; ++PI) {
      // if *PI -> CurrBlock is a back edge
      if (*PI == nullptr || !VisitedBlocks.alreadySet(*PI))
        continue;

      unsigned PrevBlockID = (*PI)->getBlockID();
      CFGBlockInfo *PrevBlockInfo = &BlockInfo[PrevBlockID];

      // Ignore edges from blocks that can't return.
      if (neverReturns(*PI) || !PrevBlockInfo->Reachable)
        continue;

      // Okay, we can reach this block from the entry.
      CurrBlockInfo->Reachable = true;

      // If the previous block ended in a 'continue' or 'break' statement, then
      // a difference in locksets is probably due to a bug in that block, rather
      // than in some other predecessor. In that case, keep the other
      // predecessor's lockset.
      if (const Stmt *Terminator = (*PI)->getTerminator()) {
        if (isa<ContinueStmt>(Terminator) || isa<BreakStmt>(Terminator)) {
          SpecialBlocks.push_back(*PI);
          continue;
        }
      }

      FactSet PrevLockset;
      getEdgeLockset(PrevLockset, PrevBlockInfo->ExitSet, *PI, CurrBlock);

      if (!LocksetInitialized) {
        CurrBlockInfo->EntrySet = PrevLockset;
        LocksetInitialized = true;
      } else {
        intersectAndWarn(CurrBlockInfo->EntrySet, PrevLockset,
                         CurrBlockInfo->EntryLoc,
                         LEK_LockedSomePredecessors);
      }
    }

    // Skip rest of block if it's not reachable.
    if (!CurrBlockInfo->Reachable)
      continue;

    // Process continue and break blocks. Assume that the lockset for the
    // resulting block is unaffected by any discrepancies in them.
    for (const auto *PrevBlock : SpecialBlocks) {
      unsigned PrevBlockID = PrevBlock->getBlockID();
      CFGBlockInfo *PrevBlockInfo = &BlockInfo[PrevBlockID];

      if (!LocksetInitialized) {
        CurrBlockInfo->EntrySet = PrevBlockInfo->ExitSet;
        LocksetInitialized = true;
      } else {
        // Determine whether this edge is a loop terminator for diagnostic
        // purposes. FIXME: A 'break' statement might be a loop terminator, but
        // it might also be part of a switch. Also, a subsequent destructor
        // might add to the lockset, in which case the real issue might be a
        // double lock on the other path.
        const Stmt *Terminator = PrevBlock->getTerminator();
        bool IsLoop = Terminator && isa<ContinueStmt>(Terminator);

        FactSet PrevLockset;
        getEdgeLockset(PrevLockset, PrevBlockInfo->ExitSet,
                       PrevBlock, CurrBlock);

        // Do not update EntrySet.
        intersectAndWarn(CurrBlockInfo->EntrySet, PrevLockset,
                         PrevBlockInfo->ExitLoc,
                         IsLoop ? LEK_LockedSomeLoopIterations
                                : LEK_LockedSomePredecessors,
                         false);
      }
    }

    BuildLockset LocksetBuilder(this, *CurrBlockInfo);

    // Visit all the statements in the basic block.
    for (const auto &BI : *CurrBlock) {
      switch (BI.getKind()) {
        case CFGElement::Statement: {
          CFGStmt CS = BI.castAs<CFGStmt>();
          LocksetBuilder.Visit(CS.getStmt());
          break;
        }
        // Ignore BaseDtor, MemberDtor, and TemporaryDtor for now.
        case CFGElement::AutomaticObjectDtor: {
          CFGAutomaticObjDtor AD = BI.castAs<CFGAutomaticObjDtor>();
          const auto *DD = AD.getDestructorDecl(AC.getASTContext());
          if (!DD->hasAttrs())
            break;

          // Create a dummy expression,
          auto *VD = const_cast<VarDecl *>(AD.getVarDecl());
          DeclRefExpr DRE(VD->getASTContext(), VD, false,
                          VD->getType().getNonReferenceType(), VK_LValue,
                          AD.getTriggerStmt()->getEndLoc());
          LocksetBuilder.handleCall(&DRE, DD);
          break;
        }
        default:
          break;
      }
    }
    CurrBlockInfo->ExitSet = LocksetBuilder.FSet;

    // For every back edge from CurrBlock (the end of the loop) to another block
    // (FirstLoopBlock) we need to check that the Lockset of Block is equal to
    // the one held at the beginning of FirstLoopBlock. We can look up the
    // Lockset held at the beginning of FirstLoopBlock in the EntryLockSets map.
    for (CFGBlock::const_succ_iterator SI = CurrBlock->succ_begin(),
         SE  = CurrBlock->succ_end(); SI != SE; ++SI) {
      // if CurrBlock -> *SI is *not* a back edge
      if (*SI == nullptr || !VisitedBlocks.alreadySet(*SI))
        continue;

      CFGBlock *FirstLoopBlock = *SI;
      CFGBlockInfo *PreLoop = &BlockInfo[FirstLoopBlock->getBlockID()];
      CFGBlockInfo *LoopEnd = &BlockInfo[CurrBlockID];
      intersectAndWarn(LoopEnd->ExitSet, PreLoop->EntrySet,
                       PreLoop->EntryLoc,
                       LEK_LockedSomeLoopIterations,
                       false);
    }
  }

  CFGBlockInfo *Initial = &BlockInfo[CFGraph->getEntry().getBlockID()];
  CFGBlockInfo *Final   = &BlockInfo[CFGraph->getExit().getBlockID()];

  // Skip the final check if the exit block is unreachable.
  if (!Final->Reachable)
    return;

  // By default, we expect all locks held on entry to be held on exit.
  FactSet ExpectedExitSet = Initial->EntrySet;

  // Adjust the expected exit set by adding or removing locks, as declared
  // by *-LOCK_FUNCTION and UNLOCK_FUNCTION.  The intersect below will then
  // issue the appropriate warning.
  // FIXME: the location here is not quite right.
  for (const auto &Lock : ExclusiveLocksAcquired)
    ExpectedExitSet.addLock(FactMan, llvm::make_unique<LockableFactEntry>(
                                         Lock, LK_Exclusive, D->getLocation()));
  for (const auto &Lock : SharedLocksAcquired)
    ExpectedExitSet.addLock(FactMan, llvm::make_unique<LockableFactEntry>(
                                         Lock, LK_Shared, D->getLocation()));
  for (const auto &Lock : LocksReleased)
    ExpectedExitSet.removeLock(FactMan, Lock);

  // FIXME: Should we call this function for all blocks which exit the function?
  intersectAndWarn(ExpectedExitSet, Final->ExitSet,
                   Final->ExitLoc,
                   LEK_LockedAtEndOfFunction,
                   LEK_NotLockedAtEndOfFunction,
                   false);

  Handler.leaveFunction(CurrentFunction);
}

/// Check a function's CFG for thread-safety violations.
///
/// We traverse the blocks in the CFG, compute the set of mutexes that are held
/// at the end of each block, and issue warnings for thread safety violations.
/// Each block in the CFG is traversed exactly once.
void threadSafety::runThreadSafetyAnalysis(AnalysisDeclContext &AC,
                                           ThreadSafetyHandler &Handler,
                                           BeforeSet **BSet) {
  if (!*BSet)
    *BSet = new BeforeSet;
  ThreadSafetyAnalyzer Analyzer(Handler, *BSet);
  Analyzer.runAnalysis(AC);
}

void threadSafety::threadSafetyCleanup(BeforeSet *Cache) { delete Cache; }

/// Helper function that returns a LockKind required for the given level
/// of access.
LockKind threadSafety::getLockKindFromAccessKind(AccessKind AK) {
  switch (AK) {
    case AK_Read :
      return LK_Shared;
    case AK_Written :
      return LK_Exclusive;
  }
  llvm_unreachable("Unknown AccessKind");
}
