//===-- OptionValueFormat.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_OptionValueFormat_h_
#define liblldb_OptionValueFormat_h_

#include "lldb/Interpreter/OptionValue.h"

namespace lldb_private {

class OptionValueFormat : public OptionValue {
public:
  OptionValueFormat(lldb::Format value)
      : OptionValue(), m_current_value(value), m_default_value(value) {}

  OptionValueFormat(lldb::Format current_value, lldb::Format default_value)
      : OptionValue(), m_current_value(current_value),
        m_default_value(default_value) {}

  ~OptionValueFormat() override {}

  //---------------------------------------------------------------------
  // Virtual subclass pure virtual overrides
  //---------------------------------------------------------------------

  OptionValue::Type GetType() const override { return eTypeFormat; }

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

  lldb::Format GetCurrentValue() const { return m_current_value; }

  lldb::Format GetDefaultValue() const { return m_default_value; }

  void SetCurrentValue(lldb::Format value) { m_current_value = value; }

  void SetDefaultValue(lldb::Format value) { m_default_value = value; }

protected:
  lldb::Format m_current_value;
  lldb::Format m_default_value;
};

} // namespace lldb_private

#endif // liblldb_OptionValueFormat_h_
