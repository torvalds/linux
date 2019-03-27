//===-- SBData.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBData_h_
#define LLDB_SBData_h_

#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBData {
public:
  SBData();

  SBData(const SBData &rhs);

  const SBData &operator=(const SBData &rhs);

  ~SBData();

  uint8_t GetAddressByteSize();

  void SetAddressByteSize(uint8_t addr_byte_size);

  void Clear();

  bool IsValid();

  size_t GetByteSize();

  lldb::ByteOrder GetByteOrder();

  void SetByteOrder(lldb::ByteOrder endian);

  float GetFloat(lldb::SBError &error, lldb::offset_t offset);

  double GetDouble(lldb::SBError &error, lldb::offset_t offset);

  long double GetLongDouble(lldb::SBError &error, lldb::offset_t offset);

  lldb::addr_t GetAddress(lldb::SBError &error, lldb::offset_t offset);

  uint8_t GetUnsignedInt8(lldb::SBError &error, lldb::offset_t offset);

  uint16_t GetUnsignedInt16(lldb::SBError &error, lldb::offset_t offset);

  uint32_t GetUnsignedInt32(lldb::SBError &error, lldb::offset_t offset);

  uint64_t GetUnsignedInt64(lldb::SBError &error, lldb::offset_t offset);

  int8_t GetSignedInt8(lldb::SBError &error, lldb::offset_t offset);

  int16_t GetSignedInt16(lldb::SBError &error, lldb::offset_t offset);

  int32_t GetSignedInt32(lldb::SBError &error, lldb::offset_t offset);

  int64_t GetSignedInt64(lldb::SBError &error, lldb::offset_t offset);

  const char *GetString(lldb::SBError &error, lldb::offset_t offset);

  size_t ReadRawData(lldb::SBError &error, lldb::offset_t offset, void *buf,
                     size_t size);

  bool GetDescription(lldb::SBStream &description,
                      lldb::addr_t base_addr = LLDB_INVALID_ADDRESS);

  // it would be nice to have SetData(SBError, const void*, size_t) when
  // endianness and address size can be inferred from the existing
  // DataExtractor, but having two SetData() signatures triggers a SWIG bug
  // where the typemap isn't applied before resolving the overload, and thus
  // the right function never gets called
  void SetData(lldb::SBError &error, const void *buf, size_t size,
               lldb::ByteOrder endian, uint8_t addr_size);

  // see SetData() for why we don't have Append(const void* buf, size_t size)
  bool Append(const SBData &rhs);

  static lldb::SBData CreateDataFromCString(lldb::ByteOrder endian,
                                            uint32_t addr_byte_size,
                                            const char *data);

  // in the following CreateData*() and SetData*() prototypes, the two
  // parameters array and array_len should not be renamed or rearranged,
  // because doing so will break the SWIG typemap
  static lldb::SBData CreateDataFromUInt64Array(lldb::ByteOrder endian,
                                                uint32_t addr_byte_size,
                                                uint64_t *array,
                                                size_t array_len);

  static lldb::SBData CreateDataFromUInt32Array(lldb::ByteOrder endian,
                                                uint32_t addr_byte_size,
                                                uint32_t *array,
                                                size_t array_len);

  static lldb::SBData CreateDataFromSInt64Array(lldb::ByteOrder endian,
                                                uint32_t addr_byte_size,
                                                int64_t *array,
                                                size_t array_len);

  static lldb::SBData CreateDataFromSInt32Array(lldb::ByteOrder endian,
                                                uint32_t addr_byte_size,
                                                int32_t *array,
                                                size_t array_len);

  static lldb::SBData CreateDataFromDoubleArray(lldb::ByteOrder endian,
                                                uint32_t addr_byte_size,
                                                double *array,
                                                size_t array_len);

  bool SetDataFromCString(const char *data);

  bool SetDataFromUInt64Array(uint64_t *array, size_t array_len);

  bool SetDataFromUInt32Array(uint32_t *array, size_t array_len);

  bool SetDataFromSInt64Array(int64_t *array, size_t array_len);

  bool SetDataFromSInt32Array(int32_t *array, size_t array_len);

  bool SetDataFromDoubleArray(double *array, size_t array_len);

protected:
  // Mimic shared pointer...
  lldb_private::DataExtractor *get() const;

  lldb_private::DataExtractor *operator->() const;

  lldb::DataExtractorSP &operator*();

  const lldb::DataExtractorSP &operator*() const;

  SBData(const lldb::DataExtractorSP &data_sp);

  void SetOpaque(const lldb::DataExtractorSP &data_sp);

private:
  friend class SBInstruction;
  friend class SBProcess;
  friend class SBSection;
  friend class SBTarget;
  friend class SBValue;

  lldb::DataExtractorSP m_opaque_sp;
};

} // namespace lldb

#endif // LLDB_SBData_h_
