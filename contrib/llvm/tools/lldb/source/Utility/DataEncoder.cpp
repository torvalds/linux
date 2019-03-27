//===-- DataEncoder.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/DataEncoder.h"

#include "lldb/Utility/DataBuffer.h"
#include "lldb/Utility/Endian.h"

#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"

#include <cassert>
#include <cstddef>

#include <string.h>

using namespace lldb;
using namespace lldb_private;
using namespace llvm::support::endian;

//----------------------------------------------------------------------
// Default constructor.
//----------------------------------------------------------------------
DataEncoder::DataEncoder()
    : m_start(nullptr), m_end(nullptr),
      m_byte_order(endian::InlHostByteOrder()), m_addr_size(sizeof(void *)),
      m_data_sp() {}

//----------------------------------------------------------------------
// This constructor allows us to use data that is owned by someone else. The
// data must stay around as long as this object is valid.
//----------------------------------------------------------------------
DataEncoder::DataEncoder(void *data, uint32_t length, ByteOrder endian,
                         uint8_t addr_size)
    : m_start((uint8_t *)data), m_end((uint8_t *)data + length),
      m_byte_order(endian), m_addr_size(addr_size), m_data_sp() {}

//----------------------------------------------------------------------
// Make a shared pointer reference to the shared data in "data_sp" and set the
// endian swapping setting to "swap", and the address size to "addr_size". The
// shared data reference will ensure the data lives as long as any DataEncoder
// objects exist that have a reference to this data.
//----------------------------------------------------------------------
DataEncoder::DataEncoder(const DataBufferSP &data_sp, ByteOrder endian,
                         uint8_t addr_size)
    : m_start(nullptr), m_end(nullptr), m_byte_order(endian),
      m_addr_size(addr_size), m_data_sp() {
  SetData(data_sp);
}

DataEncoder::~DataEncoder() = default;

//------------------------------------------------------------------
// Clears the object contents back to a default invalid state, and release any
// references to shared data that this object may contain.
//------------------------------------------------------------------
void DataEncoder::Clear() {
  m_start = nullptr;
  m_end = nullptr;
  m_byte_order = endian::InlHostByteOrder();
  m_addr_size = sizeof(void *);
  m_data_sp.reset();
}

//------------------------------------------------------------------
// If this object contains shared data, this function returns the offset into
// that shared data. Else zero is returned.
//------------------------------------------------------------------
size_t DataEncoder::GetSharedDataOffset() const {
  if (m_start != nullptr) {
    const DataBuffer *data = m_data_sp.get();
    if (data != nullptr) {
      const uint8_t *data_bytes = data->GetBytes();
      if (data_bytes != nullptr) {
        assert(m_start >= data_bytes);
        return m_start - data_bytes;
      }
    }
  }
  return 0;
}

//----------------------------------------------------------------------
// Set the data with which this object will extract from to data starting at
// BYTES and set the length of the data to LENGTH bytes long. The data is
// externally owned must be around at least as long as this object points to
// the data. No copy of the data is made, this object just refers to this data
// and can extract from it. If this object refers to any shared data upon
// entry, the reference to that data will be released. Is SWAP is set to true,
// any data extracted will be endian swapped.
//----------------------------------------------------------------------
uint32_t DataEncoder::SetData(void *bytes, uint32_t length, ByteOrder endian) {
  m_byte_order = endian;
  m_data_sp.reset();
  if (bytes == nullptr || length == 0) {
    m_start = nullptr;
    m_end = nullptr;
  } else {
    m_start = (uint8_t *)bytes;
    m_end = m_start + length;
  }
  return GetByteSize();
}

//----------------------------------------------------------------------
// Assign the data for this object to be a subrange of the shared data in
// "data_sp" starting "data_offset" bytes into "data_sp" and ending
// "data_length" bytes later. If "data_offset" is not a valid offset into
// "data_sp", then this object will contain no bytes. If "data_offset" is
// within "data_sp" yet "data_length" is too large, the length will be capped
// at the number of bytes remaining in "data_sp". A ref counted pointer to the
// data in "data_sp" will be made in this object IF the number of bytes this
// object refers to in greater than zero (if at least one byte was available
// starting at "data_offset") to ensure the data stays around as long as it is
// needed. The address size and endian swap settings will remain unchanged from
// their current settings.
//----------------------------------------------------------------------
uint32_t DataEncoder::SetData(const DataBufferSP &data_sp, uint32_t data_offset,
                              uint32_t data_length) {
  m_start = m_end = nullptr;

  if (data_length > 0) {
    m_data_sp = data_sp;
    if (data_sp) {
      const size_t data_size = data_sp->GetByteSize();
      if (data_offset < data_size) {
        m_start = data_sp->GetBytes() + data_offset;
        const size_t bytes_left = data_size - data_offset;
        // Cap the length of we asked for too many
        if (data_length <= bytes_left)
          m_end = m_start + data_length; // We got all the bytes we wanted
        else
          m_end = m_start + bytes_left; // Not all the bytes requested were
                                        // available in the shared data
      }
    }
  }

  uint32_t new_size = GetByteSize();

  // Don't hold a shared pointer to the data buffer if we don't share any valid
  // bytes in the shared buffer.
  if (new_size == 0)
    m_data_sp.reset();

  return new_size;
}

//----------------------------------------------------------------------
// Extract a single unsigned char from the binary data and update the offset
// pointed to by "offset_ptr".
//
// RETURNS the byte that was extracted, or zero on failure.
//----------------------------------------------------------------------
uint32_t DataEncoder::PutU8(uint32_t offset, uint8_t value) {
  if (ValidOffset(offset)) {
    m_start[offset] = value;
    return offset + 1;
  }
  return UINT32_MAX;
}

uint32_t DataEncoder::PutU16(uint32_t offset, uint16_t value) {
  if (ValidOffsetForDataOfSize(offset, sizeof(value))) {
    if (m_byte_order != endian::InlHostByteOrder())
      write16be(m_start + offset, value);
    else
      write16le(m_start + offset, value);

    return offset + sizeof(value);
  }
  return UINT32_MAX;
}

uint32_t DataEncoder::PutU32(uint32_t offset, uint32_t value) {
  if (ValidOffsetForDataOfSize(offset, sizeof(value))) {
    if (m_byte_order != endian::InlHostByteOrder())
      write32be(m_start + offset, value);
    else
      write32le(m_start + offset, value);

    return offset + sizeof(value);
  }
  return UINT32_MAX;
}

uint32_t DataEncoder::PutU64(uint32_t offset, uint64_t value) {
  if (ValidOffsetForDataOfSize(offset, sizeof(value))) {
    if (m_byte_order != endian::InlHostByteOrder())
      write64be(m_start + offset, value);
    else
      write64le(m_start + offset, value);

    return offset + sizeof(value);
  }
  return UINT32_MAX;
}

//----------------------------------------------------------------------
// Extract a single integer value from the data and update the offset pointed
// to by "offset_ptr". The size of the extracted integer is specified by the
// "byte_size" argument. "byte_size" should have a value >= 1 and <= 8 since
// the return value is only 64 bits wide. Any "byte_size" values less than 1 or
// greater than 8 will result in nothing being extracted, and zero being
// returned.
//
// RETURNS the integer value that was extracted, or zero on failure.
//----------------------------------------------------------------------
uint32_t DataEncoder::PutMaxU64(uint32_t offset, uint32_t byte_size,
                                uint64_t value) {
  switch (byte_size) {
  case 1:
    return PutU8(offset, value);
  case 2:
    return PutU16(offset, value);
  case 4:
    return PutU32(offset, value);
  case 8:
    return PutU64(offset, value);
  default:
    llvm_unreachable("GetMax64 unhandled case!");
  }
  return UINT32_MAX;
}

uint32_t DataEncoder::PutData(uint32_t offset, const void *src,
                              uint32_t src_len) {
  if (src == nullptr || src_len == 0)
    return offset;

  if (ValidOffsetForDataOfSize(offset, src_len)) {
    memcpy(m_start + offset, src, src_len);
    return offset + src_len;
  }
  return UINT32_MAX;
}

uint32_t DataEncoder::PutAddress(uint32_t offset, lldb::addr_t addr) {
  return PutMaxU64(offset, GetAddressByteSize(), addr);
}

uint32_t DataEncoder::PutCString(uint32_t offset, const char *cstr) {
  if (cstr != nullptr)
    return PutData(offset, cstr, strlen(cstr) + 1);
  return UINT32_MAX;
}
