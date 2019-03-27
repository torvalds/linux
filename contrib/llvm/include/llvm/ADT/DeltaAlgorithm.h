//===- DeltaAlgorithm.h - A Set Minimization Algorithm ---------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_DELTAALGORITHM_H
#define LLVM_ADT_DELTAALGORITHM_H

#include <set>
#include <vector>

namespace llvm {

/// DeltaAlgorithm - Implements the delta debugging algorithm (A. Zeller '99)
/// for minimizing arbitrary sets using a predicate function.
///
/// The result of the algorithm is a subset of the input change set which is
/// guaranteed to satisfy the predicate, assuming that the input set did. For
/// well formed predicates, the result set is guaranteed to be such that
/// removing any single element would falsify the predicate.
///
/// For best results the predicate function *should* (but need not) satisfy
/// certain properties, in particular:
///  (1) The predicate should return false on an empty set and true on the full
///  set.
///  (2) If the predicate returns true for a set of changes, it should return
///  true for all supersets of that set.
///
/// It is not an error to provide a predicate that does not satisfy these
/// requirements, and the algorithm will generally produce reasonable
/// results. However, it may run substantially more tests than with a good
/// predicate.
class DeltaAlgorithm {
public:
  using change_ty = unsigned;
  // FIXME: Use a decent data structure.
  using changeset_ty = std::set<change_ty>;
  using changesetlist_ty = std::vector<changeset_ty>;

private:
  /// Cache of failed test results. Successful test results are never cached
  /// since we always reduce following a success.
  std::set<changeset_ty> FailedTestsCache;

  /// GetTestResult - Get the test result for the \p Changes from the
  /// cache, executing the test if necessary.
  ///
  /// \param Changes - The change set to test.
  /// \return - The test result.
  bool GetTestResult(const changeset_ty &Changes);

  /// Split - Partition a set of changes \p S into one or two subsets.
  void Split(const changeset_ty &S, changesetlist_ty &Res);

  /// Delta - Minimize a set of \p Changes which has been partioned into
  /// smaller sets, by attempting to remove individual subsets.
  changeset_ty Delta(const changeset_ty &Changes,
                     const changesetlist_ty &Sets);

  /// Search - Search for a subset (or subsets) in \p Sets which can be
  /// removed from \p Changes while still satisfying the predicate.
  ///
  /// \param Res - On success, a subset of Changes which satisfies the
  /// predicate.
  /// \return - True on success.
  bool Search(const changeset_ty &Changes, const changesetlist_ty &Sets,
              changeset_ty &Res);

protected:
  /// UpdatedSearchState - Callback used when the search state changes.
  virtual void UpdatedSearchState(const changeset_ty &Changes,
                                  const changesetlist_ty &Sets) {}

  /// ExecuteOneTest - Execute a single test predicate on the change set \p S.
  virtual bool ExecuteOneTest(const changeset_ty &S) = 0;

  DeltaAlgorithm& operator=(const DeltaAlgorithm&) = default;

public:
  virtual ~DeltaAlgorithm();

  /// Run - Minimize the set \p Changes by executing \see ExecuteOneTest() on
  /// subsets of changes and returning the smallest set which still satisfies
  /// the test predicate.
  changeset_ty Run(const changeset_ty &Changes);
};

} // end namespace llvm

#endif // LLVM_ADT_DELTAALGORITHM_H
