//===-- ValueObjectCast.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ValueObjectCast.h"

#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"

namespace lldb_private {
class ConstString;
}

using namespace lldb_private;

lldb::ValueObjectSP ValueObjectCast::Create(ValueObject &parent,
                                            const ConstString &name,
                                            const CompilerType &cast_type) {
  ValueObjectCast *cast_valobj_ptr =
      new ValueObjectCast(parent, name, cast_type);
  return cast_valobj_ptr->GetSP();
}

ValueObjectCast::ValueObjectCast(ValueObject &parent, const ConstString &name,
                                 const CompilerType &cast_type)
    : ValueObject(parent), m_cast_type(cast_type) {
  SetName(name);
  // m_value.SetContext (Value::eContextTypeClangType,
  // cast_type.GetOpaqueQualType());
  m_value.SetCompilerType(cast_type);
}

ValueObjectCast::~ValueObjectCast() {}

CompilerType ValueObjectCast::GetCompilerTypeImpl() { return m_cast_type; }

size_t ValueObjectCast::CalculateNumChildren(uint32_t max) {
  ExecutionContext exe_ctx(GetExecutionContextRef());
  auto children_count = GetCompilerType().GetNumChildren(
      true, &exe_ctx);
  return children_count <= max ? children_count : max;
}

uint64_t ValueObjectCast::GetByteSize() {
  ExecutionContext exe_ctx(GetExecutionContextRef());
  return m_value.GetValueByteSize(nullptr, &exe_ctx);
}

lldb::ValueType ValueObjectCast::GetValueType() const {
  // Let our parent answer global, local, argument, etc...
  return m_parent->GetValueType();
}

bool ValueObjectCast::UpdateValue() {
  SetValueIsValid(false);
  m_error.Clear();

  if (m_parent->UpdateValueIfNeeded(false)) {
    Value old_value(m_value);
    m_update_point.SetUpdated();
    m_value = m_parent->GetValue();
    CompilerType compiler_type(GetCompilerType());
    // m_value.SetContext (Value::eContextTypeClangType, compiler_type);
    m_value.SetCompilerType(compiler_type);
    SetAddressTypeOfChildren(m_parent->GetAddressTypeOfChildren());
    if (!CanProvideValue()) {
      // this value object represents an aggregate type whose children have
      // values, but this object does not. So we say we are changed if our
      // location has changed.
      SetValueDidChange(m_value.GetValueType() != old_value.GetValueType() ||
                        m_value.GetScalar() != old_value.GetScalar());
    }
    ExecutionContext exe_ctx(GetExecutionContextRef());
    m_error = m_value.GetValueAsData(&exe_ctx, m_data, 0, GetModule().get());
    SetValueDidChange(m_parent->GetValueDidChange());
    return true;
  }

  // The dynamic value failed to get an error, pass the error along
  if (m_error.Success() && m_parent->GetError().Fail())
    m_error = m_parent->GetError();
  SetValueIsValid(false);
  return false;
}

bool ValueObjectCast::IsInScope() { return m_parent->IsInScope(); }
