//===-- Graph.h - XRay Graph Class ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// A Graph Datatype for XRay.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_XRAY_GRAPH_T_H
#define LLVM_XRAY_GRAPH_T_H

#include <initializer_list>
#include <stdint.h>
#include <type_traits>
#include <utility>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace xray {

/// A Graph object represents a Directed Graph and is used in XRay to compute
/// and store function call graphs and associated statistical information.
///
/// The graph takes in four template parameters, these are:
///  - VertexAttribute, this is a structure which is stored for each vertex.
///    Must be DefaultConstructible, CopyConstructible, CopyAssignable and
///    Destructible.
///  - EdgeAttribute, this is a structure which is stored for each edge
///    Must be DefaultConstructible, CopyConstructible, CopyAssignable and
///    Destructible.
///  - EdgeAttribute, this is a structure which is stored for each variable
///  - VI, this is a type over which DenseMapInfo is defined and is the type
///    used look up strings, available as VertexIdentifier.
///  - If the built in DenseMapInfo is not defined, provide a specialization
///    class type here.
///
/// Graph is CopyConstructible, CopyAssignable, MoveConstructible and
/// MoveAssignable but is not EqualityComparible or LessThanComparible.
///
/// Usage Example Graph with weighted edges and vertices:
///   Graph<int, int, int> G;
///
///   G[1] = 0;
///   G[2] = 2;
///   G[{1,2}] = 1;
///   G[{2,1}] = -1;
///   for(const auto &v : G.vertices()){
///     // Do something with the vertices in the graph;
///   }
///   for(const auto &e : G.edges()){
///     // Do something with the edges in the graph;
///   }
///
/// Usage Example with StrRef keys.
///   Graph<int, double, StrRef> StrG;
///    char va[] = "Vertex A";
///    char vaa[] = "Vertex A";
///    char vb[] = "Vertex B"; // Vertices are referenced by String Refs.
///    G[va] = 0;
///    G[vb] = 1;
///    G[{va, vb}] = 1.0;
///    cout() << G[vaa] << " " << G[{vaa, vb}]; //prints "0 1.0".
///
template <typename VertexAttribute, typename EdgeAttribute,
          typename VI = int32_t>
class Graph {
public:
  /// These objects are used to name edges and vertices in the graph.
  typedef VI VertexIdentifier;
  typedef std::pair<VI, VI> EdgeIdentifier;

  /// This type is the value_type of all iterators which range over vertices,
  /// Determined by the Vertices DenseMap
  using VertexValueType =
      detail::DenseMapPair<VertexIdentifier, VertexAttribute>;

  /// This type is the value_type of all iterators which range over edges,
  /// Determined by the Edges DenseMap.
  using EdgeValueType = detail::DenseMapPair<EdgeIdentifier, EdgeAttribute>;

  using size_type = std::size_t;

private:
  /// The type used for storing the EdgeAttribute for each edge in the graph
  using EdgeMapT = DenseMap<EdgeIdentifier, EdgeAttribute>;

  /// The type used for storing the VertexAttribute for each vertex in
  /// the graph.
  using VertexMapT = DenseMap<VertexIdentifier, VertexAttribute>;

  /// The type used for storing the edges entering a vertex. Indexed by
  /// the VertexIdentifier of the start of the edge. Only used to determine
  /// where the incoming edges are, the EdgeIdentifiers are stored in an
  /// InnerEdgeMapT.
  using NeighborSetT = DenseSet<VertexIdentifier>;

  /// The type storing the InnerInvGraphT corresponding to each vertex in
  /// the graph (When a vertex has an incoming edge incident to it)
  using NeighborLookupT = DenseMap<VertexIdentifier, NeighborSetT>;

private:
  /// Stores the map from the start and end vertex of an edge to it's
  /// EdgeAttribute
  EdgeMapT Edges;

  /// Stores the map from VertexIdentifier to VertexAttribute
  VertexMapT Vertices;

  /// Allows fast lookup for the incoming edge set of any given vertex.
  NeighborLookupT InNeighbors;

  /// Allows fast lookup for the outgoing edge set of any given vertex.
  NeighborLookupT OutNeighbors;

  /// An Iterator adapter using an InnerInvGraphT::iterator as a base iterator,
  /// and storing the VertexIdentifier the iterator range comes from. The
  /// dereference operator is then performed using a pointer to the graph's edge
  /// set.
  template <bool IsConst, bool IsOut,
            typename BaseIt = typename NeighborSetT::const_iterator,
            typename T = typename std::conditional<IsConst, const EdgeValueType,
                                                   EdgeValueType>::type>
  class NeighborEdgeIteratorT
      : public iterator_adaptor_base<
            NeighborEdgeIteratorT<IsConst, IsOut>, BaseIt,
            typename std::iterator_traits<BaseIt>::iterator_category, T> {
    using InternalEdgeMapT =
        typename std::conditional<IsConst, const EdgeMapT, EdgeMapT>::type;

    friend class NeighborEdgeIteratorT<false, IsOut, BaseIt, EdgeValueType>;
    friend class NeighborEdgeIteratorT<true, IsOut, BaseIt,
                                       const EdgeValueType>;

    InternalEdgeMapT *MP;
    VertexIdentifier SI;

  public:
    template <bool IsConstDest,
              typename = typename std::enable_if<IsConstDest && !IsConst>::type>
    operator NeighborEdgeIteratorT<IsConstDest, IsOut, BaseIt,
                                   const EdgeValueType>() const {
      return NeighborEdgeIteratorT<IsConstDest, IsOut, BaseIt,
                                   const EdgeValueType>(this->I, MP, SI);
    }

    NeighborEdgeIteratorT() = default;
    NeighborEdgeIteratorT(BaseIt _I, InternalEdgeMapT *_MP,
                          VertexIdentifier _SI)
        : iterator_adaptor_base<
              NeighborEdgeIteratorT<IsConst, IsOut>, BaseIt,
              typename std::iterator_traits<BaseIt>::iterator_category, T>(_I),
          MP(_MP), SI(_SI) {}

    T &operator*() const {
      if (!IsOut)
        return *(MP->find({*(this->I), SI}));
      else
        return *(MP->find({SI, *(this->I)}));
    }
  };

public:
  /// A const iterator type for iterating through the set of edges entering a
  /// vertex.
  ///
  /// Has a const EdgeValueType as its value_type
  using ConstInEdgeIterator = NeighborEdgeIteratorT<true, false>;

  /// An iterator type for iterating through the set of edges leaving a vertex.
  ///
  /// Has an EdgeValueType as its value_type
  using InEdgeIterator = NeighborEdgeIteratorT<false, false>;

  /// A const iterator type for iterating through the set of edges entering a
  /// vertex.
  ///
  /// Has a const EdgeValueType as its value_type
  using ConstOutEdgeIterator = NeighborEdgeIteratorT<true, true>;

  /// An iterator type for iterating through the set of edges leaving a vertex.
  ///
  /// Has an EdgeValueType as its value_type
  using OutEdgeIterator = NeighborEdgeIteratorT<false, true>;

  /// A class for ranging over the incoming edges incident to a vertex.
  ///
  /// Like all views in this class it provides methods to get the beginning and
  /// past the range iterators for the range, as well as methods to determine
  /// the number of elements in the range and whether the range is empty.
  template <bool isConst, bool isOut> class InOutEdgeView {
  public:
    using iterator = NeighborEdgeIteratorT<isConst, isOut>;
    using const_iterator = NeighborEdgeIteratorT<true, isOut>;
    using GraphT = typename std::conditional<isConst, const Graph, Graph>::type;
    using InternalEdgeMapT =
        typename std::conditional<isConst, const EdgeMapT, EdgeMapT>::type;

  private:
    InternalEdgeMapT &M;
    const VertexIdentifier A;
    const NeighborLookupT &NL;

  public:
    iterator begin() {
      auto It = NL.find(A);
      if (It == NL.end())
        return iterator();
      return iterator(It->second.begin(), &M, A);
    }

    const_iterator cbegin() const {
      auto It = NL.find(A);
      if (It == NL.end())
        return const_iterator();
      return const_iterator(It->second.begin(), &M, A);
    }

    const_iterator begin() const { return cbegin(); }

    iterator end() {
      auto It = NL.find(A);
      if (It == NL.end())
        return iterator();
      return iterator(It->second.end(), &M, A);
    }
    const_iterator cend() const {
      auto It = NL.find(A);
      if (It == NL.end())
        return const_iterator();
      return const_iterator(It->second.end(), &M, A);
    }

    const_iterator end() const { return cend(); }

    size_type size() const {
      auto I = NL.find(A);
      if (I == NL.end())
        return 0;
      else
        return I->second.size();
    }

    bool empty() const { return NL.count(A) == 0; };

    InOutEdgeView(GraphT &G, VertexIdentifier A)
        : M(G.Edges), A(A), NL(isOut ? G.OutNeighbors : G.InNeighbors) {}
  };

  /// A const iterator type for iterating through the whole vertex set of the
  /// graph.
  ///
  /// Has a const VertexValueType as its value_type
  using ConstVertexIterator = typename VertexMapT::const_iterator;

  /// An iterator type for iterating through the whole vertex set of the graph.
  ///
  /// Has a VertexValueType as its value_type
  using VertexIterator = typename VertexMapT::iterator;

  /// A class for ranging over the vertices in the graph.
  ///
  /// Like all views in this class it provides methods to get the beginning and
  /// past the range iterators for the range, as well as methods to determine
  /// the number of elements in the range and whether the range is empty.
  template <bool isConst> class VertexView {
  public:
    using iterator = typename std::conditional<isConst, ConstVertexIterator,
                                               VertexIterator>::type;
    using const_iterator = ConstVertexIterator;
    using GraphT = typename std::conditional<isConst, const Graph, Graph>::type;

  private:
    GraphT &G;

  public:
    iterator begin() { return G.Vertices.begin(); }
    iterator end() { return G.Vertices.end(); }
    const_iterator cbegin() const { return G.Vertices.cbegin(); }
    const_iterator cend() const { return G.Vertices.cend(); }
    const_iterator begin() const { return G.Vertices.begin(); }
    const_iterator end() const { return G.Vertices.end(); }
    size_type size() const { return G.Vertices.size(); }
    bool empty() const { return G.Vertices.empty(); }
    VertexView(GraphT &_G) : G(_G) {}
  };

  /// A const iterator for iterating through the entire edge set of the graph.
  ///
  /// Has a const EdgeValueType as its value_type
  using ConstEdgeIterator = typename EdgeMapT::const_iterator;

  /// An iterator for iterating through the entire edge set of the graph.
  ///
  /// Has an EdgeValueType as its value_type
  using EdgeIterator = typename EdgeMapT::iterator;

  /// A class for ranging over all the edges in the graph.
  ///
  /// Like all views in this class it provides methods to get the beginning and
  /// past the range iterators for the range, as well as methods to determine
  /// the number of elements in the range and whether the range is empty.
  template <bool isConst> class EdgeView {
  public:
    using iterator = typename std::conditional<isConst, ConstEdgeIterator,
                                               EdgeIterator>::type;
    using const_iterator = ConstEdgeIterator;
    using GraphT = typename std::conditional<isConst, const Graph, Graph>::type;

  private:
    GraphT &G;

  public:
    iterator begin() { return G.Edges.begin(); }
    iterator end() { return G.Edges.end(); }
    const_iterator cbegin() const { return G.Edges.cbegin(); }
    const_iterator cend() const { return G.Edges.cend(); }
    const_iterator begin() const { return G.Edges.begin(); }
    const_iterator end() const { return G.Edges.end(); }
    size_type size() const { return G.Edges.size(); }
    bool empty() const { return G.Edges.empty(); }
    EdgeView(GraphT &_G) : G(_G) {}
  };

public:
  // TODO: implement constructor to enable Graph Initialisation.\
  // Something like:
  //   Graph<int, int, int> G(
  //   {1, 2, 3, 4, 5},
  //   {{1, 2}, {2, 3}, {3, 4}});

  /// Empty the Graph
  void clear() {
    Edges.clear();
    Vertices.clear();
    InNeighbors.clear();
    OutNeighbors.clear();
  }

  /// Returns a view object allowing iteration over the vertices of the graph.
  /// also allows access to the size of the vertex set.
  VertexView<false> vertices() { return VertexView<false>(*this); }

  VertexView<true> vertices() const { return VertexView<true>(*this); }

  /// Returns a view object allowing iteration over the edges of the graph.
  /// also allows access to the size of the edge set.
  EdgeView<false> edges() { return EdgeView<false>(*this); }

  EdgeView<true> edges() const { return EdgeView<true>(*this); }

  /// Returns a view object allowing iteration over the edges which start at
  /// a vertex I.
  InOutEdgeView<false, true> outEdges(const VertexIdentifier I) {
    return InOutEdgeView<false, true>(*this, I);
  }

  InOutEdgeView<true, true> outEdges(const VertexIdentifier I) const {
    return InOutEdgeView<true, true>(*this, I);
  }

  /// Returns a view object allowing iteration over the edges which point to
  /// a vertex I.
  InOutEdgeView<false, false> inEdges(const VertexIdentifier I) {
    return InOutEdgeView<false, false>(*this, I);
  }

  InOutEdgeView<true, false> inEdges(const VertexIdentifier I) const {
    return InOutEdgeView<true, false>(*this, I);
  }

  /// Looks up the vertex with identifier I, if it does not exist it default
  /// constructs it.
  VertexAttribute &operator[](const VertexIdentifier &I) {
    return Vertices.FindAndConstruct(I).second;
  }

  /// Looks up the edge with identifier I, if it does not exist it default
  /// constructs it, if it's endpoints do not exist it also default constructs
  /// them.
  EdgeAttribute &operator[](const EdgeIdentifier &I) {
    auto &P = Edges.FindAndConstruct(I);
    Vertices.FindAndConstruct(I.first);
    Vertices.FindAndConstruct(I.second);
    InNeighbors[I.second].insert(I.first);
    OutNeighbors[I.first].insert(I.second);
    return P.second;
  }

  /// Looks up a vertex with Identifier I, or an error if it does not exist.
  Expected<VertexAttribute &> at(const VertexIdentifier &I) {
    auto It = Vertices.find(I);
    if (It == Vertices.end())
      return make_error<StringError>(
          "Vertex Identifier Does Not Exist",
          std::make_error_code(std::errc::invalid_argument));
    return It->second;
  }

  Expected<const VertexAttribute &> at(const VertexIdentifier &I) const {
    auto It = Vertices.find(I);
    if (It == Vertices.end())
      return make_error<StringError>(
          "Vertex Identifier Does Not Exist",
          std::make_error_code(std::errc::invalid_argument));
    return It->second;
  }

  /// Looks up an edge with Identifier I, or an error if it does not exist.
  Expected<EdgeAttribute &> at(const EdgeIdentifier &I) {
    auto It = Edges.find(I);
    if (It == Edges.end())
      return make_error<StringError>(
          "Edge Identifier Does Not Exist",
          std::make_error_code(std::errc::invalid_argument));
    return It->second;
  }

  Expected<const EdgeAttribute &> at(const EdgeIdentifier &I) const {
    auto It = Edges.find(I);
    if (It == Edges.end())
      return make_error<StringError>(
          "Edge Identifier Does Not Exist",
          std::make_error_code(std::errc::invalid_argument));
    return It->second;
  }

  /// Looks for a vertex with identifier I, returns 1 if one exists, and
  /// 0 otherwise
  size_type count(const VertexIdentifier &I) const {
    return Vertices.count(I);
  }

  /// Looks for an edge with Identifier I, returns 1 if one exists and 0
  /// otherwise
  size_type count(const EdgeIdentifier &I) const { return Edges.count(I); }

  /// Inserts a vertex into the graph with Identifier Val.first, and
  /// Attribute Val.second.
  std::pair<VertexIterator, bool>
  insert(const std::pair<VertexIdentifier, VertexAttribute> &Val) {
    return Vertices.insert(Val);
  }

  std::pair<VertexIterator, bool>
  insert(std::pair<VertexIdentifier, VertexAttribute> &&Val) {
    return Vertices.insert(std::move(Val));
  }

  /// Inserts an edge into the graph with Identifier Val.first, and
  /// Attribute Val.second. If the key is already in the map, it returns false
  /// and doesn't update the value.
  std::pair<EdgeIterator, bool>
  insert(const std::pair<EdgeIdentifier, EdgeAttribute> &Val) {
    const auto &p = Edges.insert(Val);
    if (p.second) {
      const auto &EI = Val.first;
      Vertices.FindAndConstruct(EI.first);
      Vertices.FindAndConstruct(EI.second);
      InNeighbors[EI.second].insert(EI.first);
      OutNeighbors[EI.first].insert(EI.second);
    };

    return p;
  }

  /// Inserts an edge into the graph with Identifier Val.first, and
  /// Attribute Val.second. If the key is already in the map, it returns false
  /// and doesn't update the value.
  std::pair<EdgeIterator, bool>
  insert(std::pair<EdgeIdentifier, EdgeAttribute> &&Val) {
    auto EI = Val.first;
    const auto &p = Edges.insert(std::move(Val));
    if (p.second) {
      Vertices.FindAndConstruct(EI.first);
      Vertices.FindAndConstruct(EI.second);
      InNeighbors[EI.second].insert(EI.first);
      OutNeighbors[EI.first].insert(EI.second);
    };

    return p;
  }
};
}
}
#endif
