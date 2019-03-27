//===- ExprEngine.h - Path-Sensitive Expression-Level Dataflow --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines a meta-engine for path-sensitive dataflow analysis that
//  is built on CoreEngine, but provides the boilerplate to execute transfer
//  functions and build the ExplodedGraph at the expression level.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_EXPRENGINE_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_EXPRENGINE_H

#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/DomainSpecific/ObjCNoReturn.h"
#include "clang/Analysis/ProgramPoint.h"
#include "clang/Basic/LLVM.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CoreEngine.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/FunctionSummary.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState_Fwd.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SValBuilder.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SubEngine.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/WorkList.h"
#include "llvm/ADT/ArrayRef.h"
#include <cassert>
#include <utility>

namespace clang {

class AnalysisDeclContextManager;
class AnalyzerOptions;
class ASTContext;
class ConstructionContext;
class CXXBindTemporaryExpr;
class CXXCatchStmt;
class CXXConstructExpr;
class CXXDeleteExpr;
class CXXNewExpr;
class CXXThisExpr;
class Decl;
class DeclStmt;
class GCCAsmStmt;
class LambdaExpr;
class LocationContext;
class MaterializeTemporaryExpr;
class MSAsmStmt;
class NamedDecl;
class ObjCAtSynchronizedStmt;
class ObjCForCollectionStmt;
class ObjCIvarRefExpr;
class ObjCMessageExpr;
class ReturnStmt;
class Stmt;

namespace cross_tu {

class CrossTranslationUnitContext;

} // namespace cross_tu

namespace ento {

class BasicValueFactory;
class CallEvent;
class CheckerManager;
class ConstraintManager;
class CXXTempObjectRegion;
class MemRegion;
class RegionAndSymbolInvalidationTraits;
class SymbolManager;

class ExprEngine : public SubEngine {
public:
  /// The modes of inlining, which override the default analysis-wide settings.
  enum InliningModes {
    /// Follow the default settings for inlining callees.
    Inline_Regular = 0,

    /// Do minimal inlining of callees.
    Inline_Minimal = 0x1
  };

  /// Hints for figuring out of a call should be inlined during evalCall().
  struct EvalCallOptions {
    /// This call is a constructor or a destructor for which we do not currently
    /// compute the this-region correctly.
    bool IsCtorOrDtorWithImproperlyModeledTargetRegion = false;

    /// This call is a constructor or a destructor for a single element within
    /// an array, a part of array construction or destruction.
    bool IsArrayCtorOrDtor = false;

    /// This call is a constructor or a destructor of a temporary value.
    bool IsTemporaryCtorOrDtor = false;

    /// This call is a constructor for a temporary that is lifetime-extended
    /// by binding it to a reference-type field within an aggregate,
    /// for example 'A { const C &c; }; A a = { C() };'
    bool IsTemporaryLifetimeExtendedViaAggregate = false;

    EvalCallOptions() {}
  };

private:
  cross_tu::CrossTranslationUnitContext &CTU;

  AnalysisManager &AMgr;

  AnalysisDeclContextManager &AnalysisDeclContexts;

  CoreEngine Engine;

  /// G - the simulation graph.
  ExplodedGraph &G;

  /// StateMgr - Object that manages the data for all created states.
  ProgramStateManager StateMgr;

  /// SymMgr - Object that manages the symbol information.
  SymbolManager &SymMgr;

  /// svalBuilder - SValBuilder object that creates SVals from expressions.
  SValBuilder &svalBuilder;

  unsigned int currStmtIdx = 0;
  const NodeBuilderContext *currBldrCtx = nullptr;

  /// Helper object to determine if an Objective-C message expression
  /// implicitly never returns.
  ObjCNoReturn ObjCNoRet;

  /// The BugReporter associated with this engine.  It is important that
  ///  this object be placed at the very end of member variables so that its
  ///  destructor is called before the rest of the ExprEngine is destroyed.
  GRBugReporter BR;

  /// The functions which have been analyzed through inlining. This is owned by
  /// AnalysisConsumer. It can be null.
  SetOfConstDecls *VisitedCallees;

  /// The flag, which specifies the mode of inlining for the engine.
  InliningModes HowToInline;

public:
  ExprEngine(cross_tu::CrossTranslationUnitContext &CTU, AnalysisManager &mgr,
             SetOfConstDecls *VisitedCalleesIn,
             FunctionSummariesTy *FS, InliningModes HowToInlineIn);

  ~ExprEngine() override;

  /// Returns true if there is still simulation state on the worklist.
  bool ExecuteWorkList(const LocationContext *L, unsigned Steps = 150000) {
    return Engine.ExecuteWorkList(L, Steps, nullptr);
  }

  /// Execute the work list with an initial state. Nodes that reaches the exit
  /// of the function are added into the Dst set, which represent the exit
  /// state of the function call. Returns true if there is still simulation
  /// state on the worklist.
  bool ExecuteWorkListWithInitialState(const LocationContext *L, unsigned Steps,
                                       ProgramStateRef InitState,
                                       ExplodedNodeSet &Dst) {
    return Engine.ExecuteWorkListWithInitialState(L, Steps, InitState, Dst);
  }

  /// getContext - Return the ASTContext associated with this analysis.
  ASTContext &getContext() const { return AMgr.getASTContext(); }

  AnalysisManager &getAnalysisManager() override { return AMgr; }

  CheckerManager &getCheckerManager() const {
    return *AMgr.getCheckerManager();
  }

  SValBuilder &getSValBuilder() { return svalBuilder; }

  BugReporter &getBugReporter() { return BR; }

  cross_tu::CrossTranslationUnitContext *
  getCrossTranslationUnitContext() override {
    return &CTU;
  }

  const NodeBuilderContext &getBuilderContext() {
    assert(currBldrCtx);
    return *currBldrCtx;
  }

  const Stmt *getStmt() const;

  void GenerateAutoTransition(ExplodedNode *N);
  void enqueueEndOfPath(ExplodedNodeSet &S);
  void GenerateCallExitNode(ExplodedNode *N);


  /// Dump graph to the specified filename.
  /// If filename is empty, generate a temporary one.
  /// \return The filename the graph is written into.
  std::string DumpGraph(bool trim = false, StringRef Filename="");

  /// Dump the graph consisting of the given nodes to a specified filename.
  /// Generate a temporary filename if it's not provided.
  /// \return The filename the graph is written into.
  std::string DumpGraph(ArrayRef<const ExplodedNode *> Nodes,
                        StringRef Filename = "");

  /// Visualize the ExplodedGraph created by executing the simulation.
  void ViewGraph(bool trim = false);

  /// Visualize a trimmed ExplodedGraph that only contains paths to the given
  /// nodes.
  void ViewGraph(ArrayRef<const ExplodedNode *> Nodes);

  /// getInitialState - Return the initial state used for the root vertex
  ///  in the ExplodedGraph.
  ProgramStateRef getInitialState(const LocationContext *InitLoc) override;

  ExplodedGraph &getGraph() { return G; }
  const ExplodedGraph &getGraph() const { return G; }

  /// Run the analyzer's garbage collection - remove dead symbols and
  /// bindings from the state.
  ///
  /// Checkers can participate in this process with two callbacks:
  /// \c checkLiveSymbols and \c checkDeadSymbols. See the CheckerDocumentation
  /// class for more information.
  ///
  /// \param Node The predecessor node, from which the processing should start.
  /// \param Out The returned set of output nodes.
  /// \param ReferenceStmt The statement which is about to be processed.
  ///        Everything needed for this statement should be considered live.
  ///        A null statement means that everything in child LocationContexts
  ///        is dead.
  /// \param LC The location context of the \p ReferenceStmt. A null location
  ///        context means that we have reached the end of analysis and that
  ///        all statements and local variables should be considered dead.
  /// \param DiagnosticStmt Used as a location for any warnings that should
  ///        occur while removing the dead (e.g. leaks). By default, the
  ///        \p ReferenceStmt is used.
  /// \param K Denotes whether this is a pre- or post-statement purge. This
  ///        must only be ProgramPoint::PostStmtPurgeDeadSymbolsKind if an
  ///        entire location context is being cleared, in which case the
  ///        \p ReferenceStmt must either be a ReturnStmt or \c NULL. Otherwise,
  ///        it must be ProgramPoint::PreStmtPurgeDeadSymbolsKind (the default)
  ///        and \p ReferenceStmt must be valid (non-null).
  void removeDead(ExplodedNode *Node, ExplodedNodeSet &Out,
            const Stmt *ReferenceStmt, const LocationContext *LC,
            const Stmt *DiagnosticStmt = nullptr,
            ProgramPoint::Kind K = ProgramPoint::PreStmtPurgeDeadSymbolsKind);

  /// processCFGElement - Called by CoreEngine. Used to generate new successor
  ///  nodes by processing the 'effects' of a CFG element.
  void processCFGElement(const CFGElement E, ExplodedNode *Pred,
                         unsigned StmtIdx, NodeBuilderContext *Ctx) override;

  void ProcessStmt(const Stmt *S, ExplodedNode *Pred);

  void ProcessLoopExit(const Stmt* S, ExplodedNode *Pred);

  void ProcessInitializer(const CFGInitializer I, ExplodedNode *Pred);

  void ProcessImplicitDtor(const CFGImplicitDtor D, ExplodedNode *Pred);

  void ProcessNewAllocator(const CXXNewExpr *NE, ExplodedNode *Pred);

  void ProcessAutomaticObjDtor(const CFGAutomaticObjDtor D,
                               ExplodedNode *Pred, ExplodedNodeSet &Dst);
  void ProcessDeleteDtor(const CFGDeleteDtor D,
                         ExplodedNode *Pred, ExplodedNodeSet &Dst);
  void ProcessBaseDtor(const CFGBaseDtor D,
                       ExplodedNode *Pred, ExplodedNodeSet &Dst);
  void ProcessMemberDtor(const CFGMemberDtor D,
                         ExplodedNode *Pred, ExplodedNodeSet &Dst);
  void ProcessTemporaryDtor(const CFGTemporaryDtor D,
                            ExplodedNode *Pred, ExplodedNodeSet &Dst);

  /// Called by CoreEngine when processing the entrance of a CFGBlock.
  void processCFGBlockEntrance(const BlockEdge &L,
                               NodeBuilderWithSinks &nodeBuilder,
                               ExplodedNode *Pred) override;

  /// ProcessBranch - Called by CoreEngine.  Used to generate successor
  ///  nodes by processing the 'effects' of a branch condition.
  void processBranch(const Stmt *Condition,
                     NodeBuilderContext& BuilderCtx,
                     ExplodedNode *Pred,
                     ExplodedNodeSet &Dst,
                     const CFGBlock *DstT,
                     const CFGBlock *DstF) override;

  /// Called by CoreEngine.
  /// Used to generate successor nodes for temporary destructors depending
  /// on whether the corresponding constructor was visited.
  void processCleanupTemporaryBranch(const CXXBindTemporaryExpr *BTE,
                                     NodeBuilderContext &BldCtx,
                                     ExplodedNode *Pred, ExplodedNodeSet &Dst,
                                     const CFGBlock *DstT,
                                     const CFGBlock *DstF) override;

  /// Called by CoreEngine.  Used to processing branching behavior
  /// at static initializers.
  void processStaticInitializer(const DeclStmt *DS,
                                NodeBuilderContext& BuilderCtx,
                                ExplodedNode *Pred,
                                ExplodedNodeSet &Dst,
                                const CFGBlock *DstT,
                                const CFGBlock *DstF) override;

  /// processIndirectGoto - Called by CoreEngine.  Used to generate successor
  ///  nodes by processing the 'effects' of a computed goto jump.
  void processIndirectGoto(IndirectGotoNodeBuilder& builder) override;

  /// ProcessSwitch - Called by CoreEngine.  Used to generate successor
  ///  nodes by processing the 'effects' of a switch statement.
  void processSwitch(SwitchNodeBuilder& builder) override;

  /// Called by CoreEngine.  Used to notify checkers that processing a
  /// function has begun. Called for both inlined and and top-level functions.
  void processBeginOfFunction(NodeBuilderContext &BC,
                              ExplodedNode *Pred, ExplodedNodeSet &Dst,
                              const BlockEdge &L) override;

  /// Called by CoreEngine.  Used to notify checkers that processing a
  /// function has ended. Called for both inlined and and top-level functions.
  void processEndOfFunction(NodeBuilderContext& BC,
                            ExplodedNode *Pred,
                            const ReturnStmt *RS = nullptr) override;

  /// Remove dead bindings/symbols before exiting a function.
  void removeDeadOnEndOfFunction(NodeBuilderContext& BC,
                                 ExplodedNode *Pred,
                                 ExplodedNodeSet &Dst);

  /// Generate the entry node of the callee.
  void processCallEnter(NodeBuilderContext& BC, CallEnter CE,
                        ExplodedNode *Pred) override;

  /// Generate the sequence of nodes that simulate the call exit and the post
  /// visit for CallExpr.
  void processCallExit(ExplodedNode *Pred) override;

  /// Called by CoreEngine when the analysis worklist has terminated.
  void processEndWorklist() override;

  /// evalAssume - Callback function invoked by the ConstraintManager when
  ///  making assumptions about state values.
  ProgramStateRef processAssume(ProgramStateRef state, SVal cond,
                                bool assumption) override;

  /// processRegionChanges - Called by ProgramStateManager whenever a change is made
  ///  to the store. Used to update checkers that track region values.
  ProgramStateRef
  processRegionChanges(ProgramStateRef state,
                       const InvalidatedSymbols *invalidated,
                       ArrayRef<const MemRegion *> ExplicitRegions,
                       ArrayRef<const MemRegion *> Regions,
                       const LocationContext *LCtx,
                       const CallEvent *Call) override;

  /// printState - Called by ProgramStateManager to print checker-specific data.
  void printState(raw_ostream &Out, ProgramStateRef State, const char *NL,
                  const char *Sep,
                  const LocationContext *LCtx = nullptr) override;

  ProgramStateManager &getStateManager() override { return StateMgr; }

  StoreManager &getStoreManager() { return StateMgr.getStoreManager(); }

  ConstraintManager &getConstraintManager() {
    return StateMgr.getConstraintManager();
  }

  // FIXME: Remove when we migrate over to just using SValBuilder.
  BasicValueFactory &getBasicVals() {
    return StateMgr.getBasicVals();
  }

  // FIXME: Remove when we migrate over to just using ValueManager.
  SymbolManager &getSymbolManager() { return SymMgr; }
  const SymbolManager &getSymbolManager() const { return SymMgr; }

  // Functions for external checking of whether we have unfinished work
  bool wasBlocksExhausted() const { return Engine.wasBlocksExhausted(); }
  bool hasEmptyWorkList() const { return !Engine.getWorkList()->hasWork(); }
  bool hasWorkRemaining() const { return Engine.hasWorkRemaining(); }

  const CoreEngine &getCoreEngine() const { return Engine; }

public:
  /// Visit - Transfer function logic for all statements.  Dispatches to
  ///  other functions that handle specific kinds of statements.
  void Visit(const Stmt *S, ExplodedNode *Pred, ExplodedNodeSet &Dst);

  /// VisitArraySubscriptExpr - Transfer function for array accesses.
  void VisitArraySubscriptExpr(const ArraySubscriptExpr *Ex,
                               ExplodedNode *Pred,
                               ExplodedNodeSet &Dst);

  /// VisitGCCAsmStmt - Transfer function logic for inline asm.
  void VisitGCCAsmStmt(const GCCAsmStmt *A, ExplodedNode *Pred,
                       ExplodedNodeSet &Dst);

  /// VisitMSAsmStmt - Transfer function logic for MS inline asm.
  void VisitMSAsmStmt(const MSAsmStmt *A, ExplodedNode *Pred,
                      ExplodedNodeSet &Dst);

  /// VisitBlockExpr - Transfer function logic for BlockExprs.
  void VisitBlockExpr(const BlockExpr *BE, ExplodedNode *Pred,
                      ExplodedNodeSet &Dst);

  /// VisitLambdaExpr - Transfer function logic for LambdaExprs.
  void VisitLambdaExpr(const LambdaExpr *LE, ExplodedNode *Pred,
                       ExplodedNodeSet &Dst);

  /// VisitBinaryOperator - Transfer function logic for binary operators.
  void VisitBinaryOperator(const BinaryOperator* B, ExplodedNode *Pred,
                           ExplodedNodeSet &Dst);


  /// VisitCall - Transfer function for function calls.
  void VisitCallExpr(const CallExpr *CE, ExplodedNode *Pred,
                     ExplodedNodeSet &Dst);

  /// VisitCast - Transfer function logic for all casts (implicit and explicit).
  void VisitCast(const CastExpr *CastE, const Expr *Ex, ExplodedNode *Pred,
                 ExplodedNodeSet &Dst);

  /// VisitCompoundLiteralExpr - Transfer function logic for compound literals.
  void VisitCompoundLiteralExpr(const CompoundLiteralExpr *CL,
                                ExplodedNode *Pred, ExplodedNodeSet &Dst);

  /// Transfer function logic for DeclRefExprs and BlockDeclRefExprs.
  void VisitCommonDeclRefExpr(const Expr *DR, const NamedDecl *D,
                              ExplodedNode *Pred, ExplodedNodeSet &Dst);

  /// VisitDeclStmt - Transfer function logic for DeclStmts.
  void VisitDeclStmt(const DeclStmt *DS, ExplodedNode *Pred,
                     ExplodedNodeSet &Dst);

  /// VisitGuardedExpr - Transfer function logic for ?, __builtin_choose
  void VisitGuardedExpr(const Expr *Ex, const Expr *L, const Expr *R,
                        ExplodedNode *Pred, ExplodedNodeSet &Dst);

  void VisitInitListExpr(const InitListExpr *E, ExplodedNode *Pred,
                         ExplodedNodeSet &Dst);

  /// VisitLogicalExpr - Transfer function logic for '&&', '||'
  void VisitLogicalExpr(const BinaryOperator* B, ExplodedNode *Pred,
                        ExplodedNodeSet &Dst);

  /// VisitMemberExpr - Transfer function for member expressions.
  void VisitMemberExpr(const MemberExpr *M, ExplodedNode *Pred,
                       ExplodedNodeSet &Dst);

  /// VisitAtomicExpr - Transfer function for builtin atomic expressions
  void VisitAtomicExpr(const AtomicExpr *E, ExplodedNode *Pred,
                       ExplodedNodeSet &Dst);

  /// Transfer function logic for ObjCAtSynchronizedStmts.
  void VisitObjCAtSynchronizedStmt(const ObjCAtSynchronizedStmt *S,
                                   ExplodedNode *Pred, ExplodedNodeSet &Dst);

  /// Transfer function logic for computing the lvalue of an Objective-C ivar.
  void VisitLvalObjCIvarRefExpr(const ObjCIvarRefExpr *DR, ExplodedNode *Pred,
                                ExplodedNodeSet &Dst);

  /// VisitObjCForCollectionStmt - Transfer function logic for
  ///  ObjCForCollectionStmt.
  void VisitObjCForCollectionStmt(const ObjCForCollectionStmt *S,
                                  ExplodedNode *Pred, ExplodedNodeSet &Dst);

  void VisitObjCMessage(const ObjCMessageExpr *ME, ExplodedNode *Pred,
                        ExplodedNodeSet &Dst);

  /// VisitReturnStmt - Transfer function logic for return statements.
  void VisitReturnStmt(const ReturnStmt *R, ExplodedNode *Pred,
                       ExplodedNodeSet &Dst);

  /// VisitOffsetOfExpr - Transfer function for offsetof.
  void VisitOffsetOfExpr(const OffsetOfExpr *Ex, ExplodedNode *Pred,
                         ExplodedNodeSet &Dst);

  /// VisitUnaryExprOrTypeTraitExpr - Transfer function for sizeof.
  void VisitUnaryExprOrTypeTraitExpr(const UnaryExprOrTypeTraitExpr *Ex,
                                     ExplodedNode *Pred, ExplodedNodeSet &Dst);

  /// VisitUnaryOperator - Transfer function logic for unary operators.
  void VisitUnaryOperator(const UnaryOperator* B, ExplodedNode *Pred,
                          ExplodedNodeSet &Dst);

  /// Handle ++ and -- (both pre- and post-increment).
  void VisitIncrementDecrementOperator(const UnaryOperator* U,
                                       ExplodedNode *Pred,
                                       ExplodedNodeSet &Dst);

  void VisitCXXBindTemporaryExpr(const CXXBindTemporaryExpr *BTE,
                                 ExplodedNodeSet &PreVisit,
                                 ExplodedNodeSet &Dst);

  void VisitCXXCatchStmt(const CXXCatchStmt *CS, ExplodedNode *Pred,
                         ExplodedNodeSet &Dst);

  void VisitCXXThisExpr(const CXXThisExpr *TE, ExplodedNode *Pred,
                        ExplodedNodeSet & Dst);

  void VisitCXXConstructExpr(const CXXConstructExpr *E, ExplodedNode *Pred,
                             ExplodedNodeSet &Dst);

  void VisitCXXDestructor(QualType ObjectType, const MemRegion *Dest,
                          const Stmt *S, bool IsBaseDtor,
                          ExplodedNode *Pred, ExplodedNodeSet &Dst,
                          const EvalCallOptions &Options);

  void VisitCXXNewAllocatorCall(const CXXNewExpr *CNE,
                                ExplodedNode *Pred,
                                ExplodedNodeSet &Dst);

  void VisitCXXNewExpr(const CXXNewExpr *CNE, ExplodedNode *Pred,
                       ExplodedNodeSet &Dst);

  void VisitCXXDeleteExpr(const CXXDeleteExpr *CDE, ExplodedNode *Pred,
                          ExplodedNodeSet &Dst);

  /// Create a C++ temporary object for an rvalue.
  void CreateCXXTemporaryObject(const MaterializeTemporaryExpr *ME,
                                ExplodedNode *Pred,
                                ExplodedNodeSet &Dst);

  /// evalEagerlyAssumeBinOpBifurcation - Given the nodes in 'Src', eagerly assume symbolic
  ///  expressions of the form 'x != 0' and generate new nodes (stored in Dst)
  ///  with those assumptions.
  void evalEagerlyAssumeBinOpBifurcation(ExplodedNodeSet &Dst, ExplodedNodeSet &Src,
                         const Expr *Ex);

  static std::pair<const ProgramPointTag *, const ProgramPointTag *>
    geteagerlyAssumeBinOpBifurcationTags();

  SVal evalMinus(SVal X) {
    return X.isValid() ? svalBuilder.evalMinus(X.castAs<NonLoc>()) : X;
  }

  SVal evalComplement(SVal X) {
    return X.isValid() ? svalBuilder.evalComplement(X.castAs<NonLoc>()) : X;
  }

  ProgramStateRef handleLValueBitCast(ProgramStateRef state, const Expr *Ex,
                                      const LocationContext *LCtx, QualType T,
                                      QualType ExTy, const CastExpr *CastE,
                                      StmtNodeBuilder &Bldr,
                                      ExplodedNode *Pred);

  ProgramStateRef handleLVectorSplat(ProgramStateRef state,
                                     const LocationContext *LCtx,
                                     const CastExpr *CastE,
                                     StmtNodeBuilder &Bldr,
                                     ExplodedNode *Pred);

  void handleUOExtension(ExplodedNodeSet::iterator I,
                         const UnaryOperator* U,
                         StmtNodeBuilder &Bldr);

public:
  SVal evalBinOp(ProgramStateRef state, BinaryOperator::Opcode op,
                 NonLoc L, NonLoc R, QualType T) {
    return svalBuilder.evalBinOpNN(state, op, L, R, T);
  }

  SVal evalBinOp(ProgramStateRef state, BinaryOperator::Opcode op,
                 NonLoc L, SVal R, QualType T) {
    return R.isValid() ? svalBuilder.evalBinOpNN(state, op, L,
                                                 R.castAs<NonLoc>(), T) : R;
  }

  SVal evalBinOp(ProgramStateRef ST, BinaryOperator::Opcode Op,
                 SVal LHS, SVal RHS, QualType T) {
    return svalBuilder.evalBinOp(ST, Op, LHS, RHS, T);
  }

  /// By looking at a certain item that may be potentially part of an object's
  /// ConstructionContext, retrieve such object's location. A particular
  /// statement can be transparently passed as \p Item in most cases.
  static Optional<SVal>
  getObjectUnderConstruction(ProgramStateRef State,
                             const ConstructionContextItem &Item,
                             const LocationContext *LC);

protected:
  /// evalBind - Handle the semantics of binding a value to a specific location.
  ///  This method is used by evalStore, VisitDeclStmt, and others.
  void evalBind(ExplodedNodeSet &Dst, const Stmt *StoreE, ExplodedNode *Pred,
                SVal location, SVal Val, bool atDeclInit = false,
                const ProgramPoint *PP = nullptr);

  /// Call PointerEscape callback when a value escapes as a result of bind.
  ProgramStateRef processPointerEscapedOnBind(ProgramStateRef State,
                                              SVal Loc,
                                              SVal Val,
                                              const LocationContext *LCtx) override;
  /// Call PointerEscape callback when a value escapes as a result of
  /// region invalidation.
  /// \param[in] ITraits Specifies invalidation traits for regions/symbols.
  ProgramStateRef notifyCheckersOfPointerEscape(
                           ProgramStateRef State,
                           const InvalidatedSymbols *Invalidated,
                           ArrayRef<const MemRegion *> ExplicitRegions,
                           const CallEvent *Call,
                           RegionAndSymbolInvalidationTraits &ITraits) override;

  /// A simple wrapper when you only need to notify checkers of pointer-escape
  /// of a single value.
  ProgramStateRef escapeValue(ProgramStateRef State, SVal V,
                              PointerEscapeKind K) const;

public:
  // FIXME: 'tag' should be removed, and a LocationContext should be used
  // instead.
  // FIXME: Comment on the meaning of the arguments, when 'St' may not
  // be the same as Pred->state, and when 'location' may not be the
  // same as state->getLValue(Ex).
  /// Simulate a read of the result of Ex.
  void evalLoad(ExplodedNodeSet &Dst,
                const Expr *NodeEx,  /* Eventually will be a CFGStmt */
                const Expr *BoundExpr,
                ExplodedNode *Pred,
                ProgramStateRef St,
                SVal location,
                const ProgramPointTag *tag = nullptr,
                QualType LoadTy = QualType());

  // FIXME: 'tag' should be removed, and a LocationContext should be used
  // instead.
  void evalStore(ExplodedNodeSet &Dst, const Expr *AssignE, const Expr *StoreE,
                 ExplodedNode *Pred, ProgramStateRef St, SVal TargetLV, SVal Val,
                 const ProgramPointTag *tag = nullptr);

  /// Return the CFG element corresponding to the worklist element
  /// that is currently being processed by ExprEngine.
  CFGElement getCurrentCFGElement() {
    return (*currBldrCtx->getBlock())[currStmtIdx];
  }

  /// Create a new state in which the call return value is binded to the
  /// call origin expression.
  ProgramStateRef bindReturnValue(const CallEvent &Call,
                                  const LocationContext *LCtx,
                                  ProgramStateRef State);

  /// Evaluate a call, running pre- and post-call checks and allowing checkers
  /// to be responsible for handling the evaluation of the call itself.
  void evalCall(ExplodedNodeSet &Dst, ExplodedNode *Pred,
                const CallEvent &Call);

  /// Default implementation of call evaluation.
  void defaultEvalCall(NodeBuilder &B, ExplodedNode *Pred,
                       const CallEvent &Call,
                       const EvalCallOptions &CallOpts = {});

private:
  ProgramStateRef finishArgumentConstruction(ProgramStateRef State,
                                             const CallEvent &Call);
  void finishArgumentConstruction(ExplodedNodeSet &Dst, ExplodedNode *Pred,
                                  const CallEvent &Call);

  void evalLoadCommon(ExplodedNodeSet &Dst,
                      const Expr *NodeEx,  /* Eventually will be a CFGStmt */
                      const Expr *BoundEx,
                      ExplodedNode *Pred,
                      ProgramStateRef St,
                      SVal location,
                      const ProgramPointTag *tag,
                      QualType LoadTy);

  void evalLocation(ExplodedNodeSet &Dst,
                    const Stmt *NodeEx, /* This will eventually be a CFGStmt */
                    const Stmt *BoundEx,
                    ExplodedNode *Pred,
                    ProgramStateRef St,
                    SVal location,
                    bool isLoad);

  /// Count the stack depth and determine if the call is recursive.
  void examineStackFrames(const Decl *D, const LocationContext *LCtx,
                          bool &IsRecursive, unsigned &StackDepth);

  enum CallInlinePolicy {
    CIP_Allowed,
    CIP_DisallowedOnce,
    CIP_DisallowedAlways
  };

  /// See if a particular call should be inlined, by only looking
  /// at the call event and the current state of analysis.
  CallInlinePolicy mayInlineCallKind(const CallEvent &Call,
                                     const ExplodedNode *Pred,
                                     AnalyzerOptions &Opts,
                                     const EvalCallOptions &CallOpts);

  /// Checks our policies and decides weither the given call should be inlined.
  bool shouldInlineCall(const CallEvent &Call, const Decl *D,
                        const ExplodedNode *Pred,
                        const EvalCallOptions &CallOpts = {});

  bool inlineCall(const CallEvent &Call, const Decl *D, NodeBuilder &Bldr,
                  ExplodedNode *Pred, ProgramStateRef State);

  /// Conservatively evaluate call by invalidating regions and binding
  /// a conjured return value.
  void conservativeEvalCall(const CallEvent &Call, NodeBuilder &Bldr,
                            ExplodedNode *Pred, ProgramStateRef State);

  /// Either inline or process the call conservatively (or both), based
  /// on DynamicDispatchBifurcation data.
  void BifurcateCall(const MemRegion *BifurReg,
                     const CallEvent &Call, const Decl *D, NodeBuilder &Bldr,
                     ExplodedNode *Pred);

  bool replayWithoutInlining(ExplodedNode *P, const LocationContext *CalleeLC);

  /// Models a trivial copy or move constructor or trivial assignment operator
  /// call with a simple bind.
  void performTrivialCopy(NodeBuilder &Bldr, ExplodedNode *Pred,
                          const CallEvent &Call);

  /// If the value of the given expression \p InitWithAdjustments is a NonLoc,
  /// copy it into a new temporary object region, and replace the value of the
  /// expression with that.
  ///
  /// If \p Result is provided, the new region will be bound to this expression
  /// instead of \p InitWithAdjustments.
  ///
  /// Returns the temporary region with adjustments into the optional
  /// OutRegionWithAdjustments out-parameter if a new region was indeed needed,
  /// otherwise sets it to nullptr.
  ProgramStateRef createTemporaryRegionIfNeeded(
      ProgramStateRef State, const LocationContext *LC,
      const Expr *InitWithAdjustments, const Expr *Result = nullptr,
      const SubRegion **OutRegionWithAdjustments = nullptr);

  /// Returns a region representing the first element of a (possibly
  /// multi-dimensional) array, for the purposes of element construction or
  /// destruction.
  ///
  /// On return, \p Ty will be set to the base type of the array.
  ///
  /// If the type is not an array type at all, the original value is returned.
  /// Otherwise the "IsArray" flag is set.
  static SVal makeZeroElementRegion(ProgramStateRef State, SVal LValue,
                                    QualType &Ty, bool &IsArray);

  /// For a DeclStmt or CXXInitCtorInitializer, walk backward in the current CFG
  /// block to find the constructor expression that directly constructed into
  /// the storage for this statement. Returns null if the constructor for this
  /// statement created a temporary object region rather than directly
  /// constructing into an existing region.
  const CXXConstructExpr *findDirectConstructorForCurrentCFGElement();

  /// Update the program state with all the path-sensitive information
  /// that's necessary to perform construction of an object with a given
  /// syntactic construction context. If the construction context is unavailable
  /// or unusable for any reason, a dummy temporary region is returned, and the
  /// IsConstructorWithImproperlyModeledTargetRegion flag is set in \p CallOpts.
  /// Returns the updated program state and the new object's this-region.
  std::pair<ProgramStateRef, SVal> prepareForObjectConstruction(
      const Expr *E, ProgramStateRef State, const LocationContext *LCtx,
      const ConstructionContext *CC, EvalCallOptions &CallOpts);

  /// Store the location of a C++ object corresponding to a statement
  /// until the statement is actually encountered. For example, if a DeclStmt
  /// has CXXConstructExpr as its initializer, the object would be considered
  /// to be "under construction" between CXXConstructExpr and DeclStmt.
  /// This allows, among other things, to keep bindings to variable's fields
  /// made within the constructor alive until its declaration actually
  /// goes into scope.
  static ProgramStateRef
  addObjectUnderConstruction(ProgramStateRef State,
                             const ConstructionContextItem &Item,
                             const LocationContext *LC, SVal V);

  /// Mark the object sa fully constructed, cleaning up the state trait
  /// that tracks objects under construction.
  static ProgramStateRef
  finishObjectConstruction(ProgramStateRef State,
                           const ConstructionContextItem &Item,
                           const LocationContext *LC);

  /// If the given expression corresponds to a temporary that was used for
  /// passing into an elidable copy/move constructor and that constructor
  /// was actually elided, track that we also need to elide the destructor.
  static ProgramStateRef elideDestructor(ProgramStateRef State,
                                         const CXXBindTemporaryExpr *BTE,
                                         const LocationContext *LC);

  /// Stop tracking the destructor that corresponds to an elided constructor.
  static ProgramStateRef
  cleanupElidedDestructor(ProgramStateRef State,
                          const CXXBindTemporaryExpr *BTE,
                          const LocationContext *LC);

  /// Returns true if the given expression corresponds to a temporary that
  /// was constructed for passing into an elidable copy/move constructor
  /// and that constructor was actually elided.
  static bool isDestructorElided(ProgramStateRef State,
                                 const CXXBindTemporaryExpr *BTE,
                                 const LocationContext *LC);

  /// Check if all objects under construction have been fully constructed
  /// for the given context range (including FromLC, not including ToLC).
  /// This is useful for assertions. Also checks if elided destructors
  /// were cleaned up.
  static bool areAllObjectsFullyConstructed(ProgramStateRef State,
                                            const LocationContext *FromLC,
                                            const LocationContext *ToLC);
};

/// Traits for storing the call processing policy inside GDM.
/// The GDM stores the corresponding CallExpr pointer.
// FIXME: This does not use the nice trait macros because it must be accessible
// from multiple translation units.
struct ReplayWithoutInlining{};
template <>
struct ProgramStateTrait<ReplayWithoutInlining> :
  public ProgramStatePartialTrait<const void*> {
  static void *GDMIndex();
};

} // namespace ento

} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_EXPRENGINE_H
