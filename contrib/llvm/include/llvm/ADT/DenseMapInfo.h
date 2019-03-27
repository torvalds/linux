//===- llvm/ADT/DenseMapInfo.h - Type traits for DenseMap -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines DenseMapInfo traits for DenseMap.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_DENSEMAPINFO_H
#define LLVM_ADT_DENSEMAPINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace llvm {

template<typename T>
struct DenseMapInfo {
  //static inline T getEmptyKey();
  //static inline T getTombstoneKey();
  //static unsigned getHashValue(const T &Val);
  //static bool isEqual(const T &LHS, const T &RHS);
};

// Provide DenseMapInfo for all pointers.
template<typename T>
struct DenseMapInfo<T*> {
  static inline T* getEmptyKey() {
    uintptr_t Val = static_cast<uintptr_t>(-1);
    Val <<= PointerLikeTypeTraits<T*>::NumLowBitsAvailable;
    return reinterpret_cast<T*>(Val);
  }

  static inline T* getTombstoneKey() {
    uintptr_t Val = static_cast<uintptr_t>(-2);
    Val <<= PointerLikeTypeTraits<T*>::NumLowBitsAvailable;
    return reinterpret_cast<T*>(Val);
  }

  static unsigned getHashValue(const T *PtrVal) {
    return (unsigned((uintptr_t)PtrVal) >> 4) ^
           (unsigned((uintptr_t)PtrVal) >> 9);
  }

  static bool isEqual(const T *LHS, const T *RHS) { return LHS == RHS; }
};

// Provide DenseMapInfo for chars.
template<> struct DenseMapInfo<char> {
  static inline char getEmptyKey() { return ~0; }
  static inline char getTombstoneKey() { return ~0 - 1; }
  static unsigned getHashValue(const char& Val) { return Val * 37U; }

  static bool isEqual(const char &LHS, const char &RHS) {
    return LHS == RHS;
  }
};

// Provide DenseMapInfo for unsigned shorts.
template <> struct DenseMapInfo<unsigned short> {
  static inline unsigned short getEmptyKey() { return 0xFFFF; }
  static inline unsigned short getTombstoneKey() { return 0xFFFF - 1; }
  static unsigned getHashValue(const unsigned short &Val) { return Val * 37U; }

  static bool isEqual(const unsigned short &LHS, const unsigned short &RHS) {
    return LHS == RHS;
  }
};

// Provide DenseMapInfo for unsigned ints.
template<> struct DenseMapInfo<unsigned> {
  static inline unsigned getEmptyKey() { return ~0U; }
  static inline unsigned getTombstoneKey() { return ~0U - 1; }
  static unsigned getHashValue(const unsigned& Val) { return Val * 37U; }

  static bool isEqual(const unsigned& LHS, const unsigned& RHS) {
    return LHS == RHS;
  }
};

// Provide DenseMapInfo for unsigned longs.
template<> struct DenseMapInfo<unsigned long> {
  static inline unsigned long getEmptyKey() { return ~0UL; }
  static inline unsigned long getTombstoneKey() { return ~0UL - 1L; }

  static unsigned getHashValue(const unsigned long& Val) {
    return (unsigned)(Val * 37UL);
  }

  static bool isEqual(const unsigned long& LHS, const unsigned long& RHS) {
    return LHS == RHS;
  }
};

// Provide DenseMapInfo for unsigned long longs.
template<> struct DenseMapInfo<unsigned long long> {
  static inline unsigned long long getEmptyKey() { return ~0ULL; }
  static inline unsigned long long getTombstoneKey() { return ~0ULL - 1ULL; }

  static unsigned getHashValue(const unsigned long long& Val) {
    return (unsigned)(Val * 37ULL);
  }

  static bool isEqual(const unsigned long long& LHS,
                      const unsigned long long& RHS) {
    return LHS == RHS;
  }
};

// Provide DenseMapInfo for shorts.
template <> struct DenseMapInfo<short> {
  static inline short getEmptyKey() { return 0x7FFF; }
  static inline short getTombstoneKey() { return -0x7FFF - 1; }
  static unsigned getHashValue(const short &Val) { return Val * 37U; }
  static bool isEqual(const short &LHS, const short &RHS) { return LHS == RHS; }
};

// Provide DenseMapInfo for ints.
template<> struct DenseMapInfo<int> {
  static inline int getEmptyKey() { return 0x7fffffff; }
  static inline int getTombstoneKey() { return -0x7fffffff - 1; }
  static unsigned getHashValue(const int& Val) { return (unsigned)(Val * 37U); }

  static bool isEqual(const int& LHS, const int& RHS) {
    return LHS == RHS;
  }
};

// Provide DenseMapInfo for longs.
template<> struct DenseMapInfo<long> {
  static inline long getEmptyKey() {
    return (1UL << (sizeof(long) * 8 - 1)) - 1UL;
  }

  static inline long getTombstoneKey() { return getEmptyKey() - 1L; }

  static unsigned getHashValue(const long& Val) {
    return (unsigned)(Val * 37UL);
  }

  static bool isEqual(const long& LHS, const long& RHS) {
    return LHS == RHS;
  }
};

// Provide DenseMapInfo for long longs.
template<> struct DenseMapInfo<long long> {
  static inline long long getEmptyKey() { return 0x7fffffffffffffffLL; }
  static inline long long getTombstoneKey() { return -0x7fffffffffffffffLL-1; }

  static unsigned getHashValue(const long long& Val) {
    return (unsigned)(Val * 37ULL);
  }

  static bool isEqual(const long long& LHS,
                      const long long& RHS) {
    return LHS == RHS;
  }
};

// Provide DenseMapInfo for all pairs whose members have info.
template<typename T, typename U>
struct DenseMapInfo<std::pair<T, U>> {
  using Pair = std::pair<T, U>;
  using FirstInfo = DenseMapInfo<T>;
  using SecondInfo = DenseMapInfo<U>;

  static inline Pair getEmptyKey() {
    return std::make_pair(FirstInfo::getEmptyKey(),
                          SecondInfo::getEmptyKey());
  }

  static inline Pair getTombstoneKey() {
    return std::make_pair(FirstInfo::getTombstoneKey(),
                          SecondInfo::getTombstoneKey());
  }

  static unsigned getHashValue(const Pair& PairVal) {
    uint64_t key = (uint64_t)FirstInfo::getHashValue(PairVal.first) << 32
          | (uint64_t)SecondInfo::getHashValue(PairVal.second);
    key += ~(key << 32);
    key ^= (key >> 22);
    key += ~(key << 13);
    key ^= (key >> 8);
    key += (key << 3);
    key ^= (key >> 15);
    key += ~(key << 27);
    key ^= (key >> 31);
    return (unsigned)key;
  }

  static bool isEqual(const Pair &LHS, const Pair &RHS) {
    return FirstInfo::isEqual(LHS.first, RHS.first) &&
           SecondInfo::isEqual(LHS.second, RHS.second);
  }
};

// Provide DenseMapInfo for StringRefs.
template <> struct DenseMapInfo<StringRef> {
  static inline StringRef getEmptyKey() {
    return StringRef(reinterpret_cast<const char *>(~static_cast<uintptr_t>(0)),
                     0);
  }

  static inline StringRef getTombstoneKey() {
    return StringRef(reinterpret_cast<const char *>(~static_cast<uintptr_t>(1)),
                     0);
  }

  static unsigned getHashValue(StringRef Val) {
    assert(Val.data() != getEmptyKey().data() && "Cannot hash the empty key!");
    assert(Val.data() != getTombstoneKey().data() &&
           "Cannot hash the tombstone key!");
    return (unsigned)(hash_value(Val));
  }

  static bool isEqual(StringRef LHS, StringRef RHS) {
    if (RHS.data() == getEmptyKey().data())
      return LHS.data() == getEmptyKey().data();
    if (RHS.data() == getTombstoneKey().data())
      return LHS.data() == getTombstoneKey().data();
    return LHS == RHS;
  }
};

// Provide DenseMapInfo for ArrayRefs.
template <typename T> struct DenseMapInfo<ArrayRef<T>> {
  static inline ArrayRef<T> getEmptyKey() {
    return ArrayRef<T>(reinterpret_cast<const T *>(~static_cast<uintptr_t>(0)),
                       size_t(0));
  }

  static inline ArrayRef<T> getTombstoneKey() {
    return ArrayRef<T>(reinterpret_cast<const T *>(~static_cast<uintptr_t>(1)),
                       size_t(0));
  }

  static unsigned getHashValue(ArrayRef<T> Val) {
    assert(Val.data() != getEmptyKey().data() && "Cannot hash the empty key!");
    assert(Val.data() != getTombstoneKey().data() &&
           "Cannot hash the tombstone key!");
    return (unsigned)(hash_value(Val));
  }

  static bool isEqual(ArrayRef<T> LHS, ArrayRef<T> RHS) {
    if (RHS.data() == getEmptyKey().data())
      return LHS.data() == getEmptyKey().data();
    if (RHS.data() == getTombstoneKey().data())
      return LHS.data() == getTombstoneKey().data();
    return LHS == RHS;
  }
};

template <> struct DenseMapInfo<hash_code> {
  static inline hash_code getEmptyKey() { return hash_code(-1); }
  static inline hash_code getTombstoneKey() { return hash_code(-2); }
  static unsigned getHashValue(hash_code val) { return val; }
  static bool isEqual(hash_code LHS, hash_code RHS) { return LHS == RHS; }
};

} // end namespace llvm

#endif // LLVM_ADT_DENSEMAPINFO_H
