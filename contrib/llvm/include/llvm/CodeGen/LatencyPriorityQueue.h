//===---- LatencyPriorityQueue.h - A latency-oriented priority queue ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the LatencyPriorityQueue class, which is a
// SchedulingPriorityQueue that schedules using latency information to
// reduce the length of the critical path through the basic block.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LATENCYPRIORITYQUEUE_H
#define LLVM_CODEGEN_LATENCYPRIORITYQUEUE_H

#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/Config/llvm-config.h"

namespace llvm {
  class LatencyPriorityQueue;

  /// Sorting functions for the Available queue.
  struct latency_sort {
    LatencyPriorityQueue *PQ;
    explicit latency_sort(LatencyPriorityQueue *pq) : PQ(pq) {}

    bool operator()(const SUnit* LHS, const SUnit* RHS) const;
  };

  class LatencyPriorityQueue : public SchedulingPriorityQueue {
    // SUnits - The SUnits for the current graph.
    std::vector<SUnit> *SUnits;

    /// NumNodesSolelyBlocking - This vector contains, for every node in the
    /// Queue, the number of nodes that the node is the sole unscheduled
    /// predecessor for.  This is used as a tie-breaker heuristic for better
    /// mobility.
    std::vector<unsigned> NumNodesSolelyBlocking;

    /// Queue - The queue.
    std::vector<SUnit*> Queue;
    latency_sort Picker;

  public:
    LatencyPriorityQueue() : Picker(this) {
    }

    bool isBottomUp() const override { return false; }

    void initNodes(std::vector<SUnit> &sunits) override {
      SUnits = &sunits;
      NumNodesSolelyBlocking.resize(SUnits->size(), 0);
    }

    void addNode(const SUnit *SU) override {
      NumNodesSolelyBlocking.resize(SUnits->size(), 0);
    }

    void updateNode(const SUnit *SU) override {
    }

    void releaseState() override {
      SUnits = nullptr;
    }

    unsigned getLatency(unsigned NodeNum) const {
      assert(NodeNum < (*SUnits).size());
      return (*SUnits)[NodeNum].getHeight();
    }

    unsigned getNumSolelyBlockNodes(unsigned NodeNum) const {
      assert(NodeNum < NumNodesSolelyBlocking.size());
      return NumNodesSolelyBlocking[NodeNum];
    }

    bool empty() const override { return Queue.empty(); }

    void push(SUnit *U) override;

    SUnit *pop() override;

    void remove(SUnit *SU) override;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
    LLVM_DUMP_METHOD void dump(ScheduleDAG *DAG) const override;
#endif

    // scheduledNode - As nodes are scheduled, we look to see if there are any
    // successor nodes that have a single unscheduled predecessor.  If so, that
    // single predecessor has a higher priority, since scheduling it will make
    // the node available.
    void scheduledNode(SUnit *SU) override;

private:
    void AdjustPriorityOfUnscheduledPreds(SUnit *SU);
    SUnit *getSingleUnscheduledPred(SUnit *SU);
  };
}

#endif
