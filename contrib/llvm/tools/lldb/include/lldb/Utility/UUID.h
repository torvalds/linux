//===-- UUID.h --------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_UUID_H
#define LLDB_UTILITY_UUID_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include "llvm/ADT/ArrayRef.h"

namespace llvm {
  class StringRef;
}

namespace lldb_private {

  class Stream;

class UUID {
public:
  UUID() = default;

  /// Creates a UUID from the data pointed to by the bytes argument. No special
  /// significance is attached to any of the values.
  static UUID fromData(const void *bytes, uint32_t num_bytes) {
    if (bytes)
      return fromData({reinterpret_cast<const uint8_t *>(bytes), num_bytes});
    return UUID();
  }

  /// Creates a uuid from the data pointed to by the bytes argument. No special
  /// significance is attached to any of the values.
  static UUID fromData(llvm::ArrayRef<uint8_t> bytes) { return UUID(bytes); }

  /// Creates a UUID from the data pointed to by the bytes argument. Data
  /// consisting purely of zero bytes is treated as an invalid UUID.
  static UUID fromOptionalData(const void *bytes, uint32_t num_bytes) {
    if (bytes)
      return fromOptionalData(
          {reinterpret_cast<const uint8_t *>(bytes), num_bytes});
    return UUID();
  }

  /// Creates a UUID from the data pointed to by the bytes argument. Data
  /// consisting purely of zero bytes is treated as an invalid UUID.
  static UUID fromOptionalData(llvm::ArrayRef<uint8_t> bytes) {
    if (llvm::all_of(bytes, [](uint8_t b) { return b == 0; }))
      return UUID();
    return UUID(bytes);
  }

  void Clear() { m_bytes.clear(); }

  void Dump(Stream *s) const;

  llvm::ArrayRef<uint8_t> GetBytes() const { return m_bytes; }

  explicit operator bool() const { return IsValid(); }
  bool IsValid() const { return !m_bytes.empty(); }

  std::string GetAsString(llvm::StringRef separator = "-") const;

  size_t SetFromStringRef(llvm::StringRef str, uint32_t num_uuid_bytes = 16);

  // Decode as many UUID bytes (up to 16) as possible from the C string "cstr"
  // This is used for auto completion where a partial UUID might have been
  // typed in. It
  //------------------------------------------------------------------
  /// Decode as many UUID bytes (up to 16) as possible from the C
  /// string \a cstr.
  ///
  /// @param[in] cstr
  ///     A NULL terminate C string that points at a UUID string value
  ///     (no leading spaces). The string must contain only hex
  ///     characters and optionally can contain the '-' sepearators.
  ///
  /// @param[in] uuid_bytes
  ///     A buffer of bytes that will contain a full or patially
  ///     decoded UUID.
  ///
  /// @return
  ///     The original string, with all decoded bytes removed.
  //------------------------------------------------------------------
  static llvm::StringRef
  DecodeUUIDBytesFromString(llvm::StringRef str,
                            llvm::SmallVectorImpl<uint8_t> &uuid_bytes,
                            uint32_t num_uuid_bytes = 16);

private:
  UUID(llvm::ArrayRef<uint8_t> bytes) : m_bytes(bytes.begin(), bytes.end()) {}

  // GNU ld generates 20-byte build-ids. Size chosen to avoid heap allocations
  // for this case.
  llvm::SmallVector<uint8_t, 20> m_bytes;

  friend bool operator==(const UUID &LHS, const UUID &RHS) {
    return LHS.m_bytes == RHS.m_bytes;
  }
  friend bool operator!=(const UUID &LHS, const UUID &RHS) {
    return !(LHS == RHS);
  }
  friend bool operator<(const UUID &LHS, const UUID &RHS) {
    return LHS.m_bytes < RHS.m_bytes;
  }
  friend bool operator<=(const UUID &LHS, const UUID &RHS) {
    return !(RHS < LHS);
  }
  friend bool operator>(const UUID &LHS, const UUID &RHS) { return RHS < LHS; }
  friend bool operator>=(const UUID &LHS, const UUID &RHS) {
    return !(LHS < RHS);
  }
};
} // namespace lldb_private

#endif // LLDB_UTILITY_UUID_H
