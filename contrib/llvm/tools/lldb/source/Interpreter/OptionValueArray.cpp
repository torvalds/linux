//===-- OptionValueArray.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValueArray.h"

#include "lldb/Host/StringConvert.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

void OptionValueArray::DumpValue(const ExecutionContext *exe_ctx, Stream &strm,
                                 uint32_t dump_mask) {
  const Type array_element_type = ConvertTypeMaskToType(m_type_mask);
  if (dump_mask & eDumpOptionType) {
    if ((GetType() == eTypeArray) && (m_type_mask != eTypeInvalid))
      strm.Printf("(%s of %ss)", GetTypeAsCString(),
                  GetBuiltinTypeAsCString(array_element_type));
    else
      strm.Printf("(%s)", GetTypeAsCString());
  }
  if (dump_mask & eDumpOptionValue) {
    const bool one_line = dump_mask & eDumpOptionCommand;
    const uint32_t size = m_values.size();
    if (dump_mask & eDumpOptionType)
      strm.Printf(" =%s", (m_values.size() > 0 && !one_line) ? "\n" : "");
    if (!one_line)
      strm.IndentMore();
    for (uint32_t i = 0; i < size; ++i) {
      if (!one_line) {
        strm.Indent();
        strm.Printf("[%u]: ", i);
      }
      const uint32_t extra_dump_options = m_raw_value_dump ? eDumpOptionRaw : 0;
      switch (array_element_type) {
      default:
      case eTypeArray:
      case eTypeDictionary:
      case eTypeProperties:
      case eTypeFileSpecList:
      case eTypePathMap:
        m_values[i]->DumpValue(exe_ctx, strm, dump_mask | extra_dump_options);
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
        m_values[i]->DumpValue(exe_ctx, strm, (dump_mask & (~eDumpOptionType)) |
                                                  extra_dump_options);
        break;
      }

      if (!one_line) {
        if (i < (size - 1))
          strm.EOL();
      } else {
        strm << ' ';
      }
    }
    if (!one_line)
      strm.IndentLess();
  }
}

Status OptionValueArray::SetValueFromString(llvm::StringRef value,
                                            VarSetOperationType op) {
  Args args(value.str());
  Status error = SetArgs(args, op);
  if (error.Success())
    NotifyValueChanged();
  return error;
}

lldb::OptionValueSP
OptionValueArray::GetSubValue(const ExecutionContext *exe_ctx,
                              llvm::StringRef name, bool will_modify,
                              Status &error) const {
  if (name.empty() || name.front() != '[') {
    error.SetErrorStringWithFormat(
      "invalid value path '%s', %s values only support '[<index>]' subvalues "
      "where <index> is a positive or negative array index",
      name.str().c_str(), GetTypeAsCString());
    return nullptr;
  }

  name = name.drop_front();
  llvm::StringRef index, sub_value;
  std::tie(index, sub_value) = name.split(']');
  if (index.size() == name.size()) {
    // Couldn't find a closing bracket
    return nullptr;
  }

  const size_t array_count = m_values.size();
  int32_t idx = 0;
  if (index.getAsInteger(0, idx))
    return nullptr;

  uint32_t new_idx = UINT32_MAX;
  if (idx < 0) {
    // Access from the end of the array if the index is negative
    new_idx = array_count - idx;
  } else {
    // Just a standard index
    new_idx = idx;
  }

  if (new_idx < array_count) {
    if (m_values[new_idx]) {
      if (!sub_value.empty())
        return m_values[new_idx]->GetSubValue(exe_ctx, sub_value,
                                              will_modify, error);
      else
        return m_values[new_idx];
    }
  } else {
    if (array_count == 0)
      error.SetErrorStringWithFormat(
          "index %i is not valid for an empty array", idx);
    else if (idx > 0)
      error.SetErrorStringWithFormat(
          "index %i out of range, valid values are 0 through %" PRIu64,
          idx, (uint64_t)(array_count - 1));
    else
      error.SetErrorStringWithFormat("negative index %i out of range, "
                                      "valid values are -1 through "
                                      "-%" PRIu64,
                                      idx, (uint64_t)array_count);
  }
  return OptionValueSP();
}

size_t OptionValueArray::GetArgs(Args &args) const {
  args.Clear();
  const uint32_t size = m_values.size();
  for (uint32_t i = 0; i < size; ++i) {
    llvm::StringRef string_value = m_values[i]->GetStringValue();
    if (!string_value.empty())
      args.AppendArgument(string_value);
  }

  return args.GetArgumentCount();
}

Status OptionValueArray::SetArgs(const Args &args, VarSetOperationType op) {
  Status error;
  const size_t argc = args.GetArgumentCount();
  switch (op) {
  case eVarSetOperationInvalid:
    error.SetErrorString("unsupported operation");
    break;

  case eVarSetOperationInsertBefore:
  case eVarSetOperationInsertAfter:
    if (argc > 1) {
      uint32_t idx =
          StringConvert::ToUInt32(args.GetArgumentAtIndex(0), UINT32_MAX);
      const uint32_t count = GetSize();
      if (idx > count) {
        error.SetErrorStringWithFormat(
            "invalid insert array index %u, index must be 0 through %u", idx,
            count);
      } else {
        if (op == eVarSetOperationInsertAfter)
          ++idx;
        for (size_t i = 1; i < argc; ++i, ++idx) {
          lldb::OptionValueSP value_sp(CreateValueFromCStringForTypeMask(
              args.GetArgumentAtIndex(i), m_type_mask, error));
          if (value_sp) {
            if (error.Fail())
              return error;
            if (idx >= m_values.size())
              m_values.push_back(value_sp);
            else
              m_values.insert(m_values.begin() + idx, value_sp);
          } else {
            error.SetErrorString(
                "array of complex types must subclass OptionValueArray");
            return error;
          }
        }
      }
    } else {
      error.SetErrorString("insert operation takes an array index followed by "
                           "one or more values");
    }
    break;

  case eVarSetOperationRemove:
    if (argc > 0) {
      const uint32_t size = m_values.size();
      std::vector<int> remove_indexes;
      bool all_indexes_valid = true;
      size_t i;
      for (i = 0; i < argc; ++i) {
        const size_t idx =
            StringConvert::ToSInt32(args.GetArgumentAtIndex(i), INT32_MAX);
        if (idx >= size) {
          all_indexes_valid = false;
          break;
        } else
          remove_indexes.push_back(idx);
      }

      if (all_indexes_valid) {
        size_t num_remove_indexes = remove_indexes.size();
        if (num_remove_indexes) {
          // Sort and then erase in reverse so indexes are always valid
          if (num_remove_indexes > 1) {
            llvm::sort(remove_indexes.begin(), remove_indexes.end());
            for (std::vector<int>::const_reverse_iterator
                     pos = remove_indexes.rbegin(),
                     end = remove_indexes.rend();
                 pos != end; ++pos) {
              m_values.erase(m_values.begin() + *pos);
            }
          } else {
            // Only one index
            m_values.erase(m_values.begin() + remove_indexes.front());
          }
        }
      } else {
        error.SetErrorStringWithFormat(
            "invalid array index '%s', aborting remove operation",
            args.GetArgumentAtIndex(i));
      }
    } else {
      error.SetErrorString("remove operation takes one or more array indices");
    }
    break;

  case eVarSetOperationClear:
    Clear();
    break;

  case eVarSetOperationReplace:
    if (argc > 1) {
      uint32_t idx =
          StringConvert::ToUInt32(args.GetArgumentAtIndex(0), UINT32_MAX);
      const uint32_t count = GetSize();
      if (idx > count) {
        error.SetErrorStringWithFormat(
            "invalid replace array index %u, index must be 0 through %u", idx,
            count);
      } else {
        for (size_t i = 1; i < argc; ++i, ++idx) {
          lldb::OptionValueSP value_sp(CreateValueFromCStringForTypeMask(
              args.GetArgumentAtIndex(i), m_type_mask, error));
          if (value_sp) {
            if (error.Fail())
              return error;
            if (idx < count)
              m_values[idx] = value_sp;
            else
              m_values.push_back(value_sp);
          } else {
            error.SetErrorString(
                "array of complex types must subclass OptionValueArray");
            return error;
          }
        }
      }
    } else {
      error.SetErrorString("replace operation takes an array index followed by "
                           "one or more values");
    }
    break;

  case eVarSetOperationAssign:
    m_values.clear();
    // Fall through to append case
    LLVM_FALLTHROUGH;
  case eVarSetOperationAppend:
    for (size_t i = 0; i < argc; ++i) {
      lldb::OptionValueSP value_sp(CreateValueFromCStringForTypeMask(
          args.GetArgumentAtIndex(i), m_type_mask, error));
      if (value_sp) {
        if (error.Fail())
          return error;
        m_value_was_set = true;
        AppendValue(value_sp);
      } else {
        error.SetErrorString(
            "array of complex types must subclass OptionValueArray");
      }
    }
    break;
  }
  return error;
}

lldb::OptionValueSP OptionValueArray::DeepCopy() const {
  OptionValueArray *copied_array =
      new OptionValueArray(m_type_mask, m_raw_value_dump);
  lldb::OptionValueSP copied_value_sp(copied_array);
  *static_cast<OptionValue *>(copied_array) = *this;
  copied_array->m_callback = m_callback;
  const uint32_t size = m_values.size();
  for (uint32_t i = 0; i < size; ++i) {
    copied_array->AppendValue(m_values[i]->DeepCopy());
  }
  return copied_value_sp;
}
