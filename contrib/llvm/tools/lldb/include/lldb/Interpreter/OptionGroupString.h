//===-- OptionGroupString.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_OptionGroupString_h_
#define liblldb_OptionGroupString_h_

#include "lldb/Interpreter/OptionValueString.h"
#include "lldb/Interpreter/Options.h"

namespace lldb_private {
//-------------------------------------------------------------------------
// OptionGroupString
//-------------------------------------------------------------------------

class OptionGroupString : public OptionGroup {
public:
  OptionGroupString(uint32_t usage_mask, bool required, const char *long_option,
                    int short_option, uint32_t completion_type,
                    lldb::CommandArgumentType argument_type,
                    const char *usage_text, const char *default_value);

  ~OptionGroupString() override;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
    return llvm::ArrayRef<OptionDefinition>(&m_option_definition, 1);
  }

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                        ExecutionContext *execution_context) override;
  Status SetOptionValue(uint32_t, const char *, ExecutionContext *) = delete;

  void OptionParsingStarting(ExecutionContext *execution_context) override;

  OptionValueString &GetOptionValue() { return m_value; }

  const OptionValueString &GetOptionValue() const { return m_value; }

protected:
  OptionValueString m_value;
  OptionDefinition m_option_definition;
};

} // namespace lldb_private

#endif // liblldb_OptionGroupString_h_
