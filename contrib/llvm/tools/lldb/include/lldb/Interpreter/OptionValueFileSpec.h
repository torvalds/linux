//===-- OptionValueFileSpec.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_OptionValueFileSpec_h_
#define liblldb_OptionValueFileSpec_h_

#include "lldb/Interpreter/OptionValue.h"

#include "lldb/Utility/FileSpec.h"
#include "llvm/Support/Chrono.h"

namespace lldb_private {

class OptionValueFileSpec : public OptionValue {
public:
  OptionValueFileSpec(bool resolve = true);

  OptionValueFileSpec(const FileSpec &value, bool resolve = true);

  OptionValueFileSpec(const FileSpec &current_value,
                      const FileSpec &default_value, bool resolve = true);

  ~OptionValueFileSpec() override {}

  //---------------------------------------------------------------------
  // Virtual subclass pure virtual overrides
  //---------------------------------------------------------------------

  OptionValue::Type GetType() const override { return eTypeFileSpec; }

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
    m_data_sp.reset();
    m_data_mod_time = llvm::sys::TimePoint<>();
    return true;
  }

  lldb::OptionValueSP DeepCopy() const override;

  size_t AutoComplete(CommandInterpreter &interpreter,
                      CompletionRequest &request) override;

  //---------------------------------------------------------------------
  // Subclass specific functions
  //---------------------------------------------------------------------

  FileSpec &GetCurrentValue() { return m_current_value; }

  const FileSpec &GetCurrentValue() const { return m_current_value; }

  const FileSpec &GetDefaultValue() const { return m_default_value; }

  void SetCurrentValue(const FileSpec &value, bool set_value_was_set) {
    m_current_value = value;
    if (set_value_was_set)
      m_value_was_set = true;
    m_data_sp.reset();
  }

  void SetDefaultValue(const FileSpec &value) { m_default_value = value; }

  const lldb::DataBufferSP &GetFileContents();

  void SetCompletionMask(uint32_t mask) { m_completion_mask = mask; }

protected:
  FileSpec m_current_value;
  FileSpec m_default_value;
  lldb::DataBufferSP m_data_sp;
  llvm::sys::TimePoint<> m_data_mod_time;
  uint32_t m_completion_mask;
  bool m_resolve;
};

} // namespace lldb_private

#endif // liblldb_OptionValueFileSpec_h_
