//===-- OptionArgParser.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb_private;
using namespace lldb;

bool OptionArgParser::ToBoolean(llvm::StringRef ref, bool fail_value,
                                bool *success_ptr) {
  if (success_ptr)
    *success_ptr = true;
  ref = ref.trim();
  if (ref.equals_lower("false") || ref.equals_lower("off") ||
      ref.equals_lower("no") || ref.equals_lower("0")) {
    return false;
  } else if (ref.equals_lower("true") || ref.equals_lower("on") ||
             ref.equals_lower("yes") || ref.equals_lower("1")) {
    return true;
  }
  if (success_ptr)
    *success_ptr = false;
  return fail_value;
}

char OptionArgParser::ToChar(llvm::StringRef s, char fail_value,
                             bool *success_ptr) {
  if (success_ptr)
    *success_ptr = false;
  if (s.size() != 1)
    return fail_value;

  if (success_ptr)
    *success_ptr = true;
  return s[0];
}

int64_t OptionArgParser::ToOptionEnum(llvm::StringRef s,
                                      const OptionEnumValues &enum_values,
                                      int32_t fail_value, Status &error) {
  error.Clear();
  if (enum_values.empty()) {
    error.SetErrorString("invalid enumeration argument");
    return fail_value;
  }

  if (s.empty()) {
    error.SetErrorString("empty enumeration string");
    return fail_value;
  }

  for (const auto &enum_value : enum_values) {
    llvm::StringRef this_enum(enum_value.string_value);
    if (this_enum.startswith(s))
      return enum_value.value;
  }

  StreamString strm;
  strm.PutCString("invalid enumeration value, valid values are: ");
  bool is_first = true;
  for (const auto &enum_value : enum_values) {
    strm.Printf("%s\"%s\"",
        is_first ? is_first = false,"" : ", ", enum_value.string_value);
  }
  error.SetErrorString(strm.GetString());
  return fail_value;
}

Status OptionArgParser::ToFormat(const char *s, lldb::Format &format,
                                 size_t *byte_size_ptr) {
  format = eFormatInvalid;
  Status error;

  if (s && s[0]) {
    if (byte_size_ptr) {
      if (isdigit(s[0])) {
        char *format_char = nullptr;
        unsigned long byte_size = ::strtoul(s, &format_char, 0);
        if (byte_size != ULONG_MAX)
          *byte_size_ptr = byte_size;
        s = format_char;
      } else
        *byte_size_ptr = 0;
    }

    const bool partial_match_ok = true;
    if (!FormatManager::GetFormatFromCString(s, partial_match_ok, format)) {
      StreamString error_strm;
      error_strm.Printf(
          "Invalid format character or name '%s'. Valid values are:\n", s);
      for (Format f = eFormatDefault; f < kNumFormats; f = Format(f + 1)) {
        char format_char = FormatManager::GetFormatAsFormatChar(f);
        if (format_char)
          error_strm.Printf("'%c' or ", format_char);

        error_strm.Printf("\"%s\"", FormatManager::GetFormatAsCString(f));
        error_strm.EOL();
      }

      if (byte_size_ptr)
        error_strm.PutCString(
            "An optional byte size can precede the format character.\n");
      error.SetErrorString(error_strm.GetString());
    }

    if (error.Fail())
      return error;
  } else {
    error.SetErrorStringWithFormat("%s option string", s ? "empty" : "invalid");
  }
  return error;
}

lldb::ScriptLanguage OptionArgParser::ToScriptLanguage(
    llvm::StringRef s, lldb::ScriptLanguage fail_value, bool *success_ptr) {
  if (success_ptr)
    *success_ptr = true;

  if (s.equals_lower("python"))
    return eScriptLanguagePython;
  if (s.equals_lower("default"))
    return eScriptLanguageDefault;
  if (s.equals_lower("none"))
    return eScriptLanguageNone;

  if (success_ptr)
    *success_ptr = false;
  return fail_value;
}

lldb::addr_t OptionArgParser::ToAddress(const ExecutionContext *exe_ctx,
                                        llvm::StringRef s,
                                        lldb::addr_t fail_value,
                                        Status *error_ptr) {
  bool error_set = false;
  if (s.empty()) {
    if (error_ptr)
      error_ptr->SetErrorStringWithFormat("invalid address expression \"%s\"",
                                          s.str().c_str());
    return fail_value;
  }

  llvm::StringRef sref = s;

  lldb::addr_t addr = LLDB_INVALID_ADDRESS;
  if (!s.getAsInteger(0, addr)) {
    if (error_ptr)
      error_ptr->Clear();
    return addr;
  }

  // Try base 16 with no prefix...
  if (!s.getAsInteger(16, addr)) {
    if (error_ptr)
      error_ptr->Clear();
    return addr;
  }

  Target *target = nullptr;
  if (!exe_ctx || !(target = exe_ctx->GetTargetPtr())) {
    if (error_ptr)
      error_ptr->SetErrorStringWithFormat("invalid address expression \"%s\"",
                                          s.str().c_str());
    return fail_value;
  }

  lldb::ValueObjectSP valobj_sp;
  EvaluateExpressionOptions options;
  options.SetCoerceToId(false);
  options.SetUnwindOnError(true);
  options.SetKeepInMemory(false);
  options.SetTryAllThreads(true);

  ExpressionResults expr_result =
      target->EvaluateExpression(s, exe_ctx->GetFramePtr(), valobj_sp, options);

  bool success = false;
  if (expr_result == eExpressionCompleted) {
    if (valobj_sp)
      valobj_sp = valobj_sp->GetQualifiedRepresentationIfAvailable(
          valobj_sp->GetDynamicValueType(), true);
    // Get the address to watch.
    if (valobj_sp)
      addr = valobj_sp->GetValueAsUnsigned(fail_value, &success);
    if (success) {
      if (error_ptr)
        error_ptr->Clear();
      return addr;
    } else {
      if (error_ptr) {
        error_set = true;
        error_ptr->SetErrorStringWithFormat(
            "address expression \"%s\" resulted in a value whose type "
            "can't be converted to an address: %s",
            s.str().c_str(), valobj_sp->GetTypeName().GetCString());
      }
    }

  } else {
    // Since the compiler can't handle things like "main + 12" we should try to
    // do this for now. The compiler doesn't like adding offsets to function
    // pointer types.
    static RegularExpression g_symbol_plus_offset_regex(
        "^(.*)([-\\+])[[:space:]]*(0x[0-9A-Fa-f]+|[0-9]+)[[:space:]]*$");
    RegularExpression::Match regex_match(3);
    if (g_symbol_plus_offset_regex.Execute(sref, &regex_match)) {
      uint64_t offset = 0;
      bool add = true;
      std::string name;
      std::string str;
      if (regex_match.GetMatchAtIndex(s, 1, name)) {
        if (regex_match.GetMatchAtIndex(s, 2, str)) {
          add = str[0] == '+';

          if (regex_match.GetMatchAtIndex(s, 3, str)) {
            if (!llvm::StringRef(str).getAsInteger(0, offset)) {
              Status error;
              addr = ToAddress(exe_ctx, name.c_str(), LLDB_INVALID_ADDRESS,
                               &error);
              if (addr != LLDB_INVALID_ADDRESS) {
                if (add)
                  return addr + offset;
                else
                  return addr - offset;
              }
            }
          }
        }
      }
    }

    if (error_ptr) {
      error_set = true;
      error_ptr->SetErrorStringWithFormat(
          "address expression \"%s\" evaluation failed", s.str().c_str());
    }
  }

  if (error_ptr) {
    if (!error_set)
      error_ptr->SetErrorStringWithFormat("invalid address expression \"%s\"",
                                          s.str().c_str());
  }
  return fail_value;
}
