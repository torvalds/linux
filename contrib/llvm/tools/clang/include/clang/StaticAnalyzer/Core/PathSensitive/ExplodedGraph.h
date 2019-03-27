//===- ExplodedGraph.h - Local, Path-Sens. "Exploded Graph" -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the template classes ExplodedNode and ExplodedGraph,
//  which represent a path-sensitive, intra-procedural "exploded graph."
//  See "Precise interprocedural dataflow analysis via graph reachability"
//  by Reps, Horwitz, and Sagiv
//  (http://portal.acm.org/citation.cfm?id=199462) for the definition of an
//  exploded graph.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_EXPLODEDGRAPH_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_EXPLODEDGRAPH_H

#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/ProgramPoint.h"
#include "clang/Analysis/Support/BumpVector.h"
#include "clang/Basic/LLVM.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState_Fwd.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace clang {

class CFG;
class Decl;
class Expr;
class ParentMap;
class Stmt;

namespace ento {

class ExplodedGraph;

//===----------------------------------------------------------------------===//
// ExplodedGraph "implementation" classes.  These classes are not typed to
// contain a specific kind of state.  Typed-specialized versions are defined
// on top of these classes.
//===----------------------------------------------------------------------===//

// ExplodedNode is not constified all over the engine because we need to add
// successors to it at any time after creating it.

class ExplodedNode : public llvm::FoldingSetNode {
  friend class BranchNodeBuilder;
  friend class CoreEngine;
  friend class EndOfFunctionNodeBuilder;
  friend class ExplodedGraph;
  friend class IndirectGotoNodeBuilder;
  friend class NodeBuilder;
  friend class SwitchNodeBuilder;

  /// Efficiently stores a list of ExplodedNodes, or an optional flag.
  ///
  /// NodeGroup provides opaque storage for a list of ExplodedNodes, optimizing
  /// for the case when there is only one node in the group. This is a fairly
  /// common case in an ExplodedGraph, where most nodes have only one
  /// predecessor and many have only one successor. It can also be used to
  /// store a flag rather than a node list, which ExplodedNode uses to mark
  /// whether a node is a sink. If the flag is set, the group is implicitly
  /// empty and no nodes may be added.
  class NodeGroup {
    // Conceptually a discriminated union. If the low bit is set, the node is
    // a sink. If the low bit is not set, the pointer refers to the storage
    // for the nodes in the group.
    // This is not a PointerIntPair in order to keep the storage type opaque.
    uintptr_t P;

  public:
    NodeGroup(bool Flag = false) : P(Flag) {
      assert(getFlag() == Flag);
    }

    ExplodedNode * const *begin() const;

    ExplodedNode * const *end() const;

    unsigned size() const;

    bool empty() const { return P == 0 || getFlag() != 0; }

    /// Adds a node to the list.
    ///
    /// The group must not have been created with its flag set.
    void addNode(ExplodedNode *N, ExplodedGraph &G);

    /// Replaces the single node in this group with a new node.
    ///
    /// Note that this should only be used when you know the group was not
    /// created with its flag set, and that the group is empty or contains
    /// only a single node.
    void replaceNode(ExplodedNode *node);

    /// Returns whether this group was created with its flag set.
    bool getFlag() const {
      return (P & 1);
    }
  };

  /// Location - The program location (within a function body) associated
  ///  with this node.
  const ProgramPoint Location;

  /// State - The state associated with this node.
  ProgramStateRef State;

  /// Preds - The predecessors of this node.
  NodeGroup Preds;

  /// Succs - The successors of this node.
  NodeGroup Succs;

public:
  explicit ExplodedNode(const ProgramPoint &loc, ProgramStateRef state,
                        bool IsSink)
      : Location(loc), State(std::move(state)), Succs(IsSink) {
    assert(isSink() == IsSink);
  }

  /// getLocation - Returns the edge associated with the given node.
  ProgramPoint getLocation() const { return Location; }

  const LocationContext *getLocationContext() const {
    return getLocation().getLocationContext();
  }

  const StackFrameContext *getStackFrame() const {
    return getLocation().getStackFrame();
  }

  const Decl &getCodeDecl() const { return *getLocationContext()->getDecl(); }

  CFG &getCFG() const { return *getLocationContext()->getCFG(); }

  ParentMap &getParentMap() const {return getLocationContext()->getParentMap();}

  template <typename T>
  T &getAnalysis() const {
    return *getLocationContext()->getAnalysis<T>();
  }

  const ProgramStateRef &getState() const { return State; }

  template <typename T>
  Optional<T> getLocationAs() const LLVM_LVALUE_FUNCTION {
    return Location.getAs<T>();
  }

  /// Get the value of an arbitrary expression at this node.
  SVal getSVal(const Stmt *S) const {
    return getState()->getSVal(S, getLocationContext());
  }

  static void Profile(llvm::FoldingSetNodeID &ID,
                      const ProgramPoint &Loc,
                      const ProgramStateRef &state,
                      bool IsSink) {
    ID.Add(Loc);
    ID.AddPointer(state.get());
    ID.AddBoolean(IsSink);
  }

  void Profile(llvm::FoldingSetNodeID& ID) const {
    // We avoid copy constructors by not using accessors.
    Profile(ID, Location, State, isSink());
  }

  /// addPredeccessor - Adds a predecessor to the current node, and
  ///  in tandem add this node as a successor of the other node.
  void addPredecessor(ExplodedNode *V, ExplodedGraph &G);

  unsigned succ_size() const { return Succs.size(); }
  unsigned pred_size() const { return Preds.size(); }
  bool succ_empty() const { return Succs.empty(); }
  bool pred_empty() const { return Preds.empty(); }

  bool isSink() const { return Succs.getFlag(); }

  bool hasSinglePred() const {
    return (pred_size() == 1);
  }

  ExplodedNode *getFirstPred() {
    return pred_empty() ? nullptr : *(pred_begin());
  }

  const ExplodedNode *getFirstPred() const {
    return const_cast<ExplodedNode*>(this)->getFirstPred();
  }

  ExplodedNode *getFirstSucc() {
    return succ_empty() ? nullptr : *(succ_begin());
  }

  const ExplodedNode *getFirstSucc() const {
    return const_cast<ExplodedNode*>(this)->getFirstSucc();
  }

  // Iterators over successor and predecessor vertices.
  using succ_iterator = ExplodedNode * const *;
  using const_succ_iterator = const ExplodedNode * const *;
  using pred_iterator = ExplodedNode * const *;
  using const_pred_iterator = const ExplodedNode * const *;

  pred_iterator pred_begin() { return Preds.begin(); }
  pred_iterator pred_end() { return Preds.end(); }

  const_pred_iterator pred_begin() const {
    return const_cast<ExplodedNode*>(this)->pred_begin();
  }
  const_pred_iterator pred_end() const {
    return const_cast<ExplodedNode*>(this)->pred_end();
  }

  succ_iterator succ_begin() { return Succs.begin(); }
  succ_iterator succ_end() { return Succs.end(); }

  const_succ_iterator succ_begin() const {
    return const_cast<ExplodedNode*>(this)->succ_begin();
  }
  const_succ_iterator succ_end() const {
    return const_cast<ExplodedNode*>(this)->succ_end();
  }

  int64_t getID(ExplodedGraph *G) const;

  /// The node is trivial if it has only one successor, only one predecessor,
  /// it's predecessor has only one successor,
  /// and its program state is the same as the program state of the previous
  /// node.
  /// Trivial nodes may be skipped while printing exploded graph.
  bool isTrivial() const;

private:
  void replaceSuccessor(ExplodedNode *node) { Succs.replaceNode(node); }
  void replacePredecessor(ExplodedNode *node) { Preds.replaceNode(node); }
};

using InterExplodedGraphMap =
    llvm::DenseMap<const ExplodedNode *, const ExplodedNode *>;

class ExplodedGraph {
protected:
  friend class CoreEngine;

  // Type definitions.
  using NodeVector = std::vector<ExplodedNode *>;

  /// The roots of the simulation graph. Usually there will be only
  /// one, but clients are free to establish multiple subgraphs within a single
  /// SimulGraph. Moreover, these subgraphs can often merge when paths from
  /// different roots reach the same state at the same program location.
  NodeVector Roots;

  /// The nodes in the simulation graph which have been
  /// specially marked as the endpoint of an abstract simulation path.
  NodeVector EndNodes;

  /// Nodes - The nodes in the graph.
  llvm::FoldingSet<ExplodedNode> Nodes;

  /// BVC - Allocator and context for allocating nodes and their predecessor
  /// and successor groups.
  BumpVectorContext BVC;

  /// NumNodes - The number of nodes in the graph.
  unsigned NumNodes = 0;

  /// A list of recently allocated nodes that can potentially be recycled.
  NodeVector ChangedNodes;

  /// A list of nodes that can be reused.
  NodeVector FreeNodes;

  /// Determines how often nodes are reclaimed.
  ///
  /// If this is 0, nodes will never be reclaimed.
  unsigned ReclaimNodeInterval = 0;

  /// Counter to determine when to reclaim nodes.
  unsigned ReclaimCounter;

public:
  ExplodedGraph();
  ~ExplodedGraph();

  /// Retrieve the node associated with a (Location,State) pair,
  ///  where the 'Location' is a ProgramPoint in the CFG.  If no node for
  ///  this pair exists, it is created. IsNew is set to true if
  ///  the node was freshly created.
  ExplodedNode *getNode(const ProgramPoint &L, ProgramStateRef State,
                        bool IsSink = false,
                        bool* IsNew = nullptr);

  /// Create a node for a (Location, State) pair,
  ///  but don't store it for deduplication later.  This
  ///  is useful when copying an already completed
  ///  ExplodedGraph for further processing.
  ExplodedNode *createUncachedNode(const ProgramPoint &L,
    ProgramStateRef State,
    bool IsSink = false);

  std::unique_ptr<ExplodedGraph> MakeEmptyGraph() const {
    return llvm::make_unique<ExplodedGraph>();
  }

  /// addRoot - Add an untyped node to the set of roots.
  ExplodedNode *addRoot(ExplodedNode *V) {
    Roots.push_back(V);
    return V;
  }

  /// addEndOfPath - Add an untyped node to the set of EOP nodes.
  ExplodedNode *addEndOfPath(ExplodedNode *V) {
    EndNodes.push_back(V);
    return V;
  }

  unsigned num_roots() const { return Roots.size(); }
  unsigned num_eops() const { return EndNodes.size(); }

  bool empty() const { return NumNodes == 0; }
  unsigned size() const { return NumNodes; }

  void reserve(unsigned NodeCount) { Nodes.reserve(NodeCount); }

  // Iterators.
  using NodeTy = ExplodedNode;
  using AllNodesTy = llvm::FoldingSet<ExplodedNode>;
  using roots_iterator = NodeVector::iterator;
  using const_roots_iterator = NodeVector::const_iterator;
  using eop_iterator = NodeVector::iterator;
  using const_eop_iterator = NodeVector::const_iterator;
  using node_iterator = AllNodesTy::iterator;
  using const_node_iterator = AllNodesTy::const_iterator;

  node_iterator nodes_begin() { return Nodes.begin(); }

  node_iterator nodes_end() { return Nodes.end(); }

  const_node_iterator nodes_begin() const { return Nodes.begin(); }

  const_node_iterator nodes_end() const { return Nodes.end(); }

  roots_iterator roots_begin() { return Roots.begin(); }

  roots_iterator roots_end() { return Roots.end(); }

  const_roots_iterator roots_begin() const { return Roots.begin(); }

  const_roots_iterator roots_end() const { return Roots.end(); }

  eop_iterator eop_begin() { return EndNodes.begin(); }

  eop_iterator eop_end() { return EndNodes.end(); }

  const_eop_iterator eop_begin() const { return EndNodes.begin(); }

  const_eop_iterator eop_end() const { return EndNodes.end(); }

  llvm::BumpPtrAllocator & getAllocator() { return BVC.getAllocator(); }
  BumpVectorContext &getNodeAllocator() { return BVC; }

  using NodeMap = llvm::DenseMap<const ExplodedNode *, ExplodedNode *>;

  /// Creates a trimmed version of the graph that only contains paths leading
  /// to the given nodes.
  ///
  /// \param Nodes The nodes which must appear in the final graph. Presumably
  ///              these are end-of-path nodes (i.e. they have no successors).
  /// \param[out] ForwardMap A optional map from nodes in this graph to nodes in
  ///                        the returned graph.
  /// \param[out] InverseMap An optional map from nodes in the returned graph to
  ///                        nodes in this graph.
  /// \returns The trimmed graph
  std::unique_ptr<ExplodedGraph>
  trim(ArrayRef<const NodeTy *> Nodes,
       InterExplodedGraphMap *ForwardMap = nullptr,
       InterExplodedGraphMap *InverseMap = nullptr) const;

  /// Enable tracking of recently allocated nodes for potential reclamation
  /// when calling reclaimRecentlyAllocatedNodes().
  void enableNodeReclamation(unsigned Interval) {
    ReclaimCounter = ReclaimNodeInterval = Interval;
  }

  /// Reclaim "uninteresting" nodes created since the last time this method
  /// was called.
  void reclaimRecentlyAllocatedNodes();

  /// Returns true if nodes for the given expression kind are always
  ///        kept around.
  static bool isInterestingLValueExpr(const Expr *Ex);

private:
  bool shouldCollect(const ExplodedNode *node);
  void collectNode(ExplodedNode *node);
};

class ExplodedNodeSet {
  using ImplTy = llvm::SmallSetVector<ExplodedNode *, 4>;
  ImplTy Impl;

public:
  ExplodedNodeSet(ExplodedNode *N) {
    assert(N && !static_cast<ExplodedNode*>(N)->isSink());
    Impl.insert(N);
  }

  ExplodedNodeSet() = default;

  void Add(ExplodedNode *N) {
    if (N && !static_cast<ExplodedNode*>(N)->isSink()) Impl.insert(N);
  }

  using iterator = ImplTy::iterator;
  using const_iterator = ImplTy::const_iterator;

  unsigned size() const { return Impl.size();  }
  bool empty()    const { return Impl.empty(); }
  bool erase(ExplodedNode *N) { return Impl.remove(N); }

  void clear() { Impl.clear(); }

  void insert(const ExplodedNodeSet &S) {
    assert(&S != this);
    if (empty())
      Impl = S.Impl;
    else
      Impl.insert(S.begin(), S.end());
  }

  iterator begin() { return Impl.begin(); }
  iterator end() { return Impl.end(); }

  const_iterator begin() const { return Impl.begin(); }
  const_iterator end() const { return Impl.end(); }
};

} // namespace ento

} // namespace clang

// GraphTraits

namespace llvm {
  template <> struct GraphTraits<clang::ento::ExplodedGraph *> {
    using GraphTy = clang::ento::ExplodedGraph *;
    using NodeRef = clang::ento::ExplodedNode *;
    using ChildIteratorType = clang::ento::ExplodedNode::succ_iterator;
    using nodes_iterator = llvm::df_iterator<GraphTy>;

    static NodeRef getEntryNode(const GraphTy G) {
      return *G->roots_begin();
    }

    static bool predecessorOfTrivial(NodeRef N) {
      return N->succ_size() == 1 && N->getFirstSucc()->isTrivial();
    }

    static ChildIteratorType child_begin(NodeRef N) {
      if (predecessorOfTrivial(N))
        return child_begin(*N->succ_begin());
      return N->succ_begin();
    }

    static ChildIteratorType child_end(NodeRef N) {
      if (predecessorOfTrivial(N))
        return child_end(N->getFirstSucc());
      return N->succ_end();
    }

    static nodes_iterator nodes_begin(const GraphTy G) {
      return df_begin(G);
    }

    static nodes_iterator nodes_end(const GraphTy G) {
      return df_end(G);
    }
  };
} // namespace llvm

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_EXPLODEDGRAPH_H
