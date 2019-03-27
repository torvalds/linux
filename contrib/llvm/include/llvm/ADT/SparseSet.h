//===- llvm/ADT/SparseSet.h - Sparse set ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the SparseSet class derived from the version described in
// Briggs, Torczon, "An efficient representation for sparse sets", ACM Letters
// on Programming Languages and Systems, Volume 2 Issue 1-4, March-Dec.  1993.
//
// A sparse set holds a small number of objects identified by integer keys from
// a moderately sized universe. The sparse set uses more memory than other
// containers in order to provide faster operations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SPARSESET_H
#define LLVM_ADT_SPARSESET_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Allocator.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <utility>

namespace llvm {

/// SparseSetValTraits - Objects in a SparseSet are identified by keys that can
/// be uniquely converted to a small integer less than the set's universe. This
/// class allows the set to hold values that differ from the set's key type as
/// long as an index can still be derived from the value. SparseSet never
/// directly compares ValueT, only their indices, so it can map keys to
/// arbitrary values. SparseSetValTraits computes the index from the value
/// object. To compute the index from a key, SparseSet uses a separate
/// KeyFunctorT template argument.
///
/// A simple type declaration, SparseSet<Type>, handles these cases:
/// - unsigned key, identity index, identity value
/// - unsigned key, identity index, fat value providing getSparseSetIndex()
///
/// The type declaration SparseSet<Type, UnaryFunction> handles:
/// - unsigned key, remapped index, identity value (virtual registers)
/// - pointer key, pointer-derived index, identity value (node+ID)
/// - pointer key, pointer-derived index, fat value with getSparseSetIndex()
///
/// Only other, unexpected cases require specializing SparseSetValTraits.
///
/// For best results, ValueT should not require a destructor.
///
template<typename ValueT>
struct SparseSetValTraits {
  static unsigned getValIndex(const ValueT &Val) {
    return Val.getSparseSetIndex();
  }
};

/// SparseSetValFunctor - Helper class for selecting SparseSetValTraits. The
/// generic implementation handles ValueT classes which either provide
/// getSparseSetIndex() or specialize SparseSetValTraits<>.
///
template<typename KeyT, typename ValueT, typename KeyFunctorT>
struct SparseSetValFunctor {
  unsigned operator()(const ValueT &Val) const {
    return SparseSetValTraits<ValueT>::getValIndex(Val);
  }
};

/// SparseSetValFunctor<KeyT, KeyT> - Helper class for the common case of
/// identity key/value sets.
template<typename KeyT, typename KeyFunctorT>
struct SparseSetValFunctor<KeyT, KeyT, KeyFunctorT> {
  unsigned operator()(const KeyT &Key) const {
    return KeyFunctorT()(Key);
  }
};

/// SparseSet - Fast set implmentation for objects that can be identified by
/// small unsigned keys.
///
/// SparseSet allocates memory proportional to the size of the key universe, so
/// it is not recommended for building composite data structures.  It is useful
/// for algorithms that require a single set with fast operations.
///
/// Compared to DenseSet and DenseMap, SparseSet provides constant-time fast
/// clear() and iteration as fast as a vector.  The find(), insert(), and
/// erase() operations are all constant time, and typically faster than a hash
/// table.  The iteration order doesn't depend on numerical key values, it only
/// depends on the order of insert() and erase() operations.  When no elements
/// have been erased, the iteration order is the insertion order.
///
/// Compared to BitVector, SparseSet<unsigned> uses 8x-40x more memory, but
/// offers constant-time clear() and size() operations as well as fast
/// iteration independent on the size of the universe.
///
/// SparseSet contains a dense vector holding all the objects and a sparse
/// array holding indexes into the dense vector.  Most of the memory is used by
/// the sparse array which is the size of the key universe.  The SparseT
/// template parameter provides a space/speed tradeoff for sets holding many
/// elements.
///
/// When SparseT is uint32_t, find() only touches 2 cache lines, but the sparse
/// array uses 4 x Universe bytes.
///
/// When SparseT is uint8_t (the default), find() touches up to 2+[N/256] cache
/// lines, but the sparse array is 4x smaller.  N is the number of elements in
/// the set.
///
/// For sets that may grow to thousands of elements, SparseT should be set to
/// uint16_t or uint32_t.
///
/// @tparam ValueT      The type of objects in the set.
/// @tparam KeyFunctorT A functor that computes an unsigned index from KeyT.
/// @tparam SparseT     An unsigned integer type. See above.
///
template<typename ValueT,
         typename KeyFunctorT = identity<unsigned>,
         typename SparseT = uint8_t>
class SparseSet {
  static_assert(std::numeric_limits<SparseT>::is_integer &&
                !std::numeric_limits<SparseT>::is_signed,
                "SparseT must be an unsigned integer type");

  using KeyT = typename KeyFunctorT::argument_type;
  using DenseT = SmallVector<ValueT, 8>;
  using size_type = unsigned;
  DenseT Dense;
  SparseT *Sparse = nullptr;
  unsigned Universe = 0;
  KeyFunctorT KeyIndexOf;
  SparseSetValFunctor<KeyT, ValueT, KeyFunctorT> ValIndexOf;

public:
  using value_type = ValueT;
  using reference = ValueT &;
  using const_reference = const ValueT &;
  using pointer = ValueT *;
  using const_pointer = const ValueT *;

  SparseSet() = default;
  SparseSet(const SparseSet &) = delete;
  SparseSet &operator=(const SparseSet &) = delete;
  ~SparseSet() { free(Sparse); }

  /// setUniverse - Set the universe size which determines the largest key the
  /// set can hold.  The universe must be sized before any elements can be
  /// added.
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

  // Import trivial vector stuff from DenseT.
  using iterator = typename DenseT::iterator;
  using const_iterator = typename DenseT::const_iterator;

  const_iterator begin() const { return Dense.begin(); }
  const_iterator end() const { return Dense.end(); }
  iterator begin() { return Dense.begin(); }
  iterator end() { return Dense.end(); }

  /// empty - Returns true if the set is empty.
  ///
  /// This is not the same as BitVector::empty().
  ///
  bool empty() const { return Dense.empty(); }

  /// size - Returns the number of elements in the set.
  ///
  /// This is not the same as BitVector::size() which returns the size of the
  /// universe.
  ///
  size_type size() const { return Dense.size(); }

  /// clear - Clears the set.  This is a very fast constant time operation.
  ///
  void clear() {
    // Sparse does not need to be cleared, see find().
    Dense.clear();
  }

  /// findIndex - Find an element by its index.
  ///
  /// @param   Idx A valid index to find.
  /// @returns An iterator to the element identified by key, or end().
  ///
  iterator findIndex(unsigned Idx) {
    assert(Idx < Universe && "Key out of range");
    const unsigned Stride = std::numeric_limits<SparseT>::max() + 1u;
    for (unsigned i = Sparse[Idx], e = size(); i < e; i += Stride) {
      const unsigned FoundIdx = ValIndexOf(Dense[i]);
      assert(FoundIdx < Universe && "Invalid key in set. Did object mutate?");
      if (Idx == FoundIdx)
        return begin() + i;
      // Stride is 0 when SparseT >= unsigned.  We don't need to loop.
      if (!Stride)
        break;
    }
    return end();
  }

  /// find - Find an element by its key.
  ///
  /// @param   Key A valid key to find.
  /// @returns An iterator to the element identified by key, or end().
  ///
  iterator find(const KeyT &Key) {
    return findIndex(KeyIndexOf(Key));
  }

  const_iterator find(const KeyT &Key) const {
    return const_cast<SparseSet*>(this)->findIndex(KeyIndexOf(Key));
  }

  /// count - Returns 1 if this set contains an element identified by Key,
  /// 0 otherwise.
  ///
  size_type count(const KeyT &Key) const {
    return find(Key) == end() ? 0 : 1;
  }

  /// insert - Attempts to insert a new element.
  ///
  /// If Val is successfully inserted, return (I, true), where I is an iterator
  /// pointing to the newly inserted element.
  ///
  /// If the set already contains an element with the same key as Val, return
  /// (I, false), where I is an iterator pointing to the existing element.
  ///
  /// Insertion invalidates all iterators.
  ///
  std::pair<iterator, bool> insert(const ValueT &Val) {
    unsigned Idx = ValIndexOf(Val);
    iterator I = findIndex(Idx);
    if (I != end())
      return std::make_pair(I, false);
    Sparse[Idx] = size();
    Dense.push_back(Val);
    return std::make_pair(end() - 1, true);
  }

  /// array subscript - If an element already exists with this key, return it.
  /// Otherwise, automatically construct a new value from Key, insert it,
  /// and return the newly inserted element.
  ValueT &operator[](const KeyT &Key) {
    return *insert(ValueT(Key)).first;
  }

  ValueT pop_back_val() {
    // Sparse does not need to be cleared, see find().
    return Dense.pop_back_val();
  }

  /// erase - Erases an existing element identified by a valid iterator.
  ///
  /// This invalidates all iterators, but erase() returns an iterator pointing
  /// to the next element.  This makes it possible to erase selected elements
  /// while iterating over the set:
  ///
  ///   for (SparseSet::iterator I = Set.begin(); I != Set.end();)
  ///     if (test(*I))
  ///       I = Set.erase(I);
  ///     else
  ///       ++I;
  ///
  /// Note that end() changes when elements are erased, unlike std::list.
  ///
  iterator erase(iterator I) {
    assert(unsigned(I - begin()) < size() && "Invalid iterator");
    if (I != end() - 1) {
      *I = Dense.back();
      unsigned BackIdx = ValIndexOf(Dense.back());
      assert(BackIdx < Universe && "Invalid key in set. Did object mutate?");
      Sparse[BackIdx] = I - begin();
    }
    // This depends on SmallVector::pop_back() not invalidating iterators.
    // std::vector::pop_back() doesn't give that guarantee.
    Dense.pop_back();
    return I;
  }

  /// erase - Erases an element identified by Key, if it exists.
  ///
  /// @param   Key The key identifying the element to erase.
  /// @returns True when an element was erased, false if no element was found.
  ///
  bool erase(const KeyT &Key) {
    iterator I = find(Key);
    if (I == end())
      return false;
    erase(I);
    return true;
  }
};

} // end namespace llvm

#endif // LLVM_ADT_SPARSESET_H
