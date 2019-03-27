//===-- ValueObjectDynamicValue.cpp ------------------------------*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ValueObjectDynamicValue.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/LanguageRuntime.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Logging.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-types.h"

#include <string.h>
namespace lldb_private {
class Declaration;
}

using namespace lldb_private;

ValueObjectDynamicValue::ValueObjectDynamicValue(
    ValueObject &parent, lldb::DynamicValueType use_dynamic)
    : ValueObject(parent), m_address(), m_dynamic_type_info(),
      m_use_dynamic(use_dynamic) {
  SetName(parent.GetName());
}

ValueObjectDynamicValue::~ValueObjectDynamicValue() {
  m_owning_valobj_sp.reset();
}

CompilerType ValueObjectDynamicValue::GetCompilerTypeImpl() {
  const bool success = UpdateValueIfNeeded(false);
  if (success) {
    if (m_dynamic_type_info.HasType())
      return m_value.GetCompilerType();
    else
      return m_parent->GetCompilerType();
  }
  return m_parent->GetCompilerType();
}

ConstString ValueObjectDynamicValue::GetTypeName() {
  const bool success = UpdateValueIfNeeded(false);
  if (success) {
    if (m_dynamic_type_info.HasName())
      return m_dynamic_type_info.GetName();
  }
  return m_parent->GetTypeName();
}

TypeImpl ValueObjectDynamicValue::GetTypeImpl() {
  const bool success = UpdateValueIfNeeded(false);
  if (success && m_type_impl.IsValid()) {
    return m_type_impl;
  }
  return m_parent->GetTypeImpl();
}

ConstString ValueObjectDynamicValue::GetQualifiedTypeName() {
  const bool success = UpdateValueIfNeeded(false);
  if (success) {
    if (m_dynamic_type_info.HasName())
      return m_dynamic_type_info.GetName();
  }
  return m_parent->GetQualifiedTypeName();
}

ConstString ValueObjectDynamicValue::GetDisplayTypeName() {
  const bool success = UpdateValueIfNeeded(false);
  if (success) {
    if (m_dynamic_type_info.HasType())
      return GetCompilerType().GetDisplayTypeName();
    if (m_dynamic_type_info.HasName())
      return m_dynamic_type_info.GetName();
  }
  return m_parent->GetDisplayTypeName();
}

size_t ValueObjectDynamicValue::CalculateNumChildren(uint32_t max) {
  const bool success = UpdateValueIfNeeded(false);
  if (success && m_dynamic_type_info.HasType()) {
    ExecutionContext exe_ctx(GetExecutionContextRef());
    auto children_count = GetCompilerType().GetNumChildren(true, &exe_ctx);
    return children_count <= max ? children_count : max;
  } else
    return m_parent->GetNumChildren(max);
}

uint64_t ValueObjectDynamicValue::GetByteSize() {
  const bool success = UpdateValueIfNeeded(false);
  if (success && m_dynamic_type_info.HasType()) {
    ExecutionContext exe_ctx(GetExecutionContextRef());
    return m_value.GetValueByteSize(nullptr, &exe_ctx);
  } else
    return m_parent->GetByteSize();
}

lldb::ValueType ValueObjectDynamicValue::GetValueType() const {
  return m_parent->GetValueType();
}

bool ValueObjectDynamicValue::UpdateValue() {
  SetValueIsValid(false);
  m_error.Clear();

  if (!m_parent->UpdateValueIfNeeded(false)) {
    // The dynamic value failed to get an error, pass the error along
    if (m_error.Success() && m_parent->GetError().Fail())
      m_error = m_parent->GetError();
    return false;
  }

  // Setting our type_sp to NULL will route everything back through our parent
  // which is equivalent to not using dynamic values.
  if (m_use_dynamic == lldb::eNoDynamicValues) {
    m_dynamic_type_info.Clear();
    return true;
  }

  ExecutionContext exe_ctx(GetExecutionContextRef());
  Target *target = exe_ctx.GetTargetPtr();
  if (target) {
    m_data.SetByteOrder(target->GetArchitecture().GetByteOrder());
    m_data.SetAddressByteSize(target->GetArchitecture().GetAddressByteSize());
  }

  // First make sure our Type and/or Address haven't changed:
  Process *process = exe_ctx.GetProcessPtr();
  if (!process)
    return false;

  TypeAndOrName class_type_or_name;
  Address dynamic_address;
  bool found_dynamic_type = false;
  Value::ValueType value_type;

  LanguageRuntime *runtime = nullptr;

  lldb::LanguageType known_type = m_parent->GetObjectRuntimeLanguage();
  if (known_type != lldb::eLanguageTypeUnknown &&
      known_type != lldb::eLanguageTypeC) {
    runtime = process->GetLanguageRuntime(known_type);
    if (runtime)
      found_dynamic_type = runtime->GetDynamicTypeAndAddress(
          *m_parent, m_use_dynamic, class_type_or_name, dynamic_address,
          value_type);
  } else {
    runtime = process->GetLanguageRuntime(lldb::eLanguageTypeC_plus_plus);
    if (runtime)
      found_dynamic_type = runtime->GetDynamicTypeAndAddress(
          *m_parent, m_use_dynamic, class_type_or_name, dynamic_address,
          value_type);

    if (!found_dynamic_type) {
      runtime = process->GetLanguageRuntime(lldb::eLanguageTypeObjC);
      if (runtime)
        found_dynamic_type = runtime->GetDynamicTypeAndAddress(
            *m_parent, m_use_dynamic, class_type_or_name, dynamic_address,
            value_type);
    }
  }

  // Getting the dynamic value may have run the program a bit, and so marked us
  // as needing updating, but we really don't...

  m_update_point.SetUpdated();

  if (runtime && found_dynamic_type) {
    if (class_type_or_name.HasType()) {
      m_type_impl =
          TypeImpl(m_parent->GetCompilerType(),
                   runtime->FixUpDynamicType(class_type_or_name, *m_parent)
                       .GetCompilerType());
    } else {
      m_type_impl.Clear();
    }
  } else {
    m_type_impl.Clear();
  }

  // If we don't have a dynamic type, then make ourselves just a echo of our
  // parent. Or we could return false, and make ourselves an echo of our
  // parent?
  if (!found_dynamic_type) {
    if (m_dynamic_type_info)
      SetValueDidChange(true);
    ClearDynamicTypeInformation();
    m_dynamic_type_info.Clear();
    m_value = m_parent->GetValue();
    m_error = m_value.GetValueAsData(&exe_ctx, m_data, 0, GetModule().get());
    return m_error.Success();
  }

  Value old_value(m_value);

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_TYPES));

  bool has_changed_type = false;

  if (!m_dynamic_type_info) {
    m_dynamic_type_info = class_type_or_name;
    has_changed_type = true;
  } else if (class_type_or_name != m_dynamic_type_info) {
    // We are another type, we need to tear down our children...
    m_dynamic_type_info = class_type_or_name;
    SetValueDidChange(true);
    has_changed_type = true;
  }

  if (has_changed_type)
    ClearDynamicTypeInformation();

  if (!m_address.IsValid() || m_address != dynamic_address) {
    if (m_address.IsValid())
      SetValueDidChange(true);

    // We've moved, so we should be fine...
    m_address = dynamic_address;
    lldb::TargetSP target_sp(GetTargetSP());
    lldb::addr_t load_address = m_address.GetLoadAddress(target_sp.get());
    m_value.GetScalar() = load_address;
  }

  if (runtime)
    m_dynamic_type_info =
        runtime->FixUpDynamicType(m_dynamic_type_info, *m_parent);

  // m_value.SetContext (Value::eContextTypeClangType, corrected_type);
  m_value.SetCompilerType(m_dynamic_type_info.GetCompilerType());

  m_value.SetValueType(value_type);

  if (has_changed_type && log)
    log->Printf("[%s %p] has a new dynamic type %s", GetName().GetCString(),
                static_cast<void *>(this), GetTypeName().GetCString());

  if (m_address.IsValid() && m_dynamic_type_info) {
    // The variable value is in the Scalar value inside the m_value. We can
    // point our m_data right to it.
    m_error = m_value.GetValueAsData(&exe_ctx, m_data, 0, GetModule().get());
    if (m_error.Success()) {
      if (!CanProvideValue()) {
        // this value object represents an aggregate type whose children have
        // values, but this object does not. So we say we are changed if our
        // location has changed.
        SetValueDidChange(m_value.GetValueType() != old_value.GetValueType() ||
                          m_value.GetScalar() != old_value.GetScalar());
      }

      SetValueIsValid(true);
      return true;
    }
  }

  // We get here if we've failed above...
  SetValueIsValid(false);
  return false;
}

bool ValueObjectDynamicValue::IsInScope() { return m_parent->IsInScope(); }

bool ValueObjectDynamicValue::SetValueFromCString(const char *value_str,
                                                  Status &error) {
  if (!UpdateValueIfNeeded(false)) {
    error.SetErrorString("unable to read value");
    return false;
  }

  uint64_t my_value = GetValueAsUnsigned(UINT64_MAX);
  uint64_t parent_value = m_parent->GetValueAsUnsigned(UINT64_MAX);

  if (my_value == UINT64_MAX || parent_value == UINT64_MAX) {
    error.SetErrorString("unable to read value");
    return false;
  }

  // if we are at an offset from our parent, in order to set ourselves
  // correctly we would need to change the new value so that it refers to the
  // correct dynamic type. we choose not to deal with that - if anything more
  // than a value overwrite is required, you should be using the expression
  // parser instead of the value editing facility
  if (my_value != parent_value) {
    // but NULL'ing out a value should always be allowed
    if (strcmp(value_str, "0")) {
      error.SetErrorString(
          "unable to modify dynamic value, use 'expression' command");
      return false;
    }
  }

  bool ret_val = m_parent->SetValueFromCString(value_str, error);
  SetNeedsUpdate();
  return ret_val;
}

bool ValueObjectDynamicValue::SetData(DataExtractor &data, Status &error) {
  if (!UpdateValueIfNeeded(false)) {
    error.SetErrorString("unable to read value");
    return false;
  }

  uint64_t my_value = GetValueAsUnsigned(UINT64_MAX);
  uint64_t parent_value = m_parent->GetValueAsUnsigned(UINT64_MAX);

  if (my_value == UINT64_MAX || parent_value == UINT64_MAX) {
    error.SetErrorString("unable to read value");
    return false;
  }

  // if we are at an offset from our parent, in order to set ourselves
  // correctly we would need to change the new value so that it refers to the
  // correct dynamic type. we choose not to deal with that - if anything more
  // than a value overwrite is required, you should be using the expression
  // parser instead of the value editing facility
  if (my_value != parent_value) {
    // but NULL'ing out a value should always be allowed
    lldb::offset_t offset = 0;

    if (data.GetPointer(&offset) != 0) {
      error.SetErrorString(
          "unable to modify dynamic value, use 'expression' command");
      return false;
    }
  }

  bool ret_val = m_parent->SetData(data, error);
  SetNeedsUpdate();
  return ret_val;
}

void ValueObjectDynamicValue::SetPreferredDisplayLanguage(
    lldb::LanguageType lang) {
  this->ValueObject::SetPreferredDisplayLanguage(lang);
  if (m_parent)
    m_parent->SetPreferredDisplayLanguage(lang);
}

lldb::LanguageType ValueObjectDynamicValue::GetPreferredDisplayLanguage() {
  if (m_preferred_display_language == lldb::eLanguageTypeUnknown) {
    if (m_parent)
      return m_parent->GetPreferredDisplayLanguage();
    return lldb::eLanguageTypeUnknown;
  } else
    return m_preferred_display_language;
}

bool ValueObjectDynamicValue::IsSyntheticChildrenGenerated() {
  if (m_parent)
    return m_parent->IsSyntheticChildrenGenerated();
  return false;
}

void ValueObjectDynamicValue::SetSyntheticChildrenGenerated(bool b) {
  if (m_parent)
    m_parent->SetSyntheticChildrenGenerated(b);
  this->ValueObject::SetSyntheticChildrenGenerated(b);
}

bool ValueObjectDynamicValue::GetDeclaration(Declaration &decl) {
  if (m_parent)
    return m_parent->GetDeclaration(decl);

  return ValueObject::GetDeclaration(decl);
}

uint64_t ValueObjectDynamicValue::GetLanguageFlags() {
  if (m_parent)
    return m_parent->GetLanguageFlags();
  return this->ValueObject::GetLanguageFlags();
}

void ValueObjectDynamicValue::SetLanguageFlags(uint64_t flags) {
  if (m_parent)
    m_parent->SetLanguageFlags(flags);
  else
    this->ValueObject::SetLanguageFlags(flags);
}
