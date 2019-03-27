//===- LoopUnroll.cpp - Loop unroller pass --------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass implements a simple loop unroller.  It works best when loops have
// been canonicalized by the -indvars pass, allowing it to determine the trip
// counts of loops easily.
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LoopUnrollPass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/LoopUnrollAnalyzer.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/UnrollLoop.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
#include <tuple>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "loop-unroll"

static cl::opt<unsigned>
    UnrollThreshold("unroll-threshold", cl::Hidden,
                    cl::desc("The cost threshold for loop unrolling"));

static cl::opt<unsigned> UnrollPartialThreshold(
    "unroll-partial-threshold", cl::Hidden,
    cl::desc("The cost threshold for partial loop unrolling"));

static cl::opt<unsigned> UnrollMaxPercentThresholdBoost(
    "unroll-max-percent-threshold-boost", cl::init(400), cl::Hidden,
    cl::desc("The maximum 'boost' (represented as a percentage >= 100) applied "
             "to the threshold when aggressively unrolling a loop due to the "
             "dynamic cost savings. If completely unrolling a loop will reduce "
             "the total runtime from X to Y, we boost the loop unroll "
             "threshold to DefaultThreshold*std::min(MaxPercentThresholdBoost, "
             "X/Y). This limit avoids excessive code bloat."));

static cl::opt<unsigned> UnrollMaxIterationsCountToAnalyze(
    "unroll-max-iteration-count-to-analyze", cl::init(10), cl::Hidden,
    cl::desc("Don't allow loop unrolling to simulate more than this number of"
             "iterations when checking full unroll profitability"));

static cl::opt<unsigned> UnrollCount(
    "unroll-count", cl::Hidden,
    cl::desc("Use this unroll count for all loops including those with "
             "unroll_count pragma values, for testing purposes"));

static cl::opt<unsigned> UnrollMaxCount(
    "unroll-max-count", cl::Hidden,
    cl::desc("Set the max unroll count for partial and runtime unrolling, for"
             "testing purposes"));

static cl::opt<unsigned> UnrollFullMaxCount(
    "unroll-full-max-count", cl::Hidden,
    cl::desc(
        "Set the max unroll count for full unrolling, for testing purposes"));

static cl::opt<unsigned> UnrollPeelCount(
    "unroll-peel-count", cl::Hidden,
    cl::desc("Set the unroll peeling count, for testing purposes"));

static cl::opt<bool>
    UnrollAllowPartial("unroll-allow-partial", cl::Hidden,
                       cl::desc("Allows loops to be partially unrolled until "
                                "-unroll-threshold loop size is reached."));

static cl::opt<bool> UnrollAllowRemainder(
    "unroll-allow-remainder", cl::Hidden,
    cl::desc("Allow generation of a loop remainder (extra iterations) "
             "when unrolling a loop."));

static cl::opt<bool>
    UnrollRuntime("unroll-runtime", cl::ZeroOrMore, cl::Hidden,
                  cl::desc("Unroll loops with run-time trip counts"));

static cl::opt<unsigned> UnrollMaxUpperBound(
    "unroll-max-upperbound", cl::init(8), cl::Hidden,
    cl::desc(
        "The max of trip count upper bound that is considered in unrolling"));

static cl::opt<unsigned> PragmaUnrollThreshold(
    "pragma-unroll-threshold", cl::init(16 * 1024), cl::Hidden,
    cl::desc("Unrolled size limit for loops with an unroll(full) or "
             "unroll_count pragma."));

static cl::opt<unsigned> FlatLoopTripCountThreshold(
    "flat-loop-tripcount-threshold", cl::init(5), cl::Hidden,
    cl::desc("If the runtime tripcount for the loop is lower than the "
             "threshold, the loop is considered as flat and will be less "
             "aggressively unrolled."));

static cl::opt<bool>
    UnrollAllowPeeling("unroll-allow-peeling", cl::init(true), cl::Hidden,
                       cl::desc("Allows loops to be peeled when the dynamic "
                                "trip count is known to be low."));

static cl::opt<bool> UnrollUnrollRemainder(
  "unroll-remainder", cl::Hidden,
  cl::desc("Allow the loop remainder to be unrolled."));

// This option isn't ever intended to be enabled, it serves to allow
// experiments to check the assumptions about when this kind of revisit is
// necessary.
static cl::opt<bool> UnrollRevisitChildLoops(
    "unroll-revisit-child-loops", cl::Hidden,
    cl::desc("Enqueue and re-visit child loops in the loop PM after unrolling. "
             "This shouldn't typically be needed as child loops (or their "
             "clones) were already visited."));

/// A magic value for use with the Threshold parameter to indicate
/// that the loop unroll should be performed regardless of how much
/// code expansion would result.
static const unsigned NoThreshold = std::numeric_limits<unsigned>::max();

/// Gather the various unrolling parameters based on the defaults, compiler
/// flags, TTI overrides and user specified parameters.
TargetTransformInfo::UnrollingPreferences llvm::gatherUnrollingPreferences(
    Loop *L, ScalarEvolution &SE, const TargetTransformInfo &TTI, int OptLevel,
    Optional<unsigned> UserThreshold, Optional<unsigned> UserCount,
    Optional<bool> UserAllowPartial, Optional<bool> UserRuntime,
    Optional<bool> UserUpperBound, Optional<bool> UserAllowPeeling) {
  TargetTransformInfo::UnrollingPreferences UP;

  // Set up the defaults
  UP.Threshold = OptLevel > 2 ? 300 : 150;
  UP.MaxPercentThresholdBoost = 400;
  UP.OptSizeThreshold = 0;
  UP.PartialThreshold = 150;
  UP.PartialOptSizeThreshold = 0;
  UP.Count = 0;
  UP.PeelCount = 0;
  UP.DefaultUnrollRuntimeCount = 8;
  UP.MaxCount = std::numeric_limits<unsigned>::max();
  UP.FullUnrollMaxCount = std::numeric_limits<unsigned>::max();
  UP.BEInsns = 2;
  UP.Partial = false;
  UP.Runtime = false;
  UP.AllowRemainder = true;
  UP.UnrollRemainder = false;
  UP.AllowExpensiveTripCount = false;
  UP.Force = false;
  UP.UpperBound = false;
  UP.AllowPeeling = true;
  UP.UnrollAndJam = false;
  UP.UnrollAndJamInnerLoopThreshold = 60;

  // Override with any target specific settings
  TTI.getUnrollingPreferences(L, SE, UP);

  // Apply size attributes
  if (L->getHeader()->getParent()->optForSize()) {
    UP.Threshold = UP.OptSizeThreshold;
    UP.PartialThreshold = UP.PartialOptSizeThreshold;
  }

  // Apply any user values specified by cl::opt
  if (UnrollThreshold.getNumOccurrences() > 0)
    UP.Threshold = UnrollThreshold;
  if (UnrollPartialThreshold.getNumOccurrences() > 0)
    UP.PartialThreshold = UnrollPartialThreshold;
  if (UnrollMaxPercentThresholdBoost.getNumOccurrences() > 0)
    UP.MaxPercentThresholdBoost = UnrollMaxPercentThresholdBoost;
  if (UnrollMaxCount.getNumOccurrences() > 0)
    UP.MaxCount = UnrollMaxCount;
  if (UnrollFullMaxCount.getNumOccurrences() > 0)
    UP.FullUnrollMaxCount = UnrollFullMaxCount;
  if (UnrollPeelCount.getNumOccurrences() > 0)
    UP.PeelCount = UnrollPeelCount;
  if (UnrollAllowPartial.getNumOccurrences() > 0)
    UP.Partial = UnrollAllowPartial;
  if (UnrollAllowRemainder.getNumOccurrences() > 0)
    UP.AllowRemainder = UnrollAllowRemainder;
  if (UnrollRuntime.getNumOccurrences() > 0)
    UP.Runtime = UnrollRuntime;
  if (UnrollMaxUpperBound == 0)
    UP.UpperBound = false;
  if (UnrollAllowPeeling.getNumOccurrences() > 0)
    UP.AllowPeeling = UnrollAllowPeeling;
  if (UnrollUnrollRemainder.getNumOccurrences() > 0)
    UP.UnrollRemainder = UnrollUnrollRemainder;

  // Apply user values provided by argument
  if (UserThreshold.hasValue()) {
    UP.Threshold = *UserThreshold;
    UP.PartialThreshold = *UserThreshold;
  }
  if (UserCount.hasValue())
    UP.Count = *UserCount;
  if (UserAllowPartial.hasValue())
    UP.Partial = *UserAllowPartial;
  if (UserRuntime.hasValue())
    UP.Runtime = *UserRuntime;
  if (UserUpperBound.hasValue())
    UP.UpperBound = *UserUpperBound;
  if (UserAllowPeeling.hasValue())
    UP.AllowPeeling = *UserAllowPeeling;

  return UP;
}

namespace {

/// A struct to densely store the state of an instruction after unrolling at
/// each iteration.
///
/// This is designed to work like a tuple of <Instruction *, int> for the
/// purposes of hashing and lookup, but to be able to associate two boolean
/// states with each key.
struct UnrolledInstState {
  Instruction *I;
  int Iteration : 30;
  unsigned IsFree : 1;
  unsigned IsCounted : 1;
};

/// Hashing and equality testing for a set of the instruction states.
struct UnrolledInstStateKeyInfo {
  using PtrInfo = DenseMapInfo<Instruction *>;
  using PairInfo = DenseMapInfo<std::pair<Instruction *, int>>;

  static inline UnrolledInstState getEmptyKey() {
    return {PtrInfo::getEmptyKey(), 0, 0, 0};
  }

  static inline UnrolledInstState getTombstoneKey() {
    return {PtrInfo::getTombstoneKey(), 0, 0, 0};
  }

  static inline unsigned getHashValue(const UnrolledInstState &S) {
    return PairInfo::getHashValue({S.I, S.Iteration});
  }

  static inline bool isEqual(const UnrolledInstState &LHS,
                             const UnrolledInstState &RHS) {
    return PairInfo::isEqual({LHS.I, LHS.Iteration}, {RHS.I, RHS.Iteration});
  }
};

struct EstimatedUnrollCost {
  /// The estimated cost after unrolling.
  unsigned UnrolledCost;

  /// The estimated dynamic cost of executing the instructions in the
  /// rolled form.
  unsigned RolledDynamicCost;
};

} // end anonymous namespace

/// Figure out if the loop is worth full unrolling.
///
/// Complete loop unrolling can make some loads constant, and we need to know
/// if that would expose any further optimization opportunities.  This routine
/// estimates this optimization.  It computes cost of unrolled loop
/// (UnrolledCost) and dynamic cost of the original loop (RolledDynamicCost). By
/// dynamic cost we mean that we won't count costs of blocks that are known not
/// to be executed (i.e. if we have a branch in the loop and we know that at the
/// given iteration its condition would be resolved to true, we won't add up the
/// cost of the 'false'-block).
/// \returns Optional value, holding the RolledDynamicCost and UnrolledCost. If
/// the analysis failed (no benefits expected from the unrolling, or the loop is
/// too big to analyze), the returned value is None.
static Optional<EstimatedUnrollCost> analyzeLoopUnrollCost(
    const Loop *L, unsigned TripCount, DominatorTree &DT, ScalarEvolution &SE,
    const SmallPtrSetImpl<const Value *> &EphValues,
    const TargetTransformInfo &TTI, unsigned MaxUnrolledLoopSize) {
  // We want to be able to scale offsets by the trip count and add more offsets
  // to them without checking for overflows, and we already don't want to
  // analyze *massive* trip counts, so we force the max to be reasonably small.
  assert(UnrollMaxIterationsCountToAnalyze <
             (unsigned)(std::numeric_limits<int>::max() / 2) &&
         "The unroll iterations max is too large!");

  // Only analyze inner loops. We can't properly estimate cost of nested loops
  // and we won't visit inner loops again anyway.
  if (!L->empty())
    return None;

  // Don't simulate loops with a big or unknown tripcount
  if (!UnrollMaxIterationsCountToAnalyze || !TripCount ||
      TripCount > UnrollMaxIterationsCountToAnalyze)
    return None;

  SmallSetVector<BasicBlock *, 16> BBWorklist;
  SmallSetVector<std::pair<BasicBlock *, BasicBlock *>, 4> ExitWorklist;
  DenseMap<Value *, Constant *> SimplifiedValues;
  SmallVector<std::pair<Value *, Constant *>, 4> SimplifiedInputValues;

  // The estimated cost of the unrolled form of the loop. We try to estimate
  // this by simplifying as much as we can while computing the estimate.
  unsigned UnrolledCost = 0;

  // We also track the estimated dynamic (that is, actually executed) cost in
  // the rolled form. This helps identify cases when the savings from unrolling
  // aren't just exposing dead control flows, but actual reduced dynamic
  // instructions due to the simplifications which we expect to occur after
  // unrolling.
  unsigned RolledDynamicCost = 0;

  // We track the simplification of each instruction in each iteration. We use
  // this to recursively merge costs into the unrolled cost on-demand so that
  // we don't count the cost of any dead code. This is essentially a map from
  // <instruction, int> to <bool, bool>, but stored as a densely packed struct.
  DenseSet<UnrolledInstState, UnrolledInstStateKeyInfo> InstCostMap;

  // A small worklist used to accumulate cost of instructions from each
  // observable and reached root in the loop.
  SmallVector<Instruction *, 16> CostWorklist;

  // PHI-used worklist used between iterations while accumulating cost.
  SmallVector<Instruction *, 4> PHIUsedList;

  // Helper function to accumulate cost for instructions in the loop.
  auto AddCostRecursively = [&](Instruction &RootI, int Iteration) {
    assert(Iteration >= 0 && "Cannot have a negative iteration!");
    assert(CostWorklist.empty() && "Must start with an empty cost list");
    assert(PHIUsedList.empty() && "Must start with an empty phi used list");
    CostWorklist.push_back(&RootI);
    for (;; --Iteration) {
      do {
        Instruction *I = CostWorklist.pop_back_val();

        // InstCostMap only uses I and Iteration as a key, the other two values
        // don't matter here.
        auto CostIter = InstCostMap.find({I, Iteration, 0, 0});
        if (CostIter == InstCostMap.end())
          // If an input to a PHI node comes from a dead path through the loop
          // we may have no cost data for it here. What that actually means is
          // that it is free.
          continue;
        auto &Cost = *CostIter;
        if (Cost.IsCounted)
          // Already counted this instruction.
          continue;

        // Mark that we are counting the cost of this instruction now.
        Cost.IsCounted = true;

        // If this is a PHI node in the loop header, just add it to the PHI set.
        if (auto *PhiI = dyn_cast<PHINode>(I))
          if (PhiI->getParent() == L->getHeader()) {
            assert(Cost.IsFree && "Loop PHIs shouldn't be evaluated as they "
                                  "inherently simplify during unrolling.");
            if (Iteration == 0)
              continue;

            // Push the incoming value from the backedge into the PHI used list
            // if it is an in-loop instruction. We'll use this to populate the
            // cost worklist for the next iteration (as we count backwards).
            if (auto *OpI = dyn_cast<Instruction>(
                    PhiI->getIncomingValueForBlock(L->getLoopLatch())))
              if (L->contains(OpI))
                PHIUsedList.push_back(OpI);
            continue;
          }

        // First accumulate the cost of this instruction.
        if (!Cost.IsFree) {
          UnrolledCost += TTI.getUserCost(I);
          LLVM_DEBUG(dbgs() << "Adding cost of instruction (iteration "
                            << Iteration << "): ");
          LLVM_DEBUG(I->dump());
        }

        // We must count the cost of every operand which is not free,
        // recursively. If we reach a loop PHI node, simply add it to the set
        // to be considered on the next iteration (backwards!).
        for (Value *Op : I->operands()) {
          // Check whether this operand is free due to being a constant or
          // outside the loop.
          auto *OpI = dyn_cast<Instruction>(Op);
          if (!OpI || !L->contains(OpI))
            continue;

          // Otherwise accumulate its cost.
          CostWorklist.push_back(OpI);
        }
      } while (!CostWorklist.empty());

      if (PHIUsedList.empty())
        // We've exhausted the search.
        break;

      assert(Iteration > 0 &&
             "Cannot track PHI-used values past the first iteration!");
      CostWorklist.append(PHIUsedList.begin(), PHIUsedList.end());
      PHIUsedList.clear();
    }
  };

  // Ensure that we don't violate the loop structure invariants relied on by
  // this analysis.
  assert(L->isLoopSimplifyForm() && "Must put loop into normal form first.");
  assert(L->isLCSSAForm(DT) &&
         "Must have loops in LCSSA form to track live-out values.");

  LLVM_DEBUG(dbgs() << "Starting LoopUnroll profitability analysis...\n");

  // Simulate execution of each iteration of the loop counting instructions,
  // which would be simplified.
  // Since the same load will take different values on different iterations,
  // we literally have to go through all loop's iterations.
  for (unsigned Iteration = 0; Iteration < TripCount; ++Iteration) {
    LLVM_DEBUG(dbgs() << " Analyzing iteration " << Iteration << "\n");

    // Prepare for the iteration by collecting any simplified entry or backedge
    // inputs.
    for (Instruction &I : *L->getHeader()) {
      auto *PHI = dyn_cast<PHINode>(&I);
      if (!PHI)
        break;

      // The loop header PHI nodes must have exactly two input: one from the
      // loop preheader and one from the loop latch.
      assert(
          PHI->getNumIncomingValues() == 2 &&
          "Must have an incoming value only for the preheader and the latch.");

      Value *V = PHI->getIncomingValueForBlock(
          Iteration == 0 ? L->getLoopPreheader() : L->getLoopLatch());
      Constant *C = dyn_cast<Constant>(V);
      if (Iteration != 0 && !C)
        C = SimplifiedValues.lookup(V);
      if (C)
        SimplifiedInputValues.push_back({PHI, C});
    }

    // Now clear and re-populate the map for the next iteration.
    SimplifiedValues.clear();
    while (!SimplifiedInputValues.empty())
      SimplifiedValues.insert(SimplifiedInputValues.pop_back_val());

    UnrolledInstAnalyzer Analyzer(Iteration, SimplifiedValues, SE, L);

    BBWorklist.clear();
    BBWorklist.insert(L->getHeader());
    // Note that we *must not* cache the size, this loop grows the worklist.
    for (unsigned Idx = 0; Idx != BBWorklist.size(); ++Idx) {
      BasicBlock *BB = BBWorklist[Idx];

      // Visit all instructions in the given basic block and try to simplify
      // it.  We don't change the actual IR, just count optimization
      // opportunities.
      for (Instruction &I : *BB) {
        // These won't get into the final code - don't even try calculating the
        // cost for them.
        if (isa<DbgInfoIntrinsic>(I) || EphValues.count(&I))
          continue;

        // Track this instruction's expected baseline cost when executing the
        // rolled loop form.
        RolledDynamicCost += TTI.getUserCost(&I);

        // Visit the instruction to analyze its loop cost after unrolling,
        // and if the visitor returns true, mark the instruction as free after
        // unrolling and continue.
        bool IsFree = Analyzer.visit(I);
        bool Inserted = InstCostMap.insert({&I, (int)Iteration,
                                           (unsigned)IsFree,
                                           /*IsCounted*/ false}).second;
        (void)Inserted;
        assert(Inserted && "Cannot have a state for an unvisited instruction!");

        if (IsFree)
          continue;

        // Can't properly model a cost of a call.
        // FIXME: With a proper cost model we should be able to do it.
        if (auto *CI = dyn_cast<CallInst>(&I)) {
          const Function *Callee = CI->getCalledFunction();
          if (!Callee || TTI.isLoweredToCall(Callee)) {
            LLVM_DEBUG(dbgs() << "Can't analyze cost of loop with call\n");
            return None;
          }
        }

        // If the instruction might have a side-effect recursively account for
        // the cost of it and all the instructions leading up to it.
        if (I.mayHaveSideEffects())
          AddCostRecursively(I, Iteration);

        // If unrolled body turns out to be too big, bail out.
        if (UnrolledCost > MaxUnrolledLoopSize) {
          LLVM_DEBUG(dbgs() << "  Exceeded threshold.. exiting.\n"
                            << "  UnrolledCost: " << UnrolledCost
                            << ", MaxUnrolledLoopSize: " << MaxUnrolledLoopSize
                            << "\n");
          return None;
        }
      }

      Instruction *TI = BB->getTerminator();

      // Add in the live successors by first checking whether we have terminator
      // that may be simplified based on the values simplified by this call.
      BasicBlock *KnownSucc = nullptr;
      if (BranchInst *BI = dyn_cast<BranchInst>(TI)) {
        if (BI->isConditional()) {
          if (Constant *SimpleCond =
                  SimplifiedValues.lookup(BI->getCondition())) {
            // Just take the first successor if condition is undef
            if (isa<UndefValue>(SimpleCond))
              KnownSucc = BI->getSuccessor(0);
            else if (ConstantInt *SimpleCondVal =
                         dyn_cast<ConstantInt>(SimpleCond))
              KnownSucc = BI->getSuccessor(SimpleCondVal->isZero() ? 1 : 0);
          }
        }
      } else if (SwitchInst *SI = dyn_cast<SwitchInst>(TI)) {
        if (Constant *SimpleCond =
                SimplifiedValues.lookup(SI->getCondition())) {
          // Just take the first successor if condition is undef
          if (isa<UndefValue>(SimpleCond))
            KnownSucc = SI->getSuccessor(0);
          else if (ConstantInt *SimpleCondVal =
                       dyn_cast<ConstantInt>(SimpleCond))
            KnownSucc = SI->findCaseValue(SimpleCondVal)->getCaseSuccessor();
        }
      }
      if (KnownSucc) {
        if (L->contains(KnownSucc))
          BBWorklist.insert(KnownSucc);
        else
          ExitWorklist.insert({BB, KnownSucc});
        continue;
      }

      // Add BB's successors to the worklist.
      for (BasicBlock *Succ : successors(BB))
        if (L->contains(Succ))
          BBWorklist.insert(Succ);
        else
          ExitWorklist.insert({BB, Succ});
      AddCostRecursively(*TI, Iteration);
    }

    // If we found no optimization opportunities on the first iteration, we
    // won't find them on later ones too.
    if (UnrolledCost == RolledDynamicCost) {
      LLVM_DEBUG(dbgs() << "  No opportunities found.. exiting.\n"
                        << "  UnrolledCost: " << UnrolledCost << "\n");
      return None;
    }
  }

  while (!ExitWorklist.empty()) {
    BasicBlock *ExitingBB, *ExitBB;
    std::tie(ExitingBB, ExitBB) = ExitWorklist.pop_back_val();

    for (Instruction &I : *ExitBB) {
      auto *PN = dyn_cast<PHINode>(&I);
      if (!PN)
        break;

      Value *Op = PN->getIncomingValueForBlock(ExitingBB);
      if (auto *OpI = dyn_cast<Instruction>(Op))
        if (L->contains(OpI))
          AddCostRecursively(*OpI, TripCount - 1);
    }
  }

  LLVM_DEBUG(dbgs() << "Analysis finished:\n"
                    << "UnrolledCost: " << UnrolledCost << ", "
                    << "RolledDynamicCost: " << RolledDynamicCost << "\n");
  return {{UnrolledCost, RolledDynamicCost}};
}

/// ApproximateLoopSize - Approximate the size of the loop.
unsigned llvm::ApproximateLoopSize(
    const Loop *L, unsigned &NumCalls, bool &NotDuplicatable, bool &Convergent,
    const TargetTransformInfo &TTI,
    const SmallPtrSetImpl<const Value *> &EphValues, unsigned BEInsns) {
  CodeMetrics Metrics;
  for (BasicBlock *BB : L->blocks())
    Metrics.analyzeBasicBlock(BB, TTI, EphValues);
  NumCalls = Metrics.NumInlineCandidates;
  NotDuplicatable = Metrics.notDuplicatable;
  Convergent = Metrics.convergent;

  unsigned LoopSize = Metrics.NumInsts;

  // Don't allow an estimate of size zero.  This would allows unrolling of loops
  // with huge iteration counts, which is a compile time problem even if it's
  // not a problem for code quality. Also, the code using this size may assume
  // that each loop has at least three instructions (likely a conditional
  // branch, a comparison feeding that branch, and some kind of loop increment
  // feeding that comparison instruction).
  LoopSize = std::max(LoopSize, BEInsns + 1);

  return LoopSize;
}

// Returns the loop hint metadata node with the given name (for example,
// "llvm.loop.unroll.count").  If no such metadata node exists, then nullptr is
// returned.
static MDNode *GetUnrollMetadataForLoop(const Loop *L, StringRef Name) {
  if (MDNode *LoopID = L->getLoopID())
    return GetUnrollMetadata(LoopID, Name);
  return nullptr;
}

// Returns true if the loop has an unroll(full) pragma.
static bool HasUnrollFullPragma(const Loop *L) {
  return GetUnrollMetadataForLoop(L, "llvm.loop.unroll.full");
}

// Returns true if the loop has an unroll(enable) pragma. This metadata is used
// for both "#pragma unroll" and "#pragma clang loop unroll(enable)" directives.
static bool HasUnrollEnablePragma(const Loop *L) {
  return GetUnrollMetadataForLoop(L, "llvm.loop.unroll.enable");
}

// Returns true if the loop has an runtime unroll(disable) pragma.
static bool HasRuntimeUnrollDisablePragma(const Loop *L) {
  return GetUnrollMetadataForLoop(L, "llvm.loop.unroll.runtime.disable");
}

// If loop has an unroll_count pragma return the (necessarily
// positive) value from the pragma.  Otherwise return 0.
static unsigned UnrollCountPragmaValue(const Loop *L) {
  MDNode *MD = GetUnrollMetadataForLoop(L, "llvm.loop.unroll.count");
  if (MD) {
    assert(MD->getNumOperands() == 2 &&
           "Unroll count hint metadata should have two operands.");
    unsigned Count =
        mdconst::extract<ConstantInt>(MD->getOperand(1))->getZExtValue();
    assert(Count >= 1 && "Unroll count must be positive.");
    return Count;
  }
  return 0;
}

// Computes the boosting factor for complete unrolling.
// If fully unrolling the loop would save a lot of RolledDynamicCost, it would
// be beneficial to fully unroll the loop even if unrolledcost is large. We
// use (RolledDynamicCost / UnrolledCost) to model the unroll benefits to adjust
// the unroll threshold.
static unsigned getFullUnrollBoostingFactor(const EstimatedUnrollCost &Cost,
                                            unsigned MaxPercentThresholdBoost) {
  if (Cost.RolledDynamicCost >= std::numeric_limits<unsigned>::max() / 100)
    return 100;
  else if (Cost.UnrolledCost != 0)
    // The boosting factor is RolledDynamicCost / UnrolledCost
    return std::min(100 * Cost.RolledDynamicCost / Cost.UnrolledCost,
                    MaxPercentThresholdBoost);
  else
    return MaxPercentThresholdBoost;
}

// Returns loop size estimation for unrolled loop.
static uint64_t getUnrolledLoopSize(
    unsigned LoopSize,
    TargetTransformInfo::UnrollingPreferences &UP) {
  assert(LoopSize >= UP.BEInsns && "LoopSize should not be less than BEInsns!");
  return (uint64_t)(LoopSize - UP.BEInsns) * UP.Count + UP.BEInsns;
}

// Returns true if unroll count was set explicitly.
// Calculates unroll count and writes it to UP.Count.
// Unless IgnoreUser is true, will also use metadata and command-line options
// that are specific to to the LoopUnroll pass (which, for instance, are
// irrelevant for the LoopUnrollAndJam pass).
// FIXME: This function is used by LoopUnroll and LoopUnrollAndJam, but consumes
// many LoopUnroll-specific options. The shared functionality should be
// refactored into it own function.
bool llvm::computeUnrollCount(
    Loop *L, const TargetTransformInfo &TTI, DominatorTree &DT, LoopInfo *LI,
    ScalarEvolution &SE, const SmallPtrSetImpl<const Value *> &EphValues,
    OptimizationRemarkEmitter *ORE, unsigned &TripCount, unsigned MaxTripCount,
    unsigned &TripMultiple, unsigned LoopSize,
    TargetTransformInfo::UnrollingPreferences &UP, bool &UseUpperBound) {

  // Check for explicit Count.
  // 1st priority is unroll count set by "unroll-count" option.
  bool UserUnrollCount = UnrollCount.getNumOccurrences() > 0;
  if (UserUnrollCount) {
    UP.Count = UnrollCount;
    UP.AllowExpensiveTripCount = true;
    UP.Force = true;
    if (UP.AllowRemainder && getUnrolledLoopSize(LoopSize, UP) < UP.Threshold)
      return true;
  }

  // 2nd priority is unroll count set by pragma.
  unsigned PragmaCount = UnrollCountPragmaValue(L);
  if (PragmaCount > 0) {
    UP.Count = PragmaCount;
    UP.Runtime = true;
    UP.AllowExpensiveTripCount = true;
    UP.Force = true;
    if ((UP.AllowRemainder || (TripMultiple % PragmaCount == 0)) &&
        getUnrolledLoopSize(LoopSize, UP) < PragmaUnrollThreshold)
      return true;
  }
  bool PragmaFullUnroll = HasUnrollFullPragma(L);
  if (PragmaFullUnroll && TripCount != 0) {
    UP.Count = TripCount;
    if (getUnrolledLoopSize(LoopSize, UP) < PragmaUnrollThreshold)
      return false;
  }

  bool PragmaEnableUnroll = HasUnrollEnablePragma(L);
  bool ExplicitUnroll = PragmaCount > 0 || PragmaFullUnroll ||
                        PragmaEnableUnroll || UserUnrollCount;

  if (ExplicitUnroll && TripCount != 0) {
    // If the loop has an unrolling pragma, we want to be more aggressive with
    // unrolling limits. Set thresholds to at least the PragmaUnrollThreshold
    // value which is larger than the default limits.
    UP.Threshold = std::max<unsigned>(UP.Threshold, PragmaUnrollThreshold);
    UP.PartialThreshold =
        std::max<unsigned>(UP.PartialThreshold, PragmaUnrollThreshold);
  }

  // 3rd priority is full unroll count.
  // Full unroll makes sense only when TripCount or its upper bound could be
  // statically calculated.
  // Also we need to check if we exceed FullUnrollMaxCount.
  // If using the upper bound to unroll, TripMultiple should be set to 1 because
  // we do not know when loop may exit.
  // MaxTripCount and ExactTripCount cannot both be non zero since we only
  // compute the former when the latter is zero.
  unsigned ExactTripCount = TripCount;
  assert((ExactTripCount == 0 || MaxTripCount == 0) &&
         "ExtractTripCount and MaxTripCount cannot both be non zero.");
  unsigned FullUnrollTripCount = ExactTripCount ? ExactTripCount : MaxTripCount;
  UP.Count = FullUnrollTripCount;
  if (FullUnrollTripCount && FullUnrollTripCount <= UP.FullUnrollMaxCount) {
    // When computing the unrolled size, note that BEInsns are not replicated
    // like the rest of the loop body.
    if (getUnrolledLoopSize(LoopSize, UP) < UP.Threshold) {
      UseUpperBound = (MaxTripCount == FullUnrollTripCount);
      TripCount = FullUnrollTripCount;
      TripMultiple = UP.UpperBound ? 1 : TripMultiple;
      return ExplicitUnroll;
    } else {
      // The loop isn't that small, but we still can fully unroll it if that
      // helps to remove a significant number of instructions.
      // To check that, run additional analysis on the loop.
      if (Optional<EstimatedUnrollCost> Cost = analyzeLoopUnrollCost(
              L, FullUnrollTripCount, DT, SE, EphValues, TTI,
              UP.Threshold * UP.MaxPercentThresholdBoost / 100)) {
        unsigned Boost =
            getFullUnrollBoostingFactor(*Cost, UP.MaxPercentThresholdBoost);
        if (Cost->UnrolledCost < UP.Threshold * Boost / 100) {
          UseUpperBound = (MaxTripCount == FullUnrollTripCount);
          TripCount = FullUnrollTripCount;
          TripMultiple = UP.UpperBound ? 1 : TripMultiple;
          return ExplicitUnroll;
        }
      }
    }
  }

  // 4th priority is loop peeling.
  computePeelCount(L, LoopSize, UP, TripCount, SE);
  if (UP.PeelCount) {
    UP.Runtime = false;
    UP.Count = 1;
    return ExplicitUnroll;
  }

  // 5th priority is partial unrolling.
  // Try partial unroll only when TripCount could be statically calculated.
  if (TripCount) {
    UP.Partial |= ExplicitUnroll;
    if (!UP.Partial) {
      LLVM_DEBUG(dbgs() << "  will not try to unroll partially because "
                        << "-unroll-allow-partial not given\n");
      UP.Count = 0;
      return false;
    }
    if (UP.Count == 0)
      UP.Count = TripCount;
    if (UP.PartialThreshold != NoThreshold) {
      // Reduce unroll count to be modulo of TripCount for partial unrolling.
      if (getUnrolledLoopSize(LoopSize, UP) > UP.PartialThreshold)
        UP.Count =
            (std::max(UP.PartialThreshold, UP.BEInsns + 1) - UP.BEInsns) /
            (LoopSize - UP.BEInsns);
      if (UP.Count > UP.MaxCount)
        UP.Count = UP.MaxCount;
      while (UP.Count != 0 && TripCount % UP.Count != 0)
        UP.Count--;
      if (UP.AllowRemainder && UP.Count <= 1) {
        // If there is no Count that is modulo of TripCount, set Count to
        // largest power-of-two factor that satisfies the threshold limit.
        // As we'll create fixup loop, do the type of unrolling only if
        // remainder loop is allowed.
        UP.Count = UP.DefaultUnrollRuntimeCount;
        while (UP.Count != 0 &&
               getUnrolledLoopSize(LoopSize, UP) > UP.PartialThreshold)
          UP.Count >>= 1;
      }
      if (UP.Count < 2) {
        if (PragmaEnableUnroll)
          ORE->emit([&]() {
            return OptimizationRemarkMissed(DEBUG_TYPE,
                                            "UnrollAsDirectedTooLarge",
                                            L->getStartLoc(), L->getHeader())
                   << "Unable to unroll loop as directed by unroll(enable) "
                      "pragma "
                      "because unrolled size is too large.";
          });
        UP.Count = 0;
      }
    } else {
      UP.Count = TripCount;
    }
    if (UP.Count > UP.MaxCount)
      UP.Count = UP.MaxCount;
    if ((PragmaFullUnroll || PragmaEnableUnroll) && TripCount &&
        UP.Count != TripCount)
      ORE->emit([&]() {
        return OptimizationRemarkMissed(DEBUG_TYPE,
                                        "FullUnrollAsDirectedTooLarge",
                                        L->getStartLoc(), L->getHeader())
               << "Unable to fully unroll loop as directed by unroll pragma "
                  "because "
                  "unrolled size is too large.";
      });
    return ExplicitUnroll;
  }
  assert(TripCount == 0 &&
         "All cases when TripCount is constant should be covered here.");
  if (PragmaFullUnroll)
    ORE->emit([&]() {
      return OptimizationRemarkMissed(
                 DEBUG_TYPE, "CantFullUnrollAsDirectedRuntimeTripCount",
                 L->getStartLoc(), L->getHeader())
             << "Unable to fully unroll loop as directed by unroll(full) "
                "pragma "
                "because loop has a runtime trip count.";
    });

  // 6th priority is runtime unrolling.
  // Don't unroll a runtime trip count loop when it is disabled.
  if (HasRuntimeUnrollDisablePragma(L)) {
    UP.Count = 0;
    return false;
  }

  // Check if the runtime trip count is too small when profile is available.
  if (L->getHeader()->getParent()->hasProfileData()) {
    if (auto ProfileTripCount = getLoopEstimatedTripCount(L)) {
      if (*ProfileTripCount < FlatLoopTripCountThreshold)
        return false;
      else
        UP.AllowExpensiveTripCount = true;
    }
  }

  // Reduce count based on the type of unrolling and the threshold values.
  UP.Runtime |= PragmaEnableUnroll || PragmaCount > 0 || UserUnrollCount;
  if (!UP.Runtime) {
    LLVM_DEBUG(
        dbgs() << "  will not try to unroll loop with runtime trip count "
               << "-unroll-runtime not given\n");
    UP.Count = 0;
    return false;
  }
  if (UP.Count == 0)
    UP.Count = UP.DefaultUnrollRuntimeCount;

  // Reduce unroll count to be the largest power-of-two factor of
  // the original count which satisfies the threshold limit.
  while (UP.Count != 0 &&
         getUnrolledLoopSize(LoopSize, UP) > UP.PartialThreshold)
    UP.Count >>= 1;

#ifndef NDEBUG
  unsigned OrigCount = UP.Count;
#endif

  if (!UP.AllowRemainder && UP.Count != 0 && (TripMultiple % UP.Count) != 0) {
    while (UP.Count != 0 && TripMultiple % UP.Count != 0)
      UP.Count >>= 1;
    LLVM_DEBUG(
        dbgs() << "Remainder loop is restricted (that could architecture "
                  "specific or because the loop contains a convergent "
                  "instruction), so unroll count must divide the trip "
                  "multiple, "
               << TripMultiple << ".  Reducing unroll count from " << OrigCount
               << " to " << UP.Count << ".\n");

    using namespace ore;

    if (PragmaCount > 0 && !UP.AllowRemainder)
      ORE->emit([&]() {
        return OptimizationRemarkMissed(DEBUG_TYPE,
                                        "DifferentUnrollCountFromDirected",
                                        L->getStartLoc(), L->getHeader())
               << "Unable to unroll loop the number of times directed by "
                  "unroll_count pragma because remainder loop is restricted "
                  "(that could architecture specific or because the loop "
                  "contains a convergent instruction) and so must have an "
                  "unroll "
                  "count that divides the loop trip multiple of "
               << NV("TripMultiple", TripMultiple) << ".  Unrolling instead "
               << NV("UnrollCount", UP.Count) << " time(s).";
      });
  }

  if (UP.Count > UP.MaxCount)
    UP.Count = UP.MaxCount;
  LLVM_DEBUG(dbgs() << "  partially unrolling with count: " << UP.Count
                    << "\n");
  if (UP.Count < 2)
    UP.Count = 0;
  return ExplicitUnroll;
}

static LoopUnrollResult tryToUnrollLoop(
    Loop *L, DominatorTree &DT, LoopInfo *LI, ScalarEvolution &SE,
    const TargetTransformInfo &TTI, AssumptionCache &AC,
    OptimizationRemarkEmitter &ORE, bool PreserveLCSSA, int OptLevel,
    bool OnlyWhenForced, Optional<unsigned> ProvidedCount,
    Optional<unsigned> ProvidedThreshold, Optional<bool> ProvidedAllowPartial,
    Optional<bool> ProvidedRuntime, Optional<bool> ProvidedUpperBound,
    Optional<bool> ProvidedAllowPeeling) {
  LLVM_DEBUG(dbgs() << "Loop Unroll: F["
                    << L->getHeader()->getParent()->getName() << "] Loop %"
                    << L->getHeader()->getName() << "\n");
  TransformationMode TM = hasUnrollTransformation(L);
  if (TM & TM_Disable)
    return LoopUnrollResult::Unmodified;
  if (!L->isLoopSimplifyForm()) {
    LLVM_DEBUG(
        dbgs() << "  Not unrolling loop which is not in loop-simplify form.\n");
    return LoopUnrollResult::Unmodified;
  }

  // When automtatic unrolling is disabled, do not unroll unless overridden for
  // this loop.
  if (OnlyWhenForced && !(TM & TM_Enable))
    return LoopUnrollResult::Unmodified;

  unsigned NumInlineCandidates;
  bool NotDuplicatable;
  bool Convergent;
  TargetTransformInfo::UnrollingPreferences UP = gatherUnrollingPreferences(
      L, SE, TTI, OptLevel, ProvidedThreshold, ProvidedCount,
      ProvidedAllowPartial, ProvidedRuntime, ProvidedUpperBound,
      ProvidedAllowPeeling);
  // Exit early if unrolling is disabled.
  if (UP.Threshold == 0 && (!UP.Partial || UP.PartialThreshold == 0))
    return LoopUnrollResult::Unmodified;

  SmallPtrSet<const Value *, 32> EphValues;
  CodeMetrics::collectEphemeralValues(L, &AC, EphValues);

  unsigned LoopSize =
      ApproximateLoopSize(L, NumInlineCandidates, NotDuplicatable, Convergent,
                          TTI, EphValues, UP.BEInsns);
  LLVM_DEBUG(dbgs() << "  Loop Size = " << LoopSize << "\n");
  if (NotDuplicatable) {
    LLVM_DEBUG(dbgs() << "  Not unrolling loop which contains non-duplicatable"
                      << " instructions.\n");
    return LoopUnrollResult::Unmodified;
  }
  if (NumInlineCandidates != 0) {
    LLVM_DEBUG(dbgs() << "  Not unrolling loop with inlinable calls.\n");
    return LoopUnrollResult::Unmodified;
  }

  // Find trip count and trip multiple if count is not available
  unsigned TripCount = 0;
  unsigned MaxTripCount = 0;
  unsigned TripMultiple = 1;
  // If there are multiple exiting blocks but one of them is the latch, use the
  // latch for the trip count estimation. Otherwise insist on a single exiting
  // block for the trip count estimation.
  BasicBlock *ExitingBlock = L->getLoopLatch();
  if (!ExitingBlock || !L->isLoopExiting(ExitingBlock))
    ExitingBlock = L->getExitingBlock();
  if (ExitingBlock) {
    TripCount = SE.getSmallConstantTripCount(L, ExitingBlock);
    TripMultiple = SE.getSmallConstantTripMultiple(L, ExitingBlock);
  }

  // If the loop contains a convergent operation, the prelude we'd add
  // to do the first few instructions before we hit the unrolled loop
  // is unsafe -- it adds a control-flow dependency to the convergent
  // operation.  Therefore restrict remainder loop (try unrollig without).
  //
  // TODO: This is quite conservative.  In practice, convergent_op()
  // is likely to be called unconditionally in the loop.  In this
  // case, the program would be ill-formed (on most architectures)
  // unless n were the same on all threads in a thread group.
  // Assuming n is the same on all threads, any kind of unrolling is
  // safe.  But currently llvm's notion of convergence isn't powerful
  // enough to express this.
  if (Convergent)
    UP.AllowRemainder = false;

  // Try to find the trip count upper bound if we cannot find the exact trip
  // count.
  bool MaxOrZero = false;
  if (!TripCount) {
    MaxTripCount = SE.getSmallConstantMaxTripCount(L);
    MaxOrZero = SE.isBackedgeTakenCountMaxOrZero(L);
    // We can unroll by the upper bound amount if it's generally allowed or if
    // we know that the loop is executed either the upper bound or zero times.
    // (MaxOrZero unrolling keeps only the first loop test, so the number of
    // loop tests remains the same compared to the non-unrolled version, whereas
    // the generic upper bound unrolling keeps all but the last loop test so the
    // number of loop tests goes up which may end up being worse on targets with
    // constrained branch predictor resources so is controlled by an option.)
    // In addition we only unroll small upper bounds.
    if (!(UP.UpperBound || MaxOrZero) || MaxTripCount > UnrollMaxUpperBound) {
      MaxTripCount = 0;
    }
  }

  // computeUnrollCount() decides whether it is beneficial to use upper bound to
  // fully unroll the loop.
  bool UseUpperBound = false;
  bool IsCountSetExplicitly = computeUnrollCount(
      L, TTI, DT, LI, SE, EphValues, &ORE, TripCount, MaxTripCount,
      TripMultiple, LoopSize, UP, UseUpperBound);
  if (!UP.Count)
    return LoopUnrollResult::Unmodified;
  // Unroll factor (Count) must be less or equal to TripCount.
  if (TripCount && UP.Count > TripCount)
    UP.Count = TripCount;

  // Save loop properties before it is transformed.
  MDNode *OrigLoopID = L->getLoopID();

  // Unroll the loop.
  Loop *RemainderLoop = nullptr;
  LoopUnrollResult UnrollResult = UnrollLoop(
      L, UP.Count, TripCount, UP.Force, UP.Runtime, UP.AllowExpensiveTripCount,
      UseUpperBound, MaxOrZero, TripMultiple, UP.PeelCount, UP.UnrollRemainder,
      LI, &SE, &DT, &AC, &ORE, PreserveLCSSA, &RemainderLoop);
  if (UnrollResult == LoopUnrollResult::Unmodified)
    return LoopUnrollResult::Unmodified;

  if (RemainderLoop) {
    Optional<MDNode *> RemainderLoopID =
        makeFollowupLoopID(OrigLoopID, {LLVMLoopUnrollFollowupAll,
                                        LLVMLoopUnrollFollowupRemainder});
    if (RemainderLoopID.hasValue())
      RemainderLoop->setLoopID(RemainderLoopID.getValue());
  }

  if (UnrollResult != LoopUnrollResult::FullyUnrolled) {
    Optional<MDNode *> NewLoopID =
        makeFollowupLoopID(OrigLoopID, {LLVMLoopUnrollFollowupAll,
                                        LLVMLoopUnrollFollowupUnrolled});
    if (NewLoopID.hasValue()) {
      L->setLoopID(NewLoopID.getValue());

      // Do not setLoopAlreadyUnrolled if loop attributes have been specified
      // explicitly.
      return UnrollResult;
    }
  }

  // If loop has an unroll count pragma or unrolled by explicitly set count
  // mark loop as unrolled to prevent unrolling beyond that requested.
  // If the loop was peeled, we already "used up" the profile information
  // we had, so we don't want to unroll or peel again.
  if (UnrollResult != LoopUnrollResult::FullyUnrolled &&
      (IsCountSetExplicitly || UP.PeelCount))
    L->setLoopAlreadyUnrolled();

  return UnrollResult;
}

namespace {

class LoopUnroll : public LoopPass {
public:
  static char ID; // Pass ID, replacement for typeid

  int OptLevel;

  /// If false, use a cost model to determine whether unrolling of a loop is
  /// profitable. If true, only loops that explicitly request unrolling via
  /// metadata are considered. All other loops are skipped.
  bool OnlyWhenForced;

  Optional<unsigned> ProvidedCount;
  Optional<unsigned> ProvidedThreshold;
  Optional<bool> ProvidedAllowPartial;
  Optional<bool> ProvidedRuntime;
  Optional<bool> ProvidedUpperBound;
  Optional<bool> ProvidedAllowPeeling;

  LoopUnroll(int OptLevel = 2, bool OnlyWhenForced = false,
             Optional<unsigned> Threshold = None,
             Optional<unsigned> Count = None,
             Optional<bool> AllowPartial = None, Optional<bool> Runtime = None,
             Optional<bool> UpperBound = None,
             Optional<bool> AllowPeeling = None)
      : LoopPass(ID), OptLevel(OptLevel), OnlyWhenForced(OnlyWhenForced),
        ProvidedCount(std::move(Count)), ProvidedThreshold(Threshold),
        ProvidedAllowPartial(AllowPartial), ProvidedRuntime(Runtime),
        ProvidedUpperBound(UpperBound), ProvidedAllowPeeling(AllowPeeling) {
    initializeLoopUnrollPass(*PassRegistry::getPassRegistry());
  }

  bool runOnLoop(Loop *L, LPPassManager &LPM) override {
    if (skipLoop(L))
      return false;

    Function &F = *L->getHeader()->getParent();

    auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    LoopInfo *LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    ScalarEvolution &SE = getAnalysis<ScalarEvolutionWrapperPass>().getSE();
    const TargetTransformInfo &TTI =
        getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
    auto &AC = getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
    // For the old PM, we can't use OptimizationRemarkEmitter as an analysis
    // pass.  Function analyses need to be preserved across loop transformations
    // but ORE cannot be preserved (see comment before the pass definition).
    OptimizationRemarkEmitter ORE(&F);
    bool PreserveLCSSA = mustPreserveAnalysisID(LCSSAID);

    LoopUnrollResult Result = tryToUnrollLoop(
        L, DT, LI, SE, TTI, AC, ORE, PreserveLCSSA, OptLevel, OnlyWhenForced,
        ProvidedCount, ProvidedThreshold, ProvidedAllowPartial, ProvidedRuntime,
        ProvidedUpperBound, ProvidedAllowPeeling);

    if (Result == LoopUnrollResult::FullyUnrolled)
      LPM.markLoopAsDeleted(*L);

    return Result != LoopUnrollResult::Unmodified;
  }

  /// This transformation requires natural loop information & requires that
  /// loop preheaders be inserted into the CFG...
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    // FIXME: Loop passes are required to preserve domtree, and for now we just
    // recreate dom info if anything gets unrolled.
    getLoopAnalysisUsage(AU);
  }
};

} // end anonymous namespace

char LoopUnroll::ID = 0;

INITIALIZE_PASS_BEGIN(LoopUnroll, "loop-unroll", "Unroll loops", false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(LoopPass)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_END(LoopUnroll, "loop-unroll", "Unroll loops", false, false)

Pass *llvm::createLoopUnrollPass(int OptLevel, bool OnlyWhenForced,
                                 int Threshold, int Count, int AllowPartial,
                                 int Runtime, int UpperBound,
                                 int AllowPeeling) {
  // TODO: It would make more sense for this function to take the optionals
  // directly, but that's dangerous since it would silently break out of tree
  // callers.
  return new LoopUnroll(
      OptLevel, OnlyWhenForced,
      Threshold == -1 ? None : Optional<unsigned>(Threshold),
      Count == -1 ? None : Optional<unsigned>(Count),
      AllowPartial == -1 ? None : Optional<bool>(AllowPartial),
      Runtime == -1 ? None : Optional<bool>(Runtime),
      UpperBound == -1 ? None : Optional<bool>(UpperBound),
      AllowPeeling == -1 ? None : Optional<bool>(AllowPeeling));
}

Pass *llvm::createSimpleLoopUnrollPass(int OptLevel, bool OnlyWhenForced) {
  return createLoopUnrollPass(OptLevel, OnlyWhenForced, -1, -1, 0, 0, 0, 0);
}

PreservedAnalyses LoopFullUnrollPass::run(Loop &L, LoopAnalysisManager &AM,
                                          LoopStandardAnalysisResults &AR,
                                          LPMUpdater &Updater) {
  const auto &FAM =
      AM.getResult<FunctionAnalysisManagerLoopProxy>(L, AR).getManager();
  Function *F = L.getHeader()->getParent();

  auto *ORE = FAM.getCachedResult<OptimizationRemarkEmitterAnalysis>(*F);
  // FIXME: This should probably be optional rather than required.
  if (!ORE)
    report_fatal_error(
        "LoopFullUnrollPass: OptimizationRemarkEmitterAnalysis not "
        "cached at a higher level");

  // Keep track of the previous loop structure so we can identify new loops
  // created by unrolling.
  Loop *ParentL = L.getParentLoop();
  SmallPtrSet<Loop *, 4> OldLoops;
  if (ParentL)
    OldLoops.insert(ParentL->begin(), ParentL->end());
  else
    OldLoops.insert(AR.LI.begin(), AR.LI.end());

  std::string LoopName = L.getName();

  bool Changed =
      tryToUnrollLoop(&L, AR.DT, &AR.LI, AR.SE, AR.TTI, AR.AC, *ORE,
                      /*PreserveLCSSA*/ true, OptLevel, OnlyWhenForced,
                      /*Count*/ None,
                      /*Threshold*/ None, /*AllowPartial*/ false,
                      /*Runtime*/ false, /*UpperBound*/ false,
                      /*AllowPeeling*/ false) != LoopUnrollResult::Unmodified;
  if (!Changed)
    return PreservedAnalyses::all();

  // The parent must not be damaged by unrolling!
#ifndef NDEBUG
  if (ParentL)
    ParentL->verifyLoop();
#endif

  // Unrolling can do several things to introduce new loops into a loop nest:
  // - Full unrolling clones child loops within the current loop but then
  //   removes the current loop making all of the children appear to be new
  //   sibling loops.
  //
  // When a new loop appears as a sibling loop after fully unrolling,
  // its nesting structure has fundamentally changed and we want to revisit
  // it to reflect that.
  //
  // When unrolling has removed the current loop, we need to tell the
  // infrastructure that it is gone.
  //
  // Finally, we support a debugging/testing mode where we revisit child loops
  // as well. These are not expected to require further optimizations as either
  // they or the loop they were cloned from have been directly visited already.
  // But the debugging mode allows us to check this assumption.
  bool IsCurrentLoopValid = false;
  SmallVector<Loop *, 4> SibLoops;
  if (ParentL)
    SibLoops.append(ParentL->begin(), ParentL->end());
  else
    SibLoops.append(AR.LI.begin(), AR.LI.end());
  erase_if(SibLoops, [&](Loop *SibLoop) {
    if (SibLoop == &L) {
      IsCurrentLoopValid = true;
      return true;
    }

    // Otherwise erase the loop from the list if it was in the old loops.
    return OldLoops.count(SibLoop) != 0;
  });
  Updater.addSiblingLoops(SibLoops);

  if (!IsCurrentLoopValid) {
    Updater.markLoopAsDeleted(L, LoopName);
  } else {
    // We can only walk child loops if the current loop remained valid.
    if (UnrollRevisitChildLoops) {
      // Walk *all* of the child loops.
      SmallVector<Loop *, 4> ChildLoops(L.begin(), L.end());
      Updater.addChildLoops(ChildLoops);
    }
  }

  return getLoopPassPreservedAnalyses();
}

template <typename RangeT>
static SmallVector<Loop *, 8> appendLoopsToWorklist(RangeT &&Loops) {
  SmallVector<Loop *, 8> Worklist;
  // We use an internal worklist to build up the preorder traversal without
  // recursion.
  SmallVector<Loop *, 4> PreOrderLoops, PreOrderWorklist;

  for (Loop *RootL : Loops) {
    assert(PreOrderLoops.empty() && "Must start with an empty preorder walk.");
    assert(PreOrderWorklist.empty() &&
           "Must start with an empty preorder walk worklist.");
    PreOrderWorklist.push_back(RootL);
    do {
      Loop *L = PreOrderWorklist.pop_back_val();
      PreOrderWorklist.append(L->begin(), L->end());
      PreOrderLoops.push_back(L);
    } while (!PreOrderWorklist.empty());

    Worklist.append(PreOrderLoops.begin(), PreOrderLoops.end());
    PreOrderLoops.clear();
  }
  return Worklist;
}

PreservedAnalyses LoopUnrollPass::run(Function &F,
                                      FunctionAnalysisManager &AM) {
  auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
  auto &LI = AM.getResult<LoopAnalysis>(F);
  auto &TTI = AM.getResult<TargetIRAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &AC = AM.getResult<AssumptionAnalysis>(F);
  auto &ORE = AM.getResult<OptimizationRemarkEmitterAnalysis>(F);

  LoopAnalysisManager *LAM = nullptr;
  if (auto *LAMProxy = AM.getCachedResult<LoopAnalysisManagerFunctionProxy>(F))
    LAM = &LAMProxy->getManager();

  const ModuleAnalysisManager &MAM =
      AM.getResult<ModuleAnalysisManagerFunctionProxy>(F).getManager();
  ProfileSummaryInfo *PSI =
      MAM.getCachedResult<ProfileSummaryAnalysis>(*F.getParent());

  bool Changed = false;

  // The unroller requires loops to be in simplified form, and also needs LCSSA.
  // Since simplification may add new inner loops, it has to run before the
  // legality and profitability checks. This means running the loop unroller
  // will simplify all loops, regardless of whether anything end up being
  // unrolled.
  for (auto &L : LI) {
    Changed |= simplifyLoop(L, &DT, &LI, &SE, &AC, false /* PreserveLCSSA */);
    Changed |= formLCSSARecursively(*L, DT, &LI, &SE);
  }

  SmallVector<Loop *, 8> Worklist = appendLoopsToWorklist(LI);

  while (!Worklist.empty()) {
    // Because the LoopInfo stores the loops in RPO, we walk the worklist
    // from back to front so that we work forward across the CFG, which
    // for unrolling is only needed to get optimization remarks emitted in
    // a forward order.
    Loop &L = *Worklist.pop_back_val();
#ifndef NDEBUG
    Loop *ParentL = L.getParentLoop();
#endif

    // Check if the profile summary indicates that the profiled application
    // has a huge working set size, in which case we disable peeling to avoid
    // bloating it further.
    Optional<bool> LocalAllowPeeling = UnrollOpts.AllowPeeling;
    if (PSI && PSI->hasHugeWorkingSetSize())
      LocalAllowPeeling = false;
    std::string LoopName = L.getName();
    // The API here is quite complex to call and we allow to select some
    // flavors of unrolling during construction time (by setting UnrollOpts).
    LoopUnrollResult Result = tryToUnrollLoop(
        &L, DT, &LI, SE, TTI, AC, ORE,
        /*PreserveLCSSA*/ true, UnrollOpts.OptLevel, UnrollOpts.OnlyWhenForced,
        /*Count*/ None,
        /*Threshold*/ None, UnrollOpts.AllowPartial, UnrollOpts.AllowRuntime,
        UnrollOpts.AllowUpperBound, LocalAllowPeeling);
    Changed |= Result != LoopUnrollResult::Unmodified;

    // The parent must not be damaged by unrolling!
#ifndef NDEBUG
    if (Result != LoopUnrollResult::Unmodified && ParentL)
      ParentL->verifyLoop();
#endif

    // Clear any cached analysis results for L if we removed it completely.
    if (LAM && Result == LoopUnrollResult::FullyUnrolled)
      LAM->clear(L, LoopName);
  }

  if (!Changed)
    return PreservedAnalyses::all();

  return getLoopPassPreservedAnalyses();
}
