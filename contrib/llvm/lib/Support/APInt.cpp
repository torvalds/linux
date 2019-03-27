//===-- APInt.cpp - Implement APInt class ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a class to represent arbitrary precision integer
// constant values and provide a variety of arithmetic operations on them.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/bit.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
using namespace llvm;

#define DEBUG_TYPE "apint"

/// A utility function for allocating memory, checking for allocation failures,
/// and ensuring the contents are zeroed.
inline static uint64_t* getClearedMemory(unsigned numWords) {
  uint64_t *result = new uint64_t[numWords];
  memset(result, 0, numWords * sizeof(uint64_t));
  return result;
}

/// A utility function for allocating memory and checking for allocation
/// failure.  The content is not zeroed.
inline static uint64_t* getMemory(unsigned numWords) {
  return new uint64_t[numWords];
}

/// A utility function that converts a character to a digit.
inline static unsigned getDigit(char cdigit, uint8_t radix) {
  unsigned r;

  if (radix == 16 || radix == 36) {
    r = cdigit - '0';
    if (r <= 9)
      return r;

    r = cdigit - 'A';
    if (r <= radix - 11U)
      return r + 10;

    r = cdigit - 'a';
    if (r <= radix - 11U)
      return r + 10;

    radix = 10;
  }

  r = cdigit - '0';
  if (r < radix)
    return r;

  return -1U;
}


void APInt::initSlowCase(uint64_t val, bool isSigned) {
  U.pVal = getClearedMemory(getNumWords());
  U.pVal[0] = val;
  if (isSigned && int64_t(val) < 0)
    for (unsigned i = 1; i < getNumWords(); ++i)
      U.pVal[i] = WORDTYPE_MAX;
  clearUnusedBits();
}

void APInt::initSlowCase(const APInt& that) {
  U.pVal = getMemory(getNumWords());
  memcpy(U.pVal, that.U.pVal, getNumWords() * APINT_WORD_SIZE);
}

void APInt::initFromArray(ArrayRef<uint64_t> bigVal) {
  assert(BitWidth && "Bitwidth too small");
  assert(bigVal.data() && "Null pointer detected!");
  if (isSingleWord())
    U.VAL = bigVal[0];
  else {
    // Get memory, cleared to 0
    U.pVal = getClearedMemory(getNumWords());
    // Calculate the number of words to copy
    unsigned words = std::min<unsigned>(bigVal.size(), getNumWords());
    // Copy the words from bigVal to pVal
    memcpy(U.pVal, bigVal.data(), words * APINT_WORD_SIZE);
  }
  // Make sure unused high bits are cleared
  clearUnusedBits();
}

APInt::APInt(unsigned numBits, ArrayRef<uint64_t> bigVal)
  : BitWidth(numBits) {
  initFromArray(bigVal);
}

APInt::APInt(unsigned numBits, unsigned numWords, const uint64_t bigVal[])
  : BitWidth(numBits) {
  initFromArray(makeArrayRef(bigVal, numWords));
}

APInt::APInt(unsigned numbits, StringRef Str, uint8_t radix)
  : BitWidth(numbits) {
  assert(BitWidth && "Bitwidth too small");
  fromString(numbits, Str, radix);
}

void APInt::reallocate(unsigned NewBitWidth) {
  // If the number of words is the same we can just change the width and stop.
  if (getNumWords() == getNumWords(NewBitWidth)) {
    BitWidth = NewBitWidth;
    return;
  }

  // If we have an allocation, delete it.
  if (!isSingleWord())
    delete [] U.pVal;

  // Update BitWidth.
  BitWidth = NewBitWidth;

  // If we are supposed to have an allocation, create it.
  if (!isSingleWord())
    U.pVal = getMemory(getNumWords());
}

void APInt::AssignSlowCase(const APInt& RHS) {
  // Don't do anything for X = X
  if (this == &RHS)
    return;

  // Adjust the bit width and handle allocations as necessary.
  reallocate(RHS.getBitWidth());

  // Copy the data.
  if (isSingleWord())
    U.VAL = RHS.U.VAL;
  else
    memcpy(U.pVal, RHS.U.pVal, getNumWords() * APINT_WORD_SIZE);
}

/// This method 'profiles' an APInt for use with FoldingSet.
void APInt::Profile(FoldingSetNodeID& ID) const {
  ID.AddInteger(BitWidth);

  if (isSingleWord()) {
    ID.AddInteger(U.VAL);
    return;
  }

  unsigned NumWords = getNumWords();
  for (unsigned i = 0; i < NumWords; ++i)
    ID.AddInteger(U.pVal[i]);
}

/// Prefix increment operator. Increments the APInt by one.
APInt& APInt::operator++() {
  if (isSingleWord())
    ++U.VAL;
  else
    tcIncrement(U.pVal, getNumWords());
  return clearUnusedBits();
}

/// Prefix decrement operator. Decrements the APInt by one.
APInt& APInt::operator--() {
  if (isSingleWord())
    --U.VAL;
  else
    tcDecrement(U.pVal, getNumWords());
  return clearUnusedBits();
}

/// Adds the RHS APint to this APInt.
/// @returns this, after addition of RHS.
/// Addition assignment operator.
APInt& APInt::operator+=(const APInt& RHS) {
  assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
  if (isSingleWord())
    U.VAL += RHS.U.VAL;
  else
    tcAdd(U.pVal, RHS.U.pVal, 0, getNumWords());
  return clearUnusedBits();
}

APInt& APInt::operator+=(uint64_t RHS) {
  if (isSingleWord())
    U.VAL += RHS;
  else
    tcAddPart(U.pVal, RHS, getNumWords());
  return clearUnusedBits();
}

/// Subtracts the RHS APInt from this APInt
/// @returns this, after subtraction
/// Subtraction assignment operator.
APInt& APInt::operator-=(const APInt& RHS) {
  assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
  if (isSingleWord())
    U.VAL -= RHS.U.VAL;
  else
    tcSubtract(U.pVal, RHS.U.pVal, 0, getNumWords());
  return clearUnusedBits();
}

APInt& APInt::operator-=(uint64_t RHS) {
  if (isSingleWord())
    U.VAL -= RHS;
  else
    tcSubtractPart(U.pVal, RHS, getNumWords());
  return clearUnusedBits();
}

APInt APInt::operator*(const APInt& RHS) const {
  assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
  if (isSingleWord())
    return APInt(BitWidth, U.VAL * RHS.U.VAL);

  APInt Result(getMemory(getNumWords()), getBitWidth());

  tcMultiply(Result.U.pVal, U.pVal, RHS.U.pVal, getNumWords());

  Result.clearUnusedBits();
  return Result;
}

void APInt::AndAssignSlowCase(const APInt& RHS) {
  tcAnd(U.pVal, RHS.U.pVal, getNumWords());
}

void APInt::OrAssignSlowCase(const APInt& RHS) {
  tcOr(U.pVal, RHS.U.pVal, getNumWords());
}

void APInt::XorAssignSlowCase(const APInt& RHS) {
  tcXor(U.pVal, RHS.U.pVal, getNumWords());
}

APInt& APInt::operator*=(const APInt& RHS) {
  assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
  *this = *this * RHS;
  return *this;
}

APInt& APInt::operator*=(uint64_t RHS) {
  if (isSingleWord()) {
    U.VAL *= RHS;
  } else {
    unsigned NumWords = getNumWords();
    tcMultiplyPart(U.pVal, U.pVal, RHS, 0, NumWords, NumWords, false);
  }
  return clearUnusedBits();
}

bool APInt::EqualSlowCase(const APInt& RHS) const {
  return std::equal(U.pVal, U.pVal + getNumWords(), RHS.U.pVal);
}

int APInt::compare(const APInt& RHS) const {
  assert(BitWidth == RHS.BitWidth && "Bit widths must be same for comparison");
  if (isSingleWord())
    return U.VAL < RHS.U.VAL ? -1 : U.VAL > RHS.U.VAL;

  return tcCompare(U.pVal, RHS.U.pVal, getNumWords());
}

int APInt::compareSigned(const APInt& RHS) const {
  assert(BitWidth == RHS.BitWidth && "Bit widths must be same for comparison");
  if (isSingleWord()) {
    int64_t lhsSext = SignExtend64(U.VAL, BitWidth);
    int64_t rhsSext = SignExtend64(RHS.U.VAL, BitWidth);
    return lhsSext < rhsSext ? -1 : lhsSext > rhsSext;
  }

  bool lhsNeg = isNegative();
  bool rhsNeg = RHS.isNegative();

  // If the sign bits don't match, then (LHS < RHS) if LHS is negative
  if (lhsNeg != rhsNeg)
    return lhsNeg ? -1 : 1;

  // Otherwise we can just use an unsigned comparison, because even negative
  // numbers compare correctly this way if both have the same signed-ness.
  return tcCompare(U.pVal, RHS.U.pVal, getNumWords());
}

void APInt::setBitsSlowCase(unsigned loBit, unsigned hiBit) {
  unsigned loWord = whichWord(loBit);
  unsigned hiWord = whichWord(hiBit);

  // Create an initial mask for the low word with zeros below loBit.
  uint64_t loMask = WORDTYPE_MAX << whichBit(loBit);

  // If hiBit is not aligned, we need a high mask.
  unsigned hiShiftAmt = whichBit(hiBit);
  if (hiShiftAmt != 0) {
    // Create a high mask with zeros above hiBit.
    uint64_t hiMask = WORDTYPE_MAX >> (APINT_BITS_PER_WORD - hiShiftAmt);
    // If loWord and hiWord are equal, then we combine the masks. Otherwise,
    // set the bits in hiWord.
    if (hiWord == loWord)
      loMask &= hiMask;
    else
      U.pVal[hiWord] |= hiMask;
  }
  // Apply the mask to the low word.
  U.pVal[loWord] |= loMask;

  // Fill any words between loWord and hiWord with all ones.
  for (unsigned word = loWord + 1; word < hiWord; ++word)
    U.pVal[word] = WORDTYPE_MAX;
}

/// Toggle every bit to its opposite value.
void APInt::flipAllBitsSlowCase() {
  tcComplement(U.pVal, getNumWords());
  clearUnusedBits();
}

/// Toggle a given bit to its opposite value whose position is given
/// as "bitPosition".
/// Toggles a given bit to its opposite value.
void APInt::flipBit(unsigned bitPosition) {
  assert(bitPosition < BitWidth && "Out of the bit-width range!");
  if ((*this)[bitPosition]) clearBit(bitPosition);
  else setBit(bitPosition);
}

void APInt::insertBits(const APInt &subBits, unsigned bitPosition) {
  unsigned subBitWidth = subBits.getBitWidth();
  assert(0 < subBitWidth && (subBitWidth + bitPosition) <= BitWidth &&
         "Illegal bit insertion");

  // Insertion is a direct copy.
  if (subBitWidth == BitWidth) {
    *this = subBits;
    return;
  }

  // Single word result can be done as a direct bitmask.
  if (isSingleWord()) {
    uint64_t mask = WORDTYPE_MAX >> (APINT_BITS_PER_WORD - subBitWidth);
    U.VAL &= ~(mask << bitPosition);
    U.VAL |= (subBits.U.VAL << bitPosition);
    return;
  }

  unsigned loBit = whichBit(bitPosition);
  unsigned loWord = whichWord(bitPosition);
  unsigned hi1Word = whichWord(bitPosition + subBitWidth - 1);

  // Insertion within a single word can be done as a direct bitmask.
  if (loWord == hi1Word) {
    uint64_t mask = WORDTYPE_MAX >> (APINT_BITS_PER_WORD - subBitWidth);
    U.pVal[loWord] &= ~(mask << loBit);
    U.pVal[loWord] |= (subBits.U.VAL << loBit);
    return;
  }

  // Insert on word boundaries.
  if (loBit == 0) {
    // Direct copy whole words.
    unsigned numWholeSubWords = subBitWidth / APINT_BITS_PER_WORD;
    memcpy(U.pVal + loWord, subBits.getRawData(),
           numWholeSubWords * APINT_WORD_SIZE);

    // Mask+insert remaining bits.
    unsigned remainingBits = subBitWidth % APINT_BITS_PER_WORD;
    if (remainingBits != 0) {
      uint64_t mask = WORDTYPE_MAX >> (APINT_BITS_PER_WORD - remainingBits);
      U.pVal[hi1Word] &= ~mask;
      U.pVal[hi1Word] |= subBits.getWord(subBitWidth - 1);
    }
    return;
  }

  // General case - set/clear individual bits in dst based on src.
  // TODO - there is scope for optimization here, but at the moment this code
  // path is barely used so prefer readability over performance.
  for (unsigned i = 0; i != subBitWidth; ++i) {
    if (subBits[i])
      setBit(bitPosition + i);
    else
      clearBit(bitPosition + i);
  }
}

APInt APInt::extractBits(unsigned numBits, unsigned bitPosition) const {
  assert(numBits > 0 && "Can't extract zero bits");
  assert(bitPosition < BitWidth && (numBits + bitPosition) <= BitWidth &&
         "Illegal bit extraction");

  if (isSingleWord())
    return APInt(numBits, U.VAL >> bitPosition);

  unsigned loBit = whichBit(bitPosition);
  unsigned loWord = whichWord(bitPosition);
  unsigned hiWord = whichWord(bitPosition + numBits - 1);

  // Single word result extracting bits from a single word source.
  if (loWord == hiWord)
    return APInt(numBits, U.pVal[loWord] >> loBit);

  // Extracting bits that start on a source word boundary can be done
  // as a fast memory copy.
  if (loBit == 0)
    return APInt(numBits, makeArrayRef(U.pVal + loWord, 1 + hiWord - loWord));

  // General case - shift + copy source words directly into place.
  APInt Result(numBits, 0);
  unsigned NumSrcWords = getNumWords();
  unsigned NumDstWords = Result.getNumWords();

  uint64_t *DestPtr = Result.isSingleWord() ? &Result.U.VAL : Result.U.pVal;
  for (unsigned word = 0; word < NumDstWords; ++word) {
    uint64_t w0 = U.pVal[loWord + word];
    uint64_t w1 =
        (loWord + word + 1) < NumSrcWords ? U.pVal[loWord + word + 1] : 0;
    DestPtr[word] = (w0 >> loBit) | (w1 << (APINT_BITS_PER_WORD - loBit));
  }

  return Result.clearUnusedBits();
}

unsigned APInt::getBitsNeeded(StringRef str, uint8_t radix) {
  assert(!str.empty() && "Invalid string length");
  assert((radix == 10 || radix == 8 || radix == 16 || radix == 2 ||
          radix == 36) &&
         "Radix should be 2, 8, 10, 16, or 36!");

  size_t slen = str.size();

  // Each computation below needs to know if it's negative.
  StringRef::iterator p = str.begin();
  unsigned isNegative = *p == '-';
  if (*p == '-' || *p == '+') {
    p++;
    slen--;
    assert(slen && "String is only a sign, needs a value.");
  }

  // For radixes of power-of-two values, the bits required is accurately and
  // easily computed
  if (radix == 2)
    return slen + isNegative;
  if (radix == 8)
    return slen * 3 + isNegative;
  if (radix == 16)
    return slen * 4 + isNegative;

  // FIXME: base 36

  // This is grossly inefficient but accurate. We could probably do something
  // with a computation of roughly slen*64/20 and then adjust by the value of
  // the first few digits. But, I'm not sure how accurate that could be.

  // Compute a sufficient number of bits that is always large enough but might
  // be too large. This avoids the assertion in the constructor. This
  // calculation doesn't work appropriately for the numbers 0-9, so just use 4
  // bits in that case.
  unsigned sufficient
    = radix == 10? (slen == 1 ? 4 : slen * 64/18)
                 : (slen == 1 ? 7 : slen * 16/3);

  // Convert to the actual binary value.
  APInt tmp(sufficient, StringRef(p, slen), radix);

  // Compute how many bits are required. If the log is infinite, assume we need
  // just bit.
  unsigned log = tmp.logBase2();
  if (log == (unsigned)-1) {
    return isNegative + 1;
  } else {
    return isNegative + log + 1;
  }
}

hash_code llvm::hash_value(const APInt &Arg) {
  if (Arg.isSingleWord())
    return hash_combine(Arg.U.VAL);

  return hash_combine_range(Arg.U.pVal, Arg.U.pVal + Arg.getNumWords());
}

bool APInt::isSplat(unsigned SplatSizeInBits) const {
  assert(getBitWidth() % SplatSizeInBits == 0 &&
         "SplatSizeInBits must divide width!");
  // We can check that all parts of an integer are equal by making use of a
  // little trick: rotate and check if it's still the same value.
  return *this == rotl(SplatSizeInBits);
}

/// This function returns the high "numBits" bits of this APInt.
APInt APInt::getHiBits(unsigned numBits) const {
  return this->lshr(BitWidth - numBits);
}

/// This function returns the low "numBits" bits of this APInt.
APInt APInt::getLoBits(unsigned numBits) const {
  APInt Result(getLowBitsSet(BitWidth, numBits));
  Result &= *this;
  return Result;
}

/// Return a value containing V broadcasted over NewLen bits.
APInt APInt::getSplat(unsigned NewLen, const APInt &V) {
  assert(NewLen >= V.getBitWidth() && "Can't splat to smaller bit width!");

  APInt Val = V.zextOrSelf(NewLen);
  for (unsigned I = V.getBitWidth(); I < NewLen; I <<= 1)
    Val |= Val << I;

  return Val;
}

unsigned APInt::countLeadingZerosSlowCase() const {
  unsigned Count = 0;
  for (int i = getNumWords()-1; i >= 0; --i) {
    uint64_t V = U.pVal[i];
    if (V == 0)
      Count += APINT_BITS_PER_WORD;
    else {
      Count += llvm::countLeadingZeros(V);
      break;
    }
  }
  // Adjust for unused bits in the most significant word (they are zero).
  unsigned Mod = BitWidth % APINT_BITS_PER_WORD;
  Count -= Mod > 0 ? APINT_BITS_PER_WORD - Mod : 0;
  return Count;
}

unsigned APInt::countLeadingOnesSlowCase() const {
  unsigned highWordBits = BitWidth % APINT_BITS_PER_WORD;
  unsigned shift;
  if (!highWordBits) {
    highWordBits = APINT_BITS_PER_WORD;
    shift = 0;
  } else {
    shift = APINT_BITS_PER_WORD - highWordBits;
  }
  int i = getNumWords() - 1;
  unsigned Count = llvm::countLeadingOnes(U.pVal[i] << shift);
  if (Count == highWordBits) {
    for (i--; i >= 0; --i) {
      if (U.pVal[i] == WORDTYPE_MAX)
        Count += APINT_BITS_PER_WORD;
      else {
        Count += llvm::countLeadingOnes(U.pVal[i]);
        break;
      }
    }
  }
  return Count;
}

unsigned APInt::countTrailingZerosSlowCase() const {
  unsigned Count = 0;
  unsigned i = 0;
  for (; i < getNumWords() && U.pVal[i] == 0; ++i)
    Count += APINT_BITS_PER_WORD;
  if (i < getNumWords())
    Count += llvm::countTrailingZeros(U.pVal[i]);
  return std::min(Count, BitWidth);
}

unsigned APInt::countTrailingOnesSlowCase() const {
  unsigned Count = 0;
  unsigned i = 0;
  for (; i < getNumWords() && U.pVal[i] == WORDTYPE_MAX; ++i)
    Count += APINT_BITS_PER_WORD;
  if (i < getNumWords())
    Count += llvm::countTrailingOnes(U.pVal[i]);
  assert(Count <= BitWidth);
  return Count;
}

unsigned APInt::countPopulationSlowCase() const {
  unsigned Count = 0;
  for (unsigned i = 0; i < getNumWords(); ++i)
    Count += llvm::countPopulation(U.pVal[i]);
  return Count;
}

bool APInt::intersectsSlowCase(const APInt &RHS) const {
  for (unsigned i = 0, e = getNumWords(); i != e; ++i)
    if ((U.pVal[i] & RHS.U.pVal[i]) != 0)
      return true;

  return false;
}

bool APInt::isSubsetOfSlowCase(const APInt &RHS) const {
  for (unsigned i = 0, e = getNumWords(); i != e; ++i)
    if ((U.pVal[i] & ~RHS.U.pVal[i]) != 0)
      return false;

  return true;
}

APInt APInt::byteSwap() const {
  assert(BitWidth >= 16 && BitWidth % 16 == 0 && "Cannot byteswap!");
  if (BitWidth == 16)
    return APInt(BitWidth, ByteSwap_16(uint16_t(U.VAL)));
  if (BitWidth == 32)
    return APInt(BitWidth, ByteSwap_32(unsigned(U.VAL)));
  if (BitWidth == 48) {
    unsigned Tmp1 = unsigned(U.VAL >> 16);
    Tmp1 = ByteSwap_32(Tmp1);
    uint16_t Tmp2 = uint16_t(U.VAL);
    Tmp2 = ByteSwap_16(Tmp2);
    return APInt(BitWidth, (uint64_t(Tmp2) << 32) | Tmp1);
  }
  if (BitWidth == 64)
    return APInt(BitWidth, ByteSwap_64(U.VAL));

  APInt Result(getNumWords() * APINT_BITS_PER_WORD, 0);
  for (unsigned I = 0, N = getNumWords(); I != N; ++I)
    Result.U.pVal[I] = ByteSwap_64(U.pVal[N - I - 1]);
  if (Result.BitWidth != BitWidth) {
    Result.lshrInPlace(Result.BitWidth - BitWidth);
    Result.BitWidth = BitWidth;
  }
  return Result;
}

APInt APInt::reverseBits() const {
  switch (BitWidth) {
  case 64:
    return APInt(BitWidth, llvm::reverseBits<uint64_t>(U.VAL));
  case 32:
    return APInt(BitWidth, llvm::reverseBits<uint32_t>(U.VAL));
  case 16:
    return APInt(BitWidth, llvm::reverseBits<uint16_t>(U.VAL));
  case 8:
    return APInt(BitWidth, llvm::reverseBits<uint8_t>(U.VAL));
  default:
    break;
  }

  APInt Val(*this);
  APInt Reversed(BitWidth, 0);
  unsigned S = BitWidth;

  for (; Val != 0; Val.lshrInPlace(1)) {
    Reversed <<= 1;
    Reversed |= Val[0];
    --S;
  }

  Reversed <<= S;
  return Reversed;
}

APInt llvm::APIntOps::GreatestCommonDivisor(APInt A, APInt B) {
  // Fast-path a common case.
  if (A == B) return A;

  // Corner cases: if either operand is zero, the other is the gcd.
  if (!A) return B;
  if (!B) return A;

  // Count common powers of 2 and remove all other powers of 2.
  unsigned Pow2;
  {
    unsigned Pow2_A = A.countTrailingZeros();
    unsigned Pow2_B = B.countTrailingZeros();
    if (Pow2_A > Pow2_B) {
      A.lshrInPlace(Pow2_A - Pow2_B);
      Pow2 = Pow2_B;
    } else if (Pow2_B > Pow2_A) {
      B.lshrInPlace(Pow2_B - Pow2_A);
      Pow2 = Pow2_A;
    } else {
      Pow2 = Pow2_A;
    }
  }

  // Both operands are odd multiples of 2^Pow_2:
  //
  //   gcd(a, b) = gcd(|a - b| / 2^i, min(a, b))
  //
  // This is a modified version of Stein's algorithm, taking advantage of
  // efficient countTrailingZeros().
  while (A != B) {
    if (A.ugt(B)) {
      A -= B;
      A.lshrInPlace(A.countTrailingZeros() - Pow2);
    } else {
      B -= A;
      B.lshrInPlace(B.countTrailingZeros() - Pow2);
    }
  }

  return A;
}

APInt llvm::APIntOps::RoundDoubleToAPInt(double Double, unsigned width) {
  uint64_t I = bit_cast<uint64_t>(Double);

  // Get the sign bit from the highest order bit
  bool isNeg = I >> 63;

  // Get the 11-bit exponent and adjust for the 1023 bit bias
  int64_t exp = ((I >> 52) & 0x7ff) - 1023;

  // If the exponent is negative, the value is < 0 so just return 0.
  if (exp < 0)
    return APInt(width, 0u);

  // Extract the mantissa by clearing the top 12 bits (sign + exponent).
  uint64_t mantissa = (I & (~0ULL >> 12)) | 1ULL << 52;

  // If the exponent doesn't shift all bits out of the mantissa
  if (exp < 52)
    return isNeg ? -APInt(width, mantissa >> (52 - exp)) :
                    APInt(width, mantissa >> (52 - exp));

  // If the client didn't provide enough bits for us to shift the mantissa into
  // then the result is undefined, just return 0
  if (width <= exp - 52)
    return APInt(width, 0);

  // Otherwise, we have to shift the mantissa bits up to the right location
  APInt Tmp(width, mantissa);
  Tmp <<= (unsigned)exp - 52;
  return isNeg ? -Tmp : Tmp;
}

/// This function converts this APInt to a double.
/// The layout for double is as following (IEEE Standard 754):
///  --------------------------------------
/// |  Sign    Exponent    Fraction    Bias |
/// |-------------------------------------- |
/// |  1[63]   11[62-52]   52[51-00]   1023 |
///  --------------------------------------
double APInt::roundToDouble(bool isSigned) const {

  // Handle the simple case where the value is contained in one uint64_t.
  // It is wrong to optimize getWord(0) to VAL; there might be more than one word.
  if (isSingleWord() || getActiveBits() <= APINT_BITS_PER_WORD) {
    if (isSigned) {
      int64_t sext = SignExtend64(getWord(0), BitWidth);
      return double(sext);
    } else
      return double(getWord(0));
  }

  // Determine if the value is negative.
  bool isNeg = isSigned ? (*this)[BitWidth-1] : false;

  // Construct the absolute value if we're negative.
  APInt Tmp(isNeg ? -(*this) : (*this));

  // Figure out how many bits we're using.
  unsigned n = Tmp.getActiveBits();

  // The exponent (without bias normalization) is just the number of bits
  // we are using. Note that the sign bit is gone since we constructed the
  // absolute value.
  uint64_t exp = n;

  // Return infinity for exponent overflow
  if (exp > 1023) {
    if (!isSigned || !isNeg)
      return std::numeric_limits<double>::infinity();
    else
      return -std::numeric_limits<double>::infinity();
  }
  exp += 1023; // Increment for 1023 bias

  // Number of bits in mantissa is 52. To obtain the mantissa value, we must
  // extract the high 52 bits from the correct words in pVal.
  uint64_t mantissa;
  unsigned hiWord = whichWord(n-1);
  if (hiWord == 0) {
    mantissa = Tmp.U.pVal[0];
    if (n > 52)
      mantissa >>= n - 52; // shift down, we want the top 52 bits.
  } else {
    assert(hiWord > 0 && "huh?");
    uint64_t hibits = Tmp.U.pVal[hiWord] << (52 - n % APINT_BITS_PER_WORD);
    uint64_t lobits = Tmp.U.pVal[hiWord-1] >> (11 + n % APINT_BITS_PER_WORD);
    mantissa = hibits | lobits;
  }

  // The leading bit of mantissa is implicit, so get rid of it.
  uint64_t sign = isNeg ? (1ULL << (APINT_BITS_PER_WORD - 1)) : 0;
  uint64_t I = sign | (exp << 52) | mantissa;
  return bit_cast<double>(I);
}

// Truncate to new width.
APInt APInt::trunc(unsigned width) const {
  assert(width < BitWidth && "Invalid APInt Truncate request");
  assert(width && "Can't truncate to 0 bits");

  if (width <= APINT_BITS_PER_WORD)
    return APInt(width, getRawData()[0]);

  APInt Result(getMemory(getNumWords(width)), width);

  // Copy full words.
  unsigned i;
  for (i = 0; i != width / APINT_BITS_PER_WORD; i++)
    Result.U.pVal[i] = U.pVal[i];

  // Truncate and copy any partial word.
  unsigned bits = (0 - width) % APINT_BITS_PER_WORD;
  if (bits != 0)
    Result.U.pVal[i] = U.pVal[i] << bits >> bits;

  return Result;
}

// Sign extend to a new width.
APInt APInt::sext(unsigned Width) const {
  assert(Width > BitWidth && "Invalid APInt SignExtend request");

  if (Width <= APINT_BITS_PER_WORD)
    return APInt(Width, SignExtend64(U.VAL, BitWidth));

  APInt Result(getMemory(getNumWords(Width)), Width);

  // Copy words.
  std::memcpy(Result.U.pVal, getRawData(), getNumWords() * APINT_WORD_SIZE);

  // Sign extend the last word since there may be unused bits in the input.
  Result.U.pVal[getNumWords() - 1] =
      SignExtend64(Result.U.pVal[getNumWords() - 1],
                   ((BitWidth - 1) % APINT_BITS_PER_WORD) + 1);

  // Fill with sign bits.
  std::memset(Result.U.pVal + getNumWords(), isNegative() ? -1 : 0,
              (Result.getNumWords() - getNumWords()) * APINT_WORD_SIZE);
  Result.clearUnusedBits();
  return Result;
}

//  Zero extend to a new width.
APInt APInt::zext(unsigned width) const {
  assert(width > BitWidth && "Invalid APInt ZeroExtend request");

  if (width <= APINT_BITS_PER_WORD)
    return APInt(width, U.VAL);

  APInt Result(getMemory(getNumWords(width)), width);

  // Copy words.
  std::memcpy(Result.U.pVal, getRawData(), getNumWords() * APINT_WORD_SIZE);

  // Zero remaining words.
  std::memset(Result.U.pVal + getNumWords(), 0,
              (Result.getNumWords() - getNumWords()) * APINT_WORD_SIZE);

  return Result;
}

APInt APInt::zextOrTrunc(unsigned width) const {
  if (BitWidth < width)
    return zext(width);
  if (BitWidth > width)
    return trunc(width);
  return *this;
}

APInt APInt::sextOrTrunc(unsigned width) const {
  if (BitWidth < width)
    return sext(width);
  if (BitWidth > width)
    return trunc(width);
  return *this;
}

APInt APInt::zextOrSelf(unsigned width) const {
  if (BitWidth < width)
    return zext(width);
  return *this;
}

APInt APInt::sextOrSelf(unsigned width) const {
  if (BitWidth < width)
    return sext(width);
  return *this;
}

/// Arithmetic right-shift this APInt by shiftAmt.
/// Arithmetic right-shift function.
void APInt::ashrInPlace(const APInt &shiftAmt) {
  ashrInPlace((unsigned)shiftAmt.getLimitedValue(BitWidth));
}

/// Arithmetic right-shift this APInt by shiftAmt.
/// Arithmetic right-shift function.
void APInt::ashrSlowCase(unsigned ShiftAmt) {
  // Don't bother performing a no-op shift.
  if (!ShiftAmt)
    return;

  // Save the original sign bit for later.
  bool Negative = isNegative();

  // WordShift is the inter-part shift; BitShift is intra-part shift.
  unsigned WordShift = ShiftAmt / APINT_BITS_PER_WORD;
  unsigned BitShift = ShiftAmt % APINT_BITS_PER_WORD;

  unsigned WordsToMove = getNumWords() - WordShift;
  if (WordsToMove != 0) {
    // Sign extend the last word to fill in the unused bits.
    U.pVal[getNumWords() - 1] = SignExtend64(
        U.pVal[getNumWords() - 1], ((BitWidth - 1) % APINT_BITS_PER_WORD) + 1);

    // Fastpath for moving by whole words.
    if (BitShift == 0) {
      std::memmove(U.pVal, U.pVal + WordShift, WordsToMove * APINT_WORD_SIZE);
    } else {
      // Move the words containing significant bits.
      for (unsigned i = 0; i != WordsToMove - 1; ++i)
        U.pVal[i] = (U.pVal[i + WordShift] >> BitShift) |
                    (U.pVal[i + WordShift + 1] << (APINT_BITS_PER_WORD - BitShift));

      // Handle the last word which has no high bits to copy.
      U.pVal[WordsToMove - 1] = U.pVal[WordShift + WordsToMove - 1] >> BitShift;
      // Sign extend one more time.
      U.pVal[WordsToMove - 1] =
          SignExtend64(U.pVal[WordsToMove - 1], APINT_BITS_PER_WORD - BitShift);
    }
  }

  // Fill in the remainder based on the original sign.
  std::memset(U.pVal + WordsToMove, Negative ? -1 : 0,
              WordShift * APINT_WORD_SIZE);
  clearUnusedBits();
}

/// Logical right-shift this APInt by shiftAmt.
/// Logical right-shift function.
void APInt::lshrInPlace(const APInt &shiftAmt) {
  lshrInPlace((unsigned)shiftAmt.getLimitedValue(BitWidth));
}

/// Logical right-shift this APInt by shiftAmt.
/// Logical right-shift function.
void APInt::lshrSlowCase(unsigned ShiftAmt) {
  tcShiftRight(U.pVal, getNumWords(), ShiftAmt);
}

/// Left-shift this APInt by shiftAmt.
/// Left-shift function.
APInt &APInt::operator<<=(const APInt &shiftAmt) {
  // It's undefined behavior in C to shift by BitWidth or greater.
  *this <<= (unsigned)shiftAmt.getLimitedValue(BitWidth);
  return *this;
}

void APInt::shlSlowCase(unsigned ShiftAmt) {
  tcShiftLeft(U.pVal, getNumWords(), ShiftAmt);
  clearUnusedBits();
}

// Calculate the rotate amount modulo the bit width.
static unsigned rotateModulo(unsigned BitWidth, const APInt &rotateAmt) {
  unsigned rotBitWidth = rotateAmt.getBitWidth();
  APInt rot = rotateAmt;
  if (rotBitWidth < BitWidth) {
    // Extend the rotate APInt, so that the urem doesn't divide by 0.
    // e.g. APInt(1, 32) would give APInt(1, 0).
    rot = rotateAmt.zext(BitWidth);
  }
  rot = rot.urem(APInt(rot.getBitWidth(), BitWidth));
  return rot.getLimitedValue(BitWidth);
}

APInt APInt::rotl(const APInt &rotateAmt) const {
  return rotl(rotateModulo(BitWidth, rotateAmt));
}

APInt APInt::rotl(unsigned rotateAmt) const {
  rotateAmt %= BitWidth;
  if (rotateAmt == 0)
    return *this;
  return shl(rotateAmt) | lshr(BitWidth - rotateAmt);
}

APInt APInt::rotr(const APInt &rotateAmt) const {
  return rotr(rotateModulo(BitWidth, rotateAmt));
}

APInt APInt::rotr(unsigned rotateAmt) const {
  rotateAmt %= BitWidth;
  if (rotateAmt == 0)
    return *this;
  return lshr(rotateAmt) | shl(BitWidth - rotateAmt);
}

// Square Root - this method computes and returns the square root of "this".
// Three mechanisms are used for computation. For small values (<= 5 bits),
// a table lookup is done. This gets some performance for common cases. For
// values using less than 52 bits, the value is converted to double and then
// the libc sqrt function is called. The result is rounded and then converted
// back to a uint64_t which is then used to construct the result. Finally,
// the Babylonian method for computing square roots is used.
APInt APInt::sqrt() const {

  // Determine the magnitude of the value.
  unsigned magnitude = getActiveBits();

  // Use a fast table for some small values. This also gets rid of some
  // rounding errors in libc sqrt for small values.
  if (magnitude <= 5) {
    static const uint8_t results[32] = {
      /*     0 */ 0,
      /*  1- 2 */ 1, 1,
      /*  3- 6 */ 2, 2, 2, 2,
      /*  7-12 */ 3, 3, 3, 3, 3, 3,
      /* 13-20 */ 4, 4, 4, 4, 4, 4, 4, 4,
      /* 21-30 */ 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
      /*    31 */ 6
    };
    return APInt(BitWidth, results[ (isSingleWord() ? U.VAL : U.pVal[0]) ]);
  }

  // If the magnitude of the value fits in less than 52 bits (the precision of
  // an IEEE double precision floating point value), then we can use the
  // libc sqrt function which will probably use a hardware sqrt computation.
  // This should be faster than the algorithm below.
  if (magnitude < 52) {
    return APInt(BitWidth,
                 uint64_t(::round(::sqrt(double(isSingleWord() ? U.VAL
                                                               : U.pVal[0])))));
  }

  // Okay, all the short cuts are exhausted. We must compute it. The following
  // is a classical Babylonian method for computing the square root. This code
  // was adapted to APInt from a wikipedia article on such computations.
  // See http://www.wikipedia.org/ and go to the page named
  // Calculate_an_integer_square_root.
  unsigned nbits = BitWidth, i = 4;
  APInt testy(BitWidth, 16);
  APInt x_old(BitWidth, 1);
  APInt x_new(BitWidth, 0);
  APInt two(BitWidth, 2);

  // Select a good starting value using binary logarithms.
  for (;; i += 2, testy = testy.shl(2))
    if (i >= nbits || this->ule(testy)) {
      x_old = x_old.shl(i / 2);
      break;
    }

  // Use the Babylonian method to arrive at the integer square root:
  for (;;) {
    x_new = (this->udiv(x_old) + x_old).udiv(two);
    if (x_old.ule(x_new))
      break;
    x_old = x_new;
  }

  // Make sure we return the closest approximation
  // NOTE: The rounding calculation below is correct. It will produce an
  // off-by-one discrepancy with results from pari/gp. That discrepancy has been
  // determined to be a rounding issue with pari/gp as it begins to use a
  // floating point representation after 192 bits. There are no discrepancies
  // between this algorithm and pari/gp for bit widths < 192 bits.
  APInt square(x_old * x_old);
  APInt nextSquare((x_old + 1) * (x_old +1));
  if (this->ult(square))
    return x_old;
  assert(this->ule(nextSquare) && "Error in APInt::sqrt computation");
  APInt midpoint((nextSquare - square).udiv(two));
  APInt offset(*this - square);
  if (offset.ult(midpoint))
    return x_old;
  return x_old + 1;
}

/// Computes the multiplicative inverse of this APInt for a given modulo. The
/// iterative extended Euclidean algorithm is used to solve for this value,
/// however we simplify it to speed up calculating only the inverse, and take
/// advantage of div+rem calculations. We also use some tricks to avoid copying
/// (potentially large) APInts around.
APInt APInt::multiplicativeInverse(const APInt& modulo) const {
  assert(ult(modulo) && "This APInt must be smaller than the modulo");

  // Using the properties listed at the following web page (accessed 06/21/08):
  //   http://www.numbertheory.org/php/euclid.html
  // (especially the properties numbered 3, 4 and 9) it can be proved that
  // BitWidth bits suffice for all the computations in the algorithm implemented
  // below. More precisely, this number of bits suffice if the multiplicative
  // inverse exists, but may not suffice for the general extended Euclidean
  // algorithm.

  APInt r[2] = { modulo, *this };
  APInt t[2] = { APInt(BitWidth, 0), APInt(BitWidth, 1) };
  APInt q(BitWidth, 0);

  unsigned i;
  for (i = 0; r[i^1] != 0; i ^= 1) {
    // An overview of the math without the confusing bit-flipping:
    // q = r[i-2] / r[i-1]
    // r[i] = r[i-2] % r[i-1]
    // t[i] = t[i-2] - t[i-1] * q
    udivrem(r[i], r[i^1], q, r[i]);
    t[i] -= t[i^1] * q;
  }

  // If this APInt and the modulo are not coprime, there is no multiplicative
  // inverse, so return 0. We check this by looking at the next-to-last
  // remainder, which is the gcd(*this,modulo) as calculated by the Euclidean
  // algorithm.
  if (r[i] != 1)
    return APInt(BitWidth, 0);

  // The next-to-last t is the multiplicative inverse.  However, we are
  // interested in a positive inverse. Calculate a positive one from a negative
  // one if necessary. A simple addition of the modulo suffices because
  // abs(t[i]) is known to be less than *this/2 (see the link above).
  if (t[i].isNegative())
    t[i] += modulo;

  return std::move(t[i]);
}

/// Calculate the magic numbers required to implement a signed integer division
/// by a constant as a sequence of multiplies, adds and shifts.  Requires that
/// the divisor not be 0, 1, or -1.  Taken from "Hacker's Delight", Henry S.
/// Warren, Jr., chapter 10.
APInt::ms APInt::magic() const {
  const APInt& d = *this;
  unsigned p;
  APInt ad, anc, delta, q1, r1, q2, r2, t;
  APInt signedMin = APInt::getSignedMinValue(d.getBitWidth());
  struct ms mag;

  ad = d.abs();
  t = signedMin + (d.lshr(d.getBitWidth() - 1));
  anc = t - 1 - t.urem(ad);   // absolute value of nc
  p = d.getBitWidth() - 1;    // initialize p
  q1 = signedMin.udiv(anc);   // initialize q1 = 2p/abs(nc)
  r1 = signedMin - q1*anc;    // initialize r1 = rem(2p,abs(nc))
  q2 = signedMin.udiv(ad);    // initialize q2 = 2p/abs(d)
  r2 = signedMin - q2*ad;     // initialize r2 = rem(2p,abs(d))
  do {
    p = p + 1;
    q1 = q1<<1;          // update q1 = 2p/abs(nc)
    r1 = r1<<1;          // update r1 = rem(2p/abs(nc))
    if (r1.uge(anc)) {  // must be unsigned comparison
      q1 = q1 + 1;
      r1 = r1 - anc;
    }
    q2 = q2<<1;          // update q2 = 2p/abs(d)
    r2 = r2<<1;          // update r2 = rem(2p/abs(d))
    if (r2.uge(ad)) {   // must be unsigned comparison
      q2 = q2 + 1;
      r2 = r2 - ad;
    }
    delta = ad - r2;
  } while (q1.ult(delta) || (q1 == delta && r1 == 0));

  mag.m = q2 + 1;
  if (d.isNegative()) mag.m = -mag.m;   // resulting magic number
  mag.s = p - d.getBitWidth();          // resulting shift
  return mag;
}

/// Calculate the magic numbers required to implement an unsigned integer
/// division by a constant as a sequence of multiplies, adds and shifts.
/// Requires that the divisor not be 0.  Taken from "Hacker's Delight", Henry
/// S. Warren, Jr., chapter 10.
/// LeadingZeros can be used to simplify the calculation if the upper bits
/// of the divided value are known zero.
APInt::mu APInt::magicu(unsigned LeadingZeros) const {
  const APInt& d = *this;
  unsigned p;
  APInt nc, delta, q1, r1, q2, r2;
  struct mu magu;
  magu.a = 0;               // initialize "add" indicator
  APInt allOnes = APInt::getAllOnesValue(d.getBitWidth()).lshr(LeadingZeros);
  APInt signedMin = APInt::getSignedMinValue(d.getBitWidth());
  APInt signedMax = APInt::getSignedMaxValue(d.getBitWidth());

  nc = allOnes - (allOnes - d).urem(d);
  p = d.getBitWidth() - 1;  // initialize p
  q1 = signedMin.udiv(nc);  // initialize q1 = 2p/nc
  r1 = signedMin - q1*nc;   // initialize r1 = rem(2p,nc)
  q2 = signedMax.udiv(d);   // initialize q2 = (2p-1)/d
  r2 = signedMax - q2*d;    // initialize r2 = rem((2p-1),d)
  do {
    p = p + 1;
    if (r1.uge(nc - r1)) {
      q1 = q1 + q1 + 1;  // update q1
      r1 = r1 + r1 - nc; // update r1
    }
    else {
      q1 = q1+q1; // update q1
      r1 = r1+r1; // update r1
    }
    if ((r2 + 1).uge(d - r2)) {
      if (q2.uge(signedMax)) magu.a = 1;
      q2 = q2+q2 + 1;     // update q2
      r2 = r2+r2 + 1 - d; // update r2
    }
    else {
      if (q2.uge(signedMin)) magu.a = 1;
      q2 = q2+q2;     // update q2
      r2 = r2+r2 + 1; // update r2
    }
    delta = d - 1 - r2;
  } while (p < d.getBitWidth()*2 &&
           (q1.ult(delta) || (q1 == delta && r1 == 0)));
  magu.m = q2 + 1; // resulting magic number
  magu.s = p - d.getBitWidth();  // resulting shift
  return magu;
}

/// Implementation of Knuth's Algorithm D (Division of nonnegative integers)
/// from "Art of Computer Programming, Volume 2", section 4.3.1, p. 272. The
/// variables here have the same names as in the algorithm. Comments explain
/// the algorithm and any deviation from it.
static void KnuthDiv(uint32_t *u, uint32_t *v, uint32_t *q, uint32_t* r,
                     unsigned m, unsigned n) {
  assert(u && "Must provide dividend");
  assert(v && "Must provide divisor");
  assert(q && "Must provide quotient");
  assert(u != v && u != q && v != q && "Must use different memory");
  assert(n>1 && "n must be > 1");

  // b denotes the base of the number system. In our case b is 2^32.
  const uint64_t b = uint64_t(1) << 32;

// The DEBUG macros here tend to be spam in the debug output if you're not
// debugging this code. Disable them unless KNUTH_DEBUG is defined.
#ifdef KNUTH_DEBUG
#define DEBUG_KNUTH(X) LLVM_DEBUG(X)
#else
#define DEBUG_KNUTH(X) do {} while(false)
#endif

  DEBUG_KNUTH(dbgs() << "KnuthDiv: m=" << m << " n=" << n << '\n');
  DEBUG_KNUTH(dbgs() << "KnuthDiv: original:");
  DEBUG_KNUTH(for (int i = m + n; i >= 0; i--) dbgs() << " " << u[i]);
  DEBUG_KNUTH(dbgs() << " by");
  DEBUG_KNUTH(for (int i = n; i > 0; i--) dbgs() << " " << v[i - 1]);
  DEBUG_KNUTH(dbgs() << '\n');
  // D1. [Normalize.] Set d = b / (v[n-1] + 1) and multiply all the digits of
  // u and v by d. Note that we have taken Knuth's advice here to use a power
  // of 2 value for d such that d * v[n-1] >= b/2 (b is the base). A power of
  // 2 allows us to shift instead of multiply and it is easy to determine the
  // shift amount from the leading zeros.  We are basically normalizing the u
  // and v so that its high bits are shifted to the top of v's range without
  // overflow. Note that this can require an extra word in u so that u must
  // be of length m+n+1.
  unsigned shift = countLeadingZeros(v[n-1]);
  uint32_t v_carry = 0;
  uint32_t u_carry = 0;
  if (shift) {
    for (unsigned i = 0; i < m+n; ++i) {
      uint32_t u_tmp = u[i] >> (32 - shift);
      u[i] = (u[i] << shift) | u_carry;
      u_carry = u_tmp;
    }
    for (unsigned i = 0; i < n; ++i) {
      uint32_t v_tmp = v[i] >> (32 - shift);
      v[i] = (v[i] << shift) | v_carry;
      v_carry = v_tmp;
    }
  }
  u[m+n] = u_carry;

  DEBUG_KNUTH(dbgs() << "KnuthDiv:   normal:");
  DEBUG_KNUTH(for (int i = m + n; i >= 0; i--) dbgs() << " " << u[i]);
  DEBUG_KNUTH(dbgs() << " by");
  DEBUG_KNUTH(for (int i = n; i > 0; i--) dbgs() << " " << v[i - 1]);
  DEBUG_KNUTH(dbgs() << '\n');

  // D2. [Initialize j.]  Set j to m. This is the loop counter over the places.
  int j = m;
  do {
    DEBUG_KNUTH(dbgs() << "KnuthDiv: quotient digit #" << j << '\n');
    // D3. [Calculate q'.].
    //     Set qp = (u[j+n]*b + u[j+n-1]) / v[n-1]. (qp=qprime=q')
    //     Set rp = (u[j+n]*b + u[j+n-1]) % v[n-1]. (rp=rprime=r')
    // Now test if qp == b or qp*v[n-2] > b*rp + u[j+n-2]; if so, decrease
    // qp by 1, increase rp by v[n-1], and repeat this test if rp < b. The test
    // on v[n-2] determines at high speed most of the cases in which the trial
    // value qp is one too large, and it eliminates all cases where qp is two
    // too large.
    uint64_t dividend = Make_64(u[j+n], u[j+n-1]);
    DEBUG_KNUTH(dbgs() << "KnuthDiv: dividend == " << dividend << '\n');
    uint64_t qp = dividend / v[n-1];
    uint64_t rp = dividend % v[n-1];
    if (qp == b || qp*v[n-2] > b*rp + u[j+n-2]) {
      qp--;
      rp += v[n-1];
      if (rp < b && (qp == b || qp*v[n-2] > b*rp + u[j+n-2]))
        qp--;
    }
    DEBUG_KNUTH(dbgs() << "KnuthDiv: qp == " << qp << ", rp == " << rp << '\n');

    // D4. [Multiply and subtract.] Replace (u[j+n]u[j+n-1]...u[j]) with
    // (u[j+n]u[j+n-1]..u[j]) - qp * (v[n-1]...v[1]v[0]). This computation
    // consists of a simple multiplication by a one-place number, combined with
    // a subtraction.
    // The digits (u[j+n]...u[j]) should be kept positive; if the result of
    // this step is actually negative, (u[j+n]...u[j]) should be left as the
    // true value plus b**(n+1), namely as the b's complement of
    // the true value, and a "borrow" to the left should be remembered.
    int64_t borrow = 0;
    for (unsigned i = 0; i < n; ++i) {
      uint64_t p = uint64_t(qp) * uint64_t(v[i]);
      int64_t subres = int64_t(u[j+i]) - borrow - Lo_32(p);
      u[j+i] = Lo_32(subres);
      borrow = Hi_32(p) - Hi_32(subres);
      DEBUG_KNUTH(dbgs() << "KnuthDiv: u[j+i] = " << u[j + i]
                        << ", borrow = " << borrow << '\n');
    }
    bool isNeg = u[j+n] < borrow;
    u[j+n] -= Lo_32(borrow);

    DEBUG_KNUTH(dbgs() << "KnuthDiv: after subtraction:");
    DEBUG_KNUTH(for (int i = m + n; i >= 0; i--) dbgs() << " " << u[i]);
    DEBUG_KNUTH(dbgs() << '\n');

    // D5. [Test remainder.] Set q[j] = qp. If the result of step D4 was
    // negative, go to step D6; otherwise go on to step D7.
    q[j] = Lo_32(qp);
    if (isNeg) {
      // D6. [Add back]. The probability that this step is necessary is very
      // small, on the order of only 2/b. Make sure that test data accounts for
      // this possibility. Decrease q[j] by 1
      q[j]--;
      // and add (0v[n-1]...v[1]v[0]) to (u[j+n]u[j+n-1]...u[j+1]u[j]).
      // A carry will occur to the left of u[j+n], and it should be ignored
      // since it cancels with the borrow that occurred in D4.
      bool carry = false;
      for (unsigned i = 0; i < n; i++) {
        uint32_t limit = std::min(u[j+i],v[i]);
        u[j+i] += v[i] + carry;
        carry = u[j+i] < limit || (carry && u[j+i] == limit);
      }
      u[j+n] += carry;
    }
    DEBUG_KNUTH(dbgs() << "KnuthDiv: after correction:");
    DEBUG_KNUTH(for (int i = m + n; i >= 0; i--) dbgs() << " " << u[i]);
    DEBUG_KNUTH(dbgs() << "\nKnuthDiv: digit result = " << q[j] << '\n');

    // D7. [Loop on j.]  Decrease j by one. Now if j >= 0, go back to D3.
  } while (--j >= 0);

  DEBUG_KNUTH(dbgs() << "KnuthDiv: quotient:");
  DEBUG_KNUTH(for (int i = m; i >= 0; i--) dbgs() << " " << q[i]);
  DEBUG_KNUTH(dbgs() << '\n');

  // D8. [Unnormalize]. Now q[...] is the desired quotient, and the desired
  // remainder may be obtained by dividing u[...] by d. If r is non-null we
  // compute the remainder (urem uses this).
  if (r) {
    // The value d is expressed by the "shift" value above since we avoided
    // multiplication by d by using a shift left. So, all we have to do is
    // shift right here.
    if (shift) {
      uint32_t carry = 0;
      DEBUG_KNUTH(dbgs() << "KnuthDiv: remainder:");
      for (int i = n-1; i >= 0; i--) {
        r[i] = (u[i] >> shift) | carry;
        carry = u[i] << (32 - shift);
        DEBUG_KNUTH(dbgs() << " " << r[i]);
      }
    } else {
      for (int i = n-1; i >= 0; i--) {
        r[i] = u[i];
        DEBUG_KNUTH(dbgs() << " " << r[i]);
      }
    }
    DEBUG_KNUTH(dbgs() << '\n');
  }
  DEBUG_KNUTH(dbgs() << '\n');
}

void APInt::divide(const WordType *LHS, unsigned lhsWords, const WordType *RHS,
                   unsigned rhsWords, WordType *Quotient, WordType *Remainder) {
  assert(lhsWords >= rhsWords && "Fractional result");

  // First, compose the values into an array of 32-bit words instead of
  // 64-bit words. This is a necessity of both the "short division" algorithm
  // and the Knuth "classical algorithm" which requires there to be native
  // operations for +, -, and * on an m bit value with an m*2 bit result. We
  // can't use 64-bit operands here because we don't have native results of
  // 128-bits. Furthermore, casting the 64-bit values to 32-bit values won't
  // work on large-endian machines.
  unsigned n = rhsWords * 2;
  unsigned m = (lhsWords * 2) - n;

  // Allocate space for the temporary values we need either on the stack, if
  // it will fit, or on the heap if it won't.
  uint32_t SPACE[128];
  uint32_t *U = nullptr;
  uint32_t *V = nullptr;
  uint32_t *Q = nullptr;
  uint32_t *R = nullptr;
  if ((Remainder?4:3)*n+2*m+1 <= 128) {
    U = &SPACE[0];
    V = &SPACE[m+n+1];
    Q = &SPACE[(m+n+1) + n];
    if (Remainder)
      R = &SPACE[(m+n+1) + n + (m+n)];
  } else {
    U = new uint32_t[m + n + 1];
    V = new uint32_t[n];
    Q = new uint32_t[m+n];
    if (Remainder)
      R = new uint32_t[n];
  }

  // Initialize the dividend
  memset(U, 0, (m+n+1)*sizeof(uint32_t));
  for (unsigned i = 0; i < lhsWords; ++i) {
    uint64_t tmp = LHS[i];
    U[i * 2] = Lo_32(tmp);
    U[i * 2 + 1] = Hi_32(tmp);
  }
  U[m+n] = 0; // this extra word is for "spill" in the Knuth algorithm.

  // Initialize the divisor
  memset(V, 0, (n)*sizeof(uint32_t));
  for (unsigned i = 0; i < rhsWords; ++i) {
    uint64_t tmp = RHS[i];
    V[i * 2] = Lo_32(tmp);
    V[i * 2 + 1] = Hi_32(tmp);
  }

  // initialize the quotient and remainder
  memset(Q, 0, (m+n) * sizeof(uint32_t));
  if (Remainder)
    memset(R, 0, n * sizeof(uint32_t));

  // Now, adjust m and n for the Knuth division. n is the number of words in
  // the divisor. m is the number of words by which the dividend exceeds the
  // divisor (i.e. m+n is the length of the dividend). These sizes must not
  // contain any zero words or the Knuth algorithm fails.
  for (unsigned i = n; i > 0 && V[i-1] == 0; i--) {
    n--;
    m++;
  }
  for (unsigned i = m+n; i > 0 && U[i-1] == 0; i--)
    m--;

  // If we're left with only a single word for the divisor, Knuth doesn't work
  // so we implement the short division algorithm here. This is much simpler
  // and faster because we are certain that we can divide a 64-bit quantity
  // by a 32-bit quantity at hardware speed and short division is simply a
  // series of such operations. This is just like doing short division but we
  // are using base 2^32 instead of base 10.
  assert(n != 0 && "Divide by zero?");
  if (n == 1) {
    uint32_t divisor = V[0];
    uint32_t remainder = 0;
    for (int i = m; i >= 0; i--) {
      uint64_t partial_dividend = Make_64(remainder, U[i]);
      if (partial_dividend == 0) {
        Q[i] = 0;
        remainder = 0;
      } else if (partial_dividend < divisor) {
        Q[i] = 0;
        remainder = Lo_32(partial_dividend);
      } else if (partial_dividend == divisor) {
        Q[i] = 1;
        remainder = 0;
      } else {
        Q[i] = Lo_32(partial_dividend / divisor);
        remainder = Lo_32(partial_dividend - (Q[i] * divisor));
      }
    }
    if (R)
      R[0] = remainder;
  } else {
    // Now we're ready to invoke the Knuth classical divide algorithm. In this
    // case n > 1.
    KnuthDiv(U, V, Q, R, m, n);
  }

  // If the caller wants the quotient
  if (Quotient) {
    for (unsigned i = 0; i < lhsWords; ++i)
      Quotient[i] = Make_64(Q[i*2+1], Q[i*2]);
  }

  // If the caller wants the remainder
  if (Remainder) {
    for (unsigned i = 0; i < rhsWords; ++i)
      Remainder[i] = Make_64(R[i*2+1], R[i*2]);
  }

  // Clean up the memory we allocated.
  if (U != &SPACE[0]) {
    delete [] U;
    delete [] V;
    delete [] Q;
    delete [] R;
  }
}

APInt APInt::udiv(const APInt &RHS) const {
  assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");

  // First, deal with the easy case
  if (isSingleWord()) {
    assert(RHS.U.VAL != 0 && "Divide by zero?");
    return APInt(BitWidth, U.VAL / RHS.U.VAL);
  }

  // Get some facts about the LHS and RHS number of bits and words
  unsigned lhsWords = getNumWords(getActiveBits());
  unsigned rhsBits  = RHS.getActiveBits();
  unsigned rhsWords = getNumWords(rhsBits);
  assert(rhsWords && "Divided by zero???");

  // Deal with some degenerate cases
  if (!lhsWords)
    // 0 / X ===> 0
    return APInt(BitWidth, 0);
  if (rhsBits == 1)
    // X / 1 ===> X
    return *this;
  if (lhsWords < rhsWords || this->ult(RHS))
    // X / Y ===> 0, iff X < Y
    return APInt(BitWidth, 0);
  if (*this == RHS)
    // X / X ===> 1
    return APInt(BitWidth, 1);
  if (lhsWords == 1) // rhsWords is 1 if lhsWords is 1.
    // All high words are zero, just use native divide
    return APInt(BitWidth, this->U.pVal[0] / RHS.U.pVal[0]);

  // We have to compute it the hard way. Invoke the Knuth divide algorithm.
  APInt Quotient(BitWidth, 0); // to hold result.
  divide(U.pVal, lhsWords, RHS.U.pVal, rhsWords, Quotient.U.pVal, nullptr);
  return Quotient;
}

APInt APInt::udiv(uint64_t RHS) const {
  assert(RHS != 0 && "Divide by zero?");

  // First, deal with the easy case
  if (isSingleWord())
    return APInt(BitWidth, U.VAL / RHS);

  // Get some facts about the LHS words.
  unsigned lhsWords = getNumWords(getActiveBits());

  // Deal with some degenerate cases
  if (!lhsWords)
    // 0 / X ===> 0
    return APInt(BitWidth, 0);
  if (RHS == 1)
    // X / 1 ===> X
    return *this;
  if (this->ult(RHS))
    // X / Y ===> 0, iff X < Y
    return APInt(BitWidth, 0);
  if (*this == RHS)
    // X / X ===> 1
    return APInt(BitWidth, 1);
  if (lhsWords == 1) // rhsWords is 1 if lhsWords is 1.
    // All high words are zero, just use native divide
    return APInt(BitWidth, this->U.pVal[0] / RHS);

  // We have to compute it the hard way. Invoke the Knuth divide algorithm.
  APInt Quotient(BitWidth, 0); // to hold result.
  divide(U.pVal, lhsWords, &RHS, 1, Quotient.U.pVal, nullptr);
  return Quotient;
}

APInt APInt::sdiv(const APInt &RHS) const {
  if (isNegative()) {
    if (RHS.isNegative())
      return (-(*this)).udiv(-RHS);
    return -((-(*this)).udiv(RHS));
  }
  if (RHS.isNegative())
    return -(this->udiv(-RHS));
  return this->udiv(RHS);
}

APInt APInt::sdiv(int64_t RHS) const {
  if (isNegative()) {
    if (RHS < 0)
      return (-(*this)).udiv(-RHS);
    return -((-(*this)).udiv(RHS));
  }
  if (RHS < 0)
    return -(this->udiv(-RHS));
  return this->udiv(RHS);
}

APInt APInt::urem(const APInt &RHS) const {
  assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
  if (isSingleWord()) {
    assert(RHS.U.VAL != 0 && "Remainder by zero?");
    return APInt(BitWidth, U.VAL % RHS.U.VAL);
  }

  // Get some facts about the LHS
  unsigned lhsWords = getNumWords(getActiveBits());

  // Get some facts about the RHS
  unsigned rhsBits = RHS.getActiveBits();
  unsigned rhsWords = getNumWords(rhsBits);
  assert(rhsWords && "Performing remainder operation by zero ???");

  // Check the degenerate cases
  if (lhsWords == 0)
    // 0 % Y ===> 0
    return APInt(BitWidth, 0);
  if (rhsBits == 1)
    // X % 1 ===> 0
    return APInt(BitWidth, 0);
  if (lhsWords < rhsWords || this->ult(RHS))
    // X % Y ===> X, iff X < Y
    return *this;
  if (*this == RHS)
    // X % X == 0;
    return APInt(BitWidth, 0);
  if (lhsWords == 1)
    // All high words are zero, just use native remainder
    return APInt(BitWidth, U.pVal[0] % RHS.U.pVal[0]);

  // We have to compute it the hard way. Invoke the Knuth divide algorithm.
  APInt Remainder(BitWidth, 0);
  divide(U.pVal, lhsWords, RHS.U.pVal, rhsWords, nullptr, Remainder.U.pVal);
  return Remainder;
}

uint64_t APInt::urem(uint64_t RHS) const {
  assert(RHS != 0 && "Remainder by zero?");

  if (isSingleWord())
    return U.VAL % RHS;

  // Get some facts about the LHS
  unsigned lhsWords = getNumWords(getActiveBits());

  // Check the degenerate cases
  if (lhsWords == 0)
    // 0 % Y ===> 0
    return 0;
  if (RHS == 1)
    // X % 1 ===> 0
    return 0;
  if (this->ult(RHS))
    // X % Y ===> X, iff X < Y
    return getZExtValue();
  if (*this == RHS)
    // X % X == 0;
    return 0;
  if (lhsWords == 1)
    // All high words are zero, just use native remainder
    return U.pVal[0] % RHS;

  // We have to compute it the hard way. Invoke the Knuth divide algorithm.
  uint64_t Remainder;
  divide(U.pVal, lhsWords, &RHS, 1, nullptr, &Remainder);
  return Remainder;
}

APInt APInt::srem(const APInt &RHS) const {
  if (isNegative()) {
    if (RHS.isNegative())
      return -((-(*this)).urem(-RHS));
    return -((-(*this)).urem(RHS));
  }
  if (RHS.isNegative())
    return this->urem(-RHS);
  return this->urem(RHS);
}

int64_t APInt::srem(int64_t RHS) const {
  if (isNegative()) {
    if (RHS < 0)
      return -((-(*this)).urem(-RHS));
    return -((-(*this)).urem(RHS));
  }
  if (RHS < 0)
    return this->urem(-RHS);
  return this->urem(RHS);
}

void APInt::udivrem(const APInt &LHS, const APInt &RHS,
                    APInt &Quotient, APInt &Remainder) {
  assert(LHS.BitWidth == RHS.BitWidth && "Bit widths must be the same");
  unsigned BitWidth = LHS.BitWidth;

  // First, deal with the easy case
  if (LHS.isSingleWord()) {
    assert(RHS.U.VAL != 0 && "Divide by zero?");
    uint64_t QuotVal = LHS.U.VAL / RHS.U.VAL;
    uint64_t RemVal = LHS.U.VAL % RHS.U.VAL;
    Quotient = APInt(BitWidth, QuotVal);
    Remainder = APInt(BitWidth, RemVal);
    return;
  }

  // Get some size facts about the dividend and divisor
  unsigned lhsWords = getNumWords(LHS.getActiveBits());
  unsigned rhsBits  = RHS.getActiveBits();
  unsigned rhsWords = getNumWords(rhsBits);
  assert(rhsWords && "Performing divrem operation by zero ???");

  // Check the degenerate cases
  if (lhsWords == 0) {
    Quotient = APInt(BitWidth, 0);    // 0 / Y ===> 0
    Remainder = APInt(BitWidth, 0);   // 0 % Y ===> 0
    return;
  }

  if (rhsBits == 1) {
    Quotient = LHS;                   // X / 1 ===> X
    Remainder = APInt(BitWidth, 0);   // X % 1 ===> 0
  }

  if (lhsWords < rhsWords || LHS.ult(RHS)) {
    Remainder = LHS;                  // X % Y ===> X, iff X < Y
    Quotient = APInt(BitWidth, 0);    // X / Y ===> 0, iff X < Y
    return;
  }

  if (LHS == RHS) {
    Quotient  = APInt(BitWidth, 1);   // X / X ===> 1
    Remainder = APInt(BitWidth, 0);   // X % X ===> 0;
    return;
  }

  // Make sure there is enough space to hold the results.
  // NOTE: This assumes that reallocate won't affect any bits if it doesn't
  // change the size. This is necessary if Quotient or Remainder is aliased
  // with LHS or RHS.
  Quotient.reallocate(BitWidth);
  Remainder.reallocate(BitWidth);

  if (lhsWords == 1) { // rhsWords is 1 if lhsWords is 1.
    // There is only one word to consider so use the native versions.
    uint64_t lhsValue = LHS.U.pVal[0];
    uint64_t rhsValue = RHS.U.pVal[0];
    Quotient = lhsValue / rhsValue;
    Remainder = lhsValue % rhsValue;
    return;
  }

  // Okay, lets do it the long way
  divide(LHS.U.pVal, lhsWords, RHS.U.pVal, rhsWords, Quotient.U.pVal,
         Remainder.U.pVal);
  // Clear the rest of the Quotient and Remainder.
  std::memset(Quotient.U.pVal + lhsWords, 0,
              (getNumWords(BitWidth) - lhsWords) * APINT_WORD_SIZE);
  std::memset(Remainder.U.pVal + rhsWords, 0,
              (getNumWords(BitWidth) - rhsWords) * APINT_WORD_SIZE);
}

void APInt::udivrem(const APInt &LHS, uint64_t RHS, APInt &Quotient,
                    uint64_t &Remainder) {
  assert(RHS != 0 && "Divide by zero?");
  unsigned BitWidth = LHS.BitWidth;

  // First, deal with the easy case
  if (LHS.isSingleWord()) {
    uint64_t QuotVal = LHS.U.VAL / RHS;
    Remainder = LHS.U.VAL % RHS;
    Quotient = APInt(BitWidth, QuotVal);
    return;
  }

  // Get some size facts about the dividend and divisor
  unsigned lhsWords = getNumWords(LHS.getActiveBits());

  // Check the degenerate cases
  if (lhsWords == 0) {
    Quotient = APInt(BitWidth, 0);    // 0 / Y ===> 0
    Remainder = 0;                    // 0 % Y ===> 0
    return;
  }

  if (RHS == 1) {
    Quotient = LHS;                   // X / 1 ===> X
    Remainder = 0;                    // X % 1 ===> 0
    return;
  }

  if (LHS.ult(RHS)) {
    Remainder = LHS.getZExtValue();   // X % Y ===> X, iff X < Y
    Quotient = APInt(BitWidth, 0);    // X / Y ===> 0, iff X < Y
    return;
  }

  if (LHS == RHS) {
    Quotient  = APInt(BitWidth, 1);   // X / X ===> 1
    Remainder = 0;                    // X % X ===> 0;
    return;
  }

  // Make sure there is enough space to hold the results.
  // NOTE: This assumes that reallocate won't affect any bits if it doesn't
  // change the size. This is necessary if Quotient is aliased with LHS.
  Quotient.reallocate(BitWidth);

  if (lhsWords == 1) { // rhsWords is 1 if lhsWords is 1.
    // There is only one word to consider so use the native versions.
    uint64_t lhsValue = LHS.U.pVal[0];
    Quotient = lhsValue / RHS;
    Remainder = lhsValue % RHS;
    return;
  }

  // Okay, lets do it the long way
  divide(LHS.U.pVal, lhsWords, &RHS, 1, Quotient.U.pVal, &Remainder);
  // Clear the rest of the Quotient.
  std::memset(Quotient.U.pVal + lhsWords, 0,
              (getNumWords(BitWidth) - lhsWords) * APINT_WORD_SIZE);
}

void APInt::sdivrem(const APInt &LHS, const APInt &RHS,
                    APInt &Quotient, APInt &Remainder) {
  if (LHS.isNegative()) {
    if (RHS.isNegative())
      APInt::udivrem(-LHS, -RHS, Quotient, Remainder);
    else {
      APInt::udivrem(-LHS, RHS, Quotient, Remainder);
      Quotient.negate();
    }
    Remainder.negate();
  } else if (RHS.isNegative()) {
    APInt::udivrem(LHS, -RHS, Quotient, Remainder);
    Quotient.negate();
  } else {
    APInt::udivrem(LHS, RHS, Quotient, Remainder);
  }
}

void APInt::sdivrem(const APInt &LHS, int64_t RHS,
                    APInt &Quotient, int64_t &Remainder) {
  uint64_t R = Remainder;
  if (LHS.isNegative()) {
    if (RHS < 0)
      APInt::udivrem(-LHS, -RHS, Quotient, R);
    else {
      APInt::udivrem(-LHS, RHS, Quotient, R);
      Quotient.negate();
    }
    R = -R;
  } else if (RHS < 0) {
    APInt::udivrem(LHS, -RHS, Quotient, R);
    Quotient.negate();
  } else {
    APInt::udivrem(LHS, RHS, Quotient, R);
  }
  Remainder = R;
}

APInt APInt::sadd_ov(const APInt &RHS, bool &Overflow) const {
  APInt Res = *this+RHS;
  Overflow = isNonNegative() == RHS.isNonNegative() &&
             Res.isNonNegative() != isNonNegative();
  return Res;
}

APInt APInt::uadd_ov(const APInt &RHS, bool &Overflow) const {
  APInt Res = *this+RHS;
  Overflow = Res.ult(RHS);
  return Res;
}

APInt APInt::ssub_ov(const APInt &RHS, bool &Overflow) const {
  APInt Res = *this - RHS;
  Overflow = isNonNegative() != RHS.isNonNegative() &&
             Res.isNonNegative() != isNonNegative();
  return Res;
}

APInt APInt::usub_ov(const APInt &RHS, bool &Overflow) const {
  APInt Res = *this-RHS;
  Overflow = Res.ugt(*this);
  return Res;
}

APInt APInt::sdiv_ov(const APInt &RHS, bool &Overflow) const {
  // MININT/-1  -->  overflow.
  Overflow = isMinSignedValue() && RHS.isAllOnesValue();
  return sdiv(RHS);
}

APInt APInt::smul_ov(const APInt &RHS, bool &Overflow) const {
  APInt Res = *this * RHS;

  if (*this != 0 && RHS != 0)
    Overflow = Res.sdiv(RHS) != *this || Res.sdiv(*this) != RHS;
  else
    Overflow = false;
  return Res;
}

APInt APInt::umul_ov(const APInt &RHS, bool &Overflow) const {
  APInt Res = *this * RHS;

  if (*this != 0 && RHS != 0)
    Overflow = Res.udiv(RHS) != *this || Res.udiv(*this) != RHS;
  else
    Overflow = false;
  return Res;
}

APInt APInt::sshl_ov(const APInt &ShAmt, bool &Overflow) const {
  Overflow = ShAmt.uge(getBitWidth());
  if (Overflow)
    return APInt(BitWidth, 0);

  if (isNonNegative()) // Don't allow sign change.
    Overflow = ShAmt.uge(countLeadingZeros());
  else
    Overflow = ShAmt.uge(countLeadingOnes());

  return *this << ShAmt;
}

APInt APInt::ushl_ov(const APInt &ShAmt, bool &Overflow) const {
  Overflow = ShAmt.uge(getBitWidth());
  if (Overflow)
    return APInt(BitWidth, 0);

  Overflow = ShAmt.ugt(countLeadingZeros());

  return *this << ShAmt;
}

APInt APInt::sadd_sat(const APInt &RHS) const {
  bool Overflow;
  APInt Res = sadd_ov(RHS, Overflow);
  if (!Overflow)
    return Res;

  return isNegative() ? APInt::getSignedMinValue(BitWidth)
                      : APInt::getSignedMaxValue(BitWidth);
}

APInt APInt::uadd_sat(const APInt &RHS) const {
  bool Overflow;
  APInt Res = uadd_ov(RHS, Overflow);
  if (!Overflow)
    return Res;

  return APInt::getMaxValue(BitWidth);
}

APInt APInt::ssub_sat(const APInt &RHS) const {
  bool Overflow;
  APInt Res = ssub_ov(RHS, Overflow);
  if (!Overflow)
    return Res;

  return isNegative() ? APInt::getSignedMinValue(BitWidth)
                      : APInt::getSignedMaxValue(BitWidth);
}

APInt APInt::usub_sat(const APInt &RHS) const {
  bool Overflow;
  APInt Res = usub_ov(RHS, Overflow);
  if (!Overflow)
    return Res;

  return APInt(BitWidth, 0);
}


void APInt::fromString(unsigned numbits, StringRef str, uint8_t radix) {
  // Check our assumptions here
  assert(!str.empty() && "Invalid string length");
  assert((radix == 10 || radix == 8 || radix == 16 || radix == 2 ||
          radix == 36) &&
         "Radix should be 2, 8, 10, 16, or 36!");

  StringRef::iterator p = str.begin();
  size_t slen = str.size();
  bool isNeg = *p == '-';
  if (*p == '-' || *p == '+') {
    p++;
    slen--;
    assert(slen && "String is only a sign, needs a value.");
  }
  assert((slen <= numbits || radix != 2) && "Insufficient bit width");
  assert(((slen-1)*3 <= numbits || radix != 8) && "Insufficient bit width");
  assert(((slen-1)*4 <= numbits || radix != 16) && "Insufficient bit width");
  assert((((slen-1)*64)/22 <= numbits || radix != 10) &&
         "Insufficient bit width");

  // Allocate memory if needed
  if (isSingleWord())
    U.VAL = 0;
  else
    U.pVal = getClearedMemory(getNumWords());

  // Figure out if we can shift instead of multiply
  unsigned shift = (radix == 16 ? 4 : radix == 8 ? 3 : radix == 2 ? 1 : 0);

  // Enter digit traversal loop
  for (StringRef::iterator e = str.end(); p != e; ++p) {
    unsigned digit = getDigit(*p, radix);
    assert(digit < radix && "Invalid character in digit string");

    // Shift or multiply the value by the radix
    if (slen > 1) {
      if (shift)
        *this <<= shift;
      else
        *this *= radix;
    }

    // Add in the digit we just interpreted
    *this += digit;
  }
  // If its negative, put it in two's complement form
  if (isNeg)
    this->negate();
}

void APInt::toString(SmallVectorImpl<char> &Str, unsigned Radix,
                     bool Signed, bool formatAsCLiteral) const {
  assert((Radix == 10 || Radix == 8 || Radix == 16 || Radix == 2 ||
          Radix == 36) &&
         "Radix should be 2, 8, 10, 16, or 36!");

  const char *Prefix = "";
  if (formatAsCLiteral) {
    switch (Radix) {
      case 2:
        // Binary literals are a non-standard extension added in gcc 4.3:
        // http://gcc.gnu.org/onlinedocs/gcc-4.3.0/gcc/Binary-constants.html
        Prefix = "0b";
        break;
      case 8:
        Prefix = "0";
        break;
      case 10:
        break; // No prefix
      case 16:
        Prefix = "0x";
        break;
      default:
        llvm_unreachable("Invalid radix!");
    }
  }

  // First, check for a zero value and just short circuit the logic below.
  if (*this == 0) {
    while (*Prefix) {
      Str.push_back(*Prefix);
      ++Prefix;
    };
    Str.push_back('0');
    return;
  }

  static const char Digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

  if (isSingleWord()) {
    char Buffer[65];
    char *BufPtr = std::end(Buffer);

    uint64_t N;
    if (!Signed) {
      N = getZExtValue();
    } else {
      int64_t I = getSExtValue();
      if (I >= 0) {
        N = I;
      } else {
        Str.push_back('-');
        N = -(uint64_t)I;
      }
    }

    while (*Prefix) {
      Str.push_back(*Prefix);
      ++Prefix;
    };

    while (N) {
      *--BufPtr = Digits[N % Radix];
      N /= Radix;
    }
    Str.append(BufPtr, std::end(Buffer));
    return;
  }

  APInt Tmp(*this);

  if (Signed && isNegative()) {
    // They want to print the signed version and it is a negative value
    // Flip the bits and add one to turn it into the equivalent positive
    // value and put a '-' in the result.
    Tmp.negate();
    Str.push_back('-');
  }

  while (*Prefix) {
    Str.push_back(*Prefix);
    ++Prefix;
  };

  // We insert the digits backward, then reverse them to get the right order.
  unsigned StartDig = Str.size();

  // For the 2, 8 and 16 bit cases, we can just shift instead of divide
  // because the number of bits per digit (1, 3 and 4 respectively) divides
  // equally.  We just shift until the value is zero.
  if (Radix == 2 || Radix == 8 || Radix == 16) {
    // Just shift tmp right for each digit width until it becomes zero
    unsigned ShiftAmt = (Radix == 16 ? 4 : (Radix == 8 ? 3 : 1));
    unsigned MaskAmt = Radix - 1;

    while (Tmp.getBoolValue()) {
      unsigned Digit = unsigned(Tmp.getRawData()[0]) & MaskAmt;
      Str.push_back(Digits[Digit]);
      Tmp.lshrInPlace(ShiftAmt);
    }
  } else {
    while (Tmp.getBoolValue()) {
      uint64_t Digit;
      udivrem(Tmp, Radix, Tmp, Digit);
      assert(Digit < Radix && "divide failed");
      Str.push_back(Digits[Digit]);
    }
  }

  // Reverse the digits before returning.
  std::reverse(Str.begin()+StartDig, Str.end());
}

/// Returns the APInt as a std::string. Note that this is an inefficient method.
/// It is better to pass in a SmallVector/SmallString to the methods above.
std::string APInt::toString(unsigned Radix = 10, bool Signed = true) const {
  SmallString<40> S;
  toString(S, Radix, Signed, /* formatAsCLiteral = */false);
  return S.str();
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void APInt::dump() const {
  SmallString<40> S, U;
  this->toStringUnsigned(U);
  this->toStringSigned(S);
  dbgs() << "APInt(" << BitWidth << "b, "
         << U << "u " << S << "s)\n";
}
#endif

void APInt::print(raw_ostream &OS, bool isSigned) const {
  SmallString<40> S;
  this->toString(S, 10, isSigned, /* formatAsCLiteral = */false);
  OS << S;
}

// This implements a variety of operations on a representation of
// arbitrary precision, two's-complement, bignum integer values.

// Assumed by lowHalf, highHalf, partMSB and partLSB.  A fairly safe
// and unrestricting assumption.
static_assert(APInt::APINT_BITS_PER_WORD % 2 == 0,
              "Part width must be divisible by 2!");

/* Some handy functions local to this file.  */

/* Returns the integer part with the least significant BITS set.
   BITS cannot be zero.  */
static inline APInt::WordType lowBitMask(unsigned bits) {
  assert(bits != 0 && bits <= APInt::APINT_BITS_PER_WORD);

  return ~(APInt::WordType) 0 >> (APInt::APINT_BITS_PER_WORD - bits);
}

/* Returns the value of the lower half of PART.  */
static inline APInt::WordType lowHalf(APInt::WordType part) {
  return part & lowBitMask(APInt::APINT_BITS_PER_WORD / 2);
}

/* Returns the value of the upper half of PART.  */
static inline APInt::WordType highHalf(APInt::WordType part) {
  return part >> (APInt::APINT_BITS_PER_WORD / 2);
}

/* Returns the bit number of the most significant set bit of a part.
   If the input number has no bits set -1U is returned.  */
static unsigned partMSB(APInt::WordType value) {
  return findLastSet(value, ZB_Max);
}

/* Returns the bit number of the least significant set bit of a
   part.  If the input number has no bits set -1U is returned.  */
static unsigned partLSB(APInt::WordType value) {
  return findFirstSet(value, ZB_Max);
}

/* Sets the least significant part of a bignum to the input value, and
   zeroes out higher parts.  */
void APInt::tcSet(WordType *dst, WordType part, unsigned parts) {
  assert(parts > 0);

  dst[0] = part;
  for (unsigned i = 1; i < parts; i++)
    dst[i] = 0;
}

/* Assign one bignum to another.  */
void APInt::tcAssign(WordType *dst, const WordType *src, unsigned parts) {
  for (unsigned i = 0; i < parts; i++)
    dst[i] = src[i];
}

/* Returns true if a bignum is zero, false otherwise.  */
bool APInt::tcIsZero(const WordType *src, unsigned parts) {
  for (unsigned i = 0; i < parts; i++)
    if (src[i])
      return false;

  return true;
}

/* Extract the given bit of a bignum; returns 0 or 1.  */
int APInt::tcExtractBit(const WordType *parts, unsigned bit) {
  return (parts[whichWord(bit)] & maskBit(bit)) != 0;
}

/* Set the given bit of a bignum. */
void APInt::tcSetBit(WordType *parts, unsigned bit) {
  parts[whichWord(bit)] |= maskBit(bit);
}

/* Clears the given bit of a bignum. */
void APInt::tcClearBit(WordType *parts, unsigned bit) {
  parts[whichWord(bit)] &= ~maskBit(bit);
}

/* Returns the bit number of the least significant set bit of a
   number.  If the input number has no bits set -1U is returned.  */
unsigned APInt::tcLSB(const WordType *parts, unsigned n) {
  for (unsigned i = 0; i < n; i++) {
    if (parts[i] != 0) {
      unsigned lsb = partLSB(parts[i]);

      return lsb + i * APINT_BITS_PER_WORD;
    }
  }

  return -1U;
}

/* Returns the bit number of the most significant set bit of a number.
   If the input number has no bits set -1U is returned.  */
unsigned APInt::tcMSB(const WordType *parts, unsigned n) {
  do {
    --n;

    if (parts[n] != 0) {
      unsigned msb = partMSB(parts[n]);

      return msb + n * APINT_BITS_PER_WORD;
    }
  } while (n);

  return -1U;
}

/* Copy the bit vector of width srcBITS from SRC, starting at bit
   srcLSB, to DST, of dstCOUNT parts, such that the bit srcLSB becomes
   the least significant bit of DST.  All high bits above srcBITS in
   DST are zero-filled.  */
void
APInt::tcExtract(WordType *dst, unsigned dstCount, const WordType *src,
                 unsigned srcBits, unsigned srcLSB) {
  unsigned dstParts = (srcBits + APINT_BITS_PER_WORD - 1) / APINT_BITS_PER_WORD;
  assert(dstParts <= dstCount);

  unsigned firstSrcPart = srcLSB / APINT_BITS_PER_WORD;
  tcAssign (dst, src + firstSrcPart, dstParts);

  unsigned shift = srcLSB % APINT_BITS_PER_WORD;
  tcShiftRight (dst, dstParts, shift);

  /* We now have (dstParts * APINT_BITS_PER_WORD - shift) bits from SRC
     in DST.  If this is less that srcBits, append the rest, else
     clear the high bits.  */
  unsigned n = dstParts * APINT_BITS_PER_WORD - shift;
  if (n < srcBits) {
    WordType mask = lowBitMask (srcBits - n);
    dst[dstParts - 1] |= ((src[firstSrcPart + dstParts] & mask)
                          << n % APINT_BITS_PER_WORD);
  } else if (n > srcBits) {
    if (srcBits % APINT_BITS_PER_WORD)
      dst[dstParts - 1] &= lowBitMask (srcBits % APINT_BITS_PER_WORD);
  }

  /* Clear high parts.  */
  while (dstParts < dstCount)
    dst[dstParts++] = 0;
}

/* DST += RHS + C where C is zero or one.  Returns the carry flag.  */
APInt::WordType APInt::tcAdd(WordType *dst, const WordType *rhs,
                             WordType c, unsigned parts) {
  assert(c <= 1);

  for (unsigned i = 0; i < parts; i++) {
    WordType l = dst[i];
    if (c) {
      dst[i] += rhs[i] + 1;
      c = (dst[i] <= l);
    } else {
      dst[i] += rhs[i];
      c = (dst[i] < l);
    }
  }

  return c;
}

/// This function adds a single "word" integer, src, to the multiple
/// "word" integer array, dst[]. dst[] is modified to reflect the addition and
/// 1 is returned if there is a carry out, otherwise 0 is returned.
/// @returns the carry of the addition.
APInt::WordType APInt::tcAddPart(WordType *dst, WordType src,
                                 unsigned parts) {
  for (unsigned i = 0; i < parts; ++i) {
    dst[i] += src;
    if (dst[i] >= src)
      return 0; // No need to carry so exit early.
    src = 1; // Carry one to next digit.
  }

  return 1;
}

/* DST -= RHS + C where C is zero or one.  Returns the carry flag.  */
APInt::WordType APInt::tcSubtract(WordType *dst, const WordType *rhs,
                                  WordType c, unsigned parts) {
  assert(c <= 1);

  for (unsigned i = 0; i < parts; i++) {
    WordType l = dst[i];
    if (c) {
      dst[i] -= rhs[i] + 1;
      c = (dst[i] >= l);
    } else {
      dst[i] -= rhs[i];
      c = (dst[i] > l);
    }
  }

  return c;
}

/// This function subtracts a single "word" (64-bit word), src, from
/// the multi-word integer array, dst[], propagating the borrowed 1 value until
/// no further borrowing is needed or it runs out of "words" in dst.  The result
/// is 1 if "borrowing" exhausted the digits in dst, or 0 if dst was not
/// exhausted. In other words, if src > dst then this function returns 1,
/// otherwise 0.
/// @returns the borrow out of the subtraction
APInt::WordType APInt::tcSubtractPart(WordType *dst, WordType src,
                                      unsigned parts) {
  for (unsigned i = 0; i < parts; ++i) {
    WordType Dst = dst[i];
    dst[i] -= src;
    if (src <= Dst)
      return 0; // No need to borrow so exit early.
    src = 1; // We have to "borrow 1" from next "word"
  }

  return 1;
}

/* Negate a bignum in-place.  */
void APInt::tcNegate(WordType *dst, unsigned parts) {
  tcComplement(dst, parts);
  tcIncrement(dst, parts);
}

/*  DST += SRC * MULTIPLIER + CARRY   if add is true
    DST  = SRC * MULTIPLIER + CARRY   if add is false

    Requires 0 <= DSTPARTS <= SRCPARTS + 1.  If DST overlaps SRC
    they must start at the same point, i.e. DST == SRC.

    If DSTPARTS == SRCPARTS + 1 no overflow occurs and zero is
    returned.  Otherwise DST is filled with the least significant
    DSTPARTS parts of the result, and if all of the omitted higher
    parts were zero return zero, otherwise overflow occurred and
    return one.  */
int APInt::tcMultiplyPart(WordType *dst, const WordType *src,
                          WordType multiplier, WordType carry,
                          unsigned srcParts, unsigned dstParts,
                          bool add) {
  /* Otherwise our writes of DST kill our later reads of SRC.  */
  assert(dst <= src || dst >= src + srcParts);
  assert(dstParts <= srcParts + 1);

  /* N loops; minimum of dstParts and srcParts.  */
  unsigned n = std::min(dstParts, srcParts);

  for (unsigned i = 0; i < n; i++) {
    WordType low, mid, high, srcPart;

      /* [ LOW, HIGH ] = MULTIPLIER * SRC[i] + DST[i] + CARRY.

         This cannot overflow, because

         (n - 1) * (n - 1) + 2 (n - 1) = (n - 1) * (n + 1)

         which is less than n^2.  */

    srcPart = src[i];

    if (multiplier == 0 || srcPart == 0) {
      low = carry;
      high = 0;
    } else {
      low = lowHalf(srcPart) * lowHalf(multiplier);
      high = highHalf(srcPart) * highHalf(multiplier);

      mid = lowHalf(srcPart) * highHalf(multiplier);
      high += highHalf(mid);
      mid <<= APINT_BITS_PER_WORD / 2;
      if (low + mid < low)
        high++;
      low += mid;

      mid = highHalf(srcPart) * lowHalf(multiplier);
      high += highHalf(mid);
      mid <<= APINT_BITS_PER_WORD / 2;
      if (low + mid < low)
        high++;
      low += mid;

      /* Now add carry.  */
      if (low + carry < low)
        high++;
      low += carry;
    }

    if (add) {
      /* And now DST[i], and store the new low part there.  */
      if (low + dst[i] < low)
        high++;
      dst[i] += low;
    } else
      dst[i] = low;

    carry = high;
  }

  if (srcParts < dstParts) {
    /* Full multiplication, there is no overflow.  */
    assert(srcParts + 1 == dstParts);
    dst[srcParts] = carry;
    return 0;
  }

  /* We overflowed if there is carry.  */
  if (carry)
    return 1;

  /* We would overflow if any significant unwritten parts would be
     non-zero.  This is true if any remaining src parts are non-zero
     and the multiplier is non-zero.  */
  if (multiplier)
    for (unsigned i = dstParts; i < srcParts; i++)
      if (src[i])
        return 1;

  /* We fitted in the narrow destination.  */
  return 0;
}

/* DST = LHS * RHS, where DST has the same width as the operands and
   is filled with the least significant parts of the result.  Returns
   one if overflow occurred, otherwise zero.  DST must be disjoint
   from both operands.  */
int APInt::tcMultiply(WordType *dst, const WordType *lhs,
                      const WordType *rhs, unsigned parts) {
  assert(dst != lhs && dst != rhs);

  int overflow = 0;
  tcSet(dst, 0, parts);

  for (unsigned i = 0; i < parts; i++)
    overflow |= tcMultiplyPart(&dst[i], lhs, rhs[i], 0, parts,
                               parts - i, true);

  return overflow;
}

/// DST = LHS * RHS, where DST has width the sum of the widths of the
/// operands. No overflow occurs. DST must be disjoint from both operands.
void APInt::tcFullMultiply(WordType *dst, const WordType *lhs,
                           const WordType *rhs, unsigned lhsParts,
                           unsigned rhsParts) {
  /* Put the narrower number on the LHS for less loops below.  */
  if (lhsParts > rhsParts)
    return tcFullMultiply (dst, rhs, lhs, rhsParts, lhsParts);

  assert(dst != lhs && dst != rhs);

  tcSet(dst, 0, rhsParts);

  for (unsigned i = 0; i < lhsParts; i++)
    tcMultiplyPart(&dst[i], rhs, lhs[i], 0, rhsParts, rhsParts + 1, true);
}

/* If RHS is zero LHS and REMAINDER are left unchanged, return one.
   Otherwise set LHS to LHS / RHS with the fractional part discarded,
   set REMAINDER to the remainder, return zero.  i.e.

   OLD_LHS = RHS * LHS + REMAINDER

   SCRATCH is a bignum of the same size as the operands and result for
   use by the routine; its contents need not be initialized and are
   destroyed.  LHS, REMAINDER and SCRATCH must be distinct.
*/
int APInt::tcDivide(WordType *lhs, const WordType *rhs,
                    WordType *remainder, WordType *srhs,
                    unsigned parts) {
  assert(lhs != remainder && lhs != srhs && remainder != srhs);

  unsigned shiftCount = tcMSB(rhs, parts) + 1;
  if (shiftCount == 0)
    return true;

  shiftCount = parts * APINT_BITS_PER_WORD - shiftCount;
  unsigned n = shiftCount / APINT_BITS_PER_WORD;
  WordType mask = (WordType) 1 << (shiftCount % APINT_BITS_PER_WORD);

  tcAssign(srhs, rhs, parts);
  tcShiftLeft(srhs, parts, shiftCount);
  tcAssign(remainder, lhs, parts);
  tcSet(lhs, 0, parts);

  /* Loop, subtracting SRHS if REMAINDER is greater and adding that to
     the total.  */
  for (;;) {
    int compare = tcCompare(remainder, srhs, parts);
    if (compare >= 0) {
      tcSubtract(remainder, srhs, 0, parts);
      lhs[n] |= mask;
    }

    if (shiftCount == 0)
      break;
    shiftCount--;
    tcShiftRight(srhs, parts, 1);
    if ((mask >>= 1) == 0) {
      mask = (WordType) 1 << (APINT_BITS_PER_WORD - 1);
      n--;
    }
  }

  return false;
}

/// Shift a bignum left Cound bits in-place. Shifted in bits are zero. There are
/// no restrictions on Count.
void APInt::tcShiftLeft(WordType *Dst, unsigned Words, unsigned Count) {
  // Don't bother performing a no-op shift.
  if (!Count)
    return;

  // WordShift is the inter-part shift; BitShift is the intra-part shift.
  unsigned WordShift = std::min(Count / APINT_BITS_PER_WORD, Words);
  unsigned BitShift = Count % APINT_BITS_PER_WORD;

  // Fastpath for moving by whole words.
  if (BitShift == 0) {
    std::memmove(Dst + WordShift, Dst, (Words - WordShift) * APINT_WORD_SIZE);
  } else {
    while (Words-- > WordShift) {
      Dst[Words] = Dst[Words - WordShift] << BitShift;
      if (Words > WordShift)
        Dst[Words] |=
          Dst[Words - WordShift - 1] >> (APINT_BITS_PER_WORD - BitShift);
    }
  }

  // Fill in the remainder with 0s.
  std::memset(Dst, 0, WordShift * APINT_WORD_SIZE);
}

/// Shift a bignum right Count bits in-place. Shifted in bits are zero. There
/// are no restrictions on Count.
void APInt::tcShiftRight(WordType *Dst, unsigned Words, unsigned Count) {
  // Don't bother performing a no-op shift.
  if (!Count)
    return;

  // WordShift is the inter-part shift; BitShift is the intra-part shift.
  unsigned WordShift = std::min(Count / APINT_BITS_PER_WORD, Words);
  unsigned BitShift = Count % APINT_BITS_PER_WORD;

  unsigned WordsToMove = Words - WordShift;
  // Fastpath for moving by whole words.
  if (BitShift == 0) {
    std::memmove(Dst, Dst + WordShift, WordsToMove * APINT_WORD_SIZE);
  } else {
    for (unsigned i = 0; i != WordsToMove; ++i) {
      Dst[i] = Dst[i + WordShift] >> BitShift;
      if (i + 1 != WordsToMove)
        Dst[i] |= Dst[i + WordShift + 1] << (APINT_BITS_PER_WORD - BitShift);
    }
  }

  // Fill in the remainder with 0s.
  std::memset(Dst + WordsToMove, 0, WordShift * APINT_WORD_SIZE);
}

/* Bitwise and of two bignums.  */
void APInt::tcAnd(WordType *dst, const WordType *rhs, unsigned parts) {
  for (unsigned i = 0; i < parts; i++)
    dst[i] &= rhs[i];
}

/* Bitwise inclusive or of two bignums.  */
void APInt::tcOr(WordType *dst, const WordType *rhs, unsigned parts) {
  for (unsigned i = 0; i < parts; i++)
    dst[i] |= rhs[i];
}

/* Bitwise exclusive or of two bignums.  */
void APInt::tcXor(WordType *dst, const WordType *rhs, unsigned parts) {
  for (unsigned i = 0; i < parts; i++)
    dst[i] ^= rhs[i];
}

/* Complement a bignum in-place.  */
void APInt::tcComplement(WordType *dst, unsigned parts) {
  for (unsigned i = 0; i < parts; i++)
    dst[i] = ~dst[i];
}

/* Comparison (unsigned) of two bignums.  */
int APInt::tcCompare(const WordType *lhs, const WordType *rhs,
                     unsigned parts) {
  while (parts) {
    parts--;
    if (lhs[parts] != rhs[parts])
      return (lhs[parts] > rhs[parts]) ? 1 : -1;
  }

  return 0;
}

/* Set the least significant BITS bits of a bignum, clear the
   rest.  */
void APInt::tcSetLeastSignificantBits(WordType *dst, unsigned parts,
                                      unsigned bits) {
  unsigned i = 0;
  while (bits > APINT_BITS_PER_WORD) {
    dst[i++] = ~(WordType) 0;
    bits -= APINT_BITS_PER_WORD;
  }

  if (bits)
    dst[i++] = ~(WordType) 0 >> (APINT_BITS_PER_WORD - bits);

  while (i < parts)
    dst[i++] = 0;
}

APInt llvm::APIntOps::RoundingUDiv(const APInt &A, const APInt &B,
                                   APInt::Rounding RM) {
  // Currently udivrem always rounds down.
  switch (RM) {
  case APInt::Rounding::DOWN:
  case APInt::Rounding::TOWARD_ZERO:
    return A.udiv(B);
  case APInt::Rounding::UP: {
    APInt Quo, Rem;
    APInt::udivrem(A, B, Quo, Rem);
    if (Rem == 0)
      return Quo;
    return Quo + 1;
  }
  }
  llvm_unreachable("Unknown APInt::Rounding enum");
}

APInt llvm::APIntOps::RoundingSDiv(const APInt &A, const APInt &B,
                                   APInt::Rounding RM) {
  switch (RM) {
  case APInt::Rounding::DOWN:
  case APInt::Rounding::UP: {
    APInt Quo, Rem;
    APInt::sdivrem(A, B, Quo, Rem);
    if (Rem == 0)
      return Quo;
    // This algorithm deals with arbitrary rounding mode used by sdivrem.
    // We want to check whether the non-integer part of the mathematical value
    // is negative or not. If the non-integer part is negative, we need to round
    // down from Quo; otherwise, if it's positive or 0, we return Quo, as it's
    // already rounded down.
    if (RM == APInt::Rounding::DOWN) {
      if (Rem.isNegative() != B.isNegative())
        return Quo - 1;
      return Quo;
    }
    if (Rem.isNegative() != B.isNegative())
      return Quo;
    return Quo + 1;
  }
  // Currently sdiv rounds twards zero.
  case APInt::Rounding::TOWARD_ZERO:
    return A.sdiv(B);
  }
  llvm_unreachable("Unknown APInt::Rounding enum");
}

Optional<APInt>
llvm::APIntOps::SolveQuadraticEquationWrap(APInt A, APInt B, APInt C,
                                           unsigned RangeWidth) {
  unsigned CoeffWidth = A.getBitWidth();
  assert(CoeffWidth == B.getBitWidth() && CoeffWidth == C.getBitWidth());
  assert(RangeWidth <= CoeffWidth &&
         "Value range width should be less than coefficient width");
  assert(RangeWidth > 1 && "Value range bit width should be > 1");

  LLVM_DEBUG(dbgs() << __func__ << ": solving " << A << "x^2 + " << B
                    << "x + " << C << ", rw:" << RangeWidth << '\n');

  // Identify 0 as a (non)solution immediately.
  if (C.sextOrTrunc(RangeWidth).isNullValue() ) {
    LLVM_DEBUG(dbgs() << __func__ << ": zero solution\n");
    return APInt(CoeffWidth, 0);
  }

  // The result of APInt arithmetic has the same bit width as the operands,
  // so it can actually lose high bits. A product of two n-bit integers needs
  // 2n-1 bits to represent the full value.
  // The operation done below (on quadratic coefficients) that can produce
  // the largest value is the evaluation of the equation during bisection,
  // which needs 3 times the bitwidth of the coefficient, so the total number
  // of required bits is 3n.
  //
  // The purpose of this extension is to simulate the set Z of all integers,
  // where n+1 > n for all n in Z. In Z it makes sense to talk about positive
  // and negative numbers (not so much in a modulo arithmetic). The method
  // used to solve the equation is based on the standard formula for real
  // numbers, and uses the concepts of "positive" and "negative" with their
  // usual meanings.
  CoeffWidth *= 3;
  A = A.sext(CoeffWidth);
  B = B.sext(CoeffWidth);
  C = C.sext(CoeffWidth);

  // Make A > 0 for simplicity. Negate cannot overflow at this point because
  // the bit width has increased.
  if (A.isNegative()) {
    A.negate();
    B.negate();
    C.negate();
  }

  // Solving an equation q(x) = 0 with coefficients in modular arithmetic
  // is really solving a set of equations q(x) = kR for k = 0, 1, 2, ...,
  // and R = 2^BitWidth.
  // Since we're trying not only to find exact solutions, but also values
  // that "wrap around", such a set will always have a solution, i.e. an x
  // that satisfies at least one of the equations, or such that |q(x)|
  // exceeds kR, while |q(x-1)| for the same k does not.
  //
  // We need to find a value k, such that Ax^2 + Bx + C = kR will have a
  // positive solution n (in the above sense), and also such that the n
  // will be the least among all solutions corresponding to k = 0, 1, ...
  // (more precisely, the least element in the set
  //   { n(k) | k is such that a solution n(k) exists }).
  //
  // Consider the parabola (over real numbers) that corresponds to the
  // quadratic equation. Since A > 0, the arms of the parabola will point
  // up. Picking different values of k will shift it up and down by R.
  //
  // We want to shift the parabola in such a way as to reduce the problem
  // of solving q(x) = kR to solving shifted_q(x) = 0.
  // (The interesting solutions are the ceilings of the real number
  // solutions.)
  APInt R = APInt::getOneBitSet(CoeffWidth, RangeWidth);
  APInt TwoA = 2 * A;
  APInt SqrB = B * B;
  bool PickLow;

  auto RoundUp = [] (const APInt &V, const APInt &A) -> APInt {
    assert(A.isStrictlyPositive());
    APInt T = V.abs().urem(A);
    if (T.isNullValue())
      return V;
    return V.isNegative() ? V+T : V+(A-T);
  };

  // The vertex of the parabola is at -B/2A, but since A > 0, it's negative
  // iff B is positive.
  if (B.isNonNegative()) {
    // If B >= 0, the vertex it at a negative location (or at 0), so in
    // order to have a non-negative solution we need to pick k that makes
    // C-kR negative. To satisfy all the requirements for the solution
    // that we are looking for, it needs to be closest to 0 of all k.
    C = C.srem(R);
    if (C.isStrictlyPositive())
      C -= R;
    // Pick the greater solution.
    PickLow = false;
  } else {
    // If B < 0, the vertex is at a positive location. For any solution
    // to exist, the discriminant must be non-negative. This means that
    // C-kR <= B^2/4A is a necessary condition for k, i.e. there is a
    // lower bound on values of k: kR >= C - B^2/4A.
    APInt LowkR = C - SqrB.udiv(2*TwoA); // udiv because all values > 0.
    // Round LowkR up (towards +inf) to the nearest kR.
    LowkR = RoundUp(LowkR, R);

    // If there exists k meeting the condition above, and such that
    // C-kR > 0, there will be two positive real number solutions of
    // q(x) = kR. Out of all such values of k, pick the one that makes
    // C-kR closest to 0, (i.e. pick maximum k such that C-kR > 0).
    // In other words, find maximum k such that LowkR <= kR < C.
    if (C.sgt(LowkR)) {
      // If LowkR < C, then such a k is guaranteed to exist because
      // LowkR itself is a multiple of R.
      C -= -RoundUp(-C, R);      // C = C - RoundDown(C, R)
      // Pick the smaller solution.
      PickLow = true;
    } else {
      // If C-kR < 0 for all potential k's, it means that one solution
      // will be negative, while the other will be positive. The positive
      // solution will shift towards 0 if the parabola is moved up.
      // Pick the kR closest to the lower bound (i.e. make C-kR closest
      // to 0, or in other words, out of all parabolas that have solutions,
      // pick the one that is the farthest "up").
      // Since LowkR is itself a multiple of R, simply take C-LowkR.
      C -= LowkR;
      // Pick the greater solution.
      PickLow = false;
    }
  }

  LLVM_DEBUG(dbgs() << __func__ << ": updated coefficients " << A << "x^2 + "
                    << B << "x + " << C << ", rw:" << RangeWidth << '\n');

  APInt D = SqrB - 4*A*C;
  assert(D.isNonNegative() && "Negative discriminant");
  APInt SQ = D.sqrt();

  APInt Q = SQ * SQ;
  bool InexactSQ = Q != D;
  // The calculated SQ may actually be greater than the exact (non-integer)
  // value. If that's the case, decremement SQ to get a value that is lower.
  if (Q.sgt(D))
    SQ -= 1;

  APInt X;
  APInt Rem;

  // SQ is rounded down (i.e SQ * SQ <= D), so the roots may be inexact.
  // When using the quadratic formula directly, the calculated low root
  // may be greater than the exact one, since we would be subtracting SQ.
  // To make sure that the calculated root is not greater than the exact
  // one, subtract SQ+1 when calculating the low root (for inexact value
  // of SQ).
  if (PickLow)
    APInt::sdivrem(-B - (SQ+InexactSQ), TwoA, X, Rem);
  else
    APInt::sdivrem(-B + SQ, TwoA, X, Rem);

  // The updated coefficients should be such that the (exact) solution is
  // positive. Since APInt division rounds towards 0, the calculated one
  // can be 0, but cannot be negative.
  assert(X.isNonNegative() && "Solution should be non-negative");

  if (!InexactSQ && Rem.isNullValue()) {
    LLVM_DEBUG(dbgs() << __func__ << ": solution (root): " << X << '\n');
    return X;
  }

  assert((SQ*SQ).sle(D) && "SQ = |_sqrt(D)_|, so SQ*SQ <= D");
  // The exact value of the square root of D should be between SQ and SQ+1.
  // This implies that the solution should be between that corresponding to
  // SQ (i.e. X) and that corresponding to SQ+1.
  //
  // The calculated X cannot be greater than the exact (real) solution.
  // Actually it must be strictly less than the exact solution, while
  // X+1 will be greater than or equal to it.

  APInt VX = (A*X + B)*X + C;
  APInt VY = VX + TwoA*X + A + B;
  bool SignChange = VX.isNegative() != VY.isNegative() ||
                    VX.isNullValue() != VY.isNullValue();
  // If the sign did not change between X and X+1, X is not a valid solution.
  // This could happen when the actual (exact) roots don't have an integer
  // between them, so they would both be contained between X and X+1.
  if (!SignChange) {
    LLVM_DEBUG(dbgs() << __func__ << ": no valid solution\n");
    return None;
  }

  X += 1;
  LLVM_DEBUG(dbgs() << __func__ << ": solution (wrap): " << X << '\n');
  return X;
}
