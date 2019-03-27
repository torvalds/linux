//===- PredIteratorCache.h - pred_iterator Cache ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the PredIteratorCache class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_PREDITERATORCACHE_H
#define LLVM_IR_PREDITERATORCACHE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/CFG.h"
#include "llvm/Support/Allocator.h"

namespace llvm {

/// PredIteratorCache - This class is an extremely trivial cache for
/// predecessor iterator queries.  This is useful for code that repeatedly
/// wants the predecessor list for the same blocks.
class PredIteratorCache {
  /// BlockToPredsMap - Pointer to null-terminated list.
  mutable DenseMap<BasicBlock *, BasicBlock **> BlockToPredsMap;
  mutable DenseMap<BasicBlock *, unsigned> BlockToPredCountMap;

  /// Memory - This is the space that holds cached preds.
  BumpPtrAllocator Memory;

private:
  /// GetPreds - Get a cached list for the null-terminated predecessor list of
  /// the specified block.  This can be used in a loop like this:
  ///   for (BasicBlock **PI = PredCache->GetPreds(BB); *PI; ++PI)
  ///      use(*PI);
  /// instead of:
  /// for (pred_iterator PI = pred_begin(BB), E = pred_end(BB); PI != E; ++PI)
  BasicBlock **GetPreds(BasicBlock *BB) {
    BasicBlock **&Entry = BlockToPredsMap[BB];
    if (Entry)
      return Entry;

    SmallVector<BasicBlock *, 32> PredCache(pred_begin(BB), pred_end(BB));
    PredCache.push_back(nullptr); // null terminator.

    BlockToPredCountMap[BB] = PredCache.size() - 1;

    Entry = Memory.Allocate<BasicBlock *>(PredCache.size());
    std::copy(PredCache.begin(), PredCache.end(), Entry);
    return Entry;
  }

  unsigned GetNumPreds(BasicBlock *BB) const {
    auto Result = BlockToPredCountMap.find(BB);
    if (Result != BlockToPredCountMap.end())
      return Result->second;
    return BlockToPredCountMap[BB] = std::distance(pred_begin(BB), pred_end(BB));
  }

public:
  size_t size(BasicBlock *BB) const { return GetNumPreds(BB); }
  ArrayRef<BasicBlock *> get(BasicBlock *BB) {
    return makeArrayRef(GetPreds(BB), GetNumPreds(BB));
  }

  /// clear - Remove all information.
  void clear() {
    BlockToPredsMap.clear();
    BlockToPredCountMap.clear();
    Memory.Reset();
  }
};

} // end namespace llvm

#endif
