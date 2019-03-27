//===--- DAGDeltaAlgorithm.cpp - A DAG Minimization Algorithm --*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//===----------------------------------------------------------------------===//
//
// The algorithm we use attempts to exploit the dependency information by
// minimizing top-down. We start by constructing an initial root set R, and
// then iteratively:
//
//   1. Minimize the set R using the test predicate:
//       P'(S) = P(S union pred*(S))
//
//   2. Extend R to R' = R union pred(R).
//
// until a fixed point is reached.
//
// The idea is that we want to quickly prune entire portions of the graph, so we
// try to find high-level nodes that can be eliminated with all of their
// dependents.
//
// FIXME: The current algorithm doesn't actually provide a strong guarantee
// about the minimality of the result. The problem is that after adding nodes to
// the required set, we no longer consider them for elimination. For strictly
// well formed predicates, this doesn't happen, but it commonly occurs in
// practice when there are unmodelled dependencies. I believe we can resolve
// this by allowing the required set to be minimized as well, but need more test
// cases first.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DAGDeltaAlgorithm.h"
#include "llvm/ADT/DeltaAlgorithm.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <map>
using namespace llvm;

#define DEBUG_TYPE "dag-delta"

namespace {

class DAGDeltaAlgorithmImpl {
  friend class DeltaActiveSetHelper;

public:
  typedef DAGDeltaAlgorithm::change_ty change_ty;
  typedef DAGDeltaAlgorithm::changeset_ty changeset_ty;
  typedef DAGDeltaAlgorithm::changesetlist_ty changesetlist_ty;
  typedef DAGDeltaAlgorithm::edge_ty edge_ty;

private:
  typedef std::vector<change_ty>::iterator pred_iterator_ty;
  typedef std::vector<change_ty>::iterator succ_iterator_ty;
  typedef std::set<change_ty>::iterator pred_closure_iterator_ty;
  typedef std::set<change_ty>::iterator succ_closure_iterator_ty;

  DAGDeltaAlgorithm &DDA;

  std::vector<change_ty> Roots;

  /// Cache of failed test results. Successful test results are never cached
  /// since we always reduce following a success. We maintain an independent
  /// cache from that used by the individual delta passes because we may get
  /// hits across multiple individual delta invocations.
  mutable std::set<changeset_ty> FailedTestsCache;

  // FIXME: Gross.
  std::map<change_ty, std::vector<change_ty> > Predecessors;
  std::map<change_ty, std::vector<change_ty> > Successors;

  std::map<change_ty, std::set<change_ty> > PredClosure;
  std::map<change_ty, std::set<change_ty> > SuccClosure;

private:
  pred_iterator_ty pred_begin(change_ty Node) {
    assert(Predecessors.count(Node) && "Invalid node!");
    return Predecessors[Node].begin();
  }
  pred_iterator_ty pred_end(change_ty Node) {
    assert(Predecessors.count(Node) && "Invalid node!");
    return Predecessors[Node].end();
  }

  pred_closure_iterator_ty pred_closure_begin(change_ty Node) {
    assert(PredClosure.count(Node) && "Invalid node!");
    return PredClosure[Node].begin();
  }
  pred_closure_iterator_ty pred_closure_end(change_ty Node) {
    assert(PredClosure.count(Node) && "Invalid node!");
    return PredClosure[Node].end();
  }

  succ_iterator_ty succ_begin(change_ty Node) {
    assert(Successors.count(Node) && "Invalid node!");
    return Successors[Node].begin();
  }
  succ_iterator_ty succ_end(change_ty Node) {
    assert(Successors.count(Node) && "Invalid node!");
    return Successors[Node].end();
  }

  succ_closure_iterator_ty succ_closure_begin(change_ty Node) {
    assert(SuccClosure.count(Node) && "Invalid node!");
    return SuccClosure[Node].begin();
  }
  succ_closure_iterator_ty succ_closure_end(change_ty Node) {
    assert(SuccClosure.count(Node) && "Invalid node!");
    return SuccClosure[Node].end();
  }

  void UpdatedSearchState(const changeset_ty &Changes,
                          const changesetlist_ty &Sets,
                          const changeset_ty &Required) {
    DDA.UpdatedSearchState(Changes, Sets, Required);
  }

  /// ExecuteOneTest - Execute a single test predicate on the change set \p S.
  bool ExecuteOneTest(const changeset_ty &S) {
    // Check dependencies invariant.
    LLVM_DEBUG({
      for (changeset_ty::const_iterator it = S.begin(), ie = S.end(); it != ie;
           ++it)
        for (succ_iterator_ty it2 = succ_begin(*it), ie2 = succ_end(*it);
             it2 != ie2; ++it2)
          assert(S.count(*it2) && "Attempt to run invalid changeset!");
    });

    return DDA.ExecuteOneTest(S);
  }

public:
  DAGDeltaAlgorithmImpl(DAGDeltaAlgorithm &DDA, const changeset_ty &Changes,
                        const std::vector<edge_ty> &Dependencies);

  changeset_ty Run();

  /// GetTestResult - Get the test result for the active set \p Changes with
  /// \p Required changes from the cache, executing the test if necessary.
  ///
  /// \param Changes - The set of active changes being minimized, which should
  /// have their pred closure included in the test.
  /// \param Required - The set of changes which have previously been
  /// established to be required.
  /// \return - The test result.
  bool GetTestResult(const changeset_ty &Changes, const changeset_ty &Required);
};

/// Helper object for minimizing an active set of changes.
class DeltaActiveSetHelper : public DeltaAlgorithm {
  DAGDeltaAlgorithmImpl &DDAI;

  const changeset_ty &Required;

protected:
  /// UpdatedSearchState - Callback used when the search state changes.
  void UpdatedSearchState(const changeset_ty &Changes,
                                  const changesetlist_ty &Sets) override {
    DDAI.UpdatedSearchState(Changes, Sets, Required);
  }

  bool ExecuteOneTest(const changeset_ty &S) override {
    return DDAI.GetTestResult(S, Required);
  }

public:
  DeltaActiveSetHelper(DAGDeltaAlgorithmImpl &DDAI,
                       const changeset_ty &Required)
      : DDAI(DDAI), Required(Required) {}
};

}

DAGDeltaAlgorithmImpl::DAGDeltaAlgorithmImpl(
    DAGDeltaAlgorithm &DDA, const changeset_ty &Changes,
    const std::vector<edge_ty> &Dependencies)
    : DDA(DDA) {
  for (changeset_ty::const_iterator it = Changes.begin(),
         ie = Changes.end(); it != ie; ++it) {
    Predecessors.insert(std::make_pair(*it, std::vector<change_ty>()));
    Successors.insert(std::make_pair(*it, std::vector<change_ty>()));
  }
  for (std::vector<edge_ty>::const_iterator it = Dependencies.begin(),
         ie = Dependencies.end(); it != ie; ++it) {
    Predecessors[it->second].push_back(it->first);
    Successors[it->first].push_back(it->second);
  }

  // Compute the roots.
  for (changeset_ty::const_iterator it = Changes.begin(),
         ie = Changes.end(); it != ie; ++it)
    if (succ_begin(*it) == succ_end(*it))
      Roots.push_back(*it);

  // Pre-compute the closure of the successor relation.
  std::vector<change_ty> Worklist(Roots.begin(), Roots.end());
  while (!Worklist.empty()) {
    change_ty Change = Worklist.back();
    Worklist.pop_back();

    std::set<change_ty> &ChangeSuccs = SuccClosure[Change];
    for (pred_iterator_ty it = pred_begin(Change),
           ie = pred_end(Change); it != ie; ++it) {
      SuccClosure[*it].insert(Change);
      SuccClosure[*it].insert(ChangeSuccs.begin(), ChangeSuccs.end());
      Worklist.push_back(*it);
    }
  }

  // Invert to form the predecessor closure map.
  for (changeset_ty::const_iterator it = Changes.begin(),
         ie = Changes.end(); it != ie; ++it)
    PredClosure.insert(std::make_pair(*it, std::set<change_ty>()));
  for (changeset_ty::const_iterator it = Changes.begin(),
         ie = Changes.end(); it != ie; ++it)
    for (succ_closure_iterator_ty it2 = succ_closure_begin(*it),
           ie2 = succ_closure_end(*it); it2 != ie2; ++it2)
      PredClosure[*it2].insert(*it);

  // Dump useful debug info.
  LLVM_DEBUG({
    llvm::errs() << "-- DAGDeltaAlgorithmImpl --\n";
    llvm::errs() << "Changes: [";
    for (changeset_ty::const_iterator it = Changes.begin(), ie = Changes.end();
         it != ie; ++it) {
      if (it != Changes.begin())
        llvm::errs() << ", ";
      llvm::errs() << *it;

      if (succ_begin(*it) != succ_end(*it)) {
        llvm::errs() << "(";
        for (succ_iterator_ty it2 = succ_begin(*it), ie2 = succ_end(*it);
             it2 != ie2; ++it2) {
          if (it2 != succ_begin(*it))
            llvm::errs() << ", ";
          llvm::errs() << "->" << *it2;
        }
        llvm::errs() << ")";
      }
    }
    llvm::errs() << "]\n";

    llvm::errs() << "Roots: [";
    for (std::vector<change_ty>::const_iterator it = Roots.begin(),
                                                ie = Roots.end();
         it != ie; ++it) {
      if (it != Roots.begin())
        llvm::errs() << ", ";
      llvm::errs() << *it;
    }
    llvm::errs() << "]\n";

    llvm::errs() << "Predecessor Closure:\n";
    for (changeset_ty::const_iterator it = Changes.begin(), ie = Changes.end();
         it != ie; ++it) {
      llvm::errs() << format("  %-4d: [", *it);
      for (pred_closure_iterator_ty it2 = pred_closure_begin(*it),
                                    ie2 = pred_closure_end(*it);
           it2 != ie2; ++it2) {
        if (it2 != pred_closure_begin(*it))
          llvm::errs() << ", ";
        llvm::errs() << *it2;
      }
      llvm::errs() << "]\n";
    }

    llvm::errs() << "Successor Closure:\n";
    for (changeset_ty::const_iterator it = Changes.begin(), ie = Changes.end();
         it != ie; ++it) {
      llvm::errs() << format("  %-4d: [", *it);
      for (succ_closure_iterator_ty it2 = succ_closure_begin(*it),
                                    ie2 = succ_closure_end(*it);
           it2 != ie2; ++it2) {
        if (it2 != succ_closure_begin(*it))
          llvm::errs() << ", ";
        llvm::errs() << *it2;
      }
      llvm::errs() << "]\n";
    }

    llvm::errs() << "\n\n";
  });
}

bool DAGDeltaAlgorithmImpl::GetTestResult(const changeset_ty &Changes,
                                          const changeset_ty &Required) {
  changeset_ty Extended(Required);
  Extended.insert(Changes.begin(), Changes.end());
  for (changeset_ty::const_iterator it = Changes.begin(),
         ie = Changes.end(); it != ie; ++it)
    Extended.insert(pred_closure_begin(*it), pred_closure_end(*it));

  if (FailedTestsCache.count(Extended))
    return false;

  bool Result = ExecuteOneTest(Extended);
  if (!Result)
    FailedTestsCache.insert(Extended);

  return Result;
}

DAGDeltaAlgorithm::changeset_ty
DAGDeltaAlgorithmImpl::Run() {
  // The current set of changes we are minimizing, starting at the roots.
  changeset_ty CurrentSet(Roots.begin(), Roots.end());

  // The set of required changes.
  changeset_ty Required;

  // Iterate until the active set of changes is empty. Convergence is guaranteed
  // assuming input was a DAG.
  //
  // Invariant:  CurrentSet intersect Required == {}
  // Invariant:  Required == (Required union succ*(Required))
  while (!CurrentSet.empty()) {
    LLVM_DEBUG({
      llvm::errs() << "DAG_DD - " << CurrentSet.size() << " active changes, "
                   << Required.size() << " required changes\n";
    });

    // Minimize the current set of changes.
    DeltaActiveSetHelper Helper(*this, Required);
    changeset_ty CurrentMinSet = Helper.Run(CurrentSet);

    // Update the set of required changes. Since
    //   CurrentMinSet subset CurrentSet
    // and after the last iteration,
    //   succ(CurrentSet) subset Required
    // then
    //   succ(CurrentMinSet) subset Required
    // and our invariant on Required is maintained.
    Required.insert(CurrentMinSet.begin(), CurrentMinSet.end());

    // Replace the current set with the predecssors of the minimized set of
    // active changes.
    CurrentSet.clear();
    for (changeset_ty::const_iterator it = CurrentMinSet.begin(),
           ie = CurrentMinSet.end(); it != ie; ++it)
      CurrentSet.insert(pred_begin(*it), pred_end(*it));

    // FIXME: We could enforce CurrentSet intersect Required == {} here if we
    // wanted to protect against cyclic graphs.
  }

  return Required;
}

void DAGDeltaAlgorithm::anchor() {
}

DAGDeltaAlgorithm::changeset_ty
DAGDeltaAlgorithm::Run(const changeset_ty &Changes,
                       const std::vector<edge_ty> &Dependencies) {
  return DAGDeltaAlgorithmImpl(*this, Changes, Dependencies).Run();
}
