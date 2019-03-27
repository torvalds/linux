//===- Endian.h - Utilities for IO with endian specific data ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares generic functions to read and write endian specific data.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ENDIAN_H
#define LLVM_SUPPORT_ENDIAN_H

#include "llvm/Support/AlignOf.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/SwapByteOrder.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace llvm {
namespace support {

enum endianness {big, little, native};

// These are named values for common alignments.
enum {aligned = 0, unaligned = 1};

namespace detail {

/// ::value is either alignment, or alignof(T) if alignment is 0.
template<class T, int alignment>
struct PickAlignment {
 enum { value = alignment == 0 ? alignof(T) : alignment };
};

} // end namespace detail

namespace endian {

constexpr endianness system_endianness() {
  return sys::IsBigEndianHost ? big : little;
}

template <typename value_type>
inline value_type byte_swap(value_type value, endianness endian) {
  if ((endian != native) && (endian != system_endianness()))
    sys::swapByteOrder(value);
  return value;
}

/// Swap the bytes of value to match the given endianness.
template<typename value_type, endianness endian>
inline value_type byte_swap(value_type value) {
  return byte_swap(value, endian);
}

/// Read a value of a particular endianness from memory.
template <typename value_type, std::size_t alignment>
inline value_type read(const void *memory, endianness endian) {
  value_type ret;

  memcpy(&ret,
         LLVM_ASSUME_ALIGNED(
             memory, (detail::PickAlignment<value_type, alignment>::value)),
         sizeof(value_type));
  return byte_swap<value_type>(ret, endian);
}

template<typename value_type,
         endianness endian,
         std::size_t alignment>
inline value_type read(const void *memory) {
  return read<value_type, alignment>(memory, endian);
}

/// Read a value of a particular endianness from a buffer, and increment the
/// buffer past that value.
template <typename value_type, std::size_t alignment, typename CharT>
inline value_type readNext(const CharT *&memory, endianness endian) {
  value_type ret = read<value_type, alignment>(memory, endian);
  memory += sizeof(value_type);
  return ret;
}

template<typename value_type, endianness endian, std::size_t alignment,
         typename CharT>
inline value_type readNext(const CharT *&memory) {
  return readNext<value_type, alignment, CharT>(memory, endian);
}

/// Write a value to memory with a particular endianness.
template <typename value_type, std::size_t alignment>
inline void write(void *memory, value_type value, endianness endian) {
  value = byte_swap<value_type>(value, endian);
  memcpy(LLVM_ASSUME_ALIGNED(
             memory, (detail::PickAlignment<value_type, alignment>::value)),
         &value, sizeof(value_type));
}

template<typename value_type,
         endianness endian,
         std::size_t alignment>
inline void write(void *memory, value_type value) {
  write<value_type, alignment>(memory, value, endian);
}

template <typename value_type>
using make_unsigned_t = typename std::make_unsigned<value_type>::type;

/// Read a value of a particular endianness from memory, for a location
/// that starts at the given bit offset within the first byte.
template <typename value_type, endianness endian, std::size_t alignment>
inline value_type readAtBitAlignment(const void *memory, uint64_t startBit) {
  assert(startBit < 8);
  if (startBit == 0)
    return read<value_type, endian, alignment>(memory);
  else {
    // Read two values and compose the result from them.
    value_type val[2];
    memcpy(&val[0],
           LLVM_ASSUME_ALIGNED(
               memory, (detail::PickAlignment<value_type, alignment>::value)),
           sizeof(value_type) * 2);
    val[0] = byte_swap<value_type, endian>(val[0]);
    val[1] = byte_swap<value_type, endian>(val[1]);

    // Shift bits from the lower value into place.
    make_unsigned_t<value_type> lowerVal = val[0] >> startBit;
    // Mask off upper bits after right shift in case of signed type.
    make_unsigned_t<value_type> numBitsFirstVal =
        (sizeof(value_type) * 8) - startBit;
    lowerVal &= ((make_unsigned_t<value_type>)1 << numBitsFirstVal) - 1;

    // Get the bits from the upper value.
    make_unsigned_t<value_type> upperVal =
        val[1] & (((make_unsigned_t<value_type>)1 << startBit) - 1);
    // Shift them in to place.
    upperVal <<= numBitsFirstVal;

    return lowerVal | upperVal;
  }
}

/// Write a value to memory with a particular endianness, for a location
/// that starts at the given bit offset within the first byte.
template <typename value_type, endianness endian, std::size_t alignment>
inline void writeAtBitAlignment(void *memory, value_type value,
                                uint64_t startBit) {
  assert(startBit < 8);
  if (startBit == 0)
    write<value_type, endian, alignment>(memory, value);
  else {
    // Read two values and shift the result into them.
    value_type val[2];
    memcpy(&val[0],
           LLVM_ASSUME_ALIGNED(
               memory, (detail::PickAlignment<value_type, alignment>::value)),
           sizeof(value_type) * 2);
    val[0] = byte_swap<value_type, endian>(val[0]);
    val[1] = byte_swap<value_type, endian>(val[1]);

    // Mask off any existing bits in the upper part of the lower value that
    // we want to replace.
    val[0] &= ((make_unsigned_t<value_type>)1 << startBit) - 1;
    make_unsigned_t<value_type> numBitsFirstVal =
        (sizeof(value_type) * 8) - startBit;
    make_unsigned_t<value_type> lowerVal = value;
    if (startBit > 0) {
      // Mask off the upper bits in the new value that are not going to go into
      // the lower value. This avoids a left shift of a negative value, which
      // is undefined behavior.
      lowerVal &= (((make_unsigned_t<value_type>)1 << numBitsFirstVal) - 1);
      // Now shift the new bits into place
      lowerVal <<= startBit;
    }
    val[0] |= lowerVal;

    // Mask off any existing bits in the lower part of the upper value that
    // we want to replace.
    val[1] &= ~(((make_unsigned_t<value_type>)1 << startBit) - 1);
    // Next shift the bits that go into the upper value into position.
    make_unsigned_t<value_type> upperVal = value >> numBitsFirstVal;
    // Mask off upper bits after right shift in case of signed type.
    upperVal &= ((make_unsigned_t<value_type>)1 << startBit) - 1;
    val[1] |= upperVal;

    // Finally, rewrite values.
    val[0] = byte_swap<value_type, endian>(val[0]);
    val[1] = byte_swap<value_type, endian>(val[1]);
    memcpy(LLVM_ASSUME_ALIGNED(
               memory, (detail::PickAlignment<value_type, alignment>::value)),
           &val[0], sizeof(value_type) * 2);
  }
}

} // end namespace endian

namespace detail {

template<typename value_type,
         endianness endian,
         std::size_t alignment>
struct packed_endian_specific_integral {
  packed_endian_specific_integral() = default;

  explicit packed_endian_specific_integral(value_type val) { *this = val; }

  operator value_type() const {
    return endian::read<value_type, endian, alignment>(
      (const void*)Value.buffer);
  }

  void operator=(value_type newValue) {
    endian::write<value_type, endian, alignment>(
      (void*)Value.buffer, newValue);
  }

  packed_endian_specific_integral &operator+=(value_type newValue) {
    *this = *this + newValue;
    return *this;
  }

  packed_endian_specific_integral &operator-=(value_type newValue) {
    *this = *this - newValue;
    return *this;
  }

  packed_endian_specific_integral &operator|=(value_type newValue) {
    *this = *this | newValue;
    return *this;
  }

  packed_endian_specific_integral &operator&=(value_type newValue) {
    *this = *this & newValue;
    return *this;
  }

private:
  AlignedCharArray<PickAlignment<value_type, alignment>::value,
                   sizeof(value_type)> Value;

public:
  struct ref {
    explicit ref(void *Ptr) : Ptr(Ptr) {}

    operator value_type() const {
      return endian::read<value_type, endian, alignment>(Ptr);
    }

    void operator=(value_type NewValue) {
      endian::write<value_type, endian, alignment>(Ptr, NewValue);
    }

  private:
    void *Ptr;
  };
};

} // end namespace detail

using ulittle16_t =
    detail::packed_endian_specific_integral<uint16_t, little, unaligned>;
using ulittle32_t =
    detail::packed_endian_specific_integral<uint32_t, little, unaligned>;
using ulittle64_t =
    detail::packed_endian_specific_integral<uint64_t, little, unaligned>;

using little16_t =
    detail::packed_endian_specific_integral<int16_t, little, unaligned>;
using little32_t =
    detail::packed_endian_specific_integral<int32_t, little, unaligned>;
using little64_t =
    detail::packed_endian_specific_integral<int64_t, little, unaligned>;

using aligned_ulittle16_t =
    detail::packed_endian_specific_integral<uint16_t, little, aligned>;
using aligned_ulittle32_t =
    detail::packed_endian_specific_integral<uint32_t, little, aligned>;
using aligned_ulittle64_t =
    detail::packed_endian_specific_integral<uint64_t, little, aligned>;

using aligned_little16_t =
    detail::packed_endian_specific_integral<int16_t, little, aligned>;
using aligned_little32_t =
    detail::packed_endian_specific_integral<int32_t, little, aligned>;
using aligned_little64_t =
    detail::packed_endian_specific_integral<int64_t, little, aligned>;

using ubig16_t =
    detail::packed_endian_specific_integral<uint16_t, big, unaligned>;
using ubig32_t =
    detail::packed_endian_specific_integral<uint32_t, big, unaligned>;
using ubig64_t =
    detail::packed_endian_specific_integral<uint64_t, big, unaligned>;

using big16_t =
    detail::packed_endian_specific_integral<int16_t, big, unaligned>;
using big32_t =
    detail::packed_endian_specific_integral<int32_t, big, unaligned>;
using big64_t =
    detail::packed_endian_specific_integral<int64_t, big, unaligned>;

using aligned_ubig16_t =
    detail::packed_endian_specific_integral<uint16_t, big, aligned>;
using aligned_ubig32_t =
    detail::packed_endian_specific_integral<uint32_t, big, aligned>;
using aligned_ubig64_t =
    detail::packed_endian_specific_integral<uint64_t, big, aligned>;

using aligned_big16_t =
    detail::packed_endian_specific_integral<int16_t, big, aligned>;
using aligned_big32_t =
    detail::packed_endian_specific_integral<int32_t, big, aligned>;
using aligned_big64_t =
    detail::packed_endian_specific_integral<int64_t, big, aligned>;

using unaligned_uint16_t =
    detail::packed_endian_specific_integral<uint16_t, native, unaligned>;
using unaligned_uint32_t =
    detail::packed_endian_specific_integral<uint32_t, native, unaligned>;
using unaligned_uint64_t =
    detail::packed_endian_specific_integral<uint64_t, native, unaligned>;

using unaligned_int16_t =
    detail::packed_endian_specific_integral<int16_t, native, unaligned>;
using unaligned_int32_t =
    detail::packed_endian_specific_integral<int32_t, native, unaligned>;
using unaligned_int64_t =
    detail::packed_endian_specific_integral<int64_t, native, unaligned>;

namespace endian {

template <typename T> inline T read(const void *P, endianness E) {
  return read<T, unaligned>(P, E);
}

template <typename T, endianness E> inline T read(const void *P) {
  return *(const detail::packed_endian_specific_integral<T, E, unaligned> *)P;
}

inline uint16_t read16(const void *P, endianness E) {
  return read<uint16_t>(P, E);
}
inline uint32_t read32(const void *P, endianness E) {
  return read<uint32_t>(P, E);
}
inline uint64_t read64(const void *P, endianness E) {
  return read<uint64_t>(P, E);
}

template <endianness E> inline uint16_t read16(const void *P) {
  return read<uint16_t, E>(P);
}
template <endianness E> inline uint32_t read32(const void *P) {
  return read<uint32_t, E>(P);
}
template <endianness E> inline uint64_t read64(const void *P) {
  return read<uint64_t, E>(P);
}

inline uint16_t read16le(const void *P) { return read16<little>(P); }
inline uint32_t read32le(const void *P) { return read32<little>(P); }
inline uint64_t read64le(const void *P) { return read64<little>(P); }
inline uint16_t read16be(const void *P) { return read16<big>(P); }
inline uint32_t read32be(const void *P) { return read32<big>(P); }
inline uint64_t read64be(const void *P) { return read64<big>(P); }

template <typename T> inline void write(void *P, T V, endianness E) {
  write<T, unaligned>(P, V, E);
}

template <typename T, endianness E> inline void write(void *P, T V) {
  *(detail::packed_endian_specific_integral<T, E, unaligned> *)P = V;
}

inline void write16(void *P, uint16_t V, endianness E) {
  write<uint16_t>(P, V, E);
}
inline void write32(void *P, uint32_t V, endianness E) {
  write<uint32_t>(P, V, E);
}
inline void write64(void *P, uint64_t V, endianness E) {
  write<uint64_t>(P, V, E);
}

template <endianness E> inline void write16(void *P, uint16_t V) {
  write<uint16_t, E>(P, V);
}
template <endianness E> inline void write32(void *P, uint32_t V) {
  write<uint32_t, E>(P, V);
}
template <endianness E> inline void write64(void *P, uint64_t V) {
  write<uint64_t, E>(P, V);
}

inline void write16le(void *P, uint16_t V) { write16<little>(P, V); }
inline void write32le(void *P, uint32_t V) { write32<little>(P, V); }
inline void write64le(void *P, uint64_t V) { write64<little>(P, V); }
inline void write16be(void *P, uint16_t V) { write16<big>(P, V); }
inline void write32be(void *P, uint32_t V) { write32<big>(P, V); }
inline void write64be(void *P, uint64_t V) { write64<big>(P, V); }

} // end namespace endian

} // end namespace support
} // end namespace llvm

#endif // LLVM_SUPPORT_ENDIAN_H
