//===- PlaceSafepoints.cpp - Place GC Safepoints --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Place garbage collection safepoints at appropriate locations in the IR. This
// does not make relocation semantics or variable liveness explicit.  That's
// done by RewriteStatepointsForGC.
//
// Terminology:
// - A call is said to be "parseable" if there is a stack map generated for the
// return PC of the call.  A runtime can determine where values listed in the
// deopt arguments and (after RewriteStatepointsForGC) gc arguments are located
// on the stack when the code is suspended inside such a call.  Every parse
// point is represented by a call wrapped in an gc.statepoint intrinsic.
// - A "poll" is an explicit check in the generated code to determine if the
// runtime needs the generated code to cooperate by calling a helper routine
// and thus suspending its execution at a known state. The call to the helper
// routine will be parseable.  The (gc & runtime specific) logic of a poll is
// assumed to be provided in a function of the name "gc.safepoint_poll".
//
// We aim to insert polls such that running code can quickly be brought to a
// well defined state for inspection by the collector.  In the current
// implementation, this is done via the insertion of poll sites at method entry
// and the backedge of most loops.  We try to avoid inserting more polls than
// are necessary to ensure a finite period between poll sites.  This is not
// because the poll itself is expensive in the generated code; it's not.  Polls
// do tend to impact the optimizer itself in negative ways; we'd like to avoid
// perturbing the optimization of the method as much as we can.
//
// We also need to make most call sites parseable.  The callee might execute a
// poll (or otherwise be inspected by the GC).  If so, the entire stack
// (including the suspended frame of the current method) must be parseable.
//
// This pass will insert:
// - Call parse points ("call safepoints") for any call which may need to
// reach a safepoint during the execution of the callee function.
// - Backedge safepoint polls and entry safepoint polls to ensure that
// executing code reaches a safepoint poll in a finite amount of time.
//
// We do not currently support return statepoints, but adding them would not
// be hard.  They are not required for correctness - entry safepoints are an
// alternative - but some GCs may prefer them.  Patches welcome.
//
//===----------------------------------------------------------------------===//

#include "llvm/Pass.h"

#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Statepoint.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

#define DEBUG_TYPE "safepoint-placement"

STATISTIC(NumEntrySafepoints, "Number of entry safepoints inserted");
STATISTIC(NumBackedgeSafepoints, "Number of backedge safepoints inserted");

STATISTIC(CallInLoop,
          "Number of loops without safepoints due to calls in loop");
STATISTIC(FiniteExecution,
          "Number of loops without safepoints finite execution");

using namespace llvm;

// Ignore opportunities to avoid placing safepoints on backedges, useful for
// validation
static cl::opt<bool> AllBackedges("spp-all-backedges", cl::Hidden,
                                  cl::init(false));

/// How narrow does the trip count of a loop have to be to have to be considered
/// "counted"?  Counted loops do not get safepoints at backedges.
static cl::opt<int> CountedLoopTripWidth("spp-counted-loop-trip-width",
                                         cl::Hidden, cl::init(32));

// If true, split the backedge of a loop when placing the safepoint, otherwise
// split the latch block itself.  Both are useful to support for
// experimentation, but in practice, it looks like splitting the backedge
// optimizes better.
static cl::opt<bool> SplitBackedge("spp-split-backedge", cl::Hidden,
                                   cl::init(false));

namespace {

/// An analysis pass whose purpose is to identify each of the backedges in
/// the function which require a safepoint poll to be inserted.
struct PlaceBackedgeSafepointsImpl : public FunctionPass {
  static char ID;

  /// The output of the pass - gives a list of each backedge (described by
  /// pointing at the branch) which need a poll inserted.
  std::vector<Instruction *> PollLocations;

  /// True unless we're running spp-no-calls in which case we need to disable
  /// the call-dependent placement opts.
  bool CallSafepointsEnabled;

  ScalarEvolution *SE = nullptr;
  DominatorTree *DT = nullptr;
  LoopInfo *LI = nullptr;
  TargetLibraryInfo *TLI = nullptr;

  PlaceBackedgeSafepointsImpl(bool CallSafepoints = false)
      : FunctionPass(ID), CallSafepointsEnabled(CallSafepoints) {
    initializePlaceBackedgeSafepointsImplPass(*PassRegistry::getPassRegistry());
  }

  bool runOnLoop(Loop *);
  void runOnLoopAndSubLoops(Loop *L) {
    // Visit all the subloops
    for (Loop *I : *L)
      runOnLoopAndSubLoops(I);
    runOnLoop(L);
  }

  bool runOnFunction(Function &F) override {
    SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
    DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    TLI = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
    for (Loop *I : *LI) {
      runOnLoopAndSubLoops(I);
    }
    return false;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<ScalarEvolutionWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    // We no longer modify the IR at all in this pass.  Thus all
    // analysis are preserved.
    AU.setPreservesAll();
  }
};
}

static cl::opt<bool> NoEntry("spp-no-entry", cl::Hidden, cl::init(false));
static cl::opt<bool> NoCall("spp-no-call", cl::Hidden, cl::init(false));
static cl::opt<bool> NoBackedge("spp-no-backedge", cl::Hidden, cl::init(false));

namespace {
struct PlaceSafepoints : public FunctionPass {
  static char ID; // Pass identification, replacement for typeid

  PlaceSafepoints() : FunctionPass(ID) {
    initializePlaceSafepointsPass(*PassRegistry::getPassRegistry());
  }
  bool runOnFunction(Function &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    // We modify the graph wholesale (inlining, block insertion, etc).  We
    // preserve nothing at the moment.  We could potentially preserve dom tree
    // if that was worth doing
    AU.addRequired<TargetLibraryInfoWrapperPass>();
  }
};
}

// Insert a safepoint poll immediately before the given instruction.  Does
// not handle the parsability of state at the runtime call, that's the
// callers job.
static void
InsertSafepointPoll(Instruction *InsertBefore,
                    std::vector<CallSite> &ParsePointsNeeded /*rval*/,
                    const TargetLibraryInfo &TLI);

static bool needsStatepoint(const CallSite &CS, const TargetLibraryInfo &TLI) {
  if (callsGCLeafFunction(CS, TLI))
    return false;
  if (CS.isCall()) {
    CallInst *call = cast<CallInst>(CS.getInstruction());
    if (call->isInlineAsm())
      return false;
  }

  return !(isStatepoint(CS) || isGCRelocate(CS) || isGCResult(CS));
}

/// Returns true if this loop is known to contain a call safepoint which
/// must unconditionally execute on any iteration of the loop which returns
/// to the loop header via an edge from Pred.  Returns a conservative correct
/// answer; i.e. false is always valid.
static bool containsUnconditionalCallSafepoint(Loop *L, BasicBlock *Header,
                                               BasicBlock *Pred,
                                               DominatorTree &DT,
                                               const TargetLibraryInfo &TLI) {
  // In general, we're looking for any cut of the graph which ensures
  // there's a call safepoint along every edge between Header and Pred.
  // For the moment, we look only for the 'cuts' that consist of a single call
  // instruction in a block which is dominated by the Header and dominates the
  // loop latch (Pred) block.  Somewhat surprisingly, walking the entire chain
  // of such dominating blocks gets substantially more occurrences than just
  // checking the Pred and Header blocks themselves.  This may be due to the
  // density of loop exit conditions caused by range and null checks.
  // TODO: structure this as an analysis pass, cache the result for subloops,
  // avoid dom tree recalculations
  assert(DT.dominates(Header, Pred) && "loop latch not dominated by header?");

  BasicBlock *Current = Pred;
  while (true) {
    for (Instruction &I : *Current) {
      if (auto CS = CallSite(&I))
        // Note: Technically, needing a safepoint isn't quite the right
        // condition here.  We should instead be checking if the target method
        // has an
        // unconditional poll. In practice, this is only a theoretical concern
        // since we don't have any methods with conditional-only safepoint
        // polls.
        if (needsStatepoint(CS, TLI))
          return true;
    }

    if (Current == Header)
      break;
    Current = DT.getNode(Current)->getIDom()->getBlock();
  }

  return false;
}

/// Returns true if this loop is known to terminate in a finite number of
/// iterations.  Note that this function may return false for a loop which
/// does actual terminate in a finite constant number of iterations due to
/// conservatism in the analysis.
static bool mustBeFiniteCountedLoop(Loop *L, ScalarEvolution *SE,
                                    BasicBlock *Pred) {
  // A conservative bound on the loop as a whole.
  const SCEV *MaxTrips = SE->getMaxBackedgeTakenCount(L);
  if (MaxTrips != SE->getCouldNotCompute() &&
      SE->getUnsignedRange(MaxTrips).getUnsignedMax().isIntN(
          CountedLoopTripWidth))
    return true;

  // If this is a conditional branch to the header with the alternate path
  // being outside the loop, we can ask questions about the execution frequency
  // of the exit block.
  if (L->isLoopExiting(Pred)) {
    // This returns an exact expression only.  TODO: We really only need an
    // upper bound here, but SE doesn't expose that.
    const SCEV *MaxExec = SE->getExitCount(L, Pred);
    if (MaxExec != SE->getCouldNotCompute() &&
        SE->getUnsignedRange(MaxExec).getUnsignedMax().isIntN(
            CountedLoopTripWidth))
        return true;
  }

  return /* not finite */ false;
}

static void scanOneBB(Instruction *Start, Instruction *End,
                      std::vector<CallInst *> &Calls,
                      DenseSet<BasicBlock *> &Seen,
                      std::vector<BasicBlock *> &Worklist) {
  for (BasicBlock::iterator BBI(Start), BBE0 = Start->getParent()->end(),
                                        BBE1 = BasicBlock::iterator(End);
       BBI != BBE0 && BBI != BBE1; BBI++) {
    if (CallInst *CI = dyn_cast<CallInst>(&*BBI))
      Calls.push_back(CI);

    // FIXME: This code does not handle invokes
    assert(!isa<InvokeInst>(&*BBI) &&
           "support for invokes in poll code needed");

    // Only add the successor blocks if we reach the terminator instruction
    // without encountering end first
    if (BBI->isTerminator()) {
      BasicBlock *BB = BBI->getParent();
      for (BasicBlock *Succ : successors(BB)) {
        if (Seen.insert(Succ).second) {
          Worklist.push_back(Succ);
        }
      }
    }
  }
}

static void scanInlinedCode(Instruction *Start, Instruction *End,
                            std::vector<CallInst *> &Calls,
                            DenseSet<BasicBlock *> &Seen) {
  Calls.clear();
  std::vector<BasicBlock *> Worklist;
  Seen.insert(Start->getParent());
  scanOneBB(Start, End, Calls, Seen, Worklist);
  while (!Worklist.empty()) {
    BasicBlock *BB = Worklist.back();
    Worklist.pop_back();
    scanOneBB(&*BB->begin(), End, Calls, Seen, Worklist);
  }
}

bool PlaceBackedgeSafepointsImpl::runOnLoop(Loop *L) {
  // Loop through all loop latches (branches controlling backedges).  We need
  // to place a safepoint on every backedge (potentially).
  // Note: In common usage, there will be only one edge due to LoopSimplify
  // having run sometime earlier in the pipeline, but this code must be correct
  // w.r.t. loops with multiple backedges.
  BasicBlock *Header = L->getHeader();
  SmallVector<BasicBlock*, 16> LoopLatches;
  L->getLoopLatches(LoopLatches);
  for (BasicBlock *Pred : LoopLatches) {
    assert(L->contains(Pred));

    // Make a policy decision about whether this loop needs a safepoint or
    // not.  Note that this is about unburdening the optimizer in loops, not
    // avoiding the runtime cost of the actual safepoint.
    if (!AllBackedges) {
      if (mustBeFiniteCountedLoop(L, SE, Pred)) {
        LLVM_DEBUG(dbgs() << "skipping safepoint placement in finite loop\n");
        FiniteExecution++;
        continue;
      }
      if (CallSafepointsEnabled &&
          containsUnconditionalCallSafepoint(L, Header, Pred, *DT, *TLI)) {
        // Note: This is only semantically legal since we won't do any further
        // IPO or inlining before the actual call insertion..  If we hadn't, we
        // might latter loose this call safepoint.
        LLVM_DEBUG(
            dbgs()
            << "skipping safepoint placement due to unconditional call\n");
        CallInLoop++;
        continue;
      }
    }

    // TODO: We can create an inner loop which runs a finite number of
    // iterations with an outer loop which contains a safepoint.  This would
    // not help runtime performance that much, but it might help our ability to
    // optimize the inner loop.

    // Safepoint insertion would involve creating a new basic block (as the
    // target of the current backedge) which does the safepoint (of all live
    // variables) and branches to the true header
    Instruction *Term = Pred->getTerminator();

    LLVM_DEBUG(dbgs() << "[LSP] terminator instruction: " << *Term);

    PollLocations.push_back(Term);
  }

  return false;
}

/// Returns true if an entry safepoint is not required before this callsite in
/// the caller function.
static bool doesNotRequireEntrySafepointBefore(const CallSite &CS) {
  Instruction *Inst = CS.getInstruction();
  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(Inst)) {
    switch (II->getIntrinsicID()) {
    case Intrinsic::experimental_gc_statepoint:
    case Intrinsic::experimental_patchpoint_void:
    case Intrinsic::experimental_patchpoint_i64:
      // The can wrap an actual call which may grow the stack by an unbounded
      // amount or run forever.
      return false;
    default:
      // Most LLVM intrinsics are things which do not expand to actual calls, or
      // at least if they do, are leaf functions that cause only finite stack
      // growth.  In particular, the optimizer likes to form things like memsets
      // out of stores in the original IR.  Another important example is
      // llvm.localescape which must occur in the entry block.  Inserting a
      // safepoint before it is not legal since it could push the localescape
      // out of the entry block.
      return true;
    }
  }
  return false;
}

static Instruction *findLocationForEntrySafepoint(Function &F,
                                                  DominatorTree &DT) {

  // Conceptually, this poll needs to be on method entry, but in
  // practice, we place it as late in the entry block as possible.  We
  // can place it as late as we want as long as it dominates all calls
  // that can grow the stack.  This, combined with backedge polls,
  // give us all the progress guarantees we need.

  // hasNextInstruction and nextInstruction are used to iterate
  // through a "straight line" execution sequence.

  auto HasNextInstruction = [](Instruction *I) {
    if (!I->isTerminator())
      return true;

    BasicBlock *nextBB = I->getParent()->getUniqueSuccessor();
    return nextBB && (nextBB->getUniquePredecessor() != nullptr);
  };

  auto NextInstruction = [&](Instruction *I) {
    assert(HasNextInstruction(I) &&
           "first check if there is a next instruction!");

    if (I->isTerminator())
      return &I->getParent()->getUniqueSuccessor()->front();
    return &*++I->getIterator();
  };

  Instruction *Cursor = nullptr;
  for (Cursor = &F.getEntryBlock().front(); HasNextInstruction(Cursor);
       Cursor = NextInstruction(Cursor)) {

    // We need to ensure a safepoint poll occurs before any 'real' call.  The
    // easiest way to ensure finite execution between safepoints in the face of
    // recursive and mutually recursive functions is to enforce that each take
    // a safepoint.  Additionally, we need to ensure a poll before any call
    // which can grow the stack by an unbounded amount.  This isn't required
    // for GC semantics per se, but is a common requirement for languages
    // which detect stack overflow via guard pages and then throw exceptions.
    if (auto CS = CallSite(Cursor)) {
      if (doesNotRequireEntrySafepointBefore(CS))
        continue;
      break;
    }
  }

  assert((HasNextInstruction(Cursor) || Cursor->isTerminator()) &&
         "either we stopped because of a call, or because of terminator");

  return Cursor;
}

static const char *const GCSafepointPollName = "gc.safepoint_poll";

static bool isGCSafepointPoll(Function &F) {
  return F.getName().equals(GCSafepointPollName);
}

/// Returns true if this function should be rewritten to include safepoint
/// polls and parseable call sites.  The main point of this function is to be
/// an extension point for custom logic.
static bool shouldRewriteFunction(Function &F) {
  // TODO: This should check the GCStrategy
  if (F.hasGC()) {
    const auto &FunctionGCName = F.getGC();
    const StringRef StatepointExampleName("statepoint-example");
    const StringRef CoreCLRName("coreclr");
    return (StatepointExampleName == FunctionGCName) ||
           (CoreCLRName == FunctionGCName);
  } else
    return false;
}

// TODO: These should become properties of the GCStrategy, possibly with
// command line overrides.
static bool enableEntrySafepoints(Function &F) { return !NoEntry; }
static bool enableBackedgeSafepoints(Function &F) { return !NoBackedge; }
static bool enableCallSafepoints(Function &F) { return !NoCall; }

bool PlaceSafepoints::runOnFunction(Function &F) {
  if (F.isDeclaration() || F.empty()) {
    // This is a declaration, nothing to do.  Must exit early to avoid crash in
    // dom tree calculation
    return false;
  }

  if (isGCSafepointPoll(F)) {
    // Given we're inlining this inside of safepoint poll insertion, this
    // doesn't make any sense.  Note that we do make any contained calls
    // parseable after we inline a poll.
    return false;
  }

  if (!shouldRewriteFunction(F))
    return false;

  const TargetLibraryInfo &TLI =
      getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();

  bool Modified = false;

  // In various bits below, we rely on the fact that uses are reachable from
  // defs.  When there are basic blocks unreachable from the entry, dominance
  // and reachablity queries return non-sensical results.  Thus, we preprocess
  // the function to ensure these properties hold.
  Modified |= removeUnreachableBlocks(F);

  // STEP 1 - Insert the safepoint polling locations.  We do not need to
  // actually insert parse points yet.  That will be done for all polls and
  // calls in a single pass.

  DominatorTree DT;
  DT.recalculate(F);

  SmallVector<Instruction *, 16> PollsNeeded;
  std::vector<CallSite> ParsePointNeeded;

  if (enableBackedgeSafepoints(F)) {
    // Construct a pass manager to run the LoopPass backedge logic.  We
    // need the pass manager to handle scheduling all the loop passes
    // appropriately.  Doing this by hand is painful and just not worth messing
    // with for the moment.
    legacy::FunctionPassManager FPM(F.getParent());
    bool CanAssumeCallSafepoints = enableCallSafepoints(F);
    auto *PBS = new PlaceBackedgeSafepointsImpl(CanAssumeCallSafepoints);
    FPM.add(PBS);
    FPM.run(F);

    // We preserve dominance information when inserting the poll, otherwise
    // we'd have to recalculate this on every insert
    DT.recalculate(F);

    auto &PollLocations = PBS->PollLocations;

    auto OrderByBBName = [](Instruction *a, Instruction *b) {
      return a->getParent()->getName() < b->getParent()->getName();
    };
    // We need the order of list to be stable so that naming ends up stable
    // when we split edges.  This makes test cases much easier to write.
    llvm::sort(PollLocations, OrderByBBName);

    // We can sometimes end up with duplicate poll locations.  This happens if
    // a single loop is visited more than once.   The fact this happens seems
    // wrong, but it does happen for the split-backedge.ll test case.
    PollLocations.erase(std::unique(PollLocations.begin(),
                                    PollLocations.end()),
                        PollLocations.end());

    // Insert a poll at each point the analysis pass identified
    // The poll location must be the terminator of a loop latch block.
    for (Instruction *Term : PollLocations) {
      // We are inserting a poll, the function is modified
      Modified = true;

      if (SplitBackedge) {
        // Split the backedge of the loop and insert the poll within that new
        // basic block.  This creates a loop with two latches per original
        // latch (which is non-ideal), but this appears to be easier to
        // optimize in practice than inserting the poll immediately before the
        // latch test.

        // Since this is a latch, at least one of the successors must dominate
        // it. Its possible that we have a) duplicate edges to the same header
        // and b) edges to distinct loop headers.  We need to insert pools on
        // each.
        SetVector<BasicBlock *> Headers;
        for (unsigned i = 0; i < Term->getNumSuccessors(); i++) {
          BasicBlock *Succ = Term->getSuccessor(i);
          if (DT.dominates(Succ, Term->getParent())) {
            Headers.insert(Succ);
          }
        }
        assert(!Headers.empty() && "poll location is not a loop latch?");

        // The split loop structure here is so that we only need to recalculate
        // the dominator tree once.  Alternatively, we could just keep it up to
        // date and use a more natural merged loop.
        SetVector<BasicBlock *> SplitBackedges;
        for (BasicBlock *Header : Headers) {
          BasicBlock *NewBB = SplitEdge(Term->getParent(), Header, &DT);
          PollsNeeded.push_back(NewBB->getTerminator());
          NumBackedgeSafepoints++;
        }
      } else {
        // Split the latch block itself, right before the terminator.
        PollsNeeded.push_back(Term);
        NumBackedgeSafepoints++;
      }
    }
  }

  if (enableEntrySafepoints(F)) {
    if (Instruction *Location = findLocationForEntrySafepoint(F, DT)) {
      PollsNeeded.push_back(Location);
      Modified = true;
      NumEntrySafepoints++;
    }
    // TODO: else we should assert that there was, in fact, a policy choice to
    // not insert a entry safepoint poll.
  }

  // Now that we've identified all the needed safepoint poll locations, insert
  // safepoint polls themselves.
  for (Instruction *PollLocation : PollsNeeded) {
    std::vector<CallSite> RuntimeCalls;
    InsertSafepointPoll(PollLocation, RuntimeCalls, TLI);
    ParsePointNeeded.insert(ParsePointNeeded.end(), RuntimeCalls.begin(),
                            RuntimeCalls.end());
  }

  return Modified;
}

char PlaceBackedgeSafepointsImpl::ID = 0;
char PlaceSafepoints::ID = 0;

FunctionPass *llvm::createPlaceSafepointsPass() {
  return new PlaceSafepoints();
}

INITIALIZE_PASS_BEGIN(PlaceBackedgeSafepointsImpl,
                      "place-backedge-safepoints-impl",
                      "Place Backedge Safepoints", false, false)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(PlaceBackedgeSafepointsImpl,
                    "place-backedge-safepoints-impl",
                    "Place Backedge Safepoints", false, false)

INITIALIZE_PASS_BEGIN(PlaceSafepoints, "place-safepoints", "Place Safepoints",
                      false, false)
INITIALIZE_PASS_END(PlaceSafepoints, "place-safepoints", "Place Safepoints",
                    false, false)

static void
InsertSafepointPoll(Instruction *InsertBefore,
                    std::vector<CallSite> &ParsePointsNeeded /*rval*/,
                    const TargetLibraryInfo &TLI) {
  BasicBlock *OrigBB = InsertBefore->getParent();
  Module *M = InsertBefore->getModule();
  assert(M && "must be part of a module");

  // Inline the safepoint poll implementation - this will get all the branch,
  // control flow, etc..  Most importantly, it will introduce the actual slow
  // path call - where we need to insert a safepoint (parsepoint).

  auto *F = M->getFunction(GCSafepointPollName);
  assert(F && "gc.safepoint_poll function is missing");
  assert(F->getValueType() ==
         FunctionType::get(Type::getVoidTy(M->getContext()), false) &&
         "gc.safepoint_poll declared with wrong type");
  assert(!F->empty() && "gc.safepoint_poll must be a non-empty function");
  CallInst *PollCall = CallInst::Create(F, "", InsertBefore);

  // Record some information about the call site we're replacing
  BasicBlock::iterator Before(PollCall), After(PollCall);
  bool IsBegin = false;
  if (Before == OrigBB->begin())
    IsBegin = true;
  else
    Before--;

  After++;
  assert(After != OrigBB->end() && "must have successor");

  // Do the actual inlining
  InlineFunctionInfo IFI;
  bool InlineStatus = InlineFunction(PollCall, IFI);
  assert(InlineStatus && "inline must succeed");
  (void)InlineStatus; // suppress warning in release-asserts

  // Check post-conditions
  assert(IFI.StaticAllocas.empty() && "can't have allocs");

  std::vector<CallInst *> Calls; // new calls
  DenseSet<BasicBlock *> BBs;    // new BBs + insertee

  // Include only the newly inserted instructions, Note: begin may not be valid
  // if we inserted to the beginning of the basic block
  BasicBlock::iterator Start = IsBegin ? OrigBB->begin() : std::next(Before);

  // If your poll function includes an unreachable at the end, that's not
  // valid.  Bugpoint likes to create this, so check for it.
  assert(isPotentiallyReachable(&*Start, &*After) &&
         "malformed poll function");

  scanInlinedCode(&*Start, &*After, Calls, BBs);
  assert(!Calls.empty() && "slow path not found for safepoint poll");

  // Record the fact we need a parsable state at the runtime call contained in
  // the poll function.  This is required so that the runtime knows how to
  // parse the last frame when we actually take  the safepoint (i.e. execute
  // the slow path)
  assert(ParsePointsNeeded.empty());
  for (auto *CI : Calls) {
    // No safepoint needed or wanted
    if (!needsStatepoint(CI, TLI))
      continue;

    // These are likely runtime calls.  Should we assert that via calling
    // convention or something?
    ParsePointsNeeded.push_back(CallSite(CI));
  }
  assert(ParsePointsNeeded.size() <= Calls.size());
}
