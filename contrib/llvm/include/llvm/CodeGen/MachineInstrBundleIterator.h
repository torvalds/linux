//===- llvm/CodeGen/MachineInstrBundleIterator.h ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines an iterator class that bundles MachineInstr.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEINSTRBUNDLEITERATOR_H
#define LLVM_CODEGEN_MACHINEINSTRBUNDLEITERATOR_H

#include "llvm/ADT/ilist.h"
#include "llvm/ADT/simple_ilist.h"
#include <cassert>
#include <iterator>
#include <type_traits>

namespace llvm {

template <class T, bool IsReverse> struct MachineInstrBundleIteratorTraits;
template <class T> struct MachineInstrBundleIteratorTraits<T, false> {
  using list_type = simple_ilist<T, ilist_sentinel_tracking<true>>;
  using instr_iterator = typename list_type::iterator;
  using nonconst_instr_iterator = typename list_type::iterator;
  using const_instr_iterator = typename list_type::const_iterator;
};
template <class T> struct MachineInstrBundleIteratorTraits<T, true> {
  using list_type = simple_ilist<T, ilist_sentinel_tracking<true>>;
  using instr_iterator = typename list_type::reverse_iterator;
  using nonconst_instr_iterator = typename list_type::reverse_iterator;
  using const_instr_iterator = typename list_type::const_reverse_iterator;
};
template <class T> struct MachineInstrBundleIteratorTraits<const T, false> {
  using list_type = simple_ilist<T, ilist_sentinel_tracking<true>>;
  using instr_iterator = typename list_type::const_iterator;
  using nonconst_instr_iterator = typename list_type::iterator;
  using const_instr_iterator = typename list_type::const_iterator;
};
template <class T> struct MachineInstrBundleIteratorTraits<const T, true> {
  using list_type = simple_ilist<T, ilist_sentinel_tracking<true>>;
  using instr_iterator = typename list_type::const_reverse_iterator;
  using nonconst_instr_iterator = typename list_type::reverse_iterator;
  using const_instr_iterator = typename list_type::const_reverse_iterator;
};

template <bool IsReverse> struct MachineInstrBundleIteratorHelper;
template <> struct MachineInstrBundleIteratorHelper<false> {
  /// Get the beginning of the current bundle.
  template <class Iterator> static Iterator getBundleBegin(Iterator I) {
    if (!I.isEnd())
      while (I->isBundledWithPred())
        --I;
    return I;
  }

  /// Get the final node of the current bundle.
  template <class Iterator> static Iterator getBundleFinal(Iterator I) {
    if (!I.isEnd())
      while (I->isBundledWithSucc())
        ++I;
    return I;
  }

  /// Increment forward ilist iterator.
  template <class Iterator> static void increment(Iterator &I) {
    I = std::next(getBundleFinal(I));
  }

  /// Decrement forward ilist iterator.
  template <class Iterator> static void decrement(Iterator &I) {
    I = getBundleBegin(std::prev(I));
  }
};

template <> struct MachineInstrBundleIteratorHelper<true> {
  /// Get the beginning of the current bundle.
  template <class Iterator> static Iterator getBundleBegin(Iterator I) {
    return MachineInstrBundleIteratorHelper<false>::getBundleBegin(
               I.getReverse())
        .getReverse();
  }

  /// Get the final node of the current bundle.
  template <class Iterator> static Iterator getBundleFinal(Iterator I) {
    return MachineInstrBundleIteratorHelper<false>::getBundleFinal(
               I.getReverse())
        .getReverse();
  }

  /// Increment reverse ilist iterator.
  template <class Iterator> static void increment(Iterator &I) {
    I = getBundleBegin(std::next(I));
  }

  /// Decrement reverse ilist iterator.
  template <class Iterator> static void decrement(Iterator &I) {
    I = std::prev(getBundleFinal(I));
  }
};

/// MachineBasicBlock iterator that automatically skips over MIs that are
/// inside bundles (i.e. walk top level MIs only).
template <typename Ty, bool IsReverse = false>
class MachineInstrBundleIterator : MachineInstrBundleIteratorHelper<IsReverse> {
  using Traits = MachineInstrBundleIteratorTraits<Ty, IsReverse>;
  using instr_iterator = typename Traits::instr_iterator;

  instr_iterator MII;

public:
  using value_type = typename instr_iterator::value_type;
  using difference_type = typename instr_iterator::difference_type;
  using pointer = typename instr_iterator::pointer;
  using reference = typename instr_iterator::reference;
  using const_pointer = typename instr_iterator::const_pointer;
  using const_reference = typename instr_iterator::const_reference;
  using iterator_category = std::bidirectional_iterator_tag;

private:
  using nonconst_instr_iterator = typename Traits::nonconst_instr_iterator;
  using const_instr_iterator = typename Traits::const_instr_iterator;
  using nonconst_iterator =
      MachineInstrBundleIterator<typename nonconst_instr_iterator::value_type,
                                 IsReverse>;
  using reverse_iterator = MachineInstrBundleIterator<Ty, !IsReverse>;

public:
  MachineInstrBundleIterator(instr_iterator MI) : MII(MI) {
    assert((!MI.getNodePtr() || MI.isEnd() || !MI->isBundledWithPred()) &&
           "It's not legal to initialize MachineInstrBundleIterator with a "
           "bundled MI");
  }

  MachineInstrBundleIterator(reference MI) : MII(MI) {
    assert(!MI.isBundledWithPred() && "It's not legal to initialize "
                                      "MachineInstrBundleIterator with a "
                                      "bundled MI");
  }

  MachineInstrBundleIterator(pointer MI) : MII(MI) {
    // FIXME: This conversion should be explicit.
    assert((!MI || !MI->isBundledWithPred()) && "It's not legal to initialize "
                                                "MachineInstrBundleIterator "
                                                "with a bundled MI");
  }

  // Template allows conversion from const to nonconst.
  template <class OtherTy>
  MachineInstrBundleIterator(
      const MachineInstrBundleIterator<OtherTy, IsReverse> &I,
      typename std::enable_if<std::is_convertible<OtherTy *, Ty *>::value,
                              void *>::type = nullptr)
      : MII(I.getInstrIterator()) {}

  MachineInstrBundleIterator() : MII(nullptr) {}

  /// Explicit conversion between forward/reverse iterators.
  ///
  /// Translate between forward and reverse iterators without changing range
  /// boundaries.  The resulting iterator will dereference (and have a handle)
  /// to the previous node, which is somewhat unexpected; but converting the
  /// two endpoints in a range will give the same range in reverse.
  ///
  /// This matches std::reverse_iterator conversions.
  explicit MachineInstrBundleIterator(
      const MachineInstrBundleIterator<Ty, !IsReverse> &I)
      : MachineInstrBundleIterator(++I.getReverse()) {}

  /// Get the bundle iterator for the given instruction's bundle.
  static MachineInstrBundleIterator getAtBundleBegin(instr_iterator MI) {
    return MachineInstrBundleIteratorHelper<IsReverse>::getBundleBegin(MI);
  }

  reference operator*() const { return *MII; }
  pointer operator->() const { return &operator*(); }

  /// Check for null.
  bool isValid() const { return MII.getNodePtr(); }

  friend bool operator==(const MachineInstrBundleIterator &L,
                         const MachineInstrBundleIterator &R) {
    return L.MII == R.MII;
  }
  friend bool operator==(const MachineInstrBundleIterator &L,
                         const const_instr_iterator &R) {
    return L.MII == R; // Avoid assertion about validity of R.
  }
  friend bool operator==(const const_instr_iterator &L,
                         const MachineInstrBundleIterator &R) {
    return L == R.MII; // Avoid assertion about validity of L.
  }
  friend bool operator==(const MachineInstrBundleIterator &L,
                         const nonconst_instr_iterator &R) {
    return L.MII == R; // Avoid assertion about validity of R.
  }
  friend bool operator==(const nonconst_instr_iterator &L,
                         const MachineInstrBundleIterator &R) {
    return L == R.MII; // Avoid assertion about validity of L.
  }
  friend bool operator==(const MachineInstrBundleIterator &L, const_pointer R) {
    return L == const_instr_iterator(R); // Avoid assertion about validity of R.
  }
  friend bool operator==(const_pointer L, const MachineInstrBundleIterator &R) {
    return const_instr_iterator(L) == R; // Avoid assertion about validity of L.
  }
  friend bool operator==(const MachineInstrBundleIterator &L,
                         const_reference R) {
    return L == &R; // Avoid assertion about validity of R.
  }
  friend bool operator==(const_reference L,
                         const MachineInstrBundleIterator &R) {
    return &L == R; // Avoid assertion about validity of L.
  }

  friend bool operator!=(const MachineInstrBundleIterator &L,
                         const MachineInstrBundleIterator &R) {
    return !(L == R);
  }
  friend bool operator!=(const MachineInstrBundleIterator &L,
                         const const_instr_iterator &R) {
    return !(L == R);
  }
  friend bool operator!=(const const_instr_iterator &L,
                         const MachineInstrBundleIterator &R) {
    return !(L == R);
  }
  friend bool operator!=(const MachineInstrBundleIterator &L,
                         const nonconst_instr_iterator &R) {
    return !(L == R);
  }
  friend bool operator!=(const nonconst_instr_iterator &L,
                         const MachineInstrBundleIterator &R) {
    return !(L == R);
  }
  friend bool operator!=(const MachineInstrBundleIterator &L, const_pointer R) {
    return !(L == R);
  }
  friend bool operator!=(const_pointer L, const MachineInstrBundleIterator &R) {
    return !(L == R);
  }
  friend bool operator!=(const MachineInstrBundleIterator &L,
                         const_reference R) {
    return !(L == R);
  }
  friend bool operator!=(const_reference L,
                         const MachineInstrBundleIterator &R) {
    return !(L == R);
  }

  // Increment and decrement operators...
  MachineInstrBundleIterator &operator--() {
    this->decrement(MII);
    return *this;
  }
  MachineInstrBundleIterator &operator++() {
    this->increment(MII);
    return *this;
  }
  MachineInstrBundleIterator operator--(int) {
    MachineInstrBundleIterator Temp = *this;
    --*this;
    return Temp;
  }
  MachineInstrBundleIterator operator++(int) {
    MachineInstrBundleIterator Temp = *this;
    ++*this;
    return Temp;
  }

  instr_iterator getInstrIterator() const { return MII; }

  nonconst_iterator getNonConstIterator() const { return MII.getNonConst(); }

  /// Get a reverse iterator to the same node.
  ///
  /// Gives a reverse iterator that will dereference (and have a handle) to the
  /// same node.  Converting the endpoint iterators in a range will give a
  /// different range; for range operations, use the explicit conversions.
  reverse_iterator getReverse() const { return MII.getReverse(); }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEINSTRBUNDLEITERATOR_H
