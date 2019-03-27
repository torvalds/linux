//===-- OptionGroupOutputFile.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionGroupOutputFile.h"

#include "lldb/Host/OptionParser.h"

using namespace lldb;
using namespace lldb_private;

OptionGroupOutputFile::OptionGroupOutputFile()
    : m_file(), m_append(false, false) {}

OptionGroupOutputFile::~OptionGroupOutputFile() {}

static const uint32_t SHORT_OPTION_APND = 0x61706e64; // 'apnd'

static constexpr OptionDefinition g_option_table[] = {
    {LLDB_OPT_SET_1, false, "outfile", 'o', OptionParser::eRequiredArgument,
     nullptr, {}, 0, eArgTypeFilename,
     "Specify a path for capturing command output."},
    {LLDB_OPT_SET_1, false, "append-outfile", SHORT_OPTION_APND,
     OptionParser::eNoArgument, nullptr, {}, 0, eArgTypeNone,
     "Append to the file specified with '--outfile <path>'."},
};

llvm::ArrayRef<OptionDefinition> OptionGroupOutputFile::GetDefinitions() {
  return llvm::makeArrayRef(g_option_table);
}

Status
OptionGroupOutputFile::SetOptionValue(uint32_t option_idx,
                                      llvm::StringRef option_arg,
                                      ExecutionContext *execution_context) {
  Status error;
  const int short_option = g_option_table[option_idx].short_option;

  switch (short_option) {
  case 'o':
    error = m_file.SetValueFromString(option_arg);
    break;

  case SHORT_OPTION_APND:
    m_append.SetCurrentValue(true);
    break;

  default:
    error.SetErrorStringWithFormat("unrecognized option '%c'", short_option);
    break;
  }

  return error;
}

void OptionGroupOutputFile::OptionParsingStarting(
    ExecutionContext *execution_context) {
  m_file.Clear();
  m_append.Clear();
}
