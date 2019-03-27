//===- llvm/ADT/BreadthFirstIterator.h - Breadth First iterator -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file builds on the ADT/GraphTraits.h file to build a generic breadth
// first graph iterator.  This file exposes the following functions/types:
//
// bf_begin/bf_end/bf_iterator
//   * Normal breadth-first iteration - visit a graph level-by-level.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_BREADTHFIRSTITERATOR_H
#define LLVM_ADT_BREADTHFIRSTITERATOR_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/iterator_range.h"
#include <iterator>
#include <queue>
#include <utility>

namespace llvm {

// bf_iterator_storage - A private class which is used to figure out where to
// store the visited set. We only provide a non-external variant for now.
template <class SetType> class bf_iterator_storage {
public:
  SetType Visited;
};

// The visited state for the iteration is a simple set.
template <typename NodeRef, unsigned SmallSize = 8>
using bf_iterator_default_set = SmallPtrSet<NodeRef, SmallSize>;

// Generic Breadth first search iterator.
template <class GraphT,
          class SetType =
              bf_iterator_default_set<typename GraphTraits<GraphT>::NodeRef>,
          class GT = GraphTraits<GraphT>>
class bf_iterator
    : public std::iterator<std::forward_iterator_tag, typename GT::NodeRef>,
      public bf_iterator_storage<SetType> {
  using super = std::iterator<std::forward_iterator_tag, typename GT::NodeRef>;

  using NodeRef = typename GT::NodeRef;
  using ChildItTy = typename GT::ChildIteratorType;

  // First element is the node reference, second is the next child to visit.
  using QueueElement = std::pair<NodeRef, Optional<ChildItTy>>;

  // Visit queue - used to maintain BFS ordering.
  // Optional<> because we need markers for levels.
  std::queue<Optional<QueueElement>> VisitQueue;

  // Current level.
  unsigned Level;

private:
  inline bf_iterator(NodeRef Node) {
    this->Visited.insert(Node);
    Level = 0;

    // Also, insert a dummy node as marker.
    VisitQueue.push(QueueElement(Node, None));
    VisitQueue.push(None);
  }

  inline bf_iterator() = default;

  inline void toNext() {
    Optional<QueueElement> Head = VisitQueue.front();
    QueueElement H = Head.getValue();
    NodeRef Node = H.first;
    Optional<ChildItTy> &ChildIt = H.second;

    if (!ChildIt)
      ChildIt.emplace(GT::child_begin(Node));
    while (*ChildIt != GT::child_end(Node)) {
      NodeRef Next = *(*ChildIt)++;

      // Already visited?
      if (this->Visited.insert(Next).second)
        VisitQueue.push(QueueElement(Next, None));
    }
    VisitQueue.pop();

    // Go to the next element skipping markers if needed.
    if (!VisitQueue.empty()) {
      Head = VisitQueue.front();
      if (Head != None)
        return;
      Level += 1;
      VisitQueue.pop();

      // Don't push another marker if this is the last
      // element.
      if (!VisitQueue.empty())
        VisitQueue.push(None);
    }
  }

public:
  using pointer = typename super::pointer;

  // Provide static begin and end methods as our public "constructors"
  static bf_iterator begin(const GraphT &G) {
    return bf_iterator(GT::getEntryNode(G));
  }

  static bf_iterator end(const GraphT &G) { return bf_iterator(); }

  bool operator==(const bf_iterator &RHS) const {
    return VisitQueue == RHS.VisitQueue;
  }

  bool operator!=(const bf_iterator &RHS) const { return !(*this == RHS); }

  const NodeRef &operator*() const { return VisitQueue.front()->first; }

  // This is a nonstandard operator-> that dereferenfces the pointer an extra
  // time so that you can actually call methods on the node, because the
  // contained type is a pointer.
  NodeRef operator->() const { return **this; }

  bf_iterator &operator++() { // Pre-increment
    toNext();
    return *this;
  }

  bf_iterator operator++(int) { // Post-increment
    bf_iterator ItCopy = *this;
    ++*this;
    return ItCopy;
  }

  unsigned getLevel() const { return Level; }
};

// Provide global constructors that automatically figure out correct types.
template <class T> bf_iterator<T> bf_begin(const T &G) {
  return bf_iterator<T>::begin(G);
}

template <class T> bf_iterator<T> bf_end(const T &G) {
  return bf_iterator<T>::end(G);
}

// Provide an accessor method to use them in range-based patterns.
template <class T> iterator_range<bf_iterator<T>> breadth_first(const T &G) {
  return make_range(bf_begin(G), bf_end(G));
}

} // end namespace llvm

#endif // LLVM_ADT_BREADTHFIRSTITERATOR_H
