//===---- LatencyPriorityQueue.cpp - A latency-oriented priority queue ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the LatencyPriorityQueue class, which is a
// SchedulingPriorityQueue that schedules using latency information to
// reduce the length of the critical path through the basic block.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/LatencyPriorityQueue.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "scheduler"

bool latency_sort::operator()(const SUnit *LHS, const SUnit *RHS) const {
  // The isScheduleHigh flag allows nodes with wraparound dependencies that
  // cannot easily be modeled as edges with latencies to be scheduled as
  // soon as possible in a top-down schedule.
  if (LHS->isScheduleHigh && !RHS->isScheduleHigh)
    return false;
  if (!LHS->isScheduleHigh && RHS->isScheduleHigh)
    return true;

  unsigned LHSNum = LHS->NodeNum;
  unsigned RHSNum = RHS->NodeNum;

  // The most important heuristic is scheduling the critical path.
  unsigned LHSLatency = PQ->getLatency(LHSNum);
  unsigned RHSLatency = PQ->getLatency(RHSNum);
  if (LHSLatency < RHSLatency) return true;
  if (LHSLatency > RHSLatency) return false;

  // After that, if two nodes have identical latencies, look to see if one will
  // unblock more other nodes than the other.
  unsigned LHSBlocked = PQ->getNumSolelyBlockNodes(LHSNum);
  unsigned RHSBlocked = PQ->getNumSolelyBlockNodes(RHSNum);
  if (LHSBlocked < RHSBlocked) return true;
  if (LHSBlocked > RHSBlocked) return false;

  // Finally, just to provide a stable ordering, use the node number as a
  // deciding factor.
  return RHSNum < LHSNum;
}


/// getSingleUnscheduledPred - If there is exactly one unscheduled predecessor
/// of SU, return it, otherwise return null.
SUnit *LatencyPriorityQueue::getSingleUnscheduledPred(SUnit *SU) {
  SUnit *OnlyAvailablePred = nullptr;
  for (SUnit::const_pred_iterator I = SU->Preds.begin(), E = SU->Preds.end();
       I != E; ++I) {
    SUnit &Pred = *I->getSUnit();
    if (!Pred.isScheduled) {
      // We found an available, but not scheduled, predecessor.  If it's the
      // only one we have found, keep track of it... otherwise give up.
      if (OnlyAvailablePred && OnlyAvailablePred != &Pred)
        return nullptr;
      OnlyAvailablePred = &Pred;
    }
  }

  return OnlyAvailablePred;
}

void LatencyPriorityQueue::push(SUnit *SU) {
  // Look at all of the successors of this node.  Count the number of nodes that
  // this node is the sole unscheduled node for.
  unsigned NumNodesBlocking = 0;
  for (SUnit::const_succ_iterator I = SU->Succs.begin(), E = SU->Succs.end();
       I != E; ++I) {
    if (getSingleUnscheduledPred(I->getSUnit()) == SU)
      ++NumNodesBlocking;
  }
  NumNodesSolelyBlocking[SU->NodeNum] = NumNodesBlocking;

  Queue.push_back(SU);
}


// scheduledNode - As nodes are scheduled, we look to see if there are any
// successor nodes that have a single unscheduled predecessor.  If so, that
// single predecessor has a higher priority, since scheduling it will make
// the node available.
void LatencyPriorityQueue::scheduledNode(SUnit *SU) {
  for (SUnit::const_succ_iterator I = SU->Succs.begin(), E = SU->Succs.end();
       I != E; ++I) {
    AdjustPriorityOfUnscheduledPreds(I->getSUnit());
  }
}

/// AdjustPriorityOfUnscheduledPreds - One of the predecessors of SU was just
/// scheduled.  If SU is not itself available, then there is at least one
/// predecessor node that has not been scheduled yet.  If SU has exactly ONE
/// unscheduled predecessor, we want to increase its priority: it getting
/// scheduled will make this node available, so it is better than some other
/// node of the same priority that will not make a node available.
void LatencyPriorityQueue::AdjustPriorityOfUnscheduledPreds(SUnit *SU) {
  if (SU->isAvailable) return;  // All preds scheduled.

  SUnit *OnlyAvailablePred = getSingleUnscheduledPred(SU);
  if (!OnlyAvailablePred || !OnlyAvailablePred->isAvailable) return;

  // Okay, we found a single predecessor that is available, but not scheduled.
  // Since it is available, it must be in the priority queue.  First remove it.
  remove(OnlyAvailablePred);

  // Reinsert the node into the priority queue, which recomputes its
  // NumNodesSolelyBlocking value.
  push(OnlyAvailablePred);
}

SUnit *LatencyPriorityQueue::pop() {
  if (empty()) return nullptr;
  std::vector<SUnit *>::iterator Best = Queue.begin();
  for (std::vector<SUnit *>::iterator I = std::next(Queue.begin()),
       E = Queue.end(); I != E; ++I)
    if (Picker(*Best, *I))
      Best = I;
  SUnit *V = *Best;
  if (Best != std::prev(Queue.end()))
    std::swap(*Best, Queue.back());
  Queue.pop_back();
  return V;
}

void LatencyPriorityQueue::remove(SUnit *SU) {
  assert(!Queue.empty() && "Queue is empty!");
  std::vector<SUnit *>::iterator I = find(Queue, SU);
  assert(I != Queue.end() && "Queue doesn't contain the SU being removed!");
  if (I != std::prev(Queue.end()))
    std::swap(*I, Queue.back());
  Queue.pop_back();
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void LatencyPriorityQueue::dump(ScheduleDAG *DAG) const {
  dbgs() << "Latency Priority Queue\n";
  dbgs() << "  Number of Queue Entries: " << Queue.size() << "\n";
  for (const SUnit *SU : Queue) {
    dbgs() << "    ";
    DAG->dumpNode(*SU);
  }
}
#endif
