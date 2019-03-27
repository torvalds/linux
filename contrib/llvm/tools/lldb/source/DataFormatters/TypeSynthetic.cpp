//===-- TypeSynthetic.cpp ----------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//




#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-public.h"

#include "lldb/Core/Debugger.h"
#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

void TypeFilterImpl::AddExpressionPath(const std::string &path) {
  bool need_add_dot = true;
  if (path[0] == '.' || (path[0] == '-' && path[1] == '>') || path[0] == '[')
    need_add_dot = false;
  // add a '.' symbol to help forgetful users
  if (!need_add_dot)
    m_expression_paths.push_back(path);
  else
    m_expression_paths.push_back(std::string(".") + path);
}

bool TypeFilterImpl::SetExpressionPathAtIndex(size_t i,
                                              const std::string &path) {
  if (i >= GetCount())
    return false;
  bool need_add_dot = true;
  if (path[0] == '.' || (path[0] == '-' && path[1] == '>') || path[0] == '[')
    need_add_dot = false;
  // add a '.' symbol to help forgetful users
  if (!need_add_dot)
    m_expression_paths[i] = path;
  else
    m_expression_paths[i] = std::string(".") + path;
  return true;
}

size_t
TypeFilterImpl::FrontEnd::GetIndexOfChildWithName(const ConstString &name) {
  const char *name_cstr = name.GetCString();
  if (name_cstr) {
    for (size_t i = 0; i < filter->GetCount(); i++) {
      const char *expr_cstr = filter->GetExpressionPathAtIndex(i);
      if (expr_cstr) {
        if (*expr_cstr == '.')
          expr_cstr++;
        else if (*expr_cstr == '-' && *(expr_cstr + 1) == '>')
          expr_cstr += 2;
      }
      if (expr_cstr) {
        if (!::strcmp(name_cstr, expr_cstr))
          return i;
      }
    }
  }
  return UINT32_MAX;
}

std::string TypeFilterImpl::GetDescription() {
  StreamString sstr;
  sstr.Printf("%s%s%s {\n", Cascades() ? "" : " (not cascading)",
              SkipsPointers() ? " (skip pointers)" : "",
              SkipsReferences() ? " (skip references)" : "");

  for (size_t i = 0; i < GetCount(); i++) {
    sstr.Printf("    %s\n", GetExpressionPathAtIndex(i));
  }

  sstr.Printf("}");
  return sstr.GetString();
}

std::string CXXSyntheticChildren::GetDescription() {
  StreamString sstr;
  sstr.Printf("%s%s%s %s", Cascades() ? "" : " (not cascading)",
              SkipsPointers() ? " (skip pointers)" : "",
              SkipsReferences() ? " (skip references)" : "",
              m_description.c_str());

  return sstr.GetString();
}

lldb::ValueObjectSP SyntheticChildrenFrontEnd::CreateValueObjectFromExpression(
    llvm::StringRef name, llvm::StringRef expression,
    const ExecutionContext &exe_ctx) {
  ValueObjectSP valobj_sp(
      ValueObject::CreateValueObjectFromExpression(name, expression, exe_ctx));
  if (valobj_sp)
    valobj_sp->SetSyntheticChildrenGenerated(true);
  return valobj_sp;
}

lldb::ValueObjectSP SyntheticChildrenFrontEnd::CreateValueObjectFromAddress(
    llvm::StringRef name, uint64_t address, const ExecutionContext &exe_ctx,
    CompilerType type) {
  ValueObjectSP valobj_sp(
      ValueObject::CreateValueObjectFromAddress(name, address, exe_ctx, type));
  if (valobj_sp)
    valobj_sp->SetSyntheticChildrenGenerated(true);
  return valobj_sp;
}

lldb::ValueObjectSP SyntheticChildrenFrontEnd::CreateValueObjectFromData(
    llvm::StringRef name, const DataExtractor &data,
    const ExecutionContext &exe_ctx, CompilerType type) {
  ValueObjectSP valobj_sp(
      ValueObject::CreateValueObjectFromData(name, data, exe_ctx, type));
  if (valobj_sp)
    valobj_sp->SetSyntheticChildrenGenerated(true);
  return valobj_sp;
}

#ifndef LLDB_DISABLE_PYTHON

ScriptedSyntheticChildren::FrontEnd::FrontEnd(std::string pclass,
                                              ValueObject &backend)
    : SyntheticChildrenFrontEnd(backend), m_python_class(pclass),
      m_wrapper_sp(), m_interpreter(NULL) {
  if (backend == LLDB_INVALID_UID)
    return;

  TargetSP target_sp = backend.GetTargetSP();

  if (!target_sp)
    return;

  m_interpreter =
      target_sp->GetDebugger().GetCommandInterpreter().GetScriptInterpreter();

  if (m_interpreter != NULL)
    m_wrapper_sp = m_interpreter->CreateSyntheticScriptedProvider(
        m_python_class.c_str(), backend.GetSP());
}

ScriptedSyntheticChildren::FrontEnd::~FrontEnd() {}

lldb::ValueObjectSP
ScriptedSyntheticChildren::FrontEnd::GetChildAtIndex(size_t idx) {
  if (!m_wrapper_sp || !m_interpreter)
    return lldb::ValueObjectSP();

  return m_interpreter->GetChildAtIndex(m_wrapper_sp, idx);
}

bool ScriptedSyntheticChildren::FrontEnd::IsValid() {
  return (m_wrapper_sp && m_wrapper_sp->IsValid() && m_interpreter);
}

size_t ScriptedSyntheticChildren::FrontEnd::CalculateNumChildren() {
  if (!m_wrapper_sp || m_interpreter == NULL)
    return 0;
  return m_interpreter->CalculateNumChildren(m_wrapper_sp, UINT32_MAX);
}

size_t ScriptedSyntheticChildren::FrontEnd::CalculateNumChildren(uint32_t max) {
  if (!m_wrapper_sp || m_interpreter == NULL)
    return 0;
  return m_interpreter->CalculateNumChildren(m_wrapper_sp, max);
}

bool ScriptedSyntheticChildren::FrontEnd::Update() {
  if (!m_wrapper_sp || m_interpreter == NULL)
    return false;

  return m_interpreter->UpdateSynthProviderInstance(m_wrapper_sp);
}

bool ScriptedSyntheticChildren::FrontEnd::MightHaveChildren() {
  if (!m_wrapper_sp || m_interpreter == NULL)
    return false;

  return m_interpreter->MightHaveChildrenSynthProviderInstance(m_wrapper_sp);
}

size_t ScriptedSyntheticChildren::FrontEnd::GetIndexOfChildWithName(
    const ConstString &name) {
  if (!m_wrapper_sp || m_interpreter == NULL)
    return UINT32_MAX;
  return m_interpreter->GetIndexOfChildWithName(m_wrapper_sp,
                                                name.GetCString());
}

lldb::ValueObjectSP ScriptedSyntheticChildren::FrontEnd::GetSyntheticValue() {
  if (!m_wrapper_sp || m_interpreter == NULL)
    return nullptr;

  return m_interpreter->GetSyntheticValue(m_wrapper_sp);
}

ConstString ScriptedSyntheticChildren::FrontEnd::GetSyntheticTypeName() {
  if (!m_wrapper_sp || m_interpreter == NULL)
    return ConstString();

  return m_interpreter->GetSyntheticTypeName(m_wrapper_sp);
}

std::string ScriptedSyntheticChildren::GetDescription() {
  StreamString sstr;
  sstr.Printf("%s%s%s Python class %s", Cascades() ? "" : " (not cascading)",
              SkipsPointers() ? " (skip pointers)" : "",
              SkipsReferences() ? " (skip references)" : "",
              m_python_class.c_str());

  return sstr.GetString();
}

#endif // #ifndef LLDB_DISABLE_PYTHON
