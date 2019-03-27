//===- llvm/ADT/SparseBitVector.h - Efficient Sparse BitVector --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the SparseBitVector class.  See the doxygen comment for
// SparseBitVector for more details on the algorithm used.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SPARSEBITVECTOR_H
#define LLVM_ADT_SPARSEBITVECTOR_H

#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <climits>
#include <cstring>
#include <iterator>
#include <list>

namespace llvm {

/// SparseBitVector is an implementation of a bitvector that is sparse by only
/// storing the elements that have non-zero bits set.  In order to make this
/// fast for the most common cases, SparseBitVector is implemented as a linked
/// list of SparseBitVectorElements.  We maintain a pointer to the last
/// SparseBitVectorElement accessed (in the form of a list iterator), in order
/// to make multiple in-order test/set constant time after the first one is
/// executed.  Note that using vectors to store SparseBitVectorElement's does
/// not work out very well because it causes insertion in the middle to take
/// enormous amounts of time with a large amount of bits.  Other structures that
/// have better worst cases for insertion in the middle (various balanced trees,
/// etc) do not perform as well in practice as a linked list with this iterator
/// kept up to date.  They are also significantly more memory intensive.

template <unsigned ElementSize = 128> struct SparseBitVectorElement {
public:
  using BitWord = unsigned long;
  using size_type = unsigned;
  enum {
    BITWORD_SIZE = sizeof(BitWord) * CHAR_BIT,
    BITWORDS_PER_ELEMENT = (ElementSize + BITWORD_SIZE - 1) / BITWORD_SIZE,
    BITS_PER_ELEMENT = ElementSize
  };

private:
  // Index of Element in terms of where first bit starts.
  unsigned ElementIndex;
  BitWord Bits[BITWORDS_PER_ELEMENT];

  SparseBitVectorElement() {
    ElementIndex = ~0U;
    memset(&Bits[0], 0, sizeof (BitWord) * BITWORDS_PER_ELEMENT);
  }

public:
  explicit SparseBitVectorElement(unsigned Idx) {
    ElementIndex = Idx;
    memset(&Bits[0], 0, sizeof (BitWord) * BITWORDS_PER_ELEMENT);
  }

  // Comparison.
  bool operator==(const SparseBitVectorElement &RHS) const {
    if (ElementIndex != RHS.ElementIndex)
      return false;
    for (unsigned i = 0; i < BITWORDS_PER_ELEMENT; ++i)
      if (Bits[i] != RHS.Bits[i])
        return false;
    return true;
  }

  bool operator!=(const SparseBitVectorElement &RHS) const {
    return !(*this == RHS);
  }

  // Return the bits that make up word Idx in our element.
  BitWord word(unsigned Idx) const {
    assert(Idx < BITWORDS_PER_ELEMENT);
    return Bits[Idx];
  }

  unsigned index() const {
    return ElementIndex;
  }

  bool empty() const {
    for (unsigned i = 0; i < BITWORDS_PER_ELEMENT; ++i)
      if (Bits[i])
        return false;
    return true;
  }

  void set(unsigned Idx) {
    Bits[Idx / BITWORD_SIZE] |= 1L << (Idx % BITWORD_SIZE);
  }

  bool test_and_set(unsigned Idx) {
    bool old = test(Idx);
    if (!old) {
      set(Idx);
      return true;
    }
    return false;
  }

  void reset(unsigned Idx) {
    Bits[Idx / BITWORD_SIZE] &= ~(1L << (Idx % BITWORD_SIZE));
  }

  bool test(unsigned Idx) const {
    return Bits[Idx / BITWORD_SIZE] & (1L << (Idx % BITWORD_SIZE));
  }

  size_type count() const {
    unsigned NumBits = 0;
    for (unsigned i = 0; i < BITWORDS_PER_ELEMENT; ++i)
      NumBits += countPopulation(Bits[i]);
    return NumBits;
  }

  /// find_first - Returns the index of the first set bit.
  int find_first() const {
    for (unsigned i = 0; i < BITWORDS_PER_ELEMENT; ++i)
      if (Bits[i] != 0)
        return i * BITWORD_SIZE + countTrailingZeros(Bits[i]);
    llvm_unreachable("Illegal empty element");
  }

  /// find_last - Returns the index of the last set bit.
  int find_last() const {
    for (unsigned I = 0; I < BITWORDS_PER_ELEMENT; ++I) {
      unsigned Idx = BITWORDS_PER_ELEMENT - I - 1;
      if (Bits[Idx] != 0)
        return Idx * BITWORD_SIZE + BITWORD_SIZE -
               countLeadingZeros(Bits[Idx]) - 1;
    }
    llvm_unreachable("Illegal empty element");
  }

  /// find_next - Returns the index of the next set bit starting from the
  /// "Curr" bit. Returns -1 if the next set bit is not found.
  int find_next(unsigned Curr) const {
    if (Curr >= BITS_PER_ELEMENT)
      return -1;

    unsigned WordPos = Curr / BITWORD_SIZE;
    unsigned BitPos = Curr % BITWORD_SIZE;
    BitWord Copy = Bits[WordPos];
    assert(WordPos <= BITWORDS_PER_ELEMENT
           && "Word Position outside of element");

    // Mask off previous bits.
    Copy &= ~0UL << BitPos;

    if (Copy != 0)
      return WordPos * BITWORD_SIZE + countTrailingZeros(Copy);

    // Check subsequent words.
    for (unsigned i = WordPos+1; i < BITWORDS_PER_ELEMENT; ++i)
      if (Bits[i] != 0)
        return i * BITWORD_SIZE + countTrailingZeros(Bits[i]);
    return -1;
  }

  // Union this element with RHS and return true if this one changed.
  bool unionWith(const SparseBitVectorElement &RHS) {
    bool changed = false;
    for (unsigned i = 0; i < BITWORDS_PER_ELEMENT; ++i) {
      BitWord old = changed ? 0 : Bits[i];

      Bits[i] |= RHS.Bits[i];
      if (!changed && old != Bits[i])
        changed = true;
    }
    return changed;
  }

  // Return true if we have any bits in common with RHS
  bool intersects(const SparseBitVectorElement &RHS) const {
    for (unsigned i = 0; i < BITWORDS_PER_ELEMENT; ++i) {
      if (RHS.Bits[i] & Bits[i])
        return true;
    }
    return false;
  }

  // Intersect this Element with RHS and return true if this one changed.
  // BecameZero is set to true if this element became all-zero bits.
  bool intersectWith(const SparseBitVectorElement &RHS,
                     bool &BecameZero) {
    bool changed = false;
    bool allzero = true;

    BecameZero = false;
    for (unsigned i = 0; i < BITWORDS_PER_ELEMENT; ++i) {
      BitWord old = changed ? 0 : Bits[i];

      Bits[i] &= RHS.Bits[i];
      if (Bits[i] != 0)
        allzero = false;

      if (!changed && old != Bits[i])
        changed = true;
    }
    BecameZero = allzero;
    return changed;
  }

  // Intersect this Element with the complement of RHS and return true if this
  // one changed.  BecameZero is set to true if this element became all-zero
  // bits.
  bool intersectWithComplement(const SparseBitVectorElement &RHS,
                               bool &BecameZero) {
    bool changed = false;
    bool allzero = true;

    BecameZero = false;
    for (unsigned i = 0; i < BITWORDS_PER_ELEMENT; ++i) {
      BitWord old = changed ? 0 : Bits[i];

      Bits[i] &= ~RHS.Bits[i];
      if (Bits[i] != 0)
        allzero = false;

      if (!changed && old != Bits[i])
        changed = true;
    }
    BecameZero = allzero;
    return changed;
  }

  // Three argument version of intersectWithComplement that intersects
  // RHS1 & ~RHS2 into this element
  void intersectWithComplement(const SparseBitVectorElement &RHS1,
                               const SparseBitVectorElement &RHS2,
                               bool &BecameZero) {
    bool allzero = true;

    BecameZero = false;
    for (unsigned i = 0; i < BITWORDS_PER_ELEMENT; ++i) {
      Bits[i] = RHS1.Bits[i] & ~RHS2.Bits[i];
      if (Bits[i] != 0)
        allzero = false;
    }
    BecameZero = allzero;
  }
};

template <unsigned ElementSize = 128>
class SparseBitVector {
  using ElementList = std::list<SparseBitVectorElement<ElementSize>>;
  using ElementListIter = typename ElementList::iterator;
  using ElementListConstIter = typename ElementList::const_iterator;
  enum {
    BITWORD_SIZE = SparseBitVectorElement<ElementSize>::BITWORD_SIZE
  };

  ElementList Elements;
  // Pointer to our current Element. This has no visible effect on the external
  // state of a SparseBitVector, it's just used to improve performance in the
  // common case of testing/modifying bits with similar indices.
  mutable ElementListIter CurrElementIter;

  // This is like std::lower_bound, except we do linear searching from the
  // current position.
  ElementListIter FindLowerBoundImpl(unsigned ElementIndex) const {

    // We cache a non-const iterator so we're forced to resort to const_cast to
    // get the begin/end in the case where 'this' is const. To avoid duplication
    // of code with the only difference being whether the const cast is present
    // 'this' is always const in this particular function and we sort out the
    // difference in FindLowerBound and FindLowerBoundConst.
    ElementListIter Begin =
        const_cast<SparseBitVector<ElementSize> *>(this)->Elements.begin();
    ElementListIter End =
        const_cast<SparseBitVector<ElementSize> *>(this)->Elements.end();

    if (Elements.empty()) {
      CurrElementIter = Begin;
      return CurrElementIter;
    }

    // Make sure our current iterator is valid.
    if (CurrElementIter == End)
      --CurrElementIter;

    // Search from our current iterator, either backwards or forwards,
    // depending on what element we are looking for.
    ElementListIter ElementIter = CurrElementIter;
    if (CurrElementIter->index() == ElementIndex) {
      return ElementIter;
    } else if (CurrElementIter->index() > ElementIndex) {
      while (ElementIter != Begin
             && ElementIter->index() > ElementIndex)
        --ElementIter;
    } else {
      while (ElementIter != End &&
             ElementIter->index() < ElementIndex)
        ++ElementIter;
    }
    CurrElementIter = ElementIter;
    return ElementIter;
  }
  ElementListConstIter FindLowerBoundConst(unsigned ElementIndex) const {
    return FindLowerBoundImpl(ElementIndex);
  }
  ElementListIter FindLowerBound(unsigned ElementIndex) {
    return FindLowerBoundImpl(ElementIndex);
  }

  // Iterator to walk set bits in the bitmap.  This iterator is a lot uglier
  // than it would be, in order to be efficient.
  class SparseBitVectorIterator {
  private:
    bool AtEnd;

    const SparseBitVector<ElementSize> *BitVector = nullptr;

    // Current element inside of bitmap.
    ElementListConstIter Iter;

    // Current bit number inside of our bitmap.
    unsigned BitNumber;

    // Current word number inside of our element.
    unsigned WordNumber;

    // Current bits from the element.
    typename SparseBitVectorElement<ElementSize>::BitWord Bits;

    // Move our iterator to the first non-zero bit in the bitmap.
    void AdvanceToFirstNonZero() {
      if (AtEnd)
        return;
      if (BitVector->Elements.empty()) {
        AtEnd = true;
        return;
      }
      Iter = BitVector->Elements.begin();
      BitNumber = Iter->index() * ElementSize;
      unsigned BitPos = Iter->find_first();
      BitNumber += BitPos;
      WordNumber = (BitNumber % ElementSize) / BITWORD_SIZE;
      Bits = Iter->word(WordNumber);
      Bits >>= BitPos % BITWORD_SIZE;
    }

    // Move our iterator to the next non-zero bit.
    void AdvanceToNextNonZero() {
      if (AtEnd)
        return;

      while (Bits && !(Bits & 1)) {
        Bits >>= 1;
        BitNumber += 1;
      }

      // See if we ran out of Bits in this word.
      if (!Bits) {
        int NextSetBitNumber = Iter->find_next(BitNumber % ElementSize) ;
        // If we ran out of set bits in this element, move to next element.
        if (NextSetBitNumber == -1 || (BitNumber % ElementSize == 0)) {
          ++Iter;
          WordNumber = 0;

          // We may run out of elements in the bitmap.
          if (Iter == BitVector->Elements.end()) {
            AtEnd = true;
            return;
          }
          // Set up for next non-zero word in bitmap.
          BitNumber = Iter->index() * ElementSize;
          NextSetBitNumber = Iter->find_first();
          BitNumber += NextSetBitNumber;
          WordNumber = (BitNumber % ElementSize) / BITWORD_SIZE;
          Bits = Iter->word(WordNumber);
          Bits >>= NextSetBitNumber % BITWORD_SIZE;
        } else {
          WordNumber = (NextSetBitNumber % ElementSize) / BITWORD_SIZE;
          Bits = Iter->word(WordNumber);
          Bits >>= NextSetBitNumber % BITWORD_SIZE;
          BitNumber = Iter->index() * ElementSize;
          BitNumber += NextSetBitNumber;
        }
      }
    }

  public:
    SparseBitVectorIterator() = default;

    SparseBitVectorIterator(const SparseBitVector<ElementSize> *RHS,
                            bool end = false):BitVector(RHS) {
      Iter = BitVector->Elements.begin();
      BitNumber = 0;
      Bits = 0;
      WordNumber = ~0;
      AtEnd = end;
      AdvanceToFirstNonZero();
    }

    // Preincrement.
    inline SparseBitVectorIterator& operator++() {
      ++BitNumber;
      Bits >>= 1;
      AdvanceToNextNonZero();
      return *this;
    }

    // Postincrement.
    inline SparseBitVectorIterator operator++(int) {
      SparseBitVectorIterator tmp = *this;
      ++*this;
      return tmp;
    }

    // Return the current set bit number.
    unsigned operator*() const {
      return BitNumber;
    }

    bool operator==(const SparseBitVectorIterator &RHS) const {
      // If they are both at the end, ignore the rest of the fields.
      if (AtEnd && RHS.AtEnd)
        return true;
      // Otherwise they are the same if they have the same bit number and
      // bitmap.
      return AtEnd == RHS.AtEnd && RHS.BitNumber == BitNumber;
    }

    bool operator!=(const SparseBitVectorIterator &RHS) const {
      return !(*this == RHS);
    }
  };

public:
  using iterator = SparseBitVectorIterator;

  SparseBitVector() : Elements(), CurrElementIter(Elements.begin()) {}

  SparseBitVector(const SparseBitVector &RHS)
      : Elements(RHS.Elements), CurrElementIter(Elements.begin()) {}
  SparseBitVector(SparseBitVector &&RHS)
      : Elements(std::move(RHS.Elements)), CurrElementIter(Elements.begin()) {}

  // Clear.
  void clear() {
    Elements.clear();
  }

  // Assignment
  SparseBitVector& operator=(const SparseBitVector& RHS) {
    if (this == &RHS)
      return *this;

    Elements = RHS.Elements;
    CurrElementIter = Elements.begin();
    return *this;
  }
  SparseBitVector &operator=(SparseBitVector &&RHS) {
    Elements = std::move(RHS.Elements);
    CurrElementIter = Elements.begin();
    return *this;
  }

  // Test, Reset, and Set a bit in the bitmap.
  bool test(unsigned Idx) const {
    if (Elements.empty())
      return false;

    unsigned ElementIndex = Idx / ElementSize;
    ElementListConstIter ElementIter = FindLowerBoundConst(ElementIndex);

    // If we can't find an element that is supposed to contain this bit, there
    // is nothing more to do.
    if (ElementIter == Elements.end() ||
        ElementIter->index() != ElementIndex)
      return false;
    return ElementIter->test(Idx % ElementSize);
  }

  void reset(unsigned Idx) {
    if (Elements.empty())
      return;

    unsigned ElementIndex = Idx / ElementSize;
    ElementListIter ElementIter = FindLowerBound(ElementIndex);

    // If we can't find an element that is supposed to contain this bit, there
    // is nothing more to do.
    if (ElementIter == Elements.end() ||
        ElementIter->index() != ElementIndex)
      return;
    ElementIter->reset(Idx % ElementSize);

    // When the element is zeroed out, delete it.
    if (ElementIter->empty()) {
      ++CurrElementIter;
      Elements.erase(ElementIter);
    }
  }

  void set(unsigned Idx) {
    unsigned ElementIndex = Idx / ElementSize;
    ElementListIter ElementIter;
    if (Elements.empty()) {
      ElementIter = Elements.emplace(Elements.end(), ElementIndex);
    } else {
      ElementIter = FindLowerBound(ElementIndex);

      if (ElementIter == Elements.end() ||
          ElementIter->index() != ElementIndex) {
        // We may have hit the beginning of our SparseBitVector, in which case,
        // we may need to insert right after this element, which requires moving
        // the current iterator forward one, because insert does insert before.
        if (ElementIter != Elements.end() &&
            ElementIter->index() < ElementIndex)
          ++ElementIter;
        ElementIter = Elements.emplace(ElementIter, ElementIndex);
      }
    }
    CurrElementIter = ElementIter;

    ElementIter->set(Idx % ElementSize);
  }

  bool test_and_set(unsigned Idx) {
    bool old = test(Idx);
    if (!old) {
      set(Idx);
      return true;
    }
    return false;
  }

  bool operator!=(const SparseBitVector &RHS) const {
    return !(*this == RHS);
  }

  bool operator==(const SparseBitVector &RHS) const {
    ElementListConstIter Iter1 = Elements.begin();
    ElementListConstIter Iter2 = RHS.Elements.begin();

    for (; Iter1 != Elements.end() && Iter2 != RHS.Elements.end();
         ++Iter1, ++Iter2) {
      if (*Iter1 != *Iter2)
        return false;
    }
    return Iter1 == Elements.end() && Iter2 == RHS.Elements.end();
  }

  // Union our bitmap with the RHS and return true if we changed.
  bool operator|=(const SparseBitVector &RHS) {
    if (this == &RHS)
      return false;

    bool changed = false;
    ElementListIter Iter1 = Elements.begin();
    ElementListConstIter Iter2 = RHS.Elements.begin();

    // If RHS is empty, we are done
    if (RHS.Elements.empty())
      return false;

    while (Iter2 != RHS.Elements.end()) {
      if (Iter1 == Elements.end() || Iter1->index() > Iter2->index()) {
        Elements.insert(Iter1, *Iter2);
        ++Iter2;
        changed = true;
      } else if (Iter1->index() == Iter2->index()) {
        changed |= Iter1->unionWith(*Iter2);
        ++Iter1;
        ++Iter2;
      } else {
        ++Iter1;
      }
    }
    CurrElementIter = Elements.begin();
    return changed;
  }

  // Intersect our bitmap with the RHS and return true if ours changed.
  bool operator&=(const SparseBitVector &RHS) {
    if (this == &RHS)
      return false;

    bool changed = false;
    ElementListIter Iter1 = Elements.begin();
    ElementListConstIter Iter2 = RHS.Elements.begin();

    // Check if both bitmaps are empty.
    if (Elements.empty() && RHS.Elements.empty())
      return false;

    // Loop through, intersecting as we go, erasing elements when necessary.
    while (Iter2 != RHS.Elements.end()) {
      if (Iter1 == Elements.end()) {
        CurrElementIter = Elements.begin();
        return changed;
      }

      if (Iter1->index() > Iter2->index()) {
        ++Iter2;
      } else if (Iter1->index() == Iter2->index()) {
        bool BecameZero;
        changed |= Iter1->intersectWith(*Iter2, BecameZero);
        if (BecameZero) {
          ElementListIter IterTmp = Iter1;
          ++Iter1;
          Elements.erase(IterTmp);
        } else {
          ++Iter1;
        }
        ++Iter2;
      } else {
        ElementListIter IterTmp = Iter1;
        ++Iter1;
        Elements.erase(IterTmp);
        changed = true;
      }
    }
    if (Iter1 != Elements.end()) {
      Elements.erase(Iter1, Elements.end());
      changed = true;
    }
    CurrElementIter = Elements.begin();
    return changed;
  }

  // Intersect our bitmap with the complement of the RHS and return true
  // if ours changed.
  bool intersectWithComplement(const SparseBitVector &RHS) {
    if (this == &RHS) {
      if (!empty()) {
        clear();
        return true;
      }
      return false;
    }

    bool changed = false;
    ElementListIter Iter1 = Elements.begin();
    ElementListConstIter Iter2 = RHS.Elements.begin();

    // If either our bitmap or RHS is empty, we are done
    if (Elements.empty() || RHS.Elements.empty())
      return false;

    // Loop through, intersecting as we go, erasing elements when necessary.
    while (Iter2 != RHS.Elements.end()) {
      if (Iter1 == Elements.end()) {
        CurrElementIter = Elements.begin();
        return changed;
      }

      if (Iter1->index() > Iter2->index()) {
        ++Iter2;
      } else if (Iter1->index() == Iter2->index()) {
        bool BecameZero;
        changed |= Iter1->intersectWithComplement(*Iter2, BecameZero);
        if (BecameZero) {
          ElementListIter IterTmp = Iter1;
          ++Iter1;
          Elements.erase(IterTmp);
        } else {
          ++Iter1;
        }
        ++Iter2;
      } else {
        ++Iter1;
      }
    }
    CurrElementIter = Elements.begin();
    return changed;
  }

  bool intersectWithComplement(const SparseBitVector<ElementSize> *RHS) const {
    return intersectWithComplement(*RHS);
  }

  //  Three argument version of intersectWithComplement.
  //  Result of RHS1 & ~RHS2 is stored into this bitmap.
  void intersectWithComplement(const SparseBitVector<ElementSize> &RHS1,
                               const SparseBitVector<ElementSize> &RHS2)
  {
    if (this == &RHS1) {
      intersectWithComplement(RHS2);
      return;
    } else if (this == &RHS2) {
      SparseBitVector RHS2Copy(RHS2);
      intersectWithComplement(RHS1, RHS2Copy);
      return;
    }

    Elements.clear();
    CurrElementIter = Elements.begin();
    ElementListConstIter Iter1 = RHS1.Elements.begin();
    ElementListConstIter Iter2 = RHS2.Elements.begin();

    // If RHS1 is empty, we are done
    // If RHS2 is empty, we still have to copy RHS1
    if (RHS1.Elements.empty())
      return;

    // Loop through, intersecting as we go, erasing elements when necessary.
    while (Iter2 != RHS2.Elements.end()) {
      if (Iter1 == RHS1.Elements.end())
        return;

      if (Iter1->index() > Iter2->index()) {
        ++Iter2;
      } else if (Iter1->index() == Iter2->index()) {
        bool BecameZero = false;
        Elements.emplace_back(Iter1->index());
        Elements.back().intersectWithComplement(*Iter1, *Iter2, BecameZero);
        if (BecameZero)
          Elements.pop_back();
        ++Iter1;
        ++Iter2;
      } else {
        Elements.push_back(*Iter1++);
      }
    }

    // copy the remaining elements
    std::copy(Iter1, RHS1.Elements.end(), std::back_inserter(Elements));
  }

  void intersectWithComplement(const SparseBitVector<ElementSize> *RHS1,
                               const SparseBitVector<ElementSize> *RHS2) {
    intersectWithComplement(*RHS1, *RHS2);
  }

  bool intersects(const SparseBitVector<ElementSize> *RHS) const {
    return intersects(*RHS);
  }

  // Return true if we share any bits in common with RHS
  bool intersects(const SparseBitVector<ElementSize> &RHS) const {
    ElementListConstIter Iter1 = Elements.begin();
    ElementListConstIter Iter2 = RHS.Elements.begin();

    // Check if both bitmaps are empty.
    if (Elements.empty() && RHS.Elements.empty())
      return false;

    // Loop through, intersecting stopping when we hit bits in common.
    while (Iter2 != RHS.Elements.end()) {
      if (Iter1 == Elements.end())
        return false;

      if (Iter1->index() > Iter2->index()) {
        ++Iter2;
      } else if (Iter1->index() == Iter2->index()) {
        if (Iter1->intersects(*Iter2))
          return true;
        ++Iter1;
        ++Iter2;
      } else {
        ++Iter1;
      }
    }
    return false;
  }

  // Return true iff all bits set in this SparseBitVector are
  // also set in RHS.
  bool contains(const SparseBitVector<ElementSize> &RHS) const {
    SparseBitVector<ElementSize> Result(*this);
    Result &= RHS;
    return (Result == RHS);
  }

  // Return the first set bit in the bitmap.  Return -1 if no bits are set.
  int find_first() const {
    if (Elements.empty())
      return -1;
    const SparseBitVectorElement<ElementSize> &First = *(Elements.begin());
    return (First.index() * ElementSize) + First.find_first();
  }

  // Return the last set bit in the bitmap.  Return -1 if no bits are set.
  int find_last() const {
    if (Elements.empty())
      return -1;
    const SparseBitVectorElement<ElementSize> &Last = *(Elements.rbegin());
    return (Last.index() * ElementSize) + Last.find_last();
  }

  // Return true if the SparseBitVector is empty
  bool empty() const {
    return Elements.empty();
  }

  unsigned count() const {
    unsigned BitCount = 0;
    for (ElementListConstIter Iter = Elements.begin();
         Iter != Elements.end();
         ++Iter)
      BitCount += Iter->count();

    return BitCount;
  }

  iterator begin() const {
    return iterator(this);
  }

  iterator end() const {
    return iterator(this, true);
  }
};

// Convenience functions to allow Or and And without dereferencing in the user
// code.

template <unsigned ElementSize>
inline bool operator |=(SparseBitVector<ElementSize> &LHS,
                        const SparseBitVector<ElementSize> *RHS) {
  return LHS |= *RHS;
}

template <unsigned ElementSize>
inline bool operator |=(SparseBitVector<ElementSize> *LHS,
                        const SparseBitVector<ElementSize> &RHS) {
  return LHS->operator|=(RHS);
}

template <unsigned ElementSize>
inline bool operator &=(SparseBitVector<ElementSize> *LHS,
                        const SparseBitVector<ElementSize> &RHS) {
  return LHS->operator&=(RHS);
}

template <unsigned ElementSize>
inline bool operator &=(SparseBitVector<ElementSize> &LHS,
                        const SparseBitVector<ElementSize> *RHS) {
  return LHS &= *RHS;
}

// Convenience functions for infix union, intersection, difference operators.

template <unsigned ElementSize>
inline SparseBitVector<ElementSize>
operator|(const SparseBitVector<ElementSize> &LHS,
          const SparseBitVector<ElementSize> &RHS) {
  SparseBitVector<ElementSize> Result(LHS);
  Result |= RHS;
  return Result;
}

template <unsigned ElementSize>
inline SparseBitVector<ElementSize>
operator&(const SparseBitVector<ElementSize> &LHS,
          const SparseBitVector<ElementSize> &RHS) {
  SparseBitVector<ElementSize> Result(LHS);
  Result &= RHS;
  return Result;
}

template <unsigned ElementSize>
inline SparseBitVector<ElementSize>
operator-(const SparseBitVector<ElementSize> &LHS,
          const SparseBitVector<ElementSize> &RHS) {
  SparseBitVector<ElementSize> Result;
  Result.intersectWithComplement(LHS, RHS);
  return Result;
}

// Dump a SparseBitVector to a stream
template <unsigned ElementSize>
void dump(const SparseBitVector<ElementSize> &LHS, raw_ostream &out) {
  out << "[";

  typename SparseBitVector<ElementSize>::iterator bi = LHS.begin(),
    be = LHS.end();
  if (bi != be) {
    out << *bi;
    for (++bi; bi != be; ++bi) {
      out << " " << *bi;
    }
  }
  out << "]\n";
}

} // end namespace llvm

#endif // LLVM_ADT_SPARSEBITVECTOR_H
