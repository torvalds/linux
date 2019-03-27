//===- BugReporterVisitors.h - Generate PathDiagnostics ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file declares BugReporterVisitors, which are used to generate enhanced
//  diagnostic traces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_BUGREPORTER_BUGREPORTERVISITORS_H
#define LLVM_CLANG_STATICANALYZER_CORE_BUGREPORTER_BUGREPORTERVISITORS_H

#include "clang/Basic/LLVM.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/RangedConstraintManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include <memory>

namespace clang {

class BinaryOperator;
class CFGBlock;
class DeclRefExpr;
class Expr;
class Stmt;

namespace ento {

class BugReport;
class BugReporterContext;
class ExplodedNode;
class MemRegion;
class PathDiagnosticPiece;

/// BugReporterVisitors are used to add custom diagnostics along a path.
class BugReporterVisitor : public llvm::FoldingSetNode {
public:
  BugReporterVisitor() = default;
  BugReporterVisitor(const BugReporterVisitor &) = default;
  BugReporterVisitor(BugReporterVisitor &&) {}
  virtual ~BugReporterVisitor();

  /// Return a diagnostic piece which should be associated with the
  /// given node.
  /// Note that this function does *not* get run on the very last node
  /// of the report, as the PathDiagnosticPiece associated with the
  /// last node should be unique.
  /// Use {@code getEndPath} to customize the note associated with the report
  /// end instead.
  ///
  /// The last parameter can be used to register a new visitor with the given
  /// BugReport while processing a node.
  virtual std::shared_ptr<PathDiagnosticPiece>
  VisitNode(const ExplodedNode *Succ, 
            BugReporterContext &BRC, BugReport &BR) = 0;

  /// Last function called on the visitor, no further calls to VisitNode
  /// would follow.
  virtual void finalizeVisitor(BugReporterContext &BRC,
                               const ExplodedNode *EndPathNode,
                               BugReport &BR);

  /// Provide custom definition for the final diagnostic piece on the
  /// path - the piece, which is displayed before the path is expanded.
  ///
  /// NOTE that this function can be implemented on at most one used visitor,
  /// and otherwise it crahes at runtime.
  virtual std::shared_ptr<PathDiagnosticPiece>
  getEndPath(BugReporterContext &BRC, const ExplodedNode *N, BugReport &BR);

  virtual void Profile(llvm::FoldingSetNodeID &ID) const = 0;

  /// Generates the default final diagnostic piece.
  static std::shared_ptr<PathDiagnosticPiece>
  getDefaultEndPath(BugReporterContext &BRC, const ExplodedNode *N,
                    BugReport &BR);
};

/// Finds last store into the given region,
/// which is different from a given symbolic value.
class FindLastStoreBRVisitor final : public BugReporterVisitor {
  const MemRegion *R;
  SVal V;
  bool Satisfied = false;

  /// If the visitor is tracking the value directly responsible for the
  /// bug, we are going to employ false positive suppression.
  bool EnableNullFPSuppression;

public:
  /// Creates a visitor for every VarDecl inside a Stmt and registers it with
  /// the BugReport.
  static void registerStatementVarDecls(BugReport &BR, const Stmt *S,
                                        bool EnableNullFPSuppression);

  FindLastStoreBRVisitor(KnownSVal V, const MemRegion *R,
                         bool InEnableNullFPSuppression)
      : R(R), V(V), EnableNullFPSuppression(InEnableNullFPSuppression) {}

  void Profile(llvm::FoldingSetNodeID &ID) const override;

  std::shared_ptr<PathDiagnosticPiece> VisitNode(const ExplodedNode *N,
                                                 BugReporterContext &BRC,
                                                 BugReport &BR) override;
};

class TrackConstraintBRVisitor final : public BugReporterVisitor {
  DefinedSVal Constraint;
  bool Assumption;
  bool IsSatisfied = false;
  bool IsZeroCheck;

  /// We should start tracking from the last node along the path in which the
  /// value is constrained.
  bool IsTrackingTurnedOn = false;

public:
  TrackConstraintBRVisitor(DefinedSVal constraint, bool assumption)
      : Constraint(constraint), Assumption(assumption),
        IsZeroCheck(!Assumption && Constraint.getAs<Loc>()) {}

  void Profile(llvm::FoldingSetNodeID &ID) const override;

  /// Return the tag associated with this visitor.  This tag will be used
  /// to make all PathDiagnosticPieces created by this visitor.
  static const char *getTag();

  std::shared_ptr<PathDiagnosticPiece> VisitNode(const ExplodedNode *N,
                                                 BugReporterContext &BRC,
                                                 BugReport &BR) override;

private:
  /// Checks if the constraint is valid in the current state.
  bool isUnderconstrained(const ExplodedNode *N) const;
};

/// \class NilReceiverBRVisitor
/// Prints path notes when a message is sent to a nil receiver.
class NilReceiverBRVisitor final : public BugReporterVisitor {
public:
  void Profile(llvm::FoldingSetNodeID &ID) const override {
    static int x = 0;
    ID.AddPointer(&x);
  }

  std::shared_ptr<PathDiagnosticPiece> VisitNode(const ExplodedNode *N,
                                                 BugReporterContext &BRC,
                                                 BugReport &BR) override;

  /// If the statement is a message send expression with nil receiver, returns
  /// the receiver expression. Returns NULL otherwise.
  static const Expr *getNilReceiver(const Stmt *S, const ExplodedNode *N);
};

/// Visitor that tries to report interesting diagnostics from conditions.
class ConditionBRVisitor final : public BugReporterVisitor {
  // FIXME: constexpr initialization isn't supported by MSVC2013.
  static const char *const GenericTrueMessage;
  static const char *const GenericFalseMessage;

public:
  void Profile(llvm::FoldingSetNodeID &ID) const override {
    static int x = 0;
    ID.AddPointer(&x);
  }

  /// Return the tag associated with this visitor.  This tag will be used
  /// to make all PathDiagnosticPieces created by this visitor.
  static const char *getTag();

  std::shared_ptr<PathDiagnosticPiece> VisitNode(const ExplodedNode *N,
                                                 BugReporterContext &BRC,
                                                 BugReport &BR) override;

  std::shared_ptr<PathDiagnosticPiece> VisitNodeImpl(const ExplodedNode *N,
                                                     BugReporterContext &BRC,
                                                     BugReport &BR);

  std::shared_ptr<PathDiagnosticPiece>
  VisitTerminator(const Stmt *Term, const ExplodedNode *N,
                  const CFGBlock *srcBlk, const CFGBlock *dstBlk, BugReport &R,
                  BugReporterContext &BRC);

  std::shared_ptr<PathDiagnosticPiece>
  VisitTrueTest(const Expr *Cond, bool tookTrue, BugReporterContext &BRC,
                BugReport &R, const ExplodedNode *N);

  std::shared_ptr<PathDiagnosticPiece>
  VisitTrueTest(const Expr *Cond, const DeclRefExpr *DR, const bool tookTrue,
                BugReporterContext &BRC, BugReport &R, const ExplodedNode *N);

  std::shared_ptr<PathDiagnosticPiece>
  VisitTrueTest(const Expr *Cond, const BinaryOperator *BExpr,
                const bool tookTrue, BugReporterContext &BRC, BugReport &R,
                const ExplodedNode *N);

  std::shared_ptr<PathDiagnosticPiece>
  VisitConditionVariable(StringRef LhsString, const Expr *CondVarExpr,
                         const bool tookTrue, BugReporterContext &BRC,
                         BugReport &R, const ExplodedNode *N);

  bool patternMatch(const Expr *Ex,
                    const Expr *ParentEx,
                    raw_ostream &Out,
                    BugReporterContext &BRC,
                    BugReport &R,
                    const ExplodedNode *N,
                    Optional<bool> &prunable);

  static bool isPieceMessageGeneric(const PathDiagnosticPiece *Piece);
};

/// Suppress reports that might lead to known false positives.
///
/// Currently this suppresses reports based on locations of bugs.
class LikelyFalsePositiveSuppressionBRVisitor final
    : public BugReporterVisitor {
public:
  static void *getTag() {
    static int Tag = 0;
    return static_cast<void *>(&Tag);
  }

  void Profile(llvm::FoldingSetNodeID &ID) const override {
    ID.AddPointer(getTag());
  }

  std::shared_ptr<PathDiagnosticPiece> VisitNode(const ExplodedNode *,
                                                 BugReporterContext &,
                                                 BugReport &) override {
    return nullptr;
  }

  void finalizeVisitor(BugReporterContext &BRC, const ExplodedNode *N,
                       BugReport &BR) override;
};

/// When a region containing undefined value or '0' value is passed
/// as an argument in a call, marks the call as interesting.
///
/// As a result, BugReporter will not prune the path through the function even
/// if the region's contents are not modified/accessed by the call.
class UndefOrNullArgVisitor final : public BugReporterVisitor {
  /// The interesting memory region this visitor is tracking.
  const MemRegion *R;

public:
  UndefOrNullArgVisitor(const MemRegion *InR) : R(InR) {}

  void Profile(llvm::FoldingSetNodeID &ID) const override {
    static int Tag = 0;
    ID.AddPointer(&Tag);
    ID.AddPointer(R);
  }

  std::shared_ptr<PathDiagnosticPiece> VisitNode(const ExplodedNode *N,
                                                 BugReporterContext &BRC,
                                                 BugReport &BR) override;
};

class SuppressInlineDefensiveChecksVisitor final : public BugReporterVisitor {
  /// The symbolic value for which we are tracking constraints.
  /// This value is constrained to null in the end of path.
  DefinedSVal V;

  /// Track if we found the node where the constraint was first added.
  bool IsSatisfied = false;

  /// Since the visitors can be registered on nodes previous to the last
  /// node in the BugReport, but the path traversal always starts with the last
  /// node, the visitor invariant (that we start with a node in which V is null)
  /// might not hold when node visitation starts. We are going to start tracking
  /// from the last node in which the value is null.
  bool IsTrackingTurnedOn = false;

public:
  SuppressInlineDefensiveChecksVisitor(DefinedSVal Val, const ExplodedNode *N);

  void Profile(llvm::FoldingSetNodeID &ID) const override;

  /// Return the tag associated with this visitor.  This tag will be used
  /// to make all PathDiagnosticPieces created by this visitor.
  static const char *getTag();

  std::shared_ptr<PathDiagnosticPiece> VisitNode(const ExplodedNode *Succ,
                                                 BugReporterContext &BRC,
                                                 BugReport &BR) override;
};

class CXXSelfAssignmentBRVisitor final : public BugReporterVisitor {
  bool Satisfied = false;

public:
  CXXSelfAssignmentBRVisitor() = default;

  void Profile(llvm::FoldingSetNodeID &ID) const override {}

  std::shared_ptr<PathDiagnosticPiece> VisitNode(const ExplodedNode *Succ,
                                                 BugReporterContext &BRC,
                                                 BugReport &BR) override;
};

/// The bug visitor prints a diagnostic message at the location where a given
/// variable was tainted.
class TaintBugVisitor final : public BugReporterVisitor {
private:
  const SVal V;

public:
  TaintBugVisitor(const SVal V) : V(V) {}
  void Profile(llvm::FoldingSetNodeID &ID) const override { ID.Add(V); }

  std::shared_ptr<PathDiagnosticPiece> VisitNode(const ExplodedNode *N,
                                                 BugReporterContext &BRC,
                                                 BugReport &BR) override;
};

/// The bug visitor will walk all the nodes in a path and collect all the
/// constraints. When it reaches the root node, will create a refutation
/// manager and check if the constraints are satisfiable
class FalsePositiveRefutationBRVisitor final : public BugReporterVisitor {
private:
  /// Holds the constraints in a given path
  ConstraintRangeTy Constraints;

public:
  FalsePositiveRefutationBRVisitor();

  void Profile(llvm::FoldingSetNodeID &ID) const override;

  std::shared_ptr<PathDiagnosticPiece> VisitNode(const ExplodedNode *N,
                                                 BugReporterContext &BRC,
                                                 BugReport &BR) override;

  void finalizeVisitor(BugReporterContext &BRC, const ExplodedNode *EndPathNode,
                       BugReport &BR) override;
};

namespace bugreporter {

/// Attempts to add visitors to track expression value back to its point of
/// origin.
///
/// \param N A node "downstream" from the evaluation of the statement.
/// \param E The expression value which we are tracking
/// \param R The bug report to which visitors should be attached.
/// \param EnableNullFPSuppression Whether we should employ false positive
///         suppression (inlined defensive checks, returned null).
///
/// \return Whether or not the function was able to add visitors for this
///         statement. Note that returning \c true does not actually imply
///         that any visitors were added.
bool trackExpressionValue(const ExplodedNode *N, const Expr *E, BugReport &R,
                          bool EnableNullFPSuppression = true);

const Expr *getDerefExpr(const Stmt *S);

} // namespace bugreporter

} // namespace ento

} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_BUGREPORTER_BUGREPORTERVISITORS_H
