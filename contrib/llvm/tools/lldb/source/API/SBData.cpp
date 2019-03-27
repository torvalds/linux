//===-- SBData.cpp ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <inttypes.h>

#include "lldb/API/SBData.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBStream.h"

#include "lldb/Core/DumpDataExtractor.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

SBData::SBData() : m_opaque_sp(new DataExtractor()) {}

SBData::SBData(const lldb::DataExtractorSP &data_sp) : m_opaque_sp(data_sp) {}

SBData::SBData(const SBData &rhs) : m_opaque_sp(rhs.m_opaque_sp) {}

const SBData &SBData::operator=(const SBData &rhs) {
  if (this != &rhs)
    m_opaque_sp = rhs.m_opaque_sp;
  return *this;
}

SBData::~SBData() {}

void SBData::SetOpaque(const lldb::DataExtractorSP &data_sp) {
  m_opaque_sp = data_sp;
}

lldb_private::DataExtractor *SBData::get() const { return m_opaque_sp.get(); }

lldb_private::DataExtractor *SBData::operator->() const {
  return m_opaque_sp.operator->();
}

lldb::DataExtractorSP &SBData::operator*() { return m_opaque_sp; }

const lldb::DataExtractorSP &SBData::operator*() const { return m_opaque_sp; }

bool SBData::IsValid() { return m_opaque_sp.get() != NULL; }

uint8_t SBData::GetAddressByteSize() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  uint8_t value = 0;
  if (m_opaque_sp.get())
    value = m_opaque_sp->GetAddressByteSize();
  if (log)
    log->Printf("SBData::GetAddressByteSize () => "
                "(%i)",
                value);
  return value;
}

void SBData::SetAddressByteSize(uint8_t addr_byte_size) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (m_opaque_sp.get())
    m_opaque_sp->SetAddressByteSize(addr_byte_size);
  if (log)
    log->Printf("SBData::SetAddressByteSize (%i)", addr_byte_size);
}

void SBData::Clear() {
  if (m_opaque_sp.get())
    m_opaque_sp->Clear();
}

size_t SBData::GetByteSize() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  size_t value = 0;
  if (m_opaque_sp.get())
    value = m_opaque_sp->GetByteSize();
  if (log)
    log->Printf("SBData::GetByteSize () => "
                "( %" PRIu64 " )",
                (uint64_t)value);
  return value;
}

lldb::ByteOrder SBData::GetByteOrder() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  lldb::ByteOrder value = eByteOrderInvalid;
  if (m_opaque_sp.get())
    value = m_opaque_sp->GetByteOrder();
  if (log)
    log->Printf("SBData::GetByteOrder () => "
                "(%i)",
                value);
  return value;
}

void SBData::SetByteOrder(lldb::ByteOrder endian) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (m_opaque_sp.get())
    m_opaque_sp->SetByteOrder(endian);
  if (log)
    log->Printf("SBData::GetByteOrder (%i)", endian);
}

float SBData::GetFloat(lldb::SBError &error, lldb::offset_t offset) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  float value = 0;
  if (!m_opaque_sp.get()) {
    error.SetErrorString("no value to read from");
  } else {
    uint32_t old_offset = offset;
    value = m_opaque_sp->GetFloat(&offset);
    if (offset == old_offset)
      error.SetErrorString("unable to read data");
  }
  if (log)
    log->Printf("SBData::GetFloat (error=%p,offset=%" PRIu64 ") => (%f)",
                static_cast<void *>(error.get()), offset, value);
  return value;
}

double SBData::GetDouble(lldb::SBError &error, lldb::offset_t offset) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  double value = 0;
  if (!m_opaque_sp.get()) {
    error.SetErrorString("no value to read from");
  } else {
    uint32_t old_offset = offset;
    value = m_opaque_sp->GetDouble(&offset);
    if (offset == old_offset)
      error.SetErrorString("unable to read data");
  }
  if (log)
    log->Printf("SBData::GetDouble (error=%p,offset=%" PRIu64 ") => "
                "(%f)",
                static_cast<void *>(error.get()), offset, value);
  return value;
}

long double SBData::GetLongDouble(lldb::SBError &error, lldb::offset_t offset) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  long double value = 0;
  if (!m_opaque_sp.get()) {
    error.SetErrorString("no value to read from");
  } else {
    uint32_t old_offset = offset;
    value = m_opaque_sp->GetLongDouble(&offset);
    if (offset == old_offset)
      error.SetErrorString("unable to read data");
  }
  if (log)
    log->Printf("SBData::GetLongDouble (error=%p,offset=%" PRIu64 ") => "
                "(%Lf)",
                static_cast<void *>(error.get()), offset, value);
  return value;
}

lldb::addr_t SBData::GetAddress(lldb::SBError &error, lldb::offset_t offset) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  lldb::addr_t value = 0;
  if (!m_opaque_sp.get()) {
    error.SetErrorString("no value to read from");
  } else {
    uint32_t old_offset = offset;
    value = m_opaque_sp->GetAddress(&offset);
    if (offset == old_offset)
      error.SetErrorString("unable to read data");
  }
  if (log)
    log->Printf("SBData::GetAddress (error=%p,offset=%" PRIu64 ") => "
                "(%p)",
                static_cast<void *>(error.get()), offset,
                reinterpret_cast<void *>(value));
  return value;
}

uint8_t SBData::GetUnsignedInt8(lldb::SBError &error, lldb::offset_t offset) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  uint8_t value = 0;
  if (!m_opaque_sp.get()) {
    error.SetErrorString("no value to read from");
  } else {
    uint32_t old_offset = offset;
    value = m_opaque_sp->GetU8(&offset);
    if (offset == old_offset)
      error.SetErrorString("unable to read data");
  }
  if (log)
    log->Printf("SBData::GetUnsignedInt8 (error=%p,offset=%" PRIu64 ") => "
                "(%c)",
                static_cast<void *>(error.get()), offset, value);
  return value;
}

uint16_t SBData::GetUnsignedInt16(lldb::SBError &error, lldb::offset_t offset) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  uint16_t value = 0;
  if (!m_opaque_sp.get()) {
    error.SetErrorString("no value to read from");
  } else {
    uint32_t old_offset = offset;
    value = m_opaque_sp->GetU16(&offset);
    if (offset == old_offset)
      error.SetErrorString("unable to read data");
  }
  if (log)
    log->Printf("SBData::GetUnsignedInt16 (error=%p,offset=%" PRIu64 ") => "
                "(%hd)",
                static_cast<void *>(error.get()), offset, value);
  return value;
}

uint32_t SBData::GetUnsignedInt32(lldb::SBError &error, lldb::offset_t offset) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  uint32_t value = 0;
  if (!m_opaque_sp.get()) {
    error.SetErrorString("no value to read from");
  } else {
    uint32_t old_offset = offset;
    value = m_opaque_sp->GetU32(&offset);
    if (offset == old_offset)
      error.SetErrorString("unable to read data");
  }
  if (log)
    log->Printf("SBData::GetUnsignedInt32 (error=%p,offset=%" PRIu64 ") => "
                "(%d)",
                static_cast<void *>(error.get()), offset, value);
  return value;
}

uint64_t SBData::GetUnsignedInt64(lldb::SBError &error, lldb::offset_t offset) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  uint64_t value = 0;
  if (!m_opaque_sp.get()) {
    error.SetErrorString("no value to read from");
  } else {
    uint32_t old_offset = offset;
    value = m_opaque_sp->GetU64(&offset);
    if (offset == old_offset)
      error.SetErrorString("unable to read data");
  }
  if (log)
    log->Printf("SBData::GetUnsignedInt64 (error=%p,offset=%" PRIu64 ") => "
                "(%" PRId64 ")",
                static_cast<void *>(error.get()), offset, value);
  return value;
}

int8_t SBData::GetSignedInt8(lldb::SBError &error, lldb::offset_t offset) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  int8_t value = 0;
  if (!m_opaque_sp.get()) {
    error.SetErrorString("no value to read from");
  } else {
    uint32_t old_offset = offset;
    value = (int8_t)m_opaque_sp->GetMaxS64(&offset, 1);
    if (offset == old_offset)
      error.SetErrorString("unable to read data");
  }
  if (log)
    log->Printf("SBData::GetSignedInt8 (error=%p,offset=%" PRIu64 ") => "
                "(%c)",
                static_cast<void *>(error.get()), offset, value);
  return value;
}

int16_t SBData::GetSignedInt16(lldb::SBError &error, lldb::offset_t offset) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  int16_t value = 0;
  if (!m_opaque_sp.get()) {
    error.SetErrorString("no value to read from");
  } else {
    uint32_t old_offset = offset;
    value = (int16_t)m_opaque_sp->GetMaxS64(&offset, 2);
    if (offset == old_offset)
      error.SetErrorString("unable to read data");
  }
  if (log)
    log->Printf("SBData::GetSignedInt16 (error=%p,offset=%" PRIu64 ") => "
                "(%hd)",
                static_cast<void *>(error.get()), offset, value);
  return value;
}

int32_t SBData::GetSignedInt32(lldb::SBError &error, lldb::offset_t offset) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  int32_t value = 0;
  if (!m_opaque_sp.get()) {
    error.SetErrorString("no value to read from");
  } else {
    uint32_t old_offset = offset;
    value = (int32_t)m_opaque_sp->GetMaxS64(&offset, 4);
    if (offset == old_offset)
      error.SetErrorString("unable to read data");
  }
  if (log)
    log->Printf("SBData::GetSignedInt32 (error=%p,offset=%" PRIu64 ") => "
                "(%d)",
                static_cast<void *>(error.get()), offset, value);
  return value;
}

int64_t SBData::GetSignedInt64(lldb::SBError &error, lldb::offset_t offset) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  int64_t value = 0;
  if (!m_opaque_sp.get()) {
    error.SetErrorString("no value to read from");
  } else {
    uint32_t old_offset = offset;
    value = (int64_t)m_opaque_sp->GetMaxS64(&offset, 8);
    if (offset == old_offset)
      error.SetErrorString("unable to read data");
  }
  if (log)
    log->Printf("SBData::GetSignedInt64 (error=%p,offset=%" PRIu64 ") => "
                "(%" PRId64 ")",
                static_cast<void *>(error.get()), offset, value);
  return value;
}

const char *SBData::GetString(lldb::SBError &error, lldb::offset_t offset) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  const char *value = 0;
  if (!m_opaque_sp.get()) {
    error.SetErrorString("no value to read from");
  } else {
    uint32_t old_offset = offset;
    value = m_opaque_sp->GetCStr(&offset);
    if (offset == old_offset || (value == NULL))
      error.SetErrorString("unable to read data");
  }
  if (log)
    log->Printf("SBData::GetString (error=%p,offset=%" PRIu64 ") => (%p)",
                static_cast<void *>(error.get()), offset,
                static_cast<const void *>(value));
  return value;
}

bool SBData::GetDescription(lldb::SBStream &description,
                            lldb::addr_t base_addr) {
  Stream &strm = description.ref();

  if (m_opaque_sp) {
    DumpDataExtractor(*m_opaque_sp, &strm, 0, lldb::eFormatBytesWithASCII, 1,
                      m_opaque_sp->GetByteSize(), 16, base_addr, 0, 0);
  } else
    strm.PutCString("No value");

  return true;
}

size_t SBData::ReadRawData(lldb::SBError &error, lldb::offset_t offset,
                           void *buf, size_t size) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  void *ok = NULL;
  if (!m_opaque_sp.get()) {
    error.SetErrorString("no value to read from");
  } else {
    uint32_t old_offset = offset;
    ok = m_opaque_sp->GetU8(&offset, buf, size);
    if ((offset == old_offset) || (ok == NULL))
      error.SetErrorString("unable to read data");
  }
  if (log)
    log->Printf("SBData::ReadRawData (error=%p,offset=%" PRIu64
                ",buf=%p,size=%" PRIu64 ") => "
                "(%p)",
                static_cast<void *>(error.get()), offset,
                static_cast<void *>(buf), static_cast<uint64_t>(size),
                static_cast<void *>(ok));
  return ok ? size : 0;
}

void SBData::SetData(lldb::SBError &error, const void *buf, size_t size,
                     lldb::ByteOrder endian, uint8_t addr_size) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (!m_opaque_sp.get())
    m_opaque_sp.reset(new DataExtractor(buf, size, endian, addr_size));
  else
  {
    m_opaque_sp->SetData(buf, size, endian);
    m_opaque_sp->SetAddressByteSize(addr_size);
  }

  if (log)
    log->Printf("SBData::SetData (error=%p,buf=%p,size=%" PRIu64
                ",endian=%d,addr_size=%c) => "
                "(%p)",
                static_cast<void *>(error.get()),
                static_cast<const void *>(buf), static_cast<uint64_t>(size),
                endian, addr_size, static_cast<void *>(m_opaque_sp.get()));
}

bool SBData::Append(const SBData &rhs) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  bool value = false;
  if (m_opaque_sp.get() && rhs.m_opaque_sp.get())
    value = m_opaque_sp.get()->Append(*rhs.m_opaque_sp);
  if (log)
    log->Printf("SBData::Append (rhs=%p) => (%s)",
                static_cast<void *>(rhs.get()), value ? "true" : "false");
  return value;
}

lldb::SBData SBData::CreateDataFromCString(lldb::ByteOrder endian,
                                           uint32_t addr_byte_size,
                                           const char *data) {
  if (!data || !data[0])
    return SBData();

  uint32_t data_len = strlen(data);

  lldb::DataBufferSP buffer_sp(new DataBufferHeap(data, data_len));
  lldb::DataExtractorSP data_sp(
      new DataExtractor(buffer_sp, endian, addr_byte_size));

  SBData ret(data_sp);

  return ret;
}

lldb::SBData SBData::CreateDataFromUInt64Array(lldb::ByteOrder endian,
                                               uint32_t addr_byte_size,
                                               uint64_t *array,
                                               size_t array_len) {
  if (!array || array_len == 0)
    return SBData();

  size_t data_len = array_len * sizeof(uint64_t);

  lldb::DataBufferSP buffer_sp(new DataBufferHeap(array, data_len));
  lldb::DataExtractorSP data_sp(
      new DataExtractor(buffer_sp, endian, addr_byte_size));

  SBData ret(data_sp);

  return ret;
}

lldb::SBData SBData::CreateDataFromUInt32Array(lldb::ByteOrder endian,
                                               uint32_t addr_byte_size,
                                               uint32_t *array,
                                               size_t array_len) {
  if (!array || array_len == 0)
    return SBData();

  size_t data_len = array_len * sizeof(uint32_t);

  lldb::DataBufferSP buffer_sp(new DataBufferHeap(array, data_len));
  lldb::DataExtractorSP data_sp(
      new DataExtractor(buffer_sp, endian, addr_byte_size));

  SBData ret(data_sp);

  return ret;
}

lldb::SBData SBData::CreateDataFromSInt64Array(lldb::ByteOrder endian,
                                               uint32_t addr_byte_size,
                                               int64_t *array,
                                               size_t array_len) {
  if (!array || array_len == 0)
    return SBData();

  size_t data_len = array_len * sizeof(int64_t);

  lldb::DataBufferSP buffer_sp(new DataBufferHeap(array, data_len));
  lldb::DataExtractorSP data_sp(
      new DataExtractor(buffer_sp, endian, addr_byte_size));

  SBData ret(data_sp);

  return ret;
}

lldb::SBData SBData::CreateDataFromSInt32Array(lldb::ByteOrder endian,
                                               uint32_t addr_byte_size,
                                               int32_t *array,
                                               size_t array_len) {
  if (!array || array_len == 0)
    return SBData();

  size_t data_len = array_len * sizeof(int32_t);

  lldb::DataBufferSP buffer_sp(new DataBufferHeap(array, data_len));
  lldb::DataExtractorSP data_sp(
      new DataExtractor(buffer_sp, endian, addr_byte_size));

  SBData ret(data_sp);

  return ret;
}

lldb::SBData SBData::CreateDataFromDoubleArray(lldb::ByteOrder endian,
                                               uint32_t addr_byte_size,
                                               double *array,
                                               size_t array_len) {
  if (!array || array_len == 0)
    return SBData();

  size_t data_len = array_len * sizeof(double);

  lldb::DataBufferSP buffer_sp(new DataBufferHeap(array, data_len));
  lldb::DataExtractorSP data_sp(
      new DataExtractor(buffer_sp, endian, addr_byte_size));

  SBData ret(data_sp);

  return ret;
}

bool SBData::SetDataFromCString(const char *data) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (!data) {
    if (log)
      log->Printf("SBData::SetDataFromCString (data=%p) => false",
                  static_cast<const void *>(data));
    return false;
  }

  size_t data_len = strlen(data);

  lldb::DataBufferSP buffer_sp(new DataBufferHeap(data, data_len));

  if (!m_opaque_sp.get())
    m_opaque_sp.reset(
        new DataExtractor(buffer_sp, GetByteOrder(), GetAddressByteSize()));
  else
    m_opaque_sp->SetData(buffer_sp);

  if (log)
    log->Printf("SBData::SetDataFromCString (data=%p) => true",
                static_cast<const void *>(data));

  return true;
}

bool SBData::SetDataFromUInt64Array(uint64_t *array, size_t array_len) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (!array || array_len == 0) {
    if (log)
      log->Printf(
          "SBData::SetDataFromUInt64Array (array=%p, array_len = %" PRIu64
          ") => "
          "false",
          static_cast<void *>(array), static_cast<uint64_t>(array_len));
    return false;
  }

  size_t data_len = array_len * sizeof(uint64_t);

  lldb::DataBufferSP buffer_sp(new DataBufferHeap(array, data_len));

  if (!m_opaque_sp.get())
    m_opaque_sp.reset(
        new DataExtractor(buffer_sp, GetByteOrder(), GetAddressByteSize()));
  else
    m_opaque_sp->SetData(buffer_sp);

  if (log)
    log->Printf("SBData::SetDataFromUInt64Array (array=%p, array_len = %" PRIu64
                ") => "
                "true",
                static_cast<void *>(array), static_cast<uint64_t>(array_len));

  return true;
}

bool SBData::SetDataFromUInt32Array(uint32_t *array, size_t array_len) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (!array || array_len == 0) {
    if (log)
      log->Printf(
          "SBData::SetDataFromUInt32Array (array=%p, array_len = %" PRIu64
          ") => "
          "false",
          static_cast<void *>(array), static_cast<uint64_t>(array_len));
    return false;
  }

  size_t data_len = array_len * sizeof(uint32_t);

  lldb::DataBufferSP buffer_sp(new DataBufferHeap(array, data_len));

  if (!m_opaque_sp.get())
    m_opaque_sp.reset(
        new DataExtractor(buffer_sp, GetByteOrder(), GetAddressByteSize()));
  else
    m_opaque_sp->SetData(buffer_sp);

  if (log)
    log->Printf("SBData::SetDataFromUInt32Array (array=%p, array_len = %" PRIu64
                ") => "
                "true",
                static_cast<void *>(array), static_cast<uint64_t>(array_len));

  return true;
}

bool SBData::SetDataFromSInt64Array(int64_t *array, size_t array_len) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (!array || array_len == 0) {
    if (log)
      log->Printf(
          "SBData::SetDataFromSInt64Array (array=%p, array_len = %" PRIu64
          ") => "
          "false",
          static_cast<void *>(array), static_cast<uint64_t>(array_len));
    return false;
  }

  size_t data_len = array_len * sizeof(int64_t);

  lldb::DataBufferSP buffer_sp(new DataBufferHeap(array, data_len));

  if (!m_opaque_sp.get())
    m_opaque_sp.reset(
        new DataExtractor(buffer_sp, GetByteOrder(), GetAddressByteSize()));
  else
    m_opaque_sp->SetData(buffer_sp);

  if (log)
    log->Printf("SBData::SetDataFromSInt64Array (array=%p, array_len = %" PRIu64
                ") => "
                "true",
                static_cast<void *>(array), static_cast<uint64_t>(array_len));

  return true;
}

bool SBData::SetDataFromSInt32Array(int32_t *array, size_t array_len) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (!array || array_len == 0) {
    if (log)
      log->Printf(
          "SBData::SetDataFromSInt32Array (array=%p, array_len = %" PRIu64
          ") => "
          "false",
          static_cast<void *>(array), static_cast<uint64_t>(array_len));
    return false;
  }

  size_t data_len = array_len * sizeof(int32_t);

  lldb::DataBufferSP buffer_sp(new DataBufferHeap(array, data_len));

  if (!m_opaque_sp.get())
    m_opaque_sp.reset(
        new DataExtractor(buffer_sp, GetByteOrder(), GetAddressByteSize()));
  else
    m_opaque_sp->SetData(buffer_sp);

  if (log)
    log->Printf("SBData::SetDataFromSInt32Array (array=%p, array_len = %" PRIu64
                ") => "
                "true",
                static_cast<void *>(array), static_cast<uint64_t>(array_len));

  return true;
}

bool SBData::SetDataFromDoubleArray(double *array, size_t array_len) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (!array || array_len == 0) {
    if (log)
      log->Printf(
          "SBData::SetDataFromDoubleArray (array=%p, array_len = %" PRIu64
          ") => "
          "false",
          static_cast<void *>(array), static_cast<uint64_t>(array_len));
    return false;
  }

  size_t data_len = array_len * sizeof(double);

  lldb::DataBufferSP buffer_sp(new DataBufferHeap(array, data_len));

  if (!m_opaque_sp.get())
    m_opaque_sp.reset(
        new DataExtractor(buffer_sp, GetByteOrder(), GetAddressByteSize()));
  else
    m_opaque_sp->SetData(buffer_sp);

  if (log)
    log->Printf("SBData::SetDataFromDoubleArray (array=%p, array_len = %" PRIu64
                ") => "
                "true",
                static_cast<void *>(array), static_cast<uint64_t>(array_len));

  return true;
}
