//===- llvm/ADT/IntervalMap.h - A sorted interval map -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a coalescing interval map for small objects.
//
// KeyT objects are mapped to ValT objects. Intervals of keys that map to the
// same value are represented in a compressed form.
//
// Iterators provide ordered access to the compressed intervals rather than the
// individual keys, and insert and erase operations use key intervals as well.
//
// Like SmallVector, IntervalMap will store the first N intervals in the map
// object itself without any allocations. When space is exhausted it switches to
// a B+-tree representation with very small overhead for small key and value
// objects.
//
// A Traits class specifies how keys are compared. It also allows IntervalMap to
// work with both closed and half-open intervals.
//
// Keys and values are not stored next to each other in a std::pair, so we don't
// provide such a value_type. Dereferencing iterators only returns the mapped
// value. The interval bounds are accessible through the start() and stop()
// iterator methods.
//
// IntervalMap is optimized for small key and value objects, 4 or 8 bytes each
// is the optimal size. For large objects use std::map instead.
//
//===----------------------------------------------------------------------===//
//
// Synopsis:
//
// template <typename KeyT, typename ValT, unsigned N, typename Traits>
// class IntervalMap {
// public:
//   typedef KeyT key_type;
//   typedef ValT mapped_type;
//   typedef RecyclingAllocator<...> Allocator;
//   class iterator;
//   class const_iterator;
//
//   explicit IntervalMap(Allocator&);
//   ~IntervalMap():
//
//   bool empty() const;
//   KeyT start() const;
//   KeyT stop() const;
//   ValT lookup(KeyT x, Value NotFound = Value()) const;
//
//   const_iterator begin() const;
//   const_iterator end() const;
//   iterator begin();
//   iterator end();
//   const_iterator find(KeyT x) const;
//   iterator find(KeyT x);
//
//   void insert(KeyT a, KeyT b, ValT y);
//   void clear();
// };
//
// template <typename KeyT, typename ValT, unsigned N, typename Traits>
// class IntervalMap::const_iterator :
//   public std::iterator<std::bidirectional_iterator_tag, ValT> {
// public:
//   bool operator==(const const_iterator &) const;
//   bool operator!=(const const_iterator &) const;
//   bool valid() const;
//
//   const KeyT &start() const;
//   const KeyT &stop() const;
//   const ValT &value() const;
//   const ValT &operator*() const;
//   const ValT *operator->() const;
//
//   const_iterator &operator++();
//   const_iterator &operator++(int);
//   const_iterator &operator--();
//   const_iterator &operator--(int);
//   void goToBegin();
//   void goToEnd();
//   void find(KeyT x);
//   void advanceTo(KeyT x);
// };
//
// template <typename KeyT, typename ValT, unsigned N, typename Traits>
// class IntervalMap::iterator : public const_iterator {
// public:
//   void insert(KeyT a, KeyT b, Value y);
//   void erase();
// };
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_INTERVALMAP_H
#define LLVM_ADT_INTERVALMAP_H

#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/bit.h"
#include "llvm/Support/AlignOf.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/RecyclingAllocator.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <new>
#include <utility>

namespace llvm {

//===----------------------------------------------------------------------===//
//---                              Key traits                              ---//
//===----------------------------------------------------------------------===//
//
// The IntervalMap works with closed or half-open intervals.
// Adjacent intervals that map to the same value are coalesced.
//
// The IntervalMapInfo traits class is used to determine if a key is contained
// in an interval, and if two intervals are adjacent so they can be coalesced.
// The provided implementation works for closed integer intervals, other keys
// probably need a specialized version.
//
// The point x is contained in [a;b] when !startLess(x, a) && !stopLess(b, x).
//
// It is assumed that (a;b] half-open intervals are not used, only [a;b) is
// allowed. This is so that stopLess(a, b) can be used to determine if two
// intervals overlap.
//
//===----------------------------------------------------------------------===//

template <typename T>
struct IntervalMapInfo {
  /// startLess - Return true if x is not in [a;b].
  /// This is x < a both for closed intervals and for [a;b) half-open intervals.
  static inline bool startLess(const T &x, const T &a) {
    return x < a;
  }

  /// stopLess - Return true if x is not in [a;b].
  /// This is b < x for a closed interval, b <= x for [a;b) half-open intervals.
  static inline bool stopLess(const T &b, const T &x) {
    return b < x;
  }

  /// adjacent - Return true when the intervals [x;a] and [b;y] can coalesce.
  /// This is a+1 == b for closed intervals, a == b for half-open intervals.
  static inline bool adjacent(const T &a, const T &b) {
    return a+1 == b;
  }

  /// nonEmpty - Return true if [a;b] is non-empty.
  /// This is a <= b for a closed interval, a < b for [a;b) half-open intervals.
  static inline bool nonEmpty(const T &a, const T &b) {
    return a <= b;
  }
};

template <typename T>
struct IntervalMapHalfOpenInfo {
  /// startLess - Return true if x is not in [a;b).
  static inline bool startLess(const T &x, const T &a) {
    return x < a;
  }

  /// stopLess - Return true if x is not in [a;b).
  static inline bool stopLess(const T &b, const T &x) {
    return b <= x;
  }

  /// adjacent - Return true when the intervals [x;a) and [b;y) can coalesce.
  static inline bool adjacent(const T &a, const T &b) {
    return a == b;
  }

  /// nonEmpty - Return true if [a;b) is non-empty.
  static inline bool nonEmpty(const T &a, const T &b) {
    return a < b;
  }
};

/// IntervalMapImpl - Namespace used for IntervalMap implementation details.
/// It should be considered private to the implementation.
namespace IntervalMapImpl {

using IdxPair = std::pair<unsigned,unsigned>;

//===----------------------------------------------------------------------===//
//---                    IntervalMapImpl::NodeBase                         ---//
//===----------------------------------------------------------------------===//
//
// Both leaf and branch nodes store vectors of pairs.
// Leaves store ((KeyT, KeyT), ValT) pairs, branches use (NodeRef, KeyT).
//
// Keys and values are stored in separate arrays to avoid padding caused by
// different object alignments. This also helps improve locality of reference
// when searching the keys.
//
// The nodes don't know how many elements they contain - that information is
// stored elsewhere. Omitting the size field prevents padding and allows a node
// to fill the allocated cache lines completely.
//
// These are typical key and value sizes, the node branching factor (N), and
// wasted space when nodes are sized to fit in three cache lines (192 bytes):
//
//   T1  T2   N Waste  Used by
//    4   4  24   0    Branch<4> (32-bit pointers)
//    8   4  16   0    Leaf<4,4>, Branch<4>
//    8   8  12   0    Leaf<4,8>, Branch<8>
//   16   4   9  12    Leaf<8,4>
//   16   8   8   0    Leaf<8,8>
//
//===----------------------------------------------------------------------===//

template <typename T1, typename T2, unsigned N>
class NodeBase {
public:
  enum { Capacity = N };

  T1 first[N];
  T2 second[N];

  /// copy - Copy elements from another node.
  /// @param Other Node elements are copied from.
  /// @param i     Beginning of the source range in other.
  /// @param j     Beginning of the destination range in this.
  /// @param Count Number of elements to copy.
  template <unsigned M>
  void copy(const NodeBase<T1, T2, M> &Other, unsigned i,
            unsigned j, unsigned Count) {
    assert(i + Count <= M && "Invalid source range");
    assert(j + Count <= N && "Invalid dest range");
    for (unsigned e = i + Count; i != e; ++i, ++j) {
      first[j]  = Other.first[i];
      second[j] = Other.second[i];
    }
  }

  /// moveLeft - Move elements to the left.
  /// @param i     Beginning of the source range.
  /// @param j     Beginning of the destination range.
  /// @param Count Number of elements to copy.
  void moveLeft(unsigned i, unsigned j, unsigned Count) {
    assert(j <= i && "Use moveRight shift elements right");
    copy(*this, i, j, Count);
  }

  /// moveRight - Move elements to the right.
  /// @param i     Beginning of the source range.
  /// @param j     Beginning of the destination range.
  /// @param Count Number of elements to copy.
  void moveRight(unsigned i, unsigned j, unsigned Count) {
    assert(i <= j && "Use moveLeft shift elements left");
    assert(j + Count <= N && "Invalid range");
    while (Count--) {
      first[j + Count]  = first[i + Count];
      second[j + Count] = second[i + Count];
    }
  }

  /// erase - Erase elements [i;j).
  /// @param i    Beginning of the range to erase.
  /// @param j    End of the range. (Exclusive).
  /// @param Size Number of elements in node.
  void erase(unsigned i, unsigned j, unsigned Size) {
    moveLeft(j, i, Size - j);
  }

  /// erase - Erase element at i.
  /// @param i    Index of element to erase.
  /// @param Size Number of elements in node.
  void erase(unsigned i, unsigned Size) {
    erase(i, i+1, Size);
  }

  /// shift - Shift elements [i;size) 1 position to the right.
  /// @param i    Beginning of the range to move.
  /// @param Size Number of elements in node.
  void shift(unsigned i, unsigned Size) {
    moveRight(i, i + 1, Size - i);
  }

  /// transferToLeftSib - Transfer elements to a left sibling node.
  /// @param Size  Number of elements in this.
  /// @param Sib   Left sibling node.
  /// @param SSize Number of elements in sib.
  /// @param Count Number of elements to transfer.
  void transferToLeftSib(unsigned Size, NodeBase &Sib, unsigned SSize,
                         unsigned Count) {
    Sib.copy(*this, 0, SSize, Count);
    erase(0, Count, Size);
  }

  /// transferToRightSib - Transfer elements to a right sibling node.
  /// @param Size  Number of elements in this.
  /// @param Sib   Right sibling node.
  /// @param SSize Number of elements in sib.
  /// @param Count Number of elements to transfer.
  void transferToRightSib(unsigned Size, NodeBase &Sib, unsigned SSize,
                          unsigned Count) {
    Sib.moveRight(0, Count, SSize);
    Sib.copy(*this, Size-Count, 0, Count);
  }

  /// adjustFromLeftSib - Adjust the number if elements in this node by moving
  /// elements to or from a left sibling node.
  /// @param Size  Number of elements in this.
  /// @param Sib   Right sibling node.
  /// @param SSize Number of elements in sib.
  /// @param Add   The number of elements to add to this node, possibly < 0.
  /// @return      Number of elements added to this node, possibly negative.
  int adjustFromLeftSib(unsigned Size, NodeBase &Sib, unsigned SSize, int Add) {
    if (Add > 0) {
      // We want to grow, copy from sib.
      unsigned Count = std::min(std::min(unsigned(Add), SSize), N - Size);
      Sib.transferToRightSib(SSize, *this, Size, Count);
      return Count;
    } else {
      // We want to shrink, copy to sib.
      unsigned Count = std::min(std::min(unsigned(-Add), Size), N - SSize);
      transferToLeftSib(Size, Sib, SSize, Count);
      return -Count;
    }
  }
};

/// IntervalMapImpl::adjustSiblingSizes - Move elements between sibling nodes.
/// @param Node  Array of pointers to sibling nodes.
/// @param Nodes Number of nodes.
/// @param CurSize Array of current node sizes, will be overwritten.
/// @param NewSize Array of desired node sizes.
template <typename NodeT>
void adjustSiblingSizes(NodeT *Node[], unsigned Nodes,
                        unsigned CurSize[], const unsigned NewSize[]) {
  // Move elements right.
  for (int n = Nodes - 1; n; --n) {
    if (CurSize[n] == NewSize[n])
      continue;
    for (int m = n - 1; m != -1; --m) {
      int d = Node[n]->adjustFromLeftSib(CurSize[n], *Node[m], CurSize[m],
                                         NewSize[n] - CurSize[n]);
      CurSize[m] -= d;
      CurSize[n] += d;
      // Keep going if the current node was exhausted.
      if (CurSize[n] >= NewSize[n])
          break;
    }
  }

  if (Nodes == 0)
    return;

  // Move elements left.
  for (unsigned n = 0; n != Nodes - 1; ++n) {
    if (CurSize[n] == NewSize[n])
      continue;
    for (unsigned m = n + 1; m != Nodes; ++m) {
      int d = Node[m]->adjustFromLeftSib(CurSize[m], *Node[n], CurSize[n],
                                        CurSize[n] -  NewSize[n]);
      CurSize[m] += d;
      CurSize[n] -= d;
      // Keep going if the current node was exhausted.
      if (CurSize[n] >= NewSize[n])
          break;
    }
  }

#ifndef NDEBUG
  for (unsigned n = 0; n != Nodes; n++)
    assert(CurSize[n] == NewSize[n] && "Insufficient element shuffle");
#endif
}

/// IntervalMapImpl::distribute - Compute a new distribution of node elements
/// after an overflow or underflow. Reserve space for a new element at Position,
/// and compute the node that will hold Position after redistributing node
/// elements.
///
/// It is required that
///
///   Elements == sum(CurSize), and
///   Elements + Grow <= Nodes * Capacity.
///
/// NewSize[] will be filled in such that:
///
///   sum(NewSize) == Elements, and
///   NewSize[i] <= Capacity.
///
/// The returned index is the node where Position will go, so:
///
///   sum(NewSize[0..idx-1]) <= Position
///   sum(NewSize[0..idx])   >= Position
///
/// The last equality, sum(NewSize[0..idx]) == Position, can only happen when
/// Grow is set and NewSize[idx] == Capacity-1. The index points to the node
/// before the one holding the Position'th element where there is room for an
/// insertion.
///
/// @param Nodes    The number of nodes.
/// @param Elements Total elements in all nodes.
/// @param Capacity The capacity of each node.
/// @param CurSize  Array[Nodes] of current node sizes, or NULL.
/// @param NewSize  Array[Nodes] to receive the new node sizes.
/// @param Position Insert position.
/// @param Grow     Reserve space for a new element at Position.
/// @return         (node, offset) for Position.
IdxPair distribute(unsigned Nodes, unsigned Elements, unsigned Capacity,
                   const unsigned *CurSize, unsigned NewSize[],
                   unsigned Position, bool Grow);

//===----------------------------------------------------------------------===//
//---                   IntervalMapImpl::NodeSizer                         ---//
//===----------------------------------------------------------------------===//
//
// Compute node sizes from key and value types.
//
// The branching factors are chosen to make nodes fit in three cache lines.
// This may not be possible if keys or values are very large. Such large objects
// are handled correctly, but a std::map would probably give better performance.
//
//===----------------------------------------------------------------------===//

enum {
  // Cache line size. Most architectures have 32 or 64 byte cache lines.
  // We use 64 bytes here because it provides good branching factors.
  Log2CacheLine = 6,
  CacheLineBytes = 1 << Log2CacheLine,
  DesiredNodeBytes = 3 * CacheLineBytes
};

template <typename KeyT, typename ValT>
struct NodeSizer {
  enum {
    // Compute the leaf node branching factor that makes a node fit in three
    // cache lines. The branching factor must be at least 3, or some B+-tree
    // balancing algorithms won't work.
    // LeafSize can't be larger than CacheLineBytes. This is required by the
    // PointerIntPair used by NodeRef.
    DesiredLeafSize = DesiredNodeBytes /
      static_cast<unsigned>(2*sizeof(KeyT)+sizeof(ValT)),
    MinLeafSize = 3,
    LeafSize = DesiredLeafSize > MinLeafSize ? DesiredLeafSize : MinLeafSize
  };

  using LeafBase = NodeBase<std::pair<KeyT, KeyT>, ValT, LeafSize>;

  enum {
    // Now that we have the leaf branching factor, compute the actual allocation
    // unit size by rounding up to a whole number of cache lines.
    AllocBytes = (sizeof(LeafBase) + CacheLineBytes-1) & ~(CacheLineBytes-1),

    // Determine the branching factor for branch nodes.
    BranchSize = AllocBytes /
      static_cast<unsigned>(sizeof(KeyT) + sizeof(void*))
  };

  /// Allocator - The recycling allocator used for both branch and leaf nodes.
  /// This typedef is very likely to be identical for all IntervalMaps with
  /// reasonably sized entries, so the same allocator can be shared among
  /// different kinds of maps.
  using Allocator =
      RecyclingAllocator<BumpPtrAllocator, char, AllocBytes, CacheLineBytes>;
};

//===----------------------------------------------------------------------===//
//---                     IntervalMapImpl::NodeRef                         ---//
//===----------------------------------------------------------------------===//
//
// B+-tree nodes can be leaves or branches, so we need a polymorphic node
// pointer that can point to both kinds.
//
// All nodes are cache line aligned and the low 6 bits of a node pointer are
// always 0. These bits are used to store the number of elements in the
// referenced node. Besides saving space, placing node sizes in the parents
// allow tree balancing algorithms to run without faulting cache lines for nodes
// that may not need to be modified.
//
// A NodeRef doesn't know whether it references a leaf node or a branch node.
// It is the responsibility of the caller to use the correct types.
//
// Nodes are never supposed to be empty, and it is invalid to store a node size
// of 0 in a NodeRef. The valid range of sizes is 1-64.
//
//===----------------------------------------------------------------------===//

class NodeRef {
  struct CacheAlignedPointerTraits {
    static inline void *getAsVoidPointer(void *P) { return P; }
    static inline void *getFromVoidPointer(void *P) { return P; }
    enum { NumLowBitsAvailable = Log2CacheLine };
  };
  PointerIntPair<void*, Log2CacheLine, unsigned, CacheAlignedPointerTraits> pip;

public:
  /// NodeRef - Create a null ref.
  NodeRef() = default;

  /// operator bool - Detect a null ref.
  explicit operator bool() const { return pip.getOpaqueValue(); }

  /// NodeRef - Create a reference to the node p with n elements.
  template <typename NodeT>
  NodeRef(NodeT *p, unsigned n) : pip(p, n - 1) {
    assert(n <= NodeT::Capacity && "Size too big for node");
  }

  /// size - Return the number of elements in the referenced node.
  unsigned size() const { return pip.getInt() + 1; }

  /// setSize - Update the node size.
  void setSize(unsigned n) { pip.setInt(n - 1); }

  /// subtree - Access the i'th subtree reference in a branch node.
  /// This depends on branch nodes storing the NodeRef array as their first
  /// member.
  NodeRef &subtree(unsigned i) const {
    return reinterpret_cast<NodeRef*>(pip.getPointer())[i];
  }

  /// get - Dereference as a NodeT reference.
  template <typename NodeT>
  NodeT &get() const {
    return *reinterpret_cast<NodeT*>(pip.getPointer());
  }

  bool operator==(const NodeRef &RHS) const {
    if (pip == RHS.pip)
      return true;
    assert(pip.getPointer() != RHS.pip.getPointer() && "Inconsistent NodeRefs");
    return false;
  }

  bool operator!=(const NodeRef &RHS) const {
    return !operator==(RHS);
  }
};

//===----------------------------------------------------------------------===//
//---                      IntervalMapImpl::LeafNode                       ---//
//===----------------------------------------------------------------------===//
//
// Leaf nodes store up to N disjoint intervals with corresponding values.
//
// The intervals are kept sorted and fully coalesced so there are no adjacent
// intervals mapping to the same value.
//
// These constraints are always satisfied:
//
// - Traits::stopLess(start(i), stop(i))    - Non-empty, sane intervals.
//
// - Traits::stopLess(stop(i), start(i + 1) - Sorted.
//
// - value(i) != value(i + 1) || !Traits::adjacent(stop(i), start(i + 1))
//                                          - Fully coalesced.
//
//===----------------------------------------------------------------------===//

template <typename KeyT, typename ValT, unsigned N, typename Traits>
class LeafNode : public NodeBase<std::pair<KeyT, KeyT>, ValT, N> {
public:
  const KeyT &start(unsigned i) const { return this->first[i].first; }
  const KeyT &stop(unsigned i) const { return this->first[i].second; }
  const ValT &value(unsigned i) const { return this->second[i]; }

  KeyT &start(unsigned i) { return this->first[i].first; }
  KeyT &stop(unsigned i) { return this->first[i].second; }
  ValT &value(unsigned i) { return this->second[i]; }

  /// findFrom - Find the first interval after i that may contain x.
  /// @param i    Starting index for the search.
  /// @param Size Number of elements in node.
  /// @param x    Key to search for.
  /// @return     First index with !stopLess(key[i].stop, x), or size.
  ///             This is the first interval that can possibly contain x.
  unsigned findFrom(unsigned i, unsigned Size, KeyT x) const {
    assert(i <= Size && Size <= N && "Bad indices");
    assert((i == 0 || Traits::stopLess(stop(i - 1), x)) &&
           "Index is past the needed point");
    while (i != Size && Traits::stopLess(stop(i), x)) ++i;
    return i;
  }

  /// safeFind - Find an interval that is known to exist. This is the same as
  /// findFrom except is it assumed that x is at least within range of the last
  /// interval.
  /// @param i Starting index for the search.
  /// @param x Key to search for.
  /// @return  First index with !stopLess(key[i].stop, x), never size.
  ///          This is the first interval that can possibly contain x.
  unsigned safeFind(unsigned i, KeyT x) const {
    assert(i < N && "Bad index");
    assert((i == 0 || Traits::stopLess(stop(i - 1), x)) &&
           "Index is past the needed point");
    while (Traits::stopLess(stop(i), x)) ++i;
    assert(i < N && "Unsafe intervals");
    return i;
  }

  /// safeLookup - Lookup mapped value for a safe key.
  /// It is assumed that x is within range of the last entry.
  /// @param x        Key to search for.
  /// @param NotFound Value to return if x is not in any interval.
  /// @return         The mapped value at x or NotFound.
  ValT safeLookup(KeyT x, ValT NotFound) const {
    unsigned i = safeFind(0, x);
    return Traits::startLess(x, start(i)) ? NotFound : value(i);
  }

  unsigned insertFrom(unsigned &Pos, unsigned Size, KeyT a, KeyT b, ValT y);
};

/// insertFrom - Add mapping of [a;b] to y if possible, coalescing as much as
/// possible. This may cause the node to grow by 1, or it may cause the node
/// to shrink because of coalescing.
/// @param Pos  Starting index = insertFrom(0, size, a)
/// @param Size Number of elements in node.
/// @param a    Interval start.
/// @param b    Interval stop.
/// @param y    Value be mapped.
/// @return     (insert position, new size), or (i, Capacity+1) on overflow.
template <typename KeyT, typename ValT, unsigned N, typename Traits>
unsigned LeafNode<KeyT, ValT, N, Traits>::
insertFrom(unsigned &Pos, unsigned Size, KeyT a, KeyT b, ValT y) {
  unsigned i = Pos;
  assert(i <= Size && Size <= N && "Invalid index");
  assert(!Traits::stopLess(b, a) && "Invalid interval");

  // Verify the findFrom invariant.
  assert((i == 0 || Traits::stopLess(stop(i - 1), a)));
  assert((i == Size || !Traits::stopLess(stop(i), a)));
  assert((i == Size || Traits::stopLess(b, start(i))) && "Overlapping insert");

  // Coalesce with previous interval.
  if (i && value(i - 1) == y && Traits::adjacent(stop(i - 1), a)) {
    Pos = i - 1;
    // Also coalesce with next interval?
    if (i != Size && value(i) == y && Traits::adjacent(b, start(i))) {
      stop(i - 1) = stop(i);
      this->erase(i, Size);
      return Size - 1;
    }
    stop(i - 1) = b;
    return Size;
  }

  // Detect overflow.
  if (i == N)
    return N + 1;

  // Add new interval at end.
  if (i == Size) {
    start(i) = a;
    stop(i) = b;
    value(i) = y;
    return Size + 1;
  }

  // Try to coalesce with following interval.
  if (value(i) == y && Traits::adjacent(b, start(i))) {
    start(i) = a;
    return Size;
  }

  // We must insert before i. Detect overflow.
  if (Size == N)
    return N + 1;

  // Insert before i.
  this->shift(i, Size);
  start(i) = a;
  stop(i) = b;
  value(i) = y;
  return Size + 1;
}

//===----------------------------------------------------------------------===//
//---                   IntervalMapImpl::BranchNode                        ---//
//===----------------------------------------------------------------------===//
//
// A branch node stores references to 1--N subtrees all of the same height.
//
// The key array in a branch node holds the rightmost stop key of each subtree.
// It is redundant to store the last stop key since it can be found in the
// parent node, but doing so makes tree balancing a lot simpler.
//
// It is unusual for a branch node to only have one subtree, but it can happen
// in the root node if it is smaller than the normal nodes.
//
// When all of the leaf nodes from all the subtrees are concatenated, they must
// satisfy the same constraints as a single leaf node. They must be sorted,
// sane, and fully coalesced.
//
//===----------------------------------------------------------------------===//

template <typename KeyT, typename ValT, unsigned N, typename Traits>
class BranchNode : public NodeBase<NodeRef, KeyT, N> {
public:
  const KeyT &stop(unsigned i) const { return this->second[i]; }
  const NodeRef &subtree(unsigned i) const { return this->first[i]; }

  KeyT &stop(unsigned i) { return this->second[i]; }
  NodeRef &subtree(unsigned i) { return this->first[i]; }

  /// findFrom - Find the first subtree after i that may contain x.
  /// @param i    Starting index for the search.
  /// @param Size Number of elements in node.
  /// @param x    Key to search for.
  /// @return     First index with !stopLess(key[i], x), or size.
  ///             This is the first subtree that can possibly contain x.
  unsigned findFrom(unsigned i, unsigned Size, KeyT x) const {
    assert(i <= Size && Size <= N && "Bad indices");
    assert((i == 0 || Traits::stopLess(stop(i - 1), x)) &&
           "Index to findFrom is past the needed point");
    while (i != Size && Traits::stopLess(stop(i), x)) ++i;
    return i;
  }

  /// safeFind - Find a subtree that is known to exist. This is the same as
  /// findFrom except is it assumed that x is in range.
  /// @param i Starting index for the search.
  /// @param x Key to search for.
  /// @return  First index with !stopLess(key[i], x), never size.
  ///          This is the first subtree that can possibly contain x.
  unsigned safeFind(unsigned i, KeyT x) const {
    assert(i < N && "Bad index");
    assert((i == 0 || Traits::stopLess(stop(i - 1), x)) &&
           "Index is past the needed point");
    while (Traits::stopLess(stop(i), x)) ++i;
    assert(i < N && "Unsafe intervals");
    return i;
  }

  /// safeLookup - Get the subtree containing x, Assuming that x is in range.
  /// @param x Key to search for.
  /// @return  Subtree containing x
  NodeRef safeLookup(KeyT x) const {
    return subtree(safeFind(0, x));
  }

  /// insert - Insert a new (subtree, stop) pair.
  /// @param i    Insert position, following entries will be shifted.
  /// @param Size Number of elements in node.
  /// @param Node Subtree to insert.
  /// @param Stop Last key in subtree.
  void insert(unsigned i, unsigned Size, NodeRef Node, KeyT Stop) {
    assert(Size < N && "branch node overflow");
    assert(i <= Size && "Bad insert position");
    this->shift(i, Size);
    subtree(i) = Node;
    stop(i) = Stop;
  }
};

//===----------------------------------------------------------------------===//
//---                         IntervalMapImpl::Path                        ---//
//===----------------------------------------------------------------------===//
//
// A Path is used by iterators to represent a position in a B+-tree, and the
// path to get there from the root.
//
// The Path class also contains the tree navigation code that doesn't have to
// be templatized.
//
//===----------------------------------------------------------------------===//

class Path {
  /// Entry - Each step in the path is a node pointer and an offset into that
  /// node.
  struct Entry {
    void *node;
    unsigned size;
    unsigned offset;

    Entry(void *Node, unsigned Size, unsigned Offset)
      : node(Node), size(Size), offset(Offset) {}

    Entry(NodeRef Node, unsigned Offset)
      : node(&Node.subtree(0)), size(Node.size()), offset(Offset) {}

    NodeRef &subtree(unsigned i) const {
      return reinterpret_cast<NodeRef*>(node)[i];
    }
  };

  /// path - The path entries, path[0] is the root node, path.back() is a leaf.
  SmallVector<Entry, 4> path;

public:
  // Node accessors.
  template <typename NodeT> NodeT &node(unsigned Level) const {
    return *reinterpret_cast<NodeT*>(path[Level].node);
  }
  unsigned size(unsigned Level) const { return path[Level].size; }
  unsigned offset(unsigned Level) const { return path[Level].offset; }
  unsigned &offset(unsigned Level) { return path[Level].offset; }

  // Leaf accessors.
  template <typename NodeT> NodeT &leaf() const {
    return *reinterpret_cast<NodeT*>(path.back().node);
  }
  unsigned leafSize() const { return path.back().size; }
  unsigned leafOffset() const { return path.back().offset; }
  unsigned &leafOffset() { return path.back().offset; }

  /// valid - Return true if path is at a valid node, not at end().
  bool valid() const {
    return !path.empty() && path.front().offset < path.front().size;
  }

  /// height - Return the height of the tree corresponding to this path.
  /// This matches map->height in a full path.
  unsigned height() const { return path.size() - 1; }

  /// subtree - Get the subtree referenced from Level. When the path is
  /// consistent, node(Level + 1) == subtree(Level).
  /// @param Level 0..height-1. The leaves have no subtrees.
  NodeRef &subtree(unsigned Level) const {
    return path[Level].subtree(path[Level].offset);
  }

  /// reset - Reset cached information about node(Level) from subtree(Level -1).
  /// @param Level 1..height. THe node to update after parent node changed.
  void reset(unsigned Level) {
    path[Level] = Entry(subtree(Level - 1), offset(Level));
  }

  /// push - Add entry to path.
  /// @param Node Node to add, should be subtree(path.size()-1).
  /// @param Offset Offset into Node.
  void push(NodeRef Node, unsigned Offset) {
    path.push_back(Entry(Node, Offset));
  }

  /// pop - Remove the last path entry.
  void pop() {
    path.pop_back();
  }

  /// setSize - Set the size of a node both in the path and in the tree.
  /// @param Level 0..height. Note that setting the root size won't change
  ///              map->rootSize.
  /// @param Size New node size.
  void setSize(unsigned Level, unsigned Size) {
    path[Level].size = Size;
    if (Level)
      subtree(Level - 1).setSize(Size);
  }

  /// setRoot - Clear the path and set a new root node.
  /// @param Node New root node.
  /// @param Size New root size.
  /// @param Offset Offset into root node.
  void setRoot(void *Node, unsigned Size, unsigned Offset) {
    path.clear();
    path.push_back(Entry(Node, Size, Offset));
  }

  /// replaceRoot - Replace the current root node with two new entries after the
  /// tree height has increased.
  /// @param Root The new root node.
  /// @param Size Number of entries in the new root.
  /// @param Offsets Offsets into the root and first branch nodes.
  void replaceRoot(void *Root, unsigned Size, IdxPair Offsets);

  /// getLeftSibling - Get the left sibling node at Level, or a null NodeRef.
  /// @param Level Get the sibling to node(Level).
  /// @return Left sibling, or NodeRef().
  NodeRef getLeftSibling(unsigned Level) const;

  /// moveLeft - Move path to the left sibling at Level. Leave nodes below Level
  /// unaltered.
  /// @param Level Move node(Level).
  void moveLeft(unsigned Level);

  /// fillLeft - Grow path to Height by taking leftmost branches.
  /// @param Height The target height.
  void fillLeft(unsigned Height) {
    while (height() < Height)
      push(subtree(height()), 0);
  }

  /// getLeftSibling - Get the left sibling node at Level, or a null NodeRef.
  /// @param Level Get the sinbling to node(Level).
  /// @return Left sibling, or NodeRef().
  NodeRef getRightSibling(unsigned Level) const;

  /// moveRight - Move path to the left sibling at Level. Leave nodes below
  /// Level unaltered.
  /// @param Level Move node(Level).
  void moveRight(unsigned Level);

  /// atBegin - Return true if path is at begin().
  bool atBegin() const {
    for (unsigned i = 0, e = path.size(); i != e; ++i)
      if (path[i].offset != 0)
        return false;
    return true;
  }

  /// atLastEntry - Return true if the path is at the last entry of the node at
  /// Level.
  /// @param Level Node to examine.
  bool atLastEntry(unsigned Level) const {
    return path[Level].offset == path[Level].size - 1;
  }

  /// legalizeForInsert - Prepare the path for an insertion at Level. When the
  /// path is at end(), node(Level) may not be a legal node. legalizeForInsert
  /// ensures that node(Level) is real by moving back to the last node at Level,
  /// and setting offset(Level) to size(Level) if required.
  /// @param Level The level where an insertion is about to take place.
  void legalizeForInsert(unsigned Level) {
    if (valid())
      return;
    moveLeft(Level);
    ++path[Level].offset;
  }
};

} // end namespace IntervalMapImpl

//===----------------------------------------------------------------------===//
//---                          IntervalMap                                ----//
//===----------------------------------------------------------------------===//

template <typename KeyT, typename ValT,
          unsigned N = IntervalMapImpl::NodeSizer<KeyT, ValT>::LeafSize,
          typename Traits = IntervalMapInfo<KeyT>>
class IntervalMap {
  using Sizer = IntervalMapImpl::NodeSizer<KeyT, ValT>;
  using Leaf = IntervalMapImpl::LeafNode<KeyT, ValT, Sizer::LeafSize, Traits>;
  using Branch =
      IntervalMapImpl::BranchNode<KeyT, ValT, Sizer::BranchSize, Traits>;
  using RootLeaf = IntervalMapImpl::LeafNode<KeyT, ValT, N, Traits>;
  using IdxPair = IntervalMapImpl::IdxPair;

  // The RootLeaf capacity is given as a template parameter. We must compute the
  // corresponding RootBranch capacity.
  enum {
    DesiredRootBranchCap = (sizeof(RootLeaf) - sizeof(KeyT)) /
      (sizeof(KeyT) + sizeof(IntervalMapImpl::NodeRef)),
    RootBranchCap = DesiredRootBranchCap ? DesiredRootBranchCap : 1
  };

  using RootBranch =
      IntervalMapImpl::BranchNode<KeyT, ValT, RootBranchCap, Traits>;

  // When branched, we store a global start key as well as the branch node.
  struct RootBranchData {
    KeyT start;
    RootBranch node;
  };

public:
  using Allocator = typename Sizer::Allocator;
  using KeyType = KeyT;
  using ValueType = ValT;
  using KeyTraits = Traits;

private:
  // The root data is either a RootLeaf or a RootBranchData instance.
  LLVM_ALIGNAS(RootLeaf) LLVM_ALIGNAS(RootBranchData)
  AlignedCharArrayUnion<RootLeaf, RootBranchData> data;

  // Tree height.
  // 0: Leaves in root.
  // 1: Root points to leaf.
  // 2: root->branch->leaf ...
  unsigned height;

  // Number of entries in the root node.
  unsigned rootSize;

  // Allocator used for creating external nodes.
  Allocator &allocator;

  /// Represent data as a node type without breaking aliasing rules.
  template <typename T>
  T &dataAs() const {
    return *bit_cast<T *>(const_cast<char *>(data.buffer));
  }

  const RootLeaf &rootLeaf() const {
    assert(!branched() && "Cannot acces leaf data in branched root");
    return dataAs<RootLeaf>();
  }
  RootLeaf &rootLeaf() {
    assert(!branched() && "Cannot acces leaf data in branched root");
    return dataAs<RootLeaf>();
  }

  RootBranchData &rootBranchData() const {
    assert(branched() && "Cannot access branch data in non-branched root");
    return dataAs<RootBranchData>();
  }
  RootBranchData &rootBranchData() {
    assert(branched() && "Cannot access branch data in non-branched root");
    return dataAs<RootBranchData>();
  }

  const RootBranch &rootBranch() const { return rootBranchData().node; }
  RootBranch &rootBranch()             { return rootBranchData().node; }
  KeyT rootBranchStart() const { return rootBranchData().start; }
  KeyT &rootBranchStart()      { return rootBranchData().start; }

  template <typename NodeT> NodeT *newNode() {
    return new(allocator.template Allocate<NodeT>()) NodeT();
  }

  template <typename NodeT> void deleteNode(NodeT *P) {
    P->~NodeT();
    allocator.Deallocate(P);
  }

  IdxPair branchRoot(unsigned Position);
  IdxPair splitRoot(unsigned Position);

  void switchRootToBranch() {
    rootLeaf().~RootLeaf();
    height = 1;
    new (&rootBranchData()) RootBranchData();
  }

  void switchRootToLeaf() {
    rootBranchData().~RootBranchData();
    height = 0;
    new(&rootLeaf()) RootLeaf();
  }

  bool branched() const { return height > 0; }

  ValT treeSafeLookup(KeyT x, ValT NotFound) const;
  void visitNodes(void (IntervalMap::*f)(IntervalMapImpl::NodeRef,
                  unsigned Level));
  void deleteNode(IntervalMapImpl::NodeRef Node, unsigned Level);

public:
  explicit IntervalMap(Allocator &a) : height(0), rootSize(0), allocator(a) {
    assert((uintptr_t(data.buffer) & (alignof(RootLeaf) - 1)) == 0 &&
           "Insufficient alignment");
    new(&rootLeaf()) RootLeaf();
  }

  ~IntervalMap() {
    clear();
    rootLeaf().~RootLeaf();
  }

  /// empty -  Return true when no intervals are mapped.
  bool empty() const {
    return rootSize == 0;
  }

  /// start - Return the smallest mapped key in a non-empty map.
  KeyT start() const {
    assert(!empty() && "Empty IntervalMap has no start");
    return !branched() ? rootLeaf().start(0) : rootBranchStart();
  }

  /// stop - Return the largest mapped key in a non-empty map.
  KeyT stop() const {
    assert(!empty() && "Empty IntervalMap has no stop");
    return !branched() ? rootLeaf().stop(rootSize - 1) :
                         rootBranch().stop(rootSize - 1);
  }

  /// lookup - Return the mapped value at x or NotFound.
  ValT lookup(KeyT x, ValT NotFound = ValT()) const {
    if (empty() || Traits::startLess(x, start()) || Traits::stopLess(stop(), x))
      return NotFound;
    return branched() ? treeSafeLookup(x, NotFound) :
                        rootLeaf().safeLookup(x, NotFound);
  }

  /// insert - Add a mapping of [a;b] to y, coalesce with adjacent intervals.
  /// It is assumed that no key in the interval is mapped to another value, but
  /// overlapping intervals already mapped to y will be coalesced.
  void insert(KeyT a, KeyT b, ValT y) {
    if (branched() || rootSize == RootLeaf::Capacity)
      return find(a).insert(a, b, y);

    // Easy insert into root leaf.
    unsigned p = rootLeaf().findFrom(0, rootSize, a);
    rootSize = rootLeaf().insertFrom(p, rootSize, a, b, y);
  }

  /// clear - Remove all entries.
  void clear();

  class const_iterator;
  class iterator;
  friend class const_iterator;
  friend class iterator;

  const_iterator begin() const {
    const_iterator I(*this);
    I.goToBegin();
    return I;
  }

  iterator begin() {
    iterator I(*this);
    I.goToBegin();
    return I;
  }

  const_iterator end() const {
    const_iterator I(*this);
    I.goToEnd();
    return I;
  }

  iterator end() {
    iterator I(*this);
    I.goToEnd();
    return I;
  }

  /// find - Return an iterator pointing to the first interval ending at or
  /// after x, or end().
  const_iterator find(KeyT x) const {
    const_iterator I(*this);
    I.find(x);
    return I;
  }

  iterator find(KeyT x) {
    iterator I(*this);
    I.find(x);
    return I;
  }

  /// overlaps(a, b) - Return true if the intervals in this map overlap with the
  /// interval [a;b].
  bool overlaps(KeyT a, KeyT b) {
    assert(Traits::nonEmpty(a, b));
    const_iterator I = find(a);
    if (!I.valid())
      return false;
    // [a;b] and [x;y] overlap iff x<=b and a<=y. The find() call guarantees the
    // second part (y = find(a).stop()), so it is sufficient to check the first
    // one.
    return !Traits::stopLess(b, I.start());
  }
};

/// treeSafeLookup - Return the mapped value at x or NotFound, assuming a
/// branched root.
template <typename KeyT, typename ValT, unsigned N, typename Traits>
ValT IntervalMap<KeyT, ValT, N, Traits>::
treeSafeLookup(KeyT x, ValT NotFound) const {
  assert(branched() && "treeLookup assumes a branched root");

  IntervalMapImpl::NodeRef NR = rootBranch().safeLookup(x);
  for (unsigned h = height-1; h; --h)
    NR = NR.get<Branch>().safeLookup(x);
  return NR.get<Leaf>().safeLookup(x, NotFound);
}

// branchRoot - Switch from a leaf root to a branched root.
// Return the new (root offset, node offset) corresponding to Position.
template <typename KeyT, typename ValT, unsigned N, typename Traits>
IntervalMapImpl::IdxPair IntervalMap<KeyT, ValT, N, Traits>::
branchRoot(unsigned Position) {
  using namespace IntervalMapImpl;
  // How many external leaf nodes to hold RootLeaf+1?
  const unsigned Nodes = RootLeaf::Capacity / Leaf::Capacity + 1;

  // Compute element distribution among new nodes.
  unsigned size[Nodes];
  IdxPair NewOffset(0, Position);

  // Is is very common for the root node to be smaller than external nodes.
  if (Nodes == 1)
    size[0] = rootSize;
  else
    NewOffset = distribute(Nodes, rootSize, Leaf::Capacity,  nullptr, size,
                           Position, true);

  // Allocate new nodes.
  unsigned pos = 0;
  NodeRef node[Nodes];
  for (unsigned n = 0; n != Nodes; ++n) {
    Leaf *L = newNode<Leaf>();
    L->copy(rootLeaf(), pos, 0, size[n]);
    node[n] = NodeRef(L, size[n]);
    pos += size[n];
  }

  // Destroy the old leaf node, construct branch node instead.
  switchRootToBranch();
  for (unsigned n = 0; n != Nodes; ++n) {
    rootBranch().stop(n) = node[n].template get<Leaf>().stop(size[n]-1);
    rootBranch().subtree(n) = node[n];
  }
  rootBranchStart() = node[0].template get<Leaf>().start(0);
  rootSize = Nodes;
  return NewOffset;
}

// splitRoot - Split the current BranchRoot into multiple Branch nodes.
// Return the new (root offset, node offset) corresponding to Position.
template <typename KeyT, typename ValT, unsigned N, typename Traits>
IntervalMapImpl::IdxPair IntervalMap<KeyT, ValT, N, Traits>::
splitRoot(unsigned Position) {
  using namespace IntervalMapImpl;
  // How many external leaf nodes to hold RootBranch+1?
  const unsigned Nodes = RootBranch::Capacity / Branch::Capacity + 1;

  // Compute element distribution among new nodes.
  unsigned Size[Nodes];
  IdxPair NewOffset(0, Position);

  // Is is very common for the root node to be smaller than external nodes.
  if (Nodes == 1)
    Size[0] = rootSize;
  else
    NewOffset = distribute(Nodes, rootSize, Leaf::Capacity,  nullptr, Size,
                           Position, true);

  // Allocate new nodes.
  unsigned Pos = 0;
  NodeRef Node[Nodes];
  for (unsigned n = 0; n != Nodes; ++n) {
    Branch *B = newNode<Branch>();
    B->copy(rootBranch(), Pos, 0, Size[n]);
    Node[n] = NodeRef(B, Size[n]);
    Pos += Size[n];
  }

  for (unsigned n = 0; n != Nodes; ++n) {
    rootBranch().stop(n) = Node[n].template get<Branch>().stop(Size[n]-1);
    rootBranch().subtree(n) = Node[n];
  }
  rootSize = Nodes;
  ++height;
  return NewOffset;
}

/// visitNodes - Visit each external node.
template <typename KeyT, typename ValT, unsigned N, typename Traits>
void IntervalMap<KeyT, ValT, N, Traits>::
visitNodes(void (IntervalMap::*f)(IntervalMapImpl::NodeRef, unsigned Height)) {
  if (!branched())
    return;
  SmallVector<IntervalMapImpl::NodeRef, 4> Refs, NextRefs;

  // Collect level 0 nodes from the root.
  for (unsigned i = 0; i != rootSize; ++i)
    Refs.push_back(rootBranch().subtree(i));

  // Visit all branch nodes.
  for (unsigned h = height - 1; h; --h) {
    for (unsigned i = 0, e = Refs.size(); i != e; ++i) {
      for (unsigned j = 0, s = Refs[i].size(); j != s; ++j)
        NextRefs.push_back(Refs[i].subtree(j));
      (this->*f)(Refs[i], h);
    }
    Refs.clear();
    Refs.swap(NextRefs);
  }

  // Visit all leaf nodes.
  for (unsigned i = 0, e = Refs.size(); i != e; ++i)
    (this->*f)(Refs[i], 0);
}

template <typename KeyT, typename ValT, unsigned N, typename Traits>
void IntervalMap<KeyT, ValT, N, Traits>::
deleteNode(IntervalMapImpl::NodeRef Node, unsigned Level) {
  if (Level)
    deleteNode(&Node.get<Branch>());
  else
    deleteNode(&Node.get<Leaf>());
}

template <typename KeyT, typename ValT, unsigned N, typename Traits>
void IntervalMap<KeyT, ValT, N, Traits>::
clear() {
  if (branched()) {
    visitNodes(&IntervalMap::deleteNode);
    switchRootToLeaf();
  }
  rootSize = 0;
}

//===----------------------------------------------------------------------===//
//---                   IntervalMap::const_iterator                       ----//
//===----------------------------------------------------------------------===//

template <typename KeyT, typename ValT, unsigned N, typename Traits>
class IntervalMap<KeyT, ValT, N, Traits>::const_iterator :
  public std::iterator<std::bidirectional_iterator_tag, ValT> {

protected:
  friend class IntervalMap;

  // The map referred to.
  IntervalMap *map = nullptr;

  // We store a full path from the root to the current position.
  // The path may be partially filled, but never between iterator calls.
  IntervalMapImpl::Path path;

  explicit const_iterator(const IntervalMap &map) :
    map(const_cast<IntervalMap*>(&map)) {}

  bool branched() const {
    assert(map && "Invalid iterator");
    return map->branched();
  }

  void setRoot(unsigned Offset) {
    if (branched())
      path.setRoot(&map->rootBranch(), map->rootSize, Offset);
    else
      path.setRoot(&map->rootLeaf(), map->rootSize, Offset);
  }

  void pathFillFind(KeyT x);
  void treeFind(KeyT x);
  void treeAdvanceTo(KeyT x);

  /// unsafeStart - Writable access to start() for iterator.
  KeyT &unsafeStart() const {
    assert(valid() && "Cannot access invalid iterator");
    return branched() ? path.leaf<Leaf>().start(path.leafOffset()) :
                        path.leaf<RootLeaf>().start(path.leafOffset());
  }

  /// unsafeStop - Writable access to stop() for iterator.
  KeyT &unsafeStop() const {
    assert(valid() && "Cannot access invalid iterator");
    return branched() ? path.leaf<Leaf>().stop(path.leafOffset()) :
                        path.leaf<RootLeaf>().stop(path.leafOffset());
  }

  /// unsafeValue - Writable access to value() for iterator.
  ValT &unsafeValue() const {
    assert(valid() && "Cannot access invalid iterator");
    return branched() ? path.leaf<Leaf>().value(path.leafOffset()) :
                        path.leaf<RootLeaf>().value(path.leafOffset());
  }

public:
  /// const_iterator - Create an iterator that isn't pointing anywhere.
  const_iterator() = default;

  /// setMap - Change the map iterated over. This call must be followed by a
  /// call to goToBegin(), goToEnd(), or find()
  void setMap(const IntervalMap &m) { map = const_cast<IntervalMap*>(&m); }

  /// valid - Return true if the current position is valid, false for end().
  bool valid() const { return path.valid(); }

  /// atBegin - Return true if the current position is the first map entry.
  bool atBegin() const { return path.atBegin(); }

  /// start - Return the beginning of the current interval.
  const KeyT &start() const { return unsafeStart(); }

  /// stop - Return the end of the current interval.
  const KeyT &stop() const { return unsafeStop(); }

  /// value - Return the mapped value at the current interval.
  const ValT &value() const { return unsafeValue(); }

  const ValT &operator*() const { return value(); }

  bool operator==(const const_iterator &RHS) const {
    assert(map == RHS.map && "Cannot compare iterators from different maps");
    if (!valid())
      return !RHS.valid();
    if (path.leafOffset() != RHS.path.leafOffset())
      return false;
    return &path.template leaf<Leaf>() == &RHS.path.template leaf<Leaf>();
  }

  bool operator!=(const const_iterator &RHS) const {
    return !operator==(RHS);
  }

  /// goToBegin - Move to the first interval in map.
  void goToBegin() {
    setRoot(0);
    if (branched())
      path.fillLeft(map->height);
  }

  /// goToEnd - Move beyond the last interval in map.
  void goToEnd() {
    setRoot(map->rootSize);
  }

  /// preincrement - move to the next interval.
  const_iterator &operator++() {
    assert(valid() && "Cannot increment end()");
    if (++path.leafOffset() == path.leafSize() && branched())
      path.moveRight(map->height);
    return *this;
  }

  /// postincrement - Dont do that!
  const_iterator operator++(int) {
    const_iterator tmp = *this;
    operator++();
    return tmp;
  }

  /// predecrement - move to the previous interval.
  const_iterator &operator--() {
    if (path.leafOffset() && (valid() || !branched()))
      --path.leafOffset();
    else
      path.moveLeft(map->height);
    return *this;
  }

  /// postdecrement - Dont do that!
  const_iterator operator--(int) {
    const_iterator tmp = *this;
    operator--();
    return tmp;
  }

  /// find - Move to the first interval with stop >= x, or end().
  /// This is a full search from the root, the current position is ignored.
  void find(KeyT x) {
    if (branched())
      treeFind(x);
    else
      setRoot(map->rootLeaf().findFrom(0, map->rootSize, x));
  }

  /// advanceTo - Move to the first interval with stop >= x, or end().
  /// The search is started from the current position, and no earlier positions
  /// can be found. This is much faster than find() for small moves.
  void advanceTo(KeyT x) {
    if (!valid())
      return;
    if (branched())
      treeAdvanceTo(x);
    else
      path.leafOffset() =
        map->rootLeaf().findFrom(path.leafOffset(), map->rootSize, x);
  }
};

/// pathFillFind - Complete path by searching for x.
/// @param x Key to search for.
template <typename KeyT, typename ValT, unsigned N, typename Traits>
void IntervalMap<KeyT, ValT, N, Traits>::
const_iterator::pathFillFind(KeyT x) {
  IntervalMapImpl::NodeRef NR = path.subtree(path.height());
  for (unsigned i = map->height - path.height() - 1; i; --i) {
    unsigned p = NR.get<Branch>().safeFind(0, x);
    path.push(NR, p);
    NR = NR.subtree(p);
  }
  path.push(NR, NR.get<Leaf>().safeFind(0, x));
}

/// treeFind - Find in a branched tree.
/// @param x Key to search for.
template <typename KeyT, typename ValT, unsigned N, typename Traits>
void IntervalMap<KeyT, ValT, N, Traits>::
const_iterator::treeFind(KeyT x) {
  setRoot(map->rootBranch().findFrom(0, map->rootSize, x));
  if (valid())
    pathFillFind(x);
}

/// treeAdvanceTo - Find position after the current one.
/// @param x Key to search for.
template <typename KeyT, typename ValT, unsigned N, typename Traits>
void IntervalMap<KeyT, ValT, N, Traits>::
const_iterator::treeAdvanceTo(KeyT x) {
  // Can we stay on the same leaf node?
  if (!Traits::stopLess(path.leaf<Leaf>().stop(path.leafSize() - 1), x)) {
    path.leafOffset() = path.leaf<Leaf>().safeFind(path.leafOffset(), x);
    return;
  }

  // Drop the current leaf.
  path.pop();

  // Search towards the root for a usable subtree.
  if (path.height()) {
    for (unsigned l = path.height() - 1; l; --l) {
      if (!Traits::stopLess(path.node<Branch>(l).stop(path.offset(l)), x)) {
        // The branch node at l+1 is usable
        path.offset(l + 1) =
          path.node<Branch>(l + 1).safeFind(path.offset(l + 1), x);
        return pathFillFind(x);
      }
      path.pop();
    }
    // Is the level-1 Branch usable?
    if (!Traits::stopLess(map->rootBranch().stop(path.offset(0)), x)) {
      path.offset(1) = path.node<Branch>(1).safeFind(path.offset(1), x);
      return pathFillFind(x);
    }
  }

  // We reached the root.
  setRoot(map->rootBranch().findFrom(path.offset(0), map->rootSize, x));
  if (valid())
    pathFillFind(x);
}

//===----------------------------------------------------------------------===//
//---                       IntervalMap::iterator                         ----//
//===----------------------------------------------------------------------===//

template <typename KeyT, typename ValT, unsigned N, typename Traits>
class IntervalMap<KeyT, ValT, N, Traits>::iterator : public const_iterator {
  friend class IntervalMap;

  using IdxPair = IntervalMapImpl::IdxPair;

  explicit iterator(IntervalMap &map) : const_iterator(map) {}

  void setNodeStop(unsigned Level, KeyT Stop);
  bool insertNode(unsigned Level, IntervalMapImpl::NodeRef Node, KeyT Stop);
  template <typename NodeT> bool overflow(unsigned Level);
  void treeInsert(KeyT a, KeyT b, ValT y);
  void eraseNode(unsigned Level);
  void treeErase(bool UpdateRoot = true);
  bool canCoalesceLeft(KeyT Start, ValT x);
  bool canCoalesceRight(KeyT Stop, ValT x);

public:
  /// iterator - Create null iterator.
  iterator() = default;

  /// setStart - Move the start of the current interval.
  /// This may cause coalescing with the previous interval.
  /// @param a New start key, must not overlap the previous interval.
  void setStart(KeyT a);

  /// setStop - Move the end of the current interval.
  /// This may cause coalescing with the following interval.
  /// @param b New stop key, must not overlap the following interval.
  void setStop(KeyT b);

  /// setValue - Change the mapped value of the current interval.
  /// This may cause coalescing with the previous and following intervals.
  /// @param x New value.
  void setValue(ValT x);

  /// setStartUnchecked - Move the start of the current interval without
  /// checking for coalescing or overlaps.
  /// This should only be used when it is known that coalescing is not required.
  /// @param a New start key.
  void setStartUnchecked(KeyT a) { this->unsafeStart() = a; }

  /// setStopUnchecked - Move the end of the current interval without checking
  /// for coalescing or overlaps.
  /// This should only be used when it is known that coalescing is not required.
  /// @param b New stop key.
  void setStopUnchecked(KeyT b) {
    this->unsafeStop() = b;
    // Update keys in branch nodes as well.
    if (this->path.atLastEntry(this->path.height()))
      setNodeStop(this->path.height(), b);
  }

  /// setValueUnchecked - Change the mapped value of the current interval
  /// without checking for coalescing.
  /// @param x New value.
  void setValueUnchecked(ValT x) { this->unsafeValue() = x; }

  /// insert - Insert mapping [a;b] -> y before the current position.
  void insert(KeyT a, KeyT b, ValT y);

  /// erase - Erase the current interval.
  void erase();

  iterator &operator++() {
    const_iterator::operator++();
    return *this;
  }

  iterator operator++(int) {
    iterator tmp = *this;
    operator++();
    return tmp;
  }

  iterator &operator--() {
    const_iterator::operator--();
    return *this;
  }

  iterator operator--(int) {
    iterator tmp = *this;
    operator--();
    return tmp;
  }
};

/// canCoalesceLeft - Can the current interval coalesce to the left after
/// changing start or value?
/// @param Start New start of current interval.
/// @param Value New value for current interval.
/// @return True when updating the current interval would enable coalescing.
template <typename KeyT, typename ValT, unsigned N, typename Traits>
bool IntervalMap<KeyT, ValT, N, Traits>::
iterator::canCoalesceLeft(KeyT Start, ValT Value) {
  using namespace IntervalMapImpl;
  Path &P = this->path;
  if (!this->branched()) {
    unsigned i = P.leafOffset();
    RootLeaf &Node = P.leaf<RootLeaf>();
    return i && Node.value(i-1) == Value &&
                Traits::adjacent(Node.stop(i-1), Start);
  }
  // Branched.
  if (unsigned i = P.leafOffset()) {
    Leaf &Node = P.leaf<Leaf>();
    return Node.value(i-1) == Value && Traits::adjacent(Node.stop(i-1), Start);
  } else if (NodeRef NR = P.getLeftSibling(P.height())) {
    unsigned i = NR.size() - 1;
    Leaf &Node = NR.get<Leaf>();
    return Node.value(i) == Value && Traits::adjacent(Node.stop(i), Start);
  }
  return false;
}

/// canCoalesceRight - Can the current interval coalesce to the right after
/// changing stop or value?
/// @param Stop New stop of current interval.
/// @param Value New value for current interval.
/// @return True when updating the current interval would enable coalescing.
template <typename KeyT, typename ValT, unsigned N, typename Traits>
bool IntervalMap<KeyT, ValT, N, Traits>::
iterator::canCoalesceRight(KeyT Stop, ValT Value) {
  using namespace IntervalMapImpl;
  Path &P = this->path;
  unsigned i = P.leafOffset() + 1;
  if (!this->branched()) {
    if (i >= P.leafSize())
      return false;
    RootLeaf &Node = P.leaf<RootLeaf>();
    return Node.value(i) == Value && Traits::adjacent(Stop, Node.start(i));
  }
  // Branched.
  if (i < P.leafSize()) {
    Leaf &Node = P.leaf<Leaf>();
    return Node.value(i) == Value && Traits::adjacent(Stop, Node.start(i));
  } else if (NodeRef NR = P.getRightSibling(P.height())) {
    Leaf &Node = NR.get<Leaf>();
    return Node.value(0) == Value && Traits::adjacent(Stop, Node.start(0));
  }
  return false;
}

/// setNodeStop - Update the stop key of the current node at level and above.
template <typename KeyT, typename ValT, unsigned N, typename Traits>
void IntervalMap<KeyT, ValT, N, Traits>::
iterator::setNodeStop(unsigned Level, KeyT Stop) {
  // There are no references to the root node, so nothing to update.
  if (!Level)
    return;
  IntervalMapImpl::Path &P = this->path;
  // Update nodes pointing to the current node.
  while (--Level) {
    P.node<Branch>(Level).stop(P.offset(Level)) = Stop;
    if (!P.atLastEntry(Level))
      return;
  }
  // Update root separately since it has a different layout.
  P.node<RootBranch>(Level).stop(P.offset(Level)) = Stop;
}

template <typename KeyT, typename ValT, unsigned N, typename Traits>
void IntervalMap<KeyT, ValT, N, Traits>::
iterator::setStart(KeyT a) {
  assert(Traits::nonEmpty(a, this->stop()) && "Cannot move start beyond stop");
  KeyT &CurStart = this->unsafeStart();
  if (!Traits::startLess(a, CurStart) || !canCoalesceLeft(a, this->value())) {
    CurStart = a;
    return;
  }
  // Coalesce with the interval to the left.
  --*this;
  a = this->start();
  erase();
  setStartUnchecked(a);
}

template <typename KeyT, typename ValT, unsigned N, typename Traits>
void IntervalMap<KeyT, ValT, N, Traits>::
iterator::setStop(KeyT b) {
  assert(Traits::nonEmpty(this->start(), b) && "Cannot move stop beyond start");
  if (Traits::startLess(b, this->stop()) ||
      !canCoalesceRight(b, this->value())) {
    setStopUnchecked(b);
    return;
  }
  // Coalesce with interval to the right.
  KeyT a = this->start();
  erase();
  setStartUnchecked(a);
}

template <typename KeyT, typename ValT, unsigned N, typename Traits>
void IntervalMap<KeyT, ValT, N, Traits>::
iterator::setValue(ValT x) {
  setValueUnchecked(x);
  if (canCoalesceRight(this->stop(), x)) {
    KeyT a = this->start();
    erase();
    setStartUnchecked(a);
  }
  if (canCoalesceLeft(this->start(), x)) {
    --*this;
    KeyT a = this->start();
    erase();
    setStartUnchecked(a);
  }
}

/// insertNode - insert a node before the current path at level.
/// Leave the current path pointing at the new node.
/// @param Level path index of the node to be inserted.
/// @param Node The node to be inserted.
/// @param Stop The last index in the new node.
/// @return True if the tree height was increased.
template <typename KeyT, typename ValT, unsigned N, typename Traits>
bool IntervalMap<KeyT, ValT, N, Traits>::
iterator::insertNode(unsigned Level, IntervalMapImpl::NodeRef Node, KeyT Stop) {
  assert(Level && "Cannot insert next to the root");
  bool SplitRoot = false;
  IntervalMap &IM = *this->map;
  IntervalMapImpl::Path &P = this->path;

  if (Level == 1) {
    // Insert into the root branch node.
    if (IM.rootSize < RootBranch::Capacity) {
      IM.rootBranch().insert(P.offset(0), IM.rootSize, Node, Stop);
      P.setSize(0, ++IM.rootSize);
      P.reset(Level);
      return SplitRoot;
    }

    // We need to split the root while keeping our position.
    SplitRoot = true;
    IdxPair Offset = IM.splitRoot(P.offset(0));
    P.replaceRoot(&IM.rootBranch(), IM.rootSize, Offset);

    // Fall through to insert at the new higher level.
    ++Level;
  }

  // When inserting before end(), make sure we have a valid path.
  P.legalizeForInsert(--Level);

  // Insert into the branch node at Level-1.
  if (P.size(Level) == Branch::Capacity) {
    // Branch node is full, handle handle the overflow.
    assert(!SplitRoot && "Cannot overflow after splitting the root");
    SplitRoot = overflow<Branch>(Level);
    Level += SplitRoot;
  }
  P.node<Branch>(Level).insert(P.offset(Level), P.size(Level), Node, Stop);
  P.setSize(Level, P.size(Level) + 1);
  if (P.atLastEntry(Level))
    setNodeStop(Level, Stop);
  P.reset(Level + 1);
  return SplitRoot;
}

// insert
template <typename KeyT, typename ValT, unsigned N, typename Traits>
void IntervalMap<KeyT, ValT, N, Traits>::
iterator::insert(KeyT a, KeyT b, ValT y) {
  if (this->branched())
    return treeInsert(a, b, y);
  IntervalMap &IM = *this->map;
  IntervalMapImpl::Path &P = this->path;

  // Try simple root leaf insert.
  unsigned Size = IM.rootLeaf().insertFrom(P.leafOffset(), IM.rootSize, a, b, y);

  // Was the root node insert successful?
  if (Size <= RootLeaf::Capacity) {
    P.setSize(0, IM.rootSize = Size);
    return;
  }

  // Root leaf node is full, we must branch.
  IdxPair Offset = IM.branchRoot(P.leafOffset());
  P.replaceRoot(&IM.rootBranch(), IM.rootSize, Offset);

  // Now it fits in the new leaf.
  treeInsert(a, b, y);
}

template <typename KeyT, typename ValT, unsigned N, typename Traits>
void IntervalMap<KeyT, ValT, N, Traits>::
iterator::treeInsert(KeyT a, KeyT b, ValT y) {
  using namespace IntervalMapImpl;
  Path &P = this->path;

  if (!P.valid())
    P.legalizeForInsert(this->map->height);

  // Check if this insertion will extend the node to the left.
  if (P.leafOffset() == 0 && Traits::startLess(a, P.leaf<Leaf>().start(0))) {
    // Node is growing to the left, will it affect a left sibling node?
    if (NodeRef Sib = P.getLeftSibling(P.height())) {
      Leaf &SibLeaf = Sib.get<Leaf>();
      unsigned SibOfs = Sib.size() - 1;
      if (SibLeaf.value(SibOfs) == y &&
          Traits::adjacent(SibLeaf.stop(SibOfs), a)) {
        // This insertion will coalesce with the last entry in SibLeaf. We can
        // handle it in two ways:
        //  1. Extend SibLeaf.stop to b and be done, or
        //  2. Extend a to SibLeaf, erase the SibLeaf entry and continue.
        // We prefer 1., but need 2 when coalescing to the right as well.
        Leaf &CurLeaf = P.leaf<Leaf>();
        P.moveLeft(P.height());
        if (Traits::stopLess(b, CurLeaf.start(0)) &&
            (y != CurLeaf.value(0) || !Traits::adjacent(b, CurLeaf.start(0)))) {
          // Easy, just extend SibLeaf and we're done.
          setNodeStop(P.height(), SibLeaf.stop(SibOfs) = b);
          return;
        } else {
          // We have both left and right coalescing. Erase the old SibLeaf entry
          // and continue inserting the larger interval.
          a = SibLeaf.start(SibOfs);
          treeErase(/* UpdateRoot= */false);
        }
      }
    } else {
      // No left sibling means we are at begin(). Update cached bound.
      this->map->rootBranchStart() = a;
    }
  }

  // When we are inserting at the end of a leaf node, we must update stops.
  unsigned Size = P.leafSize();
  bool Grow = P.leafOffset() == Size;
  Size = P.leaf<Leaf>().insertFrom(P.leafOffset(), Size, a, b, y);

  // Leaf insertion unsuccessful? Overflow and try again.
  if (Size > Leaf::Capacity) {
    overflow<Leaf>(P.height());
    Grow = P.leafOffset() == P.leafSize();
    Size = P.leaf<Leaf>().insertFrom(P.leafOffset(), P.leafSize(), a, b, y);
    assert(Size <= Leaf::Capacity && "overflow() didn't make room");
  }

  // Inserted, update offset and leaf size.
  P.setSize(P.height(), Size);

  // Insert was the last node entry, update stops.
  if (Grow)
    setNodeStop(P.height(), b);
}

/// erase - erase the current interval and move to the next position.
template <typename KeyT, typename ValT, unsigned N, typename Traits>
void IntervalMap<KeyT, ValT, N, Traits>::
iterator::erase() {
  IntervalMap &IM = *this->map;
  IntervalMapImpl::Path &P = this->path;
  assert(P.valid() && "Cannot erase end()");
  if (this->branched())
    return treeErase();
  IM.rootLeaf().erase(P.leafOffset(), IM.rootSize);
  P.setSize(0, --IM.rootSize);
}

/// treeErase - erase() for a branched tree.
template <typename KeyT, typename ValT, unsigned N, typename Traits>
void IntervalMap<KeyT, ValT, N, Traits>::
iterator::treeErase(bool UpdateRoot) {
  IntervalMap &IM = *this->map;
  IntervalMapImpl::Path &P = this->path;
  Leaf &Node = P.leaf<Leaf>();

  // Nodes are not allowed to become empty.
  if (P.leafSize() == 1) {
    IM.deleteNode(&Node);
    eraseNode(IM.height);
    // Update rootBranchStart if we erased begin().
    if (UpdateRoot && IM.branched() && P.valid() && P.atBegin())
      IM.rootBranchStart() = P.leaf<Leaf>().start(0);
    return;
  }

  // Erase current entry.
  Node.erase(P.leafOffset(), P.leafSize());
  unsigned NewSize = P.leafSize() - 1;
  P.setSize(IM.height, NewSize);
  // When we erase the last entry, update stop and move to a legal position.
  if (P.leafOffset() == NewSize) {
    setNodeStop(IM.height, Node.stop(NewSize - 1));
    P.moveRight(IM.height);
  } else if (UpdateRoot && P.atBegin())
    IM.rootBranchStart() = P.leaf<Leaf>().start(0);
}

/// eraseNode - Erase the current node at Level from its parent and move path to
/// the first entry of the next sibling node.
/// The node must be deallocated by the caller.
/// @param Level 1..height, the root node cannot be erased.
template <typename KeyT, typename ValT, unsigned N, typename Traits>
void IntervalMap<KeyT, ValT, N, Traits>::
iterator::eraseNode(unsigned Level) {
  assert(Level && "Cannot erase root node");
  IntervalMap &IM = *this->map;
  IntervalMapImpl::Path &P = this->path;

  if (--Level == 0) {
    IM.rootBranch().erase(P.offset(0), IM.rootSize);
    P.setSize(0, --IM.rootSize);
    // If this cleared the root, switch to height=0.
    if (IM.empty()) {
      IM.switchRootToLeaf();
      this->setRoot(0);
      return;
    }
  } else {
    // Remove node ref from branch node at Level.
    Branch &Parent = P.node<Branch>(Level);
    if (P.size(Level) == 1) {
      // Branch node became empty, remove it recursively.
      IM.deleteNode(&Parent);
      eraseNode(Level);
    } else {
      // Branch node won't become empty.
      Parent.erase(P.offset(Level), P.size(Level));
      unsigned NewSize = P.size(Level) - 1;
      P.setSize(Level, NewSize);
      // If we removed the last branch, update stop and move to a legal pos.
      if (P.offset(Level) == NewSize) {
        setNodeStop(Level, Parent.stop(NewSize - 1));
        P.moveRight(Level);
      }
    }
  }
  // Update path cache for the new right sibling position.
  if (P.valid()) {
    P.reset(Level + 1);
    P.offset(Level + 1) = 0;
  }
}

/// overflow - Distribute entries of the current node evenly among
/// its siblings and ensure that the current node is not full.
/// This may require allocating a new node.
/// @tparam NodeT The type of node at Level (Leaf or Branch).
/// @param Level path index of the overflowing node.
/// @return True when the tree height was changed.
template <typename KeyT, typename ValT, unsigned N, typename Traits>
template <typename NodeT>
bool IntervalMap<KeyT, ValT, N, Traits>::
iterator::overflow(unsigned Level) {
  using namespace IntervalMapImpl;
  Path &P = this->path;
  unsigned CurSize[4];
  NodeT *Node[4];
  unsigned Nodes = 0;
  unsigned Elements = 0;
  unsigned Offset = P.offset(Level);

  // Do we have a left sibling?
  NodeRef LeftSib = P.getLeftSibling(Level);
  if (LeftSib) {
    Offset += Elements = CurSize[Nodes] = LeftSib.size();
    Node[Nodes++] = &LeftSib.get<NodeT>();
  }

  // Current node.
  Elements += CurSize[Nodes] = P.size(Level);
  Node[Nodes++] = &P.node<NodeT>(Level);

  // Do we have a right sibling?
  NodeRef RightSib = P.getRightSibling(Level);
  if (RightSib) {
    Elements += CurSize[Nodes] = RightSib.size();
    Node[Nodes++] = &RightSib.get<NodeT>();
  }

  // Do we need to allocate a new node?
  unsigned NewNode = 0;
  if (Elements + 1 > Nodes * NodeT::Capacity) {
    // Insert NewNode at the penultimate position, or after a single node.
    NewNode = Nodes == 1 ? 1 : Nodes - 1;
    CurSize[Nodes] = CurSize[NewNode];
    Node[Nodes] = Node[NewNode];
    CurSize[NewNode] = 0;
    Node[NewNode] = this->map->template newNode<NodeT>();
    ++Nodes;
  }

  // Compute the new element distribution.
  unsigned NewSize[4];
  IdxPair NewOffset = distribute(Nodes, Elements, NodeT::Capacity,
                                 CurSize, NewSize, Offset, true);
  adjustSiblingSizes(Node, Nodes, CurSize, NewSize);

  // Move current location to the leftmost node.
  if (LeftSib)
    P.moveLeft(Level);

  // Elements have been rearranged, now update node sizes and stops.
  bool SplitRoot = false;
  unsigned Pos = 0;
  while (true) {
    KeyT Stop = Node[Pos]->stop(NewSize[Pos]-1);
    if (NewNode && Pos == NewNode) {
      SplitRoot = insertNode(Level, NodeRef(Node[Pos], NewSize[Pos]), Stop);
      Level += SplitRoot;
    } else {
      P.setSize(Level, NewSize[Pos]);
      setNodeStop(Level, Stop);
    }
    if (Pos + 1 == Nodes)
      break;
    P.moveRight(Level);
    ++Pos;
  }

  // Where was I? Find NewOffset.
  while(Pos != NewOffset.first) {
    P.moveLeft(Level);
    --Pos;
  }
  P.offset(Level) = NewOffset.second;
  return SplitRoot;
}

//===----------------------------------------------------------------------===//
//---                       IntervalMapOverlaps                           ----//
//===----------------------------------------------------------------------===//

/// IntervalMapOverlaps - Iterate over the overlaps of mapped intervals in two
/// IntervalMaps. The maps may be different, but the KeyT and Traits types
/// should be the same.
///
/// Typical uses:
///
/// 1. Test for overlap:
///    bool overlap = IntervalMapOverlaps(a, b).valid();
///
/// 2. Enumerate overlaps:
///    for (IntervalMapOverlaps I(a, b); I.valid() ; ++I) { ... }
///
template <typename MapA, typename MapB>
class IntervalMapOverlaps {
  using KeyType = typename MapA::KeyType;
  using Traits = typename MapA::KeyTraits;

  typename MapA::const_iterator posA;
  typename MapB::const_iterator posB;

  /// advance - Move posA and posB forward until reaching an overlap, or until
  /// either meets end.
  /// Don't move the iterators if they are already overlapping.
  void advance() {
    if (!valid())
      return;

    if (Traits::stopLess(posA.stop(), posB.start())) {
      // A ends before B begins. Catch up.
      posA.advanceTo(posB.start());
      if (!posA.valid() || !Traits::stopLess(posB.stop(), posA.start()))
        return;
    } else if (Traits::stopLess(posB.stop(), posA.start())) {
      // B ends before A begins. Catch up.
      posB.advanceTo(posA.start());
      if (!posB.valid() || !Traits::stopLess(posA.stop(), posB.start()))
        return;
    } else
      // Already overlapping.
      return;

    while (true) {
      // Make a.end > b.start.
      posA.advanceTo(posB.start());
      if (!posA.valid() || !Traits::stopLess(posB.stop(), posA.start()))
        return;
      // Make b.end > a.start.
      posB.advanceTo(posA.start());
      if (!posB.valid() || !Traits::stopLess(posA.stop(), posB.start()))
        return;
    }
  }

public:
  /// IntervalMapOverlaps - Create an iterator for the overlaps of a and b.
  IntervalMapOverlaps(const MapA &a, const MapB &b)
    : posA(b.empty() ? a.end() : a.find(b.start())),
      posB(posA.valid() ? b.find(posA.start()) : b.end()) { advance(); }

  /// valid - Return true if iterator is at an overlap.
  bool valid() const {
    return posA.valid() && posB.valid();
  }

  /// a - access the left hand side in the overlap.
  const typename MapA::const_iterator &a() const { return posA; }

  /// b - access the right hand side in the overlap.
  const typename MapB::const_iterator &b() const { return posB; }

  /// start - Beginning of the overlapping interval.
  KeyType start() const {
    KeyType ak = a().start();
    KeyType bk = b().start();
    return Traits::startLess(ak, bk) ? bk : ak;
  }

  /// stop - End of the overlapping interval.
  KeyType stop() const {
    KeyType ak = a().stop();
    KeyType bk = b().stop();
    return Traits::startLess(ak, bk) ? ak : bk;
  }

  /// skipA - Move to the next overlap that doesn't involve a().
  void skipA() {
    ++posA;
    advance();
  }

  /// skipB - Move to the next overlap that doesn't involve b().
  void skipB() {
    ++posB;
    advance();
  }

  /// Preincrement - Move to the next overlap.
  IntervalMapOverlaps &operator++() {
    // Bump the iterator that ends first. The other one may have more overlaps.
    if (Traits::startLess(posB.stop(), posA.stop()))
      skipB();
    else
      skipA();
    return *this;
  }

  /// advanceTo - Move to the first overlapping interval with
  /// stopLess(x, stop()).
  void advanceTo(KeyType x) {
    if (!valid())
      return;
    // Make sure advanceTo sees monotonic keys.
    if (Traits::stopLess(posA.stop(), x))
      posA.advanceTo(x);
    if (Traits::stopLess(posB.stop(), x))
      posB.advanceTo(x);
    advance();
  }
};

} // end namespace llvm

#endif // LLVM_ADT_INTERVALMAP_H
