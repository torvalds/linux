//===-- OptionGroupArchitecture.cpp -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionGroupArchitecture.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Target/Platform.h"

using namespace lldb;
using namespace lldb_private;

OptionGroupArchitecture::OptionGroupArchitecture() : m_arch_str() {}

OptionGroupArchitecture::~OptionGroupArchitecture() {}

static constexpr OptionDefinition g_option_table[] = {
    {LLDB_OPT_SET_1, false, "arch", 'a', OptionParser::eRequiredArgument,
     nullptr, {}, 0, eArgTypeArchitecture,
     "Specify the architecture for the target."},
};

llvm::ArrayRef<OptionDefinition> OptionGroupArchitecture::GetDefinitions() {
  return llvm::makeArrayRef(g_option_table);
}

bool OptionGroupArchitecture::GetArchitecture(Platform *platform,
                                              ArchSpec &arch) {
  arch = Platform::GetAugmentedArchSpec(platform, m_arch_str);
  return arch.IsValid();
}

Status
OptionGroupArchitecture::SetOptionValue(uint32_t option_idx,
                                        llvm::StringRef option_arg,
                                        ExecutionContext *execution_context) {
  Status error;
  const int short_option = g_option_table[option_idx].short_option;

  switch (short_option) {
  case 'a':
    m_arch_str.assign(option_arg);
    break;

  default:
    error.SetErrorStringWithFormat("unrecognized option '%c'", short_option);
    break;
  }

  return error;
}

void OptionGroupArchitecture::OptionParsingStarting(
    ExecutionContext *execution_context) {
  m_arch_str.clear();
}
