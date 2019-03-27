//===-- TrigramIndex.h - a heuristic for SpecialCaseList --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//===----------------------------------------------------------------------===//
//
// TrigramIndex implements a heuristic for SpecialCaseList that allows to
// filter out ~99% incoming queries when all regular expressions in the
// SpecialCaseList are simple wildcards with '*' and '.'. If rules are more
// complicated, the check is defeated and it will always pass the queries to a
// full regex.
//
// The basic idea is that in order for a wildcard to match a query, the query
// needs to have all trigrams which occur in the wildcard. We create a trigram
// index (trigram -> list of rules with it) and then count trigrams in the query
// for each rule. If the count for one of the rules reaches the expected value,
// the check passes the query to a regex. If none of the rules got enough
// trigrams, the check tells that the query is definitely not matched by any
// of the rules, and no regex matching is needed.
// A similar idea was used in Google Code Search as described in the blog post:
// https://swtch.com/~rsc/regexp/regexp4.html
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TRIGRAMINDEX_H
#define LLVM_SUPPORT_TRIGRAMINDEX_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace llvm {
class StringRef;

class TrigramIndex {
 public:
  /// Inserts a new Regex into the index.
  void insert(std::string Regex);

  /// Returns true, if special case list definitely does not have a line
  /// that matches the query. Returns false, if it's not sure.
  bool isDefinitelyOut(StringRef Query) const;

  /// Returned true, iff the heuristic is defeated and not useful.
  /// In this case isDefinitelyOut always returns false.
  bool isDefeated() { return Defeated; }
 private:
  // If true, the rules are too complicated for the check to work, and full
  // regex matching is needed for every rule.
  bool Defeated = false;
  // The minimum number of trigrams which should match for a rule to have a
  // chance to match the query. The number of elements equals the number of
  // regex rules in the SpecialCaseList.
  std::vector<unsigned> Counts;
  // Index holds a list of rules indices for each trigram. The same indices
  // are used in Counts to store per-rule limits.
  // If a trigram is too common (>4 rules with it), we stop tracking it,
  // which increases the probability for a need to match using regex, but
  // decreases the costs in the regular case.
  std::unordered_map<unsigned, SmallVector<size_t, 4>> Index{256};
};

}  // namespace llvm

#endif  // LLVM_SUPPORT_TRIGRAMINDEX_H
