//===- SetTheory.h - Generate ordered sets from DAG expressions -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the SetTheory class that computes ordered sets of
// Records from DAG expressions.  Operators for standard set operations are
// predefined, and it is possible to add special purpose set operators as well.
//
// The user may define named sets as Records of predefined classes. Set
// expanders can be added to a SetTheory instance to teach it how to find the
// elements of such a named set.
//
// These are the predefined operators. The argument lists can be individual
// elements (defs), other sets (defs of expandable classes), lists, or DAG
// expressions that are evaluated recursively.
//
// - (add S1, S2 ...) Union sets. This is also how sets are created from element
//   lists.
//
// - (sub S1, S2, ...) Set difference. Every element in S1 except for the
//   elements in S2, ...
//
// - (and S1, S2) Set intersection. Every element in S1 that is also in S2.
//
// - (shl S, N) Shift left. Remove the first N elements from S.
//
// - (trunc S, N) Truncate. The first N elements of S.
//
// - (rotl S, N) Rotate left. Same as (add (shl S, N), (trunc S, N)).
//
// - (rotr S, N) Rotate right.
//
// - (decimate S, N) Decimate S by picking every N'th element, starting with
//   the first one. For instance, (decimate S, 2) returns the even elements of
//   S.
//
// - (sequence "Format", From, To) Generate a sequence of defs with printf.
//   For instance, (sequence "R%u", 0, 3) -> [ R0, R1, R2, R3 ]
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TABLEGEN_SETTHEORY_H
#define LLVM_TABLEGEN_SETTHEORY_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/SMLoc.h"
#include <map>
#include <memory>
#include <vector>

namespace llvm {

class DagInit;
class Init;
class Record;

class SetTheory {
public:
  using RecVec = std::vector<Record *>;
  using RecSet = SmallSetVector<Record *, 16>;

  /// Operator - A callback representing a DAG operator.
  class Operator {
    virtual void anchor();

  public:
    virtual ~Operator() = default;

    /// apply - Apply this operator to Expr's arguments and insert the result
    /// in Elts.
    virtual void apply(SetTheory&, DagInit *Expr, RecSet &Elts,
                       ArrayRef<SMLoc> Loc) = 0;
  };

  /// Expander - A callback function that can transform a Record representing a
  /// set into a fully expanded list of elements. Expanders provide a way for
  /// users to define named sets that can be used in DAG expressions.
  class Expander {
    virtual void anchor();

  public:
    virtual ~Expander() = default;

    virtual void expand(SetTheory&, Record*, RecSet &Elts) = 0;
  };

private:
  // Map set defs to their fully expanded contents. This serves as a memoization
  // cache and it makes it possible to return const references on queries.
  using ExpandMap = std::map<Record *, RecVec>;
  ExpandMap Expansions;

  // Known DAG operators by name.
  StringMap<std::unique_ptr<Operator>> Operators;

  // Typed expanders by class name.
  StringMap<std::unique_ptr<Expander>> Expanders;

public:
  /// Create a SetTheory instance with only the standard operators.
  SetTheory();

  /// addExpander - Add an expander for Records with the named super class.
  void addExpander(StringRef ClassName, std::unique_ptr<Expander>);

  /// addFieldExpander - Add an expander for ClassName that simply evaluates
  /// FieldName in the Record to get the set elements.  That is all that is
  /// needed for a class like:
  ///
  ///   class Set<dag d> {
  ///     dag Elts = d;
  ///   }
  ///
  void addFieldExpander(StringRef ClassName, StringRef FieldName);

  /// addOperator - Add a DAG operator.
  void addOperator(StringRef Name, std::unique_ptr<Operator>);

  /// evaluate - Evaluate Expr and append the resulting set to Elts.
  void evaluate(Init *Expr, RecSet &Elts, ArrayRef<SMLoc> Loc);

  /// evaluate - Evaluate a sequence of Inits and append to Elts.
  template<typename Iter>
  void evaluate(Iter begin, Iter end, RecSet &Elts, ArrayRef<SMLoc> Loc) {
    while (begin != end)
      evaluate(*begin++, Elts, Loc);
  }

  /// expand - Expand a record into a set of elements if possible.  Return a
  /// pointer to the expanded elements, or NULL if Set cannot be expanded
  /// further.
  const RecVec *expand(Record *Set);
};

} // end namespace llvm

#endif // LLVM_TABLEGEN_SETTHEORY_H
