//===- LoopPassManager.cpp - Loop pass management -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Analysis/LoopInfo.h"

using namespace llvm;

// Explicit template instantiations and specialization defininitions for core
// template typedefs.
namespace llvm {
template class PassManager<Loop, LoopAnalysisManager,
                           LoopStandardAnalysisResults &, LPMUpdater &>;

/// Explicitly specialize the pass manager's run method to handle loop nest
/// structure updates.
template <>
PreservedAnalyses
PassManager<Loop, LoopAnalysisManager, LoopStandardAnalysisResults &,
            LPMUpdater &>::run(Loop &L, LoopAnalysisManager &AM,
                               LoopStandardAnalysisResults &AR, LPMUpdater &U) {
  PreservedAnalyses PA = PreservedAnalyses::all();

  if (DebugLogging)
    dbgs() << "Starting Loop pass manager run.\n";

  // Request PassInstrumentation from analysis manager, will use it to run
  // instrumenting callbacks for the passes later.
  PassInstrumentation PI = AM.getResult<PassInstrumentationAnalysis>(L, AR);
  for (auto &Pass : Passes) {
    if (DebugLogging)
      dbgs() << "Running pass: " << Pass->name() << " on " << L;

    // Check the PassInstrumentation's BeforePass callbacks before running the
    // pass, skip its execution completely if asked to (callback returns false).
    if (!PI.runBeforePass<Loop>(*Pass, L))
      continue;

    PreservedAnalyses PassPA = Pass->run(L, AM, AR, U);

    // do not pass deleted Loop into the instrumentation
    if (U.skipCurrentLoop())
      PI.runAfterPassInvalidated<Loop>(*Pass);
    else
      PI.runAfterPass<Loop>(*Pass, L);

    // If the loop was deleted, abort the run and return to the outer walk.
    if (U.skipCurrentLoop()) {
      PA.intersect(std::move(PassPA));
      break;
    }

#ifndef NDEBUG
    // Verify the loop structure and LCSSA form before visiting the loop.
    L.verifyLoop();
    assert(L.isRecursivelyLCSSAForm(AR.DT, AR.LI) &&
           "Loops must remain in LCSSA form!");
#endif

    // Update the analysis manager as each pass runs and potentially
    // invalidates analyses.
    AM.invalidate(L, PassPA);

    // Finally, we intersect the final preserved analyses to compute the
    // aggregate preserved set for this pass manager.
    PA.intersect(std::move(PassPA));

    // FIXME: Historically, the pass managers all called the LLVM context's
    // yield function here. We don't have a generic way to acquire the
    // context and it isn't yet clear what the right pattern is for yielding
    // in the new pass manager so it is currently omitted.
    // ...getContext().yield();
  }

  // Invalidation for the current loop should be handled above, and other loop
  // analysis results shouldn't be impacted by runs over this loop. Therefore,
  // the remaining analysis results in the AnalysisManager are preserved. We
  // mark this with a set so that we don't need to inspect each one
  // individually.
  // FIXME: This isn't correct! This loop and all nested loops' analyses should
  // be preserved, but unrolling should invalidate the parent loop's analyses.
  PA.preserveSet<AllAnalysesOn<Loop>>();

  if (DebugLogging)
    dbgs() << "Finished Loop pass manager run.\n";

  return PA;
}
}

PrintLoopPass::PrintLoopPass() : OS(dbgs()) {}
PrintLoopPass::PrintLoopPass(raw_ostream &OS, const std::string &Banner)
    : OS(OS), Banner(Banner) {}

PreservedAnalyses PrintLoopPass::run(Loop &L, LoopAnalysisManager &,
                                     LoopStandardAnalysisResults &,
                                     LPMUpdater &) {
  printLoop(L, OS, Banner);
  return PreservedAnalyses::all();
}
