//===-- LibCxxVector.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "LibCxx.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Utility/ConstString.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

namespace lldb_private {
namespace formatters {
class LibcxxStdVectorSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  LibcxxStdVectorSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~LibcxxStdVectorSyntheticFrontEnd() override;

  size_t CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(size_t idx) override;

  bool Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(const ConstString &name) override;

private:
  ValueObject *m_start;
  ValueObject *m_finish;
  CompilerType m_element_type;
  uint32_t m_element_size;
};

class LibcxxVectorBoolSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  LibcxxVectorBoolSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  size_t CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(size_t idx) override;

  bool Update() override;

  bool MightHaveChildren() override { return true; }

  size_t GetIndexOfChildWithName(const ConstString &name) override;

private:
  CompilerType m_bool_type;
  ExecutionContextRef m_exe_ctx_ref;
  uint64_t m_count;
  lldb::addr_t m_base_data_address;
  std::map<size_t, lldb::ValueObjectSP> m_children;
};

} // namespace formatters
} // namespace lldb_private

lldb_private::formatters::LibcxxStdVectorSyntheticFrontEnd::
    LibcxxStdVectorSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_start(nullptr),
      m_finish(nullptr), m_element_type(), m_element_size(0) {
  if (valobj_sp)
    Update();
}

lldb_private::formatters::LibcxxStdVectorSyntheticFrontEnd::
    ~LibcxxStdVectorSyntheticFrontEnd() {
  // these need to stay around because they are child objects who will follow
  // their parent's life cycle
  // delete m_start;
  // delete m_finish;
}

size_t lldb_private::formatters::LibcxxStdVectorSyntheticFrontEnd::
    CalculateNumChildren() {
  if (!m_start || !m_finish)
    return 0;
  uint64_t start_val = m_start->GetValueAsUnsigned(0);
  uint64_t finish_val = m_finish->GetValueAsUnsigned(0);

  if (start_val == 0 || finish_val == 0)
    return 0;

  if (start_val >= finish_val)
    return 0;

  size_t num_children = (finish_val - start_val);
  if (num_children % m_element_size)
    return 0;
  return num_children / m_element_size;
}

lldb::ValueObjectSP
lldb_private::formatters::LibcxxStdVectorSyntheticFrontEnd::GetChildAtIndex(
    size_t idx) {
  if (!m_start || !m_finish)
    return lldb::ValueObjectSP();

  uint64_t offset = idx * m_element_size;
  offset = offset + m_start->GetValueAsUnsigned(0);
  StreamString name;
  name.Printf("[%" PRIu64 "]", (uint64_t)idx);
  return CreateValueObjectFromAddress(name.GetString(), offset,
                                      m_backend.GetExecutionContextRef(),
                                      m_element_type);
}

bool lldb_private::formatters::LibcxxStdVectorSyntheticFrontEnd::Update() {
  m_start = m_finish = nullptr;
  ValueObjectSP data_type_finder_sp(
      m_backend.GetChildMemberWithName(ConstString("__end_cap_"), true));
  if (!data_type_finder_sp)
    return false;

  switch (data_type_finder_sp->GetCompilerType().GetNumDirectBaseClasses()) {
  case 1:
    // Assume a pre llvm r300140 __compressed_pair implementation:
    data_type_finder_sp = data_type_finder_sp->GetChildMemberWithName(
      ConstString("__first_"), true);
    break;
  case 2: {
    // Assume a post llvm r300140 __compressed_pair implementation:
    ValueObjectSP first_elem_parent_sp =
      data_type_finder_sp->GetChildAtIndex(0, true);
    data_type_finder_sp = first_elem_parent_sp->GetChildMemberWithName(
      ConstString("__value_"), true);
    break;
  }
  default:
    return false;
  }

  if (!data_type_finder_sp)
    return false;
  m_element_type = data_type_finder_sp->GetCompilerType().GetPointeeType();
  if (llvm::Optional<uint64_t> size = m_element_type.GetByteSize(nullptr)) {
    m_element_size = *size;

    if (m_element_size > 0) {
      // store raw pointers or end up with a circular dependency
      m_start =
          m_backend.GetChildMemberWithName(ConstString("__begin_"), true).get();
      m_finish =
          m_backend.GetChildMemberWithName(ConstString("__end_"), true).get();
    }
  }
  return false;
}

bool lldb_private::formatters::LibcxxStdVectorSyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

size_t lldb_private::formatters::LibcxxStdVectorSyntheticFrontEnd::
    GetIndexOfChildWithName(const ConstString &name) {
  if (!m_start || !m_finish)
    return UINT32_MAX;
  return ExtractIndexFromString(name.GetCString());
}

lldb_private::formatters::LibcxxVectorBoolSyntheticFrontEnd::
    LibcxxVectorBoolSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_bool_type(), m_exe_ctx_ref(),
      m_count(0), m_base_data_address(0), m_children() {
  if (valobj_sp) {
    Update();
    m_bool_type =
        valobj_sp->GetCompilerType().GetBasicTypeFromAST(lldb::eBasicTypeBool);
  }
}

size_t lldb_private::formatters::LibcxxVectorBoolSyntheticFrontEnd::
    CalculateNumChildren() {
  return m_count;
}

lldb::ValueObjectSP
lldb_private::formatters::LibcxxVectorBoolSyntheticFrontEnd::GetChildAtIndex(
    size_t idx) {
  auto iter = m_children.find(idx), end = m_children.end();
  if (iter != end)
    return iter->second;
  if (idx >= m_count)
    return {};
  if (m_base_data_address == 0 || m_count == 0)
    return {};
  if (!m_bool_type)
    return {};
  size_t byte_idx = (idx >> 3); // divide by 8 to get byte index
  size_t bit_index = (idx & 7); // efficient idx % 8 for bit index
  lldb::addr_t byte_location = m_base_data_address + byte_idx;
  ProcessSP process_sp(m_exe_ctx_ref.GetProcessSP());
  if (!process_sp)
    return {};
  uint8_t byte = 0;
  uint8_t mask = 0;
  Status err;
  size_t bytes_read = process_sp->ReadMemory(byte_location, &byte, 1, err);
  if (err.Fail() || bytes_read == 0)
    return {};
  mask = 1 << bit_index;
  bool bit_set = ((byte & mask) != 0);
  llvm::Optional<uint64_t> size = m_bool_type.GetByteSize(nullptr);
  if (!size)
    return {};
  DataBufferSP buffer_sp(new DataBufferHeap(*size, 0));
  if (bit_set && buffer_sp && buffer_sp->GetBytes()) {
    // regardless of endianness, anything non-zero is true
    *(buffer_sp->GetBytes()) = 1;
  }
  StreamString name;
  name.Printf("[%" PRIu64 "]", (uint64_t)idx);
  ValueObjectSP retval_sp(CreateValueObjectFromData(
      name.GetString(),
      DataExtractor(buffer_sp, process_sp->GetByteOrder(),
                    process_sp->GetAddressByteSize()),
      m_exe_ctx_ref, m_bool_type));
  if (retval_sp)
    m_children[idx] = retval_sp;
  return retval_sp;
}

/*(std::__1::vector<std::__1::allocator<bool> >) vBool = {
 __begin_ = 0x00000001001000e0
 __size_ = 56
 __cap_alloc_ = {
 std::__1::__libcpp_compressed_pair_imp<unsigned long,
 std::__1::allocator<unsigned long> > = {
 __first_ = 1
 }
 }
 }*/

bool lldb_private::formatters::LibcxxVectorBoolSyntheticFrontEnd::Update() {
  m_children.clear();
  ValueObjectSP valobj_sp = m_backend.GetSP();
  if (!valobj_sp)
    return false;
  m_exe_ctx_ref = valobj_sp->GetExecutionContextRef();
  ValueObjectSP size_sp(
      valobj_sp->GetChildMemberWithName(ConstString("__size_"), true));
  if (!size_sp)
    return false;
  m_count = size_sp->GetValueAsUnsigned(0);
  if (!m_count)
    return true;
  ValueObjectSP begin_sp(
      valobj_sp->GetChildMemberWithName(ConstString("__begin_"), true));
  if (!begin_sp) {
    m_count = 0;
    return false;
  }
  m_base_data_address = begin_sp->GetValueAsUnsigned(0);
  if (!m_base_data_address) {
    m_count = 0;
    return false;
  }
  return false;
}

size_t lldb_private::formatters::LibcxxVectorBoolSyntheticFrontEnd::
    GetIndexOfChildWithName(const ConstString &name) {
  if (!m_count || !m_base_data_address)
    return UINT32_MAX;
  const char *item_name = name.GetCString();
  uint32_t idx = ExtractIndexFromString(item_name);
  if (idx < UINT32_MAX && idx >= CalculateNumChildren())
    return UINT32_MAX;
  return idx;
}

lldb_private::SyntheticChildrenFrontEnd *
lldb_private::formatters::LibcxxStdVectorSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  if (!valobj_sp)
    return nullptr;
  CompilerType type = valobj_sp->GetCompilerType();
  if (!type.IsValid() || type.GetNumTemplateArguments() == 0)
    return nullptr;
  CompilerType arg_type = type.GetTypeTemplateArgument(0);
  if (arg_type.GetTypeName() == ConstString("bool"))
    return new LibcxxVectorBoolSyntheticFrontEnd(valobj_sp);
  return new LibcxxStdVectorSyntheticFrontEnd(valobj_sp);
}
