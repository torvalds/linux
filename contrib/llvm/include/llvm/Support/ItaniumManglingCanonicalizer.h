//===--- ItaniumManglingCanonicalizer.h -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a class for computing equivalence classes of mangled names
// given a set of equivalences between name fragments.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ITANIUMMANGLINGCANONICALIZER_H
#define LLVM_SUPPORT_ITANIUMMANGLINGCANONICALIZER_H

#include "llvm/ADT/StringRef.h"

#include <cstddef>

namespace llvm {
/// Canonicalizer for mangled names.
///
/// This class allows specifying a list of "equivalent" manglings. For example,
/// you can specify that Ss is equivalent to
///   NSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEE
/// and then manglings that refer to libstdc++'s 'std::string' will be
/// considered equivalent to manglings that are the same except that they refer
/// to libc++'s 'std::string'.
///
/// This can be used when data (eg, profiling data) is available for a version
/// of a program built in a different configuration, with correspondingly
/// different manglings.
class ItaniumManglingCanonicalizer {
public:
  ItaniumManglingCanonicalizer();
  ItaniumManglingCanonicalizer(const ItaniumManglingCanonicalizer &) = delete;
  void operator=(const ItaniumManglingCanonicalizer &) = delete;
  ~ItaniumManglingCanonicalizer();

  enum class EquivalenceError {
    Success,

    /// Both the equivalent manglings have already been used as components of
    /// some other mangling we've looked at. It's too late to add this
    /// equivalence.
    ManglingAlreadyUsed,

    /// The first equivalent mangling is invalid.
    InvalidFirstMangling,

    /// The second equivalent mangling is invalid.
    InvalidSecondMangling,
  };

  enum class FragmentKind {
    /// The mangling fragment is a <name> (or a predefined <substitution>).
    Name,
    /// The mangling fragment is a <type>.
    Type,
    /// The mangling fragment is an <encoding>.
    Encoding,
  };

  /// Add an equivalence between \p First and \p Second. Both manglings must
  /// live at least as long as the canonicalizer.
  EquivalenceError addEquivalence(FragmentKind Kind, StringRef First,
                                  StringRef Second);

  using Key = uintptr_t;

  /// Form a canonical key for the specified mangling. They key will be the
  /// same for all equivalent manglings, and different for any two
  /// non-equivalent manglings, but is otherwise unspecified.
  ///
  /// Returns Key() if (and only if) the mangling is not a valid Itanium C++
  /// ABI mangling.
  ///
  /// The string denoted by Mangling must live as long as the canonicalizer.
  Key canonicalize(StringRef Mangling);

  /// Find a canonical key for the specified mangling, if one has already been
  /// formed. Otherwise returns Key().
  Key lookup(StringRef Mangling);

private:
  struct Impl;
  Impl *P;
};
} // namespace llvm

#endif // LLVM_SUPPORT_ITANIUMMANGLINGCANONICALIZER_H
