//===-- llvm/Support/MathExtras.h - Useful math functions -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains some functions that are useful for math stuff.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_MATHEXTRAS_H
#define LLVM_SUPPORT_MATHEXTRAS_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/SwapByteOrder.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstring>
#include <limits>
#include <type_traits>

#ifdef __ANDROID_NDK__
#include <android/api-level.h>
#endif

#ifdef _MSC_VER
// Declare these intrinsics manually rather including intrin.h. It's very
// expensive, and MathExtras.h is popular.
// #include <intrin.h>
extern "C" {
unsigned char _BitScanForward(unsigned long *_Index, unsigned long _Mask);
unsigned char _BitScanForward64(unsigned long *_Index, unsigned __int64 _Mask);
unsigned char _BitScanReverse(unsigned long *_Index, unsigned long _Mask);
unsigned char _BitScanReverse64(unsigned long *_Index, unsigned __int64 _Mask);
}
#endif

namespace llvm {
/// The behavior an operation has on an input of 0.
enum ZeroBehavior {
  /// The returned value is undefined.
  ZB_Undefined,
  /// The returned value is numeric_limits<T>::max()
  ZB_Max,
  /// The returned value is numeric_limits<T>::digits
  ZB_Width
};

namespace detail {
template <typename T, std::size_t SizeOfT> struct TrailingZerosCounter {
  static std::size_t count(T Val, ZeroBehavior) {
    if (!Val)
      return std::numeric_limits<T>::digits;
    if (Val & 0x1)
      return 0;

    // Bisection method.
    std::size_t ZeroBits = 0;
    T Shift = std::numeric_limits<T>::digits >> 1;
    T Mask = std::numeric_limits<T>::max() >> Shift;
    while (Shift) {
      if ((Val & Mask) == 0) {
        Val >>= Shift;
        ZeroBits |= Shift;
      }
      Shift >>= 1;
      Mask >>= Shift;
    }
    return ZeroBits;
  }
};

#if __GNUC__ >= 4 || defined(_MSC_VER)
template <typename T> struct TrailingZerosCounter<T, 4> {
  static std::size_t count(T Val, ZeroBehavior ZB) {
    if (ZB != ZB_Undefined && Val == 0)
      return 32;

#if __has_builtin(__builtin_ctz) || LLVM_GNUC_PREREQ(4, 0, 0)
    return __builtin_ctz(Val);
#elif defined(_MSC_VER)
    unsigned long Index;
    _BitScanForward(&Index, Val);
    return Index;
#endif
  }
};

#if !defined(_MSC_VER) || defined(_M_X64)
template <typename T> struct TrailingZerosCounter<T, 8> {
  static std::size_t count(T Val, ZeroBehavior ZB) {
    if (ZB != ZB_Undefined && Val == 0)
      return 64;

#if __has_builtin(__builtin_ctzll) || LLVM_GNUC_PREREQ(4, 0, 0)
    return __builtin_ctzll(Val);
#elif defined(_MSC_VER)
    unsigned long Index;
    _BitScanForward64(&Index, Val);
    return Index;
#endif
  }
};
#endif
#endif
} // namespace detail

/// Count number of 0's from the least significant bit to the most
///   stopping at the first 1.
///
/// Only unsigned integral types are allowed.
///
/// \param ZB the behavior on an input of 0. Only ZB_Width and ZB_Undefined are
///   valid arguments.
template <typename T>
std::size_t countTrailingZeros(T Val, ZeroBehavior ZB = ZB_Width) {
  static_assert(std::numeric_limits<T>::is_integer &&
                    !std::numeric_limits<T>::is_signed,
                "Only unsigned integral types are allowed.");
  return llvm::detail::TrailingZerosCounter<T, sizeof(T)>::count(Val, ZB);
}

namespace detail {
template <typename T, std::size_t SizeOfT> struct LeadingZerosCounter {
  static std::size_t count(T Val, ZeroBehavior) {
    if (!Val)
      return std::numeric_limits<T>::digits;

    // Bisection method.
    std::size_t ZeroBits = 0;
    for (T Shift = std::numeric_limits<T>::digits >> 1; Shift; Shift >>= 1) {
      T Tmp = Val >> Shift;
      if (Tmp)
        Val = Tmp;
      else
        ZeroBits |= Shift;
    }
    return ZeroBits;
  }
};

#if __GNUC__ >= 4 || defined(_MSC_VER)
template <typename T> struct LeadingZerosCounter<T, 4> {
  static std::size_t count(T Val, ZeroBehavior ZB) {
    if (ZB != ZB_Undefined && Val == 0)
      return 32;

#if __has_builtin(__builtin_clz) || LLVM_GNUC_PREREQ(4, 0, 0)
    return __builtin_clz(Val);
#elif defined(_MSC_VER)
    unsigned long Index;
    _BitScanReverse(&Index, Val);
    return Index ^ 31;
#endif
  }
};

#if !defined(_MSC_VER) || defined(_M_X64)
template <typename T> struct LeadingZerosCounter<T, 8> {
  static std::size_t count(T Val, ZeroBehavior ZB) {
    if (ZB != ZB_Undefined && Val == 0)
      return 64;

#if __has_builtin(__builtin_clzll) || LLVM_GNUC_PREREQ(4, 0, 0)
    return __builtin_clzll(Val);
#elif defined(_MSC_VER)
    unsigned long Index;
    _BitScanReverse64(&Index, Val);
    return Index ^ 63;
#endif
  }
};
#endif
#endif
} // namespace detail

/// Count number of 0's from the most significant bit to the least
///   stopping at the first 1.
///
/// Only unsigned integral types are allowed.
///
/// \param ZB the behavior on an input of 0. Only ZB_Width and ZB_Undefined are
///   valid arguments.
template <typename T>
std::size_t countLeadingZeros(T Val, ZeroBehavior ZB = ZB_Width) {
  static_assert(std::numeric_limits<T>::is_integer &&
                    !std::numeric_limits<T>::is_signed,
                "Only unsigned integral types are allowed.");
  return llvm::detail::LeadingZerosCounter<T, sizeof(T)>::count(Val, ZB);
}

/// Get the index of the first set bit starting from the least
///   significant bit.
///
/// Only unsigned integral types are allowed.
///
/// \param ZB the behavior on an input of 0. Only ZB_Max and ZB_Undefined are
///   valid arguments.
template <typename T> T findFirstSet(T Val, ZeroBehavior ZB = ZB_Max) {
  if (ZB == ZB_Max && Val == 0)
    return std::numeric_limits<T>::max();

  return countTrailingZeros(Val, ZB_Undefined);
}

/// Create a bitmask with the N right-most bits set to 1, and all other
/// bits set to 0.  Only unsigned types are allowed.
template <typename T> T maskTrailingOnes(unsigned N) {
  static_assert(std::is_unsigned<T>::value, "Invalid type!");
  const unsigned Bits = CHAR_BIT * sizeof(T);
  assert(N <= Bits && "Invalid bit index");
  return N == 0 ? 0 : (T(-1) >> (Bits - N));
}

/// Create a bitmask with the N left-most bits set to 1, and all other
/// bits set to 0.  Only unsigned types are allowed.
template <typename T> T maskLeadingOnes(unsigned N) {
  return ~maskTrailingOnes<T>(CHAR_BIT * sizeof(T) - N);
}

/// Create a bitmask with the N right-most bits set to 0, and all other
/// bits set to 1.  Only unsigned types are allowed.
template <typename T> T maskTrailingZeros(unsigned N) {
  return maskLeadingOnes<T>(CHAR_BIT * sizeof(T) - N);
}

/// Create a bitmask with the N left-most bits set to 0, and all other
/// bits set to 1.  Only unsigned types are allowed.
template <typename T> T maskLeadingZeros(unsigned N) {
  return maskTrailingOnes<T>(CHAR_BIT * sizeof(T) - N);
}

/// Get the index of the last set bit starting from the least
///   significant bit.
///
/// Only unsigned integral types are allowed.
///
/// \param ZB the behavior on an input of 0. Only ZB_Max and ZB_Undefined are
///   valid arguments.
template <typename T> T findLastSet(T Val, ZeroBehavior ZB = ZB_Max) {
  if (ZB == ZB_Max && Val == 0)
    return std::numeric_limits<T>::max();

  // Use ^ instead of - because both gcc and llvm can remove the associated ^
  // in the __builtin_clz intrinsic on x86.
  return countLeadingZeros(Val, ZB_Undefined) ^
         (std::numeric_limits<T>::digits - 1);
}

/// Macro compressed bit reversal table for 256 bits.
///
/// http://graphics.stanford.edu/~seander/bithacks.html#BitReverseTable
static const unsigned char BitReverseTable256[256] = {
#define R2(n) n, n + 2 * 64, n + 1 * 64, n + 3 * 64
#define R4(n) R2(n), R2(n + 2 * 16), R2(n + 1 * 16), R2(n + 3 * 16)
#define R6(n) R4(n), R4(n + 2 * 4), R4(n + 1 * 4), R4(n + 3 * 4)
  R6(0), R6(2), R6(1), R6(3)
#undef R2
#undef R4
#undef R6
};

/// Reverse the bits in \p Val.
template <typename T>
T reverseBits(T Val) {
  unsigned char in[sizeof(Val)];
  unsigned char out[sizeof(Val)];
  std::memcpy(in, &Val, sizeof(Val));
  for (unsigned i = 0; i < sizeof(Val); ++i)
    out[(sizeof(Val) - i) - 1] = BitReverseTable256[in[i]];
  std::memcpy(&Val, out, sizeof(Val));
  return Val;
}

// NOTE: The following support functions use the _32/_64 extensions instead of
// type overloading so that signed and unsigned integers can be used without
// ambiguity.

/// Return the high 32 bits of a 64 bit value.
constexpr inline uint32_t Hi_32(uint64_t Value) {
  return static_cast<uint32_t>(Value >> 32);
}

/// Return the low 32 bits of a 64 bit value.
constexpr inline uint32_t Lo_32(uint64_t Value) {
  return static_cast<uint32_t>(Value);
}

/// Make a 64-bit integer from a high / low pair of 32-bit integers.
constexpr inline uint64_t Make_64(uint32_t High, uint32_t Low) {
  return ((uint64_t)High << 32) | (uint64_t)Low;
}

/// Checks if an integer fits into the given bit width.
template <unsigned N> constexpr inline bool isInt(int64_t x) {
  return N >= 64 || (-(INT64_C(1)<<(N-1)) <= x && x < (INT64_C(1)<<(N-1)));
}
// Template specializations to get better code for common cases.
template <> constexpr inline bool isInt<8>(int64_t x) {
  return static_cast<int8_t>(x) == x;
}
template <> constexpr inline bool isInt<16>(int64_t x) {
  return static_cast<int16_t>(x) == x;
}
template <> constexpr inline bool isInt<32>(int64_t x) {
  return static_cast<int32_t>(x) == x;
}

/// Checks if a signed integer is an N bit number shifted left by S.
template <unsigned N, unsigned S>
constexpr inline bool isShiftedInt(int64_t x) {
  static_assert(
      N > 0, "isShiftedInt<0> doesn't make sense (refers to a 0-bit number.");
  static_assert(N + S <= 64, "isShiftedInt<N, S> with N + S > 64 is too wide.");
  return isInt<N + S>(x) && (x % (UINT64_C(1) << S) == 0);
}

/// Checks if an unsigned integer fits into the given bit width.
///
/// This is written as two functions rather than as simply
///
///   return N >= 64 || X < (UINT64_C(1) << N);
///
/// to keep MSVC from (incorrectly) warning on isUInt<64> that we're shifting
/// left too many places.
template <unsigned N>
constexpr inline typename std::enable_if<(N < 64), bool>::type
isUInt(uint64_t X) {
  static_assert(N > 0, "isUInt<0> doesn't make sense");
  return X < (UINT64_C(1) << (N));
}
template <unsigned N>
constexpr inline typename std::enable_if<N >= 64, bool>::type
isUInt(uint64_t X) {
  return true;
}

// Template specializations to get better code for common cases.
template <> constexpr inline bool isUInt<8>(uint64_t x) {
  return static_cast<uint8_t>(x) == x;
}
template <> constexpr inline bool isUInt<16>(uint64_t x) {
  return static_cast<uint16_t>(x) == x;
}
template <> constexpr inline bool isUInt<32>(uint64_t x) {
  return static_cast<uint32_t>(x) == x;
}

/// Checks if a unsigned integer is an N bit number shifted left by S.
template <unsigned N, unsigned S>
constexpr inline bool isShiftedUInt(uint64_t x) {
  static_assert(
      N > 0, "isShiftedUInt<0> doesn't make sense (refers to a 0-bit number)");
  static_assert(N + S <= 64,
                "isShiftedUInt<N, S> with N + S > 64 is too wide.");
  // Per the two static_asserts above, S must be strictly less than 64.  So
  // 1 << S is not undefined behavior.
  return isUInt<N + S>(x) && (x % (UINT64_C(1) << S) == 0);
}

/// Gets the maximum value for a N-bit unsigned integer.
inline uint64_t maxUIntN(uint64_t N) {
  assert(N > 0 && N <= 64 && "integer width out of range");

  // uint64_t(1) << 64 is undefined behavior, so we can't do
  //   (uint64_t(1) << N) - 1
  // without checking first that N != 64.  But this works and doesn't have a
  // branch.
  return UINT64_MAX >> (64 - N);
}

/// Gets the minimum value for a N-bit signed integer.
inline int64_t minIntN(int64_t N) {
  assert(N > 0 && N <= 64 && "integer width out of range");

  return -(UINT64_C(1)<<(N-1));
}

/// Gets the maximum value for a N-bit signed integer.
inline int64_t maxIntN(int64_t N) {
  assert(N > 0 && N <= 64 && "integer width out of range");

  // This relies on two's complement wraparound when N == 64, so we convert to
  // int64_t only at the very end to avoid UB.
  return (UINT64_C(1) << (N - 1)) - 1;
}

/// Checks if an unsigned integer fits into the given (dynamic) bit width.
inline bool isUIntN(unsigned N, uint64_t x) {
  return N >= 64 || x <= maxUIntN(N);
}

/// Checks if an signed integer fits into the given (dynamic) bit width.
inline bool isIntN(unsigned N, int64_t x) {
  return N >= 64 || (minIntN(N) <= x && x <= maxIntN(N));
}

/// Return true if the argument is a non-empty sequence of ones starting at the
/// least significant bit with the remainder zero (32 bit version).
/// Ex. isMask_32(0x0000FFFFU) == true.
constexpr inline bool isMask_32(uint32_t Value) {
  return Value && ((Value + 1) & Value) == 0;
}

/// Return true if the argument is a non-empty sequence of ones starting at the
/// least significant bit with the remainder zero (64 bit version).
constexpr inline bool isMask_64(uint64_t Value) {
  return Value && ((Value + 1) & Value) == 0;
}

/// Return true if the argument contains a non-empty sequence of ones with the
/// remainder zero (32 bit version.) Ex. isShiftedMask_32(0x0000FF00U) == true.
constexpr inline bool isShiftedMask_32(uint32_t Value) {
  return Value && isMask_32((Value - 1) | Value);
}

/// Return true if the argument contains a non-empty sequence of ones with the
/// remainder zero (64 bit version.)
constexpr inline bool isShiftedMask_64(uint64_t Value) {
  return Value && isMask_64((Value - 1) | Value);
}

/// Return true if the argument is a power of two > 0.
/// Ex. isPowerOf2_32(0x00100000U) == true (32 bit edition.)
constexpr inline bool isPowerOf2_32(uint32_t Value) {
  return Value && !(Value & (Value - 1));
}

/// Return true if the argument is a power of two > 0 (64 bit edition.)
constexpr inline bool isPowerOf2_64(uint64_t Value) {
  return Value && !(Value & (Value - 1));
}

/// Return a byte-swapped representation of the 16-bit argument.
inline uint16_t ByteSwap_16(uint16_t Value) {
  return sys::SwapByteOrder_16(Value);
}

/// Return a byte-swapped representation of the 32-bit argument.
inline uint32_t ByteSwap_32(uint32_t Value) {
  return sys::SwapByteOrder_32(Value);
}

/// Return a byte-swapped representation of the 64-bit argument.
inline uint64_t ByteSwap_64(uint64_t Value) {
  return sys::SwapByteOrder_64(Value);
}

/// Count the number of ones from the most significant bit to the first
/// zero bit.
///
/// Ex. countLeadingOnes(0xFF0FFF00) == 8.
/// Only unsigned integral types are allowed.
///
/// \param ZB the behavior on an input of all ones. Only ZB_Width and
/// ZB_Undefined are valid arguments.
template <typename T>
std::size_t countLeadingOnes(T Value, ZeroBehavior ZB = ZB_Width) {
  static_assert(std::numeric_limits<T>::is_integer &&
                    !std::numeric_limits<T>::is_signed,
                "Only unsigned integral types are allowed.");
  return countLeadingZeros<T>(~Value, ZB);
}

/// Count the number of ones from the least significant bit to the first
/// zero bit.
///
/// Ex. countTrailingOnes(0x00FF00FF) == 8.
/// Only unsigned integral types are allowed.
///
/// \param ZB the behavior on an input of all ones. Only ZB_Width and
/// ZB_Undefined are valid arguments.
template <typename T>
std::size_t countTrailingOnes(T Value, ZeroBehavior ZB = ZB_Width) {
  static_assert(std::numeric_limits<T>::is_integer &&
                    !std::numeric_limits<T>::is_signed,
                "Only unsigned integral types are allowed.");
  return countTrailingZeros<T>(~Value, ZB);
}

namespace detail {
template <typename T, std::size_t SizeOfT> struct PopulationCounter {
  static unsigned count(T Value) {
    // Generic version, forward to 32 bits.
    static_assert(SizeOfT <= 4, "Not implemented!");
#if __GNUC__ >= 4
    return __builtin_popcount(Value);
#else
    uint32_t v = Value;
    v = v - ((v >> 1) & 0x55555555);
    v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
    return ((v + (v >> 4) & 0xF0F0F0F) * 0x1010101) >> 24;
#endif
  }
};

template <typename T> struct PopulationCounter<T, 8> {
  static unsigned count(T Value) {
#if __GNUC__ >= 4
    return __builtin_popcountll(Value);
#else
    uint64_t v = Value;
    v = v - ((v >> 1) & 0x5555555555555555ULL);
    v = (v & 0x3333333333333333ULL) + ((v >> 2) & 0x3333333333333333ULL);
    v = (v + (v >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return unsigned((uint64_t)(v * 0x0101010101010101ULL) >> 56);
#endif
  }
};
} // namespace detail

/// Count the number of set bits in a value.
/// Ex. countPopulation(0xF000F000) = 8
/// Returns 0 if the word is zero.
template <typename T>
inline unsigned countPopulation(T Value) {
  static_assert(std::numeric_limits<T>::is_integer &&
                    !std::numeric_limits<T>::is_signed,
                "Only unsigned integral types are allowed.");
  return detail::PopulationCounter<T, sizeof(T)>::count(Value);
}

/// Return the log base 2 of the specified value.
inline double Log2(double Value) {
#if defined(__ANDROID_API__) && __ANDROID_API__ < 18
  return __builtin_log(Value) / __builtin_log(2.0);
#else
  return log2(Value);
#endif
}

/// Return the floor log base 2 of the specified value, -1 if the value is zero.
/// (32 bit edition.)
/// Ex. Log2_32(32) == 5, Log2_32(1) == 0, Log2_32(0) == -1, Log2_32(6) == 2
inline unsigned Log2_32(uint32_t Value) {
  return 31 - countLeadingZeros(Value);
}

/// Return the floor log base 2 of the specified value, -1 if the value is zero.
/// (64 bit edition.)
inline unsigned Log2_64(uint64_t Value) {
  return 63 - countLeadingZeros(Value);
}

/// Return the ceil log base 2 of the specified value, 32 if the value is zero.
/// (32 bit edition).
/// Ex. Log2_32_Ceil(32) == 5, Log2_32_Ceil(1) == 0, Log2_32_Ceil(6) == 3
inline unsigned Log2_32_Ceil(uint32_t Value) {
  return 32 - countLeadingZeros(Value - 1);
}

/// Return the ceil log base 2 of the specified value, 64 if the value is zero.
/// (64 bit edition.)
inline unsigned Log2_64_Ceil(uint64_t Value) {
  return 64 - countLeadingZeros(Value - 1);
}

/// Return the greatest common divisor of the values using Euclid's algorithm.
inline uint64_t GreatestCommonDivisor64(uint64_t A, uint64_t B) {
  while (B) {
    uint64_t T = B;
    B = A % B;
    A = T;
  }
  return A;
}

/// This function takes a 64-bit integer and returns the bit equivalent double.
inline double BitsToDouble(uint64_t Bits) {
  double D;
  static_assert(sizeof(uint64_t) == sizeof(double), "Unexpected type sizes");
  memcpy(&D, &Bits, sizeof(Bits));
  return D;
}

/// This function takes a 32-bit integer and returns the bit equivalent float.
inline float BitsToFloat(uint32_t Bits) {
  float F;
  static_assert(sizeof(uint32_t) == sizeof(float), "Unexpected type sizes");
  memcpy(&F, &Bits, sizeof(Bits));
  return F;
}

/// This function takes a double and returns the bit equivalent 64-bit integer.
/// Note that copying doubles around changes the bits of NaNs on some hosts,
/// notably x86, so this routine cannot be used if these bits are needed.
inline uint64_t DoubleToBits(double Double) {
  uint64_t Bits;
  static_assert(sizeof(uint64_t) == sizeof(double), "Unexpected type sizes");
  memcpy(&Bits, &Double, sizeof(Double));
  return Bits;
}

/// This function takes a float and returns the bit equivalent 32-bit integer.
/// Note that copying floats around changes the bits of NaNs on some hosts,
/// notably x86, so this routine cannot be used if these bits are needed.
inline uint32_t FloatToBits(float Float) {
  uint32_t Bits;
  static_assert(sizeof(uint32_t) == sizeof(float), "Unexpected type sizes");
  memcpy(&Bits, &Float, sizeof(Float));
  return Bits;
}

/// A and B are either alignments or offsets. Return the minimum alignment that
/// may be assumed after adding the two together.
constexpr inline uint64_t MinAlign(uint64_t A, uint64_t B) {
  // The largest power of 2 that divides both A and B.
  //
  // Replace "-Value" by "1+~Value" in the following commented code to avoid
  // MSVC warning C4146
  //    return (A | B) & -(A | B);
  return (A | B) & (1 + ~(A | B));
}

/// Aligns \c Addr to \c Alignment bytes, rounding up.
///
/// Alignment should be a power of two.  This method rounds up, so
/// alignAddr(7, 4) == 8 and alignAddr(8, 4) == 8.
inline uintptr_t alignAddr(const void *Addr, size_t Alignment) {
  assert(Alignment && isPowerOf2_64((uint64_t)Alignment) &&
         "Alignment is not a power of two!");

  assert((uintptr_t)Addr + Alignment - 1 >= (uintptr_t)Addr);

  return (((uintptr_t)Addr + Alignment - 1) & ~(uintptr_t)(Alignment - 1));
}

/// Returns the necessary adjustment for aligning \c Ptr to \c Alignment
/// bytes, rounding up.
inline size_t alignmentAdjustment(const void *Ptr, size_t Alignment) {
  return alignAddr(Ptr, Alignment) - (uintptr_t)Ptr;
}

/// Returns the next power of two (in 64-bits) that is strictly greater than A.
/// Returns zero on overflow.
inline uint64_t NextPowerOf2(uint64_t A) {
  A |= (A >> 1);
  A |= (A >> 2);
  A |= (A >> 4);
  A |= (A >> 8);
  A |= (A >> 16);
  A |= (A >> 32);
  return A + 1;
}

/// Returns the power of two which is less than or equal to the given value.
/// Essentially, it is a floor operation across the domain of powers of two.
inline uint64_t PowerOf2Floor(uint64_t A) {
  if (!A) return 0;
  return 1ull << (63 - countLeadingZeros(A, ZB_Undefined));
}

/// Returns the power of two which is greater than or equal to the given value.
/// Essentially, it is a ceil operation across the domain of powers of two.
inline uint64_t PowerOf2Ceil(uint64_t A) {
  if (!A)
    return 0;
  return NextPowerOf2(A - 1);
}

/// Returns the next integer (mod 2**64) that is greater than or equal to
/// \p Value and is a multiple of \p Align. \p Align must be non-zero.
///
/// If non-zero \p Skew is specified, the return value will be a minimal
/// integer that is greater than or equal to \p Value and equal to
/// \p Align * N + \p Skew for some integer N. If \p Skew is larger than
/// \p Align, its value is adjusted to '\p Skew mod \p Align'.
///
/// Examples:
/// \code
///   alignTo(5, 8) = 8
///   alignTo(17, 8) = 24
///   alignTo(~0LL, 8) = 0
///   alignTo(321, 255) = 510
///
///   alignTo(5, 8, 7) = 7
///   alignTo(17, 8, 1) = 17
///   alignTo(~0LL, 8, 3) = 3
///   alignTo(321, 255, 42) = 552
/// \endcode
inline uint64_t alignTo(uint64_t Value, uint64_t Align, uint64_t Skew = 0) {
  assert(Align != 0u && "Align can't be 0.");
  Skew %= Align;
  return (Value + Align - 1 - Skew) / Align * Align + Skew;
}

/// Returns the next integer (mod 2**64) that is greater than or equal to
/// \p Value and is a multiple of \c Align. \c Align must be non-zero.
template <uint64_t Align> constexpr inline uint64_t alignTo(uint64_t Value) {
  static_assert(Align != 0u, "Align must be non-zero");
  return (Value + Align - 1) / Align * Align;
}

/// Returns the integer ceil(Numerator / Denominator).
inline uint64_t divideCeil(uint64_t Numerator, uint64_t Denominator) {
  return alignTo(Numerator, Denominator) / Denominator;
}

/// \c alignTo for contexts where a constant expression is required.
/// \sa alignTo
///
/// \todo FIXME: remove when \c constexpr becomes really \c constexpr
template <uint64_t Align>
struct AlignTo {
  static_assert(Align != 0u, "Align must be non-zero");
  template <uint64_t Value>
  struct from_value {
    static const uint64_t value = (Value + Align - 1) / Align * Align;
  };
};

/// Returns the largest uint64_t less than or equal to \p Value and is
/// \p Skew mod \p Align. \p Align must be non-zero
inline uint64_t alignDown(uint64_t Value, uint64_t Align, uint64_t Skew = 0) {
  assert(Align != 0u && "Align can't be 0.");
  Skew %= Align;
  return (Value - Skew) / Align * Align + Skew;
}

/// Returns the offset to the next integer (mod 2**64) that is greater than
/// or equal to \p Value and is a multiple of \p Align. \p Align must be
/// non-zero.
inline uint64_t OffsetToAlignment(uint64_t Value, uint64_t Align) {
  return alignTo(Value, Align) - Value;
}

/// Sign-extend the number in the bottom B bits of X to a 32-bit integer.
/// Requires 0 < B <= 32.
template <unsigned B> constexpr inline int32_t SignExtend32(uint32_t X) {
  static_assert(B > 0, "Bit width can't be 0.");
  static_assert(B <= 32, "Bit width out of range.");
  return int32_t(X << (32 - B)) >> (32 - B);
}

/// Sign-extend the number in the bottom B bits of X to a 32-bit integer.
/// Requires 0 < B < 32.
inline int32_t SignExtend32(uint32_t X, unsigned B) {
  assert(B > 0 && "Bit width can't be 0.");
  assert(B <= 32 && "Bit width out of range.");
  return int32_t(X << (32 - B)) >> (32 - B);
}

/// Sign-extend the number in the bottom B bits of X to a 64-bit integer.
/// Requires 0 < B < 64.
template <unsigned B> constexpr inline int64_t SignExtend64(uint64_t x) {
  static_assert(B > 0, "Bit width can't be 0.");
  static_assert(B <= 64, "Bit width out of range.");
  return int64_t(x << (64 - B)) >> (64 - B);
}

/// Sign-extend the number in the bottom B bits of X to a 64-bit integer.
/// Requires 0 < B < 64.
inline int64_t SignExtend64(uint64_t X, unsigned B) {
  assert(B > 0 && "Bit width can't be 0.");
  assert(B <= 64 && "Bit width out of range.");
  return int64_t(X << (64 - B)) >> (64 - B);
}

/// Subtract two unsigned integers, X and Y, of type T and return the absolute
/// value of the result.
template <typename T>
typename std::enable_if<std::is_unsigned<T>::value, T>::type
AbsoluteDifference(T X, T Y) {
  return std::max(X, Y) - std::min(X, Y);
}

/// Add two unsigned integers, X and Y, of type T.  Clamp the result to the
/// maximum representable value of T on overflow.  ResultOverflowed indicates if
/// the result is larger than the maximum representable value of type T.
template <typename T>
typename std::enable_if<std::is_unsigned<T>::value, T>::type
SaturatingAdd(T X, T Y, bool *ResultOverflowed = nullptr) {
  bool Dummy;
  bool &Overflowed = ResultOverflowed ? *ResultOverflowed : Dummy;
  // Hacker's Delight, p. 29
  T Z = X + Y;
  Overflowed = (Z < X || Z < Y);
  if (Overflowed)
    return std::numeric_limits<T>::max();
  else
    return Z;
}

/// Multiply two unsigned integers, X and Y, of type T.  Clamp the result to the
/// maximum representable value of T on overflow.  ResultOverflowed indicates if
/// the result is larger than the maximum representable value of type T.
template <typename T>
typename std::enable_if<std::is_unsigned<T>::value, T>::type
SaturatingMultiply(T X, T Y, bool *ResultOverflowed = nullptr) {
  bool Dummy;
  bool &Overflowed = ResultOverflowed ? *ResultOverflowed : Dummy;

  // Hacker's Delight, p. 30 has a different algorithm, but we don't use that
  // because it fails for uint16_t (where multiplication can have undefined
  // behavior due to promotion to int), and requires a division in addition
  // to the multiplication.

  Overflowed = false;

  // Log2(Z) would be either Log2Z or Log2Z + 1.
  // Special case: if X or Y is 0, Log2_64 gives -1, and Log2Z
  // will necessarily be less than Log2Max as desired.
  int Log2Z = Log2_64(X) + Log2_64(Y);
  const T Max = std::numeric_limits<T>::max();
  int Log2Max = Log2_64(Max);
  if (Log2Z < Log2Max) {
    return X * Y;
  }
  if (Log2Z > Log2Max) {
    Overflowed = true;
    return Max;
  }

  // We're going to use the top bit, and maybe overflow one
  // bit past it. Multiply all but the bottom bit then add
  // that on at the end.
  T Z = (X >> 1) * Y;
  if (Z & ~(Max >> 1)) {
    Overflowed = true;
    return Max;
  }
  Z <<= 1;
  if (X & 1)
    return SaturatingAdd(Z, Y, ResultOverflowed);

  return Z;
}

/// Multiply two unsigned integers, X and Y, and add the unsigned integer, A to
/// the product. Clamp the result to the maximum representable value of T on
/// overflow. ResultOverflowed indicates if the result is larger than the
/// maximum representable value of type T.
template <typename T>
typename std::enable_if<std::is_unsigned<T>::value, T>::type
SaturatingMultiplyAdd(T X, T Y, T A, bool *ResultOverflowed = nullptr) {
  bool Dummy;
  bool &Overflowed = ResultOverflowed ? *ResultOverflowed : Dummy;

  T Product = SaturatingMultiply(X, Y, &Overflowed);
  if (Overflowed)
    return Product;

  return SaturatingAdd(A, Product, &Overflowed);
}

/// Use this rather than HUGE_VALF; the latter causes warnings on MSVC.
extern const float huge_valf;
} // End llvm namespace

#endif
