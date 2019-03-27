//===-- OptionValueDictionary.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValueDictionary.h"

#include "llvm/ADT/StringRef.h"
#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/Interpreter/OptionValueString.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/State.h"

using namespace lldb;
using namespace lldb_private;

void OptionValueDictionary::DumpValue(const ExecutionContext *exe_ctx,
                                      Stream &strm, uint32_t dump_mask) {
  const Type dict_type = ConvertTypeMaskToType(m_type_mask);
  if (dump_mask & eDumpOptionType) {
    if (m_type_mask != eTypeInvalid)
      strm.Printf("(%s of %ss)", GetTypeAsCString(),
                  GetBuiltinTypeAsCString(dict_type));
    else
      strm.Printf("(%s)", GetTypeAsCString());
  }
  if (dump_mask & eDumpOptionValue) {
    const bool one_line = dump_mask & eDumpOptionCommand;
    if (dump_mask & eDumpOptionType)
      strm.PutCString(" =");

    collection::iterator pos, end = m_values.end();

    if (!one_line)
      strm.IndentMore();

    for (pos = m_values.begin(); pos != end; ++pos) {
      OptionValue *option_value = pos->second.get();

      if (one_line)
        strm << ' ';
      else
        strm.EOL();

      strm.Indent(pos->first.GetCString());

      const uint32_t extra_dump_options = m_raw_value_dump ? eDumpOptionRaw : 0;
      switch (dict_type) {
      default:
      case eTypeArray:
      case eTypeDictionary:
      case eTypeProperties:
      case eTypeFileSpecList:
      case eTypePathMap:
        strm.PutChar(' ');
        option_value->DumpValue(exe_ctx, strm, dump_mask | extra_dump_options);
        break;

      case eTypeBoolean:
      case eTypeChar:
      case eTypeEnum:
      case eTypeFileSpec:
      case eTypeFormat:
      case eTypeSInt64:
      case eTypeString:
      case eTypeUInt64:
      case eTypeUUID:
        // No need to show the type for dictionaries of simple items
        strm.PutCString("=");
        option_value->DumpValue(exe_ctx, strm,
                                (dump_mask & (~eDumpOptionType)) |
                                    extra_dump_options);
        break;
      }
    }
    if (!one_line)
      strm.IndentLess();
  }
}

size_t OptionValueDictionary::GetArgs(Args &args) const {
  args.Clear();
  collection::const_iterator pos, end = m_values.end();
  for (pos = m_values.begin(); pos != end; ++pos) {
    StreamString strm;
    strm.Printf("%s=", pos->first.GetCString());
    pos->second->DumpValue(nullptr, strm, eDumpOptionValue | eDumpOptionRaw);
    args.AppendArgument(strm.GetString());
  }
  return args.GetArgumentCount();
}

Status OptionValueDictionary::SetArgs(const Args &args,
                                      VarSetOperationType op) {
  Status error;
  const size_t argc = args.GetArgumentCount();
  switch (op) {
  case eVarSetOperationClear:
    Clear();
    break;

  case eVarSetOperationAppend:
  case eVarSetOperationReplace:
  case eVarSetOperationAssign:
    if (argc == 0) {
      error.SetErrorString(
          "assign operation takes one or more key=value arguments");
      return error;
    }
    for (const auto &entry : args) {
      if (entry.ref.empty()) {
        error.SetErrorString("empty argument");
        return error;
      }
      if (!entry.ref.contains('=')) {
        error.SetErrorString(
            "assign operation takes one or more key=value arguments");
        return error;
      }

      llvm::StringRef key, value;
      std::tie(key, value) = entry.ref.split('=');
      bool key_valid = false;
      if (key.empty()) {
        error.SetErrorString("empty dictionary key");
        return error;
      }

      if (key.front() == '[') {
        // Key name starts with '[', so the key value must be in single or
        // double quotes like: ['<key>'] ["<key>"]
        if ((key.size() > 2) && (key.back() == ']')) {
          // Strip leading '[' and trailing ']'
          key = key.substr(1, key.size() - 2);
          const char quote_char = key.front();
          if ((quote_char == '\'') || (quote_char == '"')) {
            if ((key.size() > 2) && (key.back() == quote_char)) {
              // Strip the quotes
              key = key.substr(1, key.size() - 2);
              key_valid = true;
            }
          } else {
            // square brackets, no quotes
            key_valid = true;
          }
        }
      } else {
        // No square brackets or quotes
        key_valid = true;
      }
      if (!key_valid) {
        error.SetErrorStringWithFormat(
            "invalid key \"%s\", the key must be a bare string or "
            "surrounded by brackets with optional quotes: [<key>] or "
            "['<key>'] or [\"<key>\"]",
            key.str().c_str());
        return error;
      }

      lldb::OptionValueSP value_sp(CreateValueFromCStringForTypeMask(
          value.str().c_str(), m_type_mask, error));
      if (value_sp) {
        if (error.Fail())
          return error;
        m_value_was_set = true;
        SetValueForKey(ConstString(key), value_sp, true);
      } else {
        error.SetErrorString("dictionaries that can contain multiple types "
                             "must subclass OptionValueArray");
      }
    }
    break;

  case eVarSetOperationRemove:
    if (argc > 0) {
      for (size_t i = 0; i < argc; ++i) {
        ConstString key(args.GetArgumentAtIndex(i));
        if (!DeleteValueForKey(key)) {
          error.SetErrorStringWithFormat(
              "no value found named '%s', aborting remove operation",
              key.GetCString());
          break;
        }
      }
    } else {
      error.SetErrorString("remove operation takes one or more key arguments");
    }
    break;

  case eVarSetOperationInsertBefore:
  case eVarSetOperationInsertAfter:
  case eVarSetOperationInvalid:
    error = OptionValue::SetValueFromString(llvm::StringRef(), op);
    break;
  }
  return error;
}

Status OptionValueDictionary::SetValueFromString(llvm::StringRef value,
                                                 VarSetOperationType op) {
  Args args(value.str());
  Status error = SetArgs(args, op);
  if (error.Success())
    NotifyValueChanged();
  return error;
}

lldb::OptionValueSP
OptionValueDictionary::GetSubValue(const ExecutionContext *exe_ctx,
                                   llvm::StringRef name, bool will_modify,
                                   Status &error) const {
  lldb::OptionValueSP value_sp;
  if (name.empty())
    return nullptr;

  llvm::StringRef left, temp;
  std::tie(left, temp) = name.split('[');
  if (left.size() == name.size()) {
    error.SetErrorStringWithFormat("invalid value path '%s', %s values only "
      "support '[<key>]' subvalues where <key> "
      "a string value optionally delimited by "
      "single or double quotes",
      name.str().c_str(), GetTypeAsCString());
    return nullptr;
  }
  assert(!temp.empty());

  llvm::StringRef key, quote_char;

  if (temp[0] == '\"' || temp[0] == '\'') {
    quote_char = temp.take_front();
    temp = temp.drop_front();
  }

  llvm::StringRef sub_name;
  std::tie(key, sub_name) = temp.split(']');

  if (!key.consume_back(quote_char) || key.empty()) {
    error.SetErrorStringWithFormat("invalid value path '%s', "
      "key names must be formatted as ['<key>'] where <key> "
      "is a string that doesn't contain quotes and the quote"
      " char is optional", name.str().c_str());
    return nullptr;
  }

  value_sp = GetValueForKey(ConstString(key));
  if (!value_sp) {
    error.SetErrorStringWithFormat(
      "dictionary does not contain a value for the key name '%s'",
      key.str().c_str());
    return nullptr;
  }

  if (sub_name.empty())
    return value_sp;
  return value_sp->GetSubValue(exe_ctx, sub_name, will_modify, error);
}

Status OptionValueDictionary::SetSubValue(const ExecutionContext *exe_ctx,
                                          VarSetOperationType op,
                                          llvm::StringRef name,
                                          llvm::StringRef value) {
  Status error;
  const bool will_modify = true;
  lldb::OptionValueSP value_sp(GetSubValue(exe_ctx, name, will_modify, error));
  if (value_sp)
    error = value_sp->SetValueFromString(value, op);
  else {
    if (error.AsCString() == nullptr)
      error.SetErrorStringWithFormat("invalid value path '%s'", name.str().c_str());
  }
  return error;
}

lldb::OptionValueSP
OptionValueDictionary::GetValueForKey(const ConstString &key) const {
  lldb::OptionValueSP value_sp;
  collection::const_iterator pos = m_values.find(key);
  if (pos != m_values.end())
    value_sp = pos->second;
  return value_sp;
}

bool OptionValueDictionary::SetValueForKey(const ConstString &key,
                                           const lldb::OptionValueSP &value_sp,
                                           bool can_replace) {
  // Make sure the value_sp object is allowed to contain values of the type
  // passed in...
  if (value_sp && (m_type_mask & value_sp->GetTypeAsMask())) {
    if (!can_replace) {
      collection::const_iterator pos = m_values.find(key);
      if (pos != m_values.end())
        return false;
    }
    m_values[key] = value_sp;
    return true;
  }
  return false;
}

bool OptionValueDictionary::DeleteValueForKey(const ConstString &key) {
  collection::iterator pos = m_values.find(key);
  if (pos != m_values.end()) {
    m_values.erase(pos);
    return true;
  }
  return false;
}

lldb::OptionValueSP OptionValueDictionary::DeepCopy() const {
  OptionValueDictionary *copied_dict =
      new OptionValueDictionary(m_type_mask, m_raw_value_dump);
  lldb::OptionValueSP copied_value_sp(copied_dict);
  collection::const_iterator pos, end = m_values.end();
  for (pos = m_values.begin(); pos != end; ++pos) {
    StreamString strm;
    strm.Printf("%s=", pos->first.GetCString());
    copied_dict->SetValueForKey(pos->first, pos->second->DeepCopy(), true);
  }
  return copied_value_sp;
}
