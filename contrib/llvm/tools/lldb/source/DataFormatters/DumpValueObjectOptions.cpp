//===-- DumpValueObjectOptions.cpp -----------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/DataFormatters/DumpValueObjectOptions.h"

#include "lldb/Core/ValueObject.h"

using namespace lldb;
using namespace lldb_private;

DumpValueObjectOptions::DumpValueObjectOptions()
    : m_summary_sp(), m_root_valobj_name(),
      m_max_ptr_depth(PointerDepth{PointerDepth::Mode::Default, 0}),
      m_decl_printing_helper(), m_pointer_as_array(), m_use_synthetic(true),
      m_scope_already_checked(false), m_flat_output(false), m_ignore_cap(false),
      m_show_types(false), m_show_location(false), m_use_objc(false),
      m_hide_root_type(false), m_hide_name(false), m_hide_value(false),
      m_run_validator(false), m_use_type_display_name(true),
      m_allow_oneliner_mode(true), m_hide_pointer_value(false),
      m_reveal_empty_aggregates(true) {}

DumpValueObjectOptions::DumpValueObjectOptions(ValueObject &valobj)
    : DumpValueObjectOptions() {
  m_use_dynamic = valobj.GetDynamicValueType();
  m_use_synthetic = valobj.IsSynthetic();
  m_varformat_language = valobj.GetPreferredDisplayLanguage();
}

DumpValueObjectOptions &
DumpValueObjectOptions::SetMaximumPointerDepth(PointerDepth depth) {
  m_max_ptr_depth = depth;
  return *this;
}

DumpValueObjectOptions &
DumpValueObjectOptions::SetMaximumDepth(uint32_t depth) {
  m_max_depth = depth;
  return *this;
}

DumpValueObjectOptions &
DumpValueObjectOptions::SetDeclPrintingHelper(DeclPrintingHelper helper) {
  m_decl_printing_helper = helper;
  return *this;
}

DumpValueObjectOptions &DumpValueObjectOptions::SetShowTypes(bool show) {
  m_show_types = show;
  return *this;
}

DumpValueObjectOptions &DumpValueObjectOptions::SetShowLocation(bool show) {
  m_show_location = show;
  return *this;
}

DumpValueObjectOptions &DumpValueObjectOptions::SetUseObjectiveC(bool use) {
  m_use_objc = use;
  return *this;
}

DumpValueObjectOptions &DumpValueObjectOptions::SetShowSummary(bool show) {
  if (!show)
    SetOmitSummaryDepth(UINT32_MAX);
  else
    SetOmitSummaryDepth(0);
  return *this;
}

DumpValueObjectOptions &
DumpValueObjectOptions::SetUseDynamicType(lldb::DynamicValueType dyn) {
  m_use_dynamic = dyn;
  return *this;
}

DumpValueObjectOptions &
DumpValueObjectOptions::SetUseSyntheticValue(bool use_synthetic) {
  m_use_synthetic = use_synthetic;
  return *this;
}

DumpValueObjectOptions &DumpValueObjectOptions::SetScopeChecked(bool check) {
  m_scope_already_checked = check;
  return *this;
}

DumpValueObjectOptions &DumpValueObjectOptions::SetFlatOutput(bool flat) {
  m_flat_output = flat;
  return *this;
}

DumpValueObjectOptions &
DumpValueObjectOptions::SetOmitSummaryDepth(uint32_t depth) {
  m_omit_summary_depth = depth;
  return *this;
}

DumpValueObjectOptions &DumpValueObjectOptions::SetIgnoreCap(bool ignore) {
  m_ignore_cap = ignore;
  return *this;
}

DumpValueObjectOptions &DumpValueObjectOptions::SetRawDisplay() {
  SetUseSyntheticValue(false);
  SetOmitSummaryDepth(UINT32_MAX);
  SetIgnoreCap(true);
  SetHideName(false);
  SetHideValue(false);
  SetUseTypeDisplayName(false);
  SetAllowOnelinerMode(false);
  return *this;
}

DumpValueObjectOptions &DumpValueObjectOptions::SetFormat(lldb::Format format) {
  m_format = format;
  return *this;
}

DumpValueObjectOptions &
DumpValueObjectOptions::SetSummary(lldb::TypeSummaryImplSP summary) {
  m_summary_sp = summary;
  return *this;
}

DumpValueObjectOptions &
DumpValueObjectOptions::SetRootValueObjectName(const char *name) {
  if (name)
    m_root_valobj_name.assign(name);
  else
    m_root_valobj_name.clear();
  return *this;
}

DumpValueObjectOptions &
DumpValueObjectOptions::SetHideRootType(bool hide_root_type) {
  m_hide_root_type = hide_root_type;
  return *this;
}

DumpValueObjectOptions &DumpValueObjectOptions::SetHideName(bool hide_name) {
  m_hide_name = hide_name;
  return *this;
}

DumpValueObjectOptions &DumpValueObjectOptions::SetHideValue(bool hide_value) {
  m_hide_value = hide_value;
  return *this;
}

DumpValueObjectOptions &DumpValueObjectOptions::SetHidePointerValue(bool hide) {
  m_hide_pointer_value = hide;
  return *this;
}

DumpValueObjectOptions &
DumpValueObjectOptions::SetVariableFormatDisplayLanguage(
    lldb::LanguageType lang) {
  m_varformat_language = lang;
  return *this;
}

DumpValueObjectOptions &DumpValueObjectOptions::SetRunValidator(bool run) {
  m_run_validator = run;
  return *this;
}

DumpValueObjectOptions &
DumpValueObjectOptions::SetUseTypeDisplayName(bool dis) {
  m_use_type_display_name = dis;
  return *this;
}

DumpValueObjectOptions &
DumpValueObjectOptions::SetAllowOnelinerMode(bool oneliner) {
  m_allow_oneliner_mode = oneliner;
  return *this;
}

DumpValueObjectOptions &
DumpValueObjectOptions::SetRevealEmptyAggregates(bool reveal) {
  m_reveal_empty_aggregates = reveal;
  return *this;
}

DumpValueObjectOptions &
DumpValueObjectOptions::SetElementCount(uint32_t element_count) {
  m_pointer_as_array = PointerAsArraySettings(element_count);
  return *this;
}

DumpValueObjectOptions &DumpValueObjectOptions::SetPointerAsArray(
    const PointerAsArraySettings &ptr_array) {
  m_pointer_as_array = ptr_array;
  return *this;
}
