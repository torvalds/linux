//===--- Random.h - Utilities for random sampling -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Utilities for random sampling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZMUTATE_RANDOM_H
#define LLVM_FUZZMUTATE_RANDOM_H

#include <random>
#include "llvm/Support/raw_ostream.h"
namespace llvm {

/// Return a uniformly distributed random value between \c Min and \c Max
template <typename T, typename GenT> T uniform(GenT &Gen, T Min, T Max) {
  return std::uniform_int_distribution<T>(Min, Max)(Gen);
}

/// Return a uniformly distributed random value of type \c T
template <typename T, typename GenT> T uniform(GenT &Gen) {
  return uniform<T>(Gen, std::numeric_limits<T>::min(),
                    std::numeric_limits<T>::max());
}

/// Randomly selects an item by sampling into a set with an unknown number of
/// elements, which may each be weighted to be more likely choices.
template <typename T, typename GenT> class ReservoirSampler {
  GenT &RandGen;
  typename std::remove_const<T>::type Selection = {};
  uint64_t TotalWeight = 0;

public:
  ReservoirSampler(GenT &RandGen) : RandGen(RandGen) {}

  uint64_t totalWeight() const { return TotalWeight; }
  bool isEmpty() const { return TotalWeight == 0; }

  const T &getSelection() const {
    assert(!isEmpty() && "Nothing selected");
    return Selection;
  }

  explicit operator bool() const { return !isEmpty();}
  const T &operator*() const { return getSelection(); }

  /// Sample each item in \c Items with unit weight
  template <typename RangeT> ReservoirSampler &sample(RangeT &&Items) {
    for (auto &I : Items)
      sample(I, 1);
    return *this;
  }

  /// Sample a single item with the given weight.
  ReservoirSampler &sample(const T &Item, uint64_t Weight) {
    if (!Weight)
      // If the weight is zero, do nothing.
      return *this;
    TotalWeight += Weight;
    // Consider switching from the current element to this one.
    if (uniform<uint64_t>(RandGen, 1, TotalWeight) <= Weight)
      Selection = Item;
    return *this;
  }
};

template <typename GenT, typename RangeT,
          typename ElT = typename std::remove_reference<
              decltype(*std::begin(std::declval<RangeT>()))>::type>
ReservoirSampler<ElT, GenT> makeSampler(GenT &RandGen, RangeT &&Items) {
  ReservoirSampler<ElT, GenT> RS(RandGen);
  RS.sample(Items);
  return RS;
}

template <typename GenT, typename T>
ReservoirSampler<T, GenT> makeSampler(GenT &RandGen, const T &Item,
                                      uint64_t Weight) {
  ReservoirSampler<T, GenT> RS(RandGen);
  RS.sample(Item, Weight);
  return RS;
}

template <typename T, typename GenT>
ReservoirSampler<T, GenT> makeSampler(GenT &RandGen) {
  return ReservoirSampler<T, GenT>(RandGen);
}

} // End llvm namespace

#endif // LLVM_FUZZMUTATE_RANDOM_H
