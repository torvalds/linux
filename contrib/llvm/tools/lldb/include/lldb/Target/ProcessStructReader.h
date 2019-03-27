//===---------------------ProcessStructReader.h ------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_PROCESSSTRUCTREADER_H
#define LLDB_TARGET_PROCESSSTRUCTREADER_H

#include "lldb/lldb-defines.h"
#include "lldb/lldb-types.h"

#include "lldb/Symbol/CompilerType.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Status.h"

#include <initializer_list>
#include <map>
#include <string>

namespace lldb_private {
class ProcessStructReader {
protected:
  struct FieldImpl {
    CompilerType type;
    size_t offset;
    size_t size;
  };

  std::map<ConstString, FieldImpl> m_fields;
  DataExtractor m_data;
  lldb::ByteOrder m_byte_order;
  size_t m_addr_byte_size;

public:
  ProcessStructReader(Process *process, lldb::addr_t base_addr,
                      CompilerType struct_type) {
    if (!process)
      return;
    if (base_addr == 0 || base_addr == LLDB_INVALID_ADDRESS)
      return;
    m_byte_order = process->GetByteOrder();
    m_addr_byte_size = process->GetAddressByteSize();

    for (size_t idx = 0; idx < struct_type.GetNumFields(); idx++) {
      std::string name;
      uint64_t bit_offset;
      uint32_t bitfield_bit_size;
      bool is_bitfield;
      CompilerType field_type = struct_type.GetFieldAtIndex(
          idx, name, &bit_offset, &bitfield_bit_size, &is_bitfield);
      // no support for bitfields in here (yet)
      if (is_bitfield)
        return;
      auto size = field_type.GetByteSize(nullptr);
      // no support for things larger than a uint64_t (yet)
      if (!size || *size > 8)
        return;
      ConstString const_name = ConstString(name.c_str());
      size_t byte_index = static_cast<size_t>(bit_offset / 8);
      m_fields[const_name] =
          FieldImpl{field_type, byte_index, static_cast<size_t>(*size)};
    }
    auto total_size = struct_type.GetByteSize(nullptr);
    if (!total_size)
      return;
    lldb::DataBufferSP buffer_sp(new DataBufferHeap(*total_size, 0));
    Status error;
    process->ReadMemoryFromInferior(base_addr, buffer_sp->GetBytes(),
                                    *total_size, error);
    if (error.Fail())
      return;
    m_data = DataExtractor(buffer_sp, m_byte_order, m_addr_byte_size);
  }

  template <typename RetType>
  RetType GetField(ConstString name, RetType fail_value = RetType()) {
    auto iter = m_fields.find(name), end = m_fields.end();
    if (iter == end)
      return fail_value;
    auto size = iter->second.size;
    if (sizeof(RetType) < size)
      return fail_value;
    lldb::offset_t offset = iter->second.offset;
    if (offset + size > m_data.GetByteSize())
      return fail_value;
    return (RetType)(m_data.GetMaxU64(&offset, size));
  }

  size_t GetOffsetOf(ConstString name, size_t fail_value = SIZE_MAX) {
    auto iter = m_fields.find(name), end = m_fields.end();
    if (iter == end)
      return fail_value;
    return iter->second.offset;
  }
};
}

#endif // utility_ProcessStructReader_h_
