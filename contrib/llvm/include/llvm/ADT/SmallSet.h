//===- llvm/ADT/SmallSet.h - 'Normally small' sets --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the SmallSet class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SMALLSET_H
#define LLVM_ADT_SMALLSET_H

#include "llvm/ADT/None.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/type_traits.h"
#include <cstddef>
#include <functional>
#include <set>
#include <type_traits>
#include <utility>

namespace llvm {

/// SmallSetIterator - This class implements a const_iterator for SmallSet by
/// delegating to the underlying SmallVector or Set iterators.
template <typename T, unsigned N, typename C>
class SmallSetIterator
    : public iterator_facade_base<SmallSetIterator<T, N, C>,
                                  std::forward_iterator_tag, T> {
private:
  using SetIterTy = typename std::set<T, C>::const_iterator;
  using VecIterTy = typename SmallVector<T, N>::const_iterator;
  using SelfTy = SmallSetIterator<T, N, C>;

  /// Iterators to the parts of the SmallSet containing the data. They are set
  /// depending on isSmall.
  union {
    SetIterTy SetIter;
    VecIterTy VecIter;
  };

  bool isSmall;

public:
  SmallSetIterator(SetIterTy SetIter) : SetIter(SetIter), isSmall(false) {}

  SmallSetIterator(VecIterTy VecIter) : VecIter(VecIter), isSmall(true) {}

  // Spell out destructor, copy/move constructor and assignment operators for
  // MSVC STL, where set<T>::const_iterator is not trivially copy constructible.
  ~SmallSetIterator() {
    if (isSmall)
      VecIter.~VecIterTy();
    else
      SetIter.~SetIterTy();
  }

  SmallSetIterator(const SmallSetIterator &Other) : isSmall(Other.isSmall) {
    if (isSmall)
      VecIter = Other.VecIter;
    else
      // Use placement new, to make sure SetIter is properly constructed, even
      // if it is not trivially copy-able (e.g. in MSVC).
      new (&SetIter) SetIterTy(Other.SetIter);
  }

  SmallSetIterator(SmallSetIterator &&Other) : isSmall(Other.isSmall) {
    if (isSmall)
      VecIter = std::move(Other.VecIter);
    else
      // Use placement new, to make sure SetIter is properly constructed, even
      // if it is not trivially copy-able (e.g. in MSVC).
      new (&SetIter) SetIterTy(std::move(Other.SetIter));
  }

  SmallSetIterator& operator=(const SmallSetIterator& Other) {
    // Call destructor for SetIter, so it gets properly destroyed if it is
    // not trivially destructible in case we are setting VecIter.
    if (!isSmall)
      SetIter.~SetIterTy();

    isSmall = Other.isSmall;
    if (isSmall)
      VecIter = Other.VecIter;
    else
      new (&SetIter) SetIterTy(Other.SetIter);
    return *this;
  }

  SmallSetIterator& operator=(SmallSetIterator&& Other) {
    // Call destructor for SetIter, so it gets properly destroyed if it is
    // not trivially destructible in case we are setting VecIter.
    if (!isSmall)
      SetIter.~SetIterTy();

    isSmall = Other.isSmall;
    if (isSmall)
      VecIter = std::move(Other.VecIter);
    else
      new (&SetIter) SetIterTy(std::move(Other.SetIter));
    return *this;
  }

  bool operator==(const SmallSetIterator &RHS) const {
    if (isSmall != RHS.isSmall)
      return false;
    if (isSmall)
      return VecIter == RHS.VecIter;
    return SetIter == RHS.SetIter;
  }

  SmallSetIterator &operator++() { // Preincrement
    if (isSmall)
      VecIter++;
    else
      SetIter++;
    return *this;
  }

  const T &operator*() const { return isSmall ? *VecIter : *SetIter; }
};

/// SmallSet - This maintains a set of unique values, optimizing for the case
/// when the set is small (less than N).  In this case, the set can be
/// maintained with no mallocs.  If the set gets large, we expand to using an
/// std::set to maintain reasonable lookup times.
template <typename T, unsigned N, typename C = std::less<T>>
class SmallSet {
  /// Use a SmallVector to hold the elements here (even though it will never
  /// reach its 'large' stage) to avoid calling the default ctors of elements
  /// we will never use.
  SmallVector<T, N> Vector;
  std::set<T, C> Set;

  using VIterator = typename SmallVector<T, N>::const_iterator;
  using mutable_iterator = typename SmallVector<T, N>::iterator;

  // In small mode SmallPtrSet uses linear search for the elements, so it is
  // not a good idea to choose this value too high. You may consider using a
  // DenseSet<> instead if you expect many elements in the set.
  static_assert(N <= 32, "N should be small");

public:
  using size_type = size_t;
  using const_iterator = SmallSetIterator<T, N, C>;

  SmallSet() = default;

  LLVM_NODISCARD bool empty() const {
    return Vector.empty() && Set.empty();
  }

  size_type size() const {
    return isSmall() ? Vector.size() : Set.size();
  }

  /// count - Return 1 if the element is in the set, 0 otherwise.
  size_type count(const T &V) const {
    if (isSmall()) {
      // Since the collection is small, just do a linear search.
      return vfind(V) == Vector.end() ? 0 : 1;
    } else {
      return Set.count(V);
    }
  }

  /// insert - Insert an element into the set if it isn't already there.
  /// Returns true if the element is inserted (it was not in the set before).
  /// The first value of the returned pair is unused and provided for
  /// partial compatibility with the standard library self-associative container
  /// concept.
  // FIXME: Add iterators that abstract over the small and large form, and then
  // return those here.
  std::pair<NoneType, bool> insert(const T &V) {
    if (!isSmall())
      return std::make_pair(None, Set.insert(V).second);

    VIterator I = vfind(V);
    if (I != Vector.end())    // Don't reinsert if it already exists.
      return std::make_pair(None, false);
    if (Vector.size() < N) {
      Vector.push_back(V);
      return std::make_pair(None, true);
    }

    // Otherwise, grow from vector to set.
    while (!Vector.empty()) {
      Set.insert(Vector.back());
      Vector.pop_back();
    }
    Set.insert(V);
    return std::make_pair(None, true);
  }

  template <typename IterT>
  void insert(IterT I, IterT E) {
    for (; I != E; ++I)
      insert(*I);
  }

  bool erase(const T &V) {
    if (!isSmall())
      return Set.erase(V);
    for (mutable_iterator I = Vector.begin(), E = Vector.end(); I != E; ++I)
      if (*I == V) {
        Vector.erase(I);
        return true;
      }
    return false;
  }

  void clear() {
    Vector.clear();
    Set.clear();
  }

  const_iterator begin() const {
    if (isSmall())
      return {Vector.begin()};
    return {Set.begin()};
  }

  const_iterator end() const {
    if (isSmall())
      return {Vector.end()};
    return {Set.end()};
  }

private:
  bool isSmall() const { return Set.empty(); }

  VIterator vfind(const T &V) const {
    for (VIterator I = Vector.begin(), E = Vector.end(); I != E; ++I)
      if (*I == V)
        return I;
    return Vector.end();
  }
};

/// If this set is of pointer values, transparently switch over to using
/// SmallPtrSet for performance.
template <typename PointeeType, unsigned N>
class SmallSet<PointeeType*, N> : public SmallPtrSet<PointeeType*, N> {};

} // end namespace llvm

#endif // LLVM_ADT_SMALLSET_H
