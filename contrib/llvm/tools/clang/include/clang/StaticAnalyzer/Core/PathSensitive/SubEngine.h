//== SubEngine.h - Interface of the subengine of CoreEngine --------*- C++ -*-//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interface of a subengine of the CoreEngine.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SUBENGINE_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SUBENGINE_H

#include "clang/Analysis/ProgramPoint.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/Store.h"

namespace clang {

class CFGBlock;
class CFGElement;
class LocationContext;
class Stmt;

namespace cross_tu {
class CrossTranslationUnitContext;
}

namespace ento {

struct NodeBuilderContext;
class AnalysisManager;
class ExplodedNodeSet;
class ExplodedNode;
class ProgramState;
class ProgramStateManager;
class BlockCounter;
class BranchNodeBuilder;
class IndirectGotoNodeBuilder;
class SwitchNodeBuilder;
class EndOfFunctionNodeBuilder;
class NodeBuilderWithSinks;
class MemRegion;

class SubEngine {
  virtual void anchor();
public:
  virtual ~SubEngine() {}

  virtual ProgramStateRef getInitialState(const LocationContext *InitLoc) = 0;

  virtual AnalysisManager &getAnalysisManager() = 0;

  virtual cross_tu::CrossTranslationUnitContext *
  getCrossTranslationUnitContext() = 0;

  virtual ProgramStateManager &getStateManager() = 0;

  /// Called by CoreEngine. Used to generate new successor
  /// nodes by processing the 'effects' of a block-level statement.
  virtual void processCFGElement(const CFGElement E, ExplodedNode* Pred,
                                 unsigned StmtIdx, NodeBuilderContext *Ctx)=0;

  /// Called by CoreEngine when it starts processing a CFGBlock.  The
  /// SubEngine is expected to populate dstNodes with new nodes representing
  /// updated analysis state, or generate no nodes at all if it doesn't.
  virtual void processCFGBlockEntrance(const BlockEdge &L,
                                       NodeBuilderWithSinks &nodeBuilder,
                                       ExplodedNode *Pred) = 0;

  /// Called by CoreEngine.  Used to generate successor
  ///  nodes by processing the 'effects' of a branch condition.
  virtual void processBranch(const Stmt *Condition,
                             NodeBuilderContext& BuilderCtx,
                             ExplodedNode *Pred,
                             ExplodedNodeSet &Dst,
                             const CFGBlock *DstT,
                             const CFGBlock *DstF) = 0;

  /// Called by CoreEngine.
  /// Used to generate successor nodes for temporary destructors depending
  /// on whether the corresponding constructor was visited.
  virtual void processCleanupTemporaryBranch(const CXXBindTemporaryExpr *BTE,
                                             NodeBuilderContext &BldCtx,
                                             ExplodedNode *Pred,
                                             ExplodedNodeSet &Dst,
                                             const CFGBlock *DstT,
                                             const CFGBlock *DstF) = 0;

  /// Called by CoreEngine.  Used to processing branching behavior
  /// at static initializers.
  virtual void processStaticInitializer(const DeclStmt *DS,
                                        NodeBuilderContext& BuilderCtx,
                                        ExplodedNode *Pred,
                                        ExplodedNodeSet &Dst,
                                        const CFGBlock *DstT,
                                        const CFGBlock *DstF) = 0;

  /// Called by CoreEngine.  Used to generate successor
  /// nodes by processing the 'effects' of a computed goto jump.
  virtual void processIndirectGoto(IndirectGotoNodeBuilder& builder) = 0;

  /// Called by CoreEngine.  Used to generate successor
  /// nodes by processing the 'effects' of a switch statement.
  virtual void processSwitch(SwitchNodeBuilder& builder) = 0;

  /// Called by CoreEngine.  Used to notify checkers that processing a
  /// function has begun. Called for both inlined and and top-level functions.
  virtual void processBeginOfFunction(NodeBuilderContext &BC,
                                      ExplodedNode *Pred,
                                      ExplodedNodeSet &Dst,
                                      const BlockEdge &L) = 0;

  /// Called by CoreEngine.  Used to notify checkers that processing a
  /// function has ended. Called for both inlined and and top-level functions.
  virtual void processEndOfFunction(NodeBuilderContext& BC,
                                    ExplodedNode *Pred,
                                    const ReturnStmt *RS = nullptr) = 0;

  // Generate the entry node of the callee.
  virtual void processCallEnter(NodeBuilderContext& BC, CallEnter CE,
                                ExplodedNode *Pred) = 0;

  // Generate the first post callsite node.
  virtual void processCallExit(ExplodedNode *Pred) = 0;

  /// Called by ConstraintManager. Used to call checker-specific
  /// logic for handling assumptions on symbolic values.
  virtual ProgramStateRef processAssume(ProgramStateRef state,
                                       SVal cond, bool assumption) = 0;

  /// processRegionChanges - Called by ProgramStateManager whenever a change is
  /// made to the store. Used to update checkers that track region values.
  virtual ProgramStateRef
  processRegionChanges(ProgramStateRef state,
                       const InvalidatedSymbols *invalidated,
                       ArrayRef<const MemRegion *> ExplicitRegions,
                       ArrayRef<const MemRegion *> Regions,
                       const LocationContext *LCtx,
                       const CallEvent *Call) = 0;


  inline ProgramStateRef
  processRegionChange(ProgramStateRef state,
                      const MemRegion* MR,
                      const LocationContext *LCtx) {
    return processRegionChanges(state, nullptr, MR, MR, LCtx, nullptr);
  }

  virtual ProgramStateRef
  processPointerEscapedOnBind(ProgramStateRef State, SVal Loc, SVal Val, const LocationContext *LCtx) = 0;

  virtual ProgramStateRef
  notifyCheckersOfPointerEscape(ProgramStateRef State,
                           const InvalidatedSymbols *Invalidated,
                           ArrayRef<const MemRegion *> ExplicitRegions,
                           const CallEvent *Call,
                           RegionAndSymbolInvalidationTraits &HTraits) = 0;

  /// printState - Called by ProgramStateManager to print checker-specific data.
  virtual void printState(raw_ostream &Out, ProgramStateRef State,
                          const char *NL, const char *Sep,
                          const LocationContext *LCtx = nullptr) = 0;

  /// Called by CoreEngine when the analysis worklist is either empty or the
  //  maximum number of analysis steps have been reached.
  virtual void processEndWorklist() = 0;
};

} // end GR namespace

} // end clang namespace

#endif
