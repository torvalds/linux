//===- LoopPassManager.h - Loop pass management -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This header provides classes for managing a pipeline of passes over loops
/// in LLVM IR.
///
/// The primary loop pass pipeline is managed in a very particular way to
/// provide a set of core guarantees:
/// 1) Loops are, where possible, in simplified form.
/// 2) Loops are *always* in LCSSA form.
/// 3) A collection of Loop-specific analysis results are available:
///    - LoopInfo
///    - DominatorTree
///    - ScalarEvolution
///    - AAManager
/// 4) All loop passes preserve #1 (where possible), #2, and #3.
/// 5) Loop passes run over each loop in the loop nest from the innermost to
///    the outermost. Specifically, all inner loops are processed before
///    passes run over outer loops. When running the pipeline across an inner
///    loop creates new inner loops, those are added and processed in this
///    order as well.
///
/// This process is designed to facilitate transformations which simplify,
/// reduce, and remove loops. For passes which are more oriented towards
/// optimizing loops, especially optimizing loop *nests* instead of single
/// loops in isolation, this framework is less interesting.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPPASSMANAGER_H
#define LLVM_TRANSFORMS_SCALAR_LOOPPASSMANAGER_H

#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/PriorityWorklist.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionAliasAnalysis.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Utils/LCSSA.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"

namespace llvm {

// Forward declarations of an update tracking API used in the pass manager.
class LPMUpdater;

// Explicit specialization and instantiation declarations for the pass manager.
// See the comments on the definition of the specialization for details on how
// it differs from the primary template.
template <>
PreservedAnalyses
PassManager<Loop, LoopAnalysisManager, LoopStandardAnalysisResults &,
            LPMUpdater &>::run(Loop &InitialL, LoopAnalysisManager &AM,
                               LoopStandardAnalysisResults &AnalysisResults,
                               LPMUpdater &U);
extern template class PassManager<Loop, LoopAnalysisManager,
                                  LoopStandardAnalysisResults &, LPMUpdater &>;

/// The Loop pass manager.
///
/// See the documentation for the PassManager template for details. It runs
/// a sequence of Loop passes over each Loop that the manager is run over. This
/// typedef serves as a convenient way to refer to this construct.
typedef PassManager<Loop, LoopAnalysisManager, LoopStandardAnalysisResults &,
                    LPMUpdater &>
    LoopPassManager;

/// A partial specialization of the require analysis template pass to forward
/// the extra parameters from a transformation's run method to the
/// AnalysisManager's getResult.
template <typename AnalysisT>
struct RequireAnalysisPass<AnalysisT, Loop, LoopAnalysisManager,
                           LoopStandardAnalysisResults &, LPMUpdater &>
    : PassInfoMixin<
          RequireAnalysisPass<AnalysisT, Loop, LoopAnalysisManager,
                              LoopStandardAnalysisResults &, LPMUpdater &>> {
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &) {
    (void)AM.template getResult<AnalysisT>(L, AR);
    return PreservedAnalyses::all();
  }
};

/// An alias template to easily name a require analysis loop pass.
template <typename AnalysisT>
using RequireAnalysisLoopPass =
    RequireAnalysisPass<AnalysisT, Loop, LoopAnalysisManager,
                        LoopStandardAnalysisResults &, LPMUpdater &>;

namespace internal {
/// Helper to implement appending of loops onto a worklist.
///
/// We want to process loops in postorder, but the worklist is a LIFO data
/// structure, so we append to it in *reverse* postorder.
///
/// For trees, a preorder traversal is a viable reverse postorder, so we
/// actually append using a preorder walk algorithm.
template <typename RangeT>
inline void appendLoopsToWorklist(RangeT &&Loops,
                                  SmallPriorityWorklist<Loop *, 4> &Worklist) {
  // We use an internal worklist to build up the preorder traversal without
  // recursion.
  SmallVector<Loop *, 4> PreOrderLoops, PreOrderWorklist;

  // We walk the initial sequence of loops in reverse because we generally want
  // to visit defs before uses and the worklist is LIFO.
  for (Loop *RootL : reverse(Loops)) {
    assert(PreOrderLoops.empty() && "Must start with an empty preorder walk.");
    assert(PreOrderWorklist.empty() &&
           "Must start with an empty preorder walk worklist.");
    PreOrderWorklist.push_back(RootL);
    do {
      Loop *L = PreOrderWorklist.pop_back_val();
      PreOrderWorklist.append(L->begin(), L->end());
      PreOrderLoops.push_back(L);
    } while (!PreOrderWorklist.empty());

    Worklist.insert(std::move(PreOrderLoops));
    PreOrderLoops.clear();
  }
}
}

template <typename LoopPassT> class FunctionToLoopPassAdaptor;

/// This class provides an interface for updating the loop pass manager based
/// on mutations to the loop nest.
///
/// A reference to an instance of this class is passed as an argument to each
/// Loop pass, and Loop passes should use it to update LPM infrastructure if
/// they modify the loop nest structure.
class LPMUpdater {
public:
  /// This can be queried by loop passes which run other loop passes (like pass
  /// managers) to know whether the loop needs to be skipped due to updates to
  /// the loop nest.
  ///
  /// If this returns true, the loop object may have been deleted, so passes
  /// should take care not to touch the object.
  bool skipCurrentLoop() const { return SkipCurrentLoop; }

  /// Loop passes should use this method to indicate they have deleted a loop
  /// from the nest.
  ///
  /// Note that this loop must either be the current loop or a subloop of the
  /// current loop. This routine must be called prior to removing the loop from
  /// the loop nest.
  ///
  /// If this is called for the current loop, in addition to clearing any
  /// state, this routine will mark that the current loop should be skipped by
  /// the rest of the pass management infrastructure.
  void markLoopAsDeleted(Loop &L, llvm::StringRef Name) {
    LAM.clear(L, Name);
    assert((&L == CurrentL || CurrentL->contains(&L)) &&
           "Cannot delete a loop outside of the "
           "subloop tree currently being processed.");
    if (&L == CurrentL)
      SkipCurrentLoop = true;
  }

  /// Loop passes should use this method to indicate they have added new child
  /// loops of the current loop.
  ///
  /// \p NewChildLoops must contain only the immediate children. Any nested
  /// loops within them will be visited in postorder as usual for the loop pass
  /// manager.
  void addChildLoops(ArrayRef<Loop *> NewChildLoops) {
    // Insert ourselves back into the worklist first, as this loop should be
    // revisited after all the children have been processed.
    Worklist.insert(CurrentL);

#ifndef NDEBUG
    for (Loop *NewL : NewChildLoops)
      assert(NewL->getParentLoop() == CurrentL && "All of the new loops must "
                                                  "be immediate children of "
                                                  "the current loop!");
#endif

    internal::appendLoopsToWorklist(NewChildLoops, Worklist);

    // Also skip further processing of the current loop--it will be revisited
    // after all of its newly added children are accounted for.
    SkipCurrentLoop = true;
  }

  /// Loop passes should use this method to indicate they have added new
  /// sibling loops to the current loop.
  ///
  /// \p NewSibLoops must only contain the immediate sibling loops. Any nested
  /// loops within them will be visited in postorder as usual for the loop pass
  /// manager.
  void addSiblingLoops(ArrayRef<Loop *> NewSibLoops) {
#ifndef NDEBUG
    for (Loop *NewL : NewSibLoops)
      assert(NewL->getParentLoop() == ParentL &&
             "All of the new loops must be siblings of the current loop!");
#endif

    internal::appendLoopsToWorklist(NewSibLoops, Worklist);

    // No need to skip the current loop or revisit it, as sibling loops
    // shouldn't impact anything.
  }

  /// Restart the current loop.
  ///
  /// Loop passes should call this method to indicate the current loop has been
  /// sufficiently changed that it should be re-visited from the begining of
  /// the loop pass pipeline rather than continuing.
  void revisitCurrentLoop() {
    // Tell the currently in-flight pipeline to stop running.
    SkipCurrentLoop = true;

    // And insert ourselves back into the worklist.
    Worklist.insert(CurrentL);
  }

private:
  template <typename LoopPassT> friend class llvm::FunctionToLoopPassAdaptor;

  /// The \c FunctionToLoopPassAdaptor's worklist of loops to process.
  SmallPriorityWorklist<Loop *, 4> &Worklist;

  /// The analysis manager for use in the current loop nest.
  LoopAnalysisManager &LAM;

  Loop *CurrentL;
  bool SkipCurrentLoop;

#ifndef NDEBUG
  // In debug builds we also track the parent loop to implement asserts even in
  // the face of loop deletion.
  Loop *ParentL;
#endif

  LPMUpdater(SmallPriorityWorklist<Loop *, 4> &Worklist,
             LoopAnalysisManager &LAM)
      : Worklist(Worklist), LAM(LAM) {}
};

/// Adaptor that maps from a function to its loops.
///
/// Designed to allow composition of a LoopPass(Manager) and a
/// FunctionPassManager. Note that if this pass is constructed with a \c
/// FunctionAnalysisManager it will run the \c LoopAnalysisManagerFunctionProxy
/// analysis prior to running the loop passes over the function to enable a \c
/// LoopAnalysisManager to be used within this run safely.
template <typename LoopPassT>
class FunctionToLoopPassAdaptor
    : public PassInfoMixin<FunctionToLoopPassAdaptor<LoopPassT>> {
public:
  explicit FunctionToLoopPassAdaptor(LoopPassT Pass, bool DebugLogging = false)
      : Pass(std::move(Pass)), LoopCanonicalizationFPM(DebugLogging) {
    LoopCanonicalizationFPM.addPass(LoopSimplifyPass());
    LoopCanonicalizationFPM.addPass(LCSSAPass());
  }

  /// Runs the loop passes across every loop in the function.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    // Before we even compute any loop analyses, first run a miniature function
    // pass pipeline to put loops into their canonical form. Note that we can
    // directly build up function analyses after this as the function pass
    // manager handles all the invalidation at that layer.
    PassInstrumentation PI = AM.getResult<PassInstrumentationAnalysis>(F);

    PreservedAnalyses PA = PreservedAnalyses::all();
    // Check the PassInstrumentation's BeforePass callbacks before running the
    // canonicalization pipeline.
    if (PI.runBeforePass<Function>(LoopCanonicalizationFPM, F)) {
      PA = LoopCanonicalizationFPM.run(F, AM);
      PI.runAfterPass<Function>(LoopCanonicalizationFPM, F);
    }

    // Get the loop structure for this function
    LoopInfo &LI = AM.getResult<LoopAnalysis>(F);

    // If there are no loops, there is nothing to do here.
    if (LI.empty())
      return PA;

    // Get the analysis results needed by loop passes.
    MemorySSA *MSSA = EnableMSSALoopDependency
                          ? (&AM.getResult<MemorySSAAnalysis>(F).getMSSA())
                          : nullptr;
    LoopStandardAnalysisResults LAR = {AM.getResult<AAManager>(F),
                                       AM.getResult<AssumptionAnalysis>(F),
                                       AM.getResult<DominatorTreeAnalysis>(F),
                                       AM.getResult<LoopAnalysis>(F),
                                       AM.getResult<ScalarEvolutionAnalysis>(F),
                                       AM.getResult<TargetLibraryAnalysis>(F),
                                       AM.getResult<TargetIRAnalysis>(F),
                                       MSSA};

    // Setup the loop analysis manager from its proxy. It is important that
    // this is only done when there are loops to process and we have built the
    // LoopStandardAnalysisResults object. The loop analyses cached in this
    // manager have access to those analysis results and so it must invalidate
    // itself when they go away.
    LoopAnalysisManager &LAM =
        AM.getResult<LoopAnalysisManagerFunctionProxy>(F).getManager();

    // A postorder worklist of loops to process.
    SmallPriorityWorklist<Loop *, 4> Worklist;

    // Register the worklist and loop analysis manager so that loop passes can
    // update them when they mutate the loop nest structure.
    LPMUpdater Updater(Worklist, LAM);

    // Add the loop nests in the reverse order of LoopInfo. For some reason,
    // they are stored in RPO w.r.t. the control flow graph in LoopInfo. For
    // the purpose of unrolling, loop deletion, and LICM, we largely want to
    // work forward across the CFG so that we visit defs before uses and can
    // propagate simplifications from one loop nest into the next.
    // FIXME: Consider changing the order in LoopInfo.
    internal::appendLoopsToWorklist(reverse(LI), Worklist);

    do {
      Loop *L = Worklist.pop_back_val();

      // Reset the update structure for this loop.
      Updater.CurrentL = L;
      Updater.SkipCurrentLoop = false;

#ifndef NDEBUG
      // Save a parent loop pointer for asserts.
      Updater.ParentL = L->getParentLoop();

      // Verify the loop structure and LCSSA form before visiting the loop.
      L->verifyLoop();
      assert(L->isRecursivelyLCSSAForm(LAR.DT, LI) &&
             "Loops must remain in LCSSA form!");
#endif
      // Check the PassInstrumentation's BeforePass callbacks before running the
      // pass, skip its execution completely if asked to (callback returns
      // false).
      if (!PI.runBeforePass<Loop>(Pass, *L))
        continue;
      PreservedAnalyses PassPA = Pass.run(*L, LAM, LAR, Updater);

      // Do not pass deleted Loop into the instrumentation.
      if (Updater.skipCurrentLoop())
        PI.runAfterPassInvalidated<Loop>(Pass);
      else
        PI.runAfterPass<Loop>(Pass, *L);

      // FIXME: We should verify the set of analyses relevant to Loop passes
      // are preserved.

      // If the loop hasn't been deleted, we need to handle invalidation here.
      if (!Updater.skipCurrentLoop())
        // We know that the loop pass couldn't have invalidated any other
        // loop's analyses (that's the contract of a loop pass), so directly
        // handle the loop analysis manager's invalidation here.
        LAM.invalidate(*L, PassPA);

      // Then intersect the preserved set so that invalidation of module
      // analyses will eventually occur when the module pass completes.
      PA.intersect(std::move(PassPA));
    } while (!Worklist.empty());

    // By definition we preserve the proxy. We also preserve all analyses on
    // Loops. This precludes *any* invalidation of loop analyses by the proxy,
    // but that's OK because we've taken care to invalidate analyses in the
    // loop analysis manager incrementally above.
    PA.preserveSet<AllAnalysesOn<Loop>>();
    PA.preserve<LoopAnalysisManagerFunctionProxy>();
    // We also preserve the set of standard analyses.
    PA.preserve<DominatorTreeAnalysis>();
    PA.preserve<LoopAnalysis>();
    PA.preserve<ScalarEvolutionAnalysis>();
    if (EnableMSSALoopDependency)
      PA.preserve<MemorySSAAnalysis>();
    // FIXME: What we really want to do here is preserve an AA category, but
    // that concept doesn't exist yet.
    PA.preserve<AAManager>();
    PA.preserve<BasicAA>();
    PA.preserve<GlobalsAA>();
    PA.preserve<SCEVAA>();
    return PA;
  }

private:
  LoopPassT Pass;

  FunctionPassManager LoopCanonicalizationFPM;
};

/// A function to deduce a loop pass type and wrap it in the templated
/// adaptor.
template <typename LoopPassT>
FunctionToLoopPassAdaptor<LoopPassT>
createFunctionToLoopPassAdaptor(LoopPassT Pass, bool DebugLogging = false) {
  return FunctionToLoopPassAdaptor<LoopPassT>(std::move(Pass), DebugLogging);
}

/// Pass for printing a loop's contents as textual IR.
class PrintLoopPass : public PassInfoMixin<PrintLoopPass> {
  raw_ostream &OS;
  std::string Banner;

public:
  PrintLoopPass();
  PrintLoopPass(raw_ostream &OS, const std::string &Banner = "");

  PreservedAnalyses run(Loop &L, LoopAnalysisManager &,
                        LoopStandardAnalysisResults &, LPMUpdater &);
};
}

#endif // LLVM_TRANSFORMS_SCALAR_LOOPPASSMANAGER_H
