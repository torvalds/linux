//===- CFGDiff.h - Define a CFG snapshot. -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines specializations of GraphTraits that allows generic
// algorithms to see a different snapshot of a CFG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CFGDIFF_H
#define LLVM_IR_CFGDIFF_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/Support/CFGUpdate.h"
#include "llvm/Support/type_traits.h"
#include <cassert>
#include <cstddef>
#include <iterator>

// Two booleans are used to define orders in graphs:
// InverseGraph defines when we need to reverse the whole graph and is as such
// also equivalent to applying updates in reverse.
// InverseEdge defines whether we want to change the edges direction. E.g., for
// a non-inversed graph, the children are naturally the successors when
// InverseEdge is false and the predecessors when InverseEdge is true.

// We define two base clases that call into GraphDiff, one for successors
// (CFGSuccessors), where InverseEdge is false, and one for predecessors
// (CFGPredecessors), where InverseEdge is true.
// FIXME: Further refactoring may merge the two base classes into a single one
// templated / parametrized on using succ_iterator/pred_iterator and false/true
// for the InverseEdge.

// CFGViewSuccessors and CFGViewPredecessors, both can be parametrized to
// consider the graph inverted or not (i.e. InverseGraph). Successors
// implicitly has InverseEdge = false and Predecessors implicitly has
// InverseEdge = true (see calls to GraphDiff methods in there). The GraphTraits
// instantiations that follow define the value of InverseGraph.

// GraphTraits instantiations:
// - GraphDiff<BasicBlock *> is equivalent to InverseGraph = false
// - GraphDiff<Inverse<BasicBlock *>> is equivalent to InverseGraph = true
// - second pair item is BasicBlock *, then InverseEdge = false (so it inherits
// from CFGViewSuccessors).
// - second pair item is Inverse<BasicBlock *>, then InverseEdge = true (so it
// inherits from CFGViewPredecessors).

// The 4 GraphTraits are as follows:
// 1. std::pair<const GraphDiff<BasicBlock *> *, BasicBlock *>> :
//        CFGViewSuccessors<false>
// Regular CFG, children means successors, InverseGraph = false,
// InverseEdge = false.
// 2. std::pair<const GraphDiff<Inverse<BasicBlock *>> *, BasicBlock *>> :
//        CFGViewSuccessors<true>
// Reverse the graph, get successors but reverse-apply updates,
// InverseGraph = true, InverseEdge = false.
// 3. std::pair<const GraphDiff<BasicBlock *> *, Inverse<BasicBlock *>>> :
//        CFGViewPredecessors<false>
// Regular CFG, reverse edges, so children mean predecessors,
// InverseGraph = false, InverseEdge = true.
// 4. std::pair<const GraphDiff<Inverse<BasicBlock *>> *, Inverse<BasicBlock *>>
//        : CFGViewPredecessors<true>
// Reverse the graph and the edges, InverseGraph = true, InverseEdge = true.

namespace llvm {

// GraphDiff defines a CFG snapshot: given a set of Update<NodePtr>, provide
// utilities to skip edges marked as deleted and return a set of edges marked as
// newly inserted. The current diff treats the CFG as a graph rather than a
// multigraph. Added edges are pruned to be unique, and deleted edges will
// remove all existing edges between two blocks.
template <typename NodePtr, bool InverseGraph = false> class GraphDiff {
  using UpdateMapType = SmallDenseMap<NodePtr, SmallVector<NodePtr, 2>>;
  UpdateMapType SuccInsert;
  UpdateMapType SuccDelete;
  UpdateMapType PredInsert;
  UpdateMapType PredDelete;
  // Using a singleton empty vector for all BasicBlock requests with no
  // children.
  SmallVector<NodePtr, 1> Empty;

  void printMap(raw_ostream &OS, const UpdateMapType &M) const {
    for (auto Pair : M)
      for (auto Child : Pair.second) {
        OS << "(";
        Pair.first->printAsOperand(OS, false);
        OS << ", ";
        Child->printAsOperand(OS, false);
        OS << ") ";
      }
    OS << "\n";
  }

public:
  GraphDiff() {}
  GraphDiff(ArrayRef<cfg::Update<NodePtr>> Updates) {
    SmallVector<cfg::Update<NodePtr>, 4> LegalizedUpdates;
    cfg::LegalizeUpdates<NodePtr>(Updates, LegalizedUpdates, InverseGraph);
    for (auto U : LegalizedUpdates) {
      if (U.getKind() == cfg::UpdateKind::Insert) {
        SuccInsert[U.getFrom()].push_back(U.getTo());
        PredInsert[U.getTo()].push_back(U.getFrom());
      } else {
        SuccDelete[U.getFrom()].push_back(U.getTo());
        PredDelete[U.getTo()].push_back(U.getFrom());
      }
    }
  }

  bool ignoreChild(const NodePtr BB, NodePtr EdgeEnd, bool InverseEdge) const {
    auto &DeleteChildren =
        (InverseEdge != InverseGraph) ? PredDelete : SuccDelete;
    auto It = DeleteChildren.find(BB);
    if (It == DeleteChildren.end())
      return false;
    auto &EdgesForBB = It->second;
    return llvm::find(EdgesForBB, EdgeEnd) != EdgesForBB.end();
  }

  iterator_range<typename SmallVectorImpl<NodePtr>::const_iterator>
  getAddedChildren(const NodePtr BB, bool InverseEdge) const {
    auto &InsertChildren =
        (InverseEdge != InverseGraph) ? PredInsert : SuccInsert;
    auto It = InsertChildren.find(BB);
    if (It == InsertChildren.end())
      return make_range(Empty.begin(), Empty.end());
    return make_range(It->second.begin(), It->second.end());
  }

  void print(raw_ostream &OS) const {
    OS << "===== GraphDiff: CFG edge changes to create a CFG snapshot. \n"
          "===== (Note: notion of children/inverse_children depends on "
          "the direction of edges and the graph.)\n";
    OS << "Children to insert:\n\t";
    printMap(OS, SuccInsert);
    OS << "Children to delete:\n\t";
    printMap(OS, SuccDelete);
    OS << "Inverse_children to insert:\n\t";
    printMap(OS, PredInsert);
    OS << "Inverse_children to delete:\n\t";
    printMap(OS, PredDelete);
    OS << "\n";
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  LLVM_DUMP_METHOD void dump() const { print(dbgs()); }
#endif
};

template <bool InverseGraph = false> struct CFGViewSuccessors {
  using DataRef = const GraphDiff<BasicBlock *, InverseGraph> *;
  using NodeRef = std::pair<DataRef, BasicBlock *>;

  using ExistingChildIterator =
      WrappedPairNodeDataIterator<succ_iterator, NodeRef, DataRef>;
  struct DeletedEdgesFilter {
    BasicBlock *BB;
    DeletedEdgesFilter(BasicBlock *BB) : BB(BB){};
    bool operator()(NodeRef N) const {
      return !N.first->ignoreChild(BB, N.second, false);
    }
  };
  using FilterExistingChildrenIterator =
      filter_iterator<ExistingChildIterator, DeletedEdgesFilter>;

  using vec_iterator = SmallVectorImpl<BasicBlock *>::const_iterator;
  using AddNewChildrenIterator =
      WrappedPairNodeDataIterator<vec_iterator, NodeRef, DataRef>;
  using ChildIteratorType =
      concat_iterator<NodeRef, FilterExistingChildrenIterator,
                      AddNewChildrenIterator>;

  static ChildIteratorType child_begin(NodeRef N) {
    auto InsertVec = N.first->getAddedChildren(N.second, false);
    // filter iterator init:
    auto firstit = make_filter_range(
        make_range<ExistingChildIterator>({succ_begin(N.second), N.first},
                                          {succ_end(N.second), N.first}),
        DeletedEdgesFilter(N.second));
    // new inserts iterator init:
    auto secondit = make_range<AddNewChildrenIterator>(
        {InsertVec.begin(), N.first}, {InsertVec.end(), N.first});

    return concat_iterator<NodeRef, FilterExistingChildrenIterator,
                           AddNewChildrenIterator>(firstit, secondit);
  }

  static ChildIteratorType child_end(NodeRef N) {
    auto InsertVec = N.first->getAddedChildren(N.second, false);
    // filter iterator init:
    auto firstit = make_filter_range(
        make_range<ExistingChildIterator>({succ_end(N.second), N.first},
                                          {succ_end(N.second), N.first}),
        DeletedEdgesFilter(N.second));
    // new inserts iterator init:
    auto secondit = make_range<AddNewChildrenIterator>(
        {InsertVec.end(), N.first}, {InsertVec.end(), N.first});

    return concat_iterator<NodeRef, FilterExistingChildrenIterator,
                           AddNewChildrenIterator>(firstit, secondit);
  }
};

template <bool InverseGraph = false> struct CFGViewPredecessors {
  using DataRef = const GraphDiff<BasicBlock *, InverseGraph> *;
  using NodeRef = std::pair<DataRef, BasicBlock *>;

  using ExistingChildIterator =
      WrappedPairNodeDataIterator<pred_iterator, NodeRef, DataRef>;
  struct DeletedEdgesFilter {
    BasicBlock *BB;
    DeletedEdgesFilter(BasicBlock *BB) : BB(BB){};
    bool operator()(NodeRef N) const {
      return !N.first->ignoreChild(BB, N.second, true);
    }
  };
  using FilterExistingChildrenIterator =
      filter_iterator<ExistingChildIterator, DeletedEdgesFilter>;

  using vec_iterator = SmallVectorImpl<BasicBlock *>::const_iterator;
  using AddNewChildrenIterator =
      WrappedPairNodeDataIterator<vec_iterator, NodeRef, DataRef>;
  using ChildIteratorType =
      concat_iterator<NodeRef, FilterExistingChildrenIterator,
                      AddNewChildrenIterator>;

  static ChildIteratorType child_begin(NodeRef N) {
    auto InsertVec = N.first->getAddedChildren(N.second, true);
    // filter iterator init:
    auto firstit = make_filter_range(
        make_range<ExistingChildIterator>({pred_begin(N.second), N.first},
                                          {pred_end(N.second), N.first}),
        DeletedEdgesFilter(N.second));
    // new inserts iterator init:
    auto secondit = make_range<AddNewChildrenIterator>(
        {InsertVec.begin(), N.first}, {InsertVec.end(), N.first});

    return concat_iterator<NodeRef, FilterExistingChildrenIterator,
                           AddNewChildrenIterator>(firstit, secondit);
  }

  static ChildIteratorType child_end(NodeRef N) {
    auto InsertVec = N.first->getAddedChildren(N.second, true);
    // filter iterator init:
    auto firstit = make_filter_range(
        make_range<ExistingChildIterator>({pred_end(N.second), N.first},
                                          {pred_end(N.second), N.first}),
        DeletedEdgesFilter(N.second));
    // new inserts iterator init:
    auto secondit = make_range<AddNewChildrenIterator>(
        {InsertVec.end(), N.first}, {InsertVec.end(), N.first});

    return concat_iterator<NodeRef, FilterExistingChildrenIterator,
                           AddNewChildrenIterator>(firstit, secondit);
  }
};

template <>
struct GraphTraits<
    std::pair<const GraphDiff<BasicBlock *, false> *, BasicBlock *>>
    : CFGViewSuccessors<false> {};
template <>
struct GraphTraits<
    std::pair<const GraphDiff<BasicBlock *, true> *, BasicBlock *>>
    : CFGViewSuccessors<true> {};
template <>
struct GraphTraits<
    std::pair<const GraphDiff<BasicBlock *, false> *, Inverse<BasicBlock *>>>
    : CFGViewPredecessors<false> {};
template <>
struct GraphTraits<
    std::pair<const GraphDiff<BasicBlock *, true> *, Inverse<BasicBlock *>>>
    : CFGViewPredecessors<true> {};
} // end namespace llvm

#endif // LLVM_IR_CFGDIFF_H
