//===-- OptionArgParser.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_OPTIONARGPARSER_H
#define LLDB_INTERPRETER_OPTIONARGPARSER_H

#include "lldb/lldb-private-types.h"

namespace lldb_private {

struct OptionArgParser {
  static lldb::addr_t ToAddress(const ExecutionContext *exe_ctx,
                                llvm::StringRef s, lldb::addr_t fail_value,
                                Status *error);

  static bool ToBoolean(llvm::StringRef s, bool fail_value, bool *success_ptr);

  static char ToChar(llvm::StringRef s, char fail_value, bool *success_ptr);

  static int64_t ToOptionEnum(llvm::StringRef s,
                              const OptionEnumValues &enum_values,
                              int32_t fail_value, Status &error);

  static lldb::ScriptLanguage ToScriptLanguage(llvm::StringRef s,
                                               lldb::ScriptLanguage fail_value,
                                               bool *success_ptr);

  // TODO: Use StringRef
  static Status ToFormat(const char *s, lldb::Format &format,
                         size_t *byte_size_ptr); // If non-NULL, then a
                                                 // byte size can precede
                                                 // the format character
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_OPTIONARGPARSER_H
