//===-- WebAssemblyCFGSort.cpp - CFG Sorting ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a CFG sorting pass.
///
/// This pass reorders the blocks in a function to put them into topological
/// order, ignoring loop backedges, and without any loop or exception being
/// interrupted by a block not dominated by the its header, with special care
/// to keep the order as similar as possible to the original order.
///
////===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssembly.h"
#include "WebAssemblyExceptionInfo.h"
#include "WebAssemblySubtarget.h"
#include "WebAssemblyUtilities.h"
#include "llvm/ADT/PriorityQueue.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-cfg-sort"

namespace {

// Wrapper for loops and exceptions
class Region {
public:
  virtual ~Region() = default;
  virtual MachineBasicBlock *getHeader() const = 0;
  virtual bool contains(const MachineBasicBlock *MBB) const = 0;
  virtual unsigned getNumBlocks() const = 0;
  using block_iterator = typename ArrayRef<MachineBasicBlock *>::const_iterator;
  virtual iterator_range<block_iterator> blocks() const = 0;
  virtual bool isLoop() const = 0;
};

template <typename T> class ConcreteRegion : public Region {
  const T *Region;

public:
  ConcreteRegion(const T *Region) : Region(Region) {}
  MachineBasicBlock *getHeader() const override { return Region->getHeader(); }
  bool contains(const MachineBasicBlock *MBB) const override {
    return Region->contains(MBB);
  }
  unsigned getNumBlocks() const override { return Region->getNumBlocks(); }
  iterator_range<block_iterator> blocks() const override {
    return Region->blocks();
  }
  bool isLoop() const override { return false; }
};

template <> bool ConcreteRegion<MachineLoop>::isLoop() const { return true; }

// This class has information of nested Regions; this is analogous to what
// LoopInfo is for loops.
class RegionInfo {
  const MachineLoopInfo &MLI;
  const WebAssemblyExceptionInfo &WEI;
  std::vector<const Region *> Regions;
  DenseMap<const MachineLoop *, std::unique_ptr<Region>> LoopMap;
  DenseMap<const WebAssemblyException *, std::unique_ptr<Region>> ExceptionMap;

public:
  RegionInfo(const MachineLoopInfo &MLI, const WebAssemblyExceptionInfo &WEI)
      : MLI(MLI), WEI(WEI) {}

  // Returns a smallest loop or exception that contains MBB
  const Region *getRegionFor(const MachineBasicBlock *MBB) {
    const auto *ML = MLI.getLoopFor(MBB);
    const auto *WE = WEI.getExceptionFor(MBB);
    if (!ML && !WE)
      return nullptr;
    if ((ML && !WE) || (ML && WE && ML->getNumBlocks() < WE->getNumBlocks())) {
      // If the smallest region containing MBB is a loop
      if (LoopMap.count(ML))
        return LoopMap[ML].get();
      LoopMap[ML] = llvm::make_unique<ConcreteRegion<MachineLoop>>(ML);
      return LoopMap[ML].get();
    } else {
      // If the smallest region containing MBB is an exception
      if (ExceptionMap.count(WE))
        return ExceptionMap[WE].get();
      ExceptionMap[WE] =
          llvm::make_unique<ConcreteRegion<WebAssemblyException>>(WE);
      return ExceptionMap[WE].get();
    }
  }
};

class WebAssemblyCFGSort final : public MachineFunctionPass {
  StringRef getPassName() const override { return "WebAssembly CFG Sort"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineDominatorTree>();
    AU.addPreserved<MachineDominatorTree>();
    AU.addRequired<MachineLoopInfo>();
    AU.addPreserved<MachineLoopInfo>();
    AU.addRequired<WebAssemblyExceptionInfo>();
    AU.addPreserved<WebAssemblyExceptionInfo>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblyCFGSort() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char WebAssemblyCFGSort::ID = 0;
INITIALIZE_PASS(WebAssemblyCFGSort, DEBUG_TYPE,
                "Reorders blocks in topological order", false, false)

FunctionPass *llvm::createWebAssemblyCFGSort() {
  return new WebAssemblyCFGSort();
}

static void MaybeUpdateTerminator(MachineBasicBlock *MBB) {
#ifndef NDEBUG
  bool AnyBarrier = false;
#endif
  bool AllAnalyzable = true;
  for (const MachineInstr &Term : MBB->terminators()) {
#ifndef NDEBUG
    AnyBarrier |= Term.isBarrier();
#endif
    AllAnalyzable &= Term.isBranch() && !Term.isIndirectBranch();
  }
  assert((AnyBarrier || AllAnalyzable) &&
         "AnalyzeBranch needs to analyze any block with a fallthrough");
  if (AllAnalyzable)
    MBB->updateTerminator();
}

namespace {
// EH pads are selected first regardless of the block comparison order.
// When only one of the BBs is an EH pad, we give a higher priority to it, to
// prevent common mismatches between possibly throwing calls and ehpads they
// unwind to, as in the example below:
//
// bb0:
//   call @foo      // If this throws, unwind to bb2
// bb1:
//   call @bar      // If this throws, unwind to bb3
// bb2 (ehpad):
//   handler_bb2
// bb3 (ehpad):
//   handler_bb3
// continuing code
//
// Because this pass tries to preserve the original BB order, this order will
// not change. But this will result in this try-catch structure in CFGStackify,
// resulting in a mismatch:
// try
//   try
//     call @foo
//     call @bar    // This should unwind to bb3, not bb2!
//   catch
//     handler_bb2
//   end
// catch
//   handler_bb3
// end
// continuing code
//
// If we give a higher priority to an EH pad whenever it is ready in this
// example, when both bb1 and bb2 are ready, we would pick up bb2 first.

/// Sort blocks by their number.
struct CompareBlockNumbers {
  bool operator()(const MachineBasicBlock *A,
                  const MachineBasicBlock *B) const {
    if (A->isEHPad() && !B->isEHPad())
      return false;
    if (!A->isEHPad() && B->isEHPad())
      return true;

    return A->getNumber() > B->getNumber();
  }
};
/// Sort blocks by their number in the opposite order..
struct CompareBlockNumbersBackwards {
  bool operator()(const MachineBasicBlock *A,
                  const MachineBasicBlock *B) const {
    // We give a higher priority to an EH pad
    if (A->isEHPad() && !B->isEHPad())
      return false;
    if (!A->isEHPad() && B->isEHPad())
      return true;

    return A->getNumber() < B->getNumber();
  }
};
/// Bookkeeping for a region to help ensure that we don't mix blocks not
/// dominated by the its header among its blocks.
struct Entry {
  const Region *TheRegion;
  unsigned NumBlocksLeft;

  /// List of blocks not dominated by Loop's header that are deferred until
  /// after all of Loop's blocks have been seen.
  std::vector<MachineBasicBlock *> Deferred;

  explicit Entry(const class Region *R)
      : TheRegion(R), NumBlocksLeft(R->getNumBlocks()) {}
};
} // end anonymous namespace

/// Sort the blocks, taking special care to make sure that regions are not
/// interrupted by blocks not dominated by their header.
/// TODO: There are many opportunities for improving the heuristics here.
/// Explore them.
static void SortBlocks(MachineFunction &MF, const MachineLoopInfo &MLI,
                       const WebAssemblyExceptionInfo &WEI,
                       const MachineDominatorTree &MDT) {
  // Prepare for a topological sort: Record the number of predecessors each
  // block has, ignoring loop backedges.
  MF.RenumberBlocks();
  SmallVector<unsigned, 16> NumPredsLeft(MF.getNumBlockIDs(), 0);
  for (MachineBasicBlock &MBB : MF) {
    unsigned N = MBB.pred_size();
    if (MachineLoop *L = MLI.getLoopFor(&MBB))
      if (L->getHeader() == &MBB)
        for (const MachineBasicBlock *Pred : MBB.predecessors())
          if (L->contains(Pred))
            --N;
    NumPredsLeft[MBB.getNumber()] = N;
  }

  // Topological sort the CFG, with additional constraints:
  //  - Between a region header and the last block in the region, there can be
  //    no blocks not dominated by its header.
  //  - It's desirable to preserve the original block order when possible.
  // We use two ready lists; Preferred and Ready. Preferred has recently
  // processed successors, to help preserve block sequences from the original
  // order. Ready has the remaining ready blocks. EH blocks are picked first
  // from both queues.
  PriorityQueue<MachineBasicBlock *, std::vector<MachineBasicBlock *>,
                CompareBlockNumbers>
      Preferred;
  PriorityQueue<MachineBasicBlock *, std::vector<MachineBasicBlock *>,
                CompareBlockNumbersBackwards>
      Ready;

  RegionInfo SUI(MLI, WEI);
  SmallVector<Entry, 4> Entries;
  for (MachineBasicBlock *MBB = &MF.front();;) {
    const Region *R = SUI.getRegionFor(MBB);
    if (R) {
      // If MBB is a region header, add it to the active region list. We can't
      // put any blocks that it doesn't dominate until we see the end of the
      // region.
      if (R->getHeader() == MBB)
        Entries.push_back(Entry(R));
      // For each active region the block is in, decrement the count. If MBB is
      // the last block in an active region, take it off the list and pick up
      // any blocks deferred because the header didn't dominate them.
      for (Entry &E : Entries)
        if (E.TheRegion->contains(MBB) && --E.NumBlocksLeft == 0)
          for (auto DeferredBlock : E.Deferred)
            Ready.push(DeferredBlock);
      while (!Entries.empty() && Entries.back().NumBlocksLeft == 0)
        Entries.pop_back();
    }
    // The main topological sort logic.
    for (MachineBasicBlock *Succ : MBB->successors()) {
      // Ignore backedges.
      if (MachineLoop *SuccL = MLI.getLoopFor(Succ))
        if (SuccL->getHeader() == Succ && SuccL->contains(MBB))
          continue;
      // Decrement the predecessor count. If it's now zero, it's ready.
      if (--NumPredsLeft[Succ->getNumber()] == 0)
        Preferred.push(Succ);
    }
    // Determine the block to follow MBB. First try to find a preferred block,
    // to preserve the original block order when possible.
    MachineBasicBlock *Next = nullptr;
    while (!Preferred.empty()) {
      Next = Preferred.top();
      Preferred.pop();
      // If X isn't dominated by the top active region header, defer it until
      // that region is done.
      if (!Entries.empty() &&
          !MDT.dominates(Entries.back().TheRegion->getHeader(), Next)) {
        Entries.back().Deferred.push_back(Next);
        Next = nullptr;
        continue;
      }
      // If Next was originally ordered before MBB, and it isn't because it was
      // loop-rotated above the header, it's not preferred.
      if (Next->getNumber() < MBB->getNumber() &&
          (!R || !R->contains(Next) ||
           R->getHeader()->getNumber() < Next->getNumber())) {
        Ready.push(Next);
        Next = nullptr;
        continue;
      }
      break;
    }
    // If we didn't find a suitable block in the Preferred list, check the
    // general Ready list.
    if (!Next) {
      // If there are no more blocks to process, we're done.
      if (Ready.empty()) {
        MaybeUpdateTerminator(MBB);
        break;
      }
      for (;;) {
        Next = Ready.top();
        Ready.pop();
        // If Next isn't dominated by the top active region header, defer it
        // until that region is done.
        if (!Entries.empty() &&
            !MDT.dominates(Entries.back().TheRegion->getHeader(), Next)) {
          Entries.back().Deferred.push_back(Next);
          continue;
        }
        break;
      }
    }
    // Move the next block into place and iterate.
    Next->moveAfter(MBB);
    MaybeUpdateTerminator(MBB);
    MBB = Next;
  }
  assert(Entries.empty() && "Active sort region list not finished");
  MF.RenumberBlocks();

#ifndef NDEBUG
  SmallSetVector<const Region *, 8> OnStack;

  // Insert a sentinel representing the degenerate loop that starts at the
  // function entry block and includes the entire function as a "loop" that
  // executes once.
  OnStack.insert(nullptr);

  for (auto &MBB : MF) {
    assert(MBB.getNumber() >= 0 && "Renumbered blocks should be non-negative.");
    const Region *Region = SUI.getRegionFor(&MBB);

    if (Region && &MBB == Region->getHeader()) {
      if (Region->isLoop()) {
        // Loop header. The loop predecessor should be sorted above, and the
        // other predecessors should be backedges below.
        for (auto Pred : MBB.predecessors())
          assert(
              (Pred->getNumber() < MBB.getNumber() || Region->contains(Pred)) &&
              "Loop header predecessors must be loop predecessors or "
              "backedges");
      } else {
        // Not a loop header. All predecessors should be sorted above.
        for (auto Pred : MBB.predecessors())
          assert(Pred->getNumber() < MBB.getNumber() &&
                 "Non-loop-header predecessors should be topologically sorted");
      }
      assert(OnStack.insert(Region) &&
             "Regions should be declared at most once.");

    } else {
      // Not a loop header. All predecessors should be sorted above.
      for (auto Pred : MBB.predecessors())
        assert(Pred->getNumber() < MBB.getNumber() &&
               "Non-loop-header predecessors should be topologically sorted");
      assert(OnStack.count(SUI.getRegionFor(&MBB)) &&
             "Blocks must be nested in their regions");
    }
    while (OnStack.size() > 1 && &MBB == WebAssembly::getBottom(OnStack.back()))
      OnStack.pop_back();
  }
  assert(OnStack.pop_back_val() == nullptr &&
         "The function entry block shouldn't actually be a region header");
  assert(OnStack.empty() &&
         "Control flow stack pushes and pops should be balanced.");
#endif
}

bool WebAssemblyCFGSort::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** CFG Sorting **********\n"
                       "********** Function: "
                    << MF.getName() << '\n');

  const auto &MLI = getAnalysis<MachineLoopInfo>();
  const auto &WEI = getAnalysis<WebAssemblyExceptionInfo>();
  auto &MDT = getAnalysis<MachineDominatorTree>();
  // Liveness is not tracked for VALUE_STACK physreg.
  MF.getRegInfo().invalidateLiveness();

  // Sort the blocks, with contiguous sort regions.
  SortBlocks(MF, MLI, WEI, MDT);

  return true;
}
