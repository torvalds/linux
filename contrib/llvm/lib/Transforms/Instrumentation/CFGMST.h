//===-- CFGMST.h - Minimum Spanning Tree for CFG ----------------*- C++ -*-===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a Union-find algorithm to compute Minimum Spanning Tree
// for a given CFG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TRANSFORMS_INSTRUMENTATION_CFGMST_H
#define LLVM_LIB_TRANSFORMS_INSTRUMENTATION_CFGMST_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <utility>
#include <vector>

#define DEBUG_TYPE "cfgmst"

namespace llvm {

/// An union-find based Minimum Spanning Tree for CFG
///
/// Implements a Union-find algorithm to compute Minimum Spanning Tree
/// for a given CFG.
template <class Edge, class BBInfo> class CFGMST {
public:
  Function &F;

  // Store all the edges in CFG. It may contain some stale edges
  // when Removed is set.
  std::vector<std::unique_ptr<Edge>> AllEdges;

  // This map records the auxiliary information for each BB.
  DenseMap<const BasicBlock *, std::unique_ptr<BBInfo>> BBInfos;

  // Whehter the function has an exit block with no successors.
  // (For function with an infinite loop, this block may be absent)
  bool ExitBlockFound = false;

  // Find the root group of the G and compress the path from G to the root.
  BBInfo *findAndCompressGroup(BBInfo *G) {
    if (G->Group != G)
      G->Group = findAndCompressGroup(static_cast<BBInfo *>(G->Group));
    return static_cast<BBInfo *>(G->Group);
  }

  // Union BB1 and BB2 into the same group and return true.
  // Returns false if BB1 and BB2 are already in the same group.
  bool unionGroups(const BasicBlock *BB1, const BasicBlock *BB2) {
    BBInfo *BB1G = findAndCompressGroup(&getBBInfo(BB1));
    BBInfo *BB2G = findAndCompressGroup(&getBBInfo(BB2));

    if (BB1G == BB2G)
      return false;

    // Make the smaller rank tree a direct child or the root of high rank tree.
    if (BB1G->Rank < BB2G->Rank)
      BB1G->Group = BB2G;
    else {
      BB2G->Group = BB1G;
      // If the ranks are the same, increment root of one tree by one.
      if (BB1G->Rank == BB2G->Rank)
        BB1G->Rank++;
    }
    return true;
  }

  // Give BB, return the auxiliary information.
  BBInfo &getBBInfo(const BasicBlock *BB) const {
    auto It = BBInfos.find(BB);
    assert(It->second.get() != nullptr);
    return *It->second.get();
  }

  // Give BB, return the auxiliary information if it's available.
  BBInfo *findBBInfo(const BasicBlock *BB) const {
    auto It = BBInfos.find(BB);
    if (It == BBInfos.end())
      return nullptr;
    return It->second.get();
  }

  // Traverse the CFG using a stack. Find all the edges and assign the weight.
  // Edges with large weight will be put into MST first so they are less likely
  // to be instrumented.
  void buildEdges() {
    LLVM_DEBUG(dbgs() << "Build Edge on " << F.getName() << "\n");

    const BasicBlock *Entry = &(F.getEntryBlock());
    uint64_t EntryWeight = (BFI != nullptr ? BFI->getEntryFreq() : 2);
    Edge *EntryIncoming = nullptr, *EntryOutgoing = nullptr,
        *ExitOutgoing = nullptr, *ExitIncoming = nullptr;
    uint64_t MaxEntryOutWeight = 0, MaxExitOutWeight = 0, MaxExitInWeight = 0;

    // Add a fake edge to the entry.
    EntryIncoming = &addEdge(nullptr, Entry, EntryWeight);
    LLVM_DEBUG(dbgs() << "  Edge: from fake node to " << Entry->getName()
                      << " w = " << EntryWeight << "\n");

    // Special handling for single BB functions.
    if (succ_empty(Entry)) {
      addEdge(Entry, nullptr, EntryWeight);
      return;
    }

    static const uint32_t CriticalEdgeMultiplier = 1000;

    for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB) {
      Instruction *TI = BB->getTerminator();
      uint64_t BBWeight =
          (BFI != nullptr ? BFI->getBlockFreq(&*BB).getFrequency() : 2);
      uint64_t Weight = 2;
      if (int successors = TI->getNumSuccessors()) {
        for (int i = 0; i != successors; ++i) {
          BasicBlock *TargetBB = TI->getSuccessor(i);
          bool Critical = isCriticalEdge(TI, i);
          uint64_t scaleFactor = BBWeight;
          if (Critical) {
            if (scaleFactor < UINT64_MAX / CriticalEdgeMultiplier)
              scaleFactor *= CriticalEdgeMultiplier;
            else
              scaleFactor = UINT64_MAX;
          }
          if (BPI != nullptr)
            Weight = BPI->getEdgeProbability(&*BB, TargetBB).scale(scaleFactor);
          auto *E = &addEdge(&*BB, TargetBB, Weight);
          E->IsCritical = Critical;
          LLVM_DEBUG(dbgs() << "  Edge: from " << BB->getName() << " to "
                            << TargetBB->getName() << "  w=" << Weight << "\n");

          // Keep track of entry/exit edges:
          if (&*BB == Entry) {
            if (Weight > MaxEntryOutWeight) {
              MaxEntryOutWeight = Weight;
              EntryOutgoing = E;
            }
          }

          auto *TargetTI = TargetBB->getTerminator();
          if (TargetTI && !TargetTI->getNumSuccessors()) {
            if (Weight > MaxExitInWeight) {
              MaxExitInWeight = Weight;
              ExitIncoming = E;
            }
          }
        }
      } else {
        ExitBlockFound = true;
        Edge *ExitO = &addEdge(&*BB, nullptr, BBWeight);
        if (BBWeight > MaxExitOutWeight) {
          MaxExitOutWeight = BBWeight;
          ExitOutgoing = ExitO;
        }
        LLVM_DEBUG(dbgs() << "  Edge: from " << BB->getName() << " to fake exit"
                          << " w = " << BBWeight << "\n");
      }
    }

    // Entry/exit edge adjustment heurisitic:
    // prefer instrumenting entry edge over exit edge
    // if possible. Those exit edges may never have a chance to be
    // executed (for instance the program is an event handling loop)
    // before the profile is asynchronously dumped.
    //
    // If EntryIncoming and ExitOutgoing has similar weight, make sure
    // ExitOutging is selected as the min-edge. Similarly, if EntryOutgoing
    // and ExitIncoming has similar weight, make sure ExitIncoming becomes
    // the min-edge.
    uint64_t EntryInWeight = EntryWeight;

    if (EntryInWeight >= MaxExitOutWeight &&
        EntryInWeight * 2 < MaxExitOutWeight * 3) {
      EntryIncoming->Weight = MaxExitOutWeight;
      ExitOutgoing->Weight = EntryInWeight + 1;
    }

    if (MaxEntryOutWeight >= MaxExitInWeight &&
        MaxEntryOutWeight * 2 < MaxExitInWeight * 3) {
      EntryOutgoing->Weight = MaxExitInWeight;
      ExitIncoming->Weight = MaxEntryOutWeight + 1;
    }
  }

  // Sort CFG edges based on its weight.
  void sortEdgesByWeight() {
    std::stable_sort(AllEdges.begin(), AllEdges.end(),
                     [](const std::unique_ptr<Edge> &Edge1,
                        const std::unique_ptr<Edge> &Edge2) {
                       return Edge1->Weight > Edge2->Weight;
                     });
  }

  // Traverse all the edges and compute the Minimum Weight Spanning Tree
  // using union-find algorithm.
  void computeMinimumSpanningTree() {
    // First, put all the critical edge with landing-pad as the Dest to MST.
    // This works around the insufficient support of critical edges split
    // when destination BB is a landing pad.
    for (auto &Ei : AllEdges) {
      if (Ei->Removed)
        continue;
      if (Ei->IsCritical) {
        if (Ei->DestBB && Ei->DestBB->isLandingPad()) {
          if (unionGroups(Ei->SrcBB, Ei->DestBB))
            Ei->InMST = true;
        }
      }
    }

    for (auto &Ei : AllEdges) {
      if (Ei->Removed)
        continue;
      // If we detect infinite loops, force
      // instrumenting the entry edge:
      if (!ExitBlockFound && Ei->SrcBB == nullptr)
        continue;
      if (unionGroups(Ei->SrcBB, Ei->DestBB))
        Ei->InMST = true;
    }
  }

  // Dump the Debug information about the instrumentation.
  void dumpEdges(raw_ostream &OS, const Twine &Message) const {
    if (!Message.str().empty())
      OS << Message << "\n";
    OS << "  Number of Basic Blocks: " << BBInfos.size() << "\n";
    for (auto &BI : BBInfos) {
      const BasicBlock *BB = BI.first;
      OS << "  BB: " << (BB == nullptr ? "FakeNode" : BB->getName()) << "  "
         << BI.second->infoString() << "\n";
    }

    OS << "  Number of Edges: " << AllEdges.size()
       << " (*: Instrument, C: CriticalEdge, -: Removed)\n";
    uint32_t Count = 0;
    for (auto &EI : AllEdges)
      OS << "  Edge " << Count++ << ": " << getBBInfo(EI->SrcBB).Index << "-->"
         << getBBInfo(EI->DestBB).Index << EI->infoString() << "\n";
  }

  // Add an edge to AllEdges with weight W.
  Edge &addEdge(const BasicBlock *Src, const BasicBlock *Dest, uint64_t W) {
    uint32_t Index = BBInfos.size();
    auto Iter = BBInfos.end();
    bool Inserted;
    std::tie(Iter, Inserted) = BBInfos.insert(std::make_pair(Src, nullptr));
    if (Inserted) {
      // Newly inserted, update the real info.
      Iter->second = std::move(llvm::make_unique<BBInfo>(Index));
      Index++;
    }
    std::tie(Iter, Inserted) = BBInfos.insert(std::make_pair(Dest, nullptr));
    if (Inserted)
      // Newly inserted, update the real info.
      Iter->second = std::move(llvm::make_unique<BBInfo>(Index));
    AllEdges.emplace_back(new Edge(Src, Dest, W));
    return *AllEdges.back();
  }

  BranchProbabilityInfo *BPI;
  BlockFrequencyInfo *BFI;

public:
  CFGMST(Function &Func, BranchProbabilityInfo *BPI_ = nullptr,
         BlockFrequencyInfo *BFI_ = nullptr)
      : F(Func), BPI(BPI_), BFI(BFI_) {
    buildEdges();
    sortEdgesByWeight();
    computeMinimumSpanningTree();
  }
};

} // end namespace llvm

#undef DEBUG_TYPE // "cfgmst"

#endif // LLVM_LIB_TRANSFORMS_INSTRUMENTATION_CFGMST_H
