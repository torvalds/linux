//===- Inliner.h - Inliner pass and infrastructure --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_INLINER_H
#define LLVM_TRANSFORMS_IPO_INLINER_H

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Utils/ImportedFunctionsInliningStatistics.h"
#include <utility>

namespace llvm {

class AssumptionCacheTracker;
class CallGraph;
class ProfileSummaryInfo;

/// This class contains all of the helper code which is used to perform the
/// inlining operations that do not depend on the policy. It contains the core
/// bottom-up inlining infrastructure that specific inliner passes use.
struct LegacyInlinerBase : public CallGraphSCCPass {
  explicit LegacyInlinerBase(char &ID);
  explicit LegacyInlinerBase(char &ID, bool InsertLifetime);

  /// For this class, we declare that we require and preserve the call graph.
  /// If the derived class implements this method, it should always explicitly
  /// call the implementation here.
  void getAnalysisUsage(AnalysisUsage &Info) const override;

  bool doInitialization(CallGraph &CG) override;

  /// Main run interface method, this implements the interface required by the
  /// Pass class.
  bool runOnSCC(CallGraphSCC &SCC) override;

  using llvm::Pass::doFinalization;

  /// Remove now-dead linkonce functions at the end of processing to avoid
  /// breaking the SCC traversal.
  bool doFinalization(CallGraph &CG) override;

  /// This method must be implemented by the subclass to determine the cost of
  /// inlining the specified call site.  If the cost returned is greater than
  /// the current inline threshold, the call site is not inlined.
  virtual InlineCost getInlineCost(CallSite CS) = 0;

  /// Remove dead functions.
  ///
  /// This also includes a hack in the form of the 'AlwaysInlineOnly' flag
  /// which restricts it to deleting functions with an 'AlwaysInline'
  /// attribute. This is useful for the InlineAlways pass that only wants to
  /// deal with that subset of the functions.
  bool removeDeadFunctions(CallGraph &CG, bool AlwaysInlineOnly = false);

  /// This function performs the main work of the pass.  The default of
  /// Inlinter::runOnSCC() calls skipSCC() before calling this method, but
  /// derived classes which cannot be skipped can override that method and call
  /// this function unconditionally.
  bool inlineCalls(CallGraphSCC &SCC);

private:
  // Insert @llvm.lifetime intrinsics.
  bool InsertLifetime = true;

protected:
  AssumptionCacheTracker *ACT;
  ProfileSummaryInfo *PSI;
  ImportedFunctionsInliningStatistics ImportedFunctionsStats;
};

/// The inliner pass for the new pass manager.
///
/// This pass wires together the inlining utilities and the inline cost
/// analysis into a CGSCC pass. It considers every call in every function in
/// the SCC and tries to inline if profitable. It can be tuned with a number of
/// parameters to control what cost model is used and what tradeoffs are made
/// when making the decision.
///
/// It should be noted that the legacy inliners do considerably more than this
/// inliner pass does. They provide logic for manually merging allocas, and
/// doing considerable DCE including the DCE of dead functions. This pass makes
/// every attempt to be simpler. DCE of functions requires complex reasoning
/// about comdat groups, etc. Instead, it is expected that other more focused
/// passes be composed to achieve the same end result.
class InlinerPass : public PassInfoMixin<InlinerPass> {
public:
  InlinerPass(InlineParams Params = getInlineParams())
      : Params(std::move(Params)) {}
  ~InlinerPass();
  InlinerPass(InlinerPass &&Arg)
      : Params(std::move(Arg.Params)),
        ImportedFunctionsStats(std::move(Arg.ImportedFunctionsStats)) {}

  PreservedAnalyses run(LazyCallGraph::SCC &C, CGSCCAnalysisManager &AM,
                        LazyCallGraph &CG, CGSCCUpdateResult &UR);

private:
  InlineParams Params;
  std::unique_ptr<ImportedFunctionsInliningStatistics> ImportedFunctionsStats;
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_INLINER_H
