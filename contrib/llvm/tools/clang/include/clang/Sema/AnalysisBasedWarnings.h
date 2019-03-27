//=- AnalysisBasedWarnings.h - Sema warnings based on libAnalysis -*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines AnalysisBasedWarnings, a worker object used by Sema
// that issues warnings based on dataflow-analysis.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_ANALYSISBASEDWARNINGS_H
#define LLVM_CLANG_SEMA_ANALYSISBASEDWARNINGS_H

#include "llvm/ADT/DenseMap.h"

namespace clang {

class BlockExpr;
class Decl;
class FunctionDecl;
class ObjCMethodDecl;
class QualType;
class Sema;
namespace sema {
  class FunctionScopeInfo;
}

namespace sema {

class AnalysisBasedWarnings {
public:
  class Policy {
    friend class AnalysisBasedWarnings;
    // The warnings to run.
    unsigned enableCheckFallThrough : 1;
    unsigned enableCheckUnreachable : 1;
    unsigned enableThreadSafetyAnalysis : 1;
    unsigned enableConsumedAnalysis : 1;
  public:
    Policy();
    void disableCheckFallThrough() { enableCheckFallThrough = 0; }
  };

private:
  Sema &S;
  Policy DefaultPolicy;

  enum VisitFlag { NotVisited = 0, Visited = 1, Pending = 2 };
  llvm::DenseMap<const FunctionDecl*, VisitFlag> VisitedFD;

  /// \name Statistics
  /// @{

  /// Number of function CFGs built and analyzed.
  unsigned NumFunctionsAnalyzed;

  /// Number of functions for which the CFG could not be successfully
  /// built.
  unsigned NumFunctionsWithBadCFGs;

  /// Total number of blocks across all CFGs.
  unsigned NumCFGBlocks;

  /// Largest number of CFG blocks for a single function analyzed.
  unsigned MaxCFGBlocksPerFunction;

  /// Total number of CFGs with variables analyzed for uninitialized
  /// uses.
  unsigned NumUninitAnalysisFunctions;

  /// Total number of variables analyzed for uninitialized uses.
  unsigned NumUninitAnalysisVariables;

  /// Max number of variables analyzed for uninitialized uses in a single
  /// function.
  unsigned MaxUninitAnalysisVariablesPerFunction;

  /// Total number of block visits during uninitialized use analysis.
  unsigned NumUninitAnalysisBlockVisits;

  /// Max number of block visits during uninitialized use analysis of
  /// a single function.
  unsigned MaxUninitAnalysisBlockVisitsPerFunction;

  /// @}

public:
  AnalysisBasedWarnings(Sema &s);

  void IssueWarnings(Policy P, FunctionScopeInfo *fscope,
                     const Decl *D, const BlockExpr *blkExpr);

  Policy getDefaultPolicy() { return DefaultPolicy; }

  void PrintStats() const;
};

}} // end namespace clang::sema

#endif
