//===- IntervalIterator.h - Interval Iterator Declaration -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines an iterator that enumerates the intervals in a control flow
// graph of some sort.  This iterator is parametric, allowing iterator over the
// following types of graphs:
//
//  1. A Function* object, composed of BasicBlock nodes.
//  2. An IntervalPartition& object, composed of Interval nodes.
//
// This iterator is defined to walk the control flow graph, returning intervals
// in depth first order.  These intervals are completely filled in except for
// the predecessor fields (the successor information is filled in however).
//
// By default, the intervals created by this iterator are deleted after they
// are no longer any use to the iterator.  This behavior can be changed by
// passing a false value into the intervals_begin() function. This causes the
// IOwnMem member to be set, and the intervals to not be deleted.
//
// It is only safe to use this if all of the intervals are deleted by the caller
// and all of the intervals are processed.  However, the user of the iterator is
// not allowed to modify or delete the intervals until after the iterator has
// been used completely.  The IntervalPartition class uses this functionality.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_INTERVALITERATOR_H
#define LLVM_ANALYSIS_INTERVALITERATOR_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/Analysis/Interval.h"
#include "llvm/Analysis/IntervalPartition.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <set>
#include <utility>
#include <vector>

namespace llvm {

class BasicBlock;

// getNodeHeader - Given a source graph node and the source graph, return the
// BasicBlock that is the header node.  This is the opposite of
// getSourceGraphNode.
inline BasicBlock *getNodeHeader(BasicBlock *BB) { return BB; }
inline BasicBlock *getNodeHeader(Interval *I) { return I->getHeaderNode(); }

// getSourceGraphNode - Given a BasicBlock and the source graph, return the
// source graph node that corresponds to the BasicBlock.  This is the opposite
// of getNodeHeader.
inline BasicBlock *getSourceGraphNode(Function *, BasicBlock *BB) {
  return BB;
}
inline Interval *getSourceGraphNode(IntervalPartition *IP, BasicBlock *BB) {
  return IP->getBlockInterval(BB);
}

// addNodeToInterval - This method exists to assist the generic ProcessNode
// with the task of adding a node to the new interval, depending on the
// type of the source node.  In the case of a CFG source graph (BasicBlock
// case), the BasicBlock itself is added to the interval.
inline void addNodeToInterval(Interval *Int, BasicBlock *BB) {
  Int->Nodes.push_back(BB);
}

// addNodeToInterval - This method exists to assist the generic ProcessNode
// with the task of adding a node to the new interval, depending on the
// type of the source node.  In the case of a CFG source graph (BasicBlock
// case), the BasicBlock itself is added to the interval.  In the case of
// an IntervalPartition source graph (Interval case), all of the member
// BasicBlocks are added to the interval.
inline void addNodeToInterval(Interval *Int, Interval *I) {
  // Add all of the nodes in I as new nodes in Int.
  Int->Nodes.insert(Int->Nodes.end(), I->Nodes.begin(), I->Nodes.end());
}

template<class NodeTy, class OrigContainer_t, class GT = GraphTraits<NodeTy *>,
         class IGT = GraphTraits<Inverse<NodeTy *>>>
class IntervalIterator {
  std::vector<std::pair<Interval *, typename Interval::succ_iterator>> IntStack;
  std::set<BasicBlock *> Visited;
  OrigContainer_t *OrigContainer;
  bool IOwnMem;     // If True, delete intervals when done with them
                    // See file header for conditions of use

public:
  using iterator_category = std::forward_iterator_tag;

  IntervalIterator() = default; // End iterator, empty stack

  IntervalIterator(Function *M, bool OwnMemory) : IOwnMem(OwnMemory) {
    OrigContainer = M;
    if (!ProcessInterval(&M->front())) {
      llvm_unreachable("ProcessInterval should never fail for first interval!");
    }
  }

  IntervalIterator(IntervalIterator &&x)
      : IntStack(std::move(x.IntStack)), Visited(std::move(x.Visited)),
        OrigContainer(x.OrigContainer), IOwnMem(x.IOwnMem) {
    x.IOwnMem = false;
  }

  IntervalIterator(IntervalPartition &IP, bool OwnMemory) : IOwnMem(OwnMemory) {
    OrigContainer = &IP;
    if (!ProcessInterval(IP.getRootInterval())) {
      llvm_unreachable("ProcessInterval should never fail for first interval!");
    }
  }

  ~IntervalIterator() {
    if (IOwnMem)
      while (!IntStack.empty()) {
        delete operator*();
        IntStack.pop_back();
      }
  }

  bool operator==(const IntervalIterator &x) const {
    return IntStack == x.IntStack;
  }
  bool operator!=(const IntervalIterator &x) const { return !(*this == x); }

  const Interval *operator*() const { return IntStack.back().first; }
  Interval *operator*() { return IntStack.back().first; }
  const Interval *operator->() const { return operator*(); }
  Interval *operator->() { return operator*(); }

  IntervalIterator &operator++() { // Preincrement
    assert(!IntStack.empty() && "Attempting to use interval iterator at end!");
    do {
      // All of the intervals on the stack have been visited.  Try visiting
      // their successors now.
      Interval::succ_iterator &SuccIt = IntStack.back().second,
                                EndIt = succ_end(IntStack.back().first);
      while (SuccIt != EndIt) {                 // Loop over all interval succs
        bool Done = ProcessInterval(getSourceGraphNode(OrigContainer, *SuccIt));
        ++SuccIt;                               // Increment iterator
        if (Done) return *this;                 // Found a new interval! Use it!
      }

      // Free interval memory... if necessary
      if (IOwnMem) delete IntStack.back().first;

      // We ran out of successors for this interval... pop off the stack
      IntStack.pop_back();
    } while (!IntStack.empty());

    return *this;
  }

  IntervalIterator operator++(int) { // Postincrement
    IntervalIterator tmp = *this;
    ++*this;
    return tmp;
  }

private:
  // ProcessInterval - This method is used during the construction of the
  // interval graph.  It walks through the source graph, recursively creating
  // an interval per invocation until the entire graph is covered.  This uses
  // the ProcessNode method to add all of the nodes to the interval.
  //
  // This method is templated because it may operate on two different source
  // graphs: a basic block graph, or a preexisting interval graph.
  bool ProcessInterval(NodeTy *Node) {
    BasicBlock *Header = getNodeHeader(Node);
    if (!Visited.insert(Header).second)
      return false;

    Interval *Int = new Interval(Header);

    // Check all of our successors to see if they are in the interval...
    for (typename GT::ChildIteratorType I = GT::child_begin(Node),
           E = GT::child_end(Node); I != E; ++I)
      ProcessNode(Int, getSourceGraphNode(OrigContainer, *I));

    IntStack.push_back(std::make_pair(Int, succ_begin(Int)));
    return true;
  }

  // ProcessNode - This method is called by ProcessInterval to add nodes to the
  // interval being constructed, and it is also called recursively as it walks
  // the source graph.  A node is added to the current interval only if all of
  // its predecessors are already in the graph.  This also takes care of keeping
  // the successor set of an interval up to date.
  //
  // This method is templated because it may operate on two different source
  // graphs: a basic block graph, or a preexisting interval graph.
  void ProcessNode(Interval *Int, NodeTy *Node) {
    assert(Int && "Null interval == bad!");
    assert(Node && "Null Node == bad!");

    BasicBlock *NodeHeader = getNodeHeader(Node);

    if (Visited.count(NodeHeader)) {     // Node already been visited?
      if (Int->contains(NodeHeader)) {   // Already in this interval...
        return;
      } else {                           // In other interval, add as successor
        if (!Int->isSuccessor(NodeHeader)) // Add only if not already in set
          Int->Successors.push_back(NodeHeader);
      }
    } else {                             // Otherwise, not in interval yet
      for (typename IGT::ChildIteratorType I = IGT::child_begin(Node),
             E = IGT::child_end(Node); I != E; ++I) {
        if (!Int->contains(*I)) {        // If pred not in interval, we can't be
          if (!Int->isSuccessor(NodeHeader)) // Add only if not already in set
            Int->Successors.push_back(NodeHeader);
          return;                        // See you later
        }
      }

      // If we get here, then all of the predecessors of BB are in the interval
      // already.  In this case, we must add BB to the interval!
      addNodeToInterval(Int, Node);
      Visited.insert(NodeHeader);     // The node has now been visited!

      if (Int->isSuccessor(NodeHeader)) {
        // If we were in the successor list from before... remove from succ list
        Int->Successors.erase(std::remove(Int->Successors.begin(),
                                          Int->Successors.end(), NodeHeader),
                              Int->Successors.end());
      }

      // Now that we have discovered that Node is in the interval, perhaps some
      // of its successors are as well?
      for (typename GT::ChildIteratorType It = GT::child_begin(Node),
             End = GT::child_end(Node); It != End; ++It)
        ProcessNode(Int, getSourceGraphNode(OrigContainer, *It));
    }
  }
};

using function_interval_iterator = IntervalIterator<BasicBlock, Function>;
using interval_part_interval_iterator =
    IntervalIterator<Interval, IntervalPartition>;

inline function_interval_iterator intervals_begin(Function *F,
                                                  bool DeleteInts = true) {
  return function_interval_iterator(F, DeleteInts);
}
inline function_interval_iterator intervals_end(Function *) {
  return function_interval_iterator();
}

inline interval_part_interval_iterator
   intervals_begin(IntervalPartition &IP, bool DeleteIntervals = true) {
  return interval_part_interval_iterator(IP, DeleteIntervals);
}

inline interval_part_interval_iterator intervals_end(IntervalPartition &IP) {
  return interval_part_interval_iterator();
}

} // end namespace llvm

#endif // LLVM_ANALYSIS_INTERVALITERATOR_H
