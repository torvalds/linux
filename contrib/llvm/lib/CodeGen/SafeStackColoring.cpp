//===- SafeStackColoring.cpp - SafeStack frame coloring -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SafeStackColoring.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/User.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <tuple>
#include <utility>

using namespace llvm;
using namespace llvm::safestack;

#define DEBUG_TYPE "safestackcoloring"

// Disabled by default due to PR32143.
static cl::opt<bool> ClColoring("safe-stack-coloring",
                                cl::desc("enable safe stack coloring"),
                                cl::Hidden, cl::init(false));

const StackColoring::LiveRange &StackColoring::getLiveRange(AllocaInst *AI) {
  const auto IT = AllocaNumbering.find(AI);
  assert(IT != AllocaNumbering.end());
  return LiveRanges[IT->second];
}

bool StackColoring::readMarker(Instruction *I, bool *IsStart) {
  if (!I->isLifetimeStartOrEnd())
    return false;

  auto *II = cast<IntrinsicInst>(I);
  *IsStart = II->getIntrinsicID() == Intrinsic::lifetime_start;
  return true;
}

void StackColoring::removeAllMarkers() {
  for (auto *I : Markers) {
    auto *Op = dyn_cast<Instruction>(I->getOperand(1));
    I->eraseFromParent();
    // Remove the operand bitcast, too, if it has no more uses left.
    if (Op && Op->use_empty())
      Op->eraseFromParent();
  }
}

void StackColoring::collectMarkers() {
  InterestingAllocas.resize(NumAllocas);
  DenseMap<BasicBlock *, SmallDenseMap<Instruction *, Marker>> BBMarkerSet;

  // Compute the set of start/end markers per basic block.
  for (unsigned AllocaNo = 0; AllocaNo < NumAllocas; ++AllocaNo) {
    AllocaInst *AI = Allocas[AllocaNo];
    SmallVector<Instruction *, 8> WorkList;
    WorkList.push_back(AI);
    while (!WorkList.empty()) {
      Instruction *I = WorkList.pop_back_val();
      for (User *U : I->users()) {
        if (auto *BI = dyn_cast<BitCastInst>(U)) {
          WorkList.push_back(BI);
          continue;
        }
        auto *UI = dyn_cast<Instruction>(U);
        if (!UI)
          continue;
        bool IsStart;
        if (!readMarker(UI, &IsStart))
          continue;
        if (IsStart)
          InterestingAllocas.set(AllocaNo);
        BBMarkerSet[UI->getParent()][UI] = {AllocaNo, IsStart};
        Markers.push_back(UI);
      }
    }
  }

  // Compute instruction numbering. Only the following instructions are
  // considered:
  // * Basic block entries
  // * Lifetime markers
  // For each basic block, compute
  // * the list of markers in the instruction order
  // * the sets of allocas whose lifetime starts or ends in this BB
  LLVM_DEBUG(dbgs() << "Instructions:\n");
  unsigned InstNo = 0;
  for (BasicBlock *BB : depth_first(&F)) {
    LLVM_DEBUG(dbgs() << "  " << InstNo << ": BB " << BB->getName() << "\n");
    unsigned BBStart = InstNo++;

    BlockLifetimeInfo &BlockInfo = BlockLiveness[BB];
    BlockInfo.Begin.resize(NumAllocas);
    BlockInfo.End.resize(NumAllocas);
    BlockInfo.LiveIn.resize(NumAllocas);
    BlockInfo.LiveOut.resize(NumAllocas);

    auto &BlockMarkerSet = BBMarkerSet[BB];
    if (BlockMarkerSet.empty()) {
      unsigned BBEnd = InstNo;
      BlockInstRange[BB] = std::make_pair(BBStart, BBEnd);
      continue;
    }

    auto ProcessMarker = [&](Instruction *I, const Marker &M) {
      LLVM_DEBUG(dbgs() << "  " << InstNo << ":  "
                        << (M.IsStart ? "start " : "end   ") << M.AllocaNo
                        << ", " << *I << "\n");

      BBMarkers[BB].push_back({InstNo, M});

      InstructionNumbering[I] = InstNo++;

      if (M.IsStart) {
        if (BlockInfo.End.test(M.AllocaNo))
          BlockInfo.End.reset(M.AllocaNo);
        BlockInfo.Begin.set(M.AllocaNo);
      } else {
        if (BlockInfo.Begin.test(M.AllocaNo))
          BlockInfo.Begin.reset(M.AllocaNo);
        BlockInfo.End.set(M.AllocaNo);
      }
    };

    if (BlockMarkerSet.size() == 1) {
      ProcessMarker(BlockMarkerSet.begin()->getFirst(),
                    BlockMarkerSet.begin()->getSecond());
    } else {
      // Scan the BB to determine the marker order.
      for (Instruction &I : *BB) {
        auto It = BlockMarkerSet.find(&I);
        if (It == BlockMarkerSet.end())
          continue;
        ProcessMarker(&I, It->getSecond());
      }
    }

    unsigned BBEnd = InstNo;
    BlockInstRange[BB] = std::make_pair(BBStart, BBEnd);
  }
  NumInst = InstNo;
}

void StackColoring::calculateLocalLiveness() {
  bool changed = true;
  while (changed) {
    changed = false;

    for (BasicBlock *BB : depth_first(&F)) {
      BlockLifetimeInfo &BlockInfo = BlockLiveness[BB];

      // Compute LiveIn by unioning together the LiveOut sets of all preds.
      BitVector LocalLiveIn;
      for (auto *PredBB : predecessors(BB)) {
        LivenessMap::const_iterator I = BlockLiveness.find(PredBB);
        // If a predecessor is unreachable, ignore it.
        if (I == BlockLiveness.end())
          continue;
        LocalLiveIn |= I->second.LiveOut;
      }

      // Compute LiveOut by subtracting out lifetimes that end in this
      // block, then adding in lifetimes that begin in this block.  If
      // we have both BEGIN and END markers in the same basic block
      // then we know that the BEGIN marker comes after the END,
      // because we already handle the case where the BEGIN comes
      // before the END when collecting the markers (and building the
      // BEGIN/END vectors).
      BitVector LocalLiveOut = LocalLiveIn;
      LocalLiveOut.reset(BlockInfo.End);
      LocalLiveOut |= BlockInfo.Begin;

      // Update block LiveIn set, noting whether it has changed.
      if (LocalLiveIn.test(BlockInfo.LiveIn)) {
        changed = true;
        BlockInfo.LiveIn |= LocalLiveIn;
      }

      // Update block LiveOut set, noting whether it has changed.
      if (LocalLiveOut.test(BlockInfo.LiveOut)) {
        changed = true;
        BlockInfo.LiveOut |= LocalLiveOut;
      }
    }
  } // while changed.
}

void StackColoring::calculateLiveIntervals() {
  for (auto IT : BlockLiveness) {
    BasicBlock *BB = IT.getFirst();
    BlockLifetimeInfo &BlockInfo = IT.getSecond();
    unsigned BBStart, BBEnd;
    std::tie(BBStart, BBEnd) = BlockInstRange[BB];

    BitVector Started, Ended;
    Started.resize(NumAllocas);
    Ended.resize(NumAllocas);
    SmallVector<unsigned, 8> Start;
    Start.resize(NumAllocas);

    // LiveIn ranges start at the first instruction.
    for (unsigned AllocaNo = 0; AllocaNo < NumAllocas; ++AllocaNo) {
      if (BlockInfo.LiveIn.test(AllocaNo)) {
        Started.set(AllocaNo);
        Start[AllocaNo] = BBStart;
      }
    }

    for (auto &It : BBMarkers[BB]) {
      unsigned InstNo = It.first;
      bool IsStart = It.second.IsStart;
      unsigned AllocaNo = It.second.AllocaNo;

      if (IsStart) {
        assert(!Started.test(AllocaNo) || Start[AllocaNo] == BBStart);
        if (!Started.test(AllocaNo)) {
          Started.set(AllocaNo);
          Ended.reset(AllocaNo);
          Start[AllocaNo] = InstNo;
        }
      } else {
        assert(!Ended.test(AllocaNo));
        if (Started.test(AllocaNo)) {
          LiveRanges[AllocaNo].AddRange(Start[AllocaNo], InstNo);
          Started.reset(AllocaNo);
        }
        Ended.set(AllocaNo);
      }
    }

    for (unsigned AllocaNo = 0; AllocaNo < NumAllocas; ++AllocaNo)
      if (Started.test(AllocaNo))
        LiveRanges[AllocaNo].AddRange(Start[AllocaNo], BBEnd);
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void StackColoring::dumpAllocas() {
  dbgs() << "Allocas:\n";
  for (unsigned AllocaNo = 0; AllocaNo < NumAllocas; ++AllocaNo)
    dbgs() << "  " << AllocaNo << ": " << *Allocas[AllocaNo] << "\n";
}

LLVM_DUMP_METHOD void StackColoring::dumpBlockLiveness() {
  dbgs() << "Block liveness:\n";
  for (auto IT : BlockLiveness) {
    BasicBlock *BB = IT.getFirst();
    BlockLifetimeInfo &BlockInfo = BlockLiveness[BB];
    auto BlockRange = BlockInstRange[BB];
    dbgs() << "  BB [" << BlockRange.first << ", " << BlockRange.second
           << "): begin " << BlockInfo.Begin << ", end " << BlockInfo.End
           << ", livein " << BlockInfo.LiveIn << ", liveout "
           << BlockInfo.LiveOut << "\n";
  }
}

LLVM_DUMP_METHOD void StackColoring::dumpLiveRanges() {
  dbgs() << "Alloca liveness:\n";
  for (unsigned AllocaNo = 0; AllocaNo < NumAllocas; ++AllocaNo) {
    LiveRange &Range = LiveRanges[AllocaNo];
    dbgs() << "  " << AllocaNo << ": " << Range << "\n";
  }
}
#endif

void StackColoring::run() {
  LLVM_DEBUG(dumpAllocas());

  for (unsigned I = 0; I < NumAllocas; ++I)
    AllocaNumbering[Allocas[I]] = I;
  LiveRanges.resize(NumAllocas);

  collectMarkers();

  if (!ClColoring) {
    for (auto &R : LiveRanges) {
      R.SetMaximum(1);
      R.AddRange(0, 1);
    }
    return;
  }

  for (auto &R : LiveRanges)
    R.SetMaximum(NumInst);
  for (unsigned I = 0; I < NumAllocas; ++I)
    if (!InterestingAllocas.test(I))
      LiveRanges[I] = getFullLiveRange();

  calculateLocalLiveness();
  LLVM_DEBUG(dumpBlockLiveness());
  calculateLiveIntervals();
  LLVM_DEBUG(dumpLiveRanges());
}
