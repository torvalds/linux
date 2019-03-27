//===- OrderedBasicBlock.cpp --------------------------------- -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the OrderedBasicBlock class. OrderedBasicBlock
// maintains an interface where clients can query if one instruction comes
// before another in a BasicBlock. Since BasicBlock currently lacks a reliable
// way to query relative position between instructions one can use
// OrderedBasicBlock to do such queries. OrderedBasicBlock is lazily built on a
// source BasicBlock and maintains an internal Instruction -> Position map. A
// OrderedBasicBlock instance should be discarded whenever the source
// BasicBlock changes.
//
// It's currently used by the CaptureTracker in order to find relative
// positions of a pair of instructions inside a BasicBlock.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/OrderedBasicBlock.h"
#include "llvm/IR/Instruction.h"
using namespace llvm;

OrderedBasicBlock::OrderedBasicBlock(const BasicBlock *BasicB)
    : NextInstPos(0), BB(BasicB) {
  LastInstFound = BB->end();
}

/// Given no cached results, find if \p A comes before \p B in \p BB.
/// Cache and number out instruction while walking \p BB.
bool OrderedBasicBlock::comesBefore(const Instruction *A,
                                    const Instruction *B) {
  const Instruction *Inst = nullptr;
  assert(!(LastInstFound == BB->end() && NextInstPos != 0) &&
         "Instruction supposed to be in NumberedInsts");
  assert(A->getParent() == BB && "Instruction supposed to be in the block!");
  assert(B->getParent() == BB && "Instruction supposed to be in the block!");

  // Start the search with the instruction found in the last lookup round.
  auto II = BB->begin();
  auto IE = BB->end();
  if (LastInstFound != IE)
    II = std::next(LastInstFound);

  // Number all instructions up to the point where we find 'A' or 'B'.
  for (; II != IE; ++II) {
    Inst = cast<Instruction>(II);
    NumberedInsts[Inst] = NextInstPos++;
    if (Inst == A || Inst == B)
      break;
  }

  assert(II != IE && "Instruction not found?");
  assert((Inst == A || Inst == B) && "Should find A or B");
  LastInstFound = II;
  return Inst != B;
}

/// Find out whether \p A dominates \p B, meaning whether \p A
/// comes before \p B in \p BB. This is a simplification that considers
/// cached instruction positions and ignores other basic blocks, being
/// only relevant to compare relative instructions positions inside \p BB.
bool OrderedBasicBlock::dominates(const Instruction *A, const Instruction *B) {
  assert(A->getParent() == B->getParent() &&
         "Instructions must be in the same basic block!");
  assert(A->getParent() == BB && "Instructions must be in the tracked block!");

  // First we lookup the instructions. If they don't exist, lookup will give us
  // back ::end(). If they both exist, we compare the numbers. Otherwise, if NA
  // exists and NB doesn't, it means NA must come before NB because we would
  // have numbered NB as well if it didn't. The same is true for NB. If it
  // exists, but NA does not, NA must come after it. If neither exist, we need
  // to number the block and cache the results (by calling comesBefore).
  auto NAI = NumberedInsts.find(A);
  auto NBI = NumberedInsts.find(B);
  if (NAI != NumberedInsts.end() && NBI != NumberedInsts.end())
    return NAI->second < NBI->second;
  if (NAI != NumberedInsts.end())
    return true;
  if (NBI != NumberedInsts.end())
    return false;

  return comesBefore(A, B);
}
