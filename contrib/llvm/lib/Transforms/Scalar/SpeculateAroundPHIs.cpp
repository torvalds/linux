//===- SpeculateAroundPHIs.cpp --------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/SpeculateAroundPHIs.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

#define DEBUG_TYPE "spec-phis"

STATISTIC(NumPHIsSpeculated, "Number of PHI nodes we speculated around");
STATISTIC(NumEdgesSplit,
          "Number of critical edges which were split for speculation");
STATISTIC(NumSpeculatedInstructions,
          "Number of instructions we speculated around the PHI nodes");
STATISTIC(NumNewRedundantInstructions,
          "Number of new, redundant instructions inserted");

/// Check whether speculating the users of a PHI node around the PHI
/// will be safe.
///
/// This checks both that all of the users are safe and also that all of their
/// operands are either recursively safe or already available along an incoming
/// edge to the PHI.
///
/// This routine caches both all the safe nodes explored in `PotentialSpecSet`
/// and the chain of nodes that definitively reach any unsafe node in
/// `UnsafeSet`. By preserving these between repeated calls to this routine for
/// PHIs in the same basic block, the exploration here can be reused. However,
/// these caches must no be reused for PHIs in a different basic block as they
/// reflect what is available along incoming edges.
static bool
isSafeToSpeculatePHIUsers(PHINode &PN, DominatorTree &DT,
                          SmallPtrSetImpl<Instruction *> &PotentialSpecSet,
                          SmallPtrSetImpl<Instruction *> &UnsafeSet) {
  auto *PhiBB = PN.getParent();
  SmallPtrSet<Instruction *, 4> Visited;
  SmallVector<std::pair<Instruction *, User::value_op_iterator>, 16> DFSStack;

  // Walk each user of the PHI node.
  for (Use &U : PN.uses()) {
    auto *UI = cast<Instruction>(U.getUser());

    // Ensure the use post-dominates the PHI node. This ensures that, in the
    // absence of unwinding, the use will actually be reached.
    // FIXME: We use a blunt hammer of requiring them to be in the same basic
    // block. We should consider using actual post-dominance here in the
    // future.
    if (UI->getParent() != PhiBB) {
      LLVM_DEBUG(dbgs() << "  Unsafe: use in a different BB: " << *UI << "\n");
      return false;
    }

    // FIXME: This check is much too conservative. We're not going to move these
    // instructions onto new dynamic paths through the program unless there is
    // a call instruction between the use and the PHI node. And memory isn't
    // changing unless there is a store in that same sequence. We should
    // probably change this to do at least a limited scan of the intervening
    // instructions and allow handling stores in easily proven safe cases.
    if (mayBeMemoryDependent(*UI)) {
      LLVM_DEBUG(dbgs() << "  Unsafe: can't speculate use: " << *UI << "\n");
      return false;
    }

    // Now do a depth-first search of everything these users depend on to make
    // sure they are transitively safe. This is a depth-first search, but we
    // check nodes in preorder to minimize the amount of checking.
    Visited.insert(UI);
    DFSStack.push_back({UI, UI->value_op_begin()});
    do {
      User::value_op_iterator OpIt;
      std::tie(UI, OpIt) = DFSStack.pop_back_val();

      while (OpIt != UI->value_op_end()) {
        auto *OpI = dyn_cast<Instruction>(*OpIt);
        // Increment to the next operand for whenever we continue.
        ++OpIt;
        // No need to visit non-instructions, which can't form dependencies.
        if (!OpI)
          continue;

        // Now do the main pre-order checks that this operand is a viable
        // dependency of something we want to speculate.

        // First do a few checks for instructions that won't require
        // speculation at all because they are trivially available on the
        // incoming edge (either through dominance or through an incoming value
        // to a PHI).
        //
        // The cases in the current block will be trivially dominated by the
        // edge.
        auto *ParentBB = OpI->getParent();
        if (ParentBB == PhiBB) {
          if (isa<PHINode>(OpI)) {
            // We can trivially map through phi nodes in the same block.
            continue;
          }
        } else if (DT.dominates(ParentBB, PhiBB)) {
          // Instructions from dominating blocks are already available.
          continue;
        }

        // Once we know that we're considering speculating the operand, check
        // if we've already explored this subgraph and found it to be safe.
        if (PotentialSpecSet.count(OpI))
          continue;

        // If we've already explored this subgraph and found it unsafe, bail.
        // If when we directly test whether this is safe it fails, bail.
        if (UnsafeSet.count(OpI) || ParentBB != PhiBB ||
            mayBeMemoryDependent(*OpI)) {
          LLVM_DEBUG(dbgs() << "  Unsafe: can't speculate transitive use: "
                            << *OpI << "\n");
          // Record the stack of instructions which reach this node as unsafe
          // so we prune subsequent searches.
          UnsafeSet.insert(OpI);
          for (auto &StackPair : DFSStack) {
            Instruction *I = StackPair.first;
            UnsafeSet.insert(I);
          }
          return false;
        }

        // Skip any operands we're already recursively checking.
        if (!Visited.insert(OpI).second)
          continue;

        // Push onto the stack and descend. We can directly continue this
        // loop when ascending.
        DFSStack.push_back({UI, OpIt});
        UI = OpI;
        OpIt = OpI->value_op_begin();
      }

      // This node and all its operands are safe. Go ahead and cache that for
      // reuse later.
      PotentialSpecSet.insert(UI);

      // Continue with the next node on the stack.
    } while (!DFSStack.empty());
  }

#ifndef NDEBUG
  // Every visited operand should have been marked as safe for speculation at
  // this point. Verify this and return success.
  for (auto *I : Visited)
    assert(PotentialSpecSet.count(I) &&
           "Failed to mark a visited instruction as safe!");
#endif
  return true;
}

/// Check whether, in isolation, a given PHI node is both safe and profitable
/// to speculate users around.
///
/// This handles checking whether there are any constant operands to a PHI
/// which could represent a useful speculation candidate, whether the users of
/// the PHI are safe to speculate including all their transitive dependencies,
/// and whether after speculation there will be some cost savings (profit) to
/// folding the operands into the users of the PHI node. Returns true if both
/// safe and profitable with relevant cost savings updated in the map and with
/// an update to the `PotentialSpecSet`. Returns false if either safety or
/// profitability are absent. Some new entries may be made to the
/// `PotentialSpecSet` even when this routine returns false, but they remain
/// conservatively correct.
///
/// The profitability check here is a local one, but it checks this in an
/// interesting way. Beyond checking that the total cost of materializing the
/// constants will be less than the cost of folding them into their users, it
/// also checks that no one incoming constant will have a higher cost when
/// folded into its users rather than materialized. This higher cost could
/// result in a dynamic *path* that is more expensive even when the total cost
/// is lower. Currently, all of the interesting cases where this optimization
/// should fire are ones where it is a no-loss operation in this sense. If we
/// ever want to be more aggressive here, we would need to balance the
/// different incoming edges' cost by looking at their respective
/// probabilities.
static bool isSafeAndProfitableToSpeculateAroundPHI(
    PHINode &PN, SmallDenseMap<PHINode *, int, 16> &CostSavingsMap,
    SmallPtrSetImpl<Instruction *> &PotentialSpecSet,
    SmallPtrSetImpl<Instruction *> &UnsafeSet, DominatorTree &DT,
    TargetTransformInfo &TTI) {
  // First see whether there is any cost savings to speculating around this
  // PHI, and build up a map of the constant inputs to how many times they
  // occur.
  bool NonFreeMat = false;
  struct CostsAndCount {
    int MatCost = TargetTransformInfo::TCC_Free;
    int FoldedCost = TargetTransformInfo::TCC_Free;
    int Count = 0;
  };
  SmallDenseMap<ConstantInt *, CostsAndCount, 16> CostsAndCounts;
  SmallPtrSet<BasicBlock *, 16> IncomingConstantBlocks;
  for (int i : llvm::seq<int>(0, PN.getNumIncomingValues())) {
    auto *IncomingC = dyn_cast<ConstantInt>(PN.getIncomingValue(i));
    if (!IncomingC)
      continue;

    // Only visit each incoming edge with a constant input once.
    if (!IncomingConstantBlocks.insert(PN.getIncomingBlock(i)).second)
      continue;

    auto InsertResult = CostsAndCounts.insert({IncomingC, {}});
    // Count how many edges share a given incoming costant.
    ++InsertResult.first->second.Count;
    // Only compute the cost the first time we see a particular constant.
    if (!InsertResult.second)
      continue;

    int &MatCost = InsertResult.first->second.MatCost;
    MatCost = TTI.getIntImmCost(IncomingC->getValue(), IncomingC->getType());
    NonFreeMat |= MatCost != TTI.TCC_Free;
  }
  if (!NonFreeMat) {
    LLVM_DEBUG(dbgs() << "    Free: " << PN << "\n");
    // No profit in free materialization.
    return false;
  }

  // Now check that the uses of this PHI can actually be speculated,
  // otherwise we'll still have to materialize the PHI value.
  if (!isSafeToSpeculatePHIUsers(PN, DT, PotentialSpecSet, UnsafeSet)) {
    LLVM_DEBUG(dbgs() << "    Unsafe PHI: " << PN << "\n");
    return false;
  }

  // Compute how much (if any) savings are available by speculating around this
  // PHI.
  for (Use &U : PN.uses()) {
    auto *UserI = cast<Instruction>(U.getUser());
    // Now check whether there is any savings to folding the incoming constants
    // into this use.
    unsigned Idx = U.getOperandNo();

    // If we have a binary operator that is commutative, an actual constant
    // operand would end up on the RHS, so pretend the use of the PHI is on the
    // RHS.
    //
    // Technically, this is a bit weird if *both* operands are PHIs we're
    // speculating. But if that is the case, giving an "optimistic" cost isn't
    // a bad thing because after speculation it will constant fold. And
    // moreover, such cases should likely have been constant folded already by
    // some other pass, so we shouldn't worry about "modeling" them terribly
    // accurately here. Similarly, if the other operand is a constant, it still
    // seems fine to be "optimistic" in our cost modeling, because when the
    // incoming operand from the PHI node is also a constant, we will end up
    // constant folding.
    if (UserI->isBinaryOp() && UserI->isCommutative() && Idx != 1)
      // Assume we will commute the constant to the RHS to be canonical.
      Idx = 1;

    // Get the intrinsic ID if this user is an intrinsic.
    Intrinsic::ID IID = Intrinsic::not_intrinsic;
    if (auto *UserII = dyn_cast<IntrinsicInst>(UserI))
      IID = UserII->getIntrinsicID();

    for (auto &IncomingConstantAndCostsAndCount : CostsAndCounts) {
      ConstantInt *IncomingC = IncomingConstantAndCostsAndCount.first;
      int MatCost = IncomingConstantAndCostsAndCount.second.MatCost;
      int &FoldedCost = IncomingConstantAndCostsAndCount.second.FoldedCost;
      if (IID)
        FoldedCost += TTI.getIntImmCost(IID, Idx, IncomingC->getValue(),
                                        IncomingC->getType());
      else
        FoldedCost +=
            TTI.getIntImmCost(UserI->getOpcode(), Idx, IncomingC->getValue(),
                              IncomingC->getType());

      // If we accumulate more folded cost for this incoming constant than
      // materialized cost, then we'll regress any edge with this constant so
      // just bail. We're only interested in cases where folding the incoming
      // constants is at least break-even on all paths.
      if (FoldedCost > MatCost) {
        LLVM_DEBUG(dbgs() << "  Not profitable to fold imm: " << *IncomingC
                          << "\n"
                             "    Materializing cost:    "
                          << MatCost
                          << "\n"
                             "    Accumulated folded cost: "
                          << FoldedCost << "\n");
        return false;
      }
    }
  }

  // Compute the total cost savings afforded by this PHI node.
  int TotalMatCost = TTI.TCC_Free, TotalFoldedCost = TTI.TCC_Free;
  for (auto IncomingConstantAndCostsAndCount : CostsAndCounts) {
    int MatCost = IncomingConstantAndCostsAndCount.second.MatCost;
    int FoldedCost = IncomingConstantAndCostsAndCount.second.FoldedCost;
    int Count = IncomingConstantAndCostsAndCount.second.Count;

    TotalMatCost += MatCost * Count;
    TotalFoldedCost += FoldedCost * Count;
  }
  assert(TotalFoldedCost <= TotalMatCost && "If each constant's folded cost is "
                                            "less that its materialized cost, "
                                            "the sum must be as well.");

  LLVM_DEBUG(dbgs() << "    Cost savings " << (TotalMatCost - TotalFoldedCost)
                    << ": " << PN << "\n");
  CostSavingsMap[&PN] = TotalMatCost - TotalFoldedCost;
  return true;
}

/// Simple helper to walk all the users of a list of phis depth first, and call
/// a visit function on each one in post-order.
///
/// All of the PHIs should be in the same basic block, and this is primarily
/// used to make a single depth-first walk across their collective users
/// without revisiting any subgraphs. Callers should provide a fast, idempotent
/// callable to test whether a node has been visited and the more important
/// callable to actually visit a particular node.
///
/// Depth-first and postorder here refer to the *operand* graph -- we start
/// from a collection of users of PHI nodes and walk "up" the operands
/// depth-first.
template <typename IsVisitedT, typename VisitT>
static void visitPHIUsersAndDepsInPostOrder(ArrayRef<PHINode *> PNs,
                                            IsVisitedT IsVisited,
                                            VisitT Visit) {
  SmallVector<std::pair<Instruction *, User::value_op_iterator>, 16> DFSStack;
  for (auto *PN : PNs)
    for (Use &U : PN->uses()) {
      auto *UI = cast<Instruction>(U.getUser());
      if (IsVisited(UI))
        // Already visited this user, continue across the roots.
        continue;

      // Otherwise, walk the operand graph depth-first and visit each
      // dependency in postorder.
      DFSStack.push_back({UI, UI->value_op_begin()});
      do {
        User::value_op_iterator OpIt;
        std::tie(UI, OpIt) = DFSStack.pop_back_val();
        while (OpIt != UI->value_op_end()) {
          auto *OpI = dyn_cast<Instruction>(*OpIt);
          // Increment to the next operand for whenever we continue.
          ++OpIt;
          // No need to visit non-instructions, which can't form dependencies,
          // or instructions outside of our potential dependency set that we
          // were given. Finally, if we've already visited the node, continue
          // to the next.
          if (!OpI || IsVisited(OpI))
            continue;

          // Push onto the stack and descend. We can directly continue this
          // loop when ascending.
          DFSStack.push_back({UI, OpIt});
          UI = OpI;
          OpIt = OpI->value_op_begin();
        }

        // Finished visiting children, visit this node.
        assert(!IsVisited(UI) && "Should not have already visited a node!");
        Visit(UI);
      } while (!DFSStack.empty());
    }
}

/// Find profitable PHIs to speculate.
///
/// For a PHI node to be profitable, we need the cost of speculating its users
/// (and their dependencies) to not exceed the savings of folding the PHI's
/// constant operands into the speculated users.
///
/// Computing this is surprisingly challenging. Because users of two different
/// PHI nodes can depend on each other or on common other instructions, it may
/// be profitable to speculate two PHI nodes together even though neither one
/// in isolation is profitable. The straightforward way to find all the
/// profitable PHIs would be to check each combination of PHIs' cost, but this
/// is exponential in complexity.
///
/// Even if we assume that we only care about cases where we can consider each
/// PHI node in isolation (rather than considering cases where none are
/// profitable in isolation but some subset are profitable as a set), we still
/// have a challenge. The obvious way to find all individually profitable PHIs
/// is to iterate until reaching a fixed point, but this will be quadratic in
/// complexity. =/
///
/// This code currently uses a linear-to-compute order for a greedy approach.
/// It won't find cases where a set of PHIs must be considered together, but it
/// handles most cases of order dependence without quadratic iteration. The
/// specific order used is the post-order across the operand DAG. When the last
/// user of a PHI is visited in this postorder walk, we check it for
/// profitability.
///
/// There is an orthogonal extra complexity to all of this: computing the cost
/// itself can easily become a linear computation making everything again (at
/// best) quadratic. Using a postorder over the operand graph makes it
/// particularly easy to avoid this through dynamic programming. As we do the
/// postorder walk, we build the transitive cost of that subgraph. It is also
/// straightforward to then update these costs when we mark a PHI for
/// speculation so that subsequent PHIs don't re-pay the cost of already
/// speculated instructions.
static SmallVector<PHINode *, 16>
findProfitablePHIs(ArrayRef<PHINode *> PNs,
                   const SmallDenseMap<PHINode *, int, 16> &CostSavingsMap,
                   const SmallPtrSetImpl<Instruction *> &PotentialSpecSet,
                   int NumPreds, DominatorTree &DT, TargetTransformInfo &TTI) {
  SmallVector<PHINode *, 16> SpecPNs;

  // First, establish a reverse mapping from immediate users of the PHI nodes
  // to the nodes themselves, and count how many users each PHI node has in
  // a way we can update while processing them.
  SmallDenseMap<Instruction *, TinyPtrVector<PHINode *>, 16> UserToPNMap;
  SmallDenseMap<PHINode *, int, 16> PNUserCountMap;
  SmallPtrSet<Instruction *, 16> UserSet;
  for (auto *PN : PNs) {
    assert(UserSet.empty() && "Must start with an empty user set!");
    for (Use &U : PN->uses())
      UserSet.insert(cast<Instruction>(U.getUser()));
    PNUserCountMap[PN] = UserSet.size();
    for (auto *UI : UserSet)
      UserToPNMap.insert({UI, {}}).first->second.push_back(PN);
    UserSet.clear();
  }

  // Now do a DFS across the operand graph of the users, computing cost as we
  // go and when all costs for a given PHI are known, checking that PHI for
  // profitability.
  SmallDenseMap<Instruction *, int, 16> SpecCostMap;
  visitPHIUsersAndDepsInPostOrder(
      PNs,
      /*IsVisited*/
      [&](Instruction *I) {
        // We consider anything that isn't potentially speculated to be
        // "visited" as it is already handled. Similarly, anything that *is*
        // potentially speculated but for which we have an entry in our cost
        // map, we're done.
        return !PotentialSpecSet.count(I) || SpecCostMap.count(I);
      },
      /*Visit*/
      [&](Instruction *I) {
        // We've fully visited the operands, so sum their cost with this node
        // and update the cost map.
        int Cost = TTI.TCC_Free;
        for (Value *OpV : I->operand_values())
          if (auto *OpI = dyn_cast<Instruction>(OpV)) {
            auto CostMapIt = SpecCostMap.find(OpI);
            if (CostMapIt != SpecCostMap.end())
              Cost += CostMapIt->second;
          }
        Cost += TTI.getUserCost(I);
        bool Inserted = SpecCostMap.insert({I, Cost}).second;
        (void)Inserted;
        assert(Inserted && "Must not re-insert a cost during the DFS!");

        // Now check if this node had a corresponding PHI node using it. If so,
        // we need to decrement the outstanding user count for it.
        auto UserPNsIt = UserToPNMap.find(I);
        if (UserPNsIt == UserToPNMap.end())
          return;
        auto &UserPNs = UserPNsIt->second;
        auto UserPNsSplitIt = std::stable_partition(
            UserPNs.begin(), UserPNs.end(), [&](PHINode *UserPN) {
              int &PNUserCount = PNUserCountMap.find(UserPN)->second;
              assert(
                  PNUserCount > 0 &&
                  "Should never re-visit a PN after its user count hits zero!");
              --PNUserCount;
              return PNUserCount != 0;
            });

        // FIXME: Rather than one at a time, we should sum the savings as the
        // cost will be completely shared.
        SmallVector<Instruction *, 16> SpecWorklist;
        for (auto *PN : llvm::make_range(UserPNsSplitIt, UserPNs.end())) {
          int SpecCost = TTI.TCC_Free;
          for (Use &U : PN->uses())
            SpecCost +=
                SpecCostMap.find(cast<Instruction>(U.getUser()))->second;
          SpecCost *= (NumPreds - 1);
          // When the user count of a PHI node hits zero, we should check its
          // profitability. If profitable, we should mark it for speculation
          // and zero out the cost of everything it depends on.
          int CostSavings = CostSavingsMap.find(PN)->second;
          if (SpecCost > CostSavings) {
            LLVM_DEBUG(dbgs() << "  Not profitable, speculation cost: " << *PN
                              << "\n"
                                 "    Cost savings:     "
                              << CostSavings
                              << "\n"
                                 "    Speculation cost: "
                              << SpecCost << "\n");
            continue;
          }

          // We're going to speculate this user-associated PHI. Copy it out and
          // add its users to the worklist to update their cost.
          SpecPNs.push_back(PN);
          for (Use &U : PN->uses()) {
            auto *UI = cast<Instruction>(U.getUser());
            auto CostMapIt = SpecCostMap.find(UI);
            if (CostMapIt->second == 0)
              continue;
            // Zero out this cost entry to avoid duplicates.
            CostMapIt->second = 0;
            SpecWorklist.push_back(UI);
          }
        }

        // Now walk all the operands of the users in the worklist transitively
        // to zero out all the memoized costs.
        while (!SpecWorklist.empty()) {
          Instruction *SpecI = SpecWorklist.pop_back_val();
          assert(SpecCostMap.find(SpecI)->second == 0 &&
                 "Didn't zero out a cost!");

          // Walk the operands recursively to zero out their cost as well.
          for (auto *OpV : SpecI->operand_values()) {
            auto *OpI = dyn_cast<Instruction>(OpV);
            if (!OpI)
              continue;
            auto CostMapIt = SpecCostMap.find(OpI);
            if (CostMapIt == SpecCostMap.end() || CostMapIt->second == 0)
              continue;
            CostMapIt->second = 0;
            SpecWorklist.push_back(OpI);
          }
        }
      });

  return SpecPNs;
}

/// Speculate users around a set of PHI nodes.
///
/// This routine does the actual speculation around a set of PHI nodes where we
/// have determined this to be both safe and profitable.
///
/// This routine handles any spliting of critical edges necessary to create
/// a safe block to speculate into as well as cloning the instructions and
/// rewriting all uses.
static void speculatePHIs(ArrayRef<PHINode *> SpecPNs,
                          SmallPtrSetImpl<Instruction *> &PotentialSpecSet,
                          SmallSetVector<BasicBlock *, 16> &PredSet,
                          DominatorTree &DT) {
  LLVM_DEBUG(dbgs() << "  Speculating around " << SpecPNs.size() << " PHIs!\n");
  NumPHIsSpeculated += SpecPNs.size();

  // Split any critical edges so that we have a block to hoist into.
  auto *ParentBB = SpecPNs[0]->getParent();
  SmallVector<BasicBlock *, 16> SpecPreds;
  SpecPreds.reserve(PredSet.size());
  for (auto *PredBB : PredSet) {
    auto *NewPredBB = SplitCriticalEdge(
        PredBB, ParentBB,
        CriticalEdgeSplittingOptions(&DT).setMergeIdenticalEdges());
    if (NewPredBB) {
      ++NumEdgesSplit;
      LLVM_DEBUG(dbgs() << "  Split critical edge from: " << PredBB->getName()
                        << "\n");
      SpecPreds.push_back(NewPredBB);
    } else {
      assert(PredBB->getSingleSuccessor() == ParentBB &&
             "We need a non-critical predecessor to speculate into.");
      assert(!isa<InvokeInst>(PredBB->getTerminator()) &&
             "Cannot have a non-critical invoke!");

      // Already non-critical, use existing pred.
      SpecPreds.push_back(PredBB);
    }
  }

  SmallPtrSet<Instruction *, 16> SpecSet;
  SmallVector<Instruction *, 16> SpecList;
  visitPHIUsersAndDepsInPostOrder(SpecPNs,
                                  /*IsVisited*/
                                  [&](Instruction *I) {
                                    // This is visited if we don't need to
                                    // speculate it or we already have
                                    // speculated it.
                                    return !PotentialSpecSet.count(I) ||
                                           SpecSet.count(I);
                                  },
                                  /*Visit*/
                                  [&](Instruction *I) {
                                    // All operands scheduled, schedule this
                                    // node.
                                    SpecSet.insert(I);
                                    SpecList.push_back(I);
                                  });

  int NumSpecInsts = SpecList.size() * SpecPreds.size();
  int NumRedundantInsts = NumSpecInsts - SpecList.size();
  LLVM_DEBUG(dbgs() << "  Inserting " << NumSpecInsts
                    << " speculated instructions, " << NumRedundantInsts
                    << " redundancies\n");
  NumSpeculatedInstructions += NumSpecInsts;
  NumNewRedundantInstructions += NumRedundantInsts;

  // Each predecessor is numbered by its index in `SpecPreds`, so for each
  // instruction we speculate, the speculated instruction is stored in that
  // index of the vector associated with the original instruction. We also
  // store the incoming values for each predecessor from any PHIs used.
  SmallDenseMap<Instruction *, SmallVector<Value *, 2>, 16> SpeculatedValueMap;

  // Inject the synthetic mappings to rewrite PHIs to the appropriate incoming
  // value. This handles both the PHIs we are speculating around and any other
  // PHIs that happen to be used.
  for (auto *OrigI : SpecList)
    for (auto *OpV : OrigI->operand_values()) {
      auto *OpPN = dyn_cast<PHINode>(OpV);
      if (!OpPN || OpPN->getParent() != ParentBB)
        continue;

      auto InsertResult = SpeculatedValueMap.insert({OpPN, {}});
      if (!InsertResult.second)
        continue;

      auto &SpeculatedVals = InsertResult.first->second;

      // Populating our structure for mapping is particularly annoying because
      // finding an incoming value for a particular predecessor block in a PHI
      // node is a linear time operation! To avoid quadratic behavior, we build
      // a map for this PHI node's incoming values and then translate it into
      // the more compact representation used below.
      SmallDenseMap<BasicBlock *, Value *, 16> IncomingValueMap;
      for (int i : llvm::seq<int>(0, OpPN->getNumIncomingValues()))
        IncomingValueMap[OpPN->getIncomingBlock(i)] = OpPN->getIncomingValue(i);

      for (auto *PredBB : SpecPreds)
        SpeculatedVals.push_back(IncomingValueMap.find(PredBB)->second);
    }

  // Speculate into each predecessor.
  for (int PredIdx : llvm::seq<int>(0, SpecPreds.size())) {
    auto *PredBB = SpecPreds[PredIdx];
    assert(PredBB->getSingleSuccessor() == ParentBB &&
           "We need a non-critical predecessor to speculate into.");

    for (auto *OrigI : SpecList) {
      auto *NewI = OrigI->clone();
      NewI->setName(Twine(OrigI->getName()) + "." + Twine(PredIdx));
      NewI->insertBefore(PredBB->getTerminator());

      // Rewrite all the operands to the previously speculated instructions.
      // Because we're walking in-order, the defs must precede the uses and we
      // should already have these mappings.
      for (Use &U : NewI->operands()) {
        auto *OpI = dyn_cast<Instruction>(U.get());
        if (!OpI)
          continue;
        auto MapIt = SpeculatedValueMap.find(OpI);
        if (MapIt == SpeculatedValueMap.end())
          continue;
        const auto &SpeculatedVals = MapIt->second;
        assert(SpeculatedVals[PredIdx] &&
               "Must have a speculated value for this predecessor!");
        assert(SpeculatedVals[PredIdx]->getType() == OpI->getType() &&
               "Speculated value has the wrong type!");

        // Rewrite the use to this predecessor's speculated instruction.
        U.set(SpeculatedVals[PredIdx]);
      }

      // Commute instructions which now have a constant in the LHS but not the
      // RHS.
      if (NewI->isBinaryOp() && NewI->isCommutative() &&
          isa<Constant>(NewI->getOperand(0)) &&
          !isa<Constant>(NewI->getOperand(1)))
        NewI->getOperandUse(0).swap(NewI->getOperandUse(1));

      SpeculatedValueMap[OrigI].push_back(NewI);
      assert(SpeculatedValueMap[OrigI][PredIdx] == NewI &&
             "Mismatched speculated instruction index!");
    }
  }

  // Walk the speculated instruction list and if they have uses, insert a PHI
  // for them from the speculated versions, and replace the uses with the PHI.
  // Then erase the instructions as they have been fully speculated. The walk
  // needs to be in reverse so that we don't think there are users when we'll
  // actually eventually remove them later.
  IRBuilder<> IRB(SpecPNs[0]);
  for (auto *OrigI : llvm::reverse(SpecList)) {
    // Check if we need a PHI for any remaining users and if so, insert it.
    if (!OrigI->use_empty()) {
      auto *SpecIPN = IRB.CreatePHI(OrigI->getType(), SpecPreds.size(),
                                    Twine(OrigI->getName()) + ".phi");
      // Add the incoming values we speculated.
      auto &SpeculatedVals = SpeculatedValueMap.find(OrigI)->second;
      for (int PredIdx : llvm::seq<int>(0, SpecPreds.size()))
        SpecIPN->addIncoming(SpeculatedVals[PredIdx], SpecPreds[PredIdx]);

      // And replace the uses with the PHI node.
      OrigI->replaceAllUsesWith(SpecIPN);
    }

    // It is important to immediately erase this so that it stops using other
    // instructions. This avoids inserting needless PHIs of them.
    OrigI->eraseFromParent();
  }

  // All of the uses of the speculated phi nodes should be removed at this
  // point, so erase them.
  for (auto *SpecPN : SpecPNs) {
    assert(SpecPN->use_empty() && "All users should have been speculated!");
    SpecPN->eraseFromParent();
  }
}

/// Try to speculate around a series of PHIs from a single basic block.
///
/// This routine checks whether any of these PHIs are profitable to speculate
/// users around. If safe and profitable, it does the speculation. It returns
/// true when at least some speculation occurs.
static bool tryToSpeculatePHIs(SmallVectorImpl<PHINode *> &PNs,
                               DominatorTree &DT, TargetTransformInfo &TTI) {
  LLVM_DEBUG(dbgs() << "Evaluating phi nodes for speculation:\n");

  // Savings in cost from speculating around a PHI node.
  SmallDenseMap<PHINode *, int, 16> CostSavingsMap;

  // Remember the set of instructions that are candidates for speculation so
  // that we can quickly walk things within that space. This prunes out
  // instructions already available along edges, etc.
  SmallPtrSet<Instruction *, 16> PotentialSpecSet;

  // Remember the set of instructions that are (transitively) unsafe to
  // speculate into the incoming edges of this basic block. This avoids
  // recomputing them for each PHI node we check. This set is specific to this
  // block though as things are pruned out of it based on what is available
  // along incoming edges.
  SmallPtrSet<Instruction *, 16> UnsafeSet;

  // For each PHI node in this block, check whether there are immediate folding
  // opportunities from speculation, and whether that speculation will be
  // valid. This determise the set of safe PHIs to speculate.
  PNs.erase(llvm::remove_if(PNs,
                            [&](PHINode *PN) {
                              return !isSafeAndProfitableToSpeculateAroundPHI(
                                  *PN, CostSavingsMap, PotentialSpecSet,
                                  UnsafeSet, DT, TTI);
                            }),
            PNs.end());
  // If no PHIs were profitable, skip.
  if (PNs.empty()) {
    LLVM_DEBUG(dbgs() << "  No safe and profitable PHIs found!\n");
    return false;
  }

  // We need to know how much speculation will cost which is determined by how
  // many incoming edges will need a copy of each speculated instruction.
  SmallSetVector<BasicBlock *, 16> PredSet;
  for (auto *PredBB : PNs[0]->blocks()) {
    if (!PredSet.insert(PredBB))
      continue;

    // We cannot speculate when a predecessor is an indirect branch.
    // FIXME: We also can't reliably create a non-critical edge block for
    // speculation if the predecessor is an invoke. This doesn't seem
    // fundamental and we should probably be splitting critical edges
    // differently.
    if (isa<IndirectBrInst>(PredBB->getTerminator()) ||
        isa<InvokeInst>(PredBB->getTerminator())) {
      LLVM_DEBUG(dbgs() << "  Invalid: predecessor terminator: "
                        << PredBB->getName() << "\n");
      return false;
    }
  }
  if (PredSet.size() < 2) {
    LLVM_DEBUG(dbgs() << "  Unimportant: phi with only one predecessor\n");
    return false;
  }

  SmallVector<PHINode *, 16> SpecPNs = findProfitablePHIs(
      PNs, CostSavingsMap, PotentialSpecSet, PredSet.size(), DT, TTI);
  if (SpecPNs.empty())
    // Nothing to do.
    return false;

  speculatePHIs(SpecPNs, PotentialSpecSet, PredSet, DT);
  return true;
}

PreservedAnalyses SpeculateAroundPHIsPass::run(Function &F,
                                               FunctionAnalysisManager &AM) {
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &TTI = AM.getResult<TargetIRAnalysis>(F);

  bool Changed = false;
  for (auto *BB : ReversePostOrderTraversal<Function *>(&F)) {
    SmallVector<PHINode *, 16> PNs;
    auto BBI = BB->begin();
    while (auto *PN = dyn_cast<PHINode>(&*BBI)) {
      PNs.push_back(PN);
      ++BBI;
    }

    if (PNs.empty())
      continue;

    Changed |= tryToSpeculatePHIs(PNs, DT, TTI);
  }

  if (!Changed)
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  return PA;
}
