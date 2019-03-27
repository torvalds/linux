//===- HashTable.h - PDB Hash Table -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_HASHTABLE_H
#define LLVM_DEBUGINFO_PDB_NATIVE_HASHTABLE_H

#include "llvm/ADT/SparseBitVector.h"
#include "llvm/ADT/iterator.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

namespace llvm {

class BinaryStreamReader;
class BinaryStreamWriter;

namespace pdb {

Error readSparseBitVector(BinaryStreamReader &Stream, SparseBitVector<> &V);
Error writeSparseBitVector(BinaryStreamWriter &Writer, SparseBitVector<> &Vec);

template <typename ValueT, typename TraitsT> class HashTable;

template <typename ValueT, typename TraitsT>
class HashTableIterator
    : public iterator_facade_base<HashTableIterator<ValueT, TraitsT>,
                                  std::forward_iterator_tag,
                                  std::pair<uint32_t, ValueT>> {
  friend HashTable<ValueT, TraitsT>;

  HashTableIterator(const HashTable<ValueT, TraitsT> &Map, uint32_t Index,
                    bool IsEnd)
      : Map(&Map), Index(Index), IsEnd(IsEnd) {}

public:
  HashTableIterator(const HashTable<ValueT, TraitsT> &Map) : Map(&Map) {
    int I = Map.Present.find_first();
    if (I == -1) {
      Index = 0;
      IsEnd = true;
    } else {
      Index = static_cast<uint32_t>(I);
      IsEnd = false;
    }
  }

  HashTableIterator &operator=(const HashTableIterator &R) {
    Map = R.Map;
    return *this;
  }
  bool operator==(const HashTableIterator &R) const {
    if (IsEnd && R.IsEnd)
      return true;
    if (IsEnd != R.IsEnd)
      return false;

    return (Map == R.Map) && (Index == R.Index);
  }
  const std::pair<uint32_t, ValueT> &operator*() const {
    assert(Map->Present.test(Index));
    return Map->Buckets[Index];
  }
  HashTableIterator &operator++() {
    while (Index < Map->Buckets.size()) {
      ++Index;
      if (Map->Present.test(Index))
        return *this;
    }

    IsEnd = true;
    return *this;
  }

private:
  bool isEnd() const { return IsEnd; }
  uint32_t index() const { return Index; }

  const HashTable<ValueT, TraitsT> *Map;
  uint32_t Index;
  bool IsEnd;
};

template <typename T> struct PdbHashTraits {};

template <> struct PdbHashTraits<uint32_t> {
  uint32_t hashLookupKey(uint32_t N) const { return N; }
  uint32_t storageKeyToLookupKey(uint32_t N) const { return N; }
  uint32_t lookupKeyToStorageKey(uint32_t N) { return N; }
};

template <typename ValueT, typename TraitsT = PdbHashTraits<ValueT>>
class HashTable {
  using iterator = HashTableIterator<ValueT, TraitsT>;
  friend iterator;

  struct Header {
    support::ulittle32_t Size;
    support::ulittle32_t Capacity;
  };

  using BucketList = std::vector<std::pair<uint32_t, ValueT>>;

public:
  HashTable() { Buckets.resize(8); }

  explicit HashTable(TraitsT Traits) : HashTable(8, std::move(Traits)) {}
  HashTable(uint32_t Capacity, TraitsT Traits) : Traits(Traits) {
    Buckets.resize(Capacity);
  }

  Error load(BinaryStreamReader &Stream) {
    const Header *H;
    if (auto EC = Stream.readObject(H))
      return EC;
    if (H->Capacity == 0)
      return make_error<RawError>(raw_error_code::corrupt_file,
                                  "Invalid Hash Table Capacity");
    if (H->Size > maxLoad(H->Capacity))
      return make_error<RawError>(raw_error_code::corrupt_file,
                                  "Invalid Hash Table Size");

    Buckets.resize(H->Capacity);

    if (auto EC = readSparseBitVector(Stream, Present))
      return EC;
    if (Present.count() != H->Size)
      return make_error<RawError>(raw_error_code::corrupt_file,
                                  "Present bit vector does not match size!");

    if (auto EC = readSparseBitVector(Stream, Deleted))
      return EC;
    if (Present.intersects(Deleted))
      return make_error<RawError>(raw_error_code::corrupt_file,
                                  "Present bit vector interesects deleted!");

    for (uint32_t P : Present) {
      if (auto EC = Stream.readInteger(Buckets[P].first))
        return EC;
      const ValueT *Value;
      if (auto EC = Stream.readObject(Value))
        return EC;
      Buckets[P].second = *Value;
    }

    return Error::success();
  }

  uint32_t calculateSerializedLength() const {
    uint32_t Size = sizeof(Header);

    constexpr int BitsPerWord = 8 * sizeof(uint32_t);

    int NumBitsP = Present.find_last() + 1;
    int NumBitsD = Deleted.find_last() + 1;

    uint32_t NumWordsP = alignTo(NumBitsP, BitsPerWord) / BitsPerWord;
    uint32_t NumWordsD = alignTo(NumBitsD, BitsPerWord) / BitsPerWord;

    // Present bit set number of words (4 bytes), followed by that many actual
    // words (4 bytes each).
    Size += sizeof(uint32_t);
    Size += NumWordsP * sizeof(uint32_t);

    // Deleted bit set number of words (4 bytes), followed by that many actual
    // words (4 bytes each).
    Size += sizeof(uint32_t);
    Size += NumWordsD * sizeof(uint32_t);

    // One (Key, ValueT) pair for each entry Present.
    Size += (sizeof(uint32_t) + sizeof(ValueT)) * size();

    return Size;
  }

  Error commit(BinaryStreamWriter &Writer) const {
    Header H;
    H.Size = size();
    H.Capacity = capacity();
    if (auto EC = Writer.writeObject(H))
      return EC;

    if (auto EC = writeSparseBitVector(Writer, Present))
      return EC;

    if (auto EC = writeSparseBitVector(Writer, Deleted))
      return EC;

    for (const auto &Entry : *this) {
      if (auto EC = Writer.writeInteger(Entry.first))
        return EC;
      if (auto EC = Writer.writeObject(Entry.second))
        return EC;
    }
    return Error::success();
  }

  void clear() {
    Buckets.resize(8);
    Present.clear();
    Deleted.clear();
  }

  bool empty() const { return size() == 0; }
  uint32_t capacity() const { return Buckets.size(); }
  uint32_t size() const { return Present.count(); }

  iterator begin() const { return iterator(*this); }
  iterator end() const { return iterator(*this, 0, true); }

  /// Find the entry whose key has the specified hash value, using the specified
  /// traits defining hash function and equality.
  template <typename Key> iterator find_as(const Key &K) const {
    uint32_t H = Traits.hashLookupKey(K) % capacity();
    uint32_t I = H;
    Optional<uint32_t> FirstUnused;
    do {
      if (isPresent(I)) {
        if (Traits.storageKeyToLookupKey(Buckets[I].first) == K)
          return iterator(*this, I, false);
      } else {
        if (!FirstUnused)
          FirstUnused = I;
        // Insertion occurs via linear probing from the slot hint, and will be
        // inserted at the first empty / deleted location.  Therefore, if we are
        // probing and find a location that is neither present nor deleted, then
        // nothing must have EVER been inserted at this location, and thus it is
        // not possible for a matching value to occur later.
        if (!isDeleted(I))
          break;
      }
      I = (I + 1) % capacity();
    } while (I != H);

    // The only way FirstUnused would not be set is if every single entry in the
    // table were Present.  But this would violate the load factor constraints
    // that we impose, so it should never happen.
    assert(FirstUnused);
    return iterator(*this, *FirstUnused, true);
  }

  /// Set the entry using a key type that the specified Traits can convert
  /// from a real key to an internal key.
  template <typename Key> bool set_as(const Key &K, ValueT V) {
    return set_as_internal(K, std::move(V), None);
  }

  template <typename Key> ValueT get(const Key &K) const {
    auto Iter = find_as(K);
    assert(Iter != end());
    return (*Iter).second;
  }

protected:
  bool isPresent(uint32_t K) const { return Present.test(K); }
  bool isDeleted(uint32_t K) const { return Deleted.test(K); }

  TraitsT Traits;
  BucketList Buckets;
  mutable SparseBitVector<> Present;
  mutable SparseBitVector<> Deleted;

private:
  /// Set the entry using a key type that the specified Traits can convert
  /// from a real key to an internal key.
  template <typename Key>
  bool set_as_internal(const Key &K, ValueT V, Optional<uint32_t> InternalKey) {
    auto Entry = find_as(K);
    if (Entry != end()) {
      assert(isPresent(Entry.index()));
      assert(Traits.storageKeyToLookupKey(Buckets[Entry.index()].first) == K);
      // We're updating, no need to do anything special.
      Buckets[Entry.index()].second = V;
      return false;
    }

    auto &B = Buckets[Entry.index()];
    assert(!isPresent(Entry.index()));
    assert(Entry.isEnd());
    B.first = InternalKey ? *InternalKey : Traits.lookupKeyToStorageKey(K);
    B.second = V;
    Present.set(Entry.index());
    Deleted.reset(Entry.index());

    grow();

    assert((find_as(K)) != end());
    return true;
  }

  static uint32_t maxLoad(uint32_t capacity) { return capacity * 2 / 3 + 1; }

  void grow() {
    uint32_t S = size();
    uint32_t MaxLoad = maxLoad(capacity());
    if (S < maxLoad(capacity()))
      return;
    assert(capacity() != UINT32_MAX && "Can't grow Hash table!");

    uint32_t NewCapacity = (capacity() <= INT32_MAX) ? MaxLoad * 2 : UINT32_MAX;

    // Growing requires rebuilding the table and re-hashing every item.  Make a
    // copy with a larger capacity, insert everything into the copy, then swap
    // it in.
    HashTable NewMap(NewCapacity, Traits);
    for (auto I : Present) {
      auto LookupKey = Traits.storageKeyToLookupKey(Buckets[I].first);
      NewMap.set_as_internal(LookupKey, Buckets[I].second, Buckets[I].first);
    }

    Buckets.swap(NewMap.Buckets);
    std::swap(Present, NewMap.Present);
    std::swap(Deleted, NewMap.Deleted);
    assert(capacity() == NewCapacity);
    assert(size() == S);
  }
};

} // end namespace pdb

} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_HASHTABLE_H
