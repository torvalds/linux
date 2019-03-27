//===--------------------- Scheduler.cpp ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// A scheduler for processor resource units and processor resource groups.
//
//===----------------------------------------------------------------------===//

#include "llvm/MCA/HardwareUnits/Scheduler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace mca {

#define DEBUG_TYPE "llvm-mca"

void Scheduler::initializeStrategy(std::unique_ptr<SchedulerStrategy> S) {
  // Ensure we have a valid (non-null) strategy object.
  Strategy = S ? std::move(S) : llvm::make_unique<DefaultSchedulerStrategy>();
}

// Anchor the vtable of SchedulerStrategy and DefaultSchedulerStrategy.
SchedulerStrategy::~SchedulerStrategy() = default;
DefaultSchedulerStrategy::~DefaultSchedulerStrategy() = default;

#ifndef NDEBUG
void Scheduler::dump() const {
  dbgs() << "[SCHEDULER]: WaitSet size is: " << WaitSet.size() << '\n';
  dbgs() << "[SCHEDULER]: ReadySet size is: " << ReadySet.size() << '\n';
  dbgs() << "[SCHEDULER]: IssuedSet size is: " << IssuedSet.size() << '\n';
  Resources->dump();
}
#endif

Scheduler::Status Scheduler::isAvailable(const InstRef &IR) const {
  const InstrDesc &Desc = IR.getInstruction()->getDesc();

  switch (Resources->canBeDispatched(Desc.Buffers)) {
  case ResourceStateEvent::RS_BUFFER_UNAVAILABLE:
    return Scheduler::SC_BUFFERS_FULL;
  case ResourceStateEvent::RS_RESERVED:
    return Scheduler::SC_DISPATCH_GROUP_STALL;
  case ResourceStateEvent::RS_BUFFER_AVAILABLE:
    break;
  }

  // Give lower priority to LSUnit stall events.
  switch (LSU.isAvailable(IR)) {
  case LSUnit::LSU_LQUEUE_FULL:
    return Scheduler::SC_LOAD_QUEUE_FULL;
  case LSUnit::LSU_SQUEUE_FULL:
    return Scheduler::SC_STORE_QUEUE_FULL;
  case LSUnit::LSU_AVAILABLE:
    return Scheduler::SC_AVAILABLE;
  }

  llvm_unreachable("Don't know how to process this LSU state result!");
}

void Scheduler::issueInstructionImpl(
    InstRef &IR,
    SmallVectorImpl<std::pair<ResourceRef, ResourceCycles>> &UsedResources) {
  Instruction *IS = IR.getInstruction();
  const InstrDesc &D = IS->getDesc();

  // Issue the instruction and collect all the consumed resources
  // into a vector. That vector is then used to notify the listener.
  Resources->issueInstruction(D, UsedResources);

  // Notify the instruction that it started executing.
  // This updates the internal state of each write.
  IS->execute();

  if (IS->isExecuting())
    IssuedSet.emplace_back(IR);
  else if (IS->isExecuted())
    LSU.onInstructionExecuted(IR);
}

// Release the buffered resources and issue the instruction.
void Scheduler::issueInstruction(
    InstRef &IR,
    SmallVectorImpl<std::pair<ResourceRef, ResourceCycles>> &UsedResources,
    SmallVectorImpl<InstRef> &ReadyInstructions) {
  const Instruction &Inst = *IR.getInstruction();
  bool HasDependentUsers = Inst.hasDependentUsers();

  Resources->releaseBuffers(Inst.getDesc().Buffers);
  issueInstructionImpl(IR, UsedResources);
  // Instructions that have been issued during this cycle might have unblocked
  // other dependent instructions. Dependent instructions may be issued during
  // this same cycle if operands have ReadAdvance entries.  Promote those
  // instructions to the ReadySet and notify the caller that those are ready.
  if (HasDependentUsers)
    promoteToReadySet(ReadyInstructions);
}

void Scheduler::promoteToReadySet(SmallVectorImpl<InstRef> &Ready) {
  // Scan the set of waiting instructions and promote them to the
  // ready queue if operands are all ready.
  unsigned RemovedElements = 0;
  for (auto I = WaitSet.begin(), E = WaitSet.end(); I != E;) {
    InstRef &IR = *I;
    if (!IR)
      break;

    // Check if this instruction is now ready. In case, force
    // a transition in state using method 'update()'.
    Instruction &IS = *IR.getInstruction();
    if (!IS.isReady())
      IS.update();

    // Check if there are still unsolved data dependencies.
    if (!isReady(IR)) {
      ++I;
      continue;
    }

    Ready.emplace_back(IR);
    ReadySet.emplace_back(IR);

    IR.invalidate();
    ++RemovedElements;
    std::iter_swap(I, E - RemovedElements);
  }

  WaitSet.resize(WaitSet.size() - RemovedElements);
}

InstRef Scheduler::select() {
  unsigned QueueIndex = ReadySet.size();
  for (unsigned I = 0, E = ReadySet.size(); I != E; ++I) {
    const InstRef &IR = ReadySet[I];
    if (QueueIndex == ReadySet.size() ||
        Strategy->compare(IR, ReadySet[QueueIndex])) {
      const InstrDesc &D = IR.getInstruction()->getDesc();
      if (Resources->canBeIssued(D))
        QueueIndex = I;
    }
  }

  if (QueueIndex == ReadySet.size())
    return InstRef();

  // We found an instruction to issue.
  InstRef IR = ReadySet[QueueIndex];
  std::swap(ReadySet[QueueIndex], ReadySet[ReadySet.size() - 1]);
  ReadySet.pop_back();
  return IR;
}

void Scheduler::updateIssuedSet(SmallVectorImpl<InstRef> &Executed) {
  unsigned RemovedElements = 0;
  for (auto I = IssuedSet.begin(), E = IssuedSet.end(); I != E;) {
    InstRef &IR = *I;
    if (!IR)
      break;
    Instruction &IS = *IR.getInstruction();
    if (!IS.isExecuted()) {
      LLVM_DEBUG(dbgs() << "[SCHEDULER]: Instruction #" << IR
                        << " is still executing.\n");
      ++I;
      continue;
    }

    // Instruction IR has completed execution.
    LSU.onInstructionExecuted(IR);
    Executed.emplace_back(IR);
    ++RemovedElements;
    IR.invalidate();
    std::iter_swap(I, E - RemovedElements);
  }

  IssuedSet.resize(IssuedSet.size() - RemovedElements);
}

void Scheduler::cycleEvent(SmallVectorImpl<ResourceRef> &Freed,
                           SmallVectorImpl<InstRef> &Executed,
                           SmallVectorImpl<InstRef> &Ready) {
  // Release consumed resources.
  Resources->cycleEvent(Freed);

  // Propagate the cycle event to the 'Issued' and 'Wait' sets.
  for (InstRef &IR : IssuedSet)
    IR.getInstruction()->cycleEvent();

  updateIssuedSet(Executed);

  for (InstRef &IR : WaitSet)
    IR.getInstruction()->cycleEvent();

  promoteToReadySet(Ready);
}

bool Scheduler::mustIssueImmediately(const InstRef &IR) const {
  const InstrDesc &Desc = IR.getInstruction()->getDesc();
  if (Desc.isZeroLatency())
    return true;
  // Instructions that use an in-order dispatch/issue processor resource must be
  // issued immediately to the pipeline(s). Any other in-order buffered
  // resources (i.e. BufferSize=1) is consumed.
  return Desc.MustIssueImmediately;
}

void Scheduler::dispatch(const InstRef &IR) {
  const InstrDesc &Desc = IR.getInstruction()->getDesc();
  Resources->reserveBuffers(Desc.Buffers);

  // If necessary, reserve queue entries in the load-store unit (LSU).
  bool IsMemOp = Desc.MayLoad || Desc.MayStore;
  if (IsMemOp)
    LSU.dispatch(IR);

  if (!isReady(IR)) {
    LLVM_DEBUG(dbgs() << "[SCHEDULER] Adding #" << IR << " to the WaitSet\n");
    WaitSet.push_back(IR);
    return;
  }

  // Don't add a zero-latency instruction to the Ready queue.
  // A zero-latency instruction doesn't consume any scheduler resources. That is
  // because it doesn't need to be executed, and it is often removed at register
  // renaming stage. For example, register-register moves are often optimized at
  // register renaming stage by simply updating register aliases. On some
  // targets, zero-idiom instructions (for example: a xor that clears the value
  // of a register) are treated specially, and are often eliminated at register
  // renaming stage.
  if (!mustIssueImmediately(IR)) {
    LLVM_DEBUG(dbgs() << "[SCHEDULER] Adding #" << IR << " to the ReadySet\n");
    ReadySet.push_back(IR);
  }
}

bool Scheduler::isReady(const InstRef &IR) const {
  const InstrDesc &Desc = IR.getInstruction()->getDesc();
  bool IsMemOp = Desc.MayLoad || Desc.MayStore;
  return IR.getInstruction()->isReady() && (!IsMemOp || LSU.isReady(IR));
}

} // namespace mca
} // namespace llvm
