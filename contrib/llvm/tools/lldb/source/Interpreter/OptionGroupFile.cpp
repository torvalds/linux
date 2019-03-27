//===-- OptionGroupFile.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionGroupFile.h"

#include "lldb/Host/OptionParser.h"

using namespace lldb;
using namespace lldb_private;

OptionGroupFile::OptionGroupFile(uint32_t usage_mask, bool required,
                                 const char *long_option, int short_option,
                                 uint32_t completion_type,
                                 lldb::CommandArgumentType argument_type,
                                 const char *usage_text)
    : m_file() {
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

OptionGroupFile::~OptionGroupFile() {}

Status OptionGroupFile::SetOptionValue(uint32_t option_idx,
                                       llvm::StringRef option_arg,
                                       ExecutionContext *execution_context) {
  Status error(m_file.SetValueFromString(option_arg));
  return error;
}

void OptionGroupFile::OptionParsingStarting(
    ExecutionContext *execution_context) {
  m_file.Clear();
}

OptionGroupFileList::OptionGroupFileList(
    uint32_t usage_mask, bool required, const char *long_option,
    int short_option, uint32_t completion_type,
    lldb::CommandArgumentType argument_type, const char *usage_text)
    : m_file_list() {
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

OptionGroupFileList::~OptionGroupFileList() {}

Status
OptionGroupFileList::SetOptionValue(uint32_t option_idx,
                                    llvm::StringRef option_value,
                                    ExecutionContext *execution_context) {
  Status error(m_file_list.SetValueFromString(option_value));
  return error;
}

void OptionGroupFileList::OptionParsingStarting(
    ExecutionContext *execution_context) {
  m_file_list.Clear();
}
