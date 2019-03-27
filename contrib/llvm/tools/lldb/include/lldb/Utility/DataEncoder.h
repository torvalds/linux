//===-- DataEncoder.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_DataEncoder_h_
#define liblldb_DataEncoder_h_

#if defined(__cplusplus)

#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"

#include <stddef.h>
#include <stdint.h>

namespace lldb_private {

//----------------------------------------------------------------------
/// @class DataEncoder DataEncoder.h "lldb/Core/DataEncoder.h" An binary data
/// encoding class.
///
/// DataEncoder is a class that can encode binary data (swapping if needed) to
/// a data buffer. The data buffer can be caller owned, or can be shared data
/// that can be shared between multiple DataEncoder or DataEncoder instances.
///
/// @see DataBuffer
//----------------------------------------------------------------------
class DataEncoder {
public:
  //------------------------------------------------------------------
  /// Default constructor.
  ///
  /// Initialize all members to a default empty state.
  //------------------------------------------------------------------
  DataEncoder();

  //------------------------------------------------------------------
  /// Construct with a buffer that is owned by the caller.
  ///
  /// This constructor allows us to use data that is owned by the caller. The
  /// data must stay around as long as this object is valid.
  ///
  /// @param[in] data
  ///     A pointer to caller owned data.
  ///
  /// @param[in] data_length
  ///     The length in bytes of \a data.
  ///
  /// @param[in] byte_order
  ///     A byte order of the data that we are extracting from.
  ///
  /// @param[in] addr_size
  ///     A new address byte size value.
  //------------------------------------------------------------------
  DataEncoder(void *data, uint32_t data_length, lldb::ByteOrder byte_order,
              uint8_t addr_size);

  //------------------------------------------------------------------
  /// Construct with shared data.
  ///
  /// Copies the data shared pointer which adds a reference to the contained
  /// in \a data_sp. The shared data reference is reference counted to ensure
  /// the data lives as long as anyone still has a valid shared pointer to the
  /// data in \a data_sp.
  ///
  /// @param[in] data_sp
  ///     A shared pointer to data.
  ///
  /// @param[in] byte_order
  ///     A byte order of the data that we are extracting from.
  ///
  /// @param[in] addr_size
  ///     A new address byte size value.
  //------------------------------------------------------------------
  DataEncoder(const lldb::DataBufferSP &data_sp, lldb::ByteOrder byte_order,
              uint8_t addr_size);

  //------------------------------------------------------------------
  /// Destructor
  ///
  /// If this object contains a valid shared data reference, the reference
  /// count on the data will be decremented, and if zero, the data will be
  /// freed.
  //------------------------------------------------------------------
  ~DataEncoder();

  //------------------------------------------------------------------
  /// Clears the object state.
  ///
  /// Clears the object contents back to a default invalid state, and release
  /// any references to shared data that this object may contain.
  //------------------------------------------------------------------
  void Clear();

  //------------------------------------------------------------------
  /// Get the current address size.
  ///
  /// Return the size in bytes of any address values this object will extract.
  ///
  /// @return
  ///     The size in bytes of address values that will be extracted.
  //------------------------------------------------------------------
  uint8_t GetAddressByteSize() const { return m_addr_size; }

  //------------------------------------------------------------------
  /// Get the number of bytes contained in this object.
  ///
  /// @return
  ///     The total number of bytes of data this object refers to.
  //------------------------------------------------------------------
  size_t GetByteSize() const { return m_end - m_start; }

  //------------------------------------------------------------------
  /// Get the data end pointer.
  ///
  /// @return
  ///     Returns a pointer to the next byte contained in this
  ///     object's data, or NULL of there is no data in this object.
  //------------------------------------------------------------------
  uint8_t *GetDataEnd() { return m_end; }

  const uint8_t *GetDataEnd() const { return m_end; }

  //------------------------------------------------------------------
  /// Get the shared data offset.
  ///
  /// Get the offset of the first byte of data in the shared data (if any).
  ///
  /// @return
  ///     If this object contains shared data, this function returns
  ///     the offset in bytes into that shared data, zero otherwise.
  //------------------------------------------------------------------
  size_t GetSharedDataOffset() const;

  //------------------------------------------------------------------
  /// Get the current byte order value.
  ///
  /// @return
  ///     The current byte order value from this object's internal
  ///     state.
  //------------------------------------------------------------------
  lldb::ByteOrder GetByteOrder() const { return m_byte_order; }

  //------------------------------------------------------------------
  /// Get the data start pointer.
  ///
  /// @return
  ///     Returns a pointer to the first byte contained in this
  ///     object's data, or NULL of there is no data in this object.
  //------------------------------------------------------------------
  uint8_t *GetDataStart() { return m_start; }

  const uint8_t *GetDataStart() const { return m_start; }

  //------------------------------------------------------------------
  /// Encode unsigned integer values into the data at \a offset.
  ///
  /// @param[in] offset
  ///     The offset within the contained data at which to put the
  ///     data.
  ///
  /// @param[in] value
  ///     The value to encode into the data.
  ///
  /// @return
  ///     The next offset in the bytes of this data if the data
  ///     was successfully encoded, UINT32_MAX if the encoding failed.
  //------------------------------------------------------------------
  uint32_t PutU8(uint32_t offset, uint8_t value);

  uint32_t PutU16(uint32_t offset, uint16_t value);

  uint32_t PutU32(uint32_t offset, uint32_t value);

  uint32_t PutU64(uint32_t offset, uint64_t value);

  //------------------------------------------------------------------
  /// Encode an unsigned integer of size \a byte_size to \a offset.
  ///
  /// Encode a single integer value at \a offset and return the offset that
  /// follows the newly encoded integer when the data is successfully encoded
  /// into the existing data. There must be enough room in the data, else
  /// UINT32_MAX will be returned to indicate that encoding failed.
  ///
  /// @param[in] offset
  ///     The offset within the contained data at which to put the
  ///     encoded integer.
  ///
  /// @param[in] byte_size
  ///     The size in byte of the integer to encode.
  ///
  /// @param[in] value
  ///     The integer value to write. The least significant bytes of
  ///     the integer value will be written if the size is less than
  ///     8 bytes.
  ///
  /// @return
  ///     The next offset in the bytes of this data if the integer
  ///     was successfully encoded, UINT32_MAX if the encoding failed.
  //------------------------------------------------------------------
  uint32_t PutMaxU64(uint32_t offset, uint32_t byte_size, uint64_t value);

  //------------------------------------------------------------------
  /// Encode an arbitrary number of bytes.
  ///
  /// @param[in] offset
  ///     The offset in bytes into the contained data at which to
  ///     start encoding.
  ///
  /// @param[in] src
  ///     The buffer that contains the bytes to encode.
  ///
  /// @param[in] src_len
  ///     The number of bytes to encode.
  ///
  /// @return
  ///     The next valid offset within data if the put operation
  ///     was successful, else UINT32_MAX to indicate the put failed.
  //------------------------------------------------------------------
  uint32_t PutData(uint32_t offset, const void *src, uint32_t src_len);

  //------------------------------------------------------------------
  /// Encode an address in the existing buffer at \a offset bytes into the
  /// buffer.
  ///
  /// Encode a single address (honoring the m_addr_size member) to the data
  /// and return the next offset where subsequent data would go. pointed to by
  /// \a offset_ptr. The size of the extracted address comes from the \a
  /// m_addr_size member variable and should be set correctly prior to
  /// extracting any address values.
  ///
  /// @param[in,out] offset_ptr
  ///     A pointer to an offset within the data that will be advanced
  ///     by the appropriate number of bytes if the value is extracted
  ///     correctly. If the offset is out of bounds or there are not
  ///     enough bytes to extract this value, the offset will be left
  ///     unmodified.
  ///
  /// @return
  ///     The next valid offset within data if the put operation
  ///     was successful, else UINT32_MAX to indicate the put failed.
  //------------------------------------------------------------------
  uint32_t PutAddress(uint32_t offset, lldb::addr_t addr);

  //------------------------------------------------------------------
  /// Put a C string to \a offset.
  ///
  /// Encodes a C string into the existing data including the terminating
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
  //------------------------------------------------------------------
  uint32_t PutCString(uint32_t offset_ptr, const char *cstr);

  lldb::DataBufferSP &GetSharedDataBuffer() { return m_data_sp; }

  //------------------------------------------------------------------
  /// Set the address byte size.
  ///
  /// Set the size in bytes that will be used when extracting any address and
  /// pointer values from data contained in this object.
  ///
  /// @param[in] addr_size
  ///     The size in bytes to use when extracting addresses.
  //------------------------------------------------------------------
  void SetAddressByteSize(uint8_t addr_size) { m_addr_size = addr_size; }

  //------------------------------------------------------------------
  /// Set data with a buffer that is caller owned.
  ///
  /// Use data that is owned by the caller when extracting values. The data
  /// must stay around as long as this object, or any object that copies a
  /// subset of this object's data, is valid. If \a bytes is NULL, or \a
  /// length is zero, this object will contain no data.
  ///
  /// @param[in] bytes
  ///     A pointer to caller owned data.
  ///
  /// @param[in] length
  ///     The length in bytes of \a bytes.
  ///
  /// @param[in] byte_order
  ///     A byte order of the data that we are extracting from.
  ///
  /// @return
  ///     The number of bytes that this object now contains.
  //------------------------------------------------------------------
  uint32_t SetData(void *bytes, uint32_t length, lldb::ByteOrder byte_order);

  //------------------------------------------------------------------
  /// Adopt a subset of shared data in \a data_sp.
  ///
  /// Copies the data shared pointer which adds a reference to the contained
  /// in \a data_sp. The shared data reference is reference counted to ensure
  /// the data lives as long as anyone still has a valid shared pointer to the
  /// data in \a data_sp. The byte order and address byte size settings remain
  /// the same. If \a offset is not a valid offset in \a data_sp, then no
  /// reference to the shared data will be added. If there are not \a length
  /// bytes available in \a data starting at \a offset, the length will be
  /// truncated to contains as many bytes as possible.
  ///
  /// @param[in] data_sp
  ///     A shared pointer to data.
  ///
  /// @param[in] offset
  ///     The offset into \a data_sp at which the subset starts.
  ///
  /// @param[in] length
  ///     The length in bytes of the subset of \a data_sp.
  ///
  /// @return
  ///     The number of bytes that this object now contains.
  //------------------------------------------------------------------
  uint32_t SetData(const lldb::DataBufferSP &data_sp, uint32_t offset = 0,
                   uint32_t length = UINT32_MAX);

  //------------------------------------------------------------------
  /// Set the byte_order value.
  ///
  /// Sets the byte order of the data to extract. Extracted values will be
  /// swapped if necessary when decoding.
  ///
  /// @param[in] byte_order
  ///     The byte order value to use when extracting data.
  //------------------------------------------------------------------
  void SetByteOrder(lldb::ByteOrder byte_order) { m_byte_order = byte_order; }

  //------------------------------------------------------------------
  /// Test the validity of \a offset.
  ///
  /// @return
  ///     \b true if \a offset is a valid offset into the data in this
  ///     object, \b false otherwise.
  //------------------------------------------------------------------
  bool ValidOffset(uint32_t offset) const { return offset < GetByteSize(); }

  //------------------------------------------------------------------
  /// Test the availability of \a length bytes of data from \a offset.
  ///
  /// @return
  ///     \b true if \a offset is a valid offset and there are \a
  ///     length bytes available at that offset, \b false otherwise.
  //------------------------------------------------------------------
  bool ValidOffsetForDataOfSize(uint32_t offset, uint32_t length) const {
    return length <= BytesLeft(offset);
  }

  uint32_t BytesLeft(uint32_t offset) const {
    const uint32_t size = GetByteSize();
    if (size > offset)
      return size - offset;
    return 0;
  }

protected:
  //------------------------------------------------------------------
  // Member variables
  //------------------------------------------------------------------
  uint8_t *m_start; ///< A pointer to the first byte of data.
  uint8_t *m_end;   ///< A pointer to the byte that is past the end of the data.
  lldb::ByteOrder
      m_byte_order;    ///< The byte order of the data we are extracting from.
  uint8_t m_addr_size; ///< The address size to use when extracting pointers or
                       /// addresses
  mutable lldb::DataBufferSP m_data_sp; ///< The shared pointer to data that can
                                        /// be shared among multiple instances

private:
  DISALLOW_COPY_AND_ASSIGN(DataEncoder);
};

} // namespace lldb_private

#endif // #if defined (__cplusplus)
#endif // #ifndef liblldb_DataEncoder_h_
