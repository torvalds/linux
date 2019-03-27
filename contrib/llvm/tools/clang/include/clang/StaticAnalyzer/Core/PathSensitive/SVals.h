//===- SVals.h - Abstract Values for Static Analysis ------------*- C++ -*-===//
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

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SVALS_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SVALS_H

#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymExpr.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/ImmutableList.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <cstdint>
#include <utility>

//==------------------------------------------------------------------------==//
//  Base SVal types.
//==------------------------------------------------------------------------==//

namespace clang {

class CXXBaseSpecifier;
class DeclaratorDecl;
class FunctionDecl;
class LabelDecl;

namespace ento {

class BasicValueFactory;
class CompoundValData;
class LazyCompoundValData;
class MemRegion;
class PointerToMemberData;
class SValBuilder;
class TypedValueRegion;

namespace nonloc {

/// Sub-kinds for NonLoc values.
enum Kind {
#define NONLOC_SVAL(Id, Parent) Id ## Kind,
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.def"
};

} // namespace nonloc

namespace loc {

/// Sub-kinds for Loc values.
enum Kind {
#define LOC_SVAL(Id, Parent) Id ## Kind,
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.def"
};

} // namespace loc

/// SVal - This represents a symbolic expression, which can be either
///  an L-value or an R-value.
///
class SVal {
public:
  enum BaseKind {
    // The enumerators must be representable using 2 bits.
#define BASIC_SVAL(Id, Parent) Id ## Kind,
#define ABSTRACT_SVAL_WITH_KIND(Id, Parent) Id ## Kind,
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.def"
  };
  enum { BaseBits = 2, BaseMask = 0x3 };

protected:
  const void *Data = nullptr;

  /// The lowest 2 bits are a BaseKind (0 -- 3).
  ///  The higher bits are an unsigned "kind" value.
  unsigned Kind = 0;

  explicit SVal(const void *d, bool isLoc, unsigned ValKind)
      : Data(d), Kind((isLoc ? LocKind : NonLocKind) | (ValKind << BaseBits)) {}

  explicit SVal(BaseKind k, const void *D = nullptr) : Data(D), Kind(k) {}

public:
  explicit SVal() = default;

  /// Convert to the specified SVal type, asserting that this SVal is of
  /// the desired type.
  template<typename T>
  T castAs() const {
    assert(T::isKind(*this));
    return *static_cast<const T *>(this);
  }

  /// Convert to the specified SVal type, returning None if this SVal is
  /// not of the desired type.
  template<typename T>
  Optional<T> getAs() const {
    if (!T::isKind(*this))
      return None;
    return *static_cast<const T *>(this);
  }

  unsigned getRawKind() const { return Kind; }
  BaseKind getBaseKind() const { return (BaseKind) (Kind & BaseMask); }
  unsigned getSubKind() const { return (Kind & ~BaseMask) >> BaseBits; }

  // This method is required for using SVal in a FoldingSetNode.  It
  // extracts a unique signature for this SVal object.
  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddInteger((unsigned) getRawKind());
    ID.AddPointer(Data);
  }

  bool operator==(const SVal &R) const {
    return getRawKind() == R.getRawKind() && Data == R.Data;
  }

  bool operator!=(const SVal &R) const {
    return !(*this == R);
  }

  bool isUnknown() const {
    return getRawKind() == UnknownValKind;
  }

  bool isUndef() const {
    return getRawKind() == UndefinedValKind;
  }

  bool isUnknownOrUndef() const {
    return getRawKind() <= UnknownValKind;
  }

  bool isValid() const {
    return getRawKind() > UnknownValKind;
  }

  bool isConstant() const;

  bool isConstant(int I) const;

  bool isZeroConstant() const;

  /// hasConjuredSymbol - If this SVal wraps a conjured symbol, return true;
  bool hasConjuredSymbol() const;

  /// getAsFunctionDecl - If this SVal is a MemRegionVal and wraps a
  /// CodeTextRegion wrapping a FunctionDecl, return that FunctionDecl.
  /// Otherwise return 0.
  const FunctionDecl *getAsFunctionDecl() const;

  /// If this SVal is a location and wraps a symbol, return that
  ///  SymbolRef. Otherwise return 0.
  ///
  /// Casts are ignored during lookup.
  /// \param IncludeBaseRegions The boolean that controls whether the search
  /// should continue to the base regions if the region is not symbolic.
  SymbolRef getAsLocSymbol(bool IncludeBaseRegions = false) const;

  /// Get the symbol in the SVal or its base region.
  SymbolRef getLocSymbolInBase() const;

  /// If this SVal wraps a symbol return that SymbolRef.
  /// Otherwise, return 0.
  ///
  /// Casts are ignored during lookup.
  /// \param IncludeBaseRegions The boolean that controls whether the search
  /// should continue to the base regions if the region is not symbolic.
  SymbolRef getAsSymbol(bool IncludeBaseRegions = false) const;

  /// getAsSymbolicExpression - If this Sval wraps a symbolic expression then
  ///  return that expression.  Otherwise return NULL.
  const SymExpr *getAsSymbolicExpression() const;

  const SymExpr *getAsSymExpr() const;

  const MemRegion *getAsRegion() const;

  void dumpToStream(raw_ostream &OS) const;
  void dump() const;

  SymExpr::symbol_iterator symbol_begin() const {
    const SymExpr *SE = getAsSymbol(/*IncludeBaseRegions=*/true);
    if (SE)
      return SE->symbol_begin();
    else
      return SymExpr::symbol_iterator();
  }

  SymExpr::symbol_iterator symbol_end() const {
    return SymExpr::symbol_end();
  }
};

inline raw_ostream &operator<<(raw_ostream &os, clang::ento::SVal V) {
  V.dumpToStream(os);
  return os;
}

class UndefinedVal : public SVal {
public:
  UndefinedVal() : SVal(UndefinedValKind) {}

private:
  friend class SVal;

  static bool isKind(const SVal& V) {
    return V.getBaseKind() == UndefinedValKind;
  }
};

class DefinedOrUnknownSVal : public SVal {
public:
  // We want calling these methods to be a compiler error since they are
  // tautologically false.
  bool isUndef() const = delete;
  bool isValid() const = delete;

protected:
  DefinedOrUnknownSVal() = default;
  explicit DefinedOrUnknownSVal(const void *d, bool isLoc, unsigned ValKind)
      : SVal(d, isLoc, ValKind) {}
  explicit DefinedOrUnknownSVal(BaseKind k, void *D = nullptr) : SVal(k, D) {}

private:
  friend class SVal;

  static bool isKind(const SVal& V) {
    return !V.isUndef();
  }
};

class UnknownVal : public DefinedOrUnknownSVal {
public:
  explicit UnknownVal() : DefinedOrUnknownSVal(UnknownValKind) {}

private:
  friend class SVal;

  static bool isKind(const SVal &V) {
    return V.getBaseKind() == UnknownValKind;
  }
};

class DefinedSVal : public DefinedOrUnknownSVal {
public:
  // We want calling these methods to be a compiler error since they are
  // tautologically true/false.
  bool isUnknown() const = delete;
  bool isUnknownOrUndef() const = delete;
  bool isValid() const = delete;

protected:
  DefinedSVal() = default;
  explicit DefinedSVal(const void *d, bool isLoc, unsigned ValKind)
      : DefinedOrUnknownSVal(d, isLoc, ValKind) {}

private:
  friend class SVal;

  static bool isKind(const SVal& V) {
    return !V.isUnknownOrUndef();
  }
};

/// Represents an SVal that is guaranteed to not be UnknownVal.
class KnownSVal : public SVal {
  friend class SVal;

  KnownSVal() = default;

  static bool isKind(const SVal &V) {
    return !V.isUnknown();
  }

public:
  KnownSVal(const DefinedSVal &V) : SVal(V) {}
  KnownSVal(const UndefinedVal &V) : SVal(V) {}
};

class NonLoc : public DefinedSVal {
protected:
  NonLoc() = default;
  explicit NonLoc(unsigned SubKind, const void *d)
      : DefinedSVal(d, false, SubKind) {}

public:
  void dumpToStream(raw_ostream &Out) const;

  static bool isCompoundType(QualType T) {
    return T->isArrayType() || T->isRecordType() ||
           T->isComplexType() || T->isVectorType();
  }

private:
  friend class SVal;

  static bool isKind(const SVal& V) {
    return V.getBaseKind() == NonLocKind;
  }
};

class Loc : public DefinedSVal {
protected:
  Loc() = default;
  explicit Loc(unsigned SubKind, const void *D)
      : DefinedSVal(const_cast<void *>(D), true, SubKind) {}

public:
  void dumpToStream(raw_ostream &Out) const;

  static bool isLocType(QualType T) {
    return T->isAnyPointerType() || T->isBlockPointerType() ||
           T->isReferenceType() || T->isNullPtrType();
  }

private:
  friend class SVal;

  static bool isKind(const SVal& V) {
    return V.getBaseKind() == LocKind;
  }
};

//==------------------------------------------------------------------------==//
//  Subclasses of NonLoc.
//==------------------------------------------------------------------------==//

namespace nonloc {

/// Represents symbolic expression that isn't a location.
class SymbolVal : public NonLoc {
public:
  SymbolVal() = delete;
  SymbolVal(SymbolRef sym) : NonLoc(SymbolValKind, sym) {
    assert(sym);
    assert(!Loc::isLocType(sym->getType()));
  }

  SymbolRef getSymbol() const {
    return (const SymExpr *) Data;
  }

  bool isExpression() const {
    return !isa<SymbolData>(getSymbol());
  }

private:
  friend class SVal;

  static bool isKind(const SVal& V) {
    return V.getBaseKind() == NonLocKind &&
           V.getSubKind() == SymbolValKind;
  }

  static bool isKind(const NonLoc& V) {
    return V.getSubKind() == SymbolValKind;
  }
};

/// Value representing integer constant.
class ConcreteInt : public NonLoc {
public:
  explicit ConcreteInt(const llvm::APSInt& V) : NonLoc(ConcreteIntKind, &V) {}

  const llvm::APSInt& getValue() const {
    return *static_cast<const llvm::APSInt *>(Data);
  }

  // Transfer functions for binary/unary operations on ConcreteInts.
  SVal evalBinOp(SValBuilder &svalBuilder, BinaryOperator::Opcode Op,
                 const ConcreteInt& R) const;

  ConcreteInt evalComplement(SValBuilder &svalBuilder) const;

  ConcreteInt evalMinus(SValBuilder &svalBuilder) const;

private:
  friend class SVal;

  ConcreteInt() = default;

  static bool isKind(const SVal& V) {
    return V.getBaseKind() == NonLocKind &&
           V.getSubKind() == ConcreteIntKind;
  }

  static bool isKind(const NonLoc& V) {
    return V.getSubKind() == ConcreteIntKind;
  }
};

class LocAsInteger : public NonLoc {
  friend class ento::SValBuilder;

  explicit LocAsInteger(const std::pair<SVal, uintptr_t> &data)
      : NonLoc(LocAsIntegerKind, &data) {
    // We do not need to represent loc::ConcreteInt as LocAsInteger,
    // as it'd collapse into a nonloc::ConcreteInt instead.
    assert(data.first.getBaseKind() == LocKind &&
           (data.first.getSubKind() == loc::MemRegionValKind ||
            data.first.getSubKind() == loc::GotoLabelKind));
  }

public:
  Loc getLoc() const {
    const std::pair<SVal, uintptr_t> *D =
      static_cast<const std::pair<SVal, uintptr_t> *>(Data);
    return D->first.castAs<Loc>();
  }

  Loc getPersistentLoc() const {
    const std::pair<SVal, uintptr_t> *D =
      static_cast<const std::pair<SVal, uintptr_t> *>(Data);
    const SVal& V = D->first;
    return V.castAs<Loc>();
  }

  unsigned getNumBits() const {
    const std::pair<SVal, uintptr_t> *D =
      static_cast<const std::pair<SVal, uintptr_t> *>(Data);
    return D->second;
  }

private:
  friend class SVal;

  LocAsInteger() = default;

  static bool isKind(const SVal& V) {
    return V.getBaseKind() == NonLocKind &&
           V.getSubKind() == LocAsIntegerKind;
  }

  static bool isKind(const NonLoc& V) {
    return V.getSubKind() == LocAsIntegerKind;
  }
};

class CompoundVal : public NonLoc {
  friend class ento::SValBuilder;

  explicit CompoundVal(const CompoundValData* D) : NonLoc(CompoundValKind, D) {}

public:
  const CompoundValData* getValue() const {
    return static_cast<const CompoundValData *>(Data);
  }

  using iterator = llvm::ImmutableList<SVal>::iterator;

  iterator begin() const;
  iterator end() const;

private:
  friend class SVal;

  CompoundVal() = default;

  static bool isKind(const SVal& V) {
    return V.getBaseKind() == NonLocKind && V.getSubKind() == CompoundValKind;
  }

  static bool isKind(const NonLoc& V) {
    return V.getSubKind() == CompoundValKind;
  }
};

class LazyCompoundVal : public NonLoc {
  friend class ento::SValBuilder;

  explicit LazyCompoundVal(const LazyCompoundValData *D)
      : NonLoc(LazyCompoundValKind, D) {}

public:
  const LazyCompoundValData *getCVData() const {
    return static_cast<const LazyCompoundValData *>(Data);
  }

  const void *getStore() const;
  const TypedValueRegion *getRegion() const;

private:
  friend class SVal;

  LazyCompoundVal() = default;

  static bool isKind(const SVal& V) {
    return V.getBaseKind() == NonLocKind &&
           V.getSubKind() == LazyCompoundValKind;
  }

  static bool isKind(const NonLoc& V) {
    return V.getSubKind() == LazyCompoundValKind;
  }
};

/// Value representing pointer-to-member.
///
/// This value is qualified as NonLoc because neither loading nor storing
/// operations are applied to it. Instead, the analyzer uses the L-value coming
/// from pointer-to-member applied to an object.
/// This SVal is represented by a DeclaratorDecl which can be a member function
/// pointer or a member data pointer and a list of CXXBaseSpecifiers. This list
/// is required to accumulate the pointer-to-member cast history to figure out
/// the correct subobject field.
class PointerToMember : public NonLoc {
  friend class ento::SValBuilder;

public:
  using PTMDataType =
      llvm::PointerUnion<const DeclaratorDecl *, const PointerToMemberData *>;

  const PTMDataType getPTMData() const {
    return PTMDataType::getFromOpaqueValue(const_cast<void *>(Data));
  }

  bool isNullMemberPointer() const;

  const DeclaratorDecl *getDecl() const;

  template<typename AdjustedDecl>
  const AdjustedDecl *getDeclAs() const {
    return dyn_cast_or_null<AdjustedDecl>(getDecl());
  }

  using iterator = llvm::ImmutableList<const CXXBaseSpecifier *>::iterator;

  iterator begin() const;
  iterator end() const;

private:
  friend class SVal;

  PointerToMember() = default;
  explicit PointerToMember(const PTMDataType D)
      : NonLoc(PointerToMemberKind, D.getOpaqueValue()) {}

  static bool isKind(const SVal& V) {
    return V.getBaseKind() == NonLocKind &&
           V.getSubKind() == PointerToMemberKind;
  }

  static bool isKind(const NonLoc& V) {
    return V.getSubKind() == PointerToMemberKind;
  }
};

} // namespace nonloc

//==------------------------------------------------------------------------==//
//  Subclasses of Loc.
//==------------------------------------------------------------------------==//

namespace loc {

class GotoLabel : public Loc {
public:
  explicit GotoLabel(const LabelDecl *Label) : Loc(GotoLabelKind, Label) {
    assert(Label);
  }

  const LabelDecl *getLabel() const {
    return static_cast<const LabelDecl *>(Data);
  }

private:
  friend class SVal;

  GotoLabel() = default;

  static bool isKind(const SVal& V) {
    return V.getBaseKind() == LocKind && V.getSubKind() == GotoLabelKind;
  }

  static bool isKind(const Loc& V) {
    return V.getSubKind() == GotoLabelKind;
  }
};

class MemRegionVal : public Loc {
public:
  explicit MemRegionVal(const MemRegion* r) : Loc(MemRegionValKind, r) {
    assert(r);
  }

  /// Get the underlining region.
  const MemRegion *getRegion() const {
    return static_cast<const MemRegion *>(Data);
  }

  /// Get the underlining region and strip casts.
  const MemRegion* stripCasts(bool StripBaseCasts = true) const;

  template <typename REGION>
  const REGION* getRegionAs() const {
    return dyn_cast<REGION>(getRegion());
  }

  bool operator==(const MemRegionVal &R) const {
    return getRegion() == R.getRegion();
  }

  bool operator!=(const MemRegionVal &R) const {
    return getRegion() != R.getRegion();
  }

private:
  friend class SVal;

  MemRegionVal() = default;

  static bool isKind(const SVal& V) {
    return V.getBaseKind() == LocKind &&
           V.getSubKind() == MemRegionValKind;
  }

  static bool isKind(const Loc& V) {
    return V.getSubKind() == MemRegionValKind;
  }
};

class ConcreteInt : public Loc {
public:
  explicit ConcreteInt(const llvm::APSInt& V) : Loc(ConcreteIntKind, &V) {}

  const llvm::APSInt &getValue() const {
    return *static_cast<const llvm::APSInt *>(Data);
  }

  // Transfer functions for binary/unary operations on ConcreteInts.
  SVal evalBinOp(BasicValueFactory& BasicVals, BinaryOperator::Opcode Op,
                 const ConcreteInt& R) const;

private:
  friend class SVal;

  ConcreteInt() = default;

  static bool isKind(const SVal& V) {
    return V.getBaseKind() == LocKind &&
           V.getSubKind() == ConcreteIntKind;
  }

  static bool isKind(const Loc& V) {
    return V.getSubKind() == ConcreteIntKind;
  }
};

} // namespace loc

} // namespace ento

} // namespace clang

namespace llvm {

template <typename T> struct isPodLike;
template <> struct isPodLike<clang::ento::SVal> {
  static const bool value = true;
};

} // namespace llvm

#endif // LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SVALS_H
