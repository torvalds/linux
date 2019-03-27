//===----------------------- LSUnit.cpp --------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// A Load-Store Unit for the llvm-mca tool.
///
//===----------------------------------------------------------------------===//

#include "llvm/MCA/HardwareUnits/LSUnit.h"
#include "llvm/MCA/Instruction.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "llvm-mca"

namespace llvm {
namespace mca {

LSUnit::LSUnit(const MCSchedModel &SM, unsigned LQ, unsigned SQ,
               bool AssumeNoAlias)
    : LQ_Size(LQ), SQ_Size(SQ), NoAlias(AssumeNoAlias) {
  if (SM.hasExtraProcessorInfo()) {
    const MCExtraProcessorInfo &EPI = SM.getExtraProcessorInfo();
    if (!LQ_Size && EPI.LoadQueueID) {
      const MCProcResourceDesc &LdQDesc = *SM.getProcResource(EPI.LoadQueueID);
      LQ_Size = LdQDesc.BufferSize;
    }

    if (!SQ_Size && EPI.StoreQueueID) {
      const MCProcResourceDesc &StQDesc = *SM.getProcResource(EPI.StoreQueueID);
      SQ_Size = StQDesc.BufferSize;
    }
  }
}

#ifndef NDEBUG
void LSUnit::dump() const {
  dbgs() << "[LSUnit] LQ_Size = " << LQ_Size << '\n';
  dbgs() << "[LSUnit] SQ_Size = " << SQ_Size << '\n';
  dbgs() << "[LSUnit] NextLQSlotIdx = " << LoadQueue.size() << '\n';
  dbgs() << "[LSUnit] NextSQSlotIdx = " << StoreQueue.size() << '\n';
}
#endif

void LSUnit::assignLQSlot(unsigned Index) {
  assert(!isLQFull());
  assert(LoadQueue.count(Index) == 0);

  LLVM_DEBUG(dbgs() << "[LSUnit] - AssignLQSlot <Idx=" << Index
                    << ",slot=" << LoadQueue.size() << ">\n");
  LoadQueue.insert(Index);
}

void LSUnit::assignSQSlot(unsigned Index) {
  assert(!isSQFull());
  assert(StoreQueue.count(Index) == 0);

  LLVM_DEBUG(dbgs() << "[LSUnit] - AssignSQSlot <Idx=" << Index
                    << ",slot=" << StoreQueue.size() << ">\n");
  StoreQueue.insert(Index);
}

void LSUnit::dispatch(const InstRef &IR) {
  const InstrDesc &Desc = IR.getInstruction()->getDesc();
  unsigned IsMemBarrier = Desc.HasSideEffects;
  assert((Desc.MayLoad || Desc.MayStore) && "Not a memory operation!");

  const unsigned Index = IR.getSourceIndex();
  if (Desc.MayLoad) {
    if (IsMemBarrier)
      LoadBarriers.insert(Index);
    assignLQSlot(Index);
  }

  if (Desc.MayStore) {
    if (IsMemBarrier)
      StoreBarriers.insert(Index);
    assignSQSlot(Index);
  }
}

LSUnit::Status LSUnit::isAvailable(const InstRef &IR) const {
  const InstrDesc &Desc = IR.getInstruction()->getDesc();
  if (Desc.MayLoad && isLQFull())
    return LSUnit::LSU_LQUEUE_FULL;
  if (Desc.MayStore && isSQFull())
    return LSUnit::LSU_SQUEUE_FULL;
  return LSUnit::LSU_AVAILABLE;
}

bool LSUnit::isReady(const InstRef &IR) const {
  const InstrDesc &Desc = IR.getInstruction()->getDesc();
  const unsigned Index = IR.getSourceIndex();
  bool IsALoad = Desc.MayLoad;
  bool IsAStore = Desc.MayStore;
  assert((IsALoad || IsAStore) && "Not a memory operation!");
  assert((!IsALoad || LoadQueue.count(Index) == 1) && "Load not in queue!");
  assert((!IsAStore || StoreQueue.count(Index) == 1) && "Store not in queue!");

  if (IsALoad && !LoadBarriers.empty()) {
    unsigned LoadBarrierIndex = *LoadBarriers.begin();
    // A younger load cannot pass a older load barrier.
    if (Index > LoadBarrierIndex)
      return false;
    // A load barrier cannot pass a older load.
    if (Index == LoadBarrierIndex && Index != *LoadQueue.begin())
      return false;
  }

  if (IsAStore && !StoreBarriers.empty()) {
    unsigned StoreBarrierIndex = *StoreBarriers.begin();
    // A younger store cannot pass a older store barrier.
    if (Index > StoreBarrierIndex)
      return false;
    // A store barrier cannot pass a older store.
    if (Index == StoreBarrierIndex && Index != *StoreQueue.begin())
      return false;
  }

  // A load may not pass a previous store unless flag 'NoAlias' is set.
  // A load may pass a previous load.
  if (NoAlias && IsALoad)
    return true;

  if (StoreQueue.size()) {
    // A load may not pass a previous store.
    // A store may not pass a previous store.
    if (Index > *StoreQueue.begin())
      return false;
  }

  // Okay, we are older than the oldest store in the queue.
  // If there are no pending loads, then we can say for sure that this
  // instruction is ready.
  if (isLQEmpty())
    return true;

  // Check if there are no older loads.
  if (Index <= *LoadQueue.begin())
    return true;

  // There is at least one younger load.
  //
  // A store may not pass a previous load.
  // A load may pass a previous load.
  return !IsAStore;
}

void LSUnit::onInstructionExecuted(const InstRef &IR) {
  const InstrDesc &Desc = IR.getInstruction()->getDesc();
  const unsigned Index = IR.getSourceIndex();
  bool IsALoad = Desc.MayLoad;
  bool IsAStore = Desc.MayStore;

  if (IsALoad) {
    if (LoadQueue.erase(Index)) {
      LLVM_DEBUG(dbgs() << "[LSUnit]: Instruction idx=" << Index
                        << " has been removed from the load queue.\n");
    }
    if (!LoadBarriers.empty() && Index == *LoadBarriers.begin()) {
      LLVM_DEBUG(
          dbgs() << "[LSUnit]: Instruction idx=" << Index
                 << " has been removed from the set of load barriers.\n");
      LoadBarriers.erase(Index);
    }
  }

  if (IsAStore) {
    if (StoreQueue.erase(Index)) {
      LLVM_DEBUG(dbgs() << "[LSUnit]: Instruction idx=" << Index
                        << " has been removed from the store queue.\n");
    }

    if (!StoreBarriers.empty() && Index == *StoreBarriers.begin()) {
      LLVM_DEBUG(
          dbgs() << "[LSUnit]: Instruction idx=" << Index
                 << " has been removed from the set of store barriers.\n");
      StoreBarriers.erase(Index);
    }
  }
}

} // namespace mca
} // namespace llvm
