//===-- OptionValueArgs.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValueArgs.h"

#include "lldb/Utility/Args.h"

using namespace lldb;
using namespace lldb_private;

size_t OptionValueArgs::GetArgs(Args &args) {
  args.Clear();
  for (auto value : m_values) {
    llvm::StringRef string_value = value->GetStringValue();
    if (!string_value.empty())
      args.AppendArgument(string_value);
  }

  return args.GetArgumentCount();
}
