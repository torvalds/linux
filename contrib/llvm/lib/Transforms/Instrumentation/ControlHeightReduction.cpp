//===-- ControlHeightReduction.cpp - Control Height Reduction -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass merges conditional blocks of code and reduces the number of
// conditional branches in the hot paths based on profiles.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/ControlHeightReduction.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/RegionIterator.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

#include <set>
#include <sstream>

using namespace llvm;

#define DEBUG_TYPE "chr"

#define CHR_DEBUG(X) LLVM_DEBUG(X)

static cl::opt<bool> ForceCHR("force-chr", cl::init(false), cl::Hidden,
                              cl::desc("Apply CHR for all functions"));

static cl::opt<double> CHRBiasThreshold(
    "chr-bias-threshold", cl::init(0.99), cl::Hidden,
    cl::desc("CHR considers a branch bias greater than this ratio as biased"));

static cl::opt<unsigned> CHRMergeThreshold(
    "chr-merge-threshold", cl::init(2), cl::Hidden,
    cl::desc("CHR merges a group of N branches/selects where N >= this value"));

static cl::opt<std::string> CHRModuleList(
    "chr-module-list", cl::init(""), cl::Hidden,
    cl::desc("Specify file to retrieve the list of modules to apply CHR to"));

static cl::opt<std::string> CHRFunctionList(
    "chr-function-list", cl::init(""), cl::Hidden,
    cl::desc("Specify file to retrieve the list of functions to apply CHR to"));

static StringSet<> CHRModules;
static StringSet<> CHRFunctions;

static void parseCHRFilterFiles() {
  if (!CHRModuleList.empty()) {
    auto FileOrErr = MemoryBuffer::getFile(CHRModuleList);
    if (!FileOrErr) {
      errs() << "Error: Couldn't read the chr-module-list file " << CHRModuleList << "\n";
      std::exit(1);
    }
    StringRef Buf = FileOrErr->get()->getBuffer();
    SmallVector<StringRef, 0> Lines;
    Buf.split(Lines, '\n');
    for (StringRef Line : Lines) {
      Line = Line.trim();
      if (!Line.empty())
        CHRModules.insert(Line);
    }
  }
  if (!CHRFunctionList.empty()) {
    auto FileOrErr = MemoryBuffer::getFile(CHRFunctionList);
    if (!FileOrErr) {
      errs() << "Error: Couldn't read the chr-function-list file " << CHRFunctionList << "\n";
      std::exit(1);
    }
    StringRef Buf = FileOrErr->get()->getBuffer();
    SmallVector<StringRef, 0> Lines;
    Buf.split(Lines, '\n');
    for (StringRef Line : Lines) {
      Line = Line.trim();
      if (!Line.empty())
        CHRFunctions.insert(Line);
    }
  }
}

namespace {
class ControlHeightReductionLegacyPass : public FunctionPass {
public:
  static char ID;

  ControlHeightReductionLegacyPass() : FunctionPass(ID) {
    initializeControlHeightReductionLegacyPassPass(
        *PassRegistry::getPassRegistry());
    parseCHRFilterFiles();
  }

  bool runOnFunction(Function &F) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<BlockFrequencyInfoWrapperPass>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<ProfileSummaryInfoWrapperPass>();
    AU.addRequired<RegionInfoPass>();
    AU.addPreserved<GlobalsAAWrapperPass>();
  }
};
} // end anonymous namespace

char ControlHeightReductionLegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(ControlHeightReductionLegacyPass,
                      "chr",
                      "Reduce control height in the hot paths",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(BlockFrequencyInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ProfileSummaryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(RegionInfoPass)
INITIALIZE_PASS_END(ControlHeightReductionLegacyPass,
                    "chr",
                    "Reduce control height in the hot paths",
                    false, false)

FunctionPass *llvm::createControlHeightReductionLegacyPass() {
  return new ControlHeightReductionLegacyPass();
}

namespace {

struct CHRStats {
  CHRStats() : NumBranches(0), NumBranchesDelta(0),
               WeightedNumBranchesDelta(0) {}
  void print(raw_ostream &OS) const {
    OS << "CHRStats: NumBranches " << NumBranches
       << " NumBranchesDelta " << NumBranchesDelta
       << " WeightedNumBranchesDelta " << WeightedNumBranchesDelta;
  }
  uint64_t NumBranches;       // The original number of conditional branches /
                              // selects
  uint64_t NumBranchesDelta;  // The decrease of the number of conditional
                              // branches / selects in the hot paths due to CHR.
  uint64_t WeightedNumBranchesDelta; // NumBranchesDelta weighted by the profile
                                     // count at the scope entry.
};

// RegInfo - some properties of a Region.
struct RegInfo {
  RegInfo() : R(nullptr), HasBranch(false) {}
  RegInfo(Region *RegionIn) : R(RegionIn), HasBranch(false) {}
  Region *R;
  bool HasBranch;
  SmallVector<SelectInst *, 8> Selects;
};

typedef DenseMap<Region *, DenseSet<Instruction *>> HoistStopMapTy;

// CHRScope - a sequence of regions to CHR together. It corresponds to a
// sequence of conditional blocks. It can have subscopes which correspond to
// nested conditional blocks. Nested CHRScopes form a tree.
class CHRScope {
 public:
  CHRScope(RegInfo RI) : BranchInsertPoint(nullptr) {
    assert(RI.R && "Null RegionIn");
    RegInfos.push_back(RI);
  }

  Region *getParentRegion() {
    assert(RegInfos.size() > 0 && "Empty CHRScope");
    Region *Parent = RegInfos[0].R->getParent();
    assert(Parent && "Unexpected to call this on the top-level region");
    return Parent;
  }

  BasicBlock *getEntryBlock() {
    assert(RegInfos.size() > 0 && "Empty CHRScope");
    return RegInfos.front().R->getEntry();
  }

  BasicBlock *getExitBlock() {
    assert(RegInfos.size() > 0 && "Empty CHRScope");
    return RegInfos.back().R->getExit();
  }

  bool appendable(CHRScope *Next) {
    // The next scope is appendable only if this scope is directly connected to
    // it (which implies it post-dominates this scope) and this scope dominates
    // it (no edge to the next scope outside this scope).
    BasicBlock *NextEntry = Next->getEntryBlock();
    if (getExitBlock() != NextEntry)
      // Not directly connected.
      return false;
    Region *LastRegion = RegInfos.back().R;
    for (BasicBlock *Pred : predecessors(NextEntry))
      if (!LastRegion->contains(Pred))
        // There's an edge going into the entry of the next scope from outside
        // of this scope.
        return false;
    return true;
  }

  void append(CHRScope *Next) {
    assert(RegInfos.size() > 0 && "Empty CHRScope");
    assert(Next->RegInfos.size() > 0 && "Empty CHRScope");
    assert(getParentRegion() == Next->getParentRegion() &&
           "Must be siblings");
    assert(getExitBlock() == Next->getEntryBlock() &&
           "Must be adjacent");
    for (RegInfo &RI : Next->RegInfos)
      RegInfos.push_back(RI);
    for (CHRScope *Sub : Next->Subs)
      Subs.push_back(Sub);
  }

  void addSub(CHRScope *SubIn) {
#ifndef NDEBUG
    bool IsChild = false;
    for (RegInfo &RI : RegInfos)
      if (RI.R == SubIn->getParentRegion()) {
        IsChild = true;
        break;
      }
    assert(IsChild && "Must be a child");
#endif
    Subs.push_back(SubIn);
  }

  // Split this scope at the boundary region into two, which will belong to the
  // tail and returns the tail.
  CHRScope *split(Region *Boundary) {
    assert(Boundary && "Boundary null");
    assert(RegInfos.begin()->R != Boundary &&
           "Can't be split at beginning");
    auto BoundaryIt = std::find_if(RegInfos.begin(), RegInfos.end(),
                                   [&Boundary](const RegInfo& RI) {
                                     return Boundary == RI.R;
                                   });
    if (BoundaryIt == RegInfos.end())
      return nullptr;
    SmallVector<RegInfo, 8> TailRegInfos;
    SmallVector<CHRScope *, 8> TailSubs;
    TailRegInfos.insert(TailRegInfos.begin(), BoundaryIt, RegInfos.end());
    RegInfos.resize(BoundaryIt - RegInfos.begin());
    DenseSet<Region *> TailRegionSet;
    for (RegInfo &RI : TailRegInfos)
      TailRegionSet.insert(RI.R);
    for (auto It = Subs.begin(); It != Subs.end(); ) {
      CHRScope *Sub = *It;
      assert(Sub && "null Sub");
      Region *Parent = Sub->getParentRegion();
      if (TailRegionSet.count(Parent)) {
        TailSubs.push_back(Sub);
        It = Subs.erase(It);
      } else {
        assert(std::find_if(RegInfos.begin(), RegInfos.end(),
                            [&Parent](const RegInfo& RI) {
                              return Parent == RI.R;
                            }) != RegInfos.end() &&
               "Must be in head");
        ++It;
      }
    }
    assert(HoistStopMap.empty() && "MapHoistStops must be empty");
    return new CHRScope(TailRegInfos, TailSubs);
  }

  bool contains(Instruction *I) const {
    BasicBlock *Parent = I->getParent();
    for (const RegInfo &RI : RegInfos)
      if (RI.R->contains(Parent))
        return true;
    return false;
  }

  void print(raw_ostream &OS) const;

  SmallVector<RegInfo, 8> RegInfos; // Regions that belong to this scope
  SmallVector<CHRScope *, 8> Subs;  // Subscopes.

  // The instruction at which to insert the CHR conditional branch (and hoist
  // the dependent condition values).
  Instruction *BranchInsertPoint;

  // True-biased and false-biased regions (conditional blocks),
  // respectively. Used only for the outermost scope and includes regions in
  // subscopes. The rest are unbiased.
  DenseSet<Region *> TrueBiasedRegions;
  DenseSet<Region *> FalseBiasedRegions;
  // Among the biased regions, the regions that get CHRed.
  SmallVector<RegInfo, 8> CHRRegions;

  // True-biased and false-biased selects, respectively. Used only for the
  // outermost scope and includes ones in subscopes.
  DenseSet<SelectInst *> TrueBiasedSelects;
  DenseSet<SelectInst *> FalseBiasedSelects;

  // Map from one of the above regions to the instructions to stop
  // hoisting instructions at through use-def chains.
  HoistStopMapTy HoistStopMap;

 private:
  CHRScope(SmallVector<RegInfo, 8> &RegInfosIn,
           SmallVector<CHRScope *, 8> &SubsIn)
    : RegInfos(RegInfosIn), Subs(SubsIn), BranchInsertPoint(nullptr) {}
};

class CHR {
 public:
  CHR(Function &Fin, BlockFrequencyInfo &BFIin, DominatorTree &DTin,
      ProfileSummaryInfo &PSIin, RegionInfo &RIin,
      OptimizationRemarkEmitter &OREin)
      : F(Fin), BFI(BFIin), DT(DTin), PSI(PSIin), RI(RIin), ORE(OREin) {}

  ~CHR() {
    for (CHRScope *Scope : Scopes) {
      delete Scope;
    }
  }

  bool run();

 private:
  // See the comments in CHR::run() for the high level flow of the algorithm and
  // what the following functions do.

  void findScopes(SmallVectorImpl<CHRScope *> &Output) {
    Region *R = RI.getTopLevelRegion();
    CHRScope *Scope = findScopes(R, nullptr, nullptr, Output);
    if (Scope) {
      Output.push_back(Scope);
    }
  }
  CHRScope *findScopes(Region *R, Region *NextRegion, Region *ParentRegion,
                        SmallVectorImpl<CHRScope *> &Scopes);
  CHRScope *findScope(Region *R);
  void checkScopeHoistable(CHRScope *Scope);

  void splitScopes(SmallVectorImpl<CHRScope *> &Input,
                   SmallVectorImpl<CHRScope *> &Output);
  SmallVector<CHRScope *, 8> splitScope(CHRScope *Scope,
                                        CHRScope *Outer,
                                        DenseSet<Value *> *OuterConditionValues,
                                        Instruction *OuterInsertPoint,
                                        SmallVectorImpl<CHRScope *> &Output,
                                        DenseSet<Instruction *> &Unhoistables);

  void classifyBiasedScopes(SmallVectorImpl<CHRScope *> &Scopes);
  void classifyBiasedScopes(CHRScope *Scope, CHRScope *OutermostScope);

  void filterScopes(SmallVectorImpl<CHRScope *> &Input,
                    SmallVectorImpl<CHRScope *> &Output);

  void setCHRRegions(SmallVectorImpl<CHRScope *> &Input,
                     SmallVectorImpl<CHRScope *> &Output);
  void setCHRRegions(CHRScope *Scope, CHRScope *OutermostScope);

  void sortScopes(SmallVectorImpl<CHRScope *> &Input,
                  SmallVectorImpl<CHRScope *> &Output);

  void transformScopes(SmallVectorImpl<CHRScope *> &CHRScopes);
  void transformScopes(CHRScope *Scope, DenseSet<PHINode *> &TrivialPHIs);
  void cloneScopeBlocks(CHRScope *Scope,
                        BasicBlock *PreEntryBlock,
                        BasicBlock *ExitBlock,
                        Region *LastRegion,
                        ValueToValueMapTy &VMap);
  BranchInst *createMergedBranch(BasicBlock *PreEntryBlock,
                                 BasicBlock *EntryBlock,
                                 BasicBlock *NewEntryBlock,
                                 ValueToValueMapTy &VMap);
  void fixupBranchesAndSelects(CHRScope *Scope,
                               BasicBlock *PreEntryBlock,
                               BranchInst *MergedBR,
                               uint64_t ProfileCount);
  void fixupBranch(Region *R,
                   CHRScope *Scope,
                   IRBuilder<> &IRB,
                   Value *&MergedCondition, BranchProbability &CHRBranchBias);
  void fixupSelect(SelectInst* SI,
                   CHRScope *Scope,
                   IRBuilder<> &IRB,
                   Value *&MergedCondition, BranchProbability &CHRBranchBias);
  void addToMergedCondition(bool IsTrueBiased, Value *Cond,
                            Instruction *BranchOrSelect,
                            CHRScope *Scope,
                            IRBuilder<> &IRB,
                            Value *&MergedCondition);

  Function &F;
  BlockFrequencyInfo &BFI;
  DominatorTree &DT;
  ProfileSummaryInfo &PSI;
  RegionInfo &RI;
  OptimizationRemarkEmitter &ORE;
  CHRStats Stats;

  // All the true-biased regions in the function
  DenseSet<Region *> TrueBiasedRegionsGlobal;
  // All the false-biased regions in the function
  DenseSet<Region *> FalseBiasedRegionsGlobal;
  // All the true-biased selects in the function
  DenseSet<SelectInst *> TrueBiasedSelectsGlobal;
  // All the false-biased selects in the function
  DenseSet<SelectInst *> FalseBiasedSelectsGlobal;
  // A map from biased regions to their branch bias
  DenseMap<Region *, BranchProbability> BranchBiasMap;
  // A map from biased selects to their branch bias
  DenseMap<SelectInst *, BranchProbability> SelectBiasMap;
  // All the scopes.
  DenseSet<CHRScope *> Scopes;
};

} // end anonymous namespace

static inline
raw_ostream LLVM_ATTRIBUTE_UNUSED &operator<<(raw_ostream &OS,
                                              const CHRStats &Stats) {
  Stats.print(OS);
  return OS;
}

static inline
raw_ostream &operator<<(raw_ostream &OS, const CHRScope &Scope) {
  Scope.print(OS);
  return OS;
}

static bool shouldApply(Function &F, ProfileSummaryInfo& PSI) {
  if (ForceCHR)
    return true;

  if (!CHRModuleList.empty() || !CHRFunctionList.empty()) {
    if (CHRModules.count(F.getParent()->getName()))
      return true;
    return CHRFunctions.count(F.getName());
  }

  assert(PSI.hasProfileSummary() && "Empty PSI?");
  return PSI.isFunctionEntryHot(&F);
}

static void LLVM_ATTRIBUTE_UNUSED dumpIR(Function &F, const char *Label,
                                         CHRStats *Stats) {
  StringRef FuncName = F.getName();
  StringRef ModuleName = F.getParent()->getName();
  (void)(FuncName); // Unused in release build.
  (void)(ModuleName); // Unused in release build.
  CHR_DEBUG(dbgs() << "CHR IR dump " << Label << " " << ModuleName << " "
            << FuncName);
  if (Stats)
    CHR_DEBUG(dbgs() << " " << *Stats);
  CHR_DEBUG(dbgs() << "\n");
  CHR_DEBUG(F.dump());
}

void CHRScope::print(raw_ostream &OS) const {
  assert(RegInfos.size() > 0 && "Empty CHRScope");
  OS << "CHRScope[";
  OS << RegInfos.size() << ", Regions[";
  for (const RegInfo &RI : RegInfos) {
    OS << RI.R->getNameStr();
    if (RI.HasBranch)
      OS << " B";
    if (RI.Selects.size() > 0)
      OS << " S" << RI.Selects.size();
    OS << ", ";
  }
  if (RegInfos[0].R->getParent()) {
    OS << "], Parent " << RegInfos[0].R->getParent()->getNameStr();
  } else {
    // top level region
    OS << "]";
  }
  OS << ", Subs[";
  for (CHRScope *Sub : Subs) {
    OS << *Sub << ", ";
  }
  OS << "]]";
}

// Return true if the given instruction type can be hoisted by CHR.
static bool isHoistableInstructionType(Instruction *I) {
  return isa<BinaryOperator>(I) || isa<CastInst>(I) || isa<SelectInst>(I) ||
      isa<GetElementPtrInst>(I) || isa<CmpInst>(I) ||
      isa<InsertElementInst>(I) || isa<ExtractElementInst>(I) ||
      isa<ShuffleVectorInst>(I) || isa<ExtractValueInst>(I) ||
      isa<InsertValueInst>(I);
}

// Return true if the given instruction can be hoisted by CHR.
static bool isHoistable(Instruction *I, DominatorTree &DT) {
  if (!isHoistableInstructionType(I))
    return false;
  return isSafeToSpeculativelyExecute(I, nullptr, &DT);
}

// Recursively traverse the use-def chains of the given value and return a set
// of the unhoistable base values defined within the scope (excluding the
// first-region entry block) or the (hoistable or unhoistable) base values that
// are defined outside (including the first-region entry block) of the
// scope. The returned set doesn't include constants.
static std::set<Value *> getBaseValues(Value *V,
                                       DominatorTree &DT) {
  std::set<Value *> Result;
  if (auto *I = dyn_cast<Instruction>(V)) {
    // We don't stop at a block that's not in the Scope because we would miss some
    // instructions that are based on the same base values if we stop there.
    if (!isHoistable(I, DT)) {
      Result.insert(I);
      return Result;
    }
    // I is hoistable above the Scope.
    for (Value *Op : I->operands()) {
      std::set<Value *> OpResult = getBaseValues(Op, DT);
      Result.insert(OpResult.begin(), OpResult.end());
    }
    return Result;
  }
  if (isa<Argument>(V)) {
    Result.insert(V);
    return Result;
  }
  // We don't include others like constants because those won't lead to any
  // chance of folding of conditions (eg two bit checks merged into one check)
  // after CHR.
  return Result;  // empty
}

// Return true if V is already hoisted or can be hoisted (along with its
// operands) above the insert point. When it returns true and HoistStops is
// non-null, the instructions to stop hoisting at through the use-def chains are
// inserted into HoistStops.
static bool
checkHoistValue(Value *V, Instruction *InsertPoint, DominatorTree &DT,
                DenseSet<Instruction *> &Unhoistables,
                DenseSet<Instruction *> *HoistStops) {
  assert(InsertPoint && "Null InsertPoint");
  if (auto *I = dyn_cast<Instruction>(V)) {
    assert(DT.getNode(I->getParent()) && "DT must contain I's parent block");
    assert(DT.getNode(InsertPoint->getParent()) && "DT must contain Destination");
    if (Unhoistables.count(I)) {
      // Don't hoist if they are not to be hoisted.
      return false;
    }
    if (DT.dominates(I, InsertPoint)) {
      // We are already above the insert point. Stop here.
      if (HoistStops)
        HoistStops->insert(I);
      return true;
    }
    // We aren't not above the insert point, check if we can hoist it above the
    // insert point.
    if (isHoistable(I, DT)) {
      // Check operands first.
      DenseSet<Instruction *> OpsHoistStops;
      bool AllOpsHoisted = true;
      for (Value *Op : I->operands()) {
        if (!checkHoistValue(Op, InsertPoint, DT, Unhoistables, &OpsHoistStops)) {
          AllOpsHoisted = false;
          break;
        }
      }
      if (AllOpsHoisted) {
        CHR_DEBUG(dbgs() << "checkHoistValue " << *I << "\n");
        if (HoistStops)
          HoistStops->insert(OpsHoistStops.begin(), OpsHoistStops.end());
        return true;
      }
    }
    return false;
  }
  // Non-instructions are considered hoistable.
  return true;
}

// Returns true and sets the true probability and false probability of an
// MD_prof metadata if it's well-formed.
static bool checkMDProf(MDNode *MD, BranchProbability &TrueProb,
                        BranchProbability &FalseProb) {
  if (!MD) return false;
  MDString *MDName = cast<MDString>(MD->getOperand(0));
  if (MDName->getString() != "branch_weights" ||
      MD->getNumOperands() != 3)
    return false;
  ConstantInt *TrueWeight = mdconst::extract<ConstantInt>(MD->getOperand(1));
  ConstantInt *FalseWeight = mdconst::extract<ConstantInt>(MD->getOperand(2));
  if (!TrueWeight || !FalseWeight)
    return false;
  uint64_t TrueWt = TrueWeight->getValue().getZExtValue();
  uint64_t FalseWt = FalseWeight->getValue().getZExtValue();
  uint64_t SumWt = TrueWt + FalseWt;

  assert(SumWt >= TrueWt && SumWt >= FalseWt &&
         "Overflow calculating branch probabilities.");

  TrueProb = BranchProbability::getBranchProbability(TrueWt, SumWt);
  FalseProb = BranchProbability::getBranchProbability(FalseWt, SumWt);
  return true;
}

static BranchProbability getCHRBiasThreshold() {
  return BranchProbability::getBranchProbability(
      static_cast<uint64_t>(CHRBiasThreshold * 1000000), 1000000);
}

// A helper for CheckBiasedBranch and CheckBiasedSelect. If TrueProb >=
// CHRBiasThreshold, put Key into TrueSet and return true. If FalseProb >=
// CHRBiasThreshold, put Key into FalseSet and return true. Otherwise, return
// false.
template <typename K, typename S, typename M>
static bool checkBias(K *Key, BranchProbability TrueProb,
                      BranchProbability FalseProb, S &TrueSet, S &FalseSet,
                      M &BiasMap) {
  BranchProbability Threshold = getCHRBiasThreshold();
  if (TrueProb >= Threshold) {
    TrueSet.insert(Key);
    BiasMap[Key] = TrueProb;
    return true;
  } else if (FalseProb >= Threshold) {
    FalseSet.insert(Key);
    BiasMap[Key] = FalseProb;
    return true;
  }
  return false;
}

// Returns true and insert a region into the right biased set and the map if the
// branch of the region is biased.
static bool checkBiasedBranch(BranchInst *BI, Region *R,
                              DenseSet<Region *> &TrueBiasedRegionsGlobal,
                              DenseSet<Region *> &FalseBiasedRegionsGlobal,
                              DenseMap<Region *, BranchProbability> &BranchBiasMap) {
  if (!BI->isConditional())
    return false;
  BranchProbability ThenProb, ElseProb;
  if (!checkMDProf(BI->getMetadata(LLVMContext::MD_prof),
                   ThenProb, ElseProb))
    return false;
  BasicBlock *IfThen = BI->getSuccessor(0);
  BasicBlock *IfElse = BI->getSuccessor(1);
  assert((IfThen == R->getExit() || IfElse == R->getExit()) &&
         IfThen != IfElse &&
         "Invariant from findScopes");
  if (IfThen == R->getExit()) {
    // Swap them so that IfThen/ThenProb means going into the conditional code
    // and IfElse/ElseProb means skipping it.
    std::swap(IfThen, IfElse);
    std::swap(ThenProb, ElseProb);
  }
  CHR_DEBUG(dbgs() << "BI " << *BI << " ");
  CHR_DEBUG(dbgs() << "ThenProb " << ThenProb << " ");
  CHR_DEBUG(dbgs() << "ElseProb " << ElseProb << "\n");
  return checkBias(R, ThenProb, ElseProb,
                   TrueBiasedRegionsGlobal, FalseBiasedRegionsGlobal,
                   BranchBiasMap);
}

// Returns true and insert a select into the right biased set and the map if the
// select is biased.
static bool checkBiasedSelect(
    SelectInst *SI, Region *R,
    DenseSet<SelectInst *> &TrueBiasedSelectsGlobal,
    DenseSet<SelectInst *> &FalseBiasedSelectsGlobal,
    DenseMap<SelectInst *, BranchProbability> &SelectBiasMap) {
  BranchProbability TrueProb, FalseProb;
  if (!checkMDProf(SI->getMetadata(LLVMContext::MD_prof),
                   TrueProb, FalseProb))
    return false;
  CHR_DEBUG(dbgs() << "SI " << *SI << " ");
  CHR_DEBUG(dbgs() << "TrueProb " << TrueProb << " ");
  CHR_DEBUG(dbgs() << "FalseProb " << FalseProb << "\n");
  return checkBias(SI, TrueProb, FalseProb,
                   TrueBiasedSelectsGlobal, FalseBiasedSelectsGlobal,
                   SelectBiasMap);
}

// Returns the instruction at which to hoist the dependent condition values and
// insert the CHR branch for a region. This is the terminator branch in the
// entry block or the first select in the entry block, if any.
static Instruction* getBranchInsertPoint(RegInfo &RI) {
  Region *R = RI.R;
  BasicBlock *EntryBB = R->getEntry();
  // The hoist point is by default the terminator of the entry block, which is
  // the same as the branch instruction if RI.HasBranch is true.
  Instruction *HoistPoint = EntryBB->getTerminator();
  for (SelectInst *SI : RI.Selects) {
    if (SI->getParent() == EntryBB) {
      // Pick the first select in Selects in the entry block.  Note Selects is
      // sorted in the instruction order within a block (asserted below).
      HoistPoint = SI;
      break;
    }
  }
  assert(HoistPoint && "Null HoistPoint");
#ifndef NDEBUG
  // Check that HoistPoint is the first one in Selects in the entry block,
  // if any.
  DenseSet<Instruction *> EntryBlockSelectSet;
  for (SelectInst *SI : RI.Selects) {
    if (SI->getParent() == EntryBB) {
      EntryBlockSelectSet.insert(SI);
    }
  }
  for (Instruction &I : *EntryBB) {
    if (EntryBlockSelectSet.count(&I) > 0) {
      assert(&I == HoistPoint &&
             "HoistPoint must be the first one in Selects");
      break;
    }
  }
#endif
  return HoistPoint;
}

// Find a CHR scope in the given region.
CHRScope * CHR::findScope(Region *R) {
  CHRScope *Result = nullptr;
  BasicBlock *Entry = R->getEntry();
  BasicBlock *Exit = R->getExit();  // null if top level.
  assert(Entry && "Entry must not be null");
  assert((Exit == nullptr) == (R->isTopLevelRegion()) &&
         "Only top level region has a null exit");
  if (Entry)
    CHR_DEBUG(dbgs() << "Entry " << Entry->getName() << "\n");
  else
    CHR_DEBUG(dbgs() << "Entry null\n");
  if (Exit)
    CHR_DEBUG(dbgs() << "Exit " << Exit->getName() << "\n");
  else
    CHR_DEBUG(dbgs() << "Exit null\n");
  // Exclude cases where Entry is part of a subregion (hence it doesn't belong
  // to this region).
  bool EntryInSubregion = RI.getRegionFor(Entry) != R;
  if (EntryInSubregion)
    return nullptr;
  // Exclude loops
  for (BasicBlock *Pred : predecessors(Entry))
    if (R->contains(Pred))
      return nullptr;
  if (Exit) {
    // Try to find an if-then block (check if R is an if-then).
    // if (cond) {
    //  ...
    // }
    auto *BI = dyn_cast<BranchInst>(Entry->getTerminator());
    if (BI)
      CHR_DEBUG(dbgs() << "BI.isConditional " << BI->isConditional() << "\n");
    else
      CHR_DEBUG(dbgs() << "BI null\n");
    if (BI && BI->isConditional()) {
      BasicBlock *S0 = BI->getSuccessor(0);
      BasicBlock *S1 = BI->getSuccessor(1);
      CHR_DEBUG(dbgs() << "S0 " << S0->getName() << "\n");
      CHR_DEBUG(dbgs() << "S1 " << S1->getName() << "\n");
      if (S0 != S1 && (S0 == Exit || S1 == Exit)) {
        RegInfo RI(R);
        RI.HasBranch = checkBiasedBranch(
            BI, R, TrueBiasedRegionsGlobal, FalseBiasedRegionsGlobal,
            BranchBiasMap);
        Result = new CHRScope(RI);
        Scopes.insert(Result);
        CHR_DEBUG(dbgs() << "Found a region with a branch\n");
        ++Stats.NumBranches;
        if (!RI.HasBranch) {
          ORE.emit([&]() {
            return OptimizationRemarkMissed(DEBUG_TYPE, "BranchNotBiased", BI)
                << "Branch not biased";
          });
        }
      }
    }
  }
  {
    // Try to look for selects in the direct child blocks (as opposed to in
    // subregions) of R.
    // ...
    // if (..) { // Some subregion
    //   ...
    // }
    // if (..) { // Some subregion
    //   ...
    // }
    // ...
    // a = cond ? b : c;
    // ...
    SmallVector<SelectInst *, 8> Selects;
    for (RegionNode *E : R->elements()) {
      if (E->isSubRegion())
        continue;
      // This returns the basic block of E if E is a direct child of R (not a
      // subregion.)
      BasicBlock *BB = E->getEntry();
      // Need to push in the order to make it easier to find the first Select
      // later.
      for (Instruction &I : *BB) {
        if (auto *SI = dyn_cast<SelectInst>(&I)) {
          Selects.push_back(SI);
          ++Stats.NumBranches;
        }
      }
    }
    if (Selects.size() > 0) {
      auto AddSelects = [&](RegInfo &RI) {
        for (auto *SI : Selects)
          if (checkBiasedSelect(SI, RI.R,
                                TrueBiasedSelectsGlobal,
                                FalseBiasedSelectsGlobal,
                                SelectBiasMap))
            RI.Selects.push_back(SI);
          else
            ORE.emit([&]() {
              return OptimizationRemarkMissed(DEBUG_TYPE, "SelectNotBiased", SI)
                  << "Select not biased";
            });
      };
      if (!Result) {
        CHR_DEBUG(dbgs() << "Found a select-only region\n");
        RegInfo RI(R);
        AddSelects(RI);
        Result = new CHRScope(RI);
        Scopes.insert(Result);
      } else {
        CHR_DEBUG(dbgs() << "Found select(s) in a region with a branch\n");
        AddSelects(Result->RegInfos[0]);
      }
    }
  }

  if (Result) {
    checkScopeHoistable(Result);
  }
  return Result;
}

// Check that any of the branch and the selects in the region could be
// hoisted above the the CHR branch insert point (the most dominating of
// them, either the branch (at the end of the first block) or the first
// select in the first block). If the branch can't be hoisted, drop the
// selects in the first blocks.
//
// For example, for the following scope/region with selects, we want to insert
// the merged branch right before the first select in the first/entry block by
// hoisting c1, c2, c3, and c4.
//
// // Branch insert point here.
// a = c1 ? b : c; // Select 1
// d = c2 ? e : f; // Select 2
// if (c3) { // Branch
//   ...
//   c4 = foo() // A call.
//   g = c4 ? h : i; // Select 3
// }
//
// But suppose we can't hoist c4 because it's dependent on the preceding
// call. Then, we drop Select 3. Furthermore, if we can't hoist c2, we also drop
// Select 2. If we can't hoist c3, we drop Selects 1 & 2.
void CHR::checkScopeHoistable(CHRScope *Scope) {
  RegInfo &RI = Scope->RegInfos[0];
  Region *R = RI.R;
  BasicBlock *EntryBB = R->getEntry();
  auto *Branch = RI.HasBranch ?
                 cast<BranchInst>(EntryBB->getTerminator()) : nullptr;
  SmallVector<SelectInst *, 8> &Selects = RI.Selects;
  if (RI.HasBranch || !Selects.empty()) {
    Instruction *InsertPoint = getBranchInsertPoint(RI);
    CHR_DEBUG(dbgs() << "InsertPoint " << *InsertPoint << "\n");
    // Avoid a data dependence from a select or a branch to a(nother)
    // select. Note no instruction can't data-depend on a branch (a branch
    // instruction doesn't produce a value).
    DenseSet<Instruction *> Unhoistables;
    // Initialize Unhoistables with the selects.
    for (SelectInst *SI : Selects) {
      Unhoistables.insert(SI);
    }
    // Remove Selects that can't be hoisted.
    for (auto it = Selects.begin(); it != Selects.end(); ) {
      SelectInst *SI = *it;
      if (SI == InsertPoint) {
        ++it;
        continue;
      }
      bool IsHoistable = checkHoistValue(SI->getCondition(), InsertPoint,
                                         DT, Unhoistables, nullptr);
      if (!IsHoistable) {
        CHR_DEBUG(dbgs() << "Dropping select " << *SI << "\n");
        ORE.emit([&]() {
          return OptimizationRemarkMissed(DEBUG_TYPE,
                                          "DropUnhoistableSelect", SI)
              << "Dropped unhoistable select";
        });
        it = Selects.erase(it);
        // Since we are dropping the select here, we also drop it from
        // Unhoistables.
        Unhoistables.erase(SI);
      } else
        ++it;
    }
    // Update InsertPoint after potentially removing selects.
    InsertPoint = getBranchInsertPoint(RI);
    CHR_DEBUG(dbgs() << "InsertPoint " << *InsertPoint << "\n");
    if (RI.HasBranch && InsertPoint != Branch) {
      bool IsHoistable = checkHoistValue(Branch->getCondition(), InsertPoint,
                                         DT, Unhoistables, nullptr);
      if (!IsHoistable) {
        // If the branch isn't hoistable, drop the selects in the entry
        // block, preferring the branch, which makes the branch the hoist
        // point.
        assert(InsertPoint != Branch && "Branch must not be the hoist point");
        CHR_DEBUG(dbgs() << "Dropping selects in entry block \n");
        CHR_DEBUG(
            for (SelectInst *SI : Selects) {
              dbgs() << "SI " << *SI << "\n";
            });
        for (SelectInst *SI : Selects) {
          ORE.emit([&]() {
            return OptimizationRemarkMissed(DEBUG_TYPE,
                                            "DropSelectUnhoistableBranch", SI)
                << "Dropped select due to unhoistable branch";
          });
        }
        Selects.erase(std::remove_if(Selects.begin(), Selects.end(),
                                     [EntryBB](SelectInst *SI) {
                                       return SI->getParent() == EntryBB;
                                     }), Selects.end());
        Unhoistables.clear();
        InsertPoint = Branch;
      }
    }
    CHR_DEBUG(dbgs() << "InsertPoint " << *InsertPoint << "\n");
#ifndef NDEBUG
    if (RI.HasBranch) {
      assert(!DT.dominates(Branch, InsertPoint) &&
             "Branch can't be already above the hoist point");
      assert(checkHoistValue(Branch->getCondition(), InsertPoint,
                             DT, Unhoistables, nullptr) &&
             "checkHoistValue for branch");
    }
    for (auto *SI : Selects) {
      assert(!DT.dominates(SI, InsertPoint) &&
             "SI can't be already above the hoist point");
      assert(checkHoistValue(SI->getCondition(), InsertPoint, DT,
                             Unhoistables, nullptr) &&
             "checkHoistValue for selects");
    }
    CHR_DEBUG(dbgs() << "Result\n");
    if (RI.HasBranch) {
      CHR_DEBUG(dbgs() << "BI " << *Branch << "\n");
    }
    for (auto *SI : Selects) {
      CHR_DEBUG(dbgs() << "SI " << *SI << "\n");
    }
#endif
  }
}

// Traverse the region tree, find all nested scopes and merge them if possible.
CHRScope * CHR::findScopes(Region *R, Region *NextRegion, Region *ParentRegion,
                           SmallVectorImpl<CHRScope *> &Scopes) {
  CHR_DEBUG(dbgs() << "findScopes " << R->getNameStr() << "\n");
  CHRScope *Result = findScope(R);
  // Visit subscopes.
  CHRScope *ConsecutiveSubscope = nullptr;
  SmallVector<CHRScope *, 8> Subscopes;
  for (auto It = R->begin(); It != R->end(); ++It) {
    const std::unique_ptr<Region> &SubR = *It;
    auto NextIt = std::next(It);
    Region *NextSubR = NextIt != R->end() ? NextIt->get() : nullptr;
    CHR_DEBUG(dbgs() << "Looking at subregion " << SubR.get()->getNameStr()
              << "\n");
    CHRScope *SubCHRScope = findScopes(SubR.get(), NextSubR, R, Scopes);
    if (SubCHRScope) {
      CHR_DEBUG(dbgs() << "Subregion Scope " << *SubCHRScope << "\n");
    } else {
      CHR_DEBUG(dbgs() << "Subregion Scope null\n");
    }
    if (SubCHRScope) {
      if (!ConsecutiveSubscope)
        ConsecutiveSubscope = SubCHRScope;
      else if (!ConsecutiveSubscope->appendable(SubCHRScope)) {
        Subscopes.push_back(ConsecutiveSubscope);
        ConsecutiveSubscope = SubCHRScope;
      } else
        ConsecutiveSubscope->append(SubCHRScope);
    } else {
      if (ConsecutiveSubscope) {
        Subscopes.push_back(ConsecutiveSubscope);
      }
      ConsecutiveSubscope = nullptr;
    }
  }
  if (ConsecutiveSubscope) {
    Subscopes.push_back(ConsecutiveSubscope);
  }
  for (CHRScope *Sub : Subscopes) {
    if (Result) {
      // Combine it with the parent.
      Result->addSub(Sub);
    } else {
      // Push Subscopes as they won't be combined with the parent.
      Scopes.push_back(Sub);
    }
  }
  return Result;
}

static DenseSet<Value *> getCHRConditionValuesForRegion(RegInfo &RI) {
  DenseSet<Value *> ConditionValues;
  if (RI.HasBranch) {
    auto *BI = cast<BranchInst>(RI.R->getEntry()->getTerminator());
    ConditionValues.insert(BI->getCondition());
  }
  for (SelectInst *SI : RI.Selects) {
    ConditionValues.insert(SI->getCondition());
  }
  return ConditionValues;
}


// Determine whether to split a scope depending on the sets of the branch
// condition values of the previous region and the current region. We split
// (return true) it if 1) the condition values of the inner/lower scope can't be
// hoisted up to the outer/upper scope, or 2) the two sets of the condition
// values have an empty intersection (because the combined branch conditions
// won't probably lead to a simpler combined condition).
static bool shouldSplit(Instruction *InsertPoint,
                        DenseSet<Value *> &PrevConditionValues,
                        DenseSet<Value *> &ConditionValues,
                        DominatorTree &DT,
                        DenseSet<Instruction *> &Unhoistables) {
  CHR_DEBUG(
      dbgs() << "shouldSplit " << *InsertPoint << " PrevConditionValues ";
      for (Value *V : PrevConditionValues) {
        dbgs() << *V << ", ";
      }
      dbgs() << " ConditionValues ";
      for (Value *V : ConditionValues) {
        dbgs() << *V << ", ";
      }
      dbgs() << "\n");
  assert(InsertPoint && "Null InsertPoint");
  // If any of Bases isn't hoistable to the hoist point, split.
  for (Value *V : ConditionValues) {
    if (!checkHoistValue(V, InsertPoint, DT, Unhoistables, nullptr)) {
      CHR_DEBUG(dbgs() << "Split. checkHoistValue false " << *V << "\n");
      return true; // Not hoistable, split.
    }
  }
  // If PrevConditionValues or ConditionValues is empty, don't split to avoid
  // unnecessary splits at scopes with no branch/selects.  If
  // PrevConditionValues and ConditionValues don't intersect at all, split.
  if (!PrevConditionValues.empty() && !ConditionValues.empty()) {
    // Use std::set as DenseSet doesn't work with set_intersection.
    std::set<Value *> PrevBases, Bases;
    for (Value *V : PrevConditionValues) {
      std::set<Value *> BaseValues = getBaseValues(V, DT);
      PrevBases.insert(BaseValues.begin(), BaseValues.end());
    }
    for (Value *V : ConditionValues) {
      std::set<Value *> BaseValues = getBaseValues(V, DT);
      Bases.insert(BaseValues.begin(), BaseValues.end());
    }
    CHR_DEBUG(
        dbgs() << "PrevBases ";
        for (Value *V : PrevBases) {
          dbgs() << *V << ", ";
        }
        dbgs() << " Bases ";
        for (Value *V : Bases) {
          dbgs() << *V << ", ";
        }
        dbgs() << "\n");
    std::set<Value *> Intersection;
    std::set_intersection(PrevBases.begin(), PrevBases.end(),
                          Bases.begin(), Bases.end(),
                          std::inserter(Intersection, Intersection.begin()));
    if (Intersection.empty()) {
      // Empty intersection, split.
      CHR_DEBUG(dbgs() << "Split. Intersection empty\n");
      return true;
    }
  }
  CHR_DEBUG(dbgs() << "No split\n");
  return false;  // Don't split.
}

static void getSelectsInScope(CHRScope *Scope,
                              DenseSet<Instruction *> &Output) {
  for (RegInfo &RI : Scope->RegInfos)
    for (SelectInst *SI : RI.Selects)
      Output.insert(SI);
  for (CHRScope *Sub : Scope->Subs)
    getSelectsInScope(Sub, Output);
}

void CHR::splitScopes(SmallVectorImpl<CHRScope *> &Input,
                      SmallVectorImpl<CHRScope *> &Output) {
  for (CHRScope *Scope : Input) {
    assert(!Scope->BranchInsertPoint &&
           "BranchInsertPoint must not be set");
    DenseSet<Instruction *> Unhoistables;
    getSelectsInScope(Scope, Unhoistables);
    splitScope(Scope, nullptr, nullptr, nullptr, Output, Unhoistables);
  }
#ifndef NDEBUG
  for (CHRScope *Scope : Output) {
    assert(Scope->BranchInsertPoint && "BranchInsertPoint must be set");
  }
#endif
}

SmallVector<CHRScope *, 8> CHR::splitScope(
    CHRScope *Scope,
    CHRScope *Outer,
    DenseSet<Value *> *OuterConditionValues,
    Instruction *OuterInsertPoint,
    SmallVectorImpl<CHRScope *> &Output,
    DenseSet<Instruction *> &Unhoistables) {
  if (Outer) {
    assert(OuterConditionValues && "Null OuterConditionValues");
    assert(OuterInsertPoint && "Null OuterInsertPoint");
  }
  bool PrevSplitFromOuter = true;
  DenseSet<Value *> PrevConditionValues;
  Instruction *PrevInsertPoint = nullptr;
  SmallVector<CHRScope *, 8> Splits;
  SmallVector<bool, 8> SplitsSplitFromOuter;
  SmallVector<DenseSet<Value *>, 8> SplitsConditionValues;
  SmallVector<Instruction *, 8> SplitsInsertPoints;
  SmallVector<RegInfo, 8> RegInfos(Scope->RegInfos);  // Copy
  for (RegInfo &RI : RegInfos) {
    Instruction *InsertPoint = getBranchInsertPoint(RI);
    DenseSet<Value *> ConditionValues = getCHRConditionValuesForRegion(RI);
    CHR_DEBUG(
        dbgs() << "ConditionValues ";
        for (Value *V : ConditionValues) {
          dbgs() << *V << ", ";
        }
        dbgs() << "\n");
    if (RI.R == RegInfos[0].R) {
      // First iteration. Check to see if we should split from the outer.
      if (Outer) {
        CHR_DEBUG(dbgs() << "Outer " << *Outer << "\n");
        CHR_DEBUG(dbgs() << "Should split from outer at "
                  << RI.R->getNameStr() << "\n");
        if (shouldSplit(OuterInsertPoint, *OuterConditionValues,
                        ConditionValues, DT, Unhoistables)) {
          PrevConditionValues = ConditionValues;
          PrevInsertPoint = InsertPoint;
          ORE.emit([&]() {
            return OptimizationRemarkMissed(DEBUG_TYPE,
                                            "SplitScopeFromOuter",
                                            RI.R->getEntry()->getTerminator())
                << "Split scope from outer due to unhoistable branch/select "
                << "and/or lack of common condition values";
          });
        } else {
          // Not splitting from the outer. Use the outer bases and insert
          // point. Union the bases.
          PrevSplitFromOuter = false;
          PrevConditionValues = *OuterConditionValues;
          PrevConditionValues.insert(ConditionValues.begin(),
                                     ConditionValues.end());
          PrevInsertPoint = OuterInsertPoint;
        }
      } else {
        CHR_DEBUG(dbgs() << "Outer null\n");
        PrevConditionValues = ConditionValues;
        PrevInsertPoint = InsertPoint;
      }
    } else {
      CHR_DEBUG(dbgs() << "Should split from prev at "
                << RI.R->getNameStr() << "\n");
      if (shouldSplit(PrevInsertPoint, PrevConditionValues, ConditionValues,
                      DT, Unhoistables)) {
        CHRScope *Tail = Scope->split(RI.R);
        Scopes.insert(Tail);
        Splits.push_back(Scope);
        SplitsSplitFromOuter.push_back(PrevSplitFromOuter);
        SplitsConditionValues.push_back(PrevConditionValues);
        SplitsInsertPoints.push_back(PrevInsertPoint);
        Scope = Tail;
        PrevConditionValues = ConditionValues;
        PrevInsertPoint = InsertPoint;
        PrevSplitFromOuter = true;
        ORE.emit([&]() {
          return OptimizationRemarkMissed(DEBUG_TYPE,
                                          "SplitScopeFromPrev",
                                          RI.R->getEntry()->getTerminator())
              << "Split scope from previous due to unhoistable branch/select "
              << "and/or lack of common condition values";
        });
      } else {
        // Not splitting. Union the bases. Keep the hoist point.
        PrevConditionValues.insert(ConditionValues.begin(), ConditionValues.end());
      }
    }
  }
  Splits.push_back(Scope);
  SplitsSplitFromOuter.push_back(PrevSplitFromOuter);
  SplitsConditionValues.push_back(PrevConditionValues);
  assert(PrevInsertPoint && "Null PrevInsertPoint");
  SplitsInsertPoints.push_back(PrevInsertPoint);
  assert(Splits.size() == SplitsConditionValues.size() &&
         Splits.size() == SplitsSplitFromOuter.size() &&
         Splits.size() == SplitsInsertPoints.size() && "Mismatching sizes");
  for (size_t I = 0; I < Splits.size(); ++I) {
    CHRScope *Split = Splits[I];
    DenseSet<Value *> &SplitConditionValues = SplitsConditionValues[I];
    Instruction *SplitInsertPoint = SplitsInsertPoints[I];
    SmallVector<CHRScope *, 8> NewSubs;
    DenseSet<Instruction *> SplitUnhoistables;
    getSelectsInScope(Split, SplitUnhoistables);
    for (CHRScope *Sub : Split->Subs) {
      SmallVector<CHRScope *, 8> SubSplits = splitScope(
          Sub, Split, &SplitConditionValues, SplitInsertPoint, Output,
          SplitUnhoistables);
      NewSubs.insert(NewSubs.end(), SubSplits.begin(), SubSplits.end());
    }
    Split->Subs = NewSubs;
  }
  SmallVector<CHRScope *, 8> Result;
  for (size_t I = 0; I < Splits.size(); ++I) {
    CHRScope *Split = Splits[I];
    if (SplitsSplitFromOuter[I]) {
      // Split from the outer.
      Output.push_back(Split);
      Split->BranchInsertPoint = SplitsInsertPoints[I];
      CHR_DEBUG(dbgs() << "BranchInsertPoint " << *SplitsInsertPoints[I]
                << "\n");
    } else {
      // Connected to the outer.
      Result.push_back(Split);
    }
  }
  if (!Outer)
    assert(Result.empty() &&
           "If no outer (top-level), must return no nested ones");
  return Result;
}

void CHR::classifyBiasedScopes(SmallVectorImpl<CHRScope *> &Scopes) {
  for (CHRScope *Scope : Scopes) {
    assert(Scope->TrueBiasedRegions.empty() && Scope->FalseBiasedRegions.empty() && "Empty");
    classifyBiasedScopes(Scope, Scope);
    CHR_DEBUG(
        dbgs() << "classifyBiasedScopes " << *Scope << "\n";
        dbgs() << "TrueBiasedRegions ";
        for (Region *R : Scope->TrueBiasedRegions) {
          dbgs() << R->getNameStr() << ", ";
        }
        dbgs() << "\n";
        dbgs() << "FalseBiasedRegions ";
        for (Region *R : Scope->FalseBiasedRegions) {
          dbgs() << R->getNameStr() << ", ";
        }
        dbgs() << "\n";
        dbgs() << "TrueBiasedSelects ";
        for (SelectInst *SI : Scope->TrueBiasedSelects) {
          dbgs() << *SI << ", ";
        }
        dbgs() << "\n";
        dbgs() << "FalseBiasedSelects ";
        for (SelectInst *SI : Scope->FalseBiasedSelects) {
          dbgs() << *SI << ", ";
        }
        dbgs() << "\n";);
  }
}

void CHR::classifyBiasedScopes(CHRScope *Scope, CHRScope *OutermostScope) {
  for (RegInfo &RI : Scope->RegInfos) {
    if (RI.HasBranch) {
      Region *R = RI.R;
      if (TrueBiasedRegionsGlobal.count(R) > 0)
        OutermostScope->TrueBiasedRegions.insert(R);
      else if (FalseBiasedRegionsGlobal.count(R) > 0)
        OutermostScope->FalseBiasedRegions.insert(R);
      else
        llvm_unreachable("Must be biased");
    }
    for (SelectInst *SI : RI.Selects) {
      if (TrueBiasedSelectsGlobal.count(SI) > 0)
        OutermostScope->TrueBiasedSelects.insert(SI);
      else if (FalseBiasedSelectsGlobal.count(SI) > 0)
        OutermostScope->FalseBiasedSelects.insert(SI);
      else
        llvm_unreachable("Must be biased");
    }
  }
  for (CHRScope *Sub : Scope->Subs) {
    classifyBiasedScopes(Sub, OutermostScope);
  }
}

static bool hasAtLeastTwoBiasedBranches(CHRScope *Scope) {
  unsigned NumBiased = Scope->TrueBiasedRegions.size() +
                       Scope->FalseBiasedRegions.size() +
                       Scope->TrueBiasedSelects.size() +
                       Scope->FalseBiasedSelects.size();
  return NumBiased >= CHRMergeThreshold;
}

void CHR::filterScopes(SmallVectorImpl<CHRScope *> &Input,
                       SmallVectorImpl<CHRScope *> &Output) {
  for (CHRScope *Scope : Input) {
    // Filter out the ones with only one region and no subs.
    if (!hasAtLeastTwoBiasedBranches(Scope)) {
      CHR_DEBUG(dbgs() << "Filtered out by biased branches truthy-regions "
                << Scope->TrueBiasedRegions.size()
                << " falsy-regions " << Scope->FalseBiasedRegions.size()
                << " true-selects " << Scope->TrueBiasedSelects.size()
                << " false-selects " << Scope->FalseBiasedSelects.size() << "\n");
      ORE.emit([&]() {
        return OptimizationRemarkMissed(
            DEBUG_TYPE,
            "DropScopeWithOneBranchOrSelect",
            Scope->RegInfos[0].R->getEntry()->getTerminator())
            << "Drop scope with < "
            << ore::NV("CHRMergeThreshold", CHRMergeThreshold)
            << " biased branch(es) or select(s)";
      });
      continue;
    }
    Output.push_back(Scope);
  }
}

void CHR::setCHRRegions(SmallVectorImpl<CHRScope *> &Input,
                        SmallVectorImpl<CHRScope *> &Output) {
  for (CHRScope *Scope : Input) {
    assert(Scope->HoistStopMap.empty() && Scope->CHRRegions.empty() &&
           "Empty");
    setCHRRegions(Scope, Scope);
    Output.push_back(Scope);
    CHR_DEBUG(
        dbgs() << "setCHRRegions HoistStopMap " << *Scope << "\n";
        for (auto pair : Scope->HoistStopMap) {
          Region *R = pair.first;
          dbgs() << "Region " << R->getNameStr() << "\n";
          for (Instruction *I : pair.second) {
            dbgs() << "HoistStop " << *I << "\n";
          }
        }
        dbgs() << "CHRRegions" << "\n";
        for (RegInfo &RI : Scope->CHRRegions) {
          dbgs() << RI.R->getNameStr() << "\n";
        });
  }
}

void CHR::setCHRRegions(CHRScope *Scope, CHRScope *OutermostScope) {
  DenseSet<Instruction *> Unhoistables;
  // Put the biased selects in Unhoistables because they should stay where they
  // are and constant-folded after CHR (in case one biased select or a branch
  // can depend on another biased select.)
  for (RegInfo &RI : Scope->RegInfos) {
    for (SelectInst *SI : RI.Selects) {
      Unhoistables.insert(SI);
    }
  }
  Instruction *InsertPoint = OutermostScope->BranchInsertPoint;
  for (RegInfo &RI : Scope->RegInfos) {
    Region *R = RI.R;
    DenseSet<Instruction *> HoistStops;
    bool IsHoisted = false;
    if (RI.HasBranch) {
      assert((OutermostScope->TrueBiasedRegions.count(R) > 0 ||
              OutermostScope->FalseBiasedRegions.count(R) > 0) &&
             "Must be truthy or falsy");
      auto *BI = cast<BranchInst>(R->getEntry()->getTerminator());
      // Note checkHoistValue fills in HoistStops.
      bool IsHoistable = checkHoistValue(BI->getCondition(), InsertPoint, DT,
                                         Unhoistables, &HoistStops);
      assert(IsHoistable && "Must be hoistable");
      (void)(IsHoistable);  // Unused in release build
      IsHoisted = true;
    }
    for (SelectInst *SI : RI.Selects) {
      assert((OutermostScope->TrueBiasedSelects.count(SI) > 0 ||
              OutermostScope->FalseBiasedSelects.count(SI) > 0) &&
             "Must be true or false biased");
      // Note checkHoistValue fills in HoistStops.
      bool IsHoistable = checkHoistValue(SI->getCondition(), InsertPoint, DT,
                                         Unhoistables, &HoistStops);
      assert(IsHoistable && "Must be hoistable");
      (void)(IsHoistable);  // Unused in release build
      IsHoisted = true;
    }
    if (IsHoisted) {
      OutermostScope->CHRRegions.push_back(RI);
      OutermostScope->HoistStopMap[R] = HoistStops;
    }
  }
  for (CHRScope *Sub : Scope->Subs)
    setCHRRegions(Sub, OutermostScope);
}

bool CHRScopeSorter(CHRScope *Scope1, CHRScope *Scope2) {
  return Scope1->RegInfos[0].R->getDepth() < Scope2->RegInfos[0].R->getDepth();
}

void CHR::sortScopes(SmallVectorImpl<CHRScope *> &Input,
                     SmallVectorImpl<CHRScope *> &Output) {
  Output.resize(Input.size());
  llvm::copy(Input, Output.begin());
  std::stable_sort(Output.begin(), Output.end(), CHRScopeSorter);
}

// Return true if V is already hoisted or was hoisted (along with its operands)
// to the insert point.
static void hoistValue(Value *V, Instruction *HoistPoint, Region *R,
                       HoistStopMapTy &HoistStopMap,
                       DenseSet<Instruction *> &HoistedSet,
                       DenseSet<PHINode *> &TrivialPHIs) {
  auto IT = HoistStopMap.find(R);
  assert(IT != HoistStopMap.end() && "Region must be in hoist stop map");
  DenseSet<Instruction *> &HoistStops = IT->second;
  if (auto *I = dyn_cast<Instruction>(V)) {
    if (I == HoistPoint)
      return;
    if (HoistStops.count(I))
      return;
    if (auto *PN = dyn_cast<PHINode>(I))
      if (TrivialPHIs.count(PN))
        // The trivial phi inserted by the previous CHR scope could replace a
        // non-phi in HoistStops. Note that since this phi is at the exit of a
        // previous CHR scope, which dominates this scope, it's safe to stop
        // hoisting there.
        return;
    if (HoistedSet.count(I))
      // Already hoisted, return.
      return;
    assert(isHoistableInstructionType(I) && "Unhoistable instruction type");
    for (Value *Op : I->operands()) {
      hoistValue(Op, HoistPoint, R, HoistStopMap, HoistedSet, TrivialPHIs);
    }
    I->moveBefore(HoistPoint);
    HoistedSet.insert(I);
    CHR_DEBUG(dbgs() << "hoistValue " << *I << "\n");
  }
}

// Hoist the dependent condition values of the branches and the selects in the
// scope to the insert point.
static void hoistScopeConditions(CHRScope *Scope, Instruction *HoistPoint,
                                 DenseSet<PHINode *> &TrivialPHIs) {
  DenseSet<Instruction *> HoistedSet;
  for (const RegInfo &RI : Scope->CHRRegions) {
    Region *R = RI.R;
    bool IsTrueBiased = Scope->TrueBiasedRegions.count(R);
    bool IsFalseBiased = Scope->FalseBiasedRegions.count(R);
    if (RI.HasBranch && (IsTrueBiased || IsFalseBiased)) {
      auto *BI = cast<BranchInst>(R->getEntry()->getTerminator());
      hoistValue(BI->getCondition(), HoistPoint, R, Scope->HoistStopMap,
                 HoistedSet, TrivialPHIs);
    }
    for (SelectInst *SI : RI.Selects) {
      bool IsTrueBiased = Scope->TrueBiasedSelects.count(SI);
      bool IsFalseBiased = Scope->FalseBiasedSelects.count(SI);
      if (!(IsTrueBiased || IsFalseBiased))
        continue;
      hoistValue(SI->getCondition(), HoistPoint, R, Scope->HoistStopMap,
                 HoistedSet, TrivialPHIs);
    }
  }
}

// Negate the predicate if an ICmp if it's used only by branches or selects by
// swapping the operands of the branches or the selects. Returns true if success.
static bool negateICmpIfUsedByBranchOrSelectOnly(ICmpInst *ICmp,
                                                 Instruction *ExcludedUser,
                                                 CHRScope *Scope) {
  for (User *U : ICmp->users()) {
    if (U == ExcludedUser)
      continue;
    if (isa<BranchInst>(U) && cast<BranchInst>(U)->isConditional())
      continue;
    if (isa<SelectInst>(U) && cast<SelectInst>(U)->getCondition() == ICmp)
      continue;
    return false;
  }
  for (User *U : ICmp->users()) {
    if (U == ExcludedUser)
      continue;
    if (auto *BI = dyn_cast<BranchInst>(U)) {
      assert(BI->isConditional() && "Must be conditional");
      BI->swapSuccessors();
      // Don't need to swap this in terms of
      // TrueBiasedRegions/FalseBiasedRegions because true-based/false-based
      // mean whehter the branch is likely go into the if-then rather than
      // successor0/successor1 and because we can tell which edge is the then or
      // the else one by comparing the destination to the region exit block.
      continue;
    }
    if (auto *SI = dyn_cast<SelectInst>(U)) {
      // Swap operands
      Value *TrueValue = SI->getTrueValue();
      Value *FalseValue = SI->getFalseValue();
      SI->setTrueValue(FalseValue);
      SI->setFalseValue(TrueValue);
      SI->swapProfMetadata();
      if (Scope->TrueBiasedSelects.count(SI)) {
        assert(Scope->FalseBiasedSelects.count(SI) == 0 &&
               "Must not be already in");
        Scope->FalseBiasedSelects.insert(SI);
      } else if (Scope->FalseBiasedSelects.count(SI)) {
        assert(Scope->TrueBiasedSelects.count(SI) == 0 &&
               "Must not be already in");
        Scope->TrueBiasedSelects.insert(SI);
      }
      continue;
    }
    llvm_unreachable("Must be a branch or a select");
  }
  ICmp->setPredicate(CmpInst::getInversePredicate(ICmp->getPredicate()));
  return true;
}

// A helper for transformScopes. Insert a trivial phi at the scope exit block
// for a value that's defined in the scope but used outside it (meaning it's
// alive at the exit block).
static void insertTrivialPHIs(CHRScope *Scope,
                              BasicBlock *EntryBlock, BasicBlock *ExitBlock,
                              DenseSet<PHINode *> &TrivialPHIs) {
  DenseSet<BasicBlock *> BlocksInScopeSet;
  SmallVector<BasicBlock *, 8> BlocksInScopeVec;
  for (RegInfo &RI : Scope->RegInfos) {
    for (BasicBlock *BB : RI.R->blocks()) { // This includes the blocks in the
                                            // sub-Scopes.
      BlocksInScopeSet.insert(BB);
      BlocksInScopeVec.push_back(BB);
    }
  }
  CHR_DEBUG(
      dbgs() << "Inserting redudant phis\n";
      for (BasicBlock *BB : BlocksInScopeVec) {
        dbgs() << "BlockInScope " << BB->getName() << "\n";
      });
  for (BasicBlock *BB : BlocksInScopeVec) {
    for (Instruction &I : *BB) {
      SmallVector<Instruction *, 8> Users;
      for (User *U : I.users()) {
        if (auto *UI = dyn_cast<Instruction>(U)) {
          if (BlocksInScopeSet.count(UI->getParent()) == 0 &&
              // Unless there's already a phi for I at the exit block.
              !(isa<PHINode>(UI) && UI->getParent() == ExitBlock)) {
            CHR_DEBUG(dbgs() << "V " << I << "\n");
            CHR_DEBUG(dbgs() << "Used outside scope by user " << *UI << "\n");
            Users.push_back(UI);
          } else if (UI->getParent() == EntryBlock && isa<PHINode>(UI)) {
            // There's a loop backedge from a block that's dominated by this
            // scope to the entry block.
            CHR_DEBUG(dbgs() << "V " << I << "\n");
            CHR_DEBUG(dbgs()
                      << "Used at entry block (for a back edge) by a phi user "
                      << *UI << "\n");
            Users.push_back(UI);
          }
        }
      }
      if (Users.size() > 0) {
        // Insert a trivial phi for I (phi [&I, P0], [&I, P1], ...) at
        // ExitBlock. Replace I with the new phi in UI unless UI is another
        // phi at ExitBlock.
        unsigned PredCount = std::distance(pred_begin(ExitBlock),
                                           pred_end(ExitBlock));
        PHINode *PN = PHINode::Create(I.getType(), PredCount, "",
                                      &ExitBlock->front());
        for (BasicBlock *Pred : predecessors(ExitBlock)) {
          PN->addIncoming(&I, Pred);
        }
        TrivialPHIs.insert(PN);
        CHR_DEBUG(dbgs() << "Insert phi " << *PN << "\n");
        for (Instruction *UI : Users) {
          for (unsigned J = 0, NumOps = UI->getNumOperands(); J < NumOps; ++J) {
            if (UI->getOperand(J) == &I) {
              UI->setOperand(J, PN);
            }
          }
          CHR_DEBUG(dbgs() << "Updated user " << *UI << "\n");
        }
      }
    }
  }
}

// Assert that all the CHR regions of the scope have a biased branch or select.
static void LLVM_ATTRIBUTE_UNUSED
assertCHRRegionsHaveBiasedBranchOrSelect(CHRScope *Scope) {
#ifndef NDEBUG
  auto HasBiasedBranchOrSelect = [](RegInfo &RI, CHRScope *Scope) {
    if (Scope->TrueBiasedRegions.count(RI.R) ||
        Scope->FalseBiasedRegions.count(RI.R))
      return true;
    for (SelectInst *SI : RI.Selects)
      if (Scope->TrueBiasedSelects.count(SI) ||
          Scope->FalseBiasedSelects.count(SI))
        return true;
    return false;
  };
  for (RegInfo &RI : Scope->CHRRegions) {
    assert(HasBiasedBranchOrSelect(RI, Scope) &&
           "Must have biased branch or select");
  }
#endif
}

// Assert that all the condition values of the biased branches and selects have
// been hoisted to the pre-entry block or outside of the scope.
static void LLVM_ATTRIBUTE_UNUSED assertBranchOrSelectConditionHoisted(
    CHRScope *Scope, BasicBlock *PreEntryBlock) {
  CHR_DEBUG(dbgs() << "Biased regions condition values \n");
  for (RegInfo &RI : Scope->CHRRegions) {
    Region *R = RI.R;
    bool IsTrueBiased = Scope->TrueBiasedRegions.count(R);
    bool IsFalseBiased = Scope->FalseBiasedRegions.count(R);
    if (RI.HasBranch && (IsTrueBiased || IsFalseBiased)) {
      auto *BI = cast<BranchInst>(R->getEntry()->getTerminator());
      Value *V = BI->getCondition();
      CHR_DEBUG(dbgs() << *V << "\n");
      if (auto *I = dyn_cast<Instruction>(V)) {
        (void)(I); // Unused in release build.
        assert((I->getParent() == PreEntryBlock ||
                !Scope->contains(I)) &&
               "Must have been hoisted to PreEntryBlock or outside the scope");
      }
    }
    for (SelectInst *SI : RI.Selects) {
      bool IsTrueBiased = Scope->TrueBiasedSelects.count(SI);
      bool IsFalseBiased = Scope->FalseBiasedSelects.count(SI);
      if (!(IsTrueBiased || IsFalseBiased))
        continue;
      Value *V = SI->getCondition();
      CHR_DEBUG(dbgs() << *V << "\n");
      if (auto *I = dyn_cast<Instruction>(V)) {
        (void)(I); // Unused in release build.
        assert((I->getParent() == PreEntryBlock ||
                !Scope->contains(I)) &&
               "Must have been hoisted to PreEntryBlock or outside the scope");
      }
    }
  }
}

void CHR::transformScopes(CHRScope *Scope, DenseSet<PHINode *> &TrivialPHIs) {
  CHR_DEBUG(dbgs() << "transformScopes " << *Scope << "\n");

  assert(Scope->RegInfos.size() >= 1 && "Should have at least one Region");
  Region *FirstRegion = Scope->RegInfos[0].R;
  BasicBlock *EntryBlock = FirstRegion->getEntry();
  Region *LastRegion = Scope->RegInfos[Scope->RegInfos.size() - 1].R;
  BasicBlock *ExitBlock = LastRegion->getExit();
  Optional<uint64_t> ProfileCount = BFI.getBlockProfileCount(EntryBlock);

  if (ExitBlock) {
    // Insert a trivial phi at the exit block (where the CHR hot path and the
    // cold path merges) for a value that's defined in the scope but used
    // outside it (meaning it's alive at the exit block). We will add the
    // incoming values for the CHR cold paths to it below. Without this, we'd
    // miss updating phi's for such values unless there happens to already be a
    // phi for that value there.
    insertTrivialPHIs(Scope, EntryBlock, ExitBlock, TrivialPHIs);
  }

  // Split the entry block of the first region. The new block becomes the new
  // entry block of the first region. The old entry block becomes the block to
  // insert the CHR branch into. Note DT gets updated. Since DT gets updated
  // through the split, we update the entry of the first region after the split,
  // and Region only points to the entry and the exit blocks, rather than
  // keeping everything in a list or set, the blocks membership and the
  // entry/exit blocks of the region are still valid after the split.
  CHR_DEBUG(dbgs() << "Splitting entry block " << EntryBlock->getName()
            << " at " << *Scope->BranchInsertPoint << "\n");
  BasicBlock *NewEntryBlock =
      SplitBlock(EntryBlock, Scope->BranchInsertPoint, &DT);
  assert(NewEntryBlock->getSinglePredecessor() == EntryBlock &&
         "NewEntryBlock's only pred must be EntryBlock");
  FirstRegion->replaceEntryRecursive(NewEntryBlock);
  BasicBlock *PreEntryBlock = EntryBlock;

  ValueToValueMapTy VMap;
  // Clone the blocks in the scope (excluding the PreEntryBlock) to split into a
  // hot path (originals) and a cold path (clones) and update the PHIs at the
  // exit block.
  cloneScopeBlocks(Scope, PreEntryBlock, ExitBlock, LastRegion, VMap);

  // Replace the old (placeholder) branch with the new (merged) conditional
  // branch.
  BranchInst *MergedBr = createMergedBranch(PreEntryBlock, EntryBlock,
                                            NewEntryBlock, VMap);

#ifndef NDEBUG
  assertCHRRegionsHaveBiasedBranchOrSelect(Scope);
#endif

  // Hoist the conditional values of the branches/selects.
  hoistScopeConditions(Scope, PreEntryBlock->getTerminator(), TrivialPHIs);

#ifndef NDEBUG
  assertBranchOrSelectConditionHoisted(Scope, PreEntryBlock);
#endif

  // Create the combined branch condition and constant-fold the branches/selects
  // in the hot path.
  fixupBranchesAndSelects(Scope, PreEntryBlock, MergedBr,
                          ProfileCount ? ProfileCount.getValue() : 0);
}

// A helper for transformScopes. Clone the blocks in the scope (excluding the
// PreEntryBlock) to split into a hot path and a cold path and update the PHIs
// at the exit block.
void CHR::cloneScopeBlocks(CHRScope *Scope,
                           BasicBlock *PreEntryBlock,
                           BasicBlock *ExitBlock,
                           Region *LastRegion,
                           ValueToValueMapTy &VMap) {
  // Clone all the blocks. The original blocks will be the hot-path
  // CHR-optimized code and the cloned blocks will be the original unoptimized
  // code. This is so that the block pointers from the
  // CHRScope/Region/RegionInfo can stay valid in pointing to the hot-path code
  // which CHR should apply to.
  SmallVector<BasicBlock*, 8> NewBlocks;
  for (RegInfo &RI : Scope->RegInfos)
    for (BasicBlock *BB : RI.R->blocks()) { // This includes the blocks in the
                                            // sub-Scopes.
      assert(BB != PreEntryBlock && "Don't copy the preetntry block");
      BasicBlock *NewBB = CloneBasicBlock(BB, VMap, ".nonchr", &F);
      NewBlocks.push_back(NewBB);
      VMap[BB] = NewBB;
    }

  // Place the cloned blocks right after the original blocks (right before the
  // exit block of.)
  if (ExitBlock)
    F.getBasicBlockList().splice(ExitBlock->getIterator(),
                                 F.getBasicBlockList(),
                                 NewBlocks[0]->getIterator(), F.end());

  // Update the cloned blocks/instructions to refer to themselves.
  for (unsigned i = 0, e = NewBlocks.size(); i != e; ++i)
    for (Instruction &I : *NewBlocks[i])
      RemapInstruction(&I, VMap,
                       RF_NoModuleLevelChanges | RF_IgnoreMissingLocals);

  // Add the cloned blocks to the PHIs of the exit blocks. ExitBlock is null for
  // the top-level region but we don't need to add PHIs. The trivial PHIs
  // inserted above will be updated here.
  if (ExitBlock)
    for (PHINode &PN : ExitBlock->phis())
      for (unsigned I = 0, NumOps = PN.getNumIncomingValues(); I < NumOps;
           ++I) {
        BasicBlock *Pred = PN.getIncomingBlock(I);
        if (LastRegion->contains(Pred)) {
          Value *V = PN.getIncomingValue(I);
          auto It = VMap.find(V);
          if (It != VMap.end()) V = It->second;
          assert(VMap.find(Pred) != VMap.end() && "Pred must have been cloned");
          PN.addIncoming(V, cast<BasicBlock>(VMap[Pred]));
        }
      }
}

// A helper for transformScope. Replace the old (placeholder) branch with the
// new (merged) conditional branch.
BranchInst *CHR::createMergedBranch(BasicBlock *PreEntryBlock,
                                    BasicBlock *EntryBlock,
                                    BasicBlock *NewEntryBlock,
                                    ValueToValueMapTy &VMap) {
  BranchInst *OldBR = cast<BranchInst>(PreEntryBlock->getTerminator());
  assert(OldBR->isUnconditional() && OldBR->getSuccessor(0) == NewEntryBlock &&
         "SplitBlock did not work correctly!");
  assert(NewEntryBlock->getSinglePredecessor() == EntryBlock &&
         "NewEntryBlock's only pred must be EntryBlock");
  assert(VMap.find(NewEntryBlock) != VMap.end() &&
         "NewEntryBlock must have been copied");
  OldBR->dropAllReferences();
  OldBR->eraseFromParent();
  // The true predicate is a placeholder. It will be replaced later in
  // fixupBranchesAndSelects().
  BranchInst *NewBR = BranchInst::Create(NewEntryBlock,
                                         cast<BasicBlock>(VMap[NewEntryBlock]),
                                         ConstantInt::getTrue(F.getContext()));
  PreEntryBlock->getInstList().push_back(NewBR);
  assert(NewEntryBlock->getSinglePredecessor() == EntryBlock &&
         "NewEntryBlock's only pred must be EntryBlock");
  return NewBR;
}

// A helper for transformScopes. Create the combined branch condition and
// constant-fold the branches/selects in the hot path.
void CHR::fixupBranchesAndSelects(CHRScope *Scope,
                                  BasicBlock *PreEntryBlock,
                                  BranchInst *MergedBR,
                                  uint64_t ProfileCount) {
  Value *MergedCondition = ConstantInt::getTrue(F.getContext());
  BranchProbability CHRBranchBias(1, 1);
  uint64_t NumCHRedBranches = 0;
  IRBuilder<> IRB(PreEntryBlock->getTerminator());
  for (RegInfo &RI : Scope->CHRRegions) {
    Region *R = RI.R;
    if (RI.HasBranch) {
      fixupBranch(R, Scope, IRB, MergedCondition, CHRBranchBias);
      ++NumCHRedBranches;
    }
    for (SelectInst *SI : RI.Selects) {
      fixupSelect(SI, Scope, IRB, MergedCondition, CHRBranchBias);
      ++NumCHRedBranches;
    }
  }
  Stats.NumBranchesDelta += NumCHRedBranches - 1;
  Stats.WeightedNumBranchesDelta += (NumCHRedBranches - 1) * ProfileCount;
  ORE.emit([&]() {
    return OptimizationRemark(DEBUG_TYPE,
                              "CHR",
                              // Refer to the hot (original) path
                              MergedBR->getSuccessor(0)->getTerminator())
        << "Merged " << ore::NV("NumCHRedBranches", NumCHRedBranches)
        << " branches or selects";
  });
  MergedBR->setCondition(MergedCondition);
  SmallVector<uint32_t, 2> Weights;
  Weights.push_back(static_cast<uint32_t>(CHRBranchBias.scale(1000)));
  Weights.push_back(static_cast<uint32_t>(CHRBranchBias.getCompl().scale(1000)));
  MDBuilder MDB(F.getContext());
  MergedBR->setMetadata(LLVMContext::MD_prof, MDB.createBranchWeights(Weights));
  CHR_DEBUG(dbgs() << "CHR branch bias " << Weights[0] << ":" << Weights[1]
            << "\n");
}

// A helper for fixupBranchesAndSelects. Add to the combined branch condition
// and constant-fold a branch in the hot path.
void CHR::fixupBranch(Region *R, CHRScope *Scope,
                      IRBuilder<> &IRB,
                      Value *&MergedCondition,
                      BranchProbability &CHRBranchBias) {
  bool IsTrueBiased = Scope->TrueBiasedRegions.count(R);
  assert((IsTrueBiased || Scope->FalseBiasedRegions.count(R)) &&
         "Must be truthy or falsy");
  auto *BI = cast<BranchInst>(R->getEntry()->getTerminator());
  assert(BranchBiasMap.find(R) != BranchBiasMap.end() &&
         "Must be in the bias map");
  BranchProbability Bias = BranchBiasMap[R];
  assert(Bias >= getCHRBiasThreshold() && "Must be highly biased");
  // Take the min.
  if (CHRBranchBias > Bias)
    CHRBranchBias = Bias;
  BasicBlock *IfThen = BI->getSuccessor(1);
  BasicBlock *IfElse = BI->getSuccessor(0);
  BasicBlock *RegionExitBlock = R->getExit();
  assert(RegionExitBlock && "Null ExitBlock");
  assert((IfThen == RegionExitBlock || IfElse == RegionExitBlock) &&
         IfThen != IfElse && "Invariant from findScopes");
  if (IfThen == RegionExitBlock) {
    // Swap them so that IfThen means going into it and IfElse means skipping
    // it.
    std::swap(IfThen, IfElse);
  }
  CHR_DEBUG(dbgs() << "IfThen " << IfThen->getName()
            << " IfElse " << IfElse->getName() << "\n");
  Value *Cond = BI->getCondition();
  BasicBlock *HotTarget = IsTrueBiased ? IfThen : IfElse;
  bool ConditionTrue = HotTarget == BI->getSuccessor(0);
  addToMergedCondition(ConditionTrue, Cond, BI, Scope, IRB,
                       MergedCondition);
  // Constant-fold the branch at ClonedEntryBlock.
  assert(ConditionTrue == (HotTarget == BI->getSuccessor(0)) &&
         "The successor shouldn't change");
  Value *NewCondition = ConditionTrue ?
                        ConstantInt::getTrue(F.getContext()) :
                        ConstantInt::getFalse(F.getContext());
  BI->setCondition(NewCondition);
}

// A helper for fixupBranchesAndSelects. Add to the combined branch condition
// and constant-fold a select in the hot path.
void CHR::fixupSelect(SelectInst *SI, CHRScope *Scope,
                      IRBuilder<> &IRB,
                      Value *&MergedCondition,
                      BranchProbability &CHRBranchBias) {
  bool IsTrueBiased = Scope->TrueBiasedSelects.count(SI);
  assert((IsTrueBiased ||
          Scope->FalseBiasedSelects.count(SI)) && "Must be biased");
  assert(SelectBiasMap.find(SI) != SelectBiasMap.end() &&
         "Must be in the bias map");
  BranchProbability Bias = SelectBiasMap[SI];
  assert(Bias >= getCHRBiasThreshold() && "Must be highly biased");
  // Take the min.
  if (CHRBranchBias > Bias)
    CHRBranchBias = Bias;
  Value *Cond = SI->getCondition();
  addToMergedCondition(IsTrueBiased, Cond, SI, Scope, IRB,
                       MergedCondition);
  Value *NewCondition = IsTrueBiased ?
                        ConstantInt::getTrue(F.getContext()) :
                        ConstantInt::getFalse(F.getContext());
  SI->setCondition(NewCondition);
}

// A helper for fixupBranch/fixupSelect. Add a branch condition to the merged
// condition.
void CHR::addToMergedCondition(bool IsTrueBiased, Value *Cond,
                               Instruction *BranchOrSelect,
                               CHRScope *Scope,
                               IRBuilder<> &IRB,
                               Value *&MergedCondition) {
  if (IsTrueBiased) {
    MergedCondition = IRB.CreateAnd(MergedCondition, Cond);
  } else {
    // If Cond is an icmp and all users of V except for BranchOrSelect is a
    // branch, negate the icmp predicate and swap the branch targets and avoid
    // inserting an Xor to negate Cond.
    bool Done = false;
    if (auto *ICmp = dyn_cast<ICmpInst>(Cond))
      if (negateICmpIfUsedByBranchOrSelectOnly(ICmp, BranchOrSelect, Scope)) {
        MergedCondition = IRB.CreateAnd(MergedCondition, Cond);
        Done = true;
      }
    if (!Done) {
      Value *Negate = IRB.CreateXor(
          ConstantInt::getTrue(F.getContext()), Cond);
      MergedCondition = IRB.CreateAnd(MergedCondition, Negate);
    }
  }
}

void CHR::transformScopes(SmallVectorImpl<CHRScope *> &CHRScopes) {
  unsigned I = 0;
  DenseSet<PHINode *> TrivialPHIs;
  for (CHRScope *Scope : CHRScopes) {
    transformScopes(Scope, TrivialPHIs);
    CHR_DEBUG(
        std::ostringstream oss;
        oss << " after transformScopes " << I++;
        dumpIR(F, oss.str().c_str(), nullptr));
    (void)I;
  }
}

static void LLVM_ATTRIBUTE_UNUSED
dumpScopes(SmallVectorImpl<CHRScope *> &Scopes, const char *Label) {
  dbgs() << Label << " " << Scopes.size() << "\n";
  for (CHRScope *Scope : Scopes) {
    dbgs() << *Scope << "\n";
  }
}

bool CHR::run() {
  if (!shouldApply(F, PSI))
    return false;

  CHR_DEBUG(dumpIR(F, "before", nullptr));

  bool Changed = false;
  {
    CHR_DEBUG(
        dbgs() << "RegionInfo:\n";
        RI.print(dbgs()));

    // Recursively traverse the region tree and find regions that have biased
    // branches and/or selects and create scopes.
    SmallVector<CHRScope *, 8> AllScopes;
    findScopes(AllScopes);
    CHR_DEBUG(dumpScopes(AllScopes, "All scopes"));

    // Split the scopes if 1) the conditiona values of the biased
    // branches/selects of the inner/lower scope can't be hoisted up to the
    // outermost/uppermost scope entry, or 2) the condition values of the biased
    // branches/selects in a scope (including subscopes) don't share at least
    // one common value.
    SmallVector<CHRScope *, 8> SplitScopes;
    splitScopes(AllScopes, SplitScopes);
    CHR_DEBUG(dumpScopes(SplitScopes, "Split scopes"));

    // After splitting, set the biased regions and selects of a scope (a tree
    // root) that include those of the subscopes.
    classifyBiasedScopes(SplitScopes);
    CHR_DEBUG(dbgs() << "Set per-scope bias " << SplitScopes.size() << "\n");

    // Filter out the scopes that has only one biased region or select (CHR
    // isn't useful in such a case).
    SmallVector<CHRScope *, 8> FilteredScopes;
    filterScopes(SplitScopes, FilteredScopes);
    CHR_DEBUG(dumpScopes(FilteredScopes, "Filtered scopes"));

    // Set the regions to be CHR'ed and their hoist stops for each scope.
    SmallVector<CHRScope *, 8> SetScopes;
    setCHRRegions(FilteredScopes, SetScopes);
    CHR_DEBUG(dumpScopes(SetScopes, "Set CHR regions"));

    // Sort CHRScopes by the depth so that outer CHRScopes comes before inner
    // ones. We need to apply CHR from outer to inner so that we apply CHR only
    // to the hot path, rather than both hot and cold paths.
    SmallVector<CHRScope *, 8> SortedScopes;
    sortScopes(SetScopes, SortedScopes);
    CHR_DEBUG(dumpScopes(SortedScopes, "Sorted scopes"));

    CHR_DEBUG(
        dbgs() << "RegionInfo:\n";
        RI.print(dbgs()));

    // Apply the CHR transformation.
    if (!SortedScopes.empty()) {
      transformScopes(SortedScopes);
      Changed = true;
    }
  }

  if (Changed) {
    CHR_DEBUG(dumpIR(F, "after", &Stats));
    ORE.emit([&]() {
      return OptimizationRemark(DEBUG_TYPE, "Stats", &F)
          << ore::NV("Function", &F) << " "
          << "Reduced the number of branches in hot paths by "
          << ore::NV("NumBranchesDelta", Stats.NumBranchesDelta)
          << " (static) and "
          << ore::NV("WeightedNumBranchesDelta", Stats.WeightedNumBranchesDelta)
          << " (weighted by PGO count)";
    });
  }

  return Changed;
}

bool ControlHeightReductionLegacyPass::runOnFunction(Function &F) {
  BlockFrequencyInfo &BFI =
      getAnalysis<BlockFrequencyInfoWrapperPass>().getBFI();
  DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  ProfileSummaryInfo &PSI =
      getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI();
  RegionInfo &RI = getAnalysis<RegionInfoPass>().getRegionInfo();
  std::unique_ptr<OptimizationRemarkEmitter> OwnedORE =
      llvm::make_unique<OptimizationRemarkEmitter>(&F);
  return CHR(F, BFI, DT, PSI, RI, *OwnedORE.get()).run();
}

namespace llvm {

ControlHeightReductionPass::ControlHeightReductionPass() {
  parseCHRFilterFiles();
}

PreservedAnalyses ControlHeightReductionPass::run(
    Function &F,
    FunctionAnalysisManager &FAM) {
  auto &BFI = FAM.getResult<BlockFrequencyAnalysis>(F);
  auto &DT = FAM.getResult<DominatorTreeAnalysis>(F);
  auto &MAMProxy = FAM.getResult<ModuleAnalysisManagerFunctionProxy>(F);
  auto &MAM = MAMProxy.getManager();
  auto &PSI = *MAM.getCachedResult<ProfileSummaryAnalysis>(*F.getParent());
  auto &RI = FAM.getResult<RegionInfoAnalysis>(F);
  auto &ORE = FAM.getResult<OptimizationRemarkEmitterAnalysis>(F);
  bool Changed = CHR(F, BFI, DT, PSI, RI, ORE).run();
  if (!Changed)
    return PreservedAnalyses::all();
  auto PA = PreservedAnalyses();
  PA.preserve<GlobalsAA>();
  return PA;
}

} // namespace llvm
