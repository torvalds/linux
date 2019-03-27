//== RangedConstraintManager.h ----------------------------------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  Ranged constraint manager, built on SimpleConstraintManager.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CORE_RANGEDCONSTRAINTMANAGER_H
#define LLVM_CLANG_LIB_STATICANALYZER_CORE_RANGEDCONSTRAINTMANAGER_H

#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SimpleConstraintManager.h"

namespace clang {

namespace ento {

/// A Range represents the closed range [from, to].  The caller must
/// guarantee that from <= to.  Note that Range is immutable, so as not
/// to subvert RangeSet's immutability.
class Range : public std::pair<const llvm::APSInt *, const llvm::APSInt *> {
public:
  Range(const llvm::APSInt &from, const llvm::APSInt &to)
      : std::pair<const llvm::APSInt *, const llvm::APSInt *>(&from, &to) {
    assert(from <= to);
  }
  bool Includes(const llvm::APSInt &v) const {
    return *first <= v && v <= *second;
  }
  const llvm::APSInt &From() const { return *first; }
  const llvm::APSInt &To() const { return *second; }
  const llvm::APSInt *getConcreteValue() const {
    return &From() == &To() ? &From() : nullptr;
  }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddPointer(&From());
    ID.AddPointer(&To());
  }
};

class RangeTrait : public llvm::ImutContainerInfo<Range> {
public:
  // When comparing if one Range is less than another, we should compare
  // the actual APSInt values instead of their pointers.  This keeps the order
  // consistent (instead of comparing by pointer values) and can potentially
  // be used to speed up some of the operations in RangeSet.
  static inline bool isLess(key_type_ref lhs, key_type_ref rhs) {
    return *lhs.first < *rhs.first ||
           (!(*rhs.first < *lhs.first) && *lhs.second < *rhs.second);
  }
};

/// RangeSet contains a set of ranges. If the set is empty, then
///  there the value of a symbol is overly constrained and there are no
///  possible values for that symbol.
class RangeSet {
  typedef llvm::ImmutableSet<Range, RangeTrait> PrimRangeSet;
  PrimRangeSet ranges; // no need to make const, since it is an
                       // ImmutableSet - this allows default operator=
                       // to work.
public:
  typedef PrimRangeSet::Factory Factory;
  typedef PrimRangeSet::iterator iterator;

  RangeSet(PrimRangeSet RS) : ranges(RS) {}

  /// Create a new set with all ranges of this set and RS.
  /// Possible intersections are not checked here.
  RangeSet addRange(Factory &F, const RangeSet &RS) {
    PrimRangeSet Ranges(RS.ranges);
    for (const auto &range : ranges)
      Ranges = F.add(Ranges, range);
    return RangeSet(Ranges);
  }

  iterator begin() const { return ranges.begin(); }
  iterator end() const { return ranges.end(); }

  bool isEmpty() const { return ranges.isEmpty(); }

  /// Construct a new RangeSet representing '{ [from, to] }'.
  RangeSet(Factory &F, const llvm::APSInt &from, const llvm::APSInt &to)
      : ranges(F.add(F.getEmptySet(), Range(from, to))) {}

  /// Profile - Generates a hash profile of this RangeSet for use
  ///  by FoldingSet.
  void Profile(llvm::FoldingSetNodeID &ID) const { ranges.Profile(ID); }

  /// getConcreteValue - If a symbol is contrained to equal a specific integer
  ///  constant then this method returns that value.  Otherwise, it returns
  ///  NULL.
  const llvm::APSInt *getConcreteValue() const {
    return ranges.isSingleton() ? ranges.begin()->getConcreteValue() : nullptr;
  }

private:
  void IntersectInRange(BasicValueFactory &BV, Factory &F,
                        const llvm::APSInt &Lower, const llvm::APSInt &Upper,
                        PrimRangeSet &newRanges, PrimRangeSet::iterator &i,
                        PrimRangeSet::iterator &e) const;

  const llvm::APSInt &getMinValue() const;

  bool pin(llvm::APSInt &Lower, llvm::APSInt &Upper) const;

public:
  RangeSet Intersect(BasicValueFactory &BV, Factory &F, llvm::APSInt Lower,
                     llvm::APSInt Upper) const;

  RangeSet Negate(BasicValueFactory &BV, Factory &F) const;

  void print(raw_ostream &os) const;

  bool operator==(const RangeSet &other) const {
    return ranges == other.ranges;
  }
};


class ConstraintRange {};
using ConstraintRangeTy = llvm::ImmutableMap<SymbolRef, RangeSet>;

template <>
struct ProgramStateTrait<ConstraintRange>
  : public ProgramStatePartialTrait<ConstraintRangeTy> {
  static void *GDMIndex();
};


class RangedConstraintManager : public SimpleConstraintManager {
public:
  RangedConstraintManager(SubEngine *SE, SValBuilder &SB)
      : SimpleConstraintManager(SE, SB) {}

  ~RangedConstraintManager() override;

  //===------------------------------------------------------------------===//
  // Implementation for interface from SimpleConstraintManager.
  //===------------------------------------------------------------------===//

  ProgramStateRef assumeSym(ProgramStateRef State, SymbolRef Sym,
                            bool Assumption) override;

  ProgramStateRef assumeSymInclusiveRange(ProgramStateRef State, SymbolRef Sym,
                                          const llvm::APSInt &From,
                                          const llvm::APSInt &To,
                                          bool InRange) override;

  ProgramStateRef assumeSymUnsupported(ProgramStateRef State, SymbolRef Sym,
                                       bool Assumption) override;

protected:
  /// Assume a constraint between a symbolic expression and a concrete integer.
  virtual ProgramStateRef assumeSymRel(ProgramStateRef State, SymbolRef Sym,
                               BinaryOperator::Opcode op,
                               const llvm::APSInt &Int);

  //===------------------------------------------------------------------===//
  // Interface that subclasses must implement.
  //===------------------------------------------------------------------===//

  // Each of these is of the form "$Sym+Adj <> V", where "<>" is the comparison
  // operation for the method being invoked.

  virtual ProgramStateRef assumeSymNE(ProgramStateRef State, SymbolRef Sym,
                                      const llvm::APSInt &V,
                                      const llvm::APSInt &Adjustment) = 0;

  virtual ProgramStateRef assumeSymEQ(ProgramStateRef State, SymbolRef Sym,
                                      const llvm::APSInt &V,
                                      const llvm::APSInt &Adjustment) = 0;

  virtual ProgramStateRef assumeSymLT(ProgramStateRef State, SymbolRef Sym,
                                      const llvm::APSInt &V,
                                      const llvm::APSInt &Adjustment) = 0;

  virtual ProgramStateRef assumeSymGT(ProgramStateRef State, SymbolRef Sym,
                                      const llvm::APSInt &V,
                                      const llvm::APSInt &Adjustment) = 0;

  virtual ProgramStateRef assumeSymLE(ProgramStateRef State, SymbolRef Sym,
                                      const llvm::APSInt &V,
                                      const llvm::APSInt &Adjustment) = 0;

  virtual ProgramStateRef assumeSymGE(ProgramStateRef State, SymbolRef Sym,
                                      const llvm::APSInt &V,
                                      const llvm::APSInt &Adjustment) = 0;

  virtual ProgramStateRef assumeSymWithinInclusiveRange(
      ProgramStateRef State, SymbolRef Sym, const llvm::APSInt &From,
      const llvm::APSInt &To, const llvm::APSInt &Adjustment) = 0;

  virtual ProgramStateRef assumeSymOutsideInclusiveRange(
      ProgramStateRef State, SymbolRef Sym, const llvm::APSInt &From,
      const llvm::APSInt &To, const llvm::APSInt &Adjustment) = 0;

  //===------------------------------------------------------------------===//
  // Internal implementation.
  //===------------------------------------------------------------------===//
private:
  static void computeAdjustment(SymbolRef &Sym, llvm::APSInt &Adjustment);
};

} // end GR namespace

} // end clang namespace

#endif
