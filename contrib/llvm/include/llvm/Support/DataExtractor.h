//===-- DataExtractor.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_DATAEXTRACTOR_H
#define LLVM_SUPPORT_DATAEXTRACTOR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DataTypes.h"

namespace llvm {

/// An auxiliary type to facilitate extraction of 3-byte entities.
struct Uint24 {
  uint8_t Bytes[3];
  Uint24(uint8_t U) {
    Bytes[0] = Bytes[1] = Bytes[2] = U;
  }
  Uint24(uint8_t U0, uint8_t U1, uint8_t U2) {
    Bytes[0] = U0; Bytes[1] = U1; Bytes[2] = U2;
  }
  uint32_t getAsUint32(bool IsLittleEndian) const {
    int LoIx = IsLittleEndian ? 0 : 2;
    return Bytes[LoIx] + (Bytes[1] << 8) + (Bytes[2-LoIx] << 16);
  }
};

using uint24_t = Uint24;
static_assert(sizeof(uint24_t) == 3, "sizeof(uint24_t) != 3");

/// Needed by swapByteOrder().
inline uint24_t getSwappedBytes(uint24_t C) {
  return uint24_t(C.Bytes[2], C.Bytes[1], C.Bytes[0]);
}

class DataExtractor {
  StringRef Data;
  uint8_t IsLittleEndian;
  uint8_t AddressSize;
public:
  /// Construct with a buffer that is owned by the caller.
  ///
  /// This constructor allows us to use data that is owned by the
  /// caller. The data must stay around as long as this object is
  /// valid.
  DataExtractor(StringRef Data, bool IsLittleEndian, uint8_t AddressSize)
    : Data(Data), IsLittleEndian(IsLittleEndian), AddressSize(AddressSize) {}

  /// Get the data pointed to by this extractor.
  StringRef getData() const { return Data; }
  /// Get the endianness for this extractor.
  bool isLittleEndian() const { return IsLittleEndian; }
  /// Get the address size for this extractor.
  uint8_t getAddressSize() const { return AddressSize; }
  /// Set the address size for this extractor.
  void setAddressSize(uint8_t Size) { AddressSize = Size; }

  /// Extract a C string from \a *offset_ptr.
  ///
  /// Returns a pointer to a C String from the data at the offset
  /// pointed to by \a offset_ptr. A variable length NULL terminated C
  /// string will be extracted and the \a offset_ptr will be
  /// updated with the offset of the byte that follows the NULL
  /// terminator byte.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @return
  ///     A pointer to the C string value in the data. If the offset
  ///     pointed to by \a offset_ptr is out of bounds, or if the
  ///     offset plus the length of the C string is out of bounds,
  ///     NULL will be returned.
  const char *getCStr(uint32_t *offset_ptr) const;

  /// Extract a C string from \a *OffsetPtr.
  ///
  /// Returns a StringRef for the C String from the data at the offset
  /// pointed to by \a OffsetPtr. A variable length NULL terminated C
  /// string will be extracted and the \a OffsetPtr will be
  /// updated with the offset of the byte that follows the NULL
  /// terminator byte.
  ///
  /// \param[in,out] OffsetPtr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// \return
  ///     A StringRef for the C string value in the data. If the offset
  ///     pointed to by \a OffsetPtr is out of bounds, or if the
  ///     offset plus the length of the C string is out of bounds,
  ///     a default-initialized StringRef will be returned.
  StringRef getCStrRef(uint32_t *OffsetPtr) const;

  /// Extract an unsigned integer of size \a byte_size from \a
  /// *offset_ptr.
  ///
  /// Extract a single unsigned integer value and update the offset
  /// pointed to by \a offset_ptr. The size of the extracted integer
  /// is specified by the \a byte_size argument. \a byte_size should
  /// have a value greater than or equal to one and less than or equal
  /// to eight since the return value is 64 bits wide. Any
  /// \a byte_size values less than 1 or greater than 8 will result in
  /// nothing being extracted, and zero being returned.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @param[in] byte_size
  ///     The size in byte of the integer to extract.
  ///
  /// @return
  ///     The unsigned integer value that was extracted, or zero on
  ///     failure.
  uint64_t getUnsigned(uint32_t *offset_ptr, uint32_t byte_size) const;

  /// Extract an signed integer of size \a byte_size from \a *offset_ptr.
  ///
  /// Extract a single signed integer value (sign extending if required)
  /// and update the offset pointed to by \a offset_ptr. The size of
  /// the extracted integer is specified by the \a byte_size argument.
  /// \a byte_size should have a value greater than or equal to one
  /// and less than or equal to eight since the return value is 64
  /// bits wide. Any \a byte_size values less than 1 or greater than
  /// 8 will result in nothing being extracted, and zero being returned.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @param[in] size
  ///     The size in bytes of the integer to extract.
  ///
  /// @return
  ///     The sign extended signed integer value that was extracted,
  ///     or zero on failure.
  int64_t getSigned(uint32_t *offset_ptr, uint32_t size) const;

  //------------------------------------------------------------------
  /// Extract an pointer from \a *offset_ptr.
  ///
  /// Extract a single pointer from the data and update the offset
  /// pointed to by \a offset_ptr. The size of the extracted pointer
  /// is \a getAddressSize(), so the address size has to be
  /// set correctly prior to extracting any pointer values.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @return
  ///     The extracted pointer value as a 64 integer.
  uint64_t getAddress(uint32_t *offset_ptr) const {
    return getUnsigned(offset_ptr, AddressSize);
  }

  /// Extract a uint8_t value from \a *offset_ptr.
  ///
  /// Extract a single uint8_t from the binary data at the offset
  /// pointed to by \a offset_ptr, and advance the offset on success.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @return
  ///     The extracted uint8_t value.
  uint8_t getU8(uint32_t *offset_ptr) const;

  /// Extract \a count uint8_t values from \a *offset_ptr.
  ///
  /// Extract \a count uint8_t values from the binary data at the
  /// offset pointed to by \a offset_ptr, and advance the offset on
  /// success. The extracted values are copied into \a dst.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @param[out] dst
  ///     A buffer to copy \a count uint8_t values into. \a dst must
  ///     be large enough to hold all requested data.
  ///
  /// @param[in] count
  ///     The number of uint8_t values to extract.
  ///
  /// @return
  ///     \a dst if all values were properly extracted and copied,
  ///     NULL otherise.
  uint8_t *getU8(uint32_t *offset_ptr, uint8_t *dst, uint32_t count) const;

  //------------------------------------------------------------------
  /// Extract a uint16_t value from \a *offset_ptr.
  ///
  /// Extract a single uint16_t from the binary data at the offset
  /// pointed to by \a offset_ptr, and update the offset on success.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @return
  ///     The extracted uint16_t value.
  //------------------------------------------------------------------
  uint16_t getU16(uint32_t *offset_ptr) const;

  /// Extract \a count uint16_t values from \a *offset_ptr.
  ///
  /// Extract \a count uint16_t values from the binary data at the
  /// offset pointed to by \a offset_ptr, and advance the offset on
  /// success. The extracted values are copied into \a dst.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @param[out] dst
  ///     A buffer to copy \a count uint16_t values into. \a dst must
  ///     be large enough to hold all requested data.
  ///
  /// @param[in] count
  ///     The number of uint16_t values to extract.
  ///
  /// @return
  ///     \a dst if all values were properly extracted and copied,
  ///     NULL otherise.
  uint16_t *getU16(uint32_t *offset_ptr, uint16_t *dst, uint32_t count) const;

  /// Extract a 24-bit unsigned value from \a *offset_ptr and return it
  /// in a uint32_t.
  ///
  /// Extract 3 bytes from the binary data at the offset pointed to by
  /// \a offset_ptr, construct a uint32_t from them and update the offset
  /// on success.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the 3 bytes if the value is extracted correctly. If the offset
  ///     is out of bounds or there are not enough bytes to extract this value,
  ///     the offset will be left unmodified.
  ///
  /// @return
  ///     The extracted 24-bit value represented in a uint32_t.
  uint32_t getU24(uint32_t *offset_ptr) const;

  /// Extract a uint32_t value from \a *offset_ptr.
  ///
  /// Extract a single uint32_t from the binary data at the offset
  /// pointed to by \a offset_ptr, and update the offset on success.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @return
  ///     The extracted uint32_t value.
  uint32_t getU32(uint32_t *offset_ptr) const;

  /// Extract \a count uint32_t values from \a *offset_ptr.
  ///
  /// Extract \a count uint32_t values from the binary data at the
  /// offset pointed to by \a offset_ptr, and advance the offset on
  /// success. The extracted values are copied into \a dst.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @param[out] dst
  ///     A buffer to copy \a count uint32_t values into. \a dst must
  ///     be large enough to hold all requested data.
  ///
  /// @param[in] count
  ///     The number of uint32_t values to extract.
  ///
  /// @return
  ///     \a dst if all values were properly extracted and copied,
  ///     NULL otherise.
  uint32_t *getU32(uint32_t *offset_ptr, uint32_t *dst, uint32_t count) const;

  /// Extract a uint64_t value from \a *offset_ptr.
  ///
  /// Extract a single uint64_t from the binary data at the offset
  /// pointed to by \a offset_ptr, and update the offset on success.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @return
  ///     The extracted uint64_t value.
  uint64_t getU64(uint32_t *offset_ptr) const;

  /// Extract \a count uint64_t values from \a *offset_ptr.
  ///
  /// Extract \a count uint64_t values from the binary data at the
  /// offset pointed to by \a offset_ptr, and advance the offset on
  /// success. The extracted values are copied into \a dst.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @param[out] dst
  ///     A buffer to copy \a count uint64_t values into. \a dst must
  ///     be large enough to hold all requested data.
  ///
  /// @param[in] count
  ///     The number of uint64_t values to extract.
  ///
  /// @return
  ///     \a dst if all values were properly extracted and copied,
  ///     NULL otherise.
  uint64_t *getU64(uint32_t *offset_ptr, uint64_t *dst, uint32_t count) const;

  /// Extract a signed LEB128 value from \a *offset_ptr.
  ///
  /// Extracts an signed LEB128 number from this object's data
  /// starting at the offset pointed to by \a offset_ptr. The offset
  /// pointed to by \a offset_ptr will be updated with the offset of
  /// the byte following the last extracted byte.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @return
  ///     The extracted signed integer value.
  int64_t getSLEB128(uint32_t *offset_ptr) const;

  /// Extract a unsigned LEB128 value from \a *offset_ptr.
  ///
  /// Extracts an unsigned LEB128 number from this object's data
  /// starting at the offset pointed to by \a offset_ptr. The offset
  /// pointed to by \a offset_ptr will be updated with the offset of
  /// the byte following the last extracted byte.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @return
  ///     The extracted unsigned integer value.
  uint64_t getULEB128(uint32_t *offset_ptr) const;

  /// Test the validity of \a offset.
  ///
  /// @return
  ///     \b true if \a offset is a valid offset into the data in this
  ///     object, \b false otherwise.
  bool isValidOffset(uint32_t offset) const { return Data.size() > offset; }

  /// Test the availability of \a length bytes of data from \a offset.
  ///
  /// @return
  ///     \b true if \a offset is a valid offset and there are \a
  ///     length bytes available at that offset, \b false otherwise.
  bool isValidOffsetForDataOfSize(uint32_t offset, uint32_t length) const {
    return offset + length >= offset && isValidOffset(offset + length - 1);
  }

  /// Test the availability of enough bytes of data for a pointer from
  /// \a offset. The size of a pointer is \a getAddressSize().
  ///
  /// @return
  ///     \b true if \a offset is a valid offset and there are enough
  ///     bytes for a pointer available at that offset, \b false
  ///     otherwise.
  bool isValidOffsetForAddress(uint32_t offset) const {
    return isValidOffsetForDataOfSize(offset, AddressSize);
  }
};

} // namespace llvm

#endif
