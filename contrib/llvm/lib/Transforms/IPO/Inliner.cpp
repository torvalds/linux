//===- Inliner.cpp - Code common to all inliners --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the mechanics required to implement inlining without
// missing any calls and updating the call graph.  The decisions of which calls
// are profitable to inline are implemented elsewhere.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/Inliner.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ImportedFunctionsInliningStatistics.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <sstream>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "inline"

STATISTIC(NumInlined, "Number of functions inlined");
STATISTIC(NumCallsDeleted, "Number of call sites deleted, not inlined");
STATISTIC(NumDeleted, "Number of functions deleted because all callers found");
STATISTIC(NumMergedAllocas, "Number of allocas merged together");

// This weirdly named statistic tracks the number of times that, when attempting
// to inline a function A into B, we analyze the callers of B in order to see
// if those would be more profitable and blocked inline steps.
STATISTIC(NumCallerCallersAnalyzed, "Number of caller-callers analyzed");

/// Flag to disable manual alloca merging.
///
/// Merging of allocas was originally done as a stack-size saving technique
/// prior to LLVM's code generator having support for stack coloring based on
/// lifetime markers. It is now in the process of being removed. To experiment
/// with disabling it and relying fully on lifetime marker based stack
/// coloring, you can pass this flag to LLVM.
static cl::opt<bool>
    DisableInlinedAllocaMerging("disable-inlined-alloca-merging",
                                cl::init(false), cl::Hidden);

namespace {

enum class InlinerFunctionImportStatsOpts {
  No = 0,
  Basic = 1,
  Verbose = 2,
};

} // end anonymous namespace

static cl::opt<InlinerFunctionImportStatsOpts> InlinerFunctionImportStats(
    "inliner-function-import-stats",
    cl::init(InlinerFunctionImportStatsOpts::No),
    cl::values(clEnumValN(InlinerFunctionImportStatsOpts::Basic, "basic",
                          "basic statistics"),
               clEnumValN(InlinerFunctionImportStatsOpts::Verbose, "verbose",
                          "printing of statistics for each inlined function")),
    cl::Hidden, cl::desc("Enable inliner stats for imported functions"));

/// Flag to add inline messages as callsite attributes 'inline-remark'.
static cl::opt<bool>
    InlineRemarkAttribute("inline-remark-attribute", cl::init(false),
                          cl::Hidden,
                          cl::desc("Enable adding inline-remark attribute to"
                                   " callsites processed by inliner but decided"
                                   " to be not inlined"));

LegacyInlinerBase::LegacyInlinerBase(char &ID) : CallGraphSCCPass(ID) {}

LegacyInlinerBase::LegacyInlinerBase(char &ID, bool InsertLifetime)
    : CallGraphSCCPass(ID), InsertLifetime(InsertLifetime) {}

/// For this class, we declare that we require and preserve the call graph.
/// If the derived class implements this method, it should
/// always explicitly call the implementation here.
void LegacyInlinerBase::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<AssumptionCacheTracker>();
  AU.addRequired<ProfileSummaryInfoWrapperPass>();
  AU.addRequired<TargetLibraryInfoWrapperPass>();
  getAAResultsAnalysisUsage(AU);
  CallGraphSCCPass::getAnalysisUsage(AU);
}

using InlinedArrayAllocasTy = DenseMap<ArrayType *, std::vector<AllocaInst *>>;

/// Look at all of the allocas that we inlined through this call site.  If we
/// have already inlined other allocas through other calls into this function,
/// then we know that they have disjoint lifetimes and that we can merge them.
///
/// There are many heuristics possible for merging these allocas, and the
/// different options have different tradeoffs.  One thing that we *really*
/// don't want to hurt is SRoA: once inlining happens, often allocas are no
/// longer address taken and so they can be promoted.
///
/// Our "solution" for that is to only merge allocas whose outermost type is an
/// array type.  These are usually not promoted because someone is using a
/// variable index into them.  These are also often the most important ones to
/// merge.
///
/// A better solution would be to have real memory lifetime markers in the IR
/// and not have the inliner do any merging of allocas at all.  This would
/// allow the backend to do proper stack slot coloring of all allocas that
/// *actually make it to the backend*, which is really what we want.
///
/// Because we don't have this information, we do this simple and useful hack.
static void mergeInlinedArrayAllocas(
    Function *Caller, InlineFunctionInfo &IFI,
    InlinedArrayAllocasTy &InlinedArrayAllocas, int InlineHistory) {
  SmallPtrSet<AllocaInst *, 16> UsedAllocas;

  // When processing our SCC, check to see if CS was inlined from some other
  // call site.  For example, if we're processing "A" in this code:
  //   A() { B() }
  //   B() { x = alloca ... C() }
  //   C() { y = alloca ... }
  // Assume that C was not inlined into B initially, and so we're processing A
  // and decide to inline B into A.  Doing this makes an alloca available for
  // reuse and makes a callsite (C) available for inlining.  When we process
  // the C call site we don't want to do any alloca merging between X and Y
  // because their scopes are not disjoint.  We could make this smarter by
  // keeping track of the inline history for each alloca in the
  // InlinedArrayAllocas but this isn't likely to be a significant win.
  if (InlineHistory != -1) // Only do merging for top-level call sites in SCC.
    return;

  // Loop over all the allocas we have so far and see if they can be merged with
  // a previously inlined alloca.  If not, remember that we had it.
  for (unsigned AllocaNo = 0, e = IFI.StaticAllocas.size(); AllocaNo != e;
       ++AllocaNo) {
    AllocaInst *AI = IFI.StaticAllocas[AllocaNo];

    // Don't bother trying to merge array allocations (they will usually be
    // canonicalized to be an allocation *of* an array), or allocations whose
    // type is not itself an array (because we're afraid of pessimizing SRoA).
    ArrayType *ATy = dyn_cast<ArrayType>(AI->getAllocatedType());
    if (!ATy || AI->isArrayAllocation())
      continue;

    // Get the list of all available allocas for this array type.
    std::vector<AllocaInst *> &AllocasForType = InlinedArrayAllocas[ATy];

    // Loop over the allocas in AllocasForType to see if we can reuse one.  Note
    // that we have to be careful not to reuse the same "available" alloca for
    // multiple different allocas that we just inlined, we use the 'UsedAllocas'
    // set to keep track of which "available" allocas are being used by this
    // function.  Also, AllocasForType can be empty of course!
    bool MergedAwayAlloca = false;
    for (AllocaInst *AvailableAlloca : AllocasForType) {
      unsigned Align1 = AI->getAlignment(),
               Align2 = AvailableAlloca->getAlignment();

      // The available alloca has to be in the right function, not in some other
      // function in this SCC.
      if (AvailableAlloca->getParent() != AI->getParent())
        continue;

      // If the inlined function already uses this alloca then we can't reuse
      // it.
      if (!UsedAllocas.insert(AvailableAlloca).second)
        continue;

      // Otherwise, we *can* reuse it, RAUW AI into AvailableAlloca and declare
      // success!
      LLVM_DEBUG(dbgs() << "    ***MERGED ALLOCA: " << *AI
                        << "\n\t\tINTO: " << *AvailableAlloca << '\n');

      // Move affected dbg.declare calls immediately after the new alloca to
      // avoid the situation when a dbg.declare precedes its alloca.
      if (auto *L = LocalAsMetadata::getIfExists(AI))
        if (auto *MDV = MetadataAsValue::getIfExists(AI->getContext(), L))
          for (User *U : MDV->users())
            if (DbgDeclareInst *DDI = dyn_cast<DbgDeclareInst>(U))
              DDI->moveBefore(AvailableAlloca->getNextNode());

      AI->replaceAllUsesWith(AvailableAlloca);

      if (Align1 != Align2) {
        if (!Align1 || !Align2) {
          const DataLayout &DL = Caller->getParent()->getDataLayout();
          unsigned TypeAlign = DL.getABITypeAlignment(AI->getAllocatedType());

          Align1 = Align1 ? Align1 : TypeAlign;
          Align2 = Align2 ? Align2 : TypeAlign;
        }

        if (Align1 > Align2)
          AvailableAlloca->setAlignment(AI->getAlignment());
      }

      AI->eraseFromParent();
      MergedAwayAlloca = true;
      ++NumMergedAllocas;
      IFI.StaticAllocas[AllocaNo] = nullptr;
      break;
    }

    // If we already nuked the alloca, we're done with it.
    if (MergedAwayAlloca)
      continue;

    // If we were unable to merge away the alloca either because there are no
    // allocas of the right type available or because we reused them all
    // already, remember that this alloca came from an inlined function and mark
    // it used so we don't reuse it for other allocas from this inline
    // operation.
    AllocasForType.push_back(AI);
    UsedAllocas.insert(AI);
  }
}

/// If it is possible to inline the specified call site,
/// do so and update the CallGraph for this operation.
///
/// This function also does some basic book-keeping to update the IR.  The
/// InlinedArrayAllocas map keeps track of any allocas that are already
/// available from other functions inlined into the caller.  If we are able to
/// inline this call site we attempt to reuse already available allocas or add
/// any new allocas to the set if not possible.
static InlineResult InlineCallIfPossible(
    CallSite CS, InlineFunctionInfo &IFI,
    InlinedArrayAllocasTy &InlinedArrayAllocas, int InlineHistory,
    bool InsertLifetime, function_ref<AAResults &(Function &)> &AARGetter,
    ImportedFunctionsInliningStatistics &ImportedFunctionsStats) {
  Function *Callee = CS.getCalledFunction();
  Function *Caller = CS.getCaller();

  AAResults &AAR = AARGetter(*Callee);

  // Try to inline the function.  Get the list of static allocas that were
  // inlined.
  InlineResult IR = InlineFunction(CS, IFI, &AAR, InsertLifetime);
  if (!IR)
    return IR;

  if (InlinerFunctionImportStats != InlinerFunctionImportStatsOpts::No)
    ImportedFunctionsStats.recordInline(*Caller, *Callee);

  AttributeFuncs::mergeAttributesForInlining(*Caller, *Callee);

  if (!DisableInlinedAllocaMerging)
    mergeInlinedArrayAllocas(Caller, IFI, InlinedArrayAllocas, InlineHistory);

  return IR; // success
}

/// Return true if inlining of CS can block the caller from being
/// inlined which is proved to be more beneficial. \p IC is the
/// estimated inline cost associated with callsite \p CS.
/// \p TotalSecondaryCost will be set to the estimated cost of inlining the
/// caller if \p CS is suppressed for inlining.
static bool
shouldBeDeferred(Function *Caller, CallSite CS, InlineCost IC,
                 int &TotalSecondaryCost,
                 function_ref<InlineCost(CallSite CS)> GetInlineCost) {
  // For now we only handle local or inline functions.
  if (!Caller->hasLocalLinkage() && !Caller->hasLinkOnceODRLinkage())
    return false;
  // If the cost of inlining CS is non-positive, it is not going to prevent the
  // caller from being inlined into its callers and hence we don't need to
  // defer.
  if (IC.getCost() <= 0)
    return false;
  // Try to detect the case where the current inlining candidate caller (call
  // it B) is a static or linkonce-ODR function and is an inlining candidate
  // elsewhere, and the current candidate callee (call it C) is large enough
  // that inlining it into B would make B too big to inline later. In these
  // circumstances it may be best not to inline C into B, but to inline B into
  // its callers.
  //
  // This only applies to static and linkonce-ODR functions because those are
  // expected to be available for inlining in the translation units where they
  // are used. Thus we will always have the opportunity to make local inlining
  // decisions. Importantly the linkonce-ODR linkage covers inline functions
  // and templates in C++.
  //
  // FIXME: All of this logic should be sunk into getInlineCost. It relies on
  // the internal implementation of the inline cost metrics rather than
  // treating them as truly abstract units etc.
  TotalSecondaryCost = 0;
  // The candidate cost to be imposed upon the current function.
  int CandidateCost = IC.getCost() - 1;
  // If the caller has local linkage and can be inlined to all its callers, we
  // can apply a huge negative bonus to TotalSecondaryCost.
  bool ApplyLastCallBonus = Caller->hasLocalLinkage() && !Caller->hasOneUse();
  // This bool tracks what happens if we DO inline C into B.
  bool inliningPreventsSomeOuterInline = false;
  for (User *U : Caller->users()) {
    // If the caller will not be removed (either because it does not have a
    // local linkage or because the LastCallToStaticBonus has been already
    // applied), then we can exit the loop early.
    if (!ApplyLastCallBonus && TotalSecondaryCost >= IC.getCost())
      return false;
    CallSite CS2(U);

    // If this isn't a call to Caller (it could be some other sort
    // of reference) skip it.  Such references will prevent the caller
    // from being removed.
    if (!CS2 || CS2.getCalledFunction() != Caller) {
      ApplyLastCallBonus = false;
      continue;
    }

    InlineCost IC2 = GetInlineCost(CS2);
    ++NumCallerCallersAnalyzed;
    if (!IC2) {
      ApplyLastCallBonus = false;
      continue;
    }
    if (IC2.isAlways())
      continue;

    // See if inlining of the original callsite would erase the cost delta of
    // this callsite. We subtract off the penalty for the call instruction,
    // which we would be deleting.
    if (IC2.getCostDelta() <= CandidateCost) {
      inliningPreventsSomeOuterInline = true;
      TotalSecondaryCost += IC2.getCost();
    }
  }
  // If all outer calls to Caller would get inlined, the cost for the last
  // one is set very low by getInlineCost, in anticipation that Caller will
  // be removed entirely.  We did not account for this above unless there
  // is only one caller of Caller.
  if (ApplyLastCallBonus)
    TotalSecondaryCost -= InlineConstants::LastCallToStaticBonus;

  if (inliningPreventsSomeOuterInline && TotalSecondaryCost < IC.getCost())
    return true;

  return false;
}

static std::basic_ostream<char> &operator<<(std::basic_ostream<char> &R,
                                            const ore::NV &Arg) {
  return R << Arg.Val;
}

template <class RemarkT>
RemarkT &operator<<(RemarkT &&R, const InlineCost &IC) {
  using namespace ore;
  if (IC.isAlways()) {
    R << "(cost=always)";
  } else if (IC.isNever()) {
    R << "(cost=never)";
  } else {
    R << "(cost=" << ore::NV("Cost", IC.getCost())
      << ", threshold=" << ore::NV("Threshold", IC.getThreshold()) << ")";
  }
  if (const char *Reason = IC.getReason())
    R << ": " << ore::NV("Reason", Reason);
  return R;
}

static std::string inlineCostStr(const InlineCost &IC) {
  std::stringstream Remark;
  Remark << IC;
  return Remark.str();
}

/// Return the cost only if the inliner should attempt to inline at the given
/// CallSite. If we return the cost, we will emit an optimisation remark later
/// using that cost, so we won't do so from this function.
static Optional<InlineCost>
shouldInline(CallSite CS, function_ref<InlineCost(CallSite CS)> GetInlineCost,
             OptimizationRemarkEmitter &ORE) {
  using namespace ore;

  InlineCost IC = GetInlineCost(CS);
  Instruction *Call = CS.getInstruction();
  Function *Callee = CS.getCalledFunction();
  Function *Caller = CS.getCaller();

  if (IC.isAlways()) {
    LLVM_DEBUG(dbgs() << "    Inlining " << inlineCostStr(IC)
                      << ", Call: " << *CS.getInstruction() << "\n");
    return IC;
  }

  if (IC.isNever()) {
    LLVM_DEBUG(dbgs() << "    NOT Inlining " << inlineCostStr(IC)
                      << ", Call: " << *CS.getInstruction() << "\n");
    ORE.emit([&]() {
      return OptimizationRemarkMissed(DEBUG_TYPE, "NeverInline", Call)
             << NV("Callee", Callee) << " not inlined into "
             << NV("Caller", Caller) << " because it should never be inlined "
             << IC;
    });
    return IC;
  }

  if (!IC) {
    LLVM_DEBUG(dbgs() << "    NOT Inlining " << inlineCostStr(IC)
                      << ", Call: " << *CS.getInstruction() << "\n");
    ORE.emit([&]() {
      return OptimizationRemarkMissed(DEBUG_TYPE, "TooCostly", Call)
             << NV("Callee", Callee) << " not inlined into "
             << NV("Caller", Caller) << " because too costly to inline " << IC;
    });
    return IC;
  }

  int TotalSecondaryCost = 0;
  if (shouldBeDeferred(Caller, CS, IC, TotalSecondaryCost, GetInlineCost)) {
    LLVM_DEBUG(dbgs() << "    NOT Inlining: " << *CS.getInstruction()
                      << " Cost = " << IC.getCost()
                      << ", outer Cost = " << TotalSecondaryCost << '\n');
    ORE.emit([&]() {
      return OptimizationRemarkMissed(DEBUG_TYPE, "IncreaseCostInOtherContexts",
                                      Call)
             << "Not inlining. Cost of inlining " << NV("Callee", Callee)
             << " increases the cost of inlining " << NV("Caller", Caller)
             << " in other contexts";
    });

    // IC does not bool() to false, so get an InlineCost that will.
    // This will not be inspected to make an error message.
    return None;
  }

  LLVM_DEBUG(dbgs() << "    Inlining " << inlineCostStr(IC)
                    << ", Call: " << *CS.getInstruction() << '\n');
  return IC;
}

/// Return true if the specified inline history ID
/// indicates an inline history that includes the specified function.
static bool InlineHistoryIncludes(
    Function *F, int InlineHistoryID,
    const SmallVectorImpl<std::pair<Function *, int>> &InlineHistory) {
  while (InlineHistoryID != -1) {
    assert(unsigned(InlineHistoryID) < InlineHistory.size() &&
           "Invalid inline history ID");
    if (InlineHistory[InlineHistoryID].first == F)
      return true;
    InlineHistoryID = InlineHistory[InlineHistoryID].second;
  }
  return false;
}

bool LegacyInlinerBase::doInitialization(CallGraph &CG) {
  if (InlinerFunctionImportStats != InlinerFunctionImportStatsOpts::No)
    ImportedFunctionsStats.setModuleInfo(CG.getModule());
  return false; // No changes to CallGraph.
}

bool LegacyInlinerBase::runOnSCC(CallGraphSCC &SCC) {
  if (skipSCC(SCC))
    return false;
  return inlineCalls(SCC);
}

static void emit_inlined_into(OptimizationRemarkEmitter &ORE, DebugLoc &DLoc,
                              const BasicBlock *Block, const Function &Callee,
                              const Function &Caller, const InlineCost &IC) {
  ORE.emit([&]() {
    bool AlwaysInline = IC.isAlways();
    StringRef RemarkName = AlwaysInline ? "AlwaysInline" : "Inlined";
    return OptimizationRemark(DEBUG_TYPE, RemarkName, DLoc, Block)
           << ore::NV("Callee", &Callee) << " inlined into "
           << ore::NV("Caller", &Caller) << " with " << IC;
  });
}

static void setInlineRemark(CallSite &CS, StringRef message) {
  if (!InlineRemarkAttribute)
    return;

  Attribute attr = Attribute::get(CS->getContext(), "inline-remark", message);
  CS.addAttribute(AttributeList::FunctionIndex, attr);
}

static bool
inlineCallsImpl(CallGraphSCC &SCC, CallGraph &CG,
                std::function<AssumptionCache &(Function &)> GetAssumptionCache,
                ProfileSummaryInfo *PSI, TargetLibraryInfo &TLI,
                bool InsertLifetime,
                function_ref<InlineCost(CallSite CS)> GetInlineCost,
                function_ref<AAResults &(Function &)> AARGetter,
                ImportedFunctionsInliningStatistics &ImportedFunctionsStats) {
  SmallPtrSet<Function *, 8> SCCFunctions;
  LLVM_DEBUG(dbgs() << "Inliner visiting SCC:");
  for (CallGraphNode *Node : SCC) {
    Function *F = Node->getFunction();
    if (F)
      SCCFunctions.insert(F);
    LLVM_DEBUG(dbgs() << " " << (F ? F->getName() : "INDIRECTNODE"));
  }

  // Scan through and identify all call sites ahead of time so that we only
  // inline call sites in the original functions, not call sites that result
  // from inlining other functions.
  SmallVector<std::pair<CallSite, int>, 16> CallSites;

  // When inlining a callee produces new call sites, we want to keep track of
  // the fact that they were inlined from the callee.  This allows us to avoid
  // infinite inlining in some obscure cases.  To represent this, we use an
  // index into the InlineHistory vector.
  SmallVector<std::pair<Function *, int>, 8> InlineHistory;

  for (CallGraphNode *Node : SCC) {
    Function *F = Node->getFunction();
    if (!F || F->isDeclaration())
      continue;

    OptimizationRemarkEmitter ORE(F);
    for (BasicBlock &BB : *F)
      for (Instruction &I : BB) {
        CallSite CS(cast<Value>(&I));
        // If this isn't a call, or it is a call to an intrinsic, it can
        // never be inlined.
        if (!CS || isa<IntrinsicInst>(I))
          continue;

        // If this is a direct call to an external function, we can never inline
        // it.  If it is an indirect call, inlining may resolve it to be a
        // direct call, so we keep it.
        if (Function *Callee = CS.getCalledFunction())
          if (Callee->isDeclaration()) {
            using namespace ore;

            setInlineRemark(CS, "unavailable definition");
            ORE.emit([&]() {
              return OptimizationRemarkMissed(DEBUG_TYPE, "NoDefinition", &I)
                     << NV("Callee", Callee) << " will not be inlined into "
                     << NV("Caller", CS.getCaller())
                     << " because its definition is unavailable"
                     << setIsVerbose();
            });
            continue;
          }

        CallSites.push_back(std::make_pair(CS, -1));
      }
  }

  LLVM_DEBUG(dbgs() << ": " << CallSites.size() << " call sites.\n");

  // If there are no calls in this function, exit early.
  if (CallSites.empty())
    return false;

  // Now that we have all of the call sites, move the ones to functions in the
  // current SCC to the end of the list.
  unsigned FirstCallInSCC = CallSites.size();
  for (unsigned i = 0; i < FirstCallInSCC; ++i)
    if (Function *F = CallSites[i].first.getCalledFunction())
      if (SCCFunctions.count(F))
        std::swap(CallSites[i--], CallSites[--FirstCallInSCC]);

  InlinedArrayAllocasTy InlinedArrayAllocas;
  InlineFunctionInfo InlineInfo(&CG, &GetAssumptionCache, PSI);

  // Now that we have all of the call sites, loop over them and inline them if
  // it looks profitable to do so.
  bool Changed = false;
  bool LocalChange;
  do {
    LocalChange = false;
    // Iterate over the outer loop because inlining functions can cause indirect
    // calls to become direct calls.
    // CallSites may be modified inside so ranged for loop can not be used.
    for (unsigned CSi = 0; CSi != CallSites.size(); ++CSi) {
      CallSite CS = CallSites[CSi].first;

      Function *Caller = CS.getCaller();
      Function *Callee = CS.getCalledFunction();

      // We can only inline direct calls to non-declarations.
      if (!Callee || Callee->isDeclaration())
        continue;

      Instruction *Instr = CS.getInstruction();

      bool IsTriviallyDead = isInstructionTriviallyDead(Instr, &TLI);

      int InlineHistoryID;
      if (!IsTriviallyDead) {
        // If this call site was obtained by inlining another function, verify
        // that the include path for the function did not include the callee
        // itself.  If so, we'd be recursively inlining the same function,
        // which would provide the same callsites, which would cause us to
        // infinitely inline.
        InlineHistoryID = CallSites[CSi].second;
        if (InlineHistoryID != -1 &&
            InlineHistoryIncludes(Callee, InlineHistoryID, InlineHistory)) {
          setInlineRemark(CS, "recursive");
          continue;
        }
      }

      // FIXME for new PM: because of the old PM we currently generate ORE and
      // in turn BFI on demand.  With the new PM, the ORE dependency should
      // just become a regular analysis dependency.
      OptimizationRemarkEmitter ORE(Caller);

      Optional<InlineCost> OIC = shouldInline(CS, GetInlineCost, ORE);
      // If the policy determines that we should inline this function,
      // delete the call instead.
      if (!OIC.hasValue()) {
        setInlineRemark(CS, "deferred");
        continue;
      }

      if (!OIC.getValue()) {
        // shouldInline() call returned a negative inline cost that explains
        // why this callsite should not be inlined.
        setInlineRemark(CS, inlineCostStr(*OIC));
        continue;
      }

      // If this call site is dead and it is to a readonly function, we should
      // just delete the call instead of trying to inline it, regardless of
      // size.  This happens because IPSCCP propagates the result out of the
      // call and then we're left with the dead call.
      if (IsTriviallyDead) {
        LLVM_DEBUG(dbgs() << "    -> Deleting dead call: " << *Instr << "\n");
        // Update the call graph by deleting the edge from Callee to Caller.
        setInlineRemark(CS, "trivially dead");
        CG[Caller]->removeCallEdgeFor(CS);
        Instr->eraseFromParent();
        ++NumCallsDeleted;
      } else {
        // Get DebugLoc to report. CS will be invalid after Inliner.
        DebugLoc DLoc = CS->getDebugLoc();
        BasicBlock *Block = CS.getParent();

        // Attempt to inline the function.
        using namespace ore;

        InlineResult IR = InlineCallIfPossible(
            CS, InlineInfo, InlinedArrayAllocas, InlineHistoryID,
            InsertLifetime, AARGetter, ImportedFunctionsStats);
        if (!IR) {
          setInlineRemark(CS, std::string(IR) + "; " + inlineCostStr(*OIC));
          ORE.emit([&]() {
            return OptimizationRemarkMissed(DEBUG_TYPE, "NotInlined", DLoc,
                                            Block)
                   << NV("Callee", Callee) << " will not be inlined into "
                   << NV("Caller", Caller) << ": " << NV("Reason", IR.message);
          });
          continue;
        }
        ++NumInlined;

        emit_inlined_into(ORE, DLoc, Block, *Callee, *Caller, *OIC);

        // If inlining this function gave us any new call sites, throw them
        // onto our worklist to process.  They are useful inline candidates.
        if (!InlineInfo.InlinedCalls.empty()) {
          // Create a new inline history entry for this, so that we remember
          // that these new callsites came about due to inlining Callee.
          int NewHistoryID = InlineHistory.size();
          InlineHistory.push_back(std::make_pair(Callee, InlineHistoryID));

          for (Value *Ptr : InlineInfo.InlinedCalls)
            CallSites.push_back(std::make_pair(CallSite(Ptr), NewHistoryID));
        }
      }

      // If we inlined or deleted the last possible call site to the function,
      // delete the function body now.
      if (Callee && Callee->use_empty() && Callee->hasLocalLinkage() &&
          // TODO: Can remove if in SCC now.
          !SCCFunctions.count(Callee) &&
          // The function may be apparently dead, but if there are indirect
          // callgraph references to the node, we cannot delete it yet, this
          // could invalidate the CGSCC iterator.
          CG[Callee]->getNumReferences() == 0) {
        LLVM_DEBUG(dbgs() << "    -> Deleting dead function: "
                          << Callee->getName() << "\n");
        CallGraphNode *CalleeNode = CG[Callee];

        // Remove any call graph edges from the callee to its callees.
        CalleeNode->removeAllCalledFunctions();

        // Removing the node for callee from the call graph and delete it.
        delete CG.removeFunctionFromModule(CalleeNode);
        ++NumDeleted;
      }

      // Remove this call site from the list.  If possible, use
      // swap/pop_back for efficiency, but do not use it if doing so would
      // move a call site to a function in this SCC before the
      // 'FirstCallInSCC' barrier.
      if (SCC.isSingular()) {
        CallSites[CSi] = CallSites.back();
        CallSites.pop_back();
      } else {
        CallSites.erase(CallSites.begin() + CSi);
      }
      --CSi;

      Changed = true;
      LocalChange = true;
    }
  } while (LocalChange);

  return Changed;
}

bool LegacyInlinerBase::inlineCalls(CallGraphSCC &SCC) {
  CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();
  ACT = &getAnalysis<AssumptionCacheTracker>();
  PSI = &getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI();
  auto &TLI = getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
  auto GetAssumptionCache = [&](Function &F) -> AssumptionCache & {
    return ACT->getAssumptionCache(F);
  };
  return inlineCallsImpl(SCC, CG, GetAssumptionCache, PSI, TLI, InsertLifetime,
                         [this](CallSite CS) { return getInlineCost(CS); },
                         LegacyAARGetter(*this), ImportedFunctionsStats);
}

/// Remove now-dead linkonce functions at the end of
/// processing to avoid breaking the SCC traversal.
bool LegacyInlinerBase::doFinalization(CallGraph &CG) {
  if (InlinerFunctionImportStats != InlinerFunctionImportStatsOpts::No)
    ImportedFunctionsStats.dump(InlinerFunctionImportStats ==
                                InlinerFunctionImportStatsOpts::Verbose);
  return removeDeadFunctions(CG);
}

/// Remove dead functions that are not included in DNR (Do Not Remove) list.
bool LegacyInlinerBase::removeDeadFunctions(CallGraph &CG,
                                            bool AlwaysInlineOnly) {
  SmallVector<CallGraphNode *, 16> FunctionsToRemove;
  SmallVector<Function *, 16> DeadFunctionsInComdats;

  auto RemoveCGN = [&](CallGraphNode *CGN) {
    // Remove any call graph edges from the function to its callees.
    CGN->removeAllCalledFunctions();

    // Remove any edges from the external node to the function's call graph
    // node.  These edges might have been made irrelegant due to
    // optimization of the program.
    CG.getExternalCallingNode()->removeAnyCallEdgeTo(CGN);

    // Removing the node for callee from the call graph and delete it.
    FunctionsToRemove.push_back(CGN);
  };

  // Scan for all of the functions, looking for ones that should now be removed
  // from the program.  Insert the dead ones in the FunctionsToRemove set.
  for (const auto &I : CG) {
    CallGraphNode *CGN = I.second.get();
    Function *F = CGN->getFunction();
    if (!F || F->isDeclaration())
      continue;

    // Handle the case when this function is called and we only want to care
    // about always-inline functions. This is a bit of a hack to share code
    // between here and the InlineAlways pass.
    if (AlwaysInlineOnly && !F->hasFnAttribute(Attribute::AlwaysInline))
      continue;

    // If the only remaining users of the function are dead constants, remove
    // them.
    F->removeDeadConstantUsers();

    if (!F->isDefTriviallyDead())
      continue;

    // It is unsafe to drop a function with discardable linkage from a COMDAT
    // without also dropping the other members of the COMDAT.
    // The inliner doesn't visit non-function entities which are in COMDAT
    // groups so it is unsafe to do so *unless* the linkage is local.
    if (!F->hasLocalLinkage()) {
      if (F->hasComdat()) {
        DeadFunctionsInComdats.push_back(F);
        continue;
      }
    }

    RemoveCGN(CGN);
  }
  if (!DeadFunctionsInComdats.empty()) {
    // Filter out the functions whose comdats remain alive.
    filterDeadComdatFunctions(CG.getModule(), DeadFunctionsInComdats);
    // Remove the rest.
    for (Function *F : DeadFunctionsInComdats)
      RemoveCGN(CG[F]);
  }

  if (FunctionsToRemove.empty())
    return false;

  // Now that we know which functions to delete, do so.  We didn't want to do
  // this inline, because that would invalidate our CallGraph::iterator
  // objects. :(
  //
  // Note that it doesn't matter that we are iterating over a non-stable order
  // here to do this, it doesn't matter which order the functions are deleted
  // in.
  array_pod_sort(FunctionsToRemove.begin(), FunctionsToRemove.end());
  FunctionsToRemove.erase(
      std::unique(FunctionsToRemove.begin(), FunctionsToRemove.end()),
      FunctionsToRemove.end());
  for (CallGraphNode *CGN : FunctionsToRemove) {
    delete CG.removeFunctionFromModule(CGN);
    ++NumDeleted;
  }
  return true;
}

InlinerPass::~InlinerPass() {
  if (ImportedFunctionsStats) {
    assert(InlinerFunctionImportStats != InlinerFunctionImportStatsOpts::No);
    ImportedFunctionsStats->dump(InlinerFunctionImportStats ==
                                 InlinerFunctionImportStatsOpts::Verbose);
  }
}

PreservedAnalyses InlinerPass::run(LazyCallGraph::SCC &InitialC,
                                   CGSCCAnalysisManager &AM, LazyCallGraph &CG,
                                   CGSCCUpdateResult &UR) {
  const ModuleAnalysisManager &MAM =
      AM.getResult<ModuleAnalysisManagerCGSCCProxy>(InitialC, CG).getManager();
  bool Changed = false;

  assert(InitialC.size() > 0 && "Cannot handle an empty SCC!");
  Module &M = *InitialC.begin()->getFunction().getParent();
  ProfileSummaryInfo *PSI = MAM.getCachedResult<ProfileSummaryAnalysis>(M);

  if (!ImportedFunctionsStats &&
      InlinerFunctionImportStats != InlinerFunctionImportStatsOpts::No) {
    ImportedFunctionsStats =
        llvm::make_unique<ImportedFunctionsInliningStatistics>();
    ImportedFunctionsStats->setModuleInfo(M);
  }

  // We use a single common worklist for calls across the entire SCC. We
  // process these in-order and append new calls introduced during inlining to
  // the end.
  //
  // Note that this particular order of processing is actually critical to
  // avoid very bad behaviors. Consider *highly connected* call graphs where
  // each function contains a small amonut of code and a couple of calls to
  // other functions. Because the LLVM inliner is fundamentally a bottom-up
  // inliner, it can handle gracefully the fact that these all appear to be
  // reasonable inlining candidates as it will flatten things until they become
  // too big to inline, and then move on and flatten another batch.
  //
  // However, when processing call edges *within* an SCC we cannot rely on this
  // bottom-up behavior. As a consequence, with heavily connected *SCCs* of
  // functions we can end up incrementally inlining N calls into each of
  // N functions because each incremental inlining decision looks good and we
  // don't have a topological ordering to prevent explosions.
  //
  // To compensate for this, we don't process transitive edges made immediate
  // by inlining until we've done one pass of inlining across the entire SCC.
  // Large, highly connected SCCs still lead to some amount of code bloat in
  // this model, but it is uniformly spread across all the functions in the SCC
  // and eventually they all become too large to inline, rather than
  // incrementally maknig a single function grow in a super linear fashion.
  SmallVector<std::pair<CallSite, int>, 16> Calls;

  FunctionAnalysisManager &FAM =
      AM.getResult<FunctionAnalysisManagerCGSCCProxy>(InitialC, CG)
          .getManager();

  // Populate the initial list of calls in this SCC.
  for (auto &N : InitialC) {
    auto &ORE =
        FAM.getResult<OptimizationRemarkEmitterAnalysis>(N.getFunction());
    // We want to generally process call sites top-down in order for
    // simplifications stemming from replacing the call with the returned value
    // after inlining to be visible to subsequent inlining decisions.
    // FIXME: Using instructions sequence is a really bad way to do this.
    // Instead we should do an actual RPO walk of the function body.
    for (Instruction &I : instructions(N.getFunction()))
      if (auto CS = CallSite(&I))
        if (Function *Callee = CS.getCalledFunction()) {
          if (!Callee->isDeclaration())
            Calls.push_back({CS, -1});
          else if (!isa<IntrinsicInst>(I)) {
            using namespace ore;
            setInlineRemark(CS, "unavailable definition");
            ORE.emit([&]() {
              return OptimizationRemarkMissed(DEBUG_TYPE, "NoDefinition", &I)
                     << NV("Callee", Callee) << " will not be inlined into "
                     << NV("Caller", CS.getCaller())
                     << " because its definition is unavailable"
                     << setIsVerbose();
            });
          }
        }
  }
  if (Calls.empty())
    return PreservedAnalyses::all();

  // Capture updatable variables for the current SCC and RefSCC.
  auto *C = &InitialC;
  auto *RC = &C->getOuterRefSCC();

  // When inlining a callee produces new call sites, we want to keep track of
  // the fact that they were inlined from the callee.  This allows us to avoid
  // infinite inlining in some obscure cases.  To represent this, we use an
  // index into the InlineHistory vector.
  SmallVector<std::pair<Function *, int>, 16> InlineHistory;

  // Track a set vector of inlined callees so that we can augment the caller
  // with all of their edges in the call graph before pruning out the ones that
  // got simplified away.
  SmallSetVector<Function *, 4> InlinedCallees;

  // Track the dead functions to delete once finished with inlining calls. We
  // defer deleting these to make it easier to handle the call graph updates.
  SmallVector<Function *, 4> DeadFunctions;

  // Loop forward over all of the calls. Note that we cannot cache the size as
  // inlining can introduce new calls that need to be processed.
  for (int i = 0; i < (int)Calls.size(); ++i) {
    // We expect the calls to typically be batched with sequences of calls that
    // have the same caller, so we first set up some shared infrastructure for
    // this caller. We also do any pruning we can at this layer on the caller
    // alone.
    Function &F = *Calls[i].first.getCaller();
    LazyCallGraph::Node &N = *CG.lookup(F);
    if (CG.lookupSCC(N) != C)
      continue;
    if (F.hasFnAttribute(Attribute::OptimizeNone)) {
      setInlineRemark(Calls[i].first, "optnone attribute");
      continue;
    }

    LLVM_DEBUG(dbgs() << "Inlining calls in: " << F.getName() << "\n");

    // Get a FunctionAnalysisManager via a proxy for this particular node. We
    // do this each time we visit a node as the SCC may have changed and as
    // we're going to mutate this particular function we want to make sure the
    // proxy is in place to forward any invalidation events. We can use the
    // manager we get here for looking up results for functions other than this
    // node however because those functions aren't going to be mutated by this
    // pass.
    FunctionAnalysisManager &FAM =
        AM.getResult<FunctionAnalysisManagerCGSCCProxy>(*C, CG)
            .getManager();

    // Get the remarks emission analysis for the caller.
    auto &ORE = FAM.getResult<OptimizationRemarkEmitterAnalysis>(F);

    std::function<AssumptionCache &(Function &)> GetAssumptionCache =
        [&](Function &F) -> AssumptionCache & {
      return FAM.getResult<AssumptionAnalysis>(F);
    };
    auto GetBFI = [&](Function &F) -> BlockFrequencyInfo & {
      return FAM.getResult<BlockFrequencyAnalysis>(F);
    };

    auto GetInlineCost = [&](CallSite CS) {
      Function &Callee = *CS.getCalledFunction();
      auto &CalleeTTI = FAM.getResult<TargetIRAnalysis>(Callee);
      return getInlineCost(CS, Params, CalleeTTI, GetAssumptionCache, {GetBFI},
                           PSI, &ORE);
    };

    // Now process as many calls as we have within this caller in the sequnece.
    // We bail out as soon as the caller has to change so we can update the
    // call graph and prepare the context of that new caller.
    bool DidInline = false;
    for (; i < (int)Calls.size() && Calls[i].first.getCaller() == &F; ++i) {
      int InlineHistoryID;
      CallSite CS;
      std::tie(CS, InlineHistoryID) = Calls[i];
      Function &Callee = *CS.getCalledFunction();

      if (InlineHistoryID != -1 &&
          InlineHistoryIncludes(&Callee, InlineHistoryID, InlineHistory)) {
        setInlineRemark(CS, "recursive");
        continue;
      }

      // Check if this inlining may repeat breaking an SCC apart that has
      // already been split once before. In that case, inlining here may
      // trigger infinite inlining, much like is prevented within the inliner
      // itself by the InlineHistory above, but spread across CGSCC iterations
      // and thus hidden from the full inline history.
      if (CG.lookupSCC(*CG.lookup(Callee)) == C &&
          UR.InlinedInternalEdges.count({&N, C})) {
        LLVM_DEBUG(dbgs() << "Skipping inlining internal SCC edge from a node "
                             "previously split out of this SCC by inlining: "
                          << F.getName() << " -> " << Callee.getName() << "\n");
        setInlineRemark(CS, "recursive SCC split");
        continue;
      }

      Optional<InlineCost> OIC = shouldInline(CS, GetInlineCost, ORE);
      // Check whether we want to inline this callsite.
      if (!OIC.hasValue()) {
        setInlineRemark(CS, "deferred");
        continue;
      }

      if (!OIC.getValue()) {
        // shouldInline() call returned a negative inline cost that explains
        // why this callsite should not be inlined.
        setInlineRemark(CS, inlineCostStr(*OIC));
        continue;
      }

      // Setup the data structure used to plumb customization into the
      // `InlineFunction` routine.
      InlineFunctionInfo IFI(
          /*cg=*/nullptr, &GetAssumptionCache, PSI,
          &FAM.getResult<BlockFrequencyAnalysis>(*(CS.getCaller())),
          &FAM.getResult<BlockFrequencyAnalysis>(Callee));

      // Get DebugLoc to report. CS will be invalid after Inliner.
      DebugLoc DLoc = CS->getDebugLoc();
      BasicBlock *Block = CS.getParent();

      using namespace ore;

      InlineResult IR = InlineFunction(CS, IFI);
      if (!IR) {
        setInlineRemark(CS, std::string(IR) + "; " + inlineCostStr(*OIC));
        ORE.emit([&]() {
          return OptimizationRemarkMissed(DEBUG_TYPE, "NotInlined", DLoc, Block)
                 << NV("Callee", &Callee) << " will not be inlined into "
                 << NV("Caller", &F) << ": " << NV("Reason", IR.message);
        });
        continue;
      }
      DidInline = true;
      InlinedCallees.insert(&Callee);

      ++NumInlined;

      emit_inlined_into(ORE, DLoc, Block, Callee, F, *OIC);

      // Add any new callsites to defined functions to the worklist.
      if (!IFI.InlinedCallSites.empty()) {
        int NewHistoryID = InlineHistory.size();
        InlineHistory.push_back({&Callee, InlineHistoryID});
        for (CallSite &CS : reverse(IFI.InlinedCallSites))
          if (Function *NewCallee = CS.getCalledFunction())
            if (!NewCallee->isDeclaration())
              Calls.push_back({CS, NewHistoryID});
      }

      if (InlinerFunctionImportStats != InlinerFunctionImportStatsOpts::No)
        ImportedFunctionsStats->recordInline(F, Callee);

      // Merge the attributes based on the inlining.
      AttributeFuncs::mergeAttributesForInlining(F, Callee);

      // For local functions, check whether this makes the callee trivially
      // dead. In that case, we can drop the body of the function eagerly
      // which may reduce the number of callers of other functions to one,
      // changing inline cost thresholds.
      if (Callee.hasLocalLinkage()) {
        // To check this we also need to nuke any dead constant uses (perhaps
        // made dead by this operation on other functions).
        Callee.removeDeadConstantUsers();
        if (Callee.use_empty() && !CG.isLibFunction(Callee)) {
          Calls.erase(
              std::remove_if(Calls.begin() + i + 1, Calls.end(),
                             [&Callee](const std::pair<CallSite, int> &Call) {
                               return Call.first.getCaller() == &Callee;
                             }),
              Calls.end());
          // Clear the body and queue the function itself for deletion when we
          // finish inlining and call graph updates.
          // Note that after this point, it is an error to do anything other
          // than use the callee's address or delete it.
          Callee.dropAllReferences();
          assert(find(DeadFunctions, &Callee) == DeadFunctions.end() &&
                 "Cannot put cause a function to become dead twice!");
          DeadFunctions.push_back(&Callee);
        }
      }
    }

    // Back the call index up by one to put us in a good position to go around
    // the outer loop.
    --i;

    if (!DidInline)
      continue;
    Changed = true;

    // Add all the inlined callees' edges as ref edges to the caller. These are
    // by definition trivial edges as we always have *some* transitive ref edge
    // chain. While in some cases these edges are direct calls inside the
    // callee, they have to be modeled in the inliner as reference edges as
    // there may be a reference edge anywhere along the chain from the current
    // caller to the callee that causes the whole thing to appear like
    // a (transitive) reference edge that will require promotion to a call edge
    // below.
    for (Function *InlinedCallee : InlinedCallees) {
      LazyCallGraph::Node &CalleeN = *CG.lookup(*InlinedCallee);
      for (LazyCallGraph::Edge &E : *CalleeN)
        RC->insertTrivialRefEdge(N, E.getNode());
    }

    // At this point, since we have made changes we have at least removed
    // a call instruction. However, in the process we do some incremental
    // simplification of the surrounding code. This simplification can
    // essentially do all of the same things as a function pass and we can
    // re-use the exact same logic for updating the call graph to reflect the
    // change.
    LazyCallGraph::SCC *OldC = C;
    C = &updateCGAndAnalysisManagerForFunctionPass(CG, *C, N, AM, UR);
    LLVM_DEBUG(dbgs() << "Updated inlining SCC: " << *C << "\n");
    RC = &C->getOuterRefSCC();

    // If this causes an SCC to split apart into multiple smaller SCCs, there
    // is a subtle risk we need to prepare for. Other transformations may
    // expose an "infinite inlining" opportunity later, and because of the SCC
    // mutation, we will revisit this function and potentially re-inline. If we
    // do, and that re-inlining also has the potentially to mutate the SCC
    // structure, the infinite inlining problem can manifest through infinite
    // SCC splits and merges. To avoid this, we capture the originating caller
    // node and the SCC containing the call edge. This is a slight over
    // approximation of the possible inlining decisions that must be avoided,
    // but is relatively efficient to store. We use C != OldC to know when
    // a new SCC is generated and the original SCC may be generated via merge
    // in later iterations.
    //
    // It is also possible that even if no new SCC is generated
    // (i.e., C == OldC), the original SCC could be split and then merged
    // into the same one as itself. and the original SCC will be added into
    // UR.CWorklist again, we want to catch such cases too.
    //
    // FIXME: This seems like a very heavyweight way of retaining the inline
    // history, we should look for a more efficient way of tracking it.
    if ((C != OldC || UR.CWorklist.count(OldC)) &&
        llvm::any_of(InlinedCallees, [&](Function *Callee) {
          return CG.lookupSCC(*CG.lookup(*Callee)) == OldC;
        })) {
      LLVM_DEBUG(dbgs() << "Inlined an internal call edge and split an SCC, "
                           "retaining this to avoid infinite inlining.\n");
      UR.InlinedInternalEdges.insert({&N, OldC});
    }
    InlinedCallees.clear();
  }

  // Now that we've finished inlining all of the calls across this SCC, delete
  // all of the trivially dead functions, updating the call graph and the CGSCC
  // pass manager in the process.
  //
  // Note that this walks a pointer set which has non-deterministic order but
  // that is OK as all we do is delete things and add pointers to unordered
  // sets.
  for (Function *DeadF : DeadFunctions) {
    // Get the necessary information out of the call graph and nuke the
    // function there. Also, cclear out any cached analyses.
    auto &DeadC = *CG.lookupSCC(*CG.lookup(*DeadF));
    FunctionAnalysisManager &FAM =
        AM.getResult<FunctionAnalysisManagerCGSCCProxy>(DeadC, CG)
            .getManager();
    FAM.clear(*DeadF, DeadF->getName());
    AM.clear(DeadC, DeadC.getName());
    auto &DeadRC = DeadC.getOuterRefSCC();
    CG.removeDeadFunction(*DeadF);

    // Mark the relevant parts of the call graph as invalid so we don't visit
    // them.
    UR.InvalidatedSCCs.insert(&DeadC);
    UR.InvalidatedRefSCCs.insert(&DeadRC);

    // And delete the actual function from the module.
    M.getFunctionList().erase(DeadF);
    ++NumDeleted;
  }

  if (!Changed)
    return PreservedAnalyses::all();

  // Even if we change the IR, we update the core CGSCC data structures and so
  // can preserve the proxy to the function analysis manager.
  PreservedAnalyses PA;
  PA.preserve<FunctionAnalysisManagerCGSCCProxy>();
  return PA;
}
