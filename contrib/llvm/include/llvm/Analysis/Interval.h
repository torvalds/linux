//===- llvm/Analysis/Interval.h - Interval Class Declaration ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the Interval class, which
// represents a set of CFG nodes and is a portion of an interval partition.
//
// Intervals have some interesting and useful properties, including the
// following:
//    1. The header node of an interval dominates all of the elements of the
//       interval
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_INTERVAL_H
#define LLVM_ANALYSIS_INTERVAL_H

#include "llvm/ADT/GraphTraits.h"
#include <vector>

namespace llvm {

class BasicBlock;
class raw_ostream;

//===----------------------------------------------------------------------===//
//
/// Interval Class - An Interval is a set of nodes defined such that every node
/// in the interval has all of its predecessors in the interval (except for the
/// header)
///
class Interval {
  /// HeaderNode - The header BasicBlock, which dominates all BasicBlocks in this
  /// interval.  Also, any loops in this interval must go through the HeaderNode.
  ///
  BasicBlock *HeaderNode;

public:
  using succ_iterator = std::vector<BasicBlock*>::iterator;
  using pred_iterator = std::vector<BasicBlock*>::iterator;
  using node_iterator = std::vector<BasicBlock*>::iterator;

  inline Interval(BasicBlock *Header) : HeaderNode(Header) {
    Nodes.push_back(Header);
  }

  inline BasicBlock *getHeaderNode() const { return HeaderNode; }

  /// Nodes - The basic blocks in this interval.
  std::vector<BasicBlock*> Nodes;

  /// Successors - List of BasicBlocks that are reachable directly from nodes in
  /// this interval, but are not in the interval themselves.
  /// These nodes necessarily must be header nodes for other intervals.
  std::vector<BasicBlock*> Successors;

  /// Predecessors - List of BasicBlocks that have this Interval's header block
  /// as one of their successors.
  std::vector<BasicBlock*> Predecessors;

  /// contains - Find out if a basic block is in this interval
  inline bool contains(BasicBlock *BB) const {
    for (BasicBlock *Node : Nodes)
      if (Node == BB)
        return true;
    return false;
    // I don't want the dependency on <algorithm>
    //return find(Nodes.begin(), Nodes.end(), BB) != Nodes.end();
  }

  /// isSuccessor - find out if a basic block is a successor of this Interval
  inline bool isSuccessor(BasicBlock *BB) const {
    for (BasicBlock *Successor : Successors)
      if (Successor == BB)
        return true;
    return false;
    // I don't want the dependency on <algorithm>
    //return find(Successors.begin(), Successors.end(), BB) != Successors.end();
  }

  /// Equality operator.  It is only valid to compare two intervals from the
  /// same partition, because of this, all we have to check is the header node
  /// for equality.
  inline bool operator==(const Interval &I) const {
    return HeaderNode == I.HeaderNode;
  }

  /// isLoop - Find out if there is a back edge in this interval...
  bool isLoop() const;

  /// print - Show contents in human readable format...
  void print(raw_ostream &O) const;
};

/// succ_begin/succ_end - define methods so that Intervals may be used
/// just like BasicBlocks can with the succ_* functions, and *::succ_iterator.
///
inline Interval::succ_iterator succ_begin(Interval *I) {
  return I->Successors.begin();
}
inline Interval::succ_iterator succ_end(Interval *I)   {
  return I->Successors.end();
}

/// pred_begin/pred_end - define methods so that Intervals may be used
/// just like BasicBlocks can with the pred_* functions, and *::pred_iterator.
///
inline Interval::pred_iterator pred_begin(Interval *I) {
  return I->Predecessors.begin();
}
inline Interval::pred_iterator pred_end(Interval *I)   {
  return I->Predecessors.end();
}

template <> struct GraphTraits<Interval*> {
  using NodeRef = Interval *;
  using ChildIteratorType = Interval::succ_iterator;

  static NodeRef getEntryNode(Interval *I) { return I; }

  /// nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  static ChildIteratorType child_begin(NodeRef N) { return succ_begin(N); }
  static ChildIteratorType child_end(NodeRef N) { return succ_end(N); }
};

template <> struct GraphTraits<Inverse<Interval*>> {
  using NodeRef = Interval *;
  using ChildIteratorType = Interval::pred_iterator;

  static NodeRef getEntryNode(Inverse<Interval *> G) { return G.Graph; }
  static ChildIteratorType child_begin(NodeRef N) { return pred_begin(N); }
  static ChildIteratorType child_end(NodeRef N) { return pred_end(N); }
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_INTERVAL_H
