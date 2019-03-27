//===-- DWARFAttribute.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DWARFAttribute.h"
#include "DWARFUnit.h"
#include "DWARFDebugInfo.h"

DWARFAttributes::DWARFAttributes() : m_infos() {}

DWARFAttributes::~DWARFAttributes() {}

uint32_t DWARFAttributes::FindAttributeIndex(dw_attr_t attr) const {
  collection::const_iterator end = m_infos.end();
  collection::const_iterator beg = m_infos.begin();
  collection::const_iterator pos;
  for (pos = beg; pos != end; ++pos) {
    if (pos->attr.get_attr() == attr)
      return std::distance(beg, pos);
  }
  return UINT32_MAX;
}

void DWARFAttributes::Append(const DWARFUnit *cu, dw_offset_t attr_die_offset,
                             dw_attr_t attr, dw_form_t form) {
  AttributeValue attr_value = {
      cu, attr_die_offset, {attr, form, DWARFFormValue::ValueType()}};
  m_infos.push_back(attr_value);
}

bool DWARFAttributes::ContainsAttribute(dw_attr_t attr) const {
  return FindAttributeIndex(attr) != UINT32_MAX;
}

bool DWARFAttributes::RemoveAttribute(dw_attr_t attr) {
  uint32_t attr_index = FindAttributeIndex(attr);
  if (attr_index != UINT32_MAX) {
    m_infos.erase(m_infos.begin() + attr_index);
    return true;
  }
  return false;
}

bool DWARFAttributes::ExtractFormValueAtIndex(
    uint32_t i, DWARFFormValue &form_value) const {
  const DWARFUnit *cu = CompileUnitAtIndex(i);
  form_value.SetCompileUnit(cu);
  form_value.SetForm(FormAtIndex(i));
  lldb::offset_t offset = DIEOffsetAtIndex(i);
  return form_value.ExtractValue(cu->GetData(), &offset);
}

uint64_t DWARFAttributes::FormValueAsUnsigned(dw_attr_t attr,
                                              uint64_t fail_value) const {
  const uint32_t attr_idx = FindAttributeIndex(attr);
  if (attr_idx != UINT32_MAX)
    return FormValueAsUnsignedAtIndex(attr_idx, fail_value);
  return fail_value;
}

uint64_t
DWARFAttributes::FormValueAsUnsignedAtIndex(uint32_t i,
                                            uint64_t fail_value) const {
  DWARFFormValue form_value;
  if (ExtractFormValueAtIndex(i, form_value))
    return form_value.Reference();
  return fail_value;
}
