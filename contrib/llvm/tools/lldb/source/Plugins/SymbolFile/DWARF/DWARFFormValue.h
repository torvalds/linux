//===-- DWARFFormValue.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_DWARFFormValue_h_
#define SymbolFileDWARF_DWARFFormValue_h_

#include "DWARFDataExtractor.h"
#include <stddef.h>

class DWARFUnit;
class SymbolFileDWARF;

class DWARFFormValue {
public:
  typedef struct ValueTypeTag {
    ValueTypeTag() : value(), data(NULL) { value.uval = 0; }

    union {
      uint64_t uval;
      int64_t sval;
      const char *cstr;
    } value;
    const uint8_t *data;
  } ValueType;

  class FixedFormSizes {
  public:
    FixedFormSizes() : m_fix_sizes(nullptr), m_size(0) {}

    FixedFormSizes(const uint8_t *fix_sizes, size_t size)
        : m_fix_sizes(fix_sizes), m_size(size) {}

    uint8_t GetSize(uint32_t index) const {
      return index < m_size ? m_fix_sizes[index] : 0;
    }

    bool Empty() const { return m_size == 0; }

  private:
    const uint8_t *m_fix_sizes;
    size_t m_size;
  };

  enum {
    eValueTypeInvalid = 0,
    eValueTypeUnsigned,
    eValueTypeSigned,
    eValueTypeCStr,
    eValueTypeBlock
  };

  DWARFFormValue();
  DWARFFormValue(const DWARFUnit *cu);
  DWARFFormValue(const DWARFUnit *cu, dw_form_t form);
  const DWARFUnit *GetCompileUnit() const { return m_cu; }
  void SetCompileUnit(const DWARFUnit *cu) { m_cu = cu; }
  dw_form_t Form() const { return m_form; }
  dw_form_t& FormRef() { return m_form; }
  void SetForm(dw_form_t form) { m_form = form; }
  const ValueType &Value() const { return m_value; }
  ValueType &ValueRef() { return m_value; }
  void SetValue(const ValueType &val) { m_value = val; }

  void Dump(lldb_private::Stream &s) const;
  bool ExtractValue(const lldb_private::DWARFDataExtractor &data,
                    lldb::offset_t *offset_ptr);
  const uint8_t *BlockData() const;
  uint64_t Reference() const;
  uint64_t Reference(dw_offset_t offset) const;
  bool Boolean() const { return m_value.value.uval != 0; }
  uint64_t Unsigned() const { return m_value.value.uval; }
  void SetUnsigned(uint64_t uval) { m_value.value.uval = uval; }
  int64_t Signed() const { return m_value.value.sval; }
  void SetSigned(int64_t sval) { m_value.value.sval = sval; }
  const char *AsCString() const;
  dw_addr_t Address() const;
  bool IsValid() const { return m_form != 0; }
  bool SkipValue(const lldb_private::DWARFDataExtractor &debug_info_data,
                 lldb::offset_t *offset_ptr) const;
  static bool SkipValue(const dw_form_t form,
                        const lldb_private::DWARFDataExtractor &debug_info_data,
                        lldb::offset_t *offset_ptr, const DWARFUnit *cu);
  static bool IsBlockForm(const dw_form_t form);
  static bool IsDataForm(const dw_form_t form);
  static FixedFormSizes GetFixedFormSizesForAddressSize(uint8_t addr_size,
                                                        bool is_dwarf64);
  static int Compare(const DWARFFormValue &a, const DWARFFormValue &b);
  void Clear();
  static bool FormIsSupported(dw_form_t form);

protected:
  const DWARFUnit *m_cu;        // Compile unit for this form
  dw_form_t m_form;             // Form for this value
  ValueType m_value;            // Contains all data for the form
};

#endif // SymbolFileDWARF_DWARFFormValue_h_
