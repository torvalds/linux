//===- llvm/Analysis/OrderedBasicBlock.h --------------------- -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the OrderedBasicBlock class. OrderedBasicBlock maintains
// an interface where clients can query if one instruction comes before another
// in a BasicBlock. Since BasicBlock currently lacks a reliable way to query
// relative position between instructions one can use OrderedBasicBlock to do
// such queries. OrderedBasicBlock is lazily built on a source BasicBlock and
// maintains an internal Instruction -> Position map. A OrderedBasicBlock
// instance should be discarded whenever the source BasicBlock changes.
//
// It's currently used by the CaptureTracker in order to find relative
// positions of a pair of instructions inside a BasicBlock.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_ORDEREDBASICBLOCK_H
#define LLVM_ANALYSIS_ORDEREDBASICBLOCK_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/BasicBlock.h"

namespace llvm {

class Instruction;
class BasicBlock;

class OrderedBasicBlock {
private:
  /// Map a instruction to its position in a BasicBlock.
  SmallDenseMap<const Instruction *, unsigned, 32> NumberedInsts;

  /// Keep track of last instruction inserted into \p NumberedInsts.
  /// It speeds up queries for uncached instructions by providing a start point
  /// for new queries in OrderedBasicBlock::comesBefore.
  BasicBlock::const_iterator LastInstFound;

  /// The position/number to tag the next instruction to be found.
  unsigned NextInstPos;

  /// The source BasicBlock to map.
  const BasicBlock *BB;

  /// Given no cached results, find if \p A comes before \p B in \p BB.
  /// Cache and number out instruction while walking \p BB.
  bool comesBefore(const Instruction *A, const Instruction *B);

public:
  OrderedBasicBlock(const BasicBlock *BasicB);

  /// Find out whether \p A dominates \p B, meaning whether \p A
  /// comes before \p B in \p BB. This is a simplification that considers
  /// cached instruction positions and ignores other basic blocks, being
  /// only relevant to compare relative instructions positions inside \p BB.
  /// Returns false for A == B.
  bool dominates(const Instruction *A, const Instruction *B);
};

} // End llvm namespace

#endif
