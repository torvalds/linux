//===- Sequence.h - Utility for producing sequences of values ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This routine provides some synthesis utilities to produce sequences of
/// values. The names are intentionally kept very short as they tend to occur
/// in common and widely used contexts.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SEQUENCE_H
#define LLVM_ADT_SEQUENCE_H

#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include <algorithm>
#include <iterator>
#include <utility>

namespace llvm {

namespace detail {

template <typename ValueT>
class value_sequence_iterator
    : public iterator_facade_base<value_sequence_iterator<ValueT>,
                                  std::random_access_iterator_tag,
                                  const ValueT> {
  using BaseT = typename value_sequence_iterator::iterator_facade_base;

  ValueT Value;

public:
  using difference_type = typename BaseT::difference_type;
  using reference = typename BaseT::reference;

  value_sequence_iterator() = default;
  value_sequence_iterator(const value_sequence_iterator &) = default;
  value_sequence_iterator(value_sequence_iterator &&Arg)
      : Value(std::move(Arg.Value)) {}

  template <typename U, typename Enabler = decltype(ValueT(std::declval<U>()))>
  value_sequence_iterator(U &&Value) : Value(std::forward<U>(Value)) {}

  value_sequence_iterator &operator+=(difference_type N) {
    Value += N;
    return *this;
  }
  value_sequence_iterator &operator-=(difference_type N) {
    Value -= N;
    return *this;
  }
  using BaseT::operator-;
  difference_type operator-(const value_sequence_iterator &RHS) const {
    return Value - RHS.Value;
  }

  bool operator==(const value_sequence_iterator &RHS) const {
    return Value == RHS.Value;
  }
  bool operator<(const value_sequence_iterator &RHS) const {
    return Value < RHS.Value;
  }

  reference operator*() const { return Value; }
};

} // end namespace detail

template <typename ValueT>
iterator_range<detail::value_sequence_iterator<ValueT>> seq(ValueT Begin,
                                                            ValueT End) {
  return make_range(detail::value_sequence_iterator<ValueT>(Begin),
                    detail::value_sequence_iterator<ValueT>(End));
}

} // end namespace llvm

#endif // LLVM_ADT_SEQUENCE_H
