//===- llvm/ADT/DenseMap.h - Dense probed hash table ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the DenseMap class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_DENSEMAP_H
#define LLVM_ADT_DENSEMAP_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/EpochTracker.h"
#include "llvm/Support/AlignOf.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/ReverseIteration.h"
#include "llvm/Support/type_traits.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <new>
#include <type_traits>
#include <utility>

namespace llvm {

namespace detail {

// We extend a pair to allow users to override the bucket type with their own
// implementation without requiring two members.
template <typename KeyT, typename ValueT>
struct DenseMapPair : public std::pair<KeyT, ValueT> {

  // FIXME: Switch to inheriting constructors when we drop support for older
  //        clang versions.
  // NOTE: This default constructor is declared with '{}' rather than
  //       '= default' to work around a separate bug in clang-3.8. This can
  //       also go when we switch to inheriting constructors.
  DenseMapPair() {}

  DenseMapPair(const KeyT &Key, const ValueT &Value)
      : std::pair<KeyT, ValueT>(Key, Value) {}

  DenseMapPair(KeyT &&Key, ValueT &&Value)
      : std::pair<KeyT, ValueT>(std::move(Key), std::move(Value)) {}

  template <typename AltKeyT, typename AltValueT>
  DenseMapPair(AltKeyT &&AltKey, AltValueT &&AltValue,
               typename std::enable_if<
                   std::is_convertible<AltKeyT, KeyT>::value &&
                   std::is_convertible<AltValueT, ValueT>::value>::type * = 0)
      : std::pair<KeyT, ValueT>(std::forward<AltKeyT>(AltKey),
                                std::forward<AltValueT>(AltValue)) {}

  template <typename AltPairT>
  DenseMapPair(AltPairT &&AltPair,
               typename std::enable_if<std::is_convertible<
                   AltPairT, std::pair<KeyT, ValueT>>::value>::type * = 0)
      : std::pair<KeyT, ValueT>(std::forward<AltPairT>(AltPair)) {}

  KeyT &getFirst() { return std::pair<KeyT, ValueT>::first; }
  const KeyT &getFirst() const { return std::pair<KeyT, ValueT>::first; }
  ValueT &getSecond() { return std::pair<KeyT, ValueT>::second; }
  const ValueT &getSecond() const { return std::pair<KeyT, ValueT>::second; }
};

} // end namespace detail

template <typename KeyT, typename ValueT,
          typename KeyInfoT = DenseMapInfo<KeyT>,
          typename Bucket = llvm::detail::DenseMapPair<KeyT, ValueT>,
          bool IsConst = false>
class DenseMapIterator;

template <typename DerivedT, typename KeyT, typename ValueT, typename KeyInfoT,
          typename BucketT>
class DenseMapBase : public DebugEpochBase {
  template <typename T>
  using const_arg_type_t = typename const_pointer_or_const_ref<T>::type;

public:
  using size_type = unsigned;
  using key_type = KeyT;
  using mapped_type = ValueT;
  using value_type = BucketT;

  using iterator = DenseMapIterator<KeyT, ValueT, KeyInfoT, BucketT>;
  using const_iterator =
      DenseMapIterator<KeyT, ValueT, KeyInfoT, BucketT, true>;

  inline iterator begin() {
    // When the map is empty, avoid the overhead of advancing/retreating past
    // empty buckets.
    if (empty())
      return end();
    if (shouldReverseIterate<KeyT>())
      return makeIterator(getBucketsEnd() - 1, getBuckets(), *this);
    return makeIterator(getBuckets(), getBucketsEnd(), *this);
  }
  inline iterator end() {
    return makeIterator(getBucketsEnd(), getBucketsEnd(), *this, true);
  }
  inline const_iterator begin() const {
    if (empty())
      return end();
    if (shouldReverseIterate<KeyT>())
      return makeConstIterator(getBucketsEnd() - 1, getBuckets(), *this);
    return makeConstIterator(getBuckets(), getBucketsEnd(), *this);
  }
  inline const_iterator end() const {
    return makeConstIterator(getBucketsEnd(), getBucketsEnd(), *this, true);
  }

  LLVM_NODISCARD bool empty() const {
    return getNumEntries() == 0;
  }
  unsigned size() const { return getNumEntries(); }

  /// Grow the densemap so that it can contain at least \p NumEntries items
  /// before resizing again.
  void reserve(size_type NumEntries) {
    auto NumBuckets = getMinBucketToReserveForEntries(NumEntries);
    incrementEpoch();
    if (NumBuckets > getNumBuckets())
      grow(NumBuckets);
  }

  void clear() {
    incrementEpoch();
    if (getNumEntries() == 0 && getNumTombstones() == 0) return;

    // If the capacity of the array is huge, and the # elements used is small,
    // shrink the array.
    if (getNumEntries() * 4 < getNumBuckets() && getNumBuckets() > 64) {
      shrink_and_clear();
      return;
    }

    const KeyT EmptyKey = getEmptyKey(), TombstoneKey = getTombstoneKey();
    if (isPodLike<KeyT>::value && isPodLike<ValueT>::value) {
      // Use a simpler loop when these are trivial types.
      for (BucketT *P = getBuckets(), *E = getBucketsEnd(); P != E; ++P)
        P->getFirst() = EmptyKey;
    } else {
      unsigned NumEntries = getNumEntries();
      for (BucketT *P = getBuckets(), *E = getBucketsEnd(); P != E; ++P) {
        if (!KeyInfoT::isEqual(P->getFirst(), EmptyKey)) {
          if (!KeyInfoT::isEqual(P->getFirst(), TombstoneKey)) {
            P->getSecond().~ValueT();
            --NumEntries;
          }
          P->getFirst() = EmptyKey;
        }
      }
      assert(NumEntries == 0 && "Node count imbalance!");
    }
    setNumEntries(0);
    setNumTombstones(0);
  }

  /// Return 1 if the specified key is in the map, 0 otherwise.
  size_type count(const_arg_type_t<KeyT> Val) const {
    const BucketT *TheBucket;
    return LookupBucketFor(Val, TheBucket) ? 1 : 0;
  }

  iterator find(const_arg_type_t<KeyT> Val) {
    BucketT *TheBucket;
    if (LookupBucketFor(Val, TheBucket))
      return makeIterator(TheBucket, getBucketsEnd(), *this, true);
    return end();
  }
  const_iterator find(const_arg_type_t<KeyT> Val) const {
    const BucketT *TheBucket;
    if (LookupBucketFor(Val, TheBucket))
      return makeConstIterator(TheBucket, getBucketsEnd(), *this, true);
    return end();
  }

  /// Alternate version of find() which allows a different, and possibly
  /// less expensive, key type.
  /// The DenseMapInfo is responsible for supplying methods
  /// getHashValue(LookupKeyT) and isEqual(LookupKeyT, KeyT) for each key
  /// type used.
  template<class LookupKeyT>
  iterator find_as(const LookupKeyT &Val) {
    BucketT *TheBucket;
    if (LookupBucketFor(Val, TheBucket))
      return makeIterator(TheBucket, getBucketsEnd(), *this, true);
    return end();
  }
  template<class LookupKeyT>
  const_iterator find_as(const LookupKeyT &Val) const {
    const BucketT *TheBucket;
    if (LookupBucketFor(Val, TheBucket))
      return makeConstIterator(TheBucket, getBucketsEnd(), *this, true);
    return end();
  }

  /// lookup - Return the entry for the specified key, or a default
  /// constructed value if no such entry exists.
  ValueT lookup(const_arg_type_t<KeyT> Val) const {
    const BucketT *TheBucket;
    if (LookupBucketFor(Val, TheBucket))
      return TheBucket->getSecond();
    return ValueT();
  }

  // Inserts key,value pair into the map if the key isn't already in the map.
  // If the key is already in the map, it returns false and doesn't update the
  // value.
  std::pair<iterator, bool> insert(const std::pair<KeyT, ValueT> &KV) {
    return try_emplace(KV.first, KV.second);
  }

  // Inserts key,value pair into the map if the key isn't already in the map.
  // If the key is already in the map, it returns false and doesn't update the
  // value.
  std::pair<iterator, bool> insert(std::pair<KeyT, ValueT> &&KV) {
    return try_emplace(std::move(KV.first), std::move(KV.second));
  }

  // Inserts key,value pair into the map if the key isn't already in the map.
  // The value is constructed in-place if the key is not in the map, otherwise
  // it is not moved.
  template <typename... Ts>
  std::pair<iterator, bool> try_emplace(KeyT &&Key, Ts &&... Args) {
    BucketT *TheBucket;
    if (LookupBucketFor(Key, TheBucket))
      return std::make_pair(
               makeIterator(TheBucket, getBucketsEnd(), *this, true),
               false); // Already in map.

    // Otherwise, insert the new element.
    TheBucket =
        InsertIntoBucket(TheBucket, std::move(Key), std::forward<Ts>(Args)...);
    return std::make_pair(
             makeIterator(TheBucket, getBucketsEnd(), *this, true),
             true);
  }

  // Inserts key,value pair into the map if the key isn't already in the map.
  // The value is constructed in-place if the key is not in the map, otherwise
  // it is not moved.
  template <typename... Ts>
  std::pair<iterator, bool> try_emplace(const KeyT &Key, Ts &&... Args) {
    BucketT *TheBucket;
    if (LookupBucketFor(Key, TheBucket))
      return std::make_pair(
               makeIterator(TheBucket, getBucketsEnd(), *this, true),
               false); // Already in map.

    // Otherwise, insert the new element.
    TheBucket = InsertIntoBucket(TheBucket, Key, std::forward<Ts>(Args)...);
    return std::make_pair(
             makeIterator(TheBucket, getBucketsEnd(), *this, true),
             true);
  }

  /// Alternate version of insert() which allows a different, and possibly
  /// less expensive, key type.
  /// The DenseMapInfo is responsible for supplying methods
  /// getHashValue(LookupKeyT) and isEqual(LookupKeyT, KeyT) for each key
  /// type used.
  template <typename LookupKeyT>
  std::pair<iterator, bool> insert_as(std::pair<KeyT, ValueT> &&KV,
                                      const LookupKeyT &Val) {
    BucketT *TheBucket;
    if (LookupBucketFor(Val, TheBucket))
      return std::make_pair(
               makeIterator(TheBucket, getBucketsEnd(), *this, true),
               false); // Already in map.

    // Otherwise, insert the new element.
    TheBucket = InsertIntoBucketWithLookup(TheBucket, std::move(KV.first),
                                           std::move(KV.second), Val);
    return std::make_pair(
             makeIterator(TheBucket, getBucketsEnd(), *this, true),
             true);
  }

  /// insert - Range insertion of pairs.
  template<typename InputIt>
  void insert(InputIt I, InputIt E) {
    for (; I != E; ++I)
      insert(*I);
  }

  bool erase(const KeyT &Val) {
    BucketT *TheBucket;
    if (!LookupBucketFor(Val, TheBucket))
      return false; // not in map.

    TheBucket->getSecond().~ValueT();
    TheBucket->getFirst() = getTombstoneKey();
    decrementNumEntries();
    incrementNumTombstones();
    return true;
  }
  void erase(iterator I) {
    BucketT *TheBucket = &*I;
    TheBucket->getSecond().~ValueT();
    TheBucket->getFirst() = getTombstoneKey();
    decrementNumEntries();
    incrementNumTombstones();
  }

  value_type& FindAndConstruct(const KeyT &Key) {
    BucketT *TheBucket;
    if (LookupBucketFor(Key, TheBucket))
      return *TheBucket;

    return *InsertIntoBucket(TheBucket, Key);
  }

  ValueT &operator[](const KeyT &Key) {
    return FindAndConstruct(Key).second;
  }

  value_type& FindAndConstruct(KeyT &&Key) {
    BucketT *TheBucket;
    if (LookupBucketFor(Key, TheBucket))
      return *TheBucket;

    return *InsertIntoBucket(TheBucket, std::move(Key));
  }

  ValueT &operator[](KeyT &&Key) {
    return FindAndConstruct(std::move(Key)).second;
  }

  /// isPointerIntoBucketsArray - Return true if the specified pointer points
  /// somewhere into the DenseMap's array of buckets (i.e. either to a key or
  /// value in the DenseMap).
  bool isPointerIntoBucketsArray(const void *Ptr) const {
    return Ptr >= getBuckets() && Ptr < getBucketsEnd();
  }

  /// getPointerIntoBucketsArray() - Return an opaque pointer into the buckets
  /// array.  In conjunction with the previous method, this can be used to
  /// determine whether an insertion caused the DenseMap to reallocate.
  const void *getPointerIntoBucketsArray() const { return getBuckets(); }

protected:
  DenseMapBase() = default;

  void destroyAll() {
    if (getNumBuckets() == 0) // Nothing to do.
      return;

    const KeyT EmptyKey = getEmptyKey(), TombstoneKey = getTombstoneKey();
    for (BucketT *P = getBuckets(), *E = getBucketsEnd(); P != E; ++P) {
      if (!KeyInfoT::isEqual(P->getFirst(), EmptyKey) &&
          !KeyInfoT::isEqual(P->getFirst(), TombstoneKey))
        P->getSecond().~ValueT();
      P->getFirst().~KeyT();
    }
  }

  void initEmpty() {
    setNumEntries(0);
    setNumTombstones(0);

    assert((getNumBuckets() & (getNumBuckets()-1)) == 0 &&
           "# initial buckets must be a power of two!");
    const KeyT EmptyKey = getEmptyKey();
    for (BucketT *B = getBuckets(), *E = getBucketsEnd(); B != E; ++B)
      ::new (&B->getFirst()) KeyT(EmptyKey);
  }

  /// Returns the number of buckets to allocate to ensure that the DenseMap can
  /// accommodate \p NumEntries without need to grow().
  unsigned getMinBucketToReserveForEntries(unsigned NumEntries) {
    // Ensure that "NumEntries * 4 < NumBuckets * 3"
    if (NumEntries == 0)
      return 0;
    // +1 is required because of the strict equality.
    // For example if NumEntries is 48, we need to return 401.
    return NextPowerOf2(NumEntries * 4 / 3 + 1);
  }

  void moveFromOldBuckets(BucketT *OldBucketsBegin, BucketT *OldBucketsEnd) {
    initEmpty();

    // Insert all the old elements.
    const KeyT EmptyKey = getEmptyKey();
    const KeyT TombstoneKey = getTombstoneKey();
    for (BucketT *B = OldBucketsBegin, *E = OldBucketsEnd; B != E; ++B) {
      if (!KeyInfoT::isEqual(B->getFirst(), EmptyKey) &&
          !KeyInfoT::isEqual(B->getFirst(), TombstoneKey)) {
        // Insert the key/value into the new table.
        BucketT *DestBucket;
        bool FoundVal = LookupBucketFor(B->getFirst(), DestBucket);
        (void)FoundVal; // silence warning.
        assert(!FoundVal && "Key already in new map?");
        DestBucket->getFirst() = std::move(B->getFirst());
        ::new (&DestBucket->getSecond()) ValueT(std::move(B->getSecond()));
        incrementNumEntries();

        // Free the value.
        B->getSecond().~ValueT();
      }
      B->getFirst().~KeyT();
    }
  }

  template <typename OtherBaseT>
  void copyFrom(
      const DenseMapBase<OtherBaseT, KeyT, ValueT, KeyInfoT, BucketT> &other) {
    assert(&other != this);
    assert(getNumBuckets() == other.getNumBuckets());

    setNumEntries(other.getNumEntries());
    setNumTombstones(other.getNumTombstones());

    if (isPodLike<KeyT>::value && isPodLike<ValueT>::value)
      memcpy(reinterpret_cast<void *>(getBuckets()), other.getBuckets(),
             getNumBuckets() * sizeof(BucketT));
    else
      for (size_t i = 0; i < getNumBuckets(); ++i) {
        ::new (&getBuckets()[i].getFirst())
            KeyT(other.getBuckets()[i].getFirst());
        if (!KeyInfoT::isEqual(getBuckets()[i].getFirst(), getEmptyKey()) &&
            !KeyInfoT::isEqual(getBuckets()[i].getFirst(), getTombstoneKey()))
          ::new (&getBuckets()[i].getSecond())
              ValueT(other.getBuckets()[i].getSecond());
      }
  }

  static unsigned getHashValue(const KeyT &Val) {
    return KeyInfoT::getHashValue(Val);
  }

  template<typename LookupKeyT>
  static unsigned getHashValue(const LookupKeyT &Val) {
    return KeyInfoT::getHashValue(Val);
  }

  static const KeyT getEmptyKey() {
    static_assert(std::is_base_of<DenseMapBase, DerivedT>::value,
                  "Must pass the derived type to this template!");
    return KeyInfoT::getEmptyKey();
  }

  static const KeyT getTombstoneKey() {
    return KeyInfoT::getTombstoneKey();
  }

private:
  iterator makeIterator(BucketT *P, BucketT *E,
                        DebugEpochBase &Epoch,
                        bool NoAdvance=false) {
    if (shouldReverseIterate<KeyT>()) {
      BucketT *B = P == getBucketsEnd() ? getBuckets() : P + 1;
      return iterator(B, E, Epoch, NoAdvance);
    }
    return iterator(P, E, Epoch, NoAdvance);
  }

  const_iterator makeConstIterator(const BucketT *P, const BucketT *E,
                                   const DebugEpochBase &Epoch,
                                   const bool NoAdvance=false) const {
    if (shouldReverseIterate<KeyT>()) {
      const BucketT *B = P == getBucketsEnd() ? getBuckets() : P + 1;
      return const_iterator(B, E, Epoch, NoAdvance);
    }
    return const_iterator(P, E, Epoch, NoAdvance);
  }

  unsigned getNumEntries() const {
    return static_cast<const DerivedT *>(this)->getNumEntries();
  }

  void setNumEntries(unsigned Num) {
    static_cast<DerivedT *>(this)->setNumEntries(Num);
  }

  void incrementNumEntries() {
    setNumEntries(getNumEntries() + 1);
  }

  void decrementNumEntries() {
    setNumEntries(getNumEntries() - 1);
  }

  unsigned getNumTombstones() const {
    return static_cast<const DerivedT *>(this)->getNumTombstones();
  }

  void setNumTombstones(unsigned Num) {
    static_cast<DerivedT *>(this)->setNumTombstones(Num);
  }

  void incrementNumTombstones() {
    setNumTombstones(getNumTombstones() + 1);
  }

  void decrementNumTombstones() {
    setNumTombstones(getNumTombstones() - 1);
  }

  const BucketT *getBuckets() const {
    return static_cast<const DerivedT *>(this)->getBuckets();
  }

  BucketT *getBuckets() {
    return static_cast<DerivedT *>(this)->getBuckets();
  }

  unsigned getNumBuckets() const {
    return static_cast<const DerivedT *>(this)->getNumBuckets();
  }

  BucketT *getBucketsEnd() {
    return getBuckets() + getNumBuckets();
  }

  const BucketT *getBucketsEnd() const {
    return getBuckets() + getNumBuckets();
  }

  void grow(unsigned AtLeast) {
    static_cast<DerivedT *>(this)->grow(AtLeast);
  }

  void shrink_and_clear() {
    static_cast<DerivedT *>(this)->shrink_and_clear();
  }

  template <typename KeyArg, typename... ValueArgs>
  BucketT *InsertIntoBucket(BucketT *TheBucket, KeyArg &&Key,
                            ValueArgs &&... Values) {
    TheBucket = InsertIntoBucketImpl(Key, Key, TheBucket);

    TheBucket->getFirst() = std::forward<KeyArg>(Key);
    ::new (&TheBucket->getSecond()) ValueT(std::forward<ValueArgs>(Values)...);
    return TheBucket;
  }

  template <typename LookupKeyT>
  BucketT *InsertIntoBucketWithLookup(BucketT *TheBucket, KeyT &&Key,
                                      ValueT &&Value, LookupKeyT &Lookup) {
    TheBucket = InsertIntoBucketImpl(Key, Lookup, TheBucket);

    TheBucket->getFirst() = std::move(Key);
    ::new (&TheBucket->getSecond()) ValueT(std::move(Value));
    return TheBucket;
  }

  template <typename LookupKeyT>
  BucketT *InsertIntoBucketImpl(const KeyT &Key, const LookupKeyT &Lookup,
                                BucketT *TheBucket) {
    incrementEpoch();

    // If the load of the hash table is more than 3/4, or if fewer than 1/8 of
    // the buckets are empty (meaning that many are filled with tombstones),
    // grow the table.
    //
    // The later case is tricky.  For example, if we had one empty bucket with
    // tons of tombstones, failing lookups (e.g. for insertion) would have to
    // probe almost the entire table until it found the empty bucket.  If the
    // table completely filled with tombstones, no lookup would ever succeed,
    // causing infinite loops in lookup.
    unsigned NewNumEntries = getNumEntries() + 1;
    unsigned NumBuckets = getNumBuckets();
    if (LLVM_UNLIKELY(NewNumEntries * 4 >= NumBuckets * 3)) {
      this->grow(NumBuckets * 2);
      LookupBucketFor(Lookup, TheBucket);
      NumBuckets = getNumBuckets();
    } else if (LLVM_UNLIKELY(NumBuckets-(NewNumEntries+getNumTombstones()) <=
                             NumBuckets/8)) {
      this->grow(NumBuckets);
      LookupBucketFor(Lookup, TheBucket);
    }
    assert(TheBucket);

    // Only update the state after we've grown our bucket space appropriately
    // so that when growing buckets we have self-consistent entry count.
    incrementNumEntries();

    // If we are writing over a tombstone, remember this.
    const KeyT EmptyKey = getEmptyKey();
    if (!KeyInfoT::isEqual(TheBucket->getFirst(), EmptyKey))
      decrementNumTombstones();

    return TheBucket;
  }

  /// LookupBucketFor - Lookup the appropriate bucket for Val, returning it in
  /// FoundBucket.  If the bucket contains the key and a value, this returns
  /// true, otherwise it returns a bucket with an empty marker or tombstone and
  /// returns false.
  template<typename LookupKeyT>
  bool LookupBucketFor(const LookupKeyT &Val,
                       const BucketT *&FoundBucket) const {
    const BucketT *BucketsPtr = getBuckets();
    const unsigned NumBuckets = getNumBuckets();

    if (NumBuckets == 0) {
      FoundBucket = nullptr;
      return false;
    }

    // FoundTombstone - Keep track of whether we find a tombstone while probing.
    const BucketT *FoundTombstone = nullptr;
    const KeyT EmptyKey = getEmptyKey();
    const KeyT TombstoneKey = getTombstoneKey();
    assert(!KeyInfoT::isEqual(Val, EmptyKey) &&
           !KeyInfoT::isEqual(Val, TombstoneKey) &&
           "Empty/Tombstone value shouldn't be inserted into map!");

    unsigned BucketNo = getHashValue(Val) & (NumBuckets-1);
    unsigned ProbeAmt = 1;
    while (true) {
      const BucketT *ThisBucket = BucketsPtr + BucketNo;
      // Found Val's bucket?  If so, return it.
      if (LLVM_LIKELY(KeyInfoT::isEqual(Val, ThisBucket->getFirst()))) {
        FoundBucket = ThisBucket;
        return true;
      }

      // If we found an empty bucket, the key doesn't exist in the set.
      // Insert it and return the default value.
      if (LLVM_LIKELY(KeyInfoT::isEqual(ThisBucket->getFirst(), EmptyKey))) {
        // If we've already seen a tombstone while probing, fill it in instead
        // of the empty bucket we eventually probed to.
        FoundBucket = FoundTombstone ? FoundTombstone : ThisBucket;
        return false;
      }

      // If this is a tombstone, remember it.  If Val ends up not in the map, we
      // prefer to return it than something that would require more probing.
      if (KeyInfoT::isEqual(ThisBucket->getFirst(), TombstoneKey) &&
          !FoundTombstone)
        FoundTombstone = ThisBucket;  // Remember the first tombstone found.

      // Otherwise, it's a hash collision or a tombstone, continue quadratic
      // probing.
      BucketNo += ProbeAmt++;
      BucketNo &= (NumBuckets-1);
    }
  }

  template <typename LookupKeyT>
  bool LookupBucketFor(const LookupKeyT &Val, BucketT *&FoundBucket) {
    const BucketT *ConstFoundBucket;
    bool Result = const_cast<const DenseMapBase *>(this)
      ->LookupBucketFor(Val, ConstFoundBucket);
    FoundBucket = const_cast<BucketT *>(ConstFoundBucket);
    return Result;
  }

public:
  /// Return the approximate size (in bytes) of the actual map.
  /// This is just the raw memory used by DenseMap.
  /// If entries are pointers to objects, the size of the referenced objects
  /// are not included.
  size_t getMemorySize() const {
    return getNumBuckets() * sizeof(BucketT);
  }
};

/// Equality comparison for DenseMap.
///
/// Iterates over elements of LHS confirming that each (key, value) pair in LHS
/// is also in RHS, and that no additional pairs are in RHS.
/// Equivalent to N calls to RHS.find and N value comparisons. Amortized
/// complexity is linear, worst case is O(N^2) (if every hash collides).
template <typename DerivedT, typename KeyT, typename ValueT, typename KeyInfoT,
          typename BucketT>
bool operator==(
    const DenseMapBase<DerivedT, KeyT, ValueT, KeyInfoT, BucketT> &LHS,
    const DenseMapBase<DerivedT, KeyT, ValueT, KeyInfoT, BucketT> &RHS) {
  if (LHS.size() != RHS.size())
    return false;

  for (auto &KV : LHS) {
    auto I = RHS.find(KV.first);
    if (I == RHS.end() || I->second != KV.second)
      return false;
  }

  return true;
}

/// Inequality comparison for DenseMap.
///
/// Equivalent to !(LHS == RHS). See operator== for performance notes.
template <typename DerivedT, typename KeyT, typename ValueT, typename KeyInfoT,
          typename BucketT>
bool operator!=(
    const DenseMapBase<DerivedT, KeyT, ValueT, KeyInfoT, BucketT> &LHS,
    const DenseMapBase<DerivedT, KeyT, ValueT, KeyInfoT, BucketT> &RHS) {
  return !(LHS == RHS);
}

template <typename KeyT, typename ValueT,
          typename KeyInfoT = DenseMapInfo<KeyT>,
          typename BucketT = llvm::detail::DenseMapPair<KeyT, ValueT>>
class DenseMap : public DenseMapBase<DenseMap<KeyT, ValueT, KeyInfoT, BucketT>,
                                     KeyT, ValueT, KeyInfoT, BucketT> {
  friend class DenseMapBase<DenseMap, KeyT, ValueT, KeyInfoT, BucketT>;

  // Lift some types from the dependent base class into this class for
  // simplicity of referring to them.
  using BaseT = DenseMapBase<DenseMap, KeyT, ValueT, KeyInfoT, BucketT>;

  BucketT *Buckets;
  unsigned NumEntries;
  unsigned NumTombstones;
  unsigned NumBuckets;

public:
  /// Create a DenseMap wth an optional \p InitialReserve that guarantee that
  /// this number of elements can be inserted in the map without grow()
  explicit DenseMap(unsigned InitialReserve = 0) { init(InitialReserve); }

  DenseMap(const DenseMap &other) : BaseT() {
    init(0);
    copyFrom(other);
  }

  DenseMap(DenseMap &&other) : BaseT() {
    init(0);
    swap(other);
  }

  template<typename InputIt>
  DenseMap(const InputIt &I, const InputIt &E) {
    init(std::distance(I, E));
    this->insert(I, E);
  }

  DenseMap(std::initializer_list<typename BaseT::value_type> Vals) {
    init(Vals.size());
    this->insert(Vals.begin(), Vals.end());
  }

  ~DenseMap() {
    this->destroyAll();
    operator delete(Buckets);
  }

  void swap(DenseMap& RHS) {
    this->incrementEpoch();
    RHS.incrementEpoch();
    std::swap(Buckets, RHS.Buckets);
    std::swap(NumEntries, RHS.NumEntries);
    std::swap(NumTombstones, RHS.NumTombstones);
    std::swap(NumBuckets, RHS.NumBuckets);
  }

  DenseMap& operator=(const DenseMap& other) {
    if (&other != this)
      copyFrom(other);
    return *this;
  }

  DenseMap& operator=(DenseMap &&other) {
    this->destroyAll();
    operator delete(Buckets);
    init(0);
    swap(other);
    return *this;
  }

  void copyFrom(const DenseMap& other) {
    this->destroyAll();
    operator delete(Buckets);
    if (allocateBuckets(other.NumBuckets)) {
      this->BaseT::copyFrom(other);
    } else {
      NumEntries = 0;
      NumTombstones = 0;
    }
  }

  void init(unsigned InitNumEntries) {
    auto InitBuckets = BaseT::getMinBucketToReserveForEntries(InitNumEntries);
    if (allocateBuckets(InitBuckets)) {
      this->BaseT::initEmpty();
    } else {
      NumEntries = 0;
      NumTombstones = 0;
    }
  }

  void grow(unsigned AtLeast) {
    unsigned OldNumBuckets = NumBuckets;
    BucketT *OldBuckets = Buckets;

    allocateBuckets(std::max<unsigned>(64, static_cast<unsigned>(NextPowerOf2(AtLeast-1))));
    assert(Buckets);
    if (!OldBuckets) {
      this->BaseT::initEmpty();
      return;
    }

    this->moveFromOldBuckets(OldBuckets, OldBuckets+OldNumBuckets);

    // Free the old table.
    operator delete(OldBuckets);
  }

  void shrink_and_clear() {
    unsigned OldNumEntries = NumEntries;
    this->destroyAll();

    // Reduce the number of buckets.
    unsigned NewNumBuckets = 0;
    if (OldNumEntries)
      NewNumBuckets = std::max(64, 1 << (Log2_32_Ceil(OldNumEntries) + 1));
    if (NewNumBuckets == NumBuckets) {
      this->BaseT::initEmpty();
      return;
    }

    operator delete(Buckets);
    init(NewNumBuckets);
  }

private:
  unsigned getNumEntries() const {
    return NumEntries;
  }

  void setNumEntries(unsigned Num) {
    NumEntries = Num;
  }

  unsigned getNumTombstones() const {
    return NumTombstones;
  }

  void setNumTombstones(unsigned Num) {
    NumTombstones = Num;
  }

  BucketT *getBuckets() const {
    return Buckets;
  }

  unsigned getNumBuckets() const {
    return NumBuckets;
  }

  bool allocateBuckets(unsigned Num) {
    NumBuckets = Num;
    if (NumBuckets == 0) {
      Buckets = nullptr;
      return false;
    }

    Buckets = static_cast<BucketT*>(operator new(sizeof(BucketT) * NumBuckets));
    return true;
  }
};

template <typename KeyT, typename ValueT, unsigned InlineBuckets = 4,
          typename KeyInfoT = DenseMapInfo<KeyT>,
          typename BucketT = llvm::detail::DenseMapPair<KeyT, ValueT>>
class SmallDenseMap
    : public DenseMapBase<
          SmallDenseMap<KeyT, ValueT, InlineBuckets, KeyInfoT, BucketT>, KeyT,
          ValueT, KeyInfoT, BucketT> {
  friend class DenseMapBase<SmallDenseMap, KeyT, ValueT, KeyInfoT, BucketT>;

  // Lift some types from the dependent base class into this class for
  // simplicity of referring to them.
  using BaseT = DenseMapBase<SmallDenseMap, KeyT, ValueT, KeyInfoT, BucketT>;

  static_assert(isPowerOf2_64(InlineBuckets),
                "InlineBuckets must be a power of 2.");

  unsigned Small : 1;
  unsigned NumEntries : 31;
  unsigned NumTombstones;

  struct LargeRep {
    BucketT *Buckets;
    unsigned NumBuckets;
  };

  /// A "union" of an inline bucket array and the struct representing
  /// a large bucket. This union will be discriminated by the 'Small' bit.
  AlignedCharArrayUnion<BucketT[InlineBuckets], LargeRep> storage;

public:
  explicit SmallDenseMap(unsigned NumInitBuckets = 0) {
    init(NumInitBuckets);
  }

  SmallDenseMap(const SmallDenseMap &other) : BaseT() {
    init(0);
    copyFrom(other);
  }

  SmallDenseMap(SmallDenseMap &&other) : BaseT() {
    init(0);
    swap(other);
  }

  template<typename InputIt>
  SmallDenseMap(const InputIt &I, const InputIt &E) {
    init(NextPowerOf2(std::distance(I, E)));
    this->insert(I, E);
  }

  ~SmallDenseMap() {
    this->destroyAll();
    deallocateBuckets();
  }

  void swap(SmallDenseMap& RHS) {
    unsigned TmpNumEntries = RHS.NumEntries;
    RHS.NumEntries = NumEntries;
    NumEntries = TmpNumEntries;
    std::swap(NumTombstones, RHS.NumTombstones);

    const KeyT EmptyKey = this->getEmptyKey();
    const KeyT TombstoneKey = this->getTombstoneKey();
    if (Small && RHS.Small) {
      // If we're swapping inline bucket arrays, we have to cope with some of
      // the tricky bits of DenseMap's storage system: the buckets are not
      // fully initialized. Thus we swap every key, but we may have
      // a one-directional move of the value.
      for (unsigned i = 0, e = InlineBuckets; i != e; ++i) {
        BucketT *LHSB = &getInlineBuckets()[i],
                *RHSB = &RHS.getInlineBuckets()[i];
        bool hasLHSValue = (!KeyInfoT::isEqual(LHSB->getFirst(), EmptyKey) &&
                            !KeyInfoT::isEqual(LHSB->getFirst(), TombstoneKey));
        bool hasRHSValue = (!KeyInfoT::isEqual(RHSB->getFirst(), EmptyKey) &&
                            !KeyInfoT::isEqual(RHSB->getFirst(), TombstoneKey));
        if (hasLHSValue && hasRHSValue) {
          // Swap together if we can...
          std::swap(*LHSB, *RHSB);
          continue;
        }
        // Swap separately and handle any assymetry.
        std::swap(LHSB->getFirst(), RHSB->getFirst());
        if (hasLHSValue) {
          ::new (&RHSB->getSecond()) ValueT(std::move(LHSB->getSecond()));
          LHSB->getSecond().~ValueT();
        } else if (hasRHSValue) {
          ::new (&LHSB->getSecond()) ValueT(std::move(RHSB->getSecond()));
          RHSB->getSecond().~ValueT();
        }
      }
      return;
    }
    if (!Small && !RHS.Small) {
      std::swap(getLargeRep()->Buckets, RHS.getLargeRep()->Buckets);
      std::swap(getLargeRep()->NumBuckets, RHS.getLargeRep()->NumBuckets);
      return;
    }

    SmallDenseMap &SmallSide = Small ? *this : RHS;
    SmallDenseMap &LargeSide = Small ? RHS : *this;

    // First stash the large side's rep and move the small side across.
    LargeRep TmpRep = std::move(*LargeSide.getLargeRep());
    LargeSide.getLargeRep()->~LargeRep();
    LargeSide.Small = true;
    // This is similar to the standard move-from-old-buckets, but the bucket
    // count hasn't actually rotated in this case. So we have to carefully
    // move construct the keys and values into their new locations, but there
    // is no need to re-hash things.
    for (unsigned i = 0, e = InlineBuckets; i != e; ++i) {
      BucketT *NewB = &LargeSide.getInlineBuckets()[i],
              *OldB = &SmallSide.getInlineBuckets()[i];
      ::new (&NewB->getFirst()) KeyT(std::move(OldB->getFirst()));
      OldB->getFirst().~KeyT();
      if (!KeyInfoT::isEqual(NewB->getFirst(), EmptyKey) &&
          !KeyInfoT::isEqual(NewB->getFirst(), TombstoneKey)) {
        ::new (&NewB->getSecond()) ValueT(std::move(OldB->getSecond()));
        OldB->getSecond().~ValueT();
      }
    }

    // The hard part of moving the small buckets across is done, just move
    // the TmpRep into its new home.
    SmallSide.Small = false;
    new (SmallSide.getLargeRep()) LargeRep(std::move(TmpRep));
  }

  SmallDenseMap& operator=(const SmallDenseMap& other) {
    if (&other != this)
      copyFrom(other);
    return *this;
  }

  SmallDenseMap& operator=(SmallDenseMap &&other) {
    this->destroyAll();
    deallocateBuckets();
    init(0);
    swap(other);
    return *this;
  }

  void copyFrom(const SmallDenseMap& other) {
    this->destroyAll();
    deallocateBuckets();
    Small = true;
    if (other.getNumBuckets() > InlineBuckets) {
      Small = false;
      new (getLargeRep()) LargeRep(allocateBuckets(other.getNumBuckets()));
    }
    this->BaseT::copyFrom(other);
  }

  void init(unsigned InitBuckets) {
    Small = true;
    if (InitBuckets > InlineBuckets) {
      Small = false;
      new (getLargeRep()) LargeRep(allocateBuckets(InitBuckets));
    }
    this->BaseT::initEmpty();
  }

  void grow(unsigned AtLeast) {
    if (AtLeast >= InlineBuckets)
      AtLeast = std::max<unsigned>(64, NextPowerOf2(AtLeast-1));

    if (Small) {
      if (AtLeast < InlineBuckets)
        return; // Nothing to do.

      // First move the inline buckets into a temporary storage.
      AlignedCharArrayUnion<BucketT[InlineBuckets]> TmpStorage;
      BucketT *TmpBegin = reinterpret_cast<BucketT *>(TmpStorage.buffer);
      BucketT *TmpEnd = TmpBegin;

      // Loop over the buckets, moving non-empty, non-tombstones into the
      // temporary storage. Have the loop move the TmpEnd forward as it goes.
      const KeyT EmptyKey = this->getEmptyKey();
      const KeyT TombstoneKey = this->getTombstoneKey();
      for (BucketT *P = getBuckets(), *E = P + InlineBuckets; P != E; ++P) {
        if (!KeyInfoT::isEqual(P->getFirst(), EmptyKey) &&
            !KeyInfoT::isEqual(P->getFirst(), TombstoneKey)) {
          assert(size_t(TmpEnd - TmpBegin) < InlineBuckets &&
                 "Too many inline buckets!");
          ::new (&TmpEnd->getFirst()) KeyT(std::move(P->getFirst()));
          ::new (&TmpEnd->getSecond()) ValueT(std::move(P->getSecond()));
          ++TmpEnd;
          P->getSecond().~ValueT();
        }
        P->getFirst().~KeyT();
      }

      // Now make this map use the large rep, and move all the entries back
      // into it.
      Small = false;
      new (getLargeRep()) LargeRep(allocateBuckets(AtLeast));
      this->moveFromOldBuckets(TmpBegin, TmpEnd);
      return;
    }

    LargeRep OldRep = std::move(*getLargeRep());
    getLargeRep()->~LargeRep();
    if (AtLeast <= InlineBuckets) {
      Small = true;
    } else {
      new (getLargeRep()) LargeRep(allocateBuckets(AtLeast));
    }

    this->moveFromOldBuckets(OldRep.Buckets, OldRep.Buckets+OldRep.NumBuckets);

    // Free the old table.
    operator delete(OldRep.Buckets);
  }

  void shrink_and_clear() {
    unsigned OldSize = this->size();
    this->destroyAll();

    // Reduce the number of buckets.
    unsigned NewNumBuckets = 0;
    if (OldSize) {
      NewNumBuckets = 1 << (Log2_32_Ceil(OldSize) + 1);
      if (NewNumBuckets > InlineBuckets && NewNumBuckets < 64u)
        NewNumBuckets = 64;
    }
    if ((Small && NewNumBuckets <= InlineBuckets) ||
        (!Small && NewNumBuckets == getLargeRep()->NumBuckets)) {
      this->BaseT::initEmpty();
      return;
    }

    deallocateBuckets();
    init(NewNumBuckets);
  }

private:
  unsigned getNumEntries() const {
    return NumEntries;
  }

  void setNumEntries(unsigned Num) {
    // NumEntries is hardcoded to be 31 bits wide.
    assert(Num < (1U << 31) && "Cannot support more than 1<<31 entries");
    NumEntries = Num;
  }

  unsigned getNumTombstones() const {
    return NumTombstones;
  }

  void setNumTombstones(unsigned Num) {
    NumTombstones = Num;
  }

  const BucketT *getInlineBuckets() const {
    assert(Small);
    // Note that this cast does not violate aliasing rules as we assert that
    // the memory's dynamic type is the small, inline bucket buffer, and the
    // 'storage.buffer' static type is 'char *'.
    return reinterpret_cast<const BucketT *>(storage.buffer);
  }

  BucketT *getInlineBuckets() {
    return const_cast<BucketT *>(
      const_cast<const SmallDenseMap *>(this)->getInlineBuckets());
  }

  const LargeRep *getLargeRep() const {
    assert(!Small);
    // Note, same rule about aliasing as with getInlineBuckets.
    return reinterpret_cast<const LargeRep *>(storage.buffer);
  }

  LargeRep *getLargeRep() {
    return const_cast<LargeRep *>(
      const_cast<const SmallDenseMap *>(this)->getLargeRep());
  }

  const BucketT *getBuckets() const {
    return Small ? getInlineBuckets() : getLargeRep()->Buckets;
  }

  BucketT *getBuckets() {
    return const_cast<BucketT *>(
      const_cast<const SmallDenseMap *>(this)->getBuckets());
  }

  unsigned getNumBuckets() const {
    return Small ? InlineBuckets : getLargeRep()->NumBuckets;
  }

  void deallocateBuckets() {
    if (Small)
      return;

    operator delete(getLargeRep()->Buckets);
    getLargeRep()->~LargeRep();
  }

  LargeRep allocateBuckets(unsigned Num) {
    assert(Num > InlineBuckets && "Must allocate more buckets than are inline");
    LargeRep Rep = {
      static_cast<BucketT*>(operator new(sizeof(BucketT) * Num)), Num
    };
    return Rep;
  }
};

template <typename KeyT, typename ValueT, typename KeyInfoT, typename Bucket,
          bool IsConst>
class DenseMapIterator : DebugEpochBase::HandleBase {
  friend class DenseMapIterator<KeyT, ValueT, KeyInfoT, Bucket, true>;
  friend class DenseMapIterator<KeyT, ValueT, KeyInfoT, Bucket, false>;

  using ConstIterator = DenseMapIterator<KeyT, ValueT, KeyInfoT, Bucket, true>;

public:
  using difference_type = ptrdiff_t;
  using value_type =
      typename std::conditional<IsConst, const Bucket, Bucket>::type;
  using pointer = value_type *;
  using reference = value_type &;
  using iterator_category = std::forward_iterator_tag;

private:
  pointer Ptr = nullptr;
  pointer End = nullptr;

public:
  DenseMapIterator() = default;

  DenseMapIterator(pointer Pos, pointer E, const DebugEpochBase &Epoch,
                   bool NoAdvance = false)
      : DebugEpochBase::HandleBase(&Epoch), Ptr(Pos), End(E) {
    assert(isHandleInSync() && "invalid construction!");

    if (NoAdvance) return;
    if (shouldReverseIterate<KeyT>()) {
      RetreatPastEmptyBuckets();
      return;
    }
    AdvancePastEmptyBuckets();
  }

  // Converting ctor from non-const iterators to const iterators. SFINAE'd out
  // for const iterator destinations so it doesn't end up as a user defined copy
  // constructor.
  template <bool IsConstSrc,
            typename = typename std::enable_if<!IsConstSrc && IsConst>::type>
  DenseMapIterator(
      const DenseMapIterator<KeyT, ValueT, KeyInfoT, Bucket, IsConstSrc> &I)
      : DebugEpochBase::HandleBase(I), Ptr(I.Ptr), End(I.End) {}

  reference operator*() const {
    assert(isHandleInSync() && "invalid iterator access!");
    if (shouldReverseIterate<KeyT>())
      return Ptr[-1];
    return *Ptr;
  }
  pointer operator->() const {
    assert(isHandleInSync() && "invalid iterator access!");
    if (shouldReverseIterate<KeyT>())
      return &(Ptr[-1]);
    return Ptr;
  }

  bool operator==(const ConstIterator &RHS) const {
    assert((!Ptr || isHandleInSync()) && "handle not in sync!");
    assert((!RHS.Ptr || RHS.isHandleInSync()) && "handle not in sync!");
    assert(getEpochAddress() == RHS.getEpochAddress() &&
           "comparing incomparable iterators!");
    return Ptr == RHS.Ptr;
  }
  bool operator!=(const ConstIterator &RHS) const {
    assert((!Ptr || isHandleInSync()) && "handle not in sync!");
    assert((!RHS.Ptr || RHS.isHandleInSync()) && "handle not in sync!");
    assert(getEpochAddress() == RHS.getEpochAddress() &&
           "comparing incomparable iterators!");
    return Ptr != RHS.Ptr;
  }

  inline DenseMapIterator& operator++() {  // Preincrement
    assert(isHandleInSync() && "invalid iterator access!");
    if (shouldReverseIterate<KeyT>()) {
      --Ptr;
      RetreatPastEmptyBuckets();
      return *this;
    }
    ++Ptr;
    AdvancePastEmptyBuckets();
    return *this;
  }
  DenseMapIterator operator++(int) {  // Postincrement
    assert(isHandleInSync() && "invalid iterator access!");
    DenseMapIterator tmp = *this; ++*this; return tmp;
  }

private:
  void AdvancePastEmptyBuckets() {
    assert(Ptr <= End);
    const KeyT Empty = KeyInfoT::getEmptyKey();
    const KeyT Tombstone = KeyInfoT::getTombstoneKey();

    while (Ptr != End && (KeyInfoT::isEqual(Ptr->getFirst(), Empty) ||
                          KeyInfoT::isEqual(Ptr->getFirst(), Tombstone)))
      ++Ptr;
  }

  void RetreatPastEmptyBuckets() {
    assert(Ptr >= End);
    const KeyT Empty = KeyInfoT::getEmptyKey();
    const KeyT Tombstone = KeyInfoT::getTombstoneKey();

    while (Ptr != End && (KeyInfoT::isEqual(Ptr[-1].getFirst(), Empty) ||
                          KeyInfoT::isEqual(Ptr[-1].getFirst(), Tombstone)))
      --Ptr;
  }
};

template <typename KeyT, typename ValueT, typename KeyInfoT>
inline size_t capacity_in_bytes(const DenseMap<KeyT, ValueT, KeyInfoT> &X) {
  return X.getMemorySize();
}

} // end namespace llvm

#endif // LLVM_ADT_DENSEMAP_H
