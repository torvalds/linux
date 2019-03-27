//===-- PGOMemOPSizeOpt.cpp - Optimizations based on value profiling ===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the transformation that optimizes memory intrinsics
// such as memcpy using the size value profile. When memory intrinsic size
// value profile metadata is available, a single memory intrinsic is expanded
// to a sequence of guarded specialized versions that are called with the
// hottest size(s), for later expansion into more optimal inline sequences.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DomTreeUpdater.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/PassSupport.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Transforms/Instrumentation/PGOInstrumentation.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <cassert>
#include <cstdint>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "pgo-memop-opt"

STATISTIC(NumOfPGOMemOPOpt, "Number of memop intrinsics optimized.");
STATISTIC(NumOfPGOMemOPAnnotate, "Number of memop intrinsics annotated.");

// The minimum call count to optimize memory intrinsic calls.
static cl::opt<unsigned>
    MemOPCountThreshold("pgo-memop-count-threshold", cl::Hidden, cl::ZeroOrMore,
                        cl::init(1000),
                        cl::desc("The minimum count to optimize memory "
                                 "intrinsic calls"));

// Command line option to disable memory intrinsic optimization. The default is
// false. This is for debug purpose.
static cl::opt<bool> DisableMemOPOPT("disable-memop-opt", cl::init(false),
                                     cl::Hidden, cl::desc("Disable optimize"));

// The percent threshold to optimize memory intrinsic calls.
static cl::opt<unsigned>
    MemOPPercentThreshold("pgo-memop-percent-threshold", cl::init(40),
                          cl::Hidden, cl::ZeroOrMore,
                          cl::desc("The percentage threshold for the "
                                   "memory intrinsic calls optimization"));

// Maximum number of versions for optimizing memory intrinsic call.
static cl::opt<unsigned>
    MemOPMaxVersion("pgo-memop-max-version", cl::init(3), cl::Hidden,
                    cl::ZeroOrMore,
                    cl::desc("The max version for the optimized memory "
                             " intrinsic calls"));

// Scale the counts from the annotation using the BB count value.
static cl::opt<bool>
    MemOPScaleCount("pgo-memop-scale-count", cl::init(true), cl::Hidden,
                    cl::desc("Scale the memop size counts using the basic "
                             " block count value"));

// This option sets the rangge of precise profile memop sizes.
extern cl::opt<std::string> MemOPSizeRange;

// This option sets the value that groups large memop sizes
extern cl::opt<unsigned> MemOPSizeLarge;

namespace {
class PGOMemOPSizeOptLegacyPass : public FunctionPass {
public:
  static char ID;

  PGOMemOPSizeOptLegacyPass() : FunctionPass(ID) {
    initializePGOMemOPSizeOptLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override { return "PGOMemOPSize"; }

private:
  bool runOnFunction(Function &F) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<BlockFrequencyInfoWrapperPass>();
    AU.addRequired<OptimizationRemarkEmitterWrapperPass>();
    AU.addPreserved<GlobalsAAWrapperPass>();
    AU.addPreserved<DominatorTreeWrapperPass>();
  }
};
} // end anonymous namespace

char PGOMemOPSizeOptLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(PGOMemOPSizeOptLegacyPass, "pgo-memop-opt",
                      "Optimize memory intrinsic using its size value profile",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(BlockFrequencyInfoWrapperPass)
INITIALIZE_PASS_END(PGOMemOPSizeOptLegacyPass, "pgo-memop-opt",
                    "Optimize memory intrinsic using its size value profile",
                    false, false)

FunctionPass *llvm::createPGOMemOPSizeOptLegacyPass() {
  return new PGOMemOPSizeOptLegacyPass();
}

namespace {
class MemOPSizeOpt : public InstVisitor<MemOPSizeOpt> {
public:
  MemOPSizeOpt(Function &Func, BlockFrequencyInfo &BFI,
               OptimizationRemarkEmitter &ORE, DominatorTree *DT)
      : Func(Func), BFI(BFI), ORE(ORE), DT(DT), Changed(false) {
    ValueDataArray =
        llvm::make_unique<InstrProfValueData[]>(MemOPMaxVersion + 2);
    // Get the MemOPSize range information from option MemOPSizeRange,
    getMemOPSizeRangeFromOption(MemOPSizeRange, PreciseRangeStart,
                                PreciseRangeLast);
  }
  bool isChanged() const { return Changed; }
  void perform() {
    WorkList.clear();
    visit(Func);

    for (auto &MI : WorkList) {
      ++NumOfPGOMemOPAnnotate;
      if (perform(MI)) {
        Changed = true;
        ++NumOfPGOMemOPOpt;
        LLVM_DEBUG(dbgs() << "MemOP call: "
                          << MI->getCalledFunction()->getName()
                          << "is Transformed.\n");
      }
    }
  }

  void visitMemIntrinsic(MemIntrinsic &MI) {
    Value *Length = MI.getLength();
    // Not perform on constant length calls.
    if (dyn_cast<ConstantInt>(Length))
      return;
    WorkList.push_back(&MI);
  }

private:
  Function &Func;
  BlockFrequencyInfo &BFI;
  OptimizationRemarkEmitter &ORE;
  DominatorTree *DT;
  bool Changed;
  std::vector<MemIntrinsic *> WorkList;
  // Start of the previse range.
  int64_t PreciseRangeStart;
  // Last value of the previse range.
  int64_t PreciseRangeLast;
  // The space to read the profile annotation.
  std::unique_ptr<InstrProfValueData[]> ValueDataArray;
  bool perform(MemIntrinsic *MI);

  // This kind shows which group the value falls in. For PreciseValue, we have
  // the profile count for that value. LargeGroup groups the values that are in
  // range [LargeValue, +inf). NonLargeGroup groups the rest of values.
  enum MemOPSizeKind { PreciseValue, NonLargeGroup, LargeGroup };

  MemOPSizeKind getMemOPSizeKind(int64_t Value) const {
    if (Value == MemOPSizeLarge && MemOPSizeLarge != 0)
      return LargeGroup;
    if (Value == PreciseRangeLast + 1)
      return NonLargeGroup;
    return PreciseValue;
  }
};

static const char *getMIName(const MemIntrinsic *MI) {
  switch (MI->getIntrinsicID()) {
  case Intrinsic::memcpy:
    return "memcpy";
  case Intrinsic::memmove:
    return "memmove";
  case Intrinsic::memset:
    return "memset";
  default:
    return "unknown";
  }
}

static bool isProfitable(uint64_t Count, uint64_t TotalCount) {
  assert(Count <= TotalCount);
  if (Count < MemOPCountThreshold)
    return false;
  if (Count < TotalCount * MemOPPercentThreshold / 100)
    return false;
  return true;
}

static inline uint64_t getScaledCount(uint64_t Count, uint64_t Num,
                                      uint64_t Denom) {
  if (!MemOPScaleCount)
    return Count;
  bool Overflowed;
  uint64_t ScaleCount = SaturatingMultiply(Count, Num, &Overflowed);
  return ScaleCount / Denom;
}

bool MemOPSizeOpt::perform(MemIntrinsic *MI) {
  assert(MI);
  if (MI->getIntrinsicID() == Intrinsic::memmove)
    return false;

  uint32_t NumVals, MaxNumPromotions = MemOPMaxVersion + 2;
  uint64_t TotalCount;
  if (!getValueProfDataFromInst(*MI, IPVK_MemOPSize, MaxNumPromotions,
                                ValueDataArray.get(), NumVals, TotalCount))
    return false;

  uint64_t ActualCount = TotalCount;
  uint64_t SavedTotalCount = TotalCount;
  if (MemOPScaleCount) {
    auto BBEdgeCount = BFI.getBlockProfileCount(MI->getParent());
    if (!BBEdgeCount)
      return false;
    ActualCount = *BBEdgeCount;
  }

  ArrayRef<InstrProfValueData> VDs(ValueDataArray.get(), NumVals);
  LLVM_DEBUG(dbgs() << "Read one memory intrinsic profile with count "
                    << ActualCount << "\n");
  LLVM_DEBUG(
      for (auto &VD
           : VDs) { dbgs() << "  (" << VD.Value << "," << VD.Count << ")\n"; });

  if (ActualCount < MemOPCountThreshold)
    return false;
  // Skip if the total value profiled count is 0, in which case we can't
  // scale up the counts properly (and there is no profitable transformation).
  if (TotalCount == 0)
    return false;

  TotalCount = ActualCount;
  if (MemOPScaleCount)
    LLVM_DEBUG(dbgs() << "Scale counts: numerator = " << ActualCount
                      << " denominator = " << SavedTotalCount << "\n");

  // Keeping track of the count of the default case:
  uint64_t RemainCount = TotalCount;
  uint64_t SavedRemainCount = SavedTotalCount;
  SmallVector<uint64_t, 16> SizeIds;
  SmallVector<uint64_t, 16> CaseCounts;
  uint64_t MaxCount = 0;
  unsigned Version = 0;
  // Default case is in the front -- save the slot here.
  CaseCounts.push_back(0);
  for (auto &VD : VDs) {
    int64_t V = VD.Value;
    uint64_t C = VD.Count;
    if (MemOPScaleCount)
      C = getScaledCount(C, ActualCount, SavedTotalCount);

    // Only care precise value here.
    if (getMemOPSizeKind(V) != PreciseValue)
      continue;

    // ValueCounts are sorted on the count. Break at the first un-profitable
    // value.
    if (!isProfitable(C, RemainCount))
      break;

    SizeIds.push_back(V);
    CaseCounts.push_back(C);
    if (C > MaxCount)
      MaxCount = C;

    assert(RemainCount >= C);
    RemainCount -= C;
    assert(SavedRemainCount >= VD.Count);
    SavedRemainCount -= VD.Count;

    if (++Version > MemOPMaxVersion && MemOPMaxVersion != 0)
      break;
  }

  if (Version == 0)
    return false;

  CaseCounts[0] = RemainCount;
  if (RemainCount > MaxCount)
    MaxCount = RemainCount;

  uint64_t SumForOpt = TotalCount - RemainCount;

  LLVM_DEBUG(dbgs() << "Optimize one memory intrinsic call to " << Version
                    << " Versions (covering " << SumForOpt << " out of "
                    << TotalCount << ")\n");

  // mem_op(..., size)
  // ==>
  // switch (size) {
  //   case s1:
  //      mem_op(..., s1);
  //      goto merge_bb;
  //   case s2:
  //      mem_op(..., s2);
  //      goto merge_bb;
  //   ...
  //   default:
  //      mem_op(..., size);
  //      goto merge_bb;
  // }
  // merge_bb:

  BasicBlock *BB = MI->getParent();
  LLVM_DEBUG(dbgs() << "\n\n== Basic Block Before ==\n");
  LLVM_DEBUG(dbgs() << *BB << "\n");
  auto OrigBBFreq = BFI.getBlockFreq(BB);

  BasicBlock *DefaultBB = SplitBlock(BB, MI, DT);
  BasicBlock::iterator It(*MI);
  ++It;
  assert(It != DefaultBB->end());
  BasicBlock *MergeBB = SplitBlock(DefaultBB, &(*It), DT);
  MergeBB->setName("MemOP.Merge");
  BFI.setBlockFreq(MergeBB, OrigBBFreq.getFrequency());
  DefaultBB->setName("MemOP.Default");

  DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Eager);
  auto &Ctx = Func.getContext();
  IRBuilder<> IRB(BB);
  BB->getTerminator()->eraseFromParent();
  Value *SizeVar = MI->getLength();
  SwitchInst *SI = IRB.CreateSwitch(SizeVar, DefaultBB, SizeIds.size());

  // Clear the value profile data.
  MI->setMetadata(LLVMContext::MD_prof, nullptr);
  // If all promoted, we don't need the MD.prof metadata.
  if (SavedRemainCount > 0 || Version != NumVals)
    // Otherwise we need update with the un-promoted records back.
    annotateValueSite(*Func.getParent(), *MI, VDs.slice(Version),
                      SavedRemainCount, IPVK_MemOPSize, NumVals);

  LLVM_DEBUG(dbgs() << "\n\n== Basic Block After==\n");

  std::vector<DominatorTree::UpdateType> Updates;
  if (DT)
    Updates.reserve(2 * SizeIds.size());

  for (uint64_t SizeId : SizeIds) {
    BasicBlock *CaseBB = BasicBlock::Create(
        Ctx, Twine("MemOP.Case.") + Twine(SizeId), &Func, DefaultBB);
    Instruction *NewInst = MI->clone();
    // Fix the argument.
    MemIntrinsic * MemI = dyn_cast<MemIntrinsic>(NewInst);
    IntegerType *SizeType = dyn_cast<IntegerType>(MemI->getLength()->getType());
    assert(SizeType && "Expected integer type size argument.");
    ConstantInt *CaseSizeId = ConstantInt::get(SizeType, SizeId);
    MemI->setLength(CaseSizeId);
    CaseBB->getInstList().push_back(NewInst);
    IRBuilder<> IRBCase(CaseBB);
    IRBCase.CreateBr(MergeBB);
    SI->addCase(CaseSizeId, CaseBB);
    if (DT) {
      Updates.push_back({DominatorTree::Insert, CaseBB, MergeBB});
      Updates.push_back({DominatorTree::Insert, BB, CaseBB});
    }
    LLVM_DEBUG(dbgs() << *CaseBB << "\n");
  }
  DTU.applyUpdates(Updates);
  Updates.clear();

  setProfMetadata(Func.getParent(), SI, CaseCounts, MaxCount);

  LLVM_DEBUG(dbgs() << *BB << "\n");
  LLVM_DEBUG(dbgs() << *DefaultBB << "\n");
  LLVM_DEBUG(dbgs() << *MergeBB << "\n");

  ORE.emit([&]() {
    using namespace ore;
    return OptimizationRemark(DEBUG_TYPE, "memopt-opt", MI)
             << "optimized " << NV("Intrinsic", StringRef(getMIName(MI)))
             << " with count " << NV("Count", SumForOpt) << " out of "
             << NV("Total", TotalCount) << " for " << NV("Versions", Version)
             << " versions";
  });

  return true;
}
} // namespace

static bool PGOMemOPSizeOptImpl(Function &F, BlockFrequencyInfo &BFI,
                                OptimizationRemarkEmitter &ORE,
                                DominatorTree *DT) {
  if (DisableMemOPOPT)
    return false;

  if (F.hasFnAttribute(Attribute::OptimizeForSize))
    return false;
  MemOPSizeOpt MemOPSizeOpt(F, BFI, ORE, DT);
  MemOPSizeOpt.perform();
  return MemOPSizeOpt.isChanged();
}

bool PGOMemOPSizeOptLegacyPass::runOnFunction(Function &F) {
  BlockFrequencyInfo &BFI =
      getAnalysis<BlockFrequencyInfoWrapperPass>().getBFI();
  auto &ORE = getAnalysis<OptimizationRemarkEmitterWrapperPass>().getORE();
  auto *DTWP = getAnalysisIfAvailable<DominatorTreeWrapperPass>();
  DominatorTree *DT = DTWP ? &DTWP->getDomTree() : nullptr;
  return PGOMemOPSizeOptImpl(F, BFI, ORE, DT);
}

namespace llvm {
char &PGOMemOPSizeOptID = PGOMemOPSizeOptLegacyPass::ID;

PreservedAnalyses PGOMemOPSizeOpt::run(Function &F,
                                       FunctionAnalysisManager &FAM) {
  auto &BFI = FAM.getResult<BlockFrequencyAnalysis>(F);
  auto &ORE = FAM.getResult<OptimizationRemarkEmitterAnalysis>(F);
  auto *DT = FAM.getCachedResult<DominatorTreeAnalysis>(F);
  bool Changed = PGOMemOPSizeOptImpl(F, BFI, ORE, DT);
  if (!Changed)
    return PreservedAnalyses::all();
  auto PA = PreservedAnalyses();
  PA.preserve<GlobalsAA>();
  PA.preserve<DominatorTreeAnalysis>();
  return PA;
}
} // namespace llvm
