//==- BlockCounter.h - ADT for counting block visits ---------------*- C++ -*-//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines BlockCounter, an abstract data type used to count
//  the number of times a given block has been visited along a path
//  analyzed by CoreEngine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_BLOCKCOUNTER_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_BLOCKCOUNTER_H

#include "llvm/Support/Allocator.h"

namespace clang {

class StackFrameContext;

namespace ento {

/// \class BlockCounter
/// An abstract data type used to count the number of times a given
/// block has been visited along a path analyzed by CoreEngine.
class BlockCounter {
  void *Data;

  BlockCounter(void *D) : Data(D) {}

public:
  BlockCounter() : Data(nullptr) {}

  unsigned getNumVisited(const StackFrameContext *CallSite,
                         unsigned BlockID) const;

  class Factory {
    void *F;
  public:
    Factory(llvm::BumpPtrAllocator& Alloc);
    ~Factory();

    BlockCounter GetEmptyCounter();
    BlockCounter IncrementCount(BlockCounter BC,
                                  const StackFrameContext *CallSite,
                                  unsigned BlockID);
  };

  friend class Factory;
};

} // end GR namespace

} // end clang namespace

#endif
