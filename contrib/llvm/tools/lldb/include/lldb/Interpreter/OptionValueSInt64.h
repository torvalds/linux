//===-- OptionValueSInt64.h --------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_OptionValueSInt64_h_
#define liblldb_OptionValueSInt64_h_

#include "lldb/Interpreter/OptionValue.h"

namespace lldb_private {

class OptionValueSInt64 : public OptionValue {
public:
  OptionValueSInt64()
      : OptionValue(), m_current_value(0), m_default_value(0),
        m_min_value(INT64_MIN), m_max_value(INT64_MAX) {}

  OptionValueSInt64(int64_t value)
      : OptionValue(), m_current_value(value), m_default_value(value),
        m_min_value(INT64_MIN), m_max_value(INT64_MAX) {}

  OptionValueSInt64(int64_t current_value, int64_t default_value)
      : OptionValue(), m_current_value(current_value),
        m_default_value(default_value), m_min_value(INT64_MIN),
        m_max_value(INT64_MAX) {}

  OptionValueSInt64(const OptionValueSInt64 &rhs)
      : OptionValue(rhs), m_current_value(rhs.m_current_value),
        m_default_value(rhs.m_default_value), m_min_value(rhs.m_min_value),
        m_max_value(rhs.m_max_value) {}

  ~OptionValueSInt64() override {}

  //---------------------------------------------------------------------
  // Virtual subclass pure virtual overrides
  //---------------------------------------------------------------------

  OptionValue::Type GetType() const override { return eTypeSInt64; }

  void DumpValue(const ExecutionContext *exe_ctx, Stream &strm,
                 uint32_t dump_mask) override;

  Status
  SetValueFromString(llvm::StringRef value,
                     VarSetOperationType op = eVarSetOperationAssign) override;
  Status
  SetValueFromString(const char *,
                     VarSetOperationType = eVarSetOperationAssign) = delete;

  bool Clear() override {
    m_current_value = m_default_value;
    m_value_was_set = false;
    return true;
  }

  lldb::OptionValueSP DeepCopy() const override;

  //---------------------------------------------------------------------
  // Subclass specific functions
  //---------------------------------------------------------------------

  const int64_t &operator=(int64_t value) {
    m_current_value = value;
    return m_current_value;
  }

  int64_t GetCurrentValue() const { return m_current_value; }

  int64_t GetDefaultValue() const { return m_default_value; }

  bool SetCurrentValue(int64_t value) {
    if (value >= m_min_value && value <= m_max_value) {
      m_current_value = value;
      return true;
    }
    return false;
  }

  bool SetDefaultValue(int64_t value) {
    if (value >= m_min_value && value <= m_max_value) {
      m_default_value = value;
      return true;
    }
    return false;
  }

  void SetMinimumValue(int64_t v) { m_min_value = v; }

  int64_t GetMinimumValue() const { return m_min_value; }

  void SetMaximumValue(int64_t v) { m_max_value = v; }

  int64_t GetMaximumValue() const { return m_max_value; }

protected:
  int64_t m_current_value;
  int64_t m_default_value;
  int64_t m_min_value;
  int64_t m_max_value;
};

} // namespace lldb_private

#endif // liblldb_OptionValueSInt64_h_
