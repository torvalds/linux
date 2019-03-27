//===- LiveVariables.h - Live Variable Analysis for Source CFGs -*- C++ --*-//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements Live Variables analysis for source-level CFGs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_LIVEVARIABLES_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_LIVEVARIABLES_H

#include "clang/AST/Decl.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "llvm/ADT/ImmutableSet.h"

namespace clang {

class CFG;
class CFGBlock;
class Stmt;
class DeclRefExpr;
class SourceManager;

class LiveVariables : public ManagedAnalysis {
public:
  class LivenessValues {
  public:

    llvm::ImmutableSet<const Stmt *> liveStmts;
    llvm::ImmutableSet<const VarDecl *> liveDecls;
    llvm::ImmutableSet<const BindingDecl *> liveBindings;

    bool equals(const LivenessValues &V) const;

    LivenessValues()
      : liveStmts(nullptr), liveDecls(nullptr), liveBindings(nullptr) {}

    LivenessValues(llvm::ImmutableSet<const Stmt *> LiveStmts,
                   llvm::ImmutableSet<const VarDecl *> LiveDecls,
                   llvm::ImmutableSet<const BindingDecl *> LiveBindings)
        : liveStmts(LiveStmts), liveDecls(LiveDecls),
          liveBindings(LiveBindings) {}

    bool isLive(const Stmt *S) const;
    bool isLive(const VarDecl *D) const;

    friend class LiveVariables;
  };

  class Observer {
    virtual void anchor();
  public:
    virtual ~Observer() {}

    /// A callback invoked right before invoking the
    ///  liveness transfer function on the given statement.
    virtual void observeStmt(const Stmt *S,
                             const CFGBlock *currentBlock,
                             const LivenessValues& V) {}

    /// Called when the live variables analysis registers
    /// that a variable is killed.
    virtual void observerKill(const DeclRefExpr *DR) {}
  };

  ~LiveVariables() override;

  /// Compute the liveness information for a given CFG.
  static LiveVariables *computeLiveness(AnalysisDeclContext &analysisContext,
                                        bool killAtAssign);

  /// Return true if a variable is live at the end of a
  /// specified block.
  bool isLive(const CFGBlock *B, const VarDecl *D);

  /// Returns true if a variable is live at the beginning of the
  ///  the statement.  This query only works if liveness information
  ///  has been recorded at the statement level (see runOnAllBlocks), and
  ///  only returns liveness information for block-level expressions.
  bool isLive(const Stmt *S, const VarDecl *D);

  /// Returns true the block-level expression "value" is live
  ///  before the given block-level expression (see runOnAllBlocks).
  bool isLive(const Stmt *Loc, const Stmt *StmtVal);

  /// Print to stderr the variable liveness information associated with
  /// each basic block.
  void dumpBlockLiveness(const SourceManager &M);

  /// Print to stderr the statement liveness information associated with
  /// each basic block.
  void dumpStmtLiveness(const SourceManager &M);

  void runOnAllBlocks(Observer &obs);

  static LiveVariables *create(AnalysisDeclContext &analysisContext) {
    return computeLiveness(analysisContext, true);
  }

  static const void *getTag();

private:
  LiveVariables(void *impl);
  void *impl;
};

class RelaxedLiveVariables : public LiveVariables {
public:
  static LiveVariables *create(AnalysisDeclContext &analysisContext) {
    return computeLiveness(analysisContext, false);
  }

  static const void *getTag();
};

} // end namespace clang

#endif
