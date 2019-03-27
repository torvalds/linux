//===- MachineBranchProbabilityInfo.cpp - Machine Branch Probability Info -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This analysis uses probability info stored in Machine Basic Blocks.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

INITIALIZE_PASS_BEGIN(MachineBranchProbabilityInfo, "machine-branch-prob",
                      "Machine Branch Probability Analysis", false, true)
INITIALIZE_PASS_END(MachineBranchProbabilityInfo, "machine-branch-prob",
                    "Machine Branch Probability Analysis", false, true)

cl::opt<unsigned>
    StaticLikelyProb("static-likely-prob",
                     cl::desc("branch probability threshold in percentage"
                              "to be considered very likely"),
                     cl::init(80), cl::Hidden);

cl::opt<unsigned> ProfileLikelyProb(
    "profile-likely-prob",
    cl::desc("branch probability threshold in percentage to be considered"
             " very likely when profile is available"),
    cl::init(51), cl::Hidden);

char MachineBranchProbabilityInfo::ID = 0;

void MachineBranchProbabilityInfo::anchor() {}

BranchProbability MachineBranchProbabilityInfo::getEdgeProbability(
    const MachineBasicBlock *Src,
    MachineBasicBlock::const_succ_iterator Dst) const {
  return Src->getSuccProbability(Dst);
}

BranchProbability MachineBranchProbabilityInfo::getEdgeProbability(
    const MachineBasicBlock *Src, const MachineBasicBlock *Dst) const {
  // This is a linear search. Try to use the const_succ_iterator version when
  // possible.
  return getEdgeProbability(Src, find(Src->successors(), Dst));
}

bool MachineBranchProbabilityInfo::isEdgeHot(
    const MachineBasicBlock *Src, const MachineBasicBlock *Dst) const {
  BranchProbability HotProb(StaticLikelyProb, 100);
  return getEdgeProbability(Src, Dst) > HotProb;
}

MachineBasicBlock *
MachineBranchProbabilityInfo::getHotSucc(MachineBasicBlock *MBB) const {
  auto MaxProb = BranchProbability::getZero();
  MachineBasicBlock *MaxSucc = nullptr;
  for (MachineBasicBlock::const_succ_iterator I = MBB->succ_begin(),
       E = MBB->succ_end(); I != E; ++I) {
    auto Prob = getEdgeProbability(MBB, I);
    if (Prob > MaxProb) {
      MaxProb = Prob;
      MaxSucc = *I;
    }
  }

  BranchProbability HotProb(StaticLikelyProb, 100);
  if (getEdgeProbability(MBB, MaxSucc) >= HotProb)
    return MaxSucc;

  return nullptr;
}

raw_ostream &MachineBranchProbabilityInfo::printEdgeProbability(
    raw_ostream &OS, const MachineBasicBlock *Src,
    const MachineBasicBlock *Dst) const {

  const BranchProbability Prob = getEdgeProbability(Src, Dst);
  OS << "edge " << printMBBReference(*Src) << " -> " << printMBBReference(*Dst)
     << " probability is " << Prob
     << (isEdgeHot(Src, Dst) ? " [HOT edge]\n" : "\n");

  return OS;
}
