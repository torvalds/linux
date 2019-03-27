//===-- RegisterValue.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/RegisterValue.h"

#include "lldb/Utility/Args.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-private-types.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

using namespace lldb;
using namespace lldb_private;

bool RegisterValue::GetData(DataExtractor &data) const {
  return data.SetData(GetBytes(), GetByteSize(), GetByteOrder()) > 0;
}

uint32_t RegisterValue::GetAsMemoryData(const RegisterInfo *reg_info, void *dst,
                                        uint32_t dst_len,
                                        lldb::ByteOrder dst_byte_order,
                                        Status &error) const {
  if (reg_info == nullptr) {
    error.SetErrorString("invalid register info argument.");
    return 0;
  }

  // ReadRegister should have already been called on this object prior to
  // calling this.
  if (GetType() == eTypeInvalid) {
    // No value has been read into this object...
    error.SetErrorStringWithFormat(
        "invalid register value type for register %s", reg_info->name);
    return 0;
  }

  if (dst_len > kMaxRegisterByteSize) {
    error.SetErrorString("destination is too big");
    return 0;
  }

  const uint32_t src_len = reg_info->byte_size;

  // Extract the register data into a data extractor
  DataExtractor reg_data;
  if (!GetData(reg_data)) {
    error.SetErrorString("invalid register value to copy into");
    return 0;
  }

  // Prepare a memory buffer that contains some or all of the register value
  const uint32_t bytes_copied =
      reg_data.CopyByteOrderedData(0,               // src offset
                                   src_len,         // src length
                                   dst,             // dst buffer
                                   dst_len,         // dst length
                                   dst_byte_order); // dst byte order
  if (bytes_copied == 0)
    error.SetErrorStringWithFormat(
        "failed to copy data for register write of %s", reg_info->name);

  return bytes_copied;
}

uint32_t RegisterValue::SetFromMemoryData(const RegisterInfo *reg_info,
                                          const void *src, uint32_t src_len,
                                          lldb::ByteOrder src_byte_order,
                                          Status &error) {
  if (reg_info == nullptr) {
    error.SetErrorString("invalid register info argument.");
    return 0;
  }

  // Moving from addr into a register
  //
  // Case 1: src_len == dst_len
  //
  //   |AABBCCDD| Address contents
  //   |AABBCCDD| Register contents
  //
  // Case 2: src_len > dst_len
  //
  //   Status!  (The register should always be big enough to hold the data)
  //
  // Case 3: src_len < dst_len
  //
  //   |AABB| Address contents
  //   |AABB0000| Register contents [on little-endian hardware]
  //   |0000AABB| Register contents [on big-endian hardware]
  if (src_len > kMaxRegisterByteSize) {
    error.SetErrorStringWithFormat(
        "register buffer is too small to receive %u bytes of data.", src_len);
    return 0;
  }

  const uint32_t dst_len = reg_info->byte_size;

  if (src_len > dst_len) {
    error.SetErrorStringWithFormat(
        "%u bytes is too big to store in register %s (%u bytes)", src_len,
        reg_info->name, dst_len);
    return 0;
  }

  // Use a data extractor to correctly copy and pad the bytes read into the
  // register value
  DataExtractor src_data(src, src_len, src_byte_order, 4);

  error = SetValueFromData(reg_info, src_data, 0, true);
  if (error.Fail())
    return 0;

  // If SetValueFromData succeeded, we must have copied all of src_len
  return src_len;
}

bool RegisterValue::GetScalarValue(Scalar &scalar) const {
  switch (m_type) {
  case eTypeInvalid:
    break;
  case eTypeBytes: {
    switch (buffer.length) {
    default:
      break;
    case 1:
      scalar = *(const uint8_t *)buffer.bytes;
      return true;
    case 2:
      scalar = *(const uint16_t *)buffer.bytes;
      return true;
    case 4:
      scalar = *(const uint32_t *)buffer.bytes;
      return true;
    case 8:
      scalar = *(const uint64_t *)buffer.bytes;
      return true;
    case 16:
    case 32:
      if (buffer.length % sizeof(uint64_t) == 0) {
        const auto length_in_bits = buffer.length * 8;
        const auto length_in_uint64 = buffer.length / sizeof(uint64_t);
        scalar =
            llvm::APInt(length_in_bits,
                        llvm::ArrayRef<uint64_t>((const uint64_t *)buffer.bytes,
                                                 length_in_uint64));
        return true;
      }
      break;
    }
  } break;
  case eTypeUInt8:
  case eTypeUInt16:
  case eTypeUInt32:
  case eTypeUInt64:
  case eTypeUInt128:
  case eTypeFloat:
  case eTypeDouble:
  case eTypeLongDouble:
    scalar = m_scalar;
    return true;
  }
  return false;
}

void RegisterValue::Clear() { m_type = eTypeInvalid; }

RegisterValue::Type RegisterValue::SetType(const RegisterInfo *reg_info) {
  // To change the type, we simply copy the data in again, using the new format
  RegisterValue copy;
  DataExtractor copy_data;
  if (copy.CopyValue(*this) && copy.GetData(copy_data))
    SetValueFromData(reg_info, copy_data, 0, true);

  return m_type;
}

Status RegisterValue::SetValueFromData(const RegisterInfo *reg_info,
                                       DataExtractor &src,
                                       lldb::offset_t src_offset,
                                       bool partial_data_ok) {
  Status error;

  if (src.GetByteSize() == 0) {
    error.SetErrorString("empty data.");
    return error;
  }

  if (reg_info->byte_size == 0) {
    error.SetErrorString("invalid register info.");
    return error;
  }

  uint32_t src_len = src.GetByteSize() - src_offset;

  if (!partial_data_ok && (src_len < reg_info->byte_size)) {
    error.SetErrorString("not enough data.");
    return error;
  }

  // Cap the data length if there is more than enough bytes for this register
  // value
  if (src_len > reg_info->byte_size)
    src_len = reg_info->byte_size;

  // Zero out the value in case we get partial data...
  memset(buffer.bytes, 0, sizeof(buffer.bytes));

  type128 int128;

  m_type = eTypeInvalid;
  switch (reg_info->encoding) {
  case eEncodingInvalid:
    break;
  case eEncodingUint:
  case eEncodingSint:
    if (reg_info->byte_size == 1)
      SetUInt8(src.GetMaxU32(&src_offset, src_len));
    else if (reg_info->byte_size <= 2)
      SetUInt16(src.GetMaxU32(&src_offset, src_len));
    else if (reg_info->byte_size <= 4)
      SetUInt32(src.GetMaxU32(&src_offset, src_len));
    else if (reg_info->byte_size <= 8)
      SetUInt64(src.GetMaxU64(&src_offset, src_len));
    else if (reg_info->byte_size <= 16) {
      uint64_t data1 = src.GetU64(&src_offset);
      uint64_t data2 = src.GetU64(&src_offset);
      if (src.GetByteSize() == eByteOrderBig) {
        int128.x[0] = data1;
        int128.x[1] = data2;
      } else {
        int128.x[0] = data2;
        int128.x[1] = data1;
      }
      SetUInt128(llvm::APInt(128, 2, int128.x));
    }
    break;
  case eEncodingIEEE754:
    if (reg_info->byte_size == sizeof(float))
      SetFloat(src.GetFloat(&src_offset));
    else if (reg_info->byte_size == sizeof(double))
      SetDouble(src.GetDouble(&src_offset));
    else if (reg_info->byte_size == sizeof(long double))
      SetLongDouble(src.GetLongDouble(&src_offset));
    break;
  case eEncodingVector: {
    m_type = eTypeBytes;
    buffer.length = reg_info->byte_size;
    buffer.byte_order = src.GetByteOrder();
    assert(buffer.length <= kMaxRegisterByteSize);
    if (buffer.length > kMaxRegisterByteSize)
      buffer.length = kMaxRegisterByteSize;
    if (src.CopyByteOrderedData(
            src_offset,    // offset within "src" to start extracting data
            src_len,       // src length
            buffer.bytes,  // dst buffer
            buffer.length, // dst length
            buffer.byte_order) == 0) // dst byte order
    {
      error.SetErrorStringWithFormat(
          "failed to copy data for register write of %s", reg_info->name);
      return error;
    }
  }
  }

  if (m_type == eTypeInvalid)
    error.SetErrorStringWithFormat(
        "invalid register value type for register %s", reg_info->name);
  return error;
}

// Helper function for RegisterValue::SetValueFromString()
static bool ParseVectorEncoding(const RegisterInfo *reg_info,
                                llvm::StringRef vector_str,
                                const uint32_t byte_size,
                                RegisterValue *reg_value) {
  // Example: vector_str = "{0x2c 0x4b 0x2a 0x3e 0xd0 0x4f 0x2a 0x3e 0xac 0x4a
  // 0x2a 0x3e 0x84 0x4f 0x2a 0x3e}".
  vector_str = vector_str.trim();
  vector_str.consume_front("{");
  vector_str.consume_back("}");
  vector_str = vector_str.trim();

  char Sep = ' ';

  // The first split should give us:
  // ('0x2c', '0x4b 0x2a 0x3e 0xd0 0x4f 0x2a 0x3e 0xac 0x4a 0x2a 0x3e 0x84 0x4f
  // 0x2a 0x3e').
  llvm::StringRef car;
  llvm::StringRef cdr = vector_str;
  std::tie(car, cdr) = vector_str.split(Sep);
  std::vector<uint8_t> bytes;
  unsigned byte = 0;

  // Using radix auto-sensing by passing 0 as the radix. Keep on processing the
  // vector elements as long as the parsing succeeds and the vector size is <
  // byte_size.
  while (!car.getAsInteger(0, byte) && bytes.size() < byte_size) {
    bytes.push_back(byte);
    std::tie(car, cdr) = cdr.split(Sep);
  }

  // Check for vector of exact byte_size elements.
  if (bytes.size() != byte_size)
    return false;

  reg_value->SetBytes(&(bytes.front()), byte_size, eByteOrderLittle);
  return true;
}

Status RegisterValue::SetValueFromString(const RegisterInfo *reg_info,
                                         llvm::StringRef value_str) {
  Status error;
  if (reg_info == nullptr) {
    error.SetErrorString("Invalid register info argument.");
    return error;
  }

  m_type = eTypeInvalid;
  if (value_str.empty()) {
    error.SetErrorString("Invalid c-string value string.");
    return error;
  }
  const uint32_t byte_size = reg_info->byte_size;

  uint64_t uval64;
  int64_t ival64;
  float flt_val;
  double dbl_val;
  long double ldbl_val;
  switch (reg_info->encoding) {
  case eEncodingInvalid:
    error.SetErrorString("Invalid encoding.");
    break;

  case eEncodingUint:
    if (byte_size > sizeof(uint64_t)) {
      error.SetErrorStringWithFormat(
          "unsupported unsigned integer byte size: %u", byte_size);
      break;
    }
    if (value_str.getAsInteger(0, uval64)) {
      error.SetErrorStringWithFormat(
          "'%s' is not a valid unsigned integer string value",
          value_str.str().c_str());
      break;
    }

    if (!Args::UInt64ValueIsValidForByteSize(uval64, byte_size)) {
      error.SetErrorStringWithFormat(
          "value 0x%" PRIx64
          " is too large to fit in a %u byte unsigned integer value",
          uval64, byte_size);
      break;
    }

    if (!SetUInt(uval64, reg_info->byte_size)) {
      error.SetErrorStringWithFormat(
          "unsupported unsigned integer byte size: %u", byte_size);
      break;
    }
    break;

  case eEncodingSint:
    if (byte_size > sizeof(long long)) {
      error.SetErrorStringWithFormat("unsupported signed integer byte size: %u",
                                     byte_size);
      break;
    }

    if (value_str.getAsInteger(0, ival64)) {
      error.SetErrorStringWithFormat(
          "'%s' is not a valid signed integer string value",
          value_str.str().c_str());
      break;
    }

    if (!Args::SInt64ValueIsValidForByteSize(ival64, byte_size)) {
      error.SetErrorStringWithFormat(
          "value 0x%" PRIx64
          " is too large to fit in a %u byte signed integer value",
          ival64, byte_size);
      break;
    }

    if (!SetUInt(ival64, reg_info->byte_size)) {
      error.SetErrorStringWithFormat("unsupported signed integer byte size: %u",
                                     byte_size);
      break;
    }
    break;

  case eEncodingIEEE754: {
    std::string value_string = value_str;
    if (byte_size == sizeof(float)) {
      if (::sscanf(value_string.c_str(), "%f", &flt_val) != 1) {
        error.SetErrorStringWithFormat("'%s' is not a valid float string value",
                                       value_string.c_str());
        break;
      }
      m_scalar = flt_val;
      m_type = eTypeFloat;
    } else if (byte_size == sizeof(double)) {
      if (::sscanf(value_string.c_str(), "%lf", &dbl_val) != 1) {
        error.SetErrorStringWithFormat("'%s' is not a valid float string value",
                                       value_string.c_str());
        break;
      }
      m_scalar = dbl_val;
      m_type = eTypeDouble;
    } else if (byte_size == sizeof(long double)) {
      if (::sscanf(value_string.c_str(), "%Lf", &ldbl_val) != 1) {
        error.SetErrorStringWithFormat("'%s' is not a valid float string value",
                                       value_string.c_str());
        break;
      }
      m_scalar = ldbl_val;
      m_type = eTypeLongDouble;
    } else {
      error.SetErrorStringWithFormat("unsupported float byte size: %u",
                                     byte_size);
      return error;
    }
    break;
  }
  case eEncodingVector:
    if (!ParseVectorEncoding(reg_info, value_str, byte_size, this))
      error.SetErrorString("unrecognized vector encoding string value.");
    break;
  }

  return error;
}

bool RegisterValue::SignExtend(uint32_t sign_bitpos) {
  switch (m_type) {
  case eTypeInvalid:
    break;

  case eTypeUInt8:
  case eTypeUInt16:
  case eTypeUInt32:
  case eTypeUInt64:
  case eTypeUInt128:
    return m_scalar.SignExtend(sign_bitpos);
  case eTypeFloat:
  case eTypeDouble:
  case eTypeLongDouble:
  case eTypeBytes:
    break;
  }
  return false;
}

bool RegisterValue::CopyValue(const RegisterValue &rhs) {
  if (this == &rhs)
    return rhs.m_type != eTypeInvalid;

  m_type = rhs.m_type;
  switch (m_type) {
  case eTypeInvalid:
    return false;
  case eTypeUInt8:
  case eTypeUInt16:
  case eTypeUInt32:
  case eTypeUInt64:
  case eTypeUInt128:
  case eTypeFloat:
  case eTypeDouble:
  case eTypeLongDouble:
    m_scalar = rhs.m_scalar;
    break;
  case eTypeBytes:
    assert(rhs.buffer.length <= kMaxRegisterByteSize);
    ::memcpy(buffer.bytes, rhs.buffer.bytes, kMaxRegisterByteSize);
    buffer.length = rhs.buffer.length;
    buffer.byte_order = rhs.buffer.byte_order;
    break;
  }
  return true;
}

uint16_t RegisterValue::GetAsUInt16(uint16_t fail_value,
                                    bool *success_ptr) const {
  if (success_ptr)
    *success_ptr = true;

  switch (m_type) {
  default:
    break;
  case eTypeUInt8:
  case eTypeUInt16:
    return m_scalar.UShort(fail_value);
  case eTypeBytes: {
    switch (buffer.length) {
    default:
      break;
    case 1:
    case 2:
      return *(const uint16_t *)buffer.bytes;
    }
  } break;
  }
  if (success_ptr)
    *success_ptr = false;
  return fail_value;
}

uint32_t RegisterValue::GetAsUInt32(uint32_t fail_value,
                                    bool *success_ptr) const {
  if (success_ptr)
    *success_ptr = true;
  switch (m_type) {
  default:
    break;
  case eTypeUInt8:
  case eTypeUInt16:
  case eTypeUInt32:
  case eTypeFloat:
  case eTypeDouble:
  case eTypeLongDouble:
    return m_scalar.UInt(fail_value);
  case eTypeBytes: {
    switch (buffer.length) {
    default:
      break;
    case 1:
    case 2:
    case 4:
      return *(const uint32_t *)buffer.bytes;
    }
  } break;
  }
  if (success_ptr)
    *success_ptr = false;
  return fail_value;
}

uint64_t RegisterValue::GetAsUInt64(uint64_t fail_value,
                                    bool *success_ptr) const {
  if (success_ptr)
    *success_ptr = true;
  switch (m_type) {
  default:
    break;
  case eTypeUInt8:
  case eTypeUInt16:
  case eTypeUInt32:
  case eTypeUInt64:
  case eTypeFloat:
  case eTypeDouble:
  case eTypeLongDouble:
    return m_scalar.ULongLong(fail_value);
  case eTypeBytes: {
    switch (buffer.length) {
    default:
      break;
    case 1:
      return *(const uint8_t *)buffer.bytes;
    case 2:
      return *(const uint16_t *)buffer.bytes;
    case 4:
      return *(const uint32_t *)buffer.bytes;
    case 8:
      return *(const uint64_t *)buffer.bytes;
    }
  } break;
  }
  if (success_ptr)
    *success_ptr = false;
  return fail_value;
}

llvm::APInt RegisterValue::GetAsUInt128(const llvm::APInt &fail_value,
                                        bool *success_ptr) const {
  if (success_ptr)
    *success_ptr = true;
  switch (m_type) {
  default:
    break;
  case eTypeUInt8:
  case eTypeUInt16:
  case eTypeUInt32:
  case eTypeUInt64:
  case eTypeUInt128:
  case eTypeFloat:
  case eTypeDouble:
  case eTypeLongDouble:
    return m_scalar.UInt128(fail_value);
  case eTypeBytes: {
    switch (buffer.length) {
    default:
      break;
    case 1:
    case 2:
    case 4:
    case 8:
    case 16:
      return llvm::APInt(BITWIDTH_INT128, NUM_OF_WORDS_INT128,
                         ((const type128 *)buffer.bytes)->x);
    }
  } break;
  }
  if (success_ptr)
    *success_ptr = false;
  return fail_value;
}

float RegisterValue::GetAsFloat(float fail_value, bool *success_ptr) const {
  if (success_ptr)
    *success_ptr = true;
  switch (m_type) {
  default:
    break;
  case eTypeUInt32:
  case eTypeUInt64:
  case eTypeUInt128:
  case eTypeFloat:
  case eTypeDouble:
  case eTypeLongDouble:
    return m_scalar.Float(fail_value);
  }
  if (success_ptr)
    *success_ptr = false;
  return fail_value;
}

double RegisterValue::GetAsDouble(double fail_value, bool *success_ptr) const {
  if (success_ptr)
    *success_ptr = true;
  switch (m_type) {
  default:
    break;

  case eTypeUInt32:
  case eTypeUInt64:
  case eTypeUInt128:
  case eTypeFloat:
  case eTypeDouble:
  case eTypeLongDouble:
    return m_scalar.Double(fail_value);
  }
  if (success_ptr)
    *success_ptr = false;
  return fail_value;
}

long double RegisterValue::GetAsLongDouble(long double fail_value,
                                           bool *success_ptr) const {
  if (success_ptr)
    *success_ptr = true;
  switch (m_type) {
  default:
    break;

  case eTypeUInt32:
  case eTypeUInt64:
  case eTypeUInt128:
  case eTypeFloat:
  case eTypeDouble:
  case eTypeLongDouble:
    return m_scalar.LongDouble();
  }
  if (success_ptr)
    *success_ptr = false;
  return fail_value;
}

const void *RegisterValue::GetBytes() const {
  switch (m_type) {
  case eTypeInvalid:
    break;
  case eTypeUInt8:
  case eTypeUInt16:
  case eTypeUInt32:
  case eTypeUInt64:
  case eTypeUInt128:
  case eTypeFloat:
  case eTypeDouble:
  case eTypeLongDouble:
    return m_scalar.GetBytes();
  case eTypeBytes:
    return buffer.bytes;
  }
  return nullptr;
}

uint32_t RegisterValue::GetByteSize() const {
  switch (m_type) {
  case eTypeInvalid:
    break;
  case eTypeUInt8:
    return 1;
  case eTypeUInt16:
    return 2;
  case eTypeUInt32:
  case eTypeUInt64:
  case eTypeUInt128:
  case eTypeFloat:
  case eTypeDouble:
  case eTypeLongDouble:
    return m_scalar.GetByteSize();
  case eTypeBytes:
    return buffer.length;
  }
  return 0;
}

bool RegisterValue::SetUInt(uint64_t uint, uint32_t byte_size) {
  if (byte_size == 0) {
    SetUInt64(uint);
  } else if (byte_size == 1) {
    SetUInt8(uint);
  } else if (byte_size <= 2) {
    SetUInt16(uint);
  } else if (byte_size <= 4) {
    SetUInt32(uint);
  } else if (byte_size <= 8) {
    SetUInt64(uint);
  } else if (byte_size <= 16) {
    SetUInt128(llvm::APInt(128, uint));
  } else
    return false;
  return true;
}

void RegisterValue::SetBytes(const void *bytes, size_t length,
                             lldb::ByteOrder byte_order) {
  // If this assertion fires off we need to increase the size of buffer.bytes,
  // or make it something that is allocated on the heap. Since the data buffer
  // is in a union, we can't make it a collection class like SmallVector...
  if (bytes && length > 0) {
    assert(length <= sizeof(buffer.bytes) &&
           "Storing too many bytes in a RegisterValue.");
    m_type = eTypeBytes;
    buffer.length = length;
    memcpy(buffer.bytes, bytes, length);
    buffer.byte_order = byte_order;
  } else {
    m_type = eTypeInvalid;
    buffer.length = 0;
  }
}

bool RegisterValue::operator==(const RegisterValue &rhs) const {
  if (m_type == rhs.m_type) {
    switch (m_type) {
    case eTypeInvalid:
      return true;
    case eTypeUInt8:
    case eTypeUInt16:
    case eTypeUInt32:
    case eTypeUInt64:
    case eTypeUInt128:
    case eTypeFloat:
    case eTypeDouble:
    case eTypeLongDouble:
      return m_scalar == rhs.m_scalar;
    case eTypeBytes:
      if (buffer.length != rhs.buffer.length)
        return false;
      else {
        uint8_t length = buffer.length;
        if (length > kMaxRegisterByteSize)
          length = kMaxRegisterByteSize;
        return memcmp(buffer.bytes, rhs.buffer.bytes, length) == 0;
      }
      break;
    }
  }
  return false;
}

bool RegisterValue::operator!=(const RegisterValue &rhs) const {
  return !(*this == rhs);
}

bool RegisterValue::ClearBit(uint32_t bit) {
  switch (m_type) {
  case eTypeInvalid:
    break;

  case eTypeUInt8:
  case eTypeUInt16:
  case eTypeUInt32:
  case eTypeUInt64:
  case eTypeUInt128:
    if (bit < (GetByteSize() * 8)) {
      return m_scalar.ClearBit(bit);
    }
    break;

  case eTypeFloat:
  case eTypeDouble:
  case eTypeLongDouble:
    break;

  case eTypeBytes:
    if (buffer.byte_order == eByteOrderBig ||
        buffer.byte_order == eByteOrderLittle) {
      uint32_t byte_idx;
      if (buffer.byte_order == eByteOrderBig)
        byte_idx = buffer.length - (bit / 8) - 1;
      else
        byte_idx = bit / 8;

      const uint32_t byte_bit = bit % 8;
      if (byte_idx < buffer.length) {
        buffer.bytes[byte_idx] &= ~(1u << byte_bit);
        return true;
      }
    }
    break;
  }
  return false;
}

bool RegisterValue::SetBit(uint32_t bit) {
  switch (m_type) {
  case eTypeInvalid:
    break;

  case eTypeUInt8:
  case eTypeUInt16:
  case eTypeUInt32:
  case eTypeUInt64:
  case eTypeUInt128:
    if (bit < (GetByteSize() * 8)) {
      return m_scalar.SetBit(bit);
    }
    break;

  case eTypeFloat:
  case eTypeDouble:
  case eTypeLongDouble:
    break;

  case eTypeBytes:
    if (buffer.byte_order == eByteOrderBig ||
        buffer.byte_order == eByteOrderLittle) {
      uint32_t byte_idx;
      if (buffer.byte_order == eByteOrderBig)
        byte_idx = buffer.length - (bit / 8) - 1;
      else
        byte_idx = bit / 8;

      const uint32_t byte_bit = bit % 8;
      if (byte_idx < buffer.length) {
        buffer.bytes[byte_idx] |= (1u << byte_bit);
        return true;
      }
    }
    break;
  }
  return false;
}
