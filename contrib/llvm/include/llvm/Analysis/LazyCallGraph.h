//===- LazyCallGraph.h - Analysis of a Module's call graph ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Implements a lazy call graph analysis and related passes for the new pass
/// manager.
///
/// NB: This is *not* a traditional call graph! It is a graph which models both
/// the current calls and potential calls. As a consequence there are many
/// edges in this call graph that do not correspond to a 'call' or 'invoke'
/// instruction.
///
/// The primary use cases of this graph analysis is to facilitate iterating
/// across the functions of a module in ways that ensure all callees are
/// visited prior to a caller (given any SCC constraints), or vice versa. As
/// such is it particularly well suited to organizing CGSCC optimizations such
/// as inlining, outlining, argument promotion, etc. That is its primary use
/// case and motivates the design. It may not be appropriate for other
/// purposes. The use graph of functions or some other conservative analysis of
/// call instructions may be interesting for optimizations and subsequent
/// analyses which don't work in the context of an overly specified
/// potential-call-edge graph.
///
/// To understand the specific rules and nature of this call graph analysis,
/// see the documentation of the \c LazyCallGraph below.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LAZYCALLGRAPH_H
#define LLVM_ANALYSIS_LAZYCALLGRAPH_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <iterator>
#include <string>
#include <utility>

namespace llvm {

class Module;
class Value;

/// A lazily constructed view of the call graph of a module.
///
/// With the edges of this graph, the motivating constraint that we are
/// attempting to maintain is that function-local optimization, CGSCC-local
/// optimizations, and optimizations transforming a pair of functions connected
/// by an edge in the graph, do not invalidate a bottom-up traversal of the SCC
/// DAG. That is, no optimizations will delete, remove, or add an edge such
/// that functions already visited in a bottom-up order of the SCC DAG are no
/// longer valid to have visited, or such that functions not yet visited in
/// a bottom-up order of the SCC DAG are not required to have already been
/// visited.
///
/// Within this constraint, the desire is to minimize the merge points of the
/// SCC DAG. The greater the fanout of the SCC DAG and the fewer merge points
/// in the SCC DAG, the more independence there is in optimizing within it.
/// There is a strong desire to enable parallelization of optimizations over
/// the call graph, and both limited fanout and merge points will (artificially
/// in some cases) limit the scaling of such an effort.
///
/// To this end, graph represents both direct and any potential resolution to
/// an indirect call edge. Another way to think about it is that it represents
/// both the direct call edges and any direct call edges that might be formed
/// through static optimizations. Specifically, it considers taking the address
/// of a function to be an edge in the call graph because this might be
/// forwarded to become a direct call by some subsequent function-local
/// optimization. The result is that the graph closely follows the use-def
/// edges for functions. Walking "up" the graph can be done by looking at all
/// of the uses of a function.
///
/// The roots of the call graph are the external functions and functions
/// escaped into global variables. Those functions can be called from outside
/// of the module or via unknowable means in the IR -- we may not be able to
/// form even a potential call edge from a function body which may dynamically
/// load the function and call it.
///
/// This analysis still requires updates to remain valid after optimizations
/// which could potentially change the set of potential callees. The
/// constraints it operates under only make the traversal order remain valid.
///
/// The entire analysis must be re-computed if full interprocedural
/// optimizations run at any point. For example, globalopt completely
/// invalidates the information in this analysis.
///
/// FIXME: This class is named LazyCallGraph in a lame attempt to distinguish
/// it from the existing CallGraph. At some point, it is expected that this
/// will be the only call graph and it will be renamed accordingly.
class LazyCallGraph {
public:
  class Node;
  class EdgeSequence;
  class SCC;
  class RefSCC;
  class edge_iterator;
  class call_edge_iterator;

  /// A class used to represent edges in the call graph.
  ///
  /// The lazy call graph models both *call* edges and *reference* edges. Call
  /// edges are much what you would expect, and exist when there is a 'call' or
  /// 'invoke' instruction of some function. Reference edges are also tracked
  /// along side these, and exist whenever any instruction (transitively
  /// through its operands) references a function. All call edges are
  /// inherently reference edges, and so the reference graph forms a superset
  /// of the formal call graph.
  ///
  /// All of these forms of edges are fundamentally represented as outgoing
  /// edges. The edges are stored in the source node and point at the target
  /// node. This allows the edge structure itself to be a very compact data
  /// structure: essentially a tagged pointer.
  class Edge {
  public:
    /// The kind of edge in the graph.
    enum Kind : bool { Ref = false, Call = true };

    Edge();
    explicit Edge(Node &N, Kind K);

    /// Test whether the edge is null.
    ///
    /// This happens when an edge has been deleted. We leave the edge objects
    /// around but clear them.
    explicit operator bool() const;

    /// Returnss the \c Kind of the edge.
    Kind getKind() const;

    /// Test whether the edge represents a direct call to a function.
    ///
    /// This requires that the edge is not null.
    bool isCall() const;

    /// Get the call graph node referenced by this edge.
    ///
    /// This requires that the edge is not null.
    Node &getNode() const;

    /// Get the function referenced by this edge.
    ///
    /// This requires that the edge is not null.
    Function &getFunction() const;

  private:
    friend class LazyCallGraph::EdgeSequence;
    friend class LazyCallGraph::RefSCC;

    PointerIntPair<Node *, 1, Kind> Value;

    void setKind(Kind K) { Value.setInt(K); }
  };

  /// The edge sequence object.
  ///
  /// This typically exists entirely within the node but is exposed as
  /// a separate type because a node doesn't initially have edges. An explicit
  /// population step is required to produce this sequence at first and it is
  /// then cached in the node. It is also used to represent edges entering the
  /// graph from outside the module to model the graph's roots.
  ///
  /// The sequence itself both iterable and indexable. The indexes remain
  /// stable even as the sequence mutates (including removal).
  class EdgeSequence {
    friend class LazyCallGraph;
    friend class LazyCallGraph::Node;
    friend class LazyCallGraph::RefSCC;

    using VectorT = SmallVector<Edge, 4>;
    using VectorImplT = SmallVectorImpl<Edge>;

  public:
    /// An iterator used for the edges to both entry nodes and child nodes.
    class iterator
        : public iterator_adaptor_base<iterator, VectorImplT::iterator,
                                       std::forward_iterator_tag> {
      friend class LazyCallGraph;
      friend class LazyCallGraph::Node;

      VectorImplT::iterator E;

      // Build the iterator for a specific position in the edge list.
      iterator(VectorImplT::iterator BaseI, VectorImplT::iterator E)
          : iterator_adaptor_base(BaseI), E(E) {
        while (I != E && !*I)
          ++I;
      }

    public:
      iterator() = default;

      using iterator_adaptor_base::operator++;
      iterator &operator++() {
        do {
          ++I;
        } while (I != E && !*I);
        return *this;
      }
    };

    /// An iterator over specifically call edges.
    ///
    /// This has the same iteration properties as the \c iterator, but
    /// restricts itself to edges which represent actual calls.
    class call_iterator
        : public iterator_adaptor_base<call_iterator, VectorImplT::iterator,
                                       std::forward_iterator_tag> {
      friend class LazyCallGraph;
      friend class LazyCallGraph::Node;

      VectorImplT::iterator E;

      /// Advance the iterator to the next valid, call edge.
      void advanceToNextEdge() {
        while (I != E && (!*I || !I->isCall()))
          ++I;
      }

      // Build the iterator for a specific position in the edge list.
      call_iterator(VectorImplT::iterator BaseI, VectorImplT::iterator E)
          : iterator_adaptor_base(BaseI), E(E) {
        advanceToNextEdge();
      }

    public:
      call_iterator() = default;

      using iterator_adaptor_base::operator++;
      call_iterator &operator++() {
        ++I;
        advanceToNextEdge();
        return *this;
      }
    };

    iterator begin() { return iterator(Edges.begin(), Edges.end()); }
    iterator end() { return iterator(Edges.end(), Edges.end()); }

    Edge &operator[](int i) { return Edges[i]; }
    Edge &operator[](Node &N) {
      assert(EdgeIndexMap.find(&N) != EdgeIndexMap.end() && "No such edge!");
      auto &E = Edges[EdgeIndexMap.find(&N)->second];
      assert(E && "Dead or null edge!");
      return E;
    }

    Edge *lookup(Node &N) {
      auto EI = EdgeIndexMap.find(&N);
      if (EI == EdgeIndexMap.end())
        return nullptr;
      auto &E = Edges[EI->second];
      return E ? &E : nullptr;
    }

    call_iterator call_begin() {
      return call_iterator(Edges.begin(), Edges.end());
    }
    call_iterator call_end() { return call_iterator(Edges.end(), Edges.end()); }

    iterator_range<call_iterator> calls() {
      return make_range(call_begin(), call_end());
    }

    bool empty() {
      for (auto &E : Edges)
        if (E)
          return false;

      return true;
    }

  private:
    VectorT Edges;
    DenseMap<Node *, int> EdgeIndexMap;

    EdgeSequence() = default;

    /// Internal helper to insert an edge to a node.
    void insertEdgeInternal(Node &ChildN, Edge::Kind EK);

    /// Internal helper to change an edge kind.
    void setEdgeKind(Node &ChildN, Edge::Kind EK);

    /// Internal helper to remove the edge to the given function.
    bool removeEdgeInternal(Node &ChildN);

    /// Internal helper to replace an edge key with a new one.
    ///
    /// This should be used when the function for a particular node in the
    /// graph gets replaced and we are updating all of the edges to that node
    /// to use the new function as the key.
    void replaceEdgeKey(Function &OldTarget, Function &NewTarget);
  };

  /// A node in the call graph.
  ///
  /// This represents a single node. It's primary roles are to cache the list of
  /// callees, de-duplicate and provide fast testing of whether a function is
  /// a callee, and facilitate iteration of child nodes in the graph.
  ///
  /// The node works much like an optional in order to lazily populate the
  /// edges of each node. Until populated, there are no edges. Once populated,
  /// you can access the edges by dereferencing the node or using the `->`
  /// operator as if the node was an `Optional<EdgeSequence>`.
  class Node {
    friend class LazyCallGraph;
    friend class LazyCallGraph::RefSCC;

  public:
    LazyCallGraph &getGraph() const { return *G; }

    Function &getFunction() const { return *F; }

    StringRef getName() const { return F->getName(); }

    /// Equality is defined as address equality.
    bool operator==(const Node &N) const { return this == &N; }
    bool operator!=(const Node &N) const { return !operator==(N); }

    /// Tests whether the node has been populated with edges.
    bool isPopulated() const { return Edges.hasValue(); }

    /// Tests whether this is actually a dead node and no longer valid.
    ///
    /// Users rarely interact with nodes in this state and other methods are
    /// invalid. This is used to model a node in an edge list where the
    /// function has been completely removed.
    bool isDead() const {
      assert(!G == !F &&
             "Both graph and function pointers should be null or non-null.");
      return !G;
    }

    // We allow accessing the edges by dereferencing or using the arrow
    // operator, essentially wrapping the internal optional.
    EdgeSequence &operator*() const {
      // Rip const off because the node itself isn't changing here.
      return const_cast<EdgeSequence &>(*Edges);
    }
    EdgeSequence *operator->() const { return &**this; }

    /// Populate the edges of this node if necessary.
    ///
    /// The first time this is called it will populate the edges for this node
    /// in the graph. It does this by scanning the underlying function, so once
    /// this is done, any changes to that function must be explicitly reflected
    /// in updates to the graph.
    ///
    /// \returns the populated \c EdgeSequence to simplify walking it.
    ///
    /// This will not update or re-scan anything if called repeatedly. Instead,
    /// the edge sequence is cached and returned immediately on subsequent
    /// calls.
    EdgeSequence &populate() {
      if (Edges)
        return *Edges;

      return populateSlow();
    }

  private:
    LazyCallGraph *G;
    Function *F;

    // We provide for the DFS numbering and Tarjan walk lowlink numbers to be
    // stored directly within the node. These are both '-1' when nodes are part
    // of an SCC (or RefSCC), or '0' when not yet reached in a DFS walk.
    int DFSNumber = 0;
    int LowLink = 0;

    Optional<EdgeSequence> Edges;

    /// Basic constructor implements the scanning of F into Edges and
    /// EdgeIndexMap.
    Node(LazyCallGraph &G, Function &F) : G(&G), F(&F) {}

    /// Implementation of the scan when populating.
    EdgeSequence &populateSlow();

    /// Internal helper to directly replace the function with a new one.
    ///
    /// This is used to facilitate tranfsormations which need to replace the
    /// formal Function object but directly move the body and users from one to
    /// the other.
    void replaceFunction(Function &NewF);

    void clear() { Edges.reset(); }

    /// Print the name of this node's function.
    friend raw_ostream &operator<<(raw_ostream &OS, const Node &N) {
      return OS << N.F->getName();
    }

    /// Dump the name of this node's function to stderr.
    void dump() const;
  };

  /// An SCC of the call graph.
  ///
  /// This represents a Strongly Connected Component of the direct call graph
  /// -- ignoring indirect calls and function references. It stores this as
  /// a collection of call graph nodes. While the order of nodes in the SCC is
  /// stable, it is not any particular order.
  ///
  /// The SCCs are nested within a \c RefSCC, see below for details about that
  /// outer structure. SCCs do not support mutation of the call graph, that
  /// must be done through the containing \c RefSCC in order to fully reason
  /// about the ordering and connections of the graph.
  class SCC {
    friend class LazyCallGraph;
    friend class LazyCallGraph::Node;

    RefSCC *OuterRefSCC;
    SmallVector<Node *, 1> Nodes;

    template <typename NodeRangeT>
    SCC(RefSCC &OuterRefSCC, NodeRangeT &&Nodes)
        : OuterRefSCC(&OuterRefSCC), Nodes(std::forward<NodeRangeT>(Nodes)) {}

    void clear() {
      OuterRefSCC = nullptr;
      Nodes.clear();
    }

    /// Print a short descrtiption useful for debugging or logging.
    ///
    /// We print the function names in the SCC wrapped in '()'s and skipping
    /// the middle functions if there are a large number.
    //
    // Note: this is defined inline to dodge issues with GCC's interpretation
    // of enclosing namespaces for friend function declarations.
    friend raw_ostream &operator<<(raw_ostream &OS, const SCC &C) {
      OS << '(';
      int i = 0;
      for (LazyCallGraph::Node &N : C) {
        if (i > 0)
          OS << ", ";
        // Elide the inner elements if there are too many.
        if (i > 8) {
          OS << "..., " << *C.Nodes.back();
          break;
        }
        OS << N;
        ++i;
      }
      OS << ')';
      return OS;
    }

    /// Dump a short description of this SCC to stderr.
    void dump() const;

#ifndef NDEBUG
    /// Verify invariants about the SCC.
    ///
    /// This will attempt to validate all of the basic invariants within an
    /// SCC, but not that it is a strongly connected componet per-se. Primarily
    /// useful while building and updating the graph to check that basic
    /// properties are in place rather than having inexplicable crashes later.
    void verify();
#endif

  public:
    using iterator = pointee_iterator<SmallVectorImpl<Node *>::const_iterator>;

    iterator begin() const { return Nodes.begin(); }
    iterator end() const { return Nodes.end(); }

    int size() const { return Nodes.size(); }

    RefSCC &getOuterRefSCC() const { return *OuterRefSCC; }

    /// Test if this SCC is a parent of \a C.
    ///
    /// Note that this is linear in the number of edges departing the current
    /// SCC.
    bool isParentOf(const SCC &C) const;

    /// Test if this SCC is an ancestor of \a C.
    ///
    /// Note that in the worst case this is linear in the number of edges
    /// departing the current SCC and every SCC in the entire graph reachable
    /// from this SCC. Thus this very well may walk every edge in the entire
    /// call graph! Do not call this in a tight loop!
    bool isAncestorOf(const SCC &C) const;

    /// Test if this SCC is a child of \a C.
    ///
    /// See the comments for \c isParentOf for detailed notes about the
    /// complexity of this routine.
    bool isChildOf(const SCC &C) const { return C.isParentOf(*this); }

    /// Test if this SCC is a descendant of \a C.
    ///
    /// See the comments for \c isParentOf for detailed notes about the
    /// complexity of this routine.
    bool isDescendantOf(const SCC &C) const { return C.isAncestorOf(*this); }

    /// Provide a short name by printing this SCC to a std::string.
    ///
    /// This copes with the fact that we don't have a name per-se for an SCC
    /// while still making the use of this in debugging and logging useful.
    std::string getName() const {
      std::string Name;
      raw_string_ostream OS(Name);
      OS << *this;
      OS.flush();
      return Name;
    }
  };

  /// A RefSCC of the call graph.
  ///
  /// This models a Strongly Connected Component of function reference edges in
  /// the call graph. As opposed to actual SCCs, these can be used to scope
  /// subgraphs of the module which are independent from other subgraphs of the
  /// module because they do not reference it in any way. This is also the unit
  /// where we do mutation of the graph in order to restrict mutations to those
  /// which don't violate this independence.
  ///
  /// A RefSCC contains a DAG of actual SCCs. All the nodes within the RefSCC
  /// are necessarily within some actual SCC that nests within it. Since
  /// a direct call *is* a reference, there will always be at least one RefSCC
  /// around any SCC.
  class RefSCC {
    friend class LazyCallGraph;
    friend class LazyCallGraph::Node;

    LazyCallGraph *G;

    /// A postorder list of the inner SCCs.
    SmallVector<SCC *, 4> SCCs;

    /// A map from SCC to index in the postorder list.
    SmallDenseMap<SCC *, int, 4> SCCIndices;

    /// Fast-path constructor. RefSCCs should instead be constructed by calling
    /// formRefSCCFast on the graph itself.
    RefSCC(LazyCallGraph &G);

    void clear() {
      SCCs.clear();
      SCCIndices.clear();
    }

    /// Print a short description useful for debugging or logging.
    ///
    /// We print the SCCs wrapped in '[]'s and skipping the middle SCCs if
    /// there are a large number.
    //
    // Note: this is defined inline to dodge issues with GCC's interpretation
    // of enclosing namespaces for friend function declarations.
    friend raw_ostream &operator<<(raw_ostream &OS, const RefSCC &RC) {
      OS << '[';
      int i = 0;
      for (LazyCallGraph::SCC &C : RC) {
        if (i > 0)
          OS << ", ";
        // Elide the inner elements if there are too many.
        if (i > 4) {
          OS << "..., " << *RC.SCCs.back();
          break;
        }
        OS << C;
        ++i;
      }
      OS << ']';
      return OS;
    }

    /// Dump a short description of this RefSCC to stderr.
    void dump() const;

#ifndef NDEBUG
    /// Verify invariants about the RefSCC and all its SCCs.
    ///
    /// This will attempt to validate all of the invariants *within* the
    /// RefSCC, but not that it is a strongly connected component of the larger
    /// graph. This makes it useful even when partially through an update.
    ///
    /// Invariants checked:
    /// - SCCs and their indices match.
    /// - The SCCs list is in fact in post-order.
    void verify();
#endif

    /// Handle any necessary parent set updates after inserting a trivial ref
    /// or call edge.
    void handleTrivialEdgeInsertion(Node &SourceN, Node &TargetN);

  public:
    using iterator = pointee_iterator<SmallVectorImpl<SCC *>::const_iterator>;
    using range = iterator_range<iterator>;
    using parent_iterator =
        pointee_iterator<SmallPtrSetImpl<RefSCC *>::const_iterator>;

    iterator begin() const { return SCCs.begin(); }
    iterator end() const { return SCCs.end(); }

    ssize_t size() const { return SCCs.size(); }

    SCC &operator[](int Idx) { return *SCCs[Idx]; }

    iterator find(SCC &C) const {
      return SCCs.begin() + SCCIndices.find(&C)->second;
    }

    /// Test if this RefSCC is a parent of \a RC.
    ///
    /// CAUTION: This method walks every edge in the \c RefSCC, it can be very
    /// expensive.
    bool isParentOf(const RefSCC &RC) const;

    /// Test if this RefSCC is an ancestor of \a RC.
    ///
    /// CAUTION: This method walks the directed graph of edges as far as
    /// necessary to find a possible path to the argument. In the worst case
    /// this may walk the entire graph and can be extremely expensive.
    bool isAncestorOf(const RefSCC &RC) const;

    /// Test if this RefSCC is a child of \a RC.
    ///
    /// CAUTION: This method walks every edge in the argument \c RefSCC, it can
    /// be very expensive.
    bool isChildOf(const RefSCC &RC) const { return RC.isParentOf(*this); }

    /// Test if this RefSCC is a descendant of \a RC.
    ///
    /// CAUTION: This method walks the directed graph of edges as far as
    /// necessary to find a possible path from the argument. In the worst case
    /// this may walk the entire graph and can be extremely expensive.
    bool isDescendantOf(const RefSCC &RC) const {
      return RC.isAncestorOf(*this);
    }

    /// Provide a short name by printing this RefSCC to a std::string.
    ///
    /// This copes with the fact that we don't have a name per-se for an RefSCC
    /// while still making the use of this in debugging and logging useful.
    std::string getName() const {
      std::string Name;
      raw_string_ostream OS(Name);
      OS << *this;
      OS.flush();
      return Name;
    }

    ///@{
    /// \name Mutation API
    ///
    /// These methods provide the core API for updating the call graph in the
    /// presence of (potentially still in-flight) DFS-found RefSCCs and SCCs.
    ///
    /// Note that these methods sometimes have complex runtimes, so be careful
    /// how you call them.

    /// Make an existing internal ref edge into a call edge.
    ///
    /// This may form a larger cycle and thus collapse SCCs into TargetN's SCC.
    /// If that happens, the optional callback \p MergedCB will be invoked (if
    /// provided) on the SCCs being merged away prior to actually performing
    /// the merge. Note that this will never include the target SCC as that
    /// will be the SCC functions are merged into to resolve the cycle. Once
    /// this function returns, these merged SCCs are not in a valid state but
    /// the pointers will remain valid until destruction of the parent graph
    /// instance for the purpose of clearing cached information. This function
    /// also returns 'true' if a cycle was formed and some SCCs merged away as
    /// a convenience.
    ///
    /// After this operation, both SourceN's SCC and TargetN's SCC may move
    /// position within this RefSCC's postorder list. Any SCCs merged are
    /// merged into the TargetN's SCC in order to preserve reachability analyses
    /// which took place on that SCC.
    bool switchInternalEdgeToCall(
        Node &SourceN, Node &TargetN,
        function_ref<void(ArrayRef<SCC *> MergedSCCs)> MergeCB = {});

    /// Make an existing internal call edge between separate SCCs into a ref
    /// edge.
    ///
    /// If SourceN and TargetN in separate SCCs within this RefSCC, changing
    /// the call edge between them to a ref edge is a trivial operation that
    /// does not require any structural changes to the call graph.
    void switchTrivialInternalEdgeToRef(Node &SourceN, Node &TargetN);

    /// Make an existing internal call edge within a single SCC into a ref
    /// edge.
    ///
    /// Since SourceN and TargetN are part of a single SCC, this SCC may be
    /// split up due to breaking a cycle in the call edges that formed it. If
    /// that happens, then this routine will insert new SCCs into the postorder
    /// list *before* the SCC of TargetN (previously the SCC of both). This
    /// preserves postorder as the TargetN can reach all of the other nodes by
    /// definition of previously being in a single SCC formed by the cycle from
    /// SourceN to TargetN.
    ///
    /// The newly added SCCs are added *immediately* and contiguously
    /// prior to the TargetN SCC and return the range covering the new SCCs in
    /// the RefSCC's postorder sequence. You can directly iterate the returned
    /// range to observe all of the new SCCs in postorder.
    ///
    /// Note that if SourceN and TargetN are in separate SCCs, the simpler
    /// routine `switchTrivialInternalEdgeToRef` should be used instead.
    iterator_range<iterator> switchInternalEdgeToRef(Node &SourceN,
                                                     Node &TargetN);

    /// Make an existing outgoing ref edge into a call edge.
    ///
    /// Note that this is trivial as there are no cyclic impacts and there
    /// remains a reference edge.
    void switchOutgoingEdgeToCall(Node &SourceN, Node &TargetN);

    /// Make an existing outgoing call edge into a ref edge.
    ///
    /// This is trivial as there are no cyclic impacts and there remains
    /// a reference edge.
    void switchOutgoingEdgeToRef(Node &SourceN, Node &TargetN);

    /// Insert a ref edge from one node in this RefSCC to another in this
    /// RefSCC.
    ///
    /// This is always a trivial operation as it doesn't change any part of the
    /// graph structure besides connecting the two nodes.
    ///
    /// Note that we don't support directly inserting internal *call* edges
    /// because that could change the graph structure and requires returning
    /// information about what became invalid. As a consequence, the pattern
    /// should be to first insert the necessary ref edge, and then to switch it
    /// to a call edge if needed and handle any invalidation that results. See
    /// the \c switchInternalEdgeToCall routine for details.
    void insertInternalRefEdge(Node &SourceN, Node &TargetN);

    /// Insert an edge whose parent is in this RefSCC and child is in some
    /// child RefSCC.
    ///
    /// There must be an existing path from the \p SourceN to the \p TargetN.
    /// This operation is inexpensive and does not change the set of SCCs and
    /// RefSCCs in the graph.
    void insertOutgoingEdge(Node &SourceN, Node &TargetN, Edge::Kind EK);

    /// Insert an edge whose source is in a descendant RefSCC and target is in
    /// this RefSCC.
    ///
    /// There must be an existing path from the target to the source in this
    /// case.
    ///
    /// NB! This is has the potential to be a very expensive function. It
    /// inherently forms a cycle in the prior RefSCC DAG and we have to merge
    /// RefSCCs to resolve that cycle. But finding all of the RefSCCs which
    /// participate in the cycle can in the worst case require traversing every
    /// RefSCC in the graph. Every attempt is made to avoid that, but passes
    /// must still exercise caution calling this routine repeatedly.
    ///
    /// Also note that this can only insert ref edges. In order to insert
    /// a call edge, first insert a ref edge and then switch it to a call edge.
    /// These are intentionally kept as separate interfaces because each step
    /// of the operation invalidates a different set of data structures.
    ///
    /// This returns all the RefSCCs which were merged into the this RefSCC
    /// (the target's). This allows callers to invalidate any cached
    /// information.
    ///
    /// FIXME: We could possibly optimize this quite a bit for cases where the
    /// caller and callee are very nearby in the graph. See comments in the
    /// implementation for details, but that use case might impact users.
    SmallVector<RefSCC *, 1> insertIncomingRefEdge(Node &SourceN,
                                                   Node &TargetN);

    /// Remove an edge whose source is in this RefSCC and target is *not*.
    ///
    /// This removes an inter-RefSCC edge. All inter-RefSCC edges originating
    /// from this SCC have been fully explored by any in-flight DFS graph
    /// formation, so this is always safe to call once you have the source
    /// RefSCC.
    ///
    /// This operation does not change the cyclic structure of the graph and so
    /// is very inexpensive. It may change the connectivity graph of the SCCs
    /// though, so be careful calling this while iterating over them.
    void removeOutgoingEdge(Node &SourceN, Node &TargetN);

    /// Remove a list of ref edges which are entirely within this RefSCC.
    ///
    /// Both the \a SourceN and all of the \a TargetNs must be within this
    /// RefSCC. Removing these edges may break cycles that form this RefSCC and
    /// thus this operation may change the RefSCC graph significantly. In
    /// particular, this operation will re-form new RefSCCs based on the
    /// remaining connectivity of the graph. The following invariants are
    /// guaranteed to hold after calling this method:
    ///
    /// 1) If a ref-cycle remains after removal, it leaves this RefSCC intact
    ///    and in the graph. No new RefSCCs are built.
    /// 2) Otherwise, this RefSCC will be dead after this call and no longer in
    ///    the graph or the postorder traversal of the call graph. Any iterator
    ///    pointing at this RefSCC will become invalid.
    /// 3) All newly formed RefSCCs will be returned and the order of the
    ///    RefSCCs returned will be a valid postorder traversal of the new
    ///    RefSCCs.
    /// 4) No RefSCC other than this RefSCC has its member set changed (this is
    ///    inherent in the definition of removing such an edge).
    ///
    /// These invariants are very important to ensure that we can build
    /// optimization pipelines on top of the CGSCC pass manager which
    /// intelligently update the RefSCC graph without invalidating other parts
    /// of the RefSCC graph.
    ///
    /// Note that we provide no routine to remove a *call* edge. Instead, you
    /// must first switch it to a ref edge using \c switchInternalEdgeToRef.
    /// This split API is intentional as each of these two steps can invalidate
    /// a different aspect of the graph structure and needs to have the
    /// invalidation handled independently.
    ///
    /// The runtime complexity of this method is, in the worst case, O(V+E)
    /// where V is the number of nodes in this RefSCC and E is the number of
    /// edges leaving the nodes in this RefSCC. Note that E includes both edges
    /// within this RefSCC and edges from this RefSCC to child RefSCCs. Some
    /// effort has been made to minimize the overhead of common cases such as
    /// self-edges and edge removals which result in a spanning tree with no
    /// more cycles.
    SmallVector<RefSCC *, 1> removeInternalRefEdge(Node &SourceN,
                                                   ArrayRef<Node *> TargetNs);

    /// A convenience wrapper around the above to handle trivial cases of
    /// inserting a new call edge.
    ///
    /// This is trivial whenever the target is in the same SCC as the source or
    /// the edge is an outgoing edge to some descendant SCC. In these cases
    /// there is no change to the cyclic structure of SCCs or RefSCCs.
    ///
    /// To further make calling this convenient, it also handles inserting
    /// already existing edges.
    void insertTrivialCallEdge(Node &SourceN, Node &TargetN);

    /// A convenience wrapper around the above to handle trivial cases of
    /// inserting a new ref edge.
    ///
    /// This is trivial whenever the target is in the same RefSCC as the source
    /// or the edge is an outgoing edge to some descendant RefSCC. In these
    /// cases there is no change to the cyclic structure of the RefSCCs.
    ///
    /// To further make calling this convenient, it also handles inserting
    /// already existing edges.
    void insertTrivialRefEdge(Node &SourceN, Node &TargetN);

    /// Directly replace a node's function with a new function.
    ///
    /// This should be used when moving the body and users of a function to
    /// a new formal function object but not otherwise changing the call graph
    /// structure in any way.
    ///
    /// It requires that the old function in the provided node have zero uses
    /// and the new function must have calls and references to it establishing
    /// an equivalent graph.
    void replaceNodeFunction(Node &N, Function &NewF);

    ///@}
  };

  /// A post-order depth-first RefSCC iterator over the call graph.
  ///
  /// This iterator walks the cached post-order sequence of RefSCCs. However,
  /// it trades stability for flexibility. It is restricted to a forward
  /// iterator but will survive mutations which insert new RefSCCs and continue
  /// to point to the same RefSCC even if it moves in the post-order sequence.
  class postorder_ref_scc_iterator
      : public iterator_facade_base<postorder_ref_scc_iterator,
                                    std::forward_iterator_tag, RefSCC> {
    friend class LazyCallGraph;
    friend class LazyCallGraph::Node;

    /// Nonce type to select the constructor for the end iterator.
    struct IsAtEndT {};

    LazyCallGraph *G;
    RefSCC *RC = nullptr;

    /// Build the begin iterator for a node.
    postorder_ref_scc_iterator(LazyCallGraph &G) : G(&G), RC(getRC(G, 0)) {}

    /// Build the end iterator for a node. This is selected purely by overload.
    postorder_ref_scc_iterator(LazyCallGraph &G, IsAtEndT /*Nonce*/) : G(&G) {}

    /// Get the post-order RefSCC at the given index of the postorder walk,
    /// populating it if necessary.
    static RefSCC *getRC(LazyCallGraph &G, int Index) {
      if (Index == (int)G.PostOrderRefSCCs.size())
        // We're at the end.
        return nullptr;

      return G.PostOrderRefSCCs[Index];
    }

  public:
    bool operator==(const postorder_ref_scc_iterator &Arg) const {
      return G == Arg.G && RC == Arg.RC;
    }

    reference operator*() const { return *RC; }

    using iterator_facade_base::operator++;
    postorder_ref_scc_iterator &operator++() {
      assert(RC && "Cannot increment the end iterator!");
      RC = getRC(*G, G->RefSCCIndices.find(RC)->second + 1);
      return *this;
    }
  };

  /// Construct a graph for the given module.
  ///
  /// This sets up the graph and computes all of the entry points of the graph.
  /// No function definitions are scanned until their nodes in the graph are
  /// requested during traversal.
  LazyCallGraph(Module &M, TargetLibraryInfo &TLI);

  LazyCallGraph(LazyCallGraph &&G);
  LazyCallGraph &operator=(LazyCallGraph &&RHS);

  EdgeSequence::iterator begin() { return EntryEdges.begin(); }
  EdgeSequence::iterator end() { return EntryEdges.end(); }

  void buildRefSCCs();

  postorder_ref_scc_iterator postorder_ref_scc_begin() {
    if (!EntryEdges.empty())
      assert(!PostOrderRefSCCs.empty() &&
             "Must form RefSCCs before iterating them!");
    return postorder_ref_scc_iterator(*this);
  }
  postorder_ref_scc_iterator postorder_ref_scc_end() {
    if (!EntryEdges.empty())
      assert(!PostOrderRefSCCs.empty() &&
             "Must form RefSCCs before iterating them!");
    return postorder_ref_scc_iterator(*this,
                                      postorder_ref_scc_iterator::IsAtEndT());
  }

  iterator_range<postorder_ref_scc_iterator> postorder_ref_sccs() {
    return make_range(postorder_ref_scc_begin(), postorder_ref_scc_end());
  }

  /// Lookup a function in the graph which has already been scanned and added.
  Node *lookup(const Function &F) const { return NodeMap.lookup(&F); }

  /// Lookup a function's SCC in the graph.
  ///
  /// \returns null if the function hasn't been assigned an SCC via the RefSCC
  /// iterator walk.
  SCC *lookupSCC(Node &N) const { return SCCMap.lookup(&N); }

  /// Lookup a function's RefSCC in the graph.
  ///
  /// \returns null if the function hasn't been assigned a RefSCC via the
  /// RefSCC iterator walk.
  RefSCC *lookupRefSCC(Node &N) const {
    if (SCC *C = lookupSCC(N))
      return &C->getOuterRefSCC();

    return nullptr;
  }

  /// Get a graph node for a given function, scanning it to populate the graph
  /// data as necessary.
  Node &get(Function &F) {
    Node *&N = NodeMap[&F];
    if (N)
      return *N;

    return insertInto(F, N);
  }

  /// Get the sequence of known and defined library functions.
  ///
  /// These functions, because they are known to LLVM, can have calls
  /// introduced out of thin air from arbitrary IR.
  ArrayRef<Function *> getLibFunctions() const {
    return LibFunctions.getArrayRef();
  }

  /// Test whether a function is a known and defined library function tracked by
  /// the call graph.
  ///
  /// Because these functions are known to LLVM they are specially modeled in
  /// the call graph and even when all IR-level references have been removed
  /// remain active and reachable.
  bool isLibFunction(Function &F) const { return LibFunctions.count(&F); }

  ///@{
  /// \name Pre-SCC Mutation API
  ///
  /// These methods are only valid to call prior to forming any SCCs for this
  /// call graph. They can be used to update the core node-graph during
  /// a node-based inorder traversal that precedes any SCC-based traversal.
  ///
  /// Once you begin manipulating a call graph's SCCs, most mutation of the
  /// graph must be performed via a RefSCC method. There are some exceptions
  /// below.

  /// Update the call graph after inserting a new edge.
  void insertEdge(Node &SourceN, Node &TargetN, Edge::Kind EK);

  /// Update the call graph after inserting a new edge.
  void insertEdge(Function &Source, Function &Target, Edge::Kind EK) {
    return insertEdge(get(Source), get(Target), EK);
  }

  /// Update the call graph after deleting an edge.
  void removeEdge(Node &SourceN, Node &TargetN);

  /// Update the call graph after deleting an edge.
  void removeEdge(Function &Source, Function &Target) {
    return removeEdge(get(Source), get(Target));
  }

  ///@}

  ///@{
  /// \name General Mutation API
  ///
  /// There are a very limited set of mutations allowed on the graph as a whole
  /// once SCCs have started to be formed. These routines have strict contracts
  /// but may be called at any point.

  /// Remove a dead function from the call graph (typically to delete it).
  ///
  /// Note that the function must have an empty use list, and the call graph
  /// must be up-to-date prior to calling this. That means it is by itself in
  /// a maximal SCC which is by itself in a maximal RefSCC, etc. No structural
  /// changes result from calling this routine other than potentially removing
  /// entry points into the call graph.
  ///
  /// If SCC formation has begun, this function must not be part of the current
  /// DFS in order to call this safely. Typically, the function will have been
  /// fully visited by the DFS prior to calling this routine.
  void removeDeadFunction(Function &F);

  ///@}

  ///@{
  /// \name Static helpers for code doing updates to the call graph.
  ///
  /// These helpers are used to implement parts of the call graph but are also
  /// useful to code doing updates or otherwise wanting to walk the IR in the
  /// same patterns as when we build the call graph.

  /// Recursively visits the defined functions whose address is reachable from
  /// every constant in the \p Worklist.
  ///
  /// Doesn't recurse through any constants already in the \p Visited set, and
  /// updates that set with every constant visited.
  ///
  /// For each defined function, calls \p Callback with that function.
  template <typename CallbackT>
  static void visitReferences(SmallVectorImpl<Constant *> &Worklist,
                              SmallPtrSetImpl<Constant *> &Visited,
                              CallbackT Callback) {
    while (!Worklist.empty()) {
      Constant *C = Worklist.pop_back_val();

      if (Function *F = dyn_cast<Function>(C)) {
        if (!F->isDeclaration())
          Callback(*F);
        continue;
      }

      if (BlockAddress *BA = dyn_cast<BlockAddress>(C)) {
        // The blockaddress constant expression is a weird special case, we
        // can't generically walk its operands the way we do for all other
        // constants.
        if (Visited.insert(BA->getFunction()).second)
          Worklist.push_back(BA->getFunction());
        continue;
      }

      for (Value *Op : C->operand_values())
        if (Visited.insert(cast<Constant>(Op)).second)
          Worklist.push_back(cast<Constant>(Op));
    }
  }

  ///@}

private:
  using node_stack_iterator = SmallVectorImpl<Node *>::reverse_iterator;
  using node_stack_range = iterator_range<node_stack_iterator>;

  /// Allocator that holds all the call graph nodes.
  SpecificBumpPtrAllocator<Node> BPA;

  /// Maps function->node for fast lookup.
  DenseMap<const Function *, Node *> NodeMap;

  /// The entry edges into the graph.
  ///
  /// These edges are from "external" sources. Put another way, they
  /// escape at the module scope.
  EdgeSequence EntryEdges;

  /// Allocator that holds all the call graph SCCs.
  SpecificBumpPtrAllocator<SCC> SCCBPA;

  /// Maps Function -> SCC for fast lookup.
  DenseMap<Node *, SCC *> SCCMap;

  /// Allocator that holds all the call graph RefSCCs.
  SpecificBumpPtrAllocator<RefSCC> RefSCCBPA;

  /// The post-order sequence of RefSCCs.
  ///
  /// This list is lazily formed the first time we walk the graph.
  SmallVector<RefSCC *, 16> PostOrderRefSCCs;

  /// A map from RefSCC to the index for it in the postorder sequence of
  /// RefSCCs.
  DenseMap<RefSCC *, int> RefSCCIndices;

  /// Defined functions that are also known library functions which the
  /// optimizer can reason about and therefore might introduce calls to out of
  /// thin air.
  SmallSetVector<Function *, 4> LibFunctions;

  /// Helper to insert a new function, with an already looked-up entry in
  /// the NodeMap.
  Node &insertInto(Function &F, Node *&MappedN);

  /// Helper to update pointers back to the graph object during moves.
  void updateGraphPtrs();

  /// Allocates an SCC and constructs it using the graph allocator.
  ///
  /// The arguments are forwarded to the constructor.
  template <typename... Ts> SCC *createSCC(Ts &&... Args) {
    return new (SCCBPA.Allocate()) SCC(std::forward<Ts>(Args)...);
  }

  /// Allocates a RefSCC and constructs it using the graph allocator.
  ///
  /// The arguments are forwarded to the constructor.
  template <typename... Ts> RefSCC *createRefSCC(Ts &&... Args) {
    return new (RefSCCBPA.Allocate()) RefSCC(std::forward<Ts>(Args)...);
  }

  /// Common logic for building SCCs from a sequence of roots.
  ///
  /// This is a very generic implementation of the depth-first walk and SCC
  /// formation algorithm. It uses a generic sequence of roots and generic
  /// callbacks for each step. This is designed to be used to implement both
  /// the RefSCC formation and SCC formation with shared logic.
  ///
  /// Currently this is a relatively naive implementation of Tarjan's DFS
  /// algorithm to form the SCCs.
  ///
  /// FIXME: We should consider newer variants such as Nuutila.
  template <typename RootsT, typename GetBeginT, typename GetEndT,
            typename GetNodeT, typename FormSCCCallbackT>
  static void buildGenericSCCs(RootsT &&Roots, GetBeginT &&GetBegin,
                               GetEndT &&GetEnd, GetNodeT &&GetNode,
                               FormSCCCallbackT &&FormSCC);

  /// Build the SCCs for a RefSCC out of a list of nodes.
  void buildSCCs(RefSCC &RC, node_stack_range Nodes);

  /// Get the index of a RefSCC within the postorder traversal.
  ///
  /// Requires that this RefSCC is a valid one in the (perhaps partial)
  /// postorder traversed part of the graph.
  int getRefSCCIndex(RefSCC &RC) {
    auto IndexIt = RefSCCIndices.find(&RC);
    assert(IndexIt != RefSCCIndices.end() && "RefSCC doesn't have an index!");
    assert(PostOrderRefSCCs[IndexIt->second] == &RC &&
           "Index does not point back at RC!");
    return IndexIt->second;
  }
};

inline LazyCallGraph::Edge::Edge() : Value() {}
inline LazyCallGraph::Edge::Edge(Node &N, Kind K) : Value(&N, K) {}

inline LazyCallGraph::Edge::operator bool() const {
  return Value.getPointer() && !Value.getPointer()->isDead();
}

inline LazyCallGraph::Edge::Kind LazyCallGraph::Edge::getKind() const {
  assert(*this && "Queried a null edge!");
  return Value.getInt();
}

inline bool LazyCallGraph::Edge::isCall() const {
  assert(*this && "Queried a null edge!");
  return getKind() == Call;
}

inline LazyCallGraph::Node &LazyCallGraph::Edge::getNode() const {
  assert(*this && "Queried a null edge!");
  return *Value.getPointer();
}

inline Function &LazyCallGraph::Edge::getFunction() const {
  assert(*this && "Queried a null edge!");
  return getNode().getFunction();
}

// Provide GraphTraits specializations for call graphs.
template <> struct GraphTraits<LazyCallGraph::Node *> {
  using NodeRef = LazyCallGraph::Node *;
  using ChildIteratorType = LazyCallGraph::EdgeSequence::iterator;

  static NodeRef getEntryNode(NodeRef N) { return N; }
  static ChildIteratorType child_begin(NodeRef N) { return (*N)->begin(); }
  static ChildIteratorType child_end(NodeRef N) { return (*N)->end(); }
};
template <> struct GraphTraits<LazyCallGraph *> {
  using NodeRef = LazyCallGraph::Node *;
  using ChildIteratorType = LazyCallGraph::EdgeSequence::iterator;

  static NodeRef getEntryNode(NodeRef N) { return N; }
  static ChildIteratorType child_begin(NodeRef N) { return (*N)->begin(); }
  static ChildIteratorType child_end(NodeRef N) { return (*N)->end(); }
};

/// An analysis pass which computes the call graph for a module.
class LazyCallGraphAnalysis : public AnalysisInfoMixin<LazyCallGraphAnalysis> {
  friend AnalysisInfoMixin<LazyCallGraphAnalysis>;

  static AnalysisKey Key;

public:
  /// Inform generic clients of the result type.
  using Result = LazyCallGraph;

  /// Compute the \c LazyCallGraph for the module \c M.
  ///
  /// This just builds the set of entry points to the call graph. The rest is
  /// built lazily as it is walked.
  LazyCallGraph run(Module &M, ModuleAnalysisManager &AM) {
    return LazyCallGraph(M, AM.getResult<TargetLibraryAnalysis>(M));
  }
};

/// A pass which prints the call graph to a \c raw_ostream.
///
/// This is primarily useful for testing the analysis.
class LazyCallGraphPrinterPass
    : public PassInfoMixin<LazyCallGraphPrinterPass> {
  raw_ostream &OS;

public:
  explicit LazyCallGraphPrinterPass(raw_ostream &OS);

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// A pass which prints the call graph as a DOT file to a \c raw_ostream.
///
/// This is primarily useful for visualization purposes.
class LazyCallGraphDOTPrinterPass
    : public PassInfoMixin<LazyCallGraphDOTPrinterPass> {
  raw_ostream &OS;

public:
  explicit LazyCallGraphDOTPrinterPass(raw_ostream &OS);

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_LAZYCALLGRAPH_H
