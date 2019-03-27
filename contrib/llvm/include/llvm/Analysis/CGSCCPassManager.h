//===- CGSCCPassManager.h - Call graph pass management ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This header provides classes for managing passes over SCCs of the call
/// graph. These passes form an important component of LLVM's interprocedural
/// optimizations. Because they operate on the SCCs of the call graph, and they
/// traverse the graph in post-order, they can effectively do pair-wise
/// interprocedural optimizations for all call edges in the program while
/// incrementally refining it and improving the context of these pair-wise
/// optimizations. At each call site edge, the callee has already been
/// optimized as much as is possible. This in turn allows very accurate
/// analysis of it for IPO.
///
/// A secondary more general goal is to be able to isolate optimization on
/// unrelated parts of the IR module. This is useful to ensure our
/// optimizations are principled and don't miss oportunities where refinement
/// of one part of the module influence transformations in another part of the
/// module. But this is also useful if we want to parallelize the optimizations
/// across common large module graph shapes which tend to be very wide and have
/// large regions of unrelated cliques.
///
/// To satisfy these goals, we use the LazyCallGraph which provides two graphs
/// nested inside each other (and built lazily from the bottom-up): the call
/// graph proper, and a reference graph. The reference graph is super set of
/// the call graph and is a conservative approximation of what could through
/// scalar or CGSCC transforms *become* the call graph. Using this allows us to
/// ensure we optimize functions prior to them being introduced into the call
/// graph by devirtualization or other technique, and thus ensures that
/// subsequent pair-wise interprocedural optimizations observe the optimized
/// form of these functions. The (potentially transitive) reference
/// reachability used by the reference graph is a conservative approximation
/// that still allows us to have independent regions of the graph.
///
/// FIXME: There is one major drawback of the reference graph: in its naive
/// form it is quadratic because it contains a distinct edge for each
/// (potentially indirect) reference, even if are all through some common
/// global table of function pointers. This can be fixed in a number of ways
/// that essentially preserve enough of the normalization. While it isn't
/// expected to completely preclude the usability of this, it will need to be
/// addressed.
///
///
/// All of these issues are made substantially more complex in the face of
/// mutations to the call graph while optimization passes are being run. When
/// mutations to the call graph occur we want to achieve two different things:
///
/// - We need to update the call graph in-flight and invalidate analyses
///   cached on entities in the graph. Because of the cache-based analysis
///   design of the pass manager, it is essential to have stable identities for
///   the elements of the IR that passes traverse, and to invalidate any
///   analyses cached on these elements as the mutations take place.
///
/// - We want to preserve the incremental and post-order traversal of the
///   graph even as it is refined and mutated. This means we want optimization
///   to observe the most refined form of the call graph and to do so in
///   post-order.
///
/// To address this, the CGSCC manager uses both worklists that can be expanded
/// by passes which transform the IR, and provides invalidation tests to skip
/// entries that become dead. This extra data is provided to every SCC pass so
/// that it can carefully update the manager's traversal as the call graph
/// mutates.
///
/// We also provide support for running function passes within the CGSCC walk,
/// and there we provide automatic update of the call graph including of the
/// pass manager to reflect call graph changes that fall out naturally as part
/// of scalar transformations.
///
/// The patterns used to ensure the goals of post-order visitation of the fully
/// refined graph:
///
/// 1) Sink toward the "bottom" as the graph is refined. This means that any
///    iteration continues in some valid post-order sequence after the mutation
///    has altered the structure.
///
/// 2) Enqueue in post-order, including the current entity. If the current
///    entity's shape changes, it and everything after it in post-order needs
///    to be visited to observe that shape.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CGSCCPASSMANAGER_H
#define LLVM_ANALYSIS_CGSCCPASSMANAGER_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/PriorityWorklist.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <utility>

namespace llvm {

struct CGSCCUpdateResult;
class Module;

// Allow debug logging in this inline function.
#define DEBUG_TYPE "cgscc"

/// Extern template declaration for the analysis set for this IR unit.
extern template class AllAnalysesOn<LazyCallGraph::SCC>;

extern template class AnalysisManager<LazyCallGraph::SCC, LazyCallGraph &>;

/// The CGSCC analysis manager.
///
/// See the documentation for the AnalysisManager template for detail
/// documentation. This type serves as a convenient way to refer to this
/// construct in the adaptors and proxies used to integrate this into the larger
/// pass manager infrastructure.
using CGSCCAnalysisManager =
    AnalysisManager<LazyCallGraph::SCC, LazyCallGraph &>;

// Explicit specialization and instantiation declarations for the pass manager.
// See the comments on the definition of the specialization for details on how
// it differs from the primary template.
template <>
PreservedAnalyses
PassManager<LazyCallGraph::SCC, CGSCCAnalysisManager, LazyCallGraph &,
            CGSCCUpdateResult &>::run(LazyCallGraph::SCC &InitialC,
                                      CGSCCAnalysisManager &AM,
                                      LazyCallGraph &G, CGSCCUpdateResult &UR);
extern template class PassManager<LazyCallGraph::SCC, CGSCCAnalysisManager,
                                  LazyCallGraph &, CGSCCUpdateResult &>;

/// The CGSCC pass manager.
///
/// See the documentation for the PassManager template for details. It runs
/// a sequence of SCC passes over each SCC that the manager is run over. This
/// type serves as a convenient way to refer to this construct.
using CGSCCPassManager =
    PassManager<LazyCallGraph::SCC, CGSCCAnalysisManager, LazyCallGraph &,
                CGSCCUpdateResult &>;

/// An explicit specialization of the require analysis template pass.
template <typename AnalysisT>
struct RequireAnalysisPass<AnalysisT, LazyCallGraph::SCC, CGSCCAnalysisManager,
                           LazyCallGraph &, CGSCCUpdateResult &>
    : PassInfoMixin<RequireAnalysisPass<AnalysisT, LazyCallGraph::SCC,
                                        CGSCCAnalysisManager, LazyCallGraph &,
                                        CGSCCUpdateResult &>> {
  PreservedAnalyses run(LazyCallGraph::SCC &C, CGSCCAnalysisManager &AM,
                        LazyCallGraph &CG, CGSCCUpdateResult &) {
    (void)AM.template getResult<AnalysisT>(C, CG);
    return PreservedAnalyses::all();
  }
};

/// A proxy from a \c CGSCCAnalysisManager to a \c Module.
using CGSCCAnalysisManagerModuleProxy =
    InnerAnalysisManagerProxy<CGSCCAnalysisManager, Module>;

/// We need a specialized result for the \c CGSCCAnalysisManagerModuleProxy so
/// it can have access to the call graph in order to walk all the SCCs when
/// invalidating things.
template <> class CGSCCAnalysisManagerModuleProxy::Result {
public:
  explicit Result(CGSCCAnalysisManager &InnerAM, LazyCallGraph &G)
      : InnerAM(&InnerAM), G(&G) {}

  /// Accessor for the analysis manager.
  CGSCCAnalysisManager &getManager() { return *InnerAM; }

  /// Handler for invalidation of the Module.
  ///
  /// If the proxy analysis itself is preserved, then we assume that the set of
  /// SCCs in the Module hasn't changed. Thus any pointers to SCCs in the
  /// CGSCCAnalysisManager are still valid, and we don't need to call \c clear
  /// on the CGSCCAnalysisManager.
  ///
  /// Regardless of whether this analysis is marked as preserved, all of the
  /// analyses in the \c CGSCCAnalysisManager are potentially invalidated based
  /// on the set of preserved analyses.
  bool invalidate(Module &M, const PreservedAnalyses &PA,
                  ModuleAnalysisManager::Invalidator &Inv);

private:
  CGSCCAnalysisManager *InnerAM;
  LazyCallGraph *G;
};

/// Provide a specialized run method for the \c CGSCCAnalysisManagerModuleProxy
/// so it can pass the lazy call graph to the result.
template <>
CGSCCAnalysisManagerModuleProxy::Result
CGSCCAnalysisManagerModuleProxy::run(Module &M, ModuleAnalysisManager &AM);

// Ensure the \c CGSCCAnalysisManagerModuleProxy is provided as an extern
// template.
extern template class InnerAnalysisManagerProxy<CGSCCAnalysisManager, Module>;

extern template class OuterAnalysisManagerProxy<
    ModuleAnalysisManager, LazyCallGraph::SCC, LazyCallGraph &>;

/// A proxy from a \c ModuleAnalysisManager to an \c SCC.
using ModuleAnalysisManagerCGSCCProxy =
    OuterAnalysisManagerProxy<ModuleAnalysisManager, LazyCallGraph::SCC,
                              LazyCallGraph &>;

/// Support structure for SCC passes to communicate updates the call graph back
/// to the CGSCC pass manager infrsatructure.
///
/// The CGSCC pass manager runs SCC passes which are allowed to update the call
/// graph and SCC structures. This means the structure the pass manager works
/// on is mutating underneath it. In order to support that, there needs to be
/// careful communication about the precise nature and ramifications of these
/// updates to the pass management infrastructure.
///
/// All SCC passes will have to accept a reference to the management layer's
/// update result struct and use it to reflect the results of any CG updates
/// performed.
///
/// Passes which do not change the call graph structure in any way can just
/// ignore this argument to their run method.
struct CGSCCUpdateResult {
  /// Worklist of the RefSCCs queued for processing.
  ///
  /// When a pass refines the graph and creates new RefSCCs or causes them to
  /// have a different shape or set of component SCCs it should add the RefSCCs
  /// to this worklist so that we visit them in the refined form.
  ///
  /// This worklist is in reverse post-order, as we pop off the back in order
  /// to observe RefSCCs in post-order. When adding RefSCCs, clients should add
  /// them in reverse post-order.
  SmallPriorityWorklist<LazyCallGraph::RefSCC *, 1> &RCWorklist;

  /// Worklist of the SCCs queued for processing.
  ///
  /// When a pass refines the graph and creates new SCCs or causes them to have
  /// a different shape or set of component functions it should add the SCCs to
  /// this worklist so that we visit them in the refined form.
  ///
  /// Note that if the SCCs are part of a RefSCC that is added to the \c
  /// RCWorklist, they don't need to be added here as visiting the RefSCC will
  /// be sufficient to re-visit the SCCs within it.
  ///
  /// This worklist is in reverse post-order, as we pop off the back in order
  /// to observe SCCs in post-order. When adding SCCs, clients should add them
  /// in reverse post-order.
  SmallPriorityWorklist<LazyCallGraph::SCC *, 1> &CWorklist;

  /// The set of invalidated RefSCCs which should be skipped if they are found
  /// in \c RCWorklist.
  ///
  /// This is used to quickly prune out RefSCCs when they get deleted and
  /// happen to already be on the worklist. We use this primarily to avoid
  /// scanning the list and removing entries from it.
  SmallPtrSetImpl<LazyCallGraph::RefSCC *> &InvalidatedRefSCCs;

  /// The set of invalidated SCCs which should be skipped if they are found
  /// in \c CWorklist.
  ///
  /// This is used to quickly prune out SCCs when they get deleted and happen
  /// to already be on the worklist. We use this primarily to avoid scanning
  /// the list and removing entries from it.
  SmallPtrSetImpl<LazyCallGraph::SCC *> &InvalidatedSCCs;

  /// If non-null, the updated current \c RefSCC being processed.
  ///
  /// This is set when a graph refinement takes place an the "current" point in
  /// the graph moves "down" or earlier in the post-order walk. This will often
  /// cause the "current" RefSCC to be a newly created RefSCC object and the
  /// old one to be added to the above worklist. When that happens, this
  /// pointer is non-null and can be used to continue processing the "top" of
  /// the post-order walk.
  LazyCallGraph::RefSCC *UpdatedRC;

  /// If non-null, the updated current \c SCC being processed.
  ///
  /// This is set when a graph refinement takes place an the "current" point in
  /// the graph moves "down" or earlier in the post-order walk. This will often
  /// cause the "current" SCC to be a newly created SCC object and the old one
  /// to be added to the above worklist. When that happens, this pointer is
  /// non-null and can be used to continue processing the "top" of the
  /// post-order walk.
  LazyCallGraph::SCC *UpdatedC;

  /// A hacky area where the inliner can retain history about inlining
  /// decisions that mutated the call graph's SCC structure in order to avoid
  /// infinite inlining. See the comments in the inliner's CG update logic.
  ///
  /// FIXME: Keeping this here seems like a big layering issue, we should look
  /// for a better technique.
  SmallDenseSet<std::pair<LazyCallGraph::Node *, LazyCallGraph::SCC *>, 4>
      &InlinedInternalEdges;
};

/// The core module pass which does a post-order walk of the SCCs and
/// runs a CGSCC pass over each one.
///
/// Designed to allow composition of a CGSCCPass(Manager) and
/// a ModulePassManager. Note that this pass must be run with a module analysis
/// manager as it uses the LazyCallGraph analysis. It will also run the
/// \c CGSCCAnalysisManagerModuleProxy analysis prior to running the CGSCC
/// pass over the module to enable a \c FunctionAnalysisManager to be used
/// within this run safely.
template <typename CGSCCPassT>
class ModuleToPostOrderCGSCCPassAdaptor
    : public PassInfoMixin<ModuleToPostOrderCGSCCPassAdaptor<CGSCCPassT>> {
public:
  explicit ModuleToPostOrderCGSCCPassAdaptor(CGSCCPassT Pass)
      : Pass(std::move(Pass)) {}

  // We have to explicitly define all the special member functions because MSVC
  // refuses to generate them.
  ModuleToPostOrderCGSCCPassAdaptor(
      const ModuleToPostOrderCGSCCPassAdaptor &Arg)
      : Pass(Arg.Pass) {}

  ModuleToPostOrderCGSCCPassAdaptor(ModuleToPostOrderCGSCCPassAdaptor &&Arg)
      : Pass(std::move(Arg.Pass)) {}

  friend void swap(ModuleToPostOrderCGSCCPassAdaptor &LHS,
                   ModuleToPostOrderCGSCCPassAdaptor &RHS) {
    std::swap(LHS.Pass, RHS.Pass);
  }

  ModuleToPostOrderCGSCCPassAdaptor &
  operator=(ModuleToPostOrderCGSCCPassAdaptor RHS) {
    swap(*this, RHS);
    return *this;
  }

  /// Runs the CGSCC pass across every SCC in the module.
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
    // Setup the CGSCC analysis manager from its proxy.
    CGSCCAnalysisManager &CGAM =
        AM.getResult<CGSCCAnalysisManagerModuleProxy>(M).getManager();

    // Get the call graph for this module.
    LazyCallGraph &CG = AM.getResult<LazyCallGraphAnalysis>(M);

    // We keep worklists to allow us to push more work onto the pass manager as
    // the passes are run.
    SmallPriorityWorklist<LazyCallGraph::RefSCC *, 1> RCWorklist;
    SmallPriorityWorklist<LazyCallGraph::SCC *, 1> CWorklist;

    // Keep sets for invalidated SCCs and RefSCCs that should be skipped when
    // iterating off the worklists.
    SmallPtrSet<LazyCallGraph::RefSCC *, 4> InvalidRefSCCSet;
    SmallPtrSet<LazyCallGraph::SCC *, 4> InvalidSCCSet;

    SmallDenseSet<std::pair<LazyCallGraph::Node *, LazyCallGraph::SCC *>, 4>
        InlinedInternalEdges;

    CGSCCUpdateResult UR = {RCWorklist,          CWorklist, InvalidRefSCCSet,
                            InvalidSCCSet,       nullptr,   nullptr,
                            InlinedInternalEdges};

    // Request PassInstrumentation from analysis manager, will use it to run
    // instrumenting callbacks for the passes later.
    PassInstrumentation PI = AM.getResult<PassInstrumentationAnalysis>(M);

    PreservedAnalyses PA = PreservedAnalyses::all();
    CG.buildRefSCCs();
    for (auto RCI = CG.postorder_ref_scc_begin(),
              RCE = CG.postorder_ref_scc_end();
         RCI != RCE;) {
      assert(RCWorklist.empty() &&
             "Should always start with an empty RefSCC worklist");
      // The postorder_ref_sccs range we are walking is lazily constructed, so
      // we only push the first one onto the worklist. The worklist allows us
      // to capture *new* RefSCCs created during transformations.
      //
      // We really want to form RefSCCs lazily because that makes them cheaper
      // to update as the program is simplified and allows us to have greater
      // cache locality as forming a RefSCC touches all the parts of all the
      // functions within that RefSCC.
      //
      // We also eagerly increment the iterator to the next position because
      // the CGSCC passes below may delete the current RefSCC.
      RCWorklist.insert(&*RCI++);

      do {
        LazyCallGraph::RefSCC *RC = RCWorklist.pop_back_val();
        if (InvalidRefSCCSet.count(RC)) {
          LLVM_DEBUG(dbgs() << "Skipping an invalid RefSCC...\n");
          continue;
        }

        assert(CWorklist.empty() &&
               "Should always start with an empty SCC worklist");

        LLVM_DEBUG(dbgs() << "Running an SCC pass across the RefSCC: " << *RC
                          << "\n");

        // Push the initial SCCs in reverse post-order as we'll pop off the
        // back and so see this in post-order.
        for (LazyCallGraph::SCC &C : llvm::reverse(*RC))
          CWorklist.insert(&C);

        do {
          LazyCallGraph::SCC *C = CWorklist.pop_back_val();
          // Due to call graph mutations, we may have invalid SCCs or SCCs from
          // other RefSCCs in the worklist. The invalid ones are dead and the
          // other RefSCCs should be queued above, so we just need to skip both
          // scenarios here.
          if (InvalidSCCSet.count(C)) {
            LLVM_DEBUG(dbgs() << "Skipping an invalid SCC...\n");
            continue;
          }
          if (&C->getOuterRefSCC() != RC) {
            LLVM_DEBUG(dbgs()
                       << "Skipping an SCC that is now part of some other "
                          "RefSCC...\n");
            continue;
          }

          do {
            // Check that we didn't miss any update scenario.
            assert(!InvalidSCCSet.count(C) && "Processing an invalid SCC!");
            assert(C->begin() != C->end() && "Cannot have an empty SCC!");
            assert(&C->getOuterRefSCC() == RC &&
                   "Processing an SCC in a different RefSCC!");

            UR.UpdatedRC = nullptr;
            UR.UpdatedC = nullptr;

            // Check the PassInstrumentation's BeforePass callbacks before
            // running the pass, skip its execution completely if asked to
            // (callback returns false).
            if (!PI.runBeforePass<LazyCallGraph::SCC>(Pass, *C))
              continue;

            PreservedAnalyses PassPA = Pass.run(*C, CGAM, CG, UR);

            if (UR.InvalidatedSCCs.count(C))
              PI.runAfterPassInvalidated<LazyCallGraph::SCC>(Pass);
            else
              PI.runAfterPass<LazyCallGraph::SCC>(Pass, *C);

            // Update the SCC and RefSCC if necessary.
            C = UR.UpdatedC ? UR.UpdatedC : C;
            RC = UR.UpdatedRC ? UR.UpdatedRC : RC;

            // If the CGSCC pass wasn't able to provide a valid updated SCC,
            // the current SCC may simply need to be skipped if invalid.
            if (UR.InvalidatedSCCs.count(C)) {
              LLVM_DEBUG(dbgs()
                         << "Skipping invalidated root or island SCC!\n");
              break;
            }
            // Check that we didn't miss any update scenario.
            assert(C->begin() != C->end() && "Cannot have an empty SCC!");

            // We handle invalidating the CGSCC analysis manager's information
            // for the (potentially updated) SCC here. Note that any other SCCs
            // whose structure has changed should have been invalidated by
            // whatever was updating the call graph. This SCC gets invalidated
            // late as it contains the nodes that were actively being
            // processed.
            CGAM.invalidate(*C, PassPA);

            // Then intersect the preserved set so that invalidation of module
            // analyses will eventually occur when the module pass completes.
            PA.intersect(std::move(PassPA));

            // The pass may have restructured the call graph and refined the
            // current SCC and/or RefSCC. We need to update our current SCC and
            // RefSCC pointers to follow these. Also, when the current SCC is
            // refined, re-run the SCC pass over the newly refined SCC in order
            // to observe the most precise SCC model available. This inherently
            // cannot cycle excessively as it only happens when we split SCCs
            // apart, at most converging on a DAG of single nodes.
            // FIXME: If we ever start having RefSCC passes, we'll want to
            // iterate there too.
            if (UR.UpdatedC)
              LLVM_DEBUG(dbgs()
                         << "Re-running SCC passes after a refinement of the "
                            "current SCC: "
                         << *UR.UpdatedC << "\n");

            // Note that both `C` and `RC` may at this point refer to deleted,
            // invalid SCC and RefSCCs respectively. But we will short circuit
            // the processing when we check them in the loop above.
          } while (UR.UpdatedC);
        } while (!CWorklist.empty());

        // We only need to keep internal inlined edge information within
        // a RefSCC, clear it to save on space and let the next time we visit
        // any of these functions have a fresh start.
        InlinedInternalEdges.clear();
      } while (!RCWorklist.empty());
    }

    // By definition we preserve the call garph, all SCC analyses, and the
    // analysis proxies by handling them above and in any nested pass managers.
    PA.preserveSet<AllAnalysesOn<LazyCallGraph::SCC>>();
    PA.preserve<LazyCallGraphAnalysis>();
    PA.preserve<CGSCCAnalysisManagerModuleProxy>();
    PA.preserve<FunctionAnalysisManagerModuleProxy>();
    return PA;
  }

private:
  CGSCCPassT Pass;
};

/// A function to deduce a function pass type and wrap it in the
/// templated adaptor.
template <typename CGSCCPassT>
ModuleToPostOrderCGSCCPassAdaptor<CGSCCPassT>
createModuleToPostOrderCGSCCPassAdaptor(CGSCCPassT Pass) {
  return ModuleToPostOrderCGSCCPassAdaptor<CGSCCPassT>(std::move(Pass));
}

/// A proxy from a \c FunctionAnalysisManager to an \c SCC.
///
/// When a module pass runs and triggers invalidation, both the CGSCC and
/// Function analysis manager proxies on the module get an invalidation event.
/// We don't want to fully duplicate responsibility for most of the
/// invalidation logic. Instead, this layer is only responsible for SCC-local
/// invalidation events. We work with the module's FunctionAnalysisManager to
/// invalidate function analyses.
class FunctionAnalysisManagerCGSCCProxy
    : public AnalysisInfoMixin<FunctionAnalysisManagerCGSCCProxy> {
public:
  class Result {
  public:
    explicit Result(FunctionAnalysisManager &FAM) : FAM(&FAM) {}

    /// Accessor for the analysis manager.
    FunctionAnalysisManager &getManager() { return *FAM; }

    bool invalidate(LazyCallGraph::SCC &C, const PreservedAnalyses &PA,
                    CGSCCAnalysisManager::Invalidator &Inv);

  private:
    FunctionAnalysisManager *FAM;
  };

  /// Computes the \c FunctionAnalysisManager and stores it in the result proxy.
  Result run(LazyCallGraph::SCC &C, CGSCCAnalysisManager &AM, LazyCallGraph &);

private:
  friend AnalysisInfoMixin<FunctionAnalysisManagerCGSCCProxy>;

  static AnalysisKey Key;
};

extern template class OuterAnalysisManagerProxy<CGSCCAnalysisManager, Function>;

/// A proxy from a \c CGSCCAnalysisManager to a \c Function.
using CGSCCAnalysisManagerFunctionProxy =
    OuterAnalysisManagerProxy<CGSCCAnalysisManager, Function>;

/// Helper to update the call graph after running a function pass.
///
/// Function passes can only mutate the call graph in specific ways. This
/// routine provides a helper that updates the call graph in those ways
/// including returning whether any changes were made and populating a CG
/// update result struct for the overall CGSCC walk.
LazyCallGraph::SCC &updateCGAndAnalysisManagerForFunctionPass(
    LazyCallGraph &G, LazyCallGraph::SCC &C, LazyCallGraph::Node &N,
    CGSCCAnalysisManager &AM, CGSCCUpdateResult &UR);

/// Adaptor that maps from a SCC to its functions.
///
/// Designed to allow composition of a FunctionPass(Manager) and
/// a CGSCCPassManager. Note that if this pass is constructed with a pointer
/// to a \c CGSCCAnalysisManager it will run the
/// \c FunctionAnalysisManagerCGSCCProxy analysis prior to running the function
/// pass over the SCC to enable a \c FunctionAnalysisManager to be used
/// within this run safely.
template <typename FunctionPassT>
class CGSCCToFunctionPassAdaptor
    : public PassInfoMixin<CGSCCToFunctionPassAdaptor<FunctionPassT>> {
public:
  explicit CGSCCToFunctionPassAdaptor(FunctionPassT Pass)
      : Pass(std::move(Pass)) {}

  // We have to explicitly define all the special member functions because MSVC
  // refuses to generate them.
  CGSCCToFunctionPassAdaptor(const CGSCCToFunctionPassAdaptor &Arg)
      : Pass(Arg.Pass) {}

  CGSCCToFunctionPassAdaptor(CGSCCToFunctionPassAdaptor &&Arg)
      : Pass(std::move(Arg.Pass)) {}

  friend void swap(CGSCCToFunctionPassAdaptor &LHS,
                   CGSCCToFunctionPassAdaptor &RHS) {
    std::swap(LHS.Pass, RHS.Pass);
  }

  CGSCCToFunctionPassAdaptor &operator=(CGSCCToFunctionPassAdaptor RHS) {
    swap(*this, RHS);
    return *this;
  }

  /// Runs the function pass across every function in the module.
  PreservedAnalyses run(LazyCallGraph::SCC &C, CGSCCAnalysisManager &AM,
                        LazyCallGraph &CG, CGSCCUpdateResult &UR) {
    // Setup the function analysis manager from its proxy.
    FunctionAnalysisManager &FAM =
        AM.getResult<FunctionAnalysisManagerCGSCCProxy>(C, CG).getManager();

    SmallVector<LazyCallGraph::Node *, 4> Nodes;
    for (LazyCallGraph::Node &N : C)
      Nodes.push_back(&N);

    // The SCC may get split while we are optimizing functions due to deleting
    // edges. If this happens, the current SCC can shift, so keep track of
    // a pointer we can overwrite.
    LazyCallGraph::SCC *CurrentC = &C;

    LLVM_DEBUG(dbgs() << "Running function passes across an SCC: " << C
                      << "\n");

    PreservedAnalyses PA = PreservedAnalyses::all();
    for (LazyCallGraph::Node *N : Nodes) {
      // Skip nodes from other SCCs. These may have been split out during
      // processing. We'll eventually visit those SCCs and pick up the nodes
      // there.
      if (CG.lookupSCC(*N) != CurrentC)
        continue;

      Function &F = N->getFunction();

      PassInstrumentation PI = FAM.getResult<PassInstrumentationAnalysis>(F);
      if (!PI.runBeforePass<Function>(Pass, F))
        continue;

      PreservedAnalyses PassPA = Pass.run(F, FAM);

      PI.runAfterPass<Function>(Pass, F);

      // We know that the function pass couldn't have invalidated any other
      // function's analyses (that's the contract of a function pass), so
      // directly handle the function analysis manager's invalidation here.
      FAM.invalidate(F, PassPA);

      // Then intersect the preserved set so that invalidation of module
      // analyses will eventually occur when the module pass completes.
      PA.intersect(std::move(PassPA));

      // If the call graph hasn't been preserved, update it based on this
      // function pass. This may also update the current SCC to point to
      // a smaller, more refined SCC.
      auto PAC = PA.getChecker<LazyCallGraphAnalysis>();
      if (!PAC.preserved() && !PAC.preservedSet<AllAnalysesOn<Module>>()) {
        CurrentC = &updateCGAndAnalysisManagerForFunctionPass(CG, *CurrentC, *N,
                                                              AM, UR);
        assert(
            CG.lookupSCC(*N) == CurrentC &&
            "Current SCC not updated to the SCC containing the current node!");
      }
    }

    // By definition we preserve the proxy. And we preserve all analyses on
    // Functions. This precludes *any* invalidation of function analyses by the
    // proxy, but that's OK because we've taken care to invalidate analyses in
    // the function analysis manager incrementally above.
    PA.preserveSet<AllAnalysesOn<Function>>();
    PA.preserve<FunctionAnalysisManagerCGSCCProxy>();

    // We've also ensured that we updated the call graph along the way.
    PA.preserve<LazyCallGraphAnalysis>();

    return PA;
  }

private:
  FunctionPassT Pass;
};

/// A function to deduce a function pass type and wrap it in the
/// templated adaptor.
template <typename FunctionPassT>
CGSCCToFunctionPassAdaptor<FunctionPassT>
createCGSCCToFunctionPassAdaptor(FunctionPassT Pass) {
  return CGSCCToFunctionPassAdaptor<FunctionPassT>(std::move(Pass));
}

/// A helper that repeats an SCC pass each time an indirect call is refined to
/// a direct call by that pass.
///
/// While the CGSCC pass manager works to re-visit SCCs and RefSCCs as they
/// change shape, we may also want to repeat an SCC pass if it simply refines
/// an indirect call to a direct call, even if doing so does not alter the
/// shape of the graph. Note that this only pertains to direct calls to
/// functions where IPO across the SCC may be able to compute more precise
/// results. For intrinsics, we assume scalar optimizations already can fully
/// reason about them.
///
/// This repetition has the potential to be very large however, as each one
/// might refine a single call site. As a consequence, in practice we use an
/// upper bound on the number of repetitions to limit things.
template <typename PassT>
class DevirtSCCRepeatedPass
    : public PassInfoMixin<DevirtSCCRepeatedPass<PassT>> {
public:
  explicit DevirtSCCRepeatedPass(PassT Pass, int MaxIterations)
      : Pass(std::move(Pass)), MaxIterations(MaxIterations) {}

  /// Runs the wrapped pass up to \c MaxIterations on the SCC, iterating
  /// whenever an indirect call is refined.
  PreservedAnalyses run(LazyCallGraph::SCC &InitialC, CGSCCAnalysisManager &AM,
                        LazyCallGraph &CG, CGSCCUpdateResult &UR) {
    PreservedAnalyses PA = PreservedAnalyses::all();
    PassInstrumentation PI =
        AM.getResult<PassInstrumentationAnalysis>(InitialC, CG);

    // The SCC may be refined while we are running passes over it, so set up
    // a pointer that we can update.
    LazyCallGraph::SCC *C = &InitialC;

    // Collect value handles for all of the indirect call sites.
    SmallVector<WeakTrackingVH, 8> CallHandles;

    // Struct to track the counts of direct and indirect calls in each function
    // of the SCC.
    struct CallCount {
      int Direct;
      int Indirect;
    };

    // Put value handles on all of the indirect calls and return the number of
    // direct calls for each function in the SCC.
    auto ScanSCC = [](LazyCallGraph::SCC &C,
                      SmallVectorImpl<WeakTrackingVH> &CallHandles) {
      assert(CallHandles.empty() && "Must start with a clear set of handles.");

      SmallVector<CallCount, 4> CallCounts;
      for (LazyCallGraph::Node &N : C) {
        CallCounts.push_back({0, 0});
        CallCount &Count = CallCounts.back();
        for (Instruction &I : instructions(N.getFunction()))
          if (auto CS = CallSite(&I)) {
            if (CS.getCalledFunction()) {
              ++Count.Direct;
            } else {
              ++Count.Indirect;
              CallHandles.push_back(WeakTrackingVH(&I));
            }
          }
      }

      return CallCounts;
    };

    // Populate the initial call handles and get the initial call counts.
    auto CallCounts = ScanSCC(*C, CallHandles);

    for (int Iteration = 0;; ++Iteration) {

      if (!PI.runBeforePass<LazyCallGraph::SCC>(Pass, *C))
        continue;

      PreservedAnalyses PassPA = Pass.run(*C, AM, CG, UR);

      if (UR.InvalidatedSCCs.count(C))
        PI.runAfterPassInvalidated<LazyCallGraph::SCC>(Pass);
      else
        PI.runAfterPass<LazyCallGraph::SCC>(Pass, *C);

      // If the SCC structure has changed, bail immediately and let the outer
      // CGSCC layer handle any iteration to reflect the refined structure.
      if (UR.UpdatedC && UR.UpdatedC != C) {
        PA.intersect(std::move(PassPA));
        break;
      }

      // Check that we didn't miss any update scenario.
      assert(!UR.InvalidatedSCCs.count(C) && "Processing an invalid SCC!");
      assert(C->begin() != C->end() && "Cannot have an empty SCC!");
      assert((int)CallCounts.size() == C->size() &&
             "Cannot have changed the size of the SCC!");

      // Check whether any of the handles were devirtualized.
      auto IsDevirtualizedHandle = [&](WeakTrackingVH &CallH) {
        if (!CallH)
          return false;
        auto CS = CallSite(CallH);
        if (!CS)
          return false;

        // If the call is still indirect, leave it alone.
        Function *F = CS.getCalledFunction();
        if (!F)
          return false;

        LLVM_DEBUG(dbgs() << "Found devirutalized call from "
                          << CS.getParent()->getParent()->getName() << " to "
                          << F->getName() << "\n");

        // We now have a direct call where previously we had an indirect call,
        // so iterate to process this devirtualization site.
        return true;
      };
      bool Devirt = llvm::any_of(CallHandles, IsDevirtualizedHandle);

      // Rescan to build up a new set of handles and count how many direct
      // calls remain. If we decide to iterate, this also sets up the input to
      // the next iteration.
      CallHandles.clear();
      auto NewCallCounts = ScanSCC(*C, CallHandles);

      // If we haven't found an explicit devirtualization already see if we
      // have decreased the number of indirect calls and increased the number
      // of direct calls for any function in the SCC. This can be fooled by all
      // manner of transformations such as DCE and other things, but seems to
      // work well in practice.
      if (!Devirt)
        for (int i = 0, Size = C->size(); i < Size; ++i)
          if (CallCounts[i].Indirect > NewCallCounts[i].Indirect &&
              CallCounts[i].Direct < NewCallCounts[i].Direct) {
            Devirt = true;
            break;
          }

      if (!Devirt) {
        PA.intersect(std::move(PassPA));
        break;
      }

      // Otherwise, if we've already hit our max, we're done.
      if (Iteration >= MaxIterations) {
        LLVM_DEBUG(
            dbgs() << "Found another devirtualization after hitting the max "
                      "number of repetitions ("
                   << MaxIterations << ") on SCC: " << *C << "\n");
        PA.intersect(std::move(PassPA));
        break;
      }

      LLVM_DEBUG(
          dbgs()
          << "Repeating an SCC pass after finding a devirtualization in: " << *C
          << "\n");

      // Move over the new call counts in preparation for iterating.
      CallCounts = std::move(NewCallCounts);

      // Update the analysis manager with each run and intersect the total set
      // of preserved analyses so we're ready to iterate.
      AM.invalidate(*C, PassPA);
      PA.intersect(std::move(PassPA));
    }

    // Note that we don't add any preserved entries here unlike a more normal
    // "pass manager" because we only handle invalidation *between* iterations,
    // not after the last iteration.
    return PA;
  }

private:
  PassT Pass;
  int MaxIterations;
};

/// A function to deduce a function pass type and wrap it in the
/// templated adaptor.
template <typename PassT>
DevirtSCCRepeatedPass<PassT> createDevirtSCCRepeatedPass(PassT Pass,
                                                         int MaxIterations) {
  return DevirtSCCRepeatedPass<PassT>(std::move(Pass), MaxIterations);
}

// Clear out the debug logging macro.
#undef DEBUG_TYPE

} // end namespace llvm

#endif // LLVM_ANALYSIS_CGSCCPASSMANAGER_H
