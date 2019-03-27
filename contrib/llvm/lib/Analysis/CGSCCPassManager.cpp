//===- CGSCCPassManager.cpp - Managing & running CGSCC passes -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <iterator>

#define DEBUG_TYPE "cgscc"

using namespace llvm;

// Explicit template instantiations and specialization definitions for core
// template typedefs.
namespace llvm {

// Explicit instantiations for the core proxy templates.
template class AllAnalysesOn<LazyCallGraph::SCC>;
template class AnalysisManager<LazyCallGraph::SCC, LazyCallGraph &>;
template class PassManager<LazyCallGraph::SCC, CGSCCAnalysisManager,
                           LazyCallGraph &, CGSCCUpdateResult &>;
template class InnerAnalysisManagerProxy<CGSCCAnalysisManager, Module>;
template class OuterAnalysisManagerProxy<ModuleAnalysisManager,
                                         LazyCallGraph::SCC, LazyCallGraph &>;
template class OuterAnalysisManagerProxy<CGSCCAnalysisManager, Function>;

/// Explicitly specialize the pass manager run method to handle call graph
/// updates.
template <>
PreservedAnalyses
PassManager<LazyCallGraph::SCC, CGSCCAnalysisManager, LazyCallGraph &,
            CGSCCUpdateResult &>::run(LazyCallGraph::SCC &InitialC,
                                      CGSCCAnalysisManager &AM,
                                      LazyCallGraph &G, CGSCCUpdateResult &UR) {
  // Request PassInstrumentation from analysis manager, will use it to run
  // instrumenting callbacks for the passes later.
  PassInstrumentation PI =
      AM.getResult<PassInstrumentationAnalysis>(InitialC, G);

  PreservedAnalyses PA = PreservedAnalyses::all();

  if (DebugLogging)
    dbgs() << "Starting CGSCC pass manager run.\n";

  // The SCC may be refined while we are running passes over it, so set up
  // a pointer that we can update.
  LazyCallGraph::SCC *C = &InitialC;

  for (auto &Pass : Passes) {
    if (DebugLogging)
      dbgs() << "Running pass: " << Pass->name() << " on " << *C << "\n";

    // Check the PassInstrumentation's BeforePass callbacks before running the
    // pass, skip its execution completely if asked to (callback returns false).
    if (!PI.runBeforePass(*Pass, *C))
      continue;

    PreservedAnalyses PassPA = Pass->run(*C, AM, G, UR);

    if (UR.InvalidatedSCCs.count(C))
      PI.runAfterPassInvalidated<LazyCallGraph::SCC>(*Pass);
    else
      PI.runAfterPass<LazyCallGraph::SCC>(*Pass, *C);

    // Update the SCC if necessary.
    C = UR.UpdatedC ? UR.UpdatedC : C;

    // If the CGSCC pass wasn't able to provide a valid updated SCC, the
    // current SCC may simply need to be skipped if invalid.
    if (UR.InvalidatedSCCs.count(C)) {
      LLVM_DEBUG(dbgs() << "Skipping invalidated root or island SCC!\n");
      break;
    }
    // Check that we didn't miss any update scenario.
    assert(C->begin() != C->end() && "Cannot have an empty SCC!");

    // Update the analysis manager as each pass runs and potentially
    // invalidates analyses.
    AM.invalidate(*C, PassPA);

    // Finally, we intersect the final preserved analyses to compute the
    // aggregate preserved set for this pass manager.
    PA.intersect(std::move(PassPA));

    // FIXME: Historically, the pass managers all called the LLVM context's
    // yield function here. We don't have a generic way to acquire the
    // context and it isn't yet clear what the right pattern is for yielding
    // in the new pass manager so it is currently omitted.
    // ...getContext().yield();
  }

  // Invalidation was handled after each pass in the above loop for the current
  // SCC. Therefore, the remaining analysis results in the AnalysisManager are
  // preserved. We mark this with a set so that we don't need to inspect each
  // one individually.
  PA.preserveSet<AllAnalysesOn<LazyCallGraph::SCC>>();

  if (DebugLogging)
    dbgs() << "Finished CGSCC pass manager run.\n";

  return PA;
}

bool CGSCCAnalysisManagerModuleProxy::Result::invalidate(
    Module &M, const PreservedAnalyses &PA,
    ModuleAnalysisManager::Invalidator &Inv) {
  // If literally everything is preserved, we're done.
  if (PA.areAllPreserved())
    return false; // This is still a valid proxy.

  // If this proxy or the call graph is going to be invalidated, we also need
  // to clear all the keys coming from that analysis.
  //
  // We also directly invalidate the FAM's module proxy if necessary, and if
  // that proxy isn't preserved we can't preserve this proxy either. We rely on
  // it to handle module -> function analysis invalidation in the face of
  // structural changes and so if it's unavailable we conservatively clear the
  // entire SCC layer as well rather than trying to do invalidation ourselves.
  auto PAC = PA.getChecker<CGSCCAnalysisManagerModuleProxy>();
  if (!(PAC.preserved() || PAC.preservedSet<AllAnalysesOn<Module>>()) ||
      Inv.invalidate<LazyCallGraphAnalysis>(M, PA) ||
      Inv.invalidate<FunctionAnalysisManagerModuleProxy>(M, PA)) {
    InnerAM->clear();

    // And the proxy itself should be marked as invalid so that we can observe
    // the new call graph. This isn't strictly necessary because we cheat
    // above, but is still useful.
    return true;
  }

  // Directly check if the relevant set is preserved so we can short circuit
  // invalidating SCCs below.
  bool AreSCCAnalysesPreserved =
      PA.allAnalysesInSetPreserved<AllAnalysesOn<LazyCallGraph::SCC>>();

  // Ok, we have a graph, so we can propagate the invalidation down into it.
  G->buildRefSCCs();
  for (auto &RC : G->postorder_ref_sccs())
    for (auto &C : RC) {
      Optional<PreservedAnalyses> InnerPA;

      // Check to see whether the preserved set needs to be adjusted based on
      // module-level analysis invalidation triggering deferred invalidation
      // for this SCC.
      if (auto *OuterProxy =
              InnerAM->getCachedResult<ModuleAnalysisManagerCGSCCProxy>(C))
        for (const auto &OuterInvalidationPair :
             OuterProxy->getOuterInvalidations()) {
          AnalysisKey *OuterAnalysisID = OuterInvalidationPair.first;
          const auto &InnerAnalysisIDs = OuterInvalidationPair.second;
          if (Inv.invalidate(OuterAnalysisID, M, PA)) {
            if (!InnerPA)
              InnerPA = PA;
            for (AnalysisKey *InnerAnalysisID : InnerAnalysisIDs)
              InnerPA->abandon(InnerAnalysisID);
          }
        }

      // Check if we needed a custom PA set. If so we'll need to run the inner
      // invalidation.
      if (InnerPA) {
        InnerAM->invalidate(C, *InnerPA);
        continue;
      }

      // Otherwise we only need to do invalidation if the original PA set didn't
      // preserve all SCC analyses.
      if (!AreSCCAnalysesPreserved)
        InnerAM->invalidate(C, PA);
    }

  // Return false to indicate that this result is still a valid proxy.
  return false;
}

template <>
CGSCCAnalysisManagerModuleProxy::Result
CGSCCAnalysisManagerModuleProxy::run(Module &M, ModuleAnalysisManager &AM) {
  // Force the Function analysis manager to also be available so that it can
  // be accessed in an SCC analysis and proxied onward to function passes.
  // FIXME: It is pretty awkward to just drop the result here and assert that
  // we can find it again later.
  (void)AM.getResult<FunctionAnalysisManagerModuleProxy>(M);

  return Result(*InnerAM, AM.getResult<LazyCallGraphAnalysis>(M));
}

AnalysisKey FunctionAnalysisManagerCGSCCProxy::Key;

FunctionAnalysisManagerCGSCCProxy::Result
FunctionAnalysisManagerCGSCCProxy::run(LazyCallGraph::SCC &C,
                                       CGSCCAnalysisManager &AM,
                                       LazyCallGraph &CG) {
  // Collect the FunctionAnalysisManager from the Module layer and use that to
  // build the proxy result.
  //
  // This allows us to rely on the FunctionAnalysisMangaerModuleProxy to
  // invalidate the function analyses.
  auto &MAM = AM.getResult<ModuleAnalysisManagerCGSCCProxy>(C, CG).getManager();
  Module &M = *C.begin()->getFunction().getParent();
  auto *FAMProxy = MAM.getCachedResult<FunctionAnalysisManagerModuleProxy>(M);
  assert(FAMProxy && "The CGSCC pass manager requires that the FAM module "
                     "proxy is run on the module prior to entering the CGSCC "
                     "walk.");

  // Note that we special-case invalidation handling of this proxy in the CGSCC
  // analysis manager's Module proxy. This avoids the need to do anything
  // special here to recompute all of this if ever the FAM's module proxy goes
  // away.
  return Result(FAMProxy->getManager());
}

bool FunctionAnalysisManagerCGSCCProxy::Result::invalidate(
    LazyCallGraph::SCC &C, const PreservedAnalyses &PA,
    CGSCCAnalysisManager::Invalidator &Inv) {
  // If literally everything is preserved, we're done.
  if (PA.areAllPreserved())
    return false; // This is still a valid proxy.

  // If this proxy isn't marked as preserved, then even if the result remains
  // valid, the key itself may no longer be valid, so we clear everything.
  //
  // Note that in order to preserve this proxy, a module pass must ensure that
  // the FAM has been completely updated to handle the deletion of functions.
  // Specifically, any FAM-cached results for those functions need to have been
  // forcibly cleared. When preserved, this proxy will only invalidate results
  // cached on functions *still in the module* at the end of the module pass.
  auto PAC = PA.getChecker<FunctionAnalysisManagerCGSCCProxy>();
  if (!PAC.preserved() && !PAC.preservedSet<AllAnalysesOn<LazyCallGraph::SCC>>()) {
    for (LazyCallGraph::Node &N : C)
      FAM->clear(N.getFunction(), N.getFunction().getName());

    return true;
  }

  // Directly check if the relevant set is preserved.
  bool AreFunctionAnalysesPreserved =
      PA.allAnalysesInSetPreserved<AllAnalysesOn<Function>>();

  // Now walk all the functions to see if any inner analysis invalidation is
  // necessary.
  for (LazyCallGraph::Node &N : C) {
    Function &F = N.getFunction();
    Optional<PreservedAnalyses> FunctionPA;

    // Check to see whether the preserved set needs to be pruned based on
    // SCC-level analysis invalidation that triggers deferred invalidation
    // registered with the outer analysis manager proxy for this function.
    if (auto *OuterProxy =
            FAM->getCachedResult<CGSCCAnalysisManagerFunctionProxy>(F))
      for (const auto &OuterInvalidationPair :
           OuterProxy->getOuterInvalidations()) {
        AnalysisKey *OuterAnalysisID = OuterInvalidationPair.first;
        const auto &InnerAnalysisIDs = OuterInvalidationPair.second;
        if (Inv.invalidate(OuterAnalysisID, C, PA)) {
          if (!FunctionPA)
            FunctionPA = PA;
          for (AnalysisKey *InnerAnalysisID : InnerAnalysisIDs)
            FunctionPA->abandon(InnerAnalysisID);
        }
      }

    // Check if we needed a custom PA set, and if so we'll need to run the
    // inner invalidation.
    if (FunctionPA) {
      FAM->invalidate(F, *FunctionPA);
      continue;
    }

    // Otherwise we only need to do invalidation if the original PA set didn't
    // preserve all function analyses.
    if (!AreFunctionAnalysesPreserved)
      FAM->invalidate(F, PA);
  }

  // Return false to indicate that this result is still a valid proxy.
  return false;
}

} // end namespace llvm

/// When a new SCC is created for the graph and there might be function
/// analysis results cached for the functions now in that SCC two forms of
/// updates are required.
///
/// First, a proxy from the SCC to the FunctionAnalysisManager needs to be
/// created so that any subsequent invalidation events to the SCC are
/// propagated to the function analysis results cached for functions within it.
///
/// Second, if any of the functions within the SCC have analysis results with
/// outer analysis dependencies, then those dependencies would point to the
/// *wrong* SCC's analysis result. We forcibly invalidate the necessary
/// function analyses so that they don't retain stale handles.
static void updateNewSCCFunctionAnalyses(LazyCallGraph::SCC &C,
                                         LazyCallGraph &G,
                                         CGSCCAnalysisManager &AM) {
  // Get the relevant function analysis manager.
  auto &FAM =
      AM.getResult<FunctionAnalysisManagerCGSCCProxy>(C, G).getManager();

  // Now walk the functions in this SCC and invalidate any function analysis
  // results that might have outer dependencies on an SCC analysis.
  for (LazyCallGraph::Node &N : C) {
    Function &F = N.getFunction();

    auto *OuterProxy =
        FAM.getCachedResult<CGSCCAnalysisManagerFunctionProxy>(F);
    if (!OuterProxy)
      // No outer analyses were queried, nothing to do.
      continue;

    // Forcibly abandon all the inner analyses with dependencies, but
    // invalidate nothing else.
    auto PA = PreservedAnalyses::all();
    for (const auto &OuterInvalidationPair :
         OuterProxy->getOuterInvalidations()) {
      const auto &InnerAnalysisIDs = OuterInvalidationPair.second;
      for (AnalysisKey *InnerAnalysisID : InnerAnalysisIDs)
        PA.abandon(InnerAnalysisID);
    }

    // Now invalidate anything we found.
    FAM.invalidate(F, PA);
  }
}

/// Helper function to update both the \c CGSCCAnalysisManager \p AM and the \c
/// CGSCCPassManager's \c CGSCCUpdateResult \p UR based on a range of newly
/// added SCCs.
///
/// The range of new SCCs must be in postorder already. The SCC they were split
/// out of must be provided as \p C. The current node being mutated and
/// triggering updates must be passed as \p N.
///
/// This function returns the SCC containing \p N. This will be either \p C if
/// no new SCCs have been split out, or it will be the new SCC containing \p N.
template <typename SCCRangeT>
static LazyCallGraph::SCC *
incorporateNewSCCRange(const SCCRangeT &NewSCCRange, LazyCallGraph &G,
                       LazyCallGraph::Node &N, LazyCallGraph::SCC *C,
                       CGSCCAnalysisManager &AM, CGSCCUpdateResult &UR) {
  using SCC = LazyCallGraph::SCC;

  if (NewSCCRange.begin() == NewSCCRange.end())
    return C;

  // Add the current SCC to the worklist as its shape has changed.
  UR.CWorklist.insert(C);
  LLVM_DEBUG(dbgs() << "Enqueuing the existing SCC in the worklist:" << *C
                    << "\n");

  SCC *OldC = C;

  // Update the current SCC. Note that if we have new SCCs, this must actually
  // change the SCC.
  assert(C != &*NewSCCRange.begin() &&
         "Cannot insert new SCCs without changing current SCC!");
  C = &*NewSCCRange.begin();
  assert(G.lookupSCC(N) == C && "Failed to update current SCC!");

  // If we had a cached FAM proxy originally, we will want to create more of
  // them for each SCC that was split off.
  bool NeedFAMProxy =
      AM.getCachedResult<FunctionAnalysisManagerCGSCCProxy>(*OldC) != nullptr;

  // We need to propagate an invalidation call to all but the newly current SCC
  // because the outer pass manager won't do that for us after splitting them.
  // FIXME: We should accept a PreservedAnalysis from the CG updater so that if
  // there are preserved analysis we can avoid invalidating them here for
  // split-off SCCs.
  // We know however that this will preserve any FAM proxy so go ahead and mark
  // that.
  PreservedAnalyses PA;
  PA.preserve<FunctionAnalysisManagerCGSCCProxy>();
  AM.invalidate(*OldC, PA);

  // Ensure the now-current SCC's function analyses are updated.
  if (NeedFAMProxy)
    updateNewSCCFunctionAnalyses(*C, G, AM);

  for (SCC &NewC : llvm::reverse(make_range(std::next(NewSCCRange.begin()),
                                            NewSCCRange.end()))) {
    assert(C != &NewC && "No need to re-visit the current SCC!");
    assert(OldC != &NewC && "Already handled the original SCC!");
    UR.CWorklist.insert(&NewC);
    LLVM_DEBUG(dbgs() << "Enqueuing a newly formed SCC:" << NewC << "\n");

    // Ensure new SCCs' function analyses are updated.
    if (NeedFAMProxy)
      updateNewSCCFunctionAnalyses(NewC, G, AM);

    // Also propagate a normal invalidation to the new SCC as only the current
    // will get one from the pass manager infrastructure.
    AM.invalidate(NewC, PA);
  }
  return C;
}

LazyCallGraph::SCC &llvm::updateCGAndAnalysisManagerForFunctionPass(
    LazyCallGraph &G, LazyCallGraph::SCC &InitialC, LazyCallGraph::Node &N,
    CGSCCAnalysisManager &AM, CGSCCUpdateResult &UR) {
  using Node = LazyCallGraph::Node;
  using Edge = LazyCallGraph::Edge;
  using SCC = LazyCallGraph::SCC;
  using RefSCC = LazyCallGraph::RefSCC;

  RefSCC &InitialRC = InitialC.getOuterRefSCC();
  SCC *C = &InitialC;
  RefSCC *RC = &InitialRC;
  Function &F = N.getFunction();

  // Walk the function body and build up the set of retained, promoted, and
  // demoted edges.
  SmallVector<Constant *, 16> Worklist;
  SmallPtrSet<Constant *, 16> Visited;
  SmallPtrSet<Node *, 16> RetainedEdges;
  SmallSetVector<Node *, 4> PromotedRefTargets;
  SmallSetVector<Node *, 4> DemotedCallTargets;

  // First walk the function and handle all called functions. We do this first
  // because if there is a single call edge, whether there are ref edges is
  // irrelevant.
  for (Instruction &I : instructions(F))
    if (auto CS = CallSite(&I))
      if (Function *Callee = CS.getCalledFunction())
        if (Visited.insert(Callee).second && !Callee->isDeclaration()) {
          Node &CalleeN = *G.lookup(*Callee);
          Edge *E = N->lookup(CalleeN);
          // FIXME: We should really handle adding new calls. While it will
          // make downstream usage more complex, there is no fundamental
          // limitation and it will allow passes within the CGSCC to be a bit
          // more flexible in what transforms they can do. Until then, we
          // verify that new calls haven't been introduced.
          assert(E && "No function transformations should introduce *new* "
                      "call edges! Any new calls should be modeled as "
                      "promoted existing ref edges!");
          bool Inserted = RetainedEdges.insert(&CalleeN).second;
          (void)Inserted;
          assert(Inserted && "We should never visit a function twice.");
          if (!E->isCall())
            PromotedRefTargets.insert(&CalleeN);
        }

  // Now walk all references.
  for (Instruction &I : instructions(F))
    for (Value *Op : I.operand_values())
      if (auto *C = dyn_cast<Constant>(Op))
        if (Visited.insert(C).second)
          Worklist.push_back(C);

  auto VisitRef = [&](Function &Referee) {
    Node &RefereeN = *G.lookup(Referee);
    Edge *E = N->lookup(RefereeN);
    // FIXME: Similarly to new calls, we also currently preclude
    // introducing new references. See above for details.
    assert(E && "No function transformations should introduce *new* ref "
                "edges! Any new ref edges would require IPO which "
                "function passes aren't allowed to do!");
    bool Inserted = RetainedEdges.insert(&RefereeN).second;
    (void)Inserted;
    assert(Inserted && "We should never visit a function twice.");
    if (E->isCall())
      DemotedCallTargets.insert(&RefereeN);
  };
  LazyCallGraph::visitReferences(Worklist, Visited, VisitRef);

  // Include synthetic reference edges to known, defined lib functions.
  for (auto *F : G.getLibFunctions())
    // While the list of lib functions doesn't have repeats, don't re-visit
    // anything handled above.
    if (!Visited.count(F))
      VisitRef(*F);

  // First remove all of the edges that are no longer present in this function.
  // The first step makes these edges uniformly ref edges and accumulates them
  // into a separate data structure so removal doesn't invalidate anything.
  SmallVector<Node *, 4> DeadTargets;
  for (Edge &E : *N) {
    if (RetainedEdges.count(&E.getNode()))
      continue;

    SCC &TargetC = *G.lookupSCC(E.getNode());
    RefSCC &TargetRC = TargetC.getOuterRefSCC();
    if (&TargetRC == RC && E.isCall()) {
      if (C != &TargetC) {
        // For separate SCCs this is trivial.
        RC->switchTrivialInternalEdgeToRef(N, E.getNode());
      } else {
        // Now update the call graph.
        C = incorporateNewSCCRange(RC->switchInternalEdgeToRef(N, E.getNode()),
                                   G, N, C, AM, UR);
      }
    }

    // Now that this is ready for actual removal, put it into our list.
    DeadTargets.push_back(&E.getNode());
  }
  // Remove the easy cases quickly and actually pull them out of our list.
  DeadTargets.erase(
      llvm::remove_if(DeadTargets,
                      [&](Node *TargetN) {
                        SCC &TargetC = *G.lookupSCC(*TargetN);
                        RefSCC &TargetRC = TargetC.getOuterRefSCC();

                        // We can't trivially remove internal targets, so skip
                        // those.
                        if (&TargetRC == RC)
                          return false;

                        RC->removeOutgoingEdge(N, *TargetN);
                        LLVM_DEBUG(dbgs() << "Deleting outgoing edge from '"
                                          << N << "' to '" << TargetN << "'\n");
                        return true;
                      }),
      DeadTargets.end());

  // Now do a batch removal of the internal ref edges left.
  auto NewRefSCCs = RC->removeInternalRefEdge(N, DeadTargets);
  if (!NewRefSCCs.empty()) {
    // The old RefSCC is dead, mark it as such.
    UR.InvalidatedRefSCCs.insert(RC);

    // Note that we don't bother to invalidate analyses as ref-edge
    // connectivity is not really observable in any way and is intended
    // exclusively to be used for ordering of transforms rather than for
    // analysis conclusions.

    // Update RC to the "bottom".
    assert(G.lookupSCC(N) == C && "Changed the SCC when splitting RefSCCs!");
    RC = &C->getOuterRefSCC();
    assert(G.lookupRefSCC(N) == RC && "Failed to update current RefSCC!");

    // The RC worklist is in reverse postorder, so we enqueue the new ones in
    // RPO except for the one which contains the source node as that is the
    // "bottom" we will continue processing in the bottom-up walk.
    assert(NewRefSCCs.front() == RC &&
           "New current RefSCC not first in the returned list!");
    for (RefSCC *NewRC : llvm::reverse(make_range(std::next(NewRefSCCs.begin()),
                                                  NewRefSCCs.end()))) {
      assert(NewRC != RC && "Should not encounter the current RefSCC further "
                            "in the postorder list of new RefSCCs.");
      UR.RCWorklist.insert(NewRC);
      LLVM_DEBUG(dbgs() << "Enqueuing a new RefSCC in the update worklist: "
                        << *NewRC << "\n");
    }
  }

  // Next demote all the call edges that are now ref edges. This helps make
  // the SCCs small which should minimize the work below as we don't want to
  // form cycles that this would break.
  for (Node *RefTarget : DemotedCallTargets) {
    SCC &TargetC = *G.lookupSCC(*RefTarget);
    RefSCC &TargetRC = TargetC.getOuterRefSCC();

    // The easy case is when the target RefSCC is not this RefSCC. This is
    // only supported when the target RefSCC is a child of this RefSCC.
    if (&TargetRC != RC) {
      assert(RC->isAncestorOf(TargetRC) &&
             "Cannot potentially form RefSCC cycles here!");
      RC->switchOutgoingEdgeToRef(N, *RefTarget);
      LLVM_DEBUG(dbgs() << "Switch outgoing call edge to a ref edge from '" << N
                        << "' to '" << *RefTarget << "'\n");
      continue;
    }

    // We are switching an internal call edge to a ref edge. This may split up
    // some SCCs.
    if (C != &TargetC) {
      // For separate SCCs this is trivial.
      RC->switchTrivialInternalEdgeToRef(N, *RefTarget);
      continue;
    }

    // Now update the call graph.
    C = incorporateNewSCCRange(RC->switchInternalEdgeToRef(N, *RefTarget), G, N,
                               C, AM, UR);
  }

  // Now promote ref edges into call edges.
  for (Node *CallTarget : PromotedRefTargets) {
    SCC &TargetC = *G.lookupSCC(*CallTarget);
    RefSCC &TargetRC = TargetC.getOuterRefSCC();

    // The easy case is when the target RefSCC is not this RefSCC. This is
    // only supported when the target RefSCC is a child of this RefSCC.
    if (&TargetRC != RC) {
      assert(RC->isAncestorOf(TargetRC) &&
             "Cannot potentially form RefSCC cycles here!");
      RC->switchOutgoingEdgeToCall(N, *CallTarget);
      LLVM_DEBUG(dbgs() << "Switch outgoing ref edge to a call edge from '" << N
                        << "' to '" << *CallTarget << "'\n");
      continue;
    }
    LLVM_DEBUG(dbgs() << "Switch an internal ref edge to a call edge from '"
                      << N << "' to '" << *CallTarget << "'\n");

    // Otherwise we are switching an internal ref edge to a call edge. This
    // may merge away some SCCs, and we add those to the UpdateResult. We also
    // need to make sure to update the worklist in the event SCCs have moved
    // before the current one in the post-order sequence
    bool HasFunctionAnalysisProxy = false;
    auto InitialSCCIndex = RC->find(*C) - RC->begin();
    bool FormedCycle = RC->switchInternalEdgeToCall(
        N, *CallTarget, [&](ArrayRef<SCC *> MergedSCCs) {
          for (SCC *MergedC : MergedSCCs) {
            assert(MergedC != &TargetC && "Cannot merge away the target SCC!");

            HasFunctionAnalysisProxy |=
                AM.getCachedResult<FunctionAnalysisManagerCGSCCProxy>(
                    *MergedC) != nullptr;

            // Mark that this SCC will no longer be valid.
            UR.InvalidatedSCCs.insert(MergedC);

            // FIXME: We should really do a 'clear' here to forcibly release
            // memory, but we don't have a good way of doing that and
            // preserving the function analyses.
            auto PA = PreservedAnalyses::allInSet<AllAnalysesOn<Function>>();
            PA.preserve<FunctionAnalysisManagerCGSCCProxy>();
            AM.invalidate(*MergedC, PA);
          }
        });

    // If we formed a cycle by creating this call, we need to update more data
    // structures.
    if (FormedCycle) {
      C = &TargetC;
      assert(G.lookupSCC(N) == C && "Failed to update current SCC!");

      // If one of the invalidated SCCs had a cached proxy to a function
      // analysis manager, we need to create a proxy in the new current SCC as
      // the invalidated SCCs had their functions moved.
      if (HasFunctionAnalysisProxy)
        AM.getResult<FunctionAnalysisManagerCGSCCProxy>(*C, G);

      // Any analyses cached for this SCC are no longer precise as the shape
      // has changed by introducing this cycle. However, we have taken care to
      // update the proxies so it remains valide.
      auto PA = PreservedAnalyses::allInSet<AllAnalysesOn<Function>>();
      PA.preserve<FunctionAnalysisManagerCGSCCProxy>();
      AM.invalidate(*C, PA);
    }
    auto NewSCCIndex = RC->find(*C) - RC->begin();
    // If we have actually moved an SCC to be topologically "below" the current
    // one due to merging, we will need to revisit the current SCC after
    // visiting those moved SCCs.
    //
    // It is critical that we *do not* revisit the current SCC unless we
    // actually move SCCs in the process of merging because otherwise we may
    // form a cycle where an SCC is split apart, merged, split, merged and so
    // on infinitely.
    if (InitialSCCIndex < NewSCCIndex) {
      // Put our current SCC back onto the worklist as we'll visit other SCCs
      // that are now definitively ordered prior to the current one in the
      // post-order sequence, and may end up observing more precise context to
      // optimize the current SCC.
      UR.CWorklist.insert(C);
      LLVM_DEBUG(dbgs() << "Enqueuing the existing SCC in the worklist: " << *C
                        << "\n");
      // Enqueue in reverse order as we pop off the back of the worklist.
      for (SCC &MovedC : llvm::reverse(make_range(RC->begin() + InitialSCCIndex,
                                                  RC->begin() + NewSCCIndex))) {
        UR.CWorklist.insert(&MovedC);
        LLVM_DEBUG(dbgs() << "Enqueuing a newly earlier in post-order SCC: "
                          << MovedC << "\n");
      }
    }
  }

  assert(!UR.InvalidatedSCCs.count(C) && "Invalidated the current SCC!");
  assert(!UR.InvalidatedRefSCCs.count(RC) && "Invalidated the current RefSCC!");
  assert(&C->getOuterRefSCC() == RC && "Current SCC not in current RefSCC!");

  // Record the current RefSCC and SCC for higher layers of the CGSCC pass
  // manager now that all the updates have been applied.
  if (RC != &InitialRC)
    UR.UpdatedRC = RC;
  if (C != &InitialC)
    UR.UpdatedC = C;

  return *C;
}
