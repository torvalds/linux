//===-- ValueObjectConstResult.cpp ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ValueObjectConstResult.h"

#include "lldb/Core/ValueObjectDynamicValue.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/ExecutionContextScope.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/DataBuffer.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Scalar.h"

namespace lldb_private {
class Module;
}

using namespace lldb;
using namespace lldb_private;

ValueObjectSP ValueObjectConstResult::Create(ExecutionContextScope *exe_scope,
                                             ByteOrder byte_order,
                                             uint32_t addr_byte_size,
                                             lldb::addr_t address) {
  return (new ValueObjectConstResult(exe_scope, byte_order, addr_byte_size,
                                     address))
      ->GetSP();
}

ValueObjectConstResult::ValueObjectConstResult(ExecutionContextScope *exe_scope,
                                               ByteOrder byte_order,
                                               uint32_t addr_byte_size,
                                               lldb::addr_t address)
    : ValueObject(exe_scope), m_type_name(), m_byte_size(0),
      m_impl(this, address) {
  SetIsConstant();
  SetValueIsValid(true);
  m_data.SetByteOrder(byte_order);
  m_data.SetAddressByteSize(addr_byte_size);
  SetAddressTypeOfChildren(eAddressTypeLoad);
}

ValueObjectSP ValueObjectConstResult::Create(ExecutionContextScope *exe_scope,
                                             const CompilerType &compiler_type,
                                             const ConstString &name,
                                             const DataExtractor &data,
                                             lldb::addr_t address) {
  return (new ValueObjectConstResult(exe_scope, compiler_type, name, data,
                                     address))
      ->GetSP();
}

ValueObjectConstResult::ValueObjectConstResult(
    ExecutionContextScope *exe_scope, const CompilerType &compiler_type,
    const ConstString &name, const DataExtractor &data, lldb::addr_t address)
    : ValueObject(exe_scope), m_type_name(), m_byte_size(0),
      m_impl(this, address) {
  m_data = data;

  if (!m_data.GetSharedDataBuffer()) {
    DataBufferSP shared_data_buffer(
        new DataBufferHeap(data.GetDataStart(), data.GetByteSize()));
    m_data.SetData(shared_data_buffer);
  }

  m_value.GetScalar() = (uintptr_t)m_data.GetDataStart();
  m_value.SetValueType(Value::eValueTypeHostAddress);
  m_value.SetCompilerType(compiler_type);
  m_name = name;
  SetIsConstant();
  SetValueIsValid(true);
  SetAddressTypeOfChildren(eAddressTypeLoad);
}

ValueObjectSP ValueObjectConstResult::Create(ExecutionContextScope *exe_scope,
                                             const CompilerType &compiler_type,
                                             const ConstString &name,
                                             const lldb::DataBufferSP &data_sp,
                                             lldb::ByteOrder data_byte_order,
                                             uint32_t data_addr_size,
                                             lldb::addr_t address) {
  return (new ValueObjectConstResult(exe_scope, compiler_type, name, data_sp,
                                     data_byte_order, data_addr_size, address))
      ->GetSP();
}

ValueObjectSP ValueObjectConstResult::Create(ExecutionContextScope *exe_scope,
                                             Value &value,
                                             const ConstString &name,
                                             Module *module) {
  return (new ValueObjectConstResult(exe_scope, value, name, module))->GetSP();
}

ValueObjectConstResult::ValueObjectConstResult(
    ExecutionContextScope *exe_scope, const CompilerType &compiler_type,
    const ConstString &name, const lldb::DataBufferSP &data_sp,
    lldb::ByteOrder data_byte_order, uint32_t data_addr_size,
    lldb::addr_t address)
    : ValueObject(exe_scope), m_type_name(), m_byte_size(0),
      m_impl(this, address) {
  m_data.SetByteOrder(data_byte_order);
  m_data.SetAddressByteSize(data_addr_size);
  m_data.SetData(data_sp);
  m_value.GetScalar() = (uintptr_t)data_sp->GetBytes();
  m_value.SetValueType(Value::eValueTypeHostAddress);
  // m_value.SetContext(Value::eContextTypeClangType, compiler_type);
  m_value.SetCompilerType(compiler_type);
  m_name = name;
  SetIsConstant();
  SetValueIsValid(true);
  SetAddressTypeOfChildren(eAddressTypeLoad);
}

ValueObjectSP ValueObjectConstResult::Create(ExecutionContextScope *exe_scope,
                                             const CompilerType &compiler_type,
                                             const ConstString &name,
                                             lldb::addr_t address,
                                             AddressType address_type,
                                             uint32_t addr_byte_size) {
  return (new ValueObjectConstResult(exe_scope, compiler_type, name, address,
                                     address_type, addr_byte_size))
      ->GetSP();
}

ValueObjectConstResult::ValueObjectConstResult(
    ExecutionContextScope *exe_scope, const CompilerType &compiler_type,
    const ConstString &name, lldb::addr_t address, AddressType address_type,
    uint32_t addr_byte_size)
    : ValueObject(exe_scope), m_type_name(), m_byte_size(0),
      m_impl(this, address) {
  m_value.GetScalar() = address;
  m_data.SetAddressByteSize(addr_byte_size);
  m_value.GetScalar().GetData(m_data, addr_byte_size);
  // m_value.SetValueType(Value::eValueTypeHostAddress);
  switch (address_type) {
  case eAddressTypeInvalid:
    m_value.SetValueType(Value::eValueTypeScalar);
    break;
  case eAddressTypeFile:
    m_value.SetValueType(Value::eValueTypeFileAddress);
    break;
  case eAddressTypeLoad:
    m_value.SetValueType(Value::eValueTypeLoadAddress);
    break;
  case eAddressTypeHost:
    m_value.SetValueType(Value::eValueTypeHostAddress);
    break;
  }
  //    m_value.SetContext(Value::eContextTypeClangType, compiler_type);
  m_value.SetCompilerType(compiler_type);
  m_name = name;
  SetIsConstant();
  SetValueIsValid(true);
  SetAddressTypeOfChildren(eAddressTypeLoad);
}

ValueObjectSP ValueObjectConstResult::Create(ExecutionContextScope *exe_scope,
                                             const Status &error) {
  return (new ValueObjectConstResult(exe_scope, error))->GetSP();
}

ValueObjectConstResult::ValueObjectConstResult(ExecutionContextScope *exe_scope,
                                               const Status &error)
    : ValueObject(exe_scope), m_type_name(), m_byte_size(0), m_impl(this) {
  m_error = error;
  SetIsConstant();
}

ValueObjectConstResult::ValueObjectConstResult(ExecutionContextScope *exe_scope,
                                               const Value &value,
                                               const ConstString &name,
                                               Module *module)
    : ValueObject(exe_scope), m_type_name(), m_byte_size(0), m_impl(this) {
  m_value = value;
  m_name = name;
  ExecutionContext exe_ctx;
  exe_scope->CalculateExecutionContext(exe_ctx);
  m_error = m_value.GetValueAsData(&exe_ctx, m_data, 0, module);
}

ValueObjectConstResult::~ValueObjectConstResult() {}

CompilerType ValueObjectConstResult::GetCompilerTypeImpl() {
  return m_value.GetCompilerType();
}

lldb::ValueType ValueObjectConstResult::GetValueType() const {
  return eValueTypeConstResult;
}

uint64_t ValueObjectConstResult::GetByteSize() {
  ExecutionContext exe_ctx(GetExecutionContextRef());
  if (m_byte_size == 0) {
    if (auto size =
        GetCompilerType().GetByteSize(exe_ctx.GetBestExecutionContextScope()))
      SetByteSize(*size);
  }
  return m_byte_size;
}

void ValueObjectConstResult::SetByteSize(size_t size) { m_byte_size = size; }

size_t ValueObjectConstResult::CalculateNumChildren(uint32_t max) {
  ExecutionContext exe_ctx(GetExecutionContextRef());
  auto children_count = GetCompilerType().GetNumChildren(true, &exe_ctx);
  return children_count <= max ? children_count : max;
}

ConstString ValueObjectConstResult::GetTypeName() {
  if (m_type_name.IsEmpty())
    m_type_name = GetCompilerType().GetConstTypeName();
  return m_type_name;
}

ConstString ValueObjectConstResult::GetDisplayTypeName() {
  return GetCompilerType().GetDisplayTypeName();
}

bool ValueObjectConstResult::UpdateValue() {
  // Const value is always valid
  SetValueIsValid(true);
  return true;
}

bool ValueObjectConstResult::IsInScope() {
  // A const result value is always in scope since it serializes all
  // information needed to contain the constant value.
  return true;
}

lldb::ValueObjectSP ValueObjectConstResult::Dereference(Status &error) {
  return m_impl.Dereference(error);
}

lldb::ValueObjectSP ValueObjectConstResult::GetSyntheticChildAtOffset(
    uint32_t offset, const CompilerType &type, bool can_create,
    ConstString name_const_str) {
  return m_impl.GetSyntheticChildAtOffset(offset, type, can_create,
                                          name_const_str);
}

lldb::ValueObjectSP ValueObjectConstResult::AddressOf(Status &error) {
  return m_impl.AddressOf(error);
}

lldb::addr_t ValueObjectConstResult::GetAddressOf(bool scalar_is_load_address,
                                                  AddressType *address_type) {
  return m_impl.GetAddressOf(scalar_is_load_address, address_type);
}

ValueObject *ValueObjectConstResult::CreateChildAtIndex(
    size_t idx, bool synthetic_array_member, int32_t synthetic_index) {
  return m_impl.CreateChildAtIndex(idx, synthetic_array_member,
                                   synthetic_index);
}

size_t ValueObjectConstResult::GetPointeeData(DataExtractor &data,
                                              uint32_t item_idx,
                                              uint32_t item_count) {
  return m_impl.GetPointeeData(data, item_idx, item_count);
}

lldb::ValueObjectSP
ValueObjectConstResult::GetDynamicValue(lldb::DynamicValueType use_dynamic) {
  // Always recalculate dynamic values for const results as the memory that
  // they might point to might have changed at any time.
  if (use_dynamic != eNoDynamicValues) {
    if (!IsDynamic()) {
      ExecutionContext exe_ctx(GetExecutionContextRef());
      Process *process = exe_ctx.GetProcessPtr();
      if (process && process->IsPossibleDynamicValue(*this))
        m_dynamic_value = new ValueObjectDynamicValue(*this, use_dynamic);
    }
    if (m_dynamic_value)
      return m_dynamic_value->GetSP();
  }
  return ValueObjectSP();
}

lldb::ValueObjectSP
ValueObjectConstResult::Cast(const CompilerType &compiler_type) {
  return m_impl.Cast(compiler_type);
}

lldb::LanguageType ValueObjectConstResult::GetPreferredDisplayLanguage() {
  if (m_preferred_display_language != lldb::eLanguageTypeUnknown)
    return m_preferred_display_language;
  return GetCompilerTypeImpl().GetMinimumLanguage();
}
