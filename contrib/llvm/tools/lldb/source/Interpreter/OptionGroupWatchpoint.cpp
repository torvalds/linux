//===-- OptionGroupWatchpoint.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionGroupWatchpoint.h"

#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/lldb-enumerations.h"

using namespace lldb;
using namespace lldb_private;

static constexpr OptionEnumValueElement g_watch_type[] = {
    {OptionGroupWatchpoint::eWatchRead, "read", "Watch for read"},
    {OptionGroupWatchpoint::eWatchWrite, "write", "Watch for write"},
    {OptionGroupWatchpoint::eWatchReadWrite, "read_write",
     "Watch for read/write"} };

static constexpr OptionEnumValueElement g_watch_size[] = {
    {1, "1", "Watch for byte size of 1"},
    {2, "2", "Watch for byte size of 2"},
    {4, "4", "Watch for byte size of 4"},
    {8, "8", "Watch for byte size of 8"} };

static constexpr OptionDefinition g_option_table[] = {
    {LLDB_OPT_SET_1, false, "watch", 'w', OptionParser::eRequiredArgument,
     nullptr, OptionEnumValues(g_watch_type), 0, eArgTypeWatchType,
     "Specify the type of watching to perform."},
    {LLDB_OPT_SET_1, false, "size", 's', OptionParser::eRequiredArgument,
     nullptr, OptionEnumValues(g_watch_size), 0, eArgTypeByteSize,
     "Number of bytes to use to watch a region."}};

bool OptionGroupWatchpoint::IsWatchSizeSupported(uint32_t watch_size) {
  for (const auto& size : g_watch_size) {
    if (0  == size.value)
      break;
    if (watch_size == size.value)
      return true;
  }
  return false;
}

OptionGroupWatchpoint::OptionGroupWatchpoint() : OptionGroup() {}

OptionGroupWatchpoint::~OptionGroupWatchpoint() {}

Status
OptionGroupWatchpoint::SetOptionValue(uint32_t option_idx,
                                      llvm::StringRef option_arg,
                                      ExecutionContext *execution_context) {
  Status error;
  const int short_option = g_option_table[option_idx].short_option;
  switch (short_option) {
  case 'w': {
    WatchType tmp_watch_type;
    tmp_watch_type = (WatchType)OptionArgParser::ToOptionEnum(
        option_arg, g_option_table[option_idx].enum_values, 0, error);
    if (error.Success()) {
      watch_type = tmp_watch_type;
      watch_type_specified = true;
    }
    break;
  }
  case 's':
    watch_size = (uint32_t)OptionArgParser::ToOptionEnum(
        option_arg, g_option_table[option_idx].enum_values, 0, error);
    break;

  default:
    error.SetErrorStringWithFormat("unrecognized short option '%c'",
                                   short_option);
    break;
  }

  return error;
}

void OptionGroupWatchpoint::OptionParsingStarting(
    ExecutionContext *execution_context) {
  watch_type_specified = false;
  watch_type = eWatchInvalid;
  watch_size = 0;
}

llvm::ArrayRef<OptionDefinition> OptionGroupWatchpoint::GetDefinitions() {
  return llvm::makeArrayRef(g_option_table);
}
