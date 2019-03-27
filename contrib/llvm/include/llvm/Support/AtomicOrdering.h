//===-- llvm/Support/AtomicOrdering.h ---Atomic Ordering---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Atomic ordering constants.
///
/// These values are used by LLVM to represent atomic ordering for C++11's
/// memory model and more, as detailed in docs/Atomics.rst.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ATOMICORDERING_H
#define LLVM_SUPPORT_ATOMICORDERING_H

#include <cstddef>

namespace llvm {

/// Atomic ordering for C11 / C++11's memody models.
///
/// These values cannot change because they are shared with standard library
/// implementations as well as with other compilers.
enum class AtomicOrderingCABI {
  relaxed = 0,
  consume = 1,
  acquire = 2,
  release = 3,
  acq_rel = 4,
  seq_cst = 5,
};

bool operator<(AtomicOrderingCABI, AtomicOrderingCABI) = delete;
bool operator>(AtomicOrderingCABI, AtomicOrderingCABI) = delete;
bool operator<=(AtomicOrderingCABI, AtomicOrderingCABI) = delete;
bool operator>=(AtomicOrderingCABI, AtomicOrderingCABI) = delete;

// Validate an integral value which isn't known to fit within the enum's range
// is a valid AtomicOrderingCABI.
template <typename Int> inline bool isValidAtomicOrderingCABI(Int I) {
  return (Int)AtomicOrderingCABI::relaxed <= I &&
         I <= (Int)AtomicOrderingCABI::seq_cst;
}

/// Atomic ordering for LLVM's memory model.
///
/// C++ defines ordering as a lattice. LLVM supplements this with NotAtomic and
/// Unordered, which are both below the C++ orders.
///
/// not_atomic-->unordered-->relaxed-->release--------------->acq_rel-->seq_cst
///                                   \-->consume-->acquire--/
enum class AtomicOrdering {
  NotAtomic = 0,
  Unordered = 1,
  Monotonic = 2, // Equivalent to C++'s relaxed.
  // Consume = 3,  // Not specified yet.
  Acquire = 4,
  Release = 5,
  AcquireRelease = 6,
  SequentiallyConsistent = 7
};

bool operator<(AtomicOrdering, AtomicOrdering) = delete;
bool operator>(AtomicOrdering, AtomicOrdering) = delete;
bool operator<=(AtomicOrdering, AtomicOrdering) = delete;
bool operator>=(AtomicOrdering, AtomicOrdering) = delete;

// Validate an integral value which isn't known to fit within the enum's range
// is a valid AtomicOrdering.
template <typename Int> inline bool isValidAtomicOrdering(Int I) {
  return static_cast<Int>(AtomicOrdering::NotAtomic) <= I &&
         I <= static_cast<Int>(AtomicOrdering::SequentiallyConsistent);
}

/// String used by LLVM IR to represent atomic ordering.
inline const char *toIRString(AtomicOrdering ao) {
  static const char *names[8] = {"not_atomic", "unordered", "monotonic",
                                 "consume",    "acquire",   "release",
                                 "acq_rel",    "seq_cst"};
  return names[static_cast<size_t>(ao)];
}

/// Returns true if ao is stronger than other as defined by the AtomicOrdering
/// lattice, which is based on C++'s definition.
inline bool isStrongerThan(AtomicOrdering ao, AtomicOrdering other) {
  static const bool lookup[8][8] = {
      //               NA     UN     RX     CO     AC     RE     AR     SC
      /* NotAtomic */ {false, false, false, false, false, false, false, false},
      /* Unordered */ { true, false, false, false, false, false, false, false},
      /* relaxed   */ { true,  true, false, false, false, false, false, false},
      /* consume   */ { true,  true,  true, false, false, false, false, false},
      /* acquire   */ { true,  true,  true,  true, false, false, false, false},
      /* release   */ { true,  true,  true, false, false, false, false, false},
      /* acq_rel   */ { true,  true,  true,  true,  true,  true, false, false},
      /* seq_cst   */ { true,  true,  true,  true,  true,  true,  true, false},
  };
  return lookup[static_cast<size_t>(ao)][static_cast<size_t>(other)];
}

inline bool isAtLeastOrStrongerThan(AtomicOrdering ao, AtomicOrdering other) {
  static const bool lookup[8][8] = {
      //               NA     UN     RX     CO     AC     RE     AR     SC
      /* NotAtomic */ { true, false, false, false, false, false, false, false},
      /* Unordered */ { true,  true, false, false, false, false, false, false},
      /* relaxed   */ { true,  true,  true, false, false, false, false, false},
      /* consume   */ { true,  true,  true,  true, false, false, false, false},
      /* acquire   */ { true,  true,  true,  true,  true, false, false, false},
      /* release   */ { true,  true,  true, false, false,  true, false, false},
      /* acq_rel   */ { true,  true,  true,  true,  true,  true,  true, false},
      /* seq_cst   */ { true,  true,  true,  true,  true,  true,  true,  true},
  };
  return lookup[static_cast<size_t>(ao)][static_cast<size_t>(other)];
}

inline bool isStrongerThanUnordered(AtomicOrdering ao) {
  return isStrongerThan(ao, AtomicOrdering::Unordered);
}

inline bool isStrongerThanMonotonic(AtomicOrdering ao) {
  return isStrongerThan(ao, AtomicOrdering::Monotonic);
}

inline bool isAcquireOrStronger(AtomicOrdering ao) {
  return isAtLeastOrStrongerThan(ao, AtomicOrdering::Acquire);
}

inline bool isReleaseOrStronger(AtomicOrdering ao) {
  return isAtLeastOrStrongerThan(ao, AtomicOrdering::Release);
}

inline AtomicOrderingCABI toCABI(AtomicOrdering ao) {
  static const AtomicOrderingCABI lookup[8] = {
      /* NotAtomic */ AtomicOrderingCABI::relaxed,
      /* Unordered */ AtomicOrderingCABI::relaxed,
      /* relaxed   */ AtomicOrderingCABI::relaxed,
      /* consume   */ AtomicOrderingCABI::consume,
      /* acquire   */ AtomicOrderingCABI::acquire,
      /* release   */ AtomicOrderingCABI::release,
      /* acq_rel   */ AtomicOrderingCABI::acq_rel,
      /* seq_cst   */ AtomicOrderingCABI::seq_cst,
  };
  return lookup[static_cast<size_t>(ao)];
}

} // end namespace llvm

#endif // LLVM_SUPPORT_ATOMICORDERING_H
