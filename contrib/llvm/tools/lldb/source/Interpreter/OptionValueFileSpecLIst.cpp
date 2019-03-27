//===-- OptionValueFileSpecLIst.cpp -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValueFileSpecList.h"

#include "lldb/Host/StringConvert.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

void OptionValueFileSpecList::DumpValue(const ExecutionContext *exe_ctx,
                                        Stream &strm, uint32_t dump_mask) {
  if (dump_mask & eDumpOptionType)
    strm.Printf("(%s)", GetTypeAsCString());
  if (dump_mask & eDumpOptionValue) {
    const bool one_line = dump_mask & eDumpOptionCommand;
    const uint32_t size = m_current_value.GetSize();
    if (dump_mask & eDumpOptionType)
      strm.Printf(" =%s",
                  (m_current_value.GetSize() > 0 && !one_line) ? "\n" : "");
    if (!one_line)
      strm.IndentMore();
    for (uint32_t i = 0; i < size; ++i) {
      if (!one_line) {
        strm.Indent();
        strm.Printf("[%u]: ", i);
      }
      m_current_value.GetFileSpecAtIndex(i).Dump(&strm);
      if (one_line)
        strm << ' ';
    }
    if (!one_line)
      strm.IndentLess();
  }
}

Status OptionValueFileSpecList::SetValueFromString(llvm::StringRef value,
                                                   VarSetOperationType op) {
  Status error;
  Args args(value.str());
  const size_t argc = args.GetArgumentCount();

  switch (op) {
  case eVarSetOperationClear:
    Clear();
    NotifyValueChanged();
    break;

  case eVarSetOperationReplace:
    if (argc > 1) {
      uint32_t idx =
          StringConvert::ToUInt32(args.GetArgumentAtIndex(0), UINT32_MAX);
      const uint32_t count = m_current_value.GetSize();
      if (idx > count) {
        error.SetErrorStringWithFormat(
            "invalid file list index %u, index must be 0 through %u", idx,
            count);
      } else {
        for (size_t i = 1; i < argc; ++i, ++idx) {
          FileSpec file(args.GetArgumentAtIndex(i));
          if (idx < count)
            m_current_value.Replace(idx, file);
          else
            m_current_value.Append(file);
        }
        NotifyValueChanged();
      }
    } else {
      error.SetErrorString("replace operation takes an array index followed by "
                           "one or more values");
    }
    break;

  case eVarSetOperationAssign:
    m_current_value.Clear();
    // Fall through to append case
    LLVM_FALLTHROUGH;
  case eVarSetOperationAppend:
    if (argc > 0) {
      m_value_was_set = true;
      for (size_t i = 0; i < argc; ++i) {
        FileSpec file(args.GetArgumentAtIndex(i));
        m_current_value.Append(file);
      }
      NotifyValueChanged();
    } else {
      error.SetErrorString(
          "assign operation takes at least one file path argument");
    }
    break;

  case eVarSetOperationInsertBefore:
  case eVarSetOperationInsertAfter:
    if (argc > 1) {
      uint32_t idx =
          StringConvert::ToUInt32(args.GetArgumentAtIndex(0), UINT32_MAX);
      const uint32_t count = m_current_value.GetSize();
      if (idx > count) {
        error.SetErrorStringWithFormat(
            "invalid insert file list index %u, index must be 0 through %u",
            idx, count);
      } else {
        if (op == eVarSetOperationInsertAfter)
          ++idx;
        for (size_t i = 1; i < argc; ++i, ++idx) {
          FileSpec file(args.GetArgumentAtIndex(i));
          m_current_value.Insert(idx, file);
        }
        NotifyValueChanged();
      }
    } else {
      error.SetErrorString("insert operation takes an array index followed by "
                           "one or more values");
    }
    break;

  case eVarSetOperationRemove:
    if (argc > 0) {
      std::vector<int> remove_indexes;
      bool all_indexes_valid = true;
      size_t i;
      for (i = 0; all_indexes_valid && i < argc; ++i) {
        const int idx =
            StringConvert::ToSInt32(args.GetArgumentAtIndex(i), INT32_MAX);
        if (idx == INT32_MAX)
          all_indexes_valid = false;
        else
          remove_indexes.push_back(idx);
      }

      if (all_indexes_valid) {
        size_t num_remove_indexes = remove_indexes.size();
        if (num_remove_indexes) {
          // Sort and then erase in reverse so indexes are always valid
          llvm::sort(remove_indexes.begin(), remove_indexes.end());
          for (size_t j = num_remove_indexes - 1; j < num_remove_indexes; ++j) {
            m_current_value.Remove(j);
          }
        }
        NotifyValueChanged();
      } else {
        error.SetErrorStringWithFormat(
            "invalid array index '%s', aborting remove operation",
            args.GetArgumentAtIndex(i));
      }
    } else {
      error.SetErrorString("remove operation takes one or more array index");
    }
    break;

  case eVarSetOperationInvalid:
    error = OptionValue::SetValueFromString(value, op);
    break;
  }
  return error;
}

lldb::OptionValueSP OptionValueFileSpecList::DeepCopy() const {
  return OptionValueSP(new OptionValueFileSpecList(*this));
}
