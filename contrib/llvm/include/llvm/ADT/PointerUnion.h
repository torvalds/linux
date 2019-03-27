//===- llvm/ADT/PointerUnion.h - Discriminated Union of 2 Ptrs --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the PointerUnion class, which is a discriminated union of
// pointer types.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_POINTERUNION_H
#define LLVM_ADT_POINTERUNION_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace llvm {

template <typename T> struct PointerUnionTypeSelectorReturn {
  using Return = T;
};

/// Get a type based on whether two types are the same or not.
///
/// For:
///
/// \code
///   using Ret = typename PointerUnionTypeSelector<T1, T2, EQ, NE>::Return;
/// \endcode
///
/// Ret will be EQ type if T1 is same as T2 or NE type otherwise.
template <typename T1, typename T2, typename RET_EQ, typename RET_NE>
struct PointerUnionTypeSelector {
  using Return = typename PointerUnionTypeSelectorReturn<RET_NE>::Return;
};

template <typename T, typename RET_EQ, typename RET_NE>
struct PointerUnionTypeSelector<T, T, RET_EQ, RET_NE> {
  using Return = typename PointerUnionTypeSelectorReturn<RET_EQ>::Return;
};

template <typename T1, typename T2, typename RET_EQ, typename RET_NE>
struct PointerUnionTypeSelectorReturn<
    PointerUnionTypeSelector<T1, T2, RET_EQ, RET_NE>> {
  using Return =
      typename PointerUnionTypeSelector<T1, T2, RET_EQ, RET_NE>::Return;
};

/// Provide PointerLikeTypeTraits for void* that is used by PointerUnion
/// for the two template arguments.
template <typename PT1, typename PT2> class PointerUnionUIntTraits {
public:
  static inline void *getAsVoidPointer(void *P) { return P; }
  static inline void *getFromVoidPointer(void *P) { return P; }

  enum {
    PT1BitsAv = (int)(PointerLikeTypeTraits<PT1>::NumLowBitsAvailable),
    PT2BitsAv = (int)(PointerLikeTypeTraits<PT2>::NumLowBitsAvailable),
    NumLowBitsAvailable = PT1BitsAv < PT2BitsAv ? PT1BitsAv : PT2BitsAv
  };
};

/// A discriminated union of two pointer types, with the discriminator in the
/// low bit of the pointer.
///
/// This implementation is extremely efficient in space due to leveraging the
/// low bits of the pointer, while exposing a natural and type-safe API.
///
/// Common use patterns would be something like this:
///    PointerUnion<int*, float*> P;
///    P = (int*)0;
///    printf("%d %d", P.is<int*>(), P.is<float*>());  // prints "1 0"
///    X = P.get<int*>();     // ok.
///    Y = P.get<float*>();   // runtime assertion failure.
///    Z = P.get<double*>();  // compile time failure.
///    P = (float*)0;
///    Y = P.get<float*>();   // ok.
///    X = P.get<int*>();     // runtime assertion failure.
template <typename PT1, typename PT2> class PointerUnion {
public:
  using ValTy =
      PointerIntPair<void *, 1, bool, PointerUnionUIntTraits<PT1, PT2>>;

private:
  ValTy Val;

  struct IsPT1 {
    static const int Num = 0;
  };
  struct IsPT2 {
    static const int Num = 1;
  };
  template <typename T> struct UNION_DOESNT_CONTAIN_TYPE {};

public:
  PointerUnion() = default;
  PointerUnion(PT1 V)
      : Val(const_cast<void *>(
            PointerLikeTypeTraits<PT1>::getAsVoidPointer(V))) {}
  PointerUnion(PT2 V)
      : Val(const_cast<void *>(PointerLikeTypeTraits<PT2>::getAsVoidPointer(V)),
            1) {}

  /// Test if the pointer held in the union is null, regardless of
  /// which type it is.
  bool isNull() const {
    // Convert from the void* to one of the pointer types, to make sure that
    // we recursively strip off low bits if we have a nested PointerUnion.
    return !PointerLikeTypeTraits<PT1>::getFromVoidPointer(Val.getPointer());
  }

  explicit operator bool() const { return !isNull(); }

  /// Test if the Union currently holds the type matching T.
  template <typename T> int is() const {
    using Ty = typename ::llvm::PointerUnionTypeSelector<
        PT1, T, IsPT1,
        ::llvm::PointerUnionTypeSelector<PT2, T, IsPT2,
                                         UNION_DOESNT_CONTAIN_TYPE<T>>>::Return;
    int TyNo = Ty::Num;
    return static_cast<int>(Val.getInt()) == TyNo;
  }

  /// Returns the value of the specified pointer type.
  ///
  /// If the specified pointer type is incorrect, assert.
  template <typename T> T get() const {
    assert(is<T>() && "Invalid accessor called");
    return PointerLikeTypeTraits<T>::getFromVoidPointer(Val.getPointer());
  }

  /// Returns the current pointer if it is of the specified pointer type,
  /// otherwises returns null.
  template <typename T> T dyn_cast() const {
    if (is<T>())
      return get<T>();
    return T();
  }

  /// If the union is set to the first pointer type get an address pointing to
  /// it.
  PT1 const *getAddrOfPtr1() const {
    return const_cast<PointerUnion *>(this)->getAddrOfPtr1();
  }

  /// If the union is set to the first pointer type get an address pointing to
  /// it.
  PT1 *getAddrOfPtr1() {
    assert(is<PT1>() && "Val is not the first pointer");
    assert(
        get<PT1>() == Val.getPointer() &&
        "Can't get the address because PointerLikeTypeTraits changes the ptr");
    return const_cast<PT1 *>(
        reinterpret_cast<const PT1 *>(Val.getAddrOfPointer()));
  }

  /// Assignment from nullptr which just clears the union.
  const PointerUnion &operator=(std::nullptr_t) {
    Val.initWithPointer(nullptr);
    return *this;
  }

  /// Assignment operators - Allow assigning into this union from either
  /// pointer type, setting the discriminator to remember what it came from.
  const PointerUnion &operator=(const PT1 &RHS) {
    Val.initWithPointer(
        const_cast<void *>(PointerLikeTypeTraits<PT1>::getAsVoidPointer(RHS)));
    return *this;
  }
  const PointerUnion &operator=(const PT2 &RHS) {
    Val.setPointerAndInt(
        const_cast<void *>(PointerLikeTypeTraits<PT2>::getAsVoidPointer(RHS)),
        1);
    return *this;
  }

  void *getOpaqueValue() const { return Val.getOpaqueValue(); }
  static inline PointerUnion getFromOpaqueValue(void *VP) {
    PointerUnion V;
    V.Val = ValTy::getFromOpaqueValue(VP);
    return V;
  }
};

template <typename PT1, typename PT2>
bool operator==(PointerUnion<PT1, PT2> lhs, PointerUnion<PT1, PT2> rhs) {
  return lhs.getOpaqueValue() == rhs.getOpaqueValue();
}

template <typename PT1, typename PT2>
bool operator!=(PointerUnion<PT1, PT2> lhs, PointerUnion<PT1, PT2> rhs) {
  return lhs.getOpaqueValue() != rhs.getOpaqueValue();
}

template <typename PT1, typename PT2>
bool operator<(PointerUnion<PT1, PT2> lhs, PointerUnion<PT1, PT2> rhs) {
  return lhs.getOpaqueValue() < rhs.getOpaqueValue();
}

// Teach SmallPtrSet that PointerUnion is "basically a pointer", that has
// # low bits available = min(PT1bits,PT2bits)-1.
template <typename PT1, typename PT2>
struct PointerLikeTypeTraits<PointerUnion<PT1, PT2>> {
  static inline void *getAsVoidPointer(const PointerUnion<PT1, PT2> &P) {
    return P.getOpaqueValue();
  }

  static inline PointerUnion<PT1, PT2> getFromVoidPointer(void *P) {
    return PointerUnion<PT1, PT2>::getFromOpaqueValue(P);
  }

  // The number of bits available are the min of the two pointer types.
  enum {
    NumLowBitsAvailable = PointerLikeTypeTraits<
        typename PointerUnion<PT1, PT2>::ValTy>::NumLowBitsAvailable
  };
};

/// A pointer union of three pointer types. See documentation for PointerUnion
/// for usage.
template <typename PT1, typename PT2, typename PT3> class PointerUnion3 {
public:
  using InnerUnion = PointerUnion<PT1, PT2>;
  using ValTy = PointerUnion<InnerUnion, PT3>;

private:
  ValTy Val;

  struct IsInnerUnion {
    ValTy Val;

    IsInnerUnion(ValTy val) : Val(val) {}

    template <typename T> int is() const {
      return Val.template is<InnerUnion>() &&
             Val.template get<InnerUnion>().template is<T>();
    }

    template <typename T> T get() const {
      return Val.template get<InnerUnion>().template get<T>();
    }
  };

  struct IsPT3 {
    ValTy Val;

    IsPT3(ValTy val) : Val(val) {}

    template <typename T> int is() const { return Val.template is<T>(); }
    template <typename T> T get() const { return Val.template get<T>(); }
  };

public:
  PointerUnion3() = default;
  PointerUnion3(PT1 V) { Val = InnerUnion(V); }
  PointerUnion3(PT2 V) { Val = InnerUnion(V); }
  PointerUnion3(PT3 V) { Val = V; }

  /// Test if the pointer held in the union is null, regardless of
  /// which type it is.
  bool isNull() const { return Val.isNull(); }
  explicit operator bool() const { return !isNull(); }

  /// Test if the Union currently holds the type matching T.
  template <typename T> int is() const {
    // If T is PT1/PT2 choose IsInnerUnion otherwise choose IsPT3.
    using Ty = typename ::llvm::PointerUnionTypeSelector<
        PT1, T, IsInnerUnion,
        ::llvm::PointerUnionTypeSelector<PT2, T, IsInnerUnion, IsPT3>>::Return;
    return Ty(Val).template is<T>();
  }

  /// Returns the value of the specified pointer type.
  ///
  /// If the specified pointer type is incorrect, assert.
  template <typename T> T get() const {
    assert(is<T>() && "Invalid accessor called");
    // If T is PT1/PT2 choose IsInnerUnion otherwise choose IsPT3.
    using Ty = typename ::llvm::PointerUnionTypeSelector<
        PT1, T, IsInnerUnion,
        ::llvm::PointerUnionTypeSelector<PT2, T, IsInnerUnion, IsPT3>>::Return;
    return Ty(Val).template get<T>();
  }

  /// Returns the current pointer if it is of the specified pointer type,
  /// otherwises returns null.
  template <typename T> T dyn_cast() const {
    if (is<T>())
      return get<T>();
    return T();
  }

  /// Assignment from nullptr which just clears the union.
  const PointerUnion3 &operator=(std::nullptr_t) {
    Val = nullptr;
    return *this;
  }

  /// Assignment operators - Allow assigning into this union from either
  /// pointer type, setting the discriminator to remember what it came from.
  const PointerUnion3 &operator=(const PT1 &RHS) {
    Val = InnerUnion(RHS);
    return *this;
  }
  const PointerUnion3 &operator=(const PT2 &RHS) {
    Val = InnerUnion(RHS);
    return *this;
  }
  const PointerUnion3 &operator=(const PT3 &RHS) {
    Val = RHS;
    return *this;
  }

  void *getOpaqueValue() const { return Val.getOpaqueValue(); }
  static inline PointerUnion3 getFromOpaqueValue(void *VP) {
    PointerUnion3 V;
    V.Val = ValTy::getFromOpaqueValue(VP);
    return V;
  }
};

// Teach SmallPtrSet that PointerUnion3 is "basically a pointer", that has
// # low bits available = min(PT1bits,PT2bits,PT2bits)-2.
template <typename PT1, typename PT2, typename PT3>
struct PointerLikeTypeTraits<PointerUnion3<PT1, PT2, PT3>> {
  static inline void *getAsVoidPointer(const PointerUnion3<PT1, PT2, PT3> &P) {
    return P.getOpaqueValue();
  }

  static inline PointerUnion3<PT1, PT2, PT3> getFromVoidPointer(void *P) {
    return PointerUnion3<PT1, PT2, PT3>::getFromOpaqueValue(P);
  }

  // The number of bits available are the min of the two pointer types.
  enum {
    NumLowBitsAvailable = PointerLikeTypeTraits<
        typename PointerUnion3<PT1, PT2, PT3>::ValTy>::NumLowBitsAvailable
  };
};

template <typename PT1, typename PT2, typename PT3>
bool operator<(PointerUnion3<PT1, PT2, PT3> lhs,
               PointerUnion3<PT1, PT2, PT3> rhs) {
  return lhs.getOpaqueValue() < rhs.getOpaqueValue();
}

/// A pointer union of four pointer types. See documentation for PointerUnion
/// for usage.
template <typename PT1, typename PT2, typename PT3, typename PT4>
class PointerUnion4 {
public:
  using InnerUnion1 = PointerUnion<PT1, PT2>;
  using InnerUnion2 = PointerUnion<PT3, PT4>;
  using ValTy = PointerUnion<InnerUnion1, InnerUnion2>;

private:
  ValTy Val;

public:
  PointerUnion4() = default;
  PointerUnion4(PT1 V) { Val = InnerUnion1(V); }
  PointerUnion4(PT2 V) { Val = InnerUnion1(V); }
  PointerUnion4(PT3 V) { Val = InnerUnion2(V); }
  PointerUnion4(PT4 V) { Val = InnerUnion2(V); }

  /// Test if the pointer held in the union is null, regardless of
  /// which type it is.
  bool isNull() const { return Val.isNull(); }
  explicit operator bool() const { return !isNull(); }

  /// Test if the Union currently holds the type matching T.
  template <typename T> int is() const {
    // If T is PT1/PT2 choose InnerUnion1 otherwise choose InnerUnion2.
    using Ty = typename ::llvm::PointerUnionTypeSelector<
        PT1, T, InnerUnion1,
        ::llvm::PointerUnionTypeSelector<PT2, T, InnerUnion1,
                                         InnerUnion2>>::Return;
    return Val.template is<Ty>() && Val.template get<Ty>().template is<T>();
  }

  /// Returns the value of the specified pointer type.
  ///
  /// If the specified pointer type is incorrect, assert.
  template <typename T> T get() const {
    assert(is<T>() && "Invalid accessor called");
    // If T is PT1/PT2 choose InnerUnion1 otherwise choose InnerUnion2.
    using Ty = typename ::llvm::PointerUnionTypeSelector<
        PT1, T, InnerUnion1,
        ::llvm::PointerUnionTypeSelector<PT2, T, InnerUnion1,
                                         InnerUnion2>>::Return;
    return Val.template get<Ty>().template get<T>();
  }

  /// Returns the current pointer if it is of the specified pointer type,
  /// otherwises returns null.
  template <typename T> T dyn_cast() const {
    if (is<T>())
      return get<T>();
    return T();
  }

  /// Assignment from nullptr which just clears the union.
  const PointerUnion4 &operator=(std::nullptr_t) {
    Val = nullptr;
    return *this;
  }

  /// Assignment operators - Allow assigning into this union from either
  /// pointer type, setting the discriminator to remember what it came from.
  const PointerUnion4 &operator=(const PT1 &RHS) {
    Val = InnerUnion1(RHS);
    return *this;
  }
  const PointerUnion4 &operator=(const PT2 &RHS) {
    Val = InnerUnion1(RHS);
    return *this;
  }
  const PointerUnion4 &operator=(const PT3 &RHS) {
    Val = InnerUnion2(RHS);
    return *this;
  }
  const PointerUnion4 &operator=(const PT4 &RHS) {
    Val = InnerUnion2(RHS);
    return *this;
  }

  void *getOpaqueValue() const { return Val.getOpaqueValue(); }
  static inline PointerUnion4 getFromOpaqueValue(void *VP) {
    PointerUnion4 V;
    V.Val = ValTy::getFromOpaqueValue(VP);
    return V;
  }
};

// Teach SmallPtrSet that PointerUnion4 is "basically a pointer", that has
// # low bits available = min(PT1bits,PT2bits,PT2bits)-2.
template <typename PT1, typename PT2, typename PT3, typename PT4>
struct PointerLikeTypeTraits<PointerUnion4<PT1, PT2, PT3, PT4>> {
  static inline void *
  getAsVoidPointer(const PointerUnion4<PT1, PT2, PT3, PT4> &P) {
    return P.getOpaqueValue();
  }

  static inline PointerUnion4<PT1, PT2, PT3, PT4> getFromVoidPointer(void *P) {
    return PointerUnion4<PT1, PT2, PT3, PT4>::getFromOpaqueValue(P);
  }

  // The number of bits available are the min of the two pointer types.
  enum {
    NumLowBitsAvailable = PointerLikeTypeTraits<
        typename PointerUnion4<PT1, PT2, PT3, PT4>::ValTy>::NumLowBitsAvailable
  };
};

// Teach DenseMap how to use PointerUnions as keys.
template <typename T, typename U> struct DenseMapInfo<PointerUnion<T, U>> {
  using Pair = PointerUnion<T, U>;
  using FirstInfo = DenseMapInfo<T>;
  using SecondInfo = DenseMapInfo<U>;

  static inline Pair getEmptyKey() { return Pair(FirstInfo::getEmptyKey()); }

  static inline Pair getTombstoneKey() {
    return Pair(FirstInfo::getTombstoneKey());
  }

  static unsigned getHashValue(const Pair &PairVal) {
    intptr_t key = (intptr_t)PairVal.getOpaqueValue();
    return DenseMapInfo<intptr_t>::getHashValue(key);
  }

  static bool isEqual(const Pair &LHS, const Pair &RHS) {
    return LHS.template is<T>() == RHS.template is<T>() &&
           (LHS.template is<T>() ? FirstInfo::isEqual(LHS.template get<T>(),
                                                      RHS.template get<T>())
                                 : SecondInfo::isEqual(LHS.template get<U>(),
                                                       RHS.template get<U>()));
  }
};

} // end namespace llvm

#endif // LLVM_ADT_POINTERUNION_H
