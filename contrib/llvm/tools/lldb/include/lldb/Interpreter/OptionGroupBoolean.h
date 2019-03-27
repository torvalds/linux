//===-- OptionGroupBoolean.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_OptionGroupBoolean_h_
#define liblldb_OptionGroupBoolean_h_

#include "lldb/Interpreter/OptionValueBoolean.h"
#include "lldb/Interpreter/Options.h"

namespace lldb_private {
//-------------------------------------------------------------------------
// OptionGroupBoolean
//-------------------------------------------------------------------------

class OptionGroupBoolean : public OptionGroup {
public:
  // When 'no_argument_toggle_default' is true, then setting the option value
  // does NOT require an argument, it sets the boolean value to the inverse of
  // the default value
  OptionGroupBoolean(uint32_t usage_mask, bool required,
                     const char *long_option, int short_option,
                     const char *usage_text, bool default_value,
                     bool no_argument_toggle_default);

  ~OptionGroupBoolean() override;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
    return llvm::ArrayRef<OptionDefinition>(&m_option_definition, 1);
  }

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                        ExecutionContext *execution_context) override;
  Status SetOptionValue(uint32_t, const char *, ExecutionContext *) = delete;

  void OptionParsingStarting(ExecutionContext *execution_context) override;

  OptionValueBoolean &GetOptionValue() { return m_value; }

  const OptionValueBoolean &GetOptionValue() const { return m_value; }

protected:
  OptionValueBoolean m_value;
  OptionDefinition m_option_definition;
};

} // namespace lldb_private

#endif // liblldb_OptionGroupBoolean_h_
