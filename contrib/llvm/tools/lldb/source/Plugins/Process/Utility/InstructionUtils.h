//===-- InstructionUtils.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_InstructionUtils_h_
#define lldb_InstructionUtils_h_

#include <cassert>
#include <cstdint>

// Common utilities for manipulating instruction bit fields.

namespace lldb_private {

// Return the bit field(s) from the most significant bit (msbit) to the
// least significant bit (lsbit) of a 64-bit unsigned value.
static inline uint64_t Bits64(const uint64_t bits, const uint32_t msbit,
                              const uint32_t lsbit) {
  assert(msbit < 64 && lsbit <= msbit);
  return (bits >> lsbit) & ((1ull << (msbit - lsbit + 1)) - 1);
}

// Return the bit field(s) from the most significant bit (msbit) to the
// least significant bit (lsbit) of a 32-bit unsigned value.
static inline uint32_t Bits32(const uint32_t bits, const uint32_t msbit,
                              const uint32_t lsbit) {
  assert(msbit < 32 && lsbit <= msbit);
  return (bits >> lsbit) & ((1u << (msbit - lsbit + 1)) - 1);
}

// Return the bit value from the 'bit' position of a 32-bit unsigned value.
static inline uint32_t Bit32(const uint32_t bits, const uint32_t bit) {
  return (bits >> bit) & 1u;
}

static inline uint64_t Bit64(const uint64_t bits, const uint32_t bit) {
  return (bits >> bit) & 1ull;
}

// Set the bit field(s) from the most significant bit (msbit) to the
// least significant bit (lsbit) of a 32-bit unsigned value to 'val'.
static inline void SetBits32(uint32_t &bits, const uint32_t msbit,
                             const uint32_t lsbit, const uint32_t val) {
  assert(msbit < 32 && lsbit < 32 && msbit >= lsbit);
  uint32_t mask = ((1u << (msbit - lsbit + 1)) - 1);
  bits &= ~(mask << lsbit);
  bits |= (val & mask) << lsbit;
}

// Set the 'bit' position of a 32-bit unsigned value to 'val'.
static inline void SetBit32(uint32_t &bits, const uint32_t bit,
                            const uint32_t val) {
  SetBits32(bits, bit, bit, val);
}

// Rotate a 32-bit unsigned value right by the specified amount.
static inline uint32_t Rotr32(uint32_t bits, uint32_t amt) {
  assert(amt < 32 && "Invalid rotate amount");
  return (bits >> amt) | (bits << ((32 - amt) & 31));
}

// Rotate a 32-bit unsigned value left by the specified amount.
static inline uint32_t Rotl32(uint32_t bits, uint32_t amt) {
  assert(amt < 32 && "Invalid rotate amount");
  return (bits << amt) | (bits >> ((32 - amt) & 31));
}

// Create a mask that starts at bit zero and includes "bit"
static inline uint64_t MaskUpToBit(const uint64_t bit) {
  if (bit >= 63)
    return -1ll;
  return (1ull << (bit + 1ull)) - 1ull;
}

// Return an integer result equal to the number of bits of x that are ones.
static inline uint32_t BitCount(uint64_t x) {
  // c accumulates the total bits set in x
  uint32_t c;
  for (c = 0; x; ++c) {
    x &= x - 1; // clear the least significant bit set
  }
  return c;
}

static inline bool BitIsSet(const uint64_t value, const uint64_t bit) {
  return (value & (1ull << bit)) != 0;
}

static inline bool BitIsClear(const uint64_t value, const uint64_t bit) {
  return (value & (1ull << bit)) == 0;
}

static inline uint64_t UnsignedBits(const uint64_t value, const uint64_t msbit,
                                    const uint64_t lsbit) {
  uint64_t result = value >> lsbit;
  result &= MaskUpToBit(msbit - lsbit);
  return result;
}

static inline int64_t SignedBits(const uint64_t value, const uint64_t msbit,
                                 const uint64_t lsbit) {
  uint64_t result = UnsignedBits(value, msbit, lsbit);
  if (BitIsSet(value, msbit)) {
    // Sign extend
    result |= ~MaskUpToBit(msbit - lsbit);
  }
  return result;
}

} // namespace lldb_private

#endif // lldb_InstructionUtils_h_
