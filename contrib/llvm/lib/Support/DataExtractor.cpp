//===-- DataExtractor.cpp -------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/SwapByteOrder.h"
using namespace llvm;

template <typename T>
static T getU(uint32_t *offset_ptr, const DataExtractor *de,
              bool isLittleEndian, const char *Data) {
  T val = 0;
  uint32_t offset = *offset_ptr;
  if (de->isValidOffsetForDataOfSize(offset, sizeof(val))) {
    std::memcpy(&val, &Data[offset], sizeof(val));
    if (sys::IsLittleEndianHost != isLittleEndian)
      sys::swapByteOrder(val);

    // Advance the offset
    *offset_ptr += sizeof(val);
  }
  return val;
}

template <typename T>
static T *getUs(uint32_t *offset_ptr, T *dst, uint32_t count,
                const DataExtractor *de, bool isLittleEndian, const char *Data){
  uint32_t offset = *offset_ptr;

  if (count > 0 && de->isValidOffsetForDataOfSize(offset, sizeof(*dst)*count)) {
    for (T *value_ptr = dst, *end = dst + count; value_ptr != end;
        ++value_ptr, offset += sizeof(*dst))
      *value_ptr = getU<T>(offset_ptr, de, isLittleEndian, Data);
    // Advance the offset
    *offset_ptr = offset;
    // Return a non-NULL pointer to the converted data as an indicator of
    // success
    return dst;
  }
  return nullptr;
}

uint8_t DataExtractor::getU8(uint32_t *offset_ptr) const {
  return getU<uint8_t>(offset_ptr, this, IsLittleEndian, Data.data());
}

uint8_t *
DataExtractor::getU8(uint32_t *offset_ptr, uint8_t *dst, uint32_t count) const {
  return getUs<uint8_t>(offset_ptr, dst, count, this, IsLittleEndian,
                       Data.data());
}


uint16_t DataExtractor::getU16(uint32_t *offset_ptr) const {
  return getU<uint16_t>(offset_ptr, this, IsLittleEndian, Data.data());
}

uint16_t *DataExtractor::getU16(uint32_t *offset_ptr, uint16_t *dst,
                                uint32_t count) const {
  return getUs<uint16_t>(offset_ptr, dst, count, this, IsLittleEndian,
                        Data.data());
}

uint32_t DataExtractor::getU24(uint32_t *offset_ptr) const {
  uint24_t ExtractedVal =
      getU<uint24_t>(offset_ptr, this, IsLittleEndian, Data.data());
  // The 3 bytes are in the correct byte order for the host.
  return ExtractedVal.getAsUint32(sys::IsLittleEndianHost);
}

uint32_t DataExtractor::getU32(uint32_t *offset_ptr) const {
  return getU<uint32_t>(offset_ptr, this, IsLittleEndian, Data.data());
}

uint32_t *DataExtractor::getU32(uint32_t *offset_ptr, uint32_t *dst,
                                uint32_t count) const {
  return getUs<uint32_t>(offset_ptr, dst, count, this, IsLittleEndian,
                        Data.data());
}

uint64_t DataExtractor::getU64(uint32_t *offset_ptr) const {
  return getU<uint64_t>(offset_ptr, this, IsLittleEndian, Data.data());
}

uint64_t *DataExtractor::getU64(uint32_t *offset_ptr, uint64_t *dst,
                                uint32_t count) const {
  return getUs<uint64_t>(offset_ptr, dst, count, this, IsLittleEndian,
                        Data.data());
}

uint64_t
DataExtractor::getUnsigned(uint32_t *offset_ptr, uint32_t byte_size) const {
  switch (byte_size) {
  case 1:
    return getU8(offset_ptr);
  case 2:
    return getU16(offset_ptr);
  case 4:
    return getU32(offset_ptr);
  case 8:
    return getU64(offset_ptr);
  }
  llvm_unreachable("getUnsigned unhandled case!");
}

int64_t
DataExtractor::getSigned(uint32_t *offset_ptr, uint32_t byte_size) const {
  switch (byte_size) {
  case 1:
    return (int8_t)getU8(offset_ptr);
  case 2:
    return (int16_t)getU16(offset_ptr);
  case 4:
    return (int32_t)getU32(offset_ptr);
  case 8:
    return (int64_t)getU64(offset_ptr);
  }
  llvm_unreachable("getSigned unhandled case!");
}

const char *DataExtractor::getCStr(uint32_t *offset_ptr) const {
  uint32_t offset = *offset_ptr;
  StringRef::size_type pos = Data.find('\0', offset);
  if (pos != StringRef::npos) {
    *offset_ptr = pos + 1;
    return Data.data() + offset;
  }
  return nullptr;
}

StringRef DataExtractor::getCStrRef(uint32_t *OffsetPtr) const {
  uint32_t Start = *OffsetPtr;
  StringRef::size_type Pos = Data.find('\0', Start);
  if (Pos != StringRef::npos) {
    *OffsetPtr = Pos + 1;
    return StringRef(Data.data() + Start, Pos - Start);
  }
  return StringRef();
}

uint64_t DataExtractor::getULEB128(uint32_t *offset_ptr) const {
  uint64_t result = 0;
  if (Data.empty())
    return 0;

  unsigned shift = 0;
  uint32_t offset = *offset_ptr;
  uint8_t byte = 0;

  while (isValidOffset(offset)) {
    byte = Data[offset++];
    result |= uint64_t(byte & 0x7f) << shift;
    shift += 7;
    if ((byte & 0x80) == 0)
      break;
  }

  *offset_ptr = offset;
  return result;
}

int64_t DataExtractor::getSLEB128(uint32_t *offset_ptr) const {
  int64_t result = 0;
  if (Data.empty())
    return 0;

  unsigned shift = 0;
  uint32_t offset = *offset_ptr;
  uint8_t byte = 0;

  while (isValidOffset(offset)) {
    byte = Data[offset++];
    result |= uint64_t(byte & 0x7f) << shift;
    shift += 7;
    if ((byte & 0x80) == 0)
      break;
  }

  // Sign bit of byte is 2nd high order bit (0x40)
  if (shift < 64 && (byte & 0x40))
    result |= -(1ULL << shift);

  *offset_ptr = offset;
  return result;
}
