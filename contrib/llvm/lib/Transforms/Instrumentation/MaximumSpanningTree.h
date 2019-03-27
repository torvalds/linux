//===- llvm/Analysis/MaximumSpanningTree.h - Interface ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This module provides means for calculating a maximum spanning tree for a
// given set of weighted edges. The type parameter T is the type of a node.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TRANSFORMS_INSTRUMENTATION_MAXIMUMSPANNINGTREE_H
#define LLVM_LIB_TRANSFORMS_INSTRUMENTATION_MAXIMUMSPANNINGTREE_H

#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/IR/BasicBlock.h"
#include <algorithm>
#include <vector>

namespace llvm {

  /// MaximumSpanningTree - A MST implementation.
  /// The type parameter T determines the type of the nodes of the graph.
  template <typename T>
  class MaximumSpanningTree {
  public:
    typedef std::pair<const T*, const T*> Edge;
    typedef std::pair<Edge, double> EdgeWeight;
    typedef std::vector<EdgeWeight> EdgeWeights;
  protected:
    typedef std::vector<Edge> MaxSpanTree;

    MaxSpanTree MST;

  private:
    // A comparing class for comparing weighted edges.
    struct EdgeWeightCompare {
      static bool getBlockSize(const T *X) {
        const BasicBlock *BB = dyn_cast_or_null<BasicBlock>(X);
        return BB ? BB->size() : 0;
      }

      bool operator()(EdgeWeight X, EdgeWeight Y) const {
        if (X.second > Y.second) return true;
        if (X.second < Y.second) return false;

        // Equal edge weights: break ties by comparing block sizes.
        size_t XSizeA = getBlockSize(X.first.first);
        size_t YSizeA = getBlockSize(Y.first.first);
        if (XSizeA > YSizeA) return true;
        if (XSizeA < YSizeA) return false;

        size_t XSizeB = getBlockSize(X.first.second);
        size_t YSizeB = getBlockSize(Y.first.second);
        if (XSizeB > YSizeB) return true;
        if (XSizeB < YSizeB) return false;

        return false;
      }
    };

  public:
    static char ID; // Class identification, replacement for typeinfo

    /// MaximumSpanningTree() - Takes a vector of weighted edges and returns a
    /// spanning tree.
    MaximumSpanningTree(EdgeWeights &EdgeVector) {

      std::stable_sort(EdgeVector.begin(), EdgeVector.end(), EdgeWeightCompare());

      // Create spanning tree, Forest contains a special data structure
      // that makes checking if two nodes are already in a common (sub-)tree
      // fast and cheap.
      EquivalenceClasses<const T*> Forest;
      for (typename EdgeWeights::iterator EWi = EdgeVector.begin(),
           EWe = EdgeVector.end(); EWi != EWe; ++EWi) {
        Edge e = (*EWi).first;

        Forest.insert(e.first);
        Forest.insert(e.second);
      }

      // Iterate over the sorted edges, biggest first.
      for (typename EdgeWeights::iterator EWi = EdgeVector.begin(),
           EWe = EdgeVector.end(); EWi != EWe; ++EWi) {
        Edge e = (*EWi).first;

        if (Forest.findLeader(e.first) != Forest.findLeader(e.second)) {
          Forest.unionSets(e.first, e.second);
          // So we know now that the edge is not already in a subtree, so we push
          // the edge to the MST.
          MST.push_back(e);
        }
      }
    }

    typename MaxSpanTree::iterator begin() {
      return MST.begin();
    }

    typename MaxSpanTree::iterator end() {
      return MST.end();
    }
  };

} // End llvm namespace

#endif // LLVM_LIB_TRANSFORMS_INSTRUMENTATION_MAXIMUMSPANNINGTREE_H
