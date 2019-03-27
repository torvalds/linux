//===- ConstantHoisting.cpp - Prepare code for expensive constants --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass identifies expensive constants to hoist and coalesces them to
// better prepare it for SelectionDAG-based code generation. This works around
// the limitations of the basic-block-at-a-time approach.
//
// First it scans all instructions for integer constants and calculates its
// cost. If the constant can be folded into the instruction (the cost is
// TCC_Free) or the cost is just a simple operation (TCC_BASIC), then we don't
// consider it expensive and leave it alone. This is the default behavior and
// the default implementation of getIntImmCost will always return TCC_Free.
//
// If the cost is more than TCC_BASIC, then the integer constant can't be folded
// into the instruction and it might be beneficial to hoist the constant.
// Similar constants are coalesced to reduce register pressure and
// materialization code.
//
// When a constant is hoisted, it is also hidden behind a bitcast to force it to
// be live-out of the basic block. Otherwise the constant would be just
// duplicated and each basic block would have its own copy in the SelectionDAG.
// The SelectionDAG recognizes such constants as opaque and doesn't perform
// certain transformations on them, which would create a new expensive constant.
//
// This optimization is only applied to integer constants in instructions and
// simple (this means not nested) constant cast expressions. For example:
// %0 = load i64* inttoptr (i64 big_constant to i64*)
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/ConstantHoisting.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <tuple>
#include <utility>

using namespace llvm;
using namespace consthoist;

#define DEBUG_TYPE "consthoist"

STATISTIC(NumConstantsHoisted, "Number of constants hoisted");
STATISTIC(NumConstantsRebased, "Number of constants rebased");

static cl::opt<bool> ConstHoistWithBlockFrequency(
    "consthoist-with-block-frequency", cl::init(true), cl::Hidden,
    cl::desc("Enable the use of the block frequency analysis to reduce the "
             "chance to execute const materialization more frequently than "
             "without hoisting."));

static cl::opt<bool> ConstHoistGEP(
    "consthoist-gep", cl::init(false), cl::Hidden,
    cl::desc("Try hoisting constant gep expressions"));

static cl::opt<unsigned>
MinNumOfDependentToRebase("consthoist-min-num-to-rebase",
    cl::desc("Do not rebase if number of dependent constants of a Base is less "
             "than this number."),
    cl::init(0), cl::Hidden);

namespace {

/// The constant hoisting pass.
class ConstantHoistingLegacyPass : public FunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid

  ConstantHoistingLegacyPass() : FunctionPass(ID) {
    initializeConstantHoistingLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &Fn) override;

  StringRef getPassName() const override { return "Constant Hoisting"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    if (ConstHoistWithBlockFrequency)
      AU.addRequired<BlockFrequencyInfoWrapperPass>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
  }

  void releaseMemory() override { Impl.releaseMemory(); }

private:
  ConstantHoistingPass Impl;
};

} // end anonymous namespace

char ConstantHoistingLegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(ConstantHoistingLegacyPass, "consthoist",
                      "Constant Hoisting", false, false)
INITIALIZE_PASS_DEPENDENCY(BlockFrequencyInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_END(ConstantHoistingLegacyPass, "consthoist",
                    "Constant Hoisting", false, false)

FunctionPass *llvm::createConstantHoistingPass() {
  return new ConstantHoistingLegacyPass();
}

/// Perform the constant hoisting optimization for the given function.
bool ConstantHoistingLegacyPass::runOnFunction(Function &Fn) {
  if (skipFunction(Fn))
    return false;

  LLVM_DEBUG(dbgs() << "********** Begin Constant Hoisting **********\n");
  LLVM_DEBUG(dbgs() << "********** Function: " << Fn.getName() << '\n');

  bool MadeChange =
      Impl.runImpl(Fn, getAnalysis<TargetTransformInfoWrapperPass>().getTTI(Fn),
                   getAnalysis<DominatorTreeWrapperPass>().getDomTree(),
                   ConstHoistWithBlockFrequency
                       ? &getAnalysis<BlockFrequencyInfoWrapperPass>().getBFI()
                       : nullptr,
                   Fn.getEntryBlock());

  if (MadeChange) {
    LLVM_DEBUG(dbgs() << "********** Function after Constant Hoisting: "
                      << Fn.getName() << '\n');
    LLVM_DEBUG(dbgs() << Fn);
  }
  LLVM_DEBUG(dbgs() << "********** End Constant Hoisting **********\n");

  return MadeChange;
}

/// Find the constant materialization insertion point.
Instruction *ConstantHoistingPass::findMatInsertPt(Instruction *Inst,
                                                   unsigned Idx) const {
  // If the operand is a cast instruction, then we have to materialize the
  // constant before the cast instruction.
  if (Idx != ~0U) {
    Value *Opnd = Inst->getOperand(Idx);
    if (auto CastInst = dyn_cast<Instruction>(Opnd))
      if (CastInst->isCast())
        return CastInst;
  }

  // The simple and common case. This also includes constant expressions.
  if (!isa<PHINode>(Inst) && !Inst->isEHPad())
    return Inst;

  // We can't insert directly before a phi node or an eh pad. Insert before
  // the terminator of the incoming or dominating block.
  assert(Entry != Inst->getParent() && "PHI or landing pad in entry block!");
  if (Idx != ~0U && isa<PHINode>(Inst))
    return cast<PHINode>(Inst)->getIncomingBlock(Idx)->getTerminator();

  // This must be an EH pad. Iterate over immediate dominators until we find a
  // non-EH pad. We need to skip over catchswitch blocks, which are both EH pads
  // and terminators.
  auto IDom = DT->getNode(Inst->getParent())->getIDom();
  while (IDom->getBlock()->isEHPad()) {
    assert(Entry != IDom->getBlock() && "eh pad in entry block");
    IDom = IDom->getIDom();
  }

  return IDom->getBlock()->getTerminator();
}

/// Given \p BBs as input, find another set of BBs which collectively
/// dominates \p BBs and have the minimal sum of frequencies. Return the BB
/// set found in \p BBs.
static void findBestInsertionSet(DominatorTree &DT, BlockFrequencyInfo &BFI,
                                 BasicBlock *Entry,
                                 SmallPtrSet<BasicBlock *, 8> &BBs) {
  assert(!BBs.count(Entry) && "Assume Entry is not in BBs");
  // Nodes on the current path to the root.
  SmallPtrSet<BasicBlock *, 8> Path;
  // Candidates includes any block 'BB' in set 'BBs' that is not strictly
  // dominated by any other blocks in set 'BBs', and all nodes in the path
  // in the dominator tree from Entry to 'BB'.
  SmallPtrSet<BasicBlock *, 16> Candidates;
  for (auto BB : BBs) {
    Path.clear();
    // Walk up the dominator tree until Entry or another BB in BBs
    // is reached. Insert the nodes on the way to the Path.
    BasicBlock *Node = BB;
    // The "Path" is a candidate path to be added into Candidates set.
    bool isCandidate = false;
    do {
      Path.insert(Node);
      if (Node == Entry || Candidates.count(Node)) {
        isCandidate = true;
        break;
      }
      assert(DT.getNode(Node)->getIDom() &&
             "Entry doens't dominate current Node");
      Node = DT.getNode(Node)->getIDom()->getBlock();
    } while (!BBs.count(Node));

    // If isCandidate is false, Node is another Block in BBs dominating
    // current 'BB'. Drop the nodes on the Path.
    if (!isCandidate)
      continue;

    // Add nodes on the Path into Candidates.
    Candidates.insert(Path.begin(), Path.end());
  }

  // Sort the nodes in Candidates in top-down order and save the nodes
  // in Orders.
  unsigned Idx = 0;
  SmallVector<BasicBlock *, 16> Orders;
  Orders.push_back(Entry);
  while (Idx != Orders.size()) {
    BasicBlock *Node = Orders[Idx++];
    for (auto ChildDomNode : DT.getNode(Node)->getChildren()) {
      if (Candidates.count(ChildDomNode->getBlock()))
        Orders.push_back(ChildDomNode->getBlock());
    }
  }

  // Visit Orders in bottom-up order.
  using InsertPtsCostPair =
      std::pair<SmallPtrSet<BasicBlock *, 16>, BlockFrequency>;

  // InsertPtsMap is a map from a BB to the best insertion points for the
  // subtree of BB (subtree not including the BB itself).
  DenseMap<BasicBlock *, InsertPtsCostPair> InsertPtsMap;
  InsertPtsMap.reserve(Orders.size() + 1);
  for (auto RIt = Orders.rbegin(); RIt != Orders.rend(); RIt++) {
    BasicBlock *Node = *RIt;
    bool NodeInBBs = BBs.count(Node);
    SmallPtrSet<BasicBlock *, 16> &InsertPts = InsertPtsMap[Node].first;
    BlockFrequency &InsertPtsFreq = InsertPtsMap[Node].second;

    // Return the optimal insert points in BBs.
    if (Node == Entry) {
      BBs.clear();
      if (InsertPtsFreq > BFI.getBlockFreq(Node) ||
          (InsertPtsFreq == BFI.getBlockFreq(Node) && InsertPts.size() > 1))
        BBs.insert(Entry);
      else
        BBs.insert(InsertPts.begin(), InsertPts.end());
      break;
    }

    BasicBlock *Parent = DT.getNode(Node)->getIDom()->getBlock();
    // Initially, ParentInsertPts is empty and ParentPtsFreq is 0. Every child
    // will update its parent's ParentInsertPts and ParentPtsFreq.
    SmallPtrSet<BasicBlock *, 16> &ParentInsertPts = InsertPtsMap[Parent].first;
    BlockFrequency &ParentPtsFreq = InsertPtsMap[Parent].second;
    // Choose to insert in Node or in subtree of Node.
    // Don't hoist to EHPad because we may not find a proper place to insert
    // in EHPad.
    // If the total frequency of InsertPts is the same as the frequency of the
    // target Node, and InsertPts contains more than one nodes, choose hoisting
    // to reduce code size.
    if (NodeInBBs ||
        (!Node->isEHPad() &&
         (InsertPtsFreq > BFI.getBlockFreq(Node) ||
          (InsertPtsFreq == BFI.getBlockFreq(Node) && InsertPts.size() > 1)))) {
      ParentInsertPts.insert(Node);
      ParentPtsFreq += BFI.getBlockFreq(Node);
    } else {
      ParentInsertPts.insert(InsertPts.begin(), InsertPts.end());
      ParentPtsFreq += InsertPtsFreq;
    }
  }
}

/// Find an insertion point that dominates all uses.
SmallPtrSet<Instruction *, 8> ConstantHoistingPass::findConstantInsertionPoint(
    const ConstantInfo &ConstInfo) const {
  assert(!ConstInfo.RebasedConstants.empty() && "Invalid constant info entry.");
  // Collect all basic blocks.
  SmallPtrSet<BasicBlock *, 8> BBs;
  SmallPtrSet<Instruction *, 8> InsertPts;
  for (auto const &RCI : ConstInfo.RebasedConstants)
    for (auto const &U : RCI.Uses)
      BBs.insert(findMatInsertPt(U.Inst, U.OpndIdx)->getParent());

  if (BBs.count(Entry)) {
    InsertPts.insert(&Entry->front());
    return InsertPts;
  }

  if (BFI) {
    findBestInsertionSet(*DT, *BFI, Entry, BBs);
    for (auto BB : BBs) {
      BasicBlock::iterator InsertPt = BB->begin();
      for (; isa<PHINode>(InsertPt) || InsertPt->isEHPad(); ++InsertPt)
        ;
      InsertPts.insert(&*InsertPt);
    }
    return InsertPts;
  }

  while (BBs.size() >= 2) {
    BasicBlock *BB, *BB1, *BB2;
    BB1 = *BBs.begin();
    BB2 = *std::next(BBs.begin());
    BB = DT->findNearestCommonDominator(BB1, BB2);
    if (BB == Entry) {
      InsertPts.insert(&Entry->front());
      return InsertPts;
    }
    BBs.erase(BB1);
    BBs.erase(BB2);
    BBs.insert(BB);
  }
  assert((BBs.size() == 1) && "Expected only one element.");
  Instruction &FirstInst = (*BBs.begin())->front();
  InsertPts.insert(findMatInsertPt(&FirstInst));
  return InsertPts;
}

/// Record constant integer ConstInt for instruction Inst at operand
/// index Idx.
///
/// The operand at index Idx is not necessarily the constant integer itself. It
/// could also be a cast instruction or a constant expression that uses the
/// constant integer.
void ConstantHoistingPass::collectConstantCandidates(
    ConstCandMapType &ConstCandMap, Instruction *Inst, unsigned Idx,
    ConstantInt *ConstInt) {
  unsigned Cost;
  // Ask the target about the cost of materializing the constant for the given
  // instruction and operand index.
  if (auto IntrInst = dyn_cast<IntrinsicInst>(Inst))
    Cost = TTI->getIntImmCost(IntrInst->getIntrinsicID(), Idx,
                              ConstInt->getValue(), ConstInt->getType());
  else
    Cost = TTI->getIntImmCost(Inst->getOpcode(), Idx, ConstInt->getValue(),
                              ConstInt->getType());

  // Ignore cheap integer constants.
  if (Cost > TargetTransformInfo::TCC_Basic) {
    ConstCandMapType::iterator Itr;
    bool Inserted;
    ConstPtrUnionType Cand = ConstInt;
    std::tie(Itr, Inserted) = ConstCandMap.insert(std::make_pair(Cand, 0));
    if (Inserted) {
      ConstIntCandVec.push_back(ConstantCandidate(ConstInt));
      Itr->second = ConstIntCandVec.size() - 1;
    }
    ConstIntCandVec[Itr->second].addUser(Inst, Idx, Cost);
    LLVM_DEBUG(if (isa<ConstantInt>(Inst->getOperand(Idx))) dbgs()
                   << "Collect constant " << *ConstInt << " from " << *Inst
                   << " with cost " << Cost << '\n';
               else dbgs() << "Collect constant " << *ConstInt
                           << " indirectly from " << *Inst << " via "
                           << *Inst->getOperand(Idx) << " with cost " << Cost
                           << '\n';);
  }
}

/// Record constant GEP expression for instruction Inst at operand index Idx.
void ConstantHoistingPass::collectConstantCandidates(
    ConstCandMapType &ConstCandMap, Instruction *Inst, unsigned Idx,
    ConstantExpr *ConstExpr) {
  // TODO: Handle vector GEPs
  if (ConstExpr->getType()->isVectorTy())
    return;

  GlobalVariable *BaseGV = dyn_cast<GlobalVariable>(ConstExpr->getOperand(0));
  if (!BaseGV)
    return;

  // Get offset from the base GV.
  PointerType *GVPtrTy = dyn_cast<PointerType>(BaseGV->getType());
  IntegerType *PtrIntTy = DL->getIntPtrType(*Ctx, GVPtrTy->getAddressSpace());
  APInt Offset(DL->getTypeSizeInBits(PtrIntTy), /*val*/0, /*isSigned*/true);
  auto *GEPO = cast<GEPOperator>(ConstExpr);
  if (!GEPO->accumulateConstantOffset(*DL, Offset))
    return;

  if (!Offset.isIntN(32))
    return;

  // A constant GEP expression that has a GlobalVariable as base pointer is
  // usually lowered to a load from constant pool. Such operation is unlikely
  // to be cheaper than compute it by <Base + Offset>, which can be lowered to
  // an ADD instruction or folded into Load/Store instruction.
  int Cost = TTI->getIntImmCost(Instruction::Add, 1, Offset, PtrIntTy);
  ConstCandVecType &ExprCandVec = ConstGEPCandMap[BaseGV];
  ConstCandMapType::iterator Itr;
  bool Inserted;
  ConstPtrUnionType Cand = ConstExpr;
  std::tie(Itr, Inserted) = ConstCandMap.insert(std::make_pair(Cand, 0));
  if (Inserted) {
    ExprCandVec.push_back(ConstantCandidate(
        ConstantInt::get(Type::getInt32Ty(*Ctx), Offset.getLimitedValue()),
        ConstExpr));
    Itr->second = ExprCandVec.size() - 1;
  }
  ExprCandVec[Itr->second].addUser(Inst, Idx, Cost);
}

/// Check the operand for instruction Inst at index Idx.
void ConstantHoistingPass::collectConstantCandidates(
    ConstCandMapType &ConstCandMap, Instruction *Inst, unsigned Idx) {
  Value *Opnd = Inst->getOperand(Idx);

  // Visit constant integers.
  if (auto ConstInt = dyn_cast<ConstantInt>(Opnd)) {
    collectConstantCandidates(ConstCandMap, Inst, Idx, ConstInt);
    return;
  }

  // Visit cast instructions that have constant integers.
  if (auto CastInst = dyn_cast<Instruction>(Opnd)) {
    // Only visit cast instructions, which have been skipped. All other
    // instructions should have already been visited.
    if (!CastInst->isCast())
      return;

    if (auto *ConstInt = dyn_cast<ConstantInt>(CastInst->getOperand(0))) {
      // Pretend the constant is directly used by the instruction and ignore
      // the cast instruction.
      collectConstantCandidates(ConstCandMap, Inst, Idx, ConstInt);
      return;
    }
  }

  // Visit constant expressions that have constant integers.
  if (auto ConstExpr = dyn_cast<ConstantExpr>(Opnd)) {
    // Handle constant gep expressions.
    if (ConstHoistGEP && ConstExpr->isGEPWithNoNotionalOverIndexing())
      collectConstantCandidates(ConstCandMap, Inst, Idx, ConstExpr);

    // Only visit constant cast expressions.
    if (!ConstExpr->isCast())
      return;

    if (auto ConstInt = dyn_cast<ConstantInt>(ConstExpr->getOperand(0))) {
      // Pretend the constant is directly used by the instruction and ignore
      // the constant expression.
      collectConstantCandidates(ConstCandMap, Inst, Idx, ConstInt);
      return;
    }
  }
}

/// Scan the instruction for expensive integer constants and record them
/// in the constant candidate vector.
void ConstantHoistingPass::collectConstantCandidates(
    ConstCandMapType &ConstCandMap, Instruction *Inst) {
  // Skip all cast instructions. They are visited indirectly later on.
  if (Inst->isCast())
    return;

  // Scan all operands.
  for (unsigned Idx = 0, E = Inst->getNumOperands(); Idx != E; ++Idx) {
    // The cost of materializing the constants (defined in
    // `TargetTransformInfo::getIntImmCost`) for instructions which only take
    // constant variables is lower than `TargetTransformInfo::TCC_Basic`. So
    // it's safe for us to collect constant candidates from all IntrinsicInsts.
    if (canReplaceOperandWithVariable(Inst, Idx) || isa<IntrinsicInst>(Inst)) {
      collectConstantCandidates(ConstCandMap, Inst, Idx);
    }
  } // end of for all operands
}

/// Collect all integer constants in the function that cannot be folded
/// into an instruction itself.
void ConstantHoistingPass::collectConstantCandidates(Function &Fn) {
  ConstCandMapType ConstCandMap;
  for (BasicBlock &BB : Fn)
    for (Instruction &Inst : BB)
      collectConstantCandidates(ConstCandMap, &Inst);
}

// This helper function is necessary to deal with values that have different
// bit widths (APInt Operator- does not like that). If the value cannot be
// represented in uint64 we return an "empty" APInt. This is then interpreted
// as the value is not in range.
static Optional<APInt> calculateOffsetDiff(const APInt &V1, const APInt &V2) {
  Optional<APInt> Res = None;
  unsigned BW = V1.getBitWidth() > V2.getBitWidth() ?
                V1.getBitWidth() : V2.getBitWidth();
  uint64_t LimVal1 = V1.getLimitedValue();
  uint64_t LimVal2 = V2.getLimitedValue();

  if (LimVal1 == ~0ULL || LimVal2 == ~0ULL)
    return Res;

  uint64_t Diff = LimVal1 - LimVal2;
  return APInt(BW, Diff, true);
}

// From a list of constants, one needs to picked as the base and the other
// constants will be transformed into an offset from that base constant. The
// question is which we can pick best? For example, consider these constants
// and their number of uses:
//
//  Constants| 2 | 4 | 12 | 42 |
//  NumUses  | 3 | 2 |  8 |  7 |
//
// Selecting constant 12 because it has the most uses will generate negative
// offsets for constants 2 and 4 (i.e. -10 and -8 respectively). If negative
// offsets lead to less optimal code generation, then there might be better
// solutions. Suppose immediates in the range of 0..35 are most optimally
// supported by the architecture, then selecting constant 2 is most optimal
// because this will generate offsets: 0, 2, 10, 40. Offsets 0, 2 and 10 are in
// range 0..35, and thus 3 + 2 + 8 = 13 uses are in range. Selecting 12 would
// have only 8 uses in range, so choosing 2 as a base is more optimal. Thus, in
// selecting the base constant the range of the offsets is a very important
// factor too that we take into account here. This algorithm calculates a total
// costs for selecting a constant as the base and substract the costs if
// immediates are out of range. It has quadratic complexity, so we call this
// function only when we're optimising for size and there are less than 100
// constants, we fall back to the straightforward algorithm otherwise
// which does not do all the offset calculations.
unsigned
ConstantHoistingPass::maximizeConstantsInRange(ConstCandVecType::iterator S,
                                           ConstCandVecType::iterator E,
                                           ConstCandVecType::iterator &MaxCostItr) {
  unsigned NumUses = 0;

  if(!Entry->getParent()->optForSize() || std::distance(S,E) > 100) {
    for (auto ConstCand = S; ConstCand != E; ++ConstCand) {
      NumUses += ConstCand->Uses.size();
      if (ConstCand->CumulativeCost > MaxCostItr->CumulativeCost)
        MaxCostItr = ConstCand;
    }
    return NumUses;
  }

  LLVM_DEBUG(dbgs() << "== Maximize constants in range ==\n");
  int MaxCost = -1;
  for (auto ConstCand = S; ConstCand != E; ++ConstCand) {
    auto Value = ConstCand->ConstInt->getValue();
    Type *Ty = ConstCand->ConstInt->getType();
    int Cost = 0;
    NumUses += ConstCand->Uses.size();
    LLVM_DEBUG(dbgs() << "= Constant: " << ConstCand->ConstInt->getValue()
                      << "\n");

    for (auto User : ConstCand->Uses) {
      unsigned Opcode = User.Inst->getOpcode();
      unsigned OpndIdx = User.OpndIdx;
      Cost += TTI->getIntImmCost(Opcode, OpndIdx, Value, Ty);
      LLVM_DEBUG(dbgs() << "Cost: " << Cost << "\n");

      for (auto C2 = S; C2 != E; ++C2) {
        Optional<APInt> Diff = calculateOffsetDiff(
                                   C2->ConstInt->getValue(),
                                   ConstCand->ConstInt->getValue());
        if (Diff) {
          const int ImmCosts =
            TTI->getIntImmCodeSizeCost(Opcode, OpndIdx, Diff.getValue(), Ty);
          Cost -= ImmCosts;
          LLVM_DEBUG(dbgs() << "Offset " << Diff.getValue() << " "
                            << "has penalty: " << ImmCosts << "\n"
                            << "Adjusted cost: " << Cost << "\n");
        }
      }
    }
    LLVM_DEBUG(dbgs() << "Cumulative cost: " << Cost << "\n");
    if (Cost > MaxCost) {
      MaxCost = Cost;
      MaxCostItr = ConstCand;
      LLVM_DEBUG(dbgs() << "New candidate: " << MaxCostItr->ConstInt->getValue()
                        << "\n");
    }
  }
  return NumUses;
}

/// Find the base constant within the given range and rebase all other
/// constants with respect to the base constant.
void ConstantHoistingPass::findAndMakeBaseConstant(
    ConstCandVecType::iterator S, ConstCandVecType::iterator E,
    SmallVectorImpl<consthoist::ConstantInfo> &ConstInfoVec) {
  auto MaxCostItr = S;
  unsigned NumUses = maximizeConstantsInRange(S, E, MaxCostItr);

  // Don't hoist constants that have only one use.
  if (NumUses <= 1)
    return;

  ConstantInt *ConstInt = MaxCostItr->ConstInt;
  ConstantExpr *ConstExpr = MaxCostItr->ConstExpr;
  ConstantInfo ConstInfo;
  ConstInfo.BaseInt = ConstInt;
  ConstInfo.BaseExpr = ConstExpr;
  Type *Ty = ConstInt->getType();

  // Rebase the constants with respect to the base constant.
  for (auto ConstCand = S; ConstCand != E; ++ConstCand) {
    APInt Diff = ConstCand->ConstInt->getValue() - ConstInt->getValue();
    Constant *Offset = Diff == 0 ? nullptr : ConstantInt::get(Ty, Diff);
    Type *ConstTy =
        ConstCand->ConstExpr ? ConstCand->ConstExpr->getType() : nullptr;
    ConstInfo.RebasedConstants.push_back(
      RebasedConstantInfo(std::move(ConstCand->Uses), Offset, ConstTy));
  }
  ConstInfoVec.push_back(std::move(ConstInfo));
}

/// Finds and combines constant candidates that can be easily
/// rematerialized with an add from a common base constant.
void ConstantHoistingPass::findBaseConstants(GlobalVariable *BaseGV) {
  // If BaseGV is nullptr, find base among candidate constant integers;
  // Otherwise find base among constant GEPs that share the same BaseGV.
  ConstCandVecType &ConstCandVec = BaseGV ?
      ConstGEPCandMap[BaseGV] : ConstIntCandVec;
  ConstInfoVecType &ConstInfoVec = BaseGV ?
      ConstGEPInfoMap[BaseGV] : ConstIntInfoVec;

  // Sort the constants by value and type. This invalidates the mapping!
  std::stable_sort(ConstCandVec.begin(), ConstCandVec.end(),
             [](const ConstantCandidate &LHS, const ConstantCandidate &RHS) {
    if (LHS.ConstInt->getType() != RHS.ConstInt->getType())
      return LHS.ConstInt->getType()->getBitWidth() <
             RHS.ConstInt->getType()->getBitWidth();
    return LHS.ConstInt->getValue().ult(RHS.ConstInt->getValue());
  });

  // Simple linear scan through the sorted constant candidate vector for viable
  // merge candidates.
  auto MinValItr = ConstCandVec.begin();
  for (auto CC = std::next(ConstCandVec.begin()), E = ConstCandVec.end();
       CC != E; ++CC) {
    if (MinValItr->ConstInt->getType() == CC->ConstInt->getType()) {
      Type *MemUseValTy = nullptr;
      for (auto &U : CC->Uses) {
        auto *UI = U.Inst;
        if (LoadInst *LI = dyn_cast<LoadInst>(UI)) {
          MemUseValTy = LI->getType();
          break;
        } else if (StoreInst *SI = dyn_cast<StoreInst>(UI)) {
          // Make sure the constant is used as pointer operand of the StoreInst.
          if (SI->getPointerOperand() == SI->getOperand(U.OpndIdx)) {
            MemUseValTy = SI->getValueOperand()->getType();
            break;
          }
        }
      }

      // Check if the constant is in range of an add with immediate.
      APInt Diff = CC->ConstInt->getValue() - MinValItr->ConstInt->getValue();
      if ((Diff.getBitWidth() <= 64) &&
          TTI->isLegalAddImmediate(Diff.getSExtValue()) &&
          // Check if Diff can be used as offset in addressing mode of the user
          // memory instruction.
          (!MemUseValTy || TTI->isLegalAddressingMode(MemUseValTy,
           /*BaseGV*/nullptr, /*BaseOffset*/Diff.getSExtValue(),
           /*HasBaseReg*/true, /*Scale*/0)))
        continue;
    }
    // We either have now a different constant type or the constant is not in
    // range of an add with immediate anymore.
    findAndMakeBaseConstant(MinValItr, CC, ConstInfoVec);
    // Start a new base constant search.
    MinValItr = CC;
  }
  // Finalize the last base constant search.
  findAndMakeBaseConstant(MinValItr, ConstCandVec.end(), ConstInfoVec);
}

/// Updates the operand at Idx in instruction Inst with the result of
///        instruction Mat. If the instruction is a PHI node then special
///        handling for duplicate values form the same incoming basic block is
///        required.
/// \return The update will always succeed, but the return value indicated if
///         Mat was used for the update or not.
static bool updateOperand(Instruction *Inst, unsigned Idx, Instruction *Mat) {
  if (auto PHI = dyn_cast<PHINode>(Inst)) {
    // Check if any previous operand of the PHI node has the same incoming basic
    // block. This is a very odd case that happens when the incoming basic block
    // has a switch statement. In this case use the same value as the previous
    // operand(s), otherwise we will fail verification due to different values.
    // The values are actually the same, but the variable names are different
    // and the verifier doesn't like that.
    BasicBlock *IncomingBB = PHI->getIncomingBlock(Idx);
    for (unsigned i = 0; i < Idx; ++i) {
      if (PHI->getIncomingBlock(i) == IncomingBB) {
        Value *IncomingVal = PHI->getIncomingValue(i);
        Inst->setOperand(Idx, IncomingVal);
        return false;
      }
    }
  }

  Inst->setOperand(Idx, Mat);
  return true;
}

/// Emit materialization code for all rebased constants and update their
/// users.
void ConstantHoistingPass::emitBaseConstants(Instruction *Base,
                                             Constant *Offset,
                                             Type *Ty,
                                             const ConstantUser &ConstUser) {
  Instruction *Mat = Base;

  // The same offset can be dereferenced to different types in nested struct.
  if (!Offset && Ty && Ty != Base->getType())
    Offset = ConstantInt::get(Type::getInt32Ty(*Ctx), 0);

  if (Offset) {
    Instruction *InsertionPt = findMatInsertPt(ConstUser.Inst,
                                               ConstUser.OpndIdx);
    if (Ty) {
      // Constant being rebased is a ConstantExpr.
      PointerType *Int8PtrTy = Type::getInt8PtrTy(*Ctx,
          cast<PointerType>(Ty)->getAddressSpace());
      Base = new BitCastInst(Base, Int8PtrTy, "base_bitcast", InsertionPt);
      Mat = GetElementPtrInst::Create(Int8PtrTy->getElementType(), Base,
          Offset, "mat_gep", InsertionPt);
      Mat = new BitCastInst(Mat, Ty, "mat_bitcast", InsertionPt);
    } else
      // Constant being rebased is a ConstantInt.
      Mat = BinaryOperator::Create(Instruction::Add, Base, Offset,
                                 "const_mat", InsertionPt);

    LLVM_DEBUG(dbgs() << "Materialize constant (" << *Base->getOperand(0)
                      << " + " << *Offset << ") in BB "
                      << Mat->getParent()->getName() << '\n'
                      << *Mat << '\n');
    Mat->setDebugLoc(ConstUser.Inst->getDebugLoc());
  }
  Value *Opnd = ConstUser.Inst->getOperand(ConstUser.OpndIdx);

  // Visit constant integer.
  if (isa<ConstantInt>(Opnd)) {
    LLVM_DEBUG(dbgs() << "Update: " << *ConstUser.Inst << '\n');
    if (!updateOperand(ConstUser.Inst, ConstUser.OpndIdx, Mat) && Offset)
      Mat->eraseFromParent();
    LLVM_DEBUG(dbgs() << "To    : " << *ConstUser.Inst << '\n');
    return;
  }

  // Visit cast instruction.
  if (auto CastInst = dyn_cast<Instruction>(Opnd)) {
    assert(CastInst->isCast() && "Expected an cast instruction!");
    // Check if we already have visited this cast instruction before to avoid
    // unnecessary cloning.
    Instruction *&ClonedCastInst = ClonedCastMap[CastInst];
    if (!ClonedCastInst) {
      ClonedCastInst = CastInst->clone();
      ClonedCastInst->setOperand(0, Mat);
      ClonedCastInst->insertAfter(CastInst);
      // Use the same debug location as the original cast instruction.
      ClonedCastInst->setDebugLoc(CastInst->getDebugLoc());
      LLVM_DEBUG(dbgs() << "Clone instruction: " << *CastInst << '\n'
                        << "To               : " << *ClonedCastInst << '\n');
    }

    LLVM_DEBUG(dbgs() << "Update: " << *ConstUser.Inst << '\n');
    updateOperand(ConstUser.Inst, ConstUser.OpndIdx, ClonedCastInst);
    LLVM_DEBUG(dbgs() << "To    : " << *ConstUser.Inst << '\n');
    return;
  }

  // Visit constant expression.
  if (auto ConstExpr = dyn_cast<ConstantExpr>(Opnd)) {
    if (ConstExpr->isGEPWithNoNotionalOverIndexing()) {
      // Operand is a ConstantGEP, replace it.
      updateOperand(ConstUser.Inst, ConstUser.OpndIdx, Mat);
      return;
    }

    // Aside from constant GEPs, only constant cast expressions are collected.
    assert(ConstExpr->isCast() && "ConstExpr should be a cast");
    Instruction *ConstExprInst = ConstExpr->getAsInstruction();
    ConstExprInst->setOperand(0, Mat);
    ConstExprInst->insertBefore(findMatInsertPt(ConstUser.Inst,
                                                ConstUser.OpndIdx));

    // Use the same debug location as the instruction we are about to update.
    ConstExprInst->setDebugLoc(ConstUser.Inst->getDebugLoc());

    LLVM_DEBUG(dbgs() << "Create instruction: " << *ConstExprInst << '\n'
                      << "From              : " << *ConstExpr << '\n');
    LLVM_DEBUG(dbgs() << "Update: " << *ConstUser.Inst << '\n');
    if (!updateOperand(ConstUser.Inst, ConstUser.OpndIdx, ConstExprInst)) {
      ConstExprInst->eraseFromParent();
      if (Offset)
        Mat->eraseFromParent();
    }
    LLVM_DEBUG(dbgs() << "To    : " << *ConstUser.Inst << '\n');
    return;
  }
}

/// Hoist and hide the base constant behind a bitcast and emit
/// materialization code for derived constants.
bool ConstantHoistingPass::emitBaseConstants(GlobalVariable *BaseGV) {
  bool MadeChange = false;
  SmallVectorImpl<consthoist::ConstantInfo> &ConstInfoVec =
      BaseGV ? ConstGEPInfoMap[BaseGV] : ConstIntInfoVec;
  for (auto const &ConstInfo : ConstInfoVec) {
    SmallPtrSet<Instruction *, 8> IPSet = findConstantInsertionPoint(ConstInfo);
    assert(!IPSet.empty() && "IPSet is empty");

    unsigned UsesNum = 0;
    unsigned ReBasesNum = 0;
    unsigned NotRebasedNum = 0;
    for (Instruction *IP : IPSet) {
      // First, collect constants depending on this IP of the base.
      unsigned Uses = 0;
      using RebasedUse = std::tuple<Constant *, Type *, ConstantUser>;
      SmallVector<RebasedUse, 4> ToBeRebased;
      for (auto const &RCI : ConstInfo.RebasedConstants) {
        for (auto const &U : RCI.Uses) {
          Uses++;
          BasicBlock *OrigMatInsertBB =
              findMatInsertPt(U.Inst, U.OpndIdx)->getParent();
          // If Base constant is to be inserted in multiple places,
          // generate rebase for U using the Base dominating U.
          if (IPSet.size() == 1 ||
              DT->dominates(IP->getParent(), OrigMatInsertBB))
            ToBeRebased.push_back(RebasedUse(RCI.Offset, RCI.Ty, U));
        }
      }
      UsesNum = Uses;

      // If only few constants depend on this IP of base, skip rebasing,
      // assuming the base and the rebased have the same materialization cost.
      if (ToBeRebased.size() < MinNumOfDependentToRebase) {
        NotRebasedNum += ToBeRebased.size();
        continue;
      }

      // Emit an instance of the base at this IP.
      Instruction *Base = nullptr;
      // Hoist and hide the base constant behind a bitcast.
      if (ConstInfo.BaseExpr) {
        assert(BaseGV && "A base constant expression must have an base GV");
        Type *Ty = ConstInfo.BaseExpr->getType();
        Base = new BitCastInst(ConstInfo.BaseExpr, Ty, "const", IP);
      } else {
        IntegerType *Ty = ConstInfo.BaseInt->getType();
        Base = new BitCastInst(ConstInfo.BaseInt, Ty, "const", IP);
      }

      Base->setDebugLoc(IP->getDebugLoc());

      LLVM_DEBUG(dbgs() << "Hoist constant (" << *ConstInfo.BaseInt
                        << ") to BB " << IP->getParent()->getName() << '\n'
                        << *Base << '\n');

      // Emit materialization code for rebased constants depending on this IP.
      for (auto const &R : ToBeRebased) {
        Constant *Off = std::get<0>(R);
        Type *Ty = std::get<1>(R);
        ConstantUser U = std::get<2>(R);
        emitBaseConstants(Base, Off, Ty, U);
        ReBasesNum++;
        // Use the same debug location as the last user of the constant.
        Base->setDebugLoc(DILocation::getMergedLocation(
            Base->getDebugLoc(), U.Inst->getDebugLoc()));
      }
      assert(!Base->use_empty() && "The use list is empty!?");
      assert(isa<Instruction>(Base->user_back()) &&
             "All uses should be instructions.");
    }
    (void)UsesNum;
    (void)ReBasesNum;
    (void)NotRebasedNum;
    // Expect all uses are rebased after rebase is done.
    assert(UsesNum == (ReBasesNum + NotRebasedNum) &&
           "Not all uses are rebased");

    NumConstantsHoisted++;

    // Base constant is also included in ConstInfo.RebasedConstants, so
    // deduct 1 from ConstInfo.RebasedConstants.size().
    NumConstantsRebased += ConstInfo.RebasedConstants.size() - 1;

    MadeChange = true;
  }
  return MadeChange;
}

/// Check all cast instructions we made a copy of and remove them if they
/// have no more users.
void ConstantHoistingPass::deleteDeadCastInst() const {
  for (auto const &I : ClonedCastMap)
    if (I.first->use_empty())
      I.first->eraseFromParent();
}

/// Optimize expensive integer constants in the given function.
bool ConstantHoistingPass::runImpl(Function &Fn, TargetTransformInfo &TTI,
                                   DominatorTree &DT, BlockFrequencyInfo *BFI,
                                   BasicBlock &Entry) {
  this->TTI = &TTI;
  this->DT = &DT;
  this->BFI = BFI;
  this->DL = &Fn.getParent()->getDataLayout();
  this->Ctx = &Fn.getContext();
  this->Entry = &Entry;
  // Collect all constant candidates.
  collectConstantCandidates(Fn);

  // Combine constants that can be easily materialized with an add from a common
  // base constant.
  if (!ConstIntCandVec.empty())
    findBaseConstants(nullptr);
  for (auto &MapEntry : ConstGEPCandMap)
    if (!MapEntry.second.empty())
      findBaseConstants(MapEntry.first);

  // Finally hoist the base constant and emit materialization code for dependent
  // constants.
  bool MadeChange = false;
  if (!ConstIntInfoVec.empty())
    MadeChange = emitBaseConstants(nullptr);
  for (auto MapEntry : ConstGEPInfoMap)
    if (!MapEntry.second.empty())
      MadeChange |= emitBaseConstants(MapEntry.first);


  // Cleanup dead instructions.
  deleteDeadCastInst();

  return MadeChange;
}

PreservedAnalyses ConstantHoistingPass::run(Function &F,
                                            FunctionAnalysisManager &AM) {
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &TTI = AM.getResult<TargetIRAnalysis>(F);
  auto BFI = ConstHoistWithBlockFrequency
                 ? &AM.getResult<BlockFrequencyAnalysis>(F)
                 : nullptr;
  if (!runImpl(F, TTI, DT, BFI, F.getEntryBlock()))
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}
