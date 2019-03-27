//===- SSAUpdaterImpl.h - SSA Updater Implementation ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides a template that implements the core algorithm for the
// SSAUpdater and MachineSSAUpdater.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SSAUPDATERIMPL_H
#define LLVM_TRANSFORMS_UTILS_SSAUPDATERIMPL_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "ssaupdater"

namespace llvm {

template<typename T> class SSAUpdaterTraits;

template<typename UpdaterT>
class SSAUpdaterImpl {
private:
  UpdaterT *Updater;

  using Traits = SSAUpdaterTraits<UpdaterT>;
  using BlkT = typename Traits::BlkT;
  using ValT = typename Traits::ValT;
  using PhiT = typename Traits::PhiT;

  /// BBInfo - Per-basic block information used internally by SSAUpdaterImpl.
  /// The predecessors of each block are cached here since pred_iterator is
  /// slow and we need to iterate over the blocks at least a few times.
  class BBInfo {
  public:
    // Back-pointer to the corresponding block.
    BlkT *BB;

    // Value to use in this block.
    ValT AvailableVal;

    // Block that defines the available value.
    BBInfo *DefBB;

    // Postorder number.
    int BlkNum = 0;

    // Immediate dominator.
    BBInfo *IDom = nullptr;

    // Number of predecessor blocks.
    unsigned NumPreds = 0;

    // Array[NumPreds] of predecessor blocks.
    BBInfo **Preds = nullptr;

    // Marker for existing PHIs that match.
    PhiT *PHITag = nullptr;

    BBInfo(BlkT *ThisBB, ValT V)
      : BB(ThisBB), AvailableVal(V), DefBB(V ? this : nullptr) {}
  };

  using AvailableValsTy = DenseMap<BlkT *, ValT>;

  AvailableValsTy *AvailableVals;

  SmallVectorImpl<PhiT *> *InsertedPHIs;

  using BlockListTy = SmallVectorImpl<BBInfo *>;
  using BBMapTy = DenseMap<BlkT *, BBInfo *>;

  BBMapTy BBMap;
  BumpPtrAllocator Allocator;

public:
  explicit SSAUpdaterImpl(UpdaterT *U, AvailableValsTy *A,
                          SmallVectorImpl<PhiT *> *Ins) :
    Updater(U), AvailableVals(A), InsertedPHIs(Ins) {}

  /// GetValue - Check to see if AvailableVals has an entry for the specified
  /// BB and if so, return it.  If not, construct SSA form by first
  /// calculating the required placement of PHIs and then inserting new PHIs
  /// where needed.
  ValT GetValue(BlkT *BB) {
    SmallVector<BBInfo *, 100> BlockList;
    BBInfo *PseudoEntry = BuildBlockList(BB, &BlockList);

    // Special case: bail out if BB is unreachable.
    if (BlockList.size() == 0) {
      ValT V = Traits::GetUndefVal(BB, Updater);
      (*AvailableVals)[BB] = V;
      return V;
    }

    FindDominators(&BlockList, PseudoEntry);
    FindPHIPlacement(&BlockList);
    FindAvailableVals(&BlockList);

    return BBMap[BB]->DefBB->AvailableVal;
  }

  /// BuildBlockList - Starting from the specified basic block, traverse back
  /// through its predecessors until reaching blocks with known values.
  /// Create BBInfo structures for the blocks and append them to the block
  /// list.
  BBInfo *BuildBlockList(BlkT *BB, BlockListTy *BlockList) {
    SmallVector<BBInfo *, 10> RootList;
    SmallVector<BBInfo *, 64> WorkList;

    BBInfo *Info = new (Allocator) BBInfo(BB, 0);
    BBMap[BB] = Info;
    WorkList.push_back(Info);

    // Search backward from BB, creating BBInfos along the way and stopping
    // when reaching blocks that define the value.  Record those defining
    // blocks on the RootList.
    SmallVector<BlkT *, 10> Preds;
    while (!WorkList.empty()) {
      Info = WorkList.pop_back_val();
      Preds.clear();
      Traits::FindPredecessorBlocks(Info->BB, &Preds);
      Info->NumPreds = Preds.size();
      if (Info->NumPreds == 0)
        Info->Preds = nullptr;
      else
        Info->Preds = static_cast<BBInfo **>(Allocator.Allocate(
            Info->NumPreds * sizeof(BBInfo *), alignof(BBInfo *)));

      for (unsigned p = 0; p != Info->NumPreds; ++p) {
        BlkT *Pred = Preds[p];
        // Check if BBMap already has a BBInfo for the predecessor block.
        typename BBMapTy::value_type &BBMapBucket =
          BBMap.FindAndConstruct(Pred);
        if (BBMapBucket.second) {
          Info->Preds[p] = BBMapBucket.second;
          continue;
        }

        // Create a new BBInfo for the predecessor.
        ValT PredVal = AvailableVals->lookup(Pred);
        BBInfo *PredInfo = new (Allocator) BBInfo(Pred, PredVal);
        BBMapBucket.second = PredInfo;
        Info->Preds[p] = PredInfo;

        if (PredInfo->AvailableVal) {
          RootList.push_back(PredInfo);
          continue;
        }
        WorkList.push_back(PredInfo);
      }
    }

    // Now that we know what blocks are backwards-reachable from the starting
    // block, do a forward depth-first traversal to assign postorder numbers
    // to those blocks.
    BBInfo *PseudoEntry = new (Allocator) BBInfo(nullptr, 0);
    unsigned BlkNum = 1;

    // Initialize the worklist with the roots from the backward traversal.
    while (!RootList.empty()) {
      Info = RootList.pop_back_val();
      Info->IDom = PseudoEntry;
      Info->BlkNum = -1;
      WorkList.push_back(Info);
    }

    while (!WorkList.empty()) {
      Info = WorkList.back();

      if (Info->BlkNum == -2) {
        // All the successors have been handled; assign the postorder number.
        Info->BlkNum = BlkNum++;
        // If not a root, put it on the BlockList.
        if (!Info->AvailableVal)
          BlockList->push_back(Info);
        WorkList.pop_back();
        continue;
      }

      // Leave this entry on the worklist, but set its BlkNum to mark that its
      // successors have been put on the worklist.  When it returns to the top
      // the list, after handling its successors, it will be assigned a
      // number.
      Info->BlkNum = -2;

      // Add unvisited successors to the work list.
      for (typename Traits::BlkSucc_iterator SI =
             Traits::BlkSucc_begin(Info->BB),
             E = Traits::BlkSucc_end(Info->BB); SI != E; ++SI) {
        BBInfo *SuccInfo = BBMap[*SI];
        if (!SuccInfo || SuccInfo->BlkNum)
          continue;
        SuccInfo->BlkNum = -1;
        WorkList.push_back(SuccInfo);
      }
    }
    PseudoEntry->BlkNum = BlkNum;
    return PseudoEntry;
  }

  /// IntersectDominators - This is the dataflow lattice "meet" operation for
  /// finding dominators.  Given two basic blocks, it walks up the dominator
  /// tree until it finds a common dominator of both.  It uses the postorder
  /// number of the blocks to determine how to do that.
  BBInfo *IntersectDominators(BBInfo *Blk1, BBInfo *Blk2) {
    while (Blk1 != Blk2) {
      while (Blk1->BlkNum < Blk2->BlkNum) {
        Blk1 = Blk1->IDom;
        if (!Blk1)
          return Blk2;
      }
      while (Blk2->BlkNum < Blk1->BlkNum) {
        Blk2 = Blk2->IDom;
        if (!Blk2)
          return Blk1;
      }
    }
    return Blk1;
  }

  /// FindDominators - Calculate the dominator tree for the subset of the CFG
  /// corresponding to the basic blocks on the BlockList.  This uses the
  /// algorithm from: "A Simple, Fast Dominance Algorithm" by Cooper, Harvey
  /// and Kennedy, published in Software--Practice and Experience, 2001,
  /// 4:1-10.  Because the CFG subset does not include any edges leading into
  /// blocks that define the value, the results are not the usual dominator
  /// tree.  The CFG subset has a single pseudo-entry node with edges to a set
  /// of root nodes for blocks that define the value.  The dominators for this
  /// subset CFG are not the standard dominators but they are adequate for
  /// placing PHIs within the subset CFG.
  void FindDominators(BlockListTy *BlockList, BBInfo *PseudoEntry) {
    bool Changed;
    do {
      Changed = false;
      // Iterate over the list in reverse order, i.e., forward on CFG edges.
      for (typename BlockListTy::reverse_iterator I = BlockList->rbegin(),
             E = BlockList->rend(); I != E; ++I) {
        BBInfo *Info = *I;
        BBInfo *NewIDom = nullptr;

        // Iterate through the block's predecessors.
        for (unsigned p = 0; p != Info->NumPreds; ++p) {
          BBInfo *Pred = Info->Preds[p];

          // Treat an unreachable predecessor as a definition with 'undef'.
          if (Pred->BlkNum == 0) {
            Pred->AvailableVal = Traits::GetUndefVal(Pred->BB, Updater);
            (*AvailableVals)[Pred->BB] = Pred->AvailableVal;
            Pred->DefBB = Pred;
            Pred->BlkNum = PseudoEntry->BlkNum;
            PseudoEntry->BlkNum++;
          }

          if (!NewIDom)
            NewIDom = Pred;
          else
            NewIDom = IntersectDominators(NewIDom, Pred);
        }

        // Check if the IDom value has changed.
        if (NewIDom && NewIDom != Info->IDom) {
          Info->IDom = NewIDom;
          Changed = true;
        }
      }
    } while (Changed);
  }

  /// IsDefInDomFrontier - Search up the dominator tree from Pred to IDom for
  /// any blocks containing definitions of the value.  If one is found, then
  /// the successor of Pred is in the dominance frontier for the definition,
  /// and this function returns true.
  bool IsDefInDomFrontier(const BBInfo *Pred, const BBInfo *IDom) {
    for (; Pred != IDom; Pred = Pred->IDom) {
      if (Pred->DefBB == Pred)
        return true;
    }
    return false;
  }

  /// FindPHIPlacement - PHIs are needed in the iterated dominance frontiers
  /// of the known definitions.  Iteratively add PHIs in the dom frontiers
  /// until nothing changes.  Along the way, keep track of the nearest
  /// dominating definitions for non-PHI blocks.
  void FindPHIPlacement(BlockListTy *BlockList) {
    bool Changed;
    do {
      Changed = false;
      // Iterate over the list in reverse order, i.e., forward on CFG edges.
      for (typename BlockListTy::reverse_iterator I = BlockList->rbegin(),
             E = BlockList->rend(); I != E; ++I) {
        BBInfo *Info = *I;

        // If this block already needs a PHI, there is nothing to do here.
        if (Info->DefBB == Info)
          continue;

        // Default to use the same def as the immediate dominator.
        BBInfo *NewDefBB = Info->IDom->DefBB;
        for (unsigned p = 0; p != Info->NumPreds; ++p) {
          if (IsDefInDomFrontier(Info->Preds[p], Info->IDom)) {
            // Need a PHI here.
            NewDefBB = Info;
            break;
          }
        }

        // Check if anything changed.
        if (NewDefBB != Info->DefBB) {
          Info->DefBB = NewDefBB;
          Changed = true;
        }
      }
    } while (Changed);
  }

  /// FindAvailableVal - If this block requires a PHI, first check if an
  /// existing PHI matches the PHI placement and reaching definitions computed
  /// earlier, and if not, create a new PHI.  Visit all the block's
  /// predecessors to calculate the available value for each one and fill in
  /// the incoming values for a new PHI.
  void FindAvailableVals(BlockListTy *BlockList) {
    // Go through the worklist in forward order (i.e., backward through the CFG)
    // and check if existing PHIs can be used.  If not, create empty PHIs where
    // they are needed.
    for (typename BlockListTy::iterator I = BlockList->begin(),
           E = BlockList->end(); I != E; ++I) {
      BBInfo *Info = *I;
      // Check if there needs to be a PHI in BB.
      if (Info->DefBB != Info)
        continue;

      // Look for an existing PHI.
      FindExistingPHI(Info->BB, BlockList);
      if (Info->AvailableVal)
        continue;

      ValT PHI = Traits::CreateEmptyPHI(Info->BB, Info->NumPreds, Updater);
      Info->AvailableVal = PHI;
      (*AvailableVals)[Info->BB] = PHI;
    }

    // Now go back through the worklist in reverse order to fill in the
    // arguments for any new PHIs added in the forward traversal.
    for (typename BlockListTy::reverse_iterator I = BlockList->rbegin(),
           E = BlockList->rend(); I != E; ++I) {
      BBInfo *Info = *I;

      if (Info->DefBB != Info) {
        // Record the available value to speed up subsequent uses of this
        // SSAUpdater for the same value.
        (*AvailableVals)[Info->BB] = Info->DefBB->AvailableVal;
        continue;
      }

      // Check if this block contains a newly added PHI.
      PhiT *PHI = Traits::ValueIsNewPHI(Info->AvailableVal, Updater);
      if (!PHI)
        continue;

      // Iterate through the block's predecessors.
      for (unsigned p = 0; p != Info->NumPreds; ++p) {
        BBInfo *PredInfo = Info->Preds[p];
        BlkT *Pred = PredInfo->BB;
        // Skip to the nearest preceding definition.
        if (PredInfo->DefBB != PredInfo)
          PredInfo = PredInfo->DefBB;
        Traits::AddPHIOperand(PHI, PredInfo->AvailableVal, Pred);
      }

      LLVM_DEBUG(dbgs() << "  Inserted PHI: " << *PHI << "\n");

      // If the client wants to know about all new instructions, tell it.
      if (InsertedPHIs) InsertedPHIs->push_back(PHI);
    }
  }

  /// FindExistingPHI - Look through the PHI nodes in a block to see if any of
  /// them match what is needed.
  void FindExistingPHI(BlkT *BB, BlockListTy *BlockList) {
    for (auto &SomePHI : BB->phis()) {
      if (CheckIfPHIMatches(&SomePHI)) {
        RecordMatchingPHIs(BlockList);
        break;
      }
      // Match failed: clear all the PHITag values.
      for (typename BlockListTy::iterator I = BlockList->begin(),
             E = BlockList->end(); I != E; ++I)
        (*I)->PHITag = nullptr;
    }
  }

  /// CheckIfPHIMatches - Check if a PHI node matches the placement and values
  /// in the BBMap.
  bool CheckIfPHIMatches(PhiT *PHI) {
    SmallVector<PhiT *, 20> WorkList;
    WorkList.push_back(PHI);

    // Mark that the block containing this PHI has been visited.
    BBMap[PHI->getParent()]->PHITag = PHI;

    while (!WorkList.empty()) {
      PHI = WorkList.pop_back_val();

      // Iterate through the PHI's incoming values.
      for (typename Traits::PHI_iterator I = Traits::PHI_begin(PHI),
             E = Traits::PHI_end(PHI); I != E; ++I) {
        ValT IncomingVal = I.getIncomingValue();
        BBInfo *PredInfo = BBMap[I.getIncomingBlock()];
        // Skip to the nearest preceding definition.
        if (PredInfo->DefBB != PredInfo)
          PredInfo = PredInfo->DefBB;

        // Check if it matches the expected value.
        if (PredInfo->AvailableVal) {
          if (IncomingVal == PredInfo->AvailableVal)
            continue;
          return false;
        }

        // Check if the value is a PHI in the correct block.
        PhiT *IncomingPHIVal = Traits::ValueIsPHI(IncomingVal, Updater);
        if (!IncomingPHIVal || IncomingPHIVal->getParent() != PredInfo->BB)
          return false;

        // If this block has already been visited, check if this PHI matches.
        if (PredInfo->PHITag) {
          if (IncomingPHIVal == PredInfo->PHITag)
            continue;
          return false;
        }
        PredInfo->PHITag = IncomingPHIVal;

        WorkList.push_back(IncomingPHIVal);
      }
    }
    return true;
  }

  /// RecordMatchingPHIs - For each PHI node that matches, record it in both
  /// the BBMap and the AvailableVals mapping.
  void RecordMatchingPHIs(BlockListTy *BlockList) {
    for (typename BlockListTy::iterator I = BlockList->begin(),
           E = BlockList->end(); I != E; ++I)
      if (PhiT *PHI = (*I)->PHITag) {
        BlkT *BB = PHI->getParent();
        ValT PHIVal = Traits::GetPHIValue(PHI);
        (*AvailableVals)[BB] = PHIVal;
        BBMap[BB]->AvailableVal = PHIVal;
      }
  }
};

} // end namespace llvm

#undef DEBUG_TYPE // "ssaupdater"

#endif // LLVM_TRANSFORMS_UTILS_SSAUPDATERIMPL_H
