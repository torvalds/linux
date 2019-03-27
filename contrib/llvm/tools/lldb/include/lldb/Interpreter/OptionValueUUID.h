//===-- OptionValueUUID.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_OptionValueUUID_h_
#define liblldb_OptionValueUUID_h_

#include "lldb/Utility/UUID.h"
#include "lldb/Interpreter/OptionValue.h"

namespace lldb_private {

class OptionValueUUID : public OptionValue {
public:
  OptionValueUUID() : OptionValue(), m_uuid() {}

  OptionValueUUID(const UUID &uuid) : OptionValue(), m_uuid(uuid) {}

  ~OptionValueUUID() override {}

  //---------------------------------------------------------------------
  // Virtual subclass pure virtual overrides
  //---------------------------------------------------------------------

  OptionValue::Type GetType() const override { return eTypeUUID; }

  void DumpValue(const ExecutionContext *exe_ctx, Stream &strm,
                 uint32_t dump_mask) override;

  Status
  SetValueFromString(llvm::StringRef value,
                     VarSetOperationType op = eVarSetOperationAssign) override;
  Status
  SetValueFromString(const char *,
                     VarSetOperationType = eVarSetOperationAssign) = delete;

  bool Clear() override {
    m_uuid.Clear();
    m_value_was_set = false;
    return true;
  }

  lldb::OptionValueSP DeepCopy() const override;

  //---------------------------------------------------------------------
  // Subclass specific functions
  //---------------------------------------------------------------------

  UUID &GetCurrentValue() { return m_uuid; }

  const UUID &GetCurrentValue() const { return m_uuid; }

  void SetCurrentValue(const UUID &value) { m_uuid = value; }

  size_t AutoComplete(CommandInterpreter &interpreter,
                      CompletionRequest &request) override;

protected:
  UUID m_uuid;
};

} // namespace lldb_private

#endif // liblldb_OptionValueUUID_h_
