//===- llvm/Support/LEB128.h - [SU]LEB128 utility functions -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares some utility functions for encoding SLEB128 and
// ULEB128 values.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_LEB128_H
#define LLVM_SUPPORT_LEB128_H

#include "llvm/Support/raw_ostream.h"

namespace llvm {

/// Utility function to encode a SLEB128 value to an output stream. Returns
/// the length in bytes of the encoded value.
inline unsigned encodeSLEB128(int64_t Value, raw_ostream &OS,
                              unsigned PadTo = 0) {
  bool More;
  unsigned Count = 0;
  do {
    uint8_t Byte = Value & 0x7f;
    // NOTE: this assumes that this signed shift is an arithmetic right shift.
    Value >>= 7;
    More = !((((Value == 0 ) && ((Byte & 0x40) == 0)) ||
              ((Value == -1) && ((Byte & 0x40) != 0))));
    Count++;
    if (More || Count < PadTo)
      Byte |= 0x80; // Mark this byte to show that more bytes will follow.
    OS << char(Byte);
  } while (More);

  // Pad with 0x80 and emit a terminating byte at the end.
  if (Count < PadTo) {
    uint8_t PadValue = Value < 0 ? 0x7f : 0x00;
    for (; Count < PadTo - 1; ++Count)
      OS << char(PadValue | 0x80);
    OS << char(PadValue);
    Count++;
  }
  return Count;
}

/// Utility function to encode a SLEB128 value to a buffer. Returns
/// the length in bytes of the encoded value.
inline unsigned encodeSLEB128(int64_t Value, uint8_t *p, unsigned PadTo = 0) {
  uint8_t *orig_p = p;
  unsigned Count = 0;
  bool More;
  do {
    uint8_t Byte = Value & 0x7f;
    // NOTE: this assumes that this signed shift is an arithmetic right shift.
    Value >>= 7;
    More = !((((Value == 0 ) && ((Byte & 0x40) == 0)) ||
              ((Value == -1) && ((Byte & 0x40) != 0))));
    Count++;
    if (More || Count < PadTo)
      Byte |= 0x80; // Mark this byte to show that more bytes will follow.
    *p++ = Byte;
  } while (More);

  // Pad with 0x80 and emit a terminating byte at the end.
  if (Count < PadTo) {
    uint8_t PadValue = Value < 0 ? 0x7f : 0x00;
    for (; Count < PadTo - 1; ++Count)
      *p++ = (PadValue | 0x80);
    *p++ = PadValue;
  }
  return (unsigned)(p - orig_p);
}

/// Utility function to encode a ULEB128 value to an output stream. Returns
/// the length in bytes of the encoded value.
inline unsigned encodeULEB128(uint64_t Value, raw_ostream &OS,
                              unsigned PadTo = 0) {
  unsigned Count = 0;
  do {
    uint8_t Byte = Value & 0x7f;
    Value >>= 7;
    Count++;
    if (Value != 0 || Count < PadTo)
      Byte |= 0x80; // Mark this byte to show that more bytes will follow.
    OS << char(Byte);
  } while (Value != 0);

  // Pad with 0x80 and emit a null byte at the end.
  if (Count < PadTo) {
    for (; Count < PadTo - 1; ++Count)
      OS << '\x80';
    OS << '\x00';
    Count++;
  }
  return Count;
}

/// Utility function to encode a ULEB128 value to a buffer. Returns
/// the length in bytes of the encoded value.
inline unsigned encodeULEB128(uint64_t Value, uint8_t *p,
                              unsigned PadTo = 0) {
  uint8_t *orig_p = p;
  unsigned Count = 0;
  do {
    uint8_t Byte = Value & 0x7f;
    Value >>= 7;
    Count++;
    if (Value != 0 || Count < PadTo)
      Byte |= 0x80; // Mark this byte to show that more bytes will follow.
    *p++ = Byte;
  } while (Value != 0);

  // Pad with 0x80 and emit a null byte at the end.
  if (Count < PadTo) {
    for (; Count < PadTo - 1; ++Count)
      *p++ = '\x80';
    *p++ = '\x00';
  }

  return (unsigned)(p - orig_p);
}

/// Utility function to decode a ULEB128 value.
inline uint64_t decodeULEB128(const uint8_t *p, unsigned *n = nullptr,
                              const uint8_t *end = nullptr,
                              const char **error = nullptr) {
  const uint8_t *orig_p = p;
  uint64_t Value = 0;
  unsigned Shift = 0;
  if (error)
    *error = nullptr;
  do {
    if (end && p == end) {
      if (error)
        *error = "malformed uleb128, extends past end";
      if (n)
        *n = (unsigned)(p - orig_p);
      return 0;
    }
    uint64_t Slice = *p & 0x7f;
    if (Shift >= 64 || Slice << Shift >> Shift != Slice) {
      if (error)
        *error = "uleb128 too big for uint64";
      if (n)
        *n = (unsigned)(p - orig_p);
      return 0;
    }
    Value += uint64_t(*p & 0x7f) << Shift;
    Shift += 7;
  } while (*p++ >= 128);
  if (n)
    *n = (unsigned)(p - orig_p);
  return Value;
}

/// Utility function to decode a SLEB128 value.
inline int64_t decodeSLEB128(const uint8_t *p, unsigned *n = nullptr,
                             const uint8_t *end = nullptr,
                             const char **error = nullptr) {
  const uint8_t *orig_p = p;
  int64_t Value = 0;
  unsigned Shift = 0;
  uint8_t Byte;
  do {
    if (end && p == end) {
      if (error)
        *error = "malformed sleb128, extends past end";
      if (n)
        *n = (unsigned)(p - orig_p);
      return 0;
    }
    Byte = *p++;
    Value |= (int64_t(Byte & 0x7f) << Shift);
    Shift += 7;
  } while (Byte >= 128);
  // Sign extend negative numbers.
  if (Byte & 0x40)
    Value |= (-1ULL) << Shift;
  if (n)
    *n = (unsigned)(p - orig_p);
  return Value;
}

/// Utility function to get the size of the ULEB128-encoded value.
extern unsigned getULEB128Size(uint64_t Value);

/// Utility function to get the size of the SLEB128-encoded value.
extern unsigned getSLEB128Size(int64_t Value);

} // namespace llvm

#endif // LLVM_SYSTEM_LEB128_H
