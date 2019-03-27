//===- PartialInlining.cpp - Inline parts of functions --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass performs partial inlining, typically by inlining an if statement
// that surrounds the body of the function.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/PartialInlining.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/User.h"
#include "llvm/Pass.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <tuple>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "partial-inlining"

STATISTIC(NumPartialInlined,
          "Number of callsites functions partially inlined into.");
STATISTIC(NumColdOutlinePartialInlined, "Number of times functions with "
                                        "cold outlined regions were partially "
                                        "inlined into its caller(s).");
STATISTIC(NumColdRegionsFound,
           "Number of cold single entry/exit regions found.");
STATISTIC(NumColdRegionsOutlined,
           "Number of cold single entry/exit regions outlined.");

// Command line option to disable partial-inlining. The default is false:
static cl::opt<bool>
    DisablePartialInlining("disable-partial-inlining", cl::init(false),
                           cl::Hidden, cl::desc("Disable partial inlining"));
// Command line option to disable multi-region partial-inlining. The default is
// false:
static cl::opt<bool> DisableMultiRegionPartialInline(
    "disable-mr-partial-inlining", cl::init(false), cl::Hidden,
    cl::desc("Disable multi-region partial inlining"));

// Command line option to force outlining in regions with live exit variables.
// The default is false:
static cl::opt<bool>
    ForceLiveExit("pi-force-live-exit-outline", cl::init(false), cl::Hidden,
               cl::desc("Force outline regions with live exits"));

// Command line option to enable marking outline functions with Cold Calling
// Convention. The default is false:
static cl::opt<bool>
    MarkOutlinedColdCC("pi-mark-coldcc", cl::init(false), cl::Hidden,
                       cl::desc("Mark outline function calls with ColdCC"));

#ifndef NDEBUG
// Command line option to debug partial-inlining. The default is none:
static cl::opt<bool> TracePartialInlining("trace-partial-inlining",
                                          cl::init(false), cl::Hidden,
                                          cl::desc("Trace partial inlining."));
#endif

// This is an option used by testing:
static cl::opt<bool> SkipCostAnalysis("skip-partial-inlining-cost-analysis",
                                      cl::init(false), cl::ZeroOrMore,
                                      cl::ReallyHidden,
                                      cl::desc("Skip Cost Analysis"));
// Used to determine if a cold region is worth outlining based on
// its inlining cost compared to the original function.  Default is set at 10%.
// ie. if the cold region reduces the inlining cost of the original function by
// at least 10%.
static cl::opt<float> MinRegionSizeRatio(
    "min-region-size-ratio", cl::init(0.1), cl::Hidden,
    cl::desc("Minimum ratio comparing relative sizes of each "
             "outline candidate and original function"));
// Used to tune the minimum number of execution counts needed in the predecessor
// block to the cold edge. ie. confidence interval.
static cl::opt<unsigned>
    MinBlockCounterExecution("min-block-execution", cl::init(100), cl::Hidden,
                             cl::desc("Minimum block executions to consider "
                                      "its BranchProbabilityInfo valid"));
// Used to determine when an edge is considered cold. Default is set to 10%. ie.
// if the branch probability is 10% or less, then it is deemed as 'cold'.
static cl::opt<float> ColdBranchRatio(
    "cold-branch-ratio", cl::init(0.1), cl::Hidden,
    cl::desc("Minimum BranchProbability to consider a region cold."));

static cl::opt<unsigned> MaxNumInlineBlocks(
    "max-num-inline-blocks", cl::init(5), cl::Hidden,
    cl::desc("Max number of blocks to be partially inlined"));

// Command line option to set the maximum number of partial inlining allowed
// for the module. The default value of -1 means no limit.
static cl::opt<int> MaxNumPartialInlining(
    "max-partial-inlining", cl::init(-1), cl::Hidden, cl::ZeroOrMore,
    cl::desc("Max number of partial inlining. The default is unlimited"));

// Used only when PGO or user annotated branch data is absent. It is
// the least value that is used to weigh the outline region. If BFI
// produces larger value, the BFI value will be used.
static cl::opt<int>
    OutlineRegionFreqPercent("outline-region-freq-percent", cl::init(75),
                             cl::Hidden, cl::ZeroOrMore,
                             cl::desc("Relative frequency of outline region to "
                                      "the entry block"));

static cl::opt<unsigned> ExtraOutliningPenalty(
    "partial-inlining-extra-penalty", cl::init(0), cl::Hidden,
    cl::desc("A debug option to add additional penalty to the computed one."));

namespace {

struct FunctionOutliningInfo {
  FunctionOutliningInfo() = default;

  // Returns the number of blocks to be inlined including all blocks
  // in Entries and one return block.
  unsigned GetNumInlinedBlocks() const { return Entries.size() + 1; }

  // A set of blocks including the function entry that guard
  // the region to be outlined.
  SmallVector<BasicBlock *, 4> Entries;

  // The return block that is not included in the outlined region.
  BasicBlock *ReturnBlock = nullptr;

  // The dominating block of the region to be outlined.
  BasicBlock *NonReturnBlock = nullptr;

  // The set of blocks in Entries that that are predecessors to ReturnBlock
  SmallVector<BasicBlock *, 4> ReturnBlockPreds;
};

struct FunctionOutliningMultiRegionInfo {
  FunctionOutliningMultiRegionInfo()
      : ORI() {}

  // Container for outline regions
  struct OutlineRegionInfo {
    OutlineRegionInfo(SmallVector<BasicBlock *, 8> Region,
                      BasicBlock *EntryBlock, BasicBlock *ExitBlock,
                      BasicBlock *ReturnBlock)
        : Region(Region), EntryBlock(EntryBlock), ExitBlock(ExitBlock),
          ReturnBlock(ReturnBlock) {}
    SmallVector<BasicBlock *, 8> Region;
    BasicBlock *EntryBlock;
    BasicBlock *ExitBlock;
    BasicBlock *ReturnBlock;
  };

  SmallVector<OutlineRegionInfo, 4> ORI;
};

struct PartialInlinerImpl {

  PartialInlinerImpl(
      std::function<AssumptionCache &(Function &)> *GetAC,
      std::function<TargetTransformInfo &(Function &)> *GTTI,
      Optional<function_ref<BlockFrequencyInfo &(Function &)>> GBFI,
      ProfileSummaryInfo *ProfSI)
      : GetAssumptionCache(GetAC), GetTTI(GTTI), GetBFI(GBFI), PSI(ProfSI) {}

  bool run(Module &M);
  // Main part of the transformation that calls helper functions to find
  // outlining candidates, clone & outline the function, and attempt to
  // partially inline the resulting function. Returns true if
  // inlining was successful, false otherwise.  Also returns the outline
  // function (only if we partially inlined early returns) as there is a
  // possibility to further "peel" early return statements that were left in the
  // outline function due to code size.
  std::pair<bool, Function *> unswitchFunction(Function *F);

  // This class speculatively clones the function to be partial inlined.
  // At the end of partial inlining, the remaining callsites to the cloned
  // function that are not partially inlined will be fixed up to reference
  // the original function, and the cloned function will be erased.
  struct FunctionCloner {
    // Two constructors, one for single region outlining, the other for
    // multi-region outlining.
    FunctionCloner(Function *F, FunctionOutliningInfo *OI,
                   OptimizationRemarkEmitter &ORE);
    FunctionCloner(Function *F, FunctionOutliningMultiRegionInfo *OMRI,
                   OptimizationRemarkEmitter &ORE);
    ~FunctionCloner();

    // Prepare for function outlining: making sure there is only
    // one incoming edge from the extracted/outlined region to
    // the return block.
    void NormalizeReturnBlock();

    // Do function outlining for cold regions.
    bool doMultiRegionFunctionOutlining();
    // Do function outlining for region after early return block(s).
    // NOTE: For vararg functions that do the vararg handling in the outlined
    //       function, we temporarily generate IR that does not properly
    //       forward varargs to the outlined function. Calling InlineFunction
    //       will update calls to the outlined functions to properly forward
    //       the varargs.
    Function *doSingleRegionFunctionOutlining();

    Function *OrigFunc = nullptr;
    Function *ClonedFunc = nullptr;

    typedef std::pair<Function *, BasicBlock *> FuncBodyCallerPair;
    // Keep track of Outlined Functions and the basic block they're called from.
    SmallVector<FuncBodyCallerPair, 4> OutlinedFunctions;

    // ClonedFunc is inlined in one of its callers after function
    // outlining.
    bool IsFunctionInlined = false;
    // The cost of the region to be outlined.
    int OutlinedRegionCost = 0;
    // ClonedOI is specific to outlining non-early return blocks.
    std::unique_ptr<FunctionOutliningInfo> ClonedOI = nullptr;
    // ClonedOMRI is specific to outlining cold regions.
    std::unique_ptr<FunctionOutliningMultiRegionInfo> ClonedOMRI = nullptr;
    std::unique_ptr<BlockFrequencyInfo> ClonedFuncBFI = nullptr;
    OptimizationRemarkEmitter &ORE;
  };

private:
  int NumPartialInlining = 0;
  std::function<AssumptionCache &(Function &)> *GetAssumptionCache;
  std::function<TargetTransformInfo &(Function &)> *GetTTI;
  Optional<function_ref<BlockFrequencyInfo &(Function &)>> GetBFI;
  ProfileSummaryInfo *PSI;

  // Return the frequency of the OutlininingBB relative to F's entry point.
  // The result is no larger than 1 and is represented using BP.
  // (Note that the outlined region's 'head' block can only have incoming
  // edges from the guarding entry blocks).
  BranchProbability getOutliningCallBBRelativeFreq(FunctionCloner &Cloner);

  // Return true if the callee of CS should be partially inlined with
  // profit.
  bool shouldPartialInline(CallSite CS, FunctionCloner &Cloner,
                           BlockFrequency WeightedOutliningRcost,
                           OptimizationRemarkEmitter &ORE);

  // Try to inline DuplicateFunction (cloned from F with call to
  // the OutlinedFunction into its callers. Return true
  // if there is any successful inlining.
  bool tryPartialInline(FunctionCloner &Cloner);

  // Compute the mapping from use site of DuplicationFunction to the enclosing
  // BB's profile count.
  void computeCallsiteToProfCountMap(Function *DuplicateFunction,
                                     DenseMap<User *, uint64_t> &SiteCountMap);

  bool IsLimitReached() {
    return (MaxNumPartialInlining != -1 &&
            NumPartialInlining >= MaxNumPartialInlining);
  }

  static CallSite getCallSite(User *U) {
    CallSite CS;
    if (CallInst *CI = dyn_cast<CallInst>(U))
      CS = CallSite(CI);
    else if (InvokeInst *II = dyn_cast<InvokeInst>(U))
      CS = CallSite(II);
    else
      llvm_unreachable("All uses must be calls");
    return CS;
  }

  static CallSite getOneCallSiteTo(Function *F) {
    User *User = *F->user_begin();
    return getCallSite(User);
  }

  std::tuple<DebugLoc, BasicBlock *> getOneDebugLoc(Function *F) {
    CallSite CS = getOneCallSiteTo(F);
    DebugLoc DLoc = CS.getInstruction()->getDebugLoc();
    BasicBlock *Block = CS.getParent();
    return std::make_tuple(DLoc, Block);
  }

  // Returns the costs associated with function outlining:
  // - The first value is the non-weighted runtime cost for making the call
  //   to the outlined function, including the addtional  setup cost in the
  //    outlined function itself;
  // - The second value is the estimated size of the new call sequence in
  //   basic block Cloner.OutliningCallBB;
  std::tuple<int, int> computeOutliningCosts(FunctionCloner &Cloner);

  // Compute the 'InlineCost' of block BB. InlineCost is a proxy used to
  // approximate both the size and runtime cost (Note that in the current
  // inline cost analysis, there is no clear distinction there either).
  static int computeBBInlineCost(BasicBlock *BB);

  std::unique_ptr<FunctionOutliningInfo> computeOutliningInfo(Function *F);
  std::unique_ptr<FunctionOutliningMultiRegionInfo>
  computeOutliningColdRegionsInfo(Function *F, OptimizationRemarkEmitter &ORE);
};

struct PartialInlinerLegacyPass : public ModulePass {
  static char ID; // Pass identification, replacement for typeid

  PartialInlinerLegacyPass() : ModulePass(ID) {
    initializePartialInlinerLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<ProfileSummaryInfoWrapperPass>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
  }

  bool runOnModule(Module &M) override {
    if (skipModule(M))
      return false;

    AssumptionCacheTracker *ACT = &getAnalysis<AssumptionCacheTracker>();
    TargetTransformInfoWrapperPass *TTIWP =
        &getAnalysis<TargetTransformInfoWrapperPass>();
    ProfileSummaryInfo *PSI =
        &getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI();

    std::function<AssumptionCache &(Function &)> GetAssumptionCache =
        [&ACT](Function &F) -> AssumptionCache & {
      return ACT->getAssumptionCache(F);
    };

    std::function<TargetTransformInfo &(Function &)> GetTTI =
        [&TTIWP](Function &F) -> TargetTransformInfo & {
      return TTIWP->getTTI(F);
    };

    return PartialInlinerImpl(&GetAssumptionCache, &GetTTI, NoneType::None, PSI)
        .run(M);
  }
};

} // end anonymous namespace

std::unique_ptr<FunctionOutliningMultiRegionInfo>
PartialInlinerImpl::computeOutliningColdRegionsInfo(Function *F,
                                                    OptimizationRemarkEmitter &ORE) {
  BasicBlock *EntryBlock = &F->front();

  DominatorTree DT(*F);
  LoopInfo LI(DT);
  BranchProbabilityInfo BPI(*F, LI);
  std::unique_ptr<BlockFrequencyInfo> ScopedBFI;
  BlockFrequencyInfo *BFI;
  if (!GetBFI) {
    ScopedBFI.reset(new BlockFrequencyInfo(*F, BPI, LI));
    BFI = ScopedBFI.get();
  } else
    BFI = &(*GetBFI)(*F);

  // Return if we don't have profiling information.
  if (!PSI->hasInstrumentationProfile())
    return std::unique_ptr<FunctionOutliningMultiRegionInfo>();

  std::unique_ptr<FunctionOutliningMultiRegionInfo> OutliningInfo =
      llvm::make_unique<FunctionOutliningMultiRegionInfo>();

  auto IsSingleEntry = [](SmallVectorImpl<BasicBlock *> &BlockList) {
    BasicBlock *Dom = BlockList.front();
    return BlockList.size() > 1 && Dom->hasNPredecessors(1);
  };

  auto IsSingleExit =
      [&ORE](SmallVectorImpl<BasicBlock *> &BlockList) -> BasicBlock * {
    BasicBlock *ExitBlock = nullptr;
    for (auto *Block : BlockList) {
      for (auto SI = succ_begin(Block); SI != succ_end(Block); ++SI) {
        if (!is_contained(BlockList, *SI)) {
          if (ExitBlock) {
            ORE.emit([&]() {
              return OptimizationRemarkMissed(DEBUG_TYPE, "MultiExitRegion",
                                              &SI->front())
                     << "Region dominated by "
                     << ore::NV("Block", BlockList.front()->getName())
                     << " has more than one region exit edge.";
            });
            return nullptr;
          } else
            ExitBlock = Block;
        }
      }
    }
    return ExitBlock;
  };

  auto BBProfileCount = [BFI](BasicBlock *BB) {
    return BFI->getBlockProfileCount(BB)
               ? BFI->getBlockProfileCount(BB).getValue()
               : 0;
  };

  // Use the same computeBBInlineCost function to compute the cost savings of
  // the outlining the candidate region.
  int OverallFunctionCost = 0;
  for (auto &BB : *F)
    OverallFunctionCost += computeBBInlineCost(&BB);

#ifndef NDEBUG
  if (TracePartialInlining)
    dbgs() << "OverallFunctionCost = " << OverallFunctionCost << "\n";
#endif
  int MinOutlineRegionCost =
      static_cast<int>(OverallFunctionCost * MinRegionSizeRatio);
  BranchProbability MinBranchProbability(
      static_cast<int>(ColdBranchRatio * MinBlockCounterExecution),
      MinBlockCounterExecution);
  bool ColdCandidateFound = false;
  BasicBlock *CurrEntry = EntryBlock;
  std::vector<BasicBlock *> DFS;
  DenseMap<BasicBlock *, bool> VisitedMap;
  DFS.push_back(CurrEntry);
  VisitedMap[CurrEntry] = true;
  // Use Depth First Search on the basic blocks to find CFG edges that are
  // considered cold.
  // Cold regions considered must also have its inline cost compared to the
  // overall inline cost of the original function.  The region is outlined only
  // if it reduced the inline cost of the function by 'MinOutlineRegionCost' or
  // more.
  while (!DFS.empty()) {
    auto *thisBB = DFS.back();
    DFS.pop_back();
    // Only consider regions with predecessor blocks that are considered
    // not-cold (default: part of the top 99.99% of all block counters)
    // AND greater than our minimum block execution count (default: 100).
    if (PSI->isColdBlock(thisBB, BFI) ||
        BBProfileCount(thisBB) < MinBlockCounterExecution)
      continue;
    for (auto SI = succ_begin(thisBB); SI != succ_end(thisBB); ++SI) {
      if (VisitedMap[*SI])
        continue;
      VisitedMap[*SI] = true;
      DFS.push_back(*SI);
      // If branch isn't cold, we skip to the next one.
      BranchProbability SuccProb = BPI.getEdgeProbability(thisBB, *SI);
      if (SuccProb > MinBranchProbability)
        continue;
#ifndef NDEBUG
      if (TracePartialInlining) {
        dbgs() << "Found cold edge: " << thisBB->getName() << "->"
               << (*SI)->getName() << "\nBranch Probability = " << SuccProb
               << "\n";
      }
#endif
      SmallVector<BasicBlock *, 8> DominateVector;
      DT.getDescendants(*SI, DominateVector);
      // We can only outline single entry regions (for now).
      if (!IsSingleEntry(DominateVector))
        continue;
      BasicBlock *ExitBlock = nullptr;
      // We can only outline single exit regions (for now).
      if (!(ExitBlock = IsSingleExit(DominateVector)))
        continue;
      int OutlineRegionCost = 0;
      for (auto *BB : DominateVector)
        OutlineRegionCost += computeBBInlineCost(BB);

#ifndef NDEBUG
      if (TracePartialInlining)
        dbgs() << "OutlineRegionCost = " << OutlineRegionCost << "\n";
#endif

      if (OutlineRegionCost < MinOutlineRegionCost) {
        ORE.emit([&]() {
          return OptimizationRemarkAnalysis(DEBUG_TYPE, "TooCostly",
                                            &SI->front())
                 << ore::NV("Callee", F) << " inline cost-savings smaller than "
                 << ore::NV("Cost", MinOutlineRegionCost);
        });
        continue;
      }
      // For now, ignore blocks that belong to a SISE region that is a
      // candidate for outlining.  In the future, we may want to look
      // at inner regions because the outer region may have live-exit
      // variables.
      for (auto *BB : DominateVector)
        VisitedMap[BB] = true;
      // ReturnBlock here means the block after the outline call
      BasicBlock *ReturnBlock = ExitBlock->getSingleSuccessor();
      // assert(ReturnBlock && "ReturnBlock is NULL somehow!");
      FunctionOutliningMultiRegionInfo::OutlineRegionInfo RegInfo(
          DominateVector, DominateVector.front(), ExitBlock, ReturnBlock);
      RegInfo.Region = DominateVector;
      OutliningInfo->ORI.push_back(RegInfo);
#ifndef NDEBUG
      if (TracePartialInlining) {
        dbgs() << "Found Cold Candidate starting at block: "
               << DominateVector.front()->getName() << "\n";
      }
#endif
      ColdCandidateFound = true;
      NumColdRegionsFound++;
    }
  }
  if (ColdCandidateFound)
    return OutliningInfo;
  else
    return std::unique_ptr<FunctionOutliningMultiRegionInfo>();
}

std::unique_ptr<FunctionOutliningInfo>
PartialInlinerImpl::computeOutliningInfo(Function *F) {
  BasicBlock *EntryBlock = &F->front();
  BranchInst *BR = dyn_cast<BranchInst>(EntryBlock->getTerminator());
  if (!BR || BR->isUnconditional())
    return std::unique_ptr<FunctionOutliningInfo>();

  // Returns true if Succ is BB's successor
  auto IsSuccessor = [](BasicBlock *Succ, BasicBlock *BB) {
    return is_contained(successors(BB), Succ);
  };

  auto IsReturnBlock = [](BasicBlock *BB) {
    Instruction *TI = BB->getTerminator();
    return isa<ReturnInst>(TI);
  };

  auto GetReturnBlock = [&](BasicBlock *Succ1, BasicBlock *Succ2) {
    if (IsReturnBlock(Succ1))
      return std::make_tuple(Succ1, Succ2);
    if (IsReturnBlock(Succ2))
      return std::make_tuple(Succ2, Succ1);

    return std::make_tuple<BasicBlock *, BasicBlock *>(nullptr, nullptr);
  };

  // Detect a triangular shape:
  auto GetCommonSucc = [&](BasicBlock *Succ1, BasicBlock *Succ2) {
    if (IsSuccessor(Succ1, Succ2))
      return std::make_tuple(Succ1, Succ2);
    if (IsSuccessor(Succ2, Succ1))
      return std::make_tuple(Succ2, Succ1);

    return std::make_tuple<BasicBlock *, BasicBlock *>(nullptr, nullptr);
  };

  std::unique_ptr<FunctionOutliningInfo> OutliningInfo =
      llvm::make_unique<FunctionOutliningInfo>();

  BasicBlock *CurrEntry = EntryBlock;
  bool CandidateFound = false;
  do {
    // The number of blocks to be inlined has already reached
    // the limit. When MaxNumInlineBlocks is set to 0 or 1, this
    // disables partial inlining for the function.
    if (OutliningInfo->GetNumInlinedBlocks() >= MaxNumInlineBlocks)
      break;

    if (succ_size(CurrEntry) != 2)
      break;

    BasicBlock *Succ1 = *succ_begin(CurrEntry);
    BasicBlock *Succ2 = *(succ_begin(CurrEntry) + 1);

    BasicBlock *ReturnBlock, *NonReturnBlock;
    std::tie(ReturnBlock, NonReturnBlock) = GetReturnBlock(Succ1, Succ2);

    if (ReturnBlock) {
      OutliningInfo->Entries.push_back(CurrEntry);
      OutliningInfo->ReturnBlock = ReturnBlock;
      OutliningInfo->NonReturnBlock = NonReturnBlock;
      CandidateFound = true;
      break;
    }

    BasicBlock *CommSucc;
    BasicBlock *OtherSucc;
    std::tie(CommSucc, OtherSucc) = GetCommonSucc(Succ1, Succ2);

    if (!CommSucc)
      break;

    OutliningInfo->Entries.push_back(CurrEntry);
    CurrEntry = OtherSucc;
  } while (true);

  if (!CandidateFound)
    return std::unique_ptr<FunctionOutliningInfo>();

  // Do sanity check of the entries: threre should not
  // be any successors (not in the entry set) other than
  // {ReturnBlock, NonReturnBlock}
  assert(OutliningInfo->Entries[0] == &F->front() &&
         "Function Entry must be the first in Entries vector");
  DenseSet<BasicBlock *> Entries;
  for (BasicBlock *E : OutliningInfo->Entries)
    Entries.insert(E);

  // Returns true of BB has Predecessor which is not
  // in Entries set.
  auto HasNonEntryPred = [Entries](BasicBlock *BB) {
    for (auto Pred : predecessors(BB)) {
      if (!Entries.count(Pred))
        return true;
    }
    return false;
  };
  auto CheckAndNormalizeCandidate =
      [Entries, HasNonEntryPred](FunctionOutliningInfo *OutliningInfo) {
        for (BasicBlock *E : OutliningInfo->Entries) {
          for (auto Succ : successors(E)) {
            if (Entries.count(Succ))
              continue;
            if (Succ == OutliningInfo->ReturnBlock)
              OutliningInfo->ReturnBlockPreds.push_back(E);
            else if (Succ != OutliningInfo->NonReturnBlock)
              return false;
          }
          // There should not be any outside incoming edges either:
          if (HasNonEntryPred(E))
            return false;
        }
        return true;
      };

  if (!CheckAndNormalizeCandidate(OutliningInfo.get()))
    return std::unique_ptr<FunctionOutliningInfo>();

  // Now further growing the candidate's inlining region by
  // peeling off dominating blocks from the outlining region:
  while (OutliningInfo->GetNumInlinedBlocks() < MaxNumInlineBlocks) {
    BasicBlock *Cand = OutliningInfo->NonReturnBlock;
    if (succ_size(Cand) != 2)
      break;

    if (HasNonEntryPred(Cand))
      break;

    BasicBlock *Succ1 = *succ_begin(Cand);
    BasicBlock *Succ2 = *(succ_begin(Cand) + 1);

    BasicBlock *ReturnBlock, *NonReturnBlock;
    std::tie(ReturnBlock, NonReturnBlock) = GetReturnBlock(Succ1, Succ2);
    if (!ReturnBlock || ReturnBlock != OutliningInfo->ReturnBlock)
      break;

    if (NonReturnBlock->getSinglePredecessor() != Cand)
      break;

    // Now grow and update OutlininigInfo:
    OutliningInfo->Entries.push_back(Cand);
    OutliningInfo->NonReturnBlock = NonReturnBlock;
    OutliningInfo->ReturnBlockPreds.push_back(Cand);
    Entries.insert(Cand);
  }

  return OutliningInfo;
}

// Check if there is PGO data or user annoated branch data:
static bool hasProfileData(Function *F, FunctionOutliningInfo *OI) {
  if (F->hasProfileData())
    return true;
  // Now check if any of the entry block has MD_prof data:
  for (auto *E : OI->Entries) {
    BranchInst *BR = dyn_cast<BranchInst>(E->getTerminator());
    if (!BR || BR->isUnconditional())
      continue;
    uint64_t T, F;
    if (BR->extractProfMetadata(T, F))
      return true;
  }
  return false;
}

BranchProbability
PartialInlinerImpl::getOutliningCallBBRelativeFreq(FunctionCloner &Cloner) {
  BasicBlock *OutliningCallBB = Cloner.OutlinedFunctions.back().second;
  auto EntryFreq =
      Cloner.ClonedFuncBFI->getBlockFreq(&Cloner.ClonedFunc->getEntryBlock());
  auto OutliningCallFreq =
      Cloner.ClonedFuncBFI->getBlockFreq(OutliningCallBB);
  // FIXME Hackery needed because ClonedFuncBFI is based on the function BEFORE
  // we outlined any regions, so we may encounter situations where the
  // OutliningCallFreq is *slightly* bigger than the EntryFreq.
  if (OutliningCallFreq.getFrequency() > EntryFreq.getFrequency()) {
    OutliningCallFreq = EntryFreq;
  }
  auto OutlineRegionRelFreq = BranchProbability::getBranchProbability(
      OutliningCallFreq.getFrequency(), EntryFreq.getFrequency());

  if (hasProfileData(Cloner.OrigFunc, Cloner.ClonedOI.get()))
    return OutlineRegionRelFreq;

  // When profile data is not available, we need to be conservative in
  // estimating the overall savings. Static branch prediction can usually
  // guess the branch direction right (taken/non-taken), but the guessed
  // branch probability is usually not biased enough. In case when the
  // outlined region is predicted to be likely, its probability needs
  // to be made higher (more biased) to not under-estimate the cost of
  // function outlining. On the other hand, if the outlined region
  // is predicted to be less likely, the predicted probablity is usually
  // higher than the actual. For instance, the actual probability of the
  // less likely target is only 5%, but the guessed probablity can be
  // 40%. In the latter case, there is no need for further adjustement.
  // FIXME: add an option for this.
  if (OutlineRegionRelFreq < BranchProbability(45, 100))
    return OutlineRegionRelFreq;

  OutlineRegionRelFreq = std::max(
      OutlineRegionRelFreq, BranchProbability(OutlineRegionFreqPercent, 100));

  return OutlineRegionRelFreq;
}

bool PartialInlinerImpl::shouldPartialInline(
    CallSite CS, FunctionCloner &Cloner,
    BlockFrequency WeightedOutliningRcost,
    OptimizationRemarkEmitter &ORE) {
  using namespace ore;

  Instruction *Call = CS.getInstruction();
  Function *Callee = CS.getCalledFunction();
  assert(Callee == Cloner.ClonedFunc);

  if (SkipCostAnalysis)
    return isInlineViable(*Callee);

  Function *Caller = CS.getCaller();
  auto &CalleeTTI = (*GetTTI)(*Callee);
  InlineCost IC = getInlineCost(CS, getInlineParams(), CalleeTTI,
                                *GetAssumptionCache, GetBFI, PSI, &ORE);

  if (IC.isAlways()) {
    ORE.emit([&]() {
      return OptimizationRemarkAnalysis(DEBUG_TYPE, "AlwaysInline", Call)
             << NV("Callee", Cloner.OrigFunc)
             << " should always be fully inlined, not partially";
    });
    return false;
  }

  if (IC.isNever()) {
    ORE.emit([&]() {
      return OptimizationRemarkMissed(DEBUG_TYPE, "NeverInline", Call)
             << NV("Callee", Cloner.OrigFunc) << " not partially inlined into "
             << NV("Caller", Caller)
             << " because it should never be inlined (cost=never)";
    });
    return false;
  }

  if (!IC) {
    ORE.emit([&]() {
      return OptimizationRemarkAnalysis(DEBUG_TYPE, "TooCostly", Call)
             << NV("Callee", Cloner.OrigFunc) << " not partially inlined into "
             << NV("Caller", Caller) << " because too costly to inline (cost="
             << NV("Cost", IC.getCost()) << ", threshold="
             << NV("Threshold", IC.getCostDelta() + IC.getCost()) << ")";
    });
    return false;
  }
  const DataLayout &DL = Caller->getParent()->getDataLayout();

  // The savings of eliminating the call:
  int NonWeightedSavings = getCallsiteCost(CS, DL);
  BlockFrequency NormWeightedSavings(NonWeightedSavings);

  // Weighted saving is smaller than weighted cost, return false
  if (NormWeightedSavings < WeightedOutliningRcost) {
    ORE.emit([&]() {
      return OptimizationRemarkAnalysis(DEBUG_TYPE, "OutliningCallcostTooHigh",
                                        Call)
             << NV("Callee", Cloner.OrigFunc) << " not partially inlined into "
             << NV("Caller", Caller) << " runtime overhead (overhead="
             << NV("Overhead", (unsigned)WeightedOutliningRcost.getFrequency())
             << ", savings="
             << NV("Savings", (unsigned)NormWeightedSavings.getFrequency())
             << ")"
             << " of making the outlined call is too high";
    });

    return false;
  }

  ORE.emit([&]() {
    return OptimizationRemarkAnalysis(DEBUG_TYPE, "CanBePartiallyInlined", Call)
           << NV("Callee", Cloner.OrigFunc) << " can be partially inlined into "
           << NV("Caller", Caller) << " with cost=" << NV("Cost", IC.getCost())
           << " (threshold="
           << NV("Threshold", IC.getCostDelta() + IC.getCost()) << ")";
  });
  return true;
}

// TODO: Ideally  we should share Inliner's InlineCost Analysis code.
// For now use a simplified version. The returned 'InlineCost' will be used
// to esimate the size cost as well as runtime cost of the BB.
int PartialInlinerImpl::computeBBInlineCost(BasicBlock *BB) {
  int InlineCost = 0;
  const DataLayout &DL = BB->getParent()->getParent()->getDataLayout();
  for (Instruction &I : BB->instructionsWithoutDebug()) {
    // Skip free instructions.
    switch (I.getOpcode()) {
    case Instruction::BitCast:
    case Instruction::PtrToInt:
    case Instruction::IntToPtr:
    case Instruction::Alloca:
    case Instruction::PHI:
      continue;
    case Instruction::GetElementPtr:
      if (cast<GetElementPtrInst>(&I)->hasAllZeroIndices())
        continue;
      break;
    default:
      break;
    }

    if (I.isLifetimeStartOrEnd())
      continue;

    if (CallInst *CI = dyn_cast<CallInst>(&I)) {
      InlineCost += getCallsiteCost(CallSite(CI), DL);
      continue;
    }

    if (InvokeInst *II = dyn_cast<InvokeInst>(&I)) {
      InlineCost += getCallsiteCost(CallSite(II), DL);
      continue;
    }

    if (SwitchInst *SI = dyn_cast<SwitchInst>(&I)) {
      InlineCost += (SI->getNumCases() + 1) * InlineConstants::InstrCost;
      continue;
    }
    InlineCost += InlineConstants::InstrCost;
  }
  return InlineCost;
}

std::tuple<int, int>
PartialInlinerImpl::computeOutliningCosts(FunctionCloner &Cloner) {
  int OutliningFuncCallCost = 0, OutlinedFunctionCost = 0;
  for (auto FuncBBPair : Cloner.OutlinedFunctions) {
    Function *OutlinedFunc = FuncBBPair.first;
    BasicBlock* OutliningCallBB = FuncBBPair.second;
    // Now compute the cost of the call sequence to the outlined function
    // 'OutlinedFunction' in BB 'OutliningCallBB':
    OutliningFuncCallCost += computeBBInlineCost(OutliningCallBB);

    // Now compute the cost of the extracted/outlined function itself:
    for (BasicBlock &BB : *OutlinedFunc)
      OutlinedFunctionCost += computeBBInlineCost(&BB);
  }
  assert(OutlinedFunctionCost >= Cloner.OutlinedRegionCost &&
         "Outlined function cost should be no less than the outlined region");

  // The code extractor introduces a new root and exit stub blocks with
  // additional unconditional branches. Those branches will be eliminated
  // later with bb layout. The cost should be adjusted accordingly:
  OutlinedFunctionCost -=
      2 * InlineConstants::InstrCost * Cloner.OutlinedFunctions.size();

  int OutliningRuntimeOverhead =
      OutliningFuncCallCost +
      (OutlinedFunctionCost - Cloner.OutlinedRegionCost) +
      ExtraOutliningPenalty;

  return std::make_tuple(OutliningFuncCallCost, OutliningRuntimeOverhead);
}

// Create the callsite to profile count map which is
// used to update the original function's entry count,
// after the function is partially inlined into the callsite.
void PartialInlinerImpl::computeCallsiteToProfCountMap(
    Function *DuplicateFunction,
    DenseMap<User *, uint64_t> &CallSiteToProfCountMap) {
  std::vector<User *> Users(DuplicateFunction->user_begin(),
                            DuplicateFunction->user_end());
  Function *CurrentCaller = nullptr;
  std::unique_ptr<BlockFrequencyInfo> TempBFI;
  BlockFrequencyInfo *CurrentCallerBFI = nullptr;

  auto ComputeCurrBFI = [&,this](Function *Caller) {
      // For the old pass manager:
      if (!GetBFI) {
        DominatorTree DT(*Caller);
        LoopInfo LI(DT);
        BranchProbabilityInfo BPI(*Caller, LI);
        TempBFI.reset(new BlockFrequencyInfo(*Caller, BPI, LI));
        CurrentCallerBFI = TempBFI.get();
      } else {
        // New pass manager:
        CurrentCallerBFI = &(*GetBFI)(*Caller);
      }
  };

  for (User *User : Users) {
    CallSite CS = getCallSite(User);
    Function *Caller = CS.getCaller();
    if (CurrentCaller != Caller) {
      CurrentCaller = Caller;
      ComputeCurrBFI(Caller);
    } else {
      assert(CurrentCallerBFI && "CallerBFI is not set");
    }
    BasicBlock *CallBB = CS.getInstruction()->getParent();
    auto Count = CurrentCallerBFI->getBlockProfileCount(CallBB);
    if (Count)
      CallSiteToProfCountMap[User] = *Count;
    else
      CallSiteToProfCountMap[User] = 0;
  }
}

PartialInlinerImpl::FunctionCloner::FunctionCloner(
    Function *F, FunctionOutliningInfo *OI, OptimizationRemarkEmitter &ORE)
    : OrigFunc(F), ORE(ORE) {
  ClonedOI = llvm::make_unique<FunctionOutliningInfo>();

  // Clone the function, so that we can hack away on it.
  ValueToValueMapTy VMap;
  ClonedFunc = CloneFunction(F, VMap);

  ClonedOI->ReturnBlock = cast<BasicBlock>(VMap[OI->ReturnBlock]);
  ClonedOI->NonReturnBlock = cast<BasicBlock>(VMap[OI->NonReturnBlock]);
  for (BasicBlock *BB : OI->Entries) {
    ClonedOI->Entries.push_back(cast<BasicBlock>(VMap[BB]));
  }
  for (BasicBlock *E : OI->ReturnBlockPreds) {
    BasicBlock *NewE = cast<BasicBlock>(VMap[E]);
    ClonedOI->ReturnBlockPreds.push_back(NewE);
  }
  // Go ahead and update all uses to the duplicate, so that we can just
  // use the inliner functionality when we're done hacking.
  F->replaceAllUsesWith(ClonedFunc);
}

PartialInlinerImpl::FunctionCloner::FunctionCloner(
    Function *F, FunctionOutliningMultiRegionInfo *OI,
    OptimizationRemarkEmitter &ORE)
    : OrigFunc(F), ORE(ORE) {
  ClonedOMRI = llvm::make_unique<FunctionOutliningMultiRegionInfo>();

  // Clone the function, so that we can hack away on it.
  ValueToValueMapTy VMap;
  ClonedFunc = CloneFunction(F, VMap);

  // Go through all Outline Candidate Regions and update all BasicBlock
  // information.
  for (FunctionOutliningMultiRegionInfo::OutlineRegionInfo RegionInfo :
       OI->ORI) {
    SmallVector<BasicBlock *, 8> Region;
    for (BasicBlock *BB : RegionInfo.Region) {
      Region.push_back(cast<BasicBlock>(VMap[BB]));
    }
    BasicBlock *NewEntryBlock = cast<BasicBlock>(VMap[RegionInfo.EntryBlock]);
    BasicBlock *NewExitBlock = cast<BasicBlock>(VMap[RegionInfo.ExitBlock]);
    BasicBlock *NewReturnBlock = nullptr;
    if (RegionInfo.ReturnBlock)
      NewReturnBlock = cast<BasicBlock>(VMap[RegionInfo.ReturnBlock]);
    FunctionOutliningMultiRegionInfo::OutlineRegionInfo MappedRegionInfo(
        Region, NewEntryBlock, NewExitBlock, NewReturnBlock);
    ClonedOMRI->ORI.push_back(MappedRegionInfo);
  }
  // Go ahead and update all uses to the duplicate, so that we can just
  // use the inliner functionality when we're done hacking.
  F->replaceAllUsesWith(ClonedFunc);
}

void PartialInlinerImpl::FunctionCloner::NormalizeReturnBlock() {
  auto getFirstPHI = [](BasicBlock *BB) {
    BasicBlock::iterator I = BB->begin();
    PHINode *FirstPhi = nullptr;
    while (I != BB->end()) {
      PHINode *Phi = dyn_cast<PHINode>(I);
      if (!Phi)
        break;
      if (!FirstPhi) {
        FirstPhi = Phi;
        break;
      }
    }
    return FirstPhi;
  };

  // Shouldn't need to normalize PHIs if we're not outlining non-early return
  // blocks.
  if (!ClonedOI)
    return;

  // Special hackery is needed with PHI nodes that have inputs from more than
  // one extracted block.  For simplicity, just split the PHIs into a two-level
  // sequence of PHIs, some of which will go in the extracted region, and some
  // of which will go outside.
  BasicBlock *PreReturn = ClonedOI->ReturnBlock;
  // only split block when necessary:
  PHINode *FirstPhi = getFirstPHI(PreReturn);
  unsigned NumPredsFromEntries = ClonedOI->ReturnBlockPreds.size();

  if (!FirstPhi || FirstPhi->getNumIncomingValues() <= NumPredsFromEntries + 1)
    return;

  auto IsTrivialPhi = [](PHINode *PN) -> Value * {
    Value *CommonValue = PN->getIncomingValue(0);
    if (all_of(PN->incoming_values(),
               [&](Value *V) { return V == CommonValue; }))
      return CommonValue;
    return nullptr;
  };

  ClonedOI->ReturnBlock = ClonedOI->ReturnBlock->splitBasicBlock(
      ClonedOI->ReturnBlock->getFirstNonPHI()->getIterator());
  BasicBlock::iterator I = PreReturn->begin();
  Instruction *Ins = &ClonedOI->ReturnBlock->front();
  SmallVector<Instruction *, 4> DeadPhis;
  while (I != PreReturn->end()) {
    PHINode *OldPhi = dyn_cast<PHINode>(I);
    if (!OldPhi)
      break;

    PHINode *RetPhi =
        PHINode::Create(OldPhi->getType(), NumPredsFromEntries + 1, "", Ins);
    OldPhi->replaceAllUsesWith(RetPhi);
    Ins = ClonedOI->ReturnBlock->getFirstNonPHI();

    RetPhi->addIncoming(&*I, PreReturn);
    for (BasicBlock *E : ClonedOI->ReturnBlockPreds) {
      RetPhi->addIncoming(OldPhi->getIncomingValueForBlock(E), E);
      OldPhi->removeIncomingValue(E);
    }

    // After incoming values splitting, the old phi may become trivial.
    // Keeping the trivial phi can introduce definition inside the outline
    // region which is live-out, causing necessary overhead (load, store
    // arg passing etc).
    if (auto *OldPhiVal = IsTrivialPhi(OldPhi)) {
      OldPhi->replaceAllUsesWith(OldPhiVal);
      DeadPhis.push_back(OldPhi);
    }
    ++I;
  }
  for (auto *DP : DeadPhis)
    DP->eraseFromParent();

  for (auto E : ClonedOI->ReturnBlockPreds) {
    E->getTerminator()->replaceUsesOfWith(PreReturn, ClonedOI->ReturnBlock);
  }
}

bool PartialInlinerImpl::FunctionCloner::doMultiRegionFunctionOutlining() {

  auto ComputeRegionCost = [](SmallVectorImpl<BasicBlock *> &Region) {
    int Cost = 0;
    for (BasicBlock* BB : Region)
      Cost += computeBBInlineCost(BB);
    return Cost;
  };

  assert(ClonedOMRI && "Expecting OutlineInfo for multi region outline");

  if (ClonedOMRI->ORI.empty())
    return false;

  // The CodeExtractor needs a dominator tree.
  DominatorTree DT;
  DT.recalculate(*ClonedFunc);

  // Manually calculate a BlockFrequencyInfo and BranchProbabilityInfo.
  LoopInfo LI(DT);
  BranchProbabilityInfo BPI(*ClonedFunc, LI);
  ClonedFuncBFI.reset(new BlockFrequencyInfo(*ClonedFunc, BPI, LI));

  SetVector<Value *> Inputs, Outputs, Sinks;
  for (FunctionOutliningMultiRegionInfo::OutlineRegionInfo RegionInfo :
       ClonedOMRI->ORI) {
    int CurrentOutlinedRegionCost = ComputeRegionCost(RegionInfo.Region);

    CodeExtractor CE(RegionInfo.Region, &DT, /*AggregateArgs*/ false,
                     ClonedFuncBFI.get(), &BPI, /* AllowVarargs */ false);

    CE.findInputsOutputs(Inputs, Outputs, Sinks);

#ifndef NDEBUG
    if (TracePartialInlining) {
      dbgs() << "inputs: " << Inputs.size() << "\n";
      dbgs() << "outputs: " << Outputs.size() << "\n";
      for (Value *value : Inputs)
        dbgs() << "value used in func: " << *value << "\n";
      for (Value *output : Outputs)
        dbgs() << "instr used in func: " << *output << "\n";
    }
#endif
    // Do not extract regions that have live exit variables.
    if (Outputs.size() > 0 && !ForceLiveExit)
      continue;

    Function *OutlinedFunc = CE.extractCodeRegion();

    if (OutlinedFunc) {
      CallSite OCS = PartialInlinerImpl::getOneCallSiteTo(OutlinedFunc);
      BasicBlock *OutliningCallBB = OCS.getInstruction()->getParent();
      assert(OutliningCallBB->getParent() == ClonedFunc);
      OutlinedFunctions.push_back(std::make_pair(OutlinedFunc,OutliningCallBB));
      NumColdRegionsOutlined++;
      OutlinedRegionCost += CurrentOutlinedRegionCost;

      if (MarkOutlinedColdCC) {
        OutlinedFunc->setCallingConv(CallingConv::Cold);
        OCS.setCallingConv(CallingConv::Cold);
      }
    } else
      ORE.emit([&]() {
        return OptimizationRemarkMissed(DEBUG_TYPE, "ExtractFailed",
                                        &RegionInfo.Region.front()->front())
               << "Failed to extract region at block "
               << ore::NV("Block", RegionInfo.Region.front());
      });
  }

  return !OutlinedFunctions.empty();
}

Function *
PartialInlinerImpl::FunctionCloner::doSingleRegionFunctionOutlining() {
  // Returns true if the block is to be partial inlined into the caller
  // (i.e. not to be extracted to the out of line function)
  auto ToBeInlined = [&, this](BasicBlock *BB) {
    return BB == ClonedOI->ReturnBlock ||
           (std::find(ClonedOI->Entries.begin(), ClonedOI->Entries.end(), BB) !=
            ClonedOI->Entries.end());
  };

  assert(ClonedOI && "Expecting OutlineInfo for single region outline");
  // The CodeExtractor needs a dominator tree.
  DominatorTree DT;
  DT.recalculate(*ClonedFunc);

  // Manually calculate a BlockFrequencyInfo and BranchProbabilityInfo.
  LoopInfo LI(DT);
  BranchProbabilityInfo BPI(*ClonedFunc, LI);
  ClonedFuncBFI.reset(new BlockFrequencyInfo(*ClonedFunc, BPI, LI));

  // Gather up the blocks that we're going to extract.
  std::vector<BasicBlock *> ToExtract;
  ToExtract.push_back(ClonedOI->NonReturnBlock);
  OutlinedRegionCost +=
      PartialInlinerImpl::computeBBInlineCost(ClonedOI->NonReturnBlock);
  for (BasicBlock &BB : *ClonedFunc)
    if (!ToBeInlined(&BB) && &BB != ClonedOI->NonReturnBlock) {
      ToExtract.push_back(&BB);
      // FIXME: the code extractor may hoist/sink more code
      // into the outlined function which may make the outlining
      // overhead (the difference of the outlined function cost
      // and OutliningRegionCost) look larger.
      OutlinedRegionCost += computeBBInlineCost(&BB);
    }

  // Extract the body of the if.
  Function *OutlinedFunc =
      CodeExtractor(ToExtract, &DT, /*AggregateArgs*/ false,
                    ClonedFuncBFI.get(), &BPI,
                    /* AllowVarargs */ true)
          .extractCodeRegion();

  if (OutlinedFunc) {
    BasicBlock *OutliningCallBB =
        PartialInlinerImpl::getOneCallSiteTo(OutlinedFunc)
            .getInstruction()
            ->getParent();
    assert(OutliningCallBB->getParent() == ClonedFunc);
    OutlinedFunctions.push_back(std::make_pair(OutlinedFunc, OutliningCallBB));
  } else
    ORE.emit([&]() {
      return OptimizationRemarkMissed(DEBUG_TYPE, "ExtractFailed",
                                      &ToExtract.front()->front())
             << "Failed to extract region at block "
             << ore::NV("Block", ToExtract.front());
    });

  return OutlinedFunc;
}

PartialInlinerImpl::FunctionCloner::~FunctionCloner() {
  // Ditch the duplicate, since we're done with it, and rewrite all remaining
  // users (function pointers, etc.) back to the original function.
  ClonedFunc->replaceAllUsesWith(OrigFunc);
  ClonedFunc->eraseFromParent();
  if (!IsFunctionInlined) {
    // Remove each function that was speculatively created if there is no
    // reference.
    for (auto FuncBBPair : OutlinedFunctions) {
      Function *Func = FuncBBPair.first;
      Func->eraseFromParent();
    }
  }
}

std::pair<bool, Function *> PartialInlinerImpl::unswitchFunction(Function *F) {

  if (F->hasAddressTaken())
    return {false, nullptr};

  // Let inliner handle it
  if (F->hasFnAttribute(Attribute::AlwaysInline))
    return {false, nullptr};

  if (F->hasFnAttribute(Attribute::NoInline))
    return {false, nullptr};

  if (PSI->isFunctionEntryCold(F))
    return {false, nullptr};

  if (empty(F->users()))
    return {false, nullptr};

  OptimizationRemarkEmitter ORE(F);

  // Only try to outline cold regions if we have a profile summary, which
  // implies we have profiling information.
  if (PSI->hasProfileSummary() && F->hasProfileData() &&
      !DisableMultiRegionPartialInline) {
    std::unique_ptr<FunctionOutliningMultiRegionInfo> OMRI =
        computeOutliningColdRegionsInfo(F, ORE);
    if (OMRI) {
      FunctionCloner Cloner(F, OMRI.get(), ORE);

#ifndef NDEBUG
      if (TracePartialInlining) {
        dbgs() << "HotCountThreshold = " << PSI->getHotCountThreshold() << "\n";
        dbgs() << "ColdCountThreshold = " << PSI->getColdCountThreshold()
               << "\n";
      }
#endif
      bool DidOutline = Cloner.doMultiRegionFunctionOutlining();

      if (DidOutline) {
#ifndef NDEBUG
        if (TracePartialInlining) {
          dbgs() << ">>>>>> Outlined (Cloned) Function >>>>>>\n";
          Cloner.ClonedFunc->print(dbgs());
          dbgs() << "<<<<<< Outlined (Cloned) Function <<<<<<\n";
        }
#endif

        if (tryPartialInline(Cloner))
          return {true, nullptr};
      }
    }
  }

  // Fall-thru to regular partial inlining if we:
  //    i) can't find any cold regions to outline, or
  //   ii) can't inline the outlined function anywhere.
  std::unique_ptr<FunctionOutliningInfo> OI = computeOutliningInfo(F);
  if (!OI)
    return {false, nullptr};

  FunctionCloner Cloner(F, OI.get(), ORE);
  Cloner.NormalizeReturnBlock();

  Function *OutlinedFunction = Cloner.doSingleRegionFunctionOutlining();

  if (!OutlinedFunction)
    return {false, nullptr};

  bool AnyInline = tryPartialInline(Cloner);

  if (AnyInline)
    return {true, OutlinedFunction};

  return {false, nullptr};
}

bool PartialInlinerImpl::tryPartialInline(FunctionCloner &Cloner) {
  if (Cloner.OutlinedFunctions.empty())
    return false;

  int SizeCost = 0;
  BlockFrequency WeightedRcost;
  int NonWeightedRcost;
  std::tie(SizeCost, NonWeightedRcost) = computeOutliningCosts(Cloner);

  // Only calculate RelativeToEntryFreq when we are doing single region
  // outlining.
  BranchProbability RelativeToEntryFreq;
  if (Cloner.ClonedOI) {
    RelativeToEntryFreq = getOutliningCallBBRelativeFreq(Cloner);
  } else
    // RelativeToEntryFreq doesn't make sense when we have more than one
    // outlined call because each call will have a different relative frequency
    // to the entry block.  We can consider using the average, but the
    // usefulness of that information is questionable. For now, assume we never
    // execute the calls to outlined functions.
    RelativeToEntryFreq = BranchProbability(0, 1);

  WeightedRcost = BlockFrequency(NonWeightedRcost) * RelativeToEntryFreq;

  // The call sequence(s) to the outlined function(s) are larger than the sum of
  // the original outlined region size(s), it does not increase the chances of
  // inlining the function with outlining (The inliner uses the size increase to
  // model the cost of inlining a callee).
  if (!SkipCostAnalysis && Cloner.OutlinedRegionCost < SizeCost) {
    OptimizationRemarkEmitter OrigFuncORE(Cloner.OrigFunc);
    DebugLoc DLoc;
    BasicBlock *Block;
    std::tie(DLoc, Block) = getOneDebugLoc(Cloner.ClonedFunc);
    OrigFuncORE.emit([&]() {
      return OptimizationRemarkAnalysis(DEBUG_TYPE, "OutlineRegionTooSmall",
                                        DLoc, Block)
             << ore::NV("Function", Cloner.OrigFunc)
             << " not partially inlined into callers (Original Size = "
             << ore::NV("OutlinedRegionOriginalSize", Cloner.OutlinedRegionCost)
             << ", Size of call sequence to outlined function = "
             << ore::NV("NewSize", SizeCost) << ")";
    });
    return false;
  }

  assert(empty(Cloner.OrigFunc->users()) &&
         "F's users should all be replaced!");

  std::vector<User *> Users(Cloner.ClonedFunc->user_begin(),
                            Cloner.ClonedFunc->user_end());

  DenseMap<User *, uint64_t> CallSiteToProfCountMap;
  auto CalleeEntryCount = Cloner.OrigFunc->getEntryCount();
  if (CalleeEntryCount)
    computeCallsiteToProfCountMap(Cloner.ClonedFunc, CallSiteToProfCountMap);

  uint64_t CalleeEntryCountV =
      (CalleeEntryCount ? CalleeEntryCount.getCount() : 0);

  bool AnyInline = false;
  for (User *User : Users) {
    CallSite CS = getCallSite(User);

    if (IsLimitReached())
      continue;

    OptimizationRemarkEmitter CallerORE(CS.getCaller());
    if (!shouldPartialInline(CS, Cloner, WeightedRcost, CallerORE))
      continue;

    // Construct remark before doing the inlining, as after successful inlining
    // the callsite is removed.
    OptimizationRemark OR(DEBUG_TYPE, "PartiallyInlined", CS.getInstruction());
    OR << ore::NV("Callee", Cloner.OrigFunc) << " partially inlined into "
       << ore::NV("Caller", CS.getCaller());

    InlineFunctionInfo IFI(nullptr, GetAssumptionCache, PSI);
    // We can only forward varargs when we outlined a single region, else we
    // bail on vararg functions.
    if (!InlineFunction(CS, IFI, nullptr, true,
                        (Cloner.ClonedOI ? Cloner.OutlinedFunctions.back().first
                                         : nullptr)))
      continue;

    CallerORE.emit(OR);

    // Now update the entry count:
    if (CalleeEntryCountV && CallSiteToProfCountMap.count(User)) {
      uint64_t CallSiteCount = CallSiteToProfCountMap[User];
      CalleeEntryCountV -= std::min(CalleeEntryCountV, CallSiteCount);
    }

    AnyInline = true;
    NumPartialInlining++;
    // Update the stats
    if (Cloner.ClonedOI)
      NumPartialInlined++;
    else
      NumColdOutlinePartialInlined++;

  }

  if (AnyInline) {
    Cloner.IsFunctionInlined = true;
    if (CalleeEntryCount)
      Cloner.OrigFunc->setEntryCount(
          CalleeEntryCount.setCount(CalleeEntryCountV));
    OptimizationRemarkEmitter OrigFuncORE(Cloner.OrigFunc);
    OrigFuncORE.emit([&]() {
      return OptimizationRemark(DEBUG_TYPE, "PartiallyInlined", Cloner.OrigFunc)
             << "Partially inlined into at least one caller";
    });

  }

  return AnyInline;
}

bool PartialInlinerImpl::run(Module &M) {
  if (DisablePartialInlining)
    return false;

  std::vector<Function *> Worklist;
  Worklist.reserve(M.size());
  for (Function &F : M)
    if (!F.use_empty() && !F.isDeclaration())
      Worklist.push_back(&F);

  bool Changed = false;
  while (!Worklist.empty()) {
    Function *CurrFunc = Worklist.back();
    Worklist.pop_back();

    if (CurrFunc->use_empty())
      continue;

    bool Recursive = false;
    for (User *U : CurrFunc->users())
      if (Instruction *I = dyn_cast<Instruction>(U))
        if (I->getParent()->getParent() == CurrFunc) {
          Recursive = true;
          break;
        }
    if (Recursive)
      continue;

    std::pair<bool, Function * > Result = unswitchFunction(CurrFunc);
    if (Result.second)
      Worklist.push_back(Result.second);
    Changed |= Result.first;
  }

  return Changed;
}

char PartialInlinerLegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(PartialInlinerLegacyPass, "partial-inliner",
                      "Partial Inliner", false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(ProfileSummaryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_END(PartialInlinerLegacyPass, "partial-inliner",
                    "Partial Inliner", false, false)

ModulePass *llvm::createPartialInliningPass() {
  return new PartialInlinerLegacyPass();
}

PreservedAnalyses PartialInlinerPass::run(Module &M,
                                          ModuleAnalysisManager &AM) {
  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  std::function<AssumptionCache &(Function &)> GetAssumptionCache =
      [&FAM](Function &F) -> AssumptionCache & {
    return FAM.getResult<AssumptionAnalysis>(F);
  };

  std::function<BlockFrequencyInfo &(Function &)> GetBFI =
      [&FAM](Function &F) -> BlockFrequencyInfo & {
    return FAM.getResult<BlockFrequencyAnalysis>(F);
  };

  std::function<TargetTransformInfo &(Function &)> GetTTI =
      [&FAM](Function &F) -> TargetTransformInfo & {
    return FAM.getResult<TargetIRAnalysis>(F);
  };

  ProfileSummaryInfo *PSI = &AM.getResult<ProfileSummaryAnalysis>(M);

  if (PartialInlinerImpl(&GetAssumptionCache, &GetTTI, {GetBFI}, PSI)
          .run(M))
    return PreservedAnalyses::none();
  return PreservedAnalyses::all();
}
