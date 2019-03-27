//===- llvm/ADT/CachedHashString.h - Prehashed string/StringRef -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines CachedHashString and CachedHashStringRef.  These are owning
// and not-owning string types that store their hash in addition to their string
// data.
//
// Unlike std::string, CachedHashString can be used in DenseSet/DenseMap
// (because, unlike std::string, CachedHashString lets us have empty and
// tombstone values).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_CACHED_HASH_STRING_H
#define LLVM_ADT_CACHED_HASH_STRING_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

/// A container which contains a StringRef plus a precomputed hash.
class CachedHashStringRef {
  const char *P;
  uint32_t Size;
  uint32_t Hash;

public:
  // Explicit because hashing a string isn't free.
  explicit CachedHashStringRef(StringRef S)
      : CachedHashStringRef(S, DenseMapInfo<StringRef>::getHashValue(S)) {}

  CachedHashStringRef(StringRef S, uint32_t Hash)
      : P(S.data()), Size(S.size()), Hash(Hash) {
    assert(S.size() <= std::numeric_limits<uint32_t>::max());
  }

  StringRef val() const { return StringRef(P, Size); }
  const char *data() const { return P; }
  uint32_t size() const { return Size; }
  uint32_t hash() const { return Hash; }
};

template <> struct DenseMapInfo<CachedHashStringRef> {
  static CachedHashStringRef getEmptyKey() {
    return CachedHashStringRef(DenseMapInfo<StringRef>::getEmptyKey(), 0);
  }
  static CachedHashStringRef getTombstoneKey() {
    return CachedHashStringRef(DenseMapInfo<StringRef>::getTombstoneKey(), 1);
  }
  static unsigned getHashValue(const CachedHashStringRef &S) {
    assert(!isEqual(S, getEmptyKey()) && "Cannot hash the empty key!");
    assert(!isEqual(S, getTombstoneKey()) && "Cannot hash the tombstone key!");
    return S.hash();
  }
  static bool isEqual(const CachedHashStringRef &LHS,
                      const CachedHashStringRef &RHS) {
    return LHS.hash() == RHS.hash() &&
           DenseMapInfo<StringRef>::isEqual(LHS.val(), RHS.val());
  }
};

/// A container which contains a string, which it owns, plus a precomputed hash.
///
/// We do not null-terminate the string.
class CachedHashString {
  friend struct DenseMapInfo<CachedHashString>;

  char *P;
  uint32_t Size;
  uint32_t Hash;

  static char *getEmptyKeyPtr() { return DenseMapInfo<char *>::getEmptyKey(); }
  static char *getTombstoneKeyPtr() {
    return DenseMapInfo<char *>::getTombstoneKey();
  }

  bool isEmptyOrTombstone() const {
    return P == getEmptyKeyPtr() || P == getTombstoneKeyPtr();
  }

  struct ConstructEmptyOrTombstoneTy {};

  CachedHashString(ConstructEmptyOrTombstoneTy, char *EmptyOrTombstonePtr)
      : P(EmptyOrTombstonePtr), Size(0), Hash(0) {
    assert(isEmptyOrTombstone());
  }

  // TODO: Use small-string optimization to avoid allocating.

public:
  explicit CachedHashString(const char *S) : CachedHashString(StringRef(S)) {}

  // Explicit because copying and hashing a string isn't free.
  explicit CachedHashString(StringRef S)
      : CachedHashString(S, DenseMapInfo<StringRef>::getHashValue(S)) {}

  CachedHashString(StringRef S, uint32_t Hash)
      : P(new char[S.size()]), Size(S.size()), Hash(Hash) {
    memcpy(P, S.data(), S.size());
  }

  // Ideally this class would not be copyable.  But SetVector requires copyable
  // keys, and we want this to be usable there.
  CachedHashString(const CachedHashString &Other)
      : Size(Other.Size), Hash(Other.Hash) {
    if (Other.isEmptyOrTombstone()) {
      P = Other.P;
    } else {
      P = new char[Size];
      memcpy(P, Other.P, Size);
    }
  }

  CachedHashString &operator=(CachedHashString Other) {
    swap(*this, Other);
    return *this;
  }

  CachedHashString(CachedHashString &&Other) noexcept
      : P(Other.P), Size(Other.Size), Hash(Other.Hash) {
    Other.P = getEmptyKeyPtr();
  }

  ~CachedHashString() {
    if (!isEmptyOrTombstone())
      delete[] P;
  }

  StringRef val() const { return StringRef(P, Size); }
  uint32_t size() const { return Size; }
  uint32_t hash() const { return Hash; }

  operator StringRef() const { return val(); }
  operator CachedHashStringRef() const {
    return CachedHashStringRef(val(), Hash);
  }

  friend void swap(CachedHashString &LHS, CachedHashString &RHS) {
    using std::swap;
    swap(LHS.P, RHS.P);
    swap(LHS.Size, RHS.Size);
    swap(LHS.Hash, RHS.Hash);
  }
};

template <> struct DenseMapInfo<CachedHashString> {
  static CachedHashString getEmptyKey() {
    return CachedHashString(CachedHashString::ConstructEmptyOrTombstoneTy(),
                            CachedHashString::getEmptyKeyPtr());
  }
  static CachedHashString getTombstoneKey() {
    return CachedHashString(CachedHashString::ConstructEmptyOrTombstoneTy(),
                            CachedHashString::getTombstoneKeyPtr());
  }
  static unsigned getHashValue(const CachedHashString &S) {
    assert(!isEqual(S, getEmptyKey()) && "Cannot hash the empty key!");
    assert(!isEqual(S, getTombstoneKey()) && "Cannot hash the tombstone key!");
    return S.hash();
  }
  static bool isEqual(const CachedHashString &LHS,
                      const CachedHashString &RHS) {
    if (LHS.hash() != RHS.hash())
      return false;
    if (LHS.P == CachedHashString::getEmptyKeyPtr())
      return RHS.P == CachedHashString::getEmptyKeyPtr();
    if (LHS.P == CachedHashString::getTombstoneKeyPtr())
      return RHS.P == CachedHashString::getTombstoneKeyPtr();

    // This is safe because if RHS.P is the empty or tombstone key, it will have
    // length 0, so we'll never dereference its pointer.
    return LHS.val() == RHS.val();
  }
};

} // namespace llvm

#endif
