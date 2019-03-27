//===- CodeGenDAGPatterns.cpp - Read DAG patterns from .td file -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the CodeGenDAGPatterns class, which is used to read and
// represent the patterns present in a .td file for instructions.
//
//===----------------------------------------------------------------------===//

#include "CodeGenDAGPatterns.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <set>
using namespace llvm;

#define DEBUG_TYPE "dag-patterns"

static inline bool isIntegerOrPtr(MVT VT) {
  return VT.isInteger() || VT == MVT::iPTR;
}
static inline bool isFloatingPoint(MVT VT) {
  return VT.isFloatingPoint();
}
static inline bool isVector(MVT VT) {
  return VT.isVector();
}
static inline bool isScalar(MVT VT) {
  return !VT.isVector();
}

template <typename Predicate>
static bool berase_if(MachineValueTypeSet &S, Predicate P) {
  bool Erased = false;
  // It is ok to iterate over MachineValueTypeSet and remove elements from it
  // at the same time.
  for (MVT T : S) {
    if (!P(T))
      continue;
    Erased = true;
    S.erase(T);
  }
  return Erased;
}

// --- TypeSetByHwMode

// This is a parameterized type-set class. For each mode there is a list
// of types that are currently possible for a given tree node. Type
// inference will apply to each mode separately.

TypeSetByHwMode::TypeSetByHwMode(ArrayRef<ValueTypeByHwMode> VTList) {
  for (const ValueTypeByHwMode &VVT : VTList)
    insert(VVT);
}

bool TypeSetByHwMode::isValueTypeByHwMode(bool AllowEmpty) const {
  for (const auto &I : *this) {
    if (I.second.size() > 1)
      return false;
    if (!AllowEmpty && I.second.empty())
      return false;
  }
  return true;
}

ValueTypeByHwMode TypeSetByHwMode::getValueTypeByHwMode() const {
  assert(isValueTypeByHwMode(true) &&
         "The type set has multiple types for at least one HW mode");
  ValueTypeByHwMode VVT;
  for (const auto &I : *this) {
    MVT T = I.second.empty() ? MVT::Other : *I.second.begin();
    VVT.getOrCreateTypeForMode(I.first, T);
  }
  return VVT;
}

bool TypeSetByHwMode::isPossible() const {
  for (const auto &I : *this)
    if (!I.second.empty())
      return true;
  return false;
}

bool TypeSetByHwMode::insert(const ValueTypeByHwMode &VVT) {
  bool Changed = false;
  bool ContainsDefault = false;
  MVT DT = MVT::Other;

  SmallDenseSet<unsigned, 4> Modes;
  for (const auto &P : VVT) {
    unsigned M = P.first;
    Modes.insert(M);
    // Make sure there exists a set for each specific mode from VVT.
    Changed |= getOrCreate(M).insert(P.second).second;
    // Cache VVT's default mode.
    if (DefaultMode == M) {
      ContainsDefault = true;
      DT = P.second;
    }
  }

  // If VVT has a default mode, add the corresponding type to all
  // modes in "this" that do not exist in VVT.
  if (ContainsDefault)
    for (auto &I : *this)
      if (!Modes.count(I.first))
        Changed |= I.second.insert(DT).second;

  return Changed;
}

// Constrain the type set to be the intersection with VTS.
bool TypeSetByHwMode::constrain(const TypeSetByHwMode &VTS) {
  bool Changed = false;
  if (hasDefault()) {
    for (const auto &I : VTS) {
      unsigned M = I.first;
      if (M == DefaultMode || hasMode(M))
        continue;
      Map.insert({M, Map.at(DefaultMode)});
      Changed = true;
    }
  }

  for (auto &I : *this) {
    unsigned M = I.first;
    SetType &S = I.second;
    if (VTS.hasMode(M) || VTS.hasDefault()) {
      Changed |= intersect(I.second, VTS.get(M));
    } else if (!S.empty()) {
      S.clear();
      Changed = true;
    }
  }
  return Changed;
}

template <typename Predicate>
bool TypeSetByHwMode::constrain(Predicate P) {
  bool Changed = false;
  for (auto &I : *this)
    Changed |= berase_if(I.second, [&P](MVT VT) { return !P(VT); });
  return Changed;
}

template <typename Predicate>
bool TypeSetByHwMode::assign_if(const TypeSetByHwMode &VTS, Predicate P) {
  assert(empty());
  for (const auto &I : VTS) {
    SetType &S = getOrCreate(I.first);
    for (auto J : I.second)
      if (P(J))
        S.insert(J);
  }
  return !empty();
}

void TypeSetByHwMode::writeToStream(raw_ostream &OS) const {
  SmallVector<unsigned, 4> Modes;
  Modes.reserve(Map.size());

  for (const auto &I : *this)
    Modes.push_back(I.first);
  if (Modes.empty()) {
    OS << "{}";
    return;
  }
  array_pod_sort(Modes.begin(), Modes.end());

  OS << '{';
  for (unsigned M : Modes) {
    OS << ' ' << getModeName(M) << ':';
    writeToStream(get(M), OS);
  }
  OS << " }";
}

void TypeSetByHwMode::writeToStream(const SetType &S, raw_ostream &OS) {
  SmallVector<MVT, 4> Types(S.begin(), S.end());
  array_pod_sort(Types.begin(), Types.end());

  OS << '[';
  for (unsigned i = 0, e = Types.size(); i != e; ++i) {
    OS << ValueTypeByHwMode::getMVTName(Types[i]);
    if (i != e-1)
      OS << ' ';
  }
  OS << ']';
}

bool TypeSetByHwMode::operator==(const TypeSetByHwMode &VTS) const {
  // The isSimple call is much quicker than hasDefault - check this first.
  bool IsSimple = isSimple();
  bool VTSIsSimple = VTS.isSimple();
  if (IsSimple && VTSIsSimple)
    return *begin() == *VTS.begin();

  // Speedup: We have a default if the set is simple.
  bool HaveDefault = IsSimple || hasDefault();
  bool VTSHaveDefault = VTSIsSimple || VTS.hasDefault();
  if (HaveDefault != VTSHaveDefault)
    return false;

  SmallDenseSet<unsigned, 4> Modes;
  for (auto &I : *this)
    Modes.insert(I.first);
  for (const auto &I : VTS)
    Modes.insert(I.first);

  if (HaveDefault) {
    // Both sets have default mode.
    for (unsigned M : Modes) {
      if (get(M) != VTS.get(M))
        return false;
    }
  } else {
    // Neither set has default mode.
    for (unsigned M : Modes) {
      // If there is no default mode, an empty set is equivalent to not having
      // the corresponding mode.
      bool NoModeThis = !hasMode(M) || get(M).empty();
      bool NoModeVTS = !VTS.hasMode(M) || VTS.get(M).empty();
      if (NoModeThis != NoModeVTS)
        return false;
      if (!NoModeThis)
        if (get(M) != VTS.get(M))
          return false;
    }
  }

  return true;
}

namespace llvm {
  raw_ostream &operator<<(raw_ostream &OS, const TypeSetByHwMode &T) {
    T.writeToStream(OS);
    return OS;
  }
}

LLVM_DUMP_METHOD
void TypeSetByHwMode::dump() const {
  dbgs() << *this << '\n';
}

bool TypeSetByHwMode::intersect(SetType &Out, const SetType &In) {
  bool OutP = Out.count(MVT::iPTR), InP = In.count(MVT::iPTR);
  auto Int = [&In](MVT T) -> bool { return !In.count(T); };

  if (OutP == InP)
    return berase_if(Out, Int);

  // Compute the intersection of scalars separately to account for only
  // one set containing iPTR.
  // The itersection of iPTR with a set of integer scalar types that does not
  // include iPTR will result in the most specific scalar type:
  // - iPTR is more specific than any set with two elements or more
  // - iPTR is less specific than any single integer scalar type.
  // For example
  // { iPTR } * { i32 }     -> { i32 }
  // { iPTR } * { i32 i64 } -> { iPTR }
  // and
  // { iPTR i32 } * { i32 }          -> { i32 }
  // { iPTR i32 } * { i32 i64 }      -> { i32 i64 }
  // { iPTR i32 } * { i32 i64 i128 } -> { iPTR i32 }

  // Compute the difference between the two sets in such a way that the
  // iPTR is in the set that is being subtracted. This is to see if there
  // are any extra scalars in the set without iPTR that are not in the
  // set containing iPTR. Then the iPTR could be considered a "wildcard"
  // matching these scalars. If there is only one such scalar, it would
  // replace the iPTR, if there are more, the iPTR would be retained.
  SetType Diff;
  if (InP) {
    Diff = Out;
    berase_if(Diff, [&In](MVT T) { return In.count(T); });
    // Pre-remove these elements and rely only on InP/OutP to determine
    // whether a change has been made.
    berase_if(Out, [&Diff](MVT T) { return Diff.count(T); });
  } else {
    Diff = In;
    berase_if(Diff, [&Out](MVT T) { return Out.count(T); });
    Out.erase(MVT::iPTR);
  }

  // The actual intersection.
  bool Changed = berase_if(Out, Int);
  unsigned NumD = Diff.size();
  if (NumD == 0)
    return Changed;

  if (NumD == 1) {
    Out.insert(*Diff.begin());
    // This is a change only if Out was the one with iPTR (which is now
    // being replaced).
    Changed |= OutP;
  } else {
    // Multiple elements from Out are now replaced with iPTR.
    Out.insert(MVT::iPTR);
    Changed |= !OutP;
  }
  return Changed;
}

bool TypeSetByHwMode::validate() const {
#ifndef NDEBUG
  if (empty())
    return true;
  bool AllEmpty = true;
  for (const auto &I : *this)
    AllEmpty &= I.second.empty();
  return !AllEmpty;
#endif
  return true;
}

// --- TypeInfer

bool TypeInfer::MergeInTypeInfo(TypeSetByHwMode &Out,
                                const TypeSetByHwMode &In) {
  ValidateOnExit _1(Out, *this);
  In.validate();
  if (In.empty() || Out == In || TP.hasError())
    return false;
  if (Out.empty()) {
    Out = In;
    return true;
  }

  bool Changed = Out.constrain(In);
  if (Changed && Out.empty())
    TP.error("Type contradiction");

  return Changed;
}

bool TypeInfer::forceArbitrary(TypeSetByHwMode &Out) {
  ValidateOnExit _1(Out, *this);
  if (TP.hasError())
    return false;
  assert(!Out.empty() && "cannot pick from an empty set");

  bool Changed = false;
  for (auto &I : Out) {
    TypeSetByHwMode::SetType &S = I.second;
    if (S.size() <= 1)
      continue;
    MVT T = *S.begin(); // Pick the first element.
    S.clear();
    S.insert(T);
    Changed = true;
  }
  return Changed;
}

bool TypeInfer::EnforceInteger(TypeSetByHwMode &Out) {
  ValidateOnExit _1(Out, *this);
  if (TP.hasError())
    return false;
  if (!Out.empty())
    return Out.constrain(isIntegerOrPtr);

  return Out.assign_if(getLegalTypes(), isIntegerOrPtr);
}

bool TypeInfer::EnforceFloatingPoint(TypeSetByHwMode &Out) {
  ValidateOnExit _1(Out, *this);
  if (TP.hasError())
    return false;
  if (!Out.empty())
    return Out.constrain(isFloatingPoint);

  return Out.assign_if(getLegalTypes(), isFloatingPoint);
}

bool TypeInfer::EnforceScalar(TypeSetByHwMode &Out) {
  ValidateOnExit _1(Out, *this);
  if (TP.hasError())
    return false;
  if (!Out.empty())
    return Out.constrain(isScalar);

  return Out.assign_if(getLegalTypes(), isScalar);
}

bool TypeInfer::EnforceVector(TypeSetByHwMode &Out) {
  ValidateOnExit _1(Out, *this);
  if (TP.hasError())
    return false;
  if (!Out.empty())
    return Out.constrain(isVector);

  return Out.assign_if(getLegalTypes(), isVector);
}

bool TypeInfer::EnforceAny(TypeSetByHwMode &Out) {
  ValidateOnExit _1(Out, *this);
  if (TP.hasError() || !Out.empty())
    return false;

  Out = getLegalTypes();
  return true;
}

template <typename Iter, typename Pred, typename Less>
static Iter min_if(Iter B, Iter E, Pred P, Less L) {
  if (B == E)
    return E;
  Iter Min = E;
  for (Iter I = B; I != E; ++I) {
    if (!P(*I))
      continue;
    if (Min == E || L(*I, *Min))
      Min = I;
  }
  return Min;
}

template <typename Iter, typename Pred, typename Less>
static Iter max_if(Iter B, Iter E, Pred P, Less L) {
  if (B == E)
    return E;
  Iter Max = E;
  for (Iter I = B; I != E; ++I) {
    if (!P(*I))
      continue;
    if (Max == E || L(*Max, *I))
      Max = I;
  }
  return Max;
}

/// Make sure that for each type in Small, there exists a larger type in Big.
bool TypeInfer::EnforceSmallerThan(TypeSetByHwMode &Small,
                                   TypeSetByHwMode &Big) {
  ValidateOnExit _1(Small, *this), _2(Big, *this);
  if (TP.hasError())
    return false;
  bool Changed = false;

  if (Small.empty())
    Changed |= EnforceAny(Small);
  if (Big.empty())
    Changed |= EnforceAny(Big);

  assert(Small.hasDefault() && Big.hasDefault());

  std::vector<unsigned> Modes = union_modes(Small, Big);

  // 1. Only allow integer or floating point types and make sure that
  //    both sides are both integer or both floating point.
  // 2. Make sure that either both sides have vector types, or neither
  //    of them does.
  for (unsigned M : Modes) {
    TypeSetByHwMode::SetType &S = Small.get(M);
    TypeSetByHwMode::SetType &B = Big.get(M);

    if (any_of(S, isIntegerOrPtr) && any_of(S, isIntegerOrPtr)) {
      auto NotInt = [](MVT VT) { return !isIntegerOrPtr(VT); };
      Changed |= berase_if(S, NotInt) |
                 berase_if(B, NotInt);
    } else if (any_of(S, isFloatingPoint) && any_of(B, isFloatingPoint)) {
      auto NotFP = [](MVT VT) { return !isFloatingPoint(VT); };
      Changed |= berase_if(S, NotFP) |
                 berase_if(B, NotFP);
    } else if (S.empty() || B.empty()) {
      Changed = !S.empty() || !B.empty();
      S.clear();
      B.clear();
    } else {
      TP.error("Incompatible types");
      return Changed;
    }

    if (none_of(S, isVector) || none_of(B, isVector)) {
      Changed |= berase_if(S, isVector) |
                 berase_if(B, isVector);
    }
  }

  auto LT = [](MVT A, MVT B) -> bool {
    return A.getScalarSizeInBits() < B.getScalarSizeInBits() ||
           (A.getScalarSizeInBits() == B.getScalarSizeInBits() &&
            A.getSizeInBits() < B.getSizeInBits());
  };
  auto LE = [](MVT A, MVT B) -> bool {
    // This function is used when removing elements: when a vector is compared
    // to a non-vector, it should return false (to avoid removal).
    if (A.isVector() != B.isVector())
      return false;

    // Note on the < comparison below:
    // X86 has patterns like
    //   (set VR128X:$dst, (v16i8 (X86vtrunc (v4i32 VR128X:$src1)))),
    // where the truncated vector is given a type v16i8, while the source
    // vector has type v4i32. They both have the same size in bits.
    // The minimal type in the result is obviously v16i8, and when we remove
    // all types from the source that are smaller-or-equal than v8i16, the
    // only source type would also be removed (since it's equal in size).
    return A.getScalarSizeInBits() <= B.getScalarSizeInBits() ||
           A.getSizeInBits() < B.getSizeInBits();
  };

  for (unsigned M : Modes) {
    TypeSetByHwMode::SetType &S = Small.get(M);
    TypeSetByHwMode::SetType &B = Big.get(M);
    // MinS = min scalar in Small, remove all scalars from Big that are
    // smaller-or-equal than MinS.
    auto MinS = min_if(S.begin(), S.end(), isScalar, LT);
    if (MinS != S.end())
      Changed |= berase_if(B, std::bind(LE, std::placeholders::_1, *MinS));

    // MaxS = max scalar in Big, remove all scalars from Small that are
    // larger than MaxS.
    auto MaxS = max_if(B.begin(), B.end(), isScalar, LT);
    if (MaxS != B.end())
      Changed |= berase_if(S, std::bind(LE, *MaxS, std::placeholders::_1));

    // MinV = min vector in Small, remove all vectors from Big that are
    // smaller-or-equal than MinV.
    auto MinV = min_if(S.begin(), S.end(), isVector, LT);
    if (MinV != S.end())
      Changed |= berase_if(B, std::bind(LE, std::placeholders::_1, *MinV));

    // MaxV = max vector in Big, remove all vectors from Small that are
    // larger than MaxV.
    auto MaxV = max_if(B.begin(), B.end(), isVector, LT);
    if (MaxV != B.end())
      Changed |= berase_if(S, std::bind(LE, *MaxV, std::placeholders::_1));
  }

  return Changed;
}

/// 1. Ensure that for each type T in Vec, T is a vector type, and that
///    for each type U in Elem, U is a scalar type.
/// 2. Ensure that for each (scalar) type U in Elem, there exists a (vector)
///    type T in Vec, such that U is the element type of T.
bool TypeInfer::EnforceVectorEltTypeIs(TypeSetByHwMode &Vec,
                                       TypeSetByHwMode &Elem) {
  ValidateOnExit _1(Vec, *this), _2(Elem, *this);
  if (TP.hasError())
    return false;
  bool Changed = false;

  if (Vec.empty())
    Changed |= EnforceVector(Vec);
  if (Elem.empty())
    Changed |= EnforceScalar(Elem);

  for (unsigned M : union_modes(Vec, Elem)) {
    TypeSetByHwMode::SetType &V = Vec.get(M);
    TypeSetByHwMode::SetType &E = Elem.get(M);

    Changed |= berase_if(V, isScalar);  // Scalar = !vector
    Changed |= berase_if(E, isVector);  // Vector = !scalar
    assert(!V.empty() && !E.empty());

    SmallSet<MVT,4> VT, ST;
    // Collect element types from the "vector" set.
    for (MVT T : V)
      VT.insert(T.getVectorElementType());
    // Collect scalar types from the "element" set.
    for (MVT T : E)
      ST.insert(T);

    // Remove from V all (vector) types whose element type is not in S.
    Changed |= berase_if(V, [&ST](MVT T) -> bool {
                              return !ST.count(T.getVectorElementType());
                            });
    // Remove from E all (scalar) types, for which there is no corresponding
    // type in V.
    Changed |= berase_if(E, [&VT](MVT T) -> bool { return !VT.count(T); });
  }

  return Changed;
}

bool TypeInfer::EnforceVectorEltTypeIs(TypeSetByHwMode &Vec,
                                       const ValueTypeByHwMode &VVT) {
  TypeSetByHwMode Tmp(VVT);
  ValidateOnExit _1(Vec, *this), _2(Tmp, *this);
  return EnforceVectorEltTypeIs(Vec, Tmp);
}

/// Ensure that for each type T in Sub, T is a vector type, and there
/// exists a type U in Vec such that U is a vector type with the same
/// element type as T and at least as many elements as T.
bool TypeInfer::EnforceVectorSubVectorTypeIs(TypeSetByHwMode &Vec,
                                             TypeSetByHwMode &Sub) {
  ValidateOnExit _1(Vec, *this), _2(Sub, *this);
  if (TP.hasError())
    return false;

  /// Return true if B is a suB-vector of P, i.e. P is a suPer-vector of B.
  auto IsSubVec = [](MVT B, MVT P) -> bool {
    if (!B.isVector() || !P.isVector())
      return false;
    // Logically a <4 x i32> is a valid subvector of <n x 4 x i32>
    // but until there are obvious use-cases for this, keep the
    // types separate.
    if (B.isScalableVector() != P.isScalableVector())
      return false;
    if (B.getVectorElementType() != P.getVectorElementType())
      return false;
    return B.getVectorNumElements() < P.getVectorNumElements();
  };

  /// Return true if S has no element (vector type) that T is a sub-vector of,
  /// i.e. has the same element type as T and more elements.
  auto NoSubV = [&IsSubVec](const TypeSetByHwMode::SetType &S, MVT T) -> bool {
    for (const auto &I : S)
      if (IsSubVec(T, I))
        return false;
    return true;
  };

  /// Return true if S has no element (vector type) that T is a super-vector
  /// of, i.e. has the same element type as T and fewer elements.
  auto NoSupV = [&IsSubVec](const TypeSetByHwMode::SetType &S, MVT T) -> bool {
    for (const auto &I : S)
      if (IsSubVec(I, T))
        return false;
    return true;
  };

  bool Changed = false;

  if (Vec.empty())
    Changed |= EnforceVector(Vec);
  if (Sub.empty())
    Changed |= EnforceVector(Sub);

  for (unsigned M : union_modes(Vec, Sub)) {
    TypeSetByHwMode::SetType &S = Sub.get(M);
    TypeSetByHwMode::SetType &V = Vec.get(M);

    Changed |= berase_if(S, isScalar);

    // Erase all types from S that are not sub-vectors of a type in V.
    Changed |= berase_if(S, std::bind(NoSubV, V, std::placeholders::_1));

    // Erase all types from V that are not super-vectors of a type in S.
    Changed |= berase_if(V, std::bind(NoSupV, S, std::placeholders::_1));
  }

  return Changed;
}

/// 1. Ensure that V has a scalar type iff W has a scalar type.
/// 2. Ensure that for each vector type T in V, there exists a vector
///    type U in W, such that T and U have the same number of elements.
/// 3. Ensure that for each vector type U in W, there exists a vector
///    type T in V, such that T and U have the same number of elements
///    (reverse of 2).
bool TypeInfer::EnforceSameNumElts(TypeSetByHwMode &V, TypeSetByHwMode &W) {
  ValidateOnExit _1(V, *this), _2(W, *this);
  if (TP.hasError())
    return false;

  bool Changed = false;
  if (V.empty())
    Changed |= EnforceAny(V);
  if (W.empty())
    Changed |= EnforceAny(W);

  // An actual vector type cannot have 0 elements, so we can treat scalars
  // as zero-length vectors. This way both vectors and scalars can be
  // processed identically.
  auto NoLength = [](const SmallSet<unsigned,2> &Lengths, MVT T) -> bool {
    return !Lengths.count(T.isVector() ? T.getVectorNumElements() : 0);
  };

  for (unsigned M : union_modes(V, W)) {
    TypeSetByHwMode::SetType &VS = V.get(M);
    TypeSetByHwMode::SetType &WS = W.get(M);

    SmallSet<unsigned,2> VN, WN;
    for (MVT T : VS)
      VN.insert(T.isVector() ? T.getVectorNumElements() : 0);
    for (MVT T : WS)
      WN.insert(T.isVector() ? T.getVectorNumElements() : 0);

    Changed |= berase_if(VS, std::bind(NoLength, WN, std::placeholders::_1));
    Changed |= berase_if(WS, std::bind(NoLength, VN, std::placeholders::_1));
  }
  return Changed;
}

/// 1. Ensure that for each type T in A, there exists a type U in B,
///    such that T and U have equal size in bits.
/// 2. Ensure that for each type U in B, there exists a type T in A
///    such that T and U have equal size in bits (reverse of 1).
bool TypeInfer::EnforceSameSize(TypeSetByHwMode &A, TypeSetByHwMode &B) {
  ValidateOnExit _1(A, *this), _2(B, *this);
  if (TP.hasError())
    return false;
  bool Changed = false;
  if (A.empty())
    Changed |= EnforceAny(A);
  if (B.empty())
    Changed |= EnforceAny(B);

  auto NoSize = [](const SmallSet<unsigned,2> &Sizes, MVT T) -> bool {
    return !Sizes.count(T.getSizeInBits());
  };

  for (unsigned M : union_modes(A, B)) {
    TypeSetByHwMode::SetType &AS = A.get(M);
    TypeSetByHwMode::SetType &BS = B.get(M);
    SmallSet<unsigned,2> AN, BN;

    for (MVT T : AS)
      AN.insert(T.getSizeInBits());
    for (MVT T : BS)
      BN.insert(T.getSizeInBits());

    Changed |= berase_if(AS, std::bind(NoSize, BN, std::placeholders::_1));
    Changed |= berase_if(BS, std::bind(NoSize, AN, std::placeholders::_1));
  }

  return Changed;
}

void TypeInfer::expandOverloads(TypeSetByHwMode &VTS) {
  ValidateOnExit _1(VTS, *this);
  const TypeSetByHwMode &Legal = getLegalTypes();
  assert(Legal.isDefaultOnly() && "Default-mode only expected");
  const TypeSetByHwMode::SetType &LegalTypes = Legal.get(DefaultMode);

  for (auto &I : VTS)
    expandOverloads(I.second, LegalTypes);
}

void TypeInfer::expandOverloads(TypeSetByHwMode::SetType &Out,
                                const TypeSetByHwMode::SetType &Legal) {
  std::set<MVT> Ovs;
  for (MVT T : Out) {
    if (!T.isOverloaded())
      continue;

    Ovs.insert(T);
    // MachineValueTypeSet allows iteration and erasing.
    Out.erase(T);
  }

  for (MVT Ov : Ovs) {
    switch (Ov.SimpleTy) {
      case MVT::iPTRAny:
        Out.insert(MVT::iPTR);
        return;
      case MVT::iAny:
        for (MVT T : MVT::integer_valuetypes())
          if (Legal.count(T))
            Out.insert(T);
        for (MVT T : MVT::integer_vector_valuetypes())
          if (Legal.count(T))
            Out.insert(T);
        return;
      case MVT::fAny:
        for (MVT T : MVT::fp_valuetypes())
          if (Legal.count(T))
            Out.insert(T);
        for (MVT T : MVT::fp_vector_valuetypes())
          if (Legal.count(T))
            Out.insert(T);
        return;
      case MVT::vAny:
        for (MVT T : MVT::vector_valuetypes())
          if (Legal.count(T))
            Out.insert(T);
        return;
      case MVT::Any:
        for (MVT T : MVT::all_valuetypes())
          if (Legal.count(T))
            Out.insert(T);
        return;
      default:
        break;
    }
  }
}

const TypeSetByHwMode &TypeInfer::getLegalTypes() {
  if (!LegalTypesCached) {
    TypeSetByHwMode::SetType &LegalTypes = LegalCache.getOrCreate(DefaultMode);
    // Stuff all types from all modes into the default mode.
    const TypeSetByHwMode &LTS = TP.getDAGPatterns().getLegalTypes();
    for (const auto &I : LTS)
      LegalTypes.insert(I.second);
    LegalTypesCached = true;
  }
  assert(LegalCache.isDefaultOnly() && "Default-mode only expected");
  return LegalCache;
}

#ifndef NDEBUG
TypeInfer::ValidateOnExit::~ValidateOnExit() {
  if (Infer.Validate && !VTS.validate()) {
    dbgs() << "Type set is empty for each HW mode:\n"
              "possible type contradiction in the pattern below "
              "(use -print-records with llvm-tblgen to see all "
              "expanded records).\n";
    Infer.TP.dump();
    llvm_unreachable(nullptr);
  }
}
#endif


//===----------------------------------------------------------------------===//
// ScopedName Implementation
//===----------------------------------------------------------------------===//

bool ScopedName::operator==(const ScopedName &o) const {
  return Scope == o.Scope && Identifier == o.Identifier;
}

bool ScopedName::operator!=(const ScopedName &o) const {
  return !(*this == o);
}


//===----------------------------------------------------------------------===//
// TreePredicateFn Implementation
//===----------------------------------------------------------------------===//

/// TreePredicateFn constructor.  Here 'N' is a subclass of PatFrag.
TreePredicateFn::TreePredicateFn(TreePattern *N) : PatFragRec(N) {
  assert(
      (!hasPredCode() || !hasImmCode()) &&
      ".td file corrupt: can't have a node predicate *and* an imm predicate");
}

bool TreePredicateFn::hasPredCode() const {
  return isLoad() || isStore() || isAtomic() ||
         !PatFragRec->getRecord()->getValueAsString("PredicateCode").empty();
}

std::string TreePredicateFn::getPredCode() const {
  std::string Code = "";

  if (!isLoad() && !isStore() && !isAtomic()) {
    Record *MemoryVT = getMemoryVT();

    if (MemoryVT)
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "MemoryVT requires IsLoad or IsStore");
  }

  if (!isLoad() && !isStore()) {
    if (isUnindexed())
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "IsUnindexed requires IsLoad or IsStore");

    Record *ScalarMemoryVT = getScalarMemoryVT();

    if (ScalarMemoryVT)
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "ScalarMemoryVT requires IsLoad or IsStore");
  }

  if (isLoad() + isStore() + isAtomic() > 1)
    PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                    "IsLoad, IsStore, and IsAtomic are mutually exclusive");

  if (isLoad()) {
    if (!isUnindexed() && !isNonExtLoad() && !isAnyExtLoad() &&
        !isSignExtLoad() && !isZeroExtLoad() && getMemoryVT() == nullptr &&
        getScalarMemoryVT() == nullptr)
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "IsLoad cannot be used by itself");
  } else {
    if (isNonExtLoad())
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "IsNonExtLoad requires IsLoad");
    if (isAnyExtLoad())
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "IsAnyExtLoad requires IsLoad");
    if (isSignExtLoad())
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "IsSignExtLoad requires IsLoad");
    if (isZeroExtLoad())
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "IsZeroExtLoad requires IsLoad");
  }

  if (isStore()) {
    if (!isUnindexed() && !isTruncStore() && !isNonTruncStore() &&
        getMemoryVT() == nullptr && getScalarMemoryVT() == nullptr)
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "IsStore cannot be used by itself");
  } else {
    if (isNonTruncStore())
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "IsNonTruncStore requires IsStore");
    if (isTruncStore())
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "IsTruncStore requires IsStore");
  }

  if (isAtomic()) {
    if (getMemoryVT() == nullptr && !isAtomicOrderingMonotonic() &&
        !isAtomicOrderingAcquire() && !isAtomicOrderingRelease() &&
        !isAtomicOrderingAcquireRelease() &&
        !isAtomicOrderingSequentiallyConsistent() &&
        !isAtomicOrderingAcquireOrStronger() &&
        !isAtomicOrderingReleaseOrStronger() &&
        !isAtomicOrderingWeakerThanAcquire() &&
        !isAtomicOrderingWeakerThanRelease())
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "IsAtomic cannot be used by itself");
  } else {
    if (isAtomicOrderingMonotonic())
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "IsAtomicOrderingMonotonic requires IsAtomic");
    if (isAtomicOrderingAcquire())
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "IsAtomicOrderingAcquire requires IsAtomic");
    if (isAtomicOrderingRelease())
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "IsAtomicOrderingRelease requires IsAtomic");
    if (isAtomicOrderingAcquireRelease())
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "IsAtomicOrderingAcquireRelease requires IsAtomic");
    if (isAtomicOrderingSequentiallyConsistent())
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "IsAtomicOrderingSequentiallyConsistent requires IsAtomic");
    if (isAtomicOrderingAcquireOrStronger())
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "IsAtomicOrderingAcquireOrStronger requires IsAtomic");
    if (isAtomicOrderingReleaseOrStronger())
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "IsAtomicOrderingReleaseOrStronger requires IsAtomic");
    if (isAtomicOrderingWeakerThanAcquire())
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "IsAtomicOrderingWeakerThanAcquire requires IsAtomic");
  }

  if (isLoad() || isStore() || isAtomic()) {
    StringRef SDNodeName =
        isLoad() ? "LoadSDNode" : isStore() ? "StoreSDNode" : "AtomicSDNode";

    Record *MemoryVT = getMemoryVT();

    if (MemoryVT)
      Code += ("if (cast<" + SDNodeName + ">(N)->getMemoryVT() != MVT::" +
               MemoryVT->getName() + ") return false;\n")
                  .str();
  }

  if (isAtomic() && isAtomicOrderingMonotonic())
    Code += "if (cast<AtomicSDNode>(N)->getOrdering() != "
            "AtomicOrdering::Monotonic) return false;\n";
  if (isAtomic() && isAtomicOrderingAcquire())
    Code += "if (cast<AtomicSDNode>(N)->getOrdering() != "
            "AtomicOrdering::Acquire) return false;\n";
  if (isAtomic() && isAtomicOrderingRelease())
    Code += "if (cast<AtomicSDNode>(N)->getOrdering() != "
            "AtomicOrdering::Release) return false;\n";
  if (isAtomic() && isAtomicOrderingAcquireRelease())
    Code += "if (cast<AtomicSDNode>(N)->getOrdering() != "
            "AtomicOrdering::AcquireRelease) return false;\n";
  if (isAtomic() && isAtomicOrderingSequentiallyConsistent())
    Code += "if (cast<AtomicSDNode>(N)->getOrdering() != "
            "AtomicOrdering::SequentiallyConsistent) return false;\n";

  if (isAtomic() && isAtomicOrderingAcquireOrStronger())
    Code += "if (!isAcquireOrStronger(cast<AtomicSDNode>(N)->getOrdering())) "
            "return false;\n";
  if (isAtomic() && isAtomicOrderingWeakerThanAcquire())
    Code += "if (isAcquireOrStronger(cast<AtomicSDNode>(N)->getOrdering())) "
            "return false;\n";

  if (isAtomic() && isAtomicOrderingReleaseOrStronger())
    Code += "if (!isReleaseOrStronger(cast<AtomicSDNode>(N)->getOrdering())) "
            "return false;\n";
  if (isAtomic() && isAtomicOrderingWeakerThanRelease())
    Code += "if (isReleaseOrStronger(cast<AtomicSDNode>(N)->getOrdering())) "
            "return false;\n";

  if (isLoad() || isStore()) {
    StringRef SDNodeName = isLoad() ? "LoadSDNode" : "StoreSDNode";

    if (isUnindexed())
      Code += ("if (cast<" + SDNodeName +
               ">(N)->getAddressingMode() != ISD::UNINDEXED) "
               "return false;\n")
                  .str();

    if (isLoad()) {
      if ((isNonExtLoad() + isAnyExtLoad() + isSignExtLoad() +
           isZeroExtLoad()) > 1)
        PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                        "IsNonExtLoad, IsAnyExtLoad, IsSignExtLoad, and "
                        "IsZeroExtLoad are mutually exclusive");
      if (isNonExtLoad())
        Code += "if (cast<LoadSDNode>(N)->getExtensionType() != "
                "ISD::NON_EXTLOAD) return false;\n";
      if (isAnyExtLoad())
        Code += "if (cast<LoadSDNode>(N)->getExtensionType() != ISD::EXTLOAD) "
                "return false;\n";
      if (isSignExtLoad())
        Code += "if (cast<LoadSDNode>(N)->getExtensionType() != ISD::SEXTLOAD) "
                "return false;\n";
      if (isZeroExtLoad())
        Code += "if (cast<LoadSDNode>(N)->getExtensionType() != ISD::ZEXTLOAD) "
                "return false;\n";
    } else {
      if ((isNonTruncStore() + isTruncStore()) > 1)
        PrintFatalError(
            getOrigPatFragRecord()->getRecord()->getLoc(),
            "IsNonTruncStore, and IsTruncStore are mutually exclusive");
      if (isNonTruncStore())
        Code +=
            " if (cast<StoreSDNode>(N)->isTruncatingStore()) return false;\n";
      if (isTruncStore())
        Code +=
            " if (!cast<StoreSDNode>(N)->isTruncatingStore()) return false;\n";
    }

    Record *ScalarMemoryVT = getScalarMemoryVT();

    if (ScalarMemoryVT)
      Code += ("if (cast<" + SDNodeName +
               ">(N)->getMemoryVT().getScalarType() != MVT::" +
               ScalarMemoryVT->getName() + ") return false;\n")
                  .str();
  }

  std::string PredicateCode = PatFragRec->getRecord()->getValueAsString("PredicateCode");

  Code += PredicateCode;

  if (PredicateCode.empty() && !Code.empty())
    Code += "return true;\n";

  return Code;
}

bool TreePredicateFn::hasImmCode() const {
  return !PatFragRec->getRecord()->getValueAsString("ImmediateCode").empty();
}

std::string TreePredicateFn::getImmCode() const {
  return PatFragRec->getRecord()->getValueAsString("ImmediateCode");
}

bool TreePredicateFn::immCodeUsesAPInt() const {
  return getOrigPatFragRecord()->getRecord()->getValueAsBit("IsAPInt");
}

bool TreePredicateFn::immCodeUsesAPFloat() const {
  bool Unset;
  // The return value will be false when IsAPFloat is unset.
  return getOrigPatFragRecord()->getRecord()->getValueAsBitOrUnset("IsAPFloat",
                                                                   Unset);
}

bool TreePredicateFn::isPredefinedPredicateEqualTo(StringRef Field,
                                                   bool Value) const {
  bool Unset;
  bool Result =
      getOrigPatFragRecord()->getRecord()->getValueAsBitOrUnset(Field, Unset);
  if (Unset)
    return false;
  return Result == Value;
}
bool TreePredicateFn::usesOperands() const {
  return isPredefinedPredicateEqualTo("PredicateCodeUsesOperands", true);
}
bool TreePredicateFn::isLoad() const {
  return isPredefinedPredicateEqualTo("IsLoad", true);
}
bool TreePredicateFn::isStore() const {
  return isPredefinedPredicateEqualTo("IsStore", true);
}
bool TreePredicateFn::isAtomic() const {
  return isPredefinedPredicateEqualTo("IsAtomic", true);
}
bool TreePredicateFn::isUnindexed() const {
  return isPredefinedPredicateEqualTo("IsUnindexed", true);
}
bool TreePredicateFn::isNonExtLoad() const {
  return isPredefinedPredicateEqualTo("IsNonExtLoad", true);
}
bool TreePredicateFn::isAnyExtLoad() const {
  return isPredefinedPredicateEqualTo("IsAnyExtLoad", true);
}
bool TreePredicateFn::isSignExtLoad() const {
  return isPredefinedPredicateEqualTo("IsSignExtLoad", true);
}
bool TreePredicateFn::isZeroExtLoad() const {
  return isPredefinedPredicateEqualTo("IsZeroExtLoad", true);
}
bool TreePredicateFn::isNonTruncStore() const {
  return isPredefinedPredicateEqualTo("IsTruncStore", false);
}
bool TreePredicateFn::isTruncStore() const {
  return isPredefinedPredicateEqualTo("IsTruncStore", true);
}
bool TreePredicateFn::isAtomicOrderingMonotonic() const {
  return isPredefinedPredicateEqualTo("IsAtomicOrderingMonotonic", true);
}
bool TreePredicateFn::isAtomicOrderingAcquire() const {
  return isPredefinedPredicateEqualTo("IsAtomicOrderingAcquire", true);
}
bool TreePredicateFn::isAtomicOrderingRelease() const {
  return isPredefinedPredicateEqualTo("IsAtomicOrderingRelease", true);
}
bool TreePredicateFn::isAtomicOrderingAcquireRelease() const {
  return isPredefinedPredicateEqualTo("IsAtomicOrderingAcquireRelease", true);
}
bool TreePredicateFn::isAtomicOrderingSequentiallyConsistent() const {
  return isPredefinedPredicateEqualTo("IsAtomicOrderingSequentiallyConsistent",
                                      true);
}
bool TreePredicateFn::isAtomicOrderingAcquireOrStronger() const {
  return isPredefinedPredicateEqualTo("IsAtomicOrderingAcquireOrStronger", true);
}
bool TreePredicateFn::isAtomicOrderingWeakerThanAcquire() const {
  return isPredefinedPredicateEqualTo("IsAtomicOrderingAcquireOrStronger", false);
}
bool TreePredicateFn::isAtomicOrderingReleaseOrStronger() const {
  return isPredefinedPredicateEqualTo("IsAtomicOrderingReleaseOrStronger", true);
}
bool TreePredicateFn::isAtomicOrderingWeakerThanRelease() const {
  return isPredefinedPredicateEqualTo("IsAtomicOrderingReleaseOrStronger", false);
}
Record *TreePredicateFn::getMemoryVT() const {
  Record *R = getOrigPatFragRecord()->getRecord();
  if (R->isValueUnset("MemoryVT"))
    return nullptr;
  return R->getValueAsDef("MemoryVT");
}
Record *TreePredicateFn::getScalarMemoryVT() const {
  Record *R = getOrigPatFragRecord()->getRecord();
  if (R->isValueUnset("ScalarMemoryVT"))
    return nullptr;
  return R->getValueAsDef("ScalarMemoryVT");
}
bool TreePredicateFn::hasGISelPredicateCode() const {
  return !PatFragRec->getRecord()
              ->getValueAsString("GISelPredicateCode")
              .empty();
}
std::string TreePredicateFn::getGISelPredicateCode() const {
  return PatFragRec->getRecord()->getValueAsString("GISelPredicateCode");
}

StringRef TreePredicateFn::getImmType() const {
  if (immCodeUsesAPInt())
    return "const APInt &";
  if (immCodeUsesAPFloat())
    return "const APFloat &";
  return "int64_t";
}

StringRef TreePredicateFn::getImmTypeIdentifier() const {
  if (immCodeUsesAPInt())
    return "APInt";
  else if (immCodeUsesAPFloat())
    return "APFloat";
  return "I64";
}

/// isAlwaysTrue - Return true if this is a noop predicate.
bool TreePredicateFn::isAlwaysTrue() const {
  return !hasPredCode() && !hasImmCode();
}

/// Return the name to use in the generated code to reference this, this is
/// "Predicate_foo" if from a pattern fragment "foo".
std::string TreePredicateFn::getFnName() const {
  return "Predicate_" + PatFragRec->getRecord()->getName().str();
}

/// getCodeToRunOnSDNode - Return the code for the function body that
/// evaluates this predicate.  The argument is expected to be in "Node",
/// not N.  This handles casting and conversion to a concrete node type as
/// appropriate.
std::string TreePredicateFn::getCodeToRunOnSDNode() const {
  // Handle immediate predicates first.
  std::string ImmCode = getImmCode();
  if (!ImmCode.empty()) {
    if (isLoad())
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "IsLoad cannot be used with ImmLeaf or its subclasses");
    if (isStore())
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "IsStore cannot be used with ImmLeaf or its subclasses");
    if (isUnindexed())
      PrintFatalError(
          getOrigPatFragRecord()->getRecord()->getLoc(),
          "IsUnindexed cannot be used with ImmLeaf or its subclasses");
    if (isNonExtLoad())
      PrintFatalError(
          getOrigPatFragRecord()->getRecord()->getLoc(),
          "IsNonExtLoad cannot be used with ImmLeaf or its subclasses");
    if (isAnyExtLoad())
      PrintFatalError(
          getOrigPatFragRecord()->getRecord()->getLoc(),
          "IsAnyExtLoad cannot be used with ImmLeaf or its subclasses");
    if (isSignExtLoad())
      PrintFatalError(
          getOrigPatFragRecord()->getRecord()->getLoc(),
          "IsSignExtLoad cannot be used with ImmLeaf or its subclasses");
    if (isZeroExtLoad())
      PrintFatalError(
          getOrigPatFragRecord()->getRecord()->getLoc(),
          "IsZeroExtLoad cannot be used with ImmLeaf or its subclasses");
    if (isNonTruncStore())
      PrintFatalError(
          getOrigPatFragRecord()->getRecord()->getLoc(),
          "IsNonTruncStore cannot be used with ImmLeaf or its subclasses");
    if (isTruncStore())
      PrintFatalError(
          getOrigPatFragRecord()->getRecord()->getLoc(),
          "IsTruncStore cannot be used with ImmLeaf or its subclasses");
    if (getMemoryVT())
      PrintFatalError(getOrigPatFragRecord()->getRecord()->getLoc(),
                      "MemoryVT cannot be used with ImmLeaf or its subclasses");
    if (getScalarMemoryVT())
      PrintFatalError(
          getOrigPatFragRecord()->getRecord()->getLoc(),
          "ScalarMemoryVT cannot be used with ImmLeaf or its subclasses");

    std::string Result = ("    " + getImmType() + " Imm = ").str();
    if (immCodeUsesAPFloat())
      Result += "cast<ConstantFPSDNode>(Node)->getValueAPF();\n";
    else if (immCodeUsesAPInt())
      Result += "cast<ConstantSDNode>(Node)->getAPIntValue();\n";
    else
      Result += "cast<ConstantSDNode>(Node)->getSExtValue();\n";
    return Result + ImmCode;
  }

  // Handle arbitrary node predicates.
  assert(hasPredCode() && "Don't have any predicate code!");
  StringRef ClassName;
  if (PatFragRec->getOnlyTree()->isLeaf())
    ClassName = "SDNode";
  else {
    Record *Op = PatFragRec->getOnlyTree()->getOperator();
    ClassName = PatFragRec->getDAGPatterns().getSDNodeInfo(Op).getSDClassName();
  }
  std::string Result;
  if (ClassName == "SDNode")
    Result = "    SDNode *N = Node;\n";
  else
    Result = "    auto *N = cast<" + ClassName.str() + ">(Node);\n";

  return (Twine(Result) + "    (void)N;\n" + getPredCode()).str();
}

//===----------------------------------------------------------------------===//
// PatternToMatch implementation
//

/// getPatternSize - Return the 'size' of this pattern.  We want to match large
/// patterns before small ones.  This is used to determine the size of a
/// pattern.
static unsigned getPatternSize(const TreePatternNode *P,
                               const CodeGenDAGPatterns &CGP) {
  unsigned Size = 3;  // The node itself.
  // If the root node is a ConstantSDNode, increases its size.
  // e.g. (set R32:$dst, 0).
  if (P->isLeaf() && isa<IntInit>(P->getLeafValue()))
    Size += 2;

  if (const ComplexPattern *AM = P->getComplexPatternInfo(CGP)) {
    Size += AM->getComplexity();
    // We don't want to count any children twice, so return early.
    return Size;
  }

  // If this node has some predicate function that must match, it adds to the
  // complexity of this node.
  if (!P->getPredicateCalls().empty())
    ++Size;

  // Count children in the count if they are also nodes.
  for (unsigned i = 0, e = P->getNumChildren(); i != e; ++i) {
    const TreePatternNode *Child = P->getChild(i);
    if (!Child->isLeaf() && Child->getNumTypes()) {
      const TypeSetByHwMode &T0 = Child->getExtType(0);
      // At this point, all variable type sets should be simple, i.e. only
      // have a default mode.
      if (T0.getMachineValueType() != MVT::Other) {
        Size += getPatternSize(Child, CGP);
        continue;
      }
    }
    if (Child->isLeaf()) {
      if (isa<IntInit>(Child->getLeafValue()))
        Size += 5;  // Matches a ConstantSDNode (+3) and a specific value (+2).
      else if (Child->getComplexPatternInfo(CGP))
        Size += getPatternSize(Child, CGP);
      else if (!Child->getPredicateCalls().empty())
        ++Size;
    }
  }

  return Size;
}

/// Compute the complexity metric for the input pattern.  This roughly
/// corresponds to the number of nodes that are covered.
int PatternToMatch::
getPatternComplexity(const CodeGenDAGPatterns &CGP) const {
  return getPatternSize(getSrcPattern(), CGP) + getAddedComplexity();
}

/// getPredicateCheck - Return a single string containing all of this
/// pattern's predicates concatenated with "&&" operators.
///
std::string PatternToMatch::getPredicateCheck() const {
  SmallVector<const Predicate*,4> PredList;
  for (const Predicate &P : Predicates)
    PredList.push_back(&P);
  llvm::sort(PredList, deref<llvm::less>());

  std::string Check;
  for (unsigned i = 0, e = PredList.size(); i != e; ++i) {
    if (i != 0)
      Check += " && ";
    Check += '(' + PredList[i]->getCondString() + ')';
  }
  return Check;
}

//===----------------------------------------------------------------------===//
// SDTypeConstraint implementation
//

SDTypeConstraint::SDTypeConstraint(Record *R, const CodeGenHwModes &CGH) {
  OperandNo = R->getValueAsInt("OperandNum");

  if (R->isSubClassOf("SDTCisVT")) {
    ConstraintType = SDTCisVT;
    VVT = getValueTypeByHwMode(R->getValueAsDef("VT"), CGH);
    for (const auto &P : VVT)
      if (P.second == MVT::isVoid)
        PrintFatalError(R->getLoc(), "Cannot use 'Void' as type to SDTCisVT");
  } else if (R->isSubClassOf("SDTCisPtrTy")) {
    ConstraintType = SDTCisPtrTy;
  } else if (R->isSubClassOf("SDTCisInt")) {
    ConstraintType = SDTCisInt;
  } else if (R->isSubClassOf("SDTCisFP")) {
    ConstraintType = SDTCisFP;
  } else if (R->isSubClassOf("SDTCisVec")) {
    ConstraintType = SDTCisVec;
  } else if (R->isSubClassOf("SDTCisSameAs")) {
    ConstraintType = SDTCisSameAs;
    x.SDTCisSameAs_Info.OtherOperandNum = R->getValueAsInt("OtherOperandNum");
  } else if (R->isSubClassOf("SDTCisVTSmallerThanOp")) {
    ConstraintType = SDTCisVTSmallerThanOp;
    x.SDTCisVTSmallerThanOp_Info.OtherOperandNum =
      R->getValueAsInt("OtherOperandNum");
  } else if (R->isSubClassOf("SDTCisOpSmallerThanOp")) {
    ConstraintType = SDTCisOpSmallerThanOp;
    x.SDTCisOpSmallerThanOp_Info.BigOperandNum =
      R->getValueAsInt("BigOperandNum");
  } else if (R->isSubClassOf("SDTCisEltOfVec")) {
    ConstraintType = SDTCisEltOfVec;
    x.SDTCisEltOfVec_Info.OtherOperandNum = R->getValueAsInt("OtherOpNum");
  } else if (R->isSubClassOf("SDTCisSubVecOfVec")) {
    ConstraintType = SDTCisSubVecOfVec;
    x.SDTCisSubVecOfVec_Info.OtherOperandNum =
      R->getValueAsInt("OtherOpNum");
  } else if (R->isSubClassOf("SDTCVecEltisVT")) {
    ConstraintType = SDTCVecEltisVT;
    VVT = getValueTypeByHwMode(R->getValueAsDef("VT"), CGH);
    for (const auto &P : VVT) {
      MVT T = P.second;
      if (T.isVector())
        PrintFatalError(R->getLoc(),
                        "Cannot use vector type as SDTCVecEltisVT");
      if (!T.isInteger() && !T.isFloatingPoint())
        PrintFatalError(R->getLoc(), "Must use integer or floating point type "
                                     "as SDTCVecEltisVT");
    }
  } else if (R->isSubClassOf("SDTCisSameNumEltsAs")) {
    ConstraintType = SDTCisSameNumEltsAs;
    x.SDTCisSameNumEltsAs_Info.OtherOperandNum =
      R->getValueAsInt("OtherOperandNum");
  } else if (R->isSubClassOf("SDTCisSameSizeAs")) {
    ConstraintType = SDTCisSameSizeAs;
    x.SDTCisSameSizeAs_Info.OtherOperandNum =
      R->getValueAsInt("OtherOperandNum");
  } else {
    PrintFatalError("Unrecognized SDTypeConstraint '" + R->getName() + "'!\n");
  }
}

/// getOperandNum - Return the node corresponding to operand #OpNo in tree
/// N, and the result number in ResNo.
static TreePatternNode *getOperandNum(unsigned OpNo, TreePatternNode *N,
                                      const SDNodeInfo &NodeInfo,
                                      unsigned &ResNo) {
  unsigned NumResults = NodeInfo.getNumResults();
  if (OpNo < NumResults) {
    ResNo = OpNo;
    return N;
  }

  OpNo -= NumResults;

  if (OpNo >= N->getNumChildren()) {
    std::string S;
    raw_string_ostream OS(S);
    OS << "Invalid operand number in type constraint "
           << (OpNo+NumResults) << " ";
    N->print(OS);
    PrintFatalError(OS.str());
  }

  return N->getChild(OpNo);
}

/// ApplyTypeConstraint - Given a node in a pattern, apply this type
/// constraint to the nodes operands.  This returns true if it makes a
/// change, false otherwise.  If a type contradiction is found, flag an error.
bool SDTypeConstraint::ApplyTypeConstraint(TreePatternNode *N,
                                           const SDNodeInfo &NodeInfo,
                                           TreePattern &TP) const {
  if (TP.hasError())
    return false;

  unsigned ResNo = 0; // The result number being referenced.
  TreePatternNode *NodeToApply = getOperandNum(OperandNo, N, NodeInfo, ResNo);
  TypeInfer &TI = TP.getInfer();

  switch (ConstraintType) {
  case SDTCisVT:
    // Operand must be a particular type.
    return NodeToApply->UpdateNodeType(ResNo, VVT, TP);
  case SDTCisPtrTy:
    // Operand must be same as target pointer type.
    return NodeToApply->UpdateNodeType(ResNo, MVT::iPTR, TP);
  case SDTCisInt:
    // Require it to be one of the legal integer VTs.
     return TI.EnforceInteger(NodeToApply->getExtType(ResNo));
  case SDTCisFP:
    // Require it to be one of the legal fp VTs.
    return TI.EnforceFloatingPoint(NodeToApply->getExtType(ResNo));
  case SDTCisVec:
    // Require it to be one of the legal vector VTs.
    return TI.EnforceVector(NodeToApply->getExtType(ResNo));
  case SDTCisSameAs: {
    unsigned OResNo = 0;
    TreePatternNode *OtherNode =
      getOperandNum(x.SDTCisSameAs_Info.OtherOperandNum, N, NodeInfo, OResNo);
    return NodeToApply->UpdateNodeType(ResNo, OtherNode->getExtType(OResNo),TP)|
           OtherNode->UpdateNodeType(OResNo,NodeToApply->getExtType(ResNo),TP);
  }
  case SDTCisVTSmallerThanOp: {
    // The NodeToApply must be a leaf node that is a VT.  OtherOperandNum must
    // have an integer type that is smaller than the VT.
    if (!NodeToApply->isLeaf() ||
        !isa<DefInit>(NodeToApply->getLeafValue()) ||
        !static_cast<DefInit*>(NodeToApply->getLeafValue())->getDef()
               ->isSubClassOf("ValueType")) {
      TP.error(N->getOperator()->getName() + " expects a VT operand!");
      return false;
    }
    DefInit *DI = static_cast<DefInit*>(NodeToApply->getLeafValue());
    const CodeGenTarget &T = TP.getDAGPatterns().getTargetInfo();
    auto VVT = getValueTypeByHwMode(DI->getDef(), T.getHwModes());
    TypeSetByHwMode TypeListTmp(VVT);

    unsigned OResNo = 0;
    TreePatternNode *OtherNode =
      getOperandNum(x.SDTCisVTSmallerThanOp_Info.OtherOperandNum, N, NodeInfo,
                    OResNo);

    return TI.EnforceSmallerThan(TypeListTmp, OtherNode->getExtType(OResNo));
  }
  case SDTCisOpSmallerThanOp: {
    unsigned BResNo = 0;
    TreePatternNode *BigOperand =
      getOperandNum(x.SDTCisOpSmallerThanOp_Info.BigOperandNum, N, NodeInfo,
                    BResNo);
    return TI.EnforceSmallerThan(NodeToApply->getExtType(ResNo),
                                 BigOperand->getExtType(BResNo));
  }
  case SDTCisEltOfVec: {
    unsigned VResNo = 0;
    TreePatternNode *VecOperand =
      getOperandNum(x.SDTCisEltOfVec_Info.OtherOperandNum, N, NodeInfo,
                    VResNo);
    // Filter vector types out of VecOperand that don't have the right element
    // type.
    return TI.EnforceVectorEltTypeIs(VecOperand->getExtType(VResNo),
                                     NodeToApply->getExtType(ResNo));
  }
  case SDTCisSubVecOfVec: {
    unsigned VResNo = 0;
    TreePatternNode *BigVecOperand =
      getOperandNum(x.SDTCisSubVecOfVec_Info.OtherOperandNum, N, NodeInfo,
                    VResNo);

    // Filter vector types out of BigVecOperand that don't have the
    // right subvector type.
    return TI.EnforceVectorSubVectorTypeIs(BigVecOperand->getExtType(VResNo),
                                           NodeToApply->getExtType(ResNo));
  }
  case SDTCVecEltisVT: {
    return TI.EnforceVectorEltTypeIs(NodeToApply->getExtType(ResNo), VVT);
  }
  case SDTCisSameNumEltsAs: {
    unsigned OResNo = 0;
    TreePatternNode *OtherNode =
      getOperandNum(x.SDTCisSameNumEltsAs_Info.OtherOperandNum,
                    N, NodeInfo, OResNo);
    return TI.EnforceSameNumElts(OtherNode->getExtType(OResNo),
                                 NodeToApply->getExtType(ResNo));
  }
  case SDTCisSameSizeAs: {
    unsigned OResNo = 0;
    TreePatternNode *OtherNode =
      getOperandNum(x.SDTCisSameSizeAs_Info.OtherOperandNum,
                    N, NodeInfo, OResNo);
    return TI.EnforceSameSize(OtherNode->getExtType(OResNo),
                              NodeToApply->getExtType(ResNo));
  }
  }
  llvm_unreachable("Invalid ConstraintType!");
}

// Update the node type to match an instruction operand or result as specified
// in the ins or outs lists on the instruction definition. Return true if the
// type was actually changed.
bool TreePatternNode::UpdateNodeTypeFromInst(unsigned ResNo,
                                             Record *Operand,
                                             TreePattern &TP) {
  // The 'unknown' operand indicates that types should be inferred from the
  // context.
  if (Operand->isSubClassOf("unknown_class"))
    return false;

  // The Operand class specifies a type directly.
  if (Operand->isSubClassOf("Operand")) {
    Record *R = Operand->getValueAsDef("Type");
    const CodeGenTarget &T = TP.getDAGPatterns().getTargetInfo();
    return UpdateNodeType(ResNo, getValueTypeByHwMode(R, T.getHwModes()), TP);
  }

  // PointerLikeRegClass has a type that is determined at runtime.
  if (Operand->isSubClassOf("PointerLikeRegClass"))
    return UpdateNodeType(ResNo, MVT::iPTR, TP);

  // Both RegisterClass and RegisterOperand operands derive their types from a
  // register class def.
  Record *RC = nullptr;
  if (Operand->isSubClassOf("RegisterClass"))
    RC = Operand;
  else if (Operand->isSubClassOf("RegisterOperand"))
    RC = Operand->getValueAsDef("RegClass");

  assert(RC && "Unknown operand type");
  CodeGenTarget &Tgt = TP.getDAGPatterns().getTargetInfo();
  return UpdateNodeType(ResNo, Tgt.getRegisterClass(RC).getValueTypes(), TP);
}

bool TreePatternNode::ContainsUnresolvedType(TreePattern &TP) const {
  for (unsigned i = 0, e = Types.size(); i != e; ++i)
    if (!TP.getInfer().isConcrete(Types[i], true))
      return true;
  for (unsigned i = 0, e = getNumChildren(); i != e; ++i)
    if (getChild(i)->ContainsUnresolvedType(TP))
      return true;
  return false;
}

bool TreePatternNode::hasProperTypeByHwMode() const {
  for (const TypeSetByHwMode &S : Types)
    if (!S.isDefaultOnly())
      return true;
  for (const TreePatternNodePtr &C : Children)
    if (C->hasProperTypeByHwMode())
      return true;
  return false;
}

bool TreePatternNode::hasPossibleType() const {
  for (const TypeSetByHwMode &S : Types)
    if (!S.isPossible())
      return false;
  for (const TreePatternNodePtr &C : Children)
    if (!C->hasPossibleType())
      return false;
  return true;
}

bool TreePatternNode::setDefaultMode(unsigned Mode) {
  for (TypeSetByHwMode &S : Types) {
    S.makeSimple(Mode);
    // Check if the selected mode had a type conflict.
    if (S.get(DefaultMode).empty())
      return false;
  }
  for (const TreePatternNodePtr &C : Children)
    if (!C->setDefaultMode(Mode))
      return false;
  return true;
}

//===----------------------------------------------------------------------===//
// SDNodeInfo implementation
//
SDNodeInfo::SDNodeInfo(Record *R, const CodeGenHwModes &CGH) : Def(R) {
  EnumName    = R->getValueAsString("Opcode");
  SDClassName = R->getValueAsString("SDClass");
  Record *TypeProfile = R->getValueAsDef("TypeProfile");
  NumResults = TypeProfile->getValueAsInt("NumResults");
  NumOperands = TypeProfile->getValueAsInt("NumOperands");

  // Parse the properties.
  Properties = parseSDPatternOperatorProperties(R);

  // Parse the type constraints.
  std::vector<Record*> ConstraintList =
    TypeProfile->getValueAsListOfDefs("Constraints");
  for (Record *R : ConstraintList)
    TypeConstraints.emplace_back(R, CGH);
}

/// getKnownType - If the type constraints on this node imply a fixed type
/// (e.g. all stores return void, etc), then return it as an
/// MVT::SimpleValueType.  Otherwise, return EEVT::Other.
MVT::SimpleValueType SDNodeInfo::getKnownType(unsigned ResNo) const {
  unsigned NumResults = getNumResults();
  assert(NumResults <= 1 &&
         "We only work with nodes with zero or one result so far!");
  assert(ResNo == 0 && "Only handles single result nodes so far");

  for (const SDTypeConstraint &Constraint : TypeConstraints) {
    // Make sure that this applies to the correct node result.
    if (Constraint.OperandNo >= NumResults)  // FIXME: need value #
      continue;

    switch (Constraint.ConstraintType) {
    default: break;
    case SDTypeConstraint::SDTCisVT:
      if (Constraint.VVT.isSimple())
        return Constraint.VVT.getSimple().SimpleTy;
      break;
    case SDTypeConstraint::SDTCisPtrTy:
      return MVT::iPTR;
    }
  }
  return MVT::Other;
}

//===----------------------------------------------------------------------===//
// TreePatternNode implementation
//

static unsigned GetNumNodeResults(Record *Operator, CodeGenDAGPatterns &CDP) {
  if (Operator->getName() == "set" ||
      Operator->getName() == "implicit")
    return 0;  // All return nothing.

  if (Operator->isSubClassOf("Intrinsic"))
    return CDP.getIntrinsic(Operator).IS.RetVTs.size();

  if (Operator->isSubClassOf("SDNode"))
    return CDP.getSDNodeInfo(Operator).getNumResults();

  if (Operator->isSubClassOf("PatFrags")) {
    // If we've already parsed this pattern fragment, get it.  Otherwise, handle
    // the forward reference case where one pattern fragment references another
    // before it is processed.
    if (TreePattern *PFRec = CDP.getPatternFragmentIfRead(Operator)) {
      // The number of results of a fragment with alternative records is the
      // maximum number of results across all alternatives.
      unsigned NumResults = 0;
      for (auto T : PFRec->getTrees())
        NumResults = std::max(NumResults, T->getNumTypes());
      return NumResults;
    }

    ListInit *LI = Operator->getValueAsListInit("Fragments");
    assert(LI && "Invalid Fragment");
    unsigned NumResults = 0;
    for (Init *I : LI->getValues()) {
      Record *Op = nullptr;
      if (DagInit *Dag = dyn_cast<DagInit>(I))
        if (DefInit *DI = dyn_cast<DefInit>(Dag->getOperator()))
          Op = DI->getDef();
      assert(Op && "Invalid Fragment");
      NumResults = std::max(NumResults, GetNumNodeResults(Op, CDP));
    }
    return NumResults;
  }

  if (Operator->isSubClassOf("Instruction")) {
    CodeGenInstruction &InstInfo = CDP.getTargetInfo().getInstruction(Operator);

    unsigned NumDefsToAdd = InstInfo.Operands.NumDefs;

    // Subtract any defaulted outputs.
    for (unsigned i = 0; i != InstInfo.Operands.NumDefs; ++i) {
      Record *OperandNode = InstInfo.Operands[i].Rec;

      if (OperandNode->isSubClassOf("OperandWithDefaultOps") &&
          !CDP.getDefaultOperand(OperandNode).DefaultOps.empty())
        --NumDefsToAdd;
    }

    // Add on one implicit def if it has a resolvable type.
    if (InstInfo.HasOneImplicitDefWithKnownVT(CDP.getTargetInfo()) !=MVT::Other)
      ++NumDefsToAdd;
    return NumDefsToAdd;
  }

  if (Operator->isSubClassOf("SDNodeXForm"))
    return 1;  // FIXME: Generalize SDNodeXForm

  if (Operator->isSubClassOf("ValueType"))
    return 1;  // A type-cast of one result.

  if (Operator->isSubClassOf("ComplexPattern"))
    return 1;

  errs() << *Operator;
  PrintFatalError("Unhandled node in GetNumNodeResults");
}

void TreePatternNode::print(raw_ostream &OS) const {
  if (isLeaf())
    OS << *getLeafValue();
  else
    OS << '(' << getOperator()->getName();

  for (unsigned i = 0, e = Types.size(); i != e; ++i) {
    OS << ':';
    getExtType(i).writeToStream(OS);
  }

  if (!isLeaf()) {
    if (getNumChildren() != 0) {
      OS << " ";
      getChild(0)->print(OS);
      for (unsigned i = 1, e = getNumChildren(); i != e; ++i) {
        OS << ", ";
        getChild(i)->print(OS);
      }
    }
    OS << ")";
  }

  for (const TreePredicateCall &Pred : PredicateCalls) {
    OS << "<<P:";
    if (Pred.Scope)
      OS << Pred.Scope << ":";
    OS << Pred.Fn.getFnName() << ">>";
  }
  if (TransformFn)
    OS << "<<X:" << TransformFn->getName() << ">>";
  if (!getName().empty())
    OS << ":$" << getName();

  for (const ScopedName &Name : NamesAsPredicateArg)
    OS << ":$pred:" << Name.getScope() << ":" << Name.getIdentifier();
}
void TreePatternNode::dump() const {
  print(errs());
}

/// isIsomorphicTo - Return true if this node is recursively
/// isomorphic to the specified node.  For this comparison, the node's
/// entire state is considered. The assigned name is ignored, since
/// nodes with differing names are considered isomorphic. However, if
/// the assigned name is present in the dependent variable set, then
/// the assigned name is considered significant and the node is
/// isomorphic if the names match.
bool TreePatternNode::isIsomorphicTo(const TreePatternNode *N,
                                     const MultipleUseVarSet &DepVars) const {
  if (N == this) return true;
  if (N->isLeaf() != isLeaf() || getExtTypes() != N->getExtTypes() ||
      getPredicateCalls() != N->getPredicateCalls() ||
      getTransformFn() != N->getTransformFn())
    return false;

  if (isLeaf()) {
    if (DefInit *DI = dyn_cast<DefInit>(getLeafValue())) {
      if (DefInit *NDI = dyn_cast<DefInit>(N->getLeafValue())) {
        return ((DI->getDef() == NDI->getDef())
                && (DepVars.find(getName()) == DepVars.end()
                    || getName() == N->getName()));
      }
    }
    return getLeafValue() == N->getLeafValue();
  }

  if (N->getOperator() != getOperator() ||
      N->getNumChildren() != getNumChildren()) return false;
  for (unsigned i = 0, e = getNumChildren(); i != e; ++i)
    if (!getChild(i)->isIsomorphicTo(N->getChild(i), DepVars))
      return false;
  return true;
}

/// clone - Make a copy of this tree and all of its children.
///
TreePatternNodePtr TreePatternNode::clone() const {
  TreePatternNodePtr New;
  if (isLeaf()) {
    New = std::make_shared<TreePatternNode>(getLeafValue(), getNumTypes());
  } else {
    std::vector<TreePatternNodePtr> CChildren;
    CChildren.reserve(Children.size());
    for (unsigned i = 0, e = getNumChildren(); i != e; ++i)
      CChildren.push_back(getChild(i)->clone());
    New = std::make_shared<TreePatternNode>(getOperator(), std::move(CChildren),
                                            getNumTypes());
  }
  New->setName(getName());
  New->setNamesAsPredicateArg(getNamesAsPredicateArg());
  New->Types = Types;
  New->setPredicateCalls(getPredicateCalls());
  New->setTransformFn(getTransformFn());
  return New;
}

/// RemoveAllTypes - Recursively strip all the types of this tree.
void TreePatternNode::RemoveAllTypes() {
  // Reset to unknown type.
  std::fill(Types.begin(), Types.end(), TypeSetByHwMode());
  if (isLeaf()) return;
  for (unsigned i = 0, e = getNumChildren(); i != e; ++i)
    getChild(i)->RemoveAllTypes();
}


/// SubstituteFormalArguments - Replace the formal arguments in this tree
/// with actual values specified by ArgMap.
void TreePatternNode::SubstituteFormalArguments(
    std::map<std::string, TreePatternNodePtr> &ArgMap) {
  if (isLeaf()) return;

  for (unsigned i = 0, e = getNumChildren(); i != e; ++i) {
    TreePatternNode *Child = getChild(i);
    if (Child->isLeaf()) {
      Init *Val = Child->getLeafValue();
      // Note that, when substituting into an output pattern, Val might be an
      // UnsetInit.
      if (isa<UnsetInit>(Val) || (isa<DefInit>(Val) &&
          cast<DefInit>(Val)->getDef()->getName() == "node")) {
        // We found a use of a formal argument, replace it with its value.
        TreePatternNodePtr NewChild = ArgMap[Child->getName()];
        assert(NewChild && "Couldn't find formal argument!");
        assert((Child->getPredicateCalls().empty() ||
                NewChild->getPredicateCalls() == Child->getPredicateCalls()) &&
               "Non-empty child predicate clobbered!");
        setChild(i, std::move(NewChild));
      }
    } else {
      getChild(i)->SubstituteFormalArguments(ArgMap);
    }
  }
}


/// InlinePatternFragments - If this pattern refers to any pattern
/// fragments, return the set of inlined versions (this can be more than
/// one if a PatFrags record has multiple alternatives).
void TreePatternNode::InlinePatternFragments(
  TreePatternNodePtr T, TreePattern &TP,
  std::vector<TreePatternNodePtr> &OutAlternatives) {

  if (TP.hasError())
    return;

  if (isLeaf()) {
    OutAlternatives.push_back(T);  // nothing to do.
    return;
  }

  Record *Op = getOperator();

  if (!Op->isSubClassOf("PatFrags")) {
    if (getNumChildren() == 0) {
      OutAlternatives.push_back(T);
      return;
    }

    // Recursively inline children nodes.
    std::vector<std::vector<TreePatternNodePtr> > ChildAlternatives;
    ChildAlternatives.resize(getNumChildren());
    for (unsigned i = 0, e = getNumChildren(); i != e; ++i) {
      TreePatternNodePtr Child = getChildShared(i);
      Child->InlinePatternFragments(Child, TP, ChildAlternatives[i]);
      // If there are no alternatives for any child, there are no
      // alternatives for this expression as whole.
      if (ChildAlternatives[i].empty())
        return;

      for (auto NewChild : ChildAlternatives[i])
        assert((Child->getPredicateCalls().empty() ||
                NewChild->getPredicateCalls() == Child->getPredicateCalls()) &&
               "Non-empty child predicate clobbered!");
    }

    // The end result is an all-pairs construction of the resultant pattern.
    std::vector<unsigned> Idxs;
    Idxs.resize(ChildAlternatives.size());
    bool NotDone;
    do {
      // Create the variant and add it to the output list.
      std::vector<TreePatternNodePtr> NewChildren;
      for (unsigned i = 0, e = ChildAlternatives.size(); i != e; ++i)
        NewChildren.push_back(ChildAlternatives[i][Idxs[i]]);
      TreePatternNodePtr R = std::make_shared<TreePatternNode>(
          getOperator(), std::move(NewChildren), getNumTypes());

      // Copy over properties.
      R->setName(getName());
      R->setNamesAsPredicateArg(getNamesAsPredicateArg());
      R->setPredicateCalls(getPredicateCalls());
      R->setTransformFn(getTransformFn());
      for (unsigned i = 0, e = getNumTypes(); i != e; ++i)
        R->setType(i, getExtType(i));
      for (unsigned i = 0, e = getNumResults(); i != e; ++i)
        R->setResultIndex(i, getResultIndex(i));

      // Register alternative.
      OutAlternatives.push_back(R);

      // Increment indices to the next permutation by incrementing the
      // indices from last index backward, e.g., generate the sequence
      // [0, 0], [0, 1], [1, 0], [1, 1].
      int IdxsIdx;
      for (IdxsIdx = Idxs.size() - 1; IdxsIdx >= 0; --IdxsIdx) {
        if (++Idxs[IdxsIdx] == ChildAlternatives[IdxsIdx].size())
          Idxs[IdxsIdx] = 0;
        else
          break;
      }
      NotDone = (IdxsIdx >= 0);
    } while (NotDone);

    return;
  }

  // Otherwise, we found a reference to a fragment.  First, look up its
  // TreePattern record.
  TreePattern *Frag = TP.getDAGPatterns().getPatternFragment(Op);

  // Verify that we are passing the right number of operands.
  if (Frag->getNumArgs() != Children.size()) {
    TP.error("'" + Op->getName() + "' fragment requires " +
             Twine(Frag->getNumArgs()) + " operands!");
    return;
  }

  TreePredicateFn PredFn(Frag);
  unsigned Scope = 0;
  if (TreePredicateFn(Frag).usesOperands())
    Scope = TP.getDAGPatterns().allocateScope();

  // Compute the map of formal to actual arguments.
  std::map<std::string, TreePatternNodePtr> ArgMap;
  for (unsigned i = 0, e = Frag->getNumArgs(); i != e; ++i) {
    TreePatternNodePtr Child = getChildShared(i);
    if (Scope != 0) {
      Child = Child->clone();
      Child->addNameAsPredicateArg(ScopedName(Scope, Frag->getArgName(i)));
    }
    ArgMap[Frag->getArgName(i)] = Child;
  }

  // Loop over all fragment alternatives.
  for (auto Alternative : Frag->getTrees()) {
    TreePatternNodePtr FragTree = Alternative->clone();

    if (!PredFn.isAlwaysTrue())
      FragTree->addPredicateCall(PredFn, Scope);

    // Resolve formal arguments to their actual value.
    if (Frag->getNumArgs())
      FragTree->SubstituteFormalArguments(ArgMap);

    // Transfer types.  Note that the resolved alternative may have fewer
    // (but not more) results than the PatFrags node.
    FragTree->setName(getName());
    for (unsigned i = 0, e = FragTree->getNumTypes(); i != e; ++i)
      FragTree->UpdateNodeType(i, getExtType(i), TP);

    // Transfer in the old predicates.
    for (const TreePredicateCall &Pred : getPredicateCalls())
      FragTree->addPredicateCall(Pred);

    // The fragment we inlined could have recursive inlining that is needed.  See
    // if there are any pattern fragments in it and inline them as needed.
    FragTree->InlinePatternFragments(FragTree, TP, OutAlternatives);
  }
}

/// getImplicitType - Check to see if the specified record has an implicit
/// type which should be applied to it.  This will infer the type of register
/// references from the register file information, for example.
///
/// When Unnamed is set, return the type of a DAG operand with no name, such as
/// the F8RC register class argument in:
///
///   (COPY_TO_REGCLASS GPR:$src, F8RC)
///
/// When Unnamed is false, return the type of a named DAG operand such as the
/// GPR:$src operand above.
///
static TypeSetByHwMode getImplicitType(Record *R, unsigned ResNo,
                                       bool NotRegisters,
                                       bool Unnamed,
                                       TreePattern &TP) {
  CodeGenDAGPatterns &CDP = TP.getDAGPatterns();

  // Check to see if this is a register operand.
  if (R->isSubClassOf("RegisterOperand")) {
    assert(ResNo == 0 && "Regoperand ref only has one result!");
    if (NotRegisters)
      return TypeSetByHwMode(); // Unknown.
    Record *RegClass = R->getValueAsDef("RegClass");
    const CodeGenTarget &T = TP.getDAGPatterns().getTargetInfo();
    return TypeSetByHwMode(T.getRegisterClass(RegClass).getValueTypes());
  }

  // Check to see if this is a register or a register class.
  if (R->isSubClassOf("RegisterClass")) {
    assert(ResNo == 0 && "Regclass ref only has one result!");
    // An unnamed register class represents itself as an i32 immediate, for
    // example on a COPY_TO_REGCLASS instruction.
    if (Unnamed)
      return TypeSetByHwMode(MVT::i32);

    // In a named operand, the register class provides the possible set of
    // types.
    if (NotRegisters)
      return TypeSetByHwMode(); // Unknown.
    const CodeGenTarget &T = TP.getDAGPatterns().getTargetInfo();
    return TypeSetByHwMode(T.getRegisterClass(R).getValueTypes());
  }

  if (R->isSubClassOf("PatFrags")) {
    assert(ResNo == 0 && "FIXME: PatFrag with multiple results?");
    // Pattern fragment types will be resolved when they are inlined.
    return TypeSetByHwMode(); // Unknown.
  }

  if (R->isSubClassOf("Register")) {
    assert(ResNo == 0 && "Registers only produce one result!");
    if (NotRegisters)
      return TypeSetByHwMode(); // Unknown.
    const CodeGenTarget &T = TP.getDAGPatterns().getTargetInfo();
    return TypeSetByHwMode(T.getRegisterVTs(R));
  }

  if (R->isSubClassOf("SubRegIndex")) {
    assert(ResNo == 0 && "SubRegisterIndices only produce one result!");
    return TypeSetByHwMode(MVT::i32);
  }

  if (R->isSubClassOf("ValueType")) {
    assert(ResNo == 0 && "This node only has one result!");
    // An unnamed VTSDNode represents itself as an MVT::Other immediate.
    //
    //   (sext_inreg GPR:$src, i16)
    //                         ~~~
    if (Unnamed)
      return TypeSetByHwMode(MVT::Other);
    // With a name, the ValueType simply provides the type of the named
    // variable.
    //
    //   (sext_inreg i32:$src, i16)
    //               ~~~~~~~~
    if (NotRegisters)
      return TypeSetByHwMode(); // Unknown.
    const CodeGenHwModes &CGH = CDP.getTargetInfo().getHwModes();
    return TypeSetByHwMode(getValueTypeByHwMode(R, CGH));
  }

  if (R->isSubClassOf("CondCode")) {
    assert(ResNo == 0 && "This node only has one result!");
    // Using a CondCodeSDNode.
    return TypeSetByHwMode(MVT::Other);
  }

  if (R->isSubClassOf("ComplexPattern")) {
    assert(ResNo == 0 && "FIXME: ComplexPattern with multiple results?");
    if (NotRegisters)
      return TypeSetByHwMode(); // Unknown.
    return TypeSetByHwMode(CDP.getComplexPattern(R).getValueType());
  }
  if (R->isSubClassOf("PointerLikeRegClass")) {
    assert(ResNo == 0 && "Regclass can only have one result!");
    TypeSetByHwMode VTS(MVT::iPTR);
    TP.getInfer().expandOverloads(VTS);
    return VTS;
  }

  if (R->getName() == "node" || R->getName() == "srcvalue" ||
      R->getName() == "zero_reg") {
    // Placeholder.
    return TypeSetByHwMode(); // Unknown.
  }

  if (R->isSubClassOf("Operand")) {
    const CodeGenHwModes &CGH = CDP.getTargetInfo().getHwModes();
    Record *T = R->getValueAsDef("Type");
    return TypeSetByHwMode(getValueTypeByHwMode(T, CGH));
  }

  TP.error("Unknown node flavor used in pattern: " + R->getName());
  return TypeSetByHwMode(MVT::Other);
}


/// getIntrinsicInfo - If this node corresponds to an intrinsic, return the
/// CodeGenIntrinsic information for it, otherwise return a null pointer.
const CodeGenIntrinsic *TreePatternNode::
getIntrinsicInfo(const CodeGenDAGPatterns &CDP) const {
  if (getOperator() != CDP.get_intrinsic_void_sdnode() &&
      getOperator() != CDP.get_intrinsic_w_chain_sdnode() &&
      getOperator() != CDP.get_intrinsic_wo_chain_sdnode())
    return nullptr;

  unsigned IID = cast<IntInit>(getChild(0)->getLeafValue())->getValue();
  return &CDP.getIntrinsicInfo(IID);
}

/// getComplexPatternInfo - If this node corresponds to a ComplexPattern,
/// return the ComplexPattern information, otherwise return null.
const ComplexPattern *
TreePatternNode::getComplexPatternInfo(const CodeGenDAGPatterns &CGP) const {
  Record *Rec;
  if (isLeaf()) {
    DefInit *DI = dyn_cast<DefInit>(getLeafValue());
    if (!DI)
      return nullptr;
    Rec = DI->getDef();
  } else
    Rec = getOperator();

  if (!Rec->isSubClassOf("ComplexPattern"))
    return nullptr;
  return &CGP.getComplexPattern(Rec);
}

unsigned TreePatternNode::getNumMIResults(const CodeGenDAGPatterns &CGP) const {
  // A ComplexPattern specifically declares how many results it fills in.
  if (const ComplexPattern *CP = getComplexPatternInfo(CGP))
    return CP->getNumOperands();

  // If MIOperandInfo is specified, that gives the count.
  if (isLeaf()) {
    DefInit *DI = dyn_cast<DefInit>(getLeafValue());
    if (DI && DI->getDef()->isSubClassOf("Operand")) {
      DagInit *MIOps = DI->getDef()->getValueAsDag("MIOperandInfo");
      if (MIOps->getNumArgs())
        return MIOps->getNumArgs();
    }
  }

  // Otherwise there is just one result.
  return 1;
}

/// NodeHasProperty - Return true if this node has the specified property.
bool TreePatternNode::NodeHasProperty(SDNP Property,
                                      const CodeGenDAGPatterns &CGP) const {
  if (isLeaf()) {
    if (const ComplexPattern *CP = getComplexPatternInfo(CGP))
      return CP->hasProperty(Property);

    return false;
  }

  if (Property != SDNPHasChain) {
    // The chain proprety is already present on the different intrinsic node
    // types (intrinsic_w_chain, intrinsic_void), and is not explicitly listed
    // on the intrinsic. Anything else is specific to the individual intrinsic.
    if (const CodeGenIntrinsic *Int = getIntrinsicInfo(CGP))
      return Int->hasProperty(Property);
  }

  if (!Operator->isSubClassOf("SDPatternOperator"))
    return false;

  return CGP.getSDNodeInfo(Operator).hasProperty(Property);
}




/// TreeHasProperty - Return true if any node in this tree has the specified
/// property.
bool TreePatternNode::TreeHasProperty(SDNP Property,
                                      const CodeGenDAGPatterns &CGP) const {
  if (NodeHasProperty(Property, CGP))
    return true;
  for (unsigned i = 0, e = getNumChildren(); i != e; ++i)
    if (getChild(i)->TreeHasProperty(Property, CGP))
      return true;
  return false;
}

/// isCommutativeIntrinsic - Return true if the node corresponds to a
/// commutative intrinsic.
bool
TreePatternNode::isCommutativeIntrinsic(const CodeGenDAGPatterns &CDP) const {
  if (const CodeGenIntrinsic *Int = getIntrinsicInfo(CDP))
    return Int->isCommutative;
  return false;
}

static bool isOperandClass(const TreePatternNode *N, StringRef Class) {
  if (!N->isLeaf())
    return N->getOperator()->isSubClassOf(Class);

  DefInit *DI = dyn_cast<DefInit>(N->getLeafValue());
  if (DI && DI->getDef()->isSubClassOf(Class))
    return true;

  return false;
}

static void emitTooManyOperandsError(TreePattern &TP,
                                     StringRef InstName,
                                     unsigned Expected,
                                     unsigned Actual) {
  TP.error("Instruction '" + InstName + "' was provided " + Twine(Actual) +
           " operands but expected only " + Twine(Expected) + "!");
}

static void emitTooFewOperandsError(TreePattern &TP,
                                    StringRef InstName,
                                    unsigned Actual) {
  TP.error("Instruction '" + InstName +
           "' expects more than the provided " + Twine(Actual) + " operands!");
}

/// ApplyTypeConstraints - Apply all of the type constraints relevant to
/// this node and its children in the tree.  This returns true if it makes a
/// change, false otherwise.  If a type contradiction is found, flag an error.
bool TreePatternNode::ApplyTypeConstraints(TreePattern &TP, bool NotRegisters) {
  if (TP.hasError())
    return false;

  CodeGenDAGPatterns &CDP = TP.getDAGPatterns();
  if (isLeaf()) {
    if (DefInit *DI = dyn_cast<DefInit>(getLeafValue())) {
      // If it's a regclass or something else known, include the type.
      bool MadeChange = false;
      for (unsigned i = 0, e = Types.size(); i != e; ++i)
        MadeChange |= UpdateNodeType(i, getImplicitType(DI->getDef(), i,
                                                        NotRegisters,
                                                        !hasName(), TP), TP);
      return MadeChange;
    }

    if (IntInit *II = dyn_cast<IntInit>(getLeafValue())) {
      assert(Types.size() == 1 && "Invalid IntInit");

      // Int inits are always integers. :)
      bool MadeChange = TP.getInfer().EnforceInteger(Types[0]);

      if (!TP.getInfer().isConcrete(Types[0], false))
        return MadeChange;

      ValueTypeByHwMode VVT = TP.getInfer().getConcrete(Types[0], false);
      for (auto &P : VVT) {
        MVT::SimpleValueType VT = P.second.SimpleTy;
        if (VT == MVT::iPTR || VT == MVT::iPTRAny)
          continue;
        unsigned Size = MVT(VT).getSizeInBits();
        // Make sure that the value is representable for this type.
        if (Size >= 32)
          continue;
        // Check that the value doesn't use more bits than we have. It must
        // either be a sign- or zero-extended equivalent of the original.
        int64_t SignBitAndAbove = II->getValue() >> (Size - 1);
        if (SignBitAndAbove == -1 || SignBitAndAbove == 0 ||
            SignBitAndAbove == 1)
          continue;

        TP.error("Integer value '" + Twine(II->getValue()) +
                 "' is out of range for type '" + getEnumName(VT) + "'!");
        break;
      }
      return MadeChange;
    }

    return false;
  }

  if (const CodeGenIntrinsic *Int = getIntrinsicInfo(CDP)) {
    bool MadeChange = false;

    // Apply the result type to the node.
    unsigned NumRetVTs = Int->IS.RetVTs.size();
    unsigned NumParamVTs = Int->IS.ParamVTs.size();

    for (unsigned i = 0, e = NumRetVTs; i != e; ++i)
      MadeChange |= UpdateNodeType(i, Int->IS.RetVTs[i], TP);

    if (getNumChildren() != NumParamVTs + 1) {
      TP.error("Intrinsic '" + Int->Name + "' expects " + Twine(NumParamVTs) +
               " operands, not " + Twine(getNumChildren() - 1) + " operands!");
      return false;
    }

    // Apply type info to the intrinsic ID.
    MadeChange |= getChild(0)->UpdateNodeType(0, MVT::iPTR, TP);

    for (unsigned i = 0, e = getNumChildren()-1; i != e; ++i) {
      MadeChange |= getChild(i+1)->ApplyTypeConstraints(TP, NotRegisters);

      MVT::SimpleValueType OpVT = Int->IS.ParamVTs[i];
      assert(getChild(i+1)->getNumTypes() == 1 && "Unhandled case");
      MadeChange |= getChild(i+1)->UpdateNodeType(0, OpVT, TP);
    }
    return MadeChange;
  }

  if (getOperator()->isSubClassOf("SDNode")) {
    const SDNodeInfo &NI = CDP.getSDNodeInfo(getOperator());

    // Check that the number of operands is sane.  Negative operands -> varargs.
    if (NI.getNumOperands() >= 0 &&
        getNumChildren() != (unsigned)NI.getNumOperands()) {
      TP.error(getOperator()->getName() + " node requires exactly " +
               Twine(NI.getNumOperands()) + " operands!");
      return false;
    }

    bool MadeChange = false;
    for (unsigned i = 0, e = getNumChildren(); i != e; ++i)
      MadeChange |= getChild(i)->ApplyTypeConstraints(TP, NotRegisters);
    MadeChange |= NI.ApplyTypeConstraints(this, TP);
    return MadeChange;
  }

  if (getOperator()->isSubClassOf("Instruction")) {
    const DAGInstruction &Inst = CDP.getInstruction(getOperator());
    CodeGenInstruction &InstInfo =
      CDP.getTargetInfo().getInstruction(getOperator());

    bool MadeChange = false;

    // Apply the result types to the node, these come from the things in the
    // (outs) list of the instruction.
    unsigned NumResultsToAdd = std::min(InstInfo.Operands.NumDefs,
                                        Inst.getNumResults());
    for (unsigned ResNo = 0; ResNo != NumResultsToAdd; ++ResNo)
      MadeChange |= UpdateNodeTypeFromInst(ResNo, Inst.getResult(ResNo), TP);

    // If the instruction has implicit defs, we apply the first one as a result.
    // FIXME: This sucks, it should apply all implicit defs.
    if (!InstInfo.ImplicitDefs.empty()) {
      unsigned ResNo = NumResultsToAdd;

      // FIXME: Generalize to multiple possible types and multiple possible
      // ImplicitDefs.
      MVT::SimpleValueType VT =
        InstInfo.HasOneImplicitDefWithKnownVT(CDP.getTargetInfo());

      if (VT != MVT::Other)
        MadeChange |= UpdateNodeType(ResNo, VT, TP);
    }

    // If this is an INSERT_SUBREG, constrain the source and destination VTs to
    // be the same.
    if (getOperator()->getName() == "INSERT_SUBREG") {
      assert(getChild(0)->getNumTypes() == 1 && "FIXME: Unhandled");
      MadeChange |= UpdateNodeType(0, getChild(0)->getExtType(0), TP);
      MadeChange |= getChild(0)->UpdateNodeType(0, getExtType(0), TP);
    } else if (getOperator()->getName() == "REG_SEQUENCE") {
      // We need to do extra, custom typechecking for REG_SEQUENCE since it is
      // variadic.

      unsigned NChild = getNumChildren();
      if (NChild < 3) {
        TP.error("REG_SEQUENCE requires at least 3 operands!");
        return false;
      }

      if (NChild % 2 == 0) {
        TP.error("REG_SEQUENCE requires an odd number of operands!");
        return false;
      }

      if (!isOperandClass(getChild(0), "RegisterClass")) {
        TP.error("REG_SEQUENCE requires a RegisterClass for first operand!");
        return false;
      }

      for (unsigned I = 1; I < NChild; I += 2) {
        TreePatternNode *SubIdxChild = getChild(I + 1);
        if (!isOperandClass(SubIdxChild, "SubRegIndex")) {
          TP.error("REG_SEQUENCE requires a SubRegIndex for operand " +
                   Twine(I + 1) + "!");
          return false;
        }
      }
    }

    unsigned ChildNo = 0;
    for (unsigned i = 0, e = Inst.getNumOperands(); i != e; ++i) {
      Record *OperandNode = Inst.getOperand(i);

      // If the instruction expects a predicate or optional def operand, we
      // codegen this by setting the operand to it's default value if it has a
      // non-empty DefaultOps field.
      if (OperandNode->isSubClassOf("OperandWithDefaultOps") &&
          !CDP.getDefaultOperand(OperandNode).DefaultOps.empty())
        continue;

      // Verify that we didn't run out of provided operands.
      if (ChildNo >= getNumChildren()) {
        emitTooFewOperandsError(TP, getOperator()->getName(), getNumChildren());
        return false;
      }

      TreePatternNode *Child = getChild(ChildNo++);
      unsigned ChildResNo = 0;  // Instructions always use res #0 of their op.

      // If the operand has sub-operands, they may be provided by distinct
      // child patterns, so attempt to match each sub-operand separately.
      if (OperandNode->isSubClassOf("Operand")) {
        DagInit *MIOpInfo = OperandNode->getValueAsDag("MIOperandInfo");
        if (unsigned NumArgs = MIOpInfo->getNumArgs()) {
          // But don't do that if the whole operand is being provided by
          // a single ComplexPattern-related Operand.

          if (Child->getNumMIResults(CDP) < NumArgs) {
            // Match first sub-operand against the child we already have.
            Record *SubRec = cast<DefInit>(MIOpInfo->getArg(0))->getDef();
            MadeChange |=
              Child->UpdateNodeTypeFromInst(ChildResNo, SubRec, TP);

            // And the remaining sub-operands against subsequent children.
            for (unsigned Arg = 1; Arg < NumArgs; ++Arg) {
              if (ChildNo >= getNumChildren()) {
                emitTooFewOperandsError(TP, getOperator()->getName(),
                                        getNumChildren());
                return false;
              }
              Child = getChild(ChildNo++);

              SubRec = cast<DefInit>(MIOpInfo->getArg(Arg))->getDef();
              MadeChange |=
                Child->UpdateNodeTypeFromInst(ChildResNo, SubRec, TP);
            }
            continue;
          }
        }
      }

      // If we didn't match by pieces above, attempt to match the whole
      // operand now.
      MadeChange |= Child->UpdateNodeTypeFromInst(ChildResNo, OperandNode, TP);
    }

    if (!InstInfo.Operands.isVariadic && ChildNo != getNumChildren()) {
      emitTooManyOperandsError(TP, getOperator()->getName(),
                               ChildNo, getNumChildren());
      return false;
    }

    for (unsigned i = 0, e = getNumChildren(); i != e; ++i)
      MadeChange |= getChild(i)->ApplyTypeConstraints(TP, NotRegisters);
    return MadeChange;
  }

  if (getOperator()->isSubClassOf("ComplexPattern")) {
    bool MadeChange = false;

    for (unsigned i = 0; i < getNumChildren(); ++i)
      MadeChange |= getChild(i)->ApplyTypeConstraints(TP, NotRegisters);

    return MadeChange;
  }

  assert(getOperator()->isSubClassOf("SDNodeXForm") && "Unknown node type!");

  // Node transforms always take one operand.
  if (getNumChildren() != 1) {
    TP.error("Node transform '" + getOperator()->getName() +
             "' requires one operand!");
    return false;
  }

  bool MadeChange = getChild(0)->ApplyTypeConstraints(TP, NotRegisters);
  return MadeChange;
}

/// OnlyOnRHSOfCommutative - Return true if this value is only allowed on the
/// RHS of a commutative operation, not the on LHS.
static bool OnlyOnRHSOfCommutative(TreePatternNode *N) {
  if (!N->isLeaf() && N->getOperator()->getName() == "imm")
    return true;
  if (N->isLeaf() && isa<IntInit>(N->getLeafValue()))
    return true;
  return false;
}


/// canPatternMatch - If it is impossible for this pattern to match on this
/// target, fill in Reason and return false.  Otherwise, return true.  This is
/// used as a sanity check for .td files (to prevent people from writing stuff
/// that can never possibly work), and to prevent the pattern permuter from
/// generating stuff that is useless.
bool TreePatternNode::canPatternMatch(std::string &Reason,
                                      const CodeGenDAGPatterns &CDP) {
  if (isLeaf()) return true;

  for (unsigned i = 0, e = getNumChildren(); i != e; ++i)
    if (!getChild(i)->canPatternMatch(Reason, CDP))
      return false;

  // If this is an intrinsic, handle cases that would make it not match.  For
  // example, if an operand is required to be an immediate.
  if (getOperator()->isSubClassOf("Intrinsic")) {
    // TODO:
    return true;
  }

  if (getOperator()->isSubClassOf("ComplexPattern"))
    return true;

  // If this node is a commutative operator, check that the LHS isn't an
  // immediate.
  const SDNodeInfo &NodeInfo = CDP.getSDNodeInfo(getOperator());
  bool isCommIntrinsic = isCommutativeIntrinsic(CDP);
  if (NodeInfo.hasProperty(SDNPCommutative) || isCommIntrinsic) {
    // Scan all of the operands of the node and make sure that only the last one
    // is a constant node, unless the RHS also is.
    if (!OnlyOnRHSOfCommutative(getChild(getNumChildren()-1))) {
      unsigned Skip = isCommIntrinsic ? 1 : 0; // First operand is intrinsic id.
      for (unsigned i = Skip, e = getNumChildren()-1; i != e; ++i)
        if (OnlyOnRHSOfCommutative(getChild(i))) {
          Reason="Immediate value must be on the RHS of commutative operators!";
          return false;
        }
    }
  }

  return true;
}

//===----------------------------------------------------------------------===//
// TreePattern implementation
//

TreePattern::TreePattern(Record *TheRec, ListInit *RawPat, bool isInput,
                         CodeGenDAGPatterns &cdp) : TheRecord(TheRec), CDP(cdp),
                         isInputPattern(isInput), HasError(false),
                         Infer(*this) {
  for (Init *I : RawPat->getValues())
    Trees.push_back(ParseTreePattern(I, ""));
}

TreePattern::TreePattern(Record *TheRec, DagInit *Pat, bool isInput,
                         CodeGenDAGPatterns &cdp) : TheRecord(TheRec), CDP(cdp),
                         isInputPattern(isInput), HasError(false),
                         Infer(*this) {
  Trees.push_back(ParseTreePattern(Pat, ""));
}

TreePattern::TreePattern(Record *TheRec, TreePatternNodePtr Pat, bool isInput,
                         CodeGenDAGPatterns &cdp)
    : TheRecord(TheRec), CDP(cdp), isInputPattern(isInput), HasError(false),
      Infer(*this) {
  Trees.push_back(Pat);
}

void TreePattern::error(const Twine &Msg) {
  if (HasError)
    return;
  dump();
  PrintError(TheRecord->getLoc(), "In " + TheRecord->getName() + ": " + Msg);
  HasError = true;
}

void TreePattern::ComputeNamedNodes() {
  for (TreePatternNodePtr &Tree : Trees)
    ComputeNamedNodes(Tree.get());
}

void TreePattern::ComputeNamedNodes(TreePatternNode *N) {
  if (!N->getName().empty())
    NamedNodes[N->getName()].push_back(N);

  for (unsigned i = 0, e = N->getNumChildren(); i != e; ++i)
    ComputeNamedNodes(N->getChild(i));
}

TreePatternNodePtr TreePattern::ParseTreePattern(Init *TheInit,
                                                 StringRef OpName) {
  if (DefInit *DI = dyn_cast<DefInit>(TheInit)) {
    Record *R = DI->getDef();

    // Direct reference to a leaf DagNode or PatFrag?  Turn it into a
    // TreePatternNode of its own.  For example:
    ///   (foo GPR, imm) -> (foo GPR, (imm))
    if (R->isSubClassOf("SDNode") || R->isSubClassOf("PatFrags"))
      return ParseTreePattern(
        DagInit::get(DI, nullptr,
                     std::vector<std::pair<Init*, StringInit*> >()),
        OpName);

    // Input argument?
    TreePatternNodePtr Res = std::make_shared<TreePatternNode>(DI, 1);
    if (R->getName() == "node" && !OpName.empty()) {
      if (OpName.empty())
        error("'node' argument requires a name to match with operand list");
      Args.push_back(OpName);
    }

    Res->setName(OpName);
    return Res;
  }

  // ?:$name or just $name.
  if (isa<UnsetInit>(TheInit)) {
    if (OpName.empty())
      error("'?' argument requires a name to match with operand list");
    TreePatternNodePtr Res = std::make_shared<TreePatternNode>(TheInit, 1);
    Args.push_back(OpName);
    Res->setName(OpName);
    return Res;
  }

  if (isa<IntInit>(TheInit) || isa<BitInit>(TheInit)) {
    if (!OpName.empty())
      error("Constant int or bit argument should not have a name!");
    if (isa<BitInit>(TheInit))
      TheInit = TheInit->convertInitializerTo(IntRecTy::get());
    return std::make_shared<TreePatternNode>(TheInit, 1);
  }

  if (BitsInit *BI = dyn_cast<BitsInit>(TheInit)) {
    // Turn this into an IntInit.
    Init *II = BI->convertInitializerTo(IntRecTy::get());
    if (!II || !isa<IntInit>(II))
      error("Bits value must be constants!");
    return ParseTreePattern(II, OpName);
  }

  DagInit *Dag = dyn_cast<DagInit>(TheInit);
  if (!Dag) {
    TheInit->print(errs());
    error("Pattern has unexpected init kind!");
  }
  DefInit *OpDef = dyn_cast<DefInit>(Dag->getOperator());
  if (!OpDef) error("Pattern has unexpected operator type!");
  Record *Operator = OpDef->getDef();

  if (Operator->isSubClassOf("ValueType")) {
    // If the operator is a ValueType, then this must be "type cast" of a leaf
    // node.
    if (Dag->getNumArgs() != 1)
      error("Type cast only takes one operand!");

    TreePatternNodePtr New =
        ParseTreePattern(Dag->getArg(0), Dag->getArgNameStr(0));

    // Apply the type cast.
    assert(New->getNumTypes() == 1 && "FIXME: Unhandled");
    const CodeGenHwModes &CGH = getDAGPatterns().getTargetInfo().getHwModes();
    New->UpdateNodeType(0, getValueTypeByHwMode(Operator, CGH), *this);

    if (!OpName.empty())
      error("ValueType cast should not have a name!");
    return New;
  }

  // Verify that this is something that makes sense for an operator.
  if (!Operator->isSubClassOf("PatFrags") &&
      !Operator->isSubClassOf("SDNode") &&
      !Operator->isSubClassOf("Instruction") &&
      !Operator->isSubClassOf("SDNodeXForm") &&
      !Operator->isSubClassOf("Intrinsic") &&
      !Operator->isSubClassOf("ComplexPattern") &&
      Operator->getName() != "set" &&
      Operator->getName() != "implicit")
    error("Unrecognized node '" + Operator->getName() + "'!");

  //  Check to see if this is something that is illegal in an input pattern.
  if (isInputPattern) {
    if (Operator->isSubClassOf("Instruction") ||
        Operator->isSubClassOf("SDNodeXForm"))
      error("Cannot use '" + Operator->getName() + "' in an input pattern!");
  } else {
    if (Operator->isSubClassOf("Intrinsic"))
      error("Cannot use '" + Operator->getName() + "' in an output pattern!");

    if (Operator->isSubClassOf("SDNode") &&
        Operator->getName() != "imm" &&
        Operator->getName() != "fpimm" &&
        Operator->getName() != "tglobaltlsaddr" &&
        Operator->getName() != "tconstpool" &&
        Operator->getName() != "tjumptable" &&
        Operator->getName() != "tframeindex" &&
        Operator->getName() != "texternalsym" &&
        Operator->getName() != "tblockaddress" &&
        Operator->getName() != "tglobaladdr" &&
        Operator->getName() != "bb" &&
        Operator->getName() != "vt" &&
        Operator->getName() != "mcsym")
      error("Cannot use '" + Operator->getName() + "' in an output pattern!");
  }

  std::vector<TreePatternNodePtr> Children;

  // Parse all the operands.
  for (unsigned i = 0, e = Dag->getNumArgs(); i != e; ++i)
    Children.push_back(ParseTreePattern(Dag->getArg(i), Dag->getArgNameStr(i)));

  // Get the actual number of results before Operator is converted to an intrinsic
  // node (which is hard-coded to have either zero or one result).
  unsigned NumResults = GetNumNodeResults(Operator, CDP);

  // If the operator is an intrinsic, then this is just syntactic sugar for
  // (intrinsic_* <number>, ..children..).  Pick the right intrinsic node, and
  // convert the intrinsic name to a number.
  if (Operator->isSubClassOf("Intrinsic")) {
    const CodeGenIntrinsic &Int = getDAGPatterns().getIntrinsic(Operator);
    unsigned IID = getDAGPatterns().getIntrinsicID(Operator)+1;

    // If this intrinsic returns void, it must have side-effects and thus a
    // chain.
    if (Int.IS.RetVTs.empty())
      Operator = getDAGPatterns().get_intrinsic_void_sdnode();
    else if (Int.ModRef != CodeGenIntrinsic::NoMem)
      // Has side-effects, requires chain.
      Operator = getDAGPatterns().get_intrinsic_w_chain_sdnode();
    else // Otherwise, no chain.
      Operator = getDAGPatterns().get_intrinsic_wo_chain_sdnode();

    Children.insert(Children.begin(),
                    std::make_shared<TreePatternNode>(IntInit::get(IID), 1));
  }

  if (Operator->isSubClassOf("ComplexPattern")) {
    for (unsigned i = 0; i < Children.size(); ++i) {
      TreePatternNodePtr Child = Children[i];

      if (Child->getName().empty())
        error("All arguments to a ComplexPattern must be named");

      // Check that the ComplexPattern uses are consistent: "(MY_PAT $a, $b)"
      // and "(MY_PAT $b, $a)" should not be allowed in the same pattern;
      // neither should "(MY_PAT_1 $a, $b)" and "(MY_PAT_2 $a, $b)".
      auto OperandId = std::make_pair(Operator, i);
      auto PrevOp = ComplexPatternOperands.find(Child->getName());
      if (PrevOp != ComplexPatternOperands.end()) {
        if (PrevOp->getValue() != OperandId)
          error("All ComplexPattern operands must appear consistently: "
                "in the same order in just one ComplexPattern instance.");
      } else
        ComplexPatternOperands[Child->getName()] = OperandId;
    }
  }

  TreePatternNodePtr Result =
      std::make_shared<TreePatternNode>(Operator, std::move(Children),
                                        NumResults);
  Result->setName(OpName);

  if (Dag->getName()) {
    assert(Result->getName().empty());
    Result->setName(Dag->getNameStr());
  }
  return Result;
}

/// SimplifyTree - See if we can simplify this tree to eliminate something that
/// will never match in favor of something obvious that will.  This is here
/// strictly as a convenience to target authors because it allows them to write
/// more type generic things and have useless type casts fold away.
///
/// This returns true if any change is made.
static bool SimplifyTree(TreePatternNodePtr &N) {
  if (N->isLeaf())
    return false;

  // If we have a bitconvert with a resolved type and if the source and
  // destination types are the same, then the bitconvert is useless, remove it.
  if (N->getOperator()->getName() == "bitconvert" &&
      N->getExtType(0).isValueTypeByHwMode(false) &&
      N->getExtType(0) == N->getChild(0)->getExtType(0) &&
      N->getName().empty()) {
    N = N->getChildShared(0);
    SimplifyTree(N);
    return true;
  }

  // Walk all children.
  bool MadeChange = false;
  for (unsigned i = 0, e = N->getNumChildren(); i != e; ++i) {
    TreePatternNodePtr Child = N->getChildShared(i);
    MadeChange |= SimplifyTree(Child);
    N->setChild(i, std::move(Child));
  }
  return MadeChange;
}



/// InferAllTypes - Infer/propagate as many types throughout the expression
/// patterns as possible.  Return true if all types are inferred, false
/// otherwise.  Flags an error if a type contradiction is found.
bool TreePattern::
InferAllTypes(const StringMap<SmallVector<TreePatternNode*,1> > *InNamedTypes) {
  if (NamedNodes.empty())
    ComputeNamedNodes();

  bool MadeChange = true;
  while (MadeChange) {
    MadeChange = false;
    for (TreePatternNodePtr &Tree : Trees) {
      MadeChange |= Tree->ApplyTypeConstraints(*this, false);
      MadeChange |= SimplifyTree(Tree);
    }

    // If there are constraints on our named nodes, apply them.
    for (auto &Entry : NamedNodes) {
      SmallVectorImpl<TreePatternNode*> &Nodes = Entry.second;

      // If we have input named node types, propagate their types to the named
      // values here.
      if (InNamedTypes) {
        if (!InNamedTypes->count(Entry.getKey())) {
          error("Node '" + std::string(Entry.getKey()) +
                "' in output pattern but not input pattern");
          return true;
        }

        const SmallVectorImpl<TreePatternNode*> &InNodes =
          InNamedTypes->find(Entry.getKey())->second;

        // The input types should be fully resolved by now.
        for (TreePatternNode *Node : Nodes) {
          // If this node is a register class, and it is the root of the pattern
          // then we're mapping something onto an input register.  We allow
          // changing the type of the input register in this case.  This allows
          // us to match things like:
          //  def : Pat<(v1i64 (bitconvert(v2i32 DPR:$src))), (v1i64 DPR:$src)>;
          if (Node == Trees[0].get() && Node->isLeaf()) {
            DefInit *DI = dyn_cast<DefInit>(Node->getLeafValue());
            if (DI && (DI->getDef()->isSubClassOf("RegisterClass") ||
                       DI->getDef()->isSubClassOf("RegisterOperand")))
              continue;
          }

          assert(Node->getNumTypes() == 1 &&
                 InNodes[0]->getNumTypes() == 1 &&
                 "FIXME: cannot name multiple result nodes yet");
          MadeChange |= Node->UpdateNodeType(0, InNodes[0]->getExtType(0),
                                             *this);
        }
      }

      // If there are multiple nodes with the same name, they must all have the
      // same type.
      if (Entry.second.size() > 1) {
        for (unsigned i = 0, e = Nodes.size()-1; i != e; ++i) {
          TreePatternNode *N1 = Nodes[i], *N2 = Nodes[i+1];
          assert(N1->getNumTypes() == 1 && N2->getNumTypes() == 1 &&
                 "FIXME: cannot name multiple result nodes yet");

          MadeChange |= N1->UpdateNodeType(0, N2->getExtType(0), *this);
          MadeChange |= N2->UpdateNodeType(0, N1->getExtType(0), *this);
        }
      }
    }
  }

  bool HasUnresolvedTypes = false;
  for (const TreePatternNodePtr &Tree : Trees)
    HasUnresolvedTypes |= Tree->ContainsUnresolvedType(*this);
  return !HasUnresolvedTypes;
}

void TreePattern::print(raw_ostream &OS) const {
  OS << getRecord()->getName();
  if (!Args.empty()) {
    OS << "(" << Args[0];
    for (unsigned i = 1, e = Args.size(); i != e; ++i)
      OS << ", " << Args[i];
    OS << ")";
  }
  OS << ": ";

  if (Trees.size() > 1)
    OS << "[\n";
  for (const TreePatternNodePtr &Tree : Trees) {
    OS << "\t";
    Tree->print(OS);
    OS << "\n";
  }

  if (Trees.size() > 1)
    OS << "]\n";
}

void TreePattern::dump() const { print(errs()); }

//===----------------------------------------------------------------------===//
// CodeGenDAGPatterns implementation
//

CodeGenDAGPatterns::CodeGenDAGPatterns(RecordKeeper &R,
                                       PatternRewriterFn PatternRewriter)
    : Records(R), Target(R), LegalVTS(Target.getLegalValueTypes()),
      PatternRewriter(PatternRewriter) {

  Intrinsics = CodeGenIntrinsicTable(Records, false);
  TgtIntrinsics = CodeGenIntrinsicTable(Records, true);
  ParseNodeInfo();
  ParseNodeTransforms();
  ParseComplexPatterns();
  ParsePatternFragments();
  ParseDefaultOperands();
  ParseInstructions();
  ParsePatternFragments(/*OutFrags*/true);
  ParsePatterns();

  // Break patterns with parameterized types into a series of patterns,
  // where each one has a fixed type and is predicated on the conditions
  // of the associated HW mode.
  ExpandHwModeBasedTypes();

  // Generate variants.  For example, commutative patterns can match
  // multiple ways.  Add them to PatternsToMatch as well.
  GenerateVariants();

  // Infer instruction flags.  For example, we can detect loads,
  // stores, and side effects in many cases by examining an
  // instruction's pattern.
  InferInstructionFlags();

  // Verify that instruction flags match the patterns.
  VerifyInstructionFlags();
}

Record *CodeGenDAGPatterns::getSDNodeNamed(const std::string &Name) const {
  Record *N = Records.getDef(Name);
  if (!N || !N->isSubClassOf("SDNode"))
    PrintFatalError("Error getting SDNode '" + Name + "'!");

  return N;
}

// Parse all of the SDNode definitions for the target, populating SDNodes.
void CodeGenDAGPatterns::ParseNodeInfo() {
  std::vector<Record*> Nodes = Records.getAllDerivedDefinitions("SDNode");
  const CodeGenHwModes &CGH = getTargetInfo().getHwModes();

  while (!Nodes.empty()) {
    Record *R = Nodes.back();
    SDNodes.insert(std::make_pair(R, SDNodeInfo(R, CGH)));
    Nodes.pop_back();
  }

  // Get the builtin intrinsic nodes.
  intrinsic_void_sdnode     = getSDNodeNamed("intrinsic_void");
  intrinsic_w_chain_sdnode  = getSDNodeNamed("intrinsic_w_chain");
  intrinsic_wo_chain_sdnode = getSDNodeNamed("intrinsic_wo_chain");
}

/// ParseNodeTransforms - Parse all SDNodeXForm instances into the SDNodeXForms
/// map, and emit them to the file as functions.
void CodeGenDAGPatterns::ParseNodeTransforms() {
  std::vector<Record*> Xforms = Records.getAllDerivedDefinitions("SDNodeXForm");
  while (!Xforms.empty()) {
    Record *XFormNode = Xforms.back();
    Record *SDNode = XFormNode->getValueAsDef("Opcode");
    StringRef Code = XFormNode->getValueAsString("XFormFunction");
    SDNodeXForms.insert(std::make_pair(XFormNode, NodeXForm(SDNode, Code)));

    Xforms.pop_back();
  }
}

void CodeGenDAGPatterns::ParseComplexPatterns() {
  std::vector<Record*> AMs = Records.getAllDerivedDefinitions("ComplexPattern");
  while (!AMs.empty()) {
    ComplexPatterns.insert(std::make_pair(AMs.back(), AMs.back()));
    AMs.pop_back();
  }
}


/// ParsePatternFragments - Parse all of the PatFrag definitions in the .td
/// file, building up the PatternFragments map.  After we've collected them all,
/// inline fragments together as necessary, so that there are no references left
/// inside a pattern fragment to a pattern fragment.
///
void CodeGenDAGPatterns::ParsePatternFragments(bool OutFrags) {
  std::vector<Record*> Fragments = Records.getAllDerivedDefinitions("PatFrags");

  // First step, parse all of the fragments.
  for (Record *Frag : Fragments) {
    if (OutFrags != Frag->isSubClassOf("OutPatFrag"))
      continue;

    ListInit *LI = Frag->getValueAsListInit("Fragments");
    TreePattern *P =
        (PatternFragments[Frag] = llvm::make_unique<TreePattern>(
             Frag, LI, !Frag->isSubClassOf("OutPatFrag"),
             *this)).get();

    // Validate the argument list, converting it to set, to discard duplicates.
    std::vector<std::string> &Args = P->getArgList();
    // Copy the args so we can take StringRefs to them.
    auto ArgsCopy = Args;
    SmallDenseSet<StringRef, 4> OperandsSet;
    OperandsSet.insert(ArgsCopy.begin(), ArgsCopy.end());

    if (OperandsSet.count(""))
      P->error("Cannot have unnamed 'node' values in pattern fragment!");

    // Parse the operands list.
    DagInit *OpsList = Frag->getValueAsDag("Operands");
    DefInit *OpsOp = dyn_cast<DefInit>(OpsList->getOperator());
    // Special cases: ops == outs == ins. Different names are used to
    // improve readability.
    if (!OpsOp ||
        (OpsOp->getDef()->getName() != "ops" &&
         OpsOp->getDef()->getName() != "outs" &&
         OpsOp->getDef()->getName() != "ins"))
      P->error("Operands list should start with '(ops ... '!");

    // Copy over the arguments.
    Args.clear();
    for (unsigned j = 0, e = OpsList->getNumArgs(); j != e; ++j) {
      if (!isa<DefInit>(OpsList->getArg(j)) ||
          cast<DefInit>(OpsList->getArg(j))->getDef()->getName() != "node")
        P->error("Operands list should all be 'node' values.");
      if (!OpsList->getArgName(j))
        P->error("Operands list should have names for each operand!");
      StringRef ArgNameStr = OpsList->getArgNameStr(j);
      if (!OperandsSet.count(ArgNameStr))
        P->error("'" + ArgNameStr +
                 "' does not occur in pattern or was multiply specified!");
      OperandsSet.erase(ArgNameStr);
      Args.push_back(ArgNameStr);
    }

    if (!OperandsSet.empty())
      P->error("Operands list does not contain an entry for operand '" +
               *OperandsSet.begin() + "'!");

    // If there is a node transformation corresponding to this, keep track of
    // it.
    Record *Transform = Frag->getValueAsDef("OperandTransform");
    if (!getSDNodeTransform(Transform).second.empty())    // not noop xform?
      for (auto T : P->getTrees())
        T->setTransformFn(Transform);
  }

  // Now that we've parsed all of the tree fragments, do a closure on them so
  // that there are not references to PatFrags left inside of them.
  for (Record *Frag : Fragments) {
    if (OutFrags != Frag->isSubClassOf("OutPatFrag"))
      continue;

    TreePattern &ThePat = *PatternFragments[Frag];
    ThePat.InlinePatternFragments();

    // Infer as many types as possible.  Don't worry about it if we don't infer
    // all of them, some may depend on the inputs of the pattern.  Also, don't
    // validate type sets; validation may cause spurious failures e.g. if a
    // fragment needs floating-point types but the current target does not have
    // any (this is only an error if that fragment is ever used!).
    {
      TypeInfer::SuppressValidation SV(ThePat.getInfer());
      ThePat.InferAllTypes();
      ThePat.resetError();
    }

    // If debugging, print out the pattern fragment result.
    LLVM_DEBUG(ThePat.dump());
  }
}

void CodeGenDAGPatterns::ParseDefaultOperands() {
  std::vector<Record*> DefaultOps;
  DefaultOps = Records.getAllDerivedDefinitions("OperandWithDefaultOps");

  // Find some SDNode.
  assert(!SDNodes.empty() && "No SDNodes parsed?");
  Init *SomeSDNode = DefInit::get(SDNodes.begin()->first);

  for (unsigned i = 0, e = DefaultOps.size(); i != e; ++i) {
    DagInit *DefaultInfo = DefaultOps[i]->getValueAsDag("DefaultOps");

    // Clone the DefaultInfo dag node, changing the operator from 'ops' to
    // SomeSDnode so that we can parse this.
    std::vector<std::pair<Init*, StringInit*> > Ops;
    for (unsigned op = 0, e = DefaultInfo->getNumArgs(); op != e; ++op)
      Ops.push_back(std::make_pair(DefaultInfo->getArg(op),
                                   DefaultInfo->getArgName(op)));
    DagInit *DI = DagInit::get(SomeSDNode, nullptr, Ops);

    // Create a TreePattern to parse this.
    TreePattern P(DefaultOps[i], DI, false, *this);
    assert(P.getNumTrees() == 1 && "This ctor can only produce one tree!");

    // Copy the operands over into a DAGDefaultOperand.
    DAGDefaultOperand DefaultOpInfo;

    const TreePatternNodePtr &T = P.getTree(0);
    for (unsigned op = 0, e = T->getNumChildren(); op != e; ++op) {
      TreePatternNodePtr TPN = T->getChildShared(op);
      while (TPN->ApplyTypeConstraints(P, false))
        /* Resolve all types */;

      if (TPN->ContainsUnresolvedType(P)) {
        PrintFatalError("Value #" + Twine(i) + " of OperandWithDefaultOps '" +
                        DefaultOps[i]->getName() +
                        "' doesn't have a concrete type!");
      }
      DefaultOpInfo.DefaultOps.push_back(std::move(TPN));
    }

    // Insert it into the DefaultOperands map so we can find it later.
    DefaultOperands[DefaultOps[i]] = DefaultOpInfo;
  }
}

/// HandleUse - Given "Pat" a leaf in the pattern, check to see if it is an
/// instruction input.  Return true if this is a real use.
static bool HandleUse(TreePattern &I, TreePatternNodePtr Pat,
                      std::map<std::string, TreePatternNodePtr> &InstInputs) {
  // No name -> not interesting.
  if (Pat->getName().empty()) {
    if (Pat->isLeaf()) {
      DefInit *DI = dyn_cast<DefInit>(Pat->getLeafValue());
      if (DI && (DI->getDef()->isSubClassOf("RegisterClass") ||
                 DI->getDef()->isSubClassOf("RegisterOperand")))
        I.error("Input " + DI->getDef()->getName() + " must be named!");
    }
    return false;
  }

  Record *Rec;
  if (Pat->isLeaf()) {
    DefInit *DI = dyn_cast<DefInit>(Pat->getLeafValue());
    if (!DI)
      I.error("Input $" + Pat->getName() + " must be an identifier!");
    Rec = DI->getDef();
  } else {
    Rec = Pat->getOperator();
  }

  // SRCVALUE nodes are ignored.
  if (Rec->getName() == "srcvalue")
    return false;

  TreePatternNodePtr &Slot = InstInputs[Pat->getName()];
  if (!Slot) {
    Slot = Pat;
    return true;
  }
  Record *SlotRec;
  if (Slot->isLeaf()) {
    SlotRec = cast<DefInit>(Slot->getLeafValue())->getDef();
  } else {
    assert(Slot->getNumChildren() == 0 && "can't be a use with children!");
    SlotRec = Slot->getOperator();
  }

  // Ensure that the inputs agree if we've already seen this input.
  if (Rec != SlotRec)
    I.error("All $" + Pat->getName() + " inputs must agree with each other");
  // Ensure that the types can agree as well.
  Slot->UpdateNodeType(0, Pat->getExtType(0), I);
  Pat->UpdateNodeType(0, Slot->getExtType(0), I);
  if (Slot->getExtTypes() != Pat->getExtTypes())
    I.error("All $" + Pat->getName() + " inputs must agree with each other");
  return true;
}

/// FindPatternInputsAndOutputs - Scan the specified TreePatternNode (which is
/// part of "I", the instruction), computing the set of inputs and outputs of
/// the pattern.  Report errors if we see anything naughty.
void CodeGenDAGPatterns::FindPatternInputsAndOutputs(
    TreePattern &I, TreePatternNodePtr Pat,
    std::map<std::string, TreePatternNodePtr> &InstInputs,
    MapVector<std::string, TreePatternNodePtr, std::map<std::string, unsigned>>
        &InstResults,
    std::vector<Record *> &InstImpResults) {

  // The instruction pattern still has unresolved fragments.  For *named*
  // nodes we must resolve those here.  This may not result in multiple
  // alternatives.
  if (!Pat->getName().empty()) {
    TreePattern SrcPattern(I.getRecord(), Pat, true, *this);
    SrcPattern.InlinePatternFragments();
    SrcPattern.InferAllTypes();
    Pat = SrcPattern.getOnlyTree();
  }

  if (Pat->isLeaf()) {
    bool isUse = HandleUse(I, Pat, InstInputs);
    if (!isUse && Pat->getTransformFn())
      I.error("Cannot specify a transform function for a non-input value!");
    return;
  }

  if (Pat->getOperator()->getName() == "implicit") {
    for (unsigned i = 0, e = Pat->getNumChildren(); i != e; ++i) {
      TreePatternNode *Dest = Pat->getChild(i);
      if (!Dest->isLeaf())
        I.error("implicitly defined value should be a register!");

      DefInit *Val = dyn_cast<DefInit>(Dest->getLeafValue());
      if (!Val || !Val->getDef()->isSubClassOf("Register"))
        I.error("implicitly defined value should be a register!");
      InstImpResults.push_back(Val->getDef());
    }
    return;
  }

  if (Pat->getOperator()->getName() != "set") {
    // If this is not a set, verify that the children nodes are not void typed,
    // and recurse.
    for (unsigned i = 0, e = Pat->getNumChildren(); i != e; ++i) {
      if (Pat->getChild(i)->getNumTypes() == 0)
        I.error("Cannot have void nodes inside of patterns!");
      FindPatternInputsAndOutputs(I, Pat->getChildShared(i), InstInputs,
                                  InstResults, InstImpResults);
    }

    // If this is a non-leaf node with no children, treat it basically as if
    // it were a leaf.  This handles nodes like (imm).
    bool isUse = HandleUse(I, Pat, InstInputs);

    if (!isUse && Pat->getTransformFn())
      I.error("Cannot specify a transform function for a non-input value!");
    return;
  }

  // Otherwise, this is a set, validate and collect instruction results.
  if (Pat->getNumChildren() == 0)
    I.error("set requires operands!");

  if (Pat->getTransformFn())
    I.error("Cannot specify a transform function on a set node!");

  // Check the set destinations.
  unsigned NumDests = Pat->getNumChildren()-1;
  for (unsigned i = 0; i != NumDests; ++i) {
    TreePatternNodePtr Dest = Pat->getChildShared(i);
    // For set destinations we also must resolve fragments here.
    TreePattern DestPattern(I.getRecord(), Dest, false, *this);
    DestPattern.InlinePatternFragments();
    DestPattern.InferAllTypes();
    Dest = DestPattern.getOnlyTree();

    if (!Dest->isLeaf())
      I.error("set destination should be a register!");

    DefInit *Val = dyn_cast<DefInit>(Dest->getLeafValue());
    if (!Val) {
      I.error("set destination should be a register!");
      continue;
    }

    if (Val->getDef()->isSubClassOf("RegisterClass") ||
        Val->getDef()->isSubClassOf("ValueType") ||
        Val->getDef()->isSubClassOf("RegisterOperand") ||
        Val->getDef()->isSubClassOf("PointerLikeRegClass")) {
      if (Dest->getName().empty())
        I.error("set destination must have a name!");
      if (InstResults.count(Dest->getName()))
        I.error("cannot set '" + Dest->getName() + "' multiple times");
      InstResults[Dest->getName()] = Dest;
    } else if (Val->getDef()->isSubClassOf("Register")) {
      InstImpResults.push_back(Val->getDef());
    } else {
      I.error("set destination should be a register!");
    }
  }

  // Verify and collect info from the computation.
  FindPatternInputsAndOutputs(I, Pat->getChildShared(NumDests), InstInputs,
                              InstResults, InstImpResults);
}

//===----------------------------------------------------------------------===//
// Instruction Analysis
//===----------------------------------------------------------------------===//

class InstAnalyzer {
  const CodeGenDAGPatterns &CDP;
public:
  bool hasSideEffects;
  bool mayStore;
  bool mayLoad;
  bool isBitcast;
  bool isVariadic;
  bool hasChain;

  InstAnalyzer(const CodeGenDAGPatterns &cdp)
    : CDP(cdp), hasSideEffects(false), mayStore(false), mayLoad(false),
      isBitcast(false), isVariadic(false), hasChain(false) {}

  void Analyze(const PatternToMatch &Pat) {
    const TreePatternNode *N = Pat.getSrcPattern();
    AnalyzeNode(N);
    // These properties are detected only on the root node.
    isBitcast = IsNodeBitcast(N);
  }

private:
  bool IsNodeBitcast(const TreePatternNode *N) const {
    if (hasSideEffects || mayLoad || mayStore || isVariadic)
      return false;

    if (N->isLeaf())
      return false;
    if (N->getNumChildren() != 1 || !N->getChild(0)->isLeaf())
      return false;

    const SDNodeInfo &OpInfo = CDP.getSDNodeInfo(N->getOperator());
    if (OpInfo.getNumResults() != 1 || OpInfo.getNumOperands() != 1)
      return false;
    return OpInfo.getEnumName() == "ISD::BITCAST";
  }

public:
  void AnalyzeNode(const TreePatternNode *N) {
    if (N->isLeaf()) {
      if (DefInit *DI = dyn_cast<DefInit>(N->getLeafValue())) {
        Record *LeafRec = DI->getDef();
        // Handle ComplexPattern leaves.
        if (LeafRec->isSubClassOf("ComplexPattern")) {
          const ComplexPattern &CP = CDP.getComplexPattern(LeafRec);
          if (CP.hasProperty(SDNPMayStore)) mayStore = true;
          if (CP.hasProperty(SDNPMayLoad)) mayLoad = true;
          if (CP.hasProperty(SDNPSideEffect)) hasSideEffects = true;
        }
      }
      return;
    }

    // Analyze children.
    for (unsigned i = 0, e = N->getNumChildren(); i != e; ++i)
      AnalyzeNode(N->getChild(i));

    // Notice properties of the node.
    if (N->NodeHasProperty(SDNPMayStore, CDP)) mayStore = true;
    if (N->NodeHasProperty(SDNPMayLoad, CDP)) mayLoad = true;
    if (N->NodeHasProperty(SDNPSideEffect, CDP)) hasSideEffects = true;
    if (N->NodeHasProperty(SDNPVariadic, CDP)) isVariadic = true;
    if (N->NodeHasProperty(SDNPHasChain, CDP)) hasChain = true;

    if (const CodeGenIntrinsic *IntInfo = N->getIntrinsicInfo(CDP)) {
      // If this is an intrinsic, analyze it.
      if (IntInfo->ModRef & CodeGenIntrinsic::MR_Ref)
        mayLoad = true;// These may load memory.

      if (IntInfo->ModRef & CodeGenIntrinsic::MR_Mod)
        mayStore = true;// Intrinsics that can write to memory are 'mayStore'.

      if (IntInfo->ModRef >= CodeGenIntrinsic::ReadWriteMem ||
          IntInfo->hasSideEffects)
        // ReadWriteMem intrinsics can have other strange effects.
        hasSideEffects = true;
    }
  }

};

static bool InferFromPattern(CodeGenInstruction &InstInfo,
                             const InstAnalyzer &PatInfo,
                             Record *PatDef) {
  bool Error = false;

  // Remember where InstInfo got its flags.
  if (InstInfo.hasUndefFlags())
      InstInfo.InferredFrom = PatDef;

  // Check explicitly set flags for consistency.
  if (InstInfo.hasSideEffects != PatInfo.hasSideEffects &&
      !InstInfo.hasSideEffects_Unset) {
    // Allow explicitly setting hasSideEffects = 1 on instructions, even when
    // the pattern has no side effects. That could be useful for div/rem
    // instructions that may trap.
    if (!InstInfo.hasSideEffects) {
      Error = true;
      PrintError(PatDef->getLoc(), "Pattern doesn't match hasSideEffects = " +
                 Twine(InstInfo.hasSideEffects));
    }
  }

  if (InstInfo.mayStore != PatInfo.mayStore && !InstInfo.mayStore_Unset) {
    Error = true;
    PrintError(PatDef->getLoc(), "Pattern doesn't match mayStore = " +
               Twine(InstInfo.mayStore));
  }

  if (InstInfo.mayLoad != PatInfo.mayLoad && !InstInfo.mayLoad_Unset) {
    // Allow explicitly setting mayLoad = 1, even when the pattern has no loads.
    // Some targets translate immediates to loads.
    if (!InstInfo.mayLoad) {
      Error = true;
      PrintError(PatDef->getLoc(), "Pattern doesn't match mayLoad = " +
                 Twine(InstInfo.mayLoad));
    }
  }

  // Transfer inferred flags.
  InstInfo.hasSideEffects |= PatInfo.hasSideEffects;
  InstInfo.mayStore |= PatInfo.mayStore;
  InstInfo.mayLoad |= PatInfo.mayLoad;

  // These flags are silently added without any verification.
  // FIXME: To match historical behavior of TableGen, for now add those flags
  // only when we're inferring from the primary instruction pattern.
  if (PatDef->isSubClassOf("Instruction")) {
    InstInfo.isBitcast |= PatInfo.isBitcast;
    InstInfo.hasChain |= PatInfo.hasChain;
    InstInfo.hasChain_Inferred = true;
  }

  // Don't infer isVariadic. This flag means something different on SDNodes and
  // instructions. For example, a CALL SDNode is variadic because it has the
  // call arguments as operands, but a CALL instruction is not variadic - it
  // has argument registers as implicit, not explicit uses.

  return Error;
}

/// hasNullFragReference - Return true if the DAG has any reference to the
/// null_frag operator.
static bool hasNullFragReference(DagInit *DI) {
  DefInit *OpDef = dyn_cast<DefInit>(DI->getOperator());
  if (!OpDef) return false;
  Record *Operator = OpDef->getDef();

  // If this is the null fragment, return true.
  if (Operator->getName() == "null_frag") return true;
  // If any of the arguments reference the null fragment, return true.
  for (unsigned i = 0, e = DI->getNumArgs(); i != e; ++i) {
    DagInit *Arg = dyn_cast<DagInit>(DI->getArg(i));
    if (Arg && hasNullFragReference(Arg))
      return true;
  }

  return false;
}

/// hasNullFragReference - Return true if any DAG in the list references
/// the null_frag operator.
static bool hasNullFragReference(ListInit *LI) {
  for (Init *I : LI->getValues()) {
    DagInit *DI = dyn_cast<DagInit>(I);
    assert(DI && "non-dag in an instruction Pattern list?!");
    if (hasNullFragReference(DI))
      return true;
  }
  return false;
}

/// Get all the instructions in a tree.
static void
getInstructionsInTree(TreePatternNode *Tree, SmallVectorImpl<Record*> &Instrs) {
  if (Tree->isLeaf())
    return;
  if (Tree->getOperator()->isSubClassOf("Instruction"))
    Instrs.push_back(Tree->getOperator());
  for (unsigned i = 0, e = Tree->getNumChildren(); i != e; ++i)
    getInstructionsInTree(Tree->getChild(i), Instrs);
}

/// Check the class of a pattern leaf node against the instruction operand it
/// represents.
static bool checkOperandClass(CGIOperandList::OperandInfo &OI,
                              Record *Leaf) {
  if (OI.Rec == Leaf)
    return true;

  // Allow direct value types to be used in instruction set patterns.
  // The type will be checked later.
  if (Leaf->isSubClassOf("ValueType"))
    return true;

  // Patterns can also be ComplexPattern instances.
  if (Leaf->isSubClassOf("ComplexPattern"))
    return true;

  return false;
}

void CodeGenDAGPatterns::parseInstructionPattern(
    CodeGenInstruction &CGI, ListInit *Pat, DAGInstMap &DAGInsts) {

  assert(!DAGInsts.count(CGI.TheDef) && "Instruction already parsed!");

  // Parse the instruction.
  TreePattern I(CGI.TheDef, Pat, true, *this);

  // InstInputs - Keep track of all of the inputs of the instruction, along
  // with the record they are declared as.
  std::map<std::string, TreePatternNodePtr> InstInputs;

  // InstResults - Keep track of all the virtual registers that are 'set'
  // in the instruction, including what reg class they are.
  MapVector<std::string, TreePatternNodePtr, std::map<std::string, unsigned>>
      InstResults;

  std::vector<Record*> InstImpResults;

  // Verify that the top-level forms in the instruction are of void type, and
  // fill in the InstResults map.
  SmallString<32> TypesString;
  for (unsigned j = 0, e = I.getNumTrees(); j != e; ++j) {
    TypesString.clear();
    TreePatternNodePtr Pat = I.getTree(j);
    if (Pat->getNumTypes() != 0) {
      raw_svector_ostream OS(TypesString);
      for (unsigned k = 0, ke = Pat->getNumTypes(); k != ke; ++k) {
        if (k > 0)
          OS << ", ";
        Pat->getExtType(k).writeToStream(OS);
      }
      I.error("Top-level forms in instruction pattern should have"
               " void types, has types " +
               OS.str());
    }

    // Find inputs and outputs, and verify the structure of the uses/defs.
    FindPatternInputsAndOutputs(I, Pat, InstInputs, InstResults,
                                InstImpResults);
  }

  // Now that we have inputs and outputs of the pattern, inspect the operands
  // list for the instruction.  This determines the order that operands are
  // added to the machine instruction the node corresponds to.
  unsigned NumResults = InstResults.size();

  // Parse the operands list from the (ops) list, validating it.
  assert(I.getArgList().empty() && "Args list should still be empty here!");

  // Check that all of the results occur first in the list.
  std::vector<Record*> Results;
  std::vector<unsigned> ResultIndices;
  SmallVector<TreePatternNodePtr, 2> ResNodes;
  for (unsigned i = 0; i != NumResults; ++i) {
    if (i == CGI.Operands.size()) {
      const std::string &OpName =
          std::find_if(InstResults.begin(), InstResults.end(),
                       [](const std::pair<std::string, TreePatternNodePtr> &P) {
                         return P.second;
                       })
              ->first;

      I.error("'" + OpName + "' set but does not appear in operand list!");
    }

    const std::string &OpName = CGI.Operands[i].Name;

    // Check that it exists in InstResults.
    auto InstResultIter = InstResults.find(OpName);
    if (InstResultIter == InstResults.end() || !InstResultIter->second)
      I.error("Operand $" + OpName + " does not exist in operand list!");

    TreePatternNodePtr RNode = InstResultIter->second;
    Record *R = cast<DefInit>(RNode->getLeafValue())->getDef();
    ResNodes.push_back(std::move(RNode));
    if (!R)
      I.error("Operand $" + OpName + " should be a set destination: all "
               "outputs must occur before inputs in operand list!");

    if (!checkOperandClass(CGI.Operands[i], R))
      I.error("Operand $" + OpName + " class mismatch!");

    // Remember the return type.
    Results.push_back(CGI.Operands[i].Rec);

    // Remember the result index.
    ResultIndices.push_back(std::distance(InstResults.begin(), InstResultIter));

    // Okay, this one checks out.
    InstResultIter->second = nullptr;
  }

  // Loop over the inputs next.
  std::vector<TreePatternNodePtr> ResultNodeOperands;
  std::vector<Record*> Operands;
  for (unsigned i = NumResults, e = CGI.Operands.size(); i != e; ++i) {
    CGIOperandList::OperandInfo &Op = CGI.Operands[i];
    const std::string &OpName = Op.Name;
    if (OpName.empty())
      I.error("Operand #" + Twine(i) + " in operands list has no name!");

    if (!InstInputs.count(OpName)) {
      // If this is an operand with a DefaultOps set filled in, we can ignore
      // this.  When we codegen it, we will do so as always executed.
      if (Op.Rec->isSubClassOf("OperandWithDefaultOps")) {
        // Does it have a non-empty DefaultOps field?  If so, ignore this
        // operand.
        if (!getDefaultOperand(Op.Rec).DefaultOps.empty())
          continue;
      }
      I.error("Operand $" + OpName +
               " does not appear in the instruction pattern");
    }
    TreePatternNodePtr InVal = InstInputs[OpName];
    InstInputs.erase(OpName);   // It occurred, remove from map.

    if (InVal->isLeaf() && isa<DefInit>(InVal->getLeafValue())) {
      Record *InRec = static_cast<DefInit*>(InVal->getLeafValue())->getDef();
      if (!checkOperandClass(Op, InRec))
        I.error("Operand $" + OpName + "'s register class disagrees"
                 " between the operand and pattern");
    }
    Operands.push_back(Op.Rec);

    // Construct the result for the dest-pattern operand list.
    TreePatternNodePtr OpNode = InVal->clone();

    // No predicate is useful on the result.
    OpNode->clearPredicateCalls();

    // Promote the xform function to be an explicit node if set.
    if (Record *Xform = OpNode->getTransformFn()) {
      OpNode->setTransformFn(nullptr);
      std::vector<TreePatternNodePtr> Children;
      Children.push_back(OpNode);
      OpNode = std::make_shared<TreePatternNode>(Xform, std::move(Children),
                                                 OpNode->getNumTypes());
    }

    ResultNodeOperands.push_back(std::move(OpNode));
  }

  if (!InstInputs.empty())
    I.error("Input operand $" + InstInputs.begin()->first +
            " occurs in pattern but not in operands list!");

  TreePatternNodePtr ResultPattern = std::make_shared<TreePatternNode>(
      I.getRecord(), std::move(ResultNodeOperands),
      GetNumNodeResults(I.getRecord(), *this));
  // Copy fully inferred output node types to instruction result pattern.
  for (unsigned i = 0; i != NumResults; ++i) {
    assert(ResNodes[i]->getNumTypes() == 1 && "FIXME: Unhandled");
    ResultPattern->setType(i, ResNodes[i]->getExtType(0));
    ResultPattern->setResultIndex(i, ResultIndices[i]);
  }

  // FIXME: Assume only the first tree is the pattern. The others are clobber
  // nodes.
  TreePatternNodePtr Pattern = I.getTree(0);
  TreePatternNodePtr SrcPattern;
  if (Pattern->getOperator()->getName() == "set") {
    SrcPattern = Pattern->getChild(Pattern->getNumChildren()-1)->clone();
  } else{
    // Not a set (store or something?)
    SrcPattern = Pattern;
  }

  // Create and insert the instruction.
  // FIXME: InstImpResults should not be part of DAGInstruction.
  Record *R = I.getRecord();
  DAGInsts.emplace(std::piecewise_construct, std::forward_as_tuple(R),
                   std::forward_as_tuple(Results, Operands, InstImpResults,
                                         SrcPattern, ResultPattern));

  LLVM_DEBUG(I.dump());
}

/// ParseInstructions - Parse all of the instructions, inlining and resolving
/// any fragments involved.  This populates the Instructions list with fully
/// resolved instructions.
void CodeGenDAGPatterns::ParseInstructions() {
  std::vector<Record*> Instrs = Records.getAllDerivedDefinitions("Instruction");

  for (Record *Instr : Instrs) {
    ListInit *LI = nullptr;

    if (isa<ListInit>(Instr->getValueInit("Pattern")))
      LI = Instr->getValueAsListInit("Pattern");

    // If there is no pattern, only collect minimal information about the
    // instruction for its operand list.  We have to assume that there is one
    // result, as we have no detailed info. A pattern which references the
    // null_frag operator is as-if no pattern were specified. Normally this
    // is from a multiclass expansion w/ a SDPatternOperator passed in as
    // null_frag.
    if (!LI || LI->empty() || hasNullFragReference(LI)) {
      std::vector<Record*> Results;
      std::vector<Record*> Operands;

      CodeGenInstruction &InstInfo = Target.getInstruction(Instr);

      if (InstInfo.Operands.size() != 0) {
        for (unsigned j = 0, e = InstInfo.Operands.NumDefs; j < e; ++j)
          Results.push_back(InstInfo.Operands[j].Rec);

        // The rest are inputs.
        for (unsigned j = InstInfo.Operands.NumDefs,
               e = InstInfo.Operands.size(); j < e; ++j)
          Operands.push_back(InstInfo.Operands[j].Rec);
      }

      // Create and insert the instruction.
      std::vector<Record*> ImpResults;
      Instructions.insert(std::make_pair(Instr,
                            DAGInstruction(Results, Operands, ImpResults)));
      continue;  // no pattern.
    }

    CodeGenInstruction &CGI = Target.getInstruction(Instr);
    parseInstructionPattern(CGI, LI, Instructions);
  }

  // If we can, convert the instructions to be patterns that are matched!
  for (auto &Entry : Instructions) {
    Record *Instr = Entry.first;
    DAGInstruction &TheInst = Entry.second;
    TreePatternNodePtr SrcPattern = TheInst.getSrcPattern();
    TreePatternNodePtr ResultPattern = TheInst.getResultPattern();

    if (SrcPattern && ResultPattern) {
      TreePattern Pattern(Instr, SrcPattern, true, *this);
      TreePattern Result(Instr, ResultPattern, false, *this);
      ParseOnePattern(Instr, Pattern, Result, TheInst.getImpResults());
    }
  }
}

typedef std::pair<TreePatternNode *, unsigned> NameRecord;

static void FindNames(TreePatternNode *P,
                      std::map<std::string, NameRecord> &Names,
                      TreePattern *PatternTop) {
  if (!P->getName().empty()) {
    NameRecord &Rec = Names[P->getName()];
    // If this is the first instance of the name, remember the node.
    if (Rec.second++ == 0)
      Rec.first = P;
    else if (Rec.first->getExtTypes() != P->getExtTypes())
      PatternTop->error("repetition of value: $" + P->getName() +
                        " where different uses have different types!");
  }

  if (!P->isLeaf()) {
    for (unsigned i = 0, e = P->getNumChildren(); i != e; ++i)
      FindNames(P->getChild(i), Names, PatternTop);
  }
}

std::vector<Predicate> CodeGenDAGPatterns::makePredList(ListInit *L) {
  std::vector<Predicate> Preds;
  for (Init *I : L->getValues()) {
    if (DefInit *Pred = dyn_cast<DefInit>(I))
      Preds.push_back(Pred->getDef());
    else
      llvm_unreachable("Non-def on the list");
  }

  // Sort so that different orders get canonicalized to the same string.
  llvm::sort(Preds);
  return Preds;
}

void CodeGenDAGPatterns::AddPatternToMatch(TreePattern *Pattern,
                                           PatternToMatch &&PTM) {
  // Do some sanity checking on the pattern we're about to match.
  std::string Reason;
  if (!PTM.getSrcPattern()->canPatternMatch(Reason, *this)) {
    PrintWarning(Pattern->getRecord()->getLoc(),
      Twine("Pattern can never match: ") + Reason);
    return;
  }

  // If the source pattern's root is a complex pattern, that complex pattern
  // must specify the nodes it can potentially match.
  if (const ComplexPattern *CP =
        PTM.getSrcPattern()->getComplexPatternInfo(*this))
    if (CP->getRootNodes().empty())
      Pattern->error("ComplexPattern at root must specify list of opcodes it"
                     " could match");


  // Find all of the named values in the input and output, ensure they have the
  // same type.
  std::map<std::string, NameRecord> SrcNames, DstNames;
  FindNames(PTM.getSrcPattern(), SrcNames, Pattern);
  FindNames(PTM.getDstPattern(), DstNames, Pattern);

  // Scan all of the named values in the destination pattern, rejecting them if
  // they don't exist in the input pattern.
  for (const auto &Entry : DstNames) {
    if (SrcNames[Entry.first].first == nullptr)
      Pattern->error("Pattern has input without matching name in output: $" +
                     Entry.first);
  }

  // Scan all of the named values in the source pattern, rejecting them if the
  // name isn't used in the dest, and isn't used to tie two values together.
  for (const auto &Entry : SrcNames)
    if (DstNames[Entry.first].first == nullptr &&
        SrcNames[Entry.first].second == 1)
      Pattern->error("Pattern has dead named input: $" + Entry.first);

  PatternsToMatch.push_back(PTM);
}

void CodeGenDAGPatterns::InferInstructionFlags() {
  ArrayRef<const CodeGenInstruction*> Instructions =
    Target.getInstructionsByEnumValue();

  unsigned Errors = 0;

  // Try to infer flags from all patterns in PatternToMatch.  These include
  // both the primary instruction patterns (which always come first) and
  // patterns defined outside the instruction.
  for (const PatternToMatch &PTM : ptms()) {
    // We can only infer from single-instruction patterns, otherwise we won't
    // know which instruction should get the flags.
    SmallVector<Record*, 8> PatInstrs;
    getInstructionsInTree(PTM.getDstPattern(), PatInstrs);
    if (PatInstrs.size() != 1)
      continue;

    // Get the single instruction.
    CodeGenInstruction &InstInfo = Target.getInstruction(PatInstrs.front());

    // Only infer properties from the first pattern. We'll verify the others.
    if (InstInfo.InferredFrom)
      continue;

    InstAnalyzer PatInfo(*this);
    PatInfo.Analyze(PTM);
    Errors += InferFromPattern(InstInfo, PatInfo, PTM.getSrcRecord());
  }

  if (Errors)
    PrintFatalError("pattern conflicts");

  // If requested by the target, guess any undefined properties.
  if (Target.guessInstructionProperties()) {
    for (unsigned i = 0, e = Instructions.size(); i != e; ++i) {
      CodeGenInstruction *InstInfo =
        const_cast<CodeGenInstruction *>(Instructions[i]);
      if (InstInfo->InferredFrom)
        continue;
      // The mayLoad and mayStore flags default to false.
      // Conservatively assume hasSideEffects if it wasn't explicit.
      if (InstInfo->hasSideEffects_Unset)
        InstInfo->hasSideEffects = true;
    }
    return;
  }

  // Complain about any flags that are still undefined.
  for (unsigned i = 0, e = Instructions.size(); i != e; ++i) {
    CodeGenInstruction *InstInfo =
      const_cast<CodeGenInstruction *>(Instructions[i]);
    if (InstInfo->InferredFrom)
      continue;
    if (InstInfo->hasSideEffects_Unset)
      PrintError(InstInfo->TheDef->getLoc(),
                 "Can't infer hasSideEffects from patterns");
    if (InstInfo->mayStore_Unset)
      PrintError(InstInfo->TheDef->getLoc(),
                 "Can't infer mayStore from patterns");
    if (InstInfo->mayLoad_Unset)
      PrintError(InstInfo->TheDef->getLoc(),
                 "Can't infer mayLoad from patterns");
  }
}


/// Verify instruction flags against pattern node properties.
void CodeGenDAGPatterns::VerifyInstructionFlags() {
  unsigned Errors = 0;
  for (ptm_iterator I = ptm_begin(), E = ptm_end(); I != E; ++I) {
    const PatternToMatch &PTM = *I;
    SmallVector<Record*, 8> Instrs;
    getInstructionsInTree(PTM.getDstPattern(), Instrs);
    if (Instrs.empty())
      continue;

    // Count the number of instructions with each flag set.
    unsigned NumSideEffects = 0;
    unsigned NumStores = 0;
    unsigned NumLoads = 0;
    for (const Record *Instr : Instrs) {
      const CodeGenInstruction &InstInfo = Target.getInstruction(Instr);
      NumSideEffects += InstInfo.hasSideEffects;
      NumStores += InstInfo.mayStore;
      NumLoads += InstInfo.mayLoad;
    }

    // Analyze the source pattern.
    InstAnalyzer PatInfo(*this);
    PatInfo.Analyze(PTM);

    // Collect error messages.
    SmallVector<std::string, 4> Msgs;

    // Check for missing flags in the output.
    // Permit extra flags for now at least.
    if (PatInfo.hasSideEffects && !NumSideEffects)
      Msgs.push_back("pattern has side effects, but hasSideEffects isn't set");

    // Don't verify store flags on instructions with side effects. At least for
    // intrinsics, side effects implies mayStore.
    if (!PatInfo.hasSideEffects && PatInfo.mayStore && !NumStores)
      Msgs.push_back("pattern may store, but mayStore isn't set");

    // Similarly, mayStore implies mayLoad on intrinsics.
    if (!PatInfo.mayStore && PatInfo.mayLoad && !NumLoads)
      Msgs.push_back("pattern may load, but mayLoad isn't set");

    // Print error messages.
    if (Msgs.empty())
      continue;
    ++Errors;

    for (const std::string &Msg : Msgs)
      PrintError(PTM.getSrcRecord()->getLoc(), Twine(Msg) + " on the " +
                 (Instrs.size() == 1 ?
                  "instruction" : "output instructions"));
    // Provide the location of the relevant instruction definitions.
    for (const Record *Instr : Instrs) {
      if (Instr != PTM.getSrcRecord())
        PrintError(Instr->getLoc(), "defined here");
      const CodeGenInstruction &InstInfo = Target.getInstruction(Instr);
      if (InstInfo.InferredFrom &&
          InstInfo.InferredFrom != InstInfo.TheDef &&
          InstInfo.InferredFrom != PTM.getSrcRecord())
        PrintError(InstInfo.InferredFrom->getLoc(), "inferred from pattern");
    }
  }
  if (Errors)
    PrintFatalError("Errors in DAG patterns");
}

/// Given a pattern result with an unresolved type, see if we can find one
/// instruction with an unresolved result type.  Force this result type to an
/// arbitrary element if it's possible types to converge results.
static bool ForceArbitraryInstResultType(TreePatternNode *N, TreePattern &TP) {
  if (N->isLeaf())
    return false;

  // Analyze children.
  for (unsigned i = 0, e = N->getNumChildren(); i != e; ++i)
    if (ForceArbitraryInstResultType(N->getChild(i), TP))
      return true;

  if (!N->getOperator()->isSubClassOf("Instruction"))
    return false;

  // If this type is already concrete or completely unknown we can't do
  // anything.
  TypeInfer &TI = TP.getInfer();
  for (unsigned i = 0, e = N->getNumTypes(); i != e; ++i) {
    if (N->getExtType(i).empty() || TI.isConcrete(N->getExtType(i), false))
      continue;

    // Otherwise, force its type to an arbitrary choice.
    if (TI.forceArbitrary(N->getExtType(i)))
      return true;
  }

  return false;
}

// Promote xform function to be an explicit node wherever set.
static TreePatternNodePtr PromoteXForms(TreePatternNodePtr N) {
  if (Record *Xform = N->getTransformFn()) {
      N->setTransformFn(nullptr);
      std::vector<TreePatternNodePtr> Children;
      Children.push_back(PromoteXForms(N));
      return std::make_shared<TreePatternNode>(Xform, std::move(Children),
                                               N->getNumTypes());
  }

  if (!N->isLeaf())
    for (unsigned i = 0, e = N->getNumChildren(); i != e; ++i) {
      TreePatternNodePtr Child = N->getChildShared(i);
      N->setChild(i, PromoteXForms(Child));
    }
  return N;
}

void CodeGenDAGPatterns::ParseOnePattern(Record *TheDef,
       TreePattern &Pattern, TreePattern &Result,
       const std::vector<Record *> &InstImpResults) {

  // Inline pattern fragments and expand multiple alternatives.
  Pattern.InlinePatternFragments();
  Result.InlinePatternFragments();

  if (Result.getNumTrees() != 1)
    Result.error("Cannot use multi-alternative fragments in result pattern!");

  // Infer types.
  bool IterateInference;
  bool InferredAllPatternTypes, InferredAllResultTypes;
  do {
    // Infer as many types as possible.  If we cannot infer all of them, we
    // can never do anything with this pattern: report it to the user.
    InferredAllPatternTypes =
        Pattern.InferAllTypes(&Pattern.getNamedNodesMap());

    // Infer as many types as possible.  If we cannot infer all of them, we
    // can never do anything with this pattern: report it to the user.
    InferredAllResultTypes =
        Result.InferAllTypes(&Pattern.getNamedNodesMap());

    IterateInference = false;

    // Apply the type of the result to the source pattern.  This helps us
    // resolve cases where the input type is known to be a pointer type (which
    // is considered resolved), but the result knows it needs to be 32- or
    // 64-bits.  Infer the other way for good measure.
    for (auto T : Pattern.getTrees())
      for (unsigned i = 0, e = std::min(Result.getOnlyTree()->getNumTypes(),
                                        T->getNumTypes());
         i != e; ++i) {
        IterateInference |= T->UpdateNodeType(
            i, Result.getOnlyTree()->getExtType(i), Result);
        IterateInference |= Result.getOnlyTree()->UpdateNodeType(
            i, T->getExtType(i), Result);
      }

    // If our iteration has converged and the input pattern's types are fully
    // resolved but the result pattern is not fully resolved, we may have a
    // situation where we have two instructions in the result pattern and
    // the instructions require a common register class, but don't care about
    // what actual MVT is used.  This is actually a bug in our modelling:
    // output patterns should have register classes, not MVTs.
    //
    // In any case, to handle this, we just go through and disambiguate some
    // arbitrary types to the result pattern's nodes.
    if (!IterateInference && InferredAllPatternTypes &&
        !InferredAllResultTypes)
      IterateInference =
          ForceArbitraryInstResultType(Result.getTree(0).get(), Result);
  } while (IterateInference);

  // Verify that we inferred enough types that we can do something with the
  // pattern and result.  If these fire the user has to add type casts.
  if (!InferredAllPatternTypes)
    Pattern.error("Could not infer all types in pattern!");
  if (!InferredAllResultTypes) {
    Pattern.dump();
    Result.error("Could not infer all types in pattern result!");
  }

  // Promote xform function to be an explicit node wherever set.
  TreePatternNodePtr DstShared = PromoteXForms(Result.getOnlyTree());

  TreePattern Temp(Result.getRecord(), DstShared, false, *this);
  Temp.InferAllTypes();

  ListInit *Preds = TheDef->getValueAsListInit("Predicates");
  int Complexity = TheDef->getValueAsInt("AddedComplexity");

  if (PatternRewriter)
    PatternRewriter(&Pattern);

  // A pattern may end up with an "impossible" type, i.e. a situation
  // where all types have been eliminated for some node in this pattern.
  // This could occur for intrinsics that only make sense for a specific
  // value type, and use a specific register class. If, for some mode,
  // that register class does not accept that type, the type inference
  // will lead to a contradiction, which is not an error however, but
  // a sign that this pattern will simply never match.
  if (Temp.getOnlyTree()->hasPossibleType())
    for (auto T : Pattern.getTrees())
      if (T->hasPossibleType())
        AddPatternToMatch(&Pattern,
                          PatternToMatch(TheDef, makePredList(Preds),
                                         T, Temp.getOnlyTree(),
                                         InstImpResults, Complexity,
                                         TheDef->getID()));
}

void CodeGenDAGPatterns::ParsePatterns() {
  std::vector<Record*> Patterns = Records.getAllDerivedDefinitions("Pattern");

  for (Record *CurPattern : Patterns) {
    DagInit *Tree = CurPattern->getValueAsDag("PatternToMatch");

    // If the pattern references the null_frag, there's nothing to do.
    if (hasNullFragReference(Tree))
      continue;

    TreePattern Pattern(CurPattern, Tree, true, *this);

    ListInit *LI = CurPattern->getValueAsListInit("ResultInstrs");
    if (LI->empty()) continue;  // no pattern.

    // Parse the instruction.
    TreePattern Result(CurPattern, LI, false, *this);

    if (Result.getNumTrees() != 1)
      Result.error("Cannot handle instructions producing instructions "
                   "with temporaries yet!");

    // Validate that the input pattern is correct.
    std::map<std::string, TreePatternNodePtr> InstInputs;
    MapVector<std::string, TreePatternNodePtr, std::map<std::string, unsigned>>
        InstResults;
    std::vector<Record*> InstImpResults;
    for (unsigned j = 0, ee = Pattern.getNumTrees(); j != ee; ++j)
      FindPatternInputsAndOutputs(Pattern, Pattern.getTree(j), InstInputs,
                                  InstResults, InstImpResults);

    ParseOnePattern(CurPattern, Pattern, Result, InstImpResults);
  }
}

static void collectModes(std::set<unsigned> &Modes, const TreePatternNode *N) {
  for (const TypeSetByHwMode &VTS : N->getExtTypes())
    for (const auto &I : VTS)
      Modes.insert(I.first);

  for (unsigned i = 0, e = N->getNumChildren(); i != e; ++i)
    collectModes(Modes, N->getChild(i));
}

void CodeGenDAGPatterns::ExpandHwModeBasedTypes() {
  const CodeGenHwModes &CGH = getTargetInfo().getHwModes();
  std::map<unsigned,std::vector<Predicate>> ModeChecks;
  std::vector<PatternToMatch> Copy = PatternsToMatch;
  PatternsToMatch.clear();

  auto AppendPattern = [this, &ModeChecks](PatternToMatch &P, unsigned Mode) {
    TreePatternNodePtr NewSrc = P.SrcPattern->clone();
    TreePatternNodePtr NewDst = P.DstPattern->clone();
    if (!NewSrc->setDefaultMode(Mode) || !NewDst->setDefaultMode(Mode)) {
      return;
    }

    std::vector<Predicate> Preds = P.Predicates;
    const std::vector<Predicate> &MC = ModeChecks[Mode];
    Preds.insert(Preds.end(), MC.begin(), MC.end());
    PatternsToMatch.emplace_back(P.getSrcRecord(), Preds, std::move(NewSrc),
                                 std::move(NewDst), P.getDstRegs(),
                                 P.getAddedComplexity(), Record::getNewUID(),
                                 Mode);
  };

  for (PatternToMatch &P : Copy) {
    TreePatternNodePtr SrcP = nullptr, DstP = nullptr;
    if (P.SrcPattern->hasProperTypeByHwMode())
      SrcP = P.SrcPattern;
    if (P.DstPattern->hasProperTypeByHwMode())
      DstP = P.DstPattern;
    if (!SrcP && !DstP) {
      PatternsToMatch.push_back(P);
      continue;
    }

    std::set<unsigned> Modes;
    if (SrcP)
      collectModes(Modes, SrcP.get());
    if (DstP)
      collectModes(Modes, DstP.get());

    // The predicate for the default mode needs to be constructed for each
    // pattern separately.
    // Since not all modes must be present in each pattern, if a mode m is
    // absent, then there is no point in constructing a check for m. If such
    // a check was created, it would be equivalent to checking the default
    // mode, except not all modes' predicates would be a part of the checking
    // code. The subsequently generated check for the default mode would then
    // have the exact same patterns, but a different predicate code. To avoid
    // duplicated patterns with different predicate checks, construct the
    // default check as a negation of all predicates that are actually present
    // in the source/destination patterns.
    std::vector<Predicate> DefaultPred;

    for (unsigned M : Modes) {
      if (M == DefaultMode)
        continue;
      if (ModeChecks.find(M) != ModeChecks.end())
        continue;

      // Fill the map entry for this mode.
      const HwMode &HM = CGH.getMode(M);
      ModeChecks[M].emplace_back(Predicate(HM.Features, true));

      // Add negations of the HM's predicates to the default predicate.
      DefaultPred.emplace_back(Predicate(HM.Features, false));
    }

    for (unsigned M : Modes) {
      if (M == DefaultMode)
        continue;
      AppendPattern(P, M);
    }

    bool HasDefault = Modes.count(DefaultMode);
    if (HasDefault)
      AppendPattern(P, DefaultMode);
  }
}

/// Dependent variable map for CodeGenDAGPattern variant generation
typedef StringMap<int> DepVarMap;

static void FindDepVarsOf(TreePatternNode *N, DepVarMap &DepMap) {
  if (N->isLeaf()) {
    if (N->hasName() && isa<DefInit>(N->getLeafValue()))
      DepMap[N->getName()]++;
  } else {
    for (size_t i = 0, e = N->getNumChildren(); i != e; ++i)
      FindDepVarsOf(N->getChild(i), DepMap);
  }
}

/// Find dependent variables within child patterns
static void FindDepVars(TreePatternNode *N, MultipleUseVarSet &DepVars) {
  DepVarMap depcounts;
  FindDepVarsOf(N, depcounts);
  for (const auto &Pair : depcounts) {
    if (Pair.getValue() > 1)
      DepVars.insert(Pair.getKey());
  }
}

#ifndef NDEBUG
/// Dump the dependent variable set:
static void DumpDepVars(MultipleUseVarSet &DepVars) {
  if (DepVars.empty()) {
    LLVM_DEBUG(errs() << "<empty set>");
  } else {
    LLVM_DEBUG(errs() << "[ ");
    for (const auto &DepVar : DepVars) {
      LLVM_DEBUG(errs() << DepVar.getKey() << " ");
    }
    LLVM_DEBUG(errs() << "]");
  }
}
#endif


/// CombineChildVariants - Given a bunch of permutations of each child of the
/// 'operator' node, put them together in all possible ways.
static void CombineChildVariants(
    TreePatternNodePtr Orig,
    const std::vector<std::vector<TreePatternNodePtr>> &ChildVariants,
    std::vector<TreePatternNodePtr> &OutVariants, CodeGenDAGPatterns &CDP,
    const MultipleUseVarSet &DepVars) {
  // Make sure that each operand has at least one variant to choose from.
  for (const auto &Variants : ChildVariants)
    if (Variants.empty())
      return;

  // The end result is an all-pairs construction of the resultant pattern.
  std::vector<unsigned> Idxs;
  Idxs.resize(ChildVariants.size());
  bool NotDone;
  do {
#ifndef NDEBUG
    LLVM_DEBUG(if (!Idxs.empty()) {
      errs() << Orig->getOperator()->getName() << ": Idxs = [ ";
      for (unsigned Idx : Idxs) {
        errs() << Idx << " ";
      }
      errs() << "]\n";
    });
#endif
    // Create the variant and add it to the output list.
    std::vector<TreePatternNodePtr> NewChildren;
    for (unsigned i = 0, e = ChildVariants.size(); i != e; ++i)
      NewChildren.push_back(ChildVariants[i][Idxs[i]]);
    TreePatternNodePtr R = std::make_shared<TreePatternNode>(
        Orig->getOperator(), std::move(NewChildren), Orig->getNumTypes());

    // Copy over properties.
    R->setName(Orig->getName());
    R->setNamesAsPredicateArg(Orig->getNamesAsPredicateArg());
    R->setPredicateCalls(Orig->getPredicateCalls());
    R->setTransformFn(Orig->getTransformFn());
    for (unsigned i = 0, e = Orig->getNumTypes(); i != e; ++i)
      R->setType(i, Orig->getExtType(i));

    // If this pattern cannot match, do not include it as a variant.
    std::string ErrString;
    // Scan to see if this pattern has already been emitted.  We can get
    // duplication due to things like commuting:
    //   (and GPRC:$a, GPRC:$b) -> (and GPRC:$b, GPRC:$a)
    // which are the same pattern.  Ignore the dups.
    if (R->canPatternMatch(ErrString, CDP) &&
        none_of(OutVariants, [&](TreePatternNodePtr Variant) {
          return R->isIsomorphicTo(Variant.get(), DepVars);
        }))
      OutVariants.push_back(R);

    // Increment indices to the next permutation by incrementing the
    // indices from last index backward, e.g., generate the sequence
    // [0, 0], [0, 1], [1, 0], [1, 1].
    int IdxsIdx;
    for (IdxsIdx = Idxs.size() - 1; IdxsIdx >= 0; --IdxsIdx) {
      if (++Idxs[IdxsIdx] == ChildVariants[IdxsIdx].size())
        Idxs[IdxsIdx] = 0;
      else
        break;
    }
    NotDone = (IdxsIdx >= 0);
  } while (NotDone);
}

/// CombineChildVariants - A helper function for binary operators.
///
static void CombineChildVariants(TreePatternNodePtr Orig,
                                 const std::vector<TreePatternNodePtr> &LHS,
                                 const std::vector<TreePatternNodePtr> &RHS,
                                 std::vector<TreePatternNodePtr> &OutVariants,
                                 CodeGenDAGPatterns &CDP,
                                 const MultipleUseVarSet &DepVars) {
  std::vector<std::vector<TreePatternNodePtr>> ChildVariants;
  ChildVariants.push_back(LHS);
  ChildVariants.push_back(RHS);
  CombineChildVariants(Orig, ChildVariants, OutVariants, CDP, DepVars);
}

static void
GatherChildrenOfAssociativeOpcode(TreePatternNodePtr N,
                                  std::vector<TreePatternNodePtr> &Children) {
  assert(N->getNumChildren()==2 &&"Associative but doesn't have 2 children!");
  Record *Operator = N->getOperator();

  // Only permit raw nodes.
  if (!N->getName().empty() || !N->getPredicateCalls().empty() ||
      N->getTransformFn()) {
    Children.push_back(N);
    return;
  }

  if (N->getChild(0)->isLeaf() || N->getChild(0)->getOperator() != Operator)
    Children.push_back(N->getChildShared(0));
  else
    GatherChildrenOfAssociativeOpcode(N->getChildShared(0), Children);

  if (N->getChild(1)->isLeaf() || N->getChild(1)->getOperator() != Operator)
    Children.push_back(N->getChildShared(1));
  else
    GatherChildrenOfAssociativeOpcode(N->getChildShared(1), Children);
}

/// GenerateVariantsOf - Given a pattern N, generate all permutations we can of
/// the (potentially recursive) pattern by using algebraic laws.
///
static void GenerateVariantsOf(TreePatternNodePtr N,
                               std::vector<TreePatternNodePtr> &OutVariants,
                               CodeGenDAGPatterns &CDP,
                               const MultipleUseVarSet &DepVars) {
  // We cannot permute leaves or ComplexPattern uses.
  if (N->isLeaf() || N->getOperator()->isSubClassOf("ComplexPattern")) {
    OutVariants.push_back(N);
    return;
  }

  // Look up interesting info about the node.
  const SDNodeInfo &NodeInfo = CDP.getSDNodeInfo(N->getOperator());

  // If this node is associative, re-associate.
  if (NodeInfo.hasProperty(SDNPAssociative)) {
    // Re-associate by pulling together all of the linked operators
    std::vector<TreePatternNodePtr> MaximalChildren;
    GatherChildrenOfAssociativeOpcode(N, MaximalChildren);

    // Only handle child sizes of 3.  Otherwise we'll end up trying too many
    // permutations.
    if (MaximalChildren.size() == 3) {
      // Find the variants of all of our maximal children.
      std::vector<TreePatternNodePtr> AVariants, BVariants, CVariants;
      GenerateVariantsOf(MaximalChildren[0], AVariants, CDP, DepVars);
      GenerateVariantsOf(MaximalChildren[1], BVariants, CDP, DepVars);
      GenerateVariantsOf(MaximalChildren[2], CVariants, CDP, DepVars);

      // There are only two ways we can permute the tree:
      //   (A op B) op C    and    A op (B op C)
      // Within these forms, we can also permute A/B/C.

      // Generate legal pair permutations of A/B/C.
      std::vector<TreePatternNodePtr> ABVariants;
      std::vector<TreePatternNodePtr> BAVariants;
      std::vector<TreePatternNodePtr> ACVariants;
      std::vector<TreePatternNodePtr> CAVariants;
      std::vector<TreePatternNodePtr> BCVariants;
      std::vector<TreePatternNodePtr> CBVariants;
      CombineChildVariants(N, AVariants, BVariants, ABVariants, CDP, DepVars);
      CombineChildVariants(N, BVariants, AVariants, BAVariants, CDP, DepVars);
      CombineChildVariants(N, AVariants, CVariants, ACVariants, CDP, DepVars);
      CombineChildVariants(N, CVariants, AVariants, CAVariants, CDP, DepVars);
      CombineChildVariants(N, BVariants, CVariants, BCVariants, CDP, DepVars);
      CombineChildVariants(N, CVariants, BVariants, CBVariants, CDP, DepVars);

      // Combine those into the result: (x op x) op x
      CombineChildVariants(N, ABVariants, CVariants, OutVariants, CDP, DepVars);
      CombineChildVariants(N, BAVariants, CVariants, OutVariants, CDP, DepVars);
      CombineChildVariants(N, ACVariants, BVariants, OutVariants, CDP, DepVars);
      CombineChildVariants(N, CAVariants, BVariants, OutVariants, CDP, DepVars);
      CombineChildVariants(N, BCVariants, AVariants, OutVariants, CDP, DepVars);
      CombineChildVariants(N, CBVariants, AVariants, OutVariants, CDP, DepVars);

      // Combine those into the result: x op (x op x)
      CombineChildVariants(N, CVariants, ABVariants, OutVariants, CDP, DepVars);
      CombineChildVariants(N, CVariants, BAVariants, OutVariants, CDP, DepVars);
      CombineChildVariants(N, BVariants, ACVariants, OutVariants, CDP, DepVars);
      CombineChildVariants(N, BVariants, CAVariants, OutVariants, CDP, DepVars);
      CombineChildVariants(N, AVariants, BCVariants, OutVariants, CDP, DepVars);
      CombineChildVariants(N, AVariants, CBVariants, OutVariants, CDP, DepVars);
      return;
    }
  }

  // Compute permutations of all children.
  std::vector<std::vector<TreePatternNodePtr>> ChildVariants;
  ChildVariants.resize(N->getNumChildren());
  for (unsigned i = 0, e = N->getNumChildren(); i != e; ++i)
    GenerateVariantsOf(N->getChildShared(i), ChildVariants[i], CDP, DepVars);

  // Build all permutations based on how the children were formed.
  CombineChildVariants(N, ChildVariants, OutVariants, CDP, DepVars);

  // If this node is commutative, consider the commuted order.
  bool isCommIntrinsic = N->isCommutativeIntrinsic(CDP);
  if (NodeInfo.hasProperty(SDNPCommutative) || isCommIntrinsic) {
    assert((N->getNumChildren()>=2 || isCommIntrinsic) &&
           "Commutative but doesn't have 2 children!");
    // Don't count children which are actually register references.
    unsigned NC = 0;
    for (unsigned i = 0, e = N->getNumChildren(); i != e; ++i) {
      TreePatternNode *Child = N->getChild(i);
      if (Child->isLeaf())
        if (DefInit *DI = dyn_cast<DefInit>(Child->getLeafValue())) {
          Record *RR = DI->getDef();
          if (RR->isSubClassOf("Register"))
            continue;
        }
      NC++;
    }
    // Consider the commuted order.
    if (isCommIntrinsic) {
      // Commutative intrinsic. First operand is the intrinsic id, 2nd and 3rd
      // operands are the commutative operands, and there might be more operands
      // after those.
      assert(NC >= 3 &&
             "Commutative intrinsic should have at least 3 children!");
      std::vector<std::vector<TreePatternNodePtr>> Variants;
      Variants.push_back(std::move(ChildVariants[0])); // Intrinsic id.
      Variants.push_back(std::move(ChildVariants[2]));
      Variants.push_back(std::move(ChildVariants[1]));
      for (unsigned i = 3; i != NC; ++i)
        Variants.push_back(std::move(ChildVariants[i]));
      CombineChildVariants(N, Variants, OutVariants, CDP, DepVars);
    } else if (NC == N->getNumChildren()) {
      std::vector<std::vector<TreePatternNodePtr>> Variants;
      Variants.push_back(std::move(ChildVariants[1]));
      Variants.push_back(std::move(ChildVariants[0]));
      for (unsigned i = 2; i != NC; ++i)
        Variants.push_back(std::move(ChildVariants[i]));
      CombineChildVariants(N, Variants, OutVariants, CDP, DepVars);
    }
  }
}


// GenerateVariants - Generate variants.  For example, commutative patterns can
// match multiple ways.  Add them to PatternsToMatch as well.
void CodeGenDAGPatterns::GenerateVariants() {
  LLVM_DEBUG(errs() << "Generating instruction variants.\n");

  // Loop over all of the patterns we've collected, checking to see if we can
  // generate variants of the instruction, through the exploitation of
  // identities.  This permits the target to provide aggressive matching without
  // the .td file having to contain tons of variants of instructions.
  //
  // Note that this loop adds new patterns to the PatternsToMatch list, but we
  // intentionally do not reconsider these.  Any variants of added patterns have
  // already been added.
  //
  const unsigned NumOriginalPatterns = PatternsToMatch.size();
  BitVector MatchedPatterns(NumOriginalPatterns);
  std::vector<BitVector> MatchedPredicates(NumOriginalPatterns,
                                           BitVector(NumOriginalPatterns));

  typedef std::pair<MultipleUseVarSet, std::vector<TreePatternNodePtr>>
      DepsAndVariants;
  std::map<unsigned, DepsAndVariants> PatternsWithVariants;

  // Collect patterns with more than one variant.
  for (unsigned i = 0; i != NumOriginalPatterns; ++i) {
    MultipleUseVarSet DepVars;
    std::vector<TreePatternNodePtr> Variants;
    FindDepVars(PatternsToMatch[i].getSrcPattern(), DepVars);
    LLVM_DEBUG(errs() << "Dependent/multiply used variables: ");
    LLVM_DEBUG(DumpDepVars(DepVars));
    LLVM_DEBUG(errs() << "\n");
    GenerateVariantsOf(PatternsToMatch[i].getSrcPatternShared(), Variants,
                       *this, DepVars);

    assert(!Variants.empty() && "Must create at least original variant!");
    if (Variants.size() == 1) // No additional variants for this pattern.
      continue;

    LLVM_DEBUG(errs() << "FOUND VARIANTS OF: ";
               PatternsToMatch[i].getSrcPattern()->dump(); errs() << "\n");

    PatternsWithVariants[i] = std::make_pair(DepVars, Variants);

    // Cache matching predicates.
    if (MatchedPatterns[i])
      continue;

    const std::vector<Predicate> &Predicates =
        PatternsToMatch[i].getPredicates();

    BitVector &Matches = MatchedPredicates[i];
    MatchedPatterns.set(i);
    Matches.set(i);

    // Don't test patterns that have already been cached - it won't match.
    for (unsigned p = 0; p != NumOriginalPatterns; ++p)
      if (!MatchedPatterns[p])
        Matches[p] = (Predicates == PatternsToMatch[p].getPredicates());

    // Copy this to all the matching patterns.
    for (int p = Matches.find_first(); p != -1; p = Matches.find_next(p))
      if (p != (int)i) {
        MatchedPatterns.set(p);
        MatchedPredicates[p] = Matches;
      }
  }

  for (auto it : PatternsWithVariants) {
    unsigned i = it.first;
    const MultipleUseVarSet &DepVars = it.second.first;
    const std::vector<TreePatternNodePtr> &Variants = it.second.second;

    for (unsigned v = 0, e = Variants.size(); v != e; ++v) {
      TreePatternNodePtr Variant = Variants[v];
      BitVector &Matches = MatchedPredicates[i];

      LLVM_DEBUG(errs() << "  VAR#" << v << ": "; Variant->dump();
                 errs() << "\n");

      // Scan to see if an instruction or explicit pattern already matches this.
      bool AlreadyExists = false;
      for (unsigned p = 0, e = PatternsToMatch.size(); p != e; ++p) {
        // Skip if the top level predicates do not match.
        if (!Matches[p])
          continue;
        // Check to see if this variant already exists.
        if (Variant->isIsomorphicTo(PatternsToMatch[p].getSrcPattern(),
                                    DepVars)) {
          LLVM_DEBUG(errs() << "  *** ALREADY EXISTS, ignoring variant.\n");
          AlreadyExists = true;
          break;
        }
      }
      // If we already have it, ignore the variant.
      if (AlreadyExists) continue;

      // Otherwise, add it to the list of patterns we have.
      PatternsToMatch.push_back(PatternToMatch(
          PatternsToMatch[i].getSrcRecord(), PatternsToMatch[i].getPredicates(),
          Variant, PatternsToMatch[i].getDstPatternShared(),
          PatternsToMatch[i].getDstRegs(),
          PatternsToMatch[i].getAddedComplexity(), Record::getNewUID()));
      MatchedPredicates.push_back(Matches);

      // Add a new match the same as this pattern.
      for (auto &P : MatchedPredicates)
        P.push_back(P[i]);
    }

    LLVM_DEBUG(errs() << "\n");
  }
}
