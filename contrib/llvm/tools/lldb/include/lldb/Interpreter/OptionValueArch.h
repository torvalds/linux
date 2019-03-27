//===-- OptionValueArch.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_OptionValueArch_h_
#define liblldb_OptionValueArch_h_

#include "lldb/Interpreter/OptionValue.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/CompletionRequest.h"

namespace lldb_private {

class OptionValueArch : public OptionValue {
public:
  OptionValueArch() : OptionValue(), m_current_value(), m_default_value() {}

  OptionValueArch(const char *triple)
      : OptionValue(), m_current_value(triple), m_default_value() {
    m_default_value = m_current_value;
  }

  OptionValueArch(const ArchSpec &value)
      : OptionValue(), m_current_value(value), m_default_value(value) {}

  OptionValueArch(const ArchSpec &current_value, const ArchSpec &default_value)
      : OptionValue(), m_current_value(current_value),
        m_default_value(default_value) {}

  ~OptionValueArch() override {}

  //---------------------------------------------------------------------
  // Virtual subclass pure virtual overrides
  //---------------------------------------------------------------------

  OptionValue::Type GetType() const override { return eTypeArch; }

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

  size_t AutoComplete(CommandInterpreter &interpreter,
                      lldb_private::CompletionRequest &request) override;

  //---------------------------------------------------------------------
  // Subclass specific functions
  //---------------------------------------------------------------------

  ArchSpec &GetCurrentValue() { return m_current_value; }

  const ArchSpec &GetCurrentValue() const { return m_current_value; }

  const ArchSpec &GetDefaultValue() const { return m_default_value; }

  void SetCurrentValue(const ArchSpec &value, bool set_value_was_set) {
    m_current_value = value;
    if (set_value_was_set)
      m_value_was_set = true;
  }

  void SetDefaultValue(const ArchSpec &value) { m_default_value = value; }

protected:
  ArchSpec m_current_value;
  ArchSpec m_default_value;
};

} // namespace lldb_private

#endif // liblldb_OptionValueArch_h_
