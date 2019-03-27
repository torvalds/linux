//===- ProfileSummaryInfo.cpp - Global profile summary information --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that provides access to the global profile summary
// information.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ProfileSummary.h"
using namespace llvm;

// The following two parameters determine the threshold for a count to be
// considered hot/cold. These two parameters are percentile values (multiplied
// by 10000). If the counts are sorted in descending order, the minimum count to
// reach ProfileSummaryCutoffHot gives the threshold to determine a hot count.
// Similarly, the minimum count to reach ProfileSummaryCutoffCold gives the
// threshold for determining cold count (everything <= this threshold is
// considered cold).

static cl::opt<int> ProfileSummaryCutoffHot(
    "profile-summary-cutoff-hot", cl::Hidden, cl::init(990000), cl::ZeroOrMore,
    cl::desc("A count is hot if it exceeds the minimum count to"
             " reach this percentile of total counts."));

static cl::opt<int> ProfileSummaryCutoffCold(
    "profile-summary-cutoff-cold", cl::Hidden, cl::init(999999), cl::ZeroOrMore,
    cl::desc("A count is cold if it is below the minimum count"
             " to reach this percentile of total counts."));

static cl::opt<unsigned> ProfileSummaryHugeWorkingSetSizeThreshold(
    "profile-summary-huge-working-set-size-threshold", cl::Hidden,
    cl::init(15000), cl::ZeroOrMore,
    cl::desc("The code working set size is considered huge if the number of"
             " blocks required to reach the -profile-summary-cutoff-hot"
             " percentile exceeds this count."));

// The next two options override the counts derived from summary computation and
// are useful for debugging purposes.
static cl::opt<int> ProfileSummaryHotCount(
    "profile-summary-hot-count", cl::ReallyHidden, cl::ZeroOrMore,
    cl::desc("A fixed hot count that overrides the count derived from"
             " profile-summary-cutoff-hot"));

static cl::opt<int> ProfileSummaryColdCount(
    "profile-summary-cold-count", cl::ReallyHidden, cl::ZeroOrMore,
    cl::desc("A fixed cold count that overrides the count derived from"
             " profile-summary-cutoff-cold"));

// Find the summary entry for a desired percentile of counts.
static const ProfileSummaryEntry &getEntryForPercentile(SummaryEntryVector &DS,
                                                        uint64_t Percentile) {
  auto Compare = [](const ProfileSummaryEntry &Entry, uint64_t Percentile) {
    return Entry.Cutoff < Percentile;
  };
  auto It = std::lower_bound(DS.begin(), DS.end(), Percentile, Compare);
  // The required percentile has to be <= one of the percentiles in the
  // detailed summary.
  if (It == DS.end())
    report_fatal_error("Desired percentile exceeds the maximum cutoff");
  return *It;
}

// The profile summary metadata may be attached either by the frontend or by
// any backend passes (IR level instrumentation, for example). This method
// checks if the Summary is null and if so checks if the summary metadata is now
// available in the module and parses it to get the Summary object. Returns true
// if a valid Summary is available.
bool ProfileSummaryInfo::computeSummary() {
  if (Summary)
    return true;
  auto *SummaryMD = M.getProfileSummary();
  if (!SummaryMD)
    return false;
  Summary.reset(ProfileSummary::getFromMD(SummaryMD));
  return true;
}

Optional<uint64_t>
ProfileSummaryInfo::getProfileCount(const Instruction *Inst,
                                    BlockFrequencyInfo *BFI) {
  if (!Inst)
    return None;
  assert((isa<CallInst>(Inst) || isa<InvokeInst>(Inst)) &&
         "We can only get profile count for call/invoke instruction.");
  if (hasSampleProfile()) {
    // In sample PGO mode, check if there is a profile metadata on the
    // instruction. If it is present, determine hotness solely based on that,
    // since the sampled entry count may not be accurate. If there is no
    // annotated on the instruction, return None.
    uint64_t TotalCount;
    if (Inst->extractProfTotalWeight(TotalCount))
      return TotalCount;
    return None;
  }
  if (BFI)
    return BFI->getBlockProfileCount(Inst->getParent());
  return None;
}

/// Returns true if the function's entry is hot. If it returns false, it
/// either means it is not hot or it is unknown whether it is hot or not (for
/// example, no profile data is available).
bool ProfileSummaryInfo::isFunctionEntryHot(const Function *F) {
  if (!F || !computeSummary())
    return false;
  auto FunctionCount = F->getEntryCount();
  // FIXME: The heuristic used below for determining hotness is based on
  // preliminary SPEC tuning for inliner. This will eventually be a
  // convenience method that calls isHotCount.
  return FunctionCount && isHotCount(FunctionCount.getCount());
}

/// Returns true if the function contains hot code. This can include a hot
/// function entry count, hot basic block, or (in the case of Sample PGO)
/// hot total call edge count.
/// If it returns false, it either means it is not hot or it is unknown
/// (for example, no profile data is available).
bool ProfileSummaryInfo::isFunctionHotInCallGraph(const Function *F,
                                                  BlockFrequencyInfo &BFI) {
  if (!F || !computeSummary())
    return false;
  if (auto FunctionCount = F->getEntryCount())
    if (isHotCount(FunctionCount.getCount()))
      return true;

  if (hasSampleProfile()) {
    uint64_t TotalCallCount = 0;
    for (const auto &BB : *F)
      for (const auto &I : BB)
        if (isa<CallInst>(I) || isa<InvokeInst>(I))
          if (auto CallCount = getProfileCount(&I, nullptr))
            TotalCallCount += CallCount.getValue();
    if (isHotCount(TotalCallCount))
      return true;
  }
  for (const auto &BB : *F)
    if (isHotBlock(&BB, &BFI))
      return true;
  return false;
}

/// Returns true if the function only contains cold code. This means that
/// the function entry and blocks are all cold, and (in the case of Sample PGO)
/// the total call edge count is cold.
/// If it returns false, it either means it is not cold or it is unknown
/// (for example, no profile data is available).
bool ProfileSummaryInfo::isFunctionColdInCallGraph(const Function *F,
                                                   BlockFrequencyInfo &BFI) {
  if (!F || !computeSummary())
    return false;
  if (auto FunctionCount = F->getEntryCount())
    if (!isColdCount(FunctionCount.getCount()))
      return false;

  if (hasSampleProfile()) {
    uint64_t TotalCallCount = 0;
    for (const auto &BB : *F)
      for (const auto &I : BB)
        if (isa<CallInst>(I) || isa<InvokeInst>(I))
          if (auto CallCount = getProfileCount(&I, nullptr))
            TotalCallCount += CallCount.getValue();
    if (!isColdCount(TotalCallCount))
      return false;
  }
  for (const auto &BB : *F)
    if (!isColdBlock(&BB, &BFI))
      return false;
  return true;
}

/// Returns true if the function's entry is a cold. If it returns false, it
/// either means it is not cold or it is unknown whether it is cold or not (for
/// example, no profile data is available).
bool ProfileSummaryInfo::isFunctionEntryCold(const Function *F) {
  if (!F)
    return false;
  if (F->hasFnAttribute(Attribute::Cold))
    return true;
  if (!computeSummary())
    return false;
  auto FunctionCount = F->getEntryCount();
  // FIXME: The heuristic used below for determining coldness is based on
  // preliminary SPEC tuning for inliner. This will eventually be a
  // convenience method that calls isHotCount.
  return FunctionCount && isColdCount(FunctionCount.getCount());
}

/// Compute the hot and cold thresholds.
void ProfileSummaryInfo::computeThresholds() {
  if (!computeSummary())
    return;
  auto &DetailedSummary = Summary->getDetailedSummary();
  auto &HotEntry =
      getEntryForPercentile(DetailedSummary, ProfileSummaryCutoffHot);
  HotCountThreshold = HotEntry.MinCount;
  if (ProfileSummaryHotCount.getNumOccurrences() > 0)
    HotCountThreshold = ProfileSummaryHotCount;
  auto &ColdEntry =
      getEntryForPercentile(DetailedSummary, ProfileSummaryCutoffCold);
  ColdCountThreshold = ColdEntry.MinCount;
  if (ProfileSummaryColdCount.getNumOccurrences() > 0)
    ColdCountThreshold = ProfileSummaryColdCount;
  assert(ColdCountThreshold <= HotCountThreshold &&
         "Cold count threshold cannot exceed hot count threshold!");
  HasHugeWorkingSetSize =
      HotEntry.NumCounts > ProfileSummaryHugeWorkingSetSizeThreshold;
}

bool ProfileSummaryInfo::hasHugeWorkingSetSize() {
  if (!HasHugeWorkingSetSize)
    computeThresholds();
  return HasHugeWorkingSetSize && HasHugeWorkingSetSize.getValue();
}

bool ProfileSummaryInfo::isHotCount(uint64_t C) {
  if (!HotCountThreshold)
    computeThresholds();
  return HotCountThreshold && C >= HotCountThreshold.getValue();
}

bool ProfileSummaryInfo::isColdCount(uint64_t C) {
  if (!ColdCountThreshold)
    computeThresholds();
  return ColdCountThreshold && C <= ColdCountThreshold.getValue();
}

uint64_t ProfileSummaryInfo::getOrCompHotCountThreshold() {
  if (!HotCountThreshold)
    computeThresholds();
  return HotCountThreshold ? HotCountThreshold.getValue() : UINT64_MAX;
}

uint64_t ProfileSummaryInfo::getOrCompColdCountThreshold() {
  if (!ColdCountThreshold)
    computeThresholds();
  return ColdCountThreshold ? ColdCountThreshold.getValue() : 0;
}

bool ProfileSummaryInfo::isHotBlock(const BasicBlock *BB, BlockFrequencyInfo *BFI) {
  auto Count = BFI->getBlockProfileCount(BB);
  return Count && isHotCount(*Count);
}

bool ProfileSummaryInfo::isColdBlock(const BasicBlock *BB,
                                  BlockFrequencyInfo *BFI) {
  auto Count = BFI->getBlockProfileCount(BB);
  return Count && isColdCount(*Count);
}

bool ProfileSummaryInfo::isHotCallSite(const CallSite &CS,
                                       BlockFrequencyInfo *BFI) {
  auto C = getProfileCount(CS.getInstruction(), BFI);
  return C && isHotCount(*C);
}

bool ProfileSummaryInfo::isColdCallSite(const CallSite &CS,
                                        BlockFrequencyInfo *BFI) {
  auto C = getProfileCount(CS.getInstruction(), BFI);
  if (C)
    return isColdCount(*C);

  // In SamplePGO, if the caller has been sampled, and there is no profile
  // annotated on the callsite, we consider the callsite as cold.
  return hasSampleProfile() && CS.getCaller()->hasProfileData();
}

INITIALIZE_PASS(ProfileSummaryInfoWrapperPass, "profile-summary-info",
                "Profile summary info", false, true)

ProfileSummaryInfoWrapperPass::ProfileSummaryInfoWrapperPass()
    : ImmutablePass(ID) {
  initializeProfileSummaryInfoWrapperPassPass(*PassRegistry::getPassRegistry());
}

bool ProfileSummaryInfoWrapperPass::doInitialization(Module &M) {
  PSI.reset(new ProfileSummaryInfo(M));
  return false;
}

bool ProfileSummaryInfoWrapperPass::doFinalization(Module &M) {
  PSI.reset();
  return false;
}

AnalysisKey ProfileSummaryAnalysis::Key;
ProfileSummaryInfo ProfileSummaryAnalysis::run(Module &M,
                                               ModuleAnalysisManager &) {
  return ProfileSummaryInfo(M);
}

PreservedAnalyses ProfileSummaryPrinterPass::run(Module &M,
                                                 ModuleAnalysisManager &AM) {
  ProfileSummaryInfo &PSI = AM.getResult<ProfileSummaryAnalysis>(M);

  OS << "Functions in " << M.getName() << " with hot/cold annotations: \n";
  for (auto &F : M) {
    OS << F.getName();
    if (PSI.isFunctionEntryHot(&F))
      OS << " :hot entry ";
    else if (PSI.isFunctionEntryCold(&F))
      OS << " :cold entry ";
    OS << "\n";
  }
  return PreservedAnalyses::all();
}

char ProfileSummaryInfoWrapperPass::ID = 0;
