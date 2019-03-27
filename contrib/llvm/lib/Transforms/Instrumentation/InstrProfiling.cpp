//===-- InstrProfiling.cpp - Frontend instrumentation based profiling -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass lowers instrprof_* intrinsics emitted by a frontend for profiling.
// It also builds the data structures and initialization code needed for
// updating execution counts and emitting the profile at runtime.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/InstrProfiling.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>

using namespace llvm;

#define DEBUG_TYPE "instrprof"

// The start and end values of precise value profile range for memory
// intrinsic sizes
cl::opt<std::string> MemOPSizeRange(
    "memop-size-range",
    cl::desc("Set the range of size in memory intrinsic calls to be profiled "
             "precisely, in a format of <start_val>:<end_val>"),
    cl::init(""));

// The value that considered to be large value in  memory intrinsic.
cl::opt<unsigned> MemOPSizeLarge(
    "memop-size-large",
    cl::desc("Set large value thresthold in memory intrinsic size profiling. "
             "Value of 0 disables the large value profiling."),
    cl::init(8192));

namespace {

cl::opt<bool> DoNameCompression("enable-name-compression",
                                cl::desc("Enable name string compression"),
                                cl::init(true));

cl::opt<bool> DoHashBasedCounterSplit(
    "hash-based-counter-split",
    cl::desc("Rename counter variable of a comdat function based on cfg hash"),
    cl::init(true));

cl::opt<bool> ValueProfileStaticAlloc(
    "vp-static-alloc",
    cl::desc("Do static counter allocation for value profiler"),
    cl::init(true));

cl::opt<double> NumCountersPerValueSite(
    "vp-counters-per-site",
    cl::desc("The average number of profile counters allocated "
             "per value profiling site."),
    // This is set to a very small value because in real programs, only
    // a very small percentage of value sites have non-zero targets, e.g, 1/30.
    // For those sites with non-zero profile, the average number of targets
    // is usually smaller than 2.
    cl::init(1.0));

cl::opt<bool> AtomicCounterUpdateAll(
    "instrprof-atomic-counter-update-all", cl::ZeroOrMore,
    cl::desc("Make all profile counter updates atomic (for testing only)"),
    cl::init(false));

cl::opt<bool> AtomicCounterUpdatePromoted(
    "atomic-counter-update-promoted", cl::ZeroOrMore,
    cl::desc("Do counter update using atomic fetch add "
             " for promoted counters only"),
    cl::init(false));

// If the option is not specified, the default behavior about whether
// counter promotion is done depends on how instrumentaiton lowering
// pipeline is setup, i.e., the default value of true of this option
// does not mean the promotion will be done by default. Explicitly
// setting this option can override the default behavior.
cl::opt<bool> DoCounterPromotion("do-counter-promotion", cl::ZeroOrMore,
                                 cl::desc("Do counter register promotion"),
                                 cl::init(false));
cl::opt<unsigned> MaxNumOfPromotionsPerLoop(
    cl::ZeroOrMore, "max-counter-promotions-per-loop", cl::init(20),
    cl::desc("Max number counter promotions per loop to avoid"
             " increasing register pressure too much"));

// A debug option
cl::opt<int>
    MaxNumOfPromotions(cl::ZeroOrMore, "max-counter-promotions", cl::init(-1),
                       cl::desc("Max number of allowed counter promotions"));

cl::opt<unsigned> SpeculativeCounterPromotionMaxExiting(
    cl::ZeroOrMore, "speculative-counter-promotion-max-exiting", cl::init(3),
    cl::desc("The max number of exiting blocks of a loop to allow "
             " speculative counter promotion"));

cl::opt<bool> SpeculativeCounterPromotionToLoop(
    cl::ZeroOrMore, "speculative-counter-promotion-to-loop", cl::init(false),
    cl::desc("When the option is false, if the target block is in a loop, "
             "the promotion will be disallowed unless the promoted counter "
             " update can be further/iteratively promoted into an acyclic "
             " region."));

cl::opt<bool> IterativeCounterPromotion(
    cl::ZeroOrMore, "iterative-counter-promotion", cl::init(true),
    cl::desc("Allow counter promotion across the whole loop nest."));

class InstrProfilingLegacyPass : public ModulePass {
  InstrProfiling InstrProf;

public:
  static char ID;

  InstrProfilingLegacyPass() : ModulePass(ID) {}
  InstrProfilingLegacyPass(const InstrProfOptions &Options)
      : ModulePass(ID), InstrProf(Options) {}

  StringRef getPassName() const override {
    return "Frontend instrumentation-based coverage lowering";
  }

  bool runOnModule(Module &M) override {
    return InstrProf.run(M, getAnalysis<TargetLibraryInfoWrapperPass>().getTLI());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
  }
};

///
/// A helper class to promote one counter RMW operation in the loop
/// into register update.
///
/// RWM update for the counter will be sinked out of the loop after
/// the transformation.
///
class PGOCounterPromoterHelper : public LoadAndStorePromoter {
public:
  PGOCounterPromoterHelper(
      Instruction *L, Instruction *S, SSAUpdater &SSA, Value *Init,
      BasicBlock *PH, ArrayRef<BasicBlock *> ExitBlocks,
      ArrayRef<Instruction *> InsertPts,
      DenseMap<Loop *, SmallVector<LoadStorePair, 8>> &LoopToCands,
      LoopInfo &LI)
      : LoadAndStorePromoter({L, S}, SSA), Store(S), ExitBlocks(ExitBlocks),
        InsertPts(InsertPts), LoopToCandidates(LoopToCands), LI(LI) {
    assert(isa<LoadInst>(L));
    assert(isa<StoreInst>(S));
    SSA.AddAvailableValue(PH, Init);
  }

  void doExtraRewritesBeforeFinalDeletion() const override {
    for (unsigned i = 0, e = ExitBlocks.size(); i != e; ++i) {
      BasicBlock *ExitBlock = ExitBlocks[i];
      Instruction *InsertPos = InsertPts[i];
      // Get LiveIn value into the ExitBlock. If there are multiple
      // predecessors, the value is defined by a PHI node in this
      // block.
      Value *LiveInValue = SSA.GetValueInMiddleOfBlock(ExitBlock);
      Value *Addr = cast<StoreInst>(Store)->getPointerOperand();
      IRBuilder<> Builder(InsertPos);
      if (AtomicCounterUpdatePromoted)
        // automic update currently can only be promoted across the current
        // loop, not the whole loop nest.
        Builder.CreateAtomicRMW(AtomicRMWInst::Add, Addr, LiveInValue,
                                AtomicOrdering::SequentiallyConsistent);
      else {
        LoadInst *OldVal = Builder.CreateLoad(Addr, "pgocount.promoted");
        auto *NewVal = Builder.CreateAdd(OldVal, LiveInValue);
        auto *NewStore = Builder.CreateStore(NewVal, Addr);

        // Now update the parent loop's candidate list:
        if (IterativeCounterPromotion) {
          auto *TargetLoop = LI.getLoopFor(ExitBlock);
          if (TargetLoop)
            LoopToCandidates[TargetLoop].emplace_back(OldVal, NewStore);
        }
      }
    }
  }

private:
  Instruction *Store;
  ArrayRef<BasicBlock *> ExitBlocks;
  ArrayRef<Instruction *> InsertPts;
  DenseMap<Loop *, SmallVector<LoadStorePair, 8>> &LoopToCandidates;
  LoopInfo &LI;
};

/// A helper class to do register promotion for all profile counter
/// updates in a loop.
///
class PGOCounterPromoter {
public:
  PGOCounterPromoter(
      DenseMap<Loop *, SmallVector<LoadStorePair, 8>> &LoopToCands,
      Loop &CurLoop, LoopInfo &LI)
      : LoopToCandidates(LoopToCands), ExitBlocks(), InsertPts(), L(CurLoop),
        LI(LI) {

    SmallVector<BasicBlock *, 8> LoopExitBlocks;
    SmallPtrSet<BasicBlock *, 8> BlockSet;
    L.getExitBlocks(LoopExitBlocks);

    for (BasicBlock *ExitBlock : LoopExitBlocks) {
      if (BlockSet.insert(ExitBlock).second) {
        ExitBlocks.push_back(ExitBlock);
        InsertPts.push_back(&*ExitBlock->getFirstInsertionPt());
      }
    }
  }

  bool run(int64_t *NumPromoted) {
    // Skip 'infinite' loops:
    if (ExitBlocks.size() == 0)
      return false;
    unsigned MaxProm = getMaxNumOfPromotionsInLoop(&L);
    if (MaxProm == 0)
      return false;

    unsigned Promoted = 0;
    for (auto &Cand : LoopToCandidates[&L]) {

      SmallVector<PHINode *, 4> NewPHIs;
      SSAUpdater SSA(&NewPHIs);
      Value *InitVal = ConstantInt::get(Cand.first->getType(), 0);

      PGOCounterPromoterHelper Promoter(Cand.first, Cand.second, SSA, InitVal,
                                        L.getLoopPreheader(), ExitBlocks,
                                        InsertPts, LoopToCandidates, LI);
      Promoter.run(SmallVector<Instruction *, 2>({Cand.first, Cand.second}));
      Promoted++;
      if (Promoted >= MaxProm)
        break;

      (*NumPromoted)++;
      if (MaxNumOfPromotions != -1 && *NumPromoted >= MaxNumOfPromotions)
        break;
    }

    LLVM_DEBUG(dbgs() << Promoted << " counters promoted for loop (depth="
                      << L.getLoopDepth() << ")\n");
    return Promoted != 0;
  }

private:
  bool allowSpeculativeCounterPromotion(Loop *LP) {
    SmallVector<BasicBlock *, 8> ExitingBlocks;
    L.getExitingBlocks(ExitingBlocks);
    // Not considierered speculative.
    if (ExitingBlocks.size() == 1)
      return true;
    if (ExitingBlocks.size() > SpeculativeCounterPromotionMaxExiting)
      return false;
    return true;
  }

  // Returns the max number of Counter Promotions for LP.
  unsigned getMaxNumOfPromotionsInLoop(Loop *LP) {
    // We can't insert into a catchswitch.
    SmallVector<BasicBlock *, 8> LoopExitBlocks;
    LP->getExitBlocks(LoopExitBlocks);
    if (llvm::any_of(LoopExitBlocks, [](BasicBlock *Exit) {
          return isa<CatchSwitchInst>(Exit->getTerminator());
        }))
      return 0;

    if (!LP->hasDedicatedExits())
      return 0;

    BasicBlock *PH = LP->getLoopPreheader();
    if (!PH)
      return 0;

    SmallVector<BasicBlock *, 8> ExitingBlocks;
    LP->getExitingBlocks(ExitingBlocks);
    // Not considierered speculative.
    if (ExitingBlocks.size() == 1)
      return MaxNumOfPromotionsPerLoop;

    if (ExitingBlocks.size() > SpeculativeCounterPromotionMaxExiting)
      return 0;

    // Whether the target block is in a loop does not matter:
    if (SpeculativeCounterPromotionToLoop)
      return MaxNumOfPromotionsPerLoop;

    // Now check the target block:
    unsigned MaxProm = MaxNumOfPromotionsPerLoop;
    for (auto *TargetBlock : LoopExitBlocks) {
      auto *TargetLoop = LI.getLoopFor(TargetBlock);
      if (!TargetLoop)
        continue;
      unsigned MaxPromForTarget = getMaxNumOfPromotionsInLoop(TargetLoop);
      unsigned PendingCandsInTarget = LoopToCandidates[TargetLoop].size();
      MaxProm =
          std::min(MaxProm, std::max(MaxPromForTarget, PendingCandsInTarget) -
                                PendingCandsInTarget);
    }
    return MaxProm;
  }

  DenseMap<Loop *, SmallVector<LoadStorePair, 8>> &LoopToCandidates;
  SmallVector<BasicBlock *, 8> ExitBlocks;
  SmallVector<Instruction *, 8> InsertPts;
  Loop &L;
  LoopInfo &LI;
};

} // end anonymous namespace

PreservedAnalyses InstrProfiling::run(Module &M, ModuleAnalysisManager &AM) {
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(M);
  if (!run(M, TLI))
    return PreservedAnalyses::all();

  return PreservedAnalyses::none();
}

char InstrProfilingLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(
    InstrProfilingLegacyPass, "instrprof",
    "Frontend instrumentation-based coverage lowering.", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(
    InstrProfilingLegacyPass, "instrprof",
    "Frontend instrumentation-based coverage lowering.", false, false)

ModulePass *
llvm::createInstrProfilingLegacyPass(const InstrProfOptions &Options) {
  return new InstrProfilingLegacyPass(Options);
}

static InstrProfIncrementInst *castToIncrementInst(Instruction *Instr) {
  InstrProfIncrementInst *Inc = dyn_cast<InstrProfIncrementInstStep>(Instr);
  if (Inc)
    return Inc;
  return dyn_cast<InstrProfIncrementInst>(Instr);
}

bool InstrProfiling::lowerIntrinsics(Function *F) {
  bool MadeChange = false;
  PromotionCandidates.clear();
  for (BasicBlock &BB : *F) {
    for (auto I = BB.begin(), E = BB.end(); I != E;) {
      auto Instr = I++;
      InstrProfIncrementInst *Inc = castToIncrementInst(&*Instr);
      if (Inc) {
        lowerIncrement(Inc);
        MadeChange = true;
      } else if (auto *Ind = dyn_cast<InstrProfValueProfileInst>(Instr)) {
        lowerValueProfileInst(Ind);
        MadeChange = true;
      }
    }
  }

  if (!MadeChange)
    return false;

  promoteCounterLoadStores(F);
  return true;
}

bool InstrProfiling::isCounterPromotionEnabled() const {
  if (DoCounterPromotion.getNumOccurrences() > 0)
    return DoCounterPromotion;

  return Options.DoCounterPromotion;
}

void InstrProfiling::promoteCounterLoadStores(Function *F) {
  if (!isCounterPromotionEnabled())
    return;

  DominatorTree DT(*F);
  LoopInfo LI(DT);
  DenseMap<Loop *, SmallVector<LoadStorePair, 8>> LoopPromotionCandidates;

  for (const auto &LoadStore : PromotionCandidates) {
    auto *CounterLoad = LoadStore.first;
    auto *CounterStore = LoadStore.second;
    BasicBlock *BB = CounterLoad->getParent();
    Loop *ParentLoop = LI.getLoopFor(BB);
    if (!ParentLoop)
      continue;
    LoopPromotionCandidates[ParentLoop].emplace_back(CounterLoad, CounterStore);
  }

  SmallVector<Loop *, 4> Loops = LI.getLoopsInPreorder();

  // Do a post-order traversal of the loops so that counter updates can be
  // iteratively hoisted outside the loop nest.
  for (auto *Loop : llvm::reverse(Loops)) {
    PGOCounterPromoter Promoter(LoopPromotionCandidates, *Loop, LI);
    Promoter.run(&TotalCountersPromoted);
  }
}

/// Check if the module contains uses of any profiling intrinsics.
static bool containsProfilingIntrinsics(Module &M) {
  if (auto *F = M.getFunction(
          Intrinsic::getName(llvm::Intrinsic::instrprof_increment)))
    if (!F->use_empty())
      return true;
  if (auto *F = M.getFunction(
          Intrinsic::getName(llvm::Intrinsic::instrprof_increment_step)))
    if (!F->use_empty())
      return true;
  if (auto *F = M.getFunction(
          Intrinsic::getName(llvm::Intrinsic::instrprof_value_profile)))
    if (!F->use_empty())
      return true;
  return false;
}

bool InstrProfiling::run(Module &M, const TargetLibraryInfo &TLI) {
  this->M = &M;
  this->TLI = &TLI;
  NamesVar = nullptr;
  NamesSize = 0;
  ProfileDataMap.clear();
  UsedVars.clear();
  getMemOPSizeRangeFromOption(MemOPSizeRange, MemOPSizeRangeStart,
                              MemOPSizeRangeLast);
  TT = Triple(M.getTargetTriple());

  // Emit the runtime hook even if no counters are present.
  bool MadeChange = emitRuntimeHook();

  // Improve compile time by avoiding linear scans when there is no work.
  GlobalVariable *CoverageNamesVar =
      M.getNamedGlobal(getCoverageUnusedNamesVarName());
  if (!containsProfilingIntrinsics(M) && !CoverageNamesVar)
    return MadeChange;

  // We did not know how many value sites there would be inside
  // the instrumented function. This is counting the number of instrumented
  // target value sites to enter it as field in the profile data variable.
  for (Function &F : M) {
    InstrProfIncrementInst *FirstProfIncInst = nullptr;
    for (BasicBlock &BB : F)
      for (auto I = BB.begin(), E = BB.end(); I != E; I++)
        if (auto *Ind = dyn_cast<InstrProfValueProfileInst>(I))
          computeNumValueSiteCounts(Ind);
        else if (FirstProfIncInst == nullptr)
          FirstProfIncInst = dyn_cast<InstrProfIncrementInst>(I);

    // Value profiling intrinsic lowering requires per-function profile data
    // variable to be created first.
    if (FirstProfIncInst != nullptr)
      static_cast<void>(getOrCreateRegionCounters(FirstProfIncInst));
  }

  for (Function &F : M)
    MadeChange |= lowerIntrinsics(&F);

  if (CoverageNamesVar) {
    lowerCoverageData(CoverageNamesVar);
    MadeChange = true;
  }

  if (!MadeChange)
    return false;

  emitVNodes();
  emitNameData();
  emitRegistration();
  emitUses();
  emitInitialization();
  return true;
}

static Constant *getOrInsertValueProfilingCall(Module &M,
                                               const TargetLibraryInfo &TLI,
                                               bool IsRange = false) {
  LLVMContext &Ctx = M.getContext();
  auto *ReturnTy = Type::getVoidTy(M.getContext());

  Constant *Res;
  if (!IsRange) {
    Type *ParamTypes[] = {
#define VALUE_PROF_FUNC_PARAM(ParamType, ParamName, ParamLLVMType) ParamLLVMType
#include "llvm/ProfileData/InstrProfData.inc"
    };
    auto *ValueProfilingCallTy =
        FunctionType::get(ReturnTy, makeArrayRef(ParamTypes), false);
    Res = M.getOrInsertFunction(getInstrProfValueProfFuncName(),
                                ValueProfilingCallTy);
  } else {
    Type *RangeParamTypes[] = {
#define VALUE_RANGE_PROF 1
#define VALUE_PROF_FUNC_PARAM(ParamType, ParamName, ParamLLVMType) ParamLLVMType
#include "llvm/ProfileData/InstrProfData.inc"
#undef VALUE_RANGE_PROF
    };
    auto *ValueRangeProfilingCallTy =
        FunctionType::get(ReturnTy, makeArrayRef(RangeParamTypes), false);
    Res = M.getOrInsertFunction(getInstrProfValueRangeProfFuncName(),
                                ValueRangeProfilingCallTy);
  }

  if (Function *FunRes = dyn_cast<Function>(Res)) {
    if (auto AK = TLI.getExtAttrForI32Param(false))
      FunRes->addParamAttr(2, AK);
  }
  return Res;
}

void InstrProfiling::computeNumValueSiteCounts(InstrProfValueProfileInst *Ind) {
  GlobalVariable *Name = Ind->getName();
  uint64_t ValueKind = Ind->getValueKind()->getZExtValue();
  uint64_t Index = Ind->getIndex()->getZExtValue();
  auto It = ProfileDataMap.find(Name);
  if (It == ProfileDataMap.end()) {
    PerFunctionProfileData PD;
    PD.NumValueSites[ValueKind] = Index + 1;
    ProfileDataMap[Name] = PD;
  } else if (It->second.NumValueSites[ValueKind] <= Index)
    It->second.NumValueSites[ValueKind] = Index + 1;
}

void InstrProfiling::lowerValueProfileInst(InstrProfValueProfileInst *Ind) {
  GlobalVariable *Name = Ind->getName();
  auto It = ProfileDataMap.find(Name);
  assert(It != ProfileDataMap.end() && It->second.DataVar &&
         "value profiling detected in function with no counter incerement");

  GlobalVariable *DataVar = It->second.DataVar;
  uint64_t ValueKind = Ind->getValueKind()->getZExtValue();
  uint64_t Index = Ind->getIndex()->getZExtValue();
  for (uint32_t Kind = IPVK_First; Kind < ValueKind; ++Kind)
    Index += It->second.NumValueSites[Kind];

  IRBuilder<> Builder(Ind);
  bool IsRange = (Ind->getValueKind()->getZExtValue() ==
                  llvm::InstrProfValueKind::IPVK_MemOPSize);
  CallInst *Call = nullptr;
  if (!IsRange) {
    Value *Args[3] = {Ind->getTargetValue(),
                      Builder.CreateBitCast(DataVar, Builder.getInt8PtrTy()),
                      Builder.getInt32(Index)};
    Call = Builder.CreateCall(getOrInsertValueProfilingCall(*M, *TLI), Args);
  } else {
    Value *Args[6] = {
        Ind->getTargetValue(),
        Builder.CreateBitCast(DataVar, Builder.getInt8PtrTy()),
        Builder.getInt32(Index),
        Builder.getInt64(MemOPSizeRangeStart),
        Builder.getInt64(MemOPSizeRangeLast),
        Builder.getInt64(MemOPSizeLarge == 0 ? INT64_MIN : MemOPSizeLarge)};
    Call =
        Builder.CreateCall(getOrInsertValueProfilingCall(*M, *TLI, true), Args);
  }
  if (auto AK = TLI->getExtAttrForI32Param(false))
    Call->addParamAttr(2, AK);
  Ind->replaceAllUsesWith(Call);
  Ind->eraseFromParent();
}

void InstrProfiling::lowerIncrement(InstrProfIncrementInst *Inc) {
  GlobalVariable *Counters = getOrCreateRegionCounters(Inc);

  IRBuilder<> Builder(Inc);
  uint64_t Index = Inc->getIndex()->getZExtValue();
  Value *Addr = Builder.CreateConstInBoundsGEP2_64(Counters, 0, Index);

  if (Options.Atomic || AtomicCounterUpdateAll) {
    Builder.CreateAtomicRMW(AtomicRMWInst::Add, Addr, Inc->getStep(),
                            AtomicOrdering::Monotonic);
  } else {
    Value *Load = Builder.CreateLoad(Addr, "pgocount");
    auto *Count = Builder.CreateAdd(Load, Inc->getStep());
    auto *Store = Builder.CreateStore(Count, Addr);
    if (isCounterPromotionEnabled())
      PromotionCandidates.emplace_back(cast<Instruction>(Load), Store);
  }
  Inc->eraseFromParent();
}

void InstrProfiling::lowerCoverageData(GlobalVariable *CoverageNamesVar) {
  ConstantArray *Names =
      cast<ConstantArray>(CoverageNamesVar->getInitializer());
  for (unsigned I = 0, E = Names->getNumOperands(); I < E; ++I) {
    Constant *NC = Names->getOperand(I);
    Value *V = NC->stripPointerCasts();
    assert(isa<GlobalVariable>(V) && "Missing reference to function name");
    GlobalVariable *Name = cast<GlobalVariable>(V);

    Name->setLinkage(GlobalValue::PrivateLinkage);
    ReferencedNames.push_back(Name);
    NC->dropAllReferences();
  }
  CoverageNamesVar->eraseFromParent();
}

/// Get the name of a profiling variable for a particular function.
static std::string getVarName(InstrProfIncrementInst *Inc, StringRef Prefix) {
  StringRef NamePrefix = getInstrProfNameVarPrefix();
  StringRef Name = Inc->getName()->getName().substr(NamePrefix.size());
  Function *F = Inc->getParent()->getParent();
  Module *M = F->getParent();
  if (!DoHashBasedCounterSplit || !isIRPGOFlagSet(M) ||
      !canRenameComdatFunc(*F))
    return (Prefix + Name).str();
  uint64_t FuncHash = Inc->getHash()->getZExtValue();
  SmallVector<char, 24> HashPostfix;
  if (Name.endswith((Twine(".") + Twine(FuncHash)).toStringRef(HashPostfix)))
    return (Prefix + Name).str();
  return (Prefix + Name + "." + Twine(FuncHash)).str();
}

static inline bool shouldRecordFunctionAddr(Function *F) {
  // Check the linkage
  bool HasAvailableExternallyLinkage = F->hasAvailableExternallyLinkage();
  if (!F->hasLinkOnceLinkage() && !F->hasLocalLinkage() &&
      !HasAvailableExternallyLinkage)
    return true;

  // A function marked 'alwaysinline' with available_externally linkage can't
  // have its address taken. Doing so would create an undefined external ref to
  // the function, which would fail to link.
  if (HasAvailableExternallyLinkage &&
      F->hasFnAttribute(Attribute::AlwaysInline))
    return false;

  // Prohibit function address recording if the function is both internal and
  // COMDAT. This avoids the profile data variable referencing internal symbols
  // in COMDAT.
  if (F->hasLocalLinkage() && F->hasComdat())
    return false;

  // Check uses of this function for other than direct calls or invokes to it.
  // Inline virtual functions have linkeOnceODR linkage. When a key method
  // exists, the vtable will only be emitted in the TU where the key method
  // is defined. In a TU where vtable is not available, the function won't
  // be 'addresstaken'. If its address is not recorded here, the profile data
  // with missing address may be picked by the linker leading  to missing
  // indirect call target info.
  return F->hasAddressTaken() || F->hasLinkOnceLinkage();
}

static inline Comdat *getOrCreateProfileComdat(Module &M, Function &F,
                                               InstrProfIncrementInst *Inc) {
  if (!needsComdatForCounter(F, M))
    return nullptr;

  // COFF format requires a COMDAT section to have a key symbol with the same
  // name. The linker targeting COFF also requires that the COMDAT
  // a section is associated to must precede the associating section. For this
  // reason, we must choose the counter var's name as the name of the comdat.
  StringRef ComdatPrefix = (Triple(M.getTargetTriple()).isOSBinFormatCOFF()
                                ? getInstrProfCountersVarPrefix()
                                : getInstrProfComdatPrefix());
  return M.getOrInsertComdat(StringRef(getVarName(Inc, ComdatPrefix)));
}

static bool needsRuntimeRegistrationOfSectionRange(const Module &M) {
  // Don't do this for Darwin.  compiler-rt uses linker magic.
  if (Triple(M.getTargetTriple()).isOSDarwin())
    return false;

  // Use linker script magic to get data/cnts/name start/end.
  if (Triple(M.getTargetTriple()).isOSLinux() ||
      Triple(M.getTargetTriple()).isOSFreeBSD() ||
      Triple(M.getTargetTriple()).isOSNetBSD() ||
      Triple(M.getTargetTriple()).isOSFuchsia() ||
      Triple(M.getTargetTriple()).isPS4CPU())
    return false;

  return true;
}

GlobalVariable *
InstrProfiling::getOrCreateRegionCounters(InstrProfIncrementInst *Inc) {
  GlobalVariable *NamePtr = Inc->getName();
  auto It = ProfileDataMap.find(NamePtr);
  PerFunctionProfileData PD;
  if (It != ProfileDataMap.end()) {
    if (It->second.RegionCounters)
      return It->second.RegionCounters;
    PD = It->second;
  }

  // Move the name variable to the right section. Place them in a COMDAT group
  // if the associated function is a COMDAT. This will make sure that
  // only one copy of counters of the COMDAT function will be emitted after
  // linking.
  Function *Fn = Inc->getParent()->getParent();
  Comdat *ProfileVarsComdat = nullptr;
  ProfileVarsComdat = getOrCreateProfileComdat(*M, *Fn, Inc);

  uint64_t NumCounters = Inc->getNumCounters()->getZExtValue();
  LLVMContext &Ctx = M->getContext();
  ArrayType *CounterTy = ArrayType::get(Type::getInt64Ty(Ctx), NumCounters);

  // Create the counters variable.
  auto *CounterPtr =
      new GlobalVariable(*M, CounterTy, false, NamePtr->getLinkage(),
                         Constant::getNullValue(CounterTy),
                         getVarName(Inc, getInstrProfCountersVarPrefix()));
  CounterPtr->setVisibility(NamePtr->getVisibility());
  CounterPtr->setSection(
      getInstrProfSectionName(IPSK_cnts, TT.getObjectFormat()));
  CounterPtr->setAlignment(8);
  CounterPtr->setComdat(ProfileVarsComdat);

  auto *Int8PtrTy = Type::getInt8PtrTy(Ctx);
  // Allocate statically the array of pointers to value profile nodes for
  // the current function.
  Constant *ValuesPtrExpr = ConstantPointerNull::get(Int8PtrTy);
  if (ValueProfileStaticAlloc && !needsRuntimeRegistrationOfSectionRange(*M)) {
    uint64_t NS = 0;
    for (uint32_t Kind = IPVK_First; Kind <= IPVK_Last; ++Kind)
      NS += PD.NumValueSites[Kind];
    if (NS) {
      ArrayType *ValuesTy = ArrayType::get(Type::getInt64Ty(Ctx), NS);

      auto *ValuesVar =
          new GlobalVariable(*M, ValuesTy, false, NamePtr->getLinkage(),
                             Constant::getNullValue(ValuesTy),
                             getVarName(Inc, getInstrProfValuesVarPrefix()));
      ValuesVar->setVisibility(NamePtr->getVisibility());
      ValuesVar->setSection(
          getInstrProfSectionName(IPSK_vals, TT.getObjectFormat()));
      ValuesVar->setAlignment(8);
      ValuesVar->setComdat(ProfileVarsComdat);
      ValuesPtrExpr =
          ConstantExpr::getBitCast(ValuesVar, Type::getInt8PtrTy(Ctx));
    }
  }

  // Create data variable.
  auto *Int16Ty = Type::getInt16Ty(Ctx);
  auto *Int16ArrayTy = ArrayType::get(Int16Ty, IPVK_Last + 1);
  Type *DataTypes[] = {
#define INSTR_PROF_DATA(Type, LLVMType, Name, Init) LLVMType,
#include "llvm/ProfileData/InstrProfData.inc"
  };
  auto *DataTy = StructType::get(Ctx, makeArrayRef(DataTypes));

  Constant *FunctionAddr = shouldRecordFunctionAddr(Fn)
                               ? ConstantExpr::getBitCast(Fn, Int8PtrTy)
                               : ConstantPointerNull::get(Int8PtrTy);

  Constant *Int16ArrayVals[IPVK_Last + 1];
  for (uint32_t Kind = IPVK_First; Kind <= IPVK_Last; ++Kind)
    Int16ArrayVals[Kind] = ConstantInt::get(Int16Ty, PD.NumValueSites[Kind]);

  Constant *DataVals[] = {
#define INSTR_PROF_DATA(Type, LLVMType, Name, Init) Init,
#include "llvm/ProfileData/InstrProfData.inc"
  };
  auto *Data = new GlobalVariable(*M, DataTy, false, NamePtr->getLinkage(),
                                  ConstantStruct::get(DataTy, DataVals),
                                  getVarName(Inc, getInstrProfDataVarPrefix()));
  Data->setVisibility(NamePtr->getVisibility());
  Data->setSection(getInstrProfSectionName(IPSK_data, TT.getObjectFormat()));
  Data->setAlignment(INSTR_PROF_DATA_ALIGNMENT);
  Data->setComdat(ProfileVarsComdat);

  PD.RegionCounters = CounterPtr;
  PD.DataVar = Data;
  ProfileDataMap[NamePtr] = PD;

  // Mark the data variable as used so that it isn't stripped out.
  UsedVars.push_back(Data);
  // Now that the linkage set by the FE has been passed to the data and counter
  // variables, reset Name variable's linkage and visibility to private so that
  // it can be removed later by the compiler.
  NamePtr->setLinkage(GlobalValue::PrivateLinkage);
  // Collect the referenced names to be used by emitNameData.
  ReferencedNames.push_back(NamePtr);

  return CounterPtr;
}

void InstrProfiling::emitVNodes() {
  if (!ValueProfileStaticAlloc)
    return;

  // For now only support this on platforms that do
  // not require runtime registration to discover
  // named section start/end.
  if (needsRuntimeRegistrationOfSectionRange(*M))
    return;

  size_t TotalNS = 0;
  for (auto &PD : ProfileDataMap) {
    for (uint32_t Kind = IPVK_First; Kind <= IPVK_Last; ++Kind)
      TotalNS += PD.second.NumValueSites[Kind];
  }

  if (!TotalNS)
    return;

  uint64_t NumCounters = TotalNS * NumCountersPerValueSite;
// Heuristic for small programs with very few total value sites.
// The default value of vp-counters-per-site is chosen based on
// the observation that large apps usually have a low percentage
// of value sites that actually have any profile data, and thus
// the average number of counters per site is low. For small
// apps with very few sites, this may not be true. Bump up the
// number of counters in this case.
#define INSTR_PROF_MIN_VAL_COUNTS 10
  if (NumCounters < INSTR_PROF_MIN_VAL_COUNTS)
    NumCounters = std::max(INSTR_PROF_MIN_VAL_COUNTS, (int)NumCounters * 2);

  auto &Ctx = M->getContext();
  Type *VNodeTypes[] = {
#define INSTR_PROF_VALUE_NODE(Type, LLVMType, Name, Init) LLVMType,
#include "llvm/ProfileData/InstrProfData.inc"
  };
  auto *VNodeTy = StructType::get(Ctx, makeArrayRef(VNodeTypes));

  ArrayType *VNodesTy = ArrayType::get(VNodeTy, NumCounters);
  auto *VNodesVar = new GlobalVariable(
      *M, VNodesTy, false, GlobalValue::PrivateLinkage,
      Constant::getNullValue(VNodesTy), getInstrProfVNodesVarName());
  VNodesVar->setSection(
      getInstrProfSectionName(IPSK_vnodes, TT.getObjectFormat()));
  UsedVars.push_back(VNodesVar);
}

void InstrProfiling::emitNameData() {
  std::string UncompressedData;

  if (ReferencedNames.empty())
    return;

  std::string CompressedNameStr;
  if (Error E = collectPGOFuncNameStrings(ReferencedNames, CompressedNameStr,
                                          DoNameCompression)) {
    report_fatal_error(toString(std::move(E)), false);
  }

  auto &Ctx = M->getContext();
  auto *NamesVal = ConstantDataArray::getString(
      Ctx, StringRef(CompressedNameStr), false);
  NamesVar = new GlobalVariable(*M, NamesVal->getType(), true,
                                GlobalValue::PrivateLinkage, NamesVal,
                                getInstrProfNamesVarName());
  NamesSize = CompressedNameStr.size();
  NamesVar->setSection(
      getInstrProfSectionName(IPSK_name, TT.getObjectFormat()));
  UsedVars.push_back(NamesVar);

  for (auto *NamePtr : ReferencedNames)
    NamePtr->eraseFromParent();
}

void InstrProfiling::emitRegistration() {
  if (!needsRuntimeRegistrationOfSectionRange(*M))
    return;

  // Construct the function.
  auto *VoidTy = Type::getVoidTy(M->getContext());
  auto *VoidPtrTy = Type::getInt8PtrTy(M->getContext());
  auto *Int64Ty = Type::getInt64Ty(M->getContext());
  auto *RegisterFTy = FunctionType::get(VoidTy, false);
  auto *RegisterF = Function::Create(RegisterFTy, GlobalValue::InternalLinkage,
                                     getInstrProfRegFuncsName(), M);
  RegisterF->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  if (Options.NoRedZone)
    RegisterF->addFnAttr(Attribute::NoRedZone);

  auto *RuntimeRegisterTy = FunctionType::get(VoidTy, VoidPtrTy, false);
  auto *RuntimeRegisterF =
      Function::Create(RuntimeRegisterTy, GlobalVariable::ExternalLinkage,
                       getInstrProfRegFuncName(), M);

  IRBuilder<> IRB(BasicBlock::Create(M->getContext(), "", RegisterF));
  for (Value *Data : UsedVars)
    if (Data != NamesVar && !isa<Function>(Data))
      IRB.CreateCall(RuntimeRegisterF, IRB.CreateBitCast(Data, VoidPtrTy));

  if (NamesVar) {
    Type *ParamTypes[] = {VoidPtrTy, Int64Ty};
    auto *NamesRegisterTy =
        FunctionType::get(VoidTy, makeArrayRef(ParamTypes), false);
    auto *NamesRegisterF =
        Function::Create(NamesRegisterTy, GlobalVariable::ExternalLinkage,
                         getInstrProfNamesRegFuncName(), M);
    IRB.CreateCall(NamesRegisterF, {IRB.CreateBitCast(NamesVar, VoidPtrTy),
                                    IRB.getInt64(NamesSize)});
  }

  IRB.CreateRetVoid();
}

bool InstrProfiling::emitRuntimeHook() {
  // We expect the linker to be invoked with -u<hook_var> flag for linux,
  // for which case there is no need to emit the user function.
  if (Triple(M->getTargetTriple()).isOSLinux())
    return false;

  // If the module's provided its own runtime, we don't need to do anything.
  if (M->getGlobalVariable(getInstrProfRuntimeHookVarName()))
    return false;

  // Declare an external variable that will pull in the runtime initialization.
  auto *Int32Ty = Type::getInt32Ty(M->getContext());
  auto *Var =
      new GlobalVariable(*M, Int32Ty, false, GlobalValue::ExternalLinkage,
                         nullptr, getInstrProfRuntimeHookVarName());

  // Make a function that uses it.
  auto *User = Function::Create(FunctionType::get(Int32Ty, false),
                                GlobalValue::LinkOnceODRLinkage,
                                getInstrProfRuntimeHookVarUseFuncName(), M);
  User->addFnAttr(Attribute::NoInline);
  if (Options.NoRedZone)
    User->addFnAttr(Attribute::NoRedZone);
  User->setVisibility(GlobalValue::HiddenVisibility);
  if (Triple(M->getTargetTriple()).supportsCOMDAT())
    User->setComdat(M->getOrInsertComdat(User->getName()));

  IRBuilder<> IRB(BasicBlock::Create(M->getContext(), "", User));
  auto *Load = IRB.CreateLoad(Var);
  IRB.CreateRet(Load);

  // Mark the user variable as used so that it isn't stripped out.
  UsedVars.push_back(User);
  return true;
}

void InstrProfiling::emitUses() {
  if (!UsedVars.empty())
    appendToUsed(*M, UsedVars);
}

void InstrProfiling::emitInitialization() {
  StringRef InstrProfileOutput = Options.InstrProfileOutput;

  if (!InstrProfileOutput.empty()) {
    // Create variable for profile name.
    Constant *ProfileNameConst =
        ConstantDataArray::getString(M->getContext(), InstrProfileOutput, true);
    GlobalVariable *ProfileNameVar = new GlobalVariable(
        *M, ProfileNameConst->getType(), true, GlobalValue::WeakAnyLinkage,
        ProfileNameConst, INSTR_PROF_QUOTE(INSTR_PROF_PROFILE_NAME_VAR));
    if (TT.supportsCOMDAT()) {
      ProfileNameVar->setLinkage(GlobalValue::ExternalLinkage);
      ProfileNameVar->setComdat(M->getOrInsertComdat(
          StringRef(INSTR_PROF_QUOTE(INSTR_PROF_PROFILE_NAME_VAR))));
    }
  }

  Constant *RegisterF = M->getFunction(getInstrProfRegFuncsName());
  if (!RegisterF)
    return;

  // Create the initialization function.
  auto *VoidTy = Type::getVoidTy(M->getContext());
  auto *F = Function::Create(FunctionType::get(VoidTy, false),
                             GlobalValue::InternalLinkage,
                             getInstrProfInitFuncName(), M);
  F->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  F->addFnAttr(Attribute::NoInline);
  if (Options.NoRedZone)
    F->addFnAttr(Attribute::NoRedZone);

  // Add the basic block and the necessary calls.
  IRBuilder<> IRB(BasicBlock::Create(M->getContext(), "", F));
  if (RegisterF)
    IRB.CreateCall(RegisterF, {});
  IRB.CreateRetVoid();

  appendToGlobalCtors(*M, F, 0);
}
