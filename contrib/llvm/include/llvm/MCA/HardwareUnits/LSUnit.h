//===------------------------- LSUnit.h --------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// A Load/Store unit class that models load/store queues and that implements
/// a simple weak memory consistency model.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_LSUNIT_H
#define LLVM_MCA_LSUNIT_H

#include "llvm/ADT/SmallSet.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/MCA/HardwareUnits/HardwareUnit.h"

namespace llvm {
namespace mca {

class InstRef;
class Scheduler;

/// A Load/Store Unit implementing a load and store queues.
///
/// This class implements a load queue and a store queue to emulate the
/// out-of-order execution of memory operations.
/// Each load (or store) consumes an entry in the load (or store) queue.
///
/// Rules are:
/// 1) A younger load is allowed to pass an older load only if there are no
///    stores nor barriers in between the two loads.
/// 2) An younger store is not allowed to pass an older store.
/// 3) A younger store is not allowed to pass an older load.
/// 4) A younger load is allowed to pass an older store only if the load does
///    not alias with the store.
///
/// This class optimistically assumes that loads don't alias store operations.
/// Under this assumption, younger loads are always allowed to pass older
/// stores (this would only affects rule 4).
/// Essentially, this class doesn't perform any sort alias analysis to
/// identify aliasing loads and stores.
///
/// To enforce aliasing between loads and stores, flag `AssumeNoAlias` must be
/// set to `false` by the constructor of LSUnit.
///
/// Note that this class doesn't know about the existence of different memory
/// types for memory operations (example: write-through, write-combining, etc.).
/// Derived classes are responsible for implementing that extra knowledge, and
/// provide different sets of rules for loads and stores by overriding method
/// `isReady()`.
/// To emulate a write-combining memory type, rule 2. must be relaxed in a
/// derived class to enable the reordering of non-aliasing store operations.
///
/// No assumptions are made by this class on the size of the store buffer.  This
/// class doesn't know how to identify cases where store-to-load forwarding may
/// occur.
///
/// LSUnit doesn't attempt to predict whether a load or store hits or misses
/// the L1 cache. To be more specific, LSUnit doesn't know anything about
/// cache hierarchy and memory types.
/// It only knows if an instruction "mayLoad" and/or "mayStore". For loads, the
/// scheduling model provides an "optimistic" load-to-use latency (which usually
/// matches the load-to-use latency for when there is a hit in the L1D).
/// Derived classes may expand this knowledge.
///
/// Class MCInstrDesc in LLVM doesn't know about serializing operations, nor
/// memory-barrier like instructions.
/// LSUnit conservatively assumes that an instruction which `mayLoad` and has
/// `unmodeled side effects` behave like a "soft" load-barrier. That means, it
/// serializes loads without forcing a flush of the load queue.
/// Similarly, instructions that both `mayStore` and have `unmodeled side
/// effects` are treated like store barriers. A full memory
/// barrier is a 'mayLoad' and 'mayStore' instruction with unmodeled side
/// effects. This is obviously inaccurate, but this is the best that we can do
/// at the moment.
///
/// Each load/store barrier consumes one entry in the load/store queue. A
/// load/store barrier enforces ordering of loads/stores:
///  - A younger load cannot pass a load barrier.
///  - A younger store cannot pass a store barrier.
///
/// A younger load has to wait for the memory load barrier to execute.
/// A load/store barrier is "executed" when it becomes the oldest entry in
/// the load/store queue(s). That also means, all the older loads/stores have
/// already been executed.
class LSUnit : public HardwareUnit {
  // Load queue size.
  // LQ_Size == 0 means that there are infinite slots in the load queue.
  unsigned LQ_Size;

  // Store queue size.
  // SQ_Size == 0 means that there are infinite slots in the store queue.
  unsigned SQ_Size;

  // If true, loads will never alias with stores. This is the default.
  bool NoAlias;

  // When a `MayLoad` instruction is dispatched to the schedulers for execution,
  // the LSUnit reserves an entry in the `LoadQueue` for it.
  //
  // LoadQueue keeps track of all the loads that are in-flight. A load
  // instruction is eventually removed from the LoadQueue when it reaches
  // completion stage. That means, a load leaves the queue whe it is 'executed',
  // and its value can be forwarded on the data path to outside units.
  //
  // This class doesn't know about the latency of a load instruction. So, it
  // conservatively/pessimistically assumes that the latency of a load opcode
  // matches the instruction latency.
  //
  // FIXME: In the absence of cache misses (i.e. L1I/L1D/iTLB/dTLB hits/misses),
  // and load/store conflicts, the latency of a load is determined by the depth
  // of the load pipeline. So, we could use field `LoadLatency` in the
  // MCSchedModel to model that latency.
  // Field `LoadLatency` often matches the so-called 'load-to-use' latency from
  // L1D, and it usually already accounts for any extra latency due to data
  // forwarding.
  // When doing throughput analysis, `LoadLatency` is likely to
  // be a better predictor of load latency than instruction latency. This is
  // particularly true when simulating code with temporal/spatial locality of
  // memory accesses.
  // Using `LoadLatency` (instead of the instruction latency) is also expected
  // to improve the load queue allocation for long latency instructions with
  // folded memory operands (See PR39829).
  //
  // FIXME: On some processors, load/store operations are split into multiple
  // uOps. For example, X86 AMD Jaguar natively supports 128-bit data types, but
  // not 256-bit data types. So, a 256-bit load is effectively split into two
  // 128-bit loads, and each split load consumes one 'LoadQueue' entry. For
  // simplicity, this class optimistically assumes that a load instruction only
  // consumes one entry in the LoadQueue.  Similarly, store instructions only
  // consume a single entry in the StoreQueue.
  // In future, we should reassess the quality of this design, and consider
  // alternative approaches that let instructions specify the number of
  // load/store queue entries which they consume at dispatch stage (See
  // PR39830).
  SmallSet<unsigned, 16> LoadQueue;
  SmallSet<unsigned, 16> StoreQueue;

  void assignLQSlot(unsigned Index);
  void assignSQSlot(unsigned Index);
  bool isReadyNoAlias(unsigned Index) const;

  // An instruction that both 'mayStore' and 'HasUnmodeledSideEffects' is
  // conservatively treated as a store barrier. It forces older store to be
  // executed before newer stores are issued.
  SmallSet<unsigned, 8> StoreBarriers;

  // An instruction that both 'MayLoad' and 'HasUnmodeledSideEffects' is
  // conservatively treated as a load barrier. It forces older loads to execute
  // before newer loads are issued.
  SmallSet<unsigned, 8> LoadBarriers;

  bool isSQEmpty() const { return StoreQueue.empty(); }
  bool isLQEmpty() const { return LoadQueue.empty(); }
  bool isSQFull() const { return SQ_Size != 0 && StoreQueue.size() == SQ_Size; }
  bool isLQFull() const { return LQ_Size != 0 && LoadQueue.size() == LQ_Size; }

public:
  LSUnit(const MCSchedModel &SM, unsigned LQ = 0, unsigned SQ = 0,
         bool AssumeNoAlias = false);

#ifndef NDEBUG
  void dump() const;
#endif

  enum Status { LSU_AVAILABLE = 0, LSU_LQUEUE_FULL, LSU_SQUEUE_FULL };

  // Returns LSU_AVAILABLE if there are enough load/store queue entries to serve
  // IR. It also returns LSU_AVAILABLE if IR is not a memory operation.
  Status isAvailable(const InstRef &IR) const;

  // Allocates load/store queue resources for IR.
  //
  // This method assumes that a previous call to `isAvailable(IR)` returned
  // LSU_AVAILABLE, and that IR is a memory operation.
  void dispatch(const InstRef &IR);

  // By default, rules are:
  // 1. A store may not pass a previous store.
  // 2. A load may not pass a previous store unless flag 'NoAlias' is set.
  // 3. A load may pass a previous load.
  // 4. A store may not pass a previous load (regardless of flag 'NoAlias').
  // 5. A load has to wait until an older load barrier is fully executed.
  // 6. A store has to wait until an older store barrier is fully executed.
  virtual bool isReady(const InstRef &IR) const;

  // Load and store instructions are tracked by their corresponding queues from
  // dispatch until the "instruction executed" event.
  // Only when a load instruction reaches the 'Executed' stage, its value
  // becomes available to the users. At that point, the load no longer needs to
  // be tracked by the load queue.
  // FIXME: For simplicity, we optimistically assume a similar behavior for
  // store instructions. In practice, store operations don't tend to leave the
  // store queue until they reach the 'Retired' stage (See PR39830).
  void onInstructionExecuted(const InstRef &IR);
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_LSUNIT_H
