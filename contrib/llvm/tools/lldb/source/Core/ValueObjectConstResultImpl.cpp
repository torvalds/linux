//===-- ValueObjectConstResultImpl.cpp ---------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ValueObjectConstResultImpl.h"

#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Core/ValueObjectConstResultCast.h"
#include "lldb/Core/ValueObjectConstResultChild.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/SharingPtr.h"

#include <string>

namespace lldb_private {
class DataExtractor;
}
namespace lldb_private {
class Status;
}

using namespace lldb;
using namespace lldb_private;

ValueObjectConstResultImpl::ValueObjectConstResultImpl(
    ValueObject *valobj, lldb::addr_t live_address)
    : m_impl_backend(valobj), m_live_address(live_address),
      m_live_address_type(eAddressTypeLoad), m_load_addr_backend(),
      m_address_of_backend() {}

lldb::ValueObjectSP ValueObjectConstResultImpl::Dereference(Status &error) {
  if (m_impl_backend == NULL)
    return lldb::ValueObjectSP();

  return m_impl_backend->ValueObject::Dereference(error);
}

ValueObject *ValueObjectConstResultImpl::CreateChildAtIndex(
    size_t idx, bool synthetic_array_member, int32_t synthetic_index) {
  if (m_impl_backend == NULL)
    return NULL;

  m_impl_backend->UpdateValueIfNeeded(false);

  ValueObjectConstResultChild *valobj = NULL;

  bool omit_empty_base_classes = true;
  bool ignore_array_bounds = synthetic_array_member;
  std::string child_name_str;
  uint32_t child_byte_size = 0;
  int32_t child_byte_offset = 0;
  uint32_t child_bitfield_bit_size = 0;
  uint32_t child_bitfield_bit_offset = 0;
  bool child_is_base_class = false;
  bool child_is_deref_of_parent = false;
  uint64_t language_flags;

  const bool transparent_pointers = !synthetic_array_member;
  CompilerType compiler_type = m_impl_backend->GetCompilerType();
  CompilerType child_compiler_type;

  ExecutionContext exe_ctx(m_impl_backend->GetExecutionContextRef());

  child_compiler_type = compiler_type.GetChildCompilerTypeAtIndex(
      &exe_ctx, idx, transparent_pointers, omit_empty_base_classes,
      ignore_array_bounds, child_name_str, child_byte_size, child_byte_offset,
      child_bitfield_bit_size, child_bitfield_bit_offset, child_is_base_class,
      child_is_deref_of_parent, m_impl_backend, language_flags);

  // One might think we should check that the size of the children
  // is always strictly positive, hence we could avoid creating a
  // ValueObject if that's not the case, but it turns out there
  // are languages out there which allow zero-size types with
  // children (e.g. Swift).
  if (child_compiler_type) {
    if (synthetic_index)
      child_byte_offset += child_byte_size * synthetic_index;

    ConstString child_name;
    if (!child_name_str.empty())
      child_name.SetCString(child_name_str.c_str());

    valobj = new ValueObjectConstResultChild(
        *m_impl_backend, child_compiler_type, child_name, child_byte_size,
        child_byte_offset, child_bitfield_bit_size, child_bitfield_bit_offset,
        child_is_base_class, child_is_deref_of_parent,
        m_live_address == LLDB_INVALID_ADDRESS
            ? m_live_address
            : m_live_address + child_byte_offset,
        language_flags);
  }

  return valobj;
}

lldb::ValueObjectSP ValueObjectConstResultImpl::GetSyntheticChildAtOffset(
    uint32_t offset, const CompilerType &type, bool can_create,
    ConstString name_const_str) {
  if (m_impl_backend == NULL)
    return lldb::ValueObjectSP();

  return m_impl_backend->ValueObject::GetSyntheticChildAtOffset(
      offset, type, can_create, name_const_str);
}

lldb::ValueObjectSP ValueObjectConstResultImpl::AddressOf(Status &error) {
  if (m_address_of_backend.get() != NULL)
    return m_address_of_backend;

  if (m_impl_backend == NULL)
    return lldb::ValueObjectSP();
  if (m_live_address != LLDB_INVALID_ADDRESS) {
    CompilerType compiler_type(m_impl_backend->GetCompilerType());

    lldb::DataBufferSP buffer(new lldb_private::DataBufferHeap(
        &m_live_address, sizeof(lldb::addr_t)));

    std::string new_name("&");
    new_name.append(m_impl_backend->GetName().AsCString(""));
    ExecutionContext exe_ctx(m_impl_backend->GetExecutionContextRef());
    m_address_of_backend = ValueObjectConstResult::Create(
        exe_ctx.GetBestExecutionContextScope(), compiler_type.GetPointerType(),
        ConstString(new_name.c_str()), buffer, endian::InlHostByteOrder(),
        exe_ctx.GetAddressByteSize());

    m_address_of_backend->GetValue().SetValueType(Value::eValueTypeScalar);
    m_address_of_backend->GetValue().GetScalar() = m_live_address;

    return m_address_of_backend;
  } else
    return m_impl_backend->ValueObject::AddressOf(error);
}

lldb::ValueObjectSP
ValueObjectConstResultImpl::Cast(const CompilerType &compiler_type) {
  if (m_impl_backend == NULL)
    return lldb::ValueObjectSP();

  ValueObjectConstResultCast *result_cast =
      new ValueObjectConstResultCast(*m_impl_backend, m_impl_backend->GetName(),
                                     compiler_type, m_live_address);
  return result_cast->GetSP();
}

lldb::addr_t
ValueObjectConstResultImpl::GetAddressOf(bool scalar_is_load_address,
                                         AddressType *address_type) {

  if (m_impl_backend == NULL)
    return 0;

  if (m_live_address == LLDB_INVALID_ADDRESS) {
    return m_impl_backend->ValueObject::GetAddressOf(scalar_is_load_address,
                                                     address_type);
  }

  if (address_type)
    *address_type = m_live_address_type;

  return m_live_address;
}

size_t ValueObjectConstResultImpl::GetPointeeData(DataExtractor &data,
                                                  uint32_t item_idx,
                                                  uint32_t item_count) {
  if (m_impl_backend == NULL)
    return 0;
  return m_impl_backend->ValueObject::GetPointeeData(data, item_idx,
                                                     item_count);
}
