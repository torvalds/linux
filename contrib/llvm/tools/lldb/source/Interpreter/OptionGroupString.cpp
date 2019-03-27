//===-- OptionGroupString.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionGroupString.h"

#include "lldb/Host/OptionParser.h"

using namespace lldb;
using namespace lldb_private;

OptionGroupString::OptionGroupString(uint32_t usage_mask, bool required,
                                     const char *long_option, int short_option,
                                     uint32_t completion_type,
                                     lldb::CommandArgumentType argument_type,
                                     const char *usage_text,
                                     const char *default_value)
    : m_value(default_value, default_value) {
  m_option_definition.usage_mask = usage_mask;
  m_option_definition.required = required;
  m_option_definition.long_option = long_option;
  m_option_definition.short_option = short_option;
  m_option_definition.validator = nullptr;
  m_option_definition.option_has_arg = OptionParser::eRequiredArgument;
  m_option_definition.enum_values = {};
  m_option_definition.completion_type = completion_type;
  m_option_definition.argument_type = argument_type;
  m_option_definition.usage_text = usage_text;
}

OptionGroupString::~OptionGroupString() {}

Status OptionGroupString::SetOptionValue(uint32_t option_idx,
                                         llvm::StringRef option_arg,
                                         ExecutionContext *execution_context) {
  Status error(m_value.SetValueFromString(option_arg));
  return error;
}

void OptionGroupString::OptionParsingStarting(
    ExecutionContext *execution_context) {
  m_value.Clear();
}
