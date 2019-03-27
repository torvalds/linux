//===- BranchProbabilityInfo.cpp - Branch Probability Analysis ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Loops should be simplified before this analysis.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "branch-prob"

static cl::opt<bool> PrintBranchProb(
    "print-bpi", cl::init(false), cl::Hidden,
    cl::desc("Print the branch probability info."));

cl::opt<std::string> PrintBranchProbFuncName(
    "print-bpi-func-name", cl::Hidden,
    cl::desc("The option to specify the name of the function "
             "whose branch probability info is printed."));

INITIALIZE_PASS_BEGIN(BranchProbabilityInfoWrapperPass, "branch-prob",
                      "Branch Probability Analysis", false, true)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(BranchProbabilityInfoWrapperPass, "branch-prob",
                    "Branch Probability Analysis", false, true)

char BranchProbabilityInfoWrapperPass::ID = 0;

// Weights are for internal use only. They are used by heuristics to help to
// estimate edges' probability. Example:
//
// Using "Loop Branch Heuristics" we predict weights of edges for the
// block BB2.
//         ...
//          |
//          V
//         BB1<-+
//          |   |
//          |   | (Weight = 124)
//          V   |
//         BB2--+
//          |
//          | (Weight = 4)
//          V
//         BB3
//
// Probability of the edge BB2->BB1 = 124 / (124 + 4) = 0.96875
// Probability of the edge BB2->BB3 = 4 / (124 + 4) = 0.03125
static const uint32_t LBH_TAKEN_WEIGHT = 124;
static const uint32_t LBH_NONTAKEN_WEIGHT = 4;
// Unlikely edges within a loop are half as likely as other edges
static const uint32_t LBH_UNLIKELY_WEIGHT = 62;

/// Unreachable-terminating branch taken probability.
///
/// This is the probability for a branch being taken to a block that terminates
/// (eventually) in unreachable. These are predicted as unlikely as possible.
/// All reachable probability will equally share the remaining part.
static const BranchProbability UR_TAKEN_PROB = BranchProbability::getRaw(1);

/// Weight for a branch taken going into a cold block.
///
/// This is the weight for a branch taken toward a block marked
/// cold.  A block is marked cold if it's postdominated by a
/// block containing a call to a cold function.  Cold functions
/// are those marked with attribute 'cold'.
static const uint32_t CC_TAKEN_WEIGHT = 4;

/// Weight for a branch not-taken into a cold block.
///
/// This is the weight for a branch not taken toward a block marked
/// cold.
static const uint32_t CC_NONTAKEN_WEIGHT = 64;

static const uint32_t PH_TAKEN_WEIGHT = 20;
static const uint32_t PH_NONTAKEN_WEIGHT = 12;

static const uint32_t ZH_TAKEN_WEIGHT = 20;
static const uint32_t ZH_NONTAKEN_WEIGHT = 12;

static const uint32_t FPH_TAKEN_WEIGHT = 20;
static const uint32_t FPH_NONTAKEN_WEIGHT = 12;

/// Invoke-terminating normal branch taken weight
///
/// This is the weight for branching to the normal destination of an invoke
/// instruction. We expect this to happen most of the time. Set the weight to an
/// absurdly high value so that nested loops subsume it.
static const uint32_t IH_TAKEN_WEIGHT = 1024 * 1024 - 1;

/// Invoke-terminating normal branch not-taken weight.
///
/// This is the weight for branching to the unwind destination of an invoke
/// instruction. This is essentially never taken.
static const uint32_t IH_NONTAKEN_WEIGHT = 1;

/// Add \p BB to PostDominatedByUnreachable set if applicable.
void
BranchProbabilityInfo::updatePostDominatedByUnreachable(const BasicBlock *BB) {
  const Instruction *TI = BB->getTerminator();
  if (TI->getNumSuccessors() == 0) {
    if (isa<UnreachableInst>(TI) ||
        // If this block is terminated by a call to
        // @llvm.experimental.deoptimize then treat it like an unreachable since
        // the @llvm.experimental.deoptimize call is expected to practically
        // never execute.
        BB->getTerminatingDeoptimizeCall())
      PostDominatedByUnreachable.insert(BB);
    return;
  }

  // If the terminator is an InvokeInst, check only the normal destination block
  // as the unwind edge of InvokeInst is also very unlikely taken.
  if (auto *II = dyn_cast<InvokeInst>(TI)) {
    if (PostDominatedByUnreachable.count(II->getNormalDest()))
      PostDominatedByUnreachable.insert(BB);
    return;
  }

  for (auto *I : successors(BB))
    // If any of successor is not post dominated then BB is also not.
    if (!PostDominatedByUnreachable.count(I))
      return;

  PostDominatedByUnreachable.insert(BB);
}

/// Add \p BB to PostDominatedByColdCall set if applicable.
void
BranchProbabilityInfo::updatePostDominatedByColdCall(const BasicBlock *BB) {
  assert(!PostDominatedByColdCall.count(BB));
  const Instruction *TI = BB->getTerminator();
  if (TI->getNumSuccessors() == 0)
    return;

  // If all of successor are post dominated then BB is also done.
  if (llvm::all_of(successors(BB), [&](const BasicBlock *SuccBB) {
        return PostDominatedByColdCall.count(SuccBB);
      })) {
    PostDominatedByColdCall.insert(BB);
    return;
  }

  // If the terminator is an InvokeInst, check only the normal destination
  // block as the unwind edge of InvokeInst is also very unlikely taken.
  if (auto *II = dyn_cast<InvokeInst>(TI))
    if (PostDominatedByColdCall.count(II->getNormalDest())) {
      PostDominatedByColdCall.insert(BB);
      return;
    }

  // Otherwise, if the block itself contains a cold function, add it to the
  // set of blocks post-dominated by a cold call.
  for (auto &I : *BB)
    if (const CallInst *CI = dyn_cast<CallInst>(&I))
      if (CI->hasFnAttr(Attribute::Cold)) {
        PostDominatedByColdCall.insert(BB);
        return;
      }
}

/// Calculate edge weights for successors lead to unreachable.
///
/// Predict that a successor which leads necessarily to an
/// unreachable-terminated block as extremely unlikely.
bool BranchProbabilityInfo::calcUnreachableHeuristics(const BasicBlock *BB) {
  const Instruction *TI = BB->getTerminator();
  (void) TI;
  assert(TI->getNumSuccessors() > 1 && "expected more than one successor!");
  assert(!isa<InvokeInst>(TI) &&
         "Invokes should have already been handled by calcInvokeHeuristics");

  SmallVector<unsigned, 4> UnreachableEdges;
  SmallVector<unsigned, 4> ReachableEdges;

  for (succ_const_iterator I = succ_begin(BB), E = succ_end(BB); I != E; ++I)
    if (PostDominatedByUnreachable.count(*I))
      UnreachableEdges.push_back(I.getSuccessorIndex());
    else
      ReachableEdges.push_back(I.getSuccessorIndex());

  // Skip probabilities if all were reachable.
  if (UnreachableEdges.empty())
    return false;

  if (ReachableEdges.empty()) {
    BranchProbability Prob(1, UnreachableEdges.size());
    for (unsigned SuccIdx : UnreachableEdges)
      setEdgeProbability(BB, SuccIdx, Prob);
    return true;
  }

  auto UnreachableProb = UR_TAKEN_PROB;
  auto ReachableProb =
      (BranchProbability::getOne() - UR_TAKEN_PROB * UnreachableEdges.size()) /
      ReachableEdges.size();

  for (unsigned SuccIdx : UnreachableEdges)
    setEdgeProbability(BB, SuccIdx, UnreachableProb);
  for (unsigned SuccIdx : ReachableEdges)
    setEdgeProbability(BB, SuccIdx, ReachableProb);

  return true;
}

// Propagate existing explicit probabilities from either profile data or
// 'expect' intrinsic processing. Examine metadata against unreachable
// heuristic. The probability of the edge coming to unreachable block is
// set to min of metadata and unreachable heuristic.
bool BranchProbabilityInfo::calcMetadataWeights(const BasicBlock *BB) {
  const Instruction *TI = BB->getTerminator();
  assert(TI->getNumSuccessors() > 1 && "expected more than one successor!");
  if (!(isa<BranchInst>(TI) || isa<SwitchInst>(TI) || isa<IndirectBrInst>(TI)))
    return false;

  MDNode *WeightsNode = TI->getMetadata(LLVMContext::MD_prof);
  if (!WeightsNode)
    return false;

  // Check that the number of successors is manageable.
  assert(TI->getNumSuccessors() < UINT32_MAX && "Too many successors");

  // Ensure there are weights for all of the successors. Note that the first
  // operand to the metadata node is a name, not a weight.
  if (WeightsNode->getNumOperands() != TI->getNumSuccessors() + 1)
    return false;

  // Build up the final weights that will be used in a temporary buffer.
  // Compute the sum of all weights to later decide whether they need to
  // be scaled to fit in 32 bits.
  uint64_t WeightSum = 0;
  SmallVector<uint32_t, 2> Weights;
  SmallVector<unsigned, 2> UnreachableIdxs;
  SmallVector<unsigned, 2> ReachableIdxs;
  Weights.reserve(TI->getNumSuccessors());
  for (unsigned i = 1, e = WeightsNode->getNumOperands(); i != e; ++i) {
    ConstantInt *Weight =
        mdconst::dyn_extract<ConstantInt>(WeightsNode->getOperand(i));
    if (!Weight)
      return false;
    assert(Weight->getValue().getActiveBits() <= 32 &&
           "Too many bits for uint32_t");
    Weights.push_back(Weight->getZExtValue());
    WeightSum += Weights.back();
    if (PostDominatedByUnreachable.count(TI->getSuccessor(i - 1)))
      UnreachableIdxs.push_back(i - 1);
    else
      ReachableIdxs.push_back(i - 1);
  }
  assert(Weights.size() == TI->getNumSuccessors() && "Checked above");

  // If the sum of weights does not fit in 32 bits, scale every weight down
  // accordingly.
  uint64_t ScalingFactor =
      (WeightSum > UINT32_MAX) ? WeightSum / UINT32_MAX + 1 : 1;

  if (ScalingFactor > 1) {
    WeightSum = 0;
    for (unsigned i = 0, e = TI->getNumSuccessors(); i != e; ++i) {
      Weights[i] /= ScalingFactor;
      WeightSum += Weights[i];
    }
  }
  assert(WeightSum <= UINT32_MAX &&
         "Expected weights to scale down to 32 bits");

  if (WeightSum == 0 || ReachableIdxs.size() == 0) {
    for (unsigned i = 0, e = TI->getNumSuccessors(); i != e; ++i)
      Weights[i] = 1;
    WeightSum = TI->getNumSuccessors();
  }

  // Set the probability.
  SmallVector<BranchProbability, 2> BP;
  for (unsigned i = 0, e = TI->getNumSuccessors(); i != e; ++i)
    BP.push_back({ Weights[i], static_cast<uint32_t>(WeightSum) });

  // Examine the metadata against unreachable heuristic.
  // If the unreachable heuristic is more strong then we use it for this edge.
  if (UnreachableIdxs.size() > 0 && ReachableIdxs.size() > 0) {
    auto ToDistribute = BranchProbability::getZero();
    auto UnreachableProb = UR_TAKEN_PROB;
    for (auto i : UnreachableIdxs)
      if (UnreachableProb < BP[i]) {
        ToDistribute += BP[i] - UnreachableProb;
        BP[i] = UnreachableProb;
      }

    // If we modified the probability of some edges then we must distribute
    // the difference between reachable blocks.
    if (ToDistribute > BranchProbability::getZero()) {
      BranchProbability PerEdge = ToDistribute / ReachableIdxs.size();
      for (auto i : ReachableIdxs)
        BP[i] += PerEdge;
    }
  }

  for (unsigned i = 0, e = TI->getNumSuccessors(); i != e; ++i)
    setEdgeProbability(BB, i, BP[i]);

  return true;
}

/// Calculate edge weights for edges leading to cold blocks.
///
/// A cold block is one post-dominated by  a block with a call to a
/// cold function.  Those edges are unlikely to be taken, so we give
/// them relatively low weight.
///
/// Return true if we could compute the weights for cold edges.
/// Return false, otherwise.
bool BranchProbabilityInfo::calcColdCallHeuristics(const BasicBlock *BB) {
  const Instruction *TI = BB->getTerminator();
  (void) TI;
  assert(TI->getNumSuccessors() > 1 && "expected more than one successor!");
  assert(!isa<InvokeInst>(TI) &&
         "Invokes should have already been handled by calcInvokeHeuristics");

  // Determine which successors are post-dominated by a cold block.
  SmallVector<unsigned, 4> ColdEdges;
  SmallVector<unsigned, 4> NormalEdges;
  for (succ_const_iterator I = succ_begin(BB), E = succ_end(BB); I != E; ++I)
    if (PostDominatedByColdCall.count(*I))
      ColdEdges.push_back(I.getSuccessorIndex());
    else
      NormalEdges.push_back(I.getSuccessorIndex());

  // Skip probabilities if no cold edges.
  if (ColdEdges.empty())
    return false;

  if (NormalEdges.empty()) {
    BranchProbability Prob(1, ColdEdges.size());
    for (unsigned SuccIdx : ColdEdges)
      setEdgeProbability(BB, SuccIdx, Prob);
    return true;
  }

  auto ColdProb = BranchProbability::getBranchProbability(
      CC_TAKEN_WEIGHT,
      (CC_TAKEN_WEIGHT + CC_NONTAKEN_WEIGHT) * uint64_t(ColdEdges.size()));
  auto NormalProb = BranchProbability::getBranchProbability(
      CC_NONTAKEN_WEIGHT,
      (CC_TAKEN_WEIGHT + CC_NONTAKEN_WEIGHT) * uint64_t(NormalEdges.size()));

  for (unsigned SuccIdx : ColdEdges)
    setEdgeProbability(BB, SuccIdx, ColdProb);
  for (unsigned SuccIdx : NormalEdges)
    setEdgeProbability(BB, SuccIdx, NormalProb);

  return true;
}

// Calculate Edge Weights using "Pointer Heuristics". Predict a comparison
// between two pointer or pointer and NULL will fail.
bool BranchProbabilityInfo::calcPointerHeuristics(const BasicBlock *BB) {
  const BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator());
  if (!BI || !BI->isConditional())
    return false;

  Value *Cond = BI->getCondition();
  ICmpInst *CI = dyn_cast<ICmpInst>(Cond);
  if (!CI || !CI->isEquality())
    return false;

  Value *LHS = CI->getOperand(0);

  if (!LHS->getType()->isPointerTy())
    return false;

  assert(CI->getOperand(1)->getType()->isPointerTy());

  // p != 0   ->   isProb = true
  // p == 0   ->   isProb = false
  // p != q   ->   isProb = true
  // p == q   ->   isProb = false;
  unsigned TakenIdx = 0, NonTakenIdx = 1;
  bool isProb = CI->getPredicate() == ICmpInst::ICMP_NE;
  if (!isProb)
    std::swap(TakenIdx, NonTakenIdx);

  BranchProbability TakenProb(PH_TAKEN_WEIGHT,
                              PH_TAKEN_WEIGHT + PH_NONTAKEN_WEIGHT);
  setEdgeProbability(BB, TakenIdx, TakenProb);
  setEdgeProbability(BB, NonTakenIdx, TakenProb.getCompl());
  return true;
}

static int getSCCNum(const BasicBlock *BB,
                     const BranchProbabilityInfo::SccInfo &SccI) {
  auto SccIt = SccI.SccNums.find(BB);
  if (SccIt == SccI.SccNums.end())
    return -1;
  return SccIt->second;
}

// Consider any block that is an entry point to the SCC as a header.
static bool isSCCHeader(const BasicBlock *BB, int SccNum,
                        BranchProbabilityInfo::SccInfo &SccI) {
  assert(getSCCNum(BB, SccI) == SccNum);

  // Lazily compute the set of headers for a given SCC and cache the results
  // in the SccHeaderMap.
  if (SccI.SccHeaders.size() <= static_cast<unsigned>(SccNum))
    SccI.SccHeaders.resize(SccNum + 1);
  auto &HeaderMap = SccI.SccHeaders[SccNum];
  bool Inserted;
  BranchProbabilityInfo::SccHeaderMap::iterator HeaderMapIt;
  std::tie(HeaderMapIt, Inserted) = HeaderMap.insert(std::make_pair(BB, false));
  if (Inserted) {
    bool IsHeader = llvm::any_of(make_range(pred_begin(BB), pred_end(BB)),
                                 [&](const BasicBlock *Pred) {
                                   return getSCCNum(Pred, SccI) != SccNum;
                                 });
    HeaderMapIt->second = IsHeader;
    return IsHeader;
  } else
    return HeaderMapIt->second;
}

// Compute the unlikely successors to the block BB in the loop L, specifically
// those that are unlikely because this is a loop, and add them to the
// UnlikelyBlocks set.
static void
computeUnlikelySuccessors(const BasicBlock *BB, Loop *L,
                          SmallPtrSetImpl<const BasicBlock*> &UnlikelyBlocks) {
  // Sometimes in a loop we have a branch whose condition is made false by
  // taking it. This is typically something like
  //  int n = 0;
  //  while (...) {
  //    if (++n >= MAX) {
  //      n = 0;
  //    }
  //  }
  // In this sort of situation taking the branch means that at the very least it
  // won't be taken again in the next iteration of the loop, so we should
  // consider it less likely than a typical branch.
  //
  // We detect this by looking back through the graph of PHI nodes that sets the
  // value that the condition depends on, and seeing if we can reach a successor
  // block which can be determined to make the condition false.
  //
  // FIXME: We currently consider unlikely blocks to be half as likely as other
  // blocks, but if we consider the example above the likelyhood is actually
  // 1/MAX. We could therefore be more precise in how unlikely we consider
  // blocks to be, but it would require more careful examination of the form
  // of the comparison expression.
  const BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator());
  if (!BI || !BI->isConditional())
    return;

  // Check if the branch is based on an instruction compared with a constant
  CmpInst *CI = dyn_cast<CmpInst>(BI->getCondition());
  if (!CI || !isa<Instruction>(CI->getOperand(0)) ||
      !isa<Constant>(CI->getOperand(1)))
    return;

  // Either the instruction must be a PHI, or a chain of operations involving
  // constants that ends in a PHI which we can then collapse into a single value
  // if the PHI value is known.
  Instruction *CmpLHS = dyn_cast<Instruction>(CI->getOperand(0));
  PHINode *CmpPHI = dyn_cast<PHINode>(CmpLHS);
  Constant *CmpConst = dyn_cast<Constant>(CI->getOperand(1));
  // Collect the instructions until we hit a PHI
  SmallVector<BinaryOperator *, 1> InstChain;
  while (!CmpPHI && CmpLHS && isa<BinaryOperator>(CmpLHS) &&
         isa<Constant>(CmpLHS->getOperand(1))) {
    // Stop if the chain extends outside of the loop
    if (!L->contains(CmpLHS))
      return;
    InstChain.push_back(cast<BinaryOperator>(CmpLHS));
    CmpLHS = dyn_cast<Instruction>(CmpLHS->getOperand(0));
    if (CmpLHS)
      CmpPHI = dyn_cast<PHINode>(CmpLHS);
  }
  if (!CmpPHI || !L->contains(CmpPHI))
    return;

  // Trace the phi node to find all values that come from successors of BB
  SmallPtrSet<PHINode*, 8> VisitedInsts;
  SmallVector<PHINode*, 8> WorkList;
  WorkList.push_back(CmpPHI);
  VisitedInsts.insert(CmpPHI);
  while (!WorkList.empty()) {
    PHINode *P = WorkList.back();
    WorkList.pop_back();
    for (BasicBlock *B : P->blocks()) {
      // Skip blocks that aren't part of the loop
      if (!L->contains(B))
        continue;
      Value *V = P->getIncomingValueForBlock(B);
      // If the source is a PHI add it to the work list if we haven't
      // already visited it.
      if (PHINode *PN = dyn_cast<PHINode>(V)) {
        if (VisitedInsts.insert(PN).second)
          WorkList.push_back(PN);
        continue;
      }
      // If this incoming value is a constant and B is a successor of BB, then
      // we can constant-evaluate the compare to see if it makes the branch be
      // taken or not.
      Constant *CmpLHSConst = dyn_cast<Constant>(V);
      if (!CmpLHSConst ||
          std::find(succ_begin(BB), succ_end(BB), B) == succ_end(BB))
        continue;
      // First collapse InstChain
      for (Instruction *I : llvm::reverse(InstChain)) {
        CmpLHSConst = ConstantExpr::get(I->getOpcode(), CmpLHSConst,
                                        cast<Constant>(I->getOperand(1)), true);
        if (!CmpLHSConst)
          break;
      }
      if (!CmpLHSConst)
        continue;
      // Now constant-evaluate the compare
      Constant *Result = ConstantExpr::getCompare(CI->getPredicate(),
                                                  CmpLHSConst, CmpConst, true);
      // If the result means we don't branch to the block then that block is
      // unlikely.
      if (Result &&
          ((Result->isZeroValue() && B == BI->getSuccessor(0)) ||
           (Result->isOneValue() && B == BI->getSuccessor(1))))
        UnlikelyBlocks.insert(B);
    }
  }
}

// Calculate Edge Weights using "Loop Branch Heuristics". Predict backedges
// as taken, exiting edges as not-taken.
bool BranchProbabilityInfo::calcLoopBranchHeuristics(const BasicBlock *BB,
                                                     const LoopInfo &LI,
                                                     SccInfo &SccI) {
  int SccNum;
  Loop *L = LI.getLoopFor(BB);
  if (!L) {
    SccNum = getSCCNum(BB, SccI);
    if (SccNum < 0)
      return false;
  }

  SmallPtrSet<const BasicBlock*, 8> UnlikelyBlocks;
  if (L)
    computeUnlikelySuccessors(BB, L, UnlikelyBlocks);

  SmallVector<unsigned, 8> BackEdges;
  SmallVector<unsigned, 8> ExitingEdges;
  SmallVector<unsigned, 8> InEdges; // Edges from header to the loop.
  SmallVector<unsigned, 8> UnlikelyEdges;

  for (succ_const_iterator I = succ_begin(BB), E = succ_end(BB); I != E; ++I) {
    // Use LoopInfo if we have it, otherwise fall-back to SCC info to catch
    // irreducible loops.
    if (L) {
      if (UnlikelyBlocks.count(*I) != 0)
        UnlikelyEdges.push_back(I.getSuccessorIndex());
      else if (!L->contains(*I))
        ExitingEdges.push_back(I.getSuccessorIndex());
      else if (L->getHeader() == *I)
        BackEdges.push_back(I.getSuccessorIndex());
      else
        InEdges.push_back(I.getSuccessorIndex());
    } else {
      if (getSCCNum(*I, SccI) != SccNum)
        ExitingEdges.push_back(I.getSuccessorIndex());
      else if (isSCCHeader(*I, SccNum, SccI))
        BackEdges.push_back(I.getSuccessorIndex());
      else
        InEdges.push_back(I.getSuccessorIndex());
    }
  }

  if (BackEdges.empty() && ExitingEdges.empty() && UnlikelyEdges.empty())
    return false;

  // Collect the sum of probabilities of back-edges/in-edges/exiting-edges, and
  // normalize them so that they sum up to one.
  unsigned Denom = (BackEdges.empty() ? 0 : LBH_TAKEN_WEIGHT) +
                   (InEdges.empty() ? 0 : LBH_TAKEN_WEIGHT) +
                   (UnlikelyEdges.empty() ? 0 : LBH_UNLIKELY_WEIGHT) +
                   (ExitingEdges.empty() ? 0 : LBH_NONTAKEN_WEIGHT);

  if (uint32_t numBackEdges = BackEdges.size()) {
    BranchProbability TakenProb = BranchProbability(LBH_TAKEN_WEIGHT, Denom);
    auto Prob = TakenProb / numBackEdges;
    for (unsigned SuccIdx : BackEdges)
      setEdgeProbability(BB, SuccIdx, Prob);
  }

  if (uint32_t numInEdges = InEdges.size()) {
    BranchProbability TakenProb = BranchProbability(LBH_TAKEN_WEIGHT, Denom);
    auto Prob = TakenProb / numInEdges;
    for (unsigned SuccIdx : InEdges)
      setEdgeProbability(BB, SuccIdx, Prob);
  }

  if (uint32_t numExitingEdges = ExitingEdges.size()) {
    BranchProbability NotTakenProb = BranchProbability(LBH_NONTAKEN_WEIGHT,
                                                       Denom);
    auto Prob = NotTakenProb / numExitingEdges;
    for (unsigned SuccIdx : ExitingEdges)
      setEdgeProbability(BB, SuccIdx, Prob);
  }

  if (uint32_t numUnlikelyEdges = UnlikelyEdges.size()) {
    BranchProbability UnlikelyProb = BranchProbability(LBH_UNLIKELY_WEIGHT,
                                                       Denom);
    auto Prob = UnlikelyProb / numUnlikelyEdges;
    for (unsigned SuccIdx : UnlikelyEdges)
      setEdgeProbability(BB, SuccIdx, Prob);
  }

  return true;
}

bool BranchProbabilityInfo::calcZeroHeuristics(const BasicBlock *BB,
                                               const TargetLibraryInfo *TLI) {
  const BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator());
  if (!BI || !BI->isConditional())
    return false;

  Value *Cond = BI->getCondition();
  ICmpInst *CI = dyn_cast<ICmpInst>(Cond);
  if (!CI)
    return false;

  Value *RHS = CI->getOperand(1);
  ConstantInt *CV = dyn_cast<ConstantInt>(RHS);
  if (!CV)
    return false;

  // If the LHS is the result of AND'ing a value with a single bit bitmask,
  // we don't have information about probabilities.
  if (Instruction *LHS = dyn_cast<Instruction>(CI->getOperand(0)))
    if (LHS->getOpcode() == Instruction::And)
      if (ConstantInt *AndRHS = dyn_cast<ConstantInt>(LHS->getOperand(1)))
        if (AndRHS->getValue().isPowerOf2())
          return false;

  // Check if the LHS is the return value of a library function
  LibFunc Func = NumLibFuncs;
  if (TLI)
    if (CallInst *Call = dyn_cast<CallInst>(CI->getOperand(0)))
      if (Function *CalledFn = Call->getCalledFunction())
        TLI->getLibFunc(*CalledFn, Func);

  bool isProb;
  if (Func == LibFunc_strcasecmp ||
      Func == LibFunc_strcmp ||
      Func == LibFunc_strncasecmp ||
      Func == LibFunc_strncmp ||
      Func == LibFunc_memcmp) {
    // strcmp and similar functions return zero, negative, or positive, if the
    // first string is equal, less, or greater than the second. We consider it
    // likely that the strings are not equal, so a comparison with zero is
    // probably false, but also a comparison with any other number is also
    // probably false given that what exactly is returned for nonzero values is
    // not specified. Any kind of comparison other than equality we know
    // nothing about.
    switch (CI->getPredicate()) {
    case CmpInst::ICMP_EQ:
      isProb = false;
      break;
    case CmpInst::ICMP_NE:
      isProb = true;
      break;
    default:
      return false;
    }
  } else if (CV->isZero()) {
    switch (CI->getPredicate()) {
    case CmpInst::ICMP_EQ:
      // X == 0   ->  Unlikely
      isProb = false;
      break;
    case CmpInst::ICMP_NE:
      // X != 0   ->  Likely
      isProb = true;
      break;
    case CmpInst::ICMP_SLT:
      // X < 0   ->  Unlikely
      isProb = false;
      break;
    case CmpInst::ICMP_SGT:
      // X > 0   ->  Likely
      isProb = true;
      break;
    default:
      return false;
    }
  } else if (CV->isOne() && CI->getPredicate() == CmpInst::ICMP_SLT) {
    // InstCombine canonicalizes X <= 0 into X < 1.
    // X <= 0   ->  Unlikely
    isProb = false;
  } else if (CV->isMinusOne()) {
    switch (CI->getPredicate()) {
    case CmpInst::ICMP_EQ:
      // X == -1  ->  Unlikely
      isProb = false;
      break;
    case CmpInst::ICMP_NE:
      // X != -1  ->  Likely
      isProb = true;
      break;
    case CmpInst::ICMP_SGT:
      // InstCombine canonicalizes X >= 0 into X > -1.
      // X >= 0   ->  Likely
      isProb = true;
      break;
    default:
      return false;
    }
  } else {
    return false;
  }

  unsigned TakenIdx = 0, NonTakenIdx = 1;

  if (!isProb)
    std::swap(TakenIdx, NonTakenIdx);

  BranchProbability TakenProb(ZH_TAKEN_WEIGHT,
                              ZH_TAKEN_WEIGHT + ZH_NONTAKEN_WEIGHT);
  setEdgeProbability(BB, TakenIdx, TakenProb);
  setEdgeProbability(BB, NonTakenIdx, TakenProb.getCompl());
  return true;
}

bool BranchProbabilityInfo::calcFloatingPointHeuristics(const BasicBlock *BB) {
  const BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator());
  if (!BI || !BI->isConditional())
    return false;

  Value *Cond = BI->getCondition();
  FCmpInst *FCmp = dyn_cast<FCmpInst>(Cond);
  if (!FCmp)
    return false;

  bool isProb;
  if (FCmp->isEquality()) {
    // f1 == f2 -> Unlikely
    // f1 != f2 -> Likely
    isProb = !FCmp->isTrueWhenEqual();
  } else if (FCmp->getPredicate() == FCmpInst::FCMP_ORD) {
    // !isnan -> Likely
    isProb = true;
  } else if (FCmp->getPredicate() == FCmpInst::FCMP_UNO) {
    // isnan -> Unlikely
    isProb = false;
  } else {
    return false;
  }

  unsigned TakenIdx = 0, NonTakenIdx = 1;

  if (!isProb)
    std::swap(TakenIdx, NonTakenIdx);

  BranchProbability TakenProb(FPH_TAKEN_WEIGHT,
                              FPH_TAKEN_WEIGHT + FPH_NONTAKEN_WEIGHT);
  setEdgeProbability(BB, TakenIdx, TakenProb);
  setEdgeProbability(BB, NonTakenIdx, TakenProb.getCompl());
  return true;
}

bool BranchProbabilityInfo::calcInvokeHeuristics(const BasicBlock *BB) {
  const InvokeInst *II = dyn_cast<InvokeInst>(BB->getTerminator());
  if (!II)
    return false;

  BranchProbability TakenProb(IH_TAKEN_WEIGHT,
                              IH_TAKEN_WEIGHT + IH_NONTAKEN_WEIGHT);
  setEdgeProbability(BB, 0 /*Index for Normal*/, TakenProb);
  setEdgeProbability(BB, 1 /*Index for Unwind*/, TakenProb.getCompl());
  return true;
}

void BranchProbabilityInfo::releaseMemory() {
  Probs.clear();
}

void BranchProbabilityInfo::print(raw_ostream &OS) const {
  OS << "---- Branch Probabilities ----\n";
  // We print the probabilities from the last function the analysis ran over,
  // or the function it is currently running over.
  assert(LastF && "Cannot print prior to running over a function");
  for (const auto &BI : *LastF) {
    for (succ_const_iterator SI = succ_begin(&BI), SE = succ_end(&BI); SI != SE;
         ++SI) {
      printEdgeProbability(OS << "  ", &BI, *SI);
    }
  }
}

bool BranchProbabilityInfo::
isEdgeHot(const BasicBlock *Src, const BasicBlock *Dst) const {
  // Hot probability is at least 4/5 = 80%
  // FIXME: Compare against a static "hot" BranchProbability.
  return getEdgeProbability(Src, Dst) > BranchProbability(4, 5);
}

const BasicBlock *
BranchProbabilityInfo::getHotSucc(const BasicBlock *BB) const {
  auto MaxProb = BranchProbability::getZero();
  const BasicBlock *MaxSucc = nullptr;

  for (succ_const_iterator I = succ_begin(BB), E = succ_end(BB); I != E; ++I) {
    const BasicBlock *Succ = *I;
    auto Prob = getEdgeProbability(BB, Succ);
    if (Prob > MaxProb) {
      MaxProb = Prob;
      MaxSucc = Succ;
    }
  }

  // Hot probability is at least 4/5 = 80%
  if (MaxProb > BranchProbability(4, 5))
    return MaxSucc;

  return nullptr;
}

/// Get the raw edge probability for the edge. If can't find it, return a
/// default probability 1/N where N is the number of successors. Here an edge is
/// specified using PredBlock and an
/// index to the successors.
BranchProbability
BranchProbabilityInfo::getEdgeProbability(const BasicBlock *Src,
                                          unsigned IndexInSuccessors) const {
  auto I = Probs.find(std::make_pair(Src, IndexInSuccessors));

  if (I != Probs.end())
    return I->second;

  return {1, static_cast<uint32_t>(succ_size(Src))};
}

BranchProbability
BranchProbabilityInfo::getEdgeProbability(const BasicBlock *Src,
                                          succ_const_iterator Dst) const {
  return getEdgeProbability(Src, Dst.getSuccessorIndex());
}

/// Get the raw edge probability calculated for the block pair. This returns the
/// sum of all raw edge probabilities from Src to Dst.
BranchProbability
BranchProbabilityInfo::getEdgeProbability(const BasicBlock *Src,
                                          const BasicBlock *Dst) const {
  auto Prob = BranchProbability::getZero();
  bool FoundProb = false;
  for (succ_const_iterator I = succ_begin(Src), E = succ_end(Src); I != E; ++I)
    if (*I == Dst) {
      auto MapI = Probs.find(std::make_pair(Src, I.getSuccessorIndex()));
      if (MapI != Probs.end()) {
        FoundProb = true;
        Prob += MapI->second;
      }
    }
  uint32_t succ_num = std::distance(succ_begin(Src), succ_end(Src));
  return FoundProb ? Prob : BranchProbability(1, succ_num);
}

/// Set the edge probability for a given edge specified by PredBlock and an
/// index to the successors.
void BranchProbabilityInfo::setEdgeProbability(const BasicBlock *Src,
                                               unsigned IndexInSuccessors,
                                               BranchProbability Prob) {
  Probs[std::make_pair(Src, IndexInSuccessors)] = Prob;
  Handles.insert(BasicBlockCallbackVH(Src, this));
  LLVM_DEBUG(dbgs() << "set edge " << Src->getName() << " -> "
                    << IndexInSuccessors << " successor probability to " << Prob
                    << "\n");
}

raw_ostream &
BranchProbabilityInfo::printEdgeProbability(raw_ostream &OS,
                                            const BasicBlock *Src,
                                            const BasicBlock *Dst) const {
  const BranchProbability Prob = getEdgeProbability(Src, Dst);
  OS << "edge " << Src->getName() << " -> " << Dst->getName()
     << " probability is " << Prob
     << (isEdgeHot(Src, Dst) ? " [HOT edge]\n" : "\n");

  return OS;
}

void BranchProbabilityInfo::eraseBlock(const BasicBlock *BB) {
  for (auto I = Probs.begin(), E = Probs.end(); I != E; ++I) {
    auto Key = I->first;
    if (Key.first == BB)
      Probs.erase(Key);
  }
}

void BranchProbabilityInfo::calculate(const Function &F, const LoopInfo &LI,
                                      const TargetLibraryInfo *TLI) {
  LLVM_DEBUG(dbgs() << "---- Branch Probability Info : " << F.getName()
                    << " ----\n\n");
  LastF = &F; // Store the last function we ran on for printing.
  assert(PostDominatedByUnreachable.empty());
  assert(PostDominatedByColdCall.empty());

  // Record SCC numbers of blocks in the CFG to identify irreducible loops.
  // FIXME: We could only calculate this if the CFG is known to be irreducible
  // (perhaps cache this info in LoopInfo if we can easily calculate it there?).
  int SccNum = 0;
  SccInfo SccI;
  for (scc_iterator<const Function *> It = scc_begin(&F); !It.isAtEnd();
       ++It, ++SccNum) {
    // Ignore single-block SCCs since they either aren't loops or LoopInfo will
    // catch them.
    const std::vector<const BasicBlock *> &Scc = *It;
    if (Scc.size() == 1)
      continue;

    LLVM_DEBUG(dbgs() << "BPI: SCC " << SccNum << ":");
    for (auto *BB : Scc) {
      LLVM_DEBUG(dbgs() << " " << BB->getName());
      SccI.SccNums[BB] = SccNum;
    }
    LLVM_DEBUG(dbgs() << "\n");
  }

  // Walk the basic blocks in post-order so that we can build up state about
  // the successors of a block iteratively.
  for (auto BB : post_order(&F.getEntryBlock())) {
    LLVM_DEBUG(dbgs() << "Computing probabilities for " << BB->getName()
                      << "\n");
    updatePostDominatedByUnreachable(BB);
    updatePostDominatedByColdCall(BB);
    // If there is no at least two successors, no sense to set probability.
    if (BB->getTerminator()->getNumSuccessors() < 2)
      continue;
    if (calcMetadataWeights(BB))
      continue;
    if (calcInvokeHeuristics(BB))
      continue;
    if (calcUnreachableHeuristics(BB))
      continue;
    if (calcColdCallHeuristics(BB))
      continue;
    if (calcLoopBranchHeuristics(BB, LI, SccI))
      continue;
    if (calcPointerHeuristics(BB))
      continue;
    if (calcZeroHeuristics(BB, TLI))
      continue;
    if (calcFloatingPointHeuristics(BB))
      continue;
  }

  PostDominatedByUnreachable.clear();
  PostDominatedByColdCall.clear();

  if (PrintBranchProb &&
      (PrintBranchProbFuncName.empty() ||
       F.getName().equals(PrintBranchProbFuncName))) {
    print(dbgs());
  }
}

void BranchProbabilityInfoWrapperPass::getAnalysisUsage(
    AnalysisUsage &AU) const {
  // We require DT so it's available when LI is available. The LI updating code
  // asserts that DT is also present so if we don't make sure that we have DT
  // here, that assert will trigger.
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.addRequired<TargetLibraryInfoWrapperPass>();
  AU.setPreservesAll();
}

bool BranchProbabilityInfoWrapperPass::runOnFunction(Function &F) {
  const LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  const TargetLibraryInfo &TLI = getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
  BPI.calculate(F, LI, &TLI);
  return false;
}

void BranchProbabilityInfoWrapperPass::releaseMemory() { BPI.releaseMemory(); }

void BranchProbabilityInfoWrapperPass::print(raw_ostream &OS,
                                             const Module *) const {
  BPI.print(OS);
}

AnalysisKey BranchProbabilityAnalysis::Key;
BranchProbabilityInfo
BranchProbabilityAnalysis::run(Function &F, FunctionAnalysisManager &AM) {
  BranchProbabilityInfo BPI;
  BPI.calculate(F, AM.getResult<LoopAnalysis>(F), &AM.getResult<TargetLibraryAnalysis>(F));
  return BPI;
}

PreservedAnalyses
BranchProbabilityPrinterPass::run(Function &F, FunctionAnalysisManager &AM) {
  OS << "Printing analysis results of BPI for function "
     << "'" << F.getName() << "':"
     << "\n";
  AM.getResult<BranchProbabilityAnalysis>(F).print(OS);
  return PreservedAnalyses::all();
}
