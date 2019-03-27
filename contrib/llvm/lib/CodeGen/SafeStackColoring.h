//===- SafeStackColoring.h - SafeStack frame coloring ----------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_SAFESTACKCOLORING_H
#define LLVM_LIB_CODEGEN_SAFESTACKCOLORING_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <utility>

namespace llvm {

class BasicBlock;
class Function;
class Instruction;

namespace safestack {

/// Compute live ranges of allocas.
/// Live ranges are represented as sets of "interesting" instructions, which are
/// defined as instructions that may start or end an alloca's lifetime. These
/// are:
/// * lifetime.start and lifetime.end intrinsics
/// * first instruction of any basic block
/// Interesting instructions are numbered in the depth-first walk of the CFG,
/// and in the program order inside each basic block.
class StackColoring {
  /// A class representing liveness information for a single basic block.
  /// Each bit in the BitVector represents the liveness property
  /// for a different stack slot.
  struct BlockLifetimeInfo {
    /// Which slots BEGINs in each basic block.
    BitVector Begin;

    /// Which slots ENDs in each basic block.
    BitVector End;

    /// Which slots are marked as LIVE_IN, coming into each basic block.
    BitVector LiveIn;

    /// Which slots are marked as LIVE_OUT, coming out of each basic block.
    BitVector LiveOut;
  };

public:
  /// This class represents a set of interesting instructions where an alloca is
  /// live.
  struct LiveRange {
    BitVector bv;

    void SetMaximum(int size) { bv.resize(size); }
    void AddRange(unsigned start, unsigned end) { bv.set(start, end); }

    bool Overlaps(const LiveRange &Other) const {
      return bv.anyCommon(Other.bv);
    }

    void Join(const LiveRange &Other) { bv |= Other.bv; }
  };

private:
  Function &F;

  /// Maps active slots (per bit) for each basic block.
  using LivenessMap = DenseMap<BasicBlock *, BlockLifetimeInfo>;
  LivenessMap BlockLiveness;

  /// Number of interesting instructions.
  int NumInst = -1;

  /// Numeric ids for interesting instructions.
  DenseMap<Instruction *, unsigned> InstructionNumbering;

  /// A range [Start, End) of instruction ids for each basic block.
  /// Instructions inside each BB have monotonic and consecutive ids.
  DenseMap<const BasicBlock *, std::pair<unsigned, unsigned>> BlockInstRange;

  ArrayRef<AllocaInst *> Allocas;
  unsigned NumAllocas;
  DenseMap<AllocaInst *, unsigned> AllocaNumbering;

  /// LiveRange for allocas.
  SmallVector<LiveRange, 8> LiveRanges;

  /// The set of allocas that have at least one lifetime.start. All other
  /// allocas get LiveRange that corresponds to the entire function.
  BitVector InterestingAllocas;
  SmallVector<Instruction *, 8> Markers;

  struct Marker {
    unsigned AllocaNo;
    bool IsStart;
  };

  /// List of {InstNo, {AllocaNo, IsStart}} for each BB, ordered by InstNo.
  DenseMap<BasicBlock *, SmallVector<std::pair<unsigned, Marker>, 4>> BBMarkers;

  void dumpAllocas();
  void dumpBlockLiveness();
  void dumpLiveRanges();

  bool readMarker(Instruction *I, bool *IsStart);
  void collectMarkers();
  void calculateLocalLiveness();
  void calculateLiveIntervals();

public:
  StackColoring(Function &F, ArrayRef<AllocaInst *> Allocas)
      : F(F), Allocas(Allocas), NumAllocas(Allocas.size()) {}

  void run();
  void removeAllMarkers();

  /// Returns a set of "interesting" instructions where the given alloca is
  /// live. Not all instructions in a function are interesting: we pick a set
  /// that is large enough for LiveRange::Overlaps to be correct.
  const LiveRange &getLiveRange(AllocaInst *AI);

  /// Returns a live range that represents an alloca that is live throughout the
  /// entire function.
  LiveRange getFullLiveRange() {
    assert(NumInst >= 0);
    LiveRange R;
    R.SetMaximum(NumInst);
    R.AddRange(0, NumInst);
    return R;
  }
};

static inline raw_ostream &operator<<(raw_ostream &OS, const BitVector &V) {
  OS << "{";
  int idx = V.find_first();
  bool first = true;
  while (idx >= 0) {
    if (!first) {
      OS << ", ";
    }
    first = false;
    OS << idx;
    idx = V.find_next(idx);
  }
  OS << "}";
  return OS;
}

static inline raw_ostream &operator<<(raw_ostream &OS,
                                      const StackColoring::LiveRange &R) {
  return OS << R.bv;
}

} // end namespace safestack

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_SAFESTACKCOLORING_H
