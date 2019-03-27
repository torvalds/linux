//===- llvm/ADT/SparseMultiSet.h - Sparse multiset --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the SparseMultiSet class, which adds multiset behavior to
// the SparseSet.
//
// A sparse multiset holds a small number of objects identified by integer keys
// from a moderately sized universe. The sparse multiset uses more memory than
// other containers in order to provide faster operations. Any key can map to
// multiple values. A SparseMultiSetNode class is provided, which serves as a
// convenient base class for the contents of a SparseMultiSet.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SPARSEMULTISET_H
#define LLVM_ADT_SPARSEMULTISET_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SparseSet.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <utility>

namespace llvm {

/// Fast multiset implementation for objects that can be identified by small
/// unsigned keys.
///
/// SparseMultiSet allocates memory proportional to the size of the key
/// universe, so it is not recommended for building composite data structures.
/// It is useful for algorithms that require a single set with fast operations.
///
/// Compared to DenseSet and DenseMap, SparseMultiSet provides constant-time
/// fast clear() as fast as a vector.  The find(), insert(), and erase()
/// operations are all constant time, and typically faster than a hash table.
/// The iteration order doesn't depend on numerical key values, it only depends
/// on the order of insert() and erase() operations.  Iteration order is the
/// insertion order. Iteration is only provided over elements of equivalent
/// keys, but iterators are bidirectional.
///
/// Compared to BitVector, SparseMultiSet<unsigned> uses 8x-40x more memory, but
/// offers constant-time clear() and size() operations as well as fast iteration
/// independent on the size of the universe.
///
/// SparseMultiSet contains a dense vector holding all the objects and a sparse
/// array holding indexes into the dense vector.  Most of the memory is used by
/// the sparse array which is the size of the key universe. The SparseT template
/// parameter provides a space/speed tradeoff for sets holding many elements.
///
/// When SparseT is uint32_t, find() only touches up to 3 cache lines, but the
/// sparse array uses 4 x Universe bytes.
///
/// When SparseT is uint8_t (the default), find() touches up to 3+[N/256] cache
/// lines, but the sparse array is 4x smaller.  N is the number of elements in
/// the set.
///
/// For sets that may grow to thousands of elements, SparseT should be set to
/// uint16_t or uint32_t.
///
/// Multiset behavior is provided by providing doubly linked lists for values
/// that are inlined in the dense vector. SparseMultiSet is a good choice when
/// one desires a growable number of entries per key, as it will retain the
/// SparseSet algorithmic properties despite being growable. Thus, it is often a
/// better choice than a SparseSet of growable containers or a vector of
/// vectors. SparseMultiSet also keeps iterators valid after erasure (provided
/// the iterators don't point to the element erased), allowing for more
/// intuitive and fast removal.
///
/// @tparam ValueT      The type of objects in the set.
/// @tparam KeyFunctorT A functor that computes an unsigned index from KeyT.
/// @tparam SparseT     An unsigned integer type. See above.
///
template<typename ValueT,
         typename KeyFunctorT = identity<unsigned>,
         typename SparseT = uint8_t>
class SparseMultiSet {
  static_assert(std::numeric_limits<SparseT>::is_integer &&
                !std::numeric_limits<SparseT>::is_signed,
                "SparseT must be an unsigned integer type");

  /// The actual data that's stored, as a doubly-linked list implemented via
  /// indices into the DenseVector.  The doubly linked list is implemented
  /// circular in Prev indices, and INVALID-terminated in Next indices. This
  /// provides efficient access to list tails. These nodes can also be
  /// tombstones, in which case they are actually nodes in a single-linked
  /// freelist of recyclable slots.
  struct SMSNode {
    static const unsigned INVALID = ~0U;

    ValueT Data;
    unsigned Prev;
    unsigned Next;

    SMSNode(ValueT D, unsigned P, unsigned N) : Data(D), Prev(P), Next(N) {}

    /// List tails have invalid Nexts.
    bool isTail() const {
      return Next == INVALID;
    }

    /// Whether this node is a tombstone node, and thus is in our freelist.
    bool isTombstone() const {
      return Prev == INVALID;
    }

    /// Since the list is circular in Prev, all non-tombstone nodes have a valid
    /// Prev.
    bool isValid() const { return Prev != INVALID; }
  };

  using KeyT = typename KeyFunctorT::argument_type;
  using DenseT = SmallVector<SMSNode, 8>;
  DenseT Dense;
  SparseT *Sparse = nullptr;
  unsigned Universe = 0;
  KeyFunctorT KeyIndexOf;
  SparseSetValFunctor<KeyT, ValueT, KeyFunctorT> ValIndexOf;

  /// We have a built-in recycler for reusing tombstone slots. This recycler
  /// puts a singly-linked free list into tombstone slots, allowing us quick
  /// erasure, iterator preservation, and dense size.
  unsigned FreelistIdx = SMSNode::INVALID;
  unsigned NumFree = 0;

  unsigned sparseIndex(const ValueT &Val) const {
    assert(ValIndexOf(Val) < Universe &&
           "Invalid key in set. Did object mutate?");
    return ValIndexOf(Val);
  }
  unsigned sparseIndex(const SMSNode &N) const { return sparseIndex(N.Data); }

  /// Whether the given entry is the head of the list. List heads's previous
  /// pointers are to the tail of the list, allowing for efficient access to the
  /// list tail. D must be a valid entry node.
  bool isHead(const SMSNode &D) const {
    assert(D.isValid() && "Invalid node for head");
    return Dense[D.Prev].isTail();
  }

  /// Whether the given entry is a singleton entry, i.e. the only entry with
  /// that key.
  bool isSingleton(const SMSNode &N) const {
    assert(N.isValid() && "Invalid node for singleton");
    // Is N its own predecessor?
    return &Dense[N.Prev] == &N;
  }

  /// Add in the given SMSNode. Uses a free entry in our freelist if
  /// available. Returns the index of the added node.
  unsigned addValue(const ValueT& V, unsigned Prev, unsigned Next) {
    if (NumFree == 0) {
      Dense.push_back(SMSNode(V, Prev, Next));
      return Dense.size() - 1;
    }

    // Peel off a free slot
    unsigned Idx = FreelistIdx;
    unsigned NextFree = Dense[Idx].Next;
    assert(Dense[Idx].isTombstone() && "Non-tombstone free?");

    Dense[Idx] = SMSNode(V, Prev, Next);
    FreelistIdx = NextFree;
    --NumFree;
    return Idx;
  }

  /// Make the current index a new tombstone. Pushes it onto the freelist.
  void makeTombstone(unsigned Idx) {
    Dense[Idx].Prev = SMSNode::INVALID;
    Dense[Idx].Next = FreelistIdx;
    FreelistIdx = Idx;
    ++NumFree;
  }

public:
  using value_type = ValueT;
  using reference = ValueT &;
  using const_reference = const ValueT &;
  using pointer = ValueT *;
  using const_pointer = const ValueT *;
  using size_type = unsigned;

  SparseMultiSet() = default;
  SparseMultiSet(const SparseMultiSet &) = delete;
  SparseMultiSet &operator=(const SparseMultiSet &) = delete;
  ~SparseMultiSet() { free(Sparse); }

  /// Set the universe size which determines the largest key the set can hold.
  /// The universe must be sized before any elements can be added.
  ///
  /// @param U Universe size. All object keys must be less than U.
  ///
  void setUniverse(unsigned U) {
    // It's not hard to resize the universe on a non-empty set, but it doesn't
    // seem like a likely use case, so we can add that code when we need it.
    assert(empty() && "Can only resize universe on an empty map");
    // Hysteresis prevents needless reallocations.
    if (U >= Universe/4 && U <= Universe)
      return;
    free(Sparse);
    // The Sparse array doesn't actually need to be initialized, so malloc
    // would be enough here, but that will cause tools like valgrind to
    // complain about branching on uninitialized data.
    Sparse = static_cast<SparseT*>(safe_calloc(U, sizeof(SparseT)));
    Universe = U;
  }

  /// Our iterators are iterators over the collection of objects that share a
  /// key.
  template<typename SMSPtrTy>
  class iterator_base : public std::iterator<std::bidirectional_iterator_tag,
                                             ValueT> {
    friend class SparseMultiSet;

    SMSPtrTy SMS;
    unsigned Idx;
    unsigned SparseIdx;

    iterator_base(SMSPtrTy P, unsigned I, unsigned SI)
      : SMS(P), Idx(I), SparseIdx(SI) {}

    /// Whether our iterator has fallen outside our dense vector.
    bool isEnd() const {
      if (Idx == SMSNode::INVALID)
        return true;

      assert(Idx < SMS->Dense.size() && "Out of range, non-INVALID Idx?");
      return false;
    }

    /// Whether our iterator is properly keyed, i.e. the SparseIdx is valid
    bool isKeyed() const { return SparseIdx < SMS->Universe; }

    unsigned Prev() const { return SMS->Dense[Idx].Prev; }
    unsigned Next() const { return SMS->Dense[Idx].Next; }

    void setPrev(unsigned P) { SMS->Dense[Idx].Prev = P; }
    void setNext(unsigned N) { SMS->Dense[Idx].Next = N; }

  public:
    using super = std::iterator<std::bidirectional_iterator_tag, ValueT>;
    using value_type = typename super::value_type;
    using difference_type = typename super::difference_type;
    using pointer = typename super::pointer;
    using reference = typename super::reference;

    reference operator*() const {
      assert(isKeyed() && SMS->sparseIndex(SMS->Dense[Idx].Data) == SparseIdx &&
             "Dereferencing iterator of invalid key or index");

      return SMS->Dense[Idx].Data;
    }
    pointer operator->() const { return &operator*(); }

    /// Comparison operators
    bool operator==(const iterator_base &RHS) const {
      // end compares equal
      if (SMS == RHS.SMS && Idx == RHS.Idx) {
        assert((isEnd() || SparseIdx == RHS.SparseIdx) &&
               "Same dense entry, but different keys?");
        return true;
      }

      return false;
    }

    bool operator!=(const iterator_base &RHS) const {
      return !operator==(RHS);
    }

    /// Increment and decrement operators
    iterator_base &operator--() { // predecrement - Back up
      assert(isKeyed() && "Decrementing an invalid iterator");
      assert((isEnd() || !SMS->isHead(SMS->Dense[Idx])) &&
             "Decrementing head of list");

      // If we're at the end, then issue a new find()
      if (isEnd())
        Idx = SMS->findIndex(SparseIdx).Prev();
      else
        Idx = Prev();

      return *this;
    }
    iterator_base &operator++() { // preincrement - Advance
      assert(!isEnd() && isKeyed() && "Incrementing an invalid/end iterator");
      Idx = Next();
      return *this;
    }
    iterator_base operator--(int) { // postdecrement
      iterator_base I(*this);
      --*this;
      return I;
    }
    iterator_base operator++(int) { // postincrement
      iterator_base I(*this);
      ++*this;
      return I;
    }
  };

  using iterator = iterator_base<SparseMultiSet *>;
  using const_iterator = iterator_base<const SparseMultiSet *>;

  // Convenience types
  using RangePair = std::pair<iterator, iterator>;

  /// Returns an iterator past this container. Note that such an iterator cannot
  /// be decremented, but will compare equal to other end iterators.
  iterator end() { return iterator(this, SMSNode::INVALID, SMSNode::INVALID); }
  const_iterator end() const {
    return const_iterator(this, SMSNode::INVALID, SMSNode::INVALID);
  }

  /// Returns true if the set is empty.
  ///
  /// This is not the same as BitVector::empty().
  ///
  bool empty() const { return size() == 0; }

  /// Returns the number of elements in the set.
  ///
  /// This is not the same as BitVector::size() which returns the size of the
  /// universe.
  ///
  size_type size() const {
    assert(NumFree <= Dense.size() && "Out-of-bounds free entries");
    return Dense.size() - NumFree;
  }

  /// Clears the set.  This is a very fast constant time operation.
  ///
  void clear() {
    // Sparse does not need to be cleared, see find().
    Dense.clear();
    NumFree = 0;
    FreelistIdx = SMSNode::INVALID;
  }

  /// Find an element by its index.
  ///
  /// @param   Idx A valid index to find.
  /// @returns An iterator to the element identified by key, or end().
  ///
  iterator findIndex(unsigned Idx) {
    assert(Idx < Universe && "Key out of range");
    const unsigned Stride = std::numeric_limits<SparseT>::max() + 1u;
    for (unsigned i = Sparse[Idx], e = Dense.size(); i < e; i += Stride) {
      const unsigned FoundIdx = sparseIndex(Dense[i]);
      // Check that we're pointing at the correct entry and that it is the head
      // of a valid list.
      if (Idx == FoundIdx && Dense[i].isValid() && isHead(Dense[i]))
        return iterator(this, i, Idx);
      // Stride is 0 when SparseT >= unsigned.  We don't need to loop.
      if (!Stride)
        break;
    }
    return end();
  }

  /// Find an element by its key.
  ///
  /// @param   Key A valid key to find.
  /// @returns An iterator to the element identified by key, or end().
  ///
  iterator find(const KeyT &Key) {
    return findIndex(KeyIndexOf(Key));
  }

  const_iterator find(const KeyT &Key) const {
    iterator I = const_cast<SparseMultiSet*>(this)->findIndex(KeyIndexOf(Key));
    return const_iterator(I.SMS, I.Idx, KeyIndexOf(Key));
  }

  /// Returns the number of elements identified by Key. This will be linear in
  /// the number of elements of that key.
  size_type count(const KeyT &Key) const {
    unsigned Ret = 0;
    for (const_iterator It = find(Key); It != end(); ++It)
      ++Ret;

    return Ret;
  }

  /// Returns true if this set contains an element identified by Key.
  bool contains(const KeyT &Key) const {
    return find(Key) != end();
  }

  /// Return the head and tail of the subset's list, otherwise returns end().
  iterator getHead(const KeyT &Key) { return find(Key); }
  iterator getTail(const KeyT &Key) {
    iterator I = find(Key);
    if (I != end())
      I = iterator(this, I.Prev(), KeyIndexOf(Key));
    return I;
  }

  /// The bounds of the range of items sharing Key K. First member is the head
  /// of the list, and the second member is a decrementable end iterator for
  /// that key.
  RangePair equal_range(const KeyT &K) {
    iterator B = find(K);
    iterator E = iterator(this, SMSNode::INVALID, B.SparseIdx);
    return make_pair(B, E);
  }

  /// Insert a new element at the tail of the subset list. Returns an iterator
  /// to the newly added entry.
  iterator insert(const ValueT &Val) {
    unsigned Idx = sparseIndex(Val);
    iterator I = findIndex(Idx);

    unsigned NodeIdx = addValue(Val, SMSNode::INVALID, SMSNode::INVALID);

    if (I == end()) {
      // Make a singleton list
      Sparse[Idx] = NodeIdx;
      Dense[NodeIdx].Prev = NodeIdx;
      return iterator(this, NodeIdx, Idx);
    }

    // Stick it at the end.
    unsigned HeadIdx = I.Idx;
    unsigned TailIdx = I.Prev();
    Dense[TailIdx].Next = NodeIdx;
    Dense[HeadIdx].Prev = NodeIdx;
    Dense[NodeIdx].Prev = TailIdx;

    return iterator(this, NodeIdx, Idx);
  }

  /// Erases an existing element identified by a valid iterator.
  ///
  /// This invalidates iterators pointing at the same entry, but erase() returns
  /// an iterator pointing to the next element in the subset's list. This makes
  /// it possible to erase selected elements while iterating over the subset:
  ///
  ///   tie(I, E) = Set.equal_range(Key);
  ///   while (I != E)
  ///     if (test(*I))
  ///       I = Set.erase(I);
  ///     else
  ///       ++I;
  ///
  /// Note that if the last element in the subset list is erased, this will
  /// return an end iterator which can be decremented to get the new tail (if it
  /// exists):
  ///
  ///  tie(B, I) = Set.equal_range(Key);
  ///  for (bool isBegin = B == I; !isBegin; /* empty */) {
  ///    isBegin = (--I) == B;
  ///    if (test(I))
  ///      break;
  ///    I = erase(I);
  ///  }
  iterator erase(iterator I) {
    assert(I.isKeyed() && !I.isEnd() && !Dense[I.Idx].isTombstone() &&
           "erasing invalid/end/tombstone iterator");

    // First, unlink the node from its list. Then swap the node out with the
    // dense vector's last entry
    iterator NextI = unlink(Dense[I.Idx]);

    // Put in a tombstone.
    makeTombstone(I.Idx);

    return NextI;
  }

  /// Erase all elements with the given key. This invalidates all
  /// iterators of that key.
  void eraseAll(const KeyT &K) {
    for (iterator I = find(K); I != end(); /* empty */)
      I = erase(I);
  }

private:
  /// Unlink the node from its list. Returns the next node in the list.
  iterator unlink(const SMSNode &N) {
    if (isSingleton(N)) {
      // Singleton is already unlinked
      assert(N.Next == SMSNode::INVALID && "Singleton has next?");
      return iterator(this, SMSNode::INVALID, ValIndexOf(N.Data));
    }

    if (isHead(N)) {
      // If we're the head, then update the sparse array and our next.
      Sparse[sparseIndex(N)] = N.Next;
      Dense[N.Next].Prev = N.Prev;
      return iterator(this, N.Next, ValIndexOf(N.Data));
    }

    if (N.isTail()) {
      // If we're the tail, then update our head and our previous.
      findIndex(sparseIndex(N)).setPrev(N.Prev);
      Dense[N.Prev].Next = N.Next;

      // Give back an end iterator that can be decremented
      iterator I(this, N.Prev, ValIndexOf(N.Data));
      return ++I;
    }

    // Otherwise, just drop us
    Dense[N.Next].Prev = N.Prev;
    Dense[N.Prev].Next = N.Next;
    return iterator(this, N.Next, ValIndexOf(N.Data));
  }
};

} // end namespace llvm

#endif // LLVM_ADT_SPARSEMULTISET_H
