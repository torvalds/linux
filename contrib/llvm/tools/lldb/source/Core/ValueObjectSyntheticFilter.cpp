//===-- ValueObjectSyntheticFilter.cpp --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ValueObjectSyntheticFilter.h"

#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Logging.h"
#include "lldb/Utility/SharingPtr.h"
#include "lldb/Utility/Status.h"

#include "llvm/ADT/STLExtras.h"

namespace lldb_private {
class Declaration;
}

using namespace lldb_private;

class DummySyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  DummySyntheticFrontEnd(ValueObject &backend)
      : SyntheticChildrenFrontEnd(backend) {}

  size_t CalculateNumChildren() override { return m_backend.GetNumChildren(); }

  lldb::ValueObjectSP GetChildAtIndex(size_t idx) override {
    return m_backend.GetChildAtIndex(idx, true);
  }

  size_t GetIndexOfChildWithName(const ConstString &name) override {
    return m_backend.GetIndexOfChildWithName(name);
  }

  bool MightHaveChildren() override { return true; }

  bool Update() override { return false; }
};

ValueObjectSynthetic::ValueObjectSynthetic(ValueObject &parent,
                                           lldb::SyntheticChildrenSP filter)
    : ValueObject(parent), m_synth_sp(filter), m_children_byindex(),
      m_name_toindex(), m_synthetic_children_count(UINT32_MAX),
      m_synthetic_children_cache(), m_parent_type_name(parent.GetTypeName()),
      m_might_have_children(eLazyBoolCalculate),
      m_provides_value(eLazyBoolCalculate) {
  SetName(parent.GetName());
  CopyValueData(m_parent);
  CreateSynthFilter();
}

ValueObjectSynthetic::~ValueObjectSynthetic() = default;

CompilerType ValueObjectSynthetic::GetCompilerTypeImpl() {
  return m_parent->GetCompilerType();
}

ConstString ValueObjectSynthetic::GetTypeName() {
  return m_parent->GetTypeName();
}

ConstString ValueObjectSynthetic::GetQualifiedTypeName() {
  return m_parent->GetQualifiedTypeName();
}

ConstString ValueObjectSynthetic::GetDisplayTypeName() {
  if (ConstString synth_name = m_synth_filter_ap->GetSyntheticTypeName())
    return synth_name;

  return m_parent->GetDisplayTypeName();
}

size_t ValueObjectSynthetic::CalculateNumChildren(uint32_t max) {
  Log *log = GetLogIfAllCategoriesSet(LIBLLDB_LOG_DATAFORMATTERS);

  UpdateValueIfNeeded();
  if (m_synthetic_children_count < UINT32_MAX)
    return m_synthetic_children_count <= max ? m_synthetic_children_count : max;

  if (max < UINT32_MAX) {
    size_t num_children = m_synth_filter_ap->CalculateNumChildren(max);
    if (log)
      log->Printf("[ValueObjectSynthetic::CalculateNumChildren] for VO of name "
                  "%s and type %s, the filter returned %zu child values",
                  GetName().AsCString(), GetTypeName().AsCString(),
                  num_children);
    return num_children;
  } else {
    size_t num_children = (m_synthetic_children_count =
                               m_synth_filter_ap->CalculateNumChildren(max));
    if (log)
      log->Printf("[ValueObjectSynthetic::CalculateNumChildren] for VO of name "
                  "%s and type %s, the filter returned %zu child values",
                  GetName().AsCString(), GetTypeName().AsCString(),
                  num_children);
    return num_children;
  }
}

lldb::ValueObjectSP
ValueObjectSynthetic::GetDynamicValue(lldb::DynamicValueType valueType) {
  if (!m_parent)
    return lldb::ValueObjectSP();
  if (IsDynamic() && GetDynamicValueType() == valueType)
    return GetSP();
  return m_parent->GetDynamicValue(valueType);
}

bool ValueObjectSynthetic::MightHaveChildren() {
  if (m_might_have_children == eLazyBoolCalculate)
    m_might_have_children =
        (m_synth_filter_ap->MightHaveChildren() ? eLazyBoolYes : eLazyBoolNo);
  return (m_might_have_children != eLazyBoolNo);
}

uint64_t ValueObjectSynthetic::GetByteSize() { return m_parent->GetByteSize(); }

lldb::ValueType ValueObjectSynthetic::GetValueType() const {
  return m_parent->GetValueType();
}

void ValueObjectSynthetic::CreateSynthFilter() {
  ValueObject *valobj_for_frontend = m_parent;
  if (m_synth_sp->WantsDereference())
  {
    CompilerType type = m_parent->GetCompilerType();
    if (type.IsValid() && type.IsPointerOrReferenceType())
    {
      Status error;
      lldb::ValueObjectSP deref_sp = m_parent->Dereference(error);
      if (error.Success())
        valobj_for_frontend = deref_sp.get();
    }
  }
  m_synth_filter_ap = (m_synth_sp->GetFrontEnd(*valobj_for_frontend));
  if (!m_synth_filter_ap.get())
    m_synth_filter_ap = llvm::make_unique<DummySyntheticFrontEnd>(*m_parent);
}

bool ValueObjectSynthetic::UpdateValue() {
  Log *log = GetLogIfAllCategoriesSet(LIBLLDB_LOG_DATAFORMATTERS);

  SetValueIsValid(false);
  m_error.Clear();

  if (!m_parent->UpdateValueIfNeeded(false)) {
    // our parent could not update.. as we are meaningless without a parent,
    // just stop
    if (m_parent->GetError().Fail())
      m_error = m_parent->GetError();
    return false;
  }

  // regenerate the synthetic filter if our typename changes
  // <rdar://problem/12424824>
  ConstString new_parent_type_name = m_parent->GetTypeName();
  if (new_parent_type_name != m_parent_type_name) {
    if (log)
      log->Printf("[ValueObjectSynthetic::UpdateValue] name=%s, type changed "
                  "from %s to %s, recomputing synthetic filter",
                  GetName().AsCString(), m_parent_type_name.AsCString(),
                  new_parent_type_name.AsCString());
    m_parent_type_name = new_parent_type_name;
    CreateSynthFilter();
  }

  // let our backend do its update
  if (!m_synth_filter_ap->Update()) {
    if (log)
      log->Printf("[ValueObjectSynthetic::UpdateValue] name=%s, synthetic "
                  "filter said caches are stale - clearing",
                  GetName().AsCString());
    // filter said that cached values are stale
    m_children_byindex.Clear();
    m_name_toindex.Clear();
    // usually, an object's value can change but this does not alter its
    // children count for a synthetic VO that might indeed happen, so we need
    // to tell the upper echelons that they need to come back to us asking for
    // children
    m_children_count_valid = false;
    m_synthetic_children_cache.Clear();
    m_synthetic_children_count = UINT32_MAX;
    m_might_have_children = eLazyBoolCalculate;
  } else {
    if (log)
      log->Printf("[ValueObjectSynthetic::UpdateValue] name=%s, synthetic "
                  "filter said caches are still valid",
                  GetName().AsCString());
  }

  m_provides_value = eLazyBoolCalculate;

  lldb::ValueObjectSP synth_val(m_synth_filter_ap->GetSyntheticValue());

  if (synth_val && synth_val->CanProvideValue()) {
    if (log)
      log->Printf("[ValueObjectSynthetic::UpdateValue] name=%s, synthetic "
                  "filter said it can provide a value",
                  GetName().AsCString());

    m_provides_value = eLazyBoolYes;
    CopyValueData(synth_val.get());
  } else {
    if (log)
      log->Printf("[ValueObjectSynthetic::UpdateValue] name=%s, synthetic "
                  "filter said it will not provide a value",
                  GetName().AsCString());

    m_provides_value = eLazyBoolNo;
    CopyValueData(m_parent);
  }

  SetValueIsValid(true);
  return true;
}

lldb::ValueObjectSP ValueObjectSynthetic::GetChildAtIndex(size_t idx,
                                                          bool can_create) {
  Log *log = GetLogIfAllCategoriesSet(LIBLLDB_LOG_DATAFORMATTERS);

  if (log)
    log->Printf("[ValueObjectSynthetic::GetChildAtIndex] name=%s, retrieving "
                "child at index %zu",
                GetName().AsCString(), idx);

  UpdateValueIfNeeded();

  ValueObject *valobj;
  if (!m_children_byindex.GetValueForKey(idx, valobj)) {
    if (can_create && m_synth_filter_ap.get() != nullptr) {
      if (log)
        log->Printf("[ValueObjectSynthetic::GetChildAtIndex] name=%s, child at "
                    "index %zu not cached and will be created",
                    GetName().AsCString(), idx);

      lldb::ValueObjectSP synth_guy = m_synth_filter_ap->GetChildAtIndex(idx);

      if (log)
        log->Printf(
            "[ValueObjectSynthetic::GetChildAtIndex] name=%s, child at index "
            "%zu created as %p (is "
            "synthetic: %s)",
            GetName().AsCString(), idx, static_cast<void *>(synth_guy.get()),
            synth_guy.get()
                ? (synth_guy->IsSyntheticChildrenGenerated() ? "yes" : "no")
                : "no");

      if (!synth_guy)
        return synth_guy;

      if (synth_guy->IsSyntheticChildrenGenerated())
        m_synthetic_children_cache.AppendObject(synth_guy);
      m_children_byindex.SetValueForKey(idx, synth_guy.get());
      synth_guy->SetPreferredDisplayLanguageIfNeeded(
          GetPreferredDisplayLanguage());
      return synth_guy;
    } else {
      if (log)
        log->Printf("[ValueObjectSynthetic::GetChildAtIndex] name=%s, child at "
                    "index %zu not cached and cannot "
                    "be created (can_create = %s, synth_filter = %p)",
                    GetName().AsCString(), idx, can_create ? "yes" : "no",
                    static_cast<void *>(m_synth_filter_ap.get()));

      return lldb::ValueObjectSP();
    }
  } else {
    if (log)
      log->Printf("[ValueObjectSynthetic::GetChildAtIndex] name=%s, child at "
                  "index %zu cached as %p",
                  GetName().AsCString(), idx, static_cast<void *>(valobj));

    return valobj->GetSP();
  }
}

lldb::ValueObjectSP
ValueObjectSynthetic::GetChildMemberWithName(const ConstString &name,
                                             bool can_create) {
  UpdateValueIfNeeded();

  uint32_t index = GetIndexOfChildWithName(name);

  if (index == UINT32_MAX)
    return lldb::ValueObjectSP();

  return GetChildAtIndex(index, can_create);
}

size_t ValueObjectSynthetic::GetIndexOfChildWithName(const ConstString &name) {
  UpdateValueIfNeeded();

  uint32_t found_index = UINT32_MAX;
  bool did_find = m_name_toindex.GetValueForKey(name.GetCString(), found_index);

  if (!did_find && m_synth_filter_ap.get() != nullptr) {
    uint32_t index = m_synth_filter_ap->GetIndexOfChildWithName(name);
    if (index == UINT32_MAX)
      return index;
    m_name_toindex.SetValueForKey(name.GetCString(), index);
    return index;
  } else if (!did_find && m_synth_filter_ap.get() == nullptr)
    return UINT32_MAX;
  else /*if (iter != m_name_toindex.end())*/
    return found_index;
}

bool ValueObjectSynthetic::IsInScope() { return m_parent->IsInScope(); }

lldb::ValueObjectSP ValueObjectSynthetic::GetNonSyntheticValue() {
  return m_parent->GetSP();
}

void ValueObjectSynthetic::CopyValueData(ValueObject *source) {
  m_value = (source->UpdateValueIfNeeded(), source->GetValue());
  ExecutionContext exe_ctx(GetExecutionContextRef());
  m_error = m_value.GetValueAsData(&exe_ctx, m_data, 0, GetModule().get());
}

bool ValueObjectSynthetic::CanProvideValue() {
  if (!UpdateValueIfNeeded())
    return false;
  if (m_provides_value == eLazyBoolYes)
    return true;
  return m_parent->CanProvideValue();
}

bool ValueObjectSynthetic::SetValueFromCString(const char *value_str,
                                               Status &error) {
  return m_parent->SetValueFromCString(value_str, error);
}

void ValueObjectSynthetic::SetFormat(lldb::Format format) {
  if (m_parent) {
    m_parent->ClearUserVisibleData(eClearUserVisibleDataItemsAll);
    m_parent->SetFormat(format);
  }
  this->ValueObject::SetFormat(format);
  this->ClearUserVisibleData(eClearUserVisibleDataItemsAll);
}

void ValueObjectSynthetic::SetPreferredDisplayLanguage(
    lldb::LanguageType lang) {
  this->ValueObject::SetPreferredDisplayLanguage(lang);
  if (m_parent)
    m_parent->SetPreferredDisplayLanguage(lang);
}

lldb::LanguageType ValueObjectSynthetic::GetPreferredDisplayLanguage() {
  if (m_preferred_display_language == lldb::eLanguageTypeUnknown) {
    if (m_parent)
      return m_parent->GetPreferredDisplayLanguage();
    return lldb::eLanguageTypeUnknown;
  } else
    return m_preferred_display_language;
}

bool ValueObjectSynthetic::IsSyntheticChildrenGenerated() {
  if (m_parent)
    return m_parent->IsSyntheticChildrenGenerated();
  return false;
}

void ValueObjectSynthetic::SetSyntheticChildrenGenerated(bool b) {
  if (m_parent)
    m_parent->SetSyntheticChildrenGenerated(b);
  this->ValueObject::SetSyntheticChildrenGenerated(b);
}

bool ValueObjectSynthetic::GetDeclaration(Declaration &decl) {
  if (m_parent)
    return m_parent->GetDeclaration(decl);

  return ValueObject::GetDeclaration(decl);
}

uint64_t ValueObjectSynthetic::GetLanguageFlags() {
  if (m_parent)
    return m_parent->GetLanguageFlags();
  return this->ValueObject::GetLanguageFlags();
}

void ValueObjectSynthetic::SetLanguageFlags(uint64_t flags) {
  if (m_parent)
    m_parent->SetLanguageFlags(flags);
  else
    this->ValueObject::SetLanguageFlags(flags);
}
