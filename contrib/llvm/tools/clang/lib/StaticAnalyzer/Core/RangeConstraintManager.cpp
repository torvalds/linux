//== RangeConstraintManager.cpp - Manage range constraints.------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines RangeConstraintManager, a class that tracks simple
//  equality and inequality constraints on symbolic values of ProgramState.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/APSIntType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/RangedConstraintManager.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/ImmutableSet.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

void RangeSet::IntersectInRange(BasicValueFactory &BV, Factory &F,
                      const llvm::APSInt &Lower, const llvm::APSInt &Upper,
                      PrimRangeSet &newRanges, PrimRangeSet::iterator &i,
                      PrimRangeSet::iterator &e) const {
  // There are six cases for each range R in the set:
  //   1. R is entirely before the intersection range.
  //   2. R is entirely after the intersection range.
  //   3. R contains the entire intersection range.
  //   4. R starts before the intersection range and ends in the middle.
  //   5. R starts in the middle of the intersection range and ends after it.
  //   6. R is entirely contained in the intersection range.
  // These correspond to each of the conditions below.
  for (/* i = begin(), e = end() */; i != e; ++i) {
    if (i->To() < Lower) {
      continue;
    }
    if (i->From() > Upper) {
      break;
    }

    if (i->Includes(Lower)) {
      if (i->Includes(Upper)) {
        newRanges =
            F.add(newRanges, Range(BV.getValue(Lower), BV.getValue(Upper)));
        break;
      } else
        newRanges = F.add(newRanges, Range(BV.getValue(Lower), i->To()));
    } else {
      if (i->Includes(Upper)) {
        newRanges = F.add(newRanges, Range(i->From(), BV.getValue(Upper)));
        break;
      } else
        newRanges = F.add(newRanges, *i);
    }
  }
}

const llvm::APSInt &RangeSet::getMinValue() const {
  assert(!isEmpty());
  return ranges.begin()->From();
}

bool RangeSet::pin(llvm::APSInt &Lower, llvm::APSInt &Upper) const {
  // This function has nine cases, the cartesian product of range-testing
  // both the upper and lower bounds against the symbol's type.
  // Each case requires a different pinning operation.
  // The function returns false if the described range is entirely outside
  // the range of values for the associated symbol.
  APSIntType Type(getMinValue());
  APSIntType::RangeTestResultKind LowerTest = Type.testInRange(Lower, true);
  APSIntType::RangeTestResultKind UpperTest = Type.testInRange(Upper, true);

  switch (LowerTest) {
  case APSIntType::RTR_Below:
    switch (UpperTest) {
    case APSIntType::RTR_Below:
      // The entire range is outside the symbol's set of possible values.
      // If this is a conventionally-ordered range, the state is infeasible.
      if (Lower <= Upper)
        return false;

      // However, if the range wraps around, it spans all possible values.
      Lower = Type.getMinValue();
      Upper = Type.getMaxValue();
      break;
    case APSIntType::RTR_Within:
      // The range starts below what's possible but ends within it. Pin.
      Lower = Type.getMinValue();
      Type.apply(Upper);
      break;
    case APSIntType::RTR_Above:
      // The range spans all possible values for the symbol. Pin.
      Lower = Type.getMinValue();
      Upper = Type.getMaxValue();
      break;
    }
    break;
  case APSIntType::RTR_Within:
    switch (UpperTest) {
    case APSIntType::RTR_Below:
      // The range wraps around, but all lower values are not possible.
      Type.apply(Lower);
      Upper = Type.getMaxValue();
      break;
    case APSIntType::RTR_Within:
      // The range may or may not wrap around, but both limits are valid.
      Type.apply(Lower);
      Type.apply(Upper);
      break;
    case APSIntType::RTR_Above:
      // The range starts within what's possible but ends above it. Pin.
      Type.apply(Lower);
      Upper = Type.getMaxValue();
      break;
    }
    break;
  case APSIntType::RTR_Above:
    switch (UpperTest) {
    case APSIntType::RTR_Below:
      // The range wraps but is outside the symbol's set of possible values.
      return false;
    case APSIntType::RTR_Within:
      // The range starts above what's possible but ends within it (wrap).
      Lower = Type.getMinValue();
      Type.apply(Upper);
      break;
    case APSIntType::RTR_Above:
      // The entire range is outside the symbol's set of possible values.
      // If this is a conventionally-ordered range, the state is infeasible.
      if (Lower <= Upper)
        return false;

      // However, if the range wraps around, it spans all possible values.
      Lower = Type.getMinValue();
      Upper = Type.getMaxValue();
      break;
    }
    break;
  }

  return true;
}

// Returns a set containing the values in the receiving set, intersected with
// the closed range [Lower, Upper]. Unlike the Range type, this range uses
// modular arithmetic, corresponding to the common treatment of C integer
// overflow. Thus, if the Lower bound is greater than the Upper bound, the
// range is taken to wrap around. This is equivalent to taking the
// intersection with the two ranges [Min, Upper] and [Lower, Max],
// or, alternatively, /removing/ all integers between Upper and Lower.
RangeSet RangeSet::Intersect(BasicValueFactory &BV, Factory &F,
                             llvm::APSInt Lower, llvm::APSInt Upper) const {
  if (!pin(Lower, Upper))
    return F.getEmptySet();

  PrimRangeSet newRanges = F.getEmptySet();

  PrimRangeSet::iterator i = begin(), e = end();
  if (Lower <= Upper)
    IntersectInRange(BV, F, Lower, Upper, newRanges, i, e);
  else {
    // The order of the next two statements is important!
    // IntersectInRange() does not reset the iteration state for i and e.
    // Therefore, the lower range most be handled first.
    IntersectInRange(BV, F, BV.getMinValue(Upper), Upper, newRanges, i, e);
    IntersectInRange(BV, F, Lower, BV.getMaxValue(Lower), newRanges, i, e);
  }

  return newRanges;
}

// Turn all [A, B] ranges to [-B, -A]. Ranges [MIN, B] are turned to range set
// [MIN, MIN] U [-B, MAX], when MIN and MAX are the minimal and the maximal
// signed values of the type.
RangeSet RangeSet::Negate(BasicValueFactory &BV, Factory &F) const {
  PrimRangeSet newRanges = F.getEmptySet();

  for (iterator i = begin(), e = end(); i != e; ++i) {
    const llvm::APSInt &from = i->From(), &to = i->To();
    const llvm::APSInt &newTo = (from.isMinSignedValue() ?
                                 BV.getMaxValue(from) :
                                 BV.getValue(- from));
    if (to.isMaxSignedValue() && !newRanges.isEmpty() &&
        newRanges.begin()->From().isMinSignedValue()) {
      assert(newRanges.begin()->To().isMinSignedValue() &&
             "Ranges should not overlap");
      assert(!from.isMinSignedValue() && "Ranges should not overlap");
      const llvm::APSInt &newFrom = newRanges.begin()->From();
      newRanges =
        F.add(F.remove(newRanges, *newRanges.begin()), Range(newFrom, newTo));
    } else if (!to.isMinSignedValue()) {
      const llvm::APSInt &newFrom = BV.getValue(- to);
      newRanges = F.add(newRanges, Range(newFrom, newTo));
    }
    if (from.isMinSignedValue()) {
      newRanges = F.add(newRanges, Range(BV.getMinValue(from),
                                         BV.getMinValue(from)));
    }
  }

  return newRanges;
}

void RangeSet::print(raw_ostream &os) const {
  bool isFirst = true;
  os << "{ ";
  for (iterator i = begin(), e = end(); i != e; ++i) {
    if (isFirst)
      isFirst = false;
    else
      os << ", ";

    os << '[' << i->From().toString(10) << ", " << i->To().toString(10)
       << ']';
  }
  os << " }";
}

namespace {
class RangeConstraintManager : public RangedConstraintManager {
public:
  RangeConstraintManager(SubEngine *SE, SValBuilder &SVB)
      : RangedConstraintManager(SE, SVB) {}

  //===------------------------------------------------------------------===//
  // Implementation for interface from ConstraintManager.
  //===------------------------------------------------------------------===//

  bool canReasonAbout(SVal X) const override;

  ConditionTruthVal checkNull(ProgramStateRef State, SymbolRef Sym) override;

  const llvm::APSInt *getSymVal(ProgramStateRef State,
                                SymbolRef Sym) const override;

  ProgramStateRef removeDeadBindings(ProgramStateRef State,
                                     SymbolReaper &SymReaper) override;

  void print(ProgramStateRef State, raw_ostream &Out, const char *nl,
             const char *sep) override;

  //===------------------------------------------------------------------===//
  // Implementation for interface from RangedConstraintManager.
  //===------------------------------------------------------------------===//

  ProgramStateRef assumeSymNE(ProgramStateRef State, SymbolRef Sym,
                              const llvm::APSInt &V,
                              const llvm::APSInt &Adjustment) override;

  ProgramStateRef assumeSymEQ(ProgramStateRef State, SymbolRef Sym,
                              const llvm::APSInt &V,
                              const llvm::APSInt &Adjustment) override;

  ProgramStateRef assumeSymLT(ProgramStateRef State, SymbolRef Sym,
                              const llvm::APSInt &V,
                              const llvm::APSInt &Adjustment) override;

  ProgramStateRef assumeSymGT(ProgramStateRef State, SymbolRef Sym,
                              const llvm::APSInt &V,
                              const llvm::APSInt &Adjustment) override;

  ProgramStateRef assumeSymLE(ProgramStateRef State, SymbolRef Sym,
                              const llvm::APSInt &V,
                              const llvm::APSInt &Adjustment) override;

  ProgramStateRef assumeSymGE(ProgramStateRef State, SymbolRef Sym,
                              const llvm::APSInt &V,
                              const llvm::APSInt &Adjustment) override;

  ProgramStateRef assumeSymWithinInclusiveRange(
      ProgramStateRef State, SymbolRef Sym, const llvm::APSInt &From,
      const llvm::APSInt &To, const llvm::APSInt &Adjustment) override;

  ProgramStateRef assumeSymOutsideInclusiveRange(
      ProgramStateRef State, SymbolRef Sym, const llvm::APSInt &From,
      const llvm::APSInt &To, const llvm::APSInt &Adjustment) override;

private:
  RangeSet::Factory F;

  RangeSet getRange(ProgramStateRef State, SymbolRef Sym);
  const RangeSet* getRangeForMinusSymbol(ProgramStateRef State,
                                         SymbolRef Sym);

  RangeSet getSymLTRange(ProgramStateRef St, SymbolRef Sym,
                         const llvm::APSInt &Int,
                         const llvm::APSInt &Adjustment);
  RangeSet getSymGTRange(ProgramStateRef St, SymbolRef Sym,
                         const llvm::APSInt &Int,
                         const llvm::APSInt &Adjustment);
  RangeSet getSymLERange(ProgramStateRef St, SymbolRef Sym,
                         const llvm::APSInt &Int,
                         const llvm::APSInt &Adjustment);
  RangeSet getSymLERange(llvm::function_ref<RangeSet()> RS,
                         const llvm::APSInt &Int,
                         const llvm::APSInt &Adjustment);
  RangeSet getSymGERange(ProgramStateRef St, SymbolRef Sym,
                         const llvm::APSInt &Int,
                         const llvm::APSInt &Adjustment);

};

} // end anonymous namespace

std::unique_ptr<ConstraintManager>
ento::CreateRangeConstraintManager(ProgramStateManager &StMgr, SubEngine *Eng) {
  return llvm::make_unique<RangeConstraintManager>(Eng, StMgr.getSValBuilder());
}

bool RangeConstraintManager::canReasonAbout(SVal X) const {
  Optional<nonloc::SymbolVal> SymVal = X.getAs<nonloc::SymbolVal>();
  if (SymVal && SymVal->isExpression()) {
    const SymExpr *SE = SymVal->getSymbol();

    if (const SymIntExpr *SIE = dyn_cast<SymIntExpr>(SE)) {
      switch (SIE->getOpcode()) {
      // We don't reason yet about bitwise-constraints on symbolic values.
      case BO_And:
      case BO_Or:
      case BO_Xor:
        return false;
      // We don't reason yet about these arithmetic constraints on
      // symbolic values.
      case BO_Mul:
      case BO_Div:
      case BO_Rem:
      case BO_Shl:
      case BO_Shr:
        return false;
      // All other cases.
      default:
        return true;
      }
    }

    if (const SymSymExpr *SSE = dyn_cast<SymSymExpr>(SE)) {
      // FIXME: Handle <=> here.
      if (BinaryOperator::isEqualityOp(SSE->getOpcode()) ||
          BinaryOperator::isRelationalOp(SSE->getOpcode())) {
        // We handle Loc <> Loc comparisons, but not (yet) NonLoc <> NonLoc.
        // We've recently started producing Loc <> NonLoc comparisons (that
        // result from casts of one of the operands between eg. intptr_t and
        // void *), but we can't reason about them yet.
        if (Loc::isLocType(SSE->getLHS()->getType())) {
          return Loc::isLocType(SSE->getRHS()->getType());
        }
      }
    }

    return false;
  }

  return true;
}

ConditionTruthVal RangeConstraintManager::checkNull(ProgramStateRef State,
                                                    SymbolRef Sym) {
  const RangeSet *Ranges = State->get<ConstraintRange>(Sym);

  // If we don't have any information about this symbol, it's underconstrained.
  if (!Ranges)
    return ConditionTruthVal();

  // If we have a concrete value, see if it's zero.
  if (const llvm::APSInt *Value = Ranges->getConcreteValue())
    return *Value == 0;

  BasicValueFactory &BV = getBasicVals();
  APSIntType IntType = BV.getAPSIntType(Sym->getType());
  llvm::APSInt Zero = IntType.getZeroValue();

  // Check if zero is in the set of possible values.
  if (Ranges->Intersect(BV, F, Zero, Zero).isEmpty())
    return false;

  // Zero is a possible value, but it is not the /only/ possible value.
  return ConditionTruthVal();
}

const llvm::APSInt *RangeConstraintManager::getSymVal(ProgramStateRef St,
                                                      SymbolRef Sym) const {
  const ConstraintRangeTy::data_type *T = St->get<ConstraintRange>(Sym);
  return T ? T->getConcreteValue() : nullptr;
}

/// Scan all symbols referenced by the constraints. If the symbol is not alive
/// as marked in LSymbols, mark it as dead in DSymbols.
ProgramStateRef
RangeConstraintManager::removeDeadBindings(ProgramStateRef State,
                                           SymbolReaper &SymReaper) {
  bool Changed = false;
  ConstraintRangeTy CR = State->get<ConstraintRange>();
  ConstraintRangeTy::Factory &CRFactory = State->get_context<ConstraintRange>();

  for (ConstraintRangeTy::iterator I = CR.begin(), E = CR.end(); I != E; ++I) {
    SymbolRef Sym = I.getKey();
    if (SymReaper.isDead(Sym)) {
      Changed = true;
      CR = CRFactory.remove(CR, Sym);
    }
  }

  return Changed ? State->set<ConstraintRange>(CR) : State;
}

/// Return a range set subtracting zero from \p Domain.
static RangeSet assumeNonZero(
    BasicValueFactory &BV,
    RangeSet::Factory &F,
    SymbolRef Sym,
    RangeSet Domain) {
  APSIntType IntType = BV.getAPSIntType(Sym->getType());
  return Domain.Intersect(BV, F, ++IntType.getZeroValue(),
      --IntType.getZeroValue());
}

/// Apply implicit constraints for bitwise OR- and AND-.
/// For unsigned types, bitwise OR with a constant always returns
/// a value greater-or-equal than the constant, and bitwise AND
/// returns a value less-or-equal then the constant.
///
/// Pattern matches the expression \p Sym against those rule,
/// and applies the required constraints.
/// \p Input Previously established expression range set
static RangeSet applyBitwiseConstraints(
    BasicValueFactory &BV,
    RangeSet::Factory &F,
    RangeSet Input,
    const SymIntExpr* SIE) {
  QualType T = SIE->getType();
  bool IsUnsigned = T->isUnsignedIntegerType();
  const llvm::APSInt &RHS = SIE->getRHS();
  const llvm::APSInt &Zero = BV.getAPSIntType(T).getZeroValue();
  BinaryOperator::Opcode Operator = SIE->getOpcode();

  // For unsigned types, the output of bitwise-or is bigger-or-equal than RHS.
  if (Operator == BO_Or && IsUnsigned)
    return Input.Intersect(BV, F, RHS, BV.getMaxValue(T));

  // Bitwise-or with a non-zero constant is always non-zero.
  if (Operator == BO_Or && RHS != Zero)
    return assumeNonZero(BV, F, SIE, Input);

  // For unsigned types, or positive RHS,
  // bitwise-and output is always smaller-or-equal than RHS (assuming two's
  // complement representation of signed types).
  if (Operator == BO_And && (IsUnsigned || RHS >= Zero))
    return Input.Intersect(BV, F, BV.getMinValue(T), RHS);

  return Input;
}

RangeSet RangeConstraintManager::getRange(ProgramStateRef State,
                                          SymbolRef Sym) {
  if (ConstraintRangeTy::data_type *V = State->get<ConstraintRange>(Sym))
    return *V;

  BasicValueFactory &BV = getBasicVals();

  // If Sym is a difference of symbols A - B, then maybe we have range set
  // stored for B - A.
  if (const RangeSet *R = getRangeForMinusSymbol(State, Sym))
    return R->Negate(BV, F);

  // Lazily generate a new RangeSet representing all possible values for the
  // given symbol type.
  QualType T = Sym->getType();

  RangeSet Result(F, BV.getMinValue(T), BV.getMaxValue(T));

  // References are known to be non-zero.
  if (T->isReferenceType())
    return assumeNonZero(BV, F, Sym, Result);

  // Known constraints on ranges of bitwise expressions.
  if (const SymIntExpr* SIE = dyn_cast<SymIntExpr>(Sym))
    return applyBitwiseConstraints(BV, F, Result, SIE);

  return Result;
}

// FIXME: Once SValBuilder supports unary minus, we should use SValBuilder to
//        obtain the negated symbolic expression instead of constructing the
//        symbol manually. This will allow us to support finding ranges of not
//        only negated SymSymExpr-type expressions, but also of other, simpler
//        expressions which we currently do not know how to negate.
const RangeSet*
RangeConstraintManager::getRangeForMinusSymbol(ProgramStateRef State,
                                               SymbolRef Sym) {
  if (const SymSymExpr *SSE = dyn_cast<SymSymExpr>(Sym)) {
    if (SSE->getOpcode() == BO_Sub) {
      QualType T = Sym->getType();
      SymbolManager &SymMgr = State->getSymbolManager();
      SymbolRef negSym = SymMgr.getSymSymExpr(SSE->getRHS(), BO_Sub,
                                              SSE->getLHS(), T);
      if (const RangeSet *negV = State->get<ConstraintRange>(negSym)) {
        // Unsigned range set cannot be negated, unless it is [0, 0].
        if ((negV->getConcreteValue() &&
             (*negV->getConcreteValue() == 0)) ||
            T->isSignedIntegerOrEnumerationType())
          return negV;
      }
    }
  }
  return nullptr;
}

//===------------------------------------------------------------------------===
// assumeSymX methods: protected interface for RangeConstraintManager.
//===------------------------------------------------------------------------===/

// The syntax for ranges below is mathematical, using [x, y] for closed ranges
// and (x, y) for open ranges. These ranges are modular, corresponding with
// a common treatment of C integer overflow. This means that these methods
// do not have to worry about overflow; RangeSet::Intersect can handle such a
// "wraparound" range.
// As an example, the range [UINT_MAX-1, 3) contains five values: UINT_MAX-1,
// UINT_MAX, 0, 1, and 2.

ProgramStateRef
RangeConstraintManager::assumeSymNE(ProgramStateRef St, SymbolRef Sym,
                                    const llvm::APSInt &Int,
                                    const llvm::APSInt &Adjustment) {
  // Before we do any real work, see if the value can even show up.
  APSIntType AdjustmentType(Adjustment);
  if (AdjustmentType.testInRange(Int, true) != APSIntType::RTR_Within)
    return St;

  llvm::APSInt Lower = AdjustmentType.convert(Int) - Adjustment;
  llvm::APSInt Upper = Lower;
  --Lower;
  ++Upper;

  // [Int-Adjustment+1, Int-Adjustment-1]
  // Notice that the lower bound is greater than the upper bound.
  RangeSet New = getRange(St, Sym).Intersect(getBasicVals(), F, Upper, Lower);
  return New.isEmpty() ? nullptr : St->set<ConstraintRange>(Sym, New);
}

ProgramStateRef
RangeConstraintManager::assumeSymEQ(ProgramStateRef St, SymbolRef Sym,
                                    const llvm::APSInt &Int,
                                    const llvm::APSInt &Adjustment) {
  // Before we do any real work, see if the value can even show up.
  APSIntType AdjustmentType(Adjustment);
  if (AdjustmentType.testInRange(Int, true) != APSIntType::RTR_Within)
    return nullptr;

  // [Int-Adjustment, Int-Adjustment]
  llvm::APSInt AdjInt = AdjustmentType.convert(Int) - Adjustment;
  RangeSet New = getRange(St, Sym).Intersect(getBasicVals(), F, AdjInt, AdjInt);
  return New.isEmpty() ? nullptr : St->set<ConstraintRange>(Sym, New);
}

RangeSet RangeConstraintManager::getSymLTRange(ProgramStateRef St,
                                               SymbolRef Sym,
                                               const llvm::APSInt &Int,
                                               const llvm::APSInt &Adjustment) {
  // Before we do any real work, see if the value can even show up.
  APSIntType AdjustmentType(Adjustment);
  switch (AdjustmentType.testInRange(Int, true)) {
  case APSIntType::RTR_Below:
    return F.getEmptySet();
  case APSIntType::RTR_Within:
    break;
  case APSIntType::RTR_Above:
    return getRange(St, Sym);
  }

  // Special case for Int == Min. This is always false.
  llvm::APSInt ComparisonVal = AdjustmentType.convert(Int);
  llvm::APSInt Min = AdjustmentType.getMinValue();
  if (ComparisonVal == Min)
    return F.getEmptySet();

  llvm::APSInt Lower = Min - Adjustment;
  llvm::APSInt Upper = ComparisonVal - Adjustment;
  --Upper;

  return getRange(St, Sym).Intersect(getBasicVals(), F, Lower, Upper);
}

ProgramStateRef
RangeConstraintManager::assumeSymLT(ProgramStateRef St, SymbolRef Sym,
                                    const llvm::APSInt &Int,
                                    const llvm::APSInt &Adjustment) {
  RangeSet New = getSymLTRange(St, Sym, Int, Adjustment);
  return New.isEmpty() ? nullptr : St->set<ConstraintRange>(Sym, New);
}

RangeSet RangeConstraintManager::getSymGTRange(ProgramStateRef St,
                                               SymbolRef Sym,
                                               const llvm::APSInt &Int,
                                               const llvm::APSInt &Adjustment) {
  // Before we do any real work, see if the value can even show up.
  APSIntType AdjustmentType(Adjustment);
  switch (AdjustmentType.testInRange(Int, true)) {
  case APSIntType::RTR_Below:
    return getRange(St, Sym);
  case APSIntType::RTR_Within:
    break;
  case APSIntType::RTR_Above:
    return F.getEmptySet();
  }

  // Special case for Int == Max. This is always false.
  llvm::APSInt ComparisonVal = AdjustmentType.convert(Int);
  llvm::APSInt Max = AdjustmentType.getMaxValue();
  if (ComparisonVal == Max)
    return F.getEmptySet();

  llvm::APSInt Lower = ComparisonVal - Adjustment;
  llvm::APSInt Upper = Max - Adjustment;
  ++Lower;

  return getRange(St, Sym).Intersect(getBasicVals(), F, Lower, Upper);
}

ProgramStateRef
RangeConstraintManager::assumeSymGT(ProgramStateRef St, SymbolRef Sym,
                                    const llvm::APSInt &Int,
                                    const llvm::APSInt &Adjustment) {
  RangeSet New = getSymGTRange(St, Sym, Int, Adjustment);
  return New.isEmpty() ? nullptr : St->set<ConstraintRange>(Sym, New);
}

RangeSet RangeConstraintManager::getSymGERange(ProgramStateRef St,
                                               SymbolRef Sym,
                                               const llvm::APSInt &Int,
                                               const llvm::APSInt &Adjustment) {
  // Before we do any real work, see if the value can even show up.
  APSIntType AdjustmentType(Adjustment);
  switch (AdjustmentType.testInRange(Int, true)) {
  case APSIntType::RTR_Below:
    return getRange(St, Sym);
  case APSIntType::RTR_Within:
    break;
  case APSIntType::RTR_Above:
    return F.getEmptySet();
  }

  // Special case for Int == Min. This is always feasible.
  llvm::APSInt ComparisonVal = AdjustmentType.convert(Int);
  llvm::APSInt Min = AdjustmentType.getMinValue();
  if (ComparisonVal == Min)
    return getRange(St, Sym);

  llvm::APSInt Max = AdjustmentType.getMaxValue();
  llvm::APSInt Lower = ComparisonVal - Adjustment;
  llvm::APSInt Upper = Max - Adjustment;

  return getRange(St, Sym).Intersect(getBasicVals(), F, Lower, Upper);
}

ProgramStateRef
RangeConstraintManager::assumeSymGE(ProgramStateRef St, SymbolRef Sym,
                                    const llvm::APSInt &Int,
                                    const llvm::APSInt &Adjustment) {
  RangeSet New = getSymGERange(St, Sym, Int, Adjustment);
  return New.isEmpty() ? nullptr : St->set<ConstraintRange>(Sym, New);
}

RangeSet RangeConstraintManager::getSymLERange(
      llvm::function_ref<RangeSet()> RS,
      const llvm::APSInt &Int,
      const llvm::APSInt &Adjustment) {
  // Before we do any real work, see if the value can even show up.
  APSIntType AdjustmentType(Adjustment);
  switch (AdjustmentType.testInRange(Int, true)) {
  case APSIntType::RTR_Below:
    return F.getEmptySet();
  case APSIntType::RTR_Within:
    break;
  case APSIntType::RTR_Above:
    return RS();
  }

  // Special case for Int == Max. This is always feasible.
  llvm::APSInt ComparisonVal = AdjustmentType.convert(Int);
  llvm::APSInt Max = AdjustmentType.getMaxValue();
  if (ComparisonVal == Max)
    return RS();

  llvm::APSInt Min = AdjustmentType.getMinValue();
  llvm::APSInt Lower = Min - Adjustment;
  llvm::APSInt Upper = ComparisonVal - Adjustment;

  return RS().Intersect(getBasicVals(), F, Lower, Upper);
}

RangeSet RangeConstraintManager::getSymLERange(ProgramStateRef St,
                                               SymbolRef Sym,
                                               const llvm::APSInt &Int,
                                               const llvm::APSInt &Adjustment) {
  return getSymLERange([&] { return getRange(St, Sym); }, Int, Adjustment);
}

ProgramStateRef
RangeConstraintManager::assumeSymLE(ProgramStateRef St, SymbolRef Sym,
                                    const llvm::APSInt &Int,
                                    const llvm::APSInt &Adjustment) {
  RangeSet New = getSymLERange(St, Sym, Int, Adjustment);
  return New.isEmpty() ? nullptr : St->set<ConstraintRange>(Sym, New);
}

ProgramStateRef RangeConstraintManager::assumeSymWithinInclusiveRange(
    ProgramStateRef State, SymbolRef Sym, const llvm::APSInt &From,
    const llvm::APSInt &To, const llvm::APSInt &Adjustment) {
  RangeSet New = getSymGERange(State, Sym, From, Adjustment);
  if (New.isEmpty())
    return nullptr;
  RangeSet Out = getSymLERange([&] { return New; }, To, Adjustment);
  return Out.isEmpty() ? nullptr : State->set<ConstraintRange>(Sym, Out);
}

ProgramStateRef RangeConstraintManager::assumeSymOutsideInclusiveRange(
    ProgramStateRef State, SymbolRef Sym, const llvm::APSInt &From,
    const llvm::APSInt &To, const llvm::APSInt &Adjustment) {
  RangeSet RangeLT = getSymLTRange(State, Sym, From, Adjustment);
  RangeSet RangeGT = getSymGTRange(State, Sym, To, Adjustment);
  RangeSet New(RangeLT.addRange(F, RangeGT));
  return New.isEmpty() ? nullptr : State->set<ConstraintRange>(Sym, New);
}

//===------------------------------------------------------------------------===
// Pretty-printing.
//===------------------------------------------------------------------------===/

void RangeConstraintManager::print(ProgramStateRef St, raw_ostream &Out,
                                   const char *nl, const char *sep) {

  ConstraintRangeTy Ranges = St->get<ConstraintRange>();

  if (Ranges.isEmpty()) {
    Out << nl << sep << "Ranges are empty." << nl;
    return;
  }

  Out << nl << sep << "Ranges of symbol values:";
  for (ConstraintRangeTy::iterator I = Ranges.begin(), E = Ranges.end(); I != E;
       ++I) {
    Out << nl << ' ' << I.getKey() << " : ";
    I.getData().print(Out);
  }
  Out << nl;
}
