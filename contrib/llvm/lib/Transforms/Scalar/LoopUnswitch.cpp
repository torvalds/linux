//===- LoopUnswitch.cpp - Hoist loop-invariant conditionals in loop -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass transforms loops that contain branches on loop-invariant conditions
// to multiple loops.  For example, it turns the left into the right code:
//
//  for (...)                  if (lic)
//    A                          for (...)
//    if (lic)                     A; B; C
//      B                      else
//    C                          for (...)
//                                 A; C
//
// This can increase the size of the code exponentially (doubling it every time
// a loop is unswitched) so we only unswitch if the resultant code will be
// smaller than a threshold.
//
// This pass expects LICM to be run before it to hoist invariant conditions out
// of the loop, to make the unswitching opportunity obvious.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LegacyDivergenceAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <algorithm>
#include <cassert>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "loop-unswitch"

STATISTIC(NumBranches, "Number of branches unswitched");
STATISTIC(NumSwitches, "Number of switches unswitched");
STATISTIC(NumGuards,   "Number of guards unswitched");
STATISTIC(NumSelects , "Number of selects unswitched");
STATISTIC(NumTrivial , "Number of unswitches that are trivial");
STATISTIC(NumSimplify, "Number of simplifications of unswitched code");
STATISTIC(TotalInsts,  "Total number of instructions analyzed");

// The specific value of 100 here was chosen based only on intuition and a
// few specific examples.
static cl::opt<unsigned>
Threshold("loop-unswitch-threshold", cl::desc("Max loop size to unswitch"),
          cl::init(100), cl::Hidden);

namespace {

  class LUAnalysisCache {
    using UnswitchedValsMap =
        DenseMap<const SwitchInst *, SmallPtrSet<const Value *, 8>>;
    using UnswitchedValsIt = UnswitchedValsMap::iterator;

    struct LoopProperties {
      unsigned CanBeUnswitchedCount;
      unsigned WasUnswitchedCount;
      unsigned SizeEstimation;
      UnswitchedValsMap UnswitchedVals;
    };

    // Here we use std::map instead of DenseMap, since we need to keep valid
    // LoopProperties pointer for current loop for better performance.
    using LoopPropsMap = std::map<const Loop *, LoopProperties>;
    using LoopPropsMapIt = LoopPropsMap::iterator;

    LoopPropsMap LoopsProperties;
    UnswitchedValsMap *CurLoopInstructions = nullptr;
    LoopProperties *CurrentLoopProperties = nullptr;

    // A loop unswitching with an estimated cost above this threshold
    // is not performed. MaxSize is turned into unswitching quota for
    // the current loop, and reduced correspondingly, though note that
    // the quota is returned by releaseMemory() when the loop has been
    // processed, so that MaxSize will return to its previous
    // value. So in most cases MaxSize will equal the Threshold flag
    // when a new loop is processed. An exception to that is that
    // MaxSize will have a smaller value while processing nested loops
    // that were introduced due to loop unswitching of an outer loop.
    //
    // FIXME: The way that MaxSize works is subtle and depends on the
    // pass manager processing loops and calling releaseMemory() in a
    // specific order. It would be good to find a more straightforward
    // way of doing what MaxSize does.
    unsigned MaxSize;

  public:
    LUAnalysisCache() : MaxSize(Threshold) {}

    // Analyze loop. Check its size, calculate is it possible to unswitch
    // it. Returns true if we can unswitch this loop.
    bool countLoop(const Loop *L, const TargetTransformInfo &TTI,
                   AssumptionCache *AC);

    // Clean all data related to given loop.
    void forgetLoop(const Loop *L);

    // Mark case value as unswitched.
    // Since SI instruction can be partly unswitched, in order to avoid
    // extra unswitching in cloned loops keep track all unswitched values.
    void setUnswitched(const SwitchInst *SI, const Value *V);

    // Check was this case value unswitched before or not.
    bool isUnswitched(const SwitchInst *SI, const Value *V);

    // Returns true if another unswitching could be done within the cost
    // threshold.
    bool CostAllowsUnswitching();

    // Clone all loop-unswitch related loop properties.
    // Redistribute unswitching quotas.
    // Note, that new loop data is stored inside the VMap.
    void cloneData(const Loop *NewLoop, const Loop *OldLoop,
                   const ValueToValueMapTy &VMap);
  };

  class LoopUnswitch : public LoopPass {
    LoopInfo *LI;  // Loop information
    LPPassManager *LPM;
    AssumptionCache *AC;

    // Used to check if second loop needs processing after
    // RewriteLoopBodyWithConditionConstant rewrites first loop.
    std::vector<Loop*> LoopProcessWorklist;

    LUAnalysisCache BranchesInfo;

    bool OptimizeForSize;
    bool redoLoop = false;

    Loop *currentLoop = nullptr;
    DominatorTree *DT = nullptr;
    MemorySSA *MSSA = nullptr;
    std::unique_ptr<MemorySSAUpdater> MSSAU;
    BasicBlock *loopHeader = nullptr;
    BasicBlock *loopPreheader = nullptr;

    bool SanitizeMemory;
    SimpleLoopSafetyInfo SafetyInfo;

    // LoopBlocks contains all of the basic blocks of the loop, including the
    // preheader of the loop, the body of the loop, and the exit blocks of the
    // loop, in that order.
    std::vector<BasicBlock*> LoopBlocks;
    // NewBlocks contained cloned copy of basic blocks from LoopBlocks.
    std::vector<BasicBlock*> NewBlocks;

    bool hasBranchDivergence;

  public:
    static char ID; // Pass ID, replacement for typeid

    explicit LoopUnswitch(bool Os = false, bool hasBranchDivergence = false)
        : LoopPass(ID), OptimizeForSize(Os),
          hasBranchDivergence(hasBranchDivergence) {
        initializeLoopUnswitchPass(*PassRegistry::getPassRegistry());
    }

    bool runOnLoop(Loop *L, LPPassManager &LPM) override;
    bool processCurrentLoop();
    bool isUnreachableDueToPreviousUnswitching(BasicBlock *);

    /// This transformation requires natural loop information & requires that
    /// loop preheaders be inserted into the CFG.
    ///
    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<AssumptionCacheTracker>();
      AU.addRequired<TargetTransformInfoWrapperPass>();
      if (EnableMSSALoopDependency) {
        AU.addRequired<MemorySSAWrapperPass>();
        AU.addPreserved<MemorySSAWrapperPass>();
      }
      if (hasBranchDivergence)
        AU.addRequired<LegacyDivergenceAnalysis>();
      getLoopAnalysisUsage(AU);
    }

  private:
    void releaseMemory() override {
      BranchesInfo.forgetLoop(currentLoop);
    }

    void initLoopData() {
      loopHeader = currentLoop->getHeader();
      loopPreheader = currentLoop->getLoopPreheader();
    }

    /// Split all of the edges from inside the loop to their exit blocks.
    /// Update the appropriate Phi nodes as we do so.
    void SplitExitEdges(Loop *L,
                        const SmallVectorImpl<BasicBlock *> &ExitBlocks);

    bool TryTrivialLoopUnswitch(bool &Changed);

    bool UnswitchIfProfitable(Value *LoopCond, Constant *Val,
                              Instruction *TI = nullptr);
    void UnswitchTrivialCondition(Loop *L, Value *Cond, Constant *Val,
                                  BasicBlock *ExitBlock, Instruction *TI);
    void UnswitchNontrivialCondition(Value *LIC, Constant *OnVal, Loop *L,
                                     Instruction *TI);

    void RewriteLoopBodyWithConditionConstant(Loop *L, Value *LIC,
                                              Constant *Val, bool isEqual);

    void EmitPreheaderBranchOnCondition(Value *LIC, Constant *Val,
                                        BasicBlock *TrueDest,
                                        BasicBlock *FalseDest,
                                        BranchInst *OldBranch, Instruction *TI);

    void SimplifyCode(std::vector<Instruction*> &Worklist, Loop *L);

    /// Given that the Invariant is not equal to Val. Simplify instructions
    /// in the loop.
    Value *SimplifyInstructionWithNotEqual(Instruction *Inst, Value *Invariant,
                                           Constant *Val);
  };

} // end anonymous namespace

// Analyze loop. Check its size, calculate is it possible to unswitch
// it. Returns true if we can unswitch this loop.
bool LUAnalysisCache::countLoop(const Loop *L, const TargetTransformInfo &TTI,
                                AssumptionCache *AC) {
  LoopPropsMapIt PropsIt;
  bool Inserted;
  std::tie(PropsIt, Inserted) =
      LoopsProperties.insert(std::make_pair(L, LoopProperties()));

  LoopProperties &Props = PropsIt->second;

  if (Inserted) {
    // New loop.

    // Limit the number of instructions to avoid causing significant code
    // expansion, and the number of basic blocks, to avoid loops with
    // large numbers of branches which cause loop unswitching to go crazy.
    // This is a very ad-hoc heuristic.

    SmallPtrSet<const Value *, 32> EphValues;
    CodeMetrics::collectEphemeralValues(L, AC, EphValues);

    // FIXME: This is overly conservative because it does not take into
    // consideration code simplification opportunities and code that can
    // be shared by the resultant unswitched loops.
    CodeMetrics Metrics;
    for (Loop::block_iterator I = L->block_begin(), E = L->block_end(); I != E;
         ++I)
      Metrics.analyzeBasicBlock(*I, TTI, EphValues);

    Props.SizeEstimation = Metrics.NumInsts;
    Props.CanBeUnswitchedCount = MaxSize / (Props.SizeEstimation);
    Props.WasUnswitchedCount = 0;
    MaxSize -= Props.SizeEstimation * Props.CanBeUnswitchedCount;

    if (Metrics.notDuplicatable) {
      LLVM_DEBUG(dbgs() << "NOT unswitching loop %" << L->getHeader()->getName()
                        << ", contents cannot be "
                        << "duplicated!\n");
      return false;
    }
  }

  // Be careful. This links are good only before new loop addition.
  CurrentLoopProperties = &Props;
  CurLoopInstructions = &Props.UnswitchedVals;

  return true;
}

// Clean all data related to given loop.
void LUAnalysisCache::forgetLoop(const Loop *L) {
  LoopPropsMapIt LIt = LoopsProperties.find(L);

  if (LIt != LoopsProperties.end()) {
    LoopProperties &Props = LIt->second;
    MaxSize += (Props.CanBeUnswitchedCount + Props.WasUnswitchedCount) *
               Props.SizeEstimation;
    LoopsProperties.erase(LIt);
  }

  CurrentLoopProperties = nullptr;
  CurLoopInstructions = nullptr;
}

// Mark case value as unswitched.
// Since SI instruction can be partly unswitched, in order to avoid
// extra unswitching in cloned loops keep track all unswitched values.
void LUAnalysisCache::setUnswitched(const SwitchInst *SI, const Value *V) {
  (*CurLoopInstructions)[SI].insert(V);
}

// Check was this case value unswitched before or not.
bool LUAnalysisCache::isUnswitched(const SwitchInst *SI, const Value *V) {
  return (*CurLoopInstructions)[SI].count(V);
}

bool LUAnalysisCache::CostAllowsUnswitching() {
  return CurrentLoopProperties->CanBeUnswitchedCount > 0;
}

// Clone all loop-unswitch related loop properties.
// Redistribute unswitching quotas.
// Note, that new loop data is stored inside the VMap.
void LUAnalysisCache::cloneData(const Loop *NewLoop, const Loop *OldLoop,
                                const ValueToValueMapTy &VMap) {
  LoopProperties &NewLoopProps = LoopsProperties[NewLoop];
  LoopProperties &OldLoopProps = *CurrentLoopProperties;
  UnswitchedValsMap &Insts = OldLoopProps.UnswitchedVals;

  // Reallocate "can-be-unswitched quota"

  --OldLoopProps.CanBeUnswitchedCount;
  ++OldLoopProps.WasUnswitchedCount;
  NewLoopProps.WasUnswitchedCount = 0;
  unsigned Quota = OldLoopProps.CanBeUnswitchedCount;
  NewLoopProps.CanBeUnswitchedCount = Quota / 2;
  OldLoopProps.CanBeUnswitchedCount = Quota - Quota / 2;

  NewLoopProps.SizeEstimation = OldLoopProps.SizeEstimation;

  // Clone unswitched values info:
  // for new loop switches we clone info about values that was
  // already unswitched and has redundant successors.
  for (UnswitchedValsIt I = Insts.begin(); I != Insts.end(); ++I) {
    const SwitchInst *OldInst = I->first;
    Value *NewI = VMap.lookup(OldInst);
    const SwitchInst *NewInst = cast_or_null<SwitchInst>(NewI);
    assert(NewInst && "All instructions that are in SrcBB must be in VMap.");

    NewLoopProps.UnswitchedVals[NewInst] = OldLoopProps.UnswitchedVals[OldInst];
  }
}

char LoopUnswitch::ID = 0;

INITIALIZE_PASS_BEGIN(LoopUnswitch, "loop-unswitch", "Unswitch loops",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(LoopPass)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LegacyDivergenceAnalysis)
INITIALIZE_PASS_DEPENDENCY(MemorySSAWrapperPass)
INITIALIZE_PASS_END(LoopUnswitch, "loop-unswitch", "Unswitch loops",
                      false, false)

Pass *llvm::createLoopUnswitchPass(bool Os, bool hasBranchDivergence) {
  return new LoopUnswitch(Os, hasBranchDivergence);
}

/// Operator chain lattice.
enum OperatorChain {
  OC_OpChainNone,    ///< There is no operator.
  OC_OpChainOr,      ///< There are only ORs.
  OC_OpChainAnd,     ///< There are only ANDs.
  OC_OpChainMixed    ///< There are ANDs and ORs.
};

/// Cond is a condition that occurs in L. If it is invariant in the loop, or has
/// an invariant piece, return the invariant. Otherwise, return null.
//
/// NOTE: FindLIVLoopCondition will not return a partial LIV by walking up a
/// mixed operator chain, as we can not reliably find a value which will simplify
/// the operator chain. If the chain is AND-only or OR-only, we can use 0 or ~0
/// to simplify the chain.
///
/// NOTE: In case a partial LIV and a mixed operator chain, we may be able to
/// simplify the condition itself to a loop variant condition, but at the
/// cost of creating an entirely new loop.
static Value *FindLIVLoopCondition(Value *Cond, Loop *L, bool &Changed,
                                   OperatorChain &ParentChain,
                                   DenseMap<Value *, Value *> &Cache) {
  auto CacheIt = Cache.find(Cond);
  if (CacheIt != Cache.end())
    return CacheIt->second;

  // We started analyze new instruction, increment scanned instructions counter.
  ++TotalInsts;

  // We can never unswitch on vector conditions.
  if (Cond->getType()->isVectorTy())
    return nullptr;

  // Constants should be folded, not unswitched on!
  if (isa<Constant>(Cond)) return nullptr;

  // TODO: Handle: br (VARIANT|INVARIANT).

  // Hoist simple values out.
  if (L->makeLoopInvariant(Cond, Changed)) {
    Cache[Cond] = Cond;
    return Cond;
  }

  // Walk up the operator chain to find partial invariant conditions.
  if (BinaryOperator *BO = dyn_cast<BinaryOperator>(Cond))
    if (BO->getOpcode() == Instruction::And ||
        BO->getOpcode() == Instruction::Or) {
      // Given the previous operator, compute the current operator chain status.
      OperatorChain NewChain;
      switch (ParentChain) {
      case OC_OpChainNone:
        NewChain = BO->getOpcode() == Instruction::And ? OC_OpChainAnd :
                                      OC_OpChainOr;
        break;
      case OC_OpChainOr:
        NewChain = BO->getOpcode() == Instruction::Or ? OC_OpChainOr :
                                      OC_OpChainMixed;
        break;
      case OC_OpChainAnd:
        NewChain = BO->getOpcode() == Instruction::And ? OC_OpChainAnd :
                                      OC_OpChainMixed;
        break;
      case OC_OpChainMixed:
        NewChain = OC_OpChainMixed;
        break;
      }

      // If we reach a Mixed state, we do not want to keep walking up as we can not
      // reliably find a value that will simplify the chain. With this check, we
      // will return null on the first sight of mixed chain and the caller will
      // either backtrack to find partial LIV in other operand or return null.
      if (NewChain != OC_OpChainMixed) {
        // Update the current operator chain type before we search up the chain.
        ParentChain = NewChain;
        // If either the left or right side is invariant, we can unswitch on this,
        // which will cause the branch to go away in one loop and the condition to
        // simplify in the other one.
        if (Value *LHS = FindLIVLoopCondition(BO->getOperand(0), L, Changed,
                                              ParentChain, Cache)) {
          Cache[Cond] = LHS;
          return LHS;
        }
        // We did not manage to find a partial LIV in operand(0). Backtrack and try
        // operand(1).
        ParentChain = NewChain;
        if (Value *RHS = FindLIVLoopCondition(BO->getOperand(1), L, Changed,
                                              ParentChain, Cache)) {
          Cache[Cond] = RHS;
          return RHS;
        }
      }
    }

  Cache[Cond] = nullptr;
  return nullptr;
}

/// Cond is a condition that occurs in L. If it is invariant in the loop, or has
/// an invariant piece, return the invariant along with the operator chain type.
/// Otherwise, return null.
static std::pair<Value *, OperatorChain> FindLIVLoopCondition(Value *Cond,
                                                              Loop *L,
                                                              bool &Changed) {
  DenseMap<Value *, Value *> Cache;
  OperatorChain OpChain = OC_OpChainNone;
  Value *FCond = FindLIVLoopCondition(Cond, L, Changed, OpChain, Cache);

  // In case we do find a LIV, it can not be obtained by walking up a mixed
  // operator chain.
  assert((!FCond || OpChain != OC_OpChainMixed) &&
        "Do not expect a partial LIV with mixed operator chain");
  return {FCond, OpChain};
}

bool LoopUnswitch::runOnLoop(Loop *L, LPPassManager &LPM_Ref) {
  if (skipLoop(L))
    return false;

  AC = &getAnalysis<AssumptionCacheTracker>().getAssumptionCache(
      *L->getHeader()->getParent());
  LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  LPM = &LPM_Ref;
  DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  if (EnableMSSALoopDependency) {
    MSSA = &getAnalysis<MemorySSAWrapperPass>().getMSSA();
    MSSAU = make_unique<MemorySSAUpdater>(MSSA);
    assert(DT && "Cannot update MemorySSA without a valid DomTree.");
  }
  currentLoop = L;
  Function *F = currentLoop->getHeader()->getParent();

  SanitizeMemory = F->hasFnAttribute(Attribute::SanitizeMemory);
  if (SanitizeMemory)
    SafetyInfo.computeLoopSafetyInfo(L);

  if (MSSA && VerifyMemorySSA)
    MSSA->verifyMemorySSA();

  bool Changed = false;
  do {
    assert(currentLoop->isLCSSAForm(*DT));
    if (MSSA && VerifyMemorySSA)
      MSSA->verifyMemorySSA();
    redoLoop = false;
    Changed |= processCurrentLoop();
  } while(redoLoop);

  if (MSSA && VerifyMemorySSA)
    MSSA->verifyMemorySSA();

  return Changed;
}

// Return true if the BasicBlock BB is unreachable from the loop header.
// Return false, otherwise.
bool LoopUnswitch::isUnreachableDueToPreviousUnswitching(BasicBlock *BB) {
  auto *Node = DT->getNode(BB)->getIDom();
  BasicBlock *DomBB = Node->getBlock();
  while (currentLoop->contains(DomBB)) {
    BranchInst *BInst = dyn_cast<BranchInst>(DomBB->getTerminator());

    Node = DT->getNode(DomBB)->getIDom();
    DomBB = Node->getBlock();

    if (!BInst || !BInst->isConditional())
      continue;

    Value *Cond = BInst->getCondition();
    if (!isa<ConstantInt>(Cond))
      continue;

    BasicBlock *UnreachableSucc =
        Cond == ConstantInt::getTrue(Cond->getContext())
            ? BInst->getSuccessor(1)
            : BInst->getSuccessor(0);

    if (DT->dominates(UnreachableSucc, BB))
      return true;
  }
  return false;
}

/// FIXME: Remove this workaround when freeze related patches are done.
/// LoopUnswitch and Equality propagation in GVN have discrepancy about
/// whether branch on undef/poison has undefine behavior. Here it is to
/// rule out some common cases that we found such discrepancy already
/// causing problems. Detail could be found in PR31652. Note if the
/// func returns true, it is unsafe. But if it is false, it doesn't mean
/// it is necessarily safe.
static bool EqualityPropUnSafe(Value &LoopCond) {
  ICmpInst *CI = dyn_cast<ICmpInst>(&LoopCond);
  if (!CI || !CI->isEquality())
    return false;

  Value *LHS = CI->getOperand(0);
  Value *RHS = CI->getOperand(1);
  if (isa<UndefValue>(LHS) || isa<UndefValue>(RHS))
    return true;

  auto hasUndefInPHI = [](PHINode &PN) {
    for (Value *Opd : PN.incoming_values()) {
      if (isa<UndefValue>(Opd))
        return true;
    }
    return false;
  };
  PHINode *LPHI = dyn_cast<PHINode>(LHS);
  PHINode *RPHI = dyn_cast<PHINode>(RHS);
  if ((LPHI && hasUndefInPHI(*LPHI)) || (RPHI && hasUndefInPHI(*RPHI)))
    return true;

  auto hasUndefInSelect = [](SelectInst &SI) {
    if (isa<UndefValue>(SI.getTrueValue()) ||
        isa<UndefValue>(SI.getFalseValue()))
      return true;
    return false;
  };
  SelectInst *LSI = dyn_cast<SelectInst>(LHS);
  SelectInst *RSI = dyn_cast<SelectInst>(RHS);
  if ((LSI && hasUndefInSelect(*LSI)) || (RSI && hasUndefInSelect(*RSI)))
    return true;
  return false;
}

/// Do actual work and unswitch loop if possible and profitable.
bool LoopUnswitch::processCurrentLoop() {
  bool Changed = false;

  initLoopData();

  // If LoopSimplify was unable to form a preheader, don't do any unswitching.
  if (!loopPreheader)
    return false;

  // Loops with indirectbr cannot be cloned.
  if (!currentLoop->isSafeToClone())
    return false;

  // Without dedicated exits, splitting the exit edge may fail.
  if (!currentLoop->hasDedicatedExits())
    return false;

  LLVMContext &Context = loopHeader->getContext();

  // Analyze loop cost, and stop unswitching if loop content can not be duplicated.
  if (!BranchesInfo.countLoop(
          currentLoop, getAnalysis<TargetTransformInfoWrapperPass>().getTTI(
                           *currentLoop->getHeader()->getParent()),
          AC))
    return false;

  // Try trivial unswitch first before loop over other basic blocks in the loop.
  if (TryTrivialLoopUnswitch(Changed)) {
    return true;
  }

  // Do not do non-trivial unswitch while optimizing for size.
  // FIXME: Use Function::optForSize().
  if (OptimizeForSize ||
      loopHeader->getParent()->hasFnAttribute(Attribute::OptimizeForSize))
    return false;

  // Run through the instructions in the loop, keeping track of three things:
  //
  //  - That we do not unswitch loops containing convergent operations, as we
  //    might be making them control dependent on the unswitch value when they
  //    were not before.
  //    FIXME: This could be refined to only bail if the convergent operation is
  //    not already control-dependent on the unswitch value.
  //
  //  - That basic blocks in the loop contain invokes whose predecessor edges we
  //    cannot split.
  //
  //  - The set of guard intrinsics encountered (these are non terminator
  //    instructions that are also profitable to be unswitched).

  SmallVector<IntrinsicInst *, 4> Guards;

  for (const auto BB : currentLoop->blocks()) {
    for (auto &I : *BB) {
      auto CS = CallSite(&I);
      if (!CS) continue;
      if (CS.hasFnAttr(Attribute::Convergent))
        return false;
      if (auto *II = dyn_cast<InvokeInst>(&I))
        if (!II->getUnwindDest()->canSplitPredecessors())
          return false;
      if (auto *II = dyn_cast<IntrinsicInst>(&I))
        if (II->getIntrinsicID() == Intrinsic::experimental_guard)
          Guards.push_back(II);
    }
  }

  for (IntrinsicInst *Guard : Guards) {
    Value *LoopCond =
        FindLIVLoopCondition(Guard->getOperand(0), currentLoop, Changed).first;
    if (LoopCond &&
        UnswitchIfProfitable(LoopCond, ConstantInt::getTrue(Context))) {
      // NB! Unswitching (if successful) could have erased some of the
      // instructions in Guards leaving dangling pointers there.  This is fine
      // because we're returning now, and won't look at Guards again.
      ++NumGuards;
      return true;
    }
  }

  // Loop over all of the basic blocks in the loop.  If we find an interior
  // block that is branching on a loop-invariant condition, we can unswitch this
  // loop.
  for (Loop::block_iterator I = currentLoop->block_begin(),
         E = currentLoop->block_end(); I != E; ++I) {
    Instruction *TI = (*I)->getTerminator();

    // Unswitching on a potentially uninitialized predicate is not
    // MSan-friendly. Limit this to the cases when the original predicate is
    // guaranteed to execute, to avoid creating a use-of-uninitialized-value
    // in the code that did not have one.
    // This is a workaround for the discrepancy between LLVM IR and MSan
    // semantics. See PR28054 for more details.
    if (SanitizeMemory &&
        !SafetyInfo.isGuaranteedToExecute(*TI, DT, currentLoop))
      continue;

    if (BranchInst *BI = dyn_cast<BranchInst>(TI)) {
      // Some branches may be rendered unreachable because of previous
      // unswitching.
      // Unswitch only those branches that are reachable.
      if (isUnreachableDueToPreviousUnswitching(*I))
        continue;

      // If this isn't branching on an invariant condition, we can't unswitch
      // it.
      if (BI->isConditional()) {
        // See if this, or some part of it, is loop invariant.  If so, we can
        // unswitch on it if we desire.
        Value *LoopCond = FindLIVLoopCondition(BI->getCondition(),
                                               currentLoop, Changed).first;
        if (LoopCond && !EqualityPropUnSafe(*LoopCond) &&
            UnswitchIfProfitable(LoopCond, ConstantInt::getTrue(Context), TI)) {
          ++NumBranches;
          return true;
        }
      }
    } else if (SwitchInst *SI = dyn_cast<SwitchInst>(TI)) {
      Value *SC = SI->getCondition();
      Value *LoopCond;
      OperatorChain OpChain;
      std::tie(LoopCond, OpChain) =
        FindLIVLoopCondition(SC, currentLoop, Changed);

      unsigned NumCases = SI->getNumCases();
      if (LoopCond && NumCases) {
        // Find a value to unswitch on:
        // FIXME: this should chose the most expensive case!
        // FIXME: scan for a case with a non-critical edge?
        Constant *UnswitchVal = nullptr;
        // Find a case value such that at least one case value is unswitched
        // out.
        if (OpChain == OC_OpChainAnd) {
          // If the chain only has ANDs and the switch has a case value of 0.
          // Dropping in a 0 to the chain will unswitch out the 0-casevalue.
          auto *AllZero = cast<ConstantInt>(Constant::getNullValue(SC->getType()));
          if (BranchesInfo.isUnswitched(SI, AllZero))
            continue;
          // We are unswitching 0 out.
          UnswitchVal = AllZero;
        } else if (OpChain == OC_OpChainOr) {
          // If the chain only has ORs and the switch has a case value of ~0.
          // Dropping in a ~0 to the chain will unswitch out the ~0-casevalue.
          auto *AllOne = cast<ConstantInt>(Constant::getAllOnesValue(SC->getType()));
          if (BranchesInfo.isUnswitched(SI, AllOne))
            continue;
          // We are unswitching ~0 out.
          UnswitchVal = AllOne;
        } else {
          assert(OpChain == OC_OpChainNone &&
                 "Expect to unswitch on trivial chain");
          // Do not process same value again and again.
          // At this point we have some cases already unswitched and
          // some not yet unswitched. Let's find the first not yet unswitched one.
          for (auto Case : SI->cases()) {
            Constant *UnswitchValCandidate = Case.getCaseValue();
            if (!BranchesInfo.isUnswitched(SI, UnswitchValCandidate)) {
              UnswitchVal = UnswitchValCandidate;
              break;
            }
          }
        }

        if (!UnswitchVal)
          continue;

        if (UnswitchIfProfitable(LoopCond, UnswitchVal)) {
          ++NumSwitches;
          // In case of a full LIV, UnswitchVal is the value we unswitched out.
          // In case of a partial LIV, we only unswitch when its an AND-chain
          // or OR-chain. In both cases switch input value simplifies to
          // UnswitchVal.
          BranchesInfo.setUnswitched(SI, UnswitchVal);
          return true;
        }
      }
    }

    // Scan the instructions to check for unswitchable values.
    for (BasicBlock::iterator BBI = (*I)->begin(), E = (*I)->end();
         BBI != E; ++BBI)
      if (SelectInst *SI = dyn_cast<SelectInst>(BBI)) {
        Value *LoopCond = FindLIVLoopCondition(SI->getCondition(),
                                               currentLoop, Changed).first;
        if (LoopCond && UnswitchIfProfitable(LoopCond,
                                             ConstantInt::getTrue(Context))) {
          ++NumSelects;
          return true;
        }
      }
  }
  return Changed;
}

/// Check to see if all paths from BB exit the loop with no side effects
/// (including infinite loops).
///
/// If true, we return true and set ExitBB to the block we
/// exit through.
///
static bool isTrivialLoopExitBlockHelper(Loop *L, BasicBlock *BB,
                                         BasicBlock *&ExitBB,
                                         std::set<BasicBlock*> &Visited) {
  if (!Visited.insert(BB).second) {
    // Already visited. Without more analysis, this could indicate an infinite
    // loop.
    return false;
  }
  if (!L->contains(BB)) {
    // Otherwise, this is a loop exit, this is fine so long as this is the
    // first exit.
    if (ExitBB) return false;
    ExitBB = BB;
    return true;
  }

  // Otherwise, this is an unvisited intra-loop node.  Check all successors.
  for (succ_iterator SI = succ_begin(BB), E = succ_end(BB); SI != E; ++SI) {
    // Check to see if the successor is a trivial loop exit.
    if (!isTrivialLoopExitBlockHelper(L, *SI, ExitBB, Visited))
      return false;
  }

  // Okay, everything after this looks good, check to make sure that this block
  // doesn't include any side effects.
  for (Instruction &I : *BB)
    if (I.mayHaveSideEffects())
      return false;

  return true;
}

/// Return true if the specified block unconditionally leads to an exit from
/// the specified loop, and has no side-effects in the process. If so, return
/// the block that is exited to, otherwise return null.
static BasicBlock *isTrivialLoopExitBlock(Loop *L, BasicBlock *BB) {
  std::set<BasicBlock*> Visited;
  Visited.insert(L->getHeader());  // Branches to header make infinite loops.
  BasicBlock *ExitBB = nullptr;
  if (isTrivialLoopExitBlockHelper(L, BB, ExitBB, Visited))
    return ExitBB;
  return nullptr;
}

/// We have found that we can unswitch currentLoop when LoopCond == Val to
/// simplify the loop.  If we decide that this is profitable,
/// unswitch the loop, reprocess the pieces, then return true.
bool LoopUnswitch::UnswitchIfProfitable(Value *LoopCond, Constant *Val,
                                        Instruction *TI) {
  // Check to see if it would be profitable to unswitch current loop.
  if (!BranchesInfo.CostAllowsUnswitching()) {
    LLVM_DEBUG(dbgs() << "NOT unswitching loop %"
                      << currentLoop->getHeader()->getName()
                      << " at non-trivial condition '" << *Val
                      << "' == " << *LoopCond << "\n"
                      << ". Cost too high.\n");
    return false;
  }
  if (hasBranchDivergence &&
      getAnalysis<LegacyDivergenceAnalysis>().isDivergent(LoopCond)) {
    LLVM_DEBUG(dbgs() << "NOT unswitching loop %"
                      << currentLoop->getHeader()->getName()
                      << " at non-trivial condition '" << *Val
                      << "' == " << *LoopCond << "\n"
                      << ". Condition is divergent.\n");
    return false;
  }

  UnswitchNontrivialCondition(LoopCond, Val, currentLoop, TI);
  return true;
}

/// Recursively clone the specified loop and all of its children,
/// mapping the blocks with the specified map.
static Loop *CloneLoop(Loop *L, Loop *PL, ValueToValueMapTy &VM,
                       LoopInfo *LI, LPPassManager *LPM) {
  Loop &New = *LI->AllocateLoop();
  if (PL)
    PL->addChildLoop(&New);
  else
    LI->addTopLevelLoop(&New);
  LPM->addLoop(New);

  // Add all of the blocks in L to the new loop.
  for (Loop::block_iterator I = L->block_begin(), E = L->block_end();
       I != E; ++I)
    if (LI->getLoopFor(*I) == L)
      New.addBasicBlockToLoop(cast<BasicBlock>(VM[*I]), *LI);

  // Add all of the subloops to the new loop.
  for (Loop *I : *L)
    CloneLoop(I, &New, VM, LI, LPM);

  return &New;
}

/// Emit a conditional branch on two values if LIC == Val, branch to TrueDst,
/// otherwise branch to FalseDest. Insert the code immediately before OldBranch
/// and remove (but not erase!) it from the function.
void LoopUnswitch::EmitPreheaderBranchOnCondition(Value *LIC, Constant *Val,
                                                  BasicBlock *TrueDest,
                                                  BasicBlock *FalseDest,
                                                  BranchInst *OldBranch,
                                                  Instruction *TI) {
  assert(OldBranch->isUnconditional() && "Preheader is not split correctly");
  assert(TrueDest != FalseDest && "Branch targets should be different");
  // Insert a conditional branch on LIC to the two preheaders.  The original
  // code is the true version and the new code is the false version.
  Value *BranchVal = LIC;
  bool Swapped = false;
  if (!isa<ConstantInt>(Val) ||
      Val->getType() != Type::getInt1Ty(LIC->getContext()))
    BranchVal = new ICmpInst(OldBranch, ICmpInst::ICMP_EQ, LIC, Val);
  else if (Val != ConstantInt::getTrue(Val->getContext())) {
    // We want to enter the new loop when the condition is true.
    std::swap(TrueDest, FalseDest);
    Swapped = true;
  }

  // Old branch will be removed, so save its parent and successor to update the
  // DomTree.
  auto *OldBranchSucc = OldBranch->getSuccessor(0);
  auto *OldBranchParent = OldBranch->getParent();

  // Insert the new branch.
  BranchInst *BI =
      IRBuilder<>(OldBranch).CreateCondBr(BranchVal, TrueDest, FalseDest, TI);
  if (Swapped)
    BI->swapProfMetadata();

  // Remove the old branch so there is only one branch at the end. This is
  // needed to perform DomTree's internal DFS walk on the function's CFG.
  OldBranch->removeFromParent();

  // Inform the DT about the new branch.
  if (DT) {
    // First, add both successors.
    SmallVector<DominatorTree::UpdateType, 3> Updates;
    if (TrueDest != OldBranchSucc)
      Updates.push_back({DominatorTree::Insert, OldBranchParent, TrueDest});
    if (FalseDest != OldBranchSucc)
      Updates.push_back({DominatorTree::Insert, OldBranchParent, FalseDest});
    // If both of the new successors are different from the old one, inform the
    // DT that the edge was deleted.
    if (OldBranchSucc != TrueDest && OldBranchSucc != FalseDest) {
      Updates.push_back({DominatorTree::Delete, OldBranchParent, OldBranchSucc});
    }
    DT->applyUpdates(Updates);

    if (MSSAU)
      MSSAU->applyUpdates(Updates, *DT);
  }

  // If either edge is critical, split it. This helps preserve LoopSimplify
  // form for enclosing loops.
  auto Options =
      CriticalEdgeSplittingOptions(DT, LI, MSSAU.get()).setPreserveLCSSA();
  SplitCriticalEdge(BI, 0, Options);
  SplitCriticalEdge(BI, 1, Options);
}

/// Given a loop that has a trivial unswitchable condition in it (a cond branch
/// from its header block to its latch block, where the path through the loop
/// that doesn't execute its body has no side-effects), unswitch it. This
/// doesn't involve any code duplication, just moving the conditional branch
/// outside of the loop and updating loop info.
void LoopUnswitch::UnswitchTrivialCondition(Loop *L, Value *Cond, Constant *Val,
                                            BasicBlock *ExitBlock,
                                            Instruction *TI) {
  LLVM_DEBUG(dbgs() << "loop-unswitch: Trivial-Unswitch loop %"
                    << loopHeader->getName() << " [" << L->getBlocks().size()
                    << " blocks] in Function "
                    << L->getHeader()->getParent()->getName()
                    << " on cond: " << *Val << " == " << *Cond << "\n");
  // We are going to make essential changes to CFG. This may invalidate cached
  // information for L or one of its parent loops in SCEV.
  if (auto *SEWP = getAnalysisIfAvailable<ScalarEvolutionWrapperPass>())
    SEWP->getSE().forgetTopmostLoop(L);

  // First step, split the preheader, so that we know that there is a safe place
  // to insert the conditional branch.  We will change loopPreheader to have a
  // conditional branch on Cond.
  BasicBlock *NewPH = SplitEdge(loopPreheader, loopHeader, DT, LI, MSSAU.get());

  // Now that we have a place to insert the conditional branch, create a place
  // to branch to: this is the exit block out of the loop that we should
  // short-circuit to.

  // Split this block now, so that the loop maintains its exit block, and so
  // that the jump from the preheader can execute the contents of the exit block
  // without actually branching to it (the exit block should be dominated by the
  // loop header, not the preheader).
  assert(!L->contains(ExitBlock) && "Exit block is in the loop?");
  BasicBlock *NewExit =
      SplitBlock(ExitBlock, &ExitBlock->front(), DT, LI, MSSAU.get());

  // Okay, now we have a position to branch from and a position to branch to,
  // insert the new conditional branch.
  auto *OldBranch = dyn_cast<BranchInst>(loopPreheader->getTerminator());
  assert(OldBranch && "Failed to split the preheader");
  EmitPreheaderBranchOnCondition(Cond, Val, NewExit, NewPH, OldBranch, TI);
  LPM->deleteSimpleAnalysisValue(OldBranch, L);

  // EmitPreheaderBranchOnCondition removed the OldBranch from the function.
  // Delete it, as it is no longer needed.
  delete OldBranch;

  // We need to reprocess this loop, it could be unswitched again.
  redoLoop = true;

  // Now that we know that the loop is never entered when this condition is a
  // particular value, rewrite the loop with this info.  We know that this will
  // at least eliminate the old branch.
  RewriteLoopBodyWithConditionConstant(L, Cond, Val, false);

  ++NumTrivial;
}

/// Check if the first non-constant condition starting from the loop header is
/// a trivial unswitch condition: that is, a condition controls whether or not
/// the loop does anything at all. If it is a trivial condition, unswitching
/// produces no code duplications (equivalently, it produces a simpler loop and
/// a new empty loop, which gets deleted). Therefore always unswitch trivial
/// condition.
bool LoopUnswitch::TryTrivialLoopUnswitch(bool &Changed) {
  BasicBlock *CurrentBB = currentLoop->getHeader();
  Instruction *CurrentTerm = CurrentBB->getTerminator();
  LLVMContext &Context = CurrentBB->getContext();

  // If loop header has only one reachable successor (currently via an
  // unconditional branch or constant foldable conditional branch, but
  // should also consider adding constant foldable switch instruction in
  // future), we should keep looking for trivial condition candidates in
  // the successor as well. An alternative is to constant fold conditions
  // and merge successors into loop header (then we only need to check header's
  // terminator). The reason for not doing this in LoopUnswitch pass is that
  // it could potentially break LoopPassManager's invariants. Folding dead
  // branches could either eliminate the current loop or make other loops
  // unreachable. LCSSA form might also not be preserved after deleting
  // branches. The following code keeps traversing loop header's successors
  // until it finds the trivial condition candidate (condition that is not a
  // constant). Since unswitching generates branches with constant conditions,
  // this scenario could be very common in practice.
  SmallPtrSet<BasicBlock*, 8> Visited;

  while (true) {
    // If we exit loop or reach a previous visited block, then
    // we can not reach any trivial condition candidates (unfoldable
    // branch instructions or switch instructions) and no unswitch
    // can happen. Exit and return false.
    if (!currentLoop->contains(CurrentBB) || !Visited.insert(CurrentBB).second)
      return false;

    // Check if this loop will execute any side-effecting instructions (e.g.
    // stores, calls, volatile loads) in the part of the loop that the code
    // *would* execute. Check the header first.
    for (Instruction &I : *CurrentBB)
      if (I.mayHaveSideEffects())
        return false;

    if (BranchInst *BI = dyn_cast<BranchInst>(CurrentTerm)) {
      if (BI->isUnconditional()) {
        CurrentBB = BI->getSuccessor(0);
      } else if (BI->getCondition() == ConstantInt::getTrue(Context)) {
        CurrentBB = BI->getSuccessor(0);
      } else if (BI->getCondition() == ConstantInt::getFalse(Context)) {
        CurrentBB = BI->getSuccessor(1);
      } else {
        // Found a trivial condition candidate: non-foldable conditional branch.
        break;
      }
    } else if (SwitchInst *SI = dyn_cast<SwitchInst>(CurrentTerm)) {
      // At this point, any constant-foldable instructions should have probably
      // been folded.
      ConstantInt *Cond = dyn_cast<ConstantInt>(SI->getCondition());
      if (!Cond)
        break;
      // Find the target block we are definitely going to.
      CurrentBB = SI->findCaseValue(Cond)->getCaseSuccessor();
    } else {
      // We do not understand these terminator instructions.
      break;
    }

    CurrentTerm = CurrentBB->getTerminator();
  }

  // CondVal is the condition that controls the trivial condition.
  // LoopExitBB is the BasicBlock that loop exits when meets trivial condition.
  Constant *CondVal = nullptr;
  BasicBlock *LoopExitBB = nullptr;

  if (BranchInst *BI = dyn_cast<BranchInst>(CurrentTerm)) {
    // If this isn't branching on an invariant condition, we can't unswitch it.
    if (!BI->isConditional())
      return false;

    Value *LoopCond = FindLIVLoopCondition(BI->getCondition(),
                                           currentLoop, Changed).first;

    // Unswitch only if the trivial condition itself is an LIV (not
    // partial LIV which could occur in and/or)
    if (!LoopCond || LoopCond != BI->getCondition())
      return false;

    // Check to see if a successor of the branch is guaranteed to
    // exit through a unique exit block without having any
    // side-effects.  If so, determine the value of Cond that causes
    // it to do this.
    if ((LoopExitBB = isTrivialLoopExitBlock(currentLoop,
                                             BI->getSuccessor(0)))) {
      CondVal = ConstantInt::getTrue(Context);
    } else if ((LoopExitBB = isTrivialLoopExitBlock(currentLoop,
                                                    BI->getSuccessor(1)))) {
      CondVal = ConstantInt::getFalse(Context);
    }

    // If we didn't find a single unique LoopExit block, or if the loop exit
    // block contains phi nodes, this isn't trivial.
    if (!LoopExitBB || isa<PHINode>(LoopExitBB->begin()))
      return false;   // Can't handle this.

    if (EqualityPropUnSafe(*LoopCond))
      return false;

    UnswitchTrivialCondition(currentLoop, LoopCond, CondVal, LoopExitBB,
                             CurrentTerm);
    ++NumBranches;
    return true;
  } else if (SwitchInst *SI = dyn_cast<SwitchInst>(CurrentTerm)) {
    // If this isn't switching on an invariant condition, we can't unswitch it.
    Value *LoopCond = FindLIVLoopCondition(SI->getCondition(),
                                           currentLoop, Changed).first;

    // Unswitch only if the trivial condition itself is an LIV (not
    // partial LIV which could occur in and/or)
    if (!LoopCond || LoopCond != SI->getCondition())
      return false;

    // Check to see if a successor of the switch is guaranteed to go to the
    // latch block or exit through a one exit block without having any
    // side-effects.  If so, determine the value of Cond that causes it to do
    // this.
    // Note that we can't trivially unswitch on the default case or
    // on already unswitched cases.
    for (auto Case : SI->cases()) {
      BasicBlock *LoopExitCandidate;
      if ((LoopExitCandidate =
               isTrivialLoopExitBlock(currentLoop, Case.getCaseSuccessor()))) {
        // Okay, we found a trivial case, remember the value that is trivial.
        ConstantInt *CaseVal = Case.getCaseValue();

        // Check that it was not unswitched before, since already unswitched
        // trivial vals are looks trivial too.
        if (BranchesInfo.isUnswitched(SI, CaseVal))
          continue;
        LoopExitBB = LoopExitCandidate;
        CondVal = CaseVal;
        break;
      }
    }

    // If we didn't find a single unique LoopExit block, or if the loop exit
    // block contains phi nodes, this isn't trivial.
    if (!LoopExitBB || isa<PHINode>(LoopExitBB->begin()))
      return false;   // Can't handle this.

    UnswitchTrivialCondition(currentLoop, LoopCond, CondVal, LoopExitBB,
                             nullptr);

    // We are only unswitching full LIV.
    BranchesInfo.setUnswitched(SI, CondVal);
    ++NumSwitches;
    return true;
  }
  return false;
}

/// Split all of the edges from inside the loop to their exit blocks.
/// Update the appropriate Phi nodes as we do so.
void LoopUnswitch::SplitExitEdges(Loop *L,
                               const SmallVectorImpl<BasicBlock *> &ExitBlocks){

  for (unsigned i = 0, e = ExitBlocks.size(); i != e; ++i) {
    BasicBlock *ExitBlock = ExitBlocks[i];
    SmallVector<BasicBlock *, 4> Preds(pred_begin(ExitBlock),
                                       pred_end(ExitBlock));

    // Although SplitBlockPredecessors doesn't preserve loop-simplify in
    // general, if we call it on all predecessors of all exits then it does.
    SplitBlockPredecessors(ExitBlock, Preds, ".us-lcssa", DT, LI, MSSAU.get(),
                           /*PreserveLCSSA*/ true);
  }
}

/// We determined that the loop is profitable to unswitch when LIC equal Val.
/// Split it into loop versions and test the condition outside of either loop.
/// Return the loops created as Out1/Out2.
void LoopUnswitch::UnswitchNontrivialCondition(Value *LIC, Constant *Val,
                                               Loop *L, Instruction *TI) {
  Function *F = loopHeader->getParent();
  LLVM_DEBUG(dbgs() << "loop-unswitch: Unswitching loop %"
                    << loopHeader->getName() << " [" << L->getBlocks().size()
                    << " blocks] in Function " << F->getName() << " when '"
                    << *Val << "' == " << *LIC << "\n");

  // We are going to make essential changes to CFG. This may invalidate cached
  // information for L or one of its parent loops in SCEV.
  if (auto *SEWP = getAnalysisIfAvailable<ScalarEvolutionWrapperPass>())
    SEWP->getSE().forgetTopmostLoop(L);

  LoopBlocks.clear();
  NewBlocks.clear();

  // First step, split the preheader and exit blocks, and add these blocks to
  // the LoopBlocks list.
  BasicBlock *NewPreheader =
      SplitEdge(loopPreheader, loopHeader, DT, LI, MSSAU.get());
  LoopBlocks.push_back(NewPreheader);

  // We want the loop to come after the preheader, but before the exit blocks.
  LoopBlocks.insert(LoopBlocks.end(), L->block_begin(), L->block_end());

  SmallVector<BasicBlock*, 8> ExitBlocks;
  L->getUniqueExitBlocks(ExitBlocks);

  // Split all of the edges from inside the loop to their exit blocks.  Update
  // the appropriate Phi nodes as we do so.
  SplitExitEdges(L, ExitBlocks);

  // The exit blocks may have been changed due to edge splitting, recompute.
  ExitBlocks.clear();
  L->getUniqueExitBlocks(ExitBlocks);

  // Add exit blocks to the loop blocks.
  LoopBlocks.insert(LoopBlocks.end(), ExitBlocks.begin(), ExitBlocks.end());

  // Next step, clone all of the basic blocks that make up the loop (including
  // the loop preheader and exit blocks), keeping track of the mapping between
  // the instructions and blocks.
  NewBlocks.reserve(LoopBlocks.size());
  ValueToValueMapTy VMap;
  for (unsigned i = 0, e = LoopBlocks.size(); i != e; ++i) {
    BasicBlock *NewBB = CloneBasicBlock(LoopBlocks[i], VMap, ".us", F);

    NewBlocks.push_back(NewBB);
    VMap[LoopBlocks[i]] = NewBB;  // Keep the BB mapping.
    LPM->cloneBasicBlockSimpleAnalysis(LoopBlocks[i], NewBB, L);
  }

  // Splice the newly inserted blocks into the function right before the
  // original preheader.
  F->getBasicBlockList().splice(NewPreheader->getIterator(),
                                F->getBasicBlockList(),
                                NewBlocks[0]->getIterator(), F->end());

  // Now we create the new Loop object for the versioned loop.
  Loop *NewLoop = CloneLoop(L, L->getParentLoop(), VMap, LI, LPM);

  // Recalculate unswitching quota, inherit simplified switches info for NewBB,
  // Probably clone more loop-unswitch related loop properties.
  BranchesInfo.cloneData(NewLoop, L, VMap);

  Loop *ParentLoop = L->getParentLoop();
  if (ParentLoop) {
    // Make sure to add the cloned preheader and exit blocks to the parent loop
    // as well.
    ParentLoop->addBasicBlockToLoop(NewBlocks[0], *LI);
  }

  for (unsigned i = 0, e = ExitBlocks.size(); i != e; ++i) {
    BasicBlock *NewExit = cast<BasicBlock>(VMap[ExitBlocks[i]]);
    // The new exit block should be in the same loop as the old one.
    if (Loop *ExitBBLoop = LI->getLoopFor(ExitBlocks[i]))
      ExitBBLoop->addBasicBlockToLoop(NewExit, *LI);

    assert(NewExit->getTerminator()->getNumSuccessors() == 1 &&
           "Exit block should have been split to have one successor!");
    BasicBlock *ExitSucc = NewExit->getTerminator()->getSuccessor(0);

    // If the successor of the exit block had PHI nodes, add an entry for
    // NewExit.
    for (PHINode &PN : ExitSucc->phis()) {
      Value *V = PN.getIncomingValueForBlock(ExitBlocks[i]);
      ValueToValueMapTy::iterator It = VMap.find(V);
      if (It != VMap.end()) V = It->second;
      PN.addIncoming(V, NewExit);
    }

    if (LandingPadInst *LPad = NewExit->getLandingPadInst()) {
      PHINode *PN = PHINode::Create(LPad->getType(), 0, "",
                                    &*ExitSucc->getFirstInsertionPt());

      for (pred_iterator I = pred_begin(ExitSucc), E = pred_end(ExitSucc);
           I != E; ++I) {
        BasicBlock *BB = *I;
        LandingPadInst *LPI = BB->getLandingPadInst();
        LPI->replaceAllUsesWith(PN);
        PN->addIncoming(LPI, BB);
      }
    }
  }

  // Rewrite the code to refer to itself.
  for (unsigned i = 0, e = NewBlocks.size(); i != e; ++i) {
    for (Instruction &I : *NewBlocks[i]) {
      RemapInstruction(&I, VMap,
                       RF_NoModuleLevelChanges | RF_IgnoreMissingLocals);
      if (auto *II = dyn_cast<IntrinsicInst>(&I))
        if (II->getIntrinsicID() == Intrinsic::assume)
          AC->registerAssumption(II);
    }
  }

  // Rewrite the original preheader to select between versions of the loop.
  BranchInst *OldBR = cast<BranchInst>(loopPreheader->getTerminator());
  assert(OldBR->isUnconditional() && OldBR->getSuccessor(0) == LoopBlocks[0] &&
         "Preheader splitting did not work correctly!");

  if (MSSAU) {
    // Update MemorySSA after cloning, and before splitting to unreachables,
    // since that invalidates the 1:1 mapping of clones in VMap.
    LoopBlocksRPO LBRPO(L);
    LBRPO.perform(LI);
    MSSAU->updateForClonedLoop(LBRPO, ExitBlocks, VMap);
  }

  // Emit the new branch that selects between the two versions of this loop.
  EmitPreheaderBranchOnCondition(LIC, Val, NewBlocks[0], LoopBlocks[0], OldBR,
                                 TI);
  LPM->deleteSimpleAnalysisValue(OldBR, L);
  if (MSSAU) {
    // Update MemoryPhis in Exit blocks.
    MSSAU->updateExitBlocksForClonedLoop(ExitBlocks, VMap, *DT);
    if (VerifyMemorySSA)
      MSSA->verifyMemorySSA();
  }

  // The OldBr was replaced by a new one and removed (but not erased) by
  // EmitPreheaderBranchOnCondition. It is no longer needed, so delete it.
  delete OldBR;

  LoopProcessWorklist.push_back(NewLoop);
  redoLoop = true;

  // Keep a WeakTrackingVH holding onto LIC.  If the first call to
  // RewriteLoopBody
  // deletes the instruction (for example by simplifying a PHI that feeds into
  // the condition that we're unswitching on), we don't rewrite the second
  // iteration.
  WeakTrackingVH LICHandle(LIC);

  // Now we rewrite the original code to know that the condition is true and the
  // new code to know that the condition is false.
  RewriteLoopBodyWithConditionConstant(L, LIC, Val, false);

  // It's possible that simplifying one loop could cause the other to be
  // changed to another value or a constant.  If its a constant, don't simplify
  // it.
  if (!LoopProcessWorklist.empty() && LoopProcessWorklist.back() == NewLoop &&
      LICHandle && !isa<Constant>(LICHandle))
    RewriteLoopBodyWithConditionConstant(NewLoop, LICHandle, Val, true);

  if (MSSA && VerifyMemorySSA)
    MSSA->verifyMemorySSA();
}

/// Remove all instances of I from the worklist vector specified.
static void RemoveFromWorklist(Instruction *I,
                               std::vector<Instruction*> &Worklist) {

  Worklist.erase(std::remove(Worklist.begin(), Worklist.end(), I),
                 Worklist.end());
}

/// When we find that I really equals V, remove I from the
/// program, replacing all uses with V and update the worklist.
static void ReplaceUsesOfWith(Instruction *I, Value *V,
                              std::vector<Instruction*> &Worklist,
                              Loop *L, LPPassManager *LPM) {
  LLVM_DEBUG(dbgs() << "Replace with '" << *V << "': " << *I << "\n");

  // Add uses to the worklist, which may be dead now.
  for (unsigned i = 0, e = I->getNumOperands(); i != e; ++i)
    if (Instruction *Use = dyn_cast<Instruction>(I->getOperand(i)))
      Worklist.push_back(Use);

  // Add users to the worklist which may be simplified now.
  for (User *U : I->users())
    Worklist.push_back(cast<Instruction>(U));
  LPM->deleteSimpleAnalysisValue(I, L);
  RemoveFromWorklist(I, Worklist);
  I->replaceAllUsesWith(V);
  if (!I->mayHaveSideEffects())
    I->eraseFromParent();
  ++NumSimplify;
}

/// We know either that the value LIC has the value specified by Val in the
/// specified loop, or we know it does NOT have that value.
/// Rewrite any uses of LIC or of properties correlated to it.
void LoopUnswitch::RewriteLoopBodyWithConditionConstant(Loop *L, Value *LIC,
                                                        Constant *Val,
                                                        bool IsEqual) {
  assert(!isa<Constant>(LIC) && "Why are we unswitching on a constant?");

  // FIXME: Support correlated properties, like:
  //  for (...)
  //    if (li1 < li2)
  //      ...
  //    if (li1 > li2)
  //      ...

  // FOLD boolean conditions (X|LIC), (X&LIC).  Fold conditional branches,
  // selects, switches.
  std::vector<Instruction*> Worklist;
  LLVMContext &Context = Val->getContext();

  // If we know that LIC == Val, or that LIC == NotVal, just replace uses of LIC
  // in the loop with the appropriate one directly.
  if (IsEqual || (isa<ConstantInt>(Val) &&
      Val->getType()->isIntegerTy(1))) {
    Value *Replacement;
    if (IsEqual)
      Replacement = Val;
    else
      Replacement = ConstantInt::get(Type::getInt1Ty(Val->getContext()),
                                     !cast<ConstantInt>(Val)->getZExtValue());

    for (User *U : LIC->users()) {
      Instruction *UI = dyn_cast<Instruction>(U);
      if (!UI || !L->contains(UI))
        continue;
      Worklist.push_back(UI);
    }

    for (Instruction *UI : Worklist)
      UI->replaceUsesOfWith(LIC, Replacement);

    SimplifyCode(Worklist, L);
    return;
  }

  // Otherwise, we don't know the precise value of LIC, but we do know that it
  // is certainly NOT "Val".  As such, simplify any uses in the loop that we
  // can.  This case occurs when we unswitch switch statements.
  for (User *U : LIC->users()) {
    Instruction *UI = dyn_cast<Instruction>(U);
    if (!UI || !L->contains(UI))
      continue;

    // At this point, we know LIC is definitely not Val. Try to use some simple
    // logic to simplify the user w.r.t. to the context.
    if (Value *Replacement = SimplifyInstructionWithNotEqual(UI, LIC, Val)) {
      if (LI->replacementPreservesLCSSAForm(UI, Replacement)) {
        // This in-loop instruction has been simplified w.r.t. its context,
        // i.e. LIC != Val, make sure we propagate its replacement value to
        // all its users.
        //
        // We can not yet delete UI, the LIC user, yet, because that would invalidate
        // the LIC->users() iterator !. However, we can make this instruction
        // dead by replacing all its users and push it onto the worklist so that
        // it can be properly deleted and its operands simplified.
        UI->replaceAllUsesWith(Replacement);
      }
    }

    // This is a LIC user, push it into the worklist so that SimplifyCode can
    // attempt to simplify it.
    Worklist.push_back(UI);

    // If we know that LIC is not Val, use this info to simplify code.
    SwitchInst *SI = dyn_cast<SwitchInst>(UI);
    if (!SI || !isa<ConstantInt>(Val)) continue;

    // NOTE: if a case value for the switch is unswitched out, we record it
    // after the unswitch finishes. We can not record it here as the switch
    // is not a direct user of the partial LIV.
    SwitchInst::CaseHandle DeadCase =
        *SI->findCaseValue(cast<ConstantInt>(Val));
    // Default case is live for multiple values.
    if (DeadCase == *SI->case_default())
      continue;

    // Found a dead case value.  Don't remove PHI nodes in the
    // successor if they become single-entry, those PHI nodes may
    // be in the Users list.

    BasicBlock *Switch = SI->getParent();
    BasicBlock *SISucc = DeadCase.getCaseSuccessor();
    BasicBlock *Latch = L->getLoopLatch();

    if (!SI->findCaseDest(SISucc)) continue;  // Edge is critical.
    // If the DeadCase successor dominates the loop latch, then the
    // transformation isn't safe since it will delete the sole predecessor edge
    // to the latch.
    if (Latch && DT->dominates(SISucc, Latch))
      continue;

    // FIXME: This is a hack.  We need to keep the successor around
    // and hooked up so as to preserve the loop structure, because
    // trying to update it is complicated.  So instead we preserve the
    // loop structure and put the block on a dead code path.
    SplitEdge(Switch, SISucc, DT, LI, MSSAU.get());
    // Compute the successors instead of relying on the return value
    // of SplitEdge, since it may have split the switch successor
    // after PHI nodes.
    BasicBlock *NewSISucc = DeadCase.getCaseSuccessor();
    BasicBlock *OldSISucc = *succ_begin(NewSISucc);
    // Create an "unreachable" destination.
    BasicBlock *Abort = BasicBlock::Create(Context, "us-unreachable",
                                           Switch->getParent(),
                                           OldSISucc);
    new UnreachableInst(Context, Abort);
    // Force the new case destination to branch to the "unreachable"
    // block while maintaining a (dead) CFG edge to the old block.
    NewSISucc->getTerminator()->eraseFromParent();
    BranchInst::Create(Abort, OldSISucc,
                       ConstantInt::getTrue(Context), NewSISucc);
    // Release the PHI operands for this edge.
    for (PHINode &PN : NewSISucc->phis())
      PN.setIncomingValue(PN.getBasicBlockIndex(Switch),
                          UndefValue::get(PN.getType()));
    // Tell the domtree about the new block. We don't fully update the
    // domtree here -- instead we force it to do a full recomputation
    // after the pass is complete -- but we do need to inform it of
    // new blocks.
    DT->addNewBlock(Abort, NewSISucc);
  }

  SimplifyCode(Worklist, L);
}

/// Now that we have simplified some instructions in the loop, walk over it and
/// constant prop, dce, and fold control flow where possible. Note that this is
/// effectively a very simple loop-structure-aware optimizer. During processing
/// of this loop, L could very well be deleted, so it must not be used.
///
/// FIXME: When the loop optimizer is more mature, separate this out to a new
/// pass.
///
void LoopUnswitch::SimplifyCode(std::vector<Instruction*> &Worklist, Loop *L) {
  const DataLayout &DL = L->getHeader()->getModule()->getDataLayout();
  while (!Worklist.empty()) {
    Instruction *I = Worklist.back();
    Worklist.pop_back();

    // Simple DCE.
    if (isInstructionTriviallyDead(I)) {
      LLVM_DEBUG(dbgs() << "Remove dead instruction '" << *I << "\n");

      // Add uses to the worklist, which may be dead now.
      for (unsigned i = 0, e = I->getNumOperands(); i != e; ++i)
        if (Instruction *Use = dyn_cast<Instruction>(I->getOperand(i)))
          Worklist.push_back(Use);
      LPM->deleteSimpleAnalysisValue(I, L);
      RemoveFromWorklist(I, Worklist);
      if (MSSAU)
        MSSAU->removeMemoryAccess(I);
      I->eraseFromParent();
      ++NumSimplify;
      continue;
    }

    // See if instruction simplification can hack this up.  This is common for
    // things like "select false, X, Y" after unswitching made the condition be
    // 'false'.  TODO: update the domtree properly so we can pass it here.
    if (Value *V = SimplifyInstruction(I, DL))
      if (LI->replacementPreservesLCSSAForm(I, V)) {
        ReplaceUsesOfWith(I, V, Worklist, L, LPM);
        continue;
      }

    // Special case hacks that appear commonly in unswitched code.
    if (BranchInst *BI = dyn_cast<BranchInst>(I)) {
      if (BI->isUnconditional()) {
        // If BI's parent is the only pred of the successor, fold the two blocks
        // together.
        BasicBlock *Pred = BI->getParent();
        BasicBlock *Succ = BI->getSuccessor(0);
        BasicBlock *SinglePred = Succ->getSinglePredecessor();
        if (!SinglePred) continue;  // Nothing to do.
        assert(SinglePred == Pred && "CFG broken");

        LLVM_DEBUG(dbgs() << "Merging blocks: " << Pred->getName() << " <- "
                          << Succ->getName() << "\n");

        // Resolve any single entry PHI nodes in Succ.
        while (PHINode *PN = dyn_cast<PHINode>(Succ->begin()))
          ReplaceUsesOfWith(PN, PN->getIncomingValue(0), Worklist, L, LPM);

        // If Succ has any successors with PHI nodes, update them to have
        // entries coming from Pred instead of Succ.
        Succ->replaceAllUsesWith(Pred);

        // Move all of the successor contents from Succ to Pred.
        Pred->getInstList().splice(BI->getIterator(), Succ->getInstList(),
                                   Succ->begin(), Succ->end());
        if (MSSAU)
          MSSAU->moveAllAfterMergeBlocks(Succ, Pred, BI);
        LPM->deleteSimpleAnalysisValue(BI, L);
        RemoveFromWorklist(BI, Worklist);
        BI->eraseFromParent();

        // Remove Succ from the loop tree.
        LI->removeBlock(Succ);
        LPM->deleteSimpleAnalysisValue(Succ, L);
        Succ->eraseFromParent();
        ++NumSimplify;
        continue;
      }

      continue;
    }
  }
}

/// Simple simplifications we can do given the information that Cond is
/// definitely not equal to Val.
Value *LoopUnswitch::SimplifyInstructionWithNotEqual(Instruction *Inst,
                                                     Value *Invariant,
                                                     Constant *Val) {
  // icmp eq cond, val -> false
  ICmpInst *CI = dyn_cast<ICmpInst>(Inst);
  if (CI && CI->isEquality()) {
    Value *Op0 = CI->getOperand(0);
    Value *Op1 = CI->getOperand(1);
    if ((Op0 == Invariant && Op1 == Val) || (Op0 == Val && Op1 == Invariant)) {
      LLVMContext &Ctx = Inst->getContext();
      if (CI->getPredicate() == CmpInst::ICMP_EQ)
        return ConstantInt::getFalse(Ctx);
      else
        return ConstantInt::getTrue(Ctx);
     }
  }

  // FIXME: there may be other opportunities, e.g. comparison with floating
  // point, or Invariant - Val != 0, etc.
  return nullptr;
}
