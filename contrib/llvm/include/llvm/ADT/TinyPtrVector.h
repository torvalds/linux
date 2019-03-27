//===- llvm/ADT/TinyPtrVector.h - 'Normally tiny' vectors -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_TINYPTRVECTOR_H
#define LLVM_ADT_TINYPTRVECTOR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <type_traits>

namespace llvm {

/// TinyPtrVector - This class is specialized for cases where there are
/// normally 0 or 1 element in a vector, but is general enough to go beyond that
/// when required.
///
/// NOTE: This container doesn't allow you to store a null pointer into it.
///
template <typename EltTy>
class TinyPtrVector {
public:
  using VecTy = SmallVector<EltTy, 4>;
  using value_type = typename VecTy::value_type;
  using PtrUnion = PointerUnion<EltTy, VecTy *>;

private:
  PtrUnion Val;

public:
  TinyPtrVector() = default;

  ~TinyPtrVector() {
    if (VecTy *V = Val.template dyn_cast<VecTy*>())
      delete V;
  }

  TinyPtrVector(const TinyPtrVector &RHS) : Val(RHS.Val) {
    if (VecTy *V = Val.template dyn_cast<VecTy*>())
      Val = new VecTy(*V);
  }

  TinyPtrVector &operator=(const TinyPtrVector &RHS) {
    if (this == &RHS)
      return *this;
    if (RHS.empty()) {
      this->clear();
      return *this;
    }

    // Try to squeeze into the single slot. If it won't fit, allocate a copied
    // vector.
    if (Val.template is<EltTy>()) {
      if (RHS.size() == 1)
        Val = RHS.front();
      else
        Val = new VecTy(*RHS.Val.template get<VecTy*>());
      return *this;
    }

    // If we have a full vector allocated, try to re-use it.
    if (RHS.Val.template is<EltTy>()) {
      Val.template get<VecTy*>()->clear();
      Val.template get<VecTy*>()->push_back(RHS.front());
    } else {
      *Val.template get<VecTy*>() = *RHS.Val.template get<VecTy*>();
    }
    return *this;
  }

  TinyPtrVector(TinyPtrVector &&RHS) : Val(RHS.Val) {
    RHS.Val = (EltTy)nullptr;
  }

  TinyPtrVector &operator=(TinyPtrVector &&RHS) {
    if (this == &RHS)
      return *this;
    if (RHS.empty()) {
      this->clear();
      return *this;
    }

    // If this vector has been allocated on the heap, re-use it if cheap. If it
    // would require more copying, just delete it and we'll steal the other
    // side.
    if (VecTy *V = Val.template dyn_cast<VecTy*>()) {
      if (RHS.Val.template is<EltTy>()) {
        V->clear();
        V->push_back(RHS.front());
        RHS.Val = (EltTy)nullptr;
        return *this;
      }
      delete V;
    }

    Val = RHS.Val;
    RHS.Val = (EltTy)nullptr;
    return *this;
  }

  TinyPtrVector(std::initializer_list<EltTy> IL)
      : Val(IL.size() == 0
                ? PtrUnion()
                : IL.size() == 1 ? PtrUnion(*IL.begin())
                                 : PtrUnion(new VecTy(IL.begin(), IL.end()))) {}

  /// Constructor from an ArrayRef.
  ///
  /// This also is a constructor for individual array elements due to the single
  /// element constructor for ArrayRef.
  explicit TinyPtrVector(ArrayRef<EltTy> Elts)
      : Val(Elts.empty()
                ? PtrUnion()
                : Elts.size() == 1
                      ? PtrUnion(Elts[0])
                      : PtrUnion(new VecTy(Elts.begin(), Elts.end()))) {}

  TinyPtrVector(size_t Count, EltTy Value)
      : Val(Count == 0 ? PtrUnion()
                       : Count == 1 ? PtrUnion(Value)
                                    : PtrUnion(new VecTy(Count, Value))) {}

  // implicit conversion operator to ArrayRef.
  operator ArrayRef<EltTy>() const {
    if (Val.isNull())
      return None;
    if (Val.template is<EltTy>())
      return *Val.getAddrOfPtr1();
    return *Val.template get<VecTy*>();
  }

  // implicit conversion operator to MutableArrayRef.
  operator MutableArrayRef<EltTy>() {
    if (Val.isNull())
      return None;
    if (Val.template is<EltTy>())
      return *Val.getAddrOfPtr1();
    return *Val.template get<VecTy*>();
  }

  // Implicit conversion to ArrayRef<U> if EltTy* implicitly converts to U*.
  template<typename U,
           typename std::enable_if<
               std::is_convertible<ArrayRef<EltTy>, ArrayRef<U>>::value,
               bool>::type = false>
  operator ArrayRef<U>() const {
    return operator ArrayRef<EltTy>();
  }

  bool empty() const {
    // This vector can be empty if it contains no element, or if it
    // contains a pointer to an empty vector.
    if (Val.isNull()) return true;
    if (VecTy *Vec = Val.template dyn_cast<VecTy*>())
      return Vec->empty();
    return false;
  }

  unsigned size() const {
    if (empty())
      return 0;
    if (Val.template is<EltTy>())
      return 1;
    return Val.template get<VecTy*>()->size();
  }

  using iterator = EltTy *;
  using const_iterator = const EltTy *;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  iterator begin() {
    if (Val.template is<EltTy>())
      return Val.getAddrOfPtr1();

    return Val.template get<VecTy *>()->begin();
  }

  iterator end() {
    if (Val.template is<EltTy>())
      return begin() + (Val.isNull() ? 0 : 1);

    return Val.template get<VecTy *>()->end();
  }

  const_iterator begin() const {
    return (const_iterator)const_cast<TinyPtrVector*>(this)->begin();
  }

  const_iterator end() const {
    return (const_iterator)const_cast<TinyPtrVector*>(this)->end();
  }

  reverse_iterator rbegin() { return reverse_iterator(end()); }
  reverse_iterator rend() { return reverse_iterator(begin()); }

  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(end());
  }

  const_reverse_iterator rend() const {
    return const_reverse_iterator(begin());
  }

  EltTy operator[](unsigned i) const {
    assert(!Val.isNull() && "can't index into an empty vector");
    if (EltTy V = Val.template dyn_cast<EltTy>()) {
      assert(i == 0 && "tinyvector index out of range");
      return V;
    }

    assert(i < Val.template get<VecTy*>()->size() &&
           "tinyvector index out of range");
    return (*Val.template get<VecTy*>())[i];
  }

  EltTy front() const {
    assert(!empty() && "vector empty");
    if (EltTy V = Val.template dyn_cast<EltTy>())
      return V;
    return Val.template get<VecTy*>()->front();
  }

  EltTy back() const {
    assert(!empty() && "vector empty");
    if (EltTy V = Val.template dyn_cast<EltTy>())
      return V;
    return Val.template get<VecTy*>()->back();
  }

  void push_back(EltTy NewVal) {
    assert(NewVal && "Can't add a null value");

    // If we have nothing, add something.
    if (Val.isNull()) {
      Val = NewVal;
      return;
    }

    // If we have a single value, convert to a vector.
    if (EltTy V = Val.template dyn_cast<EltTy>()) {
      Val = new VecTy();
      Val.template get<VecTy*>()->push_back(V);
    }

    // Add the new value, we know we have a vector.
    Val.template get<VecTy*>()->push_back(NewVal);
  }

  void pop_back() {
    // If we have a single value, convert to empty.
    if (Val.template is<EltTy>())
      Val = (EltTy)nullptr;
    else if (VecTy *Vec = Val.template get<VecTy*>())
      Vec->pop_back();
  }

  void clear() {
    // If we have a single value, convert to empty.
    if (Val.template is<EltTy>()) {
      Val = (EltTy)nullptr;
    } else if (VecTy *Vec = Val.template dyn_cast<VecTy*>()) {
      // If we have a vector form, just clear it.
      Vec->clear();
    }
    // Otherwise, we're already empty.
  }

  iterator erase(iterator I) {
    assert(I >= begin() && "Iterator to erase is out of bounds.");
    assert(I < end() && "Erasing at past-the-end iterator.");

    // If we have a single value, convert to empty.
    if (Val.template is<EltTy>()) {
      if (I == begin())
        Val = (EltTy)nullptr;
    } else if (VecTy *Vec = Val.template dyn_cast<VecTy*>()) {
      // multiple items in a vector; just do the erase, there is no
      // benefit to collapsing back to a pointer
      return Vec->erase(I);
    }
    return end();
  }

  iterator erase(iterator S, iterator E) {
    assert(S >= begin() && "Range to erase is out of bounds.");
    assert(S <= E && "Trying to erase invalid range.");
    assert(E <= end() && "Trying to erase past the end.");

    if (Val.template is<EltTy>()) {
      if (S == begin() && S != E)
        Val = (EltTy)nullptr;
    } else if (VecTy *Vec = Val.template dyn_cast<VecTy*>()) {
      return Vec->erase(S, E);
    }
    return end();
  }

  iterator insert(iterator I, const EltTy &Elt) {
    assert(I >= this->begin() && "Insertion iterator is out of bounds.");
    assert(I <= this->end() && "Inserting past the end of the vector.");
    if (I == end()) {
      push_back(Elt);
      return std::prev(end());
    }
    assert(!Val.isNull() && "Null value with non-end insert iterator.");
    if (EltTy V = Val.template dyn_cast<EltTy>()) {
      assert(I == begin());
      Val = Elt;
      push_back(V);
      return begin();
    }

    return Val.template get<VecTy*>()->insert(I, Elt);
  }

  template<typename ItTy>
  iterator insert(iterator I, ItTy From, ItTy To) {
    assert(I >= this->begin() && "Insertion iterator is out of bounds.");
    assert(I <= this->end() && "Inserting past the end of the vector.");
    if (From == To)
      return I;

    // If we have a single value, convert to a vector.
    ptrdiff_t Offset = I - begin();
    if (Val.isNull()) {
      if (std::next(From) == To) {
        Val = *From;
        return begin();
      }

      Val = new VecTy();
    } else if (EltTy V = Val.template dyn_cast<EltTy>()) {
      Val = new VecTy();
      Val.template get<VecTy*>()->push_back(V);
    }
    return Val.template get<VecTy*>()->insert(begin() + Offset, From, To);
  }
};

} // end namespace llvm

#endif // LLVM_ADT_TINYPTRVECTOR_H
