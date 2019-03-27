//===-- OptionGroupUUID.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionGroupUUID.h"

#include "lldb/Host/OptionParser.h"

using namespace lldb;
using namespace lldb_private;

OptionGroupUUID::OptionGroupUUID() : m_uuid() {}

OptionGroupUUID::~OptionGroupUUID() {}

static constexpr OptionDefinition g_option_table[] = {
    {LLDB_OPT_SET_1, false, "uuid", 'u', OptionParser::eRequiredArgument,
     nullptr, {}, 0, eArgTypeNone, "A module UUID value."},
};

llvm::ArrayRef<OptionDefinition> OptionGroupUUID::GetDefinitions() {
  return llvm::makeArrayRef(g_option_table);
}

Status OptionGroupUUID::SetOptionValue(uint32_t option_idx,
                                       llvm::StringRef option_arg,
                                       ExecutionContext *execution_context) {
  Status error;
  const int short_option = g_option_table[option_idx].short_option;

  switch (short_option) {
  case 'u':
    error = m_uuid.SetValueFromString(option_arg);
    if (error.Success())
      m_uuid.SetOptionWasSet();
    break;

  default:
    error.SetErrorStringWithFormat("unrecognized option '%c'", short_option);
    break;
  }

  return error;
}

void OptionGroupUUID::OptionParsingStarting(
    ExecutionContext *execution_context) {
  m_uuid.Clear();
}
