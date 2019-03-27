//==- BlockCounter.h - ADT for counting block visits -------------*- C++ -*-//
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

#include "clang/StaticAnalyzer/Core/PathSensitive/BlockCounter.h"
#include "llvm/ADT/ImmutableMap.h"

using namespace clang;
using namespace ento;

namespace {

class CountKey {
  const StackFrameContext *CallSite;
  unsigned BlockID;

public:
  CountKey(const StackFrameContext *CS, unsigned ID)
    : CallSite(CS), BlockID(ID) {}

  bool operator==(const CountKey &RHS) const {
    return (CallSite == RHS.CallSite) && (BlockID == RHS.BlockID);
  }

  bool operator<(const CountKey &RHS) const {
    return std::tie(CallSite, BlockID) < std::tie(RHS.CallSite, RHS.BlockID);
  }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddPointer(CallSite);
    ID.AddInteger(BlockID);
  }
};

}

typedef llvm::ImmutableMap<CountKey, unsigned> CountMap;

static inline CountMap GetMap(void *D) {
  return CountMap(static_cast<CountMap::TreeTy*>(D));
}

static inline CountMap::Factory& GetFactory(void *F) {
  return *static_cast<CountMap::Factory*>(F);
}

unsigned BlockCounter::getNumVisited(const StackFrameContext *CallSite,
                                       unsigned BlockID) const {
  CountMap M = GetMap(Data);
  CountMap::data_type* T = M.lookup(CountKey(CallSite, BlockID));
  return T ? *T : 0;
}

BlockCounter::Factory::Factory(llvm::BumpPtrAllocator& Alloc) {
  F = new CountMap::Factory(Alloc);
}

BlockCounter::Factory::~Factory() {
  delete static_cast<CountMap::Factory*>(F);
}

BlockCounter
BlockCounter::Factory::IncrementCount(BlockCounter BC,
                                        const StackFrameContext *CallSite,
                                        unsigned BlockID) {
  return BlockCounter(GetFactory(F).add(GetMap(BC.Data),
                                          CountKey(CallSite, BlockID),
                             BC.getNumVisited(CallSite, BlockID)+1).getRoot());
}

BlockCounter
BlockCounter::Factory::GetEmptyCounter() {
  return BlockCounter(GetFactory(F).getEmptyMap().getRoot());
}
