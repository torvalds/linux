//===- RValues.cpp - Abstract RValues for Path-Sens. Value Tracking -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines SVal, Loc, and NonLoc, classes that represent
//  abstract r-values for use with path-sensitive value tracking.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/BasicValueFactory.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SValBuilder.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymExpr.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"
#include "llvm/ADT/Optional.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace clang;
using namespace ento;

//===----------------------------------------------------------------------===//
// Symbol iteration within an SVal.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Utility methods.
//===----------------------------------------------------------------------===//

bool SVal::hasConjuredSymbol() const {
  if (Optional<nonloc::SymbolVal> SV = getAs<nonloc::SymbolVal>()) {
    SymbolRef sym = SV->getSymbol();
    if (isa<SymbolConjured>(sym))
      return true;
  }

  if (Optional<loc::MemRegionVal> RV = getAs<loc::MemRegionVal>()) {
    const MemRegion *R = RV->getRegion();
    if (const auto *SR = dyn_cast<SymbolicRegion>(R)) {
      SymbolRef sym = SR->getSymbol();
      if (isa<SymbolConjured>(sym))
        return true;
    }
  }

  return false;
}

const FunctionDecl *SVal::getAsFunctionDecl() const {
  if (Optional<loc::MemRegionVal> X = getAs<loc::MemRegionVal>()) {
    const MemRegion* R = X->getRegion();
    if (const FunctionCodeRegion *CTR = R->getAs<FunctionCodeRegion>())
      if (const auto *FD = dyn_cast<FunctionDecl>(CTR->getDecl()))
        return FD;
  }

  if (auto X = getAs<nonloc::PointerToMember>()) {
    if (const auto *MD = dyn_cast_or_null<CXXMethodDecl>(X->getDecl()))
      return MD;
  }
  return nullptr;
}

/// If this SVal is a location (subclasses Loc) and wraps a symbol,
/// return that SymbolRef.  Otherwise return 0.
///
/// Implicit casts (ex: void* -> char*) can turn Symbolic region into Element
/// region. If that is the case, gets the underlining region.
/// When IncludeBaseRegions is set to true and the SubRegion is non-symbolic,
/// the first symbolic parent region is returned.
SymbolRef SVal::getAsLocSymbol(bool IncludeBaseRegions) const {
  // FIXME: should we consider SymbolRef wrapped in CodeTextRegion?
  if (Optional<nonloc::LocAsInteger> X = getAs<nonloc::LocAsInteger>())
    return X->getLoc().getAsLocSymbol(IncludeBaseRegions);

  if (Optional<loc::MemRegionVal> X = getAs<loc::MemRegionVal>()) {
    const MemRegion *R = X->getRegion();
    if (const SymbolicRegion *SymR = IncludeBaseRegions ?
                                      R->getSymbolicBase() :
                                      dyn_cast<SymbolicRegion>(R->StripCasts()))
      return SymR->getSymbol();
  }
  return nullptr;
}

/// Get the symbol in the SVal or its base region.
SymbolRef SVal::getLocSymbolInBase() const {
  Optional<loc::MemRegionVal> X = getAs<loc::MemRegionVal>();

  if (!X)
    return nullptr;

  const MemRegion *R = X->getRegion();

  while (const auto *SR = dyn_cast<SubRegion>(R)) {
    if (const auto *SymR = dyn_cast<SymbolicRegion>(SR))
      return SymR->getSymbol();
    else
      R = SR->getSuperRegion();
  }

  return nullptr;
}

// TODO: The next 3 functions have to be simplified.

/// If this SVal wraps a symbol return that SymbolRef.
/// Otherwise, return 0.
///
/// Casts are ignored during lookup.
/// \param IncludeBaseRegions The boolean that controls whether the search
/// should continue to the base regions if the region is not symbolic.
SymbolRef SVal::getAsSymbol(bool IncludeBaseRegions) const {
  // FIXME: should we consider SymbolRef wrapped in CodeTextRegion?
  if (Optional<nonloc::SymbolVal> X = getAs<nonloc::SymbolVal>())
    return X->getSymbol();

  return getAsLocSymbol(IncludeBaseRegions);
}

/// getAsSymbolicExpression - If this Sval wraps a symbolic expression then
///  return that expression.  Otherwise return NULL.
const SymExpr *SVal::getAsSymbolicExpression() const {
  if (Optional<nonloc::SymbolVal> X = getAs<nonloc::SymbolVal>())
    return X->getSymbol();

  return getAsSymbol();
}

const SymExpr* SVal::getAsSymExpr() const {
  const SymExpr* Sym = getAsSymbol();
  if (!Sym)
    Sym = getAsSymbolicExpression();
  return Sym;
}

const MemRegion *SVal::getAsRegion() const {
  if (Optional<loc::MemRegionVal> X = getAs<loc::MemRegionVal>())
    return X->getRegion();

  if (Optional<nonloc::LocAsInteger> X = getAs<nonloc::LocAsInteger>())
    return X->getLoc().getAsRegion();

  return nullptr;
}

const MemRegion *loc::MemRegionVal::stripCasts(bool StripBaseCasts) const {
  const MemRegion *R = getRegion();
  return R ?  R->StripCasts(StripBaseCasts) : nullptr;
}

const void *nonloc::LazyCompoundVal::getStore() const {
  return static_cast<const LazyCompoundValData*>(Data)->getStore();
}

const TypedValueRegion *nonloc::LazyCompoundVal::getRegion() const {
  return static_cast<const LazyCompoundValData*>(Data)->getRegion();
}

bool nonloc::PointerToMember::isNullMemberPointer() const {
  return getPTMData().isNull();
}

const DeclaratorDecl *nonloc::PointerToMember::getDecl() const {
  const auto PTMD = this->getPTMData();
  if (PTMD.isNull())
    return nullptr;

  const DeclaratorDecl *DD = nullptr;
  if (PTMD.is<const DeclaratorDecl *>())
    DD = PTMD.get<const DeclaratorDecl *>();
  else
    DD = PTMD.get<const PointerToMemberData *>()->getDeclaratorDecl();

  return DD;
}

//===----------------------------------------------------------------------===//
// Other Iterators.
//===----------------------------------------------------------------------===//

nonloc::CompoundVal::iterator nonloc::CompoundVal::begin() const {
  return getValue()->begin();
}

nonloc::CompoundVal::iterator nonloc::CompoundVal::end() const {
  return getValue()->end();
}

nonloc::PointerToMember::iterator nonloc::PointerToMember::begin() const {
  const PTMDataType PTMD = getPTMData();
  if (PTMD.is<const DeclaratorDecl *>())
    return {};
  return PTMD.get<const PointerToMemberData *>()->begin();
}

nonloc::PointerToMember::iterator nonloc::PointerToMember::end() const {
  const PTMDataType PTMD = getPTMData();
  if (PTMD.is<const DeclaratorDecl *>())
    return {};
  return PTMD.get<const PointerToMemberData *>()->end();
}

//===----------------------------------------------------------------------===//
// Useful predicates.
//===----------------------------------------------------------------------===//

bool SVal::isConstant() const {
  return getAs<nonloc::ConcreteInt>() || getAs<loc::ConcreteInt>();
}

bool SVal::isConstant(int I) const {
  if (Optional<loc::ConcreteInt> LV = getAs<loc::ConcreteInt>())
    return LV->getValue() == I;
  if (Optional<nonloc::ConcreteInt> NV = getAs<nonloc::ConcreteInt>())
    return NV->getValue() == I;
  return false;
}

bool SVal::isZeroConstant() const {
  return isConstant(0);
}

//===----------------------------------------------------------------------===//
// Transfer function dispatch for Non-Locs.
//===----------------------------------------------------------------------===//

SVal nonloc::ConcreteInt::evalBinOp(SValBuilder &svalBuilder,
                                    BinaryOperator::Opcode Op,
                                    const nonloc::ConcreteInt& R) const {
  const llvm::APSInt* X =
    svalBuilder.getBasicValueFactory().evalAPSInt(Op, getValue(), R.getValue());

  if (X)
    return nonloc::ConcreteInt(*X);
  else
    return UndefinedVal();
}

nonloc::ConcreteInt
nonloc::ConcreteInt::evalComplement(SValBuilder &svalBuilder) const {
  return svalBuilder.makeIntVal(~getValue());
}

nonloc::ConcreteInt
nonloc::ConcreteInt::evalMinus(SValBuilder &svalBuilder) const {
  return svalBuilder.makeIntVal(-getValue());
}

//===----------------------------------------------------------------------===//
// Transfer function dispatch for Locs.
//===----------------------------------------------------------------------===//

SVal loc::ConcreteInt::evalBinOp(BasicValueFactory& BasicVals,
                                 BinaryOperator::Opcode Op,
                                 const loc::ConcreteInt& R) const {
  assert(BinaryOperator::isComparisonOp(Op) || Op == BO_Sub);

  const llvm::APSInt *X = BasicVals.evalAPSInt(Op, getValue(), R.getValue());

  if (X)
    return nonloc::ConcreteInt(*X);
  else
    return UndefinedVal();
}

//===----------------------------------------------------------------------===//
// Pretty-Printing.
//===----------------------------------------------------------------------===//

LLVM_DUMP_METHOD void SVal::dump() const { dumpToStream(llvm::errs()); }

void SVal::dumpToStream(raw_ostream &os) const {
  switch (getBaseKind()) {
    case UnknownValKind:
      os << "Unknown";
      break;
    case NonLocKind:
      castAs<NonLoc>().dumpToStream(os);
      break;
    case LocKind:
      castAs<Loc>().dumpToStream(os);
      break;
    case UndefinedValKind:
      os << "Undefined";
      break;
  }
}

void NonLoc::dumpToStream(raw_ostream &os) const {
  switch (getSubKind()) {
    case nonloc::ConcreteIntKind: {
      const auto &Value = castAs<nonloc::ConcreteInt>().getValue();
      os << Value << ' ' << (Value.isSigned() ? 'S' : 'U')
         << Value.getBitWidth() << 'b';
      break;
    }
    case nonloc::SymbolValKind:
      os << castAs<nonloc::SymbolVal>().getSymbol();
      break;

    case nonloc::LocAsIntegerKind: {
      const nonloc::LocAsInteger& C = castAs<nonloc::LocAsInteger>();
      os << C.getLoc() << " [as " << C.getNumBits() << " bit integer]";
      break;
    }
    case nonloc::CompoundValKind: {
      const nonloc::CompoundVal& C = castAs<nonloc::CompoundVal>();
      os << "compoundVal{";
      bool first = true;
      for (const auto &I : C) {
        if (first) {
          os << ' '; first = false;
        }
        else
          os << ", ";

        I.dumpToStream(os);
      }
      os << "}";
      break;
    }
    case nonloc::LazyCompoundValKind: {
      const nonloc::LazyCompoundVal &C = castAs<nonloc::LazyCompoundVal>();
      os << "lazyCompoundVal{" << const_cast<void *>(C.getStore())
         << ',' << C.getRegion()
         << '}';
      break;
    }
    case nonloc::PointerToMemberKind: {
      os << "pointerToMember{";
      const nonloc::PointerToMember &CastRes =
          castAs<nonloc::PointerToMember>();
      if (CastRes.getDecl())
        os << "|" << CastRes.getDecl()->getQualifiedNameAsString() << "|";
      bool first = true;
      for (const auto &I : CastRes) {
        if (first) {
          os << ' '; first = false;
        }
        else
          os << ", ";

        os << (*I).getType().getAsString();
      }

      os << '}';
      break;
    }
    default:
      assert(false && "Pretty-printed not implemented for this NonLoc.");
      break;
  }
}

void Loc::dumpToStream(raw_ostream &os) const {
  switch (getSubKind()) {
    case loc::ConcreteIntKind:
      os << castAs<loc::ConcreteInt>().getValue().getZExtValue() << " (Loc)";
      break;
    case loc::GotoLabelKind:
      os << "&&" << castAs<loc::GotoLabel>().getLabel()->getName();
      break;
    case loc::MemRegionValKind:
      os << '&' << castAs<loc::MemRegionVal>().getRegion()->getString();
      break;
    default:
      llvm_unreachable("Pretty-printing not implemented for this Loc.");
  }
}
