//=- MachineBranchProbabilityInfo.h - Branch Probability Analysis -*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass is used to evaluate branch probabilties on machine basic blocks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEBRANCHPROBABILITYINFO_H
#define LLVM_CODEGEN_MACHINEBRANCHPROBABILITYINFO_H

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/Pass.h"
#include "llvm/Support/BranchProbability.h"
#include <climits>
#include <numeric>

namespace llvm {

class MachineBranchProbabilityInfo : public ImmutablePass {
  virtual void anchor();

  // Default weight value. Used when we don't have information about the edge.
  // TODO: DEFAULT_WEIGHT makes sense during static predication, when none of
  // the successors have a weight yet. But it doesn't make sense when providing
  // weight to an edge that may have siblings with non-zero weights. This can
  // be handled various ways, but it's probably fine for an edge with unknown
  // weight to just "inherit" the non-zero weight of an adjacent successor.
  static const uint32_t DEFAULT_WEIGHT = 16;

public:
  static char ID;

  MachineBranchProbabilityInfo() : ImmutablePass(ID) {
    PassRegistry &Registry = *PassRegistry::getPassRegistry();
    initializeMachineBranchProbabilityInfoPass(Registry);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  // Return edge probability.
  BranchProbability getEdgeProbability(const MachineBasicBlock *Src,
                                       const MachineBasicBlock *Dst) const;

  // Same as above, but using a const_succ_iterator from Src. This is faster
  // when the iterator is already available.
  BranchProbability
  getEdgeProbability(const MachineBasicBlock *Src,
                     MachineBasicBlock::const_succ_iterator Dst) const;

  // A 'Hot' edge is an edge which probability is >= 80%.
  bool isEdgeHot(const MachineBasicBlock *Src,
                 const MachineBasicBlock *Dst) const;

  // Return a hot successor for the block BB or null if there isn't one.
  // NB: This routine's complexity is linear on the number of successors.
  MachineBasicBlock *getHotSucc(MachineBasicBlock *MBB) const;

  // Print value between 0 (0% probability) and 1 (100% probability),
  // however the value is never equal to 0, and can be 1 only iff SRC block
  // has only one successor.
  raw_ostream &printEdgeProbability(raw_ostream &OS,
                                    const MachineBasicBlock *Src,
                                    const MachineBasicBlock *Dst) const;
};

}


#endif
