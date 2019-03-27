//===- DomTreeUpdater.h - DomTree/Post DomTree Updater ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the DomTreeUpdater class, which provides a uniform way to
// update dominator tree related data structures.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DOMTREEUPDATER_H
#define LLVM_DOMTREEUPDATER_H

#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/GenericDomTree.h"
#include <functional>
#include <vector>

namespace llvm {
class DomTreeUpdater {
public:
  enum class UpdateStrategy : unsigned char { Eager = 0, Lazy = 1 };

  explicit DomTreeUpdater(UpdateStrategy Strategy_) : Strategy(Strategy_) {}
  DomTreeUpdater(DominatorTree &DT_, UpdateStrategy Strategy_)
      : DT(&DT_), Strategy(Strategy_) {}
  DomTreeUpdater(DominatorTree *DT_, UpdateStrategy Strategy_)
      : DT(DT_), Strategy(Strategy_) {}
  DomTreeUpdater(PostDominatorTree &PDT_, UpdateStrategy Strategy_)
      : PDT(&PDT_), Strategy(Strategy_) {}
  DomTreeUpdater(PostDominatorTree *PDT_, UpdateStrategy Strategy_)
      : PDT(PDT_), Strategy(Strategy_) {}
  DomTreeUpdater(DominatorTree &DT_, PostDominatorTree &PDT_,
                 UpdateStrategy Strategy_)
      : DT(&DT_), PDT(&PDT_), Strategy(Strategy_) {}
  DomTreeUpdater(DominatorTree *DT_, PostDominatorTree *PDT_,
                 UpdateStrategy Strategy_)
      : DT(DT_), PDT(PDT_), Strategy(Strategy_) {}

  ~DomTreeUpdater() { flush(); }

  /// Returns true if the current strategy is Lazy.
  bool isLazy() const { return Strategy == UpdateStrategy::Lazy; };

  /// Returns true if the current strategy is Eager.
  bool isEager() const { return Strategy == UpdateStrategy::Eager; };

  /// Returns true if it holds a DominatorTree.
  bool hasDomTree() const { return DT != nullptr; }

  /// Returns true if it holds a PostDominatorTree.
  bool hasPostDomTree() const { return PDT != nullptr; }

  /// Returns true if there is BasicBlock awaiting deletion.
  /// The deletion will only happen until a flush event and
  /// all available trees are up-to-date.
  /// Returns false under Eager UpdateStrategy.
  bool hasPendingDeletedBB() const { return !DeletedBBs.empty(); }

  /// Returns true if DelBB is awaiting deletion.
  /// Returns false under Eager UpdateStrategy.
  bool isBBPendingDeletion(BasicBlock *DelBB) const;

  /// Returns true if either of DT or PDT is valid and the tree has at
  /// least one update pending. If DT or PDT is nullptr it is treated
  /// as having no pending updates. This function does not check
  /// whether there is BasicBlock awaiting deletion.
  /// Returns false under Eager UpdateStrategy.
  bool hasPendingUpdates() const;

  /// Returns true if there are DominatorTree updates queued.
  /// Returns false under Eager UpdateStrategy or DT is nullptr.
  bool hasPendingDomTreeUpdates() const;

  /// Returns true if there are PostDominatorTree updates queued.
  /// Returns false under Eager UpdateStrategy or PDT is nullptr.
  bool hasPendingPostDomTreeUpdates() const;

  /// Apply updates on all available trees. Under Eager UpdateStrategy with
  /// ForceRemoveDuplicates enabled or under Lazy UpdateStrategy, it will
  /// discard duplicated updates and self-dominance updates. If both DT and PDT
  /// are nullptrs, this function discards all updates. The Eager Strategy
  /// applies the updates immediately while the Lazy Strategy queues the
  /// updates. It is required for the state of the LLVM IR to be updated
  /// *before* applying the Updates because the internal update routine will
  /// analyze the current state of the relationship between a pair of (From, To)
  /// BasicBlocks to determine whether a single update needs to be discarded.
  void applyUpdates(ArrayRef<DominatorTree::UpdateType> Updates,
                    bool ForceRemoveDuplicates = false);

  /// Notify all available trees on an edge insertion. If both DT and PDT are
  /// nullptrs, this function discards the update. Under either Strategy,
  /// self-dominance update will be removed. The Eager Strategy applies
  /// the update immediately while the Lazy Strategy queues the update.
  /// It is recommended to only use this method when you have exactly one
  /// insertion (and no deletions). It is recommended to use applyUpdates() in
  /// all other cases. This function has to be called *after* making the update
  /// on the actual CFG. An internal functions checks if the edge exists in the
  /// CFG in DEBUG mode.
  void insertEdge(BasicBlock *From, BasicBlock *To);

  /// Notify all available trees on an edge insertion.
  /// Under either Strategy, the following updates will be discard silently
  /// 1. Invalid - Inserting an edge that does not exist in the CFG.
  /// 2. Self-dominance update.
  /// 3. Both DT and PDT are nullptrs.
  /// The Eager Strategy applies the update immediately while the Lazy Strategy
  /// queues the update. It is recommended to only use this method when you have
  /// exactly one insertion (and no deletions) and want to discard an invalid
  /// update.
  void insertEdgeRelaxed(BasicBlock *From, BasicBlock *To);

  /// Notify all available trees on an edge deletion. If both DT and PDT are
  /// nullptrs, this function discards the update. Under either Strategy,
  /// self-dominance update will be removed. The Eager Strategy applies
  /// the update immediately while the Lazy Strategy queues the update.
  /// It is recommended to only use this method when you have exactly one
  /// deletion (and no insertions). It is recommended to use applyUpdates() in
  /// all other cases. This function has to be called *after* making the update
  /// on the actual CFG. An internal functions checks if the edge doesn't exist
  /// in the CFG in DEBUG mode.
  void deleteEdge(BasicBlock *From, BasicBlock *To);

  /// Notify all available trees on an edge deletion.
  /// Under either Strategy, the following updates will be discard silently
  /// 1. Invalid - Deleting an edge that still exists in the CFG.
  /// 2. Self-dominance update.
  /// 3. Both DT and PDT are nullptrs.
  /// The Eager Strategy applies the update immediately while the Lazy Strategy
  /// queues the update. It is recommended to only use this method when you have
  /// exactly one deletion (and no insertions) and want to discard an invalid
  /// update.
  void deleteEdgeRelaxed(BasicBlock *From, BasicBlock *To);

  /// Delete DelBB. DelBB will be removed from its Parent and
  /// erased from available trees if it exists and finally get deleted.
  /// Under Eager UpdateStrategy, DelBB will be processed immediately.
  /// Under Lazy UpdateStrategy, DelBB will be queued until a flush event and
  /// all available trees are up-to-date. Assert if any instruction of DelBB is
  /// modified while awaiting deletion. When both DT and PDT are nullptrs, DelBB
  /// will be queued until flush() is called.
  void deleteBB(BasicBlock *DelBB);

  /// Delete DelBB. DelBB will be removed from its Parent and
  /// erased from available trees if it exists. Then the callback will
  /// be called. Finally, DelBB will be deleted.
  /// Under Eager UpdateStrategy, DelBB will be processed immediately.
  /// Under Lazy UpdateStrategy, DelBB will be queued until a flush event and
  /// all available trees are up-to-date. Assert if any instruction of DelBB is
  /// modified while awaiting deletion. Multiple callbacks can be queued for one
  /// DelBB under Lazy UpdateStrategy.
  void callbackDeleteBB(BasicBlock *DelBB,
                        std::function<void(BasicBlock *)> Callback);

  /// Recalculate all available trees and flush all BasicBlocks
  /// awaiting deletion immediately.
  void recalculate(Function &F);

  /// Flush DomTree updates and return DomTree.
  /// It also flush out of date updates applied by all available trees
  /// and flush Deleted BBs if both trees are up-to-date.
  /// It must only be called when it has a DomTree.
  DominatorTree &getDomTree();

  /// Flush PostDomTree updates and return PostDomTree.
  /// It also flush out of date updates applied by all available trees
  /// and flush Deleted BBs if both trees are up-to-date.
  /// It must only be called when it has a PostDomTree.
  PostDominatorTree &getPostDomTree();

  /// Apply all pending updates to available trees and flush all BasicBlocks
  /// awaiting deletion.
  /// Does nothing under Eager UpdateStrategy.
  void flush();

  /// Debug method to help view the internal state of this class.
  LLVM_DUMP_METHOD void dump() const;

private:
  class CallBackOnDeletion final : public CallbackVH {
  public:
    CallBackOnDeletion(BasicBlock *V,
                       std::function<void(BasicBlock *)> Callback)
        : CallbackVH(V), DelBB(V), Callback_(Callback) {}

  private:
    BasicBlock *DelBB = nullptr;
    std::function<void(BasicBlock *)> Callback_;

    void deleted() override {
      Callback_(DelBB);
      CallbackVH::deleted();
    }
  };

  SmallVector<DominatorTree::UpdateType, 16> PendUpdates;
  size_t PendDTUpdateIndex = 0;
  size_t PendPDTUpdateIndex = 0;
  DominatorTree *DT = nullptr;
  PostDominatorTree *PDT = nullptr;
  const UpdateStrategy Strategy;
  SmallPtrSet<BasicBlock *, 8> DeletedBBs;
  std::vector<CallBackOnDeletion> Callbacks;
  bool IsRecalculatingDomTree = false;
  bool IsRecalculatingPostDomTree = false;

  /// First remove all the instructions of DelBB and then make sure DelBB has a
  /// valid terminator instruction which is necessary to have when DelBB still
  /// has to be inside of its parent Function while awaiting deletion under Lazy
  /// UpdateStrategy to prevent other routines from asserting the state of the
  /// IR is inconsistent. Assert if DelBB is nullptr or has predecessors.
  void validateDeleteBB(BasicBlock *DelBB);

  /// Returns true if at least one BasicBlock is deleted.
  bool forceFlushDeletedBB();

  /// Deduplicate and remove unnecessary updates (no-ops) when using Lazy
  /// UpdateStrategy. Returns true if the update is queued for update.
  bool applyLazyUpdate(DominatorTree::UpdateKind Kind, BasicBlock *From,
                       BasicBlock *To);

  /// Helper function to apply all pending DomTree updates.
  void applyDomTreeUpdates();

  /// Helper function to apply all pending PostDomTree updates.
  void applyPostDomTreeUpdates();

  /// Helper function to flush deleted BasicBlocks if all available
  /// trees are up-to-date.
  void tryFlushDeletedBB();

  /// Drop all updates applied by all available trees and delete BasicBlocks if
  /// all available trees are up-to-date.
  void dropOutOfDateUpdates();

  /// Erase Basic Block node that has been unlinked from Function
  /// in the DomTree and PostDomTree.
  void eraseDelBBNode(BasicBlock *DelBB);

  /// Returns true if the update appears in the LLVM IR.
  /// It is used to check whether an update is valid in
  /// insertEdge/deleteEdge or is unnecessary in the batch update.
  bool isUpdateValid(DominatorTree::UpdateType Update) const;

  /// Returns true if the update is self dominance.
  bool isSelfDominance(DominatorTree::UpdateType Update) const;
};
} // namespace llvm

#endif // LLVM_DOMTREEUPDATER_H
