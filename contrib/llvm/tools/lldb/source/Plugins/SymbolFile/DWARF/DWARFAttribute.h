//===-- DWARFAttribute.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_DWARFAttribute_h_
#define SymbolFileDWARF_DWARFAttribute_h_

#include "DWARFDefines.h"
#include "DWARFFormValue.h"
#include "llvm/ADT/SmallVector.h"
#include <vector>

class DWARFUnit;

class DWARFAttribute {
public:
  DWARFAttribute(dw_attr_t attr, dw_form_t form,
                 DWARFFormValue::ValueType value)
      : m_attr(attr), m_form(form), m_value(value) {}

  void set(dw_attr_t attr, dw_form_t form) {
    m_attr = attr;
    m_form = form;
  }
  void set_attr(dw_attr_t attr) { m_attr = attr; }
  void set_form(dw_form_t form) { m_form = form; }
  dw_attr_t get_attr() const { return m_attr; }
  dw_form_t get_form() const { return m_form; }
  void get(dw_attr_t &attr, dw_form_t &form,
           DWARFFormValue::ValueType &val) const {
    attr = m_attr;
    form = m_form;
    val = m_value;
  }
  bool operator==(const DWARFAttribute &rhs) const {
    return m_attr == rhs.m_attr && m_form == rhs.m_form;
  }
  typedef std::vector<DWARFAttribute> collection;
  typedef collection::iterator iterator;
  typedef collection::const_iterator const_iterator;

protected:
  dw_attr_t m_attr;
  dw_form_t m_form;
  DWARFFormValue::ValueType m_value;
};

class DWARFAttributes {
public:
  DWARFAttributes();
  ~DWARFAttributes();

  void Append(const DWARFUnit *cu, dw_offset_t attr_die_offset,
              dw_attr_t attr, dw_form_t form);
  const DWARFUnit *CompileUnitAtIndex(uint32_t i) const {
    return m_infos[i].cu;
  }
  dw_offset_t DIEOffsetAtIndex(uint32_t i) const {
    return m_infos[i].die_offset;
  }
  dw_attr_t AttributeAtIndex(uint32_t i) const {
    return m_infos[i].attr.get_attr();
  }
  dw_attr_t FormAtIndex(uint32_t i) const { return m_infos[i].attr.get_form(); }
  bool ExtractFormValueAtIndex(uint32_t i, DWARFFormValue &form_value) const;
  uint64_t FormValueAsUnsignedAtIndex(uint32_t i, uint64_t fail_value) const;
  uint64_t FormValueAsUnsigned(dw_attr_t attr, uint64_t fail_value) const;
  uint32_t FindAttributeIndex(dw_attr_t attr) const;
  bool ContainsAttribute(dw_attr_t attr) const;
  bool RemoveAttribute(dw_attr_t attr);
  void Clear() { m_infos.clear(); }
  size_t Size() const { return m_infos.size(); }

protected:
  struct AttributeValue {
    const DWARFUnit *cu;        // Keep the compile unit with each attribute in
                                // case we have DW_FORM_ref_addr values
    dw_offset_t die_offset;
    DWARFAttribute attr;
  };
  typedef llvm::SmallVector<AttributeValue, 8> collection;
  collection m_infos;
};

#endif // SymbolFileDWARF_DWARFAttribute_h_
