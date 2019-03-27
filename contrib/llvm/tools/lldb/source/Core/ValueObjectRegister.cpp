//===-- ValueObjectRegister.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ValueObjectRegister.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/Value.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"

#include "llvm/ADT/StringRef.h"

#include <assert.h>
#include <memory>

namespace lldb_private {
class ExecutionContextScope;
}

using namespace lldb;
using namespace lldb_private;

#pragma mark ValueObjectRegisterContext

ValueObjectRegisterContext::ValueObjectRegisterContext(
    ValueObject &parent, RegisterContextSP &reg_ctx)
    : ValueObject(parent), m_reg_ctx_sp(reg_ctx) {
  assert(reg_ctx);
  m_name.SetCString("Registers");
  SetValueIsValid(true);
}

ValueObjectRegisterContext::~ValueObjectRegisterContext() {}

CompilerType ValueObjectRegisterContext::GetCompilerTypeImpl() {
  return CompilerType();
}

ConstString ValueObjectRegisterContext::GetTypeName() { return ConstString(); }

ConstString ValueObjectRegisterContext::GetDisplayTypeName() {
  return ConstString();
}

ConstString ValueObjectRegisterContext::GetQualifiedTypeName() {
  return ConstString();
}

size_t ValueObjectRegisterContext::CalculateNumChildren(uint32_t max) {
  auto reg_set_count = m_reg_ctx_sp->GetRegisterSetCount();
  return reg_set_count <= max ? reg_set_count : max;
}

uint64_t ValueObjectRegisterContext::GetByteSize() { return 0; }

bool ValueObjectRegisterContext::UpdateValue() {
  m_error.Clear();
  ExecutionContext exe_ctx(GetExecutionContextRef());
  StackFrame *frame = exe_ctx.GetFramePtr();
  if (frame)
    m_reg_ctx_sp = frame->GetRegisterContext();
  else
    m_reg_ctx_sp.reset();

  if (m_reg_ctx_sp.get() == NULL) {
    SetValueIsValid(false);
    m_error.SetErrorToGenericError();
  } else
    SetValueIsValid(true);

  return m_error.Success();
}

ValueObject *ValueObjectRegisterContext::CreateChildAtIndex(
    size_t idx, bool synthetic_array_member, int32_t synthetic_index) {
  ValueObject *new_valobj = NULL;

  const size_t num_children = GetNumChildren();
  if (idx < num_children) {
    ExecutionContext exe_ctx(GetExecutionContextRef());
    new_valobj = new ValueObjectRegisterSet(
        exe_ctx.GetBestExecutionContextScope(), m_reg_ctx_sp, idx);
  }

  return new_valobj;
}

#pragma mark -
#pragma mark ValueObjectRegisterSet

ValueObjectSP
ValueObjectRegisterSet::Create(ExecutionContextScope *exe_scope,
                               lldb::RegisterContextSP &reg_ctx_sp,
                               uint32_t set_idx) {
  return (new ValueObjectRegisterSet(exe_scope, reg_ctx_sp, set_idx))->GetSP();
}

ValueObjectRegisterSet::ValueObjectRegisterSet(ExecutionContextScope *exe_scope,
                                               lldb::RegisterContextSP &reg_ctx,
                                               uint32_t reg_set_idx)
    : ValueObject(exe_scope), m_reg_ctx_sp(reg_ctx), m_reg_set(NULL),
      m_reg_set_idx(reg_set_idx) {
  assert(reg_ctx);
  m_reg_set = reg_ctx->GetRegisterSet(m_reg_set_idx);
  if (m_reg_set) {
    m_name.SetCString(m_reg_set->name);
  }
}

ValueObjectRegisterSet::~ValueObjectRegisterSet() {}

CompilerType ValueObjectRegisterSet::GetCompilerTypeImpl() {
  return CompilerType();
}

ConstString ValueObjectRegisterSet::GetTypeName() { return ConstString(); }

ConstString ValueObjectRegisterSet::GetQualifiedTypeName() {
  return ConstString();
}

size_t ValueObjectRegisterSet::CalculateNumChildren(uint32_t max) {
  const RegisterSet *reg_set = m_reg_ctx_sp->GetRegisterSet(m_reg_set_idx);
  if (reg_set) {
    auto reg_count = reg_set->num_registers;
    return reg_count <= max ? reg_count : max;
  }
  return 0;
}

uint64_t ValueObjectRegisterSet::GetByteSize() { return 0; }

bool ValueObjectRegisterSet::UpdateValue() {
  m_error.Clear();
  SetValueDidChange(false);
  ExecutionContext exe_ctx(GetExecutionContextRef());
  StackFrame *frame = exe_ctx.GetFramePtr();
  if (frame == NULL)
    m_reg_ctx_sp.reset();
  else {
    m_reg_ctx_sp = frame->GetRegisterContext();
    if (m_reg_ctx_sp) {
      const RegisterSet *reg_set = m_reg_ctx_sp->GetRegisterSet(m_reg_set_idx);
      if (reg_set == NULL)
        m_reg_ctx_sp.reset();
      else if (m_reg_set != reg_set) {
        SetValueDidChange(true);
        m_name.SetCString(reg_set->name);
      }
    }
  }
  if (m_reg_ctx_sp) {
    SetValueIsValid(true);
  } else {
    SetValueIsValid(false);
    m_error.SetErrorToGenericError();
    m_children.Clear();
  }
  return m_error.Success();
}

ValueObject *ValueObjectRegisterSet::CreateChildAtIndex(
    size_t idx, bool synthetic_array_member, int32_t synthetic_index) {
  ValueObject *valobj = NULL;
  if (m_reg_ctx_sp && m_reg_set) {
    const size_t num_children = GetNumChildren();
    if (idx < num_children)
      valobj = new ValueObjectRegister(*this, m_reg_ctx_sp,
                                       m_reg_set->registers[idx]);
  }
  return valobj;
}

lldb::ValueObjectSP
ValueObjectRegisterSet::GetChildMemberWithName(const ConstString &name,
                                               bool can_create) {
  ValueObject *valobj = NULL;
  if (m_reg_ctx_sp && m_reg_set) {
    const RegisterInfo *reg_info =
        m_reg_ctx_sp->GetRegisterInfoByName(name.AsCString());
    if (reg_info != NULL)
      valobj = new ValueObjectRegister(*this, m_reg_ctx_sp,
                                       reg_info->kinds[eRegisterKindLLDB]);
  }
  if (valobj)
    return valobj->GetSP();
  else
    return ValueObjectSP();
}

size_t
ValueObjectRegisterSet::GetIndexOfChildWithName(const ConstString &name) {
  if (m_reg_ctx_sp && m_reg_set) {
    const RegisterInfo *reg_info =
        m_reg_ctx_sp->GetRegisterInfoByName(name.AsCString());
    if (reg_info != NULL)
      return reg_info->kinds[eRegisterKindLLDB];
  }
  return UINT32_MAX;
}

#pragma mark -
#pragma mark ValueObjectRegister

void ValueObjectRegister::ConstructObject(uint32_t reg_num) {
  const RegisterInfo *reg_info = m_reg_ctx_sp->GetRegisterInfoAtIndex(reg_num);
  if (reg_info) {
    m_reg_info = *reg_info;
    if (reg_info->name)
      m_name.SetCString(reg_info->name);
    else if (reg_info->alt_name)
      m_name.SetCString(reg_info->alt_name);
  }
}

ValueObjectRegister::ValueObjectRegister(ValueObject &parent,
                                         lldb::RegisterContextSP &reg_ctx_sp,
                                         uint32_t reg_num)
    : ValueObject(parent), m_reg_ctx_sp(reg_ctx_sp), m_reg_info(),
      m_reg_value(), m_type_name(), m_compiler_type() {
  assert(reg_ctx_sp.get());
  ConstructObject(reg_num);
}

ValueObjectSP ValueObjectRegister::Create(ExecutionContextScope *exe_scope,
                                          lldb::RegisterContextSP &reg_ctx_sp,
                                          uint32_t reg_num) {
  return (new ValueObjectRegister(exe_scope, reg_ctx_sp, reg_num))->GetSP();
}

ValueObjectRegister::ValueObjectRegister(ExecutionContextScope *exe_scope,
                                         lldb::RegisterContextSP &reg_ctx,
                                         uint32_t reg_num)
    : ValueObject(exe_scope), m_reg_ctx_sp(reg_ctx), m_reg_info(),
      m_reg_value(), m_type_name(), m_compiler_type() {
  assert(reg_ctx);
  ConstructObject(reg_num);
}

ValueObjectRegister::~ValueObjectRegister() {}

CompilerType ValueObjectRegister::GetCompilerTypeImpl() {
  if (!m_compiler_type.IsValid()) {
    ExecutionContext exe_ctx(GetExecutionContextRef());
    Target *target = exe_ctx.GetTargetPtr();
    if (target) {
      Module *exe_module = target->GetExecutableModulePointer();
      if (exe_module) {
        TypeSystem *type_system =
            exe_module->GetTypeSystemForLanguage(eLanguageTypeC);
        if (type_system)
          m_compiler_type = type_system->GetBuiltinTypeForEncodingAndBitSize(
              m_reg_info.encoding, m_reg_info.byte_size * 8);
      }
    }
  }
  return m_compiler_type;
}

ConstString ValueObjectRegister::GetTypeName() {
  if (m_type_name.IsEmpty())
    m_type_name = GetCompilerType().GetConstTypeName();
  return m_type_name;
}

size_t ValueObjectRegister::CalculateNumChildren(uint32_t max) {
  ExecutionContext exe_ctx(GetExecutionContextRef());
  auto children_count = GetCompilerType().GetNumChildren(true, &exe_ctx);
  return children_count <= max ? children_count : max;
}

uint64_t ValueObjectRegister::GetByteSize() { return m_reg_info.byte_size; }

bool ValueObjectRegister::UpdateValue() {
  m_error.Clear();
  ExecutionContext exe_ctx(GetExecutionContextRef());
  StackFrame *frame = exe_ctx.GetFramePtr();
  if (frame == NULL) {
    m_reg_ctx_sp.reset();
    m_reg_value.Clear();
  }

  if (m_reg_ctx_sp) {
    RegisterValue m_old_reg_value(m_reg_value);
    if (m_reg_ctx_sp->ReadRegister(&m_reg_info, m_reg_value)) {
      if (m_reg_value.GetData(m_data)) {
        Process *process = exe_ctx.GetProcessPtr();
        if (process)
          m_data.SetAddressByteSize(process->GetAddressByteSize());
        m_value.SetContext(Value::eContextTypeRegisterInfo,
                           (void *)&m_reg_info);
        m_value.SetValueType(Value::eValueTypeHostAddress);
        m_value.GetScalar() = (uintptr_t)m_data.GetDataStart();
        SetValueIsValid(true);
        SetValueDidChange(!(m_old_reg_value == m_reg_value));
        return true;
      }
    }
  }

  SetValueIsValid(false);
  m_error.SetErrorToGenericError();
  return false;
}

bool ValueObjectRegister::SetValueFromCString(const char *value_str,
                                              Status &error) {
  // The new value will be in the m_data.  Copy that into our register value.
  error =
      m_reg_value.SetValueFromString(&m_reg_info, llvm::StringRef(value_str));
  if (error.Success()) {
    if (m_reg_ctx_sp->WriteRegister(&m_reg_info, m_reg_value)) {
      SetNeedsUpdate();
      return true;
    } else
      return false;
  } else
    return false;
}

bool ValueObjectRegister::SetData(DataExtractor &data, Status &error) {
  error = m_reg_value.SetValueFromData(&m_reg_info, data, 0, false);
  if (error.Success()) {
    if (m_reg_ctx_sp->WriteRegister(&m_reg_info, m_reg_value)) {
      SetNeedsUpdate();
      return true;
    } else
      return false;
  } else
    return false;
}

bool ValueObjectRegister::ResolveValue(Scalar &scalar) {
  if (UpdateValueIfNeeded(
          false)) // make sure that you are up to date before returning anything
    return m_reg_value.GetScalarValue(scalar);
  return false;
}

void ValueObjectRegister::GetExpressionPath(Stream &s,
                                            bool qualify_cxx_base_classes,
                                            GetExpressionPathFormat epformat) {
  s.Printf("$%s", m_reg_info.name);
}
